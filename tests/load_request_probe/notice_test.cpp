// Pure policy and exact native call shape. No engine/Windows UI invoked.
#include "../../skse64/LoadRefusalNotice.h"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace LoadRefusal;
static unsigned checks,calls;
static bool result;
static void Check(bool v){++checks;if(!v)throw std::runtime_error("notice contract");}
static bool Native(const char* text,void(*callback)(std::uint8_t),std::uint8_t offset,
    std::int32_t warning,std::int32_t depth,const char* button,const char* sentinel) {
    ++calls; Check(text && !callback && !offset && warning==4 && depth==10);
    Check(std::string(button)=="OK" && !sentinel); return result;
}
int main() try {
    Check(!Text(Notice()));
    for(unsigned code=1;code<=13;++code) {
        const std::string text=Text(Notice(code));
        Check(text.size()<512 && text.find("Save not loaded.")==0);
        Check(text.find("Your save was not modified.")!=std::string::npos);
        Check(text.find('<')==std::string::npos && text.find('>')==std::string::npos);
    }
    Check(std::string(Text(Notice(999)))==Text(Notice(AdapterFailure)));
    unsigned char bytes[sizeof(CreatePrefix)]; std::memcpy(bytes,CreatePrefix,sizeof(bytes));
    Check(MatchesPrefix(bytes,sizeof(bytes)));
    Check(!MatchesPrefix(nullptr,sizeof(bytes)) && !MatchesPrefix(bytes,sizeof(bytes)-1));
    for(unsigned i=0;i<sizeof(bytes);++i) {bytes[i]^=1;Check(!MatchesPrefix(bytes,sizeof(bytes)));bytes[i]^=1;}
    for(unsigned code:{0u,6u,999u}) for(bool ready:{false,true}) for(bool accepted:{false,true}) {
        calls=0;result=accepted;
        Check(QueueWith(Notice(code),ready,Native)==(code && ready && accepted));
        Check(calls==unsigned(code && ready));
    }
    std::cout << checks << " refusal text/signature/call-contract checks passed\n";
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
