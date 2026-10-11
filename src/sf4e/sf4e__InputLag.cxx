#include <windows.h>
#include <detours/detours.h>
#include <atomic>
#include <cstring>
#include <string>

#include "spdlog/spdlog.h"

#include "../Dimps/Dimps__Game__Battle.hxx"
#include "../Dimps/Dimps__Pad.hxx"
#include "../common/InputLagEdit.hxx"
#include "sf4e__CodePages.hxx"
#include "sf4e__InputLag.hxx"

using rKeyboards = Dimps::Pad::System_RawInput;
namespace fInputLag = sf4e::InputLag;

namespace {
// The keyboard hook is queued, then committed. Until then the keyboard row
// does nothing and the game keeps its own timing.
bool attached = false;
std::atomic<bool> hooked{false};
// The keyboard row as the last pad update saw it.
bool keyboardFirst = false;

sf4e::code_edit::Edit Store() { return sf4e::input_lag::StoreAtReadFrame(Dimps::Game::Battle::Network::inputStoreFrame); }

// The battle's choice is made once, before its first frame.
bool decided = false;

void Decide(bool on, const char* source, const std::string& seats) {
	decided = true;
	const auto store = Store();
	sf4e::CodePages pages;
	if (!sf4e::code_edit::Set(store, on, pages))
		spdlog::warn("Input lag: the battle's input store was left as it is: it holds other bytes than this game build's, or could not be written");
	const bool lowerLag = std::memcmp(store.at, store.patched, store.length) == 0;
	spdlog::info("Input lag: battle P1={} source={} seats={} keyboard={}", lowerLag ? "on" : "off", source, seats,
		keyboardFirst ? "on" : "off");
}

struct Keyboards : rKeyboards {
	void Update();
};

// The device loop's pass. With the keyboard row on, the pad update already
// took this frame's keys (BeforePadUpdate); this one would only age them.
void Keyboards::Update() {
	if (keyboardFirst) return;
	(this->*rKeyboards::publicMethods.Update)();
}
}

void fInputLag::Install() {
	void (Keyboards::* update)() = &Keyboards::Update;
	attached = DetourAttach((PVOID*)&rKeyboards::publicMethods.Update, *(PVOID*)&update) == NO_ERROR;
}

void fInputLag::Activate() {
	hooked.store(attached);
}

void fInputLag::StartMatch(bool on, const bool (&seats)[2]) {
	Decide(on, "match", std::string(seats[0] ? "on" : "off") + "/" + (seats[1] ? "on" : "off"));
}

void fInputLag::BeforeBattleFrame(bool setting) {
	if (!decided) Decide(setting, "setting", "-");
}

void fInputLag::CloseBattle() {
	decided = false;
	sf4e::CodePages pages;
	if (!sf4e::code_edit::Set(Store(), false, pages))
		spdlog::warn("Input lag: the battle's input store could not be put back");
}

void fInputLag::BeforePadUpdate(bool keyboard) {
	keyboardFirst = keyboard && hooked.load();
	if (!keyboardFirst) return;
	// The game's own step, through the hook's trampoline: it still waits for
	// the pad update's request, so the keyboard stays frozen while the Steam
	// overlay is open.
	if (auto* keyboards = rKeyboards::staticMethods.GetSingleton()) (keyboards->*rKeyboards::publicMethods.Update)();
}
