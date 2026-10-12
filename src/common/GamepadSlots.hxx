#pragma once
#include "GamepadIdentity.hxx"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <vector>

namespace sf4e { namespace gamepads {

using Id = std::uint32_t;
enum class Type { Unknown, Standard, Xbox360, XboxOne, PS3, PS4, PS5, SwitchPro,
    JoyConLeft, JoyConRight, JoyConPair, GameCube };
enum class Status { Slotted, Unmapped, NotInUse, SteamTwin, GameRead };

struct Pad {
    Id id = 0;
    Identity identity;
    std::uint16_t vid = 0, pid = 0;
    Type type = Type::Unknown;
    bool mapped = false;
    bool available = true;
    int slot = -1;
    Status status = Status::NotInUse;
    Id steamPad = 0;
    bool operator==(const Pad& other) const {
        return std::tie(id, identity, vid, pid, type, mapped, available, slot, status, steamPad) ==
            std::tie(other.id, other.identity, other.vid, other.pid, other.type,
                other.mapped, other.available, other.slot, other.status, other.steamPad);
    }
};

struct Inventory {
    std::vector<Pad> pads;
    bool operator==(const Inventory& other) const { return pads == other.pads; }
};

struct NativeProfile { std::uint16_t vid, pid; };
// The game's native DirectInput profiles, from guidProduct strings at 0x517900.
constexpr std::array<NativeProfile, 14> NativeProfiles = {{
    {0x0079, 0x0011}, {0x05FD, 0x262A}, {0x046D, 0xC20C}, {0x0E8F, 0x0003},
    {0x0E8F, 0x3013}, {0x0F0D, 0x0032}, {0x0F0D, 0x0022}, {0x0738, 0x8838},
    {0x1A34, 0x0802}, {0x0E8F, 0x0012}, {0x0079, 0x0006}, {0x046D, 0xC216},
    {0x0079, 0x0002}, {0x0F0D, 0x0037}
}};

inline bool SteamVirtual(const Pad& pad) { return pad.vid == 0x28DE && pad.pid == 0x11FF; }

// Also used before SDL opens handles, so Steam twins and native-profile pads are never opened.
inline void Classify(std::vector<Pad>& pads, const std::set<std::string>& savedEmberMappingGuids) {
    for (auto& pad : pads) {
        pad.slot = -1;
        pad.steamPad = 0;
        const bool native = std::any_of(NativeProfiles.begin(), NativeProfiles.end(), [&](const NativeProfile& profile) {
            return pad.vid == profile.vid && pad.pid == profile.pid;
        });
        pad.status = native && !savedEmberMappingGuids.count(pad.identity.guid) ? Status::GameRead :
            pad.mapped ? Status::NotInUse : Status::Unmapped;
    }
    for (const auto& steam : pads) if (SteamVirtual(steam)) {
        for (auto& physical : pads) if (!SteamVirtual(physical) && !physical.steamPad && physical.type == steam.type) {
            physical.steamPad = steam.id;
            if (physical.status != Status::GameRead) physical.status = Status::SteamTwin;
            break;
        }
    }
}

struct DroppedReservation {
    int slot;
    Identity owner;
    Id replacement;
};

class Slots {
public:
    Inventory Update(std::vector<Pad> pads, const std::optional<Identity>& preferred = {},
        const std::set<std::string>& savedEmberMappingGuids = {}) {
        Classify(pads, savedEmberMappingGuids);
        dropped.clear();
        std::array<Id, 4> next{};
        const auto eligible = [](const Pad& pad) { return pad.status == Status::NotInUse && pad.available; };
        const auto assign = [&](Pad& pad, int slot) {
            if (reserved[slot] && !Same(*reserved[slot], pad.identity))
                dropped.push_back({slot, *reserved[slot], pad.id});
            for (auto& owner : reserved) if (owner && Same(*owner, pad.identity)) owner.reset();
            reserved[slot] = pad.identity;
            next[slot] = pad.id;
            pad.slot = slot;
            pad.status = Status::Slotted;
        };
        // The saved pad takes slot 0 at layer start to retain the game's saved slot-based layout.
        if (startup && preferred)
            for (auto& pad : pads) if (eligible(pad) && Same(pad.identity, *preferred)) { assign(pad, 0); break; }
        for (int slot = 0; slot < 4; ++slot) if (!next[slot])
            for (auto& pad : pads) if (eligible(pad) && pad.id == current[slot]) { assign(pad, slot); break; }
        std::vector<Pad*> waiting;
        for (auto& pad : pads) if (eligible(pad)) waiting.push_back(&pad);
        // Startup uses SDL enumeration order; later waiting pads use increasing connection ids.
        if (!startup) std::sort(waiting.begin(), waiting.end(), [](const Pad* a, const Pad* b) { return a->id < b->id; });
        for (auto* pad : waiting) {
            int free = -1;
            for (int slot = 0; slot < 4; ++slot)
                if (!next[slot] && reserved[slot] && Same(*reserved[slot], pad->identity)) { free = slot; break; }
            if (free < 0)
                for (int slot = 0; slot < 4; ++slot) if (!next[slot] && !reserved[slot]) { free = slot; break; }
            if (free < 0)
                for (int slot = 0; slot < 4; ++slot) if (!next[slot]) { free = slot; break; }
            if (free >= 0) assign(*pad, free);
        }
        current = next;
        startup = false;
        return {std::move(pads)};
    }

    const std::vector<DroppedReservation>& DroppedReservations() const { return dropped; }

private:
    std::array<Id, 4> current{};
    std::array<std::optional<Identity>, 4> reserved;
    std::vector<DroppedReservation> dropped;
    bool startup = true;
};

} }
