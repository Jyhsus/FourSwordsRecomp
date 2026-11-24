// fs_sysinfo.cpp — process counters for the local session log (fs_local.cpp).
// Kept in its own file so windows.h stays out of the runner.

#include "fs_sysinfo.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#define PSAPI_VERSION 2   // GetProcessMemoryInfo from kernel32 (K32...)
#include <windows.h>
#include <psapi.h>
#endif

namespace fsr {

std::uint64_t process_page_faults() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        return counters.PageFaultCount;
    }
#endif
    return 0;
}

}  // namespace fsr
