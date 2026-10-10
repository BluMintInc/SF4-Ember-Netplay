// The match frame meter is allowed in private rooms and outside rooms, and
// never in a public room.
#include "../common/MatchFrameMeter.hxx"

#include "test_support.hxx"

int main() {
    using sf4e::match_frame_meter::Allowed;
    CHECK(Allowed(false));
    CHECK(!Allowed(true));
    return 0;
}
