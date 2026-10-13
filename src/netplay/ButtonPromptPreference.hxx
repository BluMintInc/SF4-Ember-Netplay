#pragma once
#include "PlayerPreferences.hxx"
#include <nlohmann/json.hpp>

namespace sf4e { namespace netplay {
// Missing and invalid enum values migrate to Automatic, without narrowing
// an untrusted JSON integer before its range has been checked.
inline gamepads::ButtonPrompts ReadButtonPrompts(const nlohmann::json& saved) {
    const auto found = saved.find("buttonPrompts");
    if (found == saved.end() || !found->is_number_integer() || *found < 0 || *found > 3)
        return gamepads::ButtonPrompts::Automatic;
    return static_cast<gamepads::ButtonPrompts>(found->get<int>());
}
inline void WriteButtonPrompts(const PlayerPreferences& value, nlohmann::json& saved) {
    saved["buttonPrompts"] = static_cast<int>(value.buttonPrompts);
}
} }
