#pragma once
#include "../common/GamepadSlots.hxx"

namespace sf4e { namespace Gamepads {

// Install only arms startup in DllMain. Start and Shutdown run on Main's game thread.
void Install();
void Start();
void Update();
void Shutdown();
void LogInventory();
std::optional<gamepads::Identity> IdentityForSlot(int slot);
// A copied inventory, safe for the overlay's rendering thread to read.
std::vector<gamepads::Pad> ReadPads();

} }
