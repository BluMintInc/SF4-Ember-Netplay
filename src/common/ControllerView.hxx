#pragma once
#include "GamepadBrand.hxx"
#include "XInputMapping.hxx"
#include <string>
#include <vector>

namespace sf4e { namespace gamepads {
enum class Layer { Active, GameReads, Failed };
enum class RowState { Recognised, Steam, Unmapped, NotInUse, Absent, GameRead };
enum class RawKind { Button, HatUp, HatRight, HatDown, HatLeft, AxisPlus, AxisMinus, Trigger };
struct RawInput { RawKind kind; int index; bool numberedHat = false; };
struct RawState {
    std::vector<bool> buttons;
    std::vector<unsigned> hats;
    std::vector<int> axes, rest;
};
inline std::vector<RawInput> HeldRaw(const RawState& state) {
    std::vector<RawInput> held;
    for (unsigned i = 0; i < state.buttons.size(); ++i)
        if (state.buttons[i]) held.push_back({RawKind::Button, static_cast<int>(i)});
    const RawKind directions[] = {RawKind::HatUp, RawKind::HatRight, RawKind::HatDown, RawKind::HatLeft};
    for (unsigned i = 0; i < state.hats.size(); ++i)
        for (unsigned bit = 0; bit < 4; ++bit)
            if (state.hats[i] & (1u << bit)) held.push_back({directions[bit], static_cast<int>(i), state.hats.size() > 1});
    for (unsigned i = 0; i < state.axes.size() && i < state.rest.size(); ++i) {
        const int delta = state.axes[i] - state.rest[i];
        if (delta > 16000 || delta < -16000) {
            const auto kind = state.rest[i] <= -16000 || state.rest[i] >= 16000 ? RawKind::Trigger :
                delta > 0 ? RawKind::AxisPlus : RawKind::AxisMinus;
            held.push_back({kind, static_cast<int>(i)});
        }
    }
    return held;
}
inline std::vector<std::string> HeldGlyphs(const State& state) {
    std::vector<std::string> held;
    const struct { const char* id; Button button; bool axis; } inputs[] = {
        {"up", Button::Up, state.leftY < -16384}, {"down", Button::Down, state.leftY > 16383},
        {"left", Button::Left, state.leftX < -16384}, {"right", Button::Right, state.leftX > 16383},
        {"A", Button::South, false}, {"B", Button::East, false}, {"X", Button::West, false}, {"Y", Button::North, false},
        {"LB", Button::LeftShoulder, false}, {"RB", Button::RightShoulder, false},
        {"View", Button::Back, false}, {"Start", Button::Start, false}
    };
    for (const auto& input : inputs)
        if (input.axis || state.buttons[static_cast<unsigned>(input.button)]) held.emplace_back(input.id);
    if (state.leftTrigger > 16383) held.emplace_back("LT");
    if (state.rightTrigger > 16383) held.emplace_back("RT");
    return held;
}
inline const char* ControllerPicture(const Pad& pad) {
    if (pad.hasEmberMapping || pad.arcadeStick) return "generic_joystick_red";
    switch (pad.type) {
    case Type::Xbox360: return "controller_xbox360";
    case Type::XboxOne: return "controller_xboxseries";
    case Type::PS3: return "controller_playstation3";
    case Type::PS4: return "controller_playstation4";
    case Type::PS5: return "controller_playstation5";
    case Type::SwitchPro: return "controller_switch_pro";
    case Type::JoyConLeft: case Type::JoyConRight: case Type::JoyConPair: return "controller_switch";
    default: return SteamVirtual(pad) ? "controller_steam" : "controller_generic";
    }
}
// One copied shape for the list, capture pictures and per-frame input test.
// id is a stable session row number, independent of SDL instance and game slot.
struct PadView {
    unsigned id = 0;
    unsigned connectionOrder = 0;
    Identity identity;
    std::string name, picture;
    PromptBrand brand;
    Type artVariant = Type::Unknown;
    int slot = -1, directInputIndex = -1;
    RowState state = RowState::Recognised;
    bool gameplay = false, customMapping = false, pressed = false;
    std::vector<std::string> held;
    std::vector<RawInput> raw;
};
struct PadFrame {
    Pad pad;
    State state;
    std::vector<RawInput> raw;
    bool pressed = false;
};
struct NativeController {
    int index;
    std::string instanceName, productName, path;
};
inline void OrderPadRows(std::vector<PadView>& pads) {
    const auto order = [](const PadView& pad) {
        if (pad.state == RowState::Recognised || pad.state == RowState::Steam) return std::make_pair(0, pad.slot);
        if (pad.state == RowState::Unmapped || pad.state == RowState::GameRead) return std::make_pair(1, pad.directInputIndex);
        return std::make_pair(pad.state == RowState::NotInUse ? 2 : 3,
            static_cast<int>(pad.state == RowState::NotInUse ? pad.connectionOrder : pad.id));
    };
    std::stable_sort(pads.begin(), pads.end(), [&](const PadView& a, const PadView& b) { return order(a) < order(b); });
}
inline bool LayerPending(bool savedControllerLayer, bool runningControllerLayer) { return savedControllerLayer != runningControllerLayer; }
} }
