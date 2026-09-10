// SPDX-License-Identifier: MIT
// Actual detour verifier with bounded synthetic memory, not executable hooks.
#include "../../skse64/LoadStreamLifetime.h"
#include <iostream>
#include <stdexcept>
using namespace LoadStreamLifetime;
static unsigned checks;
static unsigned char memory[256];
static bool unreadable=false;
static bool Read(const void* p,void* out,std::size_t n) {
    const auto at=reinterpret_cast<std::uintptr_t>(p),base=reinterpret_cast<std::uintptr_t>(memory);
    if(unreadable || at<base || at-base>sizeof(memory) || n>sizeof(memory)-(at-base)) return false;
    std::memcpy(out,p,n);return true;
}
#include "verify.inc"
static void Check(bool value){++checks;if(!value) throw std::runtime_error("detour readback assertion");}
template<std::size_t N> void Case(const unsigned char(&prefix)[N],bool backwards) {
    std::memset(memory,0,sizeof(memory));
    auto* entry=memory+(backwards?80:0);
    auto* stub=memory+(backwards?0:80);
    auto* forward=memory+160;
    const auto e=reinterpret_cast<std::uintptr_t>(entry),s=reinterpret_cast<std::uintptr_t>(stub),f=reinterpret_cast<std::uintptr_t>(forward);
    const std::uint64_t hook=0x7FF710203040;
    std::memcpy(entry,prefix,N);entry[0]=0xE9;
    const auto distance=static_cast<std::int32_t>(s-e-5);
    std::memcpy(entry+1,&distance,4);
    const unsigned char jump[]={0xFF,0x25,0,0,0,0};
    std::memcpy(stub,jump,6);std::memcpy(stub+6,&hook,8);
    EncodeForwarder(*reinterpret_cast<unsigned char(*)[20]>(forward),prefix,e+6);
    Check(Verify(e,hook,f,prefix));
    unreadable=true;Check(!Verify(e,hook,f,prefix));unreadable=false;
    for(std::size_t i=0;i<N;++i){entry[i]^=1;Check(!Verify(e,hook,f,prefix));entry[i]^=1;}
    for(std::size_t i=0;i<14;++i){stub[i]^=1;Check(!Verify(e,hook,f,prefix));stub[i]^=1;}
    for(std::size_t i=0;i<20;++i){forward[i]^=1;Check(!Verify(e,hook,f,prefix));forward[i]^=1;}
    Check(!Verify(e,hook+1,f,prefix));Check(!Verify(e,hook,0,prefix));
    Check(Verify(e,hook,f,prefix));
}
int main(){try{Case(ScalarPrefix,false);Case(ScalarPrefix,true);Case(NondeletingPrefix,false);Case(NondeletingPrefix,true);
std::cout<<checks<<" live detour verification checks passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
