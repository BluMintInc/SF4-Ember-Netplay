#pragma once
#include "../common/SdlMapping.hxx"
#include <SDL3/SDL.h>
#include <optional>
#include <vector>

namespace sf4e { namespace gamepads {
static_assert(static_cast<int>(SdlBindType::Button) == SDL_GAMEPAD_BINDTYPE_BUTTON && static_cast<int>(SdlBindType::Axis) == SDL_GAMEPAD_BINDTYPE_AXIS &&
    static_cast<int>(SdlBindType::Hat) == SDL_GAMEPAD_BINDTYPE_HAT && SdlLeftTrigger == SDL_GAMEPAD_AXIS_LEFT_TRIGGER &&
    SdlDpadUp == SDL_GAMEPAD_BUTTON_DPAD_UP && SdlButtonNames.size() == SDL_GAMEPAD_BUTTON_COUNT && SdlAxisNames.size() == SDL_GAMEPAD_AXIS_COUNT,
    "SDL's binding model moved");
// The one way Ember knows a pad's bindings: what SDL binds for an open
// gamepad, in SDL's order, with label hints and the HIDAPI fixup already applied.
inline std::vector<SdlBinding> ReadBindings(SDL_Gamepad* gamepad) {
    int count = 0;
    SDL_GamepadBinding** bindings = SDL_GetGamepadBindings(gamepad, &count);
    std::vector<SdlBinding> read;
    for (int i = 0; i < count; ++i) {
        const auto& sdl = *bindings[i];
        SdlBinding binding;
        binding.input.type = static_cast<SdlBindType>(sdl.input_type);
        if (sdl.input_type == SDL_GAMEPAD_BINDTYPE_BUTTON) binding.input.index = sdl.input.button;
        else if (sdl.input_type == SDL_GAMEPAD_BINDTYPE_HAT) { binding.input.index = sdl.input.hat.hat; binding.input.hatMask = sdl.input.hat.hat_mask; }
        else binding.input = {SdlBindType::Axis, sdl.input.axis.axis, sdl.input.axis.axis_min, sdl.input.axis.axis_max};
        if (sdl.output_type == SDL_GAMEPAD_BINDTYPE_BUTTON) binding.output = {SdlBindType::Button, sdl.output.button};
        else binding.output = {SdlBindType::Axis, sdl.output.axis.axis, sdl.output.axis.axis_min, sdl.output.axis.axis_max};
        read.push_back(binding);
    }
    SDL_free(bindings);
    return read;
}
// A pad's bindings by id, from its open gamepad or one opened for the read. A
// pad SDL has no mapping for binds nothing. Empty when SDL cannot open a mapped pad.
inline std::optional<std::vector<SdlBinding>> ReadBindings(SDL_JoystickID id, SDL_Gamepad* open) {
    if (open) return ReadBindings(open);
    if (!SDL_IsGamepad(id)) return std::vector<SdlBinding>{};
    SDL_Gamepad* gamepad = SDL_OpenGamepad(id);
    if (!gamepad) return {};
    auto read = ReadBindings(gamepad);
    SDL_CloseGamepad(gamepad);
    return read;
}
} }
