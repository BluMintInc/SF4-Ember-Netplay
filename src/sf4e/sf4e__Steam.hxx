#pragma once
#include <cstdint>
#include "../common/SteamId.hxx"

// The Steam client as the game's own steam_api.dll reaches it. The DLL has no
// flat C API, so every call goes through an interface's vtable. The interfaces
// are asked of ISteamClient012 by version string, so a client that does not
// serve a version we have the layout of serves nothing, rather than another
// layout. Game thread only. Every call answers "not there" (null, none or
// false) when Steam, an interface or the overlay is missing.
namespace sf4e { namespace steam {

// The versions and layouts used. The vtable positions are counted from the
// Steamworks SDK v1.26 to v1.28 headers, the SDK releases whose headers define
// exactly these three versions (docs: community-work/STEAM_FINDINGS.md).
constexpr const char* UserVersion = "SteamUser017";
constexpr const char* FriendsVersion = "SteamFriends014";
constexpr const char* UtilsVersion = "SteamUtils006";
constexpr const char* RemoteStorageVersion = "STEAMREMOTESTORAGE_INTERFACE_VERSION016";

// The client and the handles it serves interfaces for.
struct ClientHandles {
	void* client = nullptr;
	int user = 0;
	int pipe = 0;
};

// The game's client, or false while steam_api.dll is not loaded or has none.
bool FindClient(ClientHandles& out);

// The same calls against a given client, which a test supplies.
class Steam {
public:
	explicit Steam(const ClientHandles& handles) : handles_(handles) {}
	// ISteamRemoteStorage016, or null.
	void* RemoteStorage() const;
	// The signed-in account, or none when it is not a SteamId (offline,
	// no interface, or a value that names no individual).
	SteamId LocalSteamId() const;
	// ISteamUtils::IsOverlayEnabled; false without the interface.
	bool OverlayAvailable() const;
	// Opens the profile in the overlay; false when nothing was asked of it.
	bool OpenProfileOverlay(const SteamId& id) const;
private:
	ClientHandles handles_;
};

// The game's own Steam.
void* RemoteStorage();
SteamId LocalSteamId();
bool OverlayAvailable();
bool OpenProfileOverlay(const SteamId& id);

} }
