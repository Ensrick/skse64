#include "../../skse64/PapyrusMemberGuard.h"
#include <map>
#include <cstring>
#include <iostream>
#include <cstdlib>
using namespace PapyrusMemberGuard;
struct Memory {
    std::map<std::uintptr_t, unsigned char> bytes;
    template<class T> void put(std::uintptr_t address, T value) {
        unsigned char raw[sizeof(T)]; std::memcpy(raw, &value, sizeof(value));
        for (std::size_t i=0;i<sizeof(T);++i) bytes[address+i]=raw[i];
    }
    template<class T> bool operator()(std::uintptr_t address, T& value) const {
        unsigned char raw[sizeof(T)];
        for (std::size_t i=0;i<sizeof(T);++i) {
            auto found=bytes.find(address+i); if(found==bytes.end()) return false;
            raw[i]=found->second;
        }
        std::memcpy(&value,raw,sizeof(T)); return true;
    }
};
unsigned checks=0;
void check(bool ok) { ++checks; if(!ok){std::cerr<<"failed check "<<checks<<'\n'; std::exit(1);} }
Memory fixture(unsigned variables=0, unsigned encoded=7) {
    Memory m;
    m.put<std::uint32_t>(0x100,7); m.put<std::uint32_t>(0x104,encoded);
    m.put<std::uintptr_t>(0x230,0x300);
    m.put<std::uint64_t>(0x328,0x800);
    m.put<std::uintptr_t>(0x330,0x400);
    m.put<std::uintptr_t>(0x408,0x500);
    m.put<std::uint32_t>(0x520,(variables<<8)|2);
    m.put<std::uintptr_t>(0x510,0);
    return m;
}
int main() {
    auto evaluate=[](const Memory& m){return Evaluate(0x200,0x100,m);};
    auto bad=evaluate(fixture());
    check(bad.known && bad.reject && bad.index==5 && bad.count==0 && bad.object==0x400 && bad.type==0x500);
    auto good=evaluate(fixture(25)); check(good.known && !good.reject && good.count==25);
    for(unsigned count: {0u,1u,31u,1023u}) {
        check(evaluate(fixture(count,count+2)).reject);
        if(count) check(!evaluate(fixture(count,count+1)).reject);
    }
    for(unsigned kind=0;kind<7;++kind) {
        auto m=fixture();m.put<std::uint32_t>(0x100,kind);check(!evaluate(m).known);
    }
    for(unsigned index: {0u,1u}) check(!evaluate(fixture(0,index)).known);
    for(std::uint64_t type: {0ull,2ull,3ull,5ull,11ull,12ull,15ull,0x801ull}) {
        auto m=fixture();m.put<std::uint64_t>(0x328,type);check(!evaluate(m).known);
    }
    for(std::uint64_t type: {1ull,0x800ull}) {
        auto m=fixture();m.put<std::uint64_t>(0x328,type);check(evaluate(m).reject);
    }
    for(unsigned state: {0u,1u,2u,3u}) {
        auto m=fixture();m.put<std::uint32_t>(0x520,state);check(evaluate(m).known==(state>=2));
    }
    auto parent=fixture(1);parent.put<std::uintptr_t>(0x510,0x600);
    parent.put<std::uint32_t>(0x620,(24<<8)|3);parent.put<std::uintptr_t>(0x610,0);
    check(evaluate(parent).known && !evaluate(parent).reject && evaluate(parent).count==25);
    parent.put<std::uintptr_t>(0x610,0x500);check(!evaluate(parent).known);
    auto cycle=fixture();cycle.put<std::uintptr_t>(0x510,0x500);check(!evaluate(cycle).known);
    for(std::uintptr_t missing: {0x100u,0x104u,0x230u,0x328u,0x330u,0x408u,0x510u,0x520u}) {
        auto m=fixture();m.bytes.erase(missing);check(!evaluate(m).known);
    }
    for(std::uintptr_t pointer: {0x230u,0x330u,0x408u}) {
        auto m=fixture();m.put<std::uintptr_t>(pointer,0);check(!evaluate(m).known);
    }
    auto chain=fixture();
    for(unsigned i=0;i<64;++i) {
        std::uintptr_t type=0x500+i*0x100;
        chain.put<std::uint32_t>(type+0x20,(1023<<8)|3);
        chain.put<std::uintptr_t>(type+0x10,i==63?0:type+0x100);
    }
    check(evaluate(chain).known && evaluate(chain).count==65472);
    chain.put<std::uintptr_t>(0x4410,0x4500);check(!evaluate(chain).known);
    check(!Evaluate(0,0x100,fixture()).known);
    check(!Evaluate(0x200,0,fixture()).known);
    std::cout<<checks<<" member guard checks passed\n";
}
