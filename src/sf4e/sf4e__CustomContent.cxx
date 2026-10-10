#include "sf4e__CustomContent.hxx"
#include "../common/EnvFlag.hxx"
#include "../common/StageCatalog.hxx"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>

#include <windows.h>
#include <intrin.h>
#include <detours/detours.h>
#include "spdlog/spdlog.h"

namespace sf4e { namespace custom {
namespace {
// A costume load request (both handlers below take it): type 1 = load, then the fighter, costume and color.
constexpr int RequestFighter = 0x64 / 4, RequestCostume = 0x68 / 4, RequestColor = 0x6c / 4;

// The match's swaps (UseAppearance), one per battle side.
std::array<selection::CostumeSwap, 2> costumeSwaps;
std::array<selection::ColorSwap, 2> colorSwaps;

using Handler = int (__thiscall*)(void*, int*);
Handler loadModels = nullptr, loadColors = nullptr;

// While the game builds a stand-in's file names (RYU_02.obj.emo ...), it is given the custom slot (RYU_12 ...).
int Load(Handler original, void* self, int* request) {
    if (request && request[0] == 1)
        for (const auto& s : costumeSwaps)
            if (s.custom >= 0 && request[RequestFighter] == s.fighter && request[RequestCostume] == s.standIn) {
                request[RequestCostume] = s.custom;
                const int result = original(self, request);
                request[RequestCostume] = s.standIn;
                return result;
            }
    return original(self, request);
}
int __fastcall LoadModels(void* self, void*, int* request) { return Load(loadModels, self, request); }
// The same for a stand-in color (RYU_01_01.col.emb ...): it is given the custom color (RYU_01_23 ...).
int __fastcall LoadColors(void* self, void*, int* request) {
    if (request && request[0] == 1)
        for (const auto& s : colorSwaps)
            if (s.custom >= 0 && request[RequestFighter] == s.fighter && request[RequestCostume] == s.costume &&
                request[RequestColor] == s.standIn) {
                request[RequestColor] = s.custom;
                const int result = Load(loadColors, self, request);   // a custom costume's color: its slot too
                request[RequestColor] = s.standIn;
                return result;
            }
    return Load(loadColors, self, request);
}

selection::InstalledContentPtr installed;
std::once_flag scanned;

// The stage loader's own calls of the stage code lookup (Steam 1.05: Stage::Loader::Load RVA 0x1f0f80 builds
// STG_<code>.emz/.tex.emz, the step after it RVA 0x1f0d10 runs <code>_SetupObj.lua, _SetupParam.lua, _CUBE.emb),
// and the stage music's (RVA 0x1efd20 builds battle/sound/bgm/BGM_<code>.csb; the call returns to RVA 0x1efdb2)
// when the custom stage has music of its own. Everything else keeps the fallback stage's code.
using StageCode = const char* (__cdecl*)(int);
StageCode stageCode = nullptr;
std::uintptr_t loaderBegin = 0, loaderEnd = 0, musicCaller = 0;
int customNative = -1;
char customCode[4] = {};    // the custom stage's code while one is loaded, else empty
bool customMusic = false;
std::wstring battleFolder;   // patch_ae2_tu3\battle\ beside the game

const char* __cdecl LoaderStageCode(int stageId) {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if (customCode[0] && stageId == customNative &&
        ((caller >= loaderBegin && caller < loaderEnd) || (customMusic && caller == musicCaller))) return customCode;
    return stageCode(stageId);
}

// The name the setup program gives an item: the first line of the .txt beside it (UTF-8), at most 40 bytes. Only
// that much is read: a byte-order mark, 40 bytes, and the rest of a character the 40th byte may be in.
constexpr std::size_t NameBytes = 40;
std::string ReadName(const std::wstring& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    char buffer[3 + NameBytes + 4];
    in.read(buffer, sizeof(buffer));
    std::string line(buffer, static_cast<std::size_t>(in.gcount()));
    if (line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
    const auto end = line.find_first_of("\r\n");
    if (end != std::string::npos) line.resize(end);
    line.erase(std::remove_if(line.begin(), line.end(), [](char c) { return c >= 0 && c < ' '; }), line.end());
    if (line.size() > NameBytes) {
        std::size_t cut = NameBytes;
        while (cut > 0 && (line[cut] & 0xC0) == 0x80) cut--;
        line.resize(cut);
    }
    while (!line.empty() && line.back() == ' ') line.pop_back();
    return line;
}

// A regular file (not a folder of that name).
bool Exists(const std::wstring& file) {
    const DWORD attributes = GetFileAttributesW(file.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}
// The two digits at `at` of a file name (RYU_12...), or -1.
int Number(const std::wstring& name, std::size_t at) {
    if (name.size() < at + 2 || name[at] < L'0' || name[at] > L'9' || name[at + 1] < L'0' || name[at + 1] > L'9') return -1;
    return (name[at] - L'0') * 10 + (name[at + 1] - L'0');
}
// Calls `found` with the name of each regular file matching `pattern`.
template<class F> void Each(const std::wstring& pattern, F found) {
    WIN32_FIND_DATAW data;
    const HANDLE search = FindFirstFileW(pattern.c_str(), &data);
    if (search == INVALID_HANDLE_VALUE) return;
    do if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) found(std::wstring(data.cFileName));
    while (FindNextFileW(search, &data));
    FindClose(search);
}

std::shared_ptr<selection::InstalledContent> Scan() {
    auto content = std::make_shared<selection::InstalledContent>();
    wchar_t exe[MAX_PATH] = {};
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return content;
    std::wstring battle(exe);
    battle = battle.substr(0, battle.find_last_of(L'\\')) + L"\\patch_ae2_tu3\\battle\\";
    battleFolder = battle;
    if (EnvFlag("SF4E_IGNORE_CUSTOM")) {
        spdlog::info("Custom content: ignored for this copy (SF4E_IGNORE_CUSTOM)");
        return content;
    }
    const std::wstring root = battle + L"chara\\";
    int total = 0;
    for (int id = 0; id < selection::FighterCount; id++) {
        const std::string code = selection::FindFighter(id)->code;
        const std::wstring chr(code.begin(), code.end()), folder = root + chr + L"\\";
        auto& costumes = content->costumes[id];
        // A color is <name>.col.emb and <name>.obj.emm.
        const auto colorFiles = [&](const std::wstring& name) { return Exists(folder + name + L".col.emb") && Exists(folder + name + L".obj.emm"); };
        // Custom costumes: <CHR>_<NN>.obj.emo, NN 08..99, with all ten colors (an opponent may pick any of them).
        Each(folder + chr + L"_??.obj.emo", [&](const std::wstring& name) {
            const int costume = Number(name, 4) - 1;
            if (name.size() != 14 || !selection::IsCustomCostume(costume)) return;
            for (int color = 1; color <= selection::CustomColorCount; color++)
                if (!colorFiles(name.substr(0, 6) + L"_" + std::to_wstring(color / 10) + std::to_wstring(color % 10))) return;
            costumes.push_back(costume);
            const std::string label = ReadName(folder + name.substr(0, 6) + L".txt");
            if (!label.empty()) content->names[std::make_tuple(id, costume, -1)] = label;
        });
        std::sort(costumes.begin(), costumes.end());
        total += static_cast<int>(costumes.size());
        // Custom colors: <CHR>_<NN>_<CC>, CC 30..99, of one of the game's costumes or an installed custom one.
        Each(folder + chr + L"_??_??.col.emb", [&](const std::wstring& name) {
            const int costume = Number(name, 4) - 1, colorId = Number(name, 7) - 1;
            const bool costumeHere = costume >= 0 && (costume < selection::CostumeCount(id) ||
                std::binary_search(costumes.begin(), costumes.end(), costume));
            if (name.size() != 17 || !costumeHere || !selection::IsCustomColor(colorId) || !colorFiles(name.substr(0, 9))) return;
            content->colors[std::make_pair(id, costume)].push_back(colorId);
            const std::string label = ReadName(folder + name.substr(0, 9) + L".txt");
            if (!label.empty()) content->names[std::make_tuple(id, costume, colorId)] = label;
        });
    }
    for (auto& entry : content->colors) std::sort(entry.second.begin(), entry.second.end());
    spdlog::info("Custom costumes installed: {}", total);
    // Custom stages: STG_<code>.emz and STG_<code>.tex.emz with a custom code (the pattern also finds the game's own).
    Each(battle + L"stage\\STG_???.emz", [&](const std::wstring& name) {
        if (name.size() != 11 || name[4] > 127 || name[5] > 127 || name[6] > 127) return;
        const char code[4] = {static_cast<char>(name[4]), static_cast<char>(name[5]), static_cast<char>(name[6]), 0};
        const int id = selection::CustomStageId(code);
        if (id < 0 || !Exists(battle + L"stage\\" + name.substr(0, 7) + L".tex.emz")) return;
        content->stages.push_back(id);
        const std::string label = ReadName(battle + L"stage\\" + name.substr(0, 7) + L".txt");
        if (!label.empty()) content->stageNames[id] = label;
    });
    std::sort(content->stages.begin(), content->stages.end());
    spdlog::info("Custom stages installed: {}", content->stages.size());
    return content;
}
}

selection::InstalledContentPtr Installed() {
    std::call_once(scanned, [] { installed = Scan(); });
    return installed;
}

int ApplyStage(int stageId) {
    customNative = -1;
    customCode[0] = 0;
    customMusic = false;
    if (!selection::IsCustomStage(stageId)) return stageId;
    const int fallback = selection::CustomStageFallback(stageId);
    if (!Installed()->HasStage(stageId)) return fallback;
    const std::string code = selection::StageCode(stageId);
    std::memcpy(customCode, code.c_str(), sizeof(customCode));
    customNative = fallback;
    customMusic = Exists(battleFolder + L"sound\\bgm\\BGM_" + std::wstring(code.begin(), code.end()) + L".csb");
    spdlog::info("Custom stage {} plays as stage {} with its own files{}", code, fallback, customMusic ? " and music" : "");
    return fallback;
}

void UseAppearance(const selection::Appearance& appearance) {
    for (int side = 0; side < 2; side++) {
        costumeSwaps[side] = appearance.costumes[side];
        colorSwaps[side] = appearance.colors[side];
        const auto& c = costumeSwaps[side];
        if (c.custom >= 0) spdlog::info("Custom costume: side {} fighter {} slot {} plays as costume {}", side, c.fighter, c.custom + 1, c.standIn + 1);
        const auto& k = colorSwaps[side];
        if (k.custom >= 0)
            spdlog::info("Custom color: side {} fighter {} costume {} color {} plays as color {}", side, k.fighter, k.costume + 1, k.custom + 1, k.standIn + 1);
    }
}

void EndBattle() {
    costumeSwaps = {};
    colorSwaps = {};
    customNative = -1;
    customCode[0] = 0;
    customMusic = false;
}

void Install() {
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(NULL));
    loadModels = reinterpret_cast<Handler>(base + 0x1e5f80);
    loadColors = reinterpret_cast<Handler>(base + 0x1e5cf0);
    DetourAttach((PVOID*)&loadModels, (PVOID)LoadModels);
    DetourAttach((PVOID*)&loadColors, (PVOID)LoadColors);
    stageCode = reinterpret_cast<StageCode>(base + 0x2862c0);
    loaderBegin = base + 0x1f0d10;
    loaderEnd = base + 0x1f1100;
    musicCaller = base + 0x1efdb2;
    DetourAttach((PVOID*)&stageCode, (PVOID)LoaderStageCode);
}

} }
