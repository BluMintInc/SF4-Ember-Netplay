#pragma once
#include "PlayerPreferences.hxx"
#include <nlohmann/json.hpp>

namespace sf4e { namespace netplay {
// The saved country: "auto", "hidden" or a code of the table. A profile from
// before flags has none, and anything else (a code the table lacks, lower case,
// not text) is Automatic; the rest of the profile loads as saved.
inline CountryPreference ReadCountryPreference(const nlohmann::json& saved) {
    if (!saved.is_object()) return {};
    const auto found = saved.find("country");
    if (found == saved.end() || !found->is_string()) return {};
    return CountryPreference::Parsed(found->get<std::string>());
}
inline void WriteCountryPreference(const PlayerPreferences& value, nlohmann::json& saved) {
    saved["country"] = value.country.ToString();
}
} }
