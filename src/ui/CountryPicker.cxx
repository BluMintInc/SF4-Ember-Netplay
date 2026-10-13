#include "CountryPicker.hxx"
#include "CountryFlag.hxx"
#include "MenuRows.hxx"
#include "RoomCards.hxx"
#include "Theme.hxx"
#include "../common/Localization.hxx"
#include "../platform/Utf8.hxx"
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cfloat>

namespace sf4e { namespace ui {
namespace {
constexpr const char* RowPrefix = "country-";
// The interface language's locale for Windows' collation and search; the
// user's own when Windows does not know the tag.
std::wstring CollationLocale() {
    const std::string tag = loc::Tag(loc::Active());
    const std::wstring wide(tag.begin(), tag.end());
    return IsValidLocaleName(wide.c_str()) ? wide : std::wstring();
}
const wchar_t* LocaleOrUser(const std::wstring& locale) { return locale.empty() ? LOCALE_NAME_USER_DEFAULT : locale.c_str(); }
// The letter a name or a key press files under: its first character, upper
// case, without accents, so "Österreich" goes with O and "é" types E.
wchar_t Initial(const std::wstring& text) {
    if (text.empty()) return 0;
    wchar_t folded[8] = {};
    const int length = FoldStringW(MAP_COMPOSITE, text.c_str(), 1, folded, 8);
    wchar_t letter = length > 0 ? folded[0] : text[0];
    CharUpperBuffW(&letter, 1);
    return letter;
}
bool Contains(const std::wstring& locale, const std::wstring& text, const std::wstring& part) {
    if (FindNLSStringEx(LocaleOrUser(locale), FIND_FROMSTART | LINGUISTIC_IGNORECASE | LINGUISTIC_IGNOREDIACRITIC,
        text.c_str(), -1, part.c_str(), -1, nullptr, nullptr, nullptr, 0) >= 0) return true;
    // Where Windows' search is missing (an older Wine), plain case folding.
    std::wstring a = text, b = part;
    if (!a.empty()) CharUpperBuffW(&a[0], static_cast<DWORD>(a.size()));
    if (!b.empty()) CharUpperBuffW(&b[0], static_cast<DWORD>(b.size()));
    return a.find(b) != std::wstring::npos;
}
CountryCode RowCountry(const std::string& id) {
    return id.size() == 10 && id.compare(0, 8, RowPrefix) == 0 ? CountryCode::Parsed(id.substr(8)) : CountryCode();
}
}

void CountryPicker::Open() { search_.clear(); }
void CountryPicker::Search(std::string text) { search_ = std::move(text); }

const std::vector<CountryPicker::Name>& CountryPicker::Names() const {
    const std::string key = std::string(loc::Tag(loc::Active())) + "/" + loc::T(countries::Table[0].key);
    if (key == namesKey_ && !names_.empty()) return names_;
    names_.clear();
    for (int i = 0; i < countries::Count; ++i) {
        Name name;
        name.code = CountryCode::AtIndex(i);
        name.text = loc::T(countries::Table[i].key);
        name.wide = platform::Utf8ToWide(name.text.c_str());
        name.initial = Initial(name.wide);
        names_.push_back(std::move(name));
    }
    const auto locale = CollationLocale();
    std::stable_sort(names_.begin(), names_.end(), [&](const Name& a, const Name& b) {
        const int order = CompareStringEx(LocaleOrUser(locale), LINGUISTIC_IGNORECASE, a.wide.c_str(), -1, b.wide.c_str(), -1, nullptr, nullptr, 0);
        return order ? order == CSTR_LESS_THAN : a.wide < b.wide;
    });
    namesKey_ = key;
    return names_;
}

std::vector<const CountryPicker::Name*> CountryPicker::Shown() const {
    std::vector<const Name*> shown;
    const auto part = platform::Utf8ToWide(search_.c_str());
    const auto locale = CollationLocale();
    // Two letters may also be a code: "jp" finds Japan.
    std::string upper = search_;
    for (auto& c : upper) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    const auto code = CountryCode::Parsed(upper);
    for (const auto& name : Names()) {
        if (!part.empty() && !Contains(locale, name.wide, part) && name.code != code) continue;
        shown.push_back(&name);
    }
    return shown;
}

std::string CountryPicker::RowId(const CountryPreference& choice) {
    switch (choice.kind) {
    case CountryPreference::Kind::Code: return RowPrefix + choice.code.ToString();
    case CountryPreference::Kind::Hidden: return "country-hidden";
    default: return "country-auto";
    }
}

std::string CountryPicker::ChoiceText(const CountryPreference& choice, const CountryCode& automatic) {
    switch (choice.kind) {
    case CountryPreference::Kind::Code: return CountryName(choice.code);
    case CountryPreference::Kind::Hidden: return loc::T("settings.country.hidden");
    default: return automatic.Present() ? loc::Tf("settings.country.automatic_value", CountryName(automatic)) : std::string(loc::T("settings.country.automatic"));
    }
}

std::vector<MenuEntry> CountryPicker::Rows(const CountryPreference& current, const CountryCode& automatic, bool enabled) const {
    const char* reason = loc::T("settings.leave_room_to_edit");
    std::vector<MenuEntry> rows;
    auto search = TextRow("country-search", search_.empty() ? std::string(loc::T("country.search")) : loc::Tf("country.search_value", search_), search_, 40);
    search.detail = loc::T("country.search_detail");
    rows.push_back(std::move(search));
    const auto choice = [&](std::string id, std::string label, std::string detail) {
        auto row = Row(std::move(id), std::move(label), enabled ? std::move(detail) : std::string(reason), enabled);
        row.valueHighlighted = row.id == RowId(current);
        // Country names reach the font atlas as names do (loc::InterfaceText).
        row.userText = row.id != "country-hidden";
        rows.push_back(std::move(row));
    };
    choice("country-auto", automatic.Present() ? loc::Tf("settings.country.automatic_value", CountryName(automatic)) : std::string(loc::T("settings.country.automatic")),
        automatic.Present() ? loc::Tf("country.automatic_detail", CountryName(automatic)) : std::string(loc::T("country.automatic_none")));
    choice("country-hidden", loc::T("settings.country.hidden"), loc::T("country.hidden_detail"));
    const auto shown = Shown();
    // Left and Right go along the initial letters: to the start of this
    // letter, then the one before; or to the next letter.
    for (std::size_t i = 0; i < shown.size(); ++i) {
        choice(RowPrefix + shown[i]->code.ToString(), shown[i]->text, loc::T("country.choose_detail"));
        std::size_t start = i;
        while (start > 0 && shown[start - 1]->initial == shown[i]->initial) --start;
        std::size_t previous = start;
        if (start == i && start > 0) { previous = start - 1; while (previous > 0 && shown[previous - 1]->initial == shown[start - 1]->initial) --previous; }
        std::size_t next = i + 1;
        while (next < shown.size() && shown[next]->initial == shown[i]->initial) ++next;
        if (previous != i) rows.back().left = RowPrefix + shown[previous]->code.ToString();
        if (next < shown.size()) rows.back().right = RowPrefix + shown[next]->code.ToString();
    }
    if (shown.empty()) rows.push_back(InfoRow("country-none", loc::T("country.none"), "", loc::T("country.none_detail")));
    return rows;
}

std::string CountryPicker::Jump(unsigned typed, const std::string& focused) const {
    if (!typed || typed > 0xFFFF) return {};
    const wchar_t letter = Initial(std::wstring(1, static_cast<wchar_t>(typed)));
    const auto shown = Shown();
    const auto current = RowCountry(focused);
    std::size_t from = 0;
    for (std::size_t i = 0; i < shown.size(); ++i) if (shown[i]->code == current) { from = i + 1; break; }
    // From after the focused country, round to the top: pressing the letter
    // again goes on through the countries that start with it.
    for (std::size_t step = 0; step < shown.size(); ++step) {
        const auto* name = shown[(from + step) % shown.size()];
        if (name->initial == letter) return RowPrefix + name->code.ToString();
    }
    return {};
}

std::optional<CountryPreference> CountryPicker::Choose(const std::string& id) const {
    if (id == "country-auto") return CountryPreference::Automatic();
    if (id == "country-hidden") return CountryPreference::Hidden();
    const auto code = RowCountry(id);
    if (code.Present()) return CountryPreference::Of(code);
    return std::nullopt;
}

bool CountryPicker::DrawRow(const MenuEntry& e, const CountryCode& automatic, ImVec2 min, ImVec2 max) const {
    const auto code = e.id == "country-auto" ? automatic : RowCountry(e.id);
    if (!code.Present() && e.id != "country-auto" && e.id != "country-hidden") return false;
    const float s = Scale(), size = ImGui::GetFontSize(), pad = ImGui::GetStyle().FramePadding.x;
    if (ImGui::IsRectVisible(min, max)) NoteUserText(e.label);
    const float y = min.y + (max.y - min.y - size) * .5f;
    auto* d = ImGui::GetWindowDrawList();
    // The choice in use says so at the row's end.
    const char* current = loc::T("settings.country.current");
    const float currentWidth = e.valueHighlighted ? ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0, current).x : 0;
    if (e.valueHighlighted) d->AddText(ImGui::GetFont(), size, ImVec2(max.x - pad - currentWidth, y), palette::Ember, current);
    const auto flag = DrawCountryFlagBeside(d, min.x + pad, y, size, code);
    std::string elided;
    DrawCardText(elided, ImVec2(min.x + pad + flag.width, y), max.x - min.x - 2 * pad - flag.width - (currentWidth ? currentWidth + 16 * s : 0),
        e.label, size, ImGui::GetColorU32(ImGuiCol_Text));
    return true;
}
} }
