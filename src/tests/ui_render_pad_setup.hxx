#pragma once
#include "../ui/PadSetupScreen.hxx"
namespace {
template<class Draw, class Page>
void ShootPadSetup(sf4e::ui::ApplicationShell& shell, sf4e::ui::ShellView& view, const Draw& draw, const Page& page) {
    using namespace sf4e; using namespace ui; using namespace gamepads;
    const auto saved = view;
    PadView pad; pad.id = 91; pad.identity = {"03000000790000001100000000000000", "", "render-stick", "Six button arcade stick"};
    pad.name = pad.identity.name; pad.picture = "controller_generic"; pad.state = RowState::Unmapped;
    view.pads = {pad}; view.padSetup = {}; view.padSetup.padId = pad.id; view.padSetup.identity = pad.identity;
    view.padSetup.state.active = true; view.atMainMenu = view.canEditPreferences = true;
    page("pad-setup"); draw("pad-setup-step-1");
    auto& state = view.padSetup.state;
    for (int i = 0; i < 7; ++i) state.recorded[i] = RawInput{RawKind::Button, i};
    state.step = 7; draw("pad-setup-step-8-lk");
    state.feedback = SetupFeedback::Conflict; state.conflictStep = 4; state.conflictInput = state.recorded[4];
    draw("pad-setup-conflict");
    state.holdMs = 2000; draw("pad-setup-hold-ring");
    state.holdMs = 0; state.feedback = SetupFeedback::Idle; draw("pad-setup-idle-hint");
    state.feedback = SetupFeedback::Reconnect; state.reconnectSeconds = 12; draw("pad-setup-reconnect");
    state.feedback = SetupFeedback::None; state.step = 10; draw("pad-setup-optional");
    for (int i = 7; i < 14; ++i) state.recorded[i] = RawInput{RawKind::Button, i};
    state.recorded[10].reset(); state.recorded[11].reset(); state.recorded[13].reset();
    state.skipped[10] = state.skipped[11] = state.skipped[13] = true;
    state.mode = SetupMode::Summary;
    auto rows = PadSetupRows(state); draw(); shell.Navigation().Focus("save", rows); draw("pad-setup-summary");
    state.held = {*state.recorded[7]}; shell.Navigation().Focus("step-7", rows); draw("pad-setup-summary-held");
    state.active = false; view.padSetup.outcome = SetupEnd::LeftMenu; ++view.padSetup.revision; draw();
    view.padSetup = {}; page("home");
    view.pads[0].raw = {{RawKind::Button, 0}}; view.pads[0].pressed = true;
    draw("pad-setup-auto-prompt");
    Require(ImGui::GetTopMostPopupModal() != nullptr, "Automatic pad setup prompt did not open");
    draw(nullptr, MenuInput::Back, 1); draw();
    view = saved; page("home");
}
}
