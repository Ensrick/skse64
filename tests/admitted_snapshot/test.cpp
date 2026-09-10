// SPDX-License-Identifier: MIT
#include "../../skse64/AdmittedSnapshot.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
static unsigned checks;
static void Check(bool value) { ++checks; if (!value) throw std::runtime_error("snapshot assertion failed"); }
struct File {
    std::int64_t streamLength=0, streamOffset=0;
    unsigned opens=0, creates=0, closes=0, reads=0, writes=0, seeks=0;
    virtual ~File() {}
    bool Open(const char*) { ++opens; streamLength=8; streamOffset=0; return true; }
    bool Create(const char*) { ++creates; return true; }
    void Close() { ++closes; }
    virtual void ReadBuf(void*, std::uint32_t size) { ++reads; streamOffset+=size; }
    virtual void WriteBuf(const void*, std::uint32_t) { ++writes; }
    virtual void SetOffset(std::int64_t offset) { ++seeks; streamOffset=offset; }
    virtual void Skip(std::int64_t delta) { SetOffset(streamOffset+delta); }
    std::int64_t GetRemain() { return streamLength-streamOffset; }
    std::int64_t GetOffset() { return streamOffset; }
    unsigned char Read8() { unsigned char value=0; ReadBuf(&value,1); return value; }
    unsigned char Peek8() { auto old=streamOffset; auto value=Read8(); SetOffset(old); return value; }
};
int main() {
    try {
        using AdmittedSnapshot::Bytes;
        using AdmittedSnapshot::Stream;
        auto owner=std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{1,2,3,4});
        std::weak_ptr<std::vector<std::uint8_t>> weak=owner;
        Bytes bytes; bytes.owner=owner; bytes.data=owner->data(); bytes.size=owner->size();
        Stream<File> stream;
        Check(!stream.Bind({}));
        auto bad=bytes; bad.data=nullptr; Check(!stream.Bind(bad));
        bad=bytes; bad.size=0; Check(!stream.Bind(bad));
        bad=bytes; bad.size=AdmittedSnapshot::MaximumBytes+1; Check(!stream.Bind(bad));
        Check(stream.Bind(bytes) && stream.Bound() && stream.GetRemain()==4);
        Check(!stream.Bind(bytes) && !stream.Open("x") && !stream.Create("x"));
        Check(stream.opens==0 && stream.creates==0);
        Check(stream.Peek8()==1 && stream.GetRemain()==4);
        Check(stream.Read8()==1 && stream.GetRemain()==3);
        stream.Skip(2); Check(stream.Read8()==4 && stream.GetRemain()==0);
        stream.ReadBuf(nullptr,0); Check(stream.GetRemain()==0);
        for(unsigned kind=0;kind<6;++kind) {
            stream.Close(); Check(stream.Bind(bytes)); unsigned char output=99;
            switch(kind) {
            case 0: stream.ReadBuf(&output,5); break;
            case 1: stream.SetOffset(-1); break;
            case 2: stream.SetOffset(5); break;
            case 3: stream.Skip(std::numeric_limits<std::int64_t>::min()); break;
            case 4: stream.Skip(std::numeric_limits<std::int64_t>::max()); break;
            case 5: stream.ReadBuf(nullptr,1); break;
            }
            Check(stream.Fault()!=0 && stream.GetRemain()==4 && output==99);
            auto first=stream.Fault(); stream.ReadBuf(&output,1); stream.SetOffset(1); stream.Skip(1);
            Check(stream.Fault()==first && stream.GetRemain()==4 && output==99);
            stream.Peek8(); Check(stream.Fault()==first && stream.GetRemain()==4);
        }
        Check(stream.reads==0 && stream.writes==0 && stream.seeks==0);
        stream.Close(); Check(stream.Bind(bytes));
        unsigned char input=0; stream.WriteBuf(&input,1);
        Check(!stream.Fault() && stream.Misuse()==1 && stream.Read8()==1);
        bad={}; bytes={}; owner.reset(); Check(!weak.expired());
        stream.Close(); Check(weak.expired() && !stream.Bound() && stream.GetRemain()==0);
        unsigned char out=0;
        Check(stream.Open("file")); stream.ReadBuf(&out,1); stream.SetOffset(2); stream.Skip(1); stream.WriteBuf(&out,1);
        Check(stream.reads==1 && stream.writes==1 && stream.seeks==2 && stream.streamOffset==3);
        auto second=std::make_shared<std::vector<std::uint8_t>>(4,7);
        Bytes next; next.owner=second; next.data=second->data(); next.size=second->size();
        Check(!stream.Bind(next)); stream.Close(); Check(stream.closes==1);
        Check(stream.Create("file")); stream.Close(); Check(stream.creates==1 && stream.closes==2);
        stream.Close(); Check(stream.closes==2);
        std::weak_ptr<std::vector<std::uint8_t>> destroyed;
        {
            Stream<File> local;
            auto temporary=std::make_shared<std::vector<std::uint8_t>>(2,8); destroyed=temporary;
            Bytes owned; owned.owner=temporary; owned.data=temporary->data(); owned.size=2;
            Check(local.Bind(owned));
        }
        Check(destroyed.expired());
        std::cout << checks << " production snapshot adapter checks passed\n";
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
