#pragma once
#include "ButtonLayout.hxx"
#include <nlohmann/json.hpp>

namespace sf4e { namespace gamepads {
inline nlohmann::json LayoutsToJson(const std::vector<StoredLayout>& layouts) {
    auto result = nlohmann::json::array();
    for (const auto& layout : layouts) result.push_back({{"guid", layout.identity.guid}, {"serial", layout.identity.serial},
        {"path", layout.identity.path}, {"name", layout.identity.name}, {"buttons", layout.buttons}, {"edited", layout.edited}});
    return result;
}
struct LayoutLoadError {
    enum class Reason { Length, Mask, Integer } reason;
    std::uint64_t mask = 0;
};
template<class Rejected> std::vector<StoredLayout> ReadLayouts(const nlohmann::json& values, const Rejected& rejected) {
    std::vector<StoredLayout> layouts;
    const auto entries = values.find("buttonLayouts");
    if (entries == values.end() || !entries->is_array()) return layouts;
    unsigned index = 0;
    for (const auto& entry : *entries) {
        const unsigned current = index++;
        if (!entry.is_object() || !entry.contains("buttons") || !entry["buttons"].is_array() || entry["buttons"].size() != 10) {
            rejected(current, LayoutLoadError{LayoutLoadError::Reason::Length}); continue;
        }
        StoredLayout layout;
        bool valid = true;
        for (unsigned i = 0; i < 10; ++i) {
            const auto& button = entry["buttons"][i];
            if (!button.is_number_integer()) {
                rejected(current, LayoutLoadError{LayoutLoadError::Reason::Integer}); valid = false; break;
            }
            const auto mask = button.get<std::uint64_t>();
            if (mask & ~std::uint64_t(FightMask)) {
                rejected(current, LayoutLoadError{LayoutLoadError::Reason::Mask, mask}); valid = false; break;
            }
            layout.buttons[i] = static_cast<std::uint32_t>(mask);
        }
        if (!valid) continue;
        const auto text = [&](const char* key) { const auto field = entry.find(key); return field != entry.end() && field->is_string() ? field->get<std::string>() : std::string{}; };
        layout.identity = {text("guid"), text("serial"), text("path"), text("name")};
        if (entry.contains("edited") && entry["edited"].is_number_integer()) layout.edited = entry["edited"].get<std::int64_t>();
        UpsertLayout(layouts, std::move(layout));
    }
    return layouts;
}
} }
