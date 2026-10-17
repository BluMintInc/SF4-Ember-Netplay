// The dummy's recording bank, its slot picker and the banks kept between
// battles (RecordingBank.hxx). Built without TrainingSession.hxx: compiling
// this file alone checks that the bank stands apart from the session.
#include "../training/RecordingBank.hxx"
#include <cstdio>
#include <initializer_list>
#include <set>
#include <stdexcept>
#include <vector>
using namespace sf4e::training;
void Require(bool pass, const char* why) { if (!pass) throw std::runtime_error(why); }
std::vector<Input> Frames(std::initializer_list<unsigned> raws) {
    std::vector<Input> frames;
    for (const unsigned raw : raws) frames.push_back({raw, raw, 0, 0});
    return frames;
}
SlotSet Set(std::initializer_list<int> slots) {
    SlotSet set = 0;
    for (const int slot : slots) set |= 1u << slot;
    return set;
}
// The slots a picker plays over decisions that all start.
std::vector<int> Play(SlotPicker& picker, SlotSet set, Order order, int count) {
    std::vector<int> played;
    for (int i = 0; i < count; ++i) { const int slot = picker.Peek(set, order); played.push_back(slot); picker.Commit(slot); }
    return played;
}
int main() {
    try {
        {
            // A slot that gets a recording starts on; an empty one is off and
            // cannot be turned on; every change moves the revision on.
            RecordingBank bank;
            Require(!bank.On(0) && bank.Playable() == 0 && bank.Replyable() == 0 && !bank.SetOn(0, true), "An empty slot was on or could be turned on");
            auto revision = bank.Revision();
            bank.Replace(2, Frames({0, 0x10}));
            Require(bank.On(2) && bank.Revision() != revision && bank.Playable() == Set({2}) && bank.Replyable() == Set({2}), "A recorded slot did not start on");
            revision = bank.Revision();
            Require(bank.SetOn(2, false) && !bank.On(2) && bank.Playable() == 0 && bank.Revision() != revision, "Turning a slot off did not take it out");
            revision = bank.Revision();
            Require(bank.SetOn(2, false) && bank.Revision() == revision, "Turning a slot off twice changed the bank");
            bank.SetOrder(Order::InOrder);
            Require(bank.GetOrder() == Order::InOrder && bank.Revision() != revision, "The order did not change the revision");
            revision = bank.Revision();
            bank.MirrorSlot(2);
            Require(bank.Revision() != revision && bank.Slot(2)[1].raw == 0x10, "Mirror did not change the revision");
            bank.Replace(2, {});
            Require(!bank.On(2) && bank.Slot(2).empty(), "A cleared slot stayed on");
            bank.Replace(3, std::vector<Input>(MaxFrames + 10, Input{1, 1, 0, 0}));
            Require(static_cast<int>(bank.Slot(3).size()) == MaxFrames, "A slot held more than its limit");
        }
        {
            // In order with gaps: each slot that is on in turn, around again.
            SlotPicker picker;
            const SlotSet gaps = Set({1, 4, 6});
            Require(Play(picker, gaps, Order::InOrder, 4) == std::vector<int>{1, 4, 6, 1}, "In order did not play the slots that are on in turn");
            // A proposal that never starts moves nothing on.
            Require(picker.Peek(gaps, Order::InOrder) == 4 && picker.Peek(gaps, Order::InOrder) == 4, "A peek moved the order on");
            // A different set starts the order again from its first slot.
            picker.Commit(4);
            Require(picker.Peek(Set({0, 1, 4, 6}), Order::InOrder) == 0, "A change to the slots that are on kept the old place in the order");
            Require(picker.Peek(Set({4, 6}), Order::InOrder) == 4, "A smaller set did not start from its first slot");
            // At random: only slots that are on, and each of them in time.
            std::set<int> seen;
            for (int i = 0; i < 300; ++i) {
                const int slot = picker.Peek(gaps, Order::Random);
                Require(InSet(gaps, slot), "Random played a slot that is off");
                seen.insert(slot);
                if (i % 2) picker.Commit(slot);
            }
            Require(seen.size() == 3, "Random never played one of the slots that are on");
            // Each decision draws again, so one never started is not proposed for good.
            std::set<int> undecided;
            for (int i = 0; i < 100; ++i) undecided.insert(picker.Peek(gaps, Order::Random));
            Require(undecided.size() > 1, "Random kept proposing the same slot");
            // Nothing on: nothing to propose.
            Require(picker.Peek(0, Order::InOrder) == -1 && picker.Peek(0, Order::Random) == -1, "An empty set proposed a slot");
        }
        {
            // A recording that only stands is played by hand, never as a reply,
            // so ahead of a slot that presses something it never holds up the
            // reply, in either order.
            RecordingBank bank;
            bank.Replace(0, Frames({0, 0, 0}));
            bank.Replace(1, Frames({0, 0x40, 0}));
            Require(bank.Playable() == Set({0, 1}) && bank.Replyable() == Set({1}), "Standing only was a reply, or not a playback");
            for (const Order order : {Order::InOrder, Order::Random}) {
                bank.SetOrder(order);
                SlotPicker reply, manual;
                for (int i = 0; i < 20; ++i) {
                    const int slot = reply.Peek(bank.Replyable(), bank.GetOrder());
                    Require(slot == 1, "A recording that only stands was proposed as a reply");
                    reply.Commit(slot);
                }
                bool standing = false;
                for (int i = 0; i < 40; ++i) { const int slot = manual.Peek(bank.Playable(), bank.GetOrder()); standing = standing || slot == 0; manual.Commit(slot); }
                Require(standing, "A recording that only stands did not play by hand");
            }
        }
        {
            // Kept between battles, one bank per dummy fighter: A, then B, then A
            // again finds A's slots; the same fighter again keeps them; a second
            // close keeps nothing new; an unknown fighter's bank is not kept.
            const int a = 3, b = 17;
            RecordingBanks banks;
            RecordingBank first = banks.Attach(a);
            Require(first.Playable() == 0, "A new fighter's bank was not empty");
            first.Replace(0, Frames({0x10}));
            banks.Detach(first);
            RecordingBank second = banks.Attach(b);
            Require(second.Playable() == 0 && !banks.Holds(b), "Another fighter got the first one's slots");
            second.Replace(5, Frames({0x20}));
            banks.Detach(second);
            RecordingBank again = banks.Attach(a);
            Require(again.Playable() == Set({0}) && again.Slot(0)[0].raw == 0x10, "A fighter picked again lost its slots");
            again.Replace(1, Frames({0x40}));
            banks.Detach(again);
            // A second close of the same battle keeps nothing over what was kept.
            banks.Detach(RecordingBank{});
            RecordingBank restarted = banks.Attach(a);
            Require(restarted.Playable() == Set({0, 1}), "A restarted battle lost its fighter's slots, or a second close replaced them");
            banks.Detach(restarted);
            RecordingBank unknown = banks.Attach(-1);
            unknown.Replace(0, Frames({0x80}));
            banks.Detach(unknown);
            Require(!banks.Holds(-1) && banks.Attach(-1).Playable() == 0, "An unknown fighter's bank was kept");
            banks.Detach(RecordingBank{});
            Require(banks.Attach(b).Playable() == Set({5}), "The other fighter's slots went");
        }
        std::puts("Training recording bank checks passed.");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
