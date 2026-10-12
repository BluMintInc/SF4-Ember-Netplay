#pragma once
#include "MenuRows.hxx"
#include "../common/ControllerView.hxx"
#include "../netplay/InputAssignment.hxx"
#include "../common/ControllerRequest.hxx"
#include <optional>

namespace sf4e { namespace ui {
std::string RawInputName(const gamepads::RawInput& input);
std::string RawInputList(const std::vector<gamepads::RawInput>& inputs);
MenuEntry ControllerRow(const gamepads::PadView& pad, bool canChange, bool canEdit, gamepads::Layer layer = gamepads::Layer::Active);
std::vector<MenuEntry> ControllerRows(gamepads::Layer layer, const std::vector<gamepads::PadView>& pads, bool canChange, bool canEdit);
std::optional<gamepads::ControllerRequest> ControllerAction(const MenuAction& action, const std::vector<gamepads::PadView>& pads);
std::vector<MenuEntry> PlayerControllerRows(const netplay::PlayerPreferences& preferences,
    const std::string& controller, int deviceType, gamepads::PromptBrand brand,
    const std::vector<gamepads::PadView>& pads, bool pending, bool canChange, bool canEdit, bool idle);
void DrawControllerDetail(const gamepads::PadView& pad, gamepads::ButtonPrompts prompts);
void DrawCaptureDetail(input::Capture capture, const std::vector<gamepads::PadView>& pads, unsigned candidate);
} }
