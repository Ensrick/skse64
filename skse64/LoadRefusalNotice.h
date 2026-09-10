// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstring>

namespace LoadRefusal {
// Values1..9 match the admission ABI statuses. Extra adapter reasons are local.
constexpr unsigned UnsupportedStack=10, PreviousPending=11, AdapterFailure=12, DiagnosticVeto=13;
struct Notice {
    unsigned code;
    explicit constexpr Notice(unsigned value=0) : code(value) {}
};
inline const char* Text(Notice notice) {
    switch(notice.code) {
    case 0: return nullptr;
    case 2: return "Save not loaded.\n\nThe currency system is not ready to verify this save.\n\nYour save was not modified. See skse64.log for details.";
    case 3: return "Save not loaded.\n\nThe save data could not be validated.\n\nYour save was not modified. See skse64.log for details.";
    case 4: return "Save not loaded.\n\nThe active plugin list could not be validated.\n\nYour save was not modified. See skse64.log for details.";
    case 5: return "Save not loaded.\n\nThis save requires plugins that are missing or have incompatible full/light flags.\n\nYour save was not modified. See skse64.log for the plugin details.";
    case 6: return "Save not loaded.\n\nThe companion SKSE save file is missing, unreadable, or currently in use.\n\nYour save was not modified. See skse64.log for details.";
    case 7: return "Save not loaded.\n\nThe SKSE save data or currency checkpoint is incompatible with this setup.\n\nYour save was not modified. See skse64.log for details.";
    case 8: return "Save not loaded.\n\nThere was not enough memory to validate this save safely.\n\nYour save was not modified. See skse64.log for details.";
    case UnsupportedStack: return "Save not loaded.\n\nA required Engine Fixes compatibility setting is not active.\n\nYour save was not modified. See skse64.log for details.";
    case PreviousPending: return "Save not loaded.\n\nA previous load request has not finished or released its resources.\n\nYour save was not modified. See skse64.log for details.";
    case DiagnosticVeto: return "Save not loaded.\n\nThis load was deliberately refused by the diagnostic test configuration.\n\nYour save was not modified.";
    default: return "Save not loaded.\n\nCompatibility could not be verified safely.\n\nYour save was not modified. See skse64.log for details.";
    }
}

constexpr std::uintptr_t CreateRva=0x9625F0; // pinned1.7.104, AE ID52269
constexpr unsigned char CreatePrefix[]={0x44,0x89,0x4C,0x24,0x20,0x44,0x88,0x44,0x24,0x18,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xF1,0x48,0x81,0xEC,0xB8,0,0,0};
inline bool MatchesPrefix(const unsigned char* bytes, std::size_t size) {
    return bytes && size==sizeof(CreatePrefix) && !std::memcmp(bytes,CreatePrefix,size);
}
template<class Caller> bool QueueWith(Notice notice, bool supported, Caller call) {
    const auto* text=Text(notice);
    if(!text || !supported) return false;
    // Five named arguments then a null-terminated variadic button list.
    return call(text, static_cast<void(*)(std::uint8_t)>(nullptr), std::uint8_t(0),
        std::int32_t(4), std::int32_t(10), static_cast<const char*>("OK"), static_cast<const char*>(nullptr));
}
bool Queue(Notice notice); // Engine UI queue, never a Windows desktop dialog.
}
