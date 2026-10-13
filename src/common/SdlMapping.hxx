#pragma once
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sf4e { namespace gamepads {
// How SDL 3.4.16 reads a gamepad mapping, ported from src/joystick/SDL_gamepad.c
// (SDL_PrivateParseGamepadConfigString, SDL_PrivateParseGamepadElement,
// SDL_GetGamepadAxisFromString, SDL_PrivateGetGamepadButtonFromString) and the
// folding SDL_strcasecmp does (src/stdlib/SDL_string.c, SDL_casefolding.h).
// Simple DirectMedia Layer, Copyright (C) 1997-2026 Sam Lantinga, zlib licence.

// SDL_GamepadBinding, with SDL's enum values and axis ranges.
enum class SdlBindType { None, Button, Axis, Hat };
struct SdlInput {
    SdlBindType type = SdlBindType::None;
    int index = 0, min = 0, max = 0, hatMask = 0;
    bool operator==(const SdlInput& b) const { return type == b.type && index == b.index && min == b.min && max == b.max && hatMask == b.hatMask; }
};
struct SdlOutput {
    SdlBindType type = SdlBindType::None;
    int index = 0, min = 0, max = 0;
    bool operator==(const SdlOutput& b) const { return type == b.type && index == b.index && min == b.min && max == b.max; }
};
struct SdlBinding {
    SdlInput input;
    SdlOutput output;
    bool operator==(const SdlBinding& b) const { return input == b.input && output == b.output; }
};
inline constexpr int SdlAxisMin = -32768, SdlAxisMax = 32767, SdlLeftTrigger = 4;
inline constexpr std::array<std::string_view, 6> SdlAxisNames = {"leftx", "lefty", "rightx", "righty", "lefttrigger", "righttrigger"};
inline constexpr std::array<std::string_view, 26> SdlButtonNames = {"a", "b", "x", "y", "back", "guide", "start", "leftstick",
    "rightstick", "leftshoulder", "rightshoulder", "dpup", "dpdown", "dpleft", "dpright", "misc1", "paddle1", "paddle2", "paddle3",
    "paddle4", "touchpad", "misc2", "misc3", "misc4", "misc5", "misc6"};
// The side of 0 an axis range covers: 1 or -1 for a half, 0 for the whole axis.
inline int AxisHalf(int min, int max) { return min && max ? 0 : min + max > 0 ? 1 : -1; }

// Text folded as SDL_strcasecmp folds it, when it folds into ASCII. Besides
// A to Z, these are the only codepoints SDL folds into ASCII.
inline std::optional<std::string> SdlFold(std::string_view text) {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 11> folds = {{
        {"\xC3\x9F", "ss"}, {"\xC5\xBF", "s"}, {"\xE1\xBA\x9E", "ss"}, {"\xE2\x84\xAA", "k"}, {"\xEF\xAC\x80", "ff"}, {"\xEF\xAC\x81", "fi"},
        {"\xEF\xAC\x82", "fl"}, {"\xEF\xAC\x83", "ffi"}, {"\xEF\xAC\x84", "ffl"}, {"\xEF\xAC\x85", "st"}, {"\xEF\xAC\x86", "st"}}};
    std::string folded;
    while (!text.empty()) {
        const char c = text.front();
        if (static_cast<unsigned char>(c) < 0x80) {
            folded += c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
            text.remove_prefix(1);
            continue;
        }
        const auto fold = std::find_if(folds.begin(), folds.end(), [&](const auto& f) { return text.substr(0, f.first.size()) == f.first; });
        if (fold == folds.end()) return {};
        folded += fold->second;
        text.remove_prefix(fold->first.size());
    }
    return folded;
}
// The output a key names: one sign picks a stick's half, then an axis name,
// which may carry one more sign, or a button name.
inline std::optional<SdlOutput> ParseSdlOutput(std::string_view key) {
    char half = 0;
    if (!key.empty() && (key.front() == '+' || key.front() == '-')) { half = key.front(); key.remove_prefix(1); }
    const auto name = SdlFold(key);
    if (!name) return {};
    std::string_view axisName = *name;
    if (!axisName.empty() && (axisName.front() == '+' || axisName.front() == '-')) axisName.remove_prefix(1);
    const auto axis = std::find(SdlAxisNames.begin(), SdlAxisNames.end(), axisName);
    if (axis != SdlAxisNames.end()) {
        const int index = static_cast<int>(axis - SdlAxisNames.begin());
        if (index >= SdlLeftTrigger || half == '+') return SdlOutput{SdlBindType::Axis, index, 0, SdlAxisMax};
        return SdlOutput{SdlBindType::Axis, index, half ? 0 : SdlAxisMin, half ? SdlAxisMin : SdlAxisMax};
    }
    const auto button = std::find(SdlButtonNames.begin(), SdlButtonNames.end(), *name);
    if (button == SdlButtonNames.end()) return {};
    return SdlOutput{SdlBindType::Button, static_cast<int>(button - SdlButtonNames.begin())};
}
// One key and input as SDL binds them: an input sign and ~ shape an axis only,
// and indices are SDL_atoi's decimal prefix.
inline std::optional<SdlBinding> ParseSdlElement(const char* key, const char* input) {
    const auto output = ParseSdlOutput(key);
    if (!output) return {};
    char half = 0;
    if (*input == '+' || *input == '-') half = *input++;
    const auto length = std::strlen(input);
    const bool invert = length && input[length - 1] == '~';
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    const auto number = [](const char* text) { return static_cast<int>(std::strtol(text, nullptr, 10)); };
    SdlInput bound;
    if (input[0] == 'a' && digit(input[1])) {
        bound = {SdlBindType::Axis, number(input + 1), half ? 0 : SdlAxisMin, half == '-' ? SdlAxisMin : SdlAxisMax};
        if (invert) std::swap(bound.min, bound.max);
    } else if (input[0] == 'b' && digit(input[1])) bound = {SdlBindType::Button, number(input + 1)};
    else if (input[0] == 'h' && digit(input[1]) && input[2] == '.' && digit(input[3])) bound = {SdlBindType::Hat, number(input + 1), 0, 0, number(input + 3)};
    else return {};
    return SdlBinding{bound, *output};
}

// A field SDL does not bind, as written. key is what SDL read as its key.
struct SdlField { std::string key, text; };
// The bindings SDL makes, once each and in order, and every other field.
struct SdlMapping { std::vector<SdlBinding> bindings; std::vector<SdlField> metadata; };
// Reads the fields after the GUID and name of a mapping as SDL stores it. SDL
// skips spaces, starts the input again at each colon without clearing it, and
// stops at a key over 20 characters or an input over 128, so the fields from
// there on are never read. A full buffer here still ends where SDL's reads on;
// no output name fills a key.
inline SdlMapping ReadSdlMapping(const std::string& mapping) {
    SdlMapping read;
    const auto name = mapping.find(',');
    const auto body = name == std::string::npos ? name : mapping.find(',', name + 1);
    if (body == std::string::npos) return read;
    const char* field = mapping.c_str() + body + 1;
    char key[20 + 1] = {}, input[128 + 1] = {};
    const auto element = [&](const char* end) {
        const auto binding = ParseSdlElement(key, input);
        if (!binding) read.metadata.push_back({key, std::string(field, end)});
        else if (std::find(read.bindings.begin(), read.bindings.end(), *binding) == read.bindings.end()) read.bindings.push_back(*binding);
        field = end + 1;
        std::memset(key, 0, sizeof key);
        std::memset(input, 0, sizeof input);
    };
    bool inKey = true;
    std::size_t i = 0;
    const char* at = field;
    for (; *at; ++at) {
        if (*at == ':') { i = 0; inKey = false; }
        else if (*at == ' ') {}
        else if (*at == ',') { i = 0; inKey = true; element(at); }
        else if (inKey) { if (i >= 20) return read; key[i++] = *at; }
        else { if (i >= 128) return read; input[i++] = *at; }
    }
    if (key[0] || input[0]) element(at);
    return read;
}
} }
