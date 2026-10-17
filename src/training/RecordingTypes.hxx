#pragma once
#include <algorithm>
#include <vector>

// What a recording is made of, shared by the session, the recording bank and
// the recording files without the rest of Training.
namespace sf4e { namespace training {
constexpr int SlotCount = 8;
// One more slot, never selectable, holds the dummy's typed reply.
constexpr int ReplySlot = SlotCount;
constexpr int MaxFrames = 60 * 120;
constexpr unsigned FightButtons = 0xcff; // Directions and six attacks; excludes menu buttons.
constexpr unsigned AttackButtons = 0xcf0;
// wait: a loaded frame that repeats, buttons held, until the fighter can act
// again (1) or a hit lands (2), so typed moves take their timing from the
// fight; offset: frames after the cue the press lands on. A free frame is
// predicted from the fighter's script, so 0 is that frame itself and a
// negative offset is before it; a hit is pressed on the frame after it is seen.
// A reply's leading hold (3) repeats until the reply's first move is due
// (ReplyFirstMoveDue); elsewhere it is met at once.
struct Input { unsigned mapped = 0, raw = 0; unsigned char wait = 0; signed char offset = 0; };
constexpr int MinOffset = -120, MaxOffset = 120;
// A slot holds its frames as played by a fighter facing right, so a move
// recorded on one side comes out the same on the other: Left and Right are
// swapped for a fighter facing left, in both words, per played frame.
constexpr unsigned LeftBit = 4, RightBit = 8;
inline unsigned MirrorDirections(unsigned bits) {
    return (bits & ~(LeftBit | RightBit)) | (bits & LeftBit ? RightBit : 0) | (bits & RightBit ? LeftBit : 0);
}
inline Input Mirror(Input input) { input.mapped = MirrorDirections(input.mapped); input.raw = MirrorDirections(input.raw); return input; }
// Between the slot's facing and a fighter's: the same swap both ways.
inline Input ForFacing(const Input& input, bool facingRight) { return facingRight ? input : Mirror(input); }
// A reply starts on its first pressed frame; -1 when it presses nothing.
inline int ReplyStart(const std::vector<Input>& frames) {
    const auto first = std::find_if(frames.begin(), frames.end(), [](const Input& frame) { return (frame.raw & FightButtons) != 0; });
    return first == frames.end() ? -1 : static_cast<int>(first - frames.begin());
}
} }
