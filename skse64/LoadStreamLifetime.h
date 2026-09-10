// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace LoadStreamLifetime {
// Exact 1.7.104 entries; only the FIRST SIX bytes are relocated. Both are
// whole register/stack instructions, with no relative operand in those six.
constexpr std::uintptr_t ScalarRva = 0x159D3E0;
constexpr std::uintptr_t NondeletingRva = 0x159D320;
constexpr unsigned MaxEvents = 128;
constexpr unsigned char ScalarPrefix[] = {0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8D,0x05,0xAF,0x4D,0x5B,0x00};
constexpr unsigned char NondeletingPrefix[] = {0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8D,0x05,0x73,0x4E,0x5B,0x00,0x48,0x8B,0xD9};

inline bool EmitEvent(std::uint64_t number) { return number && number <= MaxEvents; }
inline bool EmitExhaustion(std::uint64_t number) { return number == MaxEvents + 1; }
inline bool Capacity(std::size_t branch, std::size_t local) { return branch >= 28 && local >= 40; }

inline void EncodeForwarder(unsigned char (&out)[20], const unsigned char* prefix,
    std::uintptr_t continuation) {
    std::memcpy(out, prefix, 6);
    const unsigned char jump[6] = {0xFF,0x25,0,0,0,0};
    std::memcpy(out+6, jump, 6);
    const std::uint64_t target = continuation;
    std::memcpy(out+12, &target, sizeof(target));
}

// Diagnostic exceptions must not skip or duplicate original destruction.
// The original's own failures are deliberately NOT caught or fabricated away.
template<class Observer, class Original, class... Args>
auto ObserveThenForward(Observer observer, Original original, Args... args)
    -> decltype(original(args...)) {
    try { observer(); } catch (...) {}
    return original(args...);
}

bool Ready() noexcept; // Both live detours and forwarders must still match.
void Install(); // Experimental build; mandatory when admission is enabled.
}
