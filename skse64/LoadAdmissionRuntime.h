// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <mutex>
#include "AdmittedSnapshot.h"
#include "LoadRefusalNotice.h"
namespace LoadAdmissionRuntime {
// Diagnostic reentry must not try_lock a std::mutex this thread already owns.
class TrackedGate {
    std::unique_lock<std::mutex> lock;
    bool& owned;
public:
    TrackedGate(std::mutex& mutex, bool& marker) : lock(mutex), owned(marker) { owned = true; }
    ~TrackedGate() { lock.unlock(); owned = false; }
    TrackedGate(const TrackedGate&) = delete;
    TrackedGate& operator=(const TrackedGate&) = delete;
};
bool Enabled();
// Current modpack prerequisite only. Does not rule out other error callbacks.
bool HasSuppressedAchievementPrompt() noexcept;
// Captured under the context gate by Begin, before entering native code.
// A raw address alone cannot identify an old request after allocator reuse.
struct RequestToken {
    void* stream;
    std::uint64_t generation;
    RequestToken(void* value = nullptr, std::uint64_t serial = 0) : stream(value), generation(serial) {}
};
bool Begin(std::uint64_t** stream, RequestToken& admitted, LoadRefusal::Notice& refused);
struct InnerAdmission {
    RequestToken token;
    AdmittedSnapshot::Bytes snapshot;
    explicit operator bool() const noexcept { return token.stream && token.generation && snapshot.owner; }
};
// Single-consumer acquisition captures name, generation and owned bytes atomically.
// This does not prove allocation identity before acquisition after a deferred
// cancellation: native lifetime coverage remains a production prerequisite.
InnerAdmission AcquireInner(void* stream) noexcept;
bool MatchesCoSave(void* handle);
void Finish(const RequestToken& token);
void RequestReturned(const RequestToken& admitted, void* callerStream, bool result);
struct PendingObservation {
    bool acquired = false;
    bool present = false;
    bool matched = false;
    std::uint64_t generation = 0;
    std::uint64_t sequence = 0;
};
PendingObservation ObservePending(void* stream) noexcept;
std::uint64_t NextEvent() noexcept;
}
