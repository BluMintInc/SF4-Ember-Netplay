#pragma once
#include <iostream>
#include <string>

namespace sf4e { namespace test {
// scripts/test-current-network.ps1 names the planned case with
// --network-case=<name>, and each fixture echoes it back once after its final
// success line, so the record says what was run. A manual run without the
// option emits nothing and so never satisfies the runner.
inline std::string& NetworkCase() {
    static std::string name;
    return name;
}

// True for a well-formed --network-case option, which it records. A second
// option or a name outside [a-z-] is refused so the fixture rejects it.
inline bool ParseNetworkCaseOption(const std::wstring& option) {
    const std::wstring prefix = L"--network-case=";
    if (option.compare(0, prefix.size(), prefix) != 0 || option.size() == prefix.size() || !NetworkCase().empty()) return false;
    std::string name;
    for (std::size_t i = prefix.size(); i < option.size(); ++i) {
        if ((option[i] < L'a' || option[i] > L'z') && option[i] != L'-') return false;
        name += static_cast<char>(option[i]);
    }
    NetworkCase() = name;
    return true;
}

// Canonical version 1 completion record. scripts/NetworkCompletion.ps1 reads
// this record only after checking process exit and unchanged artifact hashes.
inline void CompleteNetworkCase() {
    if (NetworkCase().empty()) return;
    std::cout << "EMBER_NETWORK_RESULT {\"version\":1,\"case\":\"" << NetworkCase()
        << "\",\"status\":\"success\"}" << std::endl;
}
} }
