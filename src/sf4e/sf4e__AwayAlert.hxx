#pragma once
#include <cstdint>

// A call back from Training or a ready challenge, told to a player whose game
// window is behind another or minimized (common/AwayAlert.hxx).
namespace sf4e {
	namespace AwayAlert {
		// Game thread, where the call or the challenge is decided, so it
		// comes whether or not frames are drawn. key: common/AwayAlert.hxx
		// Key. While the window is away, once for each: the taskbar button
		// flashes until the window comes forward, and with sound on a chime
		// plays through Windows at volumePercent. Never waits on either.
		void Announce(std::uint64_t key, bool sound, int volumePercent, const char* what);
	}
}
