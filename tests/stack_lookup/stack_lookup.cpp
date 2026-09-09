#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
using UInt32=std::uint32_t;
struct VMStackInfo {};
// Preserve the obsolete declared SDK shape so reintroducing its pointer chain
// compiles but fails this test. Runtime data is an opaque direct Stack pointer.
struct OldData1 { std::uint32_t a; void* b; std::uint32_t c; VMStackInfo* stackInfo; };
struct OldData2 { std::uint32_t a; void* b; std::uint32_t c,d; OldData1* unkData; };
struct VMStackTableItem { UInt32 stackId; OldData2* data; };
static void require(bool yes) { if(!yes) throw std::runtime_error("stack lookup contract failure"); }
struct Lock {
  unsigned depth=0, enters=0, leaves=0;
  void LockEnter() { ++depth; ++enters; }
  void Release() { require(depth>0); --depth; ++leaves; }
};
struct TestLock : Lock { void Lock() { LockEnter(); } };
struct Map {
  TestLock* lock;
  VMStackTableItem* entry=nullptr;
  VMStackTableItem* Find(UInt32* id) {
    require(lock->depth>=1);
    return entry && entry->stackId==*id ? entry : nullptr;
  }
};
class VMClassRegistry {
public:
  TestLock stackLock;
  Map allStacks{&stackLock};
  VMStackInfo* GetStackInfo(UInt32);
};
#include "production_lookup.inc"
int main() {
  try {
    VMClassRegistry vm;
    require(vm.GetStackInfo(42)==nullptr);
    VMStackTableItem entry{42,nullptr};
    vm.allStacks.entry=&entry;
    require(vm.GetStackInfo(42)==nullptr);
    alignas(8) std::array<std::byte,0xA0> bytes{};
    entry.data=reinterpret_cast<OldData2*>(bytes.data());
    unsigned cases=0;
    for(std::uint64_t n=0;n<32768;++n) for(auto flag:{0ull,0x80000000ull}) {
      bytes.fill(std::byte{0xA5});
      auto capacity=n|flag;
      std::memcpy(bytes.data()+0x18,&capacity,8);
      auto before=bytes;
      require(vm.GetStackInfo(42)==reinterpret_cast<VMStackInfo*>(bytes.data()));
      require(vm.GetStackInfo(43)==nullptr);
      require(bytes==before && vm.stackLock.depth==0 && vm.stackLock.enters==vm.stackLock.leaves);
      ++cases;
    }
	vm.stackLock.Lock();
	require(vm.GetStackInfo(42)==reinterpret_cast<VMStackInfo*>(bytes.data()));
	require(vm.stackLock.depth==1);
	vm.stackLock.Release();
	require(vm.stackLock.depth==0 && vm.stackLock.enters==vm.stackLock.leaves);
    std::printf("PASS: %u production direct-stack cases; missing/null entries; locked lookup; balanced unlock; no pointee writes\n",cases);
    return 0;
  } catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
