#pragma once
#include "../common/ButtonPrompts.hxx"
#include "../common/Localization.hxx"

namespace sf4e { namespace ui {
inline const char* PromptBrandWord(gamepads::Brand brand) {
    switch (brand) {
    case gamepads::Brand::PlayStation: return loc::T("controller.brand.playstation");
    case gamepads::Brand::Nintendo: return loc::T("controller.brand.nintendo");
    case gamepads::Brand::Stick: return loc::T("controller.brand.stick");
    default: return loc::T("controller.brand.xbox");
    }
}
inline std::string ButtonPromptValue(gamepads::ButtonPrompts setting) {
    if (setting != gamepads::ButtonPrompts::Automatic)
        return PromptBrandWord(gamepads::ResolvePrompts(setting, input::PadXInput, {}).brand);
    return loc::T("player.prompts_auto");
}
} }
