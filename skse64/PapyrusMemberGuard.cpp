#include "PapyrusMemberGuard.h"
#include "skse64_common/Relocation.h"
#include "skse64_common/BranchTrampoline.h"
#include <Windows.h>
#include <atomic>
#include <cstring>

namespace PapyrusMemberGuard {
namespace {
using SourceFn = bool(*)(void*, const void*, void*);
using DestinationFn = bool(*)(void*, const void*, bool*, bool*, void**);
SourceFn originalSource = nullptr;
DestinationFn originalDestination = nullptr;
std::atomic<unsigned long long> readRejects{0}, writeRejects{0};

struct NativeReader {
    template<class T> bool operator()(std::uintptr_t address, T& value) const {
        std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
        return true;
    }
};

Evaluation Inspect(void* tasklet, const void* operand) {
    // Guard failures never become a second fault. No allocations/refcount edits.
    __try {
        return Evaluate(reinterpret_cast<std::uintptr_t>(tasklet),
            reinterpret_cast<std::uintptr_t>(operand), NativeReader{});
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return Evaluation{};
    }
}

void TypeName(std::uintptr_t type, char* name, std::size_t length) {
    name[0] = 0;
    __try {
        const char* text = nullptr;
        std::memcpy(&text, reinterpret_cast<const void*>(type + 8), sizeof(text));
        if (!text) return;
        std::size_t i = 0;
        for (; i + 1 < length && text[i]; ++i) name[i] = text[i];
        name[i] = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { name[0] = 0; }
}

void Report(const Evaluation& value, bool write) {
    const auto count = (write ? writeRejects : readRejects).fetch_add(1) + 1;
    if (count > 32 && count % 1024) return;
    char name[96] = {};
    TypeName(value.type, name, sizeof(name));
    _MESSAGE("PAPYRUS_MEMBER_GUARD access=%s rejected=%llu type=%s index=%u variables=%u object=%016llX typeinfo=%016llX",
        write ? "write" : "read", count, name, value.index, value.count,
        static_cast<unsigned long long>(value.object), static_cast<unsigned long long>(value.type));
}

bool SourceHook(void* tasklet, const void* operand, void* output) {
    const auto value = Inspect(tasklet, operand);
    if (value.reject && value.known) {
        Report(value, false);
        return false; // Native failure leaves the caller's output untouched.
    }
    return originalSource(tasklet, operand, output);
}

bool DestinationHook(void* tasklet, const void* operand, bool* selfFlag, bool* stateFlag, void** output) {
    const auto value = Inspect(tasklet, operand);
    if (value.reject && value.known) {
        *selfFlag = false;
        *stateFlag = false;
        Report(value, true);
        return false; // No dereference/default/destruction of *output.
    }
    return originalDestination(tasklet, operand, selfFlag, stateFlag, output);
}

void* Forwarder(std::uintptr_t entry) {
    // Both pinned first-six-byte spans contain only register/stack operations.
    // Copy whole instructions, then jump without clobbering a register.
    auto* code = static_cast<unsigned char*>(g_localTrampoline.Allocate(20));
    std::memcpy(code, reinterpret_cast<const void*>(entry), 6);
    const unsigned char jump[6] = {0xFF, 0x25, 0, 0, 0, 0};
    std::memcpy(code + 6, jump, 6);
    const auto continuation = entry + 6;
    std::memcpy(code + 12, &continuation, sizeof(continuation));
    FlushInstructionCache(GetCurrentProcess(), code, 20);
    return code;
}
}

void Install() {
    char setting[2] = {};
    if (GetEnvironmentVariableA("SKSE_AUTOMATION_MEMBER_GUARD", setting, sizeof(setting)) != 1 || setting[0] != '1') return;
    const auto source = RelocationManager::s_baseAddr + 0x014D54C0;
    const auto destination = RelocationManager::s_baseAddr + 0x014D58F0;
    const unsigned char sourcePrefix[] = {0x40,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8D,0xAC,0x24,0xB0,0xF8,0xFF,0xFF};
    const unsigned char destinationPrefix[] = {0x4C,0x8B,0xDC,0x57,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0x40,0x08,0x00,0x00};
    if (std::memcmp(reinterpret_cast<const void*>(source), sourcePrefix, sizeof(sourcePrefix)) ||
        std::memcmp(reinterpret_cast<const void*>(destination), destinationPrefix, sizeof(destinationPrefix)) ||
        g_branchTrampoline.Remain() < 28 || g_localTrampoline.Remain() < 40) {
        _MESSAGE("PAPYRUS_MEMBER_GUARD not_installed=1 reason=signature_or_capacity");
        return;
    }
    originalSource = reinterpret_cast<SourceFn>(Forwarder(source));
    originalDestination = reinterpret_cast<DestinationFn>(Forwarder(destination));
    const bool readInstalled = g_branchTrampoline.Write5Branch(source, reinterpret_cast<std::uintptr_t>(SourceHook));
    const bool writeInstalled = g_branchTrampoline.Write5Branch(destination, reinterpret_cast<std::uintptr_t>(DestinationHook));
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(source), 6);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(destination), 6);
    _MESSAGE("PAPYRUS_MEMBER_GUARD installed_read=%u installed_write=%u runtime=1.7.104 experimental=1", unsigned(readInstalled), unsigned(writeInstalled));
}
}
