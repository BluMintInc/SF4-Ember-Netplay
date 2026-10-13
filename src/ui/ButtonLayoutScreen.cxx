#include "ButtonLayoutScreen.hxx"
#include "GameMenu.hxx"
#include "TextPill.hxx"

namespace sf4e { namespace ui {
using namespace gamepads;
std::string LayoutName(const std::optional<ButtonLayout>& layout) {
    const auto* preset = layout ? PresetOf(*layout) : nullptr;
    return loc::T(!layout ? "button_layout.game_layout" : preset ? preset->key : "button_layout.custom");
}
std::string AttackLongName(std::uint32_t mask) {
    for (const auto& function : LayoutFunctions) if (mask == function.mask) return loc::T(function.key);
    return FightName(mask);
}
FightNames LayoutButtonNames(const PadView& pad, ButtonPrompts prompts, const std::map<std::string, std::string>& mappings) {
    const auto brand = ResolvePrompts(prompts, input::PadXInput, pad.brand).brand;
    if (brand == Brand::PlayStation) return {"Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "L3", "R3"};
    if (brand == Brand::Nintendo) return {"B", "A", "Y", "X", "L", "R", "ZL", "ZR", "LS", "RS"};
    FightNames names = {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS"};
    if (brand != Brand::Stick) return names;
    const auto mapping = mappings.find(pad.identity.guid);
    if (pad.mapping.empty() && mapping == mappings.end()) return names;
    constexpr std::array<const char*, 10> targets = {"a", "b", "x", "y", "leftshoulder", "rightshoulder", "lefttrigger", "righttrigger", "leftstick", "rightstick"};
    for (const auto& field : ParseMapping(pad.mapping.empty() ? mapping->second : pad.mapping).fields)
        for (unsigned i = 0; i < targets.size(); ++i) if (field.first == targets[i])
            if (auto raw = ParseRawBinding(field.second)) { raw->numberedHat = pad.numberedHats; names[i] = RawInputName(*raw); }
    return names;
}
std::vector<MenuEntry> ButtonLayoutRows(const PadView& pad, bool editable) {
    std::vector<MenuEntry> rows;
    const bool absent = pad.state == RowState::Absent;
    const bool setup = pad.state == RowState::GameRead || pad.state == RowState::Unmapped;
    if (setup) {
        auto first = InfoRow("layout-setup-first", loc::T("button_layout.setup_first"), "", loc::Tf("button_layout.setup_first_detail", pad.name));
        first.detailText = DetailText::Name;
        return {std::move(first), Row("setup", loc::T("controllers.choice_setup"), loc::T(editable ? "controllers.choice_setup_detail" : "settings.leave_room_to_edit"), editable)};
    }
    const auto unavailable = absent ? loc::Tf("button_layout.absent_detail", pad.name) : std::string(loc::T("settings.leave_room_to_edit"));
    auto first = InfoRow(absent ? "layout-absent" : "test", loc::T(absent ? "button_layout.absent" : "button_layout.test"), "",
        absent ? unavailable : loc::Tf("button_layout.test_detail", pad.name));
    first.detailText = DetailText::Name; rows.push_back(std::move(first));
    const auto missing = MissingAttacks(pad.table, pad.has);
    for (const auto& function : LayoutFunctions) {
        auto detail = editable && !absent ? loc::Tf("button_layout.row_detail", loc::T(function.key)) : unavailable;
        if (editable && std::find(missing.begin(), missing.end(), function.mask) != missing.end()) detail += "\n\n" + std::string(loc::T("button_layout.row_missing"));
        auto row = Row(std::string("fn-") + function.id, loc::T(function.key), detail, editable && !absent);
        for (unsigned i = 0; i < pad.has.size(); ++i) if (pad.has[i] && pad.table[i] == function.mask) {
            if (!row.value.empty()) row.value += ", ";
            row.value += pad.buttonNames[i];
        }
        if (row.value.empty()) row.value = loc::T("button_layout.no_button");
        row.detailText = DetailText::Name; row.quiet = !row.enabled; rows.push_back(std::move(row));
    }
    rows.push_back(Row("edit-button", loc::T("button_layout.edit_button"), editable && !absent ? loc::T("button_layout.edit_button_detail") : unavailable, editable && !absent));
    auto preset = Row("preset", loc::T("button_layout.preset"), editable && !absent ? loc::T("button_layout.preset_detail") : unavailable, editable && !absent);
    preset.value = LayoutName(pad.layout);
    if (pad.layout) if (const auto* current = PresetOf(*pad.layout)) preset.chosen = current->id;
    for (const auto& choice : LayoutPresets) preset.choices.push_back({choice.id, loc::T(choice.key), loc::T(choice.detailKey)});
    rows.push_back(std::move(preset));
    if (editable && !absent && pad.layout) rows.push_back(ConfirmRow("remove", loc::T("button_layout.remove"), loc::T("button_layout.remove_detail")));
    return rows;
}
std::vector<MenuEntry> ButtonEditorRows(const PadView& pad, unsigned button, bool editable) {
    std::vector<MenuEntry> rows;
    for (unsigned i = 0; i < 6; ++i) {
        const auto& function = LayoutFunctions[i];
        rows.push_back(Value(std::string("bit-") + function.id, loc::T(function.key), loc::T(pad.table[button] & function.mask ? "common.on" : "common.off"),
            loc::Tf("button_layout.bit_detail", loc::T(function.key)), editable));
    }
    rows.push_back(Row("clear", loc::T("button_layout.clear"), loc::T("button_layout.clear_detail"), editable && pad.table[button] != 0));
    return rows;
}
namespace {
bool Held(const PadView& pad, const char* glyph) { return std::find(pad.held.begin(), pad.held.end(), glyph) != pad.held.end(); }
void PictureName(const PadView& pad) {
    const float size = 64 * Scale(); const auto at = ImGui::GetCursorScreenPos();
    const auto picture = MenuArt() ? MenuArt()->InputPrompt(pad.picture) : SelectionImage{};
    if (picture.texture) ImGui::GetWindowDrawList()->AddImage(picture.texture, at, ImVec2(at.x + size, at.y + size), picture.uvMin, picture.uvMax);
    ImGui::Dummy(ImVec2(size, size)); ImGui::SameLine();
    NoteUserText(pad.name.c_str()); ImGui::TextWrapped("%s", pad.name.c_str());
}
void LayoutMap(const PadView& pad, PromptBrand brand) {
    const float s = Scale(), size = 40 * s;
    const auto names = NamesOf(pad.table);
    const std::vector<std::vector<unsigned>> lines = {{2, 3, 5, 4}, {0, 1, 7, 6}, {8, 9}};
    auto* draw = ImGui::GetWindowDrawList();
    for (const auto& line : lines) {
        auto at = ImGui::GetCursorScreenPos(); unsigned count = 0; float labelHeight = 0;
        for (auto i : line) if (pad.has[i]) {
            const bool held = Held(pad, LayoutGlyphs[i]);
            DrawPromptGlyph(draw, LayoutGlyphs[i], at, size, MenuArt(), s, 0, brand, held ? 1.f : .35f, &names);
            if (held) draw->AddRectFilled(ImVec2(at.x, at.y + size + 2 * s), ImVec2(at.x + size, at.y + size + 4 * s), palette::Ember);
            const auto label = names[i].empty() ? std::string(loc::T("button_layout.nothing")) : names[i];
            const auto measured = ImGui::GetFont()->CalcTextSizeA(12 * s, FLT_MAX, size, label.c_str());
            labelHeight = (std::max)(labelHeight, measured.y);
            draw->AddText(ImGui::GetFont(), 12 * s, ImVec2(at.x, at.y + size + 6 * s), palette::Muted, label.c_str(), nullptr, size);
            at.x += size + 16 * s; ++count;
        }
        if (count) ImGui::Dummy(ImVec2(0, size + labelHeight + 12 * s));
    }
}
void Attacks(const PadView& pad) {
    ImGui::TextUnformatted(loc::T("button_layout.attacks"));
    std::uint32_t active = 0;
    for (unsigned i = 0; i < pad.has.size(); ++i) if (pad.has[i] && Held(pad, LayoutGlyphs[i])) active |= pad.table[i];
    const float s = Scale(), height = ImGui::GetTextLineHeight(), gap = 6 * s;
    auto at = ImGui::GetCursorScreenPos(); auto* draw = ImGui::GetWindowDrawList();
    for (unsigned i = 0; i < AttackMasks.size(); ++i) {
        const bool held = (active & AttackMasks[i]) != 0;
        const float width = ImGui::CalcTextSize(AttackNames[i]).x + 12 * s;
        DrawButtonPill(draw, AttackNames[i], at, width, height, s, held ? 1.f : .35f, held);
        at.x += width + gap;
    }
    ImGui::Dummy(ImVec2(0, height));
}
}
void DrawButtonLayoutDetail(const PadView& pad, ButtonPrompts prompts, bool capture, const std::optional<std::uint32_t>& mask) {
    PictureName(pad);
    if (pad.state == RowState::GameRead || pad.state == RowState::Unmapped) return;
    if (capture) {
        const auto text = mask ? loc::Tf("button_layout.press_for", AttackLongName(*mask)) : std::string(loc::T("button_layout.press_to_change"));
        const float font = 1.4f * ImGui::GetFontSize();
        const auto at = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const auto measured = ImGui::GetFont()->CalcTextSizeA(font, FLT_MAX, width, text.c_str());
        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), font, at, palette::Ivory, text.c_str(), nullptr, width);
        ImGui::Dummy(ImVec2(0, measured.y + 8 * Scale()));
        ImGui::TextWrapped("%s", loc::T("button_layout.press_cancel"));
    }
    LayoutMap(pad, ResolvePrompts(prompts, input::PadXInput, pad.brand));
    if (!capture) Attacks(pad);
}
void DrawButtonEditorDetail(const PadView& pad, ButtonPrompts prompts, unsigned button) {
    const float size = 64 * Scale(); const auto at = ImGui::GetCursorScreenPos(); const auto names = NamesOf(pad.table);
    DrawPromptGlyph(ImGui::GetWindowDrawList(), LayoutGlyphs[button], at, size, MenuArt(), Scale(), 0,
        ResolvePrompts(prompts, input::PadXInput, pad.brand), 1, &names);
    ImGui::Dummy(ImVec2(size, size));
    ImGui::TextUnformatted(pad.buttonNames[button].c_str());
    const auto name = FightName(pad.table[button]);
    ImGui::TextWrapped("%s", loc::Tf("button_layout.button_does", name.empty() ? loc::T("button_layout.nothing") : name).c_str());
    Attacks(pad);
}
} }
