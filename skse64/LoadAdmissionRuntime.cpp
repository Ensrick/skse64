// SPDX-License-Identifier: MIT
// Experimental, opt-in pinned-runtime adapter. NOT a campaign repair.
#include "LoadAdmissionRuntime.h"
#include "SaveAdmissionAbi.h"
#include "GameData.h"
#include "Serialization.h"
#include "LoadPluginSnapshot.h"
#include "skse64_common/Utilities.h"
#include <memory>
#include <mutex>
#include <vector>

namespace LoadAdmissionRuntime {
namespace {
struct Context {
    void* stream = nullptr;
    std::string basename;
    ensrick_admission_lease* lease = nullptr;
    ~Context() { ensrick_admission_release(lease); }
};
std::mutex gate;
std::unique_ptr<Context> pending;
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
bool Begin(std::uint64_t** input) {
    if (!Enabled()) return true;
    try {
        std::lock_guard<std::mutex> lock(gate);
        if (pending) throw std::runtime_error("previous admitted load is still pending");
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
        const auto status = ensrick_admission_begin(&request, &result, &next->lease);
        _MESSAGE("SAVE_ADMISSION experimental=1 status=%u reason=%s full=%u light=%u fingerprint=%016llX", status,
            result.reason, result.saved_full_count, result.saved_light_count, fingerprint);
        if (status != ENSRICK_ADMISSION_OK) return false;
        next->stream = stream;
        next->basename = name;
        pending = std::move(next);
        return true;
    } catch (const std::exception& error) {
        _MESSAGE("SAVE_ADMISSION experimental=1 refused=1 adapter_reason=%s", error.what());
    } catch (...) { _MESSAGE("SAVE_ADMISSION experimental=1 refused=1 adapter_reason=unknown_exception"); }
    return false;
}
bool OwnsStream(void* stream) {
    std::lock_guard<std::mutex> lock(gate);
    if (!pending || pending->stream != stream) return false;
    // The buffer is legitimately decompressed between these hooks; its raw
    // byte count cannot be compared here. Name continuity adds a check but is
    // NOT proof against ABA reuse after an unobserved async cancellation.
    const char* name = nullptr;
    if (!Read(static_cast<const char*>(stream)+0xBB0, &name, sizeof(name)) || !name) return false;
    for (size_t i = 0; i <= pending->basename.size(); ++i) {
        char value = 0;
        if (!Read(name+i, &value, 1) || value != pending->basename.c_str()[i]) return false;
    }
    return true;
}
bool MatchesCoSave(void* handle) {
    std::lock_guard<std::mutex> lock(gate);
    const bool match = pending && ensrick_admission_lease_matches_handle(pending->lease, handle);
    _MESSAGE("SAVE_ADMISSION_COSAVE_HANDLE experimental=1 matched=%u", unsigned(match));
    return match;
}
void Finish(void* stream) {
    std::lock_guard<std::mutex> lock(gate);
    if (pending && pending->stream == stream) {
        pending.reset();
        _MESSAGE("SAVE_ADMISSION_LEASE experimental=1 released=1");
    }
}
void RequestReturned(void* admittedStream, void* callerStream, bool result) {
    std::lock_guard<std::mutex> lock(gate);
    if (!pending || pending->stream != admittedStream) return;
    if (callerStream == admittedStream) {
        // Pinned caller625FFA..626024 unconditionally destroys its remaining
        // nonnull stream, whether target returned true or false. No deferred
        // owner exists in this case. Release even after a pre-inner failure.
        pending.reset();
        _MESSAGE("SAVE_ADMISSION_LEASE experimental=1 released=1 caller_retained_terminal_stream=1 result=%u", unsigned(result));
    } else {
        // 627FF2 clears the caller pointer when transferring to a callback.
        // Keep the lease until its inner load; cancellation/destruction of
        // that callback still needs coverage before production deployment.
        _MESSAGE("SAVE_ADMISSION_PENDING experimental=1 outer_result=%u lease_retained=1 transferred=%u route_unverified=1",
            unsigned(result), unsigned(callerStream == nullptr));
    }
}
}
