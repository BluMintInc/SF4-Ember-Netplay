#pragma once
#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sf4e { namespace gamepads {
// SDL_GamepadBinding with SDL 3.4.16's enum values and axis ranges. Bindings
// come only from SDL_GetGamepadBindings (sf4e__GamepadBindings.hxx); Ember
// writes them as mapping text and never reads bindings back from text.
enum class SdlBindType { None, Button, Axis, Hat };
// An axis input runs from min to max: a half starts at 0, and min above max
// is an inverted axis.
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
inline constexpr int SdlAxisMin = -32768, SdlAxisMax = 32767, SdlLeftTrigger = 4, SdlDpadUp = 11;
inline constexpr std::array<std::string_view, 6> SdlAxisNames = {"leftx", "lefty", "rightx", "righty", "lefttrigger", "righttrigger"};
inline constexpr std::array<std::string_view, 26> SdlButtonNames = {"a", "b", "x", "y", "back", "guide", "start", "leftstick",
    "rightstick", "leftshoulder", "rightshoulder", "dpup", "dpdown", "dpleft", "dpright", "misc1", "paddle1", "paddle2", "paddle3",
    "paddle4", "touchpad", "misc2", "misc3", "misc4", "misc5", "misc6"};
// The side of 0 an axis range covers: 1 or -1 for a half, 0 for the whole axis.
inline int AxisHalf(int min, int max) { return min && max ? 0 : min + max > 0 ? 1 : -1; }
// The output an SDL name stands for: a button by position, a whole stick axis or a trigger.
inline SdlOutput SdlTarget(std::string_view name) {
    const auto axis = std::find(SdlAxisNames.begin(), SdlAxisNames.end(), name);
    if (axis != SdlAxisNames.end()) {
        const int index = static_cast<int>(axis - SdlAxisNames.begin());
        return {SdlBindType::Axis, index, index < SdlLeftTrigger ? SdlAxisMin : 0, SdlAxisMax};
    }
    return {SdlBindType::Button, static_cast<int>(std::find(SdlButtonNames.begin(), SdlButtonNames.end(), name) - SdlButtonNames.begin())};
}

// A binding as the text SDL decodes back to the same binding: a, b, x and y
// are positions, a half axis output takes its sign, and an input axis range
// takes + or - for a half and ~ when inverted. A trigger reads 0 to max.
inline std::string FormatBinding(const SdlBinding& binding) {
    const auto& input = binding.input;
    const auto& output = binding.output;
    const auto sign = [](int half) { return std::string(half > 0 ? "+" : half < 0 ? "-" : ""); };
    std::string text = output.type == SdlBindType::Button ? std::string(SdlButtonNames[output.index]) :
        (output.index < SdlLeftTrigger ? sign(AxisHalf(output.min, output.max)) : "") + std::string(SdlAxisNames[output.index]);
    const auto index = std::to_string(input.index);
    if (input.type == SdlBindType::Button) return text + ":b" + index;
    if (input.type == SdlBindType::Hat) return text + ":h" + index + "." + std::to_string(input.hatMask);
    const int half = AxisHalf(input.min, input.max);
    return text + ":" + sign(half) + "a" + index + ((half ? input.min != 0 : input.min > input.max) ? "~" : "");
}

// SDL's face styles, spelled as a mapping's face field spells them: the labels
// SDL gives the south, east, west and north buttons.
enum class SdlFace { Abxy, Axby, Bayx, Sony };
inline constexpr std::array<std::string_view, 4> SdlFaceNames = {"abxy", "axby", "bayx", "sony"};
// What SDL makes of a pad: the bindings it decodes, the face style its button
// labels follow, and SDL's name for the type it gives the pad when that is not
// the type SDL finds for the device itself. The type is empty when they agree,
// so a mapping never pins a type SDL would find anyway.
struct SdlSnapshot {
    std::vector<SdlBinding> bindings;
    SdlFace face = SdlFace::Abxy;
    std::string type;
};

// The fields of a mapping that bind nothing and are safe to carry over, as SDL
// reads them: each key where it first appears after the GUID and name, up to
// the next comma. crc picks which pads the mapping matches, and sdk>= and
// sdk<= apply on Android alone. A value is kept only when it is plain letters
// and digits, so no kept field can hold another key. Face and type are
// written from the snapshot instead. Hints go: a label hint would rename
// positional bindings, and any other hint can stop SDL adding the mapping at all.
inline std::string SdlMetadata(std::string_view mapping) {
    const auto name = mapping.find(',');
    const auto body = name == std::string_view::npos ? name : mapping.find(',', name + 1);
    if (body == std::string_view::npos) return {};
    mapping.remove_prefix(body + 1);
    const auto plain = [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    std::string kept;
    for (const std::string_view key : {"crc:", "sdk>=:", "sdk<=:"}) {
        const auto at = mapping.find(key);
        if (at == std::string_view::npos) continue;
        auto value = mapping.substr(at + key.size());
        value = value.substr(0, value.find(','));
        if (!value.empty() && std::all_of(value.begin(), value.end(), plain)) kept += std::string(key) + std::string(value) + ",";
    }
    return kept;
}
// SDL_IsJoystickHIDAPI: byte 14 of a HIDAPI pad's GUID is 'h'.
inline bool HidapiGuid(std::string_view guid) { return guid.size() == 32 && guid.substr(28, 2) == "68"; }
// SDL's own mark of a mapping whose a, b, x and y are positions. Without a
// GameCube hint SDL appends the unnegated one to a GameCube pad's mapping and
// swaps its b and x; this first and only hint makes SDL read the bindings as
// written. SDL also takes any GameCube hint for GameCube labels unless a face
// field names the labels, so the face field always goes with it.
inline constexpr std::string_view PositionalHint = "hint:!SDL_GAMECONTROLLER_USE_GAMECUBE_LABELS:=1";

// The mapping SDL decodes back to exactly this pad's bindings, face style and
// type, with the other metadata existing mapping text carries, or none when
// SDL cannot be given them.
// SDL_FixupHIDAPIMapping takes a HIDAPI mapping for a legacy one when the first
// binding to a button from dpup on reads that same button number, and then
// moves every button input. These bindings are SDL's own, already fixed up, so
// the first such binding written is one that reads another input; when every
// one reads its own number, SDL would move them whatever the order.
inline std::optional<std::string> WriteSdlMapping(const std::string& guid, std::string name, SdlSnapshot pad, std::string_view existing = {}) {
    auto& bindings = pad.bindings;
    if (HidapiGuid(guid)) {
        const auto late = [](const SdlBinding& b) { return b.output.type == SdlBindType::Button && b.output.index >= SdlDpadUp; };
        const auto own = [](const SdlBinding& b) { return b.input.type == SdlBindType::Button && b.input.index == b.output.index; };
        const auto first = std::find_if(bindings.begin(), bindings.end(), late);
        if (first != bindings.end() && own(*first)) {
            const auto other = std::find_if(first, bindings.end(), [&](const SdlBinding& b) { return late(b) && !own(b); });
            if (other == bindings.end()) return {};
            std::rotate(first, other, other + 1);
        }
    }
    // Commas delimit SDL fields; driver names must not create mapping fields.
    std::replace(name.begin(), name.end(), ',', ' ');
    auto text = guid + "," + name + ",";
    for (const auto& binding : bindings) text += FormatBinding(binding) + ",";
    // face and type go first of the fields that bind nothing, as SDL takes the first of each;
    // platform goes once, last: SDL cuts a returned mapping at a second one (CreateMappingString).
    text += "face:" + std::string(SdlFaceNames[static_cast<int>(pad.face)]) + ",";
    if (!pad.type.empty()) text += "type:" + pad.type + ",";
    return text + std::string(PositionalHint) + "," + SdlMetadata(existing) + "platform:Windows,";
}
} }
