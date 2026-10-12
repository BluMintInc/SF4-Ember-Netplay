#include "../common/GameDisplayConfig.hxx"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#include "test_support.hxx"
using sf4e::gameconfig::ParseDisplaySettings;
using sf4e::gameconfig::RenderingThreadOverride;

// The game's live flag is kept off, and its config write sees the player's
// own value, then off again.
static void RenderingThreadIsOffButWrittenAsAsked() {
	for (const int asked : {1, 0}) {
		RenderingThreadOverride override_;
		int live = asked;
		override_.Enforce(live);
		CHECK(live == 0);
		CHECK(override_.Requested() == asked);
		int written = -1;
		override_.WriteRequested(live, [&] { written = live; });
		CHECK(written == asked);
		CHECK(live == 0);
	}
}

// A write before the flag was enforced is the game's own and untouched.
static void RenderingThreadWriteBeforeEnforceIsTheGames() {
	const RenderingThreadOverride override_;
	CHECK(!override_.Requested());
	int live = 1, written = -1;
	override_.WriteRequested(live, [&] { written = live; });
	CHECK(written == 1 && live == 1);
}

int main() {
	RenderingThreadIsOffButWrittenAsAsked();
	RenderingThreadWriteBeforeEnforceIsTheGames();
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
	// So is the render thread, so no card asks for it either.
	CHECK(ParseDisplaySettings("[SYSTEM]\r\nRenderingThread=ON\r\n").Recommended());
	return 0;
}
