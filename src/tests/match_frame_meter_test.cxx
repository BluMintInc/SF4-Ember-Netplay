// The match frame meter is watched by the player's choice in private rooms and
// outside rooms, never in a public room, and a replay's own meter is
// independent of both.
#include "../common/MatchFrameMeter.hxx"

#include "test_support.hxx"

int main() {
    using namespace sf4e::match_frame_meter;
    CHECK(Allowed(false));
    CHECK(!Allowed(true));
    for (int i = 0; i < 2; ++i) {
        const bool replay = i == 1;
        CHECK(Watched(true, false, replay));
        CHECK(Watched(false, false, replay) == replay);
        CHECK(Watched(true, true, replay) == replay);
        CHECK(Watched(false, true, replay) == replay);
    }
    return 0;
}
