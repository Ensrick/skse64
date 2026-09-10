// SPDX-License-Identifier: MIT
// Optional call-site observation. No COM queries, AddRef/Release probes, or
// changes to wrapper data. The native operation executes exactly once.
#include "TextureReleaseFix.h"
#include "skse64_common/Relocation.h"
#include "skse64_common/BranchTrampoline.h"
#include "skse64_common/Utilities.h"
#include <Windows.h>
#include <atomic>
#include <climits>

namespace TextureReleaseFix {
namespace {
using Native = void(*)(void*, void*);
Native original = nullptr;
std::atomic<std::uint64_t> calls{0};
constexpr std::uintptr_t Caller = 0x43CB54;
constexpr unsigned char CallBytes[] = {0xE8,0x37,0x26,0xBD,0x00};
bool Read(const void* address, void* output, std::size_t size) {
    SIZE_T read = 0;
    return ReadProcessMemory(GetCurrentProcess(), address, output, size, &read) && read == size;
}
void RecordCpp(std::uint64_t n, void* wrapper, bool returned) {
    if (!ProbeSample(n)) return;
    unsigned char fields[0x24] = {};
    // Never dereference wrapper AFTER native final release.
    const bool readable = !returned && Read(wrapper, fields, sizeof(fields));
    std::uint32_t references = 0;
    if (readable) std::memcpy(&references, fields+0x20, sizeof(references));
    _MESSAGE("TEXTURE_RELEASE_PROBE sample=%llu caller_rva=43CB54 wrapper=%016llX returned=%u readable_before=%u wrapper_refs_before=%u diagnostic_only=1 ownership_not_inferred=1",
        n, static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(wrapper)),
        unsigned(returned), unsigned(readable), references);
}
void RecordCppSafe(std::uint64_t n, void* wrapper, bool returned) noexcept {
    try { RecordCpp(n, wrapper, returned); } catch (...) {}
}
void Record(std::uint64_t n, void* wrapper, bool returned) noexcept {
    const DWORD error = GetLastError();
    __try { RecordCppSafe(n, wrapper, returned); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    SetLastError(error);
}
void Hook(void* renderer, void* wrapper) {
    const auto n = calls.fetch_add(1, std::memory_order_relaxed)+1;
    Record(n, wrapper, false);
    original(renderer, wrapper); // Native failures are not swallowed.
    Record(n, wrapper, true);
}
}
void InstallProbe() {
    char enabled[2] = {};
    if (GetEnvironmentVariableA("SKSE_AUTOMATION_TEXTURE_RELEASE_PROBE", enabled, sizeof(enabled)) != 1 || enabled[0] != '1') return;
    const auto entry = RelocationManager::s_baseAddr + FunctionRva;
    const auto caller = RelocationManager::s_baseAddr + Caller;
    unsigned char body[sizeof(Original)] = {}, call[sizeof(CallBytes)] = {}, patched[sizeof(Original)];
    std::memcpy(patched, Original, sizeof(patched));
    std::memcpy(patched+PatchOffset, Patch, sizeof(Patch));
    if (!Read(reinterpret_cast<void*>(entry), body, sizeof(body)) ||
        (!Matches(body, sizeof(body)) && std::memcmp(body, patched, sizeof(body))) ||
        !Read(reinterpret_cast<void*>(caller), call, sizeof(call)) ||
        std::memcmp(call, CallBytes, sizeof(call)) || g_branchTrampoline.Remain() < 14) {
        _MESSAGE("TEXTURE_RELEASE_PROBE installed=0 reason=signature_or_capacity");
        return;
    }
    auto* relay = static_cast<unsigned char*>(g_branchTrampoline.Allocate(14));
    if (!relay) { _MESSAGE("TEXTURE_RELEASE_PROBE installed=0 reason=relay_allocation"); return; }
    const auto delta = reinterpret_cast<std::intptr_t>(relay) - static_cast<std::intptr_t>(caller+5);
    if (delta < INT_MIN || delta > INT_MAX) {
        _MESSAGE("TEXTURE_RELEASE_PROBE installed=0 reason=relay_out_of_range");
        return;
    }
    const unsigned char jump[] = {0xFF,0x25,0,0,0,0};
    const auto target = reinterpret_cast<std::uintptr_t>(&Hook);
    std::memcpy(relay, jump, sizeof(jump));
    std::memcpy(relay+6, &target, sizeof(target));
    if (!FlushInstructionCache(GetCurrentProcess(), relay, 14)) {
        _MESSAGE("TEXTURE_RELEASE_PROBE installed=0 reason=relay_cache_flush error=%u", GetLastError()); return;
    }
    original = reinterpret_cast<Native>(entry);
    unsigned char replacement[5] = {0xE8};
    const auto displacement = static_cast<std::int32_t>(delta);
    std::memcpy(replacement+1, &displacement, sizeof(displacement));
    DWORD previous = 0, ignored = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(caller), 5, PAGE_EXECUTE_READWRITE, &previous)) {
        _MESSAGE("TEXTURE_RELEASE_PROBE installed=0 reason=call_protection error=%u", GetLastError()); return;
    }
    std::memcpy(reinterpret_cast<void*>(caller), replacement, sizeof(replacement));
    const bool protectedAgain = VirtualProtect(reinterpret_cast<void*>(caller), 5, previous, &ignored) != FALSE;
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(caller), 5) != FALSE;
    const bool verified = Read(reinterpret_cast<void*>(caller), call, sizeof(call)) && !std::memcmp(call, replacement, sizeof(call));
    _MESSAGE("TEXTURE_RELEASE_PROBE installed=%u protection_restored=%u instruction_cache_flushed=%u caller_rva=43CB54 diagnostic_only=1 com_queries=0",
        unsigned(verified), unsigned(protectedAgain), unsigned(flushed));
}
}
