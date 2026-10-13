#pragma once
#include "../common/ControllerView.hxx"

namespace sf4e { namespace Gamepads {

// Install patches imports in DllMain. Providers pass through until Start succeeds.
// Start and Shutdown run on Main's game thread.
void Install();
void Start();
void Update();
void Shutdown();
void LogInventory();
std::optional<gamepads::Identity> IdentityForSlot(int slot);
gamepads::PromptBrand BrandForDevice(int deviceType, int slot);
// A copied inventory, safe for the overlay's rendering thread to read.
std::vector<gamepads::Pad> ReadPads();
struct LayerFrame {
    gamepads::Layer mode = gamepads::Layer::Failed;
    bool requested = true;
    std::vector<gamepads::PadFrame> pads;
    gamepads::SetupSnapshot setup;
};
LayerFrame ReadFrame();
void SetupPrompt(const gamepads::Identity& identity);
void BeginSetup(const gamepads::Identity& identity, unsigned padId);
void ControlSetup(gamepads::SetupCommand command, int step);
std::optional<std::string> SaveSetup();

} }
