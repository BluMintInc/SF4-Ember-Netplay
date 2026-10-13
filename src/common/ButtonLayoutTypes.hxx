#pragma once
#include "GamepadIdentity.hxx"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace sf4e { namespace gamepads {
using ButtonLayout = std::array<std::uint32_t, 10>;
using ListedButtons = std::array<bool, 10>;
using FightNames = std::array<std::string, 10>;
constexpr ListedButtons AllLayoutButtons = {true, true, true, true, true, true, true, true, true, true};
constexpr std::array<const char*, 10> LayoutGlyphs = {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS"};
constexpr std::array<unsigned, 10> LayoutBits = {18, 17, 19, 16, 20, 21, 22, 23, 24, 25};
constexpr std::array<std::uint32_t, 6> AttackMasks = {0x10, 0x20, 0x400, 0x40, 0x80, 0x800};
constexpr std::array<const char*, 6> AttackNames = {"LP", "MP", "HP", "LK", "MK", "HK"};
constexpr std::uint32_t FightMask = 0xCF0;
constexpr ButtonLayout DefaultLayout = {0x40, 0x80, 0x10, 0x20, 0x430, 0x400, 0x8C0, 0x800, 0, 0};
inline bool ValidLayout(const ButtonLayout& layout) {
    return std::all_of(layout.begin(), layout.end(), [](std::uint32_t mask) { return !(mask & ~FightMask); });
}
struct StoredLayout {
    Identity identity;
    ButtonLayout buttons = DefaultLayout;
    std::int64_t edited = 0;
    bool operator==(const StoredLayout& other) const {
        return identity == other.identity && buttons == other.buttons && edited == other.edited;
    }
};
} }
