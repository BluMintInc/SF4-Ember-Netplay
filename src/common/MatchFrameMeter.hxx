#pragma once

namespace sf4e { namespace match_frame_meter {

// The frame meter over a match is for private rooms. A public room never
// shows it, whatever the player saved; the saved choice is kept for the
// private rooms they play in.
inline bool Allowed(bool publicRoom) { return !publicRoom; }

} }
