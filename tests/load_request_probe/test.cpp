// Actual probe control flow; mocks memory reader, engine target and logging.
// Does not prove engine cancellation, pointer layouts or hook installation.
#include <cstdint>
#include <atomic>
#include <cstring>
#include <cctype>
#include <iostream>
#include <stdexcept>
using UInt8 = std::uint8_t;
using UInt32 = std::uint32_t;
using UInt64 = std::uint64_t;
static char g_rejectLoadBasename[260]{};
static bool g_recoverRejectedMainLoad;
static thread_local bool g_rejectedRequestNeedsRecovery;
static std::atomic<UInt64> g_loadRequestGeneration{0};
static thread_local UInt64 g_rejectedRequestGeneration;
static unsigned originalFailures, recoveries;
static bool recoveryQueued;
static void NotifyOriginalLoadFailure() { ++originalFailures; }
static bool ScheduleRejectedMainLoadRecovery(UInt64 generation) {
    if (originalFailures != 1) throw std::runtime_error("recovery preceded native failure notification");
    if (generation != g_loadRequestGeneration.load()) throw std::runtime_error("wrong request generation");
    ++recoveries;
    return recoveryQueued;
}
static bool readable, targetResult;
static UInt64** expectedStream;
static UInt32 expected1, expected4;
static UInt8 expected2, expected3;
static unsigned calls;
static const char* sourceName;
static void require(bool pass) { if (!pass) throw std::runtime_error("probe contract failed"); }
static bool ReadLoadProbeName(UInt64** stream, char (&name)[260]) {
    require(stream == expectedStream);
    if (readable) std::strcpy(name,sourceName);
    return readable;
}
static int TestCaseCompare(const char* a, const char* b) {
    for (; *a && *b; ++a,++b) {
        int diff = std::tolower(static_cast<unsigned char>(*a)) - std::tolower(static_cast<unsigned char>(*b));
        if (diff) return diff;
    }
    return *a - *b;
}
template<class... T> void logMessage(T...) {}
#define _MESSAGE(...) logMessage(__VA_ARGS__)
#define _stricmp TestCaseCompare
#define CALL_MEMBER_FN(object, name) (object)->name
#define DEFINE_MEMBER_FN(name, result, address, ...) result name(__VA_ARGS__)
struct BGSSaveLoadManager {
#include "declarations.inc"
};
bool BGSSaveLoadManager::LoadRequestProbe_Target(UInt64** stream, UInt32 a, UInt8 b, UInt8 c, UInt32 d) {
    ++calls;
    require(stream == expectedStream && a == expected1 && b == expected2 && c == expected3 && d == expected4);
    return targetResult;
}
#include "production.inc"
int main() {
    BGSSaveLoadManager manager;
    UInt64 value = 0xDEADBEEF;
    UInt64* pointer = &value;
    expectedStream = &pointer;
    unsigned cases = 0;
    for (unsigned b=0;b<256;++b) for (unsigned c=0;c<256;++c) {
        for (unsigned mode=0;mode<8;++mode) for(unsigned recoveryMode=0;recoveryMode<3;++recoveryMode) {
            g_recoverRejectedMainLoad = recoveryMode != 0;
            recoveryQueued = recoveryMode == 2;
            // Entry must clear stale recovery eligibility even for forwarded loads.
            g_rejectedRequestNeedsRecovery = true;
            originalFailures = recoveries = 0;
            readable = mode != 3 && mode != 7;
            const bool configured = mode != 0 && mode != 4;
            const bool matches = mode == 2 || mode == 6;
            sourceName = matches ? "BAD.ESS" : "good.ess";
            std::strcpy(g_rejectLoadBasename,configured ? "bad.ess" : "");
            targetResult = mode >= 4;
            expected1 = 0xFEED0000u | b;
            expected4 = 0xC0DE0000u | c;
            expected2 = UInt8(b); expected3 = UInt8(c);
            calls = 0;
            const bool reject = configured && (!readable || matches);
            const bool result = manager.LoadRequestProbe_Hook(&pointer,expected1,expected2,expected3,expected4);
            require(result == (reject ? false : targetResult));
            require(calls == (reject ? 0u : 1u));
            require(pointer == &value && value == 0xDEADBEEF);
            require(g_rejectedRequestNeedsRecovery == (reject && g_recoverRejectedMainLoad));
            if (!result) {
                LoadRequestFailureRecovery_Hook();
                require(originalFailures == 1);
                require(recoveries == (reject && g_recoverRejectedMainLoad ? 1u : 0u));
                require(!g_rejectedRequestNeedsRecovery);
                originalFailures = recoveries = 0;
                // Eligibility is one-use; an unrelated second failure stays native.
                LoadRequestFailureRecovery_Hook();
                require(originalFailures == 1 && recoveries == 0);
            }
            ++cases;
        }
    }
    std::cout << cases << " production forwarding/rejection cases passed\n";
}
