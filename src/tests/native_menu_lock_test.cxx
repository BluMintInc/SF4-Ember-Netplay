#include "../common/NativeMenuLock.hxx"
#include "test_support.hxx"

int main() {
    using namespace sf4e::input;
    using namespace sf4e::netplay;
    const RoomState rooms[] = {RoomState::Idle, RoomState::Opening, RoomState::Joined, RoomState::Closing, RoomState::Lost};
    const MatchState matches[] = {MatchState::None, MatchState::Preparing, MatchState::Playing, MatchState::PostMatch, MatchState::Failed};
    // Locked by any room, joined or on its way in or out, and by a live game;
    // open only with neither. A game outranks the room it is played in.
    for (const auto room : rooms)
        for (const auto match : matches) {
            const auto lock = NativeMenuLockFor(room, match);
            if (Fighting(match)) CHECK(lock == NativeMenuLock::Match);
            else if (room != RoomState::Idle) CHECK(lock == NativeMenuLock::Room);
            else CHECK(lock == NativeMenuLock::None);
        }
    CHECK(NativeMenuLockFor(RoomState::Idle, MatchState::PostMatch) == NativeMenuLock::None);
    CHECK(NativeMenuLockFor(RoomState::Joined, MatchState::PostMatch) == NativeMenuLock::Room);
    // The game's items by id (MMI_*): every one is open when unlocked; locked,
    // only MULTIPLAYER BATTLE (2), which Ember turns into its Play page.
    for (int mode = 0; mode <= 13; ++mode) {
        CHECK(NativeSelectionAllowed(NativeMenuLock::None, mode));
        CHECK(NativeSelectionAllowed(NativeMenuLock::Room, mode) == (mode == 2));
        CHECK(NativeSelectionAllowed(NativeMenuLock::Match, mode) == (mode == 2));
    }
    CHECK(!NativeSelectionAllowed(NativeMenuLock::Room, 8));   // Benchmark
    CHECK(!NativeSelectionAllowed(NativeMenuLock::Room, 10));  // Quit
    CHECK(!NativeSelectionAllowed(NativeMenuLock::Room, 12));  // back to the title
    // Back on Ember's Home hides Ember only outside a room.
    for (const auto room : rooms) CHECK(HomeBackHidesShell(room) == (room == RoomState::Idle));
    return 0;
}
