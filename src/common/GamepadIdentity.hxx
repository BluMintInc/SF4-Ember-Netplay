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

// The vendor and product an SDL GUID encodes, or 0:0 when it is not in SDL's
// standard form: little-endian uint16 bus, crc, vendor, 0, product, 0, version,
// then driver data (SDL_GetJoystickGUIDInfo). SDL may report a different
// VID:PID for the device, such as the physical pad behind Steam's virtual one.
struct GuidIds { std::uint16_t vendor = 0, product = 0; };
inline GuidIds GuidVendorProduct(const std::string& guid) {
    if (guid.size() != 32 || !std::all_of(guid.begin(), guid.end(), [](unsigned char c) { return std::isxdigit(c) != 0; })) return {};
    const auto word = [&](int i) { return static_cast<std::uint16_t>(std::stoul(guid.substr(i * 4 + 2, 2) + guid.substr(i * 4, 2), nullptr, 16)); };
    const auto bus = word(0);
    if ((bus >= 0x20 && bus != 0xFF) || word(3) || word(5)) return {};
    return {word(2), word(4)};
}

// Stable short log tag. Keep serials and paths out of the logs themselves.
inline std::uint32_t IdentityHash(const Identity& identity) {
    std::uint32_t hash = 2166136261u;
    for (unsigned char c : identity.serial.empty() ? identity.path : identity.serial) hash = (hash ^ c) * 16777619u;
    return hash;
}

} }
