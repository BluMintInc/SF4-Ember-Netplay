// The pad's chord, Back then Start, by the dummy's mode: on standby it starts
// the recording and opens nothing, so a pad alone can record; at rest, while
// recording and while playing it opens the controls, which interrupts a
// recording and keeps it (TrainingSession.hxx: ChordStartsRecording,
// TrainingPad.hxx: TrainingPadInput).
#include "../common/TrainingPad.hxx"
#include "../training/TrainingSession.hxx"
#include "test_support.hxx"
#include <cstdint>
#include <cstdio>

using namespace sf4e;
// One chord and its release, as the pad poll sees it, for a mode; what it
// asked for over the three samples.
struct Chord {
    input::TrainingPadInput pad;
    input::TrainingControls controls;
    double now = 0;
    input::TrainingPadEvents Press(training::Mode mode) {
        const float place[2] = {0, 0};
        input::PadOwner owner; owner.generation = 1; owner.epoch = controls.Epoch();
        input::TrainingPadEvents seen;
        for (const std::uint32_t physical : {input::PhysicalBack, input::PhysicalBack | input::PhysicalStart, 0u}) {
            now += .05;
            const auto events = pad.Update(physical, controls, now, input::TrainingCall::None, owner, place,
                training::ChordStartsRecording(mode)).events;
            seen.open = seen.open || events.open; seen.close = seen.close || events.close;
            seen.advance = seen.advance || events.advance;
            seen.reset = seen.reset || events.reset; seen.save = seen.save || events.save;
            seen.owned |= events.owned;
        }
        return seen;
    }
};

int main() {
    using training::Mode;
    // Standby: the recording starts; the controls stay shut, the Start is
    // still the chord's so the game never pauses on it, and Back neither
    // resets nor saves.
    {
        Chord chord;
        const auto seen = chord.Press(Mode::Standby);
        CHECK(seen.advance && !seen.open && !chord.controls.Open());
        CHECK((seen.owned & input::PhysicalStart) != 0 && !seen.reset && !seen.save);
    }
    // At rest, recording and playing: the chord opens the controls, as ever.
    for (const Mode mode : {Mode::Idle, Mode::Recording, Mode::Playback}) {
        Chord chord;
        const auto seen = chord.Press(mode);
        CHECK(seen.open && !seen.advance && chord.controls.Open() && !seen.reset && !seen.save);
    }
    // The advance is posted as the position events are, under the owner the
    // press went down with, so it reaches the controls' queue in order.
    {
        input::TrainingControls queue;
        input::TrainingPadEvent event; event.kind = input::TrainingPadEvent::Kind::Advance; event.generation = 1; event.epoch = queue.Epoch();
        CHECK(queue.Post(event));
        const auto taken = queue.Take();
        CHECK(taken.size() == 1 && taken[0].kind == input::TrainingPadEvent::Kind::Advance);
    }
    std::puts("Training pad standby checks passed.");
    return 0;
}
