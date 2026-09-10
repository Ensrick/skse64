// Compile the actual load hook/declarations, mocking engine collaborators only.
// This tests forwarding and hook housekeeping, not the engine ABI/layout.
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using UInt8 = std::uint8_t;
using UInt32 = std::uint32_t;
using UInt64 = std::uint64_t;
static void require(bool value) { if (!value) throw std::runtime_error("contract failed"); }
static std::vector<std::string> events;
static int depth;
static bool expectedResult, probe;
static UInt8 expectedByte;
static UInt32 expected1, expected2;
static UInt64* expectedBuffer;
static void* expectedPointer;
static std::string expectedName;
static UInt32 clearEnabled;
static int cleared;
struct Lock {
    void Enter() { require(depth++ == 0); events.push_back("enter"); }
    void Leave() { require(--depth == 0); events.push_back("leave"); }
} g_loadGameLock;
namespace Serialization {
    void SetSaveName(const char* name) {
        require(depth == 1);
        if (name) { require(name == expectedName); events.push_back("set-name"); }
        else events.push_back("clear-name");
    }
}
struct SKSEMessagingInterface { enum { kMessage_PreLoadGame, kMessage_PostLoadGame }; };
namespace PluginManager {
    void Dispatch_Message(int, int message, void* payload, std::size_t length, void*) {
        require(depth == 1);
        if (message == SKSEMessagingInterface::kMessage_PreLoadGame) {
            require(expectedName == static_cast<const char*>(payload));
            require(length == expectedName.size());
            events.push_back("pre");
        } else {
            require(reinterpret_cast<std::uintptr_t>(payload) == unsigned(expectedResult));
            require(length == 1);
            events.push_back("post");
        }
    }
}
static unsigned GetEnvironmentVariableA(const char*, char* buffer, unsigned size) {
    require(size == 2); buffer[0] = probe ? '1' : 0; return probe ? 1 : 0;
}
static bool GetConfigOption_UInt32(const char*, const char*, UInt32* value) {
    *value = clearEnabled; return true;
}
struct VM { UInt32 ClearInvalidRegistrations() { require(depth == 0); ++cleared; return 0; } } vm;
static VM* vmPointer = &vm;
static VM** g_skyrimVM = &vmPointer;
template<class... T> void logMessage(T...) {}
#define _MESSAGE(...) logMessage(__VA_ARGS__)
#define CALL_MEMBER_FN(object, name) (object)->name
#define DEFINE_MEMBER_FN(name, result, address, ...) result name(__VA_ARGS__)
struct BGSSaveLoadManager {
#include "production_declarations.inc"
};
bool BGSSaveLoadManager::LoadGame_HookTarget(UInt64* buffer, UInt32 a, UInt32 b, void* pointer, UInt8 byte) {
    require(depth == 1);
    require(buffer == expectedBuffer && a == expected1 && b == expected2);
    require(pointer == expectedPointer && byte == expectedByte);
    events.push_back("target");
    return expectedResult;
}
#include "production_load.inc"
int main() {
    BGSSaveLoadManager manager;
    std::array<UInt64, 0xBB8 / 8> buffer{};
    expectedBuffer = buffer.data();
    int sentinel = 0;
    expectedPointer = &sentinel;
    unsigned cases = 0;
    for (unsigned byte = 0; byte < 256; ++byte) {
        for (unsigned bits = 0; bits < 8; ++bits) {
            expectedByte = UInt8(byte);
            expected1 = 0xAB000000u | byte;
            expected2 = 0xFEDC0000u | (byte << 8);
            expectedResult = bits & 1;
            probe = bits & 2;
            clearEnabled = (bits >> 2) & 1;
            expectedName = (byte & 1) ? "test_save" : "";
            buffer[0xBB0 / 8] = (byte & 1) ? reinterpret_cast<UInt64>(expectedName.c_str()) : 0;
            events.clear(); cleared = 0;
            require(manager.LoadGame_Hook(buffer.data(), expected1, expected2, expectedPointer, expectedByte) == expectedResult);
            require(depth == 0 && cleared == int(clearEnabled));
            require(events == std::vector<std::string>{"enter", "set-name", "pre", "target", "post", "clear-name", "leave"});
            ++cases;
        }
    }
    std::cout << cases << " production forwarding/message/lock cases passed\n";
}
