// SPDX-License-Identifier: MIT
// Experimental, opt-in pinned-runtime adapter. NOT a campaign repair.
#include "LoadAdmissionRuntime.h"
#include "SaveAdmissionAbi.h"
#include "GameData.h"
#include "Serialization.h"
#include "LoadPluginSnapshot.h"
#include "LoadStreamLifetime.h"
#include "skse64_common/Utilities.h"
#include <memory>
#include <mutex>
#include <vector>
#include <atomic>
#include <cstring>

static_assert(ENSRICK_ADMISSION_INVALID_ARGUMENT==1 && ENSRICK_ADMISSION_FINGERPRINT_UNAVAILABLE==2 &&
    ENSRICK_ADMISSION_MALFORMED_SAVE==3 && ENSRICK_ADMISSION_ACTIVE_PLUGINS_INVALID==4 &&
    ENSRICK_ADMISSION_PLUGIN_MISMATCH==5 && ENSRICK_ADMISSION_LEASE_FAILED==6 &&
    ENSRICK_ADMISSION_COSAVE_REFUSED==7 && ENSRICK_ADMISSION_OUT_OF_MEMORY==8 &&
    ENSRICK_ADMISSION_INTERNAL_ERROR==9, "Update refusal message codes for changed ABI statuses");

namespace LoadAdmissionRuntime {
namespace {
struct Context {
    void* stream = nullptr;
    std::string basename;
    std::shared_ptr<ensrick_admission_lease> lease;
    std::uint64_t generation = 0;
    AdmittedSnapshot::Bytes snapshot;
    bool innerAcquired = false;
    bool innerReturned = false;
    bool outerInFlight = true;
    bool awaitingResume = false;
};
std::mutex gate;
thread_local bool gateOwned = false;
std::unique_ptr<Context> pending;
std::atomic<std::uint64_t> generations{0}, events{0};
std::atomic<bool> lifetimeFault{false};
bool Read(const void* address, void* output, size_t size) {
    SIZE_T got = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), address, output, size, &got) && got == size;
}
std::wstring Wide(const std::string& value) {
    const int length = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, value.c_str(), -1, nullptr, 0);
    if (length <= 1) throw std::runtime_error("co-save path conversion failed");
    std::wstring result(length, 0);
    if (MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, value.c_str(), -1, &result[0], length) != length)
        throw std::runtime_error("co-save path conversion failed");
    return result;
}
std::vector<ensrick_admission_plugin_name> Names(const std::vector<std::string>& names) {
    std::vector<ensrick_admission_plugin_name> result;
    for (const auto& name : names) result.push_back({name.c_str(), static_cast<uint32_t>(name.size()+1), 0});
    return result;
}
}
bool Enabled() {
    static const bool enabled = [] {
        char value[2] = {};
        return GetEnvironmentVariableA("SKSE_AUTOMATION_ENFORCE_SAVE_ADMISSION", value, sizeof(value)) == 1 && value[0] == '1';
    }();
    return enabled;
}
bool Begin(std::uint64_t** input, RequestToken& admitted, LoadRefusal::Notice& refused) {
    admitted = {};
    refused = LoadRefusal::Notice();
    if (!Enabled()) return true;
    refused = LoadRefusal::Notice(LoadRefusal::AdapterFailure);
    try {
        if (lifetimeFault.load() || !LoadStreamLifetime::Ready()) {
            refused=LoadRefusal::Notice(LoadRefusal::UnsupportedStack);
            throw std::runtime_error("save stream lifetime coverage unavailable");
        }
        // Record Engine Fixes' independent achievement prompt patch. Mandatory
        // stream lifetime coverage now protects transferred callback ownership;
        // absence of this patch is no longer an admission bypass or veto.
        HasSuppressedAchievementPrompt();
        std::uint64_t* resumeStream = nullptr;
        if (!Read(input, &resumeStream, sizeof(resumeStream)))
            throw std::runtime_error("unreadable caller stream");
        const auto resume = TryResume(resumeStream, admitted);
        if (resume == ResumeAttempt::Resumed) { refused=LoadRefusal::Notice(); return true; }
        if (resume == ResumeAttempt::Refused) {
            refused=LoadRefusal::Notice(LoadRefusal::PreviousPending);
            throw std::runtime_error("pending load is not an owned deferred resume");
        }
        TrackedGate lock(gate, gateOwned);
        if (pending) {
            refused=LoadRefusal::Notice(LoadRefusal::PreviousPending);
            throw std::runtime_error("previous admitted load is still pending");
        }
        std::uint64_t *stream = nullptr, vtable = 0, memory = 0;
        std::uint32_t size = 0, position = 0;
        std::uint8_t decompressed = 255;
        if (!Read(input, &stream, sizeof(stream)) || !Read(stream, &vtable, sizeof(vtable))
            || vtable != RelocationManager::s_baseAddr + 0x01B521A0)
            throw std::runtime_error("unsupported engine save stream");
        const auto field = [stream](size_t offset) { return reinterpret_cast<const char*>(stream) + offset; };
        if (!Read(field(0xBD0), &memory, sizeof(memory)) || !Read(field(0x174), &size, sizeof(size))
            || !Read(field(0xBE0), &position, sizeof(position)) || !Read(field(0xBCA), &decompressed, 1)
            || !memory || !size || size > 0x04000000 || position || decompressed)
            throw std::runtime_error("engine save snapshot is unavailable or already consumed");
        std::vector<std::uint8_t> snapshot(size);
        if (!Read(reinterpret_cast<const void*>(memory), snapshot.data(), size))
            throw std::runtime_error("engine save snapshot could not be read");
        const char* namePointer = nullptr;
        char name[260] = {};
        if (!Read(field(0xBB0), &namePointer, sizeof(namePointer)) || !namePointer)
            throw std::runtime_error("save name unavailable");
        size_t count = 0;
        for (; count < sizeof(name); ++count) {
            if (!Read(namePointer+count, name+count, 1)) throw std::runtime_error("unreadable save name");
            if (!name[count]) break;
        }
        if (count < 5 || count == sizeof(name) || strpbrk(name, "\\/:") || _stricmp(name+count-4, ".ess"))
            throw std::runtime_error("unsupported save basename");
        DataHandler* handler = nullptr;
        if (!Read(g_dataHandler.GetPtr(), &handler, sizeof(handler))) throw std::runtime_error("loaded plugins unavailable");
        const auto reader = [](std::uintptr_t address, void* target, size_t length) { return Read(reinterpret_cast<const void*>(address), target, length); };
        const auto plugins = LoadPluginSnapshot::Read(reader, reinterpret_cast<std::uintptr_t>(handler));
        const auto full = Names(plugins.full), light = Names(plugins.light);
        const auto module = GetModuleHandleA("EnsrickCurrencyDenominations.dll");
        typedef std::uint64_t (*Getter)();
        const auto getter = module ? reinterpret_cast<Getter>(GetProcAddress(module, "EnsrickCurrency_GetAdmissionFingerprintV1")) : nullptr;
        const auto fingerprint = getter ? getter() : 0;
        if (lifetimeFault.load()) throw std::runtime_error("save lifetime invariant failed during validation");
        const auto path = Wide(Serialization::GetCoSavePath(name));
        const std::vector<uint16_t> pathUnits(path.begin(), path.end());
        ensrick_admission_request request = {};
        request.struct_size = sizeof(request); request.abi_version = ENSRICK_ADMISSION_ABI_VERSION;
        request.ess_bytes = snapshot.data(); request.ess_size = snapshot.size();
        request.full_plugins = full.data(); request.full_count = static_cast<uint32_t>(full.size());
        request.light_plugins = light.data(); request.light_count = static_cast<uint32_t>(light.size());
        request.expected_fingerprint = fingerprint;
        request.cosave_path_utf16 = pathUnits.data();
        request.cosave_path_capacity_units = static_cast<uint32_t>(pathUnits.size());
        ensrick_admission_result result = {}; result.struct_size = sizeof(result);
        std::unique_ptr<Context> next(new Context);
        ensrick_admission_lease* acquired = nullptr;
        const auto status = ensrick_admission_begin(&request, &result, &acquired);
        if (acquired) next->lease = std::shared_ptr<ensrick_admission_lease>(acquired, ensrick_admission_release);
        _MESSAGE("SAVE_ADMISSION experimental=1 status=%u reason=%s full=%u light=%u fingerprint=%016llX", status,
            result.reason, result.saved_full_count, result.saved_light_count, fingerprint);
        if (status != ENSRICK_ADMISSION_OK) { refused=LoadRefusal::Notice(status); return false; }
        if (ensrick_admission_lease_cosave(next->lease.get(), &next->snapshot.data, &next->snapshot.size) != ENSRICK_ADMISSION_OK ||
            !next->snapshot.data || !next->snapshot.size || next->snapshot.size > AdmittedSnapshot::MaximumBytes)
            throw std::runtime_error("validated co-save snapshot unavailable");
        next->snapshot.owner = next->lease;
        next->stream = stream;
        next->basename = name;
        next->generation = generations.fetch_add(1, std::memory_order_relaxed)+1;
        _MESSAGE("SAVE_ADMISSION_CONTEXT generation=%llu stream=%016llX seq=%llu identity=pointer_only",
            next->generation, static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(stream)), NextEvent());
        pending = std::move(next);
        admitted = RequestToken(pending->stream, pending->generation);
        refused=LoadRefusal::Notice();
        return true;
    } catch (const std::exception& error) {
        _MESSAGE("SAVE_ADMISSION experimental=1 refused=1 adapter_reason=%s", error.what());
    } catch (...) { _MESSAGE("SAVE_ADMISSION experimental=1 refused=1 adapter_reason=unknown_exception"); }
    return false;
}
std::uint64_t NextEvent() noexcept { return events.fetch_add(1, std::memory_order_relaxed)+1; }
bool PendingNameMatches(void* stream) {
    if (!stream || !pending || pending->stream != stream) return false;
    const char* name = nullptr;
    std::uint64_t vtable=0;
    if (!Read(stream,&vtable,sizeof(vtable)) || vtable!=RelocationManager::s_baseAddr+0x1B521A0 ||
        !Read(static_cast<const char*>(stream)+0xBB0,&name,sizeof(name)) || !name) return false;
    for(size_t i=0;i<=pending->basename.size();++i) {
        char value=0;
        if(!Read(name+i,&value,1) || value!=pending->basename.c_str()[i]) return false;
    }
    return true;
}
ResumeAttempt TryResume(void* stream, RequestToken& token) noexcept {
    token={};
    try {
        TrackedGate lock(gate,gateOwned);
        if(lifetimeFault.load()) return ResumeAttempt::Refused;
        if(!pending) return ResumeAttempt::NoPending;
        if(!pending->generation || !pending->awaitingResume || pending->outerInFlight ||
            pending->innerAcquired || !PendingNameMatches(stream)) return ResumeAttempt::Refused;
        pending->outerInFlight=true;
        pending->awaitingResume=false;
        pending->innerReturned=false;
        // A queued retry is a new outer invocation even though it owns the
        // same live stream and immutable lease. Old return tokens stay stale.
        pending->generation=generations.fetch_add(1,std::memory_order_relaxed)+1;
        token=RequestToken(stream,pending->generation);
        _MESSAGE("SAVE_ADMISSION_RESUME generation=%llu seq=%llu retained_snapshot=1 engine_cursor_untouched=1",token.generation,NextEvent());
        return ResumeAttempt::Resumed;
    } catch(...) { return ResumeAttempt::Refused; }
}
void InnerReturned(const RequestToken& token,bool result) {
    TrackedGate lock(gate,gateOwned);
    if(!pending || !token.generation || pending->stream!=token.stream || pending->generation!=token.generation) return;
    if(!pending->innerAcquired || !pending->outerInFlight) { lifetimeFault.store(true); return; }
    pending->innerAcquired=false;
    pending->innerReturned=true;
    _MESSAGE("SAVE_ADMISSION_INNER_RETURN generation=%llu seq=%llu result=%u lease_retained_until_outer_or_destroy=1",token.generation,NextEvent(),unsigned(result));
}
void StreamDestroying(void* stream) noexcept {
    // A gate holder must not call native destruction. Never deadlock or admit
    // a reused address if a foreign callback violates this invariant.
    if(gateOwned) { lifetimeFault.store(true); return; }
    try {
        TrackedGate lock(gate,gateOwned);
        if(pending && pending->stream==stream) {
            const auto generation=pending->generation;
            pending.reset();
            _MESSAGE("SAVE_ADMISSION_DESTROY generation=%llu seq=%llu before_native_destructor=1",generation,NextEvent());
        }
    } catch(...) { lifetimeFault.store(true); }
}
bool HasSuppressedAchievementPrompt() noexcept {
    try {
        // Pinned 1.7.104 ID441528. Engine Fixes: xor rax,rax; ret; INT3.
        // This signature establishes only this entry's immediate return path.
        const unsigned char expected[] = {0x48, 0x31, 0xC0, 0xC3, 0xCC};
        unsigned char actual[sizeof(expected)] = {};
        const bool readable = Read(reinterpret_cast<const void*>(RelocationManager::s_baseAddr + 0x1BFA00), actual, sizeof(actual));
        const bool matched = readable && std::memcmp(actual, expected, sizeof(actual)) == 0;
        _MESSAGE("SAVE_ADMISSION_ACHIEVEMENT_GATE supported=%u readable=%u bytes=%02X%02X%02X%02X%02X scope=achievement_prompt_only",
            unsigned(matched), unsigned(readable), unsigned(actual[0]), unsigned(actual[1]), unsigned(actual[2]), unsigned(actual[3]), unsigned(actual[4]));
        return matched;
    } catch (...) { return false; }
}
PendingObservation ObservePending(void* stream) noexcept {
    PendingObservation result;
    if (gateOwned) return result;
    try {
        std::unique_lock<std::mutex> lock(gate, std::try_to_lock);
        if (!lock.owns_lock()) return result;
        result.acquired = true;
        result.sequence = NextEvent(); // ordered with releases under the same gate
        result.present = bool(pending);
        result.matched = pending && pending->stream == stream;
        result.generation = pending ? pending->generation : 0;
    } catch (...) {} // failed observation is unknown, never "no pending load"
    return result;
}
InnerAdmission AcquireInner(void* stream) noexcept {
    try {
        TrackedGate lock(gate, gateOwned);
        if (lifetimeFault.load() || !stream || !pending || pending->innerAcquired || pending->innerReturned ||
            !pending->outerInFlight || !pending->generation || pending->stream != stream ||
            !pending->snapshot.owner || !pending->snapshot.data || !pending->snapshot.size ||
            pending->snapshot.size > AdmittedSnapshot::MaximumBytes) return {};
        // The buffer is legitimately decompressed between these hooks; its raw
        // byte count cannot be compared here. Name continuity adds a check but is
        // NOT proof against ABA reuse after an unobserved async cancellation.
        if(!PendingNameMatches(stream)) return {};
        InnerAdmission admitted;
        admitted.token = RequestToken(stream, pending->generation);
        admitted.snapshot = pending->snapshot;
        pending->innerAcquired = true;
        return admitted;
    } catch (...) { return {}; }
}
bool MatchesCoSave(void* handle) {
    TrackedGate lock(gate, gateOwned);
    const bool match = pending && ensrick_admission_lease_matches_handle(pending->lease.get(), handle);
    _MESSAGE("SAVE_ADMISSION_COSAVE_HANDLE experimental=1 matched=%u", unsigned(match));
    return match;
}
void RequestReturned(const RequestToken& admitted, void* callerStream, bool result) {
    void* admittedStream = admitted.stream;
    TrackedGate lock(gate, gateOwned);
    // Native execution may finish one context and allow another before this
    // wrapper returns. Never let an old request release the newer generation,
    // even if allocator reuse gives it the identical address and basename.
    if (!pending || !admitted.generation || pending->stream != admittedStream ||
        pending->generation != admitted.generation) return;
    if (callerStream == admittedStream) {
        // Pinned caller625FFA..626024 unconditionally destroys its remaining
        // nonnull stream, whether target returned true or false. No deferred
        // owner exists in this case. Release even after a pre-inner failure.
        const auto generation = pending->generation;
        pending.reset();
        _MESSAGE("SAVE_ADMISSION_CONTEXT_RELEASE experimental=1 released=1 snapshot_readers_may_retain_lease=1 caller_retained_terminal_stream=1 result=%u generation=%llu seq=%llu", unsigned(result), generation, NextEvent());
    } else {
        // 627FF2 clears the caller pointer when transferring to a callback.
        // Keep the lease until resumed outer completion or the mandatory
        // pre-destruction hook retires a cancelled native owner.
        pending->outerInFlight=false;
        pending->awaitingResume=callerStream==nullptr && !pending->innerAcquired;
        if(callerStream!=nullptr || pending->innerAcquired) lifetimeFault.store(true);
        _MESSAGE("SAVE_ADMISSION_PENDING experimental=1 seq=%llu outer_result=%u lease_retained=1 transferred=%u",
            NextEvent(), unsigned(result), unsigned(callerStream == nullptr));
    }
}
}
