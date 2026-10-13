#pragma once
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace sf4e { namespace gamepads {
enum class RawKind { Button, HatUp, HatRight, HatDown, HatLeft, AxisPlus, AxisMinus, Trigger };
struct RawInput {
    RawKind kind = RawKind::Button;
    int index = 0;
    bool numberedHat = false, inverted = false;
    bool operator==(const RawInput& b) const { return kind == b.kind && index == b.index && inverted == b.inverted; }
};
struct RawState {
    std::vector<bool> buttons;
    std::vector<unsigned> hats;
    std::vector<int> axes, rest;
};
inline std::string RawBinding(const RawInput& input) {
    const auto i = std::to_string(input.index);
    switch (input.kind) {
    case RawKind::Button: return "b" + i;
    case RawKind::AxisPlus: return "+a" + i;
    case RawKind::AxisMinus: return "-a" + i;
    case RawKind::Trigger: return "a" + i + (input.inverted ? "~" : "");
    default: return "h" + i + "." + std::to_string(1u << (static_cast<unsigned>(input.kind) - 1));
    }
}
// The inverse of RawBinding: bN, hN.mask, signed +aN/-aN, and aN or aN~.
inline std::optional<RawInput> ParseRawBinding(std::string value) {
    if (value.empty()) return {};
    RawInput input;
    if (value[0] == '+' || value[0] == '-') {
        input.kind = value[0] == '+' ? RawKind::AxisPlus : RawKind::AxisMinus;
        value.erase(0, 1);
        if (value.empty() || value[0] != 'a') return {};
    } else input.kind = value[0] == 'b' ? RawKind::Button : value[0] == 'h' ? RawKind::HatUp : RawKind::Trigger;
    const char kind = value[0];
    if (kind != 'a' && kind != 'b' && kind != 'h') return {};
    const auto parsed = std::from_chars(value.data() + 1, value.data() + value.size(), input.index);
    if (parsed.ec != std::errc{} || input.index < 0) return {};
    if (kind == 'h') {
        if (parsed.ptr == value.data() + value.size() || *parsed.ptr != '.') return {};
        unsigned mask = 0;
        const auto hat = std::from_chars(parsed.ptr + 1, value.data() + value.size(), mask);
        if (hat.ec != std::errc{} || hat.ptr != value.data() + value.size()) return {};
        if (mask == 1) input.kind = RawKind::HatUp;
        else if (mask == 2) input.kind = RawKind::HatRight;
        else if (mask == 4) input.kind = RawKind::HatDown;
        else if (mask == 8) input.kind = RawKind::HatLeft;
        else return {};
    } else if (parsed.ptr != value.data() + value.size()) {
        if (kind != 'a' || *parsed.ptr != '~' || parsed.ptr + 1 != value.data() + value.size()) return {};
        input.inverted = true;
    }
    return input;
}
// Whether two bindings read the same physical input. A full axis (aN or aN~)
// covers both of its halves.
inline bool SharesInput(const RawInput& a, const RawInput& b) {
    const auto axis = [](RawKind kind) { return kind >= RawKind::AxisPlus; };
    if (axis(a.kind) && axis(b.kind))
        return a.index == b.index && (a.kind == b.kind || a.kind == RawKind::Trigger || b.kind == RawKind::Trigger);
    return a.kind == b.kind && a.index == b.index;
}
inline std::vector<RawInput> HeldRaw(const RawState& state, int threshold = 16000) {
    std::vector<RawInput> held;
    for (unsigned i = 0; i < state.buttons.size(); ++i)
        if (state.buttons[i]) held.push_back({RawKind::Button, static_cast<int>(i)});
    for (unsigned i = 0; i < state.hats.size(); ++i)
        for (unsigned bit = 0; bit < 4; ++bit)
            if (state.hats[i] & (1u << bit))
                held.push_back({static_cast<RawKind>(bit + 1), static_cast<int>(i), state.hats.size() > 1});
    for (unsigned i = 0; i < state.axes.size() && i < state.rest.size(); ++i) {
        const int delta = state.axes[i] - state.rest[i];
        if (std::abs(delta) > threshold) {
            const auto kind = std::abs(state.rest[i]) > 24000 ? RawKind::Trigger :
                delta > 0 ? RawKind::AxisPlus : RawKind::AxisMinus;
            held.push_back({kind, static_cast<int>(i), false, kind == RawKind::Trigger && state.rest[i] > 0});
        }
    }
    return held;
}
struct SetupAxisEntry { int index, rest; };
struct SetupReading {
    bool releaseButtons = false, ready = false, atRest = true;
    std::optional<RawInput> owner;
    std::vector<RawInput> held, ignored;
};
// Samples rest after entry and reconnect. One input owns each gesture until
// every input is at rest, including inputs that arrived after the owner.
class SetupInputReader {
public:
    void Reset(const std::optional<SetupAxisEntry>& entry = {}) { *this = {}; entry_ = entry; }
    SetupReading Read(std::uint64_t now, const RawState& raw) {
        SetupReading out;
        if (entry_) {
            if (entry_->index < static_cast<int>(raw.axes.size()) && std::abs(raw.axes[entry_->index] - entry_->rest) > 8000) {
                out.releaseButtons = true; out.atRest = false; return out;
            }
            entry_.reset();
        }
        if (!started_) { started_ = true; since_ = now; }
        if (!sampled_) {
            const bool held = std::any_of(raw.buttons.begin(), raw.buttons.end(), [](bool b) { return b; });
            if (held && now - since_ < 5000) { out.releaseButtons = true; out.atRest = false; return out; }
            if (std::any_of(raw.hats.begin(), raw.hats.end(), [](unsigned hat) { return hat != 0; })) { out.atRest = false; return out; }
            rest_ = raw.axes; ignored_ = raw.buttons;
            for (unsigned i = 0; i < ignored_.size(); ++i)
                if (ignored_[i]) out.ignored.push_back({RawKind::Button, static_cast<int>(i)});
            sampled_ = true;
        }
        RawState state = raw; state.rest = rest_;
        for (unsigned i = 0; i < state.buttons.size() && i < ignored_.size(); ++i)
            if (ignored_[i]) state.buttons[i] = false;
        auto activeState = state;
        for (auto& hat : activeState.hats) if (hat != 1 && hat != 2 && hat != 4 && hat != 8) hat = 0;
        auto active = HeldRaw(activeState);
        // Axis hysteresis retains the active direction until within 8000 of rest.
        for (const auto& old : held_) if (old.kind >= RawKind::AxisPlus &&
            old.index < static_cast<int>(state.axes.size()) && old.index < static_cast<int>(rest_.size()) &&
            (old.kind == RawKind::AxisPlus ? state.axes[old.index] - rest_[old.index] > 8000 :
                old.kind == RawKind::AxisMinus ? state.axes[old.index] - rest_[old.index] < -8000 :
                std::abs(state.axes[old.index] - rest_[old.index]) > 8000) &&
            std::find(active.begin(), active.end(), old) == active.end()) active.push_back(old);
        if (!owner_ && !active.empty()) owner_ = active.front();
        out.ready = true; out.held = active; out.owner = owner_;
        // A diagonal is not recordable, but is not a release either.
        out.atRest = HeldRaw(state, 8000).empty();
        held_ = std::move(active);
        if (out.atRest) owner_.reset();
        return out;
    }
    const std::vector<int>& Rest() const { return rest_; }
private:
    bool started_ = false, sampled_ = false;
    std::uint64_t since_ = 0;
    std::vector<int> rest_;
    std::vector<bool> ignored_;
    std::vector<RawInput> held_;
    std::optional<RawInput> owner_;
    std::optional<SetupAxisEntry> entry_;
};
} }
