#pragma once
#include "GamepadSlots.hxx"
#include <cstring>
#include <map>

namespace sf4e { namespace gamepads {

inline bool Served(std::uint32_t vidpid, const std::vector<Pad>& inventory) {
    return std::any_of(inventory.begin(), inventory.end(), [&](const Pad& pad) {
        return pad.available && vidpid == (std::uint32_t(pad.pid) << 16 | pad.vid) &&
            pad.status != Status::Unmapped && pad.status != Status::GameRead;
    });
}

struct AxisRange {
    std::int32_t min = 0, max = 65535;
    std::int32_t Centre() const { return static_cast<std::int32_t>((std::int64_t(min) + max) / 2); }
};

// DirectInput's caller chooses the byte layout. Keep only the fields needed
// to produce neutral input, with resolved object ids for per-axis ranges.
class NeutralState {
public:
    enum class Kind { Axis, Pov };
    struct Field { std::uint32_t offset, id; Kind kind; };

    void SetFormat(std::uint32_t size, std::vector<Field> fields) {
        size_ = size;
        fields_ = std::move(fields);
    }
    void SetRange(AxisRange range) { default_ = range; ranges_.clear(); }
    void SetRange(std::uint32_t id, AxisRange range) { ranges_[id] = range; }

    bool Write(std::uint32_t size, void* out) const {
        if (!out || !size_ || size != size_) return false;
        auto* bytes = static_cast<unsigned char*>(out);
        std::memset(bytes, 0, size);
        for (const auto& field : fields_) {
            if (field.offset > size || size - field.offset < sizeof(std::uint32_t)) return false;
            const auto range = ranges_.find(field.id);
            const std::uint32_t value = field.kind == Kind::Pov ? 0xFFFFFFFFu :
                static_cast<std::uint32_t>((range == ranges_.end() ? default_ : range->second).Centre());
            std::memcpy(bytes + field.offset, &value, sizeof(value));
        }
        return true;
    }

private:
    std::uint32_t size_ = 0;
    std::vector<Field> fields_;
    AxisRange default_;
    std::map<std::uint32_t, AxisRange> ranges_;
};

} }
