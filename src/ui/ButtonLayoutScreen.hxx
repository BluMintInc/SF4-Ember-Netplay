#pragma once
#include "ControllersScreen.hxx"
#include "../common/ButtonLayout.hxx"

namespace sf4e { namespace ui {
struct LayoutFunction { const char* id; const char* key; std::uint32_t mask; };
constexpr std::array<LayoutFunction, 11> LayoutFunctions = {{
    {"lp", "pad_setup.step.lp", 0x10}, {"mp", "pad_setup.step.mp", 0x20}, {"hp", "pad_setup.step.hp", 0x400},
    {"lk", "pad_setup.step.lk", 0x40}, {"mk", "pad_setup.step.mk", 0x80}, {"hk", "pad_setup.step.hk", 0x800},
    {"3p", "pad_setup.step.3p", 0x430}, {"3k", "pad_setup.step.3k", 0x8C0},
    {"throw", "button_layout.throw", 0x50}, {"focus", "button_layout.focus", 0xA0},
    {"red-focus", "button_layout.red_focus", 0xB0}
}};
std::string LayoutName(const std::optional<gamepads::ButtonLayout>& layout);
// The row a capture belongs to: fn- and the function's id for an assign, edit-button for a pick.
inline std::string LayoutCaptureRow(const std::optional<unsigned>& function) {
    return function ? std::string("fn-") + LayoutFunctions[*function].id : std::string("edit-button");
}
std::string AttackLongName(std::uint32_t mask);
gamepads::FightNames LayoutButtonNames(const gamepads::PadView& pad, gamepads::ButtonPrompts prompts);
// How a capture shows on the rows: the row waiting for a press reads
// button_layout.waiting in Ember and every other row dims; the row that just
// took a press shows its value in Ember for a moment. Empty ids mark nothing.
struct LayoutRowMarks { std::string waiting, flash; };
std::vector<MenuEntry> ButtonLayoutRows(const gamepads::PadView& pad, bool editable, const LayoutRowMarks& marks = {});
std::vector<MenuEntry> ButtonEditorRows(const gamepads::PadView& pad, unsigned button, bool editable);
void DrawButtonLayoutDetail(const gamepads::PadView& pad, gamepads::ButtonPrompts prompts, bool capture);
// The card over the list while a capture waits: what to press (press_for, or
// press_to_change for a pick), the controller's name, a bar that empties over
// the capture's timeout (left, 1 to 0) and how to cancel.
struct CaptureCard { std::string prompt, pad; double left = 1; };
CaptureCard LayoutCaptureCard(const gamepads::PadView& pad, const std::optional<std::uint32_t>& mask, double left);
float CaptureCardHeight(const CaptureCard& card, float width);
void DrawCaptureCard(const CaptureCard& card);
void DrawButtonEditorDetail(const gamepads::PadView& pad, gamepads::ButtonPrompts prompts, unsigned button);
} }
