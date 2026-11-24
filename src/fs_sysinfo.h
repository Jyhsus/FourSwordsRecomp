#pragma once
// fs_sysinfo.h — process counters for the local session log.

#include <cstdint>

namespace fsr {

// Page faults of this process since it started (soft and hard; Windows only,
// 0 elsewhere). A jump during one frame means memory that had to be brought
// in, such as code run for the first time.
std::uint64_t process_page_faults();

}  // namespace fsr
