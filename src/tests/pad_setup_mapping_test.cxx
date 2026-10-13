#include "../common/PadSetup.hxx"
#include "../netplay/ControllerMappings.hxx"
#include <SDL3/SDL.h>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
using namespace sf4e::gamepads;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (0)
int Step(const char* name) {
    for (int i = 0; i < SetupStepCount; ++i) if (std::string(SetupSteps[i].name) == name) return i;
    throw std::runtime_error(name);
}
SetupState Record(std::initializer_list<std::pair<const char*, RawInput>> steps) {
    SetupState state;
    for (const auto& step : steps) state.recorded[Step(step.first)] = step.second;
    return state;
}
std::size_t Count(const std::string& text, const std::string& part) {
    std::size_t count = 0;
    for (auto at = text.find(part); at != std::string::npos; at = text.find(part, at + 1)) ++count;
    return count;
}
int main() {
    // The wizard's bindings read back as the inputs the merge compares, and name the same inputs.
    for (const RawInput input : {RawInput{RawKind::Button, 12}, RawInput{RawKind::HatLeft, 1}, RawInput{RawKind::AxisPlus, 2},
        RawInput{RawKind::AxisMinus, 0}, RawInput{RawKind::Trigger, 5}, RawInput{RawKind::Trigger, 5, false, true}}) {
        CHECK(ParseMappingInput(RawBinding(input)) == ToMappingInput(input));
        CHECK(ToRawInput(ToMappingInput(input)) == input);
    }
    using Kind = MappingInput::Kind;
    CHECK(ParseMappingInput("h0.3") == (MappingInput{Kind::Hat, 0, 3}) && ParseMappingInput("h1.15") == (MappingInput{Kind::Hat, 1, 15}));
    CHECK(ParseMappingInput("-a3~") == (MappingInput{Kind::Axis, 3, 0, '-', true}) && ParseMappingInput("b003") == (MappingInput{Kind::Button, 3}));
    for (const char* value : {"67fa", "abxy", "+b1", "b1~", "h0", "h0.0", "h0.16", "a", "a-1", "b2x", "", "~"}) CHECK(!ParseMappingInput(value));
    CHECK(!ToRawInput(MappingInput{Kind::Hat, 0, 3}));
    // Overlap: one button, hat masks that share a bit, and an axis whose halves are separate.
    const auto in = [](const char* value) { return *ParseMappingInput(value); };
    CHECK(Overlaps(in("b3"), in("b3")) && !Overlaps(in("b3"), in("b4")) && !Overlaps(in("b0"), in("a0")));
    CHECK(Overlaps(in("h0.3"), in("h0.1")) && Overlaps(in("h0.3"), in("h0.2")) && !Overlaps(in("h0.3"), in("h0.4")) && !Overlaps(in("h0.1"), in("h1.1")));
    CHECK(Overlaps(in("a2"), in("+a2")) && Overlaps(in("a2~"), in("-a2")) && Overlaps(in("+a2"), in("+a2~")));
    CHECK(!Overlaps(in("+a2"), in("-a2")) && !Overlaps(in("a2"), in("a3")));
    // Only outputs SDL knows, signed or not, are bindings; every other field is metadata.
    const auto parsed = ParseMapping("guid,Name,a:b0,+leftx:b1,misc6:b2,paddle4:h0.4,crc:b003,type:b4,sdk>=:2.0,face:abxy,x:junk,platform:Windows,");
    CHECK(parsed.bindings.size() == 4 && parsed.bindings[1].output == "+leftx" && MappingTarget(parsed.bindings[1].output) == "leftx");
    CHECK(parsed.metadata.size() == 5 && parsed.metadata[0].key == "crc" && parsed.metadata[2].key == "sdk>=" && parsed.metadata[4].key == "x");

    // An unmapped pad: exactly the recorded pairs, as before the merge existed.
    const std::string guid = "03000000790000001100000000000000";
    auto state = Record({{"up", {RawKind::HatUp, 0}}, {"down", {RawKind::HatDown, 0}}, {"left", {RawKind::AxisMinus, 1}}, {"right", {RawKind::AxisPlus, 1}},
        {"lp", {RawKind::Button, 0}}, {"mp", {RawKind::Button, 1}}, {"hp", {RawKind::Button, 2}}, {"lk", {RawKind::Button, 3}}, {"mk", {RawKind::Button, 4}},
        {"hk", {RawKind::Trigger, 5, false, true}}, {"3p", {RawKind::Button, 6}}, {"3k", {RawKind::Trigger, 6}}, {"start", {RawKind::Button, 8}}, {"select", {RawKind::Button, 9}}});
    const auto full = BuildSetupMapping(guid, "Full pad", state);
    CHECK(full == guid + ",Full pad,dpup:h0.1,dpdown:h0.4,dpleft:-a1,dpright:+a1,x:b0,y:b1,rightshoulder:b2,a:b3,b:b4,righttrigger:a5~,leftshoulder:b6,lefttrigger:a6,start:b8,back:b9,platform:Windows,");
    CHECK(BuildSetupMapping(guid, "Full pad", state, "") == full);
    state.recorded[Step("3p")].reset(); state.recorded[Step("3k")].reset(); state.recorded[Step("select")].reset();
    state.recorded[Step("hk")] = RawInput{RawKind::Button, 5};
    const auto stick = BuildSetupMapping(guid, "Six button stick", state);
    CHECK(stick == guid + ",Six button stick,dpup:h0.1,dpdown:h0.4,dpleft:-a1,dpright:+a1,x:b0,y:b1,rightshoulder:b2,a:b3,b:b4,righttrigger:b5,start:b8,platform:Windows,");
    CHECK(BuildSetupMapping(guid, "Driver,name", state).find(",Driver name,") != std::string::npos);

    // Stick steps map the full axis; a button or hat drives the plus half.
    state.recorded[Step("ls")] = RawInput{RawKind::Button, 11}; state.recorded[Step("lx")] = RawInput{RawKind::AxisPlus, 0};
    state.recorded[Step("ly")] = RawInput{RawKind::AxisMinus, 2}; state.recorded[Step("rx")] = RawInput{RawKind::Button, 7};
    state.recorded[Step("ry")] = RawInput{RawKind::HatDown, 1};
    const auto sticks = BuildSetupMapping(guid, "Stick pad", state);
    CHECK(sticks == guid + ",Stick pad,dpup:h0.1,dpdown:h0.4,dpleft:-a1,dpright:+a1,x:b0,y:b1,rightshoulder:b2,a:b3,b:b4,righttrigger:b5,"
        "leftstick:b11,leftx:a0,lefty:a2~,+rightx:b7,+righty:h1.4,start:b8,platform:Windows,");

    // The 2026-10-13 save over SDL's XInput mapping: 3P, 3K and the sticks skipped
    // keep LB, LT, LS, RS, both sticks and Guide from the existing mapping.
    const std::string steam = "0300fa67de280000ff11000000007801";
    const std::string xinput = steam + ",*,a:b0,b:b1,back:b6,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b10,leftshoulder:b4,leftstick:b8,"
        "lefttrigger:a2,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b9,righttrigger:a5,rightx:a3,righty:a4,start:b7,x:b2,y:b3,platform:Windows,";
    auto owner = Record({{"up", {RawKind::HatUp, 0}}, {"down", {RawKind::HatDown, 0}}, {"left", {RawKind::HatLeft, 0}}, {"right", {RawKind::HatRight, 0}},
        {"lp", {RawKind::Button, 2}}, {"mp", {RawKind::Button, 3}}, {"hp", {RawKind::Button, 5}}, {"lk", {RawKind::Button, 0}}, {"mk", {RawKind::Button, 1}},
        {"hk", {RawKind::Trigger, 5}}, {"start", {RawKind::Button, 7}}, {"select", {RawKind::Button, 6}}});
    const auto merged = BuildSetupMapping(steam, "Xbox 360 controller", owner, xinput);
    CHECK(merged == steam + ",Xbox 360 controller,dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,x:b2,y:b3,rightshoulder:b5,a:b0,b:b1,righttrigger:a5,start:b7,back:b6,"
        "guide:b10,leftshoulder:b4,leftstick:b8,lefttrigger:a2,leftx:a0,lefty:a1,rightstick:b9,rightx:a3,righty:a4,platform:Windows,");

    // A reused input leaves its old target: the stick as Left and Right drops
    // leftx, and Guide's button recorded as 3P drops guide.
    auto reused = owner;
    reused.recorded[LeftStep] = RawInput{RawKind::AxisMinus, 0}; reused.recorded[RightStep] = RawInput{RawKind::AxisPlus, 0};
    reused.recorded[Step("3p")] = RawInput{RawKind::Button, 10};
    const auto moved = BuildSetupMapping(steam, "Xbox 360 controller", reused, xinput);
    CHECK(moved.find(",dpleft:-a0,dpright:+a0,") != std::string::npos && moved.find(",leftshoulder:b10,") != std::string::npos);
    CHECK(Count(moved, "leftx") == 0 && Count(moved, "guide") == 0 && Count(moved, "leftshoulder") == 1);
    CHECK(moved.find(",lefty:a1,") != std::string::npos && moved.find(",lefttrigger:a2,") != std::string::npos);
    // A stick step takes its whole target family, so +rightx replaces rightx.
    reused.recorded[Step("rx")] = RawInput{RawKind::Button, 12};
    const auto half = BuildSetupMapping(steam, "Xbox 360 controller", reused, xinput);
    CHECK(half.find(",+rightx:b12,") != std::string::npos && Count(half, "rightx") == 1);

    // Unknown fields stay in place; platform is written once, last.
    const auto hinted = BuildSetupMapping(guid, "Hinted", Record({{"lk", {RawKind::Button, 3}}}),
        guid + ",Hinted,a:b0,crc:67fa,platform:Windows,hint:!SDL_GAMECONTROLLER_USE_BUTTON_LABELS:=1,type:xbox360,platform:Windows,");
    CHECK(hinted == guid + ",Hinted,a:b3,crc:67fa,hint:!SDL_GAMECONTROLLER_USE_BUTTON_LABELS:=1,type:xbox360,platform:Windows,");
    // A composite hat binding goes when a recorded direction reads one of its bits;
    // a hat binding sharing no bit stays.
    const std::string hatGuid = "03000000790000001200000000000000";
    const auto composite = BuildSetupMapping(hatGuid, "Hat pad", Record({{"up", {RawKind::HatUp, 0}}, {"right", {RawKind::HatRight, 0}}, {"lk", {RawKind::Button, 3}}}),
        hatGuid + ",Hat pad,leftshoulder:h0.3,rightshoulder:h0.4,lefttrigger:h0.6,a:b0,platform:Windows,");
    CHECK(composite == hatGuid + ",Hat pad,dpup:h0.1,dpright:h0.2,a:b3,rightshoulder:h0.4,platform:Windows,");
    // Binding-shaped metadata stays: crc:b003 with button 3 recorded, and hint and sdk>= fields.
    const std::string crcGuid = "03000000790000001300000000000000";
    const auto crc = BuildSetupMapping(crcGuid, "Crc pad", Record({{"lk", {RawKind::Button, 3}}}),
        crcGuid + ",Crc pad,a:b0,y:b3,crc:b003,hint:!SDL_GAMECONTROLLER_USE_BUTTON_LABELS:=1,sdk>=:21,platform:Windows,");
    CHECK(crc == crcGuid + ",Crc pad,a:b3,crc:b003,hint:!SDL_GAMECONTROLLER_USE_BUTTON_LABELS:=1,sdk>=:21,platform:Windows,");
    // SDL reads output names in any case: a mixed-case binding is still a binding,
    // so it goes when its input is reused or its target recorded, and a mixed-case
    // platform field is not written twice.
    CHECK(MappingTarget("LEFTSHOULDER") == "leftshoulder" && MappingTarget("+LeftX") == "leftx" && MappingTarget("Crc").empty());
    const std::string caseGuid = "03000000790000001400000000000000";
    const auto cased = BuildSetupMapping(caseGuid, "Case pad", Record({{"up", {RawKind::HatUp, 0}}, {"lk", {RawKind::Button, 3}}}),
        caseGuid + ",Case pad,LEFTSHOULDER:h0.1,A:b0,RightShoulder:b5,Platform:Windows,");
    CHECK(cased == caseGuid + ",Case pad,dpup:h0.1,a:b3,RightShoulder:b5,platform:Windows,");

    nlohmann::json prefs = {{"controllerMappings", {{guid, full}}}};
    CHECK(sf4e::netplay::ReadControllerMappings(nlohmann::json::parse(prefs.dump())).at(guid) == full);
    CHECK(sf4e::netplay::ReadControllerMappings({{"deviceType", 0}, {"deviceIdx", 2}}).empty());
    CHECK(sf4e::netplay::ReadControllerMappings({{"controllerMappings", {{guid, 5}}}}).empty());
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "0");
    CHECK(SDL_Init(SDL_INIT_GAMEPAD));
    for (const auto& mapping : {full, stick, sticks, moved, half, merged, hinted, composite, crc}) CHECK(SDL_AddGamepadMapping(mapping.c_str()) >= 0);
    char* mapping = SDL_GetGamepadMappingForGUID(SDL_StringToGUID(guid.c_str())); CHECK(mapping); SDL_free(mapping);
    // SDL adds the GUID's CRC to what it stores; merging over that keeps one crc and one platform.
    mapping = SDL_GetGamepadMappingForGUID(SDL_StringToGUID(steam.c_str())); CHECK(mapping);
    const std::string stored = mapping; SDL_free(mapping);
    CHECK(stored.find(",leftx:a0,") != std::string::npos && Count(stored, "crc:67fa") == 1);
    const auto again = BuildSetupMapping(steam, "Xbox 360 controller", owner, stored);
    CHECK(Count(again, "crc:") == 1 && Count(again, "platform:") == 1 && SDL_AddGamepadMapping(again.c_str()) >= 0);
    SDL_Quit(); std::cout << "Exact and merged mappings, preferences migration and SDL mapping acceptance passed\n";
}
