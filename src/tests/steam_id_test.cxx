#include "../common/SteamId.hxx"
#include "test_support.hxx"

using sf4e::SteamId;
using sf4e::SteamProfileOpeners;
using sf4e::SteamProfileRoute;

static bool Accepts(const std::string& text) {
	SteamId id;
	return SteamId::Parse(text, id) && id.Present() && id.ToString() == text;
}
static bool Refuses(const std::string& text) {
	SteamId id;
	return !SteamId::Parse(text, id) && !id.Present();
}
static std::uint64_t Pack(std::uint64_t universe, std::uint64_t type, std::uint64_t instance, std::uint64_t account) {
	return (universe << 56) | (type << 52) | (instance << 32) | account;
}
static bool ValueAccepted(std::uint64_t value) {
	SteamId id;
	return SteamId::FromValue(value, id) && id.Value() == value;
}

static void TestValidation() {
	// Valve's steamclientpublic.h: universe 1, individual 1, desktop instance 1.
	CHECK(Accepts("76561200000000000"));
	CHECK(Accepts("76561197960265729"));   // account 1, the lowest
	CHECK(Accepts("76561202255233023"));   // account 0xFFFFFFFF, the highest
	CHECK(Refuses("76561190000000000"));   // below the base: instance 0
	CHECK(Refuses("76561197960265728"));   // the base itself: account 0
	CHECK(Refuses("76561202255233024"));   // one past the highest account: instance 2
	// Each field on its own.
	CHECK(ValueAccepted(Pack(1, 1, 1, 7)));
	CHECK(!ValueAccepted(Pack(0, 1, 1, 7)));    // invalid universe
	CHECK(!ValueAccepted(Pack(2, 1, 1, 7)));    // beta universe
	CHECK(!ValueAccepted(Pack(1, 0, 1, 7)));    // invalid type
	CHECK(!ValueAccepted(Pack(1, 2, 1, 7)));    // game server
	CHECK(!ValueAccepted(Pack(1, 3, 1, 7)));    // anonymous game server
	CHECK(!ValueAccepted(Pack(1, 7, 1, 7)));    // clan
	CHECK(!ValueAccepted(Pack(1, 8, 1, 7)));    // chat
	CHECK(!ValueAccepted(Pack(1, 1, 0, 7)));    // no instance
	CHECK(!ValueAccepted(Pack(1, 1, 2, 7)));    // console instance
	CHECK(!ValueAccepted(Pack(1, 1, 0x80000, 7)));  // chat flag
	CHECK(!ValueAccepted(Pack(1, 1, 1, 0)));    // zero account
	CHECK(!ValueAccepted(0));
	// Overflow: a number that does not fit 64 bits is refused, not wrapped.
	CHECK(Refuses("18446744073709551616"));
	CHECK(Refuses("18446744073709551615"));    // fits 64 bits, names no individual
	CHECK(Refuses("99999999999999999999"));
	CHECK(Refuses("100000000000000000000"));
	CHECK(Refuses("1000000000000000000000000"));
	// Only canonical decimal text.
	CHECK(Refuses(""));
	CHECK(Refuses("0"));
	CHECK(Refuses("076561200000000000"));
	CHECK(Refuses(" 76561200000000000"));
	CHECK(Refuses("76561200000000000 "));
	CHECK(Refuses("76561200000000000\n"));
	CHECK(Refuses("+76561200000000000"));
	CHECK(Refuses("-76561200000000000"));
	CHECK(Refuses("0x110000100000001"));
	CHECK(Refuses("7656120000000000x"));
	CHECK(Refuses("76561200000000000.0"));
	CHECK(Refuses(std::string("76561200000000000\0", 18)));
	// A refused parse leaves the target alone.
	SteamId kept;
	CHECK(SteamId::Parse("76561200000000000", kept));
	CHECK(!SteamId::Parse("76561190000000000", kept) && kept.ToString() == "76561200000000000");
	// None, and text that is not an ID.
	CHECK(!SteamId().Present() && SteamId().Value() == 0);
	CHECK(SteamId::Parsed("76561200000000000").Present() && !SteamId::Parsed("76561190000000000").Present());
	CHECK(SteamId::Parsed("76561200000000000") == kept && SteamId() != kept);
	CHECK(sf4e::SteamProfileUrl(kept) == "https://steamcommunity.com/profiles/76561200000000000");
}

struct Calls {
	int available = 0, overlay = 0, browser = 0;
	bool availableAnswer = true, overlayAnswer = true, browserAnswer = true;
	SteamId lastOverlay, lastBrowser;
	SteamProfileOpeners Openers() {
		SteamProfileOpeners openers;
		openers.overlayAvailable = [this] { ++available; return availableAnswer; };
		openers.overlay = [this](const SteamId& id) { ++overlay; lastOverlay = id; return overlayAnswer; };
		openers.browser = [this](const SteamId& id) { ++browser; lastBrowser = id; return browserAnswer; };
		return openers;
	}
};

static void TestRoute() {
	const SteamId id = SteamId::Parsed("76561200000000000");
	{
		Calls calls;
		CHECK(sf4e::OpenSteamProfile(id, calls.Openers()) == SteamProfileRoute::Overlay);
		CHECK(calls.overlay == 1 && calls.browser == 0 && calls.lastOverlay == id);
	}
	{
		// The overlay is disabled: the browser page is chosen, and the overlay is not asked.
		Calls calls;
		calls.availableAnswer = false;
		CHECK(sf4e::OpenSteamProfile(id, calls.Openers()) == SteamProfileRoute::Browser);
		CHECK(calls.overlay == 0 && calls.browser == 1 && calls.lastBrowser == id);
	}
	{
		// The overlay says yes and then does not take the call.
		Calls calls;
		calls.overlayAnswer = false;
		CHECK(sf4e::OpenSteamProfile(id, calls.Openers()) == SteamProfileRoute::Browser);
		CHECK(calls.overlay == 1 && calls.browser == 1);
	}
	{
		Calls calls;
		calls.availableAnswer = false;
		calls.browserAnswer = false;
		CHECK(sf4e::OpenSteamProfile(id, calls.Openers()) == SteamProfileRoute::Failed);
	}
	{
		// No Steam at all: no openers for the overlay.
		Calls calls;
		auto openers = calls.Openers();
		openers.overlayAvailable = nullptr;
		openers.overlay = nullptr;
		CHECK(sf4e::OpenSteamProfile(id, openers) == SteamProfileRoute::Browser && calls.browser == 1);
		openers.browser = nullptr;
		CHECK(sf4e::OpenSteamProfile(id, openers) == SteamProfileRoute::Failed);
	}
	{
		// No ID: nothing is opened anywhere.
		Calls calls;
		CHECK(sf4e::OpenSteamProfile(SteamId(), calls.Openers()) == SteamProfileRoute::Refused);
		CHECK(calls.available == 0 && calls.overlay == 0 && calls.browser == 0);
	}
}

static void TestSharing() {
	const SteamId own = SteamId::Parsed("76561200000000000");
	int reads = 0;
	const auto local = [&] { ++reads; return own; };
	// Off: nothing is shared, and the account is not even read.
	CHECK(!sf4e::SharedSteamId(false, local).Present() && reads == 0);
	CHECK(sf4e::SharedSteamId(true, local) == own && reads == 1);
	CHECK(!sf4e::SharedSteamId(true, [] { return SteamId(); }).Present());
}

int main() {
	TestValidation();
	TestRoute();
	TestSharing();
	return 0;
}
