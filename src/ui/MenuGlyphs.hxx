#pragma once
#include "Theme.hxx"
#include "SelectionArt.hxx"
#include "MenuProbes.hxx"
#include "../common/Localization.hxx"
#include "../common/ButtonPrompts.hxx"
#include <cstring>
#include <string>
#include <vector>

namespace sf4e { namespace ui {
// Native XInput normalization at 006D8415..006D8466. DirectInput device shape
// cannot be inferred from its binding index; never pretend it is an Xbox pad.
inline const char* PhysicalGlyph(int type,unsigned mask,const char* fallback) {
    if(type!=input::PadXInput)return fallback;
    switch(mask){case input::xinput::A:return "A";case input::xinput::B:return "B";case input::xinput::X:return "X";case input::xinput::Y:return "Y";
    case input::xinput::LB:return "LB";case input::xinput::RB:return "RB";case input::xinput::LT:return "LT";case input::xinput::RT:return "RT";default:return fallback;}
}
gamepads::PromptBrand MenuPromptBrand();
// A shell's unsaved value takes effect before the normal autosave deadline.
void SetMenuPromptBrand(gamepads::PromptBrand brand);
struct BrandedGlyph { const char* asset; const char* name; };
inline BrandedGlyph BrandGlyph(const char* id, gamepads::PromptBrand brand) {
    struct Position { const char* id; const char* xbox; const char* playStation; const char* nintendo; const char* stick; const char* fight; };
    static constexpr Position positions[] = {
        {"A", "xbox_button_color_a", "playstation_button_color_cross", "switch_button_b", "generic_button_circle_fill", "LK"},
        {"B", "xbox_button_color_b", "playstation_button_color_circle", "switch_button_a", "generic_button_circle_fill", "MK"},
        {"X", "xbox_button_color_x", "playstation_button_color_square", "switch_button_y", "generic_button_circle_fill", "LP"},
        {"Y", "xbox_button_color_y", "playstation_button_color_triangle", "switch_button_x", "generic_button_circle_fill", "MP"},
        {"LB", "xbox_lb", "playstation_trigger_l1", "switch_button_l", "generic_button_circle_fill", "3P"},
        {"RB", "xbox_rb", "playstation_trigger_r1", "switch_button_r", "generic_button_circle_fill", "HP"},
        {"LT", "xbox_lt", "playstation_trigger_l2", "switch_button_zl", "generic_button_circle_fill", "3K"},
        {"RT", "xbox_rt", "playstation_trigger_r2", "switch_button_zr", "generic_button_circle_fill", "HK"},
        {"View", "xbox_button_back", "playstation4_button_share", "switch_button_minus", "generic_button_circle_fill", "Select"},
        {"Start", "xbox_button_start", "playstation4_button_options", "switch_button_plus", "generic_button_circle_fill", "Start"},
        {"dpad", "xbox_dpad", "playstation_dpad", "switch_dpad", "generic_joystick", ""},
        {"horizontal", "xbox_dpad_horizontal", "playstation_dpad_horizontal", "switch_dpad_horizontal", "generic_joystick_horizontal", ""},
        {"up", "xbox_dpad_up", "playstation_dpad_up", "switch_dpad_up", "generic_stick_up", ""},
        {"down", "xbox_dpad_down", "playstation_dpad_down", "switch_dpad_down", "generic_stick_down", ""},
        {"left", "xbox_dpad_left", "playstation_dpad_left", "switch_dpad_left", "generic_stick_left", ""},
        {"right", "xbox_dpad_right", "playstation_dpad_right", "switch_dpad_right", "generic_stick_right", ""},
    };
    for (const auto& position : positions) if (!std::strcmp(id, position.id)) {
        switch (brand.brand) {
        case gamepads::Brand::Stick:
            return {position.stick, position.fight};
        case gamepads::Brand::PlayStation:
            if (!std::strcmp(id, "View")) return {
                brand.playStation == gamepads::PlayStationArt::PS3 ? "playstation3_button_select" :
                brand.playStation == gamepads::PlayStationArt::PS5 ? "playstation5_button_create" : "playstation4_button_share", ""};
            if (!std::strcmp(id, "Start")) return {
                brand.playStation == gamepads::PlayStationArt::PS3 ? "playstation3_button_start" :
                brand.playStation == gamepads::PlayStationArt::PS5 ? "playstation5_button_options" : "playstation4_button_options", ""};
            return {position.playStation, ""};
        case gamepads::Brand::Nintendo: return {position.nintendo, ""};
        default: return {position.xbox, ""};
        }
    }
    if (!std::strcmp(id, "Enter") || !std::strcmp(id, "Ctrl+Enter")) return {"keyboard_enter", ""};
    if (!std::strcmp(id, "Esc")) return {"keyboard_escape", ""};
    if (!std::strcmp(id, "arrows")) return {"keyboard_arrows_all", ""};
    if (!std::strcmp(id, "keys-horizontal")) return {"keyboard_arrows_horizontal", ""};
    return {"generic_button_circle_fill", id};
}
inline const char* PromptAsset(const char* glyph, gamepads::PromptBrand brand = {}) {
    return BrandGlyph(glyph, brand).asset;
}
// The keyboard keys the legend draws as a key cap with their name on it; the
// prompt art has no letter keys.
inline bool KeyCapGlyph(const char* glyph) {
    // The function keys F1 to F12 too, which the replay controls use.
    const bool function=glyph[0]=='F'&&glyph[1]>='1'&&glyph[1]<='9'&&(!glyph[2]||(glyph[1]=='1'&&glyph[2]>='0'&&glyph[2]<='2'&&!glyph[3]));
    return function||!std::strcmp(glyph,"F")||!std::strcmp(glyph,"T")||!std::strcmp(glyph,"C")||!std::strcmp(glyph,"Del");
}
inline std::string PromptLabel(const char* glyph,const std::string& label,gamepads::PromptBrand brand) {
    if(!std::strcmp(glyph,"Ctrl+Enter"))return "Ctrl+Enter "+label;
    const auto translated=BrandGlyph(glyph,brand);
    return !KeyCapGlyph(glyph)&&*translated.name?std::string(translated.name)+" "+label:label;
}
// One prompt in a size-by-size square at `at`: a key cap with its name, the
// prompt art, or, with neither, the glyph's name. `s` is the caller's scale; `keyFont`,
// when given, sizes a key cap's name.
inline void DrawPromptGlyph(ImDrawList* d,const char* glyph,ImVec2 at,float size,SelectionArt* art,float s,float keyFont=0,gamepads::PromptBrand brand=MenuPromptBrand()) {
    if(KeyCapGlyph(glyph)){
        const ImVec2 capTop(at.x+2*s,at.y+3*s),capBottom(at.x+size-2*s,at.y+size-3*s);
        d->AddRectFilled(capTop,capBottom,IM_COL32(40,38,36,235),5*s);
        d->AddRect(capTop,capBottom,palette::Ivory,5*s,0,1.5f*s);
        // 12 and 16 at the legend's 32-pixel prompts, and in step at other sizes.
        if(keyFont<=0)keyFont=(std::strlen(glyph)>1?12:16)*size/32;
        const ImVec2 text=ImGui::GetFont()->CalcTextSizeA(keyFont,FLT_MAX,0,glyph);
        d->AddText(ImGui::GetFont(),keyFont,ImVec2((capTop.x+capBottom.x-text.x)*.5f,(capTop.y+capBottom.y-text.y)*.5f),palette::Ivory,glyph);
        return;
    }
    const auto icon=art?art->InputPrompt(PromptAsset(glyph,brand)):SelectionImage{};
    if(icon.texture)d->AddImage(icon.texture,at,ImVec2(at.x+size,at.y+size),icon.uvMin,icon.uvMax);
    else {
        const char* name=BrandGlyph(glyph,brand).name;
        d->AddText(ImGui::GetFont(),12*size/32,ImVec2(at.x,at.y+8*size/32),palette::Ivory,*name?name:glyph);
    }
}
// A screen's own button, shown after the standard ones.
struct LegendHint { const char* glyph; std::string label; };
inline float MenuLegend(float width,const char* select,const char* back,bool draw,bool adjustable,SelectionArt* art,float scale=0,
                        const char* primary=loc::T("menu.select"),const std::vector<LegendHint>& extras={},const char* backText=nullptr,
                        float* lastRowEnd=nullptr) {
    const bool keyboard=!std::strcmp(select,"Enter")||!std::strcmp(select,"Ctrl+Enter");
    std::vector<LegendHint> hints{{keyboard?"arrows":"dpad",loc::T("menu.navigate")}};
    if(primary)hints.push_back({select,primary});
    const char* backWord=backText&&*backText?backText:loc::T("common.back");
    hints.push_back({back,backWord});
    if(adjustable)hints.push_back({keyboard?"keys-horizontal":"horizontal",loc::T("menu.adjust")});
    hints.insert(hints.end(),extras.begin(),extras.end());
    for(auto& hint:hints)hint.label=PromptLabel(hint.glyph,hint.label,MenuPromptBrand());
    const float s=scale>0?scale:Scale(),height=38*s,size=32*s;
    float measuredLabels=0;
    for(const auto& hint:hints)measuredLabels+=ImGui::GetFont()->CalcTextSizeA(16*s,FLT_MAX,0,hint.label.c_str()).x;
    const float fixed=hints.size()*(size+32*s);
    const float font=(std::max)(11*s,(std::min)(16*s,16*s*(std::max)(1.f,width-fixed)/(std::max)(1.f,measuredLabels)));
    float x=0,y=0;const auto start=ImGui::GetCursorScreenPos();
    for(const auto& hint:hints){
        const float w=size+8*s+ImGui::GetFont()->CalcTextSizeA(font,FLT_MAX,0,hint.label.c_str()).x+24*s;
        if(x&&x+w>width){x=0;y+=height;}
        if(draw){
            // The font shrinks and rows wrap, so only a hint wider than the
            // whole legend can be cut off.
            ReportMenuText(("legend/"+hint.label).c_str(),font,height,w,width);
            auto* d=ImGui::GetWindowDrawList();
            DrawPromptGlyph(d,hint.glyph,ImVec2(start.x+x,start.y+y),size,art,s);
            d->AddText(ImGui::GetFont(),font,ImVec2(start.x+x+size+8*s,start.y+y+(size-font)*.5f),palette::Ivory,hint.label.c_str());
        }
        x+=w;
    }
    // Where the last row's last label ends, for what shares that row.
    if(lastRowEnd)*lastRowEnd=(std::max)(0.f,x-24*s);
    return y+height;
}
} }
