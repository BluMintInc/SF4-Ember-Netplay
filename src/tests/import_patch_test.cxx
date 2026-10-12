#include "../sf4e/sf4e__ImportPatch.hxx"
#include "test_support.hxx"
#include <array>
#include <cstring>
#include <iostream>

static void ControllerImports() {
    auto* image = static_cast<BYTE*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    CHECK(image);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 128;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(image + 128);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = 512;
    auto* imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image + 512);
    const char* dlls[] = {"XINPUT9_1_0.dll", "XINPUT1_3.dll", "DINPUT8.dll"};
    const char* functions[] = {"XInputGetState", "XInputGetCapabilities", "XInputSetState", nullptr, "DirectInput8Create"};
    std::array<void**, 5> slots{};
    unsigned function = 0;
    for (unsigned dll = 0; dll < 3; ++dll) {
        const DWORD base = 640 + dll * 512;
        imports[dll].Name = base; imports[dll].OriginalFirstThunk = base + 64; imports[dll].FirstThunk = base + 128;
        std::strcpy(reinterpret_cast<char*>(image + base), dlls[dll]);
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(image + base + 64);
        auto* targets = reinterpret_cast<IMAGE_THUNK_DATA*>(image + base + 128);
        for (unsigned entry = 0; entry < (dll == 0 ? 3u : 1u); ++entry, ++function) {
            if (functions[function]) {
                const DWORD offset = base + 192 + entry * 64;
                auto* name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(image + offset);
                std::strcpy(reinterpret_cast<char*>(name->Name), functions[function]);
                names[entry].u1.AddressOfData = offset;
            } else names[entry].u1.Ordinal = IMAGE_ORDINAL_FLAG | 7;
            targets[entry].u1.Function = 0x1000 + function;
            slots[function] = reinterpret_cast<void**>(&targets[entry].u1.Function);
        }
    }
    DWORD protection;
    CHECK(VirtualProtect(image, 4096, PAGE_READONLY, &protection));
    const auto module = reinterpret_cast<HMODULE>(image);
    std::atomic<void*> originals[5]{};
    using namespace sf4e::imports;
    const auto patch = [&](const char* directInputName) {
        return PatchImports(module, {
            {dlls[0], functions[0], reinterpret_cast<void*>(0x2000), originals[0]},
            {dlls[0], functions[1], reinterpret_cast<void*>(0x2001), originals[1]},
            {dlls[0], functions[2], reinterpret_cast<void*>(0x2002), originals[2]},
            {dlls[1], WORD(7), reinterpret_cast<void*>(0x2003), originals[3]},
            {dlls[2], directInputName, reinterpret_cast<void*>(0x2004), originals[4]}});
    };
    CHECK(!patch("MissingDirectInput8Create"));
    for (unsigned i = 0; i < slots.size(); ++i) {
        CHECK(*slots[i] == reinterpret_cast<void*>(0x1000 + i));
        CHECK(!originals[i].load());
    }
    CHECK(patch(functions[4]));
    for (unsigned i = 0; i < slots.size(); ++i) {
        CHECK(*slots[i] == reinterpret_cast<void*>(0x2000 + i));
        CHECK(originals[i].load() == reinterpret_cast<void*>(0x1000 + i));
    }
    MEMORY_BASIC_INFORMATION info;
    CHECK(VirtualQuery(image, &info, sizeof(info)) == sizeof(info) && info.Protect == PAGE_READONLY);
    CHECK(VirtualFree(image, 0, MEM_RELEASE));
}

int main() {
    ControllerImports();
    auto* image = static_cast<BYTE*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    CHECK(image);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 128;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(image + 128);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = 512;
    auto* imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image + 512);
    imports[0].Name = 640; imports[0].OriginalFirstThunk = 704; imports[0].FirstThunk = 768;
    std::strcpy(reinterpret_cast<char*>(image + 640), "XINPUT1_3.dll");
    auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(image + 704);
    names[0].u1.AddressOfData = 832;
    names[1].u1.Ordinal = IMAGE_ORDINAL_FLAG | 7;
    auto* name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(image + 832);
    std::strcpy(reinterpret_cast<char*>(name->Name), "XInputGetState");
    auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(image + 768);
    slots[0].u1.Function = 0x1111; slots[1].u1.Function = 0x2222;
    DWORD protection;
    CHECK(VirtualProtect(image, 4096, PAGE_READONLY, &protection));
    const auto module = reinterpret_cast<HMODULE>(image);
    std::atomic<void*> state{nullptr}, battery{nullptr};
    using namespace sf4e::imports;
    CHECK(!PatchImports(module, {{"XINPUT1_3.dll", "XInputGetState", reinterpret_cast<void*>(0x3333), state},
        {"XINPUT1_3.dll", WORD(8), reinterpret_cast<void*>(0x4444), battery}}));
    CHECK(slots[0].u1.Function == 0x1111 && slots[1].u1.Function == 0x2222 && !state.load() && !battery.load());
    CHECK(PatchImports(module, {{"xinput1_3.DLL", "XInputGetState", reinterpret_cast<void*>(0x3333), state},
        {"XINPUT1_3.dll", WORD(7), reinterpret_cast<void*>(0x4444), battery}}));
    CHECK(state.load() == reinterpret_cast<void*>(0x1111) && battery.load() == reinterpret_cast<void*>(0x2222));
    CHECK(slots[0].u1.Function == 0x3333 && slots[1].u1.Function == 0x4444);
    MEMORY_BASIC_INFORMATION info;
    CHECK(VirtualQuery(image, &info, sizeof(info)) == sizeof(info) && info.Protect == PAGE_READONLY);
    CHECK(!PatchImport(module, "other.dll", "XInputGetState", reinterpret_cast<void*>(0x5555), state));
    CHECK(!PatchImport(module, "XINPUT1_3.dll", "XInputGetBatteryInformation", reinterpret_cast<void*>(0x5555), battery));
    CHECK(PatchImport(module, "XINPUT1_3.dll", "XInputGetState", reinterpret_cast<void*>(0x6666), state));
    CHECK(state.load() == reinterpret_cast<void*>(0x3333) && slots[0].u1.Function == 0x6666);
    CHECK(VirtualFree(image, 0, MEM_RELEASE));
    std::cout << "Named and ordinal imports, all-or-none patching and page restoration passed\n";
}
