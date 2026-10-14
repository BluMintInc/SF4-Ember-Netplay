#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace sf4e { namespace gamepads {
// The native vector's bounds are validated before any record is addressed.
// Both tables hold 32 dwords indexed by physical bit, each the game functions
// that bit triggers: the fight table in fights, the menu table in the game's
// menus (0x511E70 reads one or the other by System+0x08).
class PadBindingRecords {
public:
    static constexpr unsigned Stride = 0x27C, FightOffset = 0x17C, MenuOffset = 0x1FC;
    PadBindingRecords(void* begin = nullptr, void* end = nullptr) : begin_(static_cast<unsigned char*>(begin)), end_(static_cast<unsigned char*>(end)) {}
    std::uint32_t* FightTable(unsigned device) const { return Table(device, FightOffset); }
    std::uint32_t* MenuTable(unsigned device) const { return Table(device, MenuOffset); }
private:
    std::uint32_t* Table(unsigned device, unsigned offset) const {
        const auto begin = reinterpret_cast<std::uintptr_t>(begin_), end = reinterpret_cast<std::uintptr_t>(end_);
        if (!begin || device >= 12 || end < begin || end - begin < (device + 1) * Stride) return nullptr;
        return reinterpret_cast<std::uint32_t*>(begin_ + device * Stride + offset);
    }
    unsigned char* begin_;
    unsigned char* end_;
};
} }
