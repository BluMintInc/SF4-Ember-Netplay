#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace sf4e {

// A Steam account's 64-bit ID, as the room carries it. Valve's CSteamID
// (steamclientpublic.h) packs it as account number in bits 0-31, instance in
// bits 32-51, account type in bits 52-55 and universe in bits 56-63. A player's
// own profile has universe 1 (public), type 1 (individual), instance 1
// (desktop) and a nonzero account number; no other ID names a profile page, so
// no other value becomes a SteamId. A default SteamId is none.
class SteamId {
public:
	SteamId() : value_(0) {}
	bool Present() const { return value_ != 0; }
	std::uint64_t Value() const { return value_; }
	// The exact decimal text, which is also the part of a profile address.
	std::string ToString() const { return std::to_string(value_); }

	static bool FromValue(std::uint64_t value, SteamId& out) {
		if ((value >> 56) != 1 || ((value >> 52) & 0xF) != 1 || ((value >> 32) & 0xFFFFF) != 1 || (value & 0xFFFFFFFFu) == 0) return false;
		out.value_ = value;
		return true;
	}
	// Canonical decimal only: digits, no sign, no spaces, no leading zero, and a
	// number that fits 64 bits. `out` is left alone when it is refused.
	static bool Parse(const std::string& text, SteamId& out) {
		if (text.empty() || text.size() > 20 || text[0] == '0') return false;
		std::uint64_t value = 0;
		for (const char c : text) {
			if (c < '0' || c > '9') return false;
			const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
			if (value > (UINT64_MAX - digit) / 10) return false;
			value = value * 10 + digit;
		}
		return FromValue(value, out);
	}
	// None for text that is not a SteamId.
	static SteamId Parsed(const std::string& text) {
		SteamId id;
		if (!Parse(text, id)) id = SteamId();
		return id;
	}
	bool operator==(const SteamId& rhs) const { return value_ == rhs.value_; }
	bool operator!=(const SteamId& rhs) const { return value_ != rhs.value_; }

private:
	std::uint64_t value_;
};

// The ID a joining player sends: their own when they chose to show it, else
// none. `local` reads the signed-in account and is not called when off.
template<class Local>
SteamId SharedSteamId(bool show, Local local) { return show ? local() : SteamId(); }

// The profile page in a browser.
inline std::string SteamProfileUrl(const SteamId& id) { return "https://steamcommunity.com/profiles/" + id.ToString(); }

// How a profile was opened: in Steam's overlay, in the browser, or not at all.
enum class SteamProfileRoute { Refused, Overlay, Browser, Failed };
struct SteamProfileOpeners {
	std::function<bool()> overlayAvailable;
	std::function<bool(const SteamId&)> overlay;
	std::function<bool(const SteamId&)> browser;
};
// The overlay when Steam offers it and takes the call, otherwise the browser.
inline SteamProfileRoute OpenSteamProfile(const SteamId& id, const SteamProfileOpeners& openers) {
	if (!id.Present()) return SteamProfileRoute::Refused;
	if (openers.overlayAvailable && openers.overlay && openers.overlayAvailable() && openers.overlay(id)) return SteamProfileRoute::Overlay;
	return openers.browser && openers.browser(id) ? SteamProfileRoute::Browser : SteamProfileRoute::Failed;
}

}
