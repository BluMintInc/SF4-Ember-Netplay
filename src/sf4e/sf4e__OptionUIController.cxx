#include "sf4e__GameEvents.hxx"
#include "sf4e__Gamepads.hxx"
#include <detours/detours.h>

namespace sf4e { namespace GameEvents {
using Native = Dimps::GameEvents::OptionUIController;
void* OptionUIController::Construct(DWORD owner, DWORD mode) {
    Gamepads::LendGameTables(gamepads::GameTableUse::ButtonScreen);
    return (this->*Native::methods.Construct)(owner, mode);
}
int OptionUIController::Destroy() {
    const int result = (this->*Native::methods.Destroy)();
    Gamepads::ReturnGameTables(gamepads::GameTableUse::ButtonScreen);
    return result;
}
void OptionUIController::Install() {
    auto construct = &OptionUIController::Construct;
    auto destroy = &OptionUIController::Destroy;
    DetourAttach((PVOID*)&Native::methods.Construct, *(PVOID*)&construct);
    DetourAttach((PVOID*)&Native::methods.Destroy, *(PVOID*)&destroy);
}
} }
