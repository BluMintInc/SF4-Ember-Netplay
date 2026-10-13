#pragma once
#include "PadSetup.hxx"
namespace sf4e { namespace gamepads {
struct ControllerRequest {
    enum class Kind { None, Play, Setup, Forget, SetupControl, Prompt, DeclinePrompt } kind = Kind::None;
    unsigned id = 0;
    SetupCommand command = SetupCommand::None;
    int step = 0;
};
} }
