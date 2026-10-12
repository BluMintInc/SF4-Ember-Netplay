#pragma once
#include <cstdint>

namespace sf4e { namespace battle {
// What a room match adds to the game's battle request: its seed and whether
// it is played by Training rules. Both are part of the simulation, so every PC
// that fights or watches the match must give the game the same values.
//
// The match arms it when it is entered, from the room's frozen projection,
// before the game prepares the battle; that preparation takes it once, so
// nothing reaches the battle after it. A start that is deferred, refused or
// abandoned before the battle clears it.
struct NextRequest {
    bool training = false;
    bool overrideSeed = false;
    std::uint32_t seed = 0xffffffff;

    static NextRequest ForMatch(bool training, std::uint32_t seed) {
        NextRequest request;
        request.training = training;
        request.overrideSeed = true;
        request.seed = seed;
        return request;
    }
    // The request for the battle being prepared; this one is left empty.
    NextRequest Take() {
        const NextRequest taken = *this;
        *this = NextRequest();
        return taken;
    }
};
} }
