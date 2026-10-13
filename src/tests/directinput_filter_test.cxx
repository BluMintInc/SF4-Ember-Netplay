#include "../sf4e/sf4e__DirectInputFilter.hxx"
#include "../common/DirectInputState.hxx"
#include "test_support.hxx"
#include <climits>
#include <iostream>

namespace filter = sf4e::DirectInputFilter;
namespace pads = sf4e::gamepads;

struct Ansi {
    using Device = IDirectInputDevice8A; using Input = IDirectInput8A;
    using Instance = DIDEVICEINSTANCEA; using Object = DIDEVICEOBJECTINSTANCEA;
    using Objects = LPDIENUMDEVICEOBJECTSCALLBACKA; using Effects = LPDIENUMEFFECTSCALLBACKA;
    using Effect = DIEFFECTINFOA; using Actions = DIACTIONFORMATA;
    using Image = DIDEVICEIMAGEINFOHEADERA; using String = LPCSTR;
    using Devices = LPDIENUMDEVICESCALLBACKA; using Semantics = LPDIENUMDEVICESBYSEMANTICSCBA;
    using Configure = DICONFIGUREDEVICESPARAMSA;
    static const GUID& DeviceIid() { return IID_IDirectInputDevice8A; }
    static const GUID& InputIid() { return IID_IDirectInput8A; }
    static const GUID& SiblingDeviceIid() { return IID_IDirectInputDevice8W; }
    static const GUID& SiblingInputIid() { return IID_IDirectInput8W; }
};
struct Wide {
    using Device = IDirectInputDevice8W; using Input = IDirectInput8W;
    using Instance = DIDEVICEINSTANCEW; using Object = DIDEVICEOBJECTINSTANCEW;
    using Objects = LPDIENUMDEVICEOBJECTSCALLBACKW; using Effects = LPDIENUMEFFECTSCALLBACKW;
    using Effect = DIEFFECTINFOW; using Actions = DIACTIONFORMATW;
    using Image = DIDEVICEIMAGEINFOHEADERW; using String = LPCWSTR;
    using Devices = LPDIENUMDEVICESCALLBACKW; using Semantics = LPDIENUMDEVICESBYSEMANTICSCBW;
    using Configure = DICONFIGUREDEVICESPARAMSW;
    static const GUID& DeviceIid() { return IID_IDirectInputDevice8W; }
    static const GUID& InputIid() { return IID_IDirectInput8W; }
    static const GUID& SiblingDeviceIid() { return IID_IDirectInputDevice8A; }
    static const GUID& SiblingInputIid() { return IID_IDirectInput8A; }
};

// Kept on the fixture's stack. The wrapper must retire exactly the one
// reference transferred to it, regardless of its own AddRef/QI traffic.
template<class Api>
struct FakeDevice : Api::Device {
    ULONG refs = 1, added = 0, released = 0;
    unsigned queries = 0;
    unsigned reads = 0, events = 0, polls = 0, acquires = 0;
    HRESULT propertyResult = DI_OK, formatResult = DI_OK;
    std::vector<DIOBJECTDATAFORMAT> objects;
    typename Api::Instance info{};
    FakeDevice() {
        info.dwSize = sizeof(info); info.guidInstance.Data1 = 42;
        info.guidProduct.Data1 = 0x05C4054C;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        ++queries;
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != Api::DeviceIid()) return E_NOINTERFACE;
        *out = static_cast<typename Api::Device*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { ++added; return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { ++released; return --refs; }
    HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD size, void* out) override {
        ++reads; if (out) std::memset(out, 0xA5, size); return DIERR_INPUTLOST;
    }
    HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD, DIDEVICEOBJECTDATA* out, DWORD* count, DWORD) override {
        ++events; if (out) out->dwData = 0x80; if (count) *count = 1; return DI_BUFFEROVERFLOW;
    }
    HRESULT STDMETHODCALLTYPE SetDataFormat(const DIDATAFORMAT* format) override {
        if (SUCCEEDED(formatResult)) objects.assign(format->rgodf, format->rgodf + format->dwNumObjs);
        return formatResult;
    }
    HRESULT STDMETHODCALLTYPE SetProperty(REFGUID, const DIPROPHEADER*) override { return propertyResult; }
    HRESULT STDMETHODCALLTYPE GetObjectInfo(typename Api::Object* out, DWORD object, DWORD how) override {
        for (const auto& field : objects) {
            if ((how == DIPH_BYOFFSET && field.dwOfs == object) || (how == DIPH_BYID && field.dwType == object) ||
                (how == DIPH_BYUSAGE && object == MAKELONG(0x30, 1) && field.dwOfs == 0)) {
                out->dwType = field.dwType; out->dwOfs = field.dwOfs; return DI_OK;
            }
        }
        return DIERR_OBJECTNOTFOUND;
    }
    HRESULT STDMETHODCALLTYPE GetDeviceInfo(typename Api::Instance* out) override { *out = info; return DI_OK; }
    HRESULT STDMETHODCALLTYPE Acquire() override { ++acquires; return DIERR_OTHERAPPHASPRIO; }
    HRESULT STDMETHODCALLTYPE Poll() override { ++polls; return DI_NOEFFECT; }
    HRESULT STDMETHODCALLTYPE GetCapabilities(DIDEVCAPS*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumObjects(typename Api::Objects, void*, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetProperty(REFGUID, DIPROPHEADER*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Unacquire() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE, DWORD, REFGUID) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID, const DIEFFECT*, IDirectInputEffect**, IUnknown*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumEffects(typename Api::Effects, void*, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetEffectInfo(typename Api::Effect*, REFGUID) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetForceFeedbackState(DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK, void*, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Escape(DIEFFESCAPE*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD, const DIDEVICEOBJECTDATA*, DWORD*, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumEffectsInFile(typename Api::String, LPDIENUMEFFECTSINFILECALLBACK, void*, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE WriteEffectToFile(typename Api::String, DWORD, DIFILEEFFECT*, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE BuildActionMap(typename Api::Actions*, typename Api::String, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetActionMap(typename Api::Actions*, typename Api::String, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetImageInfo(typename Api::Image*) override { return E_NOTIMPL; }
};

template<class Api>
struct FakeInput : Api::Input {
    ULONG refs = 1;
    unsigned queries = 0;
    FakeDevice<Api>& device;
    explicit FakeInput(FakeDevice<Api>& device) : device(device) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** out) override { ++queries; if(out)*out=nullptr; return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
    HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID, typename Api::Device** out, IUnknown*) override {
        *out = &device; device.AddRef(); return DI_OK;
    }
    HRESULT STDMETHODCALLTYPE EnumDevices(DWORD, typename Api::Devices callback, void* context, DWORD) override {
        if (!callback) return DIERR_INVALIDPARAM;
        auto instance = device.info;
        if (callback(&instance, context) == DIENUM_STOP) return DI_OK;
        instance.guidInstance.Data1 = 43; instance.guidProduct.Data1 = 0x56781234;
        callback(&instance, context); return DI_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID) override { return DI_NOTATTACHED; }
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE FindDevice(REFGUID, typename Api::String, GUID*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(typename Api::String, typename Api::Actions*, typename Api::Semantics, void*, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK, typename Api::Configure*, DWORD, void*) override { return E_NOTIMPL; }
};

static std::vector<pads::Pad> inventory;
static bool Serves(const GUID&, DWORD product) { return pads::Served(product, inventory); }
static pads::Pad Controller() {
    pads::Pad pad; pad.vid = 0x054C; pad.pid = 0x05C4; pad.status = pads::Status::Slotted; return pad;
}

static void Decision() {
    const DWORD product = 0x05C4054C;
    auto pad = Controller();
    CHECK(!pads::Served(product, {}));
    for (auto status : {pads::Status::Slotted, pads::Status::NotInUse, pads::Status::SteamTwin,
        pads::Status::Unmapped, pads::Status::GameRead}) {
        pad.status = status;
        CHECK(pads::Served(product, {pad}) == (status != pads::Status::Unmapped && status != pads::Status::GameRead));
        CHECK(!pads::Served(0x054C05C4, {pad}));
        CHECK(!pads::Served(product + 1, {pad}));
    }
    pad.status = pads::Status::NotInUse; pad.available = false;
    CHECK(!pads::Served(product, {pad}));
}

struct JoystickFormat {
    std::vector<DIOBJECTDATAFORMAT> objects;
    DIDATAFORMAT format{};
    explicit JoystickFormat(bool extended) {
        for (DWORD i = 0; i < 8; ++i) objects.push_back({nullptr, i * sizeof(LONG), DWORD(DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(i)), 0});
        for (DWORD i = 0; i < 4; ++i) objects.push_back({nullptr, DWORD(offsetof(DIJOYSTATE, rgdwPOV)) + i * sizeof(DWORD), DWORD(DIDFT_POV | DIDFT_MAKEINSTANCE(i)), 0});
        for (DWORD i = 0; i < (extended ? 128u : 32u); ++i)
            objects.push_back({nullptr, DWORD(offsetof(DIJOYSTATE, rgbButtons)) + i, DWORD(DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(i)), 0});
        format = {sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_ABSAXIS,
            DWORD(extended ? sizeof(DIJOYSTATE2) : sizeof(DIJOYSTATE)), DWORD(objects.size()), objects.data()};
    }
};

static void NeutralBuilder() {
    CHECK((pads::AxisRange{INT_MIN, INT_MAX}.Centre() == 0));
    CHECK((pads::AxisRange{INT_MAX-2, INT_MAX}.Centre() == INT_MAX-1));
    pads::NeutralState builder;
    builder.SetFormat(12, {{0, 7, pads::NeutralState::Kind::Axis}, {4, 8, pads::NeutralState::Kind::Pov}});
    std::array<DWORD, 3> state{1,2,3};
    CHECK(builder.Write(sizeof(state), state.data()));
    CHECK(state[0] == 32767 && state[1] == 0xFFFFFFFF && state[2] == 0);
    builder.SetRange({-100, 300}); builder.SetRange(7, {-30, 10});
    CHECK(builder.Write(sizeof(state), state.data()) && LONG(state[0]) == -10);
    builder.SetRange({0,100});
    CHECK(builder.Write(sizeof(state), state.data()) && state[0] == 50);
    CHECK(!builder.Write(8, state.data()) && !builder.Write(sizeof(state), nullptr));
}

template<class Api>
static void DeviceWrapper() {
    FakeDevice<Api> inner;
    void* object = static_cast<typename Api::Device*>(&inner);
    CHECK(filter::Wrap(Api::DeviceIid(), &object, Serves) == S_OK);
    auto* device = static_cast<typename Api::Device*>(object);
    CHECK(inner.refs == 1 && inner.added == 0);
    void* queried = nullptr;
    CHECK(device->QueryInterface(Api::DeviceIid(), &queried) == S_OK && queried == object);
    CHECK(static_cast<IUnknown*>(queried)->Release() == 1);
    CHECK(device->QueryInterface(IID_IUnknown, &queried) == S_OK && queried == object);
    CHECK(static_cast<IUnknown*>(queried)->Release() == 1);
    CHECK(device->QueryInterface(IID_IDirectInputEffect, &queried) == E_NOINTERFACE && !queried);
    queried = object;
    CHECK(device->QueryInterface(Api::SiblingDeviceIid(), &queried) == E_NOINTERFACE && !queried && !inner.queries);
    CHECK(device->QueryInterface(IID_IUnknown, nullptr) == E_POINTER);
    CHECK(device->AddRef() == 2 && device->Release() == 1);

    JoystickFormat layout(true);
    CHECK(device->SetDataFormat(&layout.format) == DI_OK);
    DIJOYSTATE2 state{};
    inventory.clear();
    CHECK(device->GetDeviceState(sizeof(state), &state) == DIERR_INPUTLOST && inner.reads == 1);
    CHECK(state.rgbButtons[0] == 0xA5);
    inventory = {Controller()};
    CHECK(device->GetDeviceState(sizeof(state), &state) == DI_OK && inner.reads == 1);
    CHECK(state.lX == 32767 && state.lY == 32767 && state.lZ == 32767 && state.lRx == 32767 &&
        state.lRy == 32767 && state.lRz == 32767 && state.rglSlider[0] == 32767 && state.rglSlider[1] == 32767);
    for (auto pov : state.rgdwPOV) CHECK(pov == 0xFFFFFFFF);
    for (auto button : state.rgbButtons) CHECK(button == 0);
    CHECK(!state.lVX && !state.lAX && !state.lFX);
    DIPROPRANGE range{{sizeof(DIPROPRANGE), sizeof(DIPROPHEADER), 0, DIPH_DEVICE}, -100, 300};
    CHECK(device->SetProperty(DIPROP_RANGE, &range.diph) == DI_OK);
    range.diph.dwHow = DIPH_BYOFFSET; range.diph.dwObj = offsetof(DIJOYSTATE, lY); range.lMin = -30; range.lMax = 10;
    CHECK(device->SetProperty(DIPROP_RANGE, &range.diph) == DI_OK);
    CHECK(device->GetDeviceState(sizeof(state), &state) == DI_OK && state.lX == 100 && state.lY == -10 && state.lZ == 100);
    range.diph.dwHow = DIPH_BYID; range.diph.dwObj = DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(2); range.lMin = 0; range.lMax = 1000;
    CHECK(device->SetProperty(DIPROP_RANGE, &range.diph) == DI_OK);
    CHECK(device->GetDeviceState(sizeof(state), &state) == DI_OK && state.lZ == 500);
    range.diph.dwHow = DIPH_BYUSAGE; range.diph.dwObj = MAKELONG(0x30, 1); range.lMin = -10; range.lMax = 10;
    CHECK(device->SetProperty(DIPROP_RANGE, &range.diph) == DI_OK);
    CHECK(device->GetDeviceState(sizeof(state), &state) == DI_OK && state.lX == 0);
    inner.propertyResult = DIERR_INVALIDPARAM; range.lMin = 100; range.lMax = 200;
    CHECK(device->SetProperty(DIPROP_RANGE, &range.diph) == DIERR_INVALIDPARAM);
    CHECK(device->GetDeviceState(sizeof(state), &state) == DI_OK && state.lX == 0);
    CHECK(device->GetDeviceState(sizeof(state), nullptr) == DIERR_INVALIDPARAM);
    CHECK(device->GetDeviceState(sizeof(state)-1, &state) == DIERR_INVALIDPARAM);
    CHECK(device->Acquire() == DIERR_OTHERAPPHASPRIO && inner.acquires == 1);
    CHECK(device->Poll() == DI_NOEFFECT && inner.polls == 1);

    DWORD count = INFINITE; DIDEVICEOBJECTDATA event{};
    CHECK(device->GetDeviceData(sizeof(event), &event, &count, DIGDD_PEEK) == DI_OK && !count && !inner.events);
    CHECK(device->GetDeviceData(sizeof(event), nullptr, &count, 0) == DI_OK && !count);
    CHECK(device->GetDeviceData(sizeof(event), &event, nullptr, 0) == DIERR_INVALIDPARAM);
    inventory.front().status = pads::Status::GameRead;
    CHECK(device->GetDeviceState(sizeof(state), &state) == DIERR_INPUTLOST && inner.reads == 2);
    CHECK(device->GetDeviceData(sizeof(event), &event, &count, 0) == DI_BUFFEROVERFLOW && count == 1 && inner.events == 1);

    // A mapped pad becomes served through the same already-created handle.
    inventory.front().status = pads::Status::Unmapped;
    CHECK(device->GetDeviceState(sizeof(state), &state) == DIERR_INPUTLOST);
    inventory.front().status = pads::Status::Slotted;
    JoystickFormat shortLayout(false);
    CHECK(device->SetDataFormat(&shortLayout.format) == DI_OK);
    DIJOYSTATE shortState{};
    CHECK(device->GetDeviceState(sizeof(shortState), &shortState) == DI_OK && shortState.lY == -10);
    // A nonstandard layout follows field offsets, not sizeof(DIJOYSTATE).
    std::array<DIOBJECTDATAFORMAT, 3> fields{{{nullptr, 8, DIDFT_ABSAXIS, 0},
        {nullptr, 0, DIDFT_POV, 0}, {nullptr, 4, DIDFT_PSHBUTTON, 0}}};
    DIDATAFORMAT custom{sizeof(custom), sizeof(DIOBJECTDATAFORMAT), DIDF_ABSAXIS, 12, DWORD(fields.size()), fields.data()};
    CHECK(device->SetDataFormat(&custom) == DI_OK);
    std::array<DWORD, 3> customState{1,2,3};
    CHECK(device->GetDeviceState(sizeof(customState), customState.data()) == DI_OK);
    CHECK(customState[0] == 0xFFFFFFFF && customState[1] == 0 && customState[2] == 0);
    inner.formatResult = DIERR_ACQUIRED;
    CHECK(device->SetDataFormat(&layout.format) == DIERR_ACQUIRED);
    CHECK(device->GetDeviceState(sizeof(customState), customState.data()) == DI_OK);
    CHECK(device->Release() == 0 && inner.refs == 0 && inner.released == 1 && inner.added == 0);
}

template<class Api>
static BOOL CALLBACK Count(const typename Api::Instance*, void* context) {
    ++*static_cast<unsigned*>(context); return DIENUM_CONTINUE;
}
template<class Api>
static BOOL CALLBACK Stop(const typename Api::Instance*, void* context) {
    ++*static_cast<unsigned*>(context); return DIENUM_STOP;
}
template<class Api>
static void InputWrapper() {
    FakeDevice<Api> device; FakeInput<Api> inner(device);
    void* object = static_cast<typename Api::Input*>(&inner);
    CHECK(filter::Wrap(Api::InputIid(), &object, Serves) == S_OK);
    auto* input = static_cast<typename Api::Input*>(object);
    void* queried = nullptr;
    CHECK(input->QueryInterface(Api::InputIid(), &queried) == S_OK && queried == object);
    CHECK(static_cast<IUnknown*>(queried)->Release() == 1);
    queried = object;
    CHECK(input->QueryInterface(Api::SiblingInputIid(), &queried) == E_NOINTERFACE && !queried && !inner.queries);
    unsigned seen = 0;
    inventory.clear();
    CHECK(input->EnumDevices(DI8DEVCLASS_GAMECTRL, Count<Api>, &seen, 0) == DI_OK && seen == 2);
    inventory = {Controller()}; seen = 0;
    CHECK(input->EnumDevices(DI8DEVCLASS_GAMECTRL, Count<Api>, &seen, 0) == DI_OK && seen == 1);
    inventory.clear(); seen = 0;
    CHECK(input->EnumDevices(DI8DEVCLASS_GAMECTRL, Stop<Api>, &seen, 0) == DI_OK && seen == 1);
    CHECK(input->EnumDevices(DI8DEVCLASS_GAMECTRL, nullptr, nullptr, 0) == DIERR_INVALIDPARAM);
    CHECK(input->GetDeviceStatus(device.info.guidInstance) == DI_NOTATTACHED);
    typename Api::Device* created = nullptr;
    CHECK(input->CreateDevice(device.info.guidInstance, &created, nullptr) == DI_OK && created != &device && device.refs == 2);
    JoystickFormat layout(false);
    CHECK(created->SetDataFormat(&layout.format) == DI_OK);
    inventory = {Controller()}; DIJOYSTATE state{};
    CHECK(created->GetDeviceState(sizeof(state), &state) == DI_OK && !device.reads);
    CHECK(created->Release() == 0 && device.refs == 1);
    CHECK(input->Release() == 0 && inner.refs == 0);
    CHECK(device.Release() == 0);
}

static FakeInput<Ansi>* factoryInput = nullptr;
static HRESULT factoryResult = S_FALSE;
static HRESULT WINAPI NativeCreate(HINSTANCE instance, DWORD version, REFIID iid, void** out, IUnknown* outer) {
    CHECK(instance == reinterpret_cast<HINSTANCE>(0x1234) && version == 0x0800 && !outer);
    CHECK(iid == IID_IDirectInput8A);
    if (FAILED(factoryResult)) { *out = nullptr; return factoryResult; }
    factoryInput->AddRef(); *out = static_cast<IDirectInput8A*>(factoryInput); return factoryResult;
}
static void EarlyFactory() {
    FakeDevice<Ansi> device; FakeInput<Ansi> input(device);
    factoryInput = &input;
    const auto previous = filter::originalCreate.exchange(reinterpret_cast<void*>(&NativeCreate));
    void* out = nullptr;
    // No layer Start has run. The installed factory wraps the first objects,
    // but enumeration and existing device reads still preserve native input.
    CHECK(filter::Create(reinterpret_cast<HINSTANCE>(0x1234), 0x0800, IID_IDirectInput8A, &out, nullptr) == S_FALSE);
    auto* wrapped = static_cast<IDirectInput8A*>(out);
    CHECK(wrapped != &input && input.refs == 2);
    unsigned seen = 0;
    CHECK(wrapped->EnumDevices(DI8DEVCLASS_GAMECTRL, Count<Ansi>, &seen, 0) == DI_OK && seen == 2);
    IDirectInputDevice8A* created = nullptr;
    CHECK(wrapped->CreateDevice(device.info.guidInstance, &created, nullptr) == DI_OK && device.refs == 2);
    DIJOYSTATE state{};
    CHECK(created->GetDeviceState(sizeof(state), &state) == DIERR_INPUTLOST && device.reads == 1);
    CHECK(created->Release() == 0 && device.refs == 1);
    CHECK(wrapped->Release() == 0 && input.refs == 1);
    factoryResult = E_FAIL;
    CHECK(filter::Create(reinterpret_cast<HINSTANCE>(0x1234), 0x0800, IID_IDirectInput8A, &out, nullptr) == E_FAIL && !out);
    CHECK(input.refs == 1);
    filter::originalCreate.store(previous);
    CHECK(input.Release() == 0 && device.Release() == 0);
}

int main() {
    Decision(); NeutralBuilder();
    DeviceWrapper<Ansi>(); DeviceWrapper<Wide>();
    InputWrapper<Ansi>(); InputWrapper<Wide>();
    EarlyFactory();
    std::cout << "DirectInput filtering, neutral layouts/ranges, live serving changes and A/W COM ownership passed\n";
}
