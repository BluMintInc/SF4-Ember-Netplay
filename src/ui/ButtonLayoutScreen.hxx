#pragma once
#include "ControllersScreen.hxx"
#include "../common/ButtonLayout.hxx"

namespace sf4e { namespace ui {
struct LayoutFunction { const char* id; const char* key; std::uint32_t mask; };
constexpr std::array<LayoutFunction, 10> LayoutFunctions = {{
    {"lp", "pad_setup.step.lp", 0x10}, {"mp", "pad_setup.step.mp", 0x20}, {"hp", "pad_setup.step.hp", 0x400},
    {"lk", "pad_setup.step.lk", 0x40}, {"mk", "pad_setup.step.mk", 0x80}, {"hk", "pad_setup.step.hk", 0x800},
    {"3p", "pad_setup.step.3p", 0x430}, {"3k", "pad_setup.step.3k", 0x8C0},
    {"throw", "button_layout.throw", 0x50}, {"focus", "button_layout.focus", 0xA0}
}};
std::string LayoutName(const std::optional<gamepads::ButtonLayout>& layout);
std::string AttackLongName(std::uint32_t mask);
gamepads::FightNames LayoutButtonNames(const gamepads::PadView& pad, gamepads::ButtonPrompts prompts,
    const std::map<std::string, std::string>& mappings);
std::vector<MenuEntry> ButtonLayoutRows(const gamepads::PadView& pad, bool editable);
std::vector<MenuEntry> ButtonEditorRows(const gamepads::PadView& pad, unsigned button, bool editable);
void DrawButtonLayoutDetail(const gamepads::PadView& pad, gamepads::ButtonPrompts prompts,
    bool capture, const std::optional<std::uint32_t>& mask);
void DrawButtonEditorDetail(const gamepads::PadView& pad, gamepads::ButtonPrompts prompts, unsigned button);
} }
