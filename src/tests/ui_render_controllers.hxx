#pragma once
#include "../common/ControllerView.hxx"
namespace {
template<class Draw, class Page>
void ShootControllers(sf4e::ui::ApplicationShell& shell, sf4e::ui::ShellView& view, const Draw& draw, const Page& page) {
    using namespace sf4e; using namespace ui; using namespace gamepads;
    const auto saved = view;
    const auto pad = [](unsigned id, const char* name, RowState state, int slot, PromptBrand brand, const char* picture) {
        PadView p; p.id = id; p.name = name; p.state = state; p.slot = slot; p.brand = brand; p.picture = picture;
        return p;
    };
    view.padLayer = Layer::Active;
    view.pads = {
        pad(1, "DualSense Wireless Controller", RowState::Recognised, 0, {Brand::PlayStation, PlayStationArt::PS5}, "controller_playstation5"),
        pad(2, "Nintendo Switch Pro Controller", RowState::Recognised, 1, {Brand::Nintendo}, "controller_switch_pro"),
        pad(3, "Unmapped arcade stick", RowState::Unmapped, -1, {}, "controller_generic"),
        pad(4, "Waiting Xbox controller", RowState::NotInUse, -1, {Brand::Xbox}, "controller_xboxseries"),
        pad(5, "Saved DualShock 4", RowState::Absent, -1, {Brand::PlayStation}, "controller_disconnected"),
        pad(6, "Steam DualShock 4", RowState::Steam, 2, {Brand::PlayStation}, "controller_playstation4"),
        pad(7, "Xbox Series controller", RowState::Recognised, 3, {Brand::Xbox}, "controller_xboxseries")
    };
    view.pads[0].gameplay = true; view.pads[2].directInputIndex = 4;
    view.pads[2].raw = {{RawKind::Button, 3}, {RawKind::HatUp, 0}};
    OrderPadRows(view.pads);
    view.inputDevice.type = input::PadXInput;
    view.gameplayBrand = view.promptBrand = {Brand::PlayStation, PlayStationArt::PS5};
    view.controller = "DualSense Wireless Controller";
    SetMenuGlyphs(input::PadXInput, input::xinput::A, input::xinput::B, "LP", "LK", view.gameplayBrand);
    const auto focus = [&](const char* id) {
        for (int i = 0; i < 16 && shell.Navigation().Focus() != id; ++i) { draw(nullptr, MenuInput::Up, 1); draw(); }
        for (int i = 0; i < 16 && shell.Navigation().Focus() != id; ++i) { draw(nullptr, MenuInput::Down, 1); draw(); }
        Require(shell.Navigation().Focus() == id, "Controllers render row unreachable");
    };
    page("player"); focus("controllers"); draw("controllers-player");
    page("controllers"); focus("pad-1"); draw("controllers-ps5-up");
    view.pads[0].held = {"A", "LB", "up"}; view.pads[0].pressed = true;
    draw("controllers-ps5-held");
    focus("pad-2"); draw("controllers-switch");
    focus("pad-3"); draw("controllers-unmapped");
    focus("pad-4"); draw("controllers-not-in-use");
    focus("pad-5"); draw("controllers-absent");
    focus("pad-6"); draw("controllers-steam");
    view.pads.clear(); page("controllers"); draw("controllers-none");
    view.padLayer = Layer::GameReads;
    view.pads = {pad(7, "Controller 1", RowState::GameRead, 0, {Brand::Xbox}, "controller_xboxseries")};
    view.pads[0].gameplay = true; page("controllers"); focus("pads-game"); draw("controllers-game-reads");
    view.padLayer = Layer::Failed; page("controllers"); focus("pads-unavailable"); draw("controllers-sdl-failed");
    view = saved; view.pads = {
        pad(1, "DualSense Wireless Controller", RowState::Recognised, 0, {Brand::PlayStation, PlayStationArt::PS5}, "controller_playstation5"),
        pad(2, "Nintendo Switch Pro Controller", RowState::Recognised, 1, {Brand::Nintendo}, "controller_switch_pro")
    };
    view.pads[0].pressed = true;
    view.inputCapture = input::Capture::ReleaseAll; page("player"); draw("controllers-capture-release-all");
    view.inputCapture = input::Capture::Press; draw("controllers-capture-press");
    view.inputCapture = input::Capture::ReleaseSelected; view.captureCandidate = 1; draw("controllers-capture-selected");
    view = saved; page("home");
    SetMenuGlyphs(input::PadXInput, input::xinput::A, input::xinput::B);
}
}
