#pragma once
#include <atomic>
#include <cstdint>
#include "../common/BattlePause.hxx"
#include "../netplay/SessionController.hxx"
#include "../common/MenuInputCapture.hxx"

namespace sf4e { namespace ui {

inline bool PassiveOverlayShown(bool shellVisible, bool trainingControlsOpen, bool nativePaused) {
    return !shellVisible && !trainingControlsOpen && !nativePaused;
}

// Presentation state survives device recreation. Only native menu states may
// capture input; rendering never changes session or simulation state.
class OverlayPresentation {
public:
    void Update(bool atMenu, netplay::MatchState match, bool offline, bool focused) {
        reopened_ = false;
        const bool fighting = match == netplay::MatchState::Preparing || match == netplay::MatchState::Playing;
        if (match == netplay::MatchState::PostMatch && previousMatch_ != match) reopen_ = true;
        if (offline && !previousOffline_) requested_ = false;
        if (fighting) requested_ = false;
        // The native foreground query drops for a frame or two during menu
        // transitions. Hiding the shell on each drop re-armed every input gate
        // and strobed the room controls, so availability falls only after a
        // sustained absence. A fight or Play Offline still hides immediately.
        awayFrames_ = atMenu ? 0 : awayFrames_ + 1;
        available_ = !fighting && (atMenu || (available_ && awayFrames_ < AwayFrames));
        focused_ = focused;
        if (available_ && reopen_) { requested_ = true; reopen_ = false; reopened_ = true; }
        previousMatch_ = match;
        previousOffline_ = offline;
    }
    bool Reopened() const { return reopened_; }
    bool Available() const { return available_; }
    // Drawn while the game has lost focus (alt-tab), so the room stays on
    // screen, and the pad still drives it there (Open, Close); only the keys
    // and the mouse need focus, and the caller gates those.
    bool Visible() const { return available_ && requested_; }
    // F10's toggle: a key, so only with the focus.
    void Toggle() { if (available_ && focused_) requested_ = !requested_; }
    void Open() { if (available_) requested_ = true; }
    void Close() { requested_ = false; }
    // Consecutive updates away from the main menu before the shell hides: ~0.5 s at 60 fps.
    static constexpr unsigned AwayFrames = 30;
private:
    bool requested_ = true, available_ = false, focused_ = true;
    unsigned awayFrames_ = 0;
    bool reopened_ = false, reopen_ = false, previousOffline_ = false;
    netplay::MatchState previousMatch_ = netplay::MatchState::None;
};

// The game thread asks the drawing thread to open the shell: the native
// Network item wants the Play page, the pad's Start only the shell. Only the
// drawing thread touches the presentation and the shell's navigation, so a
// request waits for its next frame. Requests arriving before then coalesce,
// and Play outranks Controls.
class OpenRequests {
public:
    enum class Kind : int { None = 0, Controls = 1, Play = 2 };
    void Post(Kind kind) {
        int current = value_.load();
        while (current < static_cast<int>(kind) && !value_.compare_exchange_weak(current, static_cast<int>(kind))) {}
    }
    Kind Take() { return static_cast<Kind>(value_.exchange(static_cast<int>(Kind::None))); }
private:
    std::atomic<int> value_{0};
};

// Where Ember's menu may hold the game's pads, decided on the game thread
// from the context each pad update sees, as the presentation decides where
// the menu is available: never while a match is prepared or played, nor
// after it until the game is back where the menu can be shown (the main
// menu, or over an export), and away from there only for the presentation's
// AwayFrames, which ride out the native menu's brief drops.
class ShellPadPlace {
public:
    bool Update(input::MenuContext context, bool fighting) {
        if (fighting) away_ = OverlayPresentation::AwayFrames;
        else if (context == input::MenuContext::MainMenu || context == input::MenuContext::ReplayExport) away_ = 0;
        else if (away_ < OverlayPresentation::AwayFrames) ++away_;
        return away_ < OverlayPresentation::AwayFrames;
    }
private:
    // Away until the first update where the menu can be shown.
    unsigned away_ = OverlayPresentation::AwayFrames;
};

// The game's pads and Ember's menu: one owner for what both threads touch.
// The game thread asks for opens and says, first in each pad update, where
// the game is; the drawing thread takes the opens and publishes the menu
// shown or not. The menu holds the pads while it is shown or an open waits
// for its frame's answer, and only where it may hold them (ShellPadPlace).
// So what the drawing thread last published never holds the pads once the
// menu has gone, even with no frame drawn to say it hid; and the press that
// asked for an open never reaches the game, even when it comes after a
// frame took the requests and before that frame published the menu hidden.
class MenuPads {
public:
    using Kind = OpenRequests::Kind;
    struct Taken { Kind kind = Kind::None; std::uint32_t through = 0; };
    // Game thread.
    void Request(Kind kind) { requests_.Post(kind); posted_.fetch_add(1); }
    void Place(input::MenuContext context, bool fighting) { mayHold_ = place_.Update(context, fighting); }
    // Drawing thread: the open this frame answers, then, once it has decided,
    // the menu shown or not and those opens answered, in that order.
    Taken Take() { Taken taken; taken.through = posted_.load(); taken.kind = requests_.Take(); return taken; }
    void Publish(bool shown, const Taken& taken) { shown_ = shown; answered_ = taken.through; }
    // Any thread.
    bool Holds() const { return mayHold_.load() && (shown_.load() || posted_.load() != answered_.load()); }
private:
    OpenRequests requests_;
    ShellPadPlace place_;
    std::atomic<std::uint32_t> posted_{0}, answered_{0};
    std::atomic<bool> shown_{false}, mayHold_{false};
};
} }
