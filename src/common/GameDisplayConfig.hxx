#pragma once
#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace sf4e { namespace gameconfig {

// The settings of the game's own config.ini that rollback depends on and the
// player must change. VSync and RenderingThread are not here: the mod forces
// both off (RenderingThreadOverride below).
// An empty value means the key was absent; it is not reported as a mismatch.
struct DisplaySettings {
    std::string frameRate, msaa;
    bool FrameRateOk() const { return frameRate.empty() || frameRate == "FIXED"; }
    bool MsaaOk() const { return msaa.empty() || msaa == "NONE"; }
    bool Recommended() const { return FrameRateOk() && MsaaOk(); }
};

// Keys are unique across the file's sections, so sections are ignored.
inline DisplaySettings ParseDisplaySettings(std::string_view ini) {
    DisplaySettings out;
    while (!ini.empty()) {
        const auto end = ini.find('\n');
        std::string_view line = ini.substr(0, end);
        ini.remove_prefix(end == std::string_view::npos ? ini.size() : end + 1);
        const auto equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        // Values are single words, so whitespace anywhere is noise and case
        // is not significant: "MSAA = none" and "MSAA=NONE" are the same file.
        const auto normalize = [](std::string_view text) {
            std::string value(text);
            value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); }), value.end());
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            return value;
        };
        const auto key = normalize(line.substr(0, equals));
        if (key == "FRAMERATE") out.frameRate = normalize(line.substr(equals + 1));
        else if (key == "MSAA") out.msaa = normalize(line.substr(equals + 1));
    }
    return out;
}

// SYSTEM/RenderingThread. ON draws each frame on a second thread while the
// game thread simulates the next one, so the picture reaches the screen a
// frame later. Ember keeps the game's live flag off, as it does VSync, and
// leaves the player's file as it was: the game writes the flag back into
// config.ini (on exit and from its display options), and that write is given
// the player's own value (sf4e__Platform.cxx).
class RenderingThreadOverride {
public:
    // The flag as config.ini set it: remembered, then turned off.
    void Enforce(int& live) { requested_ = live; live = 0; }
    // Runs the game's config write with the player's value in the flag and
    // puts the enforced one back after. Before Enforce the flag is the game's.
    template <typename Write> void WriteRequested(int& live, Write&& write) const {
        if (!requested_) { write(); return; }
        const int enforced = live;
        live = *requested_;
        write();
        live = enforced;
    }
    // What config.ini asked for; empty before Enforce.
    std::optional<int> Requested() const { return requested_; }
private:
    std::optional<int> requested_;
};

} }
