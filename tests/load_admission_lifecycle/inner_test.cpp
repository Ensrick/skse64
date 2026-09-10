// SPDX-License-Identifier: MIT
// Actual inner hook and Prepare/Close functions; synthetic engine/reader state.
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include "../../skse64/LoadAdmissionRuntime.h"
using UInt8=std::uint8_t;
using UInt32=std::uint32_t;
using UInt64=std::uint64_t;
static unsigned checks=0, nativeCalls=0, preloads=0, postloads=0, closes=0;
static bool enabled=true, nativeResult=true, allowAcquire=true;
static unsigned nested=0;
static std::string saveName;
static bool consumed=false;
static UInt64* expectedStream;
static std::uint64_t generation=42, finished=0;
static void CheckAt(bool value, unsigned line) { ++checks; if(!value) throw std::runtime_error("inner assertion line "+std::to_string(line)); }
#define Check(value) CheckAt(value,__LINE__)
#define _MESSAGE(...) ((void)0)
#define CALL_MEMBER_FN(object,name) (object)->name
static std::string injectedName;
static unsigned GetEnvironmentVariableA(const char* key,char* out,unsigned capacity) {
    if(std::strcmp(key,"SKSE_AUTOMATION_REJECT_INNER_ONCE") || injectedName.empty()) return 0;
    if(injectedName.size()>=capacity) return unsigned(injectedName.size()+1);
    std::memcpy(out,injectedName.c_str(),injectedName.size()+1);
    return unsigned(injectedName.size());
}
static bool GetConfigOption_UInt32(const char*,const char*,UInt32*) { return false; }
struct VM { UInt32 ClearInvalidRegistrations() { throw std::runtime_error("unexpected VM mutation"); } };
static VM* vm=nullptr;
static VM** g_skyrimVM=&vm;
struct Lock { unsigned depth=0; void Enter(){++depth;} void Leave(){Check(depth>0);--depth;} } g_loadGameLock;
struct Reader {
    AdmittedSnapshot::Bytes bytes;
    bool Bind(AdmittedSnapshot::Bytes b) { if(bytes.owner || !b.owner) return false; bytes=std::move(b); return true; }
    void Close(){Check(bool(bytes.owner));bytes={};++closes;}
    std::uint64_t GetLength() const { return bytes.size; }
    std::uint64_t GetOffset() const { return 0; }
    unsigned Fault() const { return 0; }
    unsigned Misuse() const { return 0; }
};
namespace Serialization {
static Reader s_currentFile;
#include "prepare.inc"
void SetSaveName(const char* value) { saveName=value?value:""; }
}
namespace LoadAdmissionRuntime {
bool Enabled(){return enabled;}
InnerAdmission AcquireInner(void* stream) noexcept {
    InnerAdmission result;
    if(!allowAcquire || consumed) return result;
    consumed=true;
    result.token=RequestToken(stream,generation);
    result.snapshot.owner=std::shared_ptr<const void>(stream,[](const void*){});
    result.snapshot.data=reinterpret_cast<const std::uint8_t*>(stream);
    result.snapshot.size=8;
    return result;
}
void InnerReturned(const RequestToken& token,bool){if(token.generation) {Check(token.stream==expectedStream);finished=token.generation;}}
}
namespace SKSEMessagingInterface { enum { kMessage_PreLoadGame, kMessage_PostLoadGame }; }
namespace PluginManager {
void Dispatch_Message(int,int kind,void*,std::size_t,void*) {
    if(kind==SKSEMessagingInterface::kMessage_PreLoadGame) ++preloads;
    else ++postloads;
}
}
struct BGSSaveLoadManager {
    bool LoadGame_Hook(UInt64*,UInt32,UInt32,void*,UInt8);
    bool LoadGame_HookTarget(UInt64* stream,UInt32 a,UInt32 b,void* c,UInt8 d) {
        ++nativeCalls;
        Check(stream==expectedStream && a==0xABCDEF12 && b==0x1234ABCD && c==this && d==0xA5);
        Check(saveName=="Outer.ess" && g_loadGameLock.depth==1);
        if(enabled) Check(Serialization::s_admittedFilePrepared && Serialization::s_currentFile.bytes.owner);
        if(nested) {
            const auto oldName=saveName;
            const auto oldOwner=Serialization::s_currentFile.bytes.owner;
            const auto oldClose=closes;
            const auto oldPre=preloads;
            const auto oldFinish=finished;
            UInt64 inner[0xBB0/8+1]={}; inner[0xBB0/8]=reinterpret_cast<UInt64>("Nested.ess");
            if(nested==2) {consumed=false;++generation;} // another admitted context, reader still occupied
            auto* prior=expectedStream; expectedStream=inner;
            Check(!LoadGame_Hook(inner,1,2,nullptr,3));
            expectedStream=prior;
            Check(saveName==oldName && closes==oldClose && preloads==oldPre);
            Check(Serialization::s_currentFile.bytes.owner==oldOwner && Serialization::s_admittedFilePrepared);
            Check(g_loadGameLock.depth==1);
            if(nested==1) Check(finished==oldFinish); // duplicate must not finish its parent's context
            else Check(finished==43);
        }
        return nativeResult;
    }
};
#include "inner-hook.inc"
int main() {
    try {
        BGSSaveLoadManager manager;
        UInt64 stream[0xBB0/8+1]={}; stream[0xBB0/8]=reinterpret_cast<UInt64>("Outer.ess");
        expectedStream=stream;
        for(bool active:{false,true}) for(bool result:{false,true}) for(unsigned reentry:{0u,1u,2u}) {
            if(!active && reentry) continue;
            enabled=active;nativeResult=result;nested=reentry;consumed=false;generation=42;finished=0;
            nativeCalls=preloads=postloads=closes=0;saveName="";
            Check(manager.LoadGame_Hook(stream,0xABCDEF12,0x1234ABCD,&manager,0xA5)==result);
            Check(nativeCalls==1 && preloads==1 && postloads==1 && g_loadGameLock.depth==0);
            Check(closes==(active?1u:0u) && saveName.empty() && !Serialization::s_admittedFilePrepared);
            Check(!Serialization::s_currentFile.bytes.owner && finished==(active?42u:0u));
        }
        enabled=true;allowAcquire=false;saveName="Preserved.ess";finished=0;
        nativeCalls=preloads=postloads=0;
        Check(!manager.LoadGame_Hook(stream,1,2,nullptr,3));
        Check(saveName=="Preserved.ess" && !nativeCalls && !preloads && !postloads && !finished && !g_loadGameLock.depth);
        allowAcquire=true;nested=0;nativeResult=true;
        for(const auto& invalid: {std::string("Different.ess"),std::string("../Outer.ess"),std::string(260,'x')}) {
            injectedName=invalid;consumed=false;generation=42;finished=0;
            Check(manager.LoadGame_Hook(stream,0xABCDEF12,0x1234ABCD,&manager,0xA5));
        }
        injectedName="Outer.ess";consumed=false;finished=0;saveName="Preserved.ess";
        nativeCalls=preloads=postloads=closes=0;
        Check(!manager.LoadGame_Hook(stream,1,2,nullptr,3));
        Check(!nativeCalls && !preloads && !postloads && !closes && finished==42);
        Check(saveName=="Preserved.ess" && !Serialization::s_admittedFilePrepared && !g_loadGameLock.depth);
        consumed=false;finished=0;
        Check(manager.LoadGame_Hook(stream,0xABCDEF12,0x1234ABCD,&manager,0xA5));
        Check(nativeCalls==1 && preloads==1 && postloads==1 && closes==1 && finished==42);
        std::cout<<checks<<" actual inner-hook checks passed\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
