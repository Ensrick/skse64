// Production orchestration with a mock trampoline. Not a code-write/ABI test.
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
using std::uintptr_t;
static void require(bool value) { if (!value) throw std::runtime_error("installation contract"); }
struct Trampoline {
    unsigned capacity = 0;
    bool failureWorks = false, requestWorks = false;
    std::vector<uintptr_t> calls;
    unsigned Remain() const { return capacity; }
    bool Write5Call(uintptr_t site, uintptr_t target) {
        require(capacity >= 14);
        capacity -= 14;
        calls.push_back(site);
        require((site == 2 && target == 4) || (site == 1 && target == 3));
        return site == 2 ? failureWorks : requestWorks;
    }
} g_branchTrampoline;
template<class... T> void logMessage(T...) {}
#define _MESSAGE(...) logMessage(__VA_ARGS__)
#include "install-probe.inc"
int main() {
    unsigned cases = 0;
    for (unsigned capacity : {0u, 13u, 14u, 27u, 28u, 64u})
    for (bool recover : {false, true})
    for (bool failureWorks : {false, true})
    for (bool requestWorks : {false, true}) {
        g_branchTrampoline = {capacity, failureWorks, requestWorks, {}};
        const bool space = capacity >= (recover ? 28u : 14u);
        require(InstallLoadRequestProbe(1, 2, 3, 4, recover)
            == (space && (!recover || failureWorks) && requestWorks));
        std::vector<uintptr_t> expected;
        if (space) {
            if (recover) expected.push_back(2);
            if (!recover || failureWorks) expected.push_back(1);
        }
        require(g_branchTrampoline.calls == expected);
        ++cases;
    }
    std::cout << cases << " production paired-installation cases passed\n";
}
