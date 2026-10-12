// When the Training dummy plays from Player 2's pad (TrainingSession.hxx,
// DummyControl): the updates the pad covers, a pause-menu action under
// Ember, the input plan, the session and the control together through a
// held recording, resets and a restarted playback, and typed replies against
// the game's input path.
#include "../training/TrainingSession.hxx"
#include "../training/MoveInputs.hxx"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <utility>
#include <vector>
using namespace sf4e::training;
void Require(bool pass, const char* why) { if (!pass) throw std::runtime_error(why); }
int main() {
    try {
        {
            // The dummy plays from the pad for the updates that read Ember's
            // input and act on it: from the first one Ember supplies to the
            // simulated one after the last (DummyInputLag), and its own control
            // stands around them.
            using Step = DummyControl::Step;
            // One update: whether Ember supplies Player 2's input in it, then
            // whether the battle simulated it.
            const auto step = [](DummyControl& control, bool supplies, int action = 0, bool simulated = true) {
                const Step seen = control.Follow(supplies, action);
                if (simulated) control.Simulated();
                return seen;
            };
            const auto steps = [&](std::vector<bool> supplies) {
                DummyControl control; std::vector<Step> seen;
                for (const bool supply : supplies) seen.push_back(step(control, supply));
                return seen;
            };
            Require(steps({false, true, true, false, false}) == std::vector<Step>{Step::None, Step::Begin, Step::Keep, Step::Keep, Step::End},
                "The pad did not run from the update reading the first input to the one acting on the last");
            Require(steps({true, false, true, false, false}) == std::vector<Step>{Step::Begin, Step::Keep, Step::Keep, Step::Keep, Step::End},
                "The update acting on the first drive's last input left the pad");
            Require(steps({true, false, false, true}) == std::vector<Step>{Step::Begin, Step::Keep, Step::End, Step::Begin},
                "A gap that reads and acts on nothing of Ember's did not give the dummy its own control");
            {
                // Updates the battle does not simulate move nothing on: the
                // pad waits with them for the frame that acts on the last input.
                DummyControl held;
                Require(step(held, true) == Step::Begin && step(held, false, 0, false) == Step::Keep && step(held, false, 0, false) == Step::Keep &&
                    step(held, false) == Step::Keep && step(held, false) == Step::End,
                    "An update the battle did not simulate moved the input lag on");
            }
            // Ember's own changes to the action keep the pad; one from the
            // pause menu ends it and stands, and nothing begins again from the
            // update before it.
            DummyControl control; int native = 0;
            Require(!control.Reconcile(3), "An action change with Ember idle was taken for one under it");
            Require(step(control, true, native) == Step::Begin, "The pad did not begin");
            control.Set(native, 1);
            Require(!control.Reconcile(native) && step(control, true, native) == Step::Keep, "Ember's own change to the action ended the pad");
            native = 3;
            Require(control.Reconcile(native) && native == 3 && step(control, false, native) == Step::None,
                "A pause-menu action did not end the pad");
            // A pause-menu action in a drive's first update ends it, and the
            // input that update read is not acted on from the pad.
            DummyControl lagged;
            Require(step(lagged, true) == Step::Begin && lagged.Reconcile(3) && step(lagged, false, 3) == Step::None,
                "A pause-menu action in a drive's first update did not end the drive, or the pad went on after it");
            // A reset breaks the input's history, not whose action it is.
            DummyControl rewound;
            step(rewound, true);
            Require(rewound.Rewind() && rewound.Reconcile(3), "A reset lost the action Ember drove under");
            // The battle closes with the pad on, and with it off.
            DummyControl closing;
            step(closing, true);
            Require(closing.Release() && !closing.Release() && step(closing, false) == Step::None, "Closing the battle misreported the pad");
        }
        {
            // The input plan and the dummy's control together, update by
            // update as the runtime runs them: a recording held under the open
            // controls and resumed, a reset after a reply's first update and
            // later in it, a playback a reset starts again, and a pause-menu
            // action met in the same update as a reset.
            struct Bench {
                Session session; DummyControl control;
                // The Training menu's Action, as the pause menu leaves it.
                int action = 0;
                Bench() { session.Enter(); session.SetReady(true); session.SetCheckpoint(true); }
                bool Send(Action what) {
                    Command command; command.action = what; command.generation = session.GetView().generation; return session.Apply(command);
                }
                // An update's start, before its commands (ReconcileDummy), then
                // a reset among them if asked (RestoreCheckpoint). Whether the reset took.
                bool Start(bool reset) {
                    if (control.Reconcile(action)) Send(Action::Stop);
                    if (!reset) return true;
                    const bool restored = Send(Action::Restore);
                    control.Rewind();
                    return restored;
                }
                // The rest of a simulated update; menu: the controls hold the
                // pad. Whether Player 2 plays from the pad in it.
                bool Finish(bool menu = false) {
                    const InputPlan plan = PlanInput(session.GetView(), menu);
                    const auto step = control.Follow(plan.suppliesDummy, action);
                    if (plan.read) session.Commit(session.Prepare(Frame{}));
                    control.Simulated();
                    return step == DummyControl::Step::Begin || step == DummyControl::Step::Keep;
                }
                bool Update(bool menu = false) { Start(false); return Finish(menu); }
                std::vector<bool> Run(std::vector<bool> menus) {
                    std::vector<bool> pads;
                    for (const bool menu : menus) pads.push_back(Update(menu));
                    return pads;
                }
                // An update with a reset in it, up to its input.
                bool Reset() { return Start(true); }
            };
            const std::vector<Input> moves(8, Input{0x10, 0x10, 0, 0});
            {
                Bench recording;
                Require(recording.Send(Action::Record), "The recording did not start");
                const auto pads = recording.Run({false, false, true, true, false, false});
                Require(pads == std::vector<bool>{true, true, true, false, true, true},
                    "A recording held under the open controls kept the dummy on the pad, or did not take it back on resuming");
                Require(recording.session.GetView().mode == Mode::Recording && recording.session.GetView().cursor == 4,
                    "The held recording stopped or recorded the held updates");

                Bench reply;
                Require(reply.session.Reply(moves) && reply.Update(), "The reply's first input was not read from the pad");
                Require(reply.Reset() && !reply.Finish() && !reply.Update(), "A reset after the reply's first update left the dummy on the pad");
                Require(reply.session.Reply(moves) && reply.Run({false, false, false}) == std::vector<bool>{true, true, true}, "The reply did not play from the pad");
                Require(reply.Reset() && !reply.Finish(), "A reset during the reply left the dummy on the pad for the old timeline");

                Bench playback;
                Command load; load.action = Action::Load; load.generation = playback.session.GetView().generation; load.frames = moves; load.side = 1;
                Require(playback.session.Apply(load) && playback.Send(Action::Play), "The playback did not start");
                Require(playback.Run({false, false, false}) == std::vector<bool>{true, true, true}, "The playback did not play from the pad");
                Require(playback.Reset() && playback.session.GetView().mode == Mode::Playback && playback.session.GetView().cursor == 0,
                    "The reset did not start the playback again");
                Require(playback.Finish() && playback.Update(), "The restarted playback's input was not read from the pad");

                // The pause menu changed the action, and the first update to
                // see it resets the battle: the choice stands and Ember stops.
                Bench recordingTaken;
                Require(recordingTaken.Send(Action::Record) && recordingTaken.Run({false, false}) == std::vector<bool>{true, true},
                    "The recording did not play from the pad");
                recordingTaken.action = 3;
                Require(recordingTaken.Reset() && recordingTaken.session.GetView().mode == Mode::Idle && !recordingTaken.Finish() && !recordingTaken.Update(),
                    "A reset with a pause-menu action kept the recording playing the dummy");
                Bench playbackTaken;
                load.generation = playbackTaken.session.GetView().generation;
                Require(playbackTaken.session.Apply(load) && playbackTaken.Send(Action::Play) &&
                    playbackTaken.Run({false, false}) == std::vector<bool>{true, true}, "The playback did not play from the pad");
                playbackTaken.action = 3;
                Require(playbackTaken.Reset() && playbackTaken.session.GetView().mode == Mode::Idle && !playbackTaken.Finish() && !playbackTaken.Update(),
                    "A reset with a pause-menu action started the playback again under it");
            }
        }
        {
            // Typed replies, update by update as the runtime decides and plays
            // them (TrainingRuntime.cxx, BeforeUpdate and AfterUpdate), against
            // the game's side as IDA and the game's own traces show it. The game
            // reads Player 2's pad only under the pad control (0x599410), and
            // the dummy acts on it the next update, whether Lower input lag is
            // on or not (DummyInputLag), again only under the pad
            // control (0x5996F0). A press starts its move from the dummy's
            // first free frame on, a button only as a new press of all its bits
            // on that frame (0x598F60). A charge is the frames in a row its
            // direction reached the fighter (0x598590): the dummy's own guard
            // reads nothing, so it charges nothing. A new stun drops a reply
            // begun; guard stance after the stun, the dummy holding back while
            // an attack is near, does not.
            struct Game {
                std::vector<unsigned> read;
                unsigned last = 0;
                int run = 0, chargedAt = -1000;
                // One update: the pad control, Ember's input for Player 2, the
                // dummy's free frame (-1 before it is known), the move's
                // buttons and the direction it charges (0 none). Whether the move starts.
                bool Update(int frame, bool pad, unsigned input, int free, unsigned buttons, unsigned charge) {
                    read.push_back(pad ? input : 0u);
                    const unsigned acted = frame >= 1 ? read[frame - 1] : 0u, pressed = acted & ~last;
                    last = acted;
                    if (!(acted & charge)) run = 0;
                    else if (++run >= 55) chargedAt = frame;
                    const bool charged = !charge || frame - chargedAt <= 10;
                    return pad && free >= 0 && frame >= free && (pressed & buttons) == buttons && charged;
                }
            };
            // button: the update the reply's first attack button was read in;
            // free: the dummy's first free frame; move: the frame its move
            // starts; ended: the first update after the reply that no longer
            // plays it; read: Player 2's input as the game read it, per update.
            struct Run { int button = -1, free = -1, move = -1, ended = -1; bool dropped = false; std::vector<unsigned> read; };
            // What the runtime keeps across stuns: the stun watch, the session
            // and the stretch a reply was last decided for. Faces right, as a
            // slot holds its frames, so a charge's back is Left.
            struct Dummy {
                DummyWatch watch; Session session; unsigned decided = ~0u;
                Dummy() { session.Enter(); session.SetReady(true); session.SetPositions(1, 0); }
            };
            // statuses: the dummy's, one per frame; it is free from the first
            // one after the stun that holds it in none.
            const auto play = [](Dummy& dummy, const std::vector<Input>& frames, int timing, const std::vector<unsigned>& statuses, unsigned charge) {
                DummyWatch& watch = dummy.watch; Session& session = dummy.session; unsigned& decided = dummy.decided;
                Run run; bool held = false, began = false;
                DummyControl control; Game game;
                const auto sample = [&](int frame) {
                    FighterSample dummy; dummy.valid = true; dummy.status = statuses[frame]; dummy.action = static_cast<int>(statuses[frame]) * 10;
                    return dummy;
                };
                int free = -1;
                for (int frame = 0, stunned = 0; frame < static_cast<int>(statuses.size()) && free < 0; ++frame) {
                    if (DummyHold(sample(frame)) > 0) stunned = 1;
                    else if (stunned) free = frame;
                }
                const auto press = std::find_if(frames.begin(), frames.end(), [](const Input& input) { return (input.raw & AttackButtons) != 0; });
                const unsigned buttons = press == frames.end() ? 0u : press->raw & AttackButtons;
                for (int frame = 0; frame < static_cast<int>(statuses.size()); ++frame) {
                    const bool replying = session.Replying();
                    if (began && !replying && run.ended < 0) run.ended = frame;
                    const auto step = control.Follow(PlanInput(session.GetView(), false).suppliesDummy, 0);
                    const Frame output = session.Prepare(Frame{});
                    if (game.Update(frame, step == DummyControl::Step::Begin || step == DummyControl::Step::Keep, output[1].raw, free, buttons, charge) &&
                        run.move < 0)
                        run.move = frame;
                    control.Simulated();
                    // As the runtime observes the fight before committing the update (TrainingRuntime.cxx).
                    const DummySeen seen = watch.Observe(sample(frame), 100);
                    session.Observe(ClassifyStatus(statuses[frame]) == Phase::Neutral, false, -1,
                        session.Replying() && ReplyFirstMoveDue(frames, seen, timing));
                    session.Commit(output);
                    if (replying && run.button < 0 && (output[1].raw & AttackButtons)) run.button = frame;
                    if (seen.held) held = true;
                    else if (held && run.free < 0) run.free = frame;
                    if (seen.held && seen.stretch != decided && session.Replying()) { session.StopReply(); run.dropped = true; }
                    const int cause = seen.held ? seen.held : seen.out;
                    if (cause && seen.stretch != decided && ReplyBegins(frames, seen, timing)) { decided = seen.stretch; began = session.Reply(frames); }
                }
                run.read = game.read;
                return run;
            };
            // Idle, the stun, then what follows it, then idle until the reply is long over.
            const auto statuses = [](unsigned stun, int frames, unsigned after, int afterFrames) {
                std::vector<unsigned> out(5, 0u);
                out.insert(out.end(), frames, stun); out.insert(out.end(), afterFrames, after); out.insert(out.end(), 120, 0u);
                return out;
            };
            const unsigned guardCrouch = ActorStatus::GuardCrouch, walkBack = ActorStatus::Backward;
            using namespace sf4e::combo;
            // A button, two or three pressed together: all on one frame, with
            // none the frame before, so each is a new press of them all.
            const std::pair<const char*, unsigned> presses[] = {{"5MK", MK}, {"LP+LK", LP | LK}, {"MP+MK", MP | MK}, {"PPP", Punches}, {"KKK", Kicks}};
            for (const auto& pressed : presses) {
                const auto frames = Synthesize({pressed.first}, true);
                const int start = ReplyStart(frames);
                Require(start >= 0 && frames[start].raw == pressed.second && ReplyLead(frames) == 0, "A press was not all its buttons on the reply's first frame");
                for (int timing = 0; timing <= MaxReplyTiming; ++timing) {
                    // Blockstun, seen first: replied to as it ends, later by
                    // timing. Seen again: at timing 0 the press is a reversal,
                    // its move starting on the dummy's free frame; a later one
                    // waits past it.
                    Dummy dummy;
                    const auto block = statuses(ActorStatus::DamageGuard, 18, 0u, 0);
                    const Run first = play(dummy, frames, timing, block, 0);
                    Require(first.free == 23 && first.move == first.free + (std::max)(2, timing) && !first.dropped,
                        "A press after a blockstun not seen before did not come out as the reply began");
                    const Run timed = play(dummy, frames, timing, block, 0);
                    Require(timed.button + 1 == timed.free + timing && timed.move == timed.free + timing && !timed.dropped,
                        "A press after a timed blockstun did not start its move on the free frame, plus timing");
                }
            }
            {
                // A direction held from the stun's first frame until the move
                // after it is due, then the move: down-back through a
                // blockstun, given way so its button is acted on the free
                // frame plus timing, the plan's and the move's own, whatever
                // the dummy does once free.
                for (const char* move : {"3HP", "623P", "236P", "22LK", "3HP@+3", "3HP@-2"}) {
                    const auto frames = Synthesize({"[1]", move}, true);
                    Step step; std::string error;
                    Require(ParseStep(move, step, error) && ReplyHolds(frames) && ReplyStart(frames) == 0, "A reply did not begin with its hold");
                    for (const unsigned after : {0u, guardCrouch}) for (int timing = -MaxReplyTiming; timing <= MaxReplyTiming; ++timing) {
                        const int at = timing + step.offset;
                        Dummy dummy;
                        const auto block = statuses(ActorStatus::DamageGuard, 18, after, 20);
                        const Run first = play(dummy, frames, timing, block, 0);
                        // Not timed, the hold gives way once the dummy is seen free, later by timing.
                        Require(first.read[6] == (Down | Left) && first.read[22] == (Down | Left) &&
                            first.button + 1 == first.free + (std::max)(ReplyLead(frames) + 2, at) && !first.dropped,
                            "A hold over a blockstun not seen before did not begin with it, or gave way at the wrong frame");
                        const Run timed = play(dummy, frames, timing, block, 0);
                        bool heldThrough = true;
                        for (int frame = 6; frame < timed.button - ReplyLead(frames); ++frame) heldThrough = heldThrough && timed.read[frame] == (Down | Left);
                        Require(heldThrough && timed.button + 1 == timed.free + at && (at < 0 || timed.move == timed.free + at) && !timed.dropped,
                            "A hold over a timed blockstun did not give way so its move's button met the free frame plus timing");
                    }
                }
                // A stun longer than any recovery, a dizzy: the hold waits it out.
                const auto frames = Synthesize({"[1]", "3HP"}, true);
                {
                    Dummy dummy;
                    const auto dizzy = statuses(ActorStatus::Stun, 150, 0u, 0);
                    play(dummy, frames, 0, dizzy, 0);
                    Require(play(dummy, frames, 0, dizzy, 0).move == 155, "A hold gave up before a long stun ended");
                }
                // A hold later in a reply waits as any link does: walking back
                // or in guard stance the dummy is never seen neutral, and the
                // wait gives up rather than keep it on the pad.
                const auto later = Synthesize({"5LP", "[4]120f", "[4]", "6P"}, true);
                for (const unsigned after : {walkBack, guardCrouch}) {
                    Dummy dummy;
                    const auto walking = statuses(ActorStatus::DamageGuard, 18, after, 400);
                    play(dummy, later, 0, walking, 0);
                    const Run run = play(dummy, later, 0, walking, 0);
                    Require(run.ended > run.free + 120 + MaxWaitFrames && run.ended < run.free + 120 + MaxWaitFrames + 12,
                        "A later hold in a reply kept the dummy on the pad past its wait");
                }
                // The hold is the charge only when the stun is long enough to
                // charge in: after a short blockstun "[1] > 6P" comes out uncharged.
                const auto charge = Synthesize({"[1]", "6P"}, true);
                Dummy longDummy;
                const auto longBlock = statuses(ActorStatus::DamageGuard, 80, 0u, 0);
                play(longDummy, charge, 0, longBlock, Left);
                Require(play(longDummy, charge, 0, longBlock, Left).move == 85, "A charge held through a long blockstun was no reversal");
                Dummy shortDummy;
                const auto shortBlock = statuses(ActorStatus::DamageGuard, 18, 0u, 0);
                play(shortDummy, charge, 0, shortBlock, Left);
                Require(play(shortDummy, charge, 0, shortBlock, Left).move < 0, "A short blockstun charged a hold");
            }
            for (const char* move : {"[4]6P", "[2]8K"}) {
                const auto frames = Synthesize({move}, true);
                const unsigned back = move[1] == '4' ? Left : Down;
                int hold = 0;
                while (hold < static_cast<int>(frames.size()) && frames[hold].raw == back) ++hold;
                Require(hold == ChargeFrames && ChargeFrames >= 60 && ReplyStart(frames) == 0 && ReplyLead(frames) == ChargeFrames + 3,
                    "A charge was held too briefly for some fighter, or its lead misread");
                for (const int timing : {-1, 0, 2}) {
                    // Getting up, and a blockstun with the charge held back
                    // through it, each long enough to charge in: the first time
                    // replied to as it ends; timed, the button lands on the free
                    // frame, plus timing, charged.
                    const std::pair<unsigned, int> stuns[] = {{static_cast<unsigned>(ActorStatus::Rise), 100}, {static_cast<unsigned>(ActorStatus::DamageGuard), 90}};
                    for (const auto& stun : stuns) {
                        const int length = stun.second;
                        Dummy dummy;
                        const auto held = statuses(stun.first, length, 0u, 0);
                        const Run first = play(dummy, frames, timing, held, back);
                        Require(first.free == 5 + length && first.button == first.free + 1 + ChargeFrames + 3 && first.move == first.button + 1 &&
                            !first.dropped, "A charge reply to a stun not seen before was not played as it ended");
                        const Run timed = play(dummy, frames, timing, held, back);
                        Require(timed.button + 1 == timed.free + timing && !timed.dropped,
                            "A charge reply's button did not land on the free frame, plus timing");
                        Require(timing < 0 || timed.move == timed.button + 1, "A charge held through the stun did not come out on its button");
                    }
                    // Blockstun too short to charge in, then guard stance standing or crouching:
                    // the reply begins on the stun's first frame, is not dropped, and comes out charged.
                    for (const unsigned guard : {static_cast<unsigned>(ActorStatus::GuardStand), static_cast<unsigned>(ActorStatus::GuardCrouch)}) {
                        Dummy blockDummy;
                        const auto block = statuses(ActorStatus::DamageGuard, 18, guard, 80);
                        play(blockDummy, frames, timing, block, back);
                        const Run blocked = play(blockDummy, frames, timing, block, back);
                        Require(blocked.free == 23 && blocked.button == 5 + 1 + ChargeFrames + 3 && blocked.move == blocked.button + 1 && !blocked.dropped,
                            "A charge reply over a short blockstun was dropped in guard stance, started late or came out uncharged");
                    }
                }
            }
        }
        std::puts("Training dummy control checks passed.");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
