#pragma once
// Original diagnostic guard. No save or script data is rewritten.
#include <cstdint>
#include <cstddef>

namespace PapyrusMemberGuard {
struct Evaluation {
    bool known = false;
    bool reject = false;
    std::uint32_t index = 0;
    std::uint32_t count = 0;
    std::uintptr_t object = 0;
    std::uintptr_t type = 0;
};

// Reader supplies typed, non-mutating reads; native callers own object lifetime.
// Unknown/invalid metadata preserves the original behavior, not a new policy.
template<class Reader>
Evaluation Evaluate(std::uintptr_t tasklet, std::uintptr_t operand, Reader read) {
    Evaluation result;
    std::uint32_t kind = 0, encoded = 0;
    if (!operand || !read(operand, kind) || kind != 7 ||
        !read(operand + 4, encoded) || encoded < 2) return result;
    result.index = encoded - 2;
    std::uintptr_t frame = 0;
    std::uint64_t selfType = 0;
    if (!tasklet || !read(tasklet + 0x30, frame) || !frame ||
        !read(frame + 0x28, selfType)) return result;
    const std::uint32_t rawType = static_cast<std::uint32_t>(selfType);
    if (rawType != 1 && (rawType < 16 || (rawType & 1))) return result;
    if (!read(frame + 0x30, result.object) || !result.object ||
        !read(result.object + 8, result.type) || !result.type) return result;
    std::uintptr_t seen[64] = {};
    std::size_t depth = 0;
    for (auto type = result.type; type;) {
        if (depth == 64) return result;
        for (std::size_t i = 0; i < depth; ++i) if (seen[i] == type) return result;
        seen[depth++] = type;
        std::uint32_t flags = 0;
        if (!read(type + 0x20, flags) || (flags & 3) < 2) return result;
        result.count += (flags >> 8) & 0x3FF;
        if (!read(type + 0x10, type)) return result;
    }
    result.known = true;
    result.reject = result.index >= result.count;
    return result;
}
void Install();
}
