#pragma once
#include "GamepadSlots.hxx"
#include "ButtonPrompts.hxx"

namespace sf4e { namespace gamepads {
inline const char* BrandName(Brand brand) {
    switch (brand) {
    case Brand::Xbox: return "Xbox";
    case Brand::PlayStation: return "PlayStation";
    case Brand::Nintendo: return "Nintendo";
    case Brand::Stick: return "Stick";
    default: return "Generic";
    }
}
inline PromptBrand DetectBrand(const Pad& pad) {
    if (SetUpOnlyByEmber(pad)) return {Brand::Stick};
    switch (pad.type) {
    case Type::Xbox360: case Type::XboxOne: return {Brand::Xbox};
    case Type::PS3: return {Brand::PlayStation, PlayStationArt::PS3};
    case Type::PS4: return {Brand::PlayStation, PlayStationArt::PS4};
    case Type::PS5: return {Brand::PlayStation, PlayStationArt::PS5};
    case Type::SwitchPro: case Type::JoyConLeft: case Type::JoyConRight: case Type::JoyConPair:
        return {Brand::Nintendo};
    default: return {};
    }
}
// Without a layer pad, XInput still has its known layout. DirectInput keeps
// its existing LP/LK prompts; it has no detected brand.
inline PromptBrand GameplayBrand(int device, const Pad* pad = nullptr) {
    return device == input::PadXInput ? (pad ? DetectBrand(*pad) : PromptBrand{Brand::Xbox}) : PromptBrand{};
}
} }
