#pragma once

namespace sf4e {
// The largest input delay a fighter can choose, in frames. Preferences, the
// room authority, its checkpoints and the connection check all share it.
constexpr int MaximumInputDelay = 10;
// The smallest. 0 adds no delay and is only ever a player's own choice: the
// connection check never recommends it and Auto never readies with it.
constexpr int MinimumInputDelay = 0;
// Where a new profile's chosen delay starts, and what a saved delay that is
// not a whole number in range loads as (InputDelayPreference.hxx).
constexpr int DefaultInputDelay = 2;
// The connection check recommends from this to MaximumInputDelay
// (recovery.rs: recommended_delay); a 0 from it is no recommendation.
constexpr int RecommendedInputDelayMinimum = 1;

// Auto takes the connection check's recommendation, held between these.
constexpr int AutoInputDelayMinimum = 1;
constexpr int AutoInputDelayMaximum = 4;
static_assert(AutoInputDelayMinimum >= RecommendedInputDelayMinimum, "Auto never readies at 0 frames");
// The delay Auto readies with. Without a recommendation (-1) it is the
// default, where a chosen delay starts.
constexpr int AutoInputDelay(int recommended) {
    return recommended < 0 || recommended > MaximumInputDelay ? DefaultInputDelay :
        recommended < AutoInputDelayMinimum ? AutoInputDelayMinimum :
        recommended > AutoInputDelayMaximum ? AutoInputDelayMaximum : recommended;
}

// Where a delay command carries a number, this asks for Auto. It is never
// stored as a delay: preferences keep Auto in its own autoInputDelay flag.
constexpr int AutoInputDelayChoice = -2;
// One step along the delay row: Auto, then MinimumInputDelay to MaximumInputDelay.
constexpr int StepInputDelay(bool automatic, int delay, int delta) {
    return automatic ? (delta > 0 ? MinimumInputDelay : AutoInputDelayChoice) :
        delay + delta < MinimumInputDelay ? AutoInputDelayChoice :
        delay + delta > MaximumInputDelay ? MaximumInputDelay : delay + delta;
}
}
