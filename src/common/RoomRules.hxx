#pragma once
#include "RoomLimits.hxx"
#include <cstdint>

namespace sf4e { namespace room {
// A table's set length: Unlimited, or first to N for N from 1 to
// MaxSetLength. Lengths without a name here are first to that many games.
enum class SetFormat : std::uint8_t { Ft1 = 1, Ft2 = 2, Ft3 = 3, Ft4 = 4, Ft5 = 5, Ft10 = 10, Unlimited = 0 };
// The longest set: first to 10 (best of 19). The bridge has the same limit
// (ember_protocol matches::MAX_GAMES_TO_WIN); keep them equal.
constexpr int MaxSetLength = 10;
inline bool ValidGamesToWin(long long games) { return games >= 1 && games <= MaxSetLength; }
inline bool ValidSetFormat(long long format) { return format == static_cast<int>(SetFormat::Unlimited) || ValidGamesToWin(format); }
enum class RotationMode : std::uint8_t { WinnerStays = 0, LoserStays, BothRotate };
// The round times a table offers, shortest first. The longest, about 2.7
// hours, is a Practice match's: its games end only when the time runs out.
constexpr std::uint16_t RoundTimes[] = {30, 60, 99, 300, 9999};
constexpr std::uint16_t DefaultRoundTime = 99;
constexpr std::uint16_t PracticeRoundTime = 9999;
inline bool ValidRoundTime(long long time) {
    for (const auto offered : RoundTimes) if (time == offered) return true;
    return false;
}
struct Rules {
    SetFormat format = SetFormat::Unlimited;
    RotationMode rotation = RotationMode::WinnerStays;
    bool editionSelect = true;
    std::uint8_t roundCount = 3;
    std::uint16_t roundTime = DefaultRoundTime;
    // Practice with another player: both fighters' health and gauges fill
    // again and nobody is knocked out. It changes the simulation, so it is
    // fixed with the other rules when the table prepares a game.
    bool training = false;
    // The round time the table had when Practice match was turned on, which
    // turning it off gives back. 0 while it is off, and in rules written
    // before it was kept.
    std::uint16_t timeBeforePractice = 0;
    // Turning Practice match on sets the longest round time; turning it off
    // gives back the time from before, or the default where none was kept.
    void SetPractice(bool on) {
        if (on == training) return;
        if (on) { timeBeforePractice = roundTime; roundTime = PracticeRoundTime; }
        else { roundTime = timeBeforePractice ? timeBeforePractice : DefaultRoundTime; timeBeforePractice = 0; }
        training = on;
    }
    // A kept time is an offered one, and only Practice keeps one.
    bool PracticeTimeValid() const {
        return timeBeforePractice == 0 || (training && ValidRoundTime(timeBeforePractice));
    }
    bool operator==(const Rules& rhs) const {
        return format == rhs.format && rotation == rhs.rotation && editionSelect == rhs.editionSelect &&
            roundCount == rhs.roundCount && roundTime == rhs.roundTime && training == rhs.training &&
            timeBeforePractice == rhs.timeBeforePractice;
    }
};
// A public room's tables start at first to 2, winner stays, so its queue
// moves without the host finding Table options first.
inline Rules PublicRoomRules() {
    Rules rules;
    rules.format = SetFormat::Ft2;
    rules.rotation = RotationMode::WinnerStays;
    return rules;
}
} }
