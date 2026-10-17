# Controller layer validation

Branch `feat/controller-layer`, commits 47a1aee9..427eaa50, off nightly 3f30335d. The design is in `docs/design/CONTROLLER_LAYER_UX.md`.

## Automated

- Full ctest: 160 of 160 at 427eaa50. The `PackageInstaller`, `Win32Cursor`, `UpdaterRun`, `UiPreferencesStore` and `VideoEncoder` tests fail now and then under 20-way parallel runs, and pass alone.
- Controller tests:
  - GamepadSlots and GamepadSlotsImportPatch
  - DirectInputFilter
  - ButtonLayout
  - PadSetup and PadSetupMapping, which builds the SDL mapping strings and checks that `SDL_AddGamepadMapping` accepts them
  - GamepadPrompts
  - GamepadRumble
  - ControllersScreen
  - PadAssignment
  - ControllerNavigation
  - OverlayPadCapture
  - the shell journeys
  - UiRender scenarios for prompts, the Controllers screen, the setup wizard and Button layout
- Review: Astra high, thermo-nuclear standard, three rounds. Round 3 approved at 427eaa50. Round 1 claims were checked by Opus before fixing.

## In game

Run each launch only with Kate's go.

| # | Setup | Proves | Result |
|---|---|---|---|
| 1 | Xbox pad, plain launch | No regression for Xbox pads, which now go through SDL: main menu, Controllers screen, a Training match, rumble, Button layout open and close | owed |
| 2 | Switch Pro, plain launch | Nintendo prompts, recognised without setup, a Training match, a Button layout swap used in the fight, the game's own button screen open and close (the OptionUIController guard) | owed |
| 3 | Switch Pro through the Steam launch option | One row marked Steam Input, no duplicate pad | owed |
| 4 | "Let the game read controllers" on, then relaunch | Old behaviour, and `Input: controller layer off (setting)` in the log | owed |

After each run, search `sf4e.log` for:
- `Input: SDL`
- `Input: controller layer`
- `Input: pad added`
- `hiding DirectInput copy`
- `Steam virtual pad`
- `game button screen`

Also check whether a `sf4e-crash.log` was written.

## Not covered yet

- DualShock 4, DualSense, 8BitDo and other non-Xbox pads, apart from the Switch Pro. Hitboxes and arcade sticks, including the setup wizard on a real unmapped device. These are owed to Nightly field tests.
- A real two-PC online match. Review found no change to what GGPO sends, but the loopback fixtures do not load Sidecar.
- HID path matching between DirectInput and SDL on real devices.

## Rollback

There are two ways to turn the layer off:
- In Settings > Player, set "Let the game read controllers" to On. It applies at the next launch.
- If Ember's menu will not open, set `controllerLayer` to `false` in `%APPDATA%\sf4e\settings.json` and relaunch.

## Notes for the release text

- The layer is on by default for everyone.
- The escape hatch is the "Let the game read controllers" row.
- A stick SDL recognises (other than the game's 14 native profiles) now appears as an Xbox pad. A layout saved for it in the game's own Options no longer applies; set it up again in Ember's Button layout.
