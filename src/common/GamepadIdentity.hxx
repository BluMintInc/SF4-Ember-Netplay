#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
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

// Windows interface paths are case insensitive and may use either device prefix.
inline bool SameDriverPath(const std::string& a, const std::string& b) {
    if (a.empty() || a.size() != b.size()) return false;
    const auto character = [](unsigned char c) { return c == '/' ? '\\' : std::tolower(c); };
    const auto prefixed = [&](const std::string& path) {
        return path.size() >= 4 && character(path[0]) == '\\' && character(path[1]) == '\\' &&
            (path[2] == '?' || path[2] == '.') && character(path[3]) == '\\';
    };
    const auto offset = prefixed(a) && prefixed(b) ? 4 : 0;
    return std::equal(a.begin() + offset, a.end(), b.begin() + offset,
        [&](unsigned char a, unsigned char b) { return character(a) == character(b); });
}

// Stable short log tag. Keep serials and paths out of the logs themselves.
inline std::uint32_t IdentityHash(const Identity& identity) {
    std::uint32_t hash = 2166136261u;
    for (unsigned char c : identity.serial.empty() ? identity.path : identity.serial) hash = (hash ^ c) * 16777619u;
    return hash;
}

} }
