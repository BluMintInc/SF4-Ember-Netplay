#pragma once
#include "ControllersScreen.hxx"
#include "GameMenu.hxx"

namespace sf4e { namespace ui {
std::string SetupStepName(int step);
std::vector<MenuEntry> PadSetupRows(const gamepads::SetupState& state);
std::vector<LegendHint> PadSetupLegend(const gamepads::SetupState& state);
void DrawPadSetupDetail(const gamepads::PadView& pad, const gamepads::SetupState& state);
} }
