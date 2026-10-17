#pragma once
#include "../netplay/SessionController.hxx"

namespace sf4e { namespace input {

// The game's own main menu takes no input while a room is joined (or opening,
// closing or lost) or a game is being prepared or played, whether or not
// Ember's menu is on screen. Inside a room the only way on from it is
// Ember's own pages.
enum class NativeMenuLock { None, Room, Match };

inline NativeMenuLock NativeMenuLockFor(netplay::RoomState room, netplay::MatchState match) {
    if (netplay::Fighting(match)) return NativeMenuLock::Match;
    return room != netplay::RoomState::Idle ? NativeMenuLock::Room : NativeMenuLock::None;
}

inline const char* RoomStateName(netplay::RoomState room) {
    switch (room) {
    case netplay::RoomState::Idle: return "idle";
    case netplay::RoomState::Opening: return "opening";
    case netplay::RoomState::Joined: return "joined";
    case netplay::RoomState::Closing: return "closing";
    case netplay::RoomState::Lost: return "lost";
    }
    return "?";
}

inline const char* MatchStateName(netplay::MatchState match) {
    switch (match) {
    case netplay::MatchState::None: return "none";
    case netplay::MatchState::Preparing: return "preparing";
    case netplay::MatchState::Playing: return "playing";
    case netplay::MatchState::PostMatch: return "post-match";
    case netplay::MatchState::Failed: return "failed";
    }
    return "?";
}

// The main menu's MULTIPLAYER BATTLE item (MMI_NETWORK, Dimps__GameEvents.hxx),
// which Ember turns into its own Play page.
constexpr int NativeNetworkItem = 2;

// Whether a native main menu selection goes through. Unlocked, all of them do.
// Locked, only MULTIPLAYER BATTLE does; nothing else the menu offers has a
// place in a room or a match.
inline bool NativeSelectionAllowed(NativeMenuLock lock, int mode) {
    return lock == NativeMenuLock::None || mode == NativeNetworkItem;
}

// Whether Back on Ember's Home hides Ember. In a room it does not: the game's
// menu behind it is locked, so hiding would leave nothing to use.
inline bool HomeBackHidesShell(netplay::RoomState room) {
    return room == netplay::RoomState::Idle;
}

} }
