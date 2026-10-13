#include "network_completion.hxx"

// Prints the completion record exactly as a fixture does, so
// scripts/test-network-completion.ps1 validates the real emitter output.
int wmain(int argc, wchar_t** argv) {
    for (int i = 1; i < argc; ++i) {
        if (!sf4e::test::ParseNetworkCaseOption(argv[i])) return 2;
    }
    sf4e::test::CompleteNetworkCase();
    return 0;
}
