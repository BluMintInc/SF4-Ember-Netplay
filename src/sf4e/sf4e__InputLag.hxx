#pragma once

// Lower input lag and its keyboard row (Player & controller, experimental).
// - The battle: the game's own frame of input delay goes (common/InputLagEdit.hxx).
//   Decided once per battle, before its first frame, and undone when it closes:
//   a room's match by the match's flag, which its fighters and spectators all
//   receive, any other battle by the player's setting. Never mid-battle.
// - The keyboard: the game turns the window's key input into key states in its
//   device loop, after the pad update has read the previous ones. With the
//   setting on, Ember runs that step just before the pad update instead, so a
//   key reaches the pad update in the frame it was pressed. Only this PC's
//   keyboard changes, and what the pad update reads is what GGPO is given.
// Everything here runs on the game thread, apart from Install and Activate.
namespace sf4e {
	namespace InputLag {
		// Queues the keyboard hook in the open Detours transaction.
		void Install();
		// Once that transaction committed.
		void Activate();
		// A room's match, before its battle runs a frame. `seats` are the two
		// fighters' own choices, for the log.
		void StartMatch(bool on, const bool (&seats)[2]);
		// Before each battle frame: a battle no match decided follows `setting`.
		void BeforeBattleFrame(bool setting);
		// The battle closed: the game's own bytes come back.
		void CloseBattle();
		// Before the native pad update, with the keyboard row's setting.
		void BeforePadUpdate(bool keyboard);
	}
}
