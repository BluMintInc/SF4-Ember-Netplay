#pragma once
#include <windows.h>
#include <dinput.h>
#include <atomic>
#include <string>
#include <vector>
#include "../common/ControllerView.hxx"

namespace sf4e { namespace DirectInputFilter {

using Serves = bool (*)(const GUID& instance, DWORD vidpid);
extern std::atomic<void*> originalCreate;
HRESULT WINAPI Create(HINSTANCE instance, DWORD version, REFIID iid, void** out, IUnknown* outer);

// Replaces an owned Input8 or Device8 A/W reference on success. On failure
// the reference is unchanged and still belongs to the caller.
HRESULT Wrap(REFIID iid, void** object, Serves serves);

// The native provider's accepted enumeration, before SDL ordering or names.
std::vector<gamepads::NativeController> ReadControllers();

} }
