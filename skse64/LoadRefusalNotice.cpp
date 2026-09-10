// SPDX-License-Identifier: MIT
#include "LoadRefusalNotice.h"
#include "skse64_common/Relocation.h"
#include "skse64_common/Utilities.h"
#include <Windows.h>

namespace LoadRefusal {
bool Queue(Notice notice) {
    if(!Text(notice)) return false;
    const auto entry=RelocationManager::s_baseAddr+CreateRva;
    unsigned char actual[sizeof(CreatePrefix)]={};
    SIZE_T got=0;
    const bool supported=ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(entry),actual,sizeof(actual),&got)
        && got==sizeof(actual) && MatchesPrefix(actual,sizeof(actual));
    using Function=bool(*)(const char*,void(*)(std::uint8_t),std::uint8_t,std::int32_t,std::int32_t,...);
    // The native function copies strings into its owned MessageBoxData. The
    // return value means accepted for queuing, NOT proof the movie rendered.
    const bool queued=QueueWith(notice,supported,reinterpret_cast<Function>(entry));
    _MESSAGE("LOAD_REFUSAL_NOTICE code=%u supported=%u queued=%u desktop_dialog=0 rendered_unverified=1",
        notice.code,unsigned(supported),unsigned(queued));
    return queued;
}
}
