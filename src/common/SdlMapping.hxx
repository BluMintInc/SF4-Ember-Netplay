#pragma once
#include "PadSetupInput.hxx"
#include <algorithm>
#include <array>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sf4e { namespace gamepads {
// An SDL gamepad mapping string as the setup merge reads it (SDL_gamepad.c,
// SDL_PrivateParseGamepadElement): control bindings, which SDL turns into
// gamepad outputs, and every other field, which is opaque metadata.

// The physical input a binding reads: bN, hN.M with any hat mask, or aN with an
// optional + or - half and an optional ~ inversion.
struct MappingInput {
    enum class Kind { Button, Hat, Axis };
    Kind kind = Kind::Button;
    int index = 0;
    unsigned hatMask = 0;
    char half = 0; // '+' or '-' for one half of the axis, 0 for all of it
    bool inverted = false;
    bool operator==(const MappingInput& b) const {
        return kind == b.kind && index == b.index && hatMask == b.hatMask && half == b.half && inverted == b.inverted;
    }
};
inline std::optional<MappingInput> ParseMappingInput(std::string_view value) {
    MappingInput input;
    if (!value.empty() && (value.front() == '+' || value.front() == '-')) { input.half = value.front(); value.remove_prefix(1); }
    if (!value.empty() && value.back() == '~') { input.inverted = true; value.remove_suffix(1); }
    if (value.empty()) return {};
    switch (value.front()) {
    case 'a': input.kind = MappingInput::Kind::Axis; break;
    case 'b': input.kind = MappingInput::Kind::Button; break;
    case 'h': input.kind = MappingInput::Kind::Hat; break;
    default: return {};
    }
    if ((input.half || input.inverted) && input.kind != MappingInput::Kind::Axis) return {};
    const char* end = value.data() + value.size();
    auto parsed = std::from_chars(value.data() + 1, end, input.index);
    if (parsed.ec != std::errc{} || input.index < 0) return {};
    if (input.kind == MappingInput::Kind::Hat) {
        if (parsed.ptr == end || *parsed.ptr != '.') return {};
        parsed = std::from_chars(parsed.ptr + 1, end, input.hatMask);
        if (parsed.ec != std::errc{} || input.hatMask == 0 || input.hatMask > 15) return {};
    }
    if (parsed.ptr != end) return {};
    return input;
}
// Whether one physical movement can drive both: the same button, hat masks
// that share a direction, or the same axis where a full axis covers both halves.
inline bool Overlaps(const MappingInput& a, const MappingInput& b) {
    if (a.kind != b.kind || a.index != b.index) return false;
    switch (a.kind) {
    case MappingInput::Kind::Hat: return (a.hatMask & b.hatMask) != 0;
    case MappingInput::Kind::Axis: return !a.half || !b.half || a.half == b.half;
    default: return true;
    }
}
// The wizard's captured input, as the binding RawBinding writes for it.
inline MappingInput ToMappingInput(const RawInput& raw) {
    using Kind = MappingInput::Kind;
    switch (raw.kind) {
    case RawKind::Button: return {Kind::Button, raw.index};
    case RawKind::AxisPlus: return {Kind::Axis, raw.index, 0, '+'};
    case RawKind::AxisMinus: return {Kind::Axis, raw.index, 0, '-'};
    case RawKind::Trigger: return {Kind::Axis, raw.index, 0, 0, raw.inverted};
    default: return {Kind::Hat, raw.index, 1u << (static_cast<unsigned>(raw.kind) - 1)};
    }
}
// For naming a bound input. A hat mask of more than one direction has no name.
inline std::optional<RawInput> ToRawInput(const MappingInput& input) {
    switch (input.kind) {
    case MappingInput::Kind::Button: return RawInput{RawKind::Button, input.index};
    case MappingInput::Kind::Axis:
        if (!input.half) return RawInput{RawKind::Trigger, input.index, false, input.inverted};
        return RawInput{input.half == '+' ? RawKind::AxisPlus : RawKind::AxisMinus, input.index};
    default:
        for (unsigned bit = 0; bit < 4; ++bit)
            if (input.hatMask == 1u << bit) return RawInput{static_cast<RawKind>(bit + 1), input.index};
        return {};
    }
}
// SDL's gamepad outputs (map_StringForGamepadButton and map_StringForGamepadAxis).
inline constexpr std::array<std::string_view, 32> MappingOutputs = {
    "a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick", "leftshoulder", "rightshoulder",
    "dpup", "dpdown", "dpleft", "dpright", "misc1", "misc2", "misc3", "misc4", "misc5", "misc6",
    "paddle1", "paddle2", "paddle3", "paddle4", "touchpad", "leftx", "lefty", "rightx", "righty", "lefttrigger", "righttrigger"};
// The output a key drives, without the + or - half SDL allows on it.
inline std::string_view MappingTarget(std::string_view key) {
    if (!key.empty() && (key.front() == '+' || key.front() == '-')) key.remove_prefix(1);
    return key;
}
struct MappingBinding { std::string output, value; MappingInput input; };
struct MappingField { std::string key, value; };
struct SdlMapping { std::string guid, name; std::vector<MappingBinding> bindings; std::vector<MappingField> metadata; };
// A field is a binding when its key is an output and its value an input;
// anything else stays as it is. platform is left out and written once, last:
// SDL cuts a stored mapping at a second platform field (CreateMappingString).
inline SdlMapping ParseMapping(const std::string& text) {
    SdlMapping mapping;
    for (std::size_t at = 0, column = 0; at <= text.size(); ++column) {
        const auto end = (std::min)(text.find(',', at), text.size());
        const auto field = text.substr(at, end - at); at = end + 1;
        const auto colon = field.find(':');
        if (column == 0) mapping.guid = field;
        else if (column == 1) mapping.name = field;
        else if (colon != std::string::npos) {
            auto key = field.substr(0, colon), value = field.substr(colon + 1);
            if (key == "platform") continue;
            const auto target = MappingTarget(key);
            const auto input = std::find(MappingOutputs.begin(), MappingOutputs.end(), target) != MappingOutputs.end() ?
                ParseMappingInput(value) : std::nullopt;
            if (input) mapping.bindings.push_back({std::move(key), std::move(value), *input});
            else mapping.metadata.push_back({std::move(key), std::move(value)});
        }
    }
    return mapping;
}
inline std::string FormatMapping(const SdlMapping& mapping) {
    std::string text = mapping.guid + "," + mapping.name + ",";
    for (const auto& binding : mapping.bindings) text += binding.output + ":" + binding.value + ",";
    for (const auto& field : mapping.metadata) text += field.key + ":" + field.value + ",";
    return text + "platform:Windows,";
}
} }
