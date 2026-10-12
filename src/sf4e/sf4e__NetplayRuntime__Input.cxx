#include "sf4e__NetplayRuntime.hxx"
#include "../netplay/ControllerRestore.hxx"

namespace sf4e { namespace NetplayFacade {
namespace internal {
// A device as the player reads it. Ember's own names for the keyboard and for a
// pad the driver did not name are localized; a name the driver reported is not
// ours to translate. Device::name itself stays as it is: connection checks
// compare it.
std::string DeviceName(const input::Device& device) {
    if (device.type == input::PadKeyboard) return loc::T("controller.keyboard");
    if (!device.identity.guid.empty()) return device.identity.name;
    if (device.name == "Controller " + std::to_string(device.index + 1)) return loc::Tf("controller.numbered", device.index + 1);
    return device.name;
}
std::string ControllerLabel(const input::Device& device) {
    if (device.name.empty()) return loc::T("controller.none");
    return device.connected ? DeviceName(device) : loc::Tf("controller.disconnected_name", DeviceName(device));
}

void LogGameplayController(const input::Device& device, const char* reason) {
    spdlog::info("Input: gameplay controller '{}' id {:08x} slot {} by {}", DeviceName(device),
        gamepads::IdentityHash(device.identity), device.index, reason);
}

namespace {
void RestoreMenuController() {
    OverlayPrefs::Data prefs;
    OverlayPrefs::Load(prefs);
    input::Device saved;
    saved.type = prefs.deviceType; saved.index = prefs.deviceIdx;
    if (prefs.deviceIdentity) saved.identity = *prefs.deviceIdentity;
    const auto restored = input::RestoreController(runtime->inputDevices, input::MenuDevice(runtime->inputDevices), saved);
    const auto& device = restored.device;
    runtime->inputInitialized = !restored.provisional && device.connected;
    if (!device.connected) {
        if (!runtime->inputDevices.empty()) runtime->input.Begin();
        return;
    }
    const bool migration = !prefs.deviceIdentity && prefs.deviceType == input::PadXInput &&
        !device.identity.guid.empty() && !restored.provisional;
    if (!restored.provisional && !device.identity.guid.empty())
        OverlayPrefs::SaveGameplayDevice(device.type, device.index, device.identity);
    const auto& previous = runtime->input.Selected();
    if (previous.connected && previous.Same(device)) return;
    if (prefs.deviceIdentity) {
        if (restored.provisional) {
            runtime->padHint = device.type == input::PadKeyboard ? std::string{} :
                loc::Tf("controller.using_for_now", prefs.deviceIdentity->name, DeviceName(device));
            spdlog::info("Input: saved controller id {:08x} not connected; using '{}' for now",
                gamepads::IdentityHash(*prefs.deviceIdentity), DeviceName(device));
        } else {
            runtime->padHint = previous.type < 0 ? std::string{} : loc::Tf("controller.connected_adopted", DeviceName(device));
            spdlog::info("Input: saved controller '{}' id {:08x} restored to slot {}",
                DeviceName(device), gamepads::IdentityHash(device.identity), device.index);
        }
        runtime->padHintUntil = GetTickCount64() + 4000;
    }
    runtime->input.Adopt(device);
    LogGameplayController(device, migration ? "migration" : restored.provisional && device.type != input::PadKeyboard ? "only pad" : "restore");
    if (device.type != input::PadKeyboard && !input::AssignToSide(device, 0, false)) runtime->input.Begin();
}
}

void CaptureMenuInput() {
    if (GetTickCount64() >= runtime->padHintUntil) runtime->padHint.clear();
    if (AtMainMenu()) {
        runtime->inputDevices = input::ReadDevices();
        input::LogInventory(runtime->inputDevices);
        if (!runtime->inputInitialized && runtime->input.State()==input::Capture::Idle &&
            runtime->controller.GetSnapshot().room==netplay::RoomState::Idle) RestoreMenuController();
        const int previousIndex = runtime->input.Selected().index;
        const bool captured = runtime->input.Tick(runtime->inputDevices);
        const auto& chosen = runtime->input.Selected();
        const bool moved = runtime->input.Ready() && chosen.index != previousIndex &&
            runtime->controller.GetSnapshot().room == netplay::RoomState::Idle;
        if (captured || moved) {
            if (captured) runtime->padHint.clear();
            if (captured) runtime->inputInitialized=true;
            if (runtime->inputInitialized) {
                const auto identity = chosen.identity.guid.empty() ? std::nullopt : std::make_optional(chosen.identity);
                OverlayPrefs::SaveGameplayDevice(chosen.type, chosen.index, identity);
            }
            LogGameplayController(chosen, captured ? "capture" : "restore");
            if(!input::AssignToSide(runtime->input.Selected(),0,false))runtime->input.Begin();
        }
        if (UserApp::netplay && runtime->input.Ready()) {
            UserApp::netplay->deviceIdx = static_cast<uint8_t>(runtime->input.Selected().index);
            UserApp::netplay->deviceType = static_cast<uint8_t>(runtime->input.Selected().type);
        }
    } else runtime->input.Cancel();
}
} // namespace internal

bool BindRuntimeInput(int localSlot) {
    if (!runtime || !UserApp::netplay) return false;
    // Refresh connectivity just before native slot binding; never fall back to keyboard.
    runtime->input.Tick(input::ReadDevices());
    if (!runtime->input.Ready()) { runtime->error = loc::T("runtime.gameplay_device_disconnected"); return false; }
    const auto& device = runtime->input.Selected();
    UserApp::netplay->deviceIdx = static_cast<uint8_t>(device.index);
    UserApp::netplay->deviceType = static_cast<uint8_t>(device.type);
    if(!input::AssignToSide(device, localSlot, true)) {
        runtime->error=loc::T("runtime.controller_assignment_unverified");return false;
    }
    runtime->matchInput=device;runtime->matchInputSide=localSlot;runtime->matchInputFault=false;
    spdlog::info("Input: P{} plays with {} {} '{}'", localSlot + 1,
        device.type == input::PadKeyboard ? "keyboard" : device.type == input::PadXInput ? "xinput" : "dinput",
        device.index, device.identity.guid.empty() ? device.name : device.identity.name);
    return true;
}

bool ReadRuntimeMatchInput(int side,unsigned& mapped,unsigned& raw) {
    mapped=raw=0;
    if(!runtime||!runtime->attached)return false;
    const bool valid=side==runtime->matchInputSide&&input::ReadAssignedInput(runtime->matchInput,side,mapped,raw);
    runtime->matchInputFault=!valid;
    return valid;
}

} } // namespace sf4e::NetplayFacade
