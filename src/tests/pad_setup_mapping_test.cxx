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
SdlInput In(const char* input) { return ParseSdlElement("a", input).value().input; }
static_assert(static_cast<int>(SdlBindType::Button) == SDL_GAMEPAD_BINDTYPE_BUTTON && static_cast<int>(SdlBindType::Axis) == SDL_GAMEPAD_BINDTYPE_AXIS &&
    static_cast<int>(SdlBindType::Hat) == SDL_GAMEPAD_BINDTYPE_HAT && SdlLeftTrigger == SDL_GAMEPAD_AXIS_LEFT_TRIGGER, "SDL binding types moved");
SdlBinding FromSdl(const SDL_GamepadBinding& sdl) {
    SdlBinding binding;
    binding.input.type = static_cast<SdlBindType>(sdl.input_type);
    if (sdl.input_type == SDL_GAMEPAD_BINDTYPE_BUTTON) binding.input.index = sdl.input.button;
    else if (sdl.input_type == SDL_GAMEPAD_BINDTYPE_HAT) { binding.input.index = sdl.input.hat.hat; binding.input.hatMask = sdl.input.hat.hat_mask; }
    else binding.input = {SdlBindType::Axis, sdl.input.axis.axis, sdl.input.axis.axis_min, sdl.input.axis.axis_max};
    if (sdl.output_type == SDL_GAMEPAD_BINDTYPE_BUTTON) binding.output = {SdlBindType::Button, sdl.output.button};
    else binding.output = {SdlBindType::Axis, sdl.output.axis.axis, sdl.output.axis.axis_min, sdl.output.axis.axis_max};
    return binding;
}
// SDL adds the mapping and binds it on the virtual pad. The port must read the
// same bindings (outputs, input types, indices, hat masks, axis ranges and
// inversion) from the mapping SDL stores, which is what the merge reads.
std::vector<SdlBinding> SdlBinds(SDL_JoystickID pad, const std::string& mapping) {
    if (SDL_AddGamepadMapping(mapping.c_str()) < 0) throw std::runtime_error("SDL rejected " + mapping);
    SDL_Gamepad* gamepad = SDL_OpenGamepad(pad); CHECK(gamepad);
    char* stored = SDL_GetGamepadMapping(gamepad); CHECK(stored);
    int count = 0; SDL_GamepadBinding** bindings = SDL_GetGamepadBindings(gamepad, &count); CHECK(bindings);
    std::vector<SdlBinding> sdl;
    for (int i = 0; i < count; ++i) sdl.push_back(FromSdl(*bindings[i]));
    const bool same = ReadSdlMapping(stored).bindings == sdl;
    SDL_free(bindings); SDL_free(stored); SDL_CloseGamepad(gamepad);
    if (!same) throw std::runtime_error("the port reads differently from SDL: " + mapping);
    return sdl;
}
int main() {
    // Overlap: one button, hat masks that share a bit, and axis ranges on one side of 0.
    CHECK(Overlaps(In("b3"), In("b3")) && !Overlaps(In("b3"), In("b4")) && !Overlaps(In("b0"), In("a0")));
    CHECK(Overlaps(In("h0.3"), In("h0.1")) && Overlaps(In("h0.3"), In("h0.2")) && !Overlaps(In("h0.3"), In("h0.4")) && !Overlaps(In("h0.1"), In("h1.1")));
    CHECK(Overlaps(In("h0.17"), In("h0.1")) && Overlaps(In("a2"), In("+a2")) && Overlaps(In("a2~"), In("-a2")) && Overlaps(In("+a2"), In("+a2~")));
    CHECK(!Overlaps(In("+a2"), In("-a2")) && !Overlaps(In("-a2~"), In("+a2")) && !Overlaps(In("a2"), In("a3")));
    // SDL stops reading at an over-long field; nothing from there on is read, bound or kept.
    const auto stopped = ReadSdlMapping("guid,Name,a:b0,crc:1234," + std::string(21, 'k') + ":v,b:b1,type:xbox,");
    CHECK(stopped.bindings.size() == 1 && stopped.metadata.size() == 1 && stopped.metadata[0].text == "crc:1234");
    CHECK(ReadSdlMapping("guid,Name,a:b0,x:b" + std::string(128, '1') + ",b:b1,").bindings.size() == 1);
    CHECK(ReadSdlMapping("guid,Name,a:b0,x:b" + std::string(127, '1') + ",b:b1,").bindings.size() == 3);

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

    // Fields SDL does not bind stay as written; platform is written once, last.
    const auto hinted = BuildSetupMapping(guid, "Hinted", Record({{"lk", {RawKind::Button, 3}}}),
        guid + ",Hinted,a:b0,crc:67fa,platform:Windows,hint:!SDL_GAMECONTROLLER_USE_BUTTON_LABELS:=1, type : xbox360,Platform:Windows,");
    CHECK(hinted == guid + ",Hinted,a:b3,crc:67fa,hint:!SDL_GAMECONTROLLER_USE_BUTTON_LABELS:=1, type : xbox360,platform:Windows,");

    nlohmann::json prefs = {{"controllerMappings", {{guid, full}}}};
    CHECK(sf4e::netplay::ReadControllerMappings(nlohmann::json::parse(prefs.dump())).at(guid) == full);
    CHECK(sf4e::netplay::ReadControllerMappings({{"deviceType", 0}, {"deviceIdx", 2}}).empty());
    CHECK(sf4e::netplay::ReadControllerMappings({{"controllerMappings", {{guid, 5}}}}).empty());
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "0");
    CHECK(SDL_Init(SDL_INIT_GAMEPAD));
    for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i) CHECK(SdlButtonNames[i] == SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(i)));
    for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i) CHECK(SdlAxisNames[i] == SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(i)));
    for (const auto& mapping : {full, stick, sticks, moved, half, merged, hinted}) CHECK(SDL_AddGamepadMapping(mapping.c_str()) >= 0);
    // SDL adds the GUID's CRC to what it stores; merging over that keeps one crc and one platform.
    char* mapping = SDL_GetGamepadMappingForGUID(SDL_StringToGUID(steam.c_str())); CHECK(mapping);
    const std::string stored = mapping; SDL_free(mapping);
    CHECK(stored.find(",leftx:a0,") != std::string::npos && Count(stored, "crc:67fa") == 1);
    const auto again = BuildSetupMapping(steam, "Xbox 360 controller", owner, stored);
    CHECK(Count(again, "crc:") == 1 && Count(again, "platform:") == 1 && SDL_AddGamepadMapping(again.c_str()) >= 0);

    // Ground truth: SDL binds each mapping on a virtual pad as the port reads it.
    SDL_VirtualJoystickDesc desc; SDL_INIT_INTERFACE(&desc);
    desc.naxes = 6; desc.nbuttons = 32; desc.nhats = 2; desc.name = "Ember mapping corpus";
    const auto pad = SDL_AttachVirtualJoystick(&desc); CHECK(pad);
    char guidText[33]; SDL_GUIDToString(SDL_GetJoystickGUIDForID(pad), guidText, sizeof guidText);
    const std::string padGuid = guidText, prefix = padGuid + ",Corpus,";
    const std::string corpus[] = {
        // SDL's database: its XInput mapping, and a DirectInput stick with inverted axes and a CRC.
        "a:b0,b:b1,back:b6,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b10,leftshoulder:b4,leftstick:b8,lefttrigger:a2,leftx:a0,lefty:a1,"
            "rightshoulder:b5,rightstick:b9,righttrigger:a5,rightx:a3,righty:a4,start:b7,x:b2,y:b3,",
        "crc:c77a,a:b2,b:b1,back:b8,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,leftshoulder:b4,leftstick:b10,lefttrigger:b6,leftx:a0,lefty:a1,"
            "rightshoulder:b5,rightstick:b11,righttrigger:b7,rightx:a2~,righty:a3~,start:b9,x:b3,y:b0,",
        // Rounds 4 to 6: composite hats, binding-shaped metadata, case and spaces.
        "leftshoulder:h0.3,rightshoulder:h0.4,lefttrigger:h0.6,a:b0,crc:b003,type:b4,sdk>=:2.0,face:abxy,x:junk,",
        "LEFTSHOULDER:h0.1,A:b0,RightShoulder:b5,+LeftX:a0,LEFTTRIGGER:a2,Platform:Windows,",
        "leftshoulder: h0.1,a :b0, right shoulder:b5, l e f t x : - a 1 ~ ,",
        // Round 7: lenient inputs, colons, double signs and the stop.
        "leftshoulder:-h0.1,x:+b3,y:b4~,b:h0.2~,a:b5junk,+leftx:+a0junk~,dpup:h0.1junk,dpdown:h0.17,",
        "leftshoulder:h9.1:h0,x:b12:b3,y:h1.2:,a::b4,b:b6::,dpup:h0.1:h1:,",
        "++leftx:a0,+-lefty:a1,-+righty:a2,--rightx:a3,+a:b1,+-b:b2,+lefttrigger:a4,-righttrigger:a5~,++x:b3,",
        "leftshoulder:h0.1," + std::string(21, 'k') + ":v,lefttrigger:h0.1,",
        "leftshoulder:h0.1,x:b" + std::string(128, '1') + ",lefttrigger:h0.1,",
        "leftshoulder:" + std::string(100, 'x') + ":b7,rightshoulder:" + std::string(120, '9') + ":b" + std::string(120, '2') + ",",
        // Inputs SDL does or does not decode, and its folding of names.
        "a:B3,b:a,x:a-1,y:h0,leftshoulder:h0.0,rightshoulder:h10.1,leftstick:b99999999999,rightstick:b003,misc1:h1.15,touchpad:-a3~,",
        "\xC5\xBFtart:b9,bac\xE2\x84\xAA:b6,left\xEF\xAC\x86ick:b7,\xC3\x80:b1,guide:b10,",
        // No colon, empty fields, duplicate keys, metadata, a label hint SDL applies, no final comma.
        "leftshoulder,dpup,,  ,:,a:b0,a:b1,A:b0,a:b0,",
        "crc:b003,hint:SDL_X:=1,type:xbox360,sdk>=:21,sdk<=:99,x:b2,platform:Windows,",
        "a:b0,b:b1,x:b2,y:b3,hint:SDL_GAMECONTROLLER_USE_BUTTON_LABELS:=1,",
        "a:b0,x:b2"};
    for (const auto& body : corpus) SdlBinds(pad, prefix + body);
    CHECK(SdlBinds(pad, prefix + corpus[8]).size() == 1 && SdlBinds(pad, prefix + corpus[9]).size() == 1);

    // Merges over the review counterexamples: the conflicting binding goes, the
    // others stay, and SDL binds the merged mapping as expected.
    const auto merge = [&](const SetupState& recorded, const std::string& body, const std::string& expected) {
        SdlBinds(pad, padGuid + ",Pad," + body);
        char* existing = SDL_GetGamepadMappingForGUID(SDL_StringToGUID(padGuid.c_str())); CHECK(existing);
        const auto result = BuildSetupMapping(padGuid, "Pad", recorded, existing); SDL_free(existing);
        if (!(SdlBinds(pad, result) == ReadSdlMapping("guid,Pad," + expected).bindings)) throw std::runtime_error("merge " + body + " gave " + result);
        return result;
    };
    const auto upLk = Record({{"up", {RawKind::HatUp, 0}}, {"lk", {RawKind::Button, 3}}});
    for (const std::string conflict : {"LEFTSHOULDER:h0.1", "leftshoulder: h0.1", "left shoulder:h0.1", "leftshoulder:-h0.1", "leftshoulder:h0.1~",
        "leftshoulder:h0.1junk", "leftshoulder:h0.17", "leftshoulder:h9.1:h0", "++leftx:h0.1", "y:+b3", "y:b3~", "y:b3junk", "A:b0", "+a:b0"})
        merge(upLk, conflict + ",RightShoulder:b5,", "dpup:h0.1,a:b3,rightshoulder:b5");
    merge(Record({{"up", {RawKind::HatUp, 0}}, {"right", {RawKind::HatRight, 0}}, {"lk", {RawKind::Button, 3}}}),
        "leftshoulder:h0.3,rightshoulder:h0.4,lefttrigger:h0.6,a:b0,", "dpup:h0.1,dpright:h0.2,a:b3,rightshoulder:h0.4");
    const auto crc = merge(Record({{"lk", {RawKind::Button, 3}}}), "a:b0,y:b3,crc:b003,hint:!SDL_GAMECONTROLLER_USE_BUTTON_LABELS:=1,sdk>=:21,", "a:b3");
    CHECK(Count(crc, "crc:") == 1 && Count(crc, ",hint:!SDL_GAMECONTROLLER_USE_BUTTON_LABELS:=1,sdk>=:21,") == 1);
    merge(Record({{"lx", {RawKind::AxisPlus, 0}}}), "++leftx:b9,-LeftX:b10,rightshoulder:b5,", "leftx:a0,rightshoulder:b5");
    // Past SDL's stop, a binding stays inert: it is dropped, not moved ahead of the stop.
    const auto rightLk = Record({{"right", {RawKind::HatRight, 0}}, {"lk", {RawKind::Button, 3}}});
    for (const auto& overlong : {std::string(21, 'k') + ":v", "x:b" + std::string(128, '1')}) {
        const auto result = merge(rightLk, "leftshoulder:h0.1," + overlong + ",lefttrigger:h0.1,", "dpright:h0.2,a:b3,leftshoulder:h0.1");
        CHECK(Count(result, "lefttrigger") == 0 && Count(result, overlong) == 0);
    }
    // Kept bindings are written in a form SDL reads as the same binding.
    const auto kept = merge(Record({{"lk", {RawKind::Button, 3}}}), "Y:+b4,++lefty:a1junk~,LeftTrigger:-a2~,rightx:h1.17,touchpad:+a5~,",
        "a:b3,y:b4,+lefty:a1~,lefttrigger:-a2~,rightx:h1.17,touchpad:+a5~");
    CHECK(kept.find(",Pad,a:b3,y:b4,+lefty:a1~,lefttrigger:-a2~,rightx:h1.17,touchpad:+a5~,") != std::string::npos);
    SDL_DetachVirtualJoystick(pad);
    SDL_Quit(); std::cout << "Mappings, merges and the SDL corpus passed\n";
}
