// The country picker's rows, search, letter jump, Left and Right, and the
// choice it hands back, in English and in German.
#include "../ui/CountryPicker.hxx"
#include "../common/Localization.hxx"
#include "test_support.hxx"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace sf4e;
using namespace sf4e::ui;

static std::vector<std::string> CountryIds(const std::vector<MenuEntry>& rows) {
    std::vector<std::string> ids;
    for (const auto& row : rows) if (row.id.size() == 10 && row.id.compare(0, 8, "country-") == 0) ids.push_back(row.id);
    return ids;
}
static const MenuEntry& RowOf(const std::vector<MenuEntry>& rows, const std::string& id) {
    const auto found = std::find_if(rows.begin(), rows.end(), [&](const MenuEntry& e) { return e.id == id; });
    CHECK(found != rows.end());
    return *found;
}

int main() {
    loc::SetActive(loc::Locale::En);
    CountryPicker picker; picker.Open();
    const auto canada = CountryCode::Parsed("CA");
    auto rows = picker.Rows(CountryPreference::Automatic(), canada, true);
    // Search, Automatic (naming Windows' country) and Hidden lead, then every country by name.
    CHECK(rows.size() == 3 + countries::Count && rows[0].id == "country-search" && rows[0].text);
    CHECK(rows[1].id == "country-auto" && rows[1].label == "Automatic (Canada)" && rows[1].valueHighlighted);
    CHECK(rows[2].id == "country-hidden" && !rows[2].valueHighlighted);
    auto ids = CountryIds(rows);
    CHECK(ids.size() == static_cast<std::size_t>(countries::Count) && ids.front() == "country-AF");
    // Accents sort with their letter: the Aland Islands come among the A's, not after Z.
    const auto aland = std::find(ids.begin(), ids.end(), "country-AX");
    CHECK(aland != ids.end() && aland - ids.begin() < 20);
    CHECK(picker.Rows(CountryPreference::Automatic(), CountryCode(), true)[1].label == "Automatic");
    // The saved choice is marked; without the right to edit, nothing can be chosen.
    rows = picker.Rows(CountryPreference::Of(CountryCode::Parsed("JP")), canada, true);
    CHECK(RowOf(rows, "country-JP").valueHighlighted && !rows[1].valueHighlighted);
    rows = picker.Rows(CountryPreference::Hidden(), canada, false);
    CHECK(!rows[1].enabled && !rows[2].enabled && !RowOf(rows, "country-JP").enabled && rows[2].valueHighlighted);

    // Left goes to the start of this letter, then to the start of the one before;
    // Right to the next letter.
    rows = picker.Rows(CountryPreference::Automatic(), canada, true);
    ids = CountryIds(rows);
    const auto japan = std::find(ids.begin(), ids.end(), "country-JP") - ids.begin();
    const auto jamaica = std::find(ids.begin(), ids.end(), "country-JM") - ids.begin();
    CHECK(jamaica < japan && RowOf(rows, "country-JP").left == "country-JM");
    CHECK(RowOf(rows, "country-JM").left == "country-IS");
    CHECK(RowOf(rows, "country-JP").right == "country-KZ");
    CHECK(RowOf(rows, "country-AF").left.empty() && RowOf(rows, "country-ZW").right.empty());

    // A typed letter goes to the first country with it, and again to the next; case and accents do not matter.
    CHECK(picker.Jump('j', "country-search") == "country-JM");
    CHECK(picker.Jump('J', "country-JM") == "country-JP");
    CHECK(picker.Jump('j', "country-JO") == "country-JM");
    CHECK(picker.Jump(0xC5, "country-auto") == "country-AF");
    CHECK(picker.Jump('1', "country-JP").empty() && picker.Jump(0, "country-JP").empty());

    // The search keeps names that hold the text, ignoring case and accents, and a code typed in full.
    picker.Search("japan");
    rows = picker.Rows(CountryPreference::Automatic(), canada, true);
    CHECK(CountryIds(rows) == std::vector<std::string>{"country-JP"} && rows[0].label == "Search: japan");
    picker.Search("cote");
    CHECK(CountryIds(picker.Rows(CountryPreference::Automatic(), canada, true)) == std::vector<std::string>{"country-CI"});
    picker.Search("gb");
    CHECK(CountryIds(picker.Rows(CountryPreference::Automatic(), canada, true)) == std::vector<std::string>{"country-GB"});
    picker.Search("guinea");
    CHECK(CountryIds(picker.Rows(CountryPreference::Automatic(), canada, true)).size() == 4);
    picker.Search("xyzzy");
    rows = picker.Rows(CountryPreference::Automatic(), canada, true);
    CHECK(CountryIds(rows).empty() && rows.back().id == "country-none" && rows.back().info);
    CHECK(picker.Jump('j', "country-search").empty());
    // Opening again starts with no search.
    picker.Open();
    CHECK(picker.SearchText().empty() && CountryIds(picker.Rows(CountryPreference::Automatic(), canada, true)).size() == static_cast<std::size_t>(countries::Count));

    // Select hands back a typed choice, and only for a choice row.
    CHECK(picker.Choose("country-auto") == CountryPreference::Automatic());
    CHECK(picker.Choose("country-hidden") == CountryPreference::Hidden());
    CHECK(picker.Choose("country-NZ") == CountryPreference::Of(CountryCode::Parsed("NZ")));
    for (const char* other : {"country-search", "country-none", "country-XK", "country-nz", "retry-save", ""}) CHECK(!picker.Choose(other));
    for (const auto& choice : {CountryPreference::Automatic(), CountryPreference::Hidden(), CountryPreference::Of(CountryCode::Parsed("NZ"))})
        CHECK(picker.Choose(CountryPicker::RowId(choice)) == choice);
    CHECK(CountryPicker::ChoiceText(CountryPreference::Of(CountryCode::Parsed("NZ")), canada) == "New Zealand");
    CHECK(CountryPicker::ChoiceText(CountryPreference::Hidden(), canada) == "Hidden");
    CHECK(CountryPicker::ChoiceText(CountryPreference::Automatic(), canada) == "Automatic (Canada)");

    // In German the names, their order and the letters are German.
    loc::SetActive(loc::Locale::De);
    rows = picker.Rows(CountryPreference::Automatic(), canada, true);
    CHECK(RowOf(rows, "country-DE").label == "Deutschland" && rows[1].label == "Automatisch (Kanada)");
    CHECK(picker.Jump('d', "country-search") == "country-DK");
    CHECK(picker.Jump(0xD6, "country-search") == "country-OM" && picker.Jump(0xD6, "country-OM") == "country-AT");
    std::puts("CountryPicker test passed");
    return 0;
}
