#pragma once
#include <windows.h>
#include <atomic>
#include <initializer_list>
#include <variant>

namespace sf4e { namespace imports {

using Function = std::variant<const char*, WORD>;
struct Patch {
    const char* dll;
    Function function;
    void* replacement;
    std::atomic<void*>& original;
};

// Resolve and make every slot writable before changing any target. Only this module's IAT is edited.
bool PatchImports(HMODULE module, std::initializer_list<Patch> patches);
bool PatchImport(HMODULE module, const char* dll, Function function, void* replacement, std::atomic<void*>& original);

} }
