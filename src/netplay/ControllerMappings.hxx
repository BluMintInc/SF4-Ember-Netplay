#pragma once
#include <map>
#include <string>
#include <nlohmann/json.hpp>
namespace sf4e { namespace netplay {
inline std::map<std::string, std::string> ReadControllerMappings(const nlohmann::json& values) {
    std::map<std::string, std::string> mappings;
    const auto found = values.find("controllerMappings");
    if (found != values.end() && found->is_object())
        for (const auto& entry : found->items())
            if (entry.value().is_string()) mappings.emplace(entry.key(), entry.value().get<std::string>());
    return mappings;
}
} }
