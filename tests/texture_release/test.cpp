// SPDX-License-Identifier: MIT
#include "../../skse64/TextureReleaseFix.h"
#include <cstdio>
#include <cstdlib>

static unsigned checks = 0;
void Check(bool value) { ++checks; if (!value) std::abort(); }
int main() {
    using namespace TextureReleaseFix;
    Check(Matches(Original, sizeof(Original)));
    Check(!Matches(nullptr, sizeof(Original)));
    Check(!Matches(Original, sizeof(Original)-1));
    Check(!Matches(Original, sizeof(Original)+1));
    unsigned char patched[sizeof(Original)];
    std::memcpy(patched, Original, sizeof(patched));
    std::memcpy(patched + PatchOffset, Patch, sizeof(Patch));
    Check(!Matches(patched, sizeof(patched)));
    for (std::size_t i = 0; i < sizeof(Original); ++i) {
        unsigned char changed[sizeof(Original)];
        std::memcpy(changed, Original, sizeof(changed));
        changed[i] ^= 1;
        Check(!Matches(changed, sizeof(changed)));
    }
    Check(Original[PatchOffset] == 0x48 && Original[PatchOffset+1] == 0x8B && Original[PatchOffset+2] == 0x0B);
    Check(Patch[0] == 0xEB && Patch[2] == 0x90);
    Check(FunctionRva + PatchOffset + 2 + Patch[1] == 0x100F20B);
    Check(Original[0x7B] == 0xBA && Original[0x7C] == 0x28);
    Check(!ProbeSample(0));
    Check(ProbeSample(1) && ProbeSample(8) && ProbeSample(16));
    Check(!ProbeSample(9) && !ProbeSample(15) && !ProbeSample(17));
    Check(ProbeSample(std::uint64_t(1) << 63));
    Check(!ProbeSample(~std::uint64_t(0)));
    std::printf("%u texture release signature/branch checks passed\n", checks);
}
