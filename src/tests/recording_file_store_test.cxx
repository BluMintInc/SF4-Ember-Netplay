// Named recordings on disk. A whole slot written as the panel writes it is
// larger than any settings file, so the recordings store reads and publishes
// under its own limit (RecordingFileBytes) while every other JSON store keeps
// the 256 KiB one (JsonFileStore.hxx).
#include "../netplay/JsonFileStore.hxx"
#include "../training/RecordingFile.hxx"
#include "test_support.hxx"
#include "temp_root.hxx"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

int main() {
    using namespace sf4e;
    namespace store = netplay::json_file;
    const auto root = MakeTempRoot(L"sf4e-recording-store-test-");
    std::filesystem::create_directories(root);
    // A full slot of varied frames, through the shared frame encoder.
    std::vector<training::Input> full;
    for (int frame = 0; frame < training::MaxFrames; ++frame) {
        const unsigned bits = static_cast<unsigned>(frame * 37) & training::FightButtons;
        full.push_back({bits, bits, static_cast<unsigned char>(frame % 3), static_cast<signed char>(frame % 7 - 3)});
    }
    const auto document = nlohmann::json::parse(training::ExportRecording(full), nullptr, false);
    CHECK(document.is_object() && document.dump(2).size() > store::MaximumBytes && document.dump(2).size() < training::RecordingFileBytes);
    std::string error, bytes;
    bool missing = false;
    // Saved and loaded by name above 256 KiB.
    CHECK(store::Publish(root, L"Full.json", document, error, training::RecordingFileBytes));
    CHECK(std::filesystem::file_size(root / "Full.json") > store::MaximumBytes);
    CHECK(store::ReadBytes(root / "Full.json", bytes, missing, error, training::RecordingFileBytes) && !missing);
    std::vector<training::Input> loaded;
    CHECK(training::ImportRecording(bytes, loaded, error) && loaded.size() == full.size());
    for (std::size_t frame = 0; frame < full.size(); ++frame)
        CHECK(loaded[frame].raw == full[frame].raw && loaded[frame].mapped == full[frame].mapped &&
            loaded[frame].wait == full[frame].wait && loaded[frame].offset == full[frame].offset);
    // Other stores keep their limit: the same file is too large to read
    // there, and they refuse to publish it.
    CHECK(!store::ReadBytes(root / "Full.json", bytes, missing, error) && !error.empty());
    error.clear();
    CHECK(!store::Publish(root, L"settings.json", document, error) && !error.empty() && !std::filesystem::exists(root / "settings.json"));
    // Over 4 MiB a recording is refused both ways.
    auto oversized = document;
    oversized["padding"] = std::string(training::RecordingFileBytes, 'x');
    error.clear();
    CHECK(!store::Publish(root, L"Over.json", oversized, error, training::RecordingFileBytes) && !error.empty() && !std::filesystem::exists(root / "Over.json"));
    {
        std::ofstream file(root / "Over.json", std::ios::binary);
        file << std::string(training::RecordingFileBytes + 1, ' ');
    }
    error.clear();
    CHECK(!store::ReadBytes(root / "Over.json", bytes, missing, error, training::RecordingFileBytes) && !error.empty());
    RemoveTempRoot(root);
    std::puts("Recording file store checks passed.");
    return 0;
}
