#include "sf4e__DirectInputFilter.hxx"
#include "sf4e__Gamepads.hxx"
#include "../common/DirectInputState.hxx"
#include "../platform/Utf8.hxx"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <mutex>
#include <new>
#include <set>

namespace sf4e { namespace DirectInputFilter {
std::atomic<void*> originalCreate{nullptr};
namespace {
bool ServesDevice(const GUID& instance, DWORD product, const std::string& path);
std::mutex controllersMutex;
std::vector<gamepads::NativeController> nativeControllers;
// Live game devices, so a pad lent to the setup wizard is let go at once, not at
// the game's next poll. Never held while calling into Gamepads.
class Lendable {
public:
    virtual const std::string& Path() const = 0;
    virtual void LetGo() = 0;
protected:
    ~Lendable() = default;
};
struct LentState { std::mutex mutex; std::vector<std::string> paths; std::set<Lendable*> devices; };
// Leaked on purpose: the game may release its devices after this DLL's statics are destroyed.
LentState& Lent() { static auto* state = new LentState; return *state; }
bool LentLocked(const std::string& path) {
    const auto& lentPaths = Lent().paths;
    return std::any_of(lentPaths.begin(), lentPaths.end(), [&](const std::string& lent) { return gamepads::SameDriverPath(lent, path); });
}
struct GuidLess {
    bool operator()(const GUID& a, const GUID& b) const { return std::memcmp(&a, &b, sizeof(GUID)) < 0; }
};
template<class Device>
std::string DevicePath(Device* device) {
    DIPROPGUIDANDPATH path{};
    path.diph = {sizeof(path), sizeof(path.diph), 0, DIPH_DEVICE};
    if (FAILED(device->GetProperty(DIPROP_GUIDANDPATH, &path.diph))) return {};
    return platform::WideToUtf8(std::wstring(path.wszPath,
        std::find(std::begin(path.wszPath), std::end(path.wszPath), L'\0')));
}
std::string Utf8(const char* text) { return text; }
std::string Utf8(const wchar_t* text) {
    return platform::WideToUtf8(text);
}
struct Ansi {
    using Input = IDirectInput8A;
    using Device = IDirectInputDevice8A;
    using Instance = DIDEVICEINSTANCEA;
    using Object = DIDEVICEOBJECTINSTANCEA;
    using EnumDevices = LPDIENUMDEVICESCALLBACKA;
    using EnumObjects = LPDIENUMDEVICEOBJECTSCALLBACKA;
    using EnumEffects = LPDIENUMEFFECTSCALLBACKA;
    using EffectInfo = DIEFFECTINFOA;
    using ActionFormat = DIACTIONFORMATA;
    using ImageInfo = DIDEVICEIMAGEINFOHEADERA;
    using Semantics = LPDIENUMDEVICESBYSEMANTICSCBA;
    using ConfigureParams = DICONFIGUREDEVICESPARAMSA;
    using String = LPCSTR;
    static const GUID& InputIid() { return IID_IDirectInput8A; }
    static const GUID& DeviceIid() { return IID_IDirectInputDevice8A; }
};
struct Wide {
    using Input = IDirectInput8W;
    using Device = IDirectInputDevice8W;
    using Instance = DIDEVICEINSTANCEW;
    using Object = DIDEVICEOBJECTINSTANCEW;
    using EnumDevices = LPDIENUMDEVICESCALLBACKW;
    using EnumObjects = LPDIENUMDEVICEOBJECTSCALLBACKW;
    using EnumEffects = LPDIENUMEFFECTSCALLBACKW;
    using EffectInfo = DIEFFECTINFOW;
    using ActionFormat = DIACTIONFORMATW;
    using ImageInfo = DIDEVICEIMAGEINFOHEADERW;
    using Semantics = LPDIENUMDEVICESBYSEMANTICSCBW;
    using ConfigureParams = DICONFIGUREDEVICESPARAMSW;
    using String = LPCWSTR;
    static const GUID& InputIid() { return IID_IDirectInput8W; }
    static const GUID& DeviceIid() { return IID_IDirectInputDevice8W; }
};

// One inner reference for the wrapper's entire lifetime, independent of the
// number of references callers hold to its IUnknown or wrapped interface.
template<class Interface>
class OwnedCom : public Interface {
public:
    OwnedCom(Interface* inner, REFIID iid) : inner_(inner), iid_(iid) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        if (iid == IID_IUnknown || iid == iid_) {
            *out = static_cast<Interface*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = --references_;
        if (!left) delete this;
        return left;
    }
protected:
    virtual ~OwnedCom() { inner_->Release(); }
    Interface* const inner_;
private:
    const GUID iid_;
    std::atomic<ULONG> references_{1};
};

template<class Api>
class Device final : public OwnedCom<typename Api::Device>, private Lendable {
    using Base = OwnedCom<typename Api::Device>;
    using Base::inner_;
public:
    Device(typename Api::Device* inner, Serves serves) :
        Base(inner, Api::DeviceIid()), serves_(serves), path_(DevicePath(inner)) {
        info_.dwSize = sizeof(info_);
        if (FAILED(inner_->GetDeviceInfo(&info_))) info_ = {};
        std::lock_guard lock(Lent().mutex); Lent().devices.insert(this);
    }
    ~Device() override { std::lock_guard lock(Lent().mutex); Lent().devices.erase(this); }
    HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD size, void* out) override {
        if (!Served()) return inner_->GetDeviceState(size, out);
        return neutral_.Write(size, out) ? DI_OK : DIERR_INVALIDPARAM;
    }
    HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD size, DIDEVICEOBJECTDATA* out, DWORD* count, DWORD flags) override {
        if (!Served()) return inner_->GetDeviceData(size, out, count, flags);
        if (!count) return DIERR_INVALIDPARAM;
        *count = 0;
        return DI_OK;
    }
    HRESULT STDMETHODCALLTYPE SetDataFormat(const DIDATAFORMAT* format) override {
        const HRESULT result = inner_->SetDataFormat(format);
        if (SUCCEEDED(result)) {
            std::vector<gamepads::NeutralState::Field> fields;
            for (DWORD i = 0; i < format->dwNumObjs; ++i) {
                const auto* object = reinterpret_cast<const DIOBJECTDATAFORMAT*>(
                    reinterpret_cast<const BYTE*>(format->rgodf) + i * format->dwObjSize);
                const auto type = DIDFT_GETTYPE(object->dwType);
                if (!(type & (DIDFT_AXIS | DIDFT_POV))) continue;
                const auto kind = (type & DIDFT_AXIS) ? gamepads::NeutralState::Kind::Axis :
                    gamepads::NeutralState::Kind::Pov;
                typename Api::Object resolved{};
                resolved.dwSize = sizeof(resolved);
                const DWORD id = SUCCEEDED(inner_->GetObjectInfo(&resolved, object->dwOfs, DIPH_BYOFFSET)) ?
                    resolved.dwType : DIDFT_GETTYPE(object->dwType) | DIDFT_MAKEINSTANCE(DIDFT_GETINSTANCE(object->dwType));
                fields.push_back({object->dwOfs, id, kind});
            }
            neutral_.SetFormat(format->dwDataSize, std::move(fields));
        }
        return result;
    }
    HRESULT STDMETHODCALLTYPE SetProperty(REFGUID property, const DIPROPHEADER* header) override {
        const HRESULT result = inner_->SetProperty(property, header);
        // DIPROP_* are pseudo GUID references (small integer addresses).
        if (SUCCEEDED(result) && &property == &DIPROP_RANGE) {
            const auto* range = reinterpret_cast<const DIPROPRANGE*>(header);
            const gamepads::AxisRange values{range->lMin, range->lMax};
            if (header->dwHow == DIPH_DEVICE) neutral_.SetRange(values);
            else {
                typename Api::Object object{};
                object.dwSize = sizeof(object);
                if (SUCCEEDED(inner_->GetObjectInfo(&object, header->dwObj, header->dwHow)))
                    neutral_.SetRange(object.dwType, values);
            }
        }
        return result;
    }
    HRESULT STDMETHODCALLTYPE GetCapabilities(DIDEVCAPS* a) override { return inner_->GetCapabilities(a); }
    HRESULT STDMETHODCALLTYPE EnumObjects(typename Api::EnumObjects a, void* b, DWORD c) override { return inner_->EnumObjects(a,b,c); }
    HRESULT STDMETHODCALLTYPE GetProperty(REFGUID a, DIPROPHEADER* b) override { return inner_->GetProperty(a,b); }
    HRESULT STDMETHODCALLTYPE Acquire() override {
        // While the wizard reads the pad through SDL, the game polls it as
        // lost (sub_6D8C90 zeroes its state) instead of taking it back.
        std::lock_guard lock(Lent().mutex);
        if (LentLocked(path_)) { inner_->Unacquire(); return DIERR_OTHERAPPHASPRIO; }
        return inner_->Acquire();
    }
    HRESULT STDMETHODCALLTYPE Unacquire() override { return inner_->Unacquire(); }
    HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE a) override { return inner_->SetEventNotification(a); }
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND a, DWORD b) override { return inner_->SetCooperativeLevel(a,b); }
    HRESULT STDMETHODCALLTYPE GetObjectInfo(typename Api::Object* a, DWORD b, DWORD c) override { return inner_->GetObjectInfo(a,b,c); }
    HRESULT STDMETHODCALLTYPE GetDeviceInfo(typename Api::Instance* a) override { return inner_->GetDeviceInfo(a); }
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND a, DWORD b) override { return inner_->RunControlPanel(a,b); }
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE a, DWORD b, REFGUID c) override { return inner_->Initialize(a,b,c); }
    HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID a, const DIEFFECT* b, IDirectInputEffect** c, IUnknown* d) override { return inner_->CreateEffect(a,b,c,d); }
    HRESULT STDMETHODCALLTYPE EnumEffects(typename Api::EnumEffects a, void* b, DWORD c) override { return inner_->EnumEffects(a,b,c); }
    HRESULT STDMETHODCALLTYPE GetEffectInfo(typename Api::EffectInfo* a, REFGUID b) override { return inner_->GetEffectInfo(a,b); }
    HRESULT STDMETHODCALLTYPE GetForceFeedbackState(DWORD* a) override { return inner_->GetForceFeedbackState(a); }
    HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD a) override { return inner_->SendForceFeedbackCommand(a); }
    HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK a, void* b, DWORD c) override { return inner_->EnumCreatedEffectObjects(a,b,c); }
    HRESULT STDMETHODCALLTYPE Escape(DIEFFESCAPE* a) override { return inner_->Escape(a); }
    HRESULT STDMETHODCALLTYPE Poll() override { return inner_->Poll(); }
    HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD a, const DIDEVICEOBJECTDATA* b, DWORD* c, DWORD d) override { return inner_->SendDeviceData(a,b,c,d); }
    HRESULT STDMETHODCALLTYPE EnumEffectsInFile(typename Api::String a, LPDIENUMEFFECTSINFILECALLBACK b, void* c, DWORD d) override { return inner_->EnumEffectsInFile(a,b,c,d); }
    HRESULT STDMETHODCALLTYPE WriteEffectToFile(typename Api::String a, DWORD b, DIFILEEFFECT* c, DWORD d) override { return inner_->WriteEffectToFile(a,b,c,d); }
    HRESULT STDMETHODCALLTYPE BuildActionMap(typename Api::ActionFormat* a, typename Api::String b, DWORD c) override { return inner_->BuildActionMap(a,b,c); }
    HRESULT STDMETHODCALLTYPE SetActionMap(typename Api::ActionFormat* a, typename Api::String b, DWORD c) override { return inner_->SetActionMap(a,b,c); }
    HRESULT STDMETHODCALLTYPE GetImageInfo(typename Api::ImageInfo* a) override { return inner_->GetImageInfo(a); }
private:
    const std::string& Path() const override { return path_; }
    void LetGo() override { inner_->Unacquire(); }
    bool Served() const { return info_.dwSize && serves_(info_.guidInstance, info_.guidProduct.Data1, path_); }
    const Serves serves_;
    const std::string path_;
    typename Api::Instance info_{};
    gamepads::NeutralState neutral_;
};

template<class Api>
class Input final : public OwnedCom<typename Api::Input> {
    using Base = OwnedCom<typename Api::Input>;
    using Base::inner_;
public:
    Input(typename Api::Input* inner, Serves serves) : Base(inner, Api::InputIid()), serves_(serves) {}
    HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID guid, typename Api::Device** out, IUnknown* outer) override {
        const HRESULT result = inner_->CreateDevice(guid, out, outer);
        if (FAILED(result) || !out || !*out || outer) return result;
        auto* wrapped = new(std::nothrow) Device<Api>(*out, serves_);
        if (!wrapped) { (*out)->Release(); *out = nullptr; return E_OUTOFMEMORY; }
        *out = wrapped;
        return result;
    }
    HRESULT STDMETHODCALLTYPE EnumDevices(DWORD type, typename Api::EnumDevices callback, void* context, DWORD flags) override {
        if (!callback) return inner_->EnumDevices(type, callback, context, flags);
        if (serves_ == ServesDevice && type == DI8DEVCLASS_GAMECTRL) {
            std::lock_guard lock(controllersMutex); nativeControllers.clear();
        }
        Enumeration enumeration{*this, callback, context};
        return inner_->EnumDevices(type, Enumerate, &enumeration, flags);
    }
    HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID a) override { return inner_->GetDeviceStatus(a); }
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND a, DWORD b) override { return inner_->RunControlPanel(a,b); }
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE a, DWORD b) override { return inner_->Initialize(a,b); }
    HRESULT STDMETHODCALLTYPE FindDevice(REFGUID a, typename Api::String b, GUID* c) override { return inner_->FindDevice(a,b,c); }
    HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(typename Api::String a, typename Api::ActionFormat* b, typename Api::Semantics c, void* d, DWORD e) override { return inner_->EnumDevicesBySemantics(a,b,c,d,e); }
    HRESULT STDMETHODCALLTYPE ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK a, typename Api::ConfigureParams* b, DWORD c, void* d) override { return inner_->ConfigureDevices(a,b,c,d); }
private:
    std::string Path(const GUID& instance) {
        std::lock_guard lock(pathsMutex_);
        const auto cached = paths_.find(instance);
        if (cached != paths_.end()) return cached->second;
        typename Api::Device* device = nullptr;
        std::string path;
        const HRESULT result = inner_->CreateDevice(instance, &device, nullptr);
        if (SUCCEEDED(result) && device) {
            path = DevicePath(device);
            device->Release();
        }
        paths_.emplace(instance, path);
        return path;
    }
    struct Enumeration { Input& input; typename Api::EnumDevices callback; void* context; };
    static BOOL CALLBACK Enumerate(const typename Api::Instance* device, void* context) {
        auto& enumeration = *static_cast<Enumeration*>(context);
        const auto path = enumeration.input.Path(device->guidInstance);
        if (enumeration.input.serves_(device->guidInstance, device->guidProduct.Data1, path)) return DIENUM_CONTINUE;
        if (enumeration.input.serves_ == ServesDevice && GET_DIDEVICE_TYPE(device->dwDevType) >= DI8DEVTYPE_JOYSTICK) {
            std::lock_guard lock(controllersMutex);
            nativeControllers.push_back({4 + static_cast<int>(nativeControllers.size()),
                Utf8(device->tszInstanceName), Utf8(device->tszProductName), path});
        }
        return enumeration.callback(device, enumeration.context);
    }
    const Serves serves_;
    std::mutex pathsMutex_;
    std::map<GUID, std::string, GuidLess> paths_;
};

template<class Wrapper, class Interface>
HRESULT WrapOwned(void** object, Serves serves) {
    auto* wrapped = new(std::nothrow) Wrapper(static_cast<Interface*>(*object), serves);
    if (!wrapped) return E_OUTOFMEMORY;
    *object = static_cast<Interface*>(wrapped);
    return S_OK;
}

bool ServesDevice(const GUID& instance, DWORD product, const std::string& path) {
    // ReadPads releases the layer lock before logging or calling any COM method.
    const auto pads = Gamepads::ReadPads();
    if (!gamepads::Served(product, path, pads)) return false;
    static std::mutex logMutex;
    static std::set<GUID, GuidLess> hidden;
    std::lock_guard lock(logMutex);
    if (hidden.insert(instance).second) {
        spdlog::info("Input: hiding DirectInput copy {:04X}:{:04X}", product & 0xFFFF, product >> 16);
    }
    return true;
}
}

HRESULT Wrap(REFIID iid, void** object, Serves serves) {
    if (!object || !*object || !serves) return E_INVALIDARG;
    if (iid == IID_IDirectInput8A) return WrapOwned<Input<Ansi>, Ansi::Input>(object, serves);
    if (iid == IID_IDirectInput8W) return WrapOwned<Input<Wide>, Wide::Input>(object, serves);
    if (iid == IID_IDirectInputDevice8A) return WrapOwned<Device<Ansi>, Ansi::Device>(object, serves);
    if (iid == IID_IDirectInputDevice8W) return WrapOwned<Device<Wide>, Wide::Device>(object, serves);
    return E_NOINTERFACE;
}

std::vector<gamepads::NativeController> ReadControllers() {
    std::lock_guard lock(controllersMutex);
    return nativeControllers;
}

void LendToSetup(const std::vector<std::string>& paths) {
    auto& lent = Lent();
    std::lock_guard lock(lent.mutex);
    lent.paths.clear();
    for (const auto& path : paths) if (!path.empty()) lent.paths.push_back(path);
    for (auto* device : lent.devices) if (LentLocked(device->Path())) device->LetGo();
}

HRESULT WINAPI Create(HINSTANCE instance, DWORD version, REFIID iid, void** out, IUnknown* outer) {
    const auto original = reinterpret_cast<decltype(&DirectInput8Create)>(originalCreate.load());
    const HRESULT result = original(instance, version, iid, out, outer);
    if (FAILED(result) || !out || !*out || outer || (iid != IID_IDirectInput8A && iid != IID_IDirectInput8W)) return result;
    const HRESULT wrapped = Wrap(iid, out, ServesDevice);
    if (FAILED(wrapped)) { static_cast<IUnknown*>(*out)->Release(); *out = nullptr; return wrapped; }
    return result;
}

} }
