// A call back from Training or a ready challenge is announced outside the
// game only while its window is away, once for each, and never twice for
// the same sitting down. The chime is a well-formed WAV at the chosen level.
#include "../common/AwayAlert.hxx"
#include "../common/TrainingPad.hxx"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "test_support.hxx"

namespace alert = sf4e::away_alert;

static void CheckOnlyAway() {
    alert::Alert ring;
    // In front: nothing, and the call stays unannounced if the player leaves later.
    CHECK(!ring.Ring(alert::Key(alert::Source::TrainingCall, 1), false, 1000));
    CHECK(!ring.Ring(alert::Key(alert::Source::TrainingCall, 1), true, 1100));
    // Away: once per call.
    CHECK(ring.Ring(alert::Key(alert::Source::TrainingCall, 2), true, 10000));
    CHECK(!ring.Ring(alert::Key(alert::Source::TrainingCall, 2), true, 20000));
    // The same opponent's ready right after the call is the same alert.
    CHECK(!ring.Ring(alert::Key(alert::Source::ReadyChallenge, 1), true, 10000 + alert::Alert::QuietMs - 1));
    // A later challenge rings again; one in front does not.
    CHECK(ring.Ring(alert::Key(alert::Source::ReadyChallenge, 2), true, 30000));
    CHECK(!ring.Ring(alert::Key(alert::Source::ReadyChallenge, 3), false, 40000));
    CHECK(ring.Ring(alert::Key(alert::Source::TrainingCall, 3), true, 50000));
    // No key is no event.
    CHECK(!ring.Ring(0, true, 90000));
    // The two sources never share a key.
    CHECK(alert::Key(alert::Source::TrainingCall, 7) != alert::Key(alert::Source::ReadyChallenge, 7));
}

static std::uint32_t U32(const std::vector<std::uint8_t>& wave, std::size_t at) {
    std::uint32_t value; std::memcpy(&value, wave.data() + at, 4); return value;
}

static int Peak(const std::vector<std::uint8_t>& wave) {
    int peak = 0;
    for (std::size_t at = 44; at + 1 < wave.size(); at += 2) {
        std::int16_t sample; std::memcpy(&sample, wave.data() + at, 2);
        peak = (std::max)(peak, std::abs(static_cast<int>(sample)));
    }
    return peak;
}

static void CheckChime() {
    const auto full = alert::Chime(100), quiet = alert::Chime(10);
    CHECK(full.size() > 44 && std::memcmp(full.data(), "RIFF", 4) == 0 && std::memcmp(full.data() + 8, "WAVEfmt ", 8) == 0);
    CHECK(U32(full, 4) == full.size() - 8 && U32(full, 40) == full.size() - 44 && U32(full, 24) == 22050);
    CHECK(std::memcmp(full.data() + 36, "data", 4) == 0);
    const int loud = Peak(full), soft = Peak(quiet);
    CHECK(loud > 15000 && loud < 32767 && soft > 0 && soft * 8 < loud);
    // Out of range levels are held to it.
    CHECK(Peak(alert::Chime(500)) == loud && Peak(alert::Chime(0)) == soft);
}

// Go now from the pad behind another window: a fresh View only, never one
// held from before the call or from before the window went behind.
static void CheckGoNowBehind() {
    using sf4e::input::PadGoNowBehind; using sf4e::input::PhysicalBack; using sf4e::input::PhysicalStart;
    PadGoNowBehind pad;
    CHECK(!pad.Update(true, PhysicalBack));               // held as the call came
    CHECK(!pad.Update(true, PhysicalBack));
    CHECK(!pad.Update(true, 0));
    CHECK(pad.Update(true, PhysicalBack));                // a fresh press
    CHECK(!pad.Update(true, PhysicalBack));               // held: once
    CHECK(!pad.Update(true, 0) && !pad.Update(true, PhysicalStart));
    CHECK(!pad.Update(false, 0) && !pad.Update(true, PhysicalBack)); // the call went and came back held
    CHECK(!pad.Update(true, 0) && pad.Update(true, PhysicalBack | PhysicalStart) == false); // with Start: the game's pause
    CHECK(!pad.Update(true, 0) && pad.Update(true, PhysicalBack));
}

int main() {
    CheckOnlyAway();
    CheckChime();
    CheckGoNowBehind();
    return 0;
}
