// SPDX-License-Identifier: MIT
#include "TextureReleaseFix.h"
#include "skse64_common/Relocation.h"
#include "skse64_common/Utilities.h"
#include <Windows.h>

namespace TextureReleaseFix {
void Install() {
    UInt32 configured = 1;
    GetConfigOption_UInt32("General", "EnableTextureDuplicateReleaseFix", &configured);
    char overrideValue[2] = {};
    const DWORD overrideLength = GetEnvironmentVariableA("SKSE_AUTOMATION_TEXTURE_RELEASE_FIX", overrideValue, sizeof(overrideValue));
    if (!Enabled(configured, overrideLength, overrideValue[0])) {
        _MESSAGE("TEXTURE_RELEASE_FIX installed=0 disabled_by_policy=1 configured=%u environment_length=%u", configured, overrideLength);
        return;
    }
    auto* function = reinterpret_cast<unsigned char*>(RelocationManager::s_baseAddr + FunctionRva);
    unsigned char before[sizeof(Original)] = {};
    SIZE_T count = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), function, before, sizeof(before), &count) ||
        count != sizeof(before) || !Matches(before, count)) {
        _MESSAGE("TEXTURE_RELEASE_FIX installed=0 reason=unknown_or_modified_function rva=100F190");
        return;
    }
    // Startup only, after plugin patches but before engine worker execution.
    // No detour, trampoline, exception swallowing, or replacement allocator.
    auto* site = function + PatchOffset;
    DWORD previous = 0;
    if (!VirtualProtect(site, sizeof(Patch), PAGE_EXECUTE_READWRITE, &previous)) {
        _MESSAGE("TEXTURE_RELEASE_FIX installed=0 reason=protect error=%u", GetLastError());
        return;
    }
    std::memcpy(site, Patch, sizeof(Patch));
    DWORD ignored = 0;
    const bool restored = VirtualProtect(site, sizeof(Patch), previous, &ignored) != FALSE;
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), site, sizeof(Patch)) != FALSE;
    unsigned char after[sizeof(Original)] = {};
    count = 0;
    std::memcpy(before + PatchOffset, Patch, sizeof(Patch));
    const bool verified = ReadProcessMemory(GetCurrentProcess(), function, after, sizeof(after), &count) &&
        count == sizeof(after) && !std::memcmp(before, after, sizeof(after));
    _MESSAGE("TEXTURE_RELEASE_FIX installed=%u protection_restored=%u instruction_cache_flushed=%u default_enabled=1 environment_override=%u rva=100F1DE bytes=EB2B90 scope=remove_duplicate_release_triplet",
        unsigned(verified), unsigned(restored), unsigned(flushed), unsigned(overrideLength != 0));
}
}
