// Recording with standby, update by update as the runtime runs it
// (TrainingRuntime.cxx, BeforeUpdate): the command queue and its one batch
// rule (TrainingCommands.hxx), the session's modes, the slot a recording
// replaces only when it stops, and the dummy's pad control after it.
#include "../training/TrainingCommands.hxx"
#include <cstdio>
#include <initializer_list>
#include <stdexcept>
#include <utility>
#include <vector>
using namespace sf4e::training;
void Require(bool pass, const char* why) { if (!pass) throw std::runtime_error(why); }
std::vector<Input> Frames(std::initializer_list<unsigned> raws) {
    std::vector<Input> frames;
    for (const unsigned raw : raws) frames.push_back({raw, raw, 0, 0});
    return frames;
}
bool SameRaws(const std::vector<Input>& frames, std::initializer_list<unsigned> raws) {
    if (frames.size() != raws.size()) return false;
    auto at = frames.begin();
    for (const unsigned raw : raws) if ((at++)->raw != raw) return false;
    return true;
}
// The session, the queue the overlay fills and the dummy's control, with slot
// 0 holding an old recording.
struct Bench {
    Session session; CommandQueue queue; Acks acks; DummyControl control;
    std::vector<DummyControl::Step> steps;
    explicit Bench(RecordingBank bank = Old()) { session.Enter(std::move(bank)); session.SetReady(true); }
    static RecordingBank Old() { RecordingBank bank; bank.Replace(0, Frames({0x10, 0x20})); return bank; }
    const View& view() const { return session.GetView(); }
    // As the overlay submits (TrainingRuntime.cxx, Submit): whether the queue took it.
    bool Send(Action action, std::uint64_t id = 0, std::uint64_t generation = 0) {
        Command command; command.action = action; command.requestId = id; command.generation = generation ? generation : view().generation;
        return queue.Push(command);
    }
    // One update: its batch, then its input with the pad pressing pad.
    void Update(unsigned pad = 0) {
        std::deque<Command> taken; std::vector<std::uint64_t> replaced;
        queue.Take(taken, replaced);
        RunBatch(PrepareBatch(taken, replaced, view().generation), acks, [&](const Command& command) { return session.Apply(command); });
        const InputPlan plan = PlanInput(view(), false);
        steps.push_back(control.Follow(plan.suppliesDummy, 0));
        if (plan.read) { Frame physical; physical[0] = {pad, pad, 0, 0}; session.Commit(session.Prepare(physical)); }
        control.Simulated();
    }
    bool OldKept() const { return SameRaws(session.Slot(0), {0x10, 0x20}) && session.Bank().On(0); }
    bool Answered(std::uint64_t id, bool expected) const { bool accepted = !expected; return acks.Find(id, accepted) && accepted == expected; }
    // Standby, then a recording of the pad's presses, one per update.
    void Record(std::initializer_list<unsigned> presses) {
        Send(Action::AdvanceRecording); Update();
        // The update that starts the recording records its own input.
        Send(Action::AdvanceRecording);
        for (const unsigned pad : presses) Update(pad);
        Require(view().mode == Mode::Recording, "The second press did not start recording");
    }
};
int main() {
    try {
        {
            // Standby: P1's pad plays the dummy and nothing is recorded; the
            // old slot stays, and Stop leaves it as it was.
            Bench bench;
            const auto revision = bench.session.Bank().Revision();
            bench.Send(Action::AdvanceRecording); bench.Update();
            Require(bench.view().mode == Mode::Standby && DrivesDummy(bench.view()), "The first press did not go to standby on the dummy");
            Frame physical; physical[0] = {0x40, 0x40, 0, 0};
            const Frame output = bench.session.Prepare(physical);
            Require(output[1].raw == 0x40 && output[0].raw == 0, "Player 1's pad did not play the dummy in standby");
            for (int i = 0; i < 5; ++i) bench.Update(0x40);
            Require(bench.OldKept() && bench.view().cursor == 0 && bench.session.Bank().Revision() == revision, "Standby recorded something");
            bench.Send(Action::Stop); bench.Update();
            Require(bench.view().mode == Mode::Idle && bench.OldKept() && bench.session.Bank().Revision() == revision, "Stop in standby stored something");
        }
        {
            // Recording keeps the old slot until it stops; the third press
            // stops it and replaces the slot in one change.
            Bench bench;
            bench.Record({0x40, 0x80, 0x40});
            Require(bench.OldKept() && bench.view().cursor == 3, "The old slot changed while recording");
            const auto revision = bench.session.Bank().Revision();
            bench.Send(Action::AdvanceRecording); bench.Update();
            Require(bench.view().mode == Mode::Idle && SameRaws(bench.session.Slot(0), {0x40, 0x80, 0x40}) && bench.session.Bank().On(0) &&
                bench.session.Bank().Revision() == revision + 1, "The third press did not replace the slot in one change");
        }
        {
            // Losing the focus while recording interrupts it, and what was recorded is kept.
            Bench bench;
            bench.Record({0x40, 0x40});
            bench.Send(Action::Interrupt); bench.Update();
            Require(bench.view().mode == Mode::Idle && SameRaws(bench.session.Slot(0), {0x40, 0x40}), "Losing the focus lost the recording");
        }
        {
            // A full queue takes no other command but always takes Interrupt.
            // In standby it ends with the old slot.
            Bench bench;
            bench.Send(Action::AdvanceRecording); bench.Update();
            const auto revision = bench.session.Bank().Revision();
            int taken = 0;
            while (bench.Send(Action::Loop)) ++taken;
            Require(taken == static_cast<int>(CommandQueue::Capacity) - 1 && !bench.Send(Action::Play), "The queue did not keep a place for Interrupt");
            Require(bench.Send(Action::Interrupt) && bench.Send(Action::Interrupt) && bench.queue.Size() == CommandQueue::Capacity,
                "A full queue refused Interrupt, or repeats took more than its place");
            bench.Update();
            Require(bench.view().mode == Mode::Idle && bench.OldKept() && bench.session.Bank().Revision() == revision, "Interrupt in standby stored something");
        }
        {
            // During a recording: it ends Idle with what was recorded, in one
            // change, and the dummy stays on the pad for the update that acts
            // on its last input before it has its own control back.
            Bench bench;
            bench.Record({0x40, 0x80});
            const auto revision = bench.session.Bank().Revision();
            while (bench.Send(Action::Loop)) {}
            Require(bench.Send(Action::Interrupt, 9), "A full queue refused Interrupt");
            bench.Update(); bench.Update();
            Require(bench.view().mode == Mode::Idle && SameRaws(bench.session.Slot(0), {0x40, 0x80}) && bench.session.Bank().Revision() == revision + 1,
                "Interrupt did not keep the recording in one change");
            const auto n = bench.steps.size();
            Require(bench.steps[n - 2] == DummyControl::Step::Keep && bench.steps[n - 1] == DummyControl::Step::End && bench.Answered(9, true),
                "The dummy's pad control did not end one update after the recording");
        }
        {
            // Repeated Interrupts keep the latest place. Interrupt, Advance,
            // Interrupt from rest: the Advance before the last Interrupt goes,
            // so it ends Idle with the old slot, and every request is answered.
            Bench bench;
            Require(bench.Send(Action::Interrupt, 1) && bench.Send(Action::AdvanceRecording, 2) && bench.Send(Action::Interrupt, 3), "A command was refused");
            bench.Update();
            Require(bench.view().mode == Mode::Idle && bench.OldKept(), "Interrupt, Advance, Interrupt did not end at rest with the old slot");
            Require(bench.Answered(1, false) && bench.Answered(2, false) && bench.Answered(3, true), "A dropped command was not answered no");
            // Interrupt, Play, Interrupt: the playback starts and the last Interrupt ends it.
            Require(bench.Send(Action::Interrupt, 4) && bench.Send(Action::Play, 5) && bench.Send(Action::Interrupt, 6), "A command was refused");
            bench.Update();
            Require(bench.view().mode == Mode::Idle && bench.Answered(4, false) && bench.Answered(5, true) && bench.Answered(6, true),
                "Interrupt, Play, Interrupt did not end at rest");
            // Already recording: exactly one commit of what was recorded.
            bench.Record({0x40});
            const auto revision = bench.session.Bank().Revision();
            bench.Send(Action::Interrupt, 7); bench.Send(Action::AdvanceRecording, 8); bench.Send(Action::Interrupt, 10);
            bench.Update();
            Require(bench.view().mode == Mode::Idle && SameRaws(bench.session.Slot(0), {0x40}) && bench.session.Bank().Revision() == revision + 1 &&
                bench.Answered(8, false), "A recording was committed more than once, or not at all");
        }
        {
            // An Interrupt sent after an Advance in the same batch wins: from
            // rest or from standby, nothing is recorded and the old slot stays.
            for (const bool standby : {false, true}) {
                Bench bench;
                if (standby) { bench.Send(Action::AdvanceRecording); bench.Update(); }
                bench.Send(Action::AdvanceRecording, 11); bench.Send(Action::Interrupt, 12);
                bench.Update(0x40); bench.Update(0x40);
                Require(bench.view().mode == Mode::Idle && bench.OldKept() && bench.Answered(11, false) && bench.Answered(12, true),
                    "An Advance before an Interrupt in one batch went ahead");
            }
        }
        {
            // An Interrupt from a battle that has closed never reaches the next
            // one's recording, whether it waits in the batch before the new
            // battle's presses or arrives after them.
            RecordingBanks banks;
            Bench bench(banks.Attach(3));
            const auto old = bench.view().generation;
            bench.Send(Action::Interrupt, 20);
            banks.Detach(bench.session.TakeBank()); bench.session.Reset(); bench.session.Enter(banks.Attach(3)); bench.session.SetReady(true);
            bench.Send(Action::AdvanceRecording); bench.Send(Action::AdvanceRecording);
            bench.Update(0x40);
            Require(bench.view().mode == Mode::Recording && bench.Answered(20, false), "A stale Interrupt stopped the new battle's recording");
            bench.Send(Action::Interrupt, 21, old);
            bench.Update(0x40);
            Require(bench.view().mode == Mode::Recording && bench.view().cursor == 2 && bench.Answered(21, false), "A late stale Interrupt stopped the recording");
        }
        {
            // The slot chosen for editing and the one that plays are apart:
            // Play picks a slot that is on, and the selection stays.
            Bench bench;
            Command select; select.action = Action::Select; select.generation = bench.view().generation; select.slot = 5;
            Require(bench.session.Apply(select), "The empty slot could not be selected");
            bench.Send(Action::Play); bench.Update();
            Require(bench.view().mode == Mode::Playback && bench.view().active == 0 && bench.view().selected == 5, "Play changed the selection or played the wrong slot");
            // A slot turned off is not played; with none on, Play is refused.
            Command off; off.action = Action::SlotOn; off.generation = bench.view().generation; off.slot = 0; off.on = false;
            Require(bench.session.Apply(off) && !bench.view().on[0] && bench.view().playable == 0, "The slot did not turn off");
            bench.Send(Action::Play, 30); bench.Update();
            Require(bench.view().mode == Mode::Idle && bench.Answered(30, false), "Play started with no slot on");
            // Standby from a playback ends the playback first.
            off.on = true; bench.session.Apply(off);
            bench.Send(Action::Play); bench.Update();
            bench.Send(Action::AdvanceRecording); bench.Update();
            Require(bench.view().mode == Mode::Standby && bench.view().active == -1, "Standby did not end the playback");
        }
        {
            // The bank outlives the battle: closing during a recording keeps
            // it, and the same dummy finds its slots in the next battle.
            RecordingBanks banks;
            Bench bench(banks.Attach(7));
            bench.Record({0x40, 0x40, 0x40});
            banks.Detach(bench.session.TakeBank()); bench.session.Reset();
            Require(bench.session.Slot(0).empty(), "The closed battle kept its slots in the session");
            bench.session.Enter(banks.Attach(7)); bench.session.SetReady(true);
            Require(bench.view().lengths[0] == 3 && bench.view().on[0], "A recording under way at the close was not kept for the next battle");
        }
        std::puts("Training recording checks passed.");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
