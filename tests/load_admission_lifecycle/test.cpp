// SPDX-License-Identifier: MIT
// Actual source lifecycle functions with synthetic engine ownership/read state.
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#define _MESSAGE(...) ((void)0)
static unsigned checks, closes;
static void Check(bool value) { ++checks; if (!value) throw std::runtime_error("lifecycle assertion failed"); }
struct Context {
    void* stream;
    std::string basename;
    void* lease;
    ~Context() { ++closes; }
};
static std::unique_ptr<Context> pending;
static std::mutex gate;
struct Span { const char* pointer; size_t bytes; };
static std::vector<Span> readable;
static bool Read(const void* p, void* output, size_t bytes) {
    const auto address = reinterpret_cast<std::uintptr_t>(p);
    for (const auto& span : readable) {
        const auto start = reinterpret_cast<std::uintptr_t>(span.pointer);
        if (address >= start && address-start <= span.bytes && bytes <= span.bytes-(address-start)) {
            std::memcpy(output, p, bytes); return true;
        }
    }
    return false;
}
static unsigned ensrick_admission_lease_matches_handle(void* lease, void* handle) { return lease && lease == handle; }
#include "lifecycle.inc"

int main() {
    try {
        char stream[0xBB0+sizeof(void*)] = {};
        std::string name = "TestSave.ess";
        const char* text = name.c_str();
        std::memcpy(stream+0xBB0, &text, sizeof(text));
        readable.push_back({stream, sizeof(stream)});
        readable.push_back({name.c_str(), name.size()+1});
        char other[8] = {};
        const auto create = [&]() { pending.reset(new Context{stream, "TestSave.ess", stream}); };
        Check(!OwnsStream(stream));
        create(); Check(OwnsStream(stream)); Check(!OwnsStream(other));
        Check(MatchesCoSave(stream)); Check(!MatchesCoSave(other)); Check(!MatchesCoSave(nullptr));
        name[0] = 'X'; Check(!OwnsStream(stream)); name[0] = 'T';
        readable.pop_back(); Check(!OwnsStream(stream)); readable.push_back({name.c_str(), name.size()+1});
        Finish(other); Check(bool(pending));
        Finish(stream); Check(!pending && closes == 1);
        Finish(stream); Check(closes == 1);
        for (bool result : {false, true}) {
            const auto before = closes;
            create(); RequestReturned(stream, stream, result);
            Check(!pending && closes == before+1); // caller destroys terminal stream
            create(); RequestReturned(stream, nullptr, result);
            Check(bool(pending)); // transferred callback owns stream
            Finish(stream); Check(!pending && closes == before+2);
            create(); RequestReturned(stream, other, result);
            Check(bool(pending)); // unknown replacement never releases original
            RequestReturned(other, other, result); Check(bool(pending));
            Finish(stream); Check(!pending && closes == before+3);
            RequestReturned(stream, stream, result); Check(closes == before+3);
        }
        std::cout << checks << " actual lifecycle checks passed\n";
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
