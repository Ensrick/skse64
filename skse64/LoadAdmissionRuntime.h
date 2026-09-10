// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace LoadAdmissionRuntime {
bool Enabled();
bool Begin(std::uint64_t** stream);
bool OwnsStream(void* stream);
bool MatchesCoSave(void* handle);
void Finish(void* stream);
void RequestReturned(void* admittedStream, void* callerStream, bool result);
}
