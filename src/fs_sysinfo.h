#pragma once
// fs_sysinfo.h — process counters for the local session log.

#include <cstdint>

namespace fsr {

// Page faults of this process since it started (soft and hard; Windows only,
// 0 elsewhere). A jump during one frame means memory that had to be brought
// in, such as code run for the first time.
std::uint64_t process_page_faults();

// Asks Windows to read this executable's whole image into memory in the
// background (PrefetchVirtualMemory, Windows 8 and later; does nothing
// elsewhere). The recompiled game is about 110 MB of code: without this,
// each part of it is read from disk the first time it runs, in the middle
// of play. Returns the bytes requested, 0 if the request was not made.
std::uint64_t prefetch_own_image();

}  // namespace fsr
