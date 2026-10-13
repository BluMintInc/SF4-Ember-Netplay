#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include "UiPreferencesStore.hxx"
#include "../launcher/update/InstallOffer.hxx"

namespace sf4e { namespace platform {

// When the game looks for an update on its own and whether it tells the player,
// in one place. Every answer reads the saved preferences, so the Settings row
// and a run of the game agree.
//
// A check runs at most once per launch, when the preference is on and the last
// check that reached GitHub finished more than CheckCooldownSeconds ago. A check
// that failed leaves no time behind, so the next launch tries again whatever the
// cooldown. A found update is presented unless it is the version the player
// skipped; a newer release is a new question.
class UpdatePolicy {
public:
    static constexpr std::uint64_t CheckCooldownSeconds = 6 * 60 * 60;
    // Where the three saved fields live; the preferences file unless a test supplies its own.
    struct Store {
        std::function<UpdatePreferences()> load;
        std::function<bool(std::uint64_t time)> saveLastCheck;
        std::function<bool(const std::string& tag)> saveSkipped;
    };
    UpdatePolicy();
    explicit UpdatePolicy(Store store) : store_(std::move(store)) {}

    // Whether a launch check should run at `now` (unix seconds). The clock going
    // backwards reads as a cooldown that has passed, never as one that never ends.
    bool ShouldCheck(std::uint64_t now) const;
    // A launch check finished. Only one that reached GitHub counts as a check.
    void CheckFinished(bool reached, std::uint64_t now);
    // The offer to present for `result`, or nothing: no verifiable newer release,
    // a step back, or the release the player skipped.
    std::optional<launcher::InstallOffer> Present(launcher::UpdateChannel channel, const launcher::UpdateCheckResult& result) const;
    // The player chose to skip this release.
    bool Skip(const std::string& tag);

private:
    Store store_;
};

} }
