// When the Training dummy plays from Player 2's pad (TrainingSession.hxx,
// DummyControl): the updates the pad covers at either input lag, a pause-menu
// action under Ember, and the input plan, the session and the control
// together through a held recording, resets and a restarted playback.
#include "../training/TrainingSession.hxx"
#include <cstdio>
#include <stdexcept>
#include <vector>
using namespace sf4e::training;
void Require(bool pass, const char* why) { if (!pass) throw std::runtime_error(why); }
int main() {
    try {
        {
            // The dummy plays from the pad for the updates that act on Ember's
            // input: from the first one Ember supplies, input lag later, to the
            // last one, as late again, and its own control stands around them.
            using Step = DummyControl::Step;
            // One update: whether Ember supplies Player 2's input in it, then
            // whether the battle simulated it.
            const auto step = [](DummyControl& control, bool supplies, int lag, int action = 0, bool simulated = true) {
                const Step seen = control.Follow(supplies, lag, action);
                if (simulated) control.Simulated();
                return seen;
            };
            const auto steps = [&](int lag, std::vector<bool> supplies) {
                DummyControl control; std::vector<Step> seen;
                for (const bool supply : supplies) seen.push_back(step(control, supply, lag));
                return seen;
            };
            Require(steps(0, {false, true, true, false, false}) == std::vector<Step>{Step::None, Step::Begin, Step::Keep, Step::End, Step::None},
                "With no input lag the pad did not cover exactly the supplied updates");
            Require(steps(1, {false, true, true, false, false}) == std::vector<Step>{Step::None, Step::None, Step::Begin, Step::Keep, Step::End},
                "With a frame of input lag the pad did not start and end a frame later");
            Require(steps(1, {true, false, true, false}) == std::vector<Step>{Step::None, Step::Begin, Step::End, Step::Begin},
                "A one-update gap between two drives did not give the dummy its own control for that update");
            {
                // Updates the battle does not simulate move nothing on: the
                // pad waits with them for the frame that acts on the last input.
                DummyControl held;
                step(held, true, 1);
                Require(step(held, false, 1, 0, false) == Step::Begin && step(held, false, 1, 0, false) == Step::Keep &&
                    step(held, false, 1) == Step::Keep && step(held, false, 1) == Step::End,
                    "An update the battle did not simulate moved the input lag on");
            }
            // Ember's own changes to the action keep the pad; one from the
            // pause menu ends it and stands, and nothing begins again from the
            // update before it.
            DummyControl control; int native = 0;
            Require(!control.Reconcile(3), "An action change with Ember idle was taken for one under it");
            Require(step(control, true, 0, native) == Step::Begin, "The pad did not begin");
            control.Set(native, 1);
            Require(!control.Reconcile(native) && step(control, true, 0, native) == Step::Keep, "Ember's own change to the action ended the pad");
            native = 3;
            Require(control.Reconcile(native) && native == 3 && step(control, false, 0, native) == Step::None,
                "A pause-menu action did not end the pad");
            // In the input lag's first frame, before the pad, Ember already drives.
            DummyControl lagged;
            Require(step(lagged, true, 1) == Step::None && lagged.Reconcile(3) && step(lagged, false, 1, 3) == Step::None,
                "A pause-menu action in the input lag's first frame did not end the drive, or the pad began after it");
            // A reset breaks the input's history, not whose action it is.
            DummyControl rewound;
            step(rewound, true, 0);
            Require(rewound.Rewind() && rewound.Reconcile(3), "A reset lost the action Ember drove under");
            // The battle closes with the pad on, and with it off.
            DummyControl closing;
            step(closing, true, 0);
            Require(closing.Release() && !closing.Release() && step(closing, false, 0) == Step::None, "Closing the battle misreported the pad");
        }
        {
            // The input plan and the dummy's control together, update by
            // update as the runtime runs them: a recording held under the open
            // controls and resumed, a reset in a reply's first lag frame and
            // later in it, a playback a reset starts again, and a pause-menu
            // action met in the same update as a reset.
            struct Bench {
                Session session; DummyControl control; int lag;
                // The Training menu's Action, as the pause menu leaves it.
                int action = 0;
                explicit Bench(int lag) : lag(lag) { session.Enter(); session.SetReady(true); session.SetCheckpoint(true); }
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
                    const auto step = control.Follow(plan.suppliesDummy, lag, action);
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
            for (const int lag : {0, 1}) {
                Bench recording(lag);
                Require(recording.Send(Action::Record), "The recording did not start");
                const auto pads = recording.Run({false, false, true, true, false, false});
                Require(pads == (lag ? std::vector<bool>{false, true, true, false, false, true} : std::vector<bool>{true, true, false, false, true, true}),
                    "A recording held under the open controls kept the dummy on the pad, or did not take it back on resuming");
                Require(recording.session.GetView().mode == Mode::Recording && recording.session.GetView().cursor == 4,
                    "The held recording stopped or recorded the held updates");

                Bench reply(lag);
                Require(reply.session.Reply(moves) && reply.Update() == (lag == 0), "The reply's first update was wrong");
                Require(reply.Reset() && !reply.Finish() && !reply.Update(), "A reset in the reply's first lag frame left the dummy on the pad");
                Require(reply.session.Reply(moves) && reply.Run({false, false, false}) == std::vector<bool>{lag == 0, true, true}, "The reply did not play from the pad");
                Require(reply.Reset() && !reply.Finish(), "A reset during the reply left the dummy on the pad for the old timeline");

                Bench playback(lag);
                Command load; load.action = Action::Load; load.generation = playback.session.GetView().generation; load.frames = moves; load.side = 1;
                Require(playback.session.Apply(load) && playback.Send(Action::Play), "The playback did not start");
                Require(playback.Run({false, false, false}) == std::vector<bool>{lag == 0, true, true}, "The playback did not play from the pad");
                Require(playback.Reset() && playback.session.GetView().mode == Mode::Playback && playback.session.GetView().cursor == 0,
                    "The reset did not start the playback again");
                Require(playback.Finish() == (lag == 0) && playback.Update(),
                    "The restarted playback put the dummy on the pad for the old timeline's input, or not for its own");

                // The pause menu changed the action, and the first update to
                // see it resets the battle: the choice stands and Ember stops.
                Bench recordingTaken(lag);
                Require(recordingTaken.Send(Action::Record) && recordingTaken.Run({false, false}) == std::vector<bool>{lag == 0, true},
                    "The recording did not play from the pad");
                recordingTaken.action = 3;
                Require(recordingTaken.Reset() && recordingTaken.session.GetView().mode == Mode::Idle && !recordingTaken.Finish() && !recordingTaken.Update(),
                    "A reset with a pause-menu action kept the recording playing the dummy");
                Bench playbackTaken(lag);
                load.generation = playbackTaken.session.GetView().generation;
                Require(playbackTaken.session.Apply(load) && playbackTaken.Send(Action::Play) &&
                    playbackTaken.Run({false, false}) == std::vector<bool>{lag == 0, true}, "The playback did not play from the pad");
                playbackTaken.action = 3;
                Require(playbackTaken.Reset() && playbackTaken.session.GetView().mode == Mode::Idle && !playbackTaken.Finish() && !playbackTaken.Update(),
                    "A reset with a pause-menu action started the playback again under it");
            }
        }
        std::puts("Training dummy control checks passed.");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
