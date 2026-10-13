#include "../common/PadSetup.hxx"
#include "../netplay/ControllerMappings.hxx"
#include <SDL3/SDL.h>
#include <iostream>
#include <stdexcept>
using namespace sf4e::gamepads;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (0)
int main() {
    SetupState state;
    const std::string guid = "03000000790000001100000000000000";
    const RawInput inputs[] = {{RawKind::HatUp,0}, {RawKind::HatDown,0}, {RawKind::AxisMinus,1}, {RawKind::AxisPlus,1},
        {RawKind::Button,0}, {RawKind::Button,1}, {RawKind::Button,2}, {RawKind::Button,3}, {RawKind::Button,4},
        {RawKind::Trigger,5,false,true}, {RawKind::Button,6}, {RawKind::Trigger,6}, {RawKind::Button,8}, {RawKind::Button,9}};
    for (int i = 0; i < 14; ++i) state.recorded[i] = inputs[i];
    const auto full = BuildSetupMapping(guid, "Full pad", state);
    CHECK(full == guid + ",Full pad,dpup:h0.1,dpdown:h0.4,dpleft:-a1,dpright:+a1,x:b0,y:b1,rightshoulder:b2,a:b3,b:b4,righttrigger:a5~,leftshoulder:b6,lefttrigger:a6,start:b8,back:b9,platform:Windows,");
    state.recorded[10].reset(); state.recorded[11].reset(); state.recorded[13].reset();
    state.recorded[9] = RawInput{RawKind::Button, 5};
    const auto stick = BuildSetupMapping(guid, "Six button stick", state);
    CHECK(stick == guid + ",Six button stick,dpup:h0.1,dpdown:h0.4,dpleft:-a1,dpright:+a1,x:b0,y:b1,rightshoulder:b2,a:b3,b:b4,righttrigger:b5,start:b8,platform:Windows,");
    CHECK(BuildSetupMapping(guid, "Driver,name", state).find(",Driver name,") != std::string::npos);
    nlohmann::json prefs = {{"controllerMappings", {{guid, full}}}};
    CHECK(sf4e::netplay::ReadControllerMappings(nlohmann::json::parse(prefs.dump())).at(guid) == full);
    CHECK(sf4e::netplay::ReadControllerMappings({{"deviceType", 0}, {"deviceIdx", 2}}).empty());
    CHECK(sf4e::netplay::ReadControllerMappings({{"controllerMappings", {{guid, 5}}}}).empty());
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "0");
    CHECK(SDL_Init(SDL_INIT_GAMEPAD));
    CHECK(SDL_AddGamepadMapping(full.c_str()) >= 0); CHECK(SDL_AddGamepadMapping(stick.c_str()) >= 0);
    char* mapping = SDL_GetGamepadMappingForGUID(SDL_StringToGUID(guid.c_str())); CHECK(mapping); SDL_free(mapping);
    SDL_Quit(); std::cout << "Exact mappings, preferences migration and SDL mapping acceptance passed\n";
}
