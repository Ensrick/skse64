// SPDX-License-Identifier: MIT
#include "../../skse64/LoadStreamLifetime.h"
#include <iostream>
#include <stdexcept>
using namespace LoadStreamLifetime;
static unsigned checks;
static void Check(bool result) { ++checks; if (!result) throw std::runtime_error("observer assertion failed"); }
int main() {
    static_assert(sizeof(std::uintptr_t) == 8, "64-bit target required");
    try {
        Check(!Capacity(27,40)); Check(!Capacity(28,39)); Check(Capacity(28,40));
        Check(!EmitEvent(0));
        for (unsigned i=1; i<=MaxEvents; ++i) Check(EmitEvent(i) && !EmitExhaustion(i));
        Check(!EmitEvent(129) && EmitExhaustion(129));
        Check(!EmitEvent(130) && !EmitExhaustion(130));
        Check(!EmitEvent(UINT64_MAX) && !EmitExhaustion(UINT64_MAX));
        const unsigned char expectedJump[] = {0xFF,0x25,0,0,0,0};
        for (const auto prefix : {ScalarPrefix, NondeletingPrefix}) {
            unsigned char output[20] = {};
            const std::uint64_t address = 0x7FF712345678;
            EncodeForwarder(output, prefix, address);
            Check(!std::memcmp(output, prefix, 6));
            Check(!std::memcmp(output+6, expectedJump, 6));
            std::uint64_t target = 0; std::memcpy(&target, output+12, 8);
            Check(target == address);
        }
        unsigned observed=0, forwarded=0;
        const auto original = [&](void* self, unsigned flags) { ++forwarded; Check(flags==0xF001); return self; };
        int object = 0;
        auto result = ObserveThenForward([&] { ++observed; }, original, &object, 0xF001u);
        Check(result == &object && observed == 1 && forwarded == 1);
        result = ObserveThenForward([&] { ++observed; throw std::runtime_error("diagnostic failure"); }, original, &object, 0xF001u);
        Check(result == &object && observed == 2 && forwarded == 2);
        const auto single = [&](void* self) { ++forwarded; return self; };
        Check(ObserveThenForward([] {}, single, &object) == &object && forwarded == 3);
        bool propagated = false;
        try { ObserveThenForward([] {}, []()->int { throw std::runtime_error("original failure"); }); }
        catch (const std::runtime_error&) { propagated = true; }
        Check(propagated);
        std::cout << checks << " bounded observer checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
