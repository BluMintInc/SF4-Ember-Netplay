// The game's pads have two owners: Ember's menu (MenuPads) and the training
// controls. The menu's keyboard and mouse capture is the window's alone and
// never holds the pads: once the drawing thread has stopped with a focused
// menu shown, a match being prepared gives the pads back to the game at once,
// and behind another window the menu still holds them at the main menu.
#include "../sf4e/sf4e__Overlay.hxx"

#include "test_support.hxx"

int main() {
    namespace Overlay = sf4e::Overlay;
    using sf4e::input::MenuContext;
    auto& pads = Overlay::MenuPads();
    // In front at the main menu: Start asks, and the request holds the pads
    // until the frame that takes it has shown the menu.
    pads.Place(MenuContext::MainMenu, false);
    CHECK(Overlay::HasInputFocus() && !Overlay::CapturesMenuInput());
    Overlay::RequestMainControls();
    CHECK(Overlay::CapturesMenuInput());
    pads.Publish(true, pads.Take());
    CHECK(Overlay::CapturesMenuInput());
    // Drawing stops there. A match is prepared, played and over: the pads are
    // the game's throughout, whatever the last frame said.
    pads.Place(MenuContext::MainMenu, true); CHECK(!Overlay::CapturesMenuInput());
    pads.Place(MenuContext::Unavailable, true); CHECK(!Overlay::CapturesMenuInput());
    pads.Place(MenuContext::Unavailable, false); CHECK(!Overlay::CapturesMenuInput());
    // Back at the main menu behind another window: the menu, still shown,
    // holds the pads again; hidden, Start there asks for it and holds them.
    Overlay::OverlayWindowFunc(nullptr, WM_ACTIVATEAPP, FALSE, 0);
    CHECK(!Overlay::HasInputFocus());
    pads.Place(MenuContext::MainMenu, false); CHECK(Overlay::CapturesMenuInput());
    pads.Publish(false, pads.Take()); CHECK(!Overlay::CapturesMenuInput());
    Overlay::RequestMainControls(); CHECK(Overlay::CapturesMenuInput());
    // A display reset drops what waited; the next pad update and frame decide again.
    Overlay::FreeOverlay();
    CHECK(!Overlay::CapturesMenuInput());
    return 0;
}
