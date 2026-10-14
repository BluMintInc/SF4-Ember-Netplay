// Ember opened over a room game this PC only watches lands on the board at
// Stop watching, which asks first and submits one leave naming the watched
// game (ShellAction::leaveWatchedGame); the runtime decides what leaving it
// takes when it runs. A watcher by choice is offered Stop watching, a queued
// member Leave this game. Once the room has finished the game while its stream
// still plays, the row stays and says so. The row and dispatch read the same
// rule (netplay::LeaveOfWatchedGame), so a press never outlives the game it
// was about, and a room still catching up on its checkpoint takes the leave
// all the same. The table's own Unwatch of that game is the same leave.
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
        view.watchedGame = {0, 9};
        SetMenuEntriesProbe([this](const std::vector<MenuEntry>& entries) { rows = entries; });
        SetMenuStatusProbe([this](const char* text, Tone) { status = text; });
    }
    ~WatchedBoard() { SetMenuEntriesProbe({}); SetMenuStatusProbe({}); }
    room::Table& Table() { return view.room.tables[0]; }
    room::Member& Local() { return view.room.members[0]; }
    ApplicationShell::Submit Submit() { return [this](ShellAction action) { actions.push_back(std::move(action)); return true; }; }
    void Frame(unsigned buttons = 0, float seconds = 1.f / 60) {
        ImGui::GetIO().DeltaTime = seconds; SetMenuInput({buttons, 0}); ImGui::NewFrame();
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
    // Other submissions (a preference save) may ride along; only the leaves
    // count. No press may send an Unwatch of its own for the watched game.
    std::vector<netplay::WatchedGame> Leaves() const {
        std::vector<netplay::WatchedGame> sent;
        for (const auto& action : actions) {
            Check(action.command.kind != netplay::CommandKind::RoomAction || action.roomAction.kind != room::ActionKind::Unwatch,
                "A press sent an Unwatch of its own instead of the watched game's leave");
            if (action.leaveWatchedGame.Active()) sent.push_back(action.leaveWatchedGame);
        }
        return sent;
    }
    bool LeftOnce() const {
        const auto sent = Leaves();
        return sent.size() == 1 && sent[0] == netplay::WatchedGame{0, 9};
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
    Check(board.Leaves().empty(), "Stop watching did not ask first");
    board.Press(MenuInput::Right); board.Press(MenuInput::Select);
    Check(board.LeftOnce(), "Stop watching did not submit one leave of the watched game");
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
// game alone and keeps the queue place (the runtime's Unwatch keeps watching).
void QueuedWatcherLeavesTheGame() {
    WatchedBoard board;
    board.Table().queue = {1}; board.Local().status = room::MemberStatus::Queued;
    board.OpenOverGame();
    Check(board.shell.Navigation().Focus() == "stop-watching" && board.Row("stop-watching").label == loc::T("room.leave_game") &&
        board.Row("stop-watching").detail == loc::T("room.leave_game.detail"), "A queued watcher was not offered Leave this game");
    board.Confirm();
    Check(board.LeftOnce(), "Leave this game did not submit one leave of the watched game");
    std::cout << "QueuedWatcherLeavesTheGame passed\n";
}

// Once the game is no longer watched the row and its question go, and
// nothing is sent.
void DispatchReadsTheRuleWhenAnswered() {
    WatchedBoard board;
    board.OpenOverGame();
    board.Press(MenuInput::Select);
    board.view.watchedGame = {}; board.Frame();
    Check(!board.Has("stop-watching") && !board.shell.Navigation().Confirming(),
        "Stop watching, or the question about it, outlived the watched game");
    board.Press(MenuInput::Right); board.Press(MenuInput::Select);
    Check(board.Leaves().empty(), "A press after the stream ended still sent a leave");
    std::cout << "DispatchReadsTheRuleWhenAnswered passed\n";
}

// The table page's own Stop watching of the game the room still plays is the
// same leave, never an Unwatch scoped to whatever the table plays when it lands.
void TableUnwatchIsTheSameLeave() {
    WatchedBoard board;
    board.OpenOverGame();
    board.shell.Navigation().Push("room-table"); board.Frame();
    board.shell.Navigation().Focus("unwatch", board.rows);
    board.Press(MenuInput::Select);
    Check(board.LeftOnce(), "The table's own Stop watching did not submit the watched game's leave");
    std::cout << "TableUnwatchIsTheSameLeave passed\n";
}

// A fighter has no spectator's exit, whatever the context says.
void FighterHasNoWatchedExit() {
    WatchedBoard board;
    board.Table().p1 = 1; board.Local().seat = 0; board.Local().status = room::MemberStatus::Playing;
    board.Table().spectators.clear();
    board.view.room.members[1].table = -1; board.view.room.members[1].seat = -1;
    board.OpenOverGame();
    Check(!board.Has("stop-watching") && board.Leaves().empty(), "A fighter was offered Stop watching");
    std::cout << "FighterHasNoWatchedExit passed\n";
}

// The player's own leave of a locked-in watch is not reported as the room
// ending the lock-in, however long the room takes to show it; the room ending
// it otherwise still is. A leave the room can no longer take (the game ended
// first) explains nothing that happens to the lock-in later.
void LockedInLeaveIsNotReported() {
    enum class Then { RoomTakesIt, ViewDrops, GameEndsFirst };
    const auto notice = [](bool leftByPlayer, Then then) {
        WatchedBoard board;
        board.Local().spectatorLocked = true;
        board.OpenOverGame();
        if (leftByPlayer) {
            board.Confirm();
            Check(board.LeftOnce(), "A locked-in watcher's Stop watching submitted no leave");
        }
        // The stream retires at once; the room's answer is slow to show.
        board.view.watchedGame = {};
        for (int i = 0; i < 7; ++i) board.Frame(0, 1.f);
        board.status.clear();
        if (then == Then::GameEndsFirst) {
            board.Table().phase = room::TablePhase::Waiting; ++board.Table().revision; ++board.view.room.revision;
            board.Frame();
        }
        if (then != Then::ViewDrops) board.LeftTable();
        else { board.Local().spectatorLocked = false; ++board.view.room.revision; }
        for (int i = 0; i < 5; ++i) board.Frame();
        return board.status;
    };
    Check(notice(false, Then::RoomTakesIt) == loc::T("room.lock_in_ended.stopped"), "The room ending a lock-in by itself was not reported");
    const auto own = notice(true, Then::RoomTakesIt);
    for (const char* id : {"room.lock_in_ended.stopped", "room.lock_in_ended.moved", "room.lock_in_ended.playing", "room.lock_in_ended.dropped"})
        Check(own != loc::T(id), "The player's own Stop watching, shown late, was reported as a released lock-in");
    Check(notice(true, Then::ViewDrops) == loc::T("room.lock_in_ended.dropped"),
        "A lock-in the view's drop ended was taken for the player's Stop watching");
    Check(notice(true, Then::GameEndsFirst) == loc::T("room.lock_in_ended.stopped"),
        "A Stop watching the room could no longer take hid a later end of the lock-in");
    std::cout << "LockedInLeaveIsNotReported passed\n";
}

// Release lock-in from Table options is the player's own too, however late
// the room shows it. A release the room never took, with the lock-in still
// held once the table's next game has begun, explains nothing later.
void ReleaseIsNotReported() {
    const auto notice = [](bool nextGameFirst) {
        WatchedBoard board;
        board.Local().spectatorLocked = true;
        board.OpenOverGame();
        board.shell.Navigation().Push("room-table"); board.Frame();
        board.shell.Navigation().Focus("lock-spectating", board.rows);
        board.Press(MenuInput::Select);
        Check(!board.actions.empty() && board.actions.back().roomAction.kind == room::ActionKind::LockSpectating &&
            !board.actions.back().roomAction.locked, "Release lock-in was not sent");
        for (int i = 0; i < 7; ++i) board.Frame(0, 1.f);
        board.status.clear();
        if (nextGameFirst) { ++board.Table().matchGeneration; ++board.Table().revision; ++board.view.room.revision; board.Frame(); }
        board.Local().spectatorLocked = false; ++board.view.room.revision;
        for (int i = 0; i < 5; ++i) board.Frame();
        return board.status;
    };
    Check(notice(false) != loc::T("room.lock_in_ended.dropped"), "The player's own release, shown late, was announced as the room's");
    Check(notice(true) == loc::T("room.lock_in_ended.dropped"), "A release the room never took hid a later end of the lock-in");
    std::cout << "ReleaseIsNotReported passed\n";
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
        Check(board.LeftOnce(), "A room catching up did not take Stop watching");
    }
    {
        WatchedBoard board;
        board.view.session.control = netplay::Health::Lost;
        board.OpenOverGame();
        Check(board.Has("stop-watching") && !board.Row("stop-watching").enabled, "Stop watching was offered over a lost room control");
        board.Confirm();
        Check(board.Leaves().empty(), "Stop watching was sent over a lost room control");
    }
    std::cout << "CatchingUpRoomTakesTheLeave passed\n";
}

// The room has finished the watched game while its delayed stream still plays:
// the table is back in Waiting, a queued watcher was rotated into a seat, or
// the table already plays its next game. Stop watching stays, says the game
// is over, and needs no room, so a lost control does not take it away. Its
// leave names the watched game, never the table's newer one, and the runtime
// only ends the rest of the stream (LeaveWatchedGame).
void FinishedGameEndsOnlyTheView() {
    enum class After { Waiting, RotatedIntoSeat, NextGame };
    for (const auto after : {After::Waiting, After::RotatedIntoSeat, After::NextGame}) {
        WatchedBoard board;
        if (after == After::RotatedIntoSeat) board.Table().queue = {1};
        board.OpenOverGame();
        auto& table = board.Table();
        table.phase = room::TablePhase::Waiting; ++table.revision; ++board.view.room.revision;
        if (after == After::RotatedIntoSeat) {
            // The loser went to the queue and the queued watcher took its seat.
            table.queue = {3}; table.spectators = {3}; table.p2 = 1;
            board.Local().seat = 1; board.Local().status = room::MemberStatus::Idle;
            board.view.room.members[2].seat = -1; board.view.room.members[2].status = room::MemberStatus::Queued;
        }
        if (after == After::NextGame) { table.phase = room::TablePhase::Playing; table.matchGeneration = 10; }
        board.view.session.control = netplay::Health::Lost;
        board.Frame();
        const auto& row = board.Row("stop-watching");
        Check(row.enabled && row.label == loc::T("room.stop_watching") && row.detail == loc::T("room.stop_watching.finished"),
            "Stop watching did not stay for the rest of a finished game's stream");
        board.Confirm();
        Check(board.LeftOnce(), "The rest of a finished game's stream was not left with one leave naming it");
    }
    std::cout << "FinishedGameEndsOnlyTheView passed\n";
}
}

int main() try {
    HeadlessImGui imgui;
    ByChoiceWatcherStopsWatching();
    QueuedWatcherLeavesTheGame();
    DispatchReadsTheRuleWhenAnswered();
    TableUnwatchIsTheSameLeave();
    FighterHasNoWatchedExit();
    LockedInLeaveIsNotReported();
    ReleaseIsNotReported();
    CatchingUpRoomTakesTheLeave();
    FinishedGameEndsOnlyTheView();
    std::cout << "Watched match exit tests passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
