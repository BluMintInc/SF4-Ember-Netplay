#pragma once
#include <string>
#include <nlohmann/json.hpp>

#include "EndpointIdentity.hxx"
#include "NetworkRoute.hxx"

namespace sf4e {
// Where a room member's latency endpoint can be reached: its endpoint ID and,
// when the helper knows it, the region code of its home relay. Never an address.
struct LatencyRoute {
    std::string endpoint;
    std::string relay;
    bool Valid() const { return IsEndpointIdentity(endpoint); }
    // A bad endpoint ID reads as no route; a relay that is not a known region
    // code ("other" included) reads as no hint, and the endpoint ID is kept.
    static LatencyRoute Parsed(const std::string& endpoint, const std::string& relay) {
        LatencyRoute route;
        if (!IsEndpointIdentity(endpoint)) return route;
        route.endpoint = endpoint;
        if (IsRelayRegionCode(relay)) route.relay = relay;
        return route;
    }
};

// {"endpoint": "<64 hex>", "relay": "use1"}, relay omitted without a hint, and
// {} for no route.
inline void to_json(nlohmann::json& value, const LatencyRoute& route) {
    value = nlohmann::json::object();
    if (!route.Valid()) return;
    value["endpoint"] = route.endpoint;
    if (IsRelayRegionCode(route.relay)) value["relay"] = route.relay;
}
// Never throws: anything malformed reads as no route, or as no hint.
inline void from_json(const nlohmann::json& value, LatencyRoute& route) {
    route = {};
    if (!value.is_object()) return;
    const auto endpoint = value.find("endpoint"), relay = value.find("relay");
    if (endpoint == value.end() || !endpoint->is_string()) return;
    route = LatencyRoute::Parsed(endpoint->get<std::string>(),
        relay != value.end() && relay->is_string() ? relay->get<std::string>() : std::string());
}
}
