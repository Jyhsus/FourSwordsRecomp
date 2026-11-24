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

std::uint64_t prefetch_own_image() {
#if defined(_WIN32)
    const auto base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    if (!base) return 0;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    // Looked up at run time: the function does not exist before Windows 8,
    // and MinGW's headers only declare its types for Windows 8 targets.
    struct Range {   // WIN32_MEMORY_RANGE_ENTRY
        void* address;
        SIZE_T bytes;
    };
    using Prefetch = BOOL(WINAPI*)(HANDLE, ULONG_PTR, Range*, ULONG);
    const auto prefetch = reinterpret_cast<Prefetch>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "PrefetchVirtualMemory")));
    if (!prefetch) return 0;
    Range range{const_cast<unsigned char*>(base), nt->OptionalHeader.SizeOfImage};
    if (!prefetch(GetCurrentProcess(), 1, &range, 0)) return 0;
    return nt->OptionalHeader.SizeOfImage;
#else
    return 0;
#endif
}

}  // namespace fsr
