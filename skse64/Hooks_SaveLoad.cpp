#include "Hooks_SaveLoad.h"
#include "skse64_common/SafeWrite.h"
#include "skse64_common/Utilities.h"
#include "skse64_common/BranchTrampoline.h"
#include "Serialization.h"
#include "GlobalLocks.h"
#include "GameData.h"
#include "GameMenus.h"
#include "PapyrusVM.h"
#include "PluginManager.h"
#include "Hooks_UI.h"
#include "LoadPluginSnapshot.h"
#include "LoadRefusalNotice.h"
#ifdef ENSRICK_EXPERIMENTAL_SAVE_ADMISSION
#include "LoadAdmissionRuntime.h"
#endif
#include <atomic>
#include <new>

namespace {
	// Diagnostic experiment only. Not installed unless explicitly opted in.
	char g_rejectLoadBasename[260] = {};
	bool g_recoverRejectedMainLoad = false;
	std::atomic<bool> g_loadRequestHooksInstalled{false};
	thread_local bool g_rejectedRequestNeedsRecovery = false;
	thread_local LoadRefusal::Notice g_refusedLoadNotice;
	std::atomic<UInt64> g_loadRequestGeneration{0};
	thread_local UInt64 g_rejectedRequestGeneration = 0;

	bool InstallLoadRequestProbe(uintptr_t requestCall, uintptr_t failureCall,
		uintptr_t requestHook, uintptr_t failureHook, bool recover)
	{
		// Write5Call consumes 14 trampoline bytes and asserts on exhaustion.
		// Reserve capacity for the pair before making either diagnostic edit.
		if (g_branchTrampoline.Remain() < (recover ? 28u : 14u)) {
			_MESSAGE("LOAD_REQUEST_PROBE_NOT_INSTALLED trampoline_capacity=0");
			return false;
		}
		if (recover && !g_branchTrampoline.Write5Call(failureCall, failureHook)) {
			_MESSAGE("LOAD_REQUEST_PROBE_NOT_INSTALLED failure_hook_write_failed=1");
			return false;
		}
		if (!g_branchTrampoline.Write5Call(requestCall, requestHook)) {
			// A previously installed failure hook is inert: no request wrapper
			// ran to set this thread's recovery flag. Keep forwarding native failure.
			_MESSAGE("LOAD_REQUEST_PROBE_NOT_INSTALLED request_hook_write_failed=1 native_failure_forwarder_only=%u", unsigned(recover));
			return false;
		}
		return true;
	}

	void NotifyOriginalLoadFailure()
	{
		static RelocAddr<void(*)()> original(0x00627B20);
		original();
	}

	bool QueueRejectedMainLoadRecovery()
	{
		// Same factory, owned string field and UI queue as the native failure
		// handler. Queue AFTER its RefreshMenu, never raw-close the Main Menu.
		auto* menus = MenuManager::GetSingleton();
		auto* queue = UIManager::GetSingleton();
		BSFixedString mainMenu("Main Menu"), journalMenu("Journal Menu");
		const bool eligible = menus && queue && menus->IsMenuOpen(&mainMenu)
			&& !menus->IsMenuOpen(&journalMenu);
		bool queued = false;
		if (eligible) {
			BSFixedString type("BSUIMessageData"), event("CancelLoading");
			auto* data = static_cast<BSUIMessageData*>(CreateUIMessageData(&type));
			if (data) {
				CALL_MEMBER_FN(&data->unk18, Set_ref)(event);
				CALL_MEMBER_FN(queue, AddMessage)(&mainMenu, 0, data); // kUpdate; queue owns data
				queued = true;
			}
			event.Release();
			type.Release();
		}
		journalMenu.Release();
		mainMenu.Release();
		return queued;
	}

	class RejectedLoadUIRecovery : public UIDelegate_v1
	{
	public:
		explicit RejectedLoadUIRecovery(UInt64 generation, LoadRefusal::Notice notice) : generation_(generation), notice_(notice) {}
		void Run() override
		{
			const bool current = generation_ == g_loadRequestGeneration.load(std::memory_order_relaxed);
			const bool queued = current && QueueRejectedMainLoadRecovery();
			const bool noticeQueued = current && LoadRefusal::Queue(notice_);
			_MESSAGE("LOAD_REQUEST_RECOVERY_UI diagnostic_only=1 generation=%llu current=%u main_cancel_queued=%u",
				generation_, unsigned(current), unsigned(queued));
			_MESSAGE("LOAD_REQUEST_REFUSAL_UI generation=%llu current=%u reason=%u notice_queued=%u",generation_,unsigned(current),notice_.code,unsigned(noticeQueued));
		}
		void Dispose() override { delete this; }
	private:
		UInt64 generation_;
		LoadRefusal::Notice notice_; // owned code; no borrowed buffer/save path
	};

	bool ScheduleRejectedMainLoadRecovery(UInt64 generation, LoadRefusal::Notice notice)
	{
		// ProcessCommands drains native failure UI events before running this
		// delegate. Cancellation is then queued for the next native UI pass.
		auto* queue = UIManager::GetSingleton();
		if (!queue) return false;
		auto* task = new (std::nothrow) RejectedLoadUIRecovery(generation, notice);
		if (!task) return false;
		queue->QueueCommand(task);
		return true;
	}
	bool ReadLoadProbeName(UInt64** input, char (&name)[260], UInt64*& stream)
	{
		stream = nullptr;
		const char* source = nullptr;
		SIZE_T got = 0;
		if (!input || !ReadProcessMemory(GetCurrentProcess(), input, &stream, sizeof(stream), &got)
			|| got != sizeof(stream) || !stream)
			return false;
		if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const char*>(stream) + 0xBB0,
			&source, sizeof(source), &got) || got != sizeof(source) || !source)
			return false;
		for (size_t i = 0; i < sizeof(name); ++i) {
			if (!ReadProcessMemory(GetCurrentProcess(), source + i, &name[i], 1, &got) || got != 1)
				return false;
			if (!name[i])
				return i > 0;
		}
		name[sizeof(name) - 1] = 0;
		return false;
	}

	void ObserveLoadStreamSnapshot(UInt64** input)
	{
		// Opt-in, read-only experiment for pinned 1.7.104 Win32FileType.
		// This stream consumes an in-memory ESS image, not NiFile::file.
		// Never call an unknown virtual, change a cursor, or dump player data.
		char enabled[2] = {};
		if (GetEnvironmentVariableA("SKSE_AUTOMATION_LOAD_STREAM_PROBE", enabled, sizeof(enabled)) != 1 || enabled[0] != '1') return;
		// Query the loaded bridge; zero is deliberately NOT a valid identity.
		// Never load/initialize a plugin here or substitute a JSON-derived value.
		const auto currency = GetModuleHandleA("EnsrickCurrencyDenominations.dll");
		typedef UInt64 (*CurrencyIdentityGetter)();
		const auto identity = currency ? reinterpret_cast<CurrencyIdentityGetter>(
			GetProcAddress(currency, "EnsrickCurrency_GetAdmissionFingerprintV1")) : nullptr;
		const UInt64 fingerprint = identity ? identity() : 0;
		_MESSAGE("LOAD_CURRENCY_IDENTITY diagnostic_only=1 module=%u export=%u fingerprint=%016llX ready=%u",
			unsigned(currency != nullptr), unsigned(identity != nullptr), fingerprint, unsigned(fingerprint != 0));
		UInt64* stream = nullptr;
		UInt64 vtable = 0, memory = 0;
		UInt32 size = 0, position = 0;
		UInt8 decompressed = 255;
		char path[260] = {};
		auto read = [](const void* source, void* dest, SIZE_T length) {
			SIZE_T got = 0;
			return ReadProcessMemory(GetCurrentProcess(), source, dest, length, &got) && got == length;
		};
		if (!input || !read(input, &stream, sizeof(stream)) || !stream || !read(stream, &vtable, sizeof(vtable))
			|| vtable != RelocationManager::s_baseAddr + 0x01B521A0) {
			_MESSAGE("LOAD_STREAM_SNAPSHOT diagnostic_only=1 layout_match=0");
			return;
		}
		// Snapshot actual loaded arrays, never a disk plugins.txt proxy. The
		// observer is diagnostic-only; refusals here do not decide game loading.
		STATIC_ASSERT(offsetof(DataHandler, modList) == LoadPluginSnapshot::kModListOffset);
		STATIC_ASSERT(offsetof(ModInfo, fileFlags) == LoadPluginSnapshot::kFileFlagsOffset);
		STATIC_ASSERT(offsetof(ModInfo, modIndex) == LoadPluginSnapshot::kModIndexOffset);
		STATIC_ASSERT(offsetof(ModInfo, lightIndex) == LoadPluginSnapshot::kLightIndexOffset);
		try {
			DataHandler* handler = nullptr;
			if (!read(g_dataHandler.GetPtr(), &handler, sizeof(handler))) throw LoadPluginSnapshot::SnapshotError("unreadable singleton");
			auto reader = [&read](std::uintptr_t address, void* target, size_t length) { return read(reinterpret_cast<const void*>(address), target, length); };
			const auto table = LoadPluginSnapshot::Read(reader, reinterpret_cast<std::uintptr_t>(handler));
			_MESSAGE("LOAD_PLUGIN_SNAPSHOT diagnostic_only=1 ok=1 full=%u light=%u thread=%u", unsigned(table.full.size()), unsigned(table.light.size()), GetCurrentThreadId());
			for (unsigned i=0; i<table.full.size(); ++i) _MESSAGE("LOAD_PLUGIN_FULL index=%u name=%s", i, table.full[i].c_str());
			for (unsigned i=0; i<table.light.size(); ++i) _MESSAGE("LOAD_PLUGIN_LIGHT index=%u name=%s", i, table.light[i].c_str());
		} catch (const std::exception& error) {
			_MESSAGE("LOAD_PLUGIN_SNAPSHOT diagnostic_only=1 ok=0 reason=%s", error.what());
		} catch (...) {
			_MESSAGE("LOAD_PLUGIN_SNAPSHOT diagnostic_only=1 ok=0 reason=unknown_exception");
		}
		auto field = [stream](size_t offset) { return reinterpret_cast<const char*>(stream) + offset; };
		if (!read(field(0xBD0), &memory, sizeof(memory)) || !read(field(0x174), &size, sizeof(size))
			|| !read(field(0xBE0), &position, sizeof(position)) || !read(field(0xBCA), &decompressed, sizeof(decompressed))
			|| !read(field(0x64), path, sizeof(path)) || !memchr(path, 0, sizeof(path))
			|| !memory || !size || size > 0x04000000 || decompressed != 0) {
			_MESSAGE("LOAD_STREAM_SNAPSHOT diagnostic_only=1 layout_match=1 fields_valid=0 bytes=%u position=%u decompressed=%u", size, position, unsigned(decompressed));
			return;
		}
		HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			_MESSAGE("LOAD_STREAM_SNAPSHOT diagnostic_only=1 file_open=0 error=%u bytes=%u position=%u", GetLastError(), size, position);
			return;
		}
		LARGE_INTEGER fileSize = {};
		bool equal = GetFileSizeEx(file, &fileSize) && fileSize.QuadPart == size;
		UInt32 compared = 0;
		unsigned char disk[4096], engine[4096];
		while (equal && compared < size) {
			DWORD amount = (size - compared < sizeof(disk)) ? size - compared : sizeof(disk), got = 0;
			equal = ReadFile(file, disk, amount, &got, nullptr) && got == amount
				&& read(reinterpret_cast<const void*>(memory + compared), engine, amount)
				&& memcmp(disk, engine, amount) == 0;
			if (equal) compared += amount;
		}
		CloseHandle(file);
		_MESSAGE("LOAD_STREAM_SNAPSHOT diagnostic_only=1 layout_match=1 fields_valid=1 file_open=1 bytes=%u disk_bytes=%llu position=%u decompressed=%u compared=%u exact_equal=%u",
			size, static_cast<UInt64>(fileSize.QuadPart), position, unsigned(decompressed), compared, unsigned(equal));
	}
}

// Paired call site625FFE, not a generic failure-handler detour. Consume only
// this thread's deliberate rejection; ordinary engine failures remain native.
void LoadRequestFailureRecovery_Hook()
{
	const bool recover = g_rejectedRequestNeedsRecovery;
	const UInt64 generation = g_rejectedRequestGeneration;
	const auto notice = g_refusedLoadNotice;
	g_rejectedRequestNeedsRecovery = false;
	g_refusedLoadNotice = LoadRefusal::Notice();
	NotifyOriginalLoadFailure();
	if (recover) {
		const bool scheduled = ScheduleRejectedMainLoadRecovery(generation, notice);
		_MESSAGE("LOAD_REQUEST_RECOVERY diagnostic_only=1 native_failure_notified=1 deferred_ui_recovery=%u generation=%llu", unsigned(scheduled), generation);
	}
}

bool BGSSaveLoadManager::LoadRequestProbe_Hook(UInt64** stream, UInt32 arg1, UInt8 arg2, UInt8 arg3, UInt32 arg4)
{
	g_rejectedRequestNeedsRecovery = false;
	g_refusedLoadNotice = LoadRefusal::Notice();
	g_rejectedRequestGeneration = g_loadRequestGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
	char name[260] = {};
	UInt64* observedStream = nullptr;
	const bool readable = ReadLoadProbeName(stream, name, observedStream);
	ObserveLoadStreamSnapshot(stream);
	bool reject = g_rejectLoadBasename[0]
		&& (!readable || _stricmp(name, g_rejectLoadBasename) == 0);
#ifdef ENSRICK_EXPERIMENTAL_SAVE_ADMISSION
	LoadAdmissionRuntime::RequestToken admitted;
	if (!reject && LoadAdmissionRuntime::Enabled()) reject = !LoadAdmissionRuntime::Begin(stream, admitted, g_refusedLoadNotice);
	if (reject && LoadAdmissionRuntime::Enabled() && !g_refusedLoadNotice.code) g_refusedLoadNotice=LoadRefusal::Notice(LoadRefusal::DiagnosticVeto);
#endif
	_MESSAGE("LOAD_REQUEST_PROBE save=%s readable=%u arg1=%08X arg2=%02X arg3=%02X arg4=%08X reject=%u stream=%016llX identity=pointer_only",
		readable ? name : "<unreadable>", unsigned(readable), arg1, unsigned(arg2), unsigned(arg3), arg4, unsigned(reject), reinterpret_cast<UInt64>(observedStream));
	if (reject) {
		// Return to the existing caller at625FFA; it calls627B20 for false.
		// This diagnostic deliberately does not enter627DE0 or emit SKSE load messages.
		_MESSAGE("LOAD_REQUEST_REJECTED diagnostic_only=1 engine_target_entered=0");
		g_rejectedRequestNeedsRecovery = g_recoverRejectedMainLoad;
		return false;
	}
	const bool result = CALL_MEMBER_FN(this, LoadRequestProbe_Target)(stream, arg1, arg2, arg3, arg4);
#ifdef ENSRICK_EXPERIMENTAL_SAVE_ADMISSION
	// Compare the captured generation and pointer values only. Never inspect
	// the old stream after native execution; the native caller owns its lifetime.
	if (LoadAdmissionRuntime::Enabled()) {
		LoadAdmissionRuntime::RequestReturned(admitted, stream ? *stream : nullptr, result);
		// A native false result is NOT our early veto. The outer target can
		// transfer its stream to a later error callback before returning false.
		// Preserve native failure UI/ownership; do not queue CancelLoading on
		// top of that callback. Recovery eligibility is set only before entry.
	}
#endif
	_MESSAGE("LOAD_REQUEST_RESULT save=%s result=%u", readable ? name : "<unreadable>", unsigned(result));
	return result;
}

void BGSSaveLoadManager::SaveGame_Hook(UInt64 *unk0)
{
	const char *saveName = reinterpret_cast<const char *>(unk0[0xBB0 / 8]);

	// Game actually does this, we may as well do the same
	if (!saveName)
		saveName = "";

#ifdef DEBUG
	_MESSAGE("Executing BGSSaveLoadManager::SaveGame_Hook. saveName: %s", saveName);
#endif

	Serialization::SetSaveName(saveName);
	PluginManager::Dispatch_Message(0, SKSEMessagingInterface::kMessage_SaveGame, (void*)saveName, strlen(saveName), NULL);	
	CALL_MEMBER_FN(this, SaveGame_HookTarget)(unk0);
	Serialization::SetSaveName(NULL);

#ifdef DEBUG
	_MESSAGE("Executed BGSSaveLoadManager::SaveGame_Hook.");
#endif
}

bool BGSSaveLoadManager::LoadGame_Hook(UInt64 *unk0, UInt32 unk1, UInt32 unk2, void *unk3, UInt8 unk4)
{
	const char *saveName = reinterpret_cast<const char *>(unk0[0xBB0 / 8]); 

	// Game actually does this, we may as well do the same
	if (!saveName)
		saveName = ""; 

#ifdef DEBUG
	_MESSAGE("Executing BGSSaveLoadManager::LoadGame_Hook. saveName: %s", saveName);
#endif

	g_loadGameLock.Enter();

#ifdef ENSRICK_EXPERIMENTAL_SAVE_ADMISSION
	LoadAdmissionRuntime::InnerAdmission admittedInner;
	if (LoadAdmissionRuntime::Enabled()) admittedInner = LoadAdmissionRuntime::AcquireInner(unk0);
#ifdef ENSRICK_SAVE_ADMISSION_RELEASE
	const bool injectInnerFalse = false;
#else
	// Private one-shot fault injection, only in the experimental adapter.
	// Exercise our existing pre-target false path; NEVER falsify native success.
	// The recursive load lock serializes consumption, including nested calls.
	static bool innerTestConsumed = false;
	char innerTestName[260] = {};
	const auto innerTestLength = GetEnvironmentVariableA("SKSE_AUTOMATION_REJECT_INNER_ONCE", innerTestName, sizeof(innerTestName));
	bool validInnerTest = innerTestLength > 0 && innerTestLength < sizeof(innerTestName);
	for (unsigned i = 0; validInnerTest && i < innerTestLength; ++i) {
		const char c = innerTestName[i];
		validInnerTest = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
			|| c == '_' || c == '-' || c == '.';
	}
	const bool injectInnerFalse = LoadAdmissionRuntime::Enabled() && admittedInner && !innerTestConsumed
		&& validInnerTest && strcmp(innerTestName, saveName) == 0;
	if (injectInnerFalse) {
		innerTestConsumed = true;
		_MESSAGE("SAVE_ADMISSION_INNER_TEST simulated=1 once=1 before_reader_bind_preload_and_native_target=1");
	}
#endif
	if (LoadAdmissionRuntime::Enabled() && (!admittedInner || injectInnerFalse || !Serialization::PrepareAdmittedLoad(admittedInner.snapshot))) {
		_MESSAGE("SAVE_ADMISSION_INNER refused=1 before_preload_and_engine_target=1 request_hooks_installed=%u",unsigned(g_loadRequestHooksInstalled.load()));
		// A failed bind does not own the existing prepared reader. In particular,
		// do not close another invocation's snapshot when preparation is busy.
		LoadAdmissionRuntime::InnerReturned(admittedInner.token, false);
		g_loadGameLock.Leave();
		return false;
	}
#endif
	Serialization::SetSaveName(saveName);
	PluginManager::Dispatch_Message(0, SKSEMessagingInterface::kMessage_PreLoadGame, (void*)saveName, strlen(saveName), NULL);
	// 1.7.104 passes a sixth ABI argument (including this), a byte in the
	// caller's [rsp+28h]. The target reads it at entry-rsp+30h and uses it
	// during validation. Preserve it unchanged; do not invent its semantics.
	// Omitting it forwards an unrelated byte from this hook's stack instead.
	char loadArgumentProbe[2] = {};
	const bool traceLoadArguments = GetEnvironmentVariableA(
		"SKSE_AUTOMATION_LOAD_ARGUMENT_PROBE", loadArgumentProbe, sizeof(loadArgumentProbe)) == 1
		&& loadArgumentProbe[0] == '1';
	if (traceLoadArguments)
		_MESSAGE("LOAD_ARGUMENT_FORWARD save=%s arg1=%08X arg2=%08X sixth=%02X", saveName, unk1, unk2, unsigned(unk4));
	bool result = CALL_MEMBER_FN(this, LoadGame_HookTarget)(unk0, unk1, unk2, unk3, unk4);
#ifdef ENSRICK_EXPERIMENTAL_SAVE_ADMISSION
	if (LoadAdmissionRuntime::Enabled()) {
		Serialization::ClosePreparedLoad();
		LoadAdmissionRuntime::InnerReturned(admittedInner.token, result);
	}
#endif
	if (traceLoadArguments)
		_MESSAGE("LOAD_ARGUMENT_RESULT save=%s result=%u", saveName, unsigned(result));
	PluginManager::Dispatch_Message(0, SKSEMessagingInterface::kMessage_PostLoadGame, (void*)result, 1, NULL);
	Serialization::SetSaveName(NULL);

	g_loadGameLock.Leave();

	// Clear invalid handles in OnUpdate event registration list
	UInt32	enableClearRegs = 0;
	if(GetConfigOption_UInt32("General", "ClearInvalidRegistrations", &enableClearRegs))
	{
		if(enableClearRegs)
		{
			UInt32 count = (*g_skyrimVM)->ClearInvalidRegistrations();
			if (count > 0)
				_MESSAGE("ClearInvalidRegistrations: Removed %d invalid OnUpdate registration(s)", count);
		}
	}

#ifdef DEBUG
	_MESSAGE("Executed BGSSaveLoadManager::LoadGame_Hook.");
#endif

	return result;
}

bool		s_requestedSave	= false;
bool		s_requestedLoad	= false;
std::string	s_reqSaveName;
std::string	s_reqLoadName;

void BGSSaveLoadManager::RequestSave(const char * name)
{
	s_requestedSave = true;
	s_reqSaveName = name;
}

void BGSSaveLoadManager::RequestLoad(const char * name)
{
	s_requestedLoad = true;
	s_reqLoadName = name;
}

void BGSSaveLoadManager::ProcessEvents_Hook(void)
{
	CALL_MEMBER_FN(this, ProcessEvents_Internal)();
	
	// wants both? gets nothing.
	if(s_requestedSave && s_requestedLoad)
		_MESSAGE("BGSSaveLoadManager: save and load requested in the same frame, ignoring both");
	else if(s_requestedSave)
		Save(s_reqSaveName.c_str());
	else if(s_requestedLoad)
		Load(s_reqLoadName.c_str());

	s_requestedSave = false;
	s_requestedLoad = false;
	s_reqSaveName.clear();
	s_reqLoadName.clear();
}

void BGSSaveLoadManager::DeleteSavegame_Hook(const char * saveNameIn, UInt32 unk1)

{
	std::string saveName = saveNameIn;

	PluginManager::Dispatch_Message(0, SKSEMessagingInterface::kMessage_DeleteGame, (void*)saveName.c_str(), strlen(saveName.c_str()), NULL);
	CALL_MEMBER_FN(this, DeleteSavegame)(saveNameIn, unk1);
	Serialization::HandleDeleteSave(saveName);
}

UInt8 TESQuest::NewGame_Hook(UInt8 * unk1, UInt8 unk2)
{
	UInt8 ret = CALL_MEMBER_FN(this, NewGame_Internal)(unk1, unk2);
	PluginManager::Dispatch_Message(0, SKSEMessagingInterface::kMessage_NewGame, (void*)this, sizeof(void*), NULL);
	return ret;
}

RelocAddr <uintptr_t> SaveGame_HookTarget_Enter(0x006227F0 + 0x2B3);
RelocAddr <uintptr_t> LoadGame_HookTarget_Enter(0x00627DE0 + 0x34F);
// 691ACD40B8430FBDA081477DEB2A9948ACC235F1+11F
RelocAddr <uintptr_t> ProcessEvents_Enter(0x00658870 + 0x9A);
RelocAddr <uintptr_t> NewGame_Enter(0x00959160 + 0x59);
RelocAddr <uintptr_t> DeleteSaveGame_Enter(0x006145C0 + 0x77);
RelocAddr <uintptr_t> DeleteSaveGame_Enter2(0x00614690 + 0x17);

void Hooks_SaveLoad_Commit(void)
{
#ifdef ENSRICK_SAVE_ADMISSION_RELEASE
	const bool installRequestHooks = true;
#else
	char requestProbe[2] = {};
	const bool installRequestHooks = GetEnvironmentVariableA("SKSE_AUTOMATION_LOAD_REQUEST_PROBE", requestProbe, sizeof(requestProbe)) == 1
		&& requestProbe[0] == '1';
#endif
	if (installRequestHooks) {
#ifdef ENSRICK_SAVE_ADMISSION_RELEASE
		const bool valid = true;
		g_recoverRejectedMainLoad = true;
#else
		const DWORD length = GetEnvironmentVariableA("SKSE_AUTOMATION_REJECT_LOAD", g_rejectLoadBasename, sizeof(g_rejectLoadBasename));
		bool valid = length < sizeof(g_rejectLoadBasename);
		for (DWORD i = 0; valid && i < length; ++i) {
			const char c = g_rejectLoadBasename[i];
			valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
				|| c == '_' || c == '-' || c == '.';
		}
		char recover[2] = {};
		g_recoverRejectedMainLoad = GetEnvironmentVariableA("SKSE_AUTOMATION_RECOVER_REJECTED_LOAD", recover, sizeof(recover)) == 1 && recover[0] == '1';
#endif
		RelocAddr<uintptr_t> requestCall(0x00625FF5);
		RelocAddr<uintptr_t> failureCall(0x00625FFE);
		const UInt8 expected[] = { 0xE8, 0xE6, 0x1D, 0x00, 0x00 };
		const UInt8 failureExpected[] = { 0xE8, 0x1D, 0x1B, 0x00, 0x00 };
		if (valid && memcmp(reinterpret_cast<const void*>(requestCall.GetUIntPtr()), expected, sizeof(expected)) == 0
			&& (!g_recoverRejectedMainLoad || memcmp(reinterpret_cast<const void*>(failureCall.GetUIntPtr()), failureExpected, sizeof(failureExpected)) == 0)) {
			if (InstallLoadRequestProbe(requestCall, failureCall,
				GetFnAddr(&BGSSaveLoadManager::LoadRequestProbe_Hook),
				reinterpret_cast<uintptr_t>(&LoadRequestFailureRecovery_Hook), g_recoverRejectedMainLoad)) {
				g_loadRequestHooksInstalled.store(true);
				_MESSAGE("LOAD_REQUEST_PROBE_INSTALLED diagnostic_only=1 reject_basename=%s automatic_main_recovery=%u", g_rejectLoadBasename, unsigned(g_recoverRejectedMainLoad));
			}
		} else {
			_MESSAGE("LOAD_REQUEST_PROBE_NOT_INSTALLED invalid_input_or_call_bytes=1");
		}
	}
#ifdef ENSRICK_EXPERIMENTAL_SAVE_ADMISSION
	if (LoadAdmissionRuntime::Enabled()) {
		_MESSAGE("SAVE_ADMISSION_REQUEST_HOOKS installed=%u recovery_required=1 inner_enforcement_remains_enabled=1",unsigned(g_loadRequestHooksInstalled.load()));
		if(!g_loadRequestHooksInstalled.load())
			_ERROR("SAVE_ADMISSION_REQUEST_HOOKS_MISSING fail_closed=1 inner_loads_refused=1 native_error_ui_only=1");
	}
#endif
	// Load & Save
	g_branchTrampoline.Write5Call(SaveGame_HookTarget_Enter, GetFnAddr(&BGSSaveLoadManager::SaveGame_Hook));
	g_branchTrampoline.Write5Call(LoadGame_HookTarget_Enter, GetFnAddr(&BGSSaveLoadManager::LoadGame_Hook));
	g_branchTrampoline.Write5Call(ProcessEvents_Enter, GetFnAddr(&BGSSaveLoadManager::ProcessEvents_Hook));

	// New Game
	g_branchTrampoline.Write5Call(NewGame_Enter, GetFnAddr(&TESQuest::NewGame_Hook));

	// Delete savegame
	g_branchTrampoline.Write5Call(DeleteSaveGame_Enter, GetFnAddr(&BGSSaveLoadManager::DeleteSavegame_Hook));
	g_branchTrampoline.Write5Call(DeleteSaveGame_Enter2, GetFnAddr(&BGSSaveLoadManager::DeleteSavegame_Hook));
}
