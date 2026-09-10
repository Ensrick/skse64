// SPDX-License-Identifier: MIT
#include <stdexcept>
#include <iostream>
// Release must never consult a possibly absent or hostile automation variable.
static unsigned GetEnvironmentVariableA(const char*,char*,unsigned) {
    throw std::runtime_error("release queried diagnostic activation environment");
}
#include "enabled.inc"
int main() {
    try {
        for(unsigned i=0;i<100;++i) if(!Enabled()) return 1;
        std::cout<<"100 release Enabled calls passed without environment access\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
