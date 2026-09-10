// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <type_traits>

namespace AdmittedSnapshot {
// owner retains the lease, not just its address. No allocation when copied.
struct Bytes {
    std::shared_ptr<const void> owner;
    const std::uint8_t* data = nullptr;
    std::uint64_t size = 0;
};
constexpr std::uint64_t MaximumBytes = 256u * 1024u * 1024u;
template<class> struct BufferSize;
template<class C, class A, class B> struct BufferSize<void(C::*)(A,B)> { using Type = B; };

// File is IFileStream in production. Its inherited scalar readers, Peek and
// GetRemain dispatch through these methods/use the same protected cursor.
template<class File> class Stream : public File {
    // Windows common::UInt32 is unsigned long, not std::uint32_t. Preserve
    // the actual virtual signature; equal width is not equal C++ type.
    using Size = typename BufferSize<decltype(&File::ReadBuf)>::Type;
    static_assert(sizeof(Size)==4 && std::is_unsigned<Size>::value, "32-bit unsigned stream count required");
    Bytes snapshot;
    bool fileOpen = false;
    unsigned fault = 0;
    unsigned misuse = 0;
public:
    Stream() = default;
    ~Stream() { Close(); }
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;
    bool Bound() const noexcept { return bool(snapshot.owner); }
    unsigned Fault() const noexcept { return fault; }
    unsigned Misuse() const noexcept { return misuse; }
    void RejectWriteOrInvalidCall() noexcept { if (misuse != UINT32_MAX) ++misuse; }
    void Fail(unsigned code) noexcept { if (!fault) fault = code; }
    bool Bind(Bytes bytes) noexcept {
        if (Bound() || fileOpen || !bytes.owner || !bytes.data || !bytes.size || bytes.size > MaximumBytes) return false;
        snapshot = std::move(bytes);
        fault = 0;
        misuse = 0;
        this->streamLength = static_cast<std::int64_t>(snapshot.size);
        this->streamOffset = 0;
        return true;
    }
    bool Open(const char* path) {
        if (Bound()) return false;
        fileOpen = File::Open(path);
        return fileOpen;
    }
    bool Create(const char* path) {
        if (Bound()) return false;
        fileOpen = File::Create(path);
        return fileOpen;
    }
    void Close() noexcept {
        snapshot = {};
        if (fileOpen) File::Close();
        fileOpen = false;
        fault = 0;
        misuse = 0;
        this->streamLength = this->streamOffset = 0;
    }
    void ReadBuf(void* output, Size length) override {
        if (!Bound()) { File::ReadBuf(output, length); return; }
        if (fault) return;
        if ((!output && length) || this->streamOffset < 0 ||
            static_cast<std::uint64_t>(this->streamOffset) > snapshot.size ||
            length > snapshot.size-static_cast<std::uint64_t>(this->streamOffset))
            { Fail(1); return; }
        if (length) std::memcpy(output, snapshot.data+this->streamOffset, length);
        this->streamOffset += length;
    }
    void SetOffset(std::int64_t offset) override {
        if (!Bound()) { File::SetOffset(offset); return; }
        if (fault) return;
        if (offset < 0 || static_cast<std::uint64_t>(offset) > snapshot.size)
            { Fail(2); return; }
        this->streamOffset = offset;
    }
    void Skip(std::int64_t delta) override {
        if (!Bound()) { File::Skip(delta); return; }
        if (fault) return;
        if (delta < -this->streamOffset || delta > this->streamLength-this->streamOffset)
            { Fail(2); return; }
        SetOffset(this->streamOffset+delta);
    }
    void WriteBuf(const void* input, Size length) override {
        if (Bound()) { RejectWriteOrInvalidCall(); return; }
        File::WriteBuf(input, length);
    }
};
}
