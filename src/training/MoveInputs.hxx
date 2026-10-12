#pragma once
#include "MoveNotation.hxx"
#include "TrainingSession.hxx"
#include <algorithm>
#include <vector>

// Turns typed moves into pad input the training session can play back.
// Directions take a few frames each and a button two; between moves the
// input waits on the fight itself: a linked move for the fighter to be able
// to act again (plus the link gap), a cancelled move for the hit to land.
// The game decides what comes out.
namespace sf4e { namespace combo {
constexpr unsigned Up = 1, Down = 2, Left = training::LeftBit, Right = training::RightBit;
constexpr int FollowDelay = 10;
// Frames a charge is held. The fighters' command files ask 42 (M. Bison), 50
// (Guile's and Dee Jay's Sonic Boom), 55 (most charge moves and every charge
// super and ultra) and 60 (Crystal Flash, Gen); a typed move does not know
// its fighter, so it holds enough for all of them, plus a little.
constexpr int ChargeFrames = 62;
inline unsigned DirectionBits(char digit, bool facingRight) {
    const unsigned f = facingRight ? Right : Left, b = facingRight ? Left : Right;
    switch (digit) {
    case '1': return Down | b; case '2': return Down; case '3': return Down | f;
    case '4': return b; case '6': return f;
    case '7': return Up | b; case '8': return Up; case '9': return Up | f;
    default: return 0;
    }
}
// "PP" is any two punches: the lightest ones are pressed.
inline unsigned ChosenButtons(const Step& step) {
    if (step.buttons != Punches && step.buttons != Kicks) return step.buttons;
    const unsigned order[2][3] = {{LP, MP, HP}, {LK, MK, HK}};
    unsigned mask = 0;
    for (int i = 0; i < step.need && i < 3; ++i) mask |= order[step.buttons == Kicks][i];
    return mask;
}
// Typed moves as the dummy plays them for its reply. frames: as a slot holds
// them, from the reply's first played frame. offset: the first move's own
// "@N", frames after the dummy is free its button lands, added to the
// reply's timing (DummyPlan::offset).
//
// How the reply is laid out in time, the reply anchored at the dummy's free
// frame (ReplyDue): holds and waits with a count before the first move lead
// into it; a hold with no count there is its leading hold (WaitReply), its
// length the stun's; the first move's button meets the free frame plus the
// timing and its "@N". Each later move waits on the one before (a link for
// the dummy to be free, a cancel for the hit, a follow-up a few frames), its
// "@N" counted from that cue, or lands on a frame of the reply ("#N", a later
// move's "@N" counted from that frame), counted from the reply's first played frame,
// which only frames of a known length before it can reach: "#N" after a wait
// on the fight is refused, as on the first move with nothing before it. A
// first move pressed and let go ("]HP[") comes out on the release, which the
// reply cannot time, so it is refused (ParseSteps).
struct Compiled { std::vector<training::Input> frames; int offset = 0; };
// Directions are read for the facing; the buttons of a step are chosen once.
// steps: as ParseSteps leaves them. False, with out empty and error set, when
// a step is not a move, a "#N" cannot be laid out, or the input would not
// fit in a slot (MaxFrames): the moves come out whole or not at all.
inline bool Synthesize(const std::vector<std::string>& steps, bool facingRight, Compiled& compiled, std::string& error) {
    compiled = {};
    auto& out = compiled.frames;
    bool full = false, waited = false;
    const auto add = [&](training::Input input) {
        // Nothing is played before the first input: the reply starts there.
        if (out.empty() && !input.raw && !input.wait) return;
        if (out.size() < training::MaxFrames) out.push_back(input);
        else full = true;
        waited = waited || input.wait;
    };
    const auto push = [&](unsigned bits, int frames) { for (; frames > 0; --frames) add({bits, bits, 0, 0}); };
    const auto wait = [&](unsigned char on, unsigned held, int timing) {
        add({held, held, on, static_cast<signed char>((std::max)(training::MinOffset, (std::min)(training::MaxOffset, timing)))});
    };
    // A direction held until the next move begins ("[1]", ParseSteps puts a
    // move after it): that move's frames before its own input carry it, its
    // wait included. Held before the first move, it is a reply's leading hold
    // (WaitReply), given way when the move is due, the move's offset its own.
    unsigned pending = 0;
    // Whether a move has played, so a move now waits on it. Holds and waits
    // with a count before the first move are input leading into it, not a
    // move to recover from: the first move plays straight after them.
    bool follows = false;
    const auto fail = [&](std::string why) { compiled = {}; error = std::move(why); return false; };
    for (const auto& text : steps) {
        Step step;
        if (!ParseStep(text, step, error)) return fail(error);
        if (step.hold) {
            const unsigned held = DirectionBits(step.motion[0], facingRight);
            if (step.frames) push(held, step.frames);
            else pending = held;
            continue;
        }
        const int timing = step.offset;
        // A move laid out on a frame ("#45") starts its input so the press
        // lands there, or at once when that frame has passed; it waits for no cue.
        const bool timed = step.at >= 0;
        if (timed && (out.empty() || waited || (pending && !follows)))
            return fail("\"" + text + "\" needs frames of a known length before it to land on frame " + std::to_string(step.at));
        // after: an earlier move played, so this one waits on it; idle: what
        // its frames before its own input hold.
        const bool after = follows, afterLead = pending && !after;
        const unsigned idle = after ? pending : 0;
        if (afterLead) wait(training::WaitReply, pending, 0);
        if (!after) compiled.offset = timing;
        pending = 0;
        follows = true;
        const auto gap = [&](int frames) { push(idle, frames); };
        const unsigned buttons = ChosenButtons(step);
        // The frame a "#N" move presses on: its "@N" counts from it, but the
        // first move's is the reply's own (the free frame its cue).
        const int landing = step.at + (after ? timing : 0);
        const auto reach = [&](int frame) { gap(frame - static_cast<int>(out.size())); };
        const bool red = step.buttons == (LP | MP | MK) && step.need == 3;
        const bool fadc = (step.buttons == (MP | MK) && step.need == 2) || red;
        if (fadc && (step.motion == "66" || step.motion == "44") && !step.charge && !step.air) {
            // A focus cancel: focus as the hit lands, then the dash at once.
            // The red focus is tapped and let go so the attack itself comes
            // out and lands, and the dash cancels that hit.
            const unsigned dash = DirectionBits(step.motion == "44" ? '4' : '6', facingRight);
            if (timed) reach(landing);
            else if (after) wait(training::WaitHit, idle, timing);
            push(step.buttons, 2); push(0, 2);
            if (red) wait(training::WaitHit, 0, 0);
            push(dash, 2); push(0, 2); push(dash, 2);
            continue;
        }
        // The directions, as a player does them: a linked move holds its
        // first direction through the recovery before it and presses on the
        // free frame; a cancelled move does its motion during the move before
        // it, all but the last direction, and finishes it with the button as
        // the hit lands, so the motion is fresh.
        std::string motion = step.motion;
        if (motion == "360" || motion == "720") { motion.clear(); for (int turn = step.motion == "720" ? 2 : 1; turn > 0; --turn) motion += "63214789"; }
        if (step.air) { push(DirectionBits('8', facingRight), 2); push(0, 10); }
        unsigned last = 0;
        const bool link = after && !step.cancel && !timed;
        // A follow-up goes into the move before it with no cue, far enough in
        // for the move to take it: El Fuerte's run takes its stop from about
        // its sixth frame, measured in play at this delay. "@" moves it.
        if (step.follow && after && !timed) gap((std::max)(0, FollowDelay + timing));
        if (timed) {
            int lead = 0;
            for (std::size_t i = 0; i < motion.size(); ++i) lead += (step.charge && i == 0 ? ChargeFrames : step.cancel ? 2 : 3) + (i && motion[i] == motion[i - 1] ? 1 : 0);
            if (after) gap(1);
            reach(landing - lead);
        } else if (link) wait(training::WaitActionable, idle ? idle : motion.empty() ? 0 : DirectionBits(motion[0], facingRight), timing);
        else if (after) gap(1);
        // The wait held the motion's first direction, or a hold in its place;
        // a lone direction goes in with the button either way, after a
        // leading hold too.
        const bool lone = motion.size() == 1 && buttons;
        const bool firstHeld = (link && !idle) || ((link || afterLead) && lone);
        const bool finish = step.cancel && !step.follow && after && !timed && buttons && !motion.empty();
        for (std::size_t i = 0; i + (finish ? 1 : 0) < motion.size(); ++i) {
            // The same direction again is a new tap: a neutral frame between (a "66" dash).
            if (i && motion[i] == motion[i - 1]) push(0, 1);
            last = DirectionBits(motion[i], facingRight);
            const bool held = firstHeld && i == 0;
            push(last, step.charge && i == 0 ? ChargeFrames : held ? 0 : step.cancel ? 2 : 3);
        }
        if (!buttons) continue;
        if (step.cancel && !step.follow && after && !timed) wait(training::WaitHit, last, timing);
        if (finish) last = DirectionBits(motion.back(), facingRight);
        if (step.edge == Edge::Hold) push(last | buttons, 30);
        else if (step.edge == Edge::Release) { push(last | buttons, 30); push(last, 2); }
        else if (step.mash) {
            // Five presses: several buttons are cycled one per frame (a piano),
            // one button goes on and off a frame at a time.
            std::vector<Step::MashPress> cycle = step.mashOrder;
            const int presses = cycle.empty() ? 5 : static_cast<int>(cycle.size());
            if (cycle.empty()) {
                std::vector<unsigned> bits;
                if (step.buttons == Punches) bits = {LP, MP, HP};
                else if (step.buttons == Kicks) bits = {LK, MK, HK};
                else for (unsigned bit = 1; bit; bit <<= 1) if (step.buttons & bit) bits.push_back(bit);
                for (unsigned bit : bits) cycle.push_back({'0', bit});
            }
            unsigned previous = 0;
            for (int press = 0; press < presses; ++press) {
                const auto& one = cycle[press % cycle.size()];
                const unsigned direction = one.direction != '0' ? DirectionBits(one.direction, facingRight) : last;
                // The same button twice in a row needs a frame off between, to be a new press.
                if (one.button == previous) push(direction, 1);
                push(direction | one.button, 1); previous = one.button;
            }
        }
        else push(last | buttons, 1);
        // The button comes up before the next move, so its press is its own edge.
        push(last, 1);
    }
    push(0, 1);
    if (full) return fail("the moves take more than " + std::to_string(training::MaxFrames) + " frames");
    return true;
}
} }
