#pragma once
#include <iostream>

namespace sf4e { namespace test {
// Canonical version 1 completion record. scripts/NetworkCompletion.ps1 reads
// this record only after checking process exit and unchanged artifact hashes.
inline void CompleteNetworkCase(const char* name) {
    std::cout << "EMBER_NETWORK_RESULT {\"version\":1,\"case\":\"" << name
        << "\",\"status\":\"success\"}" << std::endl;
}
} }
