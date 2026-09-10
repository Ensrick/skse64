// SPDX-License-Identifier: MIT
// Actual public serialization functions, synthetic validated/invalid snapshots.
#include "../../skse64/AdmittedSnapshot.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <string>
using UInt32=std::uint32_t;
using UInt64=std::uint64_t;
#include "fake-file.inc"
static unsigned checks;
static void Check(bool value) { ++checks; if (!value) throw std::runtime_error("API assertion failed"); }
#define ASSERT(value) Check(value)
AdmittedSnapshot::Stream<File> s_currentFile;
struct PluginHeader { UInt32 signature=0, numChunks=0, length=0; } s_pluginHeader;
struct ChunkHeader { UInt32 type=0, version=0, length=0; } s_chunkHeader;
UInt64 s_pluginHeaderOffset=0, s_chunkHeaderOffset=0;
bool s_chunkOpen=false;
bool OpenRecord(UInt32,UInt32);
bool WriteRecordData(const void*,UInt32);
#include "api.inc"
std::string s_savePath="synthetic-co-save";
unsigned deletes=0;
bool DeleteFile(const char*) { ++deletes; return true; }
#define _MESSAGE(...) ((void)0)
#define _ERROR(...) ((void)0)
#include "save-prefix.inc"
int main() {
    try {
        static_assert(sizeof(ChunkHeader)==12,"wire header size");
        auto make=[](UInt32 length) {
            auto owner=std::make_shared<std::vector<std::uint8_t>>(16,0);
            ChunkHeader header; header.type=123; header.version=2; header.length=length;
            std::memcpy(owner->data(),&header,12);
            (*owner)[12]=11; (*owner)[13]=22; (*owner)[14]=33; (*owner)[15]=44;
            AdmittedSnapshot::Bytes view; view.owner=owner; view.data=owner->data(); view.size=16;
            s_currentFile.Close(); Check(s_currentFile.Bind(view));
            s_pluginHeader.numChunks=1; s_chunkHeader={}; s_chunkOpen=false;
        };
        UInt32 type=0,version=0,length=0;
        make(4); Check(GetNextRecordInfo(&type,&version,&length));
        Check(type==123 && version==2 && length==4);
        unsigned char output[8]={99,99,99,99,99,99,99,99};
        Check(ReadRecordData(output,8)==4 && output[0]==11 && output[3]==44 && output[4]==99);
        Check(!s_currentFile.Fault() && !GetNextRecordInfo(&type,&version,&length));
        make(8); Check(GetNextRecordInfo(&type,&version,&length));
        std::memset(output,99,8); Check(ReadRecordData(output,8)==0);
        Check(output[0]==99 && output[7]==99 && s_chunkHeader.length==8 && s_currentFile.GetOffset()==12);
        type=111;version=222;length=333;
        Check(!GetNextRecordInfo(&type,&version,&length) && type==111 && version==222 && length==333);
        make(4); Check(ReadRecordData(output,1)==0 && !s_currentFile.Fault() && s_currentFile.Misuse()==1);
        make(4); Check(!OpenRecord(1,1) && s_pluginHeader.numChunks==1 && s_currentFile.GetOffset()==0);
        make(4); Check(!WriteRecordData(output,1) && s_currentFile.writes==0 && !s_currentFile.Fault());
        Check(GetNextRecordInfo(&type,&version,&length) && ReadRecordData(output,4)==4);
        make(4); Check(GetNextRecordInfo(&type,&version,&length));
        Check(ReadRecordData(nullptr,0)==0 && !s_currentFile.Fault() && s_chunkHeader.length==4);
        Check(ReadRecordData(nullptr,1)==0 && !s_currentFile.Fault() && s_chunkHeader.length==4 && s_currentFile.Misuse()==1);
        HandleSaveGlobalData(); Check(deletes==0 && s_currentFile.creates==0);
        s_currentFile.Close();
        HandleSaveGlobalData(); Check(deletes==1 && s_currentFile.creates==1);
        s_currentFile.Close();
        std::cout << checks << " actual serialization API checks passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
