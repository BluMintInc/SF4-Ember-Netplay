#pragma once
#include "PadSetupInput.hxx"
#include <array>

namespace sf4e { namespace gamepads {
struct SetupStep { const char* name; const char* nameKey; const char* target; bool required; };
inline constexpr std::array<SetupStep, 14> SetupSteps = {{
    {"up", "pad_setup.step.up", "dpup", true}, {"down", "pad_setup.step.down", "dpdown", true}, {"left", "pad_setup.step.left", "dpleft", true}, {"right", "pad_setup.step.right", "dpright", true},
    {"lp", "pad_setup.step.lp", "x", true}, {"mp", "pad_setup.step.mp", "y", true}, {"hp", "pad_setup.step.hp", "rightshoulder", true}, {"lk", "pad_setup.step.lk", "a", true},
    {"mk", "pad_setup.step.mk", "b", true}, {"hk", "pad_setup.step.hk", "righttrigger", true}, {"3p", "pad_setup.step.3p", "leftshoulder", false},
    {"3k", "pad_setup.step.3k", "lefttrigger", false}, {"start", "pad_setup.step.start", "start", true}, {"select", "pad_setup.step.select", "back", false}
}};
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
    SetupSummaryInput Read(std::uint64_t now, const std::array<std::optional<RawInput>, 14>& recorded,
        const std::vector<RawInput>& held) {
        const auto pressed = [&](int step) { return recorded[step] && std::find(held.begin(), held.end(), *recorded[step]) != held.end(); };
        state_.up = pressed(0); state_.down = pressed(1);
        for (int i = 0; i < 2; ++i) {
            if (pressed(i + 7)) { if (!since_[i]) since_[i] = now; }
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
    std::array<std::optional<RawInput>, 14> recorded;
    std::array<bool, 14> skipped{};
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
        if (command == SetupCommand::Redo && state_.mode == SetupMode::Summary && step >= 0 && step < 14) {
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
        else if (++state_.step == 14) { state_.step = 13; state_.mode = SetupMode::Summary; }
    }
    void Skip(std::uint64_t now) {
        if (SetupSteps[state_.step].required) {
            state_.feedback = SetupFeedback::Required; feedbackUntil_ = now + 2000;
            events_.push_back({SetupEventKind::Required, state_.step});
        } else { state_.recorded[state_.step].reset(); state_.skipped[state_.step] = true; events_.push_back({SetupEventKind::Skip, state_.step}); Advance(); }
    }
    void Tap(const RawInput& input, std::uint64_t now) {
        if (state_.step >= 4 && state_.recorded[2] && input == *state_.recorded[2]) { Back(); return; }
        if (state_.step >= 4 && state_.recorded[3] && input == *state_.recorded[3]) { Skip(now); return; }
        for (int i = 0; i < 14; ++i) if (i != state_.step && state_.recorded[i] && input == *state_.recorded[i]) {
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
inline std::string BuildSetupMapping(const std::string& guid, std::string name, const SetupState& state) {
    // Commas delimit SDL fields; driver names must not create mapping fields.
    std::replace(name.begin(), name.end(), ',', ' ');
    std::string mapping = guid + "," + name + ",";
    for (unsigned i = 0; i < SetupSteps.size(); ++i)
        if (state.recorded[i]) mapping += std::string(SetupSteps[i].target) + ":" + RawBinding(*state.recorded[i]) + ",";
    return mapping + "platform:Windows,";
}
} }
