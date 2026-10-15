#pragma once

// Why the networking helper would not start, as the player is told. Win32 2
// (and 226, ERROR_VIRUS_DELETED) mean sf4-net.exe is gone from the folder, and
// 225 (ERROR_VIRUS_INFECTED) that Windows refused to run it: in reports both
// were security software, and the bare error number told players nothing.
#include <cstdint>

namespace sf4e { namespace netplay {

enum class HelperFailure { Missing, Blocked, Other };

inline HelperFailure ClassifyHelperFailure(std::uint32_t win32) {
    switch (win32) {
    case 2: case 226: return HelperFailure::Missing;
    case 225: return HelperFailure::Blocked;
    default: return HelperFailure::Other;
    }
}

} }
