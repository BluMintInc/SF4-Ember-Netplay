#include "sf4e__ImportPatch.hxx"
#include <cstring>
#include <vector>

namespace sf4e { namespace imports {
namespace {
void** FindImport(HMODULE module, const char* dll, const Function& function) {
    if (!module || !dll) return nullptr;
    const auto* base = reinterpret_cast<const BYTE*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress) return nullptr;
    for (auto* entry = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress); entry->Name; ++entry) {
        if (_stricmp(reinterpret_cast<const char*>(base + entry->Name), dll) != 0 || !entry->OriginalFirstThunk) continue;
        const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + entry->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(const_cast<BYTE*>(base) + entry->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (const auto* ordinal = std::get_if<WORD>(&function)) {
                if (!IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal) || IMAGE_ORDINAL(names->u1.Ordinal) != *ordinal) continue;
            } else {
                const char* name = std::get<const char*>(function);
                if (!name || IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
                const auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
                if (strcmp(reinterpret_cast<const char*>(byName->Name), name) != 0) continue;
            }
            return reinterpret_cast<void**>(&slots->u1.Function);
        }
    }
    return nullptr;
}
}

bool PatchImports(HMODULE module, std::initializer_list<Patch> patches) {
    struct Resolved { const Patch* patch; void** slot; DWORD protection = 0; };
    std::vector<Resolved> resolved;
    for (const auto& patch : patches) {
        auto* slot = FindImport(module, patch.dll, patch.function);
        if (!slot || !patch.replacement) return false;
        resolved.push_back({&patch, slot});
    }
    std::size_t writable = 0;
    for (; writable < resolved.size(); ++writable) {
        auto& entry = resolved[writable];
        if (!VirtualProtect(entry.slot, sizeof(void*), PAGE_READWRITE, &entry.protection)) break;
    }
    const bool ready = writable == resolved.size();
    if (ready) for (auto& entry : resolved) {
        entry.patch->original.store(*entry.slot);
        // One aligned pointer write: callers see either the old or the new target.
        InterlockedExchangePointer(entry.slot, entry.patch->replacement);
    }
    // Reverse order also restores pages shared by several import slots correctly.
    while (writable) {
        auto& entry = resolved[--writable];
        DWORD ignored;
        VirtualProtect(entry.slot, sizeof(void*), entry.protection, &ignored);
    }
    return ready;
}

bool PatchImport(HMODULE module, const char* dll, Function function, void* replacement, std::atomic<void*>& original) {
    return PatchImports(module, {{dll, function, replacement, original}});
}

} }
