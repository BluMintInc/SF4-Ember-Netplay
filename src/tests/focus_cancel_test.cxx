// Focus cancels on the frame meter (CancelChain): a move, a focus that
// cancels it, then a forward or back dash out of the focus. The samples are
// made by hand in the forms the adapter reads from the game (ActionClass from
// a command move's category, docs/design/FOCUS_CANCEL_MARKER.md). They test
// the meter's contract, not the game: the recognizer and these sequences
// still need a capture in the game with SF4E_TRAINING_CAPTURE=1.
#include "../training/FrameMeter.hxx"
#include <cstdio>
#include <stdexcept>
#include <vector>
using namespace sf4e::training;
namespace {
void Require(bool pass, const char* why) { if (!pass) throw std::runtime_error(why); }
using S = ActorStatus;
// A stretch of one action: actionFrame counts from, from + 1, ... through it
// (from above 1 goes on with an action from an earlier stretch). first and
// last: its attack boundary, -1 for none. total and interruptible: the
// script's own length and interruptible frame, -1 for unknown.
struct Part { unsigned status; int action; ActionClass kind; int frames; int first, last; int from; int total = -1, interruptible = -1; };
Part Stand(int frames) { return {S::Stand, 0, ActionClass::Other, frames, -1, -1, 1}; }
// A move whose script runs total frames, long enough that it is still playing when it is cancelled.
constexpr int LongMove = 40;
Part Move(int frames, int first, int action = 400, int from = 1, int total = LongMove, int interruptible = -1) {
    return {S::Skill, action, ActionClass::Other, frames, first, first + 2, from, total, interruptible};
}
Part Focus(int frames, int first, int from = 1) { return {S::Skill, 322, ActionClass::Focus, frames, first, first + 2, from}; }
Part Struck(bool blocked, int frames, int from = 1) { return {blocked ? S::DamageGuard : S::Damage, 500, ActionClass::Other, frames, -1, -1, from}; }
// A dash out of a focus, as the game plays it (the command move DASH or
// BACK_DASH, a Skill) or as a dash status.
Part Dash(bool forward, bool status, int frames) {
    const auto kind = forward ? ActionClass::ForwardDash : ActionClass::BackDash;
    if (status) return {forward ? S::FrontDash : S::BackDash, forward ? 18 : 19, kind, frames, -1, -1, 1};
    return {S::Skill, forward ? 18 : 19, kind, frames, -1, -1, 1};
}
std::vector<FighterSample> Lay(std::initializer_list<Part> parts) {
    std::vector<FighterSample> out;
    for (const auto& part : parts)
        for (int i = 1; i <= part.frames; ++i) {
            FighterSample s;
            s.valid = true; s.posture = 0; s.timeScale = 1; s.basicActionInhibited = false;
            s.status = part.status; s.action = part.action; s.actionFrame = static_cast<float>(part.from + i - 1);
            s.firstActiveFrame = part.first; s.lastActiveFrame = part.last; s.actionClass = part.kind;
            s.totalFrames = part.total; s.interruptibleFrame = part.interruptible;
            if (part.first >= 0) s.boundaryProvenance = BoundaryProvenance::BacActionHeader;
            out.push_back(s);
        }
    return out;
}
// Plays two fighters' frames on a meter, the shorter one standing on; the
// frame counter goes on from where it was. plain: a meter fed the same
// frames with no class known, which the ordinary readings must match.
struct Bout {
    FrameMeter meter, plain;
    int at = 0;
    void Play(std::vector<FighterSample> one, std::vector<FighterSample> two) {
        const auto stand = Lay({Stand(1)})[0];
        while (one.size() < two.size()) one.push_back(stand);
        while (two.size() < one.size()) two.push_back(stand);
        for (std::size_t i = 0; i < one.size(); ++i, ++at) {
            std::array<FighterSample, 2> both{{one[i], two[i]}};
            meter.Observe(at, both);
            for (auto& s : both) s.actionClass = ActionClass::Unknown;
            plain.Observe(at, both);
        }
    }
    const MeterView& View() const { return meter.View(); }
    // The ordinary counters behave as if there were no focus cancel.
    void SameAsPlain(const char* why) const {
        const auto& a = meter.View(); const auto& b = plain.View();
        bool same = a.advantage.valid == b.advantage.valid && a.advantage.frames == b.advantage.frames &&
            a.lastAdvantage.frames == b.lastAdvantage.frames && a.advantage.blocked == b.advantage.blocked;
        for (int side = 0; side < 2; ++side)
            same = same && a.startupFrames[side] == b.startupFrames[side] && a.startupUnavailable[side] == b.startupUnavailable[side] &&
                a.moves[side].active == b.moves[side].active && a.moves[side].recovery == b.moves[side].recovery &&
                a.moves[side].seen == b.moves[side].seen && a.moves[side].live == b.moves[side].live;
        Require(same && !ChainOf(b.advantage, 0) && !ChainOf(b.lastAdvantage, 0), why);
    }
};
// One DP xx FADC exchange by Player 1: the DP's first active frame meets the
// guard or the hit, two active frames, a focus three frames long, then the
// dash. The other is in stun for stun frames from the contact. What Player 1
// is left with: positive when it was free first.
struct Fadc { int dpFirst = 4, focusFirst = 21, dash = 18, stun = 20, dpTotal = LongMove; bool forward = true, blocked = true, status = false; };
int Exchange(Bout& bout, const Fadc& f) {
    const int focus = 3;
    bout.Play(Lay({Stand(2), Move(f.dpFirst + 2, f.dpFirst, 400, 1, f.dpTotal), Focus(focus, f.focusFirst), Dash(f.forward, f.status, f.dash), Stand(8)}),
        Lay({Stand(2 + f.dpFirst - 1), Struck(f.blocked, f.stun), Stand(8)}));
    const int attacker = 2 + f.dpFirst + 2 + focus + f.dash, defender = 2 + f.dpFirst - 1 + f.stun;
    return defender - attacker;
}
// The dash of a reading's cancel by Player 1, Unknown for none.
ActionClass DashOf(const FrameAdvantage& reading) { return ChainOf(reading, 0) ? ChainOf(reading, 0)->dash : ActionClass::Unknown; }
void Confirmed(const Bout& bout, const Fadc& f, int expected, const char* why) {
    const auto& view = bout.View();
    const auto* chain = ChainOf(view.advantage, 0);
    Require(view.advantage.valid && view.advantage.frames[0] == expected && view.advantage.frames[1] == -expected, why);
    Require(view.advantage.attacker == 0 && view.advantage.blocked == f.blocked, why);
    Require(chain && chain->startup == f.dpFirst && chain->provenance == BoundaryProvenance::BacActionHeader && chain->active == 2, why);
    Require(chain->dash == (f.forward ? ActionClass::ForwardDash : ActionClass::BackDash) && chain->recoveryCancelled && chain->focusFrames == 3, why);
    // Latched with its frames, in the kept reading too.
    const auto* kept = ChainOf(view.lastAdvantage, 0);
    Require(kept && kept->startup == chain->startup && kept->dash == chain->dash && view.lastAdvantage.frames == view.advantage.frames, why);
    Require(!ChainOf(view.advantage, 1), why);
    bout.SameAsPlain(why);
}
}
int main() {
    try {
        // The adapter's classes, from a command move's category or a dash status.
        Require(ResolveActionClass(true, S::Skill, true, 0x80) == ActionClass::Focus, "A focus move was not a focus");
        Require(ResolveActionClass(true, S::Skill, true, 0x1080) == ActionClass::Focus, "A red focus was not a focus");
        Require(ResolveActionClass(true, S::Skill, true, 0x200080) == ActionClass::Focus, "A focus done as a cancel was not a focus");
        Require(ResolveActionClass(true, S::Skill, true, 0x80000) == ActionClass::ForwardDash, "The command dash was not a forward dash");
        Require(ResolveActionClass(true, S::Skill, true, 0x100000) == ActionClass::BackDash, "The command back dash was not a back dash");
        Require(ResolveActionClass(true, S::Skill, true, 0x200) == ActionClass::Other, "A special was not another action");
        Require(ResolveActionClass(true, S::Skill, false, 0) == ActionClass::Unknown, "A Skill with no command move was classed");
        Require(ResolveActionClass(true, S::FrontDash, false, 0) == ActionClass::ForwardDash &&
            ResolveActionClass(true, S::BackDash, false, 0) == ActionClass::BackDash, "Dash statuses were not dashes");
        Require(ResolveActionClass(true, S::Stand, false, 0x80) == ActionClass::Other, "A category was read off a status that is no Skill");
        Require(ResolveActionClass(false, S::Skill, true, 0x80) == ActionClass::Unknown, "A missing sample was classed");

        // DP xx FADC forward and back, on hit and on block, each recovery
        // order, the dash as a Skill (the game's) and as a dash status.
        for (int form = 0; form < 2; ++form)
            for (int direction = 0; direction < 2; ++direction)
                for (int guard = 0; guard < 2; ++guard)
                    for (int order = 0; order < 2; ++order) {
                        Fadc f; f.status = form == 1; f.forward = direction == 0; f.blocked = guard == 0;
                        // Defender first: a short stun; attacker first: a long one.
                        f.stun = order == 0 ? 12 : 40;
                        Bout bout;
                        const int expected = Exchange(bout, f);
                        Require(order == 0 ? expected < 0 : expected > 0, "The fixture's recovery order is wrong");
                        Confirmed(bout, f, expected, "DP xx FADC was not read as one cancel with its own advantage");
                    }

        // Different DP and focus boundaries: the focus's own startup, reached
        // or not before the dash, never replaces the DP's in the chain.
        for (int dp = 3; dp <= 4; ++dp)
            for (int focusFirst : {2, 21}) {
                Fadc f; f.dpFirst = dp; f.focusFirst = focusFirst;
                Bout bout;
                Confirmed(bout, f, Exchange(bout, f), "The chain took the focus's startup boundary");
                if (focusFirst == 21) Require(bout.View().startupFrames[0] == -1 &&
                    bout.View().startupUnavailable[0] == MeasurementUnavailable::WaitingForAttackBoundary,
                    "The ordinary startup no longer waits for the focus's boundary");
                else Require(bout.View().startupFrames[0] == 2, "The ordinary startup did not count the focus");
            }

        // The chain forms at the dash, not before: during the focus there is none.
        {
            Bout bout;
            bout.Play(Lay({Stand(2), Move(6, 4), Focus(3, 21)}), Lay({Stand(5), Struck(true, 6)}));
            Require(!ChainOf(bout.View().advantage, 0) && bout.View().advantage.pending, "A focus alone made a cancel");
            bout.Play(Lay({Dash(true, false, 1)}), Lay({Struck(true, 1, 7)}));
            Require(ChainOf(bout.View().advantage, 0) && bout.View().advantage.pending, "The dash did not confirm the cancel");
        }

        // The boundary: a focus on the move's last frame of script finds nothing
        // to cut; one frame short of it, the move was cut. The interruptible
        // frame, before or after the focus, changes neither.
        for (int interruptible : {-1, 3, 5, 30}) {
            Fadc f; f.stun = 12;
            auto exchange = [&](int total) {
                Bout bout;
                bout.Play(Lay({Stand(2), Move(f.dpFirst + 2, f.dpFirst, 400, 1, total, interruptible), Focus(3, f.focusFirst),
                    Dash(true, false, f.dash), Stand(8)}), Lay({Stand(2 + f.dpFirst - 1), Struck(true, f.stun), Stand(8)}));
                return bout;
            };
            const int last = f.dpFirst + 2;
            const auto finished = exchange(last), cut = exchange(last + 1);
            Require(!ChainOf(finished.View().advantage, 0) && !ChainOf(finished.View().lastAdvantage, 0), "A focus on the last frame was a cancel");
            Require(ChainOf(cut.View().advantage, 0) && ChainOf(cut.View().advantage, 0)->recoveryCancelled &&
                ChainOf(cut.View().lastAdvantage, 0), "A focus one frame short of the end was not a cancel");
            finished.SameAsPlain("A finished move changed the ordinary readings");
            cut.SameAsPlain("A cut move changed the ordinary readings");
        }

        // Consecutive exchanges: each reading keeps its own cancel, and a
        // plain move after them has none.
        {
            Bout bout;
            Fadc first; first.forward = true; first.blocked = true; first.stun = 12;
            const int one = Exchange(bout, first);
            Confirmed(bout, first, one, "The first exchange was not read");
            // The next DP clears the reading; the kept one is still the first, paired.
            bout.Play(Lay({Stand(2), Move(6, 3)}), Lay({Stand(4), Struck(false, 4)}));
            Require(!bout.View().advantage.valid && !ChainOf(bout.View().advantage, 0), "The next exchange kept the old cancel");
            Require(DashOf(bout.View().lastAdvantage) == ActionClass::ForwardDash &&
                bout.View().lastAdvantage.frames[0] == one && bout.View().lastAdvantage.blocked, "The kept reading lost its cancel");
            // Its focus and back dash: the cancel is the exchange's, the kept reading still the first.
            bout.Play(Lay({Focus(3, 21), Dash(false, false, 18)}), Lay({Struck(false, 21, 5)}));
            Require(DashOf(bout.View().advantage) == ActionClass::BackDash && ChainOf(bout.View().advantage, 0)->startup == 3,
                "The second cancel was not read");
            Require(DashOf(ShownAdvantage(bout.View())) == ActionClass::ForwardDash &&
                ShownAdvantage(bout.View()).frames[0] == one, "A new cancel was shown with an old result");
            bout.Play(Lay({Stand(30)}), {});
            Require(bout.View().advantage.valid && !bout.View().advantage.blocked &&
                DashOf(bout.View().lastAdvantage) == ActionClass::BackDash, "The second reading did not latch with its cancel");
            // A plain DP: no cancel anywhere once it is measured.
            bout.Play(Lay({Stand(2), Move(30, 4), Stand(4)}), Lay({Stand(5), Struck(true, 12), Stand(30)}));
            Require(bout.View().advantage.valid && !ChainOf(bout.View().advantage, 0) && !ChainOf(bout.View().lastAdvantage, 0),
                "A plain move was read as a cancel");
            bout.SameAsPlain("Consecutive cancels changed the ordinary readings");
        }

        // Negatives: no cancel without a move still playing, a focus, then a dash.
        const auto none = [](std::vector<FighterSample> one, std::vector<FighterSample> two, const char* why) {
            Bout bout; bout.Play(one, two);
            Require(!ChainOf(bout.View().advantage, 0) && !ChainOf(bout.View().lastAdvantage, 0), why);
            bout.SameAsPlain(why);
        };
        none(Lay({Stand(2), Focus(3, 21), Dash(true, false, 18), Stand(4)}), {}, "A focus dash on its own was a cancel");
        none(Lay({Stand(2), Move(6, 4), Stand(3), Focus(3, 21), Dash(true, false, 18), Stand(4)}), Lay({Stand(5), Struck(true, 12)}),
            "A finished move and a later focus were a cancel");
        none(Lay({Stand(2), Move(6, 4), Move(20, 5, 600), Stand(4)}), Lay({Stand(5), Struck(false, 30)}),
            "A special cancelled into a super was a focus cancel");
        none(Lay({Stand(2), Move(6, 4), Dash(true, false, 18), Stand(4)}), Lay({Stand(5), Struck(true, 12)}),
            "A dash with no focus was a focus cancel");
        none(Lay({Stand(2), Move(6, 4), Focus(3, 21), Stand(2), Dash(true, true, 18), Stand(4)}), Lay({Stand(5), Struck(true, 12)}),
            "A focus let go before the dash was a cancel");
        // The move's own length is the evidence that a focus cut its recovery
        // short. A move that had played all its frames and went straight into a
        // buffered focus and a dash, with no neutral sample between, is no cancel.
        none(Lay({Stand(2), Move(6, 4, 400, 1, 6), Focus(3, 21), Dash(true, false, 18), Stand(4)}), Lay({Stand(5), Struck(true, 12)}),
            "A move that finished, then a focus and a dash, was a cancel");
        none(Lay({Stand(2), Move(7, 4, 400, 1, 6), Focus(3, 21), Dash(true, false, 18), Stand(4)}), Lay({Stand(5), Struck(true, 12)}),
            "A move sampled past its length, then a focus and a dash, was a cancel");
        none(Lay({Stand(2), Move(6, 4, 400, 1, 6, 3), Focus(3, 21), Dash(false, true, 18), Stand(4)}), Lay({Stand(5), Struck(false, 12)}),
            "A finished move with an early interruptible frame was a cancel");
        // No length known, or none usable: no evidence, no cancel.
        for (int total : {-1, 0})
            none(Lay({Stand(2), Move(6, 4, 400, 1, total), Focus(3, 21), Dash(true, false, 18), Stand(4)}), Lay({Stand(5), Struck(true, 12)}),
                "A move of unknown length was a cancel");
        none(Lay({Stand(2), Move(6, 4, 400, 1, -1, 30), Focus(3, 21), Dash(true, false, 18), Stand(4)}), Lay({Stand(5), Struck(true, 12)}),
            "An interruptible frame alone was evidence of a cancel");
        {
            auto unknown = Lay({Stand(2), Move(6, 4), Focus(3, 21), Dash(true, false, 18), Stand(4)});
            for (auto& s : unknown) if (s.action == 400) s.actionClass = ActionClass::Unknown;
            none(unknown, Lay({Stand(5), Struck(true, 12)}), "A move no command started was cancelled");
        }

        // What clears it: another attack after the dash, an invalid sample, a
        // trade, a gap in the frames and a reset. Each starts on a cancel
        // whose exchange is still being measured.
        const auto pending = [](Bout& bout) {
            bout.Play(Lay({Stand(2), Move(6, 4), Focus(3, 21), Dash(true, false, 6)}), Lay({Stand(5), Struck(true, 12)}));
            Require(ChainOf(bout.View().advantage, 0) && bout.View().advantage.pending, "The cancel was not read");
        };
        {
            Bout bout; pending(bout);
            bout.Play(Lay({Move(10, 3, 401)}), Lay({Struck(true, 10, 13)}));
            Require(!ChainOf(bout.View().advantage, 0) && bout.View().advantage.pending, "An attack after the dash kept the cancel");
        }
        {
            Bout bout; pending(bout);
            auto gone = Lay({Dash(true, false, 1)}); gone[0].valid = false;
            bout.Play(gone, Lay({Struck(true, 1, 13)}));
            Require(!ChainOf(bout.View().advantage, 0) && bout.View().advantage.unavailable == MeasurementUnavailable::InvalidSample,
                "An invalid sample kept the cancel");
        }
        {
            Bout bout; pending(bout);
            // Both hit on one frame: a trade interrupts the exchange.
            bout.Play(Lay({Struck(false, 1)}), Lay({Struck(false, 1, 13)}));
            Require(!ChainOf(bout.View().advantage, 0) && bout.View().advantage.unavailable == MeasurementUnavailable::Interrupted,
                "An interrupted exchange kept the cancel");
        }
        {
            Bout bout;
            Fadc f; const int frames = Exchange(bout, f);
            Require(ChainOf(bout.View().lastAdvantage, 0) && bout.View().lastAdvantage.frames[0] == frames, "The kept reading was not there");
            bout.at += 5;
            bout.Play(Lay({Stand(1)}), {});
            Require(!ChainOf(bout.View().advantage, 0) && !ChainOf(bout.View().lastAdvantage, 0), "A gap in the frames kept the cancel");
            Exchange(bout, f);
            bout.meter.Reset();
            Require(!ChainOf(bout.View().advantage, 0) && !ChainOf(bout.View().lastAdvantage, 0), "A reset kept the cancel");
        }
        // The other's attack while the focus plays starts the next exchange:
        // the dash after it cancels nothing of that one.
        {
            Bout bout;
            bout.Play(Lay({Stand(2), Move(6, 4), Focus(2, 21)}), Lay({Stand(5), Struck(true, 3), Stand(2)}));
            bout.Play(Lay({Focus(1, 21, 3)}), Lay({Move(1, 5, 403)}));
            bout.Play(Lay({Dash(true, false, 6)}), Lay({Move(6, 5, 403, 2)}));
            Require(!ChainOf(bout.View().advantage, 0) && !ChainOf(bout.View().advantage, 1), "A cancel formed across two exchanges");
        }
        // A cancel still forming is dropped by a reset too.
        {
            Bout bout;
            bout.Play(Lay({Stand(2), Move(6, 4), Focus(2, 21)}), Lay({Stand(5), Struck(true, 5)}));
            bout.meter.Reset(); bout.plain.Reset();
            bout.Play(Lay({Dash(true, false, 6)}), Lay({Struck(true, 6)}));
            Require(!ChainOf(bout.View().advantage, 0), "A reset kept a cancel that was forming");
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
    std::puts("focus cancel tests passed");
    return 0;
}
