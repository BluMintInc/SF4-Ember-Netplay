#pragma once
#include <cstdint>
#include <cstring>
namespace sf4e { namespace input {
// ReplayExport: a replay is being exported in the game's battle log. Ember's
// menu can be opened there with F10 to follow or cancel it, and the pad then
// drives it; Start stays the game's, whose pause it opens, so it does not
// open Ember's menu there.
enum class MenuContext { Unavailable, MainMenu, OfflineTraining, ReplayExport };
// The pad drives Ember's menus at the main menu, the training controls in
// offline Training (opened there by Back and Start, TrainingPad.hxx), and the
// menu F10 opened over an export. Start opens Ember at the main menu only.
inline bool ControllerMenuAvailable(MenuContext context) { return context != MenuContext::Unavailable; }
inline bool ControllerOpensMenu(MenuContext context) { return context == MenuContext::MainMenu; }
// Where Ember's menu may hold the pads, decided on the game thread from the
// context each pad update sees. What the drawing thread last published (the
// menu shown, or an open asked for) holds them only here, so it never holds
// them on once the menu has gone, even with no frame drawn to say it hid:
// never while a match is prepared or played, nor after it until the game is
// back where the menu can be shown (the main menu, or over an export), and
// away from there only as long as the menu itself stays available
// (OverlayPresentation::AwayFrames), which rides out the native menu's
// brief drops.
class ShellPadPlace {
public:
    static constexpr unsigned AwayUpdates = 30;
    bool Update(MenuContext context, bool fighting) {
        if (fighting) away_ = AwayUpdates;
        else if (context == MenuContext::MainMenu || context == MenuContext::ReplayExport) away_ = 0;
        else if (away_ < AwayUpdates) ++away_;
        return away_ < AwayUpdates;
    }
private:
    // Away until the first update where the menu can be shown.
    unsigned away_ = AwayUpdates;
};
// Ember's menu takes the pads while it holds them, and where Start would open
// it, with the game window in front or behind another: the pads still reach
// a window behind, so the menu never loses them (BackgroundPlay::PolicyFor).
inline bool MenuTakesPads(bool shellHolds, MenuContext context, bool fighting) {
    return shellHolds || (!fighting && ControllerOpensMenu(context));
}
// Native Pad::System publishes five adjacent uint32 input caches per player.
// Keep assignment metadata and provider input untouched for overlay polling.
inline unsigned NativeMenuHeld(const void* system) {
    const auto* bytes=static_cast<const unsigned char*>(system);unsigned held=0;
    for(unsigned side=0;side<2;++side){unsigned value;std::memcpy(&value,bytes+0x18+side*0x50,4);held|=value;}
    return held;
}
constexpr unsigned NativeStart = 0x200;
inline void ClearNativeMenuInputs(void* system, unsigned allowed = 0) {
    auto* bytes=static_cast<unsigned char*>(system);
    for(unsigned side=0;side<2;++side)for(unsigned cache=0;cache<5;++cache) {
        auto* address=bytes+0x18+side*0x50+cache*sizeof(std::uint32_t);
        std::uint32_t value;std::memcpy(&value,address,sizeof(value));value&=allowed;
        std::memcpy(address,&value,sizeof(value));
    }
}
class MenuInputCapture {
public:
    bool Update(bool capture,unsigned held) {
        if(capture)draining_=true;
        else if(!held)draining_=false;
        return capture||draining_;
    }
private:
    bool draining_=false;
};
} }
