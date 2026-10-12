#include "../sf4e/sf4e__ImportPatch.hxx"
#include "test_support.hxx"
#include <cstring>
#include <iostream>

int main() {
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
