#include "sf4e__Steam.hxx"

#include <windows.h>
#include <spdlog/spdlog.h>

namespace sf4e { namespace steam {
namespace {

// Vtable positions, counted in declaration order from the Steamworks SDK
// headers (https://github.com/rlabrecque/SteamworksSDK, public/steam, tags
// v1.26, v1.27 and v1.28, which all define SteamUser017, SteamFriends014 and
// SteamUtils006; the three headers do not differ before the methods used here
// between v1.26 and v1.28). None of the classes overloads a method, so MSVC
// keeps declaration order.
//
// ISteamClient012 (isteamclient.h): CreateSteamPipe 0, BReleaseSteamPipe 1,
// ConnectToGlobalUser 2, CreateLocalUser 3, ReleaseUser 4, GetISteamUser 5,
// GetISteamGameServer 6, SetLocalIPBinding 7, GetISteamFriends 8,
// GetISteamUtils 9, GetISteamMatchmaking 10, GetISteamMatchmakingServers 11,
// GetISteamGenericInterface 12, GetISteamUserStats 13,
// GetISteamGameServerStats 14, GetISteamApps 15, GetISteamNetworking 16,
// GetISteamRemoteStorage 17.
constexpr int ClientGetUser = 5, ClientGetFriends = 8, ClientGetUtils = 9, ClientGetRemoteStorage = 17;
// ISteamUser017 (isteamuser.h): GetHSteamUser 0, BLoggedOn 1, GetSteamID 2.
constexpr int UserGetSteamId = 2;
// ISteamFriends014 (isteamfriends.h): GetPersonaName 0, SetPersonaName 1,
// GetPersonaState 2, GetFriendCount 3, GetFriendByIndex 4,
// GetFriendRelationship 5, GetFriendPersonaState 6, GetFriendPersonaName 7,
// GetFriendGamePlayed 8, GetFriendPersonaNameHistory 9, GetPlayerNickname 10,
// HasFriend 11, GetClanCount 12, GetClanByIndex 13, GetClanName 14,
// GetClanTag 15, GetClanActivityCounts 16, DownloadClanActivityCounts 17,
// GetFriendCountFromSource 18, GetFriendFromSourceByIndex 19, IsUserInSource 20,
// SetInGameVoiceSpeaking 21, ActivateGameOverlay 22, ActivateGameOverlayToUser 23.
constexpr int FriendsActivateGameOverlayToUser = 23;
// ISteamUtils006 (isteamutils.h): GetSecondsSinceAppActive 0,
// GetSecondsSinceComputerActive 1, GetConnectedUniverse 2, GetServerRealTime 3,
// GetIPCountry 4, GetImageSize 5, GetImageRGBA 6, GetCSERIPPort 7,
// GetCurrentBatteryPower 8, GetAppID 9, SetOverlayNotificationPosition 10,
// IsAPICallCompleted 11, GetAPICallFailureReason 12, GetAPICallResult 13,
// RunFrame 14, GetIPCCallCount 15, SetWarningMessageHook 16, IsOverlayEnabled 17.
constexpr int UtilsIsOverlayEnabled = 17;

// CSteamID is a class with constructors, so MSVC x86 returns it through a
// hidden pointer the callee fills and hands back; as an argument it is its
// eight bytes on the stack, which a 64-bit integer matches.
struct User { std::uint64_t* GetSteamID(std::uint64_t* result); };
struct Friends { void ActivateGameOverlayToUser(const char* dialog, std::uint64_t id); };
struct Utils { bool IsOverlayEnabled(); };
struct RemoteStorageObject;
struct Client {
	User* GetISteamUser(int user, int pipe, const char* version);
	Friends* GetISteamFriends(int user, int pipe, const char* version);
	Utils* GetISteamUtils(int pipe, const char* version);
	RemoteStorageObject* GetISteamRemoteStorage(int user, int pipe, const char* version);
};

template<class Method, class Object>
Method Slot(Object* object, int index) {
	Method method;
	*reinterpret_cast<PVOID*>(&method) = (*reinterpret_cast<PVOID**>(object))[index];
	return method;
}

Client* AsClient(const ClientHandles& handles) { return static_cast<Client*>(handles.client); }

User* GetUser(const ClientHandles& handles) {
	Client* const client = AsClient(handles);
	return client ? (client->*Slot<User* (Client::*)(int, int, const char*)>(client, ClientGetUser))(handles.user, handles.pipe, UserVersion) : nullptr;
}
Friends* GetFriends(const ClientHandles& handles) {
	Client* const client = AsClient(handles);
	return client ? (client->*Slot<Friends* (Client::*)(int, int, const char*)>(client, ClientGetFriends))(handles.user, handles.pipe, FriendsVersion) : nullptr;
}
Utils* GetUtils(const ClientHandles& handles) {
	Client* const client = AsClient(handles);
	return client ? (client->*Slot<Utils* (Client::*)(int, const char*)>(client, ClientGetUtils))(handles.pipe, UtilsVersion) : nullptr;
}

}

bool FindClient(ClientHandles& out) {
	const HMODULE steam = GetModuleHandleW(L"steam_api.dll");
	if (!steam) return false;
	using ClientAccessor = void* (*)();
	using HandleAccessor = int (*)();
	const auto clientAccessor = reinterpret_cast<ClientAccessor>(GetProcAddress(steam, "SteamClient"));
	const auto user = reinterpret_cast<HandleAccessor>(GetProcAddress(steam, "SteamAPI_GetHSteamUser"));
	const auto pipe = reinterpret_cast<HandleAccessor>(GetProcAddress(steam, "SteamAPI_GetHSteamPipe"));
	void* const client = clientAccessor ? clientAccessor() : nullptr;
	if (!client || !user || !pipe) return false;
	out.client = client;
	out.user = user();
	out.pipe = pipe();
	return true;
}

void* Steam::RemoteStorage() const {
	Client* const client = AsClient(handles_);
	if (!client) return nullptr;
	return (client->*Slot<RemoteStorageObject* (Client::*)(int, int, const char*)>(client, ClientGetRemoteStorage))(handles_.user, handles_.pipe, RemoteStorageVersion);
}

SteamId Steam::LocalSteamId() const {
	User* const user = GetUser(handles_);
	if (!user) return SteamId();
	std::uint64_t raw = 0;
	(user->*Slot<std::uint64_t* (User::*)(std::uint64_t*)>(user, UserGetSteamId))(&raw);
	SteamId id;
	return SteamId::FromValue(raw, id) ? id : SteamId();
}

bool Steam::OverlayAvailable() const {
	Utils* const utils = GetUtils(handles_);
	return utils && (utils->*Slot<bool (Utils::*)()>(utils, UtilsIsOverlayEnabled))();
}

bool Steam::OpenProfileOverlay(const SteamId& id) const {
	if (!id.Present()) return false;
	Friends* const friends = GetFriends(handles_);
	if (!friends) return false;
	// "steamid" opens the overlay's browser on the user's profile page.
	(friends->*Slot<void (Friends::*)(const char*, std::uint64_t)>(friends, FriendsActivateGameOverlayToUser))("steamid", id.Value());
	return true;
}

namespace {
bool Own(ClientHandles& handles) {
	if (FindClient(handles)) return true;
	static bool told = false;
	if (!told) { told = true; spdlog::info("Steam: the client is not available"); }
	return false;
}
}

void* RemoteStorage() { ClientHandles handles; return Own(handles) ? Steam(handles).RemoteStorage() : nullptr; }
SteamId LocalSteamId() { ClientHandles handles; return Own(handles) ? Steam(handles).LocalSteamId() : SteamId(); }
bool OverlayAvailable() { ClientHandles handles; return Own(handles) && Steam(handles).OverlayAvailable(); }
bool OpenProfileOverlay(const SteamId& id) { ClientHandles handles; return Own(handles) && Steam(handles).OpenProfileOverlay(id); }

} }
