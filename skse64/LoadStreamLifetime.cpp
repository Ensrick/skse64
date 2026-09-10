// SPDX-License-Identifier: MIT
// Opt-in observation and mandatory pre-destruction retirement for admission.
#include "LoadStreamLifetime.h"
#include "LoadAdmissionRuntime.h"
#include "skse64_common/Relocation.h"
#include "skse64_common/BranchTrampoline.h"
#include "skse64_common/Utilities.h"
#include <Windows.h>
#include <atomic>

namespace LoadStreamLifetime {
namespace {
using ScalarFn = void*(*)(void*, unsigned);
using NondeletingFn = void*(*)(void*);
ScalarFn originalScalar = nullptr;
NondeletingFn originalNondeleting = nullptr;
std::atomic<std::uint64_t> observations{0};
std::atomic<bool> installedPair{false};
bool observationEnabled=false;

bool Read(const void* address, void* out, std::size_t size) {
    SIZE_T got = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), address, out, size, &got) && got == size;
}

void ObserveCpp(void* stream, unsigned flags, const char* site) {
    if(!observationEnabled) return;
    if (observations.load(std::memory_order_relaxed) > MaxEvents) return;
    const auto number = observations.fetch_add(1, std::memory_order_relaxed) + 1;
    if (EmitExhaustion(number)) {
        _MESSAGE("LOAD_STREAM_DESTROY diagnostic_only=1 exhausted=1 maximum=%u further_events_omitted=1", MaxEvents);
    }
    if (!EmitEvent(number)) return;
    const auto state = LoadAdmissionRuntime::ObservePending(stream);
    std::uintptr_t vtable = 0;
    const bool readable = Read(stream, &vtable, sizeof(vtable));
    _MESSAGE("LOAD_STREAM_DESTROY diagnostic_only=1 seq=%llu sample=%llu site=%s stream=%016llX flags=%u vtable_readable=%u vtable_admitted=%u gate=%u pending=%d matched=%d pending_generation=%llu thread=%u identity=pointer_only",
        state.acquired ? state.sequence : LoadAdmissionRuntime::NextEvent(), number, site,
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(stream)), flags,
        unsigned(readable), unsigned(readable && vtable == RelocationManager::s_baseAddr+0x1B521A0),
        unsigned(state.acquired), state.acquired ? int(state.present) : -1,
        state.acquired ? int(state.matched) : -1, state.generation, GetCurrentThreadId());
}

// Separate raw SEH boundary from C++ unwinding. Both kinds of diagnostic
// failure are contained. Original native destruction remains outside it.
void ObserveCppNoThrow(void* stream, unsigned flags, const char* site) noexcept {
    try { ObserveCpp(stream, flags, site); } catch (...) {}
}
void ObserveSafe(void* stream, unsigned flags, const char* site) noexcept {
    const DWORD previousError = GetLastError();
    __try { ObserveCppNoThrow(stream, flags, site); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    SetLastError(previousError);
}
void* ScalarHook(void* self, unsigned flags) {
    return ObserveThenForward([=] {
        const DWORD previousError=GetLastError();
        ObserveSafe(self, flags, "scalar_159D3E0");
        if(installedPair.load() && LoadAdmissionRuntime::Enabled()) LoadAdmissionRuntime::StreamDestroying(self);
        SetLastError(previousError);
    }, originalScalar, self, flags);
}
void* NondeletingHook(void* self) {
    return ObserveThenForward([=] {
        const DWORD previousError=GetLastError();
        ObserveSafe(self, 0, "nondeleting_159D320");
        if(installedPair.load() && LoadAdmissionRuntime::Enabled()) LoadAdmissionRuntime::StreamDestroying(self);
        SetLastError(previousError);
    }, originalNondeleting, self);
}
void* Forwarder(std::uintptr_t entry) {
    unsigned char bytes[20];
    EncodeForwarder(bytes, reinterpret_cast<const unsigned char*>(entry), entry+6);
    auto* target = g_localTrampoline.Allocate(sizeof(bytes));
    std::memcpy(target, bytes, sizeof(bytes));
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(bytes));
    return target;
}

template<std::size_t N>
bool Verify(std::uintptr_t entry,std::uintptr_t hook,std::uintptr_t forwarder,const unsigned char(&prefix)[N]) {
    unsigned char current[N]={},stub[14]={},forward[20]={},expectedForward[20]={};
    if(!Read(reinterpret_cast<void*>(entry),current,N) || current[0]!=0xE9 ||
        std::memcmp(current+5,prefix+5,N-5)) return false;
    std::int32_t offset=0;
    std::memcpy(&offset,current+1,4);
    const auto destination=entry+5+static_cast<std::intptr_t>(offset);
    if(!Read(reinterpret_cast<void*>(destination),stub,sizeof(stub))) return false;
    const unsigned char jump[]={0xFF,0x25,0,0,0,0};
    std::uint64_t target=0;
    std::memcpy(&target,stub+6,8);
    if(std::memcmp(stub,jump,6) || target!=hook) return false;
    EncodeForwarder(expectedForward,prefix,entry+6);
    return Read(reinterpret_cast<void*>(forwarder),forward,sizeof(forward)) &&
        !std::memcmp(forward,expectedForward,sizeof(forward));
}
}

bool Ready() noexcept {
    if(!installedPair.load()) return false;
    try {
        return Verify(RelocationManager::s_baseAddr+ScalarRva,reinterpret_cast<std::uintptr_t>(ScalarHook),
            reinterpret_cast<std::uintptr_t>(originalScalar),ScalarPrefix) &&
            Verify(RelocationManager::s_baseAddr+NondeletingRva,reinterpret_cast<std::uintptr_t>(NondeletingHook),
            reinterpret_cast<std::uintptr_t>(originalNondeleting),NondeletingPrefix);
    } catch(...) { return false; }
}

void Install() {
    char enabled[2] = {};
    observationEnabled=GetEnvironmentVariableA("SKSE_AUTOMATION_LOAD_STREAM_LIFETIME_PROBE", enabled, sizeof(enabled)) == 1 && enabled[0] == '1';
    if(!observationEnabled && !LoadAdmissionRuntime::Enabled()) return;
    const auto scalar = RelocationManager::s_baseAddr+ScalarRva;
    const auto nondeleting = RelocationManager::s_baseAddr+NondeletingRva;
    unsigned char a[sizeof(ScalarPrefix)] = {}, b[sizeof(NondeletingPrefix)] = {};
    if (!Read(reinterpret_cast<const void*>(scalar), a, sizeof(a)) ||
        !Read(reinterpret_cast<const void*>(nondeleting), b, sizeof(b)) ||
        std::memcmp(a, ScalarPrefix, sizeof(a)) || std::memcmp(b, NondeletingPrefix, sizeof(b)) ||
        !Capacity(g_branchTrampoline.Remain(), g_localTrampoline.Remain())) {
        _MESSAGE("LOAD_STREAM_LIFETIME_PROBE not_installed=1 reason=signature_or_capacity");
        return;
    }
    originalScalar = reinterpret_cast<ScalarFn>(Forwarder(scalar));
    originalNondeleting = reinterpret_cast<NondeletingFn>(Forwarder(nondeleting));
    const bool scalarSet = g_branchTrampoline.Write5Branch(scalar, reinterpret_cast<std::uintptr_t>(ScalarHook));
    const bool nondeletingSet = g_branchTrampoline.Write5Branch(nondeleting, reinterpret_cast<std::uintptr_t>(NondeletingHook));
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(scalar), 6);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(nondeleting), 6);
    installedPair.store(scalarSet && nondeletingSet);
    const bool verified=Ready();
    installedPair.store(verified);
    _MESSAGE("LOAD_STREAM_LIFETIME_PROBE observation=%u admission_cleanup=%u installed_scalar=%u installed_nondeleting=%u complete_pair=%u base_hooked=0 max_events=%u",
        unsigned(observationEnabled),unsigned(LoadAdmissionRuntime::Enabled() && verified),
        unsigned(scalarSet), unsigned(nondeletingSet), unsigned(verified), MaxEvents);
}
}
