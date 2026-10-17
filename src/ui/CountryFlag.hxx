#pragma once
// A member's country flag beside their name: 4:3, about as tall as the name's
// letters, with a faint 1 px edge so a white flag still reads on a light card.
// None (Hidden, or a country nobody shared) draws nothing and takes no room.
#include "../common/Country.hxx"
#include "Theme.hxx"
#include <imgui.h>
#include <cmath>
#include <string>

namespace sf4e { namespace ui {
// The flag's height beside text of `fontSize`, in whole pixels.
inline float FlagHeight(float fontSize) { return std::round(fontSize * .72f); }
// The room a flag takes beside text of `fontSize`, the gap after it included.
inline float FlagSlotWidth(float fontSize) { return std::round(FlagHeight(fontSize) * 4 / 3) + std::round(5 * Scale()); }
struct DrawnFlag {
    // FlagSlotWidth when a flag was drawn, else 0.
    float width = 0;
    bool hovered = false;
};
// The flag of `country` on a line of text of `fontSize` whose top is
// `textTop`, starting at `x`, from the menu art (SetMenuArt). Nothing while the
// atlas loads or without art.
DrawnFlag DrawCountryFlagBeside(ImDrawList* drawList, float x, float textTop, float fontSize, const CountryCode& country);
// The country's name in the interface language, its characters noted for the
// font atlas as a name's are (NoteUserText); empty for none.
std::string CountryName(const CountryCode& country);
} }
