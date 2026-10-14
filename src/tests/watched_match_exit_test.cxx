// Ember opened over a room game this PC only watches lands on the board at
// Stop watching, which asks first and leaves the game with one
// generation-scoped Unwatch. A watcher by choice leaves the table; a queued
// member leaves this game alone (Leave this game) and keeps its queue place.
// Dispatch reads the same rule as the row (room_controls::ExitFromWatchedGame)
// when the answer comes, so a press never outlives the game it was about, and
// a room still catching up on its checkpoint takes the leave all the same.
#include "../ui/ApplicationShell.hxx"
#include "../common/Localization.hxx"
#include "../ui/GameMenu.hxx"
#include "../ui/MenuRows.hxx"
#include "../ui/Theme.hxx"
#include "imgui_test_support.hxx"
#include <imgui.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace sf4e;
using namespace sf4e::ui;

void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

// A room whose table 0 plays a live game (generation 9) between members 2 and
// 3, which this PC (member 1) watches with Ember hidden.
struct WatchedBoard {
    ShellView view;
    ApplicationShell shell;
    std::vector<ShellAction> actions;
    std::vector<MenuEntry> rows;
    std::string status;
    bool open = true;
    WatchedBoard() {
        view.controllerReady = true;
        view.session.room = netplay::RoomState::Joined;
        view.session.control = netplay::Health::Healthy;
        view.session.generation.room = 1;
        view.session.match = netplay::MatchState::Playing;
        view.room.roomEpoch = 4; view.room.localMember = 1; view.room.host = 2; view.room.capacity = 16;
        for (int i = 0; i < 4; ++i) view.room.tables[i].id = i;
        room::Member local; local.id = 1; local.name = "Local"; local.table = 0; local.status = room::MemberStatus::Watching;
        room::Member p1; p1.id = 2; p1.name = "Peer"; p1.table = 0; p1.seat = 0; p1.status = room::MemberStatus::Playing;
        room::Member p2; p2.id = 3; p2.name = "Third"; p2.table = 0; p2.seat = 1; p2.status = room::MemberStatus::Playing;
        view.room.members = {local, p1, p2};
        auto& table = Table();
        table.p1 = 2; table.p2 = 3; table.phase = room::TablePhase::Playing; table.matchGeneration = 9; table.revision = 5;
        table.spectators = {1};
        view.watchedMatch = true;
        SetMenuEntriesProbe([this](const std::vector<MenuEntry>& entries) { rows = entries; });
        SetMenuStatusProbe([this](const char* text, Tone) { status = text; });
    }
    ~WatchedBoard() { SetMenuEntriesProbe({}); SetMenuStatusProbe({}); }
    room::Table& Table() { return view.room.tables[0]; }
    room::Member& Local() { return view.room.members[0]; }
    ApplicationShell::Submit Submit() { return [this](ShellAction action) { actions.push_back(std::move(action)); return true; }; }
    void Frame(unsigned buttons = 0) {
        ImGui::GetIO().DeltaTime = 1.f / 60; SetMenuInput({buttons, 0}); ImGui::NewFrame();
        shell.Draw(view, &open, Submit(), [] {});
        ImGui::Render();
    }
    void Press(unsigned buttons) { Frame(); Frame(buttons); Frame(); }
    // Start or F10 over the game: the frame after a hidden one opens Ember.
    void OpenOverGame() {
        shell.Background(view, view.room, Submit());
        shell.Navigation().Home();
        for (int i = 0; i < 3; ++i) Frame();
    }
    // Asks with Select, then answers the question's confirm button.
    void Confirm() { Press(MenuInput::Select); Press(MenuInput::Right); Press(MenuInput::Select); }
    bool Has(const char* id) const {
        return std::any_of(rows.begin(), rows.end(), [&](const MenuEntry& entry) { return entry.id == id; });
    }
    const MenuEntry& Row(const char* id) const {
        const auto found = std::find_if(rows.begin(), rows.end(), [&](const MenuEntry& entry) { return entry.id == id; });
        if (found == rows.end()) throw std::runtime_error(std::string("missing room row: ") + id);
        return *found;
    }
    // Other submissions (a preference save) may ride along; only the Unwatches count.
    std::vector<room::Action> Unwatches(std::size_t from = 0) const {
        std::vector<room::Action> sent;
        for (std::size_t i = from; i < actions.size(); ++i)
            if (actions[i].command.kind == netplay::CommandKind::RoomAction && actions[i].roomAction.kind == room::ActionKind::Unwatch)
                sent.push_back(actions[i].roomAction);
        return sent;
    }
    // The room has taken the member off the table: no longer listed, and its lock-in gone.
    void LeftTable() {
        Table().spectators.clear(); Local().table = -1; Local().spectatorLocked = false;
        Local().status = room::MemberStatus::Idle;
        ++Table().revision; ++view.room.revision;
    }
};

// A watcher by choice lands on Stop watching, is asked first, and leaves the
// table with one Unwatch scoped to the live game.
void ByChoiceWatcherStopsWatching() {
    WatchedBoard board;
    board.OpenOverGame();
    Check(board.shell.Navigation().Screen() == "room" && board.shell.Navigation().Focus() == "stop-watching",
        "Ember opened over a watched game did not land on Stop watching");
    const auto& row = board.Row("stop-watching");
    Check(row.enabled && row.confirm && row.label == loc::T("room.stop_watching") && row.detail == loc::T("room.stop_watching.detail"),
        "Stop watching was not an enabled row that asks first");
    board.Press(MenuInput::Select);
    Check(board.Unwatches().empty(), "Stop watching did not ask first");
    board.Press(MenuInput::Right); board.Press(MenuInput::Select);
    const auto sent = board.Unwatches();
    Check(sent.size() == 1 && sent[0].table == 0 && sent[0].matchGeneration == 9 && !sent[0].keepWatching,
        "Stop watching did not send one Unwatch scoped to the watched game");
    // Leaving the table and the room both say what they end over the game.
    Check(board.Row("leave").detail.find(loc::T("room.leave.watching")) != std::string::npos,
        "Leave room did not say it ends the spectator view");
    const auto& card = board.Row("table-1");
    Check(!card.choices.empty() && std::all_of(card.choices.begin(), card.choices.end(), [](const MenuChoice& choice) {
        return choice.id == "options" || choice.detail.find(loc::T("room.watching_ends")) != std::string::npos;
    }), "A seat choice elsewhere did not say it ends the spectator view");
    // Once Ember is shown, the board stays where the player goes.
    board.shell.Navigation().Home(); board.Frame();
    Check(board.shell.Navigation().Screen() != "room", "A drawn frame moved the player back to Stop watching");
    std::cout << "ByChoiceWatcherStopsWatching passed\n";
}

// A queued member watches the game it waits out: Leave this game leaves that
// game alone and keeps the queue place.
void QueuedWatcherLeavesTheGame() {
    WatchedBoard board;
    board.Table().queue = {1}; board.Local().status = room::MemberStatus::Queued;
    board.OpenOverGame();
    Check(board.shell.Navigation().Focus() == "stop-watching" && board.Row("stop-watching").label == loc::T("room.leave_game") &&
        board.Row("stop-watching").detail == loc::T("room.leave_game.detail"), "A queued watcher was not offered Leave this game");
    board.Confirm();
    const auto sent = board.Unwatches();
    Check(sent.size() == 1 && sent[0].table == 0 && sent[0].matchGeneration == 9 && sent[0].keepWatching,
        "Leave this game did not keep the queued watcher's place");
    std::cout << "QueuedWatcherLeavesTheGame passed\n";
}

// The answer is read when it comes: a queued watcher whose queue place ended
// while the question was open stops watching instead. Once the game is no
// longer watched the row and its question go, and nothing is sent.
void DispatchReadsTheRuleWhenAnswered() {
    {
        WatchedBoard board;
        board.Table().queue = {1};
        board.OpenOverGame();
        board.Press(MenuInput::Select);
        Check(board.shell.Navigation().Confirming(), "Leave this game did not ask first");
        board.Table().queue.clear(); ++board.Table().revision; ++board.view.room.revision;
        board.Press(MenuInput::Right); board.Press(MenuInput::Select);
        const auto sent = board.Unwatches();
        Check(sent.size() == 1 && !sent[0].keepWatching, "A leave answered after the queue place ended kept a place it no longer had");
    }
    {
        WatchedBoard board;
        board.OpenOverGame();
        board.Press(MenuInput::Select);
        board.view.watchedMatch = false; board.Frame();
        Check(!board.Has("stop-watching") && !board.shell.Navigation().Confirming(),
            "Stop watching, or the question about it, outlived the watched game");
        board.Press(MenuInput::Right); board.Press(MenuInput::Select);
        Check(board.Unwatches().empty(), "A press after the stream ended still sent a leave");
    }
    std::cout << "DispatchReadsTheRuleWhenAnswered passed\n";
}

// A fighter has no spectator's exit, whatever the context says.
void FighterHasNoWatchedExit() {
    WatchedBoard board;
    board.Table().p1 = 1; board.Local().seat = 0; board.Local().status = room::MemberStatus::Playing;
    board.Table().spectators.clear();
    board.view.room.members[1].table = -1; board.view.room.members[1].seat = -1;
    board.OpenOverGame();
    Check(!board.Has("stop-watching") && board.Unwatches().empty(), "A fighter was offered Stop watching");
    std::cout << "FighterHasNoWatchedExit passed\n";
}

// The player's own leave of a locked-in watch is not reported as the room
// ending the lock-in; the room ending it otherwise still is.
void LockedInLeaveIsNotReported() {
    const auto ended = [](bool leftByPlayer) {
        WatchedBoard board;
        board.Local().spectatorLocked = true;
        board.OpenOverGame();
        if (leftByPlayer) {
            board.Confirm();
            Check(board.Unwatches().size() == 1, "A locked-in watcher's Stop watching sent no Unwatch");
        }
        board.view.watchedMatch = false; board.LeftTable();
        for (int i = 0; i < 5; ++i) board.Frame();
        return board.status == loc::T("room.lock_in_ended.stopped");
    };
    Check(ended(false), "The room ending a lock-in by itself was not reported");
    Check(!ended(true), "The player's own Stop watching was reported as a released lock-in");
    std::cout << "LockedInLeaveIsNotReported passed\n";
}

// A room still catching up on its checkpoint takes the leave; a room whose
// control is recovering does not.
void CatchingUpRoomTakesTheLeave() {
    {
        WatchedBoard board;
        board.view.session.coordinated = true; board.view.session.authorityWritable = false;
        board.OpenOverGame();
        Check(board.Row("stop-watching").enabled, "Stop watching waited for the room to catch up");
        const auto& choices = board.Row("table-0").choices;
        Check(std::any_of(choices.begin(), choices.end(), [](const MenuChoice& choice) { return choice.id == "unwatch" && choice.enabled; }),
            "The watched table's own Unwatch choice waited for the room to catch up");
        board.Confirm();
        const auto sent = board.Unwatches();
        Check(sent.size() == 1 && sent[0].matchGeneration == 9, "A room catching up did not take Stop watching");
    }
    {
        WatchedBoard board;
        board.view.session.control = netplay::Health::Lost;
        board.OpenOverGame();
        Check(board.Has("stop-watching") && !board.Row("stop-watching").enabled, "Stop watching was offered over a lost room control");
        board.Confirm();
        Check(board.Unwatches().empty(), "Stop watching was sent over a lost room control");
    }
    std::cout << "CatchingUpRoomTakesTheLeave passed\n";
}
}

int main() try {
    HeadlessImGui imgui;
    ByChoiceWatcherStopsWatching();
    QueuedWatcherLeavesTheGame();
    DispatchReadsTheRuleWhenAnswered();
    FighterHasNoWatchedExit();
    LockedInLeaveIsNotReported();
    CatchingUpRoomTakesTheLeave();
    std::cout << "Watched match exit tests passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
