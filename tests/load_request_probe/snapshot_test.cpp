// Synthetic private files only. Compiles the actual read-only observer.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <array>
#include <string>
#include <iostream>
#include <stdexcept>
#include "LoadPluginSnapshot.h"
using UInt8=std::uint8_t;
using UInt32=std::uint32_t;
using UInt64=std::uint64_t;
struct RelocationManager { inline static std::uintptr_t s_baseAddr=0x140000000; };
// Actual engine layout is asserted by the full plugin build. This synthetic
// observer test deliberately supplies no loaded table and tests the independent
// snapshot failure is contained without obstructing the image comparison.
#define STATIC_ASSERT static_assert
struct DataHandler { char padding[0xD60]; int modList; };
struct ModInfo { char padding[0x438]; UInt32 fileFlags; char pad2[0x478-0x43C]; UInt8 modIndex; UInt8 pad3; std::uint16_t lightIndex; };
struct HandlerPointer { DataHandler* value=nullptr; DataHandler** GetPtr(){return &value;} } g_dataHandler;
std::string lastLog;
template<typename... Args> void logMessage(const char* format, Args... args) {
    char buf[2048]; std::snprintf(buf,sizeof(buf),format,args...); lastLog=buf;
}
#define _MESSAGE logMessage
#include "snapshot.inc"
unsigned checks=0;
void require(bool ok) { ++checks; if(!ok) throw std::runtime_error("snapshot check failed: "+lastLog); }
int main() {
    char temp[MAX_PATH], path[MAX_PATH];
    require(GetTempPathA(MAX_PATH,temp)>0);
    require(GetTempFileNameA(temp,"lds",0,path)!=0);
    struct Cleanup { const char* path; ~Cleanup() { DeleteFileA(path); SetEnvironmentVariableA("SKSE_AUTOMATION_LOAD_STREAM_PROBE",nullptr); } } cleanup{path};
    std::array<unsigned char,9001> contents{};
    for(size_t i=0;i<contents.size();++i) contents[i]=static_cast<unsigned char>(i*13);
    HANDLE file=CreateFileA(path,GENERIC_WRITE,0,nullptr,TRUNCATE_EXISTING,0,nullptr);
    require(file!=INVALID_HANDLE_VALUE);
    DWORD wrote=0; bool written=WriteFile(file,contents.data(),DWORD(contents.size()),&wrote,nullptr); CloseHandle(file);
    require(written && wrote==contents.size());
    alignas(8) std::array<unsigned char,0xBE8> storage{};
    auto put=[&](size_t at,auto value){ std::memcpy(storage.data()+at,&value,sizeof(value)); };
    put(0,UInt64(RelocationManager::s_baseAddr+0x01B521A0));
    put(0xBD0,reinterpret_cast<UInt64>(contents.data())); put(0x174,UInt32(contents.size()));
    put(0xBE0,UInt32(53)); std::memcpy(storage.data()+0x64,path,std::strlen(path)+1);
    UInt64* stream=reinterpret_cast<UInt64*>(storage.data());
    auto before=storage; auto original=contents;
    SetEnvironmentVariableA("SKSE_AUTOMATION_LOAD_STREAM_PROBE",nullptr);
    ObserveLoadStreamSnapshot(reinterpret_cast<UInt64**>(1)); require(lastLog.empty());
    SetEnvironmentVariableA("SKSE_AUTOMATION_LOAD_STREAM_PROBE","1");
    ObserveLoadStreamSnapshot(&stream); require(lastLog.find("compared=9001 exact_equal=1")!=std::string::npos);
    require(storage==before && contents==original);
    ObserveLoadStreamSnapshot(nullptr); require(lastLog.find("layout_match=0")!=std::string::npos);
    put(0,UInt64(0)); ObserveLoadStreamSnapshot(&stream); require(lastLog.find("layout_match=0")!=std::string::npos); storage=before;
    put(0x174,UInt32(0)); ObserveLoadStreamSnapshot(&stream); require(lastLog.find("fields_valid=0")!=std::string::npos); storage=before;
    put(0x174,UInt32(0x4000001)); ObserveLoadStreamSnapshot(&stream); require(lastLog.find("fields_valid=0")!=std::string::npos); storage=before;
    put(0xBCA,UInt8(1)); ObserveLoadStreamSnapshot(&stream); require(lastLog.find("fields_valid=0")!=std::string::npos); storage=before;
    contents[6000]^=1; ObserveLoadStreamSnapshot(&stream); require(lastLog.find("compared=4096 exact_equal=0")!=std::string::npos); contents=original;
    put(0xBD0,UInt64(1)); ObserveLoadStreamSnapshot(&stream); require(lastLog.find("exact_equal=0")!=std::string::npos); storage=before;
    put(0x174,UInt32(9000)); ObserveLoadStreamSnapshot(&stream); require(lastLog.find("exact_equal=0")!=std::string::npos); storage=before;
    file=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr); require(file!=INVALID_HANDLE_VALUE);
    ObserveLoadStreamSnapshot(&stream); require(lastLog.find("file_open=0")!=std::string::npos); CloseHandle(file);
    file=CreateFileA(path,GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr); require(file!=INVALID_HANDLE_VALUE); CloseHandle(file);
    require(storage==before && contents==original);
    std::cout<<checks<<" snapshot checks passed\n";
}
