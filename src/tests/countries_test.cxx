// The country table (Countries.hxx) against everything generated from the
// same manifest: the atlas and its index, a name in every catalog, the package
// inventory and the copies a build stages. Then CountryCode and the country
// preference: what each accepts, and what Automatic resolves to.
// Arguments: the source folder, then the folder Sidecar.dll is built in.
#include "../common/Country.hxx"
#include "../common/Localization.hxx"
#include "../platform/LocaleWindows.hxx"
#include "test_support.hxx"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

using namespace sf4e;
namespace fs = std::filesystem;

static std::string Read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text; text << file.rdbuf();
    return text.str();
}
static std::uint32_t BigEndian(const std::string& bytes, std::size_t at) {
    return (std::uint32_t(std::uint8_t(bytes[at])) << 24) | (std::uint32_t(std::uint8_t(bytes[at + 1])) << 16) |
        (std::uint32_t(std::uint8_t(bytes[at + 2])) << 8) | std::uint32_t(std::uint8_t(bytes[at + 3]));
}

static void TableMatchesAtlas(const fs::path& root) {
    // One row per code in code order, its index its position, its key its code.
    CHECK(countries::Count == 249);
    for (int i = 0; i < countries::Count; ++i) {
        const auto& row = countries::Table[i];
        CHECK(row.index == i && std::string(row.key) == std::string("country.") + row.code);
        CHECK(row.code[0] >= 'A' && row.code[0] <= 'Z' && row.code[1] >= 'A' && row.code[1] <= 'Z' && !row.code[2]);
        if (i) CHECK(std::string(countries::Table[i - 1].code) < row.code);
    }
    CHECK(countries::Count <= countries::Columns * countries::Rows);
    // flags.json: the same codes at the same cells, and the same layout.
    const auto index = nlohmann::json::parse(Read(root / "assets" / "flags" / "flags.json"));
    CHECK(index.at("countries").size() == static_cast<std::size_t>(countries::Count));
    for (const auto& row : countries::Table) CHECK(index.at("countries").at(row.code).get<int>() == row.index);
    CHECK(index.at("cellWidth") == countries::CellWidth && index.at("cellHeight") == countries::CellHeight &&
        index.at("padding") == countries::Padding && index.at("columns") == countries::Columns &&
        index.at("width") == countries::AtlasWidth && index.at("height") == countries::AtlasHeight);
    // flags.png: a PNG whose header gives the size the layout needs.
    const auto png = Read(root / "assets" / "flags" / "flags.png");
    CHECK(png.size() > 24 && png.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0 && png.compare(12, 4, "IHDR") == 0);
    CHECK(BigEndian(png, 16) == static_cast<std::uint32_t>(countries::AtlasWidth) && BigEndian(png, 20) == static_cast<std::uint32_t>(countries::AtlasHeight));
    CHECK(Read(root / "assets" / "flags" / "LICENSE").compare(0, 15, "The MIT License") == 0);
}

static void EveryCatalogNamesEveryCountry(const fs::path& root) {
    for (int i = 0; i < static_cast<int>(loc::Locale::Count); ++i) {
        const auto tag = loc::Tag(static_cast<loc::Locale>(i));
        loc::Catalog catalog; std::string error;
        CHECK(loc::testing::ParsePo(Read(root / "locales" / (std::string(tag) + ".po")), catalog, error));
        for (const auto& row : countries::Table) {
            const auto found = catalog.find(row.key);
            if (found == catalog.end() || found->second.empty() || found->second == row.code) {
                std::cerr << tag << " has no name for " << row.code << '\n';
                std::exit(1);
            }
        }
    }
}

static void InventoryAndStagedCopies(const fs::path& root, const fs::path& built) {
    // Packaging, preflight and the updater require both the atlas and its index.
    const auto inventory = Read(root / "src" / "common" / "PackageInventory.inc");
    for (const char* path : {"assets\\\\flags\\\\flags.png", "assets\\\\flags\\\\flags.json", "assets\\\\flags\\\\LICENSE", "notices\\\\flag-icons-MIT.txt"})
        CHECK(inventory.find(std::string("SF4E_PACKAGE_REQUIRED(\"") + path + "\")") != std::string::npos);
    // The build stages the same bytes beside Sidecar.dll, with the licence among the notices.
    for (const char* name : {"flags.png", "flags.json", "LICENSE"})
        CHECK(Read(built / "assets" / "flags" / name) == Read(root / "assets" / "flags" / name));
    CHECK(Read(built / "notices" / "flag-icons-MIT.txt") == Read(root / "assets" / "flags" / "LICENSE"));
}

static void CodesValidate() {
    CountryCode japan;
    CHECK(CountryCode::Parse("JP", japan) && japan.Present() && japan.ToString() == "JP" && std::string(japan.Key()) == "country.JP");
    CHECK(countries::Table[japan.Index()].code == std::string("JP"));
    // Every row parses to itself; nothing else does, and a refusal leaves the code alone.
    for (const auto& row : countries::Table) CHECK(CountryCode::Parsed(row.code).Index() == row.index);
    for (const char* refused : {"jp", "Jp", "J", "JPN", "", "XK", "ZZ", "AA", "A1", "UK", "EU", " JP", "JP "}) {
        CountryCode kept = japan;
        CHECK(!CountryCode::Parse(refused, kept) && kept == japan && !CountryCode::Parsed(refused).Present());
    }
    CHECK(!CountryCode().Present() && CountryCode().ToString().empty() && !CountryCode().Key());
    CHECK(CountryCode::AtIndex(0).ToString() == "AD" && CountryCode::AtIndex(countries::Count - 1).ToString() == "ZW");
    CHECK(!CountryCode::AtIndex(-1).Present() && !CountryCode::AtIndex(countries::Count).Present());
}

static void PreferenceAndAutomatic() {
    const auto japan = CountryCode::Parsed("JP"), canada = CountryCode::Parsed("CA");
    // Saved as "auto", "hidden" or the code; anything else is Automatic.
    CHECK(CountryPreference::Parsed("auto") == CountryPreference::Automatic());
    CHECK(CountryPreference::Parsed("hidden") == CountryPreference::Hidden());
    CHECK(CountryPreference::Parsed("JP") == CountryPreference::Of(japan));
    for (const char* other : {"", "jp", "XK", "Hidden", "none", "JPN"}) CHECK(CountryPreference::Parsed(other) == CountryPreference::Automatic());
    for (const auto& preference : {CountryPreference::Automatic(), CountryPreference::Hidden(), CountryPreference::Of(japan)})
        CHECK(preference.Valid() && CountryPreference::Parsed(preference.ToString()) == preference);
    CountryPreference broken; broken.kind = CountryPreference::Kind::Code;
    CHECK(!broken.Valid() && CountryPreference::Of(CountryCode()) == CountryPreference::Automatic());
    // Automatic shows Windows' country, a pick its own, Hidden none.
    CHECK(ShownCountry(CountryPreference::Automatic(), canada) == canada);
    CHECK(!ShownCountry(CountryPreference::Automatic(), CountryCode()).Present());
    CHECK(ShownCountry(CountryPreference::Of(japan), canada) == japan);
    CHECK(!ShownCountry(CountryPreference::Hidden(), canada).Present());
    // Windows' GEO_ISO2 text: a two-letter code of the table, in either case.
    CHECK(CountryFromGeoIso2(L"CA") == canada && CountryFromGeoIso2(L"jp") == japan);
    for (const wchar_t* none : {L"", L"XK", L"001", L"C", L"CAN", L"C1", L"ÉS"}) CHECK(!CountryFromGeoIso2(none).Present());
    // This PC's own answer is none or a code of the table.
    const auto local = platform::WindowsCountry();
    CHECK(!local.Present() || CountryCode::Parsed(local.ToString()) == local);
    std::cout << "Windows region country: " << (local.Present() ? local.ToString() : std::string("none")) << '\n';
}

int main(int argc, char** argv) {
    CHECK(argc == 3);
    const fs::path root = argv[1], built = argv[2];
    TableMatchesAtlas(root);
    EveryCatalogNamesEveryCountry(root);
    InventoryAndStagedCopies(root, built);
    CodesValidate();
    PreferenceAndAutomatic();
    std::cout << "Countries test passed\n";
    return 0;
}
