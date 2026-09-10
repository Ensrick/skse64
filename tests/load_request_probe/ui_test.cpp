// Actual factory/string/queue helper and deferred delegate, mocked engine API.
#include <atomic>
#include <cstdint>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
using UInt64=std::uint64_t;
static std::atomic<UInt64> g_loadRequestGeneration{1};
static unsigned messages, releases;
static bool haveMenus,haveQueue,mainOpen,journalOpen,factoryWorks;
static void require(bool v){if(!v)throw std::runtime_error("UI recovery contract");}
struct BSFixedString {
    std::string value;
    BSFixedString()=default;
    explicit BSFixedString(const char* v):value(v){}
    void Release(){value.clear();++releases;}
    void Set_ref(const BSFixedString& other){value=other.value;}
};
struct BSUIMessageData {BSFixedString unk18;};
struct UIDelegate_v1 {virtual void Run()=0;virtual void Dispose()=0;};
static UIDelegate_v1* pending=nullptr;
struct MenuManager {
    static MenuManager* GetSingleton(){static MenuManager m;return haveMenus?&m:nullptr;}
    bool IsMenuOpen(BSFixedString* n){return n->value=="Main Menu"?mainOpen:journalOpen;}
};
struct UIManager {
    static UIManager* GetSingleton(){static UIManager q;return haveQueue?&q:nullptr;}
    void AddMessage(BSFixedString* n,unsigned type,void* ptr){
        require(n->value=="Main Menu"&&type==0);
        require(static_cast<BSUIMessageData*>(ptr)->unk18.value=="CancelLoading");
        ++messages;
    }
    void QueueCommand(UIDelegate_v1* task){require(!pending);pending=task;}
};
static void* CreateUIMessageData(BSFixedString* type){
    require(type->value=="BSUIMessageData");static BSUIMessageData data;
    return factoryWorks?&data:nullptr;
}
#define CALL_MEMBER_FN(object,method) (object)->method
template<class... T>void logMessage(T...){}
#define _MESSAGE(...) logMessage(__VA_ARGS__)
#include "ui-recovery.inc"
int main(){
    unsigned cases=0;
    for(unsigned mask=0;mask<32;++mask){
        haveMenus=bool(mask&1);haveQueue=bool(mask&2);mainOpen=bool(mask&4);
        journalOpen=bool(mask&8);factoryWorks=bool(mask&16);
        messages=releases=0;
        const bool eligible=haveMenus&&haveQueue&&mainOpen&&!journalOpen;
        require(QueueRejectedMainLoadRecovery()==(eligible&&factoryWorks));
        require(messages==(eligible&&factoryWorks?1u:0u));
        require(releases==(eligible?4u:2u));
        ++cases;
    }
    haveMenus=haveQueue=mainOpen=factoryWorks=true;journalOpen=false;
    for(bool current:{false,true}){
        messages=0;g_loadRequestGeneration=7;
        require(ScheduleRejectedMainLoadRecovery(7));
        require(pending&&messages==0); // Deferred, not immediate native enqueue.
        if(!current)g_loadRequestGeneration=8;
        auto* task=pending;pending=nullptr;
        task->Run();task->Dispose();
        require(messages==(current?1u:0u));
        ++cases;
    }
    haveQueue=false;
    require(!ScheduleRejectedMainLoadRecovery(8)&&!pending);
    std::cout<<++cases<<" production UI helper/deferred generation cases passed\n";
}
