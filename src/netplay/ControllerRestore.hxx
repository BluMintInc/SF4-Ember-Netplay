#pragma once
#include "InputAssignment.hxx"

namespace sf4e { namespace input {

struct RestoredController {
    Device device;
    bool provisional;
};

// Identity saves win over legacy slot saves. A missing saved pad never changes the save.
inline RestoredController RestoreController(const std::vector<Device>& devices, const Device& native, const Device& saved,
    const Device& current) {
    Device only, keyboard, provisional;
    unsigned pads = 0;
    for (const auto& device : devices) if (device.connected) {
        const bool matches = saved.identity.guid.empty() ? device.type == saved.type &&
            (saved.type == PadKeyboard || device.index == saved.index) :
            !device.identity.guid.empty() && gamepads::Same(device.identity, saved.identity);
        if (matches) return {device, false};
        if (current.type != PadKeyboard && current.Same(device)) provisional = device;
        if (device.type == PadKeyboard) keyboard = device;
        else { only = device; ++pads; }
    }
    if (provisional.connected) return {provisional, true};
    if (!saved.identity.guid.empty() || saved.type == PadXInput || saved.type == PadDirectInput)
        return {pads == 1 ? only : pads == 0 ? keyboard : Device{}, true};
    return {native, native.type == PadKeyboard && saved.type != PadKeyboard};
}

} }
