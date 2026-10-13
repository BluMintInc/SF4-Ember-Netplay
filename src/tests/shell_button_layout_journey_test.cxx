#include "shell_journey_support.hxx"
#include "../ui/ButtonLayoutScreen.hxx"

namespace {
using namespace sf4e;
using namespace gamepads;
PadView LayoutPad() {
    PadView pad; pad.id = 27; pad.slot = 0; pad.gameplay = true;
    pad.identity = {"layout-guid", "layout-serial", "layout-path", "DualSense Wireless Controller"};
    pad.name = pad.identity.name; pad.picture = "controller_playstation5"; pad.brand = {Brand::PlayStation, PlayStationArt::PS5};
    return pad;
}
void Prepare(Harness& h) {
    h.view.padLayer = Layer::Active; h.view.inputDevice.type = input::PadXInput;
    h.view.pads = {LayoutPad()}; h.view.controller = h.view.pads[0].name;
    h.view.gameplayBrand = h.view.promptBrand = h.view.pads[0].brand;
    h.Screen("player"); h.Choose("controls");
    Check(h.shell.Navigation().Screen() == "button-layout" && h.shell.Navigation().Focus() == "test", "Player did not open Button layout on test");
}
const ControllerRequest& LastLayout(const Harness& h) {
    const auto at = std::find_if(h.actions.rbegin(), h.actions.rend(), [](const ShellAction& action) { return action.padAction.kind == ControllerRequest::Kind::Layout; });
    Check(at != h.actions.rend(), "No layout submitted"); return at->padAction;
}
void LayoutAssignPresetReset() {
    Harness h; Prepare(h);
    h.view.pads[0].held = {"A"}; h.Choose("fn-lp");
    h.Frame(0, 3); Check(std::none_of(h.actions.begin(), h.actions.end(), [](const ShellAction& a) { return a.padAction.kind == ControllerRequest::Kind::Layout; }), "Opening A assigned itself");
    h.view.pads[0].held.clear(); h.Frame(); h.view.pads[0].held = {"Y"}; h.Frame();
    const auto assigned = LastLayout(h);
    Check(assigned.id == 27 && assigned.buttons[2] == 0x20 && assigned.buttons[3] == 0x10, "Capture did not swap LP onto Triangle");
    Check(assigned.log.find("fn-lp") != std::string::npos, "Assign log omitted its row");
    h.view.pads[0].held.clear(); h.Frame(); h.Wait(.5f);
    Check(h.actions.back().command.kind == Kind::SavePreferences && h.actions.back().preferences.buttonLayouts[0].buttons == assigned.buttons,
        "Layout did not reach autosave after applying");
    h.view.preferences = h.actions.back().preferences; h.view.settingsPending = true; h.Frame(); h.view.settingsPending = false; h.Frame();
    h.Choose("preset"); h.Press(MenuInput::Down); h.Press(MenuInput::Select);
    Check(LastLayout(h).buttons == LayoutPresets[1].buttons, "Triggers preset did not apply");
    h.Choose("preset"); h.Press(MenuInput::Up); h.Press(MenuInput::Select);
    Check(LastLayout(h).buttons == DefaultLayout, "Default preset did not reset");
    h.Choose("remove"); h.Press(MenuInput::Right); h.Press(MenuInput::Select);
    Check(h.actions.back().padAction.kind == ControllerRequest::Kind::RemoveLayout && h.shell.Navigation().Focus() == "preset", "Use game's layout did not remove and focus preset");
    h.Press(MenuInput::Back); Check(h.shell.Navigation().Screen() == "player", "Remove left an unwanted missing-attack warning");
    h.Choose("game-buttons"); Check(h.actions.back().command.kind == Kind::StartOffline, "Game's button settings did not send StartOffline");
}
void LayoutEditorWarningRoom() {
    Harness h; Prepare(h);
    h.Choose("edit-button"); h.view.pads[0].held = {"X"}; h.Frame();
    Check(h.shell.Navigation().Screen() == "button-layout-button" && h.shell.Navigation().Focus() == "bit-lp", "Pick capture did not open the button editor");
    h.view.pads[0].held.clear(); h.Frame(); h.Choose("clear");
    Check(LastLayout(h).buttons[2] == 0, "Clear did not empty X");
    h.Press(MenuInput::Back); Check(h.shell.Navigation().Screen() == "button-layout" && !ImGui::GetTopMostPopupModal(), "Returning from editor opened the warning");
    h.Press(MenuInput::Back); Check(ImGui::GetTopMostPopupModal(), "Missing LP did not open leaving warning");
    h.Press(MenuInput::Select); Check(h.shell.Navigation().Screen() == "button-layout", "Keep editing was not the default");
    h.Press(MenuInput::Back); h.Press(MenuInput::Right); h.Press(MenuInput::Select);
    Check(h.shell.Navigation().Screen() == "player", "Leave did not return to Player");
    Check(h.actions.back().padAction.log.find("left with no button for LP") != std::string::npos, "Leave warning log missing");
    h.Choose("controls"); h.Press(MenuInput::Back);
    Check(h.shell.Navigation().Screen() == "player", "Unchanged visit warned again");
}
void LayoutRoom() {
    Harness room;
    Prepare(room); room.Press(MenuInput::Back);
    room.view.canEditPreferences = false; JoinedRoom(room, 42); room.Frame(); room.Screen("player"); room.Choose("controls");
    room.Choose("fn-lp"); room.view.pads[0].held = {"RB"}; room.Frame(); room.view.pads[0].held.clear(); room.Frame();
    Check(LastLayout(room).buttons[5] == 0x10, "Room layout edit was locked");
    room.Wait(.5f);
    Check(room.actions.back().padAction.kind == ControllerRequest::Kind::SaveLayouts, "Room edit did not autosave through the layout-only request");
}
void LayoutCancelsAndEdges() {
    for (const auto* cancel : {"Start", "up"}) {
        Harness h; Prepare(h); h.Choose("fn-lp"); h.view.pads[0].held = {cancel}; h.Frame();
        Check(h.actions.back().padAction.log.find("cancelled") != std::string::npos, "Target cancel did not stop capture");
        Check(std::none_of(h.actions.begin(), h.actions.end(), [](const ShellAction& a) { return a.padAction.kind == ControllerRequest::Kind::Layout; }), "Cancel changed layout");
    }
    {
        Harness escape; Prepare(escape); escape.Choose("fn-lp");
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true); escape.Frame();
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false); escape.Frame();
        Check(escape.actions.back().padAction.log.find("cancelled (escape)") != std::string::npos, "Escape did not cancel layout capture");
    }
    Harness h; Prepare(h); h.Choose("fn-lp"); h.Wait(10.1f);
    Check(h.actions.back().padAction.log.find("timeout") != std::string::npos, "Capture did not time out");
    h.Choose("fn-lp"); h.view.pads.clear(); h.Frame();
    Check(h.shell.Navigation().Focus() == "layout-absent", "Disconnect did not focus Not connected");
    Check(h.actions.back().padAction.log.find("assign fn-lp cancelled (state)") != std::string::npos, "Disconnect capture lost the original function row");
    h.view.pads = {LayoutPad()}; h.Frame(); Check(h.shell.Navigation().Focus() == "test", "Reconnect did not focus test");
    h.view.canEditPreferences = false; h.Frame();
    const auto readonly = ButtonLayoutRows(h.view.pads[0], false);
    Check(readonly[0].id == "test" && std::all_of(readonly.begin() + 1, readonly.end(), [](const MenuEntry& row) { return !row.enabled; }), "Read-only rows were editable");
    const auto before = h.actions.size(); h.Choose("fn-lp"); h.view.pads[0].held = {"Y"}; h.Frame();
    Check(h.actions.size() == before, "Read-only screen opened capture");
}
void WizardLayoutEntry() {
    Harness h;
    auto pad = LayoutPad(); pad.state = RowState::Unmapped; pad.gameplay = false;
    h.view.pads = {pad}; h.view.padLayer = Layer::Active;
    h.Screen("controllers"); h.Choose("pad-27");
    h.view.padSetup.identity = pad.identity; h.view.padSetup.padId = pad.id;
    auto& state = h.view.padSetup.state; state.active = true; state.mode = SetupMode::Summary;
    for (int i = 0; i < 14; ++i) state.recorded[i] = RawInput{RawKind::Button, i};
    h.Frame(); h.Choose("save");
    h.view.preferences.controllerMappings[pad.identity.guid] = BuildSetupMapping(pad.identity.guid, pad.name, state);
    h.view.pads[0].state = RowState::Recognised; h.view.pads[0].brand = {Brand::Stick}; h.view.pads[0].slot = 0;
    h.view.pads[0].has[8] = h.view.pads[0].has[9] = false;
    state.active = false; h.view.padSetup.outcome = SetupEnd::Saved; ++h.view.padSetup.revision; h.Frame();
    h.Press(MenuInput::Right); h.Press(MenuInput::Select);
    Check(h.shell.Navigation().Screen() == "button-layout" && h.shell.Navigation().Focus() == "test", "Wizard alternative did not open Button layout");
    h.Press(MenuInput::Back); Check(h.shell.Navigation().Screen() == "controllers", "Wizard layout did not return to Controllers");
    h.view.pads[0].state = RowState::GameRead; h.view.pads[0].gameplay = true; h.view.inputDevice.type = input::PadXInput;
    h.Screen("player"); h.Choose("controls"); h.Choose("setup");
    state = {}; state.active = true; state.mode = SetupMode::Summary;
    for (int i = 0; i < 14; ++i) state.recorded[i] = RawInput{RawKind::Button, i};
    h.Frame(); h.Choose("save"); h.view.pads[0].state = RowState::Recognised;
    state.active = false; h.view.padSetup.outcome = SetupEnd::Saved; ++h.view.padSetup.revision; h.Frame();
    Check(h.shell.Navigation().Screen() == "button-layout" && h.shell.Navigation().Focus() == "test", "Setup-first save did not return to Button layout on test");
    h.Press(MenuInput::Back); Check(h.shell.Navigation().Screen() == "button-layout", "Setup-first saved notice changed screens");
}
void TargetNavigation() {
    Harness h; Prepare(h); h.view.pads[0].gameplay = false; h.Frame();
    h.view.pads[0].held = {"down"}; h.Frame(); h.view.pads[0].held.clear(); h.Frame();
    Check(h.shell.Navigation().Focus() == "fn-lp", "Target pad did not navigate its own layout");
    h.view.pads[0].held = {"A"}; h.Frame(); h.view.pads[0].held.clear(); h.Frame();
    h.view.pads[0].held = {"B"}; h.Frame();
    Check(LastLayout(h).buttons[1] == 0x10 && h.shell.Navigation().Screen() == "button-layout", "Target B was not bindable or also went back");
    h.view.pads[0].held.clear(); h.Frame();
}

void GuidFallbackEdit() {
    Harness h; auto pad = LayoutPad(); pad.identity.serial.clear(); pad.identity.path = "new-port";
    auto saved = pad.identity; saved.path = "old-port";
    h.view.padLayer = Layer::Active; h.view.inputDevice.type = input::PadXInput; h.view.pads = {pad};
    h.view.preferences.buttonLayouts = {{saved, DefaultLayout, 1}};
    h.Screen("player"); h.Choose("controls"); h.Choose("fn-lp");
    h.view.pads[0].held = {"Y"}; h.Frame(); h.view.pads[0].held.clear(); h.Frame(); h.Wait(.5f);
    Check(h.actions.back().preferences.buttonLayouts.size() == 1 && h.actions.back().preferences.buttonLayouts[0].identity.path == "new-port",
        "Guid fallback edit duplicated the layout or saved the old port");
    h.view.preferences = h.actions.back().preferences; h.Frame(); h.Choose("remove"); h.Press(MenuInput::Right); h.Press(MenuInput::Select); h.Wait(.5f);
    Check(h.actions.back().preferences.buttonLayouts.empty(), "Guid fallback layout could not be removed");
}

}
void ButtonLayoutJourneys() { LayoutAssignPresetReset(); LayoutEditorWarningRoom(); LayoutRoom(); LayoutCancelsAndEdges(); WizardLayoutEntry(); TargetNavigation(); GuidFallbackEdit(); }
