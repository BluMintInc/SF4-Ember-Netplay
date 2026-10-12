#include "../common/GameDisplayConfig.hxx"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#include "test_support.hxx"
using sf4e::gameconfig::ParseDisplaySettings;
using sf4e::gameconfig::RenderingThreadOverride;

// Faster display on: the game's live flag is kept off whatever config.ini
// asked, and the game's config write sees the player's own value, then off again.
static void FasterDisplayKeepsTheFlagOffButWritesItAsAsked() {
	for (const int asked : {1, 0}) {
		RenderingThreadOverride override_;
		int live = asked;
		override_.Apply(true, live);
		CHECK(live == 0 && override_.Applied() && !override_.RenderThread());
		int written = -1;
		override_.WriteRequested(live, [&] { written = live; });
		CHECK(written == asked);
		CHECK(live == 0);
		CHECK(override_.Describe() == std::string("rendering thread forced off (Faster display on; game setting asked for ") + (asked ? "ON" : "OFF") + ")");
	}
}

// Faster display off: the flag and its write are the game's alone.
static void FasterDisplayOffLeavesTheFlagToTheGame() {
	for (const int asked : {1, 0}) {
		RenderingThreadOverride override_;
		int live = asked;
		override_.Apply(false, live);
		CHECK(live == asked && override_.Applied() && override_.RenderThread() == (asked != 0));
		int written = -1;
		override_.WriteRequested(live, [&] { written = live; });
		CHECK(written == asked && live == asked);
		CHECK(override_.Describe() == std::string("rendering thread left to the game (Faster display off; RenderingThread=") + (asked ? "ON" : "OFF") + ")");
	}
}

// A write before Apply is the game's own and untouched.
static void RenderingThreadWriteBeforeApplyIsTheGames() {
	const RenderingThreadOverride override_;
	CHECK(!override_.Applied());
	int live = 1, written = -1;
	override_.WriteRequested(live, [&] { written = live; });
	CHECK(written == 1 && live == 1);
}

int main() {
	FasterDisplayKeepsTheFlagOffButWritesItAsAsked();
	FasterDisplayOffLeavesTheFlagToTheGame();
	RenderingThreadWriteBeforeApplyIsTheGames();
	// The file as the game writes it, CRLF included.
	const auto good = ParseDisplaySettings("[DISPLAY]\r\nRefreshRate=240Hz\r\nVSync=OFF\r\nFrameRate=FIXED\r\n[GRAPHICS]\r\nMSAA=NONE\r\n");
	CHECK(good.Recommended() && good.frameRate == "FIXED");
	const auto bad = ParseDisplaySettings("FrameRate = smooth\nMSAA=4X");
	CHECK(!bad.FrameRateOk() && !bad.MsaaOk());
	CHECK(bad.frameRate == "SMOOTH" && bad.msaa == "4X");
	// A missing file or key is not a reason to show the card.
	CHECK(ParseDisplaySettings("").Recommended());
	CHECK(!ParseDisplaySettings("no equals\nMSAA=2x").MsaaOk());
	// VSync is forced off by the mod, so the file's value never matters.
	CHECK(ParseDisplaySettings("VSync=ON").Recommended());
	// The render thread is Faster display's, so no card asks for it either.
	CHECK(ParseDisplaySettings("[SYSTEM]\r\nRenderingThread=ON\r\n").Recommended());
	return 0;
}
