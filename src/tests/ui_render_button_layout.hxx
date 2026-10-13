#pragma once
#include "../ui/ButtonLayoutScreen.hxx"
namespace {
template<class Draw, class Page>
void ShootButtonLayout(sf4e::ui::ApplicationShell& shell, sf4e::ui::ShellView& view, const Draw& draw, const Page& page) {
    using namespace sf4e; using namespace ui; using namespace gamepads;
    const auto saved = view;
    PadView pad; pad.id = 81; pad.identity = {"render-layout", "render-layout", "render-layout", "DualSense Wireless Controller"};
    pad.name = pad.identity.name; pad.picture = "controller_playstation5"; pad.slot = 0; pad.gameplay = true;
    pad.brand = {Brand::PlayStation, PlayStationArt::PS5}; pad.held = {"LB", "A"};
    view.session = {}; view.room = {}; draw();
    view.pads = {pad}; view.padLayer = Layer::Active; view.atMainMenu = view.canEditPreferences = true;
    view.inputDevice.type = input::PadXInput; view.gameplayBrand = view.promptBrand = pad.brand;
    view.preferences.buttonLayouts.clear();
    const auto open = [&] { page("player"); draw(); shell.Navigation().Focus("controls", PlayerControllerRows(view.preferences, pad.name, input::PadXInput, pad.brand, view.pads, false, true, true, true)); draw(nullptr, MenuInput::Select, 1); draw(); };
    const auto focus = [&](const char* id) { draw(); const auto& target = view.pads[0]; shell.Navigation().Focus(id, ButtonLayoutRows(target, true)); draw(); };
    open();
    view.pads[0].brand = view.gameplayBrand = {Brand::Xbox}; view.pads[0].picture = "controller_xboxseries"; view.pads[0].name = "Xbox Series controller";
    draw("button-layout-xbox"); view.pads[0] = pad; view.gameplayBrand = pad.brand; draw("button-layout-ps5");
    view.preferences.buttonLayouts = {{pad.identity, LayoutPresets[2].buttons, 1}};
    draw("button-layout-ps5-stick-clicks"); view.preferences.buttonLayouts.clear(); draw();
    view.pads[0].brand = view.gameplayBrand = {Brand::Nintendo}; view.pads[0].name = "Nintendo Switch Pro Controller"; view.pads[0].picture = "controller_switch_pro";
    draw("button-layout-switch");
    view.pads[0].brand = view.gameplayBrand = {Brand::Stick}; view.pads[0].name = "Eight button arcade stick"; view.pads[0].picture = "generic_joystick_red";
    view.pads[0].has[8] = view.pads[0].has[9] = false;
    view.preferences.controllerMappings[pad.identity.guid] = pad.identity.guid + ",Stick,a:b7,b:b8,x:b4,y:b5,leftshoulder:b10,rightshoulder:b6,lefttrigger:a0,righttrigger:b9,";
    draw("button-layout-stick");
    view.pads[0].brand = view.gameplayBrand = pad.brand; view.pads[0].picture = pad.picture; view.pads[0].name = pad.name; view.pads[0].has = AllLayoutButtons;
    view.pads[0].held.clear(); focus("fn-lp"); draw(nullptr, MenuInput::Select, 1); draw("button-layout-capture");
    view.pads[0].held = {"Start"}; draw(); view.pads[0].held.clear(); draw();
    focus("edit-button"); draw(nullptr, MenuInput::Select, 1); draw(); view.pads[0].held = {"LB"}; draw(); view.pads[0].held.clear(); draw();
    for (const auto* bit : {"bit-mp", "bit-hp", "bit-lk"}) {
        shell.Navigation().Focus(bit, ButtonEditorRows(view.pads[0], 4, true));
        draw(nullptr, MenuInput::Right, 1); draw();
    }
    draw("button-layout-button"); draw(nullptr, MenuInput::Back, 1); draw();
    focus("edit-button"); draw(nullptr, MenuInput::Select, 1); draw(); view.pads[0].held = {"X"}; draw(); view.pads[0].held.clear(); draw();
    draw(nullptr, MenuInput::Right, 1); draw(); draw(nullptr, MenuInput::Back, 1); draw(); draw(nullptr, MenuInput::Back, 1); draw("button-layout-warning");
    draw(nullptr, MenuInput::Back, 1); draw();
    auto custom = DefaultLayout; custom[4] = 0x50; view.pads[0].layout = custom;
    page("controllers"); shell.Navigation().Focus("pad-81", ControllerRows(view.padLayer, view.pads, true, true));
    draw("button-layout-controllers-custom"); view.pads[0].layout.reset(); page("button-layout"); draw();
    view.canEditPreferences = false; view.atMainMenu = false; draw("button-layout-read-only");
    view.atMainMenu = true; view.session.room = netplay::RoomState::Joined; draw(); open();
    Require(shell.Navigation().Screen() == "button-layout", "Room render did not open Button layout"); draw("button-layout-room");
    view.pads.clear(); draw("button-layout-absent");
    view.pads = {pad}; view.pads[0].state = RowState::GameRead; view.canEditPreferences = true; view.session.room = netplay::RoomState::Idle; draw(); open();
    Require(shell.Navigation().Screen() == "button-layout", "Setup-first render did not open Button layout"); draw("button-layout-setup-first");
    view = saved; page("home");
}
}
