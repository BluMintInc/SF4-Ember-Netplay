#pragma once

namespace sf4e { namespace match_frame_meter {

// The frame meter over a match is for private rooms. A public room never
// shows it, whatever the player saved; the saved choice is kept for the
// private rooms they play in.
inline bool Allowed(bool publicRoom) { return !publicRoom; }

// Whether the runtime watches matches for the meter: the player's choice where
// it is allowed, or a replay that asks for it (a replay is not in a room).
inline bool Watched(bool preference, bool publicRoom, bool replayMeter) {
    return (preference && Allowed(publicRoom)) || replayMeter;
}

} }
