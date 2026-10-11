#include "../ui/OverlayPresentation.hxx"
#include <cstdlib>
#include <iostream>
#include "test_support.hxx"
int main() {
    using sf4e::netplay::MatchState;
    using sf4e::ui::OverlayPresentation;
    for (unsigned flags = 0; flags < 16; ++flags) {
        CHECK(sf4e::NativePauseMenuOpen(flags, -1) == false);
        CHECK(sf4e::NativePauseMenuOpen(flags, 0) == ((flags & 1) != 0));
        CHECK(sf4e::NativePauseMenuOpen(flags, 1) == ((flags & 1) != 0));
    }
    // Training load-state hold, replay Back freeze, and native online wait
    // must leave the passive overlays visible without a pause-menu player.
    CHECK(!sf4e::NativePauseMenuOpen(1, -1));
    CHECK(!sf4e::NativePauseMenuOpen(8, -1));
    CHECK(!sf4e::NativePauseMenuOpen(4, -1));
    for (int shell = 0; shell < 2; ++shell)
        for (int controls = 0; controls < 2; ++controls)
            for (int paused = 0; paused < 2; ++paused)
                CHECK(sf4e::ui::PassiveOverlayShown(shell != 0, controls != 0, paused != 0) == (!shell && !controls && !paused));
    sf4e::SetNativePauseForTest(true); CHECK(sf4e::battlePause.Paused());
    sf4e::battlePause.CloseBattle(); CHECK(!sf4e::battlePause.Paused());
    sf4e::SetNativePauseForTest(true);
    sf4e::battlePause.StartBattle(); CHECK(!sf4e::battlePause.Paused());
    OverlayPresentation menu;
    menu.Update(false, MatchState::None, false, true);
    menu.Open(); CHECK(!menu.Visible());
    menu.Update(true, MatchState::None, false, true); CHECK(menu.Visible());
    menu.Toggle(); CHECK(!menu.Visible()); menu.Open(); CHECK(menu.Visible());
    // Alt-tab keeps the shell on screen, and an unfocused window cannot toggle it.
    menu.Update(true, MatchState::None, false, false); CHECK(menu.Visible());
    menu.Toggle(); CHECK(menu.Visible());
    // The pad needs no focus: Start opens the shell and the shell closes itself.
    menu.Close(); CHECK(!menu.Visible());
    menu.Update(true, MatchState::None, false, false); menu.Open(); CHECK(menu.Visible());
    menu.Close(); CHECK(!menu.Visible()); menu.Open(); CHECK(menu.Visible());
    menu.Update(true, MatchState::None, false, true); CHECK(menu.Visible());
    menu.Update(true, MatchState::Preparing, false, true);
    menu.Open(); menu.Toggle(); CHECK(!menu.Visible() && !menu.Available());
    menu.Update(false, MatchState::Playing, false, true); CHECK(!menu.Visible());
    menu.Update(false, MatchState::PostMatch, false, true); CHECK(!menu.Visible());
    menu.Update(true, MatchState::PostMatch, false, true); CHECK(menu.Visible() && menu.Reopened());
    // A transient foreground drop after the fight must not hide the shell or
    // reopen it a second time; a sustained absence still hides it.
    for (unsigned i = 0; i + 1 < OverlayPresentation::AwayFrames; ++i) {
        menu.Update(false, MatchState::PostMatch, false, true); CHECK(menu.Visible() && !menu.Reopened());
    }
    menu.Update(true, MatchState::PostMatch, false, true); CHECK(menu.Visible() && !menu.Reopened());
    for (unsigned i = 0; i < OverlayPresentation::AwayFrames; ++i) menu.Update(false, MatchState::PostMatch, false, true);
    CHECK(!menu.Visible() && !menu.Available());
    menu.Update(true, MatchState::PostMatch, false, true); CHECK(menu.Visible() && !menu.Reopened());
    menu.Close(); menu.Update(true, MatchState::PostMatch, false, true); CHECK(!menu.Visible() && !menu.Reopened());
    menu.Update(true, MatchState::None, true, true); CHECK(!menu.Visible());
    menu.Open(); CHECK(menu.Visible());
    for (unsigned i = 0; i < OverlayPresentation::AwayFrames; ++i) menu.Update(false, MatchState::None, true, true);
    CHECK(!menu.Visible());
    menu.Update(true, MatchState::None, true, true); CHECK(menu.Visible());
    // A grant withdrawn before the fight (a late spectator link) goes straight
    // from Preparing to PostMatch, and the shell must come back all the same.
    OverlayPresentation late;
    late.Update(true, MatchState::Preparing, false, true); CHECK(!late.Visible() && !late.Available());
    late.Update(true, MatchState::PostMatch, false, true); CHECK(late.Visible() && late.Reopened());
    // Game-thread open requests wait for the drawing thread and coalesce, Play first.
    using Kind = sf4e::ui::OpenRequests::Kind;
    sf4e::ui::OpenRequests requests;
    CHECK(requests.Take() == Kind::None);
    requests.Post(Kind::Controls); requests.Post(Kind::Play);
    CHECK(requests.Take() == Kind::Play && requests.Take() == Kind::None);
    requests.Post(Kind::Play); requests.Post(Kind::Controls);
    CHECK(requests.Take() == Kind::Play);
    requests.Post(Kind::Controls);
    CHECK(requests.Take() == Kind::Controls);
    {
        // The menu's hold on the pads, as the overlay and the pad update use it.
        using sf4e::input::MenuContext;
        using sf4e::ui::MenuPads;
        OverlayPresentation shell; shell.Update(true, MatchState::None, false, false); shell.Close();
        // Start arrives after a frame took the opens and before that frame
        // publishes the menu still hidden: the request holds the pads until
        // the next frame has shown the menu.
        MenuPads pads; pads.Place(MenuContext::MainMenu, false);
        CHECK(!pads.Holds());
        auto taken = pads.Take();
        pads.Request(MenuPads::Kind::Controls);          // the game thread, mid-frame
        CHECK(pads.Holds());
        pads.Publish(shell.Visible(), taken);           // that frame: still hidden
        CHECK(!shell.Visible() && pads.Holds());
        taken = pads.Take();
        CHECK(taken.kind == MenuPads::Kind::Controls);
        shell.Open();
        CHECK(pads.Holds());                            // taken, not yet published
        pads.Publish(shell.Visible(), taken);
        CHECK(shell.Visible() && pads.Holds());
        // A request the menu cannot answer lets the pads go once answered.
        MenuPads refused; refused.Place(MenuContext::MainMenu, false);
        OverlayPresentation fight; fight.Update(true, MatchState::Preparing, false, true);
        refused.Request(MenuPads::Kind::Controls);
        CHECK(refused.Holds());
        const auto none = refused.Take(); if (none.kind != MenuPads::Kind::None && fight.Available()) fight.Open();
        refused.Publish(fight.Visible(), none);
        CHECK(!refused.Holds());
        // Drawing stops with the menu shown, so its last word stays "shown":
        // a match being prepared takes the pads from it at once, and they stay
        // the game's after the match until the main menu is back.
        CHECK(pads.Holds());
        pads.Place(MenuContext::MainMenu, true); CHECK(!pads.Holds());
        pads.Place(MenuContext::Unavailable, true); CHECK(!pads.Holds());
        pads.Place(MenuContext::Unavailable, false); CHECK(!pads.Holds());
        pads.Place(MenuContext::MainMenu, false); CHECK(pads.Holds());
        // A brief drop of the native menu is ridden out, as the menu's own
        // availability rides it; Training past that takes the pads back.
        for (unsigned i = 0; i + 1 < OverlayPresentation::AwayFrames; ++i) { pads.Place(MenuContext::Unavailable, false); CHECK(pads.Holds()); }
        pads.Place(MenuContext::MainMenu, false);
        for (unsigned i = 0; i + 1 < OverlayPresentation::AwayFrames; ++i) pads.Place(MenuContext::OfflineTraining, false);
        CHECK(pads.Holds());
        pads.Place(MenuContext::OfflineTraining, false); CHECK(!pads.Holds());
        // Over an export the menu holds them again, once no match is being prepared.
        pads.Place(MenuContext::ReplayExport, true); CHECK(!pads.Holds());
        pads.Place(MenuContext::ReplayExport, false); CHECK(pads.Holds());
        // Nothing holds before the game was ever where the menu can be shown.
        MenuPads early; early.Request(MenuPads::Kind::Controls);
        early.Place(MenuContext::Unavailable, false); CHECK(!early.Holds());
    }
    std::cout << "Safe-menu, focus, offline, loading, fight and rematch visibility passed\n";
}
