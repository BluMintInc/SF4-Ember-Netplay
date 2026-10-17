#pragma once
#include <algorithm>
#include <string>

namespace sf4e {
// An iroh endpoint ID as the helper writes it: 64 lowercase hex characters.
inline bool IsEndpointIdentity(const std::string& identity) {
    return identity.size() == 64 && std::all_of(identity.begin(), identity.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
}
