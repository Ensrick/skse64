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
#include <atomic>
#include <thread>
#include "../../skse64/LoadAdmissionRuntime.h"
using LoadAdmissionRuntime::PendingObservation;
using LoadAdmissionRuntime::TrackedGate;
using LoadAdmissionRuntime::RequestToken;
#define _MESSAGE(...) ((void)0)
static unsigned checks, closes;
static unsigned leaseCloses;
static void CheckAt(bool value, unsigned line) { ++checks; if (!value) throw std::runtime_error("lifecycle assertion failed at line " + std::to_string(line)); }
#define Check(value) CheckAt(value, __LINE__)
struct Context {
    void* stream;
    std::string basename;
    std::shared_ptr<void> lease;
    std::uint64_t generation;
    AdmittedSnapshot::Bytes snapshot;
    bool innerAcquired;
    ~Context() { ++closes; }
};
static std::unique_ptr<Context> pending;
static std::mutex gate;
static thread_local bool gateOwned = false;
static std::atomic<std::uint64_t> events{0};
struct Span { const char* pointer; size_t bytes; };
static std::vector<Span> readable;
struct RelocationManager { static std::uintptr_t s_baseAddr; };
std::uintptr_t RelocationManager::s_baseAddr = 0;
static bool throwRead = false;
static bool Read(const void* p, void* output, size_t bytes) {
    if (throwRead) throw std::runtime_error("synthetic reader failure");
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
namespace LoadAdmissionRuntime {
#include "lifecycle.inc"
}
using namespace LoadAdmissionRuntime;

int main() {
    try {
        std::vector<unsigned char> image(0x1BFA00+5, 0);
        RelocationManager::s_baseAddr = reinterpret_cast<std::uintptr_t>(image.data());
        auto* patched = image.data()+0x1BFA00;
        const unsigned char signature[] = {0x48,0x31,0xC0,0xC3,0xCC};
        std::memcpy(patched, signature, sizeof(signature));
        Check(!HasSuppressedAchievementPrompt()); // unreadable, not zero-filled success
        readable.push_back({reinterpret_cast<const char*>(patched), 5});
        Check(HasSuppressedAchievementPrompt());
        for (size_t i=0;i<5;++i) {
            patched[i] ^= 1;
            Check(!HasSuppressedAchievementPrompt());
            patched[i] ^= 1;
        }
        const unsigned char native[] = {0x48,0x83,0xEC,0x28,0xC6};
        std::memcpy(patched, native, sizeof(native));
        Check(!HasSuppressedAchievementPrompt());
        std::memcpy(patched, signature, sizeof(signature));
        throwRead=true; Check(!HasSuppressedAchievementPrompt()); throwRead=false;
        readable.pop_back();
        char stream[0xBB0+sizeof(void*)] = {};
        std::string name = "TestSave.ess";
        const char* text = name.c_str();
        std::memcpy(stream+0xBB0, &text, sizeof(text));
        readable.push_back({stream, sizeof(stream)});
        readable.push_back({name.c_str(), name.size()+1});
        char other[8] = {};
        std::uint64_t serial=100;
        const auto create = [&]() {
            pending.reset(new Context{stream, "TestSave.ess", std::shared_ptr<void>(stream, [](void*){++leaseCloses;}), ++serial, {}, false});
            pending->snapshot.owner=pending->lease;
            pending->snapshot.data=reinterpret_cast<const std::uint8_t*>(stream);
            pending->snapshot.size=sizeof(stream);
            return RequestToken(stream, pending->generation);
        };
        Check(!AcquireInner(stream));
        const auto empty = ObservePending(stream);
        Check(empty.acquired && !empty.present && !empty.matched && empty.generation == 0);
        create(); Check(bool(AcquireInner(stream))); Check(!AcquireInner(other));
        Check(!AcquireInner(stream)); // one consumer per generation
        pending->innerAcquired=false; // fresh synthetic acquisition state for name checks below
        pending->generation = 42;
        const auto matched = ObservePending(stream);
        Check(matched.acquired && matched.present && matched.matched && matched.generation == 42);
        Check(!ObservePending(other).matched && ObservePending(other).present);
        {
            TrackedGate owner(gate, gateOwned);
            Check(gateOwned && !ObservePending(stream).acquired && closes == 0);
        }
        Check(!gateOwned && ObservePending(stream).matched);
        std::atomic<bool> locked{false}, release{false};
        std::thread holder([&] { std::lock_guard<std::mutex> hold(gate); locked.store(true); while (!release.load()) std::this_thread::yield(); });
        while (!locked.load()) std::this_thread::yield();
        const auto busy = ObservePending(stream);
        release.store(true); holder.join();
        Check(!busy.acquired && bool(pending) && closes == 0);
        Check(empty.sequence > 0 && matched.sequence > empty.sequence);
        Check(busy.sequence == 0);
        const auto sequence = NextEvent();
        Check(NextEvent() == sequence + 1);
        Check(MatchesCoSave(stream)); Check(!MatchesCoSave(other)); Check(!MatchesCoSave(nullptr));
        name[0] = 'X'; Check(!AcquireInner(stream)); name[0] = 'T';
        readable.pop_back(); Check(!AcquireInner(stream)); readable.push_back({name.c_str(), name.size()+1});
        Finish(RequestToken(other,42)); Check(bool(pending));
        const auto first=AcquireInner(stream).token;
        Finish(first); Check(!pending && closes == 1);
        Finish(first); Check(closes == 1);
        for (bool result : {false, true}) {
            const auto before = closes;
            auto request=create(); RequestReturned(request, stream, result);
            Check(!pending && closes == before+1); // caller destroys terminal stream
            request=create(); RequestReturned(request, nullptr, result);
            Check(bool(pending)); // transferred callback owns stream
            Finish(request); Check(!pending && closes == before+2);
            request=create(); RequestReturned(request, other, result);
            Check(bool(pending)); // unknown replacement never releases original
            RequestReturned(RequestToken(other,request.generation), other, result); Check(bool(pending));
            Finish(request); Check(!pending && closes == before+3);
            RequestReturned(request, stream, result); Check(closes == before+3);
        }
        Check(!AcquireInner(stream).snapshot.owner);
        const auto heldToken=create(); Check(!AcquireInner(other).snapshot.owner);
        auto held=AcquireInner(stream).snapshot;
        Check(held.owner && held.data==reinterpret_cast<const std::uint8_t*>(stream) && held.size==sizeof(stream));
        const auto beforeLease=leaseCloses;
        Finish(heldToken); Check(!pending && leaseCloses==beforeLease);
        held={}; Check(leaseCloses==beforeLease+1);
        for(bool result : {false,true}) for(void* caller : {static_cast<void*>(stream), static_cast<void*>(other), static_cast<void*>(nullptr)}) {
            const auto old=create();
            Finish(old);
            const auto newer=create(); // same address and basename, different generation
            const auto beforeClose=closes;
            RequestReturned(old, caller, result);
            Check(bool(pending) && pending->generation==newer.generation && closes==beforeClose);
            RequestReturned(RequestToken(stream,0), caller, result);
            Check(bool(pending) && closes==beforeClose);
            RequestReturned(RequestToken(), caller, result);
            Check(bool(pending) && closes==beforeClose);
            RequestReturned(newer, stream, result);
            Check(!pending && closes==beforeClose+1);
        }
        create();
        auto oldInner=AcquireInner(stream);
        Finish(oldInner.token);
        create();
        const auto newInner=AcquireInner(stream);
        Finish(oldInner.token); // stale inner cleanup must not release new context
        Check(pending && pending->generation==newInner.token.generation);
        Finish(RequestToken(stream,0)); Check(bool(pending));
        Finish(RequestToken(nullptr,newInner.token.generation)); Check(bool(pending));
        Finish(RequestToken(other,newInner.token.generation)); Check(bool(pending));
        Check(oldInner.snapshot.owner && newInner.snapshot.owner &&
              (oldInner.snapshot.owner.owner_before(newInner.snapshot.owner) ||
               newInner.snapshot.owner.owner_before(oldInner.snapshot.owner)));
        Check(oldInner.token.generation!=newInner.token.generation);
        const auto beforeOldRelease=leaseCloses;
        oldInner={}; Check(leaseCloses==beforeOldRelease+1 && bool(pending));
        Check(!AcquireInner(stream));
        pending->innerAcquired=false; // independently exercise each rejected-input guard
        throwRead=true; Check(!AcquireInner(stream)); throwRead=false;
        Check(!pending->innerAcquired);
        const auto goodGeneration=pending->generation;
        pending->generation=0; Check(!AcquireInner(stream)); pending->generation=goodGeneration;
        const auto goodSnapshot=pending->snapshot;
        pending->snapshot.owner.reset(); Check(!AcquireInner(stream)); pending->snapshot=goodSnapshot;
        pending->snapshot.data=nullptr; Check(!AcquireInner(stream)); pending->snapshot=goodSnapshot;
        pending->snapshot.size=0; Check(!AcquireInner(stream)); pending->snapshot=goodSnapshot;
        pending->snapshot.size=AdmittedSnapshot::MaximumBytes+1; Check(!AcquireInner(stream)); pending->snapshot=goodSnapshot;
        Check(!pending->innerAcquired);
        Check(bool(AcquireInner(stream)) && pending->innerAcquired);
        Check(!AcquireInner(stream));
        Finish(newInner.token); Check(!pending);
        std::cout << checks << " actual lifecycle checks passed\n";
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
