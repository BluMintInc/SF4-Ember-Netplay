#pragma once
#include <string>
#include <tuple>

namespace sf4e { namespace gamepads {

struct Identity {
    std::string guid, serial, path, name;
    bool operator==(const Identity& other) const {
        return std::tie(guid, serial, path, name) == std::tie(other.guid, other.serial, other.path, other.name);
    }
    bool operator!=(const Identity& other) const { return !(*this == other); }
};

inline bool Same(const Identity& a, const Identity& b) {
    if (!a.serial.empty() && !b.serial.empty()) return a.serial == b.serial;
    return a.guid == b.guid && a.path == b.path;
}

} }
