#include "../common/GamepadSlots.hxx"
#include "../common/XInputMapping.hxx"
#include "../netplay/ControllerRestore.hxx"
#include "test_support.hxx"
#include <climits>
#include <iostream>

using namespace sf4e::gamepads;

static Identity PadIdentity(std::string guid) { return {guid, "", "path-" + guid, "Controller"}; }

static Pad Controller(Id id, std::string guid = {}) {
    Pad pad;
    pad.id = id; pad.identity = PadIdentity(guid.empty() ? "pad-" + std::to_string(id) : guid);
    pad.identity.name = "Controller " + std::to_string(id); pad.mapped = true; pad.type = Type::PS4;
    return pad;
}

static int Slot(const Inventory& inventory, Id id) {
    for (const auto& pad : inventory.pads) if (pad.id == id) return pad.slot;
    return -1;
}

static void IdentityMatching() {
    auto usb = PadIdentity("usb-guid"), bluetooth = PadIdentity("bluetooth-guid");
    usb.serial = bluetooth.serial = "DS4-123";
    CHECK(Same(usb, bluetooth));
    bluetooth.serial = "DS4-456";
    CHECK(!Same(usb, bluetooth));
    bluetooth = usb; bluetooth.serial.clear();
    CHECK(Same(usb, bluetooth));
    bluetooth.path += "other";
    CHECK(!Same(usb, bluetooth));
    bluetooth = usb; bluetooth.serial.clear(); bluetooth.guid += "other";
    CHECK(!Same(usb, bluetooth));
    bluetooth = usb; bluetooth.name = "New name";
    CHECK(Same(usb, bluetooth) && usb != bluetooth);
}

static void Reservations() {
    Slots slots;
    CHECK(slots.Update({}).pads.empty());
    CHECK(Slot(slots.Update({Controller(1), Controller(2)}), 2) == 1);
    auto pads = slots.Update({Controller(2), Controller(3)});
    // Slot 0 is reserved, so the new pad takes the first unreserved slot.
    CHECK(Slot(pads, 2) == 1 && Slot(pads, 3) == 2);
    pads = slots.Update({Controller(2), Controller(3), Controller(4, "pad-1")});
    CHECK(Slot(pads, 4) == 0 && Slot(pads, 2) == 1 && Slot(pads, 3) == 2);
    CHECK(slots.DroppedReservations().empty());
    slots.Update({});
    pads = slots.Update({Controller(5, "pad-2")});
    CHECK(Slot(pads, 5) == 1); // Session reservation survives an empty inventory.

    Slots takeover;
    takeover.Update({Controller(1), Controller(2), Controller(3), Controller(4)});
    pads = takeover.Update({Controller(2), Controller(3), Controller(4), Controller(5)});
    CHECK(Slot(pads, 5) == 0);
    CHECK(takeover.DroppedReservations().size() == 1);
    const auto& dropped = takeover.DroppedReservations().front();
    CHECK(dropped.slot == 0 && Same(dropped.owner, PadIdentity("pad-1")) && dropped.replacement == 5);
    pads = takeover.Update({Controller(2), Controller(4), Controller(5), Controller(6, "pad-1")});
    CHECK(Slot(pads, 5) == 0 && Slot(pads, 6) == 2); // Former owner takes another free reservation.
    pads = takeover.Update({Controller(2), Controller(4), Controller(6, "pad-1")});
    CHECK(Slot(pads, 6) == 2); // It never moves while connected.
    takeover.Update({Controller(2), Controller(4)});
    pads = takeover.Update({Controller(2), Controller(4), Controller(7, "pad-1")});
    CHECK(Slot(pads, 7) == 2); // Only its new reservation remains.

    Slots lowest;
    lowest.Update({Controller(1), Controller(2), Controller(3), Controller(4)});
    pads = lowest.Update({Controller(2), Controller(4), Controller(5)});
    CHECK(Slot(pads, 5) == 0); // Lowest free reservation is taken first.

    Slots serial;
    auto usb = Controller(1), bluetooth = Controller(2);
    usb.identity.serial = bluetooth.identity.serial = "DS4";
    serial.Update({Controller(3), usb}); serial.Update({Controller(3)});
    CHECK(Slot(serial.Update({Controller(3), bluetooth}), 2) == 1);
}

static void WaitingOrder() {
    Slots slots;
    auto pads = slots.Update({Controller(1), Controller(2), Controller(3), Controller(4), Controller(5), Controller(6)});
    CHECK(Slot(pads, 5) == -1 && pads.pads[4].status == Status::NotInUse && Slot(pads, 6) == -1);
    // Even with reversed enumeration, the older waiting connection wins.
    pads = slots.Update({Controller(6), Controller(5), Controller(4), Controller(3), Controller(2), Controller(7)});
    CHECK(Slot(pads, 5) == 0 && Slot(pads, 6) == -1 && Slot(pads, 7) == -1);
    pads = slots.Update({Controller(7), Controller(6), Controller(5), Controller(4), Controller(2)});
    CHECK(Slot(pads, 6) == 2 && Slot(pads, 7) == -1);
}

static void Eligibility() {
    Slots slots;
    auto unmapped = Controller(1); unmapped.mapped = false;
    auto unavailable = Controller(2); unavailable.available = false;
    auto pads = slots.Update({unmapped, unavailable, Controller(3)});
    CHECK(pads.pads[0].status == Status::Unmapped && Slot(pads, 1) == -1);
    CHECK(pads.pads[1].status == Status::NotInUse && Slot(pads, 2) == -1 && Slot(pads, 3) == 0);
    for (const auto& profile : NativeProfiles) {
        Slots native;
        auto stick = Controller(1); stick.vid = profile.vid; stick.pid = profile.pid;
        pads = native.Update({stick});
        CHECK(pads.pads[0].status == Status::GameRead && Slot(pads, 1) == -1);
        std::vector<Pad> beforeOpen{stick};
        Classify(beforeOpen, {});
        CHECK(beforeOpen[0].status == Status::GameRead);
        pads = native.Update({stick}, {}, {stick.identity.guid});
        CHECK(pads.pads[0].status == Status::Slotted && Slot(pads, 1) == 0);
        stick.mapped = false;
        CHECK(native.Update({stick}).pads[0].status == Status::GameRead);
        CHECK(native.Update({stick}, {}, {stick.identity.guid}).pads[0].status == Status::Unmapped);
    }
}

static void SteamPairing() {
    auto steam = Controller(2); steam.vid = 0x28DE; steam.pid = 0x11FF;
    auto physical = Controller(1);
    Slots both, onlyPhysical, onlySteam, unrelated;
    auto pads = both.Update({physical, steam});
    CHECK(Slot(pads, 2) == 0 && Slot(pads, 1) == -1);
    CHECK(pads.pads[0].status == Status::SteamTwin && pads.pads[0].steamPad == 2);
    std::vector<Pad> beforeOpen{physical, steam}; Classify(beforeOpen, {});
    CHECK(beforeOpen[0].status == Status::SteamTwin); // The SDL owner checks this before opening.
    CHECK(Slot(onlyPhysical.Update({physical}), 1) == 0);
    CHECK(Slot(onlySteam.Update({steam}), 2) == 0);
    auto xbox = Controller(3); xbox.type = Type::XboxOne;
    pads = unrelated.Update({steam, xbox});
    CHECK(Slot(pads, 2) == 0 && Slot(pads, 3) == 1);
    // Same brand is insufficient: PS4 and PS5 are different SDL types.
    auto ps5 = Controller(4); ps5.type = Type::PS5;
    Slots differentType;
    CHECK(Slot(differentType.Update({steam, ps5}), 4) == 1);
    auto nativeTwin = physical;
    nativeTwin.vid = NativeProfiles[0].vid; nativeTwin.pid = NativeProfiles[0].pid;
    Slots nativeSteam;
    pads = nativeSteam.Update({nativeTwin, steam});
    CHECK(pads.pads[0].status == Status::GameRead && pads.pads[0].steamPad == steam.id && Slot(pads, 2) == 0);
    auto twin = Controller(5), steam2 = steam; steam2.id = 6; steam2.identity = PadIdentity("steam2");
    Slots pairs;
    pads = pairs.Update({twin, physical, steam2, steam, Controller(7)});
    CHECK(pads.pads[0].steamPad == 6 && pads.pads[1].steamPad == 2);
    CHECK(Slot(pads, 6) == 0 && Slot(pads, 2) == 1 && Slot(pads, 7) == 2);
    pads = pairs.Update({physical, steam2, steam, Controller(7)});
    CHECK(pads.pads[0].steamPad == 6 && pads.pads.back().steamPad == 2);
}

static void PreferredAndDeterminism() {
    Slots slots;
    auto pads = slots.Update({Controller(1), Controller(2, "saved")}, PadIdentity("saved"));
    CHECK(Slot(pads, 2) == 0 && Slot(pads, 1) == 1);
    CHECK(slots.Update({Controller(1), Controller(2, "saved")}, PadIdentity("saved")) == pads);
    slots.Update({Controller(1)}, PadIdentity("saved"));
    CHECK(Slot(slots.Update({Controller(1), Controller(3, "saved")}), 3) == 0);
    Slots late;
    late.Update({Controller(1)}, PadIdentity("saved"));
    pads = late.Update({Controller(1), Controller(2, "saved")}, PadIdentity("saved"));
    CHECK(Slot(pads, 1) == 0 && Slot(pads, 2) == 1);
    CHECK(Slot(late.Update({Controller(2, "saved")}), 2) == 1);
    Slots enumeration;
    CHECK(Slot(enumeration.Update({Controller(9), Controller(1)}), 9) == 0);
    Slots a, b;
    std::vector<Pad> input = {Controller(6), Controller(3), Controller(9), Controller(1), Controller(5)};
    CHECK(a.Update(input, PadIdentity("pad-9")) == b.Update(input, PadIdentity("pad-9")));
    input.erase(input.begin() + 1);
    CHECK(a.Update(input) == b.Update(input));
}

static void AssignmentIdentity() {
    namespace input = sf4e::input;
    input::Device usb;
    usb.type = input::PadXInput; usb.index = 0; usb.name = "Controller 1"; usb.connected = true;
    usb.identity = {"usb-guid", "DS4", "usb-path", "DualShock 4"};
    auto bluetooth = usb;
    bluetooth.index = 2; bluetooth.name = "Controller 3";
    bluetooth.identity = {"bluetooth-guid", "DS4", "bluetooth-path", "Wireless Controller"};
    CHECK(usb.Same(bluetooth));
    input::Assignment assignment; assignment.Adopt(usb);
    assignment.Tick({bluetooth});
    CHECK(assignment.Ready() && assignment.Selected().index == 2);
    auto replacement = bluetooth; replacement.identity.serial = "another-pad";
    assignment.Tick({replacement});
    CHECK(!assignment.Ready());
    replacement.identity = {};
    CHECK(!usb.Same(replacement));
}

static void Restore() {
    namespace input = sf4e::input;
    input::Device keyboard;
    keyboard.type = input::PadKeyboard; keyboard.index = 0; keyboard.name = "Keyboard"; keyboard.connected = true;
    input::Device pad;
    pad.type = input::PadXInput; pad.index = 2; pad.name = "Controller 3"; pad.connected = true;
    pad.identity = PadIdentity("saved");
    auto other = pad; other.index = 1; other.identity = PadIdentity("other");
    auto saved = pad; saved.index = 0;
    auto restored = input::RestoreController({keyboard, other, pad}, other, saved);
    CHECK(restored.device.Same(pad) && restored.device.index == 2 && !restored.provisional);
    restored = input::RestoreController({keyboard, other}, keyboard, saved);
    CHECK(restored.device.Same(other) && restored.provisional);
    restored = input::RestoreController({keyboard}, keyboard, saved);
    CHECK(restored.device.Same(keyboard) && restored.provisional);
    auto third = other; third.index = 3; third.identity = PadIdentity("third");
    restored = input::RestoreController({keyboard, other, third}, other, saved);
    CHECK(!restored.device.connected && restored.provisional);
    // At the main menu a provisional device can yield to the saved pad on a later tick.
    restored = input::RestoreController({keyboard, other, pad}, other, saved);
    CHECK(restored.device.Same(pad) && !restored.provisional);
    restored = input::RestoreController({keyboard, pad}, pad, keyboard);
    CHECK(restored.device.Same(keyboard) && !restored.provisional);
    auto oldKeyboard = keyboard; oldKeyboard.index = 1;
    restored = input::RestoreController({keyboard, pad}, pad, oldKeyboard);
    CHECK(restored.device.Same(keyboard) && !restored.provisional);
    auto legacy = pad; legacy.identity = {};
    restored = input::RestoreController({keyboard, pad}, keyboard, legacy);
    CHECK(restored.device.Same(pad) && !restored.provisional);
    auto wrongSlot = pad; wrongSlot.index = 0;
    restored = input::RestoreController({keyboard, wrongSlot}, wrongSlot, legacy);
    CHECK(restored.device.Same(wrongSlot) && restored.provisional);
    restored = input::RestoreController({keyboard}, keyboard, input::Device{});
    CHECK(restored.device.Same(keyboard) && restored.provisional);
}

static void Mapping() {
    constexpr std::array<std::uint16_t, static_cast<unsigned>(Button::Count)> expected = {
        0x1000, 0x2000, 0x4000, 0x8000, 0x0020, 0, 0x0010, 0x0040, 0x0080,
        0x0100, 0x0200, 0x0001, 0x0002, 0x0004, 0x0008
    };
    CHECK(MapToXInput({}) == XInputFields{});
    for (unsigned i = 0; i < expected.size(); ++i) {
        State state; state.buttons[i] = true;
        CHECK(MapToXInput(state).buttons == expected[i]);
    }
    State state;
    state.buttons.fill(true);
    state.leftX = -32768; state.leftY = -32768;
    state.rightX = 32767; state.rightY = 32767;
    state.leftTrigger = 32767; state.rightTrigger = 16384;
    auto mapped = MapToXInput(state);
    CHECK(mapped.buttons == 0xF3FF && mapped.leftX == -32768 && mapped.rightX == 32767);
    CHECK(mapped.leftY == 32767 && mapped.rightY == -32767);
    CHECK(mapped.leftTrigger == 255 && mapped.rightTrigger == 127);
    state.leftX = INT_MIN; state.leftY = INT_MIN; state.rightX = INT_MAX; state.rightY = INT_MAX;
    state.leftTrigger = -1; state.rightTrigger = INT_MAX;
    mapped = MapToXInput(state);
    CHECK(mapped.leftX == -32768 && mapped.rightX == 32767 && mapped.leftY == 32767 && mapped.rightY == -32767);
    CHECK(mapped.leftTrigger == 0 && mapped.rightTrigger == 255);
    state.leftY = 12345; state.rightY = -12345; state.rightTrigger = 0;
    mapped = MapToXInput(state);
    CHECK(mapped.leftY == -12345 && mapped.rightY == 12345 && mapped.rightTrigger == 0);
}

int main() {
    IdentityMatching(); Reservations(); WaitingOrder(); Eligibility(); SteamPairing(); PreferredAndDeterminism(); AssignmentIdentity(); Restore(); Mapping();
    std::cout << "Gamepad slots and XInput mapping passed\n";
}
