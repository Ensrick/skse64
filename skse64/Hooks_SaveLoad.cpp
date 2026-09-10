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

namespace {
	// Diagnostic experiment only. Not installed unless explicitly opted in.
	char g_rejectLoadBasename[260] = {};
	bool ReadLoadProbeName(UInt64** input, char (&name)[260])
	{
		UInt64* stream = nullptr;
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
}

bool BGSSaveLoadManager::LoadRequestProbe_Hook(UInt64** stream, UInt32 arg1, UInt8 arg2, UInt8 arg3, UInt32 arg4)
{
	char name[260] = {};
	const bool readable = ReadLoadProbeName(stream, name);
	const bool reject = g_rejectLoadBasename[0]
		&& (!readable || _stricmp(name, g_rejectLoadBasename) == 0);
	_MESSAGE("LOAD_REQUEST_PROBE save=%s readable=%u arg1=%08X arg2=%02X arg3=%02X arg4=%08X reject=%u",
		readable ? name : "<unreadable>", unsigned(readable), arg1, unsigned(arg2), unsigned(arg3), arg4, unsigned(reject));
	if (reject) {
		// Return to the existing caller at625FFA; it calls627B20 for false.
		// This diagnostic deliberately does not enter627DE0 or emit SKSE load messages.
		_MESSAGE("LOAD_REQUEST_REJECTED diagnostic_only=1 engine_target_entered=0");
		return false;
	}
	const bool result = CALL_MEMBER_FN(this, LoadRequestProbe_Target)(stream, arg1, arg2, arg3, arg4);
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
	char requestProbe[2] = {};
	if (GetEnvironmentVariableA("SKSE_AUTOMATION_LOAD_REQUEST_PROBE", requestProbe, sizeof(requestProbe)) == 1
		&& requestProbe[0] == '1') {
		const DWORD length = GetEnvironmentVariableA("SKSE_AUTOMATION_REJECT_LOAD", g_rejectLoadBasename, sizeof(g_rejectLoadBasename));
		bool valid = length < sizeof(g_rejectLoadBasename);
		for (DWORD i = 0; valid && i < length; ++i) {
			const char c = g_rejectLoadBasename[i];
			valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
				|| c == '_' || c == '-' || c == '.';
		}
		RelocAddr<uintptr_t> requestCall(0x00625FF5);
		const UInt8 expected[] = { 0xE8, 0xE6, 0x1D, 0x00, 0x00 };
		if (valid && memcmp(reinterpret_cast<const void*>(requestCall.GetUIntPtr()), expected, sizeof(expected)) == 0) {
			g_branchTrampoline.Write5Call(requestCall, GetFnAddr(&BGSSaveLoadManager::LoadRequestProbe_Hook));
			_MESSAGE("LOAD_REQUEST_PROBE_INSTALLED diagnostic_only=1 reject_basename=%s", g_rejectLoadBasename);
		} else {
			_MESSAGE("LOAD_REQUEST_PROBE_NOT_INSTALLED invalid_input_or_call_bytes=1");
		}
	}
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
