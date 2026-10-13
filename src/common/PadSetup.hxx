#pragma once
#include "PadSetupInput.hxx"
#include <array>
#include <string_view>
#include <utility>

namespace sf4e { namespace gamepads {
// A stick step maps the whole axis its push lands on (5.3).
enum class StepKind { Direction, Button, Stick };
struct SetupStep { const char* name; const char* nameKey; const char* target; bool required; StepKind kind; };
inline constexpr std::array<SetupStep, 20> SetupSteps = {{
    {"up", "pad_setup.step.up", "dpup", true, StepKind::Direction}, {"down", "pad_setup.step.down", "dpdown", true, StepKind::Direction},
    {"left", "pad_setup.step.left", "dpleft", true, StepKind::Direction}, {"right", "pad_setup.step.right", "dpright", true, StepKind::Direction},
    {"lp", "pad_setup.step.lp", "x", true, StepKind::Button}, {"mp", "pad_setup.step.mp", "y", true, StepKind::Button},
    {"hp", "pad_setup.step.hp", "rightshoulder", true, StepKind::Button}, {"lk", "pad_setup.step.lk", "a", true, StepKind::Button},
    {"mk", "pad_setup.step.mk", "b", true, StepKind::Button}, {"hk", "pad_setup.step.hk", "righttrigger", true, StepKind::Button},
    {"3p", "pad_setup.step.3p", "leftshoulder", false, StepKind::Button}, {"3k", "pad_setup.step.3k", "lefttrigger", false, StepKind::Button},
    {"ls", "pad_setup.step.ls", "leftstick", false, StepKind::Button}, {"rs", "pad_setup.step.rs", "rightstick", false, StepKind::Button},
    {"lx", "pad_setup.step.lx", "leftx", false, StepKind::Stick}, {"ly", "pad_setup.step.ly", "lefty", false, StepKind::Stick},
    {"rx", "pad_setup.step.rx", "rightx", false, StepKind::Stick}, {"ry", "pad_setup.step.ry", "righty", false, StepKind::Stick},
    {"start", "pad_setup.step.start", "start", true, StepKind::Button}, {"select", "pad_setup.step.select", "back", false, StepKind::Button}
}};
inline constexpr int SetupStepCount = static_cast<int>(SetupSteps.size());
// Steps read by position: summary navigation and the tap ladder's back and skip.
inline constexpr int UpStep = 0, DownStep = 1, LeftStep = 2, RightStep = 3, LkStep = 7, MkStep = 8;
static_assert(std::string_view(SetupSteps[UpStep].target) == "dpup" && std::string_view(SetupSteps[DownStep].target) == "dpdown" &&
    std::string_view(SetupSteps[LeftStep].target) == "dpleft" && std::string_view(SetupSteps[RightStep].target) == "dpright" &&
    std::string_view(SetupSteps[LkStep].target) == "a" && std::string_view(SetupSteps[MkStep].target) == "b", "named steps moved");
using SetupInputs = std::array<std::optional<RawInput>, SetupSteps.size()>;
// A stick step maps the full axis: a push to plus gives aN, a push to minus aN~.
inline RawInput MappedInput(int step, RawInput input) {
    if (SetupSteps[step].kind == StepKind::Stick && (input.kind == RawKind::AxisPlus || input.kind == RawKind::AxisMinus))
        input = {RawKind::Trigger, input.index, false, input.kind == RawKind::AxisMinus};
    return input;
}
enum class SetupMode { Capture, Summary };
enum class SetupFeedback { None, Conflict, Required, Idle, Reconnect };
enum class SetupCommand { None, Escape, Backspace, Space, Redo, Restart, Cancel, Save, LeftMenu, Activity };
enum class SetupEnd { None, Hold, Escape, Confirm, Disconnect, Idle, LeftMenu, Saved, SaveFailed };
inline const char* SetupEndName(SetupEnd end) {
    switch (end) {
    case SetupEnd::Hold: return "hold";
    case SetupEnd::Escape: return "escape";
    case SetupEnd::Confirm: return "confirm";
    case SetupEnd::Disconnect: return "disconnect";
    case SetupEnd::Idle: return "idle";
    case SetupEnd::LeftMenu: return "left menu";
    case SetupEnd::Saved: return "saved";
    case SetupEnd::SaveFailed: return "save failed";
    default: return "";
    }
}
enum class SetupEventKind { Record, Back, Skip, Conflict, Required, Cancel, Save, Stopped, Ignored };
struct SetupEvent {
    SetupEventKind kind;
    int step = 0, other = -1;
    std::optional<RawInput> input;
    SetupEnd reason = SetupEnd::None;
};
enum class SetupMenuAction { None, Select, Back };
struct SetupSummaryInput {
    bool up = false, down = false;
    SetupMenuAction action = SetupMenuAction::None;
    std::uint64_t sequence = 0;
};
// Summary selection uses the same tap ladder, so holding LK on Save can
// still cancel. Latched actions survive until the drawing thread sees them.
class SetupSummaryControls {
public:
    SetupSummaryInput Read(std::uint64_t now, const SetupInputs& recorded, const std::vector<RawInput>& held) {
        const auto pressed = [&](int step) { return recorded[step] && std::find(held.begin(), held.end(), *recorded[step]) != held.end(); };
        state_.up = pressed(UpStep); state_.down = pressed(DownStep);
        constexpr int menuSteps[] = {LkStep, MkStep};
        for (int i = 0; i < 2; ++i) {
            if (pressed(menuSteps[i])) { if (!since_[i]) since_[i] = now; }
            else if (since_[i]) {
                if (now - *since_[i] < 1000) { state_.action = i == 0 ? SetupMenuAction::Select : SetupMenuAction::Back; ++state_.sequence; }
                since_[i].reset();
            }
        }
        return state_;
    }
    void Release() { since_ = {}; state_.up = state_.down = false; }
private:
    SetupSummaryInput state_;
    std::array<std::optional<std::uint64_t>, 2> since_;
};
struct SetupState {
    bool active = false, releaseButtons = false;
    SetupMode mode = SetupMode::Capture;
    int step = 0, conflictStep = -1;
    SetupInputs recorded;
    std::array<bool, SetupSteps.size()> skipped{};
    std::vector<RawInput> held;
    std::uint64_t holdMs = 0;
    SetupFeedback feedback = SetupFeedback::None;
    std::optional<RawInput> conflictInput;
    int reconnectSeconds = 0;
    SetupSummaryInput summaryInput;
    bool CanSave() const {
        for (unsigned i = 0; i < SetupSteps.size(); ++i) if (SetupSteps[i].required && !recorded[i]) return false;
        return true;
    }
};
class PadSetup {
public:
    void Start(std::uint64_t now, const std::optional<SetupAxisEntry>& entry = {}) { *this = {}; reader_.Reset(entry); state_.active = true; lastInput_ = now; }
    const SetupState& State() const { return state_; }
    const std::vector<int>& Rest() const { return reader_.Rest(); }
    std::vector<SetupEvent> Update(std::uint64_t now, const RawState& raw, bool connected = true,
        SetupCommand command = SetupCommand::None, int step = 0) {
        events_.clear();
        if (!state_.active) return events_;
        if (command != SetupCommand::None) lastInput_ = now;
        if (command == SetupCommand::LeftMenu) Stop(SetupEventKind::Cancel, SetupEnd::LeftMenu);
        else if (command == SetupCommand::Cancel || (command == SetupCommand::Escape && state_.mode == SetupMode::Capture))
            Stop(SetupEventKind::Cancel, command == SetupCommand::Escape ? SetupEnd::Escape : SetupEnd::Confirm);
        if (!state_.active) return events_;
        if (!connected) {
            if (!disconnected_) { disconnected_ = now; gesture_ = Gesture::Ready; state_.holdMs = 0; state_.held.clear(); state_.summaryInput.up = state_.summaryInput.down = false; summaryControls_.Release(); }
            if (now - *disconnected_ >= 15000) Stop(SetupEventKind::Stopped, SetupEnd::Disconnect);
            else { state_.feedback = SetupFeedback::Reconnect; state_.reconnectSeconds = static_cast<int>((15000 - (now - *disconnected_) + 999) / 1000); }
            return events_;
        }
        if (disconnected_) {
            if (now - *disconnected_ >= 15000) { Stop(SetupEventKind::Stopped, SetupEnd::Disconnect); return events_; }
            disconnected_.reset(); reader_.Reset(); lastInput_ = now; state_.feedback = SetupFeedback::None;
        }
        if (command == SetupCommand::Restart) {
            state_ = {}; state_.active = true; redo_ = false; lastInput_ = now; feedbackUntil_ = 0;
            gesture_ = Gesture::WaitForRest; summaryControls_.Release();
        }
        if (command == SetupCommand::Redo && state_.mode == SetupMode::Summary && step >= 0 && step < SetupStepCount) {
            state_.step = step; Clear(step); state_.mode = SetupMode::Capture; redo_ = true; gesture_ = Gesture::WaitForRest; state_.holdMs = 0;
        }
        if (command == SetupCommand::Save && state_.mode == SetupMode::Summary && state_.CanSave())
            events_.push_back({SetupEventKind::Save});
        if (state_.mode == SetupMode::Capture) {
            if (command == SetupCommand::Backspace || command == SetupCommand::Space) {
                gesture_ = Gesture::WaitForRest; state_.holdMs = 0;
            }
            if (command == SetupCommand::Backspace) Back();
            if (command == SetupCommand::Space) Skip(now);
        }
        const auto reading = reader_.Read(now, raw);
        state_.releaseButtons = !reading.ready; state_.held = reading.held;
        for (const auto& input : reading.ignored) events_.push_back({SetupEventKind::Ignored, state_.step, -1, input});
        if (!reading.atRest || command != SetupCommand::None) lastInput_ = now;
        if (reading.owner && gesture_ == Gesture::Ready) { gesture_ = Gesture::Holding; pressedAt_ = now; }
        if (gesture_ == Gesture::Holding) {
            const auto elapsed = now - pressedAt_;
            state_.holdMs = elapsed;
            if (elapsed >= 3000) { Stop(SetupEventKind::Cancel, SetupEnd::Hold); return events_; }
            if (std::find(reading.held.begin(), reading.held.end(), *reading.owner) == reading.held.end()) {
                gesture_ = elapsed < 1000 && state_.mode == SetupMode::Capture ? Gesture::TapPending : Gesture::WaitForRest;
                state_.holdMs = 0;
            }
        }
        if (reading.atRest) {
            if (gesture_ == Gesture::TapPending && reading.owner) Tap(*reading.owner, now);
            gesture_ = Gesture::Ready;
        }
        if (state_.mode == SetupMode::Summary) state_.summaryInput = summaryControls_.Read(now, state_.recorded, reading.held);
        else { summaryControls_.Release(); state_.summaryInput.up = state_.summaryInput.down = false; }
        if (now - lastInput_ >= 60000) Stop(SetupEventKind::Stopped, SetupEnd::Idle);
        else if (now >= feedbackUntil_) state_.feedback = state_.mode == SetupMode::Capture && now - lastInput_ >= 10000 ? SetupFeedback::Idle : SetupFeedback::None;
        return events_;
    }
private:
    void Clear(int step) { state_.recorded[step].reset(); state_.skipped[step] = false; }
    void Stop(SetupEventKind kind, SetupEnd reason) { state_.active = false; state_.summaryInput = {}; events_.push_back({kind, state_.step, -1, {}, reason}); }
    void Back() {
        if (!state_.step) return;
        --state_.step; Clear(state_.step); redo_ = false;
        events_.push_back({SetupEventKind::Back, state_.step});
    }
    void Advance() {
        if (redo_) { redo_ = false; state_.mode = SetupMode::Summary; }
        else if (++state_.step == SetupStepCount) { state_.step = SetupStepCount - 1; state_.mode = SetupMode::Summary; }
    }
    void Skip(std::uint64_t now) {
        if (SetupSteps[state_.step].required) {
            state_.feedback = SetupFeedback::Required; feedbackUntil_ = now + 2000;
            events_.push_back({SetupEventKind::Required, state_.step});
        } else { state_.recorded[state_.step].reset(); state_.skipped[state_.step] = true; events_.push_back({SetupEventKind::Skip, state_.step}); Advance(); }
    }
    void Tap(const RawInput& input, std::uint64_t now) {
        if (state_.step > RightStep && state_.recorded[LeftStep] && input == *state_.recorded[LeftStep]) { Back(); return; }
        if (state_.step > RightStep && state_.recorded[RightStep] && input == *state_.recorded[RightStep]) { Skip(now); return; }
        // A stick step's full axis also conflicts with either half recorded elsewhere.
        for (int i = 0; i < SetupStepCount; ++i) if (i != state_.step && state_.recorded[i] &&
            SharesInput(MappedInput(state_.step, input), MappedInput(i, *state_.recorded[i]))) {
            state_.feedback = SetupFeedback::Conflict; state_.conflictStep = i; state_.conflictInput = input; feedbackUntil_ = now + 2000;
            events_.push_back({SetupEventKind::Conflict, state_.step, i, input}); return;
        }
        state_.recorded[state_.step] = input; state_.skipped[state_.step] = false;
        state_.feedback = SetupFeedback::None; feedbackUntil_ = 0;
        events_.push_back({SetupEventKind::Record, state_.step, -1, input}); Advance();
    }
    SetupState state_;
    SetupInputReader reader_;
    SetupSummaryControls summaryControls_;
    std::vector<SetupEvent> events_;
    // The reader owns the input. The model only tracks the gesture's phase.
    enum class Gesture { Ready, Holding, TapPending, WaitForRest };
    Gesture gesture_ = Gesture::Ready;
    std::optional<std::uint64_t> disconnected_;
    std::uint64_t lastInput_ = 0, pressedAt_ = 0, feedbackUntil_ = 0;
    bool redo_ = false;
};
using MappingField = std::pair<std::string, std::string>;
// An SDL mapping string: guid, name, then key:value fields in order. Other
// keys (crc, hint, type) stay fields; platform is left out and written once.
struct SdlMapping { std::string guid, name; std::vector<MappingField> fields; };
inline SdlMapping ParseMapping(const std::string& text) {
    SdlMapping mapping;
    for (std::size_t at = 0, column = 0; at <= text.size(); ++column) {
        const auto end = (std::min)(text.find(',', at), text.size());
        const auto field = text.substr(at, end - at); at = end + 1;
        const auto colon = field.find(':');
        if (column == 0) mapping.guid = field;
        else if (column == 1) mapping.name = field;
        else if (colon != std::string::npos && field.compare(0, colon, "platform") != 0) mapping.fields.emplace_back(field.substr(0, colon), field.substr(colon + 1));
    }
    return mapping;
}
inline std::string FormatMapping(const SdlMapping& mapping) {
    std::string text = mapping.guid + "," + mapping.name + ",";
    for (const auto& field : mapping.fields) text += field.first + ":" + field.second + ",";
    return text + "platform:Windows,";
}
// A stick step that recorded a button, hat or trigger drives the plus half of its axis.
inline MappingField SetupField(int step, const RawInput& input) {
    const auto& setup = SetupSteps[step];
    const bool halfAxis = input.kind == RawKind::AxisPlus || input.kind == RawKind::AxisMinus;
    if (setup.kind == StepKind::Stick && !halfAxis) return {std::string("+") + setup.target, RawBinding(input)};
    return {setup.target, RawBinding(MappedInput(step, input))};
}
// Over the mapping SDL already uses for the pad (its own or an earlier Ember
// setup), recorded steps take their targets and the inputs they read; every
// other field stays, so a skipped step keeps the pad's own binding (5.9).
inline std::string BuildSetupMapping(const std::string& guid, std::string name, const SetupState& state, const std::string& existing = {}) {
    // Commas delimit SDL fields; driver names must not create mapping fields.
    std::replace(name.begin(), name.end(), ',', ' ');
    SdlMapping mapping{guid, name, {}};
    std::vector<RawInput> used;
    for (int i = 0; i < SetupStepCount; ++i) if (state.recorded[i]) {
        mapping.fields.push_back(SetupField(i, *state.recorded[i]));
        used.push_back(MappedInput(i, *state.recorded[i]));
    }
    const auto output = [](const std::string& key) { return key.empty() || (key[0] != '+' && key[0] != '-') ? key : key.substr(1); };
    const auto recorded = mapping.fields.size();
    for (auto& field : ParseMapping(existing).fields) {
        const auto input = ParseRawBinding(field.second);
        const bool retargeted = std::any_of(mapping.fields.begin(), mapping.fields.begin() + recorded,
            [&](const MappingField& step) { return output(step.first) == output(field.first); });
        const bool reused = input && std::any_of(used.begin(), used.end(), [&](const RawInput& step) { return SharesInput(step, *input); });
        if (!retargeted && !reused) mapping.fields.push_back(std::move(field));
    }
    return FormatMapping(mapping);
}
} }
