#pragma once
#include "PadKind.hxx"

namespace sf4e { namespace gamepads {

enum class Brand { Xbox, PlayStation, Nintendo, Stick, Generic };
enum class PlayStationArt { PS3, PS4, PS5 };
struct PromptBrand {
    Brand brand;
    PlayStationArt playStation;
    constexpr PromptBrand(Brand brand = Brand::Generic, PlayStationArt playStation = PlayStationArt::PS4)
        : brand(brand), playStation(playStation) {}
    bool operator==(const PromptBrand& other) const {
        return brand == other.brand && playStation == other.playStation;
    }
};
enum class ButtonPrompts { Automatic, Xbox, PlayStation, Nintendo };
inline bool Valid(ButtonPrompts value) {
    return value >= ButtonPrompts::Automatic && value <= ButtonPrompts::Nintendo;
}
inline PromptBrand ResolvePrompts(ButtonPrompts setting, int device, PromptBrand detected) {
    if (device != input::PadXInput) return {};
    switch (setting) {
    case ButtonPrompts::Xbox: return {Brand::Xbox};
    case ButtonPrompts::PlayStation: return {Brand::PlayStation, PlayStationArt::PS4};
    case ButtonPrompts::Nintendo: return {Brand::Nintendo};
    default: return detected;
    }
}
struct PositionNames { const char* view; const char* start; };
inline PositionNames ButtonPositionNames(int device, PromptBrand brand) {
    if (device == input::PadDirectInput) return {"Select", "Start"};
    switch (brand.brand) {
    case Brand::PlayStation:
        switch (brand.playStation) {
        case PlayStationArt::PS3: return {"Select", "Start"};
        case PlayStationArt::PS5: return {"Create", "Options"};
        default: return {"Share", "Options"};
        }
    case Brand::Nintendo: return {"Minus", "Plus"};
    case Brand::Stick: return {"Select", "Start"};
    default: return {"Back", "Start"};
    }
}
} }
