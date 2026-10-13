#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace sf4e { namespace gamepads {
// The native vector's bounds are validated before any record is addressed.
class PadBindingRecords {
public:
    static constexpr unsigned Stride = 0x27C, FightOffset = 0x17C;
    PadBindingRecords(void* begin = nullptr, void* end = nullptr) : begin_(static_cast<unsigned char*>(begin)), end_(static_cast<unsigned char*>(end)) {}
    std::uint32_t* FightTable(unsigned device) const {
        const auto begin = reinterpret_cast<std::uintptr_t>(begin_), end = reinterpret_cast<std::uintptr_t>(end_);
        if (!begin || device >= 12 || end < begin || end - begin < (device + 1) * Stride) return nullptr;
        return reinterpret_cast<std::uint32_t*>(begin_ + device * Stride + FightOffset);
    }
private:
    unsigned char* begin_;
    unsigned char* end_;
};
} }
