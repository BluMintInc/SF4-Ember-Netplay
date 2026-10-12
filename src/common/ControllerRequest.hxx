#pragma once
namespace sf4e { namespace gamepads {
struct ControllerRequest {
    enum class Kind { None, Play, Setup, Forget } kind = Kind::None;
    unsigned id = 0;
};
} }
