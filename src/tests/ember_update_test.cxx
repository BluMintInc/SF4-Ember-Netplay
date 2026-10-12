#include "../launcher/update/github_release_client.hxx"
#include "../launcher/update/ZipExtract.hxx"
#include <zlib.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <iostream>

#include "test_support.hxx"

namespace {
// A minimal zip writer: stored or raw-deflate entries, central directory, no zip64.
struct ZipBuilder {
    std::string bytes, directory; unsigned entries = 0;
    void Add(const std::string& name, const std::string& data, bool deflated) {
        std::string packed = data;
        if (deflated) {
            z_stream z{}; deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
            packed.resize(deflateBound(&z, static_cast<uLong>(data.size())));
            z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data())); z.avail_in = static_cast<uInt>(data.size());
            z.next_out = reinterpret_cast<Bytef*>(packed.data()); z.avail_out = static_cast<uInt>(packed.size());
            deflate(&z, Z_FINISH); packed.resize(z.total_out); deflateEnd(&z);
        }
        const auto crc = crc32(0, reinterpret_cast<const Bytef*>(data.data()), static_cast<uInt>(data.size()));
        const auto u16 = [](unsigned v) { return std::string{char(v & 255), char(v >> 8)}; };
        const auto u32 = [&](unsigned long v) { return u16(unsigned(v & 0xffff)) + u16(unsigned(v >> 16)); };
        const std::string common = u16(20) + u16(0) + u16(deflated ? 8 : 0) + u32(0) + u32(crc) + u32(packed.size()) + u32(data.size()) + u16(unsigned(name.size())) + u16(0);
        directory += u32(0x02014b50) + u16(20) + common + u16(0) + u16(0) + u16(0) + u32(0) + u32(bytes.size()) + name;
        bytes += u32(0x04034b50) + common + name + packed;
        ++entries;
    }
    void Write(const std::filesystem::path& path) {
        const auto u16 = [](unsigned v) { return std::string{char(v & 255), char(v >> 8)}; };
        const auto u32 = [&](unsigned long v) { return u16(unsigned(v & 0xffff)) + u16(unsigned(v >> 16)); };
        std::ofstream(path, std::ios::binary) << bytes << directory << u32(0x06054b50) << u16(0) << u16(0) << u16(entries) << u16(entries)
            << u32(directory.size()) << u32(bytes.size()) << u16(0);
    }
};
std::string Slurp(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary); return std::string(std::istreambuf_iterator<char>(in), {});
}
}

int main(int argc, char** argv) {
    using namespace sf4e::launcher;
    if (argc == 4 && std::string(argv[1]) == "--extract") { // Manual check on a real package zip.
        const auto error = ExtractZip(std::filesystem::path(argv[2]).wstring(), std::filesystem::path(argv[3]).wstring());
        std::cout << (error.empty() ? "extracted" : error) << "\n"; return error.empty() ? 0 : 1;
    }
    CHECK(std::string(kDefaultGithubRepo) == "Confetti3/SF4-Ember-Netplay");
    const std::string digest(64, 'a');
    nlohmann::json release = {
        {"tag_name", "v0.8.3"}, {"body", "Ember 0.8.3"},
        {"html_url", "https://github.com/Confetti3/SF4-Ember-Netplay/releases/tag/v0.8.3"},
        {"assets", nlohmann::json::array({
            {{"name", "sf4-netplay-launcher-0.6.5.zip"}, {"browser_download_url", "legacy"}},
            {{"name", "sf4-ember-netplay-0.8.3.zip.sha256"}, {"browser_download_url", "checksum"}},
            {{"name", "upgrade-sf4-ember-netplay-0.8.2-to-0.8.3.zip"}, {"browser_download_url", "incremental"},
             {"url", "upgrade-api"}, {"digest", "sha256:" + digest}},
            {{"name", "sf4-ember-netplay-0.8.3.zip"}, {"browser_download_url", "branded"},
             {"url", "api-asset"}, {"digest", "sha256:" + digest}}
        })}
    };
    const auto one = [&](const char* installed, UpdateChannel channel = UpdateChannel::Stable) {
        return ParseGithubReleases(nlohmann::json::array({release}).dump(), installed, channel);
    };
    auto result = one("0.8.2");
    CHECK(result.ok && result.updateAvailable && result.latestVersion == "v0.8.3");
    CHECK(result.zipDownloadUrl == "branded" && result.zipApiUrl == "api-asset");
    CHECK(result.expectedSha256 == digest);
    CHECK(!one("0.8.3").updateAvailable);
    CHECK(!one("0.9.0").updateAvailable);
    CHECK(one("dev").updateAvailable); // A build without a version label is offered the best release.
    release["assets"][3].erase("digest");
    CHECK(one("0.8.2").expectedSha256.empty());
    release["assets"].erase(3);
    CHECK(!one("0.8.2").ok);
    CHECK(!ParseGithubReleases("invalid json", "0.8.3", UpdateChannel::Stable).ok);
    CHECK(!ParseGithubReleases("{}", "0.8.3", UpdateChannel::Stable).ok);
    CHECK(!ParseGithubReleases("[]", "0.8.3", UpdateChannel::Stable).ok);
    CHECK(!ParseGithubReleases("{\"message\":\"rate limited\"}", "0.8.3", UpdateChannel::Stable).ok);

    // Versions: a pre-release sorts before its finished release, then by word,
    // number and the rest; anything of another shape is not a version.
    const auto before = [](const char* a, const char* b) {
        const auto x = ParseVersion(a), y = ParseVersion(b);
        return x && y && CompareVersions(*x, *y) < 0 && CompareVersions(*y, *x) > 0;
    };
    CHECK(before("1.1.0-rc1", "v1.1.0") && before("1.0.2", "1.1.0-rc1") && before("v1.1.0", "1.1.1"));
    CHECK(before("1.1.0-rc1", "1.1.0-rc2") && before("1.1.0-rc9", "1.1.0-rc10") && before("1.1.0-beta3", "1.1.0-rc1"));
    CHECK(before("1.1.0-rc1", "1.1.0-rc1junk") && CompareVersions(*ParseVersion("v1.1.0-RC1"), *ParseVersion("1.1.0-rc1")) == 0);
    CHECK(ParseVersion("1.1.0-links-sets-test1") && ParseVersion("1.1.0-links-sets-test1")->prerelease);
    for (const char* bad : {"latest", "v1.1.0-", "1.1", "1.1.0.0", "1.1.0 ", "v1.1.0-rc 1", "1234567890.0.0", "", "rc1"}) CHECK(!ParseVersion(bad));
    CHECK(!ParseVersion(nullptr));

    // The chosen channel, or the installed version's kind while none is chosen.
    CHECK(ResolveUpdateChannel("", "1.0.2") == UpdateChannel::Stable && ResolveUpdateChannel("", "1.1.0-rc1") == UpdateChannel::Prerelease);
    CHECK(ResolveUpdateChannel("", "1.1.0-links-sets-test1") == UpdateChannel::Prerelease && ResolveUpdateChannel("", "dev") == UpdateChannel::Stable);
    CHECK(ResolveUpdateChannel("stable", "1.1.0-rc1") == UpdateChannel::Stable && ResolveUpdateChannel("prerelease", "1.0.2") == UpdateChannel::Prerelease);
    CHECK(std::string(UpdateChannelName(UpdateChannel::Stable)) == "stable" && std::string(UpdateChannelName(UpdateChannel::Prerelease)) == "prerelease");

    // Each channel takes its highest listed release with a package: Stable
    // finished ones only, Pre-release either kind. Drafts, packageless
    // releases and tags that are not versions are skipped.
    release["assets"].push_back({{"name", "sf4-ember-netplay-x.zip"}, {"browser_download_url", "branded"}, {"digest", "sha256:" + digest}});
    const auto listed = [&](std::initializer_list<const char*> tags) {
        auto list = nlohmann::json::array();
        for (const char* tag : tags) { release["tag_name"] = tag; list.push_back(release); }
        return list;
    };
    auto list = listed({"v1.1.0-rc1", "v1.0.2", "latest", "v1.1.0-rc2", "v1.0.1"});
    result = ParseGithubReleases(list.dump(), "1.0.2", UpdateChannel::Prerelease);
    CHECK(result.ok && result.updateAvailable && result.latestVersion == "v1.1.0-rc2" && result.expectedSha256 == digest);
    CHECK(!ParseGithubReleases(list.dump(), "1.1.0-rc2", UpdateChannel::Prerelease).updateAvailable);
    result = ParseGithubReleases(list.dump(), "1.0.1", UpdateChannel::Stable);
    CHECK(result.ok && result.updateAvailable && result.latestVersion == "v1.0.2");
    // Stable offers an installed pre-release its best release as the way back,
    // and says so; a finished install and the pre-release channel never go back.
    result = ParseGithubReleases(list.dump(), "1.1.0-rc1", UpdateChannel::Stable);
    CHECK(result.ok && result.updateAvailable && result.goesBack && result.latestVersion == "v1.0.2");
    CHECK(!ParseGithubReleases(list.dump(), "1.0.2", UpdateChannel::Stable).goesBack);
    result = ParseGithubReleases(listed({"v1.0.2"}).dump(), "1.1.0", UpdateChannel::Stable);
    CHECK(result.ok && !result.updateAvailable && !result.goesBack);
    CHECK(!ParseGithubReleases(list.dump(), "1.1.0-rc1", UpdateChannel::Prerelease).goesBack);
    // An install only takes the kind of change it was offered.
    CHECK(TransitionOffered("v1.1.0", "1.0.2", false) && !TransitionOffered("v1.1.0", "1.0.2", true));
    CHECK(TransitionOffered("v1.0.2", "1.1.0-rc1", true) && !TransitionOffered("v1.0.2", "1.1.0-rc1", false));
    CHECK(TransitionOffered("v1.1.0", "dev", false) && !TransitionOffered("v1.1.0", "dev", true) && !TransitionOffered("latest", "1.1.0-rc1", true));
    list = listed({"v1.1.0", "v1.1.0-rc2"});
    CHECK(ParseGithubReleases(list.dump(), "1.1.0-rc2", UpdateChannel::Prerelease).latestVersion == "v1.1.0");
    CHECK(ParseGithubReleases(list.dump(), "1.1.0-rc2", UpdateChannel::Stable).updateAvailable);
    list = listed({"v1.2.0-rc1", "v1.1.0", "v1.3.0-rc1"});
    list[0]["draft"] = true; list[2]["assets"] = nlohmann::json::array();
    CHECK(ParseGithubReleases(list.dump(), "1.0.2", UpdateChannel::Prerelease).latestVersion == "v1.1.0");
    CHECK(!ParseGithubReleases(listed({"latest", "nightly"}).dump(), "1.0.2", UpdateChannel::Prerelease).ok);
    // GitHub sends null for empty fields: one such release, before or after
    // the newest, spoils nothing (a null digest only leaves it unverifiable).
    list = listed({"v1.0.1", "v1.1.0", "v1.0.0"});
    list[0]["body"] = nullptr; list[2]["body"] = nullptr; list[2]["html_url"] = nullptr;
    for (auto& asset : list[0]["assets"]) asset["digest"] = nullptr;
    result = ParseGithubReleases(list.dump(), "1.0.0", UpdateChannel::Stable);
    CHECK(result.ok && result.latestVersion == "v1.1.0" && result.expectedSha256 == digest);
    list[1]["tag_name"] = nullptr;
    result = ParseGithubReleases(list.dump(), "1.0.0", UpdateChannel::Stable);
    CHECK(result.ok && result.latestVersion == "v1.0.1" && result.expectedSha256.empty());
    // Stable also skips a release GitHub marks as a pre-release, whatever its tag.
    list = listed({"v1.2.0", "v1.1.0"});
    list[0]["prerelease"] = true;
    CHECK(ParseGithubReleases(list.dump(), "1.0.2", UpdateChannel::Stable).latestVersion == "v1.1.0");
    CHECK(ParseGithubReleases(list.dump(), "1.0.2", UpdateChannel::Prerelease).latestVersion == "v1.2.0");
    if (argc > 1 && std::string(argv[1]) == "--live") {
        result = CheckForUpdate(UpdateChannel::Stable);
        CHECK(result.ok && !result.installedVersion.empty());
        CHECK(result.latestVersion == "v" + result.installedVersion && !result.updateAvailable);
        CHECK(result.expectedSha256.size() == 64);
        CHECK(result.zipDownloadUrl.find("sf4-ember-netplay-" + result.installedVersion + ".zip") != std::string::npos);
        CHECK(result.zipDownloadUrl.find("upgrade-sf4-ember-netplay-") == std::string::npos);
    }
    {
        const auto work = std::filesystem::temp_directory_path() / "ember-zip-extract-test";
        std::filesystem::remove_all(work); std::filesystem::create_directories(work);
        const std::string big(300000, 'x');
        ZipBuilder zip; zip.Add("pkg/", "", false); zip.Add("pkg/a.txt", "stored", false);
        zip.Add("pkg/sub/b.bin", big + "tail", true); zip.Add("pkg/empty", "", true);
        zip.Write(work / "ok.zip");
        std::uint64_t last = 0;
        CHECK(ExtractZip((work / "ok.zip").wstring(), (work / "out").wstring(), [&](std::uint64_t done, std::uint64_t) { last = done; return true; }).empty());
        CHECK(Slurp(work / "out/pkg/a.txt") == "stored" && Slurp(work / "out/pkg/sub/b.bin") == big + "tail" && Slurp(work / "out/pkg/empty").empty());
        CHECK(last > 0);
        CHECK(ExtractZip((work / "ok.zip").wstring(), (work / "cancel").wstring(), [](std::uint64_t, std::uint64_t) { return false; }) == "cancelled: pkg/a.txt");
        ZipBuilder evil; evil.Add("../escape.txt", "x", false); evil.Write(work / "evil.zip");
        CHECK(ExtractZip((work / "evil.zip").wstring(), (work / "out2").wstring()).find("unsafe entry name") == 0);
        CHECK(!std::filesystem::exists(work / "escape.txt"));
        ZipBuilder bad; bad.Add("c.txt", "hello", true); auto corrupt = bad; corrupt.bytes[corrupt.bytes.size() - 1] ^= 1; corrupt.Write(work / "bad.zip");
        CHECK(!ExtractZip((work / "bad.zip").wstring(), (work / "out3").wstring()).empty());
        std::filesystem::remove_all(work);
    }
    std::cout << "Ember release identity, asset selection, version comparison, channels and checksum parsing and zip extraction passed\n";
}
