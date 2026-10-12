#include "sf4e__Gamepads.hxx"
#include "sf4e__ImportPatch.hxx"
#include "sf4e__DirectInputFilter.hxx"
#include "sf4e__OverlayPrefs.hxx"
#include "../common/XInputMapping.hxx"
#include <windows.h>
#include <Xinput.h>
#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>
#include <map>
#include <mutex>
#include <shared_mutex>

namespace sf4e { namespace Gamepads {
namespace {
using namespace gamepads;
struct Snapshot {
    Id id = 0;
    DWORD packet = 0;
    XInputFields fields;
};
// The provider polls in the game-thread device loop (System_XInput::Update).
// Overlay readers can run on the rendering thread; the same lock also makes
// import callbacks and rumble safe if another native caller uses them there.
std::shared_mutex mutex;
bool requested = false, started = false, active = false, logReady = false;
bool layerSetting = true;
std::string startupNote;
bool startupWarning = false;
std::optional<Identity> preferred;
// P4 supplies GUIDs with saved wizard mappings.
const std::set<std::string> savedEmberMappingGuids;
Slots slots;
Inventory inventory, logged;
bool haveLogged = false;
std::map<Id, SDL_Gamepad*> handles;
struct RawJoystick { SDL_Joystick* handle; std::vector<int> rest; };
std::map<Id, RawJoystick> rawHandles;
std::vector<PadFrame> frames;
std::vector<Identity> rumbleUnsupported;
std::array<Snapshot, 4> snapshots;
std::atomic<void*> originalState{nullptr}, originalCapabilities{nullptr}, originalRumble{nullptr}, originalBattery{nullptr};

std::string Text(const char* value) { return value ? value : ""; }

Type PadType(Id id) {
    switch (SDL_GetGamepadTypeForID(id)) {
    case SDL_GAMEPAD_TYPE_STANDARD: return Type::Standard;
    case SDL_GAMEPAD_TYPE_XBOX360: return Type::Xbox360;
    case SDL_GAMEPAD_TYPE_XBOXONE: return Type::XboxOne;
    case SDL_GAMEPAD_TYPE_PS3: return Type::PS3;
    case SDL_GAMEPAD_TYPE_PS4: return Type::PS4;
    case SDL_GAMEPAD_TYPE_PS5: return Type::PS5;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO: return Type::SwitchPro;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT: return Type::JoyConLeft;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: return Type::JoyConRight;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return Type::JoyConPair;
    case SDL_GAMEPAD_TYPE_GAMECUBE: return Type::GameCube;
    default: return Type::Unknown;
    }
}

const char* TypeName(Type type) {
    switch (type) {
    case Type::Standard: return "standard";
    case Type::Xbox360: return "xbox360";
    case Type::XboxOne: return "xboxone";
    case Type::PS3: return "ps3";
    case Type::PS4: return "ps4";
    case Type::PS5: return "ps5";
    case Type::SwitchPro: return "switchpro";
    case Type::JoyConLeft: return "joyconleft";
    case Type::JoyConRight: return "joyconright";
    case Type::JoyConPair: return "joyconpair";
    case Type::GameCube: return "gamecube";
    default: return "unknown";
    }
}

std::string IdentityTag(const Identity& identity) {
    return fmt::format("{:08x}", IdentityHash(identity));
}

void LogLocked() {
    if (!logReady) return;
    if (!startupNote.empty()) {
        if (startupWarning) spdlog::warn("{}", startupNote);
        else spdlog::info("{}", startupNote);
        startupNote.clear();
    }
    if (!active || (haveLogged && inventory == logged)) return;
    for (const auto& old : logged.pads) if (old.slot >= 0 && std::none_of(inventory.pads.begin(), inventory.pads.end(), [&](const Pad& pad) { return pad.id == old.id; }))
        spdlog::info("Input: pad removed '{}' id {} slot {} reserved", old.identity.name, IdentityTag(old.identity), old.slot);
    std::string line;
    for (const auto& pad : inventory.pads) {
        const auto old = std::find_if(logged.pads.begin(), logged.pads.end(), [&](const Pad& old) { return old.id == pad.id; });
        if (old == logged.pads.end()) {
            if (pad.slot >= 0) {
                const auto brand = DetectBrand(pad).brand;
                spdlog::info("Input: pad added '{}' {:04X}:{:04X} type {} brand {} guid {} id {} slot {}", pad.identity.name,
                    pad.vid, pad.pid, TypeName(pad.type), BrandName(brand), pad.identity.guid, IdentityTag(pad.identity), pad.slot);
            } else if (pad.status == Status::Unmapped)
                spdlog::info("Input: pad added '{}' {:04X}:{:04X} guid {} id {} unmapped, left to DirectInput", pad.identity.name,
                    pad.vid, pad.pid, pad.identity.guid, IdentityTag(pad.identity));
            else if (pad.status == Status::NotInUse)
                spdlog::info("Input: pad added '{}' id {} not in use, four slots taken", pad.identity.name, IdentityTag(pad.identity));
        } else if (old->status == Status::NotInUse && pad.slot >= 0)
            spdlog::info("Input: '{}' id {} takes freed slot {}", pad.identity.name, IdentityTag(pad.identity), pad.slot);
        if (!line.empty()) line += " | ";
        line += pad.status == Status::Slotted ? "slot " + std::to_string(pad.slot) :
            pad.status == Status::Unmapped ? "unmapped" : pad.status == Status::GameRead ? "game-read" :
            pad.status == Status::SteamTwin ? "Steam twin" : "not in use";
        line += fmt::format(" '{}' {} {:04X}:{:04X}", pad.identity.name, TypeName(pad.type), pad.vid, pad.pid);
        if (SteamVirtual(pad) && pad.slot >= 0)
            spdlog::info("Input: Steam virtual pad '{}' as {} slot {}", pad.identity.name, TypeName(pad.type), pad.slot);
        if (pad.steamPad) {
            const auto steam = std::find_if(inventory.pads.begin(), inventory.pads.end(),
                [&](const Pad& candidate) { return candidate.id == pad.steamPad; });
            if (steam != inventory.pads.end())
                spdlog::info("Input: physical pad '{}' id {} paired with Steam pad slot {}; not opened",
                    pad.identity.name, IdentityTag(pad.identity), steam->slot);
        }
    }
    spdlog::info("Input: controller layer {}", line.empty() ? "no pads" : line);
    logged = inventory;
    haveLogged = true;
}

void Rebuild() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    if (!ids) { spdlog::warn("Input: controller inventory failed (SDL: {})", SDL_GetError()); return; }
    std::vector<Pad> pads;
    for (int i = 0; i < count; ++i) {
        Pad pad;
        pad.id = ids[i];
        pad.vid = SDL_GetJoystickVendorForID(pad.id); pad.pid = SDL_GetJoystickProductForID(pad.id);
        pad.type = PadType(pad.id);
        pad.mapped = SDL_IsGamepad(pad.id);
        pad.arcadeStick = SDL_GetJoystickTypeForID(pad.id) == SDL_JOYSTICK_TYPE_ARCADE_STICK;
        char guid[33];
        SDL_GUIDToString(SDL_GetJoystickGUIDForID(pad.id), guid, sizeof(guid));
        pad.identity = {guid, "", Text(SDL_GetJoystickPathForID(pad.id)), Text(SDL_GetJoystickNameForID(pad.id))};
        pads.push_back(std::move(pad));
    }
    SDL_free(ids);
    Classify(pads, savedEmberMappingGuids);
    for (auto it = rawHandles.begin(); it != rawHandles.end();) {
        const bool keep = std::any_of(pads.begin(), pads.end(), [&](const Pad& pad) {
            return pad.id == it->first && pad.status == Status::Unmapped;
        });
        if (keep) { ++it; continue; }
        SDL_CloseJoystick(it->second.handle); it = rawHandles.erase(it);
    }
    for (const auto& pad : pads) if (pad.status == Status::Unmapped && !rawHandles.count(pad.id)) {
        if (auto* joystick = SDL_OpenJoystick(pad.id)) {
            RawJoystick raw{joystick, {}};
            for (int axis = 0; axis < SDL_GetNumJoystickAxes(joystick); ++axis) {
                Sint16 rest = 0;
                if (!SDL_GetJoystickAxisInitialState(joystick, axis, &rest)) rest = SDL_GetJoystickAxis(joystick, axis);
                raw.rest.push_back(rest);
            }
            rawHandles.emplace(pad.id, std::move(raw));
        }
    }
    for (auto it = handles.begin(); it != handles.end();) {
        const bool keep = std::any_of(pads.begin(), pads.end(), [&](const Pad& pad) {
            return pad.id == it->first && pad.status == Status::NotInUse;
        });
        if (keep) { ++it; continue; }
        SDL_CloseGamepad(it->second);
        it = handles.erase(it);
    }
    for (auto& pad : pads) if (pad.status == Status::NotInUse) {
        auto handle = handles.find(pad.id);
        if (handle == handles.end()) {
            if (auto* opened = SDL_OpenGamepad(pad.id)) handle = handles.emplace(pad.id, opened).first;
            else spdlog::warn("Input: controller '{}' not in use (SDL: {})", pad.identity.name, SDL_GetError());
        }
        pad.available = handle != handles.end();
        if (pad.available) pad.identity.serial = Text(SDL_GetGamepadSerial(handle->second));
    }
    inventory = slots.Update(std::move(pads), preferred, savedEmberMappingGuids);
    for (const auto& dropped : slots.DroppedReservations()) {
        const auto pad = std::find_if(inventory.pads.begin(), inventory.pads.end(),
            [&](const Pad& candidate) { return candidate.id == dropped.replacement; });
        spdlog::info("Input: slot {} reserved for id {} given to '{}' id {}", dropped.slot,
            IdentityTag(dropped.owner), pad->identity.name, IdentityTag(pad->identity));
    }
}

State ReadState(SDL_Gamepad* pad) {
    constexpr std::array<SDL_GamepadButton, static_cast<unsigned>(Button::Count)> buttons = {
        SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
        SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_GUIDE, SDL_GAMEPAD_BUTTON_START,
        SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
        SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
        SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT
    };
    State state;
    for (unsigned i = 0; i < buttons.size(); ++i) state.buttons[i] = SDL_GetGamepadButton(pad, buttons[i]);
    state.leftX = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
    state.leftY = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
    state.rightX = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX);
    state.rightY = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY);
    state.leftTrigger = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    state.rightTrigger = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    return state;
}

void UpdateLocked(bool rebuild) {
    SDL_UpdateGamepads();
    SDL_PumpEvents();
    SDL_Event events[32];
    int count;
    while ((count = SDL_PeepEvents(events, 32, SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST)) > 0)
        for (int i = 0; i < count; ++i) switch (events[i].type) {
        case SDL_EVENT_JOYSTICK_ADDED: case SDL_EVENT_JOYSTICK_REMOVED:
        case SDL_EVENT_GAMEPAD_ADDED: case SDL_EVENT_GAMEPAD_REMOVED: case SDL_EVENT_GAMEPAD_REMAPPED:
            rebuild = true;
        }
    if (rebuild) Rebuild();
    std::array<Snapshot, 4> next{};
    frames.clear();
    for (const auto& pad : inventory.pads) {
        PadFrame frame; frame.pad = pad;
        auto handle = handles.find(pad.id);
        if (handle != handles.end() && SDL_GamepadConnected(handle->second)) {
            frame.state = ReadState(handle->second);
            frame.pressed = std::any_of(frame.state.buttons.begin(), frame.state.buttons.end(), [](bool held) { return held; }) ||
                frame.state.leftTrigger > 16383 || frame.state.rightTrigger > 16383;
            if (pad.slot >= 0) {
                next[pad.slot].id = pad.id;
                next[pad.slot].fields = MapToXInput(frame.state);
            }
        }
        const auto raw = rawHandles.find(pad.id);
        if (raw != rawHandles.end()) {
            RawState state; state.rest = raw->second.rest;
            auto* joystick = raw->second.handle;
            for (int i = 0; i < SDL_GetNumJoystickButtons(joystick); ++i) state.buttons.push_back(SDL_GetJoystickButton(joystick, i));
            for (int i = 0; i < SDL_GetNumJoystickHats(joystick); ++i) state.hats.push_back(SDL_GetJoystickHat(joystick, i));
            for (int i = 0; i < SDL_GetNumJoystickAxes(joystick); ++i) state.axes.push_back(SDL_GetJoystickAxis(joystick, i));
            frame.raw = HeldRaw(state);
            frame.pressed = !frame.raw.empty();
        }
        frames.push_back(std::move(frame));
    }
    for (unsigned i = 0; i < next.size(); ++i) {
        next[i].packet = snapshots[i].packet + (next[i].id != snapshots[i].id || next[i].fields != snapshots[i].fields);
        snapshots[i] = next[i];
    }
    LogLocked();
}

DWORD WINAPI GetState(DWORD slot, XINPUT_STATE* out) {
    std::shared_lock lock(mutex);
    if (!active) {
        lock.unlock();
        return reinterpret_cast<decltype(&XInputGetState)>(originalState.load())(slot, out);
    }
    if (!out) return ERROR_BAD_ARGUMENTS;
    if (slot >= snapshots.size() || !snapshots[slot].id) return ERROR_DEVICE_NOT_CONNECTED;
    const auto& state = snapshots[slot];
    const auto& f = state.fields;
    *out = {state.packet, {f.buttons, f.leftTrigger, f.rightTrigger, f.leftX, f.leftY, f.rightX, f.rightY}};
    return ERROR_SUCCESS;
}

DWORD WINAPI GetCapabilities(DWORD slot, DWORD flags, XINPUT_CAPABILITIES* out) {
    std::shared_lock lock(mutex);
    if (!active) {
        lock.unlock();
        return reinterpret_cast<decltype(&XInputGetCapabilities)>(originalCapabilities.load())(slot, flags, out);
    }
    if (!out) return ERROR_BAD_ARGUMENTS;
    if (slot >= snapshots.size() || !snapshots[slot].id) return ERROR_DEVICE_NOT_CONNECTED;
    *out = {};
    out->Type = XINPUT_DEVTYPE_GAMEPAD; out->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
    out->Gamepad = {0xF3FF, 255, 255, 32767, 32767, 32767, 32767};
    out->Vibration = {65535, 65535};
    return ERROR_SUCCESS;
}

DWORD WINAPI SetState(DWORD slot, XINPUT_VIBRATION* vibration) {
    std::shared_lock lock(mutex);
    if (!active) {
        lock.unlock();
        return reinterpret_cast<decltype(&XInputSetState)>(originalRumble.load())(slot, vibration);
    }
    if (!vibration) return ERROR_BAD_ARGUMENTS;
    if (slot >= snapshots.size() || !snapshots[slot].id) return ERROR_DEVICE_NOT_CONNECTED;
    const auto handle = handles.find(snapshots[slot].id);
    if (handle == handles.end()) return ERROR_DEVICE_NOT_CONNECTED;
    if (SDL_RumbleGamepad(handle->second, vibration->wLeftMotorSpeed, vibration->wRightMotorSpeed, 200)) return ERROR_SUCCESS;
    if (SDL_GetBooleanProperty(SDL_GetGamepadProperties(handle->second), SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false)) return ERROR_NOT_SUPPORTED;
    const auto pad = std::find_if(inventory.pads.begin(), inventory.pads.end(), [&](const Pad& pad) { return pad.id == snapshots[slot].id; });
    const Identity unsupported = pad->identity;
    lock.unlock();
    std::unique_lock writeLock(mutex);
    if (std::none_of(rumbleUnsupported.begin(), rumbleUnsupported.end(), [&](const Identity& identity) { return Same(identity, unsupported); })) {
        rumbleUnsupported.push_back(unsupported);
        spdlog::info("Input: rumble unsupported on '{}'", unsupported.name);
    }
    return ERROR_NOT_SUPPORTED;
}

DWORD WINAPI GetBattery(DWORD slot, BYTE deviceType, XINPUT_BATTERY_INFORMATION* out) {
    std::shared_lock lock(mutex);
    if (!active) {
        lock.unlock();
        return reinterpret_cast<decltype(&XInputGetBatteryInformation)>(originalBattery.load())(slot, deviceType, out);
    }
    if (!out) return ERROR_BAD_ARGUMENTS;
    if (slot >= snapshots.size() || !snapshots[slot].id || deviceType != BATTERY_DEVTYPE_GAMEPAD)
        return ERROR_DEVICE_NOT_CONNECTED;
    *out = {BATTERY_TYPE_WIRED, BATTERY_LEVEL_FULL};
    return ERROR_SUCCESS;
}

void CloseLocked() {
    for (const auto& [id, handle] : handles) SDL_CloseGamepad(handle);
    handles.clear();
    for (const auto& [id, raw] : rawHandles) SDL_CloseJoystick(raw.handle);
    rawHandles.clear(); frames.clear();
    inventory = {}; snapshots = {};
    SDL_Quit();
    active = false;
}
}

void Install() {
    // DirectInput's window-aware device init at 0x718CE0 can precede Main::Initialize.
    // Arm every import here; the providers preserve native input until Start succeeds.
    requested = imports::PatchImports(GetModuleHandleW(nullptr), {
        {"XINPUT9_1_0.dll", "XInputGetState", reinterpret_cast<void*>(&GetState), originalState},
        {"XINPUT9_1_0.dll", "XInputGetCapabilities", reinterpret_cast<void*>(&GetCapabilities), originalCapabilities},
        {"XINPUT9_1_0.dll", "XInputSetState", reinterpret_cast<void*>(&SetState), originalRumble},
        {"XINPUT1_3.dll", WORD(7), reinterpret_cast<void*>(&GetBattery), originalBattery},
        {"DINPUT8.dll", "DirectInput8Create", reinterpret_cast<void*>(&DirectInputFilter::Create), DirectInputFilter::originalCreate}});
}

void Start() {
    std::unique_lock lock(mutex);
    if (started) return;
    started = true;
    if (!requested) { startupNote = "Input: controller layer off (game imports could not be patched)"; return; }
    OverlayPrefs::Data prefs;
    OverlayPrefs::Load(prefs);
    layerSetting = prefs.controllerLayer;
    if (!layerSetting) { startupNote = "Input: the game reads controllers itself (setting)"; return; }
    preferred = prefs.deviceIdentity;
    // Only GAMEPAD is initialized, so SDL owns no video or game window. Keep
    // SDL's default backend deduplication; disabling it can expose Xbox pads twice.
    for (const char* hint : {SDL_HINT_JOYSTICK_THREAD, SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,
        SDL_HINT_JOYSTICK_HIDAPI, SDL_HINT_JOYSTICK_HIDAPI_PS4, SDL_HINT_JOYSTICK_HIDAPI_PS5,
        SDL_HINT_JOYSTICK_HIDAPI_SWITCH, SDL_HINT_JOYSTICK_HIDAPI_STEAM, SDL_HINT_JOYSTICK_HIDAPI_XBOX,
        SDL_HINT_JOYSTICK_HIDAPI_JOY_CONS, SDL_HINT_JOYSTICK_HIDAPI_COMBINE_JOY_CONS}) SDL_SetHint(hint, "1");
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        startupWarning = true;
        startupNote = "Input: SDL failed to start (" + Text(SDL_GetError()) + "); the game reads controllers itself";
        SDL_Quit();
        return;
    }
    UpdateLocked(true);
    active = true;
    const int version = SDL_GetVersion();
    startupNote = fmt::format("Input: SDL {}.{}.{} started, hidapi {}, background events on, {} saved mappings loaded",
        SDL_VERSIONNUM_MAJOR(version), SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version),
        SDL_GetHintBoolean(SDL_HINT_JOYSTICK_HIDAPI, true) ? "on" : "off", savedEmberMappingGuids.size());
}

void Update() {
    std::unique_lock lock(mutex);
    if (active) UpdateLocked(false);
}

void Shutdown() {
    std::unique_lock lock(mutex);
    if (active) CloseLocked();
}

void LogInventory() {
    std::unique_lock lock(mutex);
    logReady = true;
    LogLocked();
}

std::optional<Identity> IdentityForSlot(int slot) {
    std::shared_lock lock(mutex);
    if (!active || slot < 0 || slot >= static_cast<int>(snapshots.size())) return {};
    for (const auto& pad : inventory.pads) if (pad.slot == slot) return pad.identity;
    return {};
}

PromptBrand BrandForDevice(int deviceType, int slot) {
    std::shared_lock lock(mutex);
    if (active && deviceType == input::PadXInput && slot >= 0 && slot < static_cast<int>(snapshots.size()))
        for (const auto& pad : inventory.pads) if (pad.slot == slot) return GameplayBrand(deviceType, &pad);
    return GameplayBrand(deviceType);
}

std::vector<Pad> ReadPads() {
    std::shared_lock lock(mutex);
    return inventory.pads;
}

LayerFrame ReadFrame() {
    std::shared_lock lock(mutex);
    return {active ? Layer::Active : layerSetting ? Layer::Failed : Layer::GameReads, layerSetting, frames};
}

void SetupPrompt(const Identity& identity) {
    spdlog::info("Input: setup prompt for '{}' guid {}", identity.name, identity.guid);
}

} }
