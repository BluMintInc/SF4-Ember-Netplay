#pragma once
#include <cstdint>

namespace sf4e { namespace gamepads {
// SDL rumble expires unless a held motor request is refreshed each poll.
inline bool ShouldApplyRumble(bool pending, std::uint16_t left, std::uint16_t right) {
    return pending || left || right;
}
} }
