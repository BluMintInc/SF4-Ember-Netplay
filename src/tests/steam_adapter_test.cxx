#include "../sf4e/sf4e__Steam.hxx"
#include "test_support.hxx"

#include <cstring>
#include <string>

// Stand-ins for the Steam client built the way Valve's headers declare it, so
// the adapter's vtable positions and calling conventions are checked against
// real virtual functions. The method order is the Steamworks SDK v1.28 public
// headers (https://github.com/rlabrecque/SteamworksSDK, public/steam): the
// release whose isteamclient.h, isteamuser.h, isteamfriends.h and
// isteamutils.h define SteamClient012, SteamUser017, SteamFriends014 and
// SteamUtils006. Methods the adapter does not call only mark a wrong call.
namespace {

using sf4e::SteamId;
using namespace sf4e::steam;

// Like CSteamID: a class with constructors, so MSVC returns it through a
// hidden pointer and passes it by value as its eight bytes.
class FakeSteamId {
public:
	FakeSteamId(unsigned long long value = 0) : value_(value) {}
	unsigned long long Value() const { return value_; }
private:
	unsigned long long value_;
};

std::string g_wrongCall;
#define WRONG(name) virtual void name() { g_wrongCall = #name; }

struct FakeUser {
	WRONG(GetHSteamUser)
	WRONG(BLoggedOn)
	virtual FakeSteamId GetSteamID() { return FakeSteamId(id); }
	WRONG(InitiateGameConnection)
	unsigned long long id = 0;
};

struct FakeFriends {
	WRONG(GetPersonaName) WRONG(SetPersonaName) WRONG(GetPersonaState) WRONG(GetFriendCount) WRONG(GetFriendByIndex)
	WRONG(GetFriendRelationship) WRONG(GetFriendPersonaState) WRONG(GetFriendPersonaName) WRONG(GetFriendGamePlayed)
	WRONG(GetFriendPersonaNameHistory) WRONG(GetPlayerNickname) WRONG(HasFriend) WRONG(GetClanCount) WRONG(GetClanByIndex)
	WRONG(GetClanName) WRONG(GetClanTag) WRONG(GetClanActivityCounts) WRONG(DownloadClanActivityCounts)
	WRONG(GetFriendCountFromSource) WRONG(GetFriendFromSourceByIndex) WRONG(IsUserInSource) WRONG(SetInGameVoiceSpeaking)
	WRONG(ActivateGameOverlay)
	virtual void ActivateGameOverlayToUser(const char* dialog, FakeSteamId steamId) {
		++toUser;
		this->dialog = dialog;
		shown = steamId.Value();
	}
	WRONG(ActivateGameOverlayToWebPage)
	int toUser = 0;
	std::string dialog;
	unsigned long long shown = 0;
};

struct FakeUtils {
	WRONG(GetSecondsSinceAppActive) WRONG(GetSecondsSinceComputerActive) WRONG(GetConnectedUniverse) WRONG(GetServerRealTime)
	WRONG(GetIPCountry) WRONG(GetImageSize) WRONG(GetImageRGBA) WRONG(GetCSERIPPort) WRONG(GetCurrentBatteryPower)
	WRONG(GetAppID) WRONG(SetOverlayNotificationPosition) WRONG(IsAPICallCompleted) WRONG(GetAPICallFailureReason)
	WRONG(GetAPICallResult) WRONG(RunFrame) WRONG(GetIPCCallCount) WRONG(SetWarningMessageHook)
	virtual bool IsOverlayEnabled() { return overlay; }
	WRONG(BOverlayNeedsPresent)
	bool overlay = true;
};

struct FakeStorage { int marker = 0; };

// ISteamClient012. Serves an interface only for the version string asked.
struct FakeClient {
	WRONG(CreateSteamPipe) WRONG(BReleaseSteamPipe) WRONG(ConnectToGlobalUser) WRONG(CreateLocalUser) WRONG(ReleaseUser)
	virtual FakeUser* GetISteamUser(int user, int pipe, const char* version) {
		lastUser = user; lastPipe = pipe; userVersion = version;
		return !std::strcmp(version, "SteamUser017") ? userObject : nullptr;
	}
	WRONG(GetISteamGameServer) WRONG(SetLocalIPBinding)
	virtual FakeFriends* GetISteamFriends(int user, int pipe, const char* version) {
		lastUser = user; lastPipe = pipe; friendsVersion = version;
		return !std::strcmp(version, "SteamFriends014") ? friendsObject : nullptr;
	}
	virtual FakeUtils* GetISteamUtils(int pipe, const char* version) {
		lastPipe = pipe; utilsVersion = version;
		return !std::strcmp(version, "SteamUtils006") ? utilsObject : nullptr;
	}
	WRONG(GetISteamMatchmaking) WRONG(GetISteamMatchmakingServers) WRONG(GetISteamGenericInterface) WRONG(GetISteamUserStats)
	WRONG(GetISteamGameServerStats) WRONG(GetISteamApps) WRONG(GetISteamNetworking)
	virtual FakeStorage* GetISteamRemoteStorage(int user, int pipe, const char* version) {
		lastUser = user; lastPipe = pipe; storageVersion = version;
		return !std::strcmp(version, "STEAMREMOTESTORAGE_INTERFACE_VERSION016") ? storageObject : nullptr;
	}
	FakeUser* userObject = nullptr;
	FakeFriends* friendsObject = nullptr;
	FakeUtils* utilsObject = nullptr;
	FakeStorage* storageObject = nullptr;
	int lastUser = -1, lastPipe = -1;
	std::string userVersion, friendsVersion, utilsVersion, storageVersion;
};

struct World {
	FakeUser user;
	FakeFriends friends;
	FakeUtils utils;
	FakeStorage storage;
	FakeClient client;
	World() {
		user.id = 76561200000000000ull;
		client.userObject = &user; client.friendsObject = &friends; client.utilsObject = &utils; client.storageObject = &storage;
	}
	ClientHandles Handles() { ClientHandles handles; handles.client = &client; handles.user = 7; handles.pipe = 9; return handles; }
};

void TestServedInterfaces() {
	World world;
	const Steam steam(world.Handles());
	// The account, through the hidden return pointer.
	CHECK(steam.LocalSteamId().ToString() == "76561200000000000");
	CHECK(world.client.userVersion == UserVersion && world.client.userVersion == "SteamUser017");
	CHECK(world.client.lastUser == 7 && world.client.lastPipe == 9);
	// The overlay flag, both ways.
	CHECK(steam.OverlayAvailable());
	CHECK(world.client.utilsVersion == "SteamUtils006" && world.client.lastPipe == 9);
	world.utils.overlay = false;
	CHECK(!steam.OverlayAvailable());
	world.utils.overlay = true;
	// The profile, with the dialog name and the ID's eight bytes by value.
	const SteamId id = SteamId::Parsed("76561200000000001");
	CHECK(steam.OpenProfileOverlay(id));
	CHECK(world.client.friendsVersion == "SteamFriends014");
	CHECK(world.friends.toUser == 1 && world.friends.dialog == "steamid" && world.friends.shown == id.Value());
	// Remote storage keeps the version the replay store was written against.
	CHECK(steam.RemoteStorage() == &world.storage && world.client.storageVersion == "STEAMREMOTESTORAGE_INTERFACE_VERSION016");
	// Every call landed on the method it names.
	CHECK(g_wrongCall.empty());
}

void TestAccountThatIsNotAProfile() {
	World world;
	const Steam steam(world.Handles());
	for (const unsigned long long raw : { 0ull, 76561197960265728ull, 76561190000000000ull, 0x0170000100000001ull, 0xFFFFFFFFFFFFFFFFull }) {
		world.user.id = raw;
		CHECK(!steam.LocalSteamId().Present());
	}
	CHECK(g_wrongCall.empty());
}

void TestMissingInterfaces() {
	// A client that does not serve the versions: nothing, and no call on a wrong layout.
	World world;
	world.client.userObject = nullptr; world.client.friendsObject = nullptr; world.client.utilsObject = nullptr; world.client.storageObject = nullptr;
	const Steam steam(world.Handles());
	CHECK(!steam.LocalSteamId().Present());
	CHECK(!steam.OverlayAvailable());
	CHECK(!steam.OpenProfileOverlay(SteamId::Parsed("76561200000000000")));
	CHECK(steam.RemoteStorage() == nullptr);
	// No client at all.
	const Steam none{ClientHandles()};
	CHECK(!none.LocalSteamId().Present() && !none.OverlayAvailable() && none.RemoteStorage() == nullptr);
	CHECK(!none.OpenProfileOverlay(SteamId::Parsed("76561200000000000")));
	// The overlay is not asked to open nobody.
	World other;
	CHECK(!Steam(other.Handles()).OpenProfileOverlay(SteamId()) && other.friends.toUser == 0);
	CHECK(g_wrongCall.empty());
}

void TestFallbackChoice() {
	const SteamId id = SteamId::Parsed("76561200000000000");
	int browser = 0;
	const auto openers = [&](const Steam& steam) {
		sf4e::SteamProfileOpeners openers;
		openers.overlayAvailable = [&steam] { return steam.OverlayAvailable(); };
		openers.overlay = [&steam](const SteamId& who) { return steam.OpenProfileOverlay(who); };
		openers.browser = [&browser](const SteamId&) { ++browser; return true; };
		return openers;
	};
	World world;
	CHECK(sf4e::OpenSteamProfile(id, openers(Steam(world.Handles()))) == sf4e::SteamProfileRoute::Overlay && browser == 0 && world.friends.toUser == 1);
	// Overlay disabled: the browser page.
	world.utils.overlay = false;
	CHECK(sf4e::OpenSteamProfile(id, openers(Steam(world.Handles()))) == sf4e::SteamProfileRoute::Browser && browser == 1 && world.friends.toUser == 1);
	// Interfaces missing: the browser page too.
	world.client.utilsObject = nullptr; world.client.friendsObject = nullptr;
	world.utils.overlay = true;
	CHECK(sf4e::OpenSteamProfile(id, openers(Steam(world.Handles()))) == sf4e::SteamProfileRoute::Browser && browser == 2);
	// Utils there and the overlay up, but the friends interface missing: the overlay call fails, the browser opens.
	world.client.utilsObject = &world.utils;
	CHECK(sf4e::OpenSteamProfile(id, openers(Steam(world.Handles()))) == sf4e::SteamProfileRoute::Browser && browser == 3);
	CHECK(g_wrongCall.empty());
}

void TestGameWithoutSteam() {
	// This process has no steam_api.dll: the game's own Steam answers "not there".
	ClientHandles handles;
	CHECK(!FindClient(handles));
	CHECK(!LocalSteamId().Present() && !OverlayAvailable() && RemoteStorage() == nullptr);
	CHECK(!OpenProfileOverlay(SteamId::Parsed("76561200000000000")));
}

}

int main() {
	TestServedInterfaces();
	TestAccountThatIsNotAProfile();
	TestMissingInterfaces();
	TestFallbackChoice();
	TestGameWithoutSteam();
	return 0;
}
