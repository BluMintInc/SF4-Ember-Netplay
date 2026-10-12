# Controller layer: UX spec

Status: design, for implementation on `feat/controller-layer` (off `nightly`).
Audience: the coding agents that implement it and the reviewer. Follow it literally. Where this spec names a component, reuse that component; do not add new widgets unless a section says so.

Engineering decisions are fixed (SDL3 gamepad layer, Ember answers the game's XInput calls for slots 0 to 3, DirectInput copies of SDL-served pads hidden, unknown-pad wizard, opt-out at next launch, identity-based saved controller, Steam virtual pad handling, netplay unaffected). This spec decides what the player sees and does.

Terms used below:

- **Recognised pad**: SDL opens it as a gamepad (built-in mapping, or a mapping the player saved with the wizard). Ember serves it to the game in an XInput slot.
- **Unmapped pad**: SDL sees it as a joystick with no gamepad mapping. The game keeps reading it through DirectInput, as today.
- **Gameplay controller**: the device in `input::Assignment::Selected()`. It drives Ember's menus and is bound to the match side.
- **Slot**: one of the game's four XInput indices 0 to 3. The player sees them as "game slot 1" to "game slot 4".
- **Brand**: Xbox, PlayStation, Nintendo, Stick (a pad set up with Ember's wizard) or Generic.

---

## 1. Principles

1. A plain Ember launch makes every pad work. No Steam setup, no drivers, no files to copy.
2. The game sees one kind of pad (Xbox). Ember adapts the pictures, never the button positions.
3. One convention everywhere: the button in the Xbox A position confirms in Ember's menus and in the game's menus.
4. A pad Ember does not know still works as it does today until the player sets it up. Nothing gets worse.
5. Messages appear only when the player can act on them, and never over a fight.
6. The wizard can be finished, undone and cancelled with nothing but the pad being set up.
7. Netplay input is untouched: Ember changes which device the game reads, not what the game does with it.

---

## 2. Glyph policy

### 2.1 Brand detection

Brand comes from `SDL_GetGamepadType` for recognised pads, from the saved wizard mapping, or from the device path for game-read pads.

| Source | Brand | Start/Select art variant |
|---|---|---|
| `SDL_GAMEPAD_TYPE_XBOX360`, `SDL_GAMEPAD_TYPE_XBOXONE` | Xbox | Xbox |
| `SDL_GAMEPAD_TYPE_PS3` | PlayStation | PS3 (Start, Select) |
| `SDL_GAMEPAD_TYPE_PS4` | PlayStation | PS4 (Options, Share) |
| `SDL_GAMEPAD_TYPE_PS5` | PlayStation | PS5 (Options, Create) |
| `SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO`, `..._JOYCON_LEFT`, `..._JOYCON_RIGHT`, `..._JOYCON_PAIR` | Nintendo | Nintendo (Plus, Minus) |
| Pad whose mapping came from Ember's wizard (any type) | Stick | Stick |
| `SDL_GAMEPAD_TYPE_STANDARD`, `SDL_GAMEPAD_TYPE_UNKNOWN`, any other value | Generic | drawn as Xbox |
| Steam virtual pad (28DE:11FF) | the type SDL reports for it (SDL3 reads Steam's virtual pad info); Generic if none | as its brand |
| Game-read XInput pad (opt-out, SDL failed) | Xbox | Xbox |
| Game-read DirectInput pad (unmapped, opt-out, SDL failed) | none: today's generic circle with "LP"/"LK" (in the launcher: "1"/"2") | unchanged |

Generic draws Xbox art because SDL lays every gamepad out on Xbox positions, and most generic pads (8BitDo in X mode, PowerA, Logitech, PC arcade sticks in SDL's database) print Xbox letters.

### 2.2 Nintendo confirm and back: by position

Ember confirms with the **south** button and goes back with the **east** button on every pad, Nintendo included. On a Switch Pro pad that is the button printed **B** (confirm) and the one printed **A** (back), and the legend shows those printed letters.

Why: SDL3 reports face buttons by position (`SDL_GAMEPAD_BUTTON_SOUTH`, `EAST`, `WEST`, `NORTH`) and the game receives the south button as Xbox A. The game's own menus therefore confirm with the south button. If Ember confirmed by label (east), the same player would press different buttons to confirm in Ember and in the game a second later. Positional keeps one rule; the picture tells the truth about which button that is.

A forced style does not move buttons either. With Button prompts set to Xbox on a Switch pad, the legend shows Xbox **A** for the same south button.

### 2.3 Glyph ids stay; art and spoken names change

Keep the logical glyph ids exactly as they are today: `"A" "B" "X" "Y" "LB" "RB" "LT" "RT" "View" "Start" "dpad" "horizontal"`. They name Xbox positions. `PhysicalGlyph` keeps returning them for every recognised pad, Stick included. Code that hard-codes ids (replay HUD `ReplayPlaybackHud.cxx:80`, room hints `ApplicationShell.cxx:836`, `GoNowGlyph`) keeps doing so.

The brand is applied at exactly one translation point, a new `BrandGlyph(id, brand)` in `MenuGlyphs.hxx`, used by `PromptAsset` and by every place that prints a glyph's name:

- For Xbox, PlayStation, Nintendo and Generic it only selects the art (2.6). No name is printed.
- For Stick it returns the generic circle plus a fight name, by position (follows the wizard's mapping in 5.3): A = `LK`, B = `MK`, X = `LP`, Y = `MP`, LB = `3P`, RB = `HP`, LT = `3K`, RT = `HK`, View = `Select`, Start = `Start`. Every hint that draws a glyph with a label (`MenuLegend`, the replay HUD hints, the room hints, the challenger banner) prints that name before the label, the way `MenuLegend`'s `prefix()` prints "LP" for DirectInput pads today. The legend reads "LK Select", "MK Back", "LP Fighter"; the replay strip reads "HP Pause".

Two paths compare the literal id and keep working unchanged:

- `GameMenu.cxx:353`: room shortcut hints (X, Y, View) show only when `selectGlyph` is `"A"`. Every recognised pad now has `"A"`, so PlayStation, Nintendo and Stick players get the shortcut hints.
- `MenuGlyphs.hxx` `MenuLegend` `prefix()`: extend its generic circle test to ask `BrandGlyph` for the name, so Stick names print and DirectInput "LP"/"LK" still print as today.

The brand that applies is chosen by `SetMenuGlyphs`, which gains a brand argument. The menu prompts follow the gameplay controller's brand, resolved through the Button prompts setting (2.4). Keyboard glyph behaviour is unchanged: keyboard prompts still win whenever the keyboard was used last (`NoteMenuDevice`, `ReadMenuInput`).

### 2.4 Button prompts setting

Values, in Left/Right order: **Automatic**, **Xbox**, **PlayStation**, **Nintendo**. Default Automatic.

| Setting | Recognised pad | Stick (wizard) pad | Game-read XInput (opt-out, SDL failed, Steam) | Game-read DirectInput |
|---|---|---|---|---|
| Automatic | its brand (2.1) | Stick ids, generic circles | Xbox | unchanged (LP/LK) |
| Xbox / PlayStation / Nintendo | that brand | that brand, Xbox ids | that brand | unchanged (LP/LK) |

The PlayStation forced style uses PS4 art for Start/Select. A forced style applies to every place prompts are drawn: the shell legend, room hints, replay HUD, training strip, challenger banner, Go now glyph.

### 2.5 Text names (where Ember prints a button name instead of art)

`TrainingShortcuts.cxx` draws button names as text pills. Use this table (not localised; they are printed button names):

| Brand | View position | Start position |
|---|---|---|
| Xbox, Generic | Back | Start |
| PlayStation PS3 | Select | Start |
| PlayStation PS4 | Share | Options |
| PlayStation PS5 | Create | Options |
| Nintendo | Minus | Plus |
| Stick | Select | Start |
| Game-read DirectInput | Select | Start (unchanged) |

### 2.6 Art files

All from Kenney Input Prompts 1.5A, folder `.../Double/`, 128 px, unmodified, copied into `assets/input/` next to the existing files. Take them from the same archive recorded in `assets/input/README.md` (check the SHA-256) and add each folder to the README's "Selected assets" sentence.

| Glyph id | Xbox (existing unless marked new) | PlayStation Series | Nintendo Switch | Stick |
|---|---|---|---|---|
| A | `xbox_button_color_a.png` | `playstation_button_color_cross.png` | `switch_button_b.png` | generic circle + "LK" |
| B | `xbox_button_color_b.png` | `playstation_button_color_circle.png` | `switch_button_a.png` | generic circle + "MK" |
| X | `xbox_button_color_x.png` | `playstation_button_color_square.png` | `switch_button_y.png` | generic circle + "LP" |
| Y | `xbox_button_color_y.png` | `playstation_button_color_triangle.png` | `switch_button_x.png` | generic circle + "MP" |
| LB | `xbox_lb.png` | `playstation_trigger_l1.png` | `switch_button_l.png` | generic circle + "3P" |
| RB | `xbox_rb.png` | `playstation_trigger_r1.png` | `switch_button_r.png` | generic circle + "HP" |
| LT | `xbox_lt.png` | `playstation_trigger_l2.png` | `switch_button_zl.png` | generic circle + "3K" |
| RT | `xbox_rt.png` | `playstation_trigger_r2.png` | `switch_button_zr.png` | generic circle + "HK" |
| View | `xbox_button_back.png` | PS3 `playstation3_button_select.png`, PS4 `playstation4_button_share.png`, PS5 `playstation5_button_create.png` | `switch_button_minus.png` | generic circle + "Select" |
| Start | `xbox_button_start.png` | PS3 `playstation3_button_start.png`, PS4 `playstation4_button_options.png`, PS5 `playstation5_button_options.png` | `switch_button_plus.png` | generic circle + "Start" |
| dpad | `xbox_dpad.png` | `playstation_dpad.png` | `switch_dpad.png` | `generic_joystick.png` (new, Generic) |
| horizontal | `xbox_dpad_horizontal.png` | `playstation_dpad_horizontal.png` | `switch_dpad_horizontal.png` | `generic_joystick_horizontal.png` (new, Generic) |
| up / down / left / right (input test only) | `xbox_dpad_up.png`, `xbox_dpad_down.png`, `xbox_dpad_left.png`, `xbox_dpad_right.png` (new) | `playstation_dpad_up.png`, `_down`, `_left`, `_right` | `switch_dpad_up.png`, `_down`, `_left`, `_right` | `generic_stick_up.png`, `_down`, `_left`, `_right` (new, Generic) |

Controller pictures (capture screen, controller list, wizard header):

| Device | File (folder) |
|---|---|
| Xbox 360 type | `controller_xbox360.png` (Xbox Series) |
| Xbox One / Series type | `controller_xboxseries.png` (Xbox Series) |
| PS3 | `controller_playstation3.png` (PlayStation Series) |
| PS4 | `controller_playstation4.png` (PlayStation Series) |
| PS5 | `controller_playstation5.png` (PlayStation Series) |
| Switch Pro | `controller_switch_pro.png` (Nintendo Switch) |
| Joy-Con pair or single Joy-Con | `controller_switch.png` (Nintendo Switch) |
| Steam virtual pad with no known original type | `controller_steam.png` (Steam Controller) |
| `SDL_JOYSTICK_TYPE_ARCADE_STICK`, or any wizard-mapped pad | `generic_joystick_red.png` (Generic) |
| Generic, unmapped, game-read DirectInput | `controller_generic.png` (Flairs) |
| Saved controller not connected | `controller_disconnected.png` (Flairs) |

Total new files: 57. Keyboard art is unchanged.

---

## 3. First connect, selection and restore

### 3.1 Slots

1. At layer start the saved gameplay controller, if connected and recognised, takes slot 1. Other recognised pads take slots 2 to 4 in SDL enumeration order.
2. A pad keeps its slot while connected.
3. A pad that disconnects frees its slot and the slot stays reserved for that pad's identity for the rest of the session.
4. A newly connected pad takes, in order: its own reserved slot if free; the lowest free slot reserved for nobody; the lowest free slot reserved for someone else (the reservation is dropped and logged); else it is **Not in use**.
5. When a slot frees and pads are waiting as Not in use, the one that connected first takes it.

Reason for rule 1: the game applies its saved button layouts to XInput slots by position, not by pad, so keeping the player's own pad in slot 1 keeps the layout they set in the game's Options with that pad.

### 3.2 Identity

A pad's identity is `{guid, serial, path, name}` from SDL. Two identities match when both serials are non-empty and equal; otherwise when guid and path are equal. (A DualShock 4 has different GUIDs over USB and Bluetooth; the serial ties them together.)

The saved gameplay controller is stored as that identity plus `brand` in Ember's player preferences, replacing `deviceType`/`deviceIdx` for pads. The keyboard is saved as today. On first launch of this version, an old saved XInput slot (`deviceType` 3, `deviceIdx` 0 to 3) is migrated by adopting whatever recognised pad is in that slot at the first main menu, then saving its identity.

### 3.3 Restore on launch

At the first main menu (`CaptureMenuInput`, which already runs there):

| Situation | Result | Player sees |
|---|---|---|
| Saved controller connected | Adopted and assigned to side 1 | Nothing new |
| Saved controller missing, exactly one pad connected | That pad is adopted for this session only; the saved identity is kept | Home status, 4 s: "{saved} is not connected. Using {pad} for now." |
| Saved controller missing, several pads | Capture starts, as today | ASSIGN CONTROLLER screen |
| Saved controller missing, no pads | Keyboard fallback, provisional, as today | Home row "Choose gameplay controller", as today |
| Saved controller connects later, at the main menu, no room, current device provisional | Saved controller adopted again | Status or passive hint: "{saved} connected. You play with it." |
| Saved is the keyboard | Unchanged | Unchanged |

### 3.4 When a pad connects

Hints are debounced 1 s: a pad that disconnects and an equivalent pad that appears within 1 s (Steam Input toggling, a wireless pad reconnecting) produce no hint.

"Passive hint" means a new centered, no-input window drawn by `DrawOverlayLayers` exactly like the existing "Ember shortcut" hint, at top 76 (scaled), shown 4 s, only when `passive` is true and `matchActive` is false. "Shell status" means the screen's status line (Home: its small-print line) in `Tone::Neutral` for 4 s; it never replaces an error the screen is showing.

| Context | Recognised pad (not the gameplay one) | Gameplay controller reconnects | Unmapped pad | Fifth pad |
|---|---|---|---|---|
| Main menu, Ember hidden | Passive hint `controller.connected` (or `controller.connected_adopted` when adopted per 3.3, or `controller.connected_keyboard` when the keyboard is the chosen device) | Passive hint `controller.reconnected` | Passive hint `controller.connected_unmapped` | Passive hint `controller.connected_full` |
| Main menu, Ember open, no room | Shell status, same keys | Shell status `controller.reconnected` | Shell status `controller.connected_unmapped`; setup prompt (5.1) on its first press | Shell status `controller.connected_full` |
| In a room (any room screen) | Nothing (log only) | Room status line no longer shows `controller.disconnected_room` | Nothing | Nothing |
| Online match, offline battle, Training, replay | Nothing (log only) | Warning disappears (3.5) | Nothing | Nothing |
| Game's own menus (Options, etc.) | Nothing | Nothing | Nothing | Nothing |

A pad plugged in inside a room cannot become the gameplay controller there: `canChangeController` requires the main menu and no room (`sf4e__NetplayRuntime__Status.cxx:466`). This spec keeps that gate.

### 3.5 Disconnects

| Context | Player sees |
|---|---|
| Main menu, Ember hidden | Passive hint `controller.disconnected_named` |
| Ember open, no room | Shell status `controller.disconnected` in `Tone::Error` while disconnected (existing `controllerUnavailable` path, ApplicationShell.cxx:522, new copy) |
| In a room, not fighting | Same path, but the room screens use `controller.disconnected_room` instead |
| Online match (GGPO), gameplay controller | Existing `DrawControllerWarning` with `runtime.match_input_blocked` (new copy). Netplay keeps running with neutral input, unchanged. When it reconnects in its reserved slot the warning disappears and input resumes. |
| Online match, it comes back in another slot (reservation lost per 3.1 rule 4) | Same warning, text `runtime.match_input_moved`, until the match ends. At the next main menu Ember adopts it again by identity. |
| Offline battle, Training | Ember adds nothing; the game reacts as it does to an unplugged Xbox pad. |

### 3.6 Capture screen (ASSIGN CONTROLLER)

Screen and flow unchanged (`input::Assignment`, Idle, ReleaseAll, Press, ReleaseSelected). Changes:

- Device names come from SDL (`"DualSense Wireless Controller"`), not the game's `"Controller N"`. `DeviceName` uses the layer's name for slots 0 to 3; DirectInput and keyboard names are unchanged.
- The row's detail pane gets a `GameMenu::Detail` callback (the same mechanism as `screen=="interface"`, ApplicationShell.cxx:901):
  - **ReleaseAll / Press**: under the existing text, one line per connected pad: 32 px controller picture (2.6), then its name. A pad whose buttons are held draws at full opacity, others at 40%.
  - **ReleaseSelected**: the candidate's 64 px picture and `controller.release_to_use` ("Release the button to play with {0}.").
- The row's detail text, legend and cancel behaviour stay as they are.

---

## 4. Settings > Player & controller

### 4.1 Player screen (`screen=="player"`, title `player.title`)

Rows in order. Existing ids are kept so the journey tests still find them.

| # | id | Component | Label | Value | Detail | Enabled |
|---|---|---|---|---|---|---|
| 1 | `name` | `TextRow` | unchanged | unchanged | unchanged | unchanged |
| 2 | `controllers` (new) | `Row` with `value` set (as `sent-reports`) | `player.controllers` | `player.controllers_value` with the count of connected pads (recognised + unmapped + not in use) | `player.controllers_detail` with the gameplay controller's label | always |
| 3 | `capture` | `Row` | unchanged | none | unchanged key; `{0}` now the SDL name | `canChangeController` |
| 4 | `keyboard` | `ConfirmRow` | unchanged | none | unchanged | `canChangeController` |
| 5 | `button-prompts` (new) | `Value` | `player.button_prompts` | `player.prompts_auto_with` ({0} = resolved brand word) / `controller.brand.xbox` / `controller.brand.playstation` / `controller.brand.nintendo` | `player.button_prompts_detail` | `canEditPreferences`, else detail `settings.leave_room_to_edit` |
| 6 | `controls` | `Row` | `player.fight_buttons` (new key, replaces `player.native_menus`) | none | `player.fight_buttons_detail` (replaces `player.native_menus_detail`) | `idle` (unchanged) |
| 7 | `background-play` | `Value` | unchanged | unchanged | unchanged | unchanged |
| 8 | `lower-input-lag` | `Value` | unchanged | unchanged | unchanged | unchanged |
| 9 | `lower-keyboard-lag` | `Value` | unchanged | unchanged | unchanged | unchanged |
| 10 | `faster-display` | `Value` | unchanged | unchanged | unchanged | unchanged |
| 11 | `game-reads-pads` (new) | `Value` | `player.game_reads` | `common.on` / `common.off` | `player.game_reads_detail`; while the saved value differs from the running mode, append `"\n\n"` + `player.game_reads_pending` | `canEditPreferences`, else detail `settings.leave_room_to_edit` |

Button behaviour on this screen (standard shell rules, listed so nothing is guessed):

| Row | A / Enter | B / Esc | Left / Right | X / Y / View |
|---|---|---|---|---|
| controllers | opens `controllers` screen | back to Settings | nothing | nothing |
| capture | starts capture (unchanged) | back | nothing | nothing |
| keyboard | confirmation (unchanged) | back | nothing | nothing |
| button-prompts | nothing | back | previous / next value, saved by the normal autosave | nothing |
| controls | unchanged (`StartOffline`, Ember closes) | back | nothing | nothing |
| game-reads-pads | nothing | back | Off / On, autosaved | nothing |

The resolved brand word for `player.prompts_auto_with`: the gameplay controller's brand name (`controller.brand.*`); `controller.brand.stick` for Stick; Xbox for Generic and game-read XInput; when the gameplay device is the keyboard or a DirectInput pad, the value is plain `player.prompts_auto`.

`button-prompts` and `game-reads-pads` are stored in `netplay::PlayerPreferences` (add both to `SamePreferences` and the bool/enum preference lists the same way `fasterDisplay` is). Button prompts apply immediately. Let the game read controllers applies at the next launch only.

### 4.2 Controllers screen (`screen=="controllers"`, new, title `controllers.title`)

Pushed from Player. Back returns to Player. A plain list (`GameMenu::Draw`, one column) with a `GameMenu::Detail` callback.

Rows, in this order:

1. **State row, first, only when it applies** (`InfoRow`, no value):
   - SDL failed to start: id `pads-unavailable`, label `controllers.unavailable`, detail `controllers.unavailable_detail`.
   - Opt-out running: id `pads-game`, label `controllers.game_reads`, detail `controllers.game_reads_detail`.
2. **One row per pad**, ordered: slot 1 to 4, then unmapped pads in DirectInput index order, then Not in use pads in connection order, then the saved controller if it is not connected. id `pad-<n>` where n is a stable per-session number. Label: the pad's name, `userText=true` (elided, not translated). Pad names are driver text and may hold any characters: pass every one to `NoteUserText` (as `ApplicationShell.cxx:347` does for the room name), and set `detailText=DetailText::Name` on any row whose detail embeds a pad name (this screen's rows, Player rows 2 and 3, the capture row, the wizard rows). Value and detail per state:

| State | Value | Detail | A / Enter |
|---|---|---|---|
| Recognised, gameplay controller | `controllers.value_yours` | `controllers.detail_yours` ({0} slot) | opens the pad choice (below) |
| Recognised, other | none | `controllers.detail_other` ({0} slot) | opens the pad choice |
| Recognised with an Ember setup | as above | as above + `"\n"` + `controllers.detail_custom` | opens the pad choice |
| Steam virtual pad | `controllers.value_steam` | `controllers.detail_steam` | opens the pad choice (setup disabled) |
| Unmapped | `controllers.value_unmapped` | `controllers.detail_unmapped` | starts the wizard (5) if `canEditPreferences`; otherwise disabled with `settings.leave_room_to_edit` |
| Not in use (more than four) | `controllers.value_full` | `controllers.detail_full` | nothing (`info=true`) |
| Saved, not connected | `controllers.value_absent` | `controllers.detail_absent` | nothing (`info=true`) |
| Opt-out or SDL failed: each game device from `ReadDevices` | `controllers.value_yours` for the gameplay one, else none | `controllers.detail_game_read` | nothing (`info=true`) |

3. With no pad rows at all: `InfoRow` id `pads-none`, label `controllers.none`, detail `controllers.none_detail`.

**Pad choice** (the row carries `choices`, so A opens the shell's choice modal; Left/Right move, A picks, B cancels):

| Choice id | Label | Detail | Enabled |
|---|---|---|---|
| `play` | `controllers.choice_play` | `controllers.choice_play_detail` | not already the gameplay controller and `canChangeController`; else detail `controllers.choice_play_current` or `settings.leave_room_to_edit` |
| `setup` | `controllers.choice_setup` | `controllers.choice_setup_detail` | `canEditPreferences` and not a Steam pad; else detail `settings.leave_room_to_edit` or `controllers.choice_setup_steam` |
| `forget` | `controllers.choice_forget` | `controllers.choice_forget_detail` | shown only when an Ember setup is saved for its GUID |

Results: `play` adopts the pad (same path as a finished capture: `Adopt`, `AssignToSide(...,0,false)`, save identity) and shows shell status `controllers.now_playing`. `setup` opens the wizard. `forget` removes the saved mapping from preferences and shows shell status `controllers.forgotten`; the pad keeps working with the old setup until the next launch.

**Detail pane drawing** (`GameMenu::Detail`, under the row's detail text, as the Interface screen draws its preview):

- Recognised and Steam pads: 64 px controller picture (2.6), then heading `controllers.test_heading`, then the **input test**:
  - Line 1: four 32 px direction squares (up, down, left, right art for the pad's resolved prompt brand).
  - Line 2: ten 32 px squares in this order: A, B, X, Y, LB, RB, LT, RT, View, Start, drawn with `DrawPromptGlyph` and the resolved brand (Stick shows generic circles with LK, MK, LP, MP, 3P, HP, 3K, HK, Select, Start).
  - Each square is drawn at 35% alpha when up and full alpha with a 2 px `palette::Ember` underline when held. A trigger counts as held past 50%. A direction lights from the d-pad or from the left stick past 50%.
  - The test reads the pad through the layer every frame, whatever slot it has and whether or not it is the gameplay controller. On the gameplay controller, A and B still act on the menu as usual.
- Unmapped pads: 64 px `controller_generic.png`, heading `controllers.test_heading`, then one text line: `controllers.test_raw` with a comma-separated list of what is held right now, using the input names from 5.6 (`"Button 4, Hat up"`), or `controllers.test_nothing`.
- Not in use, absent, game-read rows: the picture only.

### 4.3 How "Fighting buttons" (the game's own button config) is presented

Row 6 keeps its behaviour (Ember closes and the game's main menu takes over). Copy changes to say that every recognised pad appears there as an Xbox pad, and that the game only keeps a layout where every action has a button (the native validator silently drops other layouts; see Code notes). `about.controls_detail` is rewritten (section 8) so Help no longer says only Xbox pads have A/B.

---

## 5. Unknown-pad wizard

### 5.1 Entry points

1. **Automatic prompt.** When all of these hold: Ember's shell is visible; the screen is not a room screen; no capture, wizard, notice or modal is open; `canEditPreferences`; an unmapped pad that has not been prompted for this session reports a button or hat press (raw SDL joystick). Ember shows the shell's modal notice (`GameMenu::ShowNotice`) with heading `pad_setup.prompt_heading`, text `pad_setup.prompt_text` ({0} = pad name). The notice's dismiss button reads `pad_setup.not_now` (small extension: `ShowNotice` takes an optional label for its OK button). While the notice is open, Ember reads that pad raw: once all its inputs are at rest, the **next** press on it closes the notice and opens the wizard. Not now, Esc, a click or B on the gameplay controller close it; that pad is not prompted again this session.
2. **Controllers screen**: A on an unmapped row, or `setup` on a recognised pad's choice.
3. No entry from inside a room, a match or Training.

### 5.2 Screen

Screen id `pad-setup`, title `pad_setup.title`. Drawn with `GameMenu::Draw`: the list holds one row per step (label = step name, value = what is recorded), and a `GameMenu::Detail` callback draws the prompt. The wizard has two modes.

- **Capture mode**: list navigation is off (`ControllerNavigation::Update(..., enabled=false)`, as during controller capture); the focused row follows the current step. Ember reads only the pad being set up, raw, through `SDL_Joystick` (for a recognised pad, through `SDL_GetGamepadJoystick`). While the wizard runs, the layer reports that pad's XInput slot as neutral (connected, nothing held), so the game sees nothing. An unmapped pad is still read by the game through DirectInput, but Ember's visible shell already keeps the game's main menu from reacting.
- **Summary mode**: normal list navigation; see 5.8.

Detail pane in capture mode, top to bottom:

1. 64 px picture of the pad (2.6) and its name.
2. `pad_setup.step_count` ("Step {0} of {1}").
3. The step name at 1.4x the body font.
4. The instruction: `pad_setup.press_direction` for steps 1 to 4, `pad_setup.press_button` for the rest. On optional steps, add `pad_setup.optional`. On the LK step add `pad_setup.lk_selects`; on the MK step add `pad_setup.mk_backs`.
5. A feedback line, one at a time, priority top first: disconnect countdown (5.10); hold ring with `pad_setup.hold_cancel`; conflict `pad_setup.conflict` (2 s, `Tone::Error`); required `pad_setup.required` (2 s, `Tone::Error`); idle hint `pad_setup.idle_hint` (after 10 s with no input).

Legend (`MenuLegend` with `primary=nullptr` and `extras`; the standard select/back pair is not shown in capture mode):

| Hint | Glyph | Label | Shown |
|---|---|---|---|
| Keyboard cancel | `Esc` | `common.cancel` | always |
| Keyboard back a step | `Bksp` (new key cap; add `"Bksp"` and `"Space"` to `KeyCapGlyph`) | `pad_setup.back_step` | from step 2 |
| Keyboard skip | `Space` | `pad_setup.skip` | optional steps |
| Pad back a step | `generic_stick_left` | `pad_setup.back_step` | from step 5 |
| Pad skip | `generic_stick_right` | `pad_setup.skip` | optional steps from step 5 |
| Pad cancel | generic circle | `pad_setup.hold_to_cancel` | always |

Mouse: no clickable controls in capture mode beyond the shell header's Back button, which acts as Cancel.

### 5.3 Steps and mapping

The SDL target for each attack follows the game's default Xbox fight layout, so a pad set up here fights the way the player pressed it without visiting the game's button config. This default is the layout of a standard Xbox 360 arcade stick (top row X, Y, RB, LB; bottom row A, B, RT, LT). It is **assumed, not verified in this repo**; verify it against the game's default XInput fight table (`0xA5D078`, see Code notes) before implementation and change only the SDL target column if it differs.

| # | Step | Name key | SDL mapping target | Required |
|---|---|---|---|---|
| 1 | Up | `pad_setup.step.up` | `dpup` | yes |
| 2 | Down | `pad_setup.step.down` | `dpdown` | yes |
| 3 | Left | `pad_setup.step.left` | `dpleft` | yes |
| 4 | Right | `pad_setup.step.right` | `dpright` | yes |
| 5 | Light punch | `pad_setup.step.lp` | `x` (west) | yes |
| 6 | Medium punch | `pad_setup.step.mp` | `y` (north) | yes |
| 7 | Heavy punch | `pad_setup.step.hp` | `rightshoulder` | yes |
| 8 | Light kick | `pad_setup.step.lk` | `a` (south: Ember's Select) | yes |
| 9 | Medium kick | `pad_setup.step.mk` | `b` (east: Ember's Back) | yes |
| 10 | Heavy kick | `pad_setup.step.hk` | `righttrigger` | yes |
| 11 | All three punches | `pad_setup.step.3p` | `leftshoulder` | no |
| 12 | All three kicks | `pad_setup.step.3k` | `lefttrigger` | no |
| 13 | Start | `pad_setup.step.start` | `start` | yes |
| 14 | Select | `pad_setup.step.select` | `back` | no |

Order: the game's attack order LP MP HP LK MK HK, then the optional 3P and 3K so a six-button pad simply skips the last two attacks, then Start and Select. Directions go to the d-pad only; nothing is mapped to `leftx`/`lefty`.

Consequence, stated on the LK, MK and summary screens: on a set-up pad, the LK button selects and the MK button goes back in Ember's menus and in the game's menus.

### 5.4 Reading inputs

- **Rest.** When the wizard opens, after the triggering press is released, Ember samples every axis and hat; those values are rest. A button already held then is waited on for up to 5 s with `controller.release_buttons`; if it is still held it is ignored for this wizard (logged). Rest is sampled again after a reconnect (5.10).
- **Button**: becomes active on down. Recorded as `b<i>`.
- **Hat**: active when it leaves centred with a single cardinal direction (diagonals are ignored until they become cardinal). Recorded as `h<h>.<v>` with v = 1 up, 2 right, 4 down, 8 left.
- **Axis**: active when `|value - rest| > 16000`.
  - If rest is within 8000 of zero (a stick): half axis, `+a<i>` or `-a<i>` by direction of travel.
  - If rest is beyond 24000 from zero (a trigger at an end): full axis, `a<i>`, with `~` appended when rest is the positive end.
  - Otherwise: half axis as for a stick.
  - Inactive again within 8000 of rest.
- **Several at once**: the first input to become active in a step owns the step; others are ignored until everything is back at rest.
- **Hitboxes and leverless controllers**: directions are buttons and are recorded as buttons. Ember does nothing about SOCD; the device's own cleaning applies, and Ember records what the device reports.
- **Sticks with the lever on a hat**: recorded as hat directions. A stick that reports the lever on both a hat and axes records whichever arrives first.
- Ember never reads the keyboard or other pads as step input.

### 5.5 The tap and hold ladder (capture mode)

One gesture rule for every capture step:

| Gesture on any input of the pad | Result |
|---|---|
| Active, back at rest within 1000 ms | **Tap.** In steps 1 to 4: record it. From step 5: if it is the input recorded for Left, back a step; if it is the input recorded for Right, skip (optional step) or show `pad_setup.required`; any other already recorded input shows `pad_setup.conflict`; otherwise record it. |
| Held 1000 ms | Nothing is recorded. The hold ring starts with `pad_setup.hold_cancel`. |
| Released between 1000 and 3000 ms | Nothing happens; the ring disappears. |
| Held 3000 ms | Cancel (5.9). |

A recorded tap fills the row's value and moves to the next step once everything is at rest. Recording on release (not on press) is what lets a hold mean cancel.

Keyboard, any time in capture mode: Esc cancels (5.9), Backspace backs a step (nothing on step 1), Space skips an optional step (on a required one it shows `pad_setup.required`).

### 5.6 How recorded inputs are named

Used in step values, the summary, the unmapped input test and conflicts.

| Recorded | Text |
|---|---|
| `b<i>` | `pad_setup.input.button` with i + 1 |
| `h<h>.1` / `.2` / `.4` / `.8` | `pad_setup.input.hat_up` / `_right` / `_down` / `_left` (hat number omitted; if the pad has two hats, append " {h+1}") |
| `+a<i>` / `-a<i>` | `pad_setup.input.axis_plus` / `pad_setup.input.axis_minus` with i + 1 |
| `a<i>` / `a<i>~` | `pad_setup.input.trigger` with i + 1 |
| skipped | `pad_setup.skipped` |
| nothing yet | `common.not_set` |

### 5.7 Conflicts

An input already recorded for another step is never recorded twice. In capture mode the player sees `pad_setup.conflict` ("{0} is already {1}. Press a different one.") and the step waits. To move an input to another step, the player redoes the step that holds it from the summary, which clears it first.

### 5.8 Summary mode

Shown after step 14. Same screen; list navigation on.

Rows:

1. The 14 step rows (`Row`, value per 5.6). A on a step row clears that step and runs capture mode for that one step, then returns to the summary with focus on it. Ember never offers to clear a required step without redoing it.
2. `save`: `Row` label `pad_setup.save`, detail `pad_setup.save_detail`. Enabled only when every required step is recorded.
3. `restart`: `Row` label `pad_setup.start_over`, detail `pad_setup.start_over_detail`. Clears all steps and goes to step 1.
4. `cancel`: `ConfirmRow` label `pad_setup.cancel`, detail `pad_setup.cancel_detail`. Confirming cancels (5.9).

Detail pane in summary mode: pad picture and name, then `pad_setup.summary_detail`. While any recorded input is held, its step row's value draws in `palette::Ember` (the test). For step rows, `pad_setup.redo_detail` is the row detail.

Navigation in summary mode, with the pad being set up (its raw inputs read through the just recorded steps): Up and Down move, LK selects, MK is Back. Back on the summary focuses `cancel` and opens its confirmation. The hold ladder's 3000 ms cancel still applies. The gameplay controller (if it is another pad) and the keyboard navigate as on any screen; Esc is Back.

### 5.9 Cancel, save and results

- **Cancel** (3 s hold, Esc in capture mode, `cancel` confirmed, header Back in capture mode): nothing is saved, the pad's slot stops reading neutral, the wizard closes to the screen it came from, and shell status shows `pad_setup.cancelled`.
- **Save**: Ember builds the mapping string `<guid>,<name>,<pairs>,platform:Windows,` with the pairs in step order (skipped steps omitted), calls `SDL_AddGamepadMapping`, stores the string under the GUID in preferences (`controllerMappings`), and loads all stored strings at every layer start before opening pads.
  - The pad becomes a recognised pad: it takes a slot (3.1), its DirectInput copy is hidden from the game, and if it was the gameplay controller it is adopted again by identity.
  - The wizard closes to the Controllers screen with focus on that pad and shows `GameMenu::ShowNotice` heading `pad_setup.saved_heading`, text `pad_setup.saved`.
  - If SDL rejects the string: `ShowError(pad_setup.save_failed)`, nothing stored, wizard stays in summary mode.
  - If preferences cannot be written, the existing settings error path applies (`error.settings_not_saved`); the mapping still applies for this session.
- **Reset**: `forget` on the Controllers screen (4.2). Effective at the next launch.

### 5.10 Disconnect and no input

- **Pad disconnects during the wizard**: progress is kept for 15 s. The feedback line shows `pad_setup.reconnect` with the seconds left. If a pad with the same identity reconnects in time, rest is sampled again and the wizard continues at the same step (or summary). Otherwise the wizard closes, nothing is saved, and the shell shows `ShowNotice(pad_setup.stopped_disconnect)`.
- **No input**: after 10 s without any input on a capture step, show `pad_setup.idle_hint`. After 60 s without any input (pad or keyboard), the wizard closes, nothing is saved, and the shell shows `ShowNotice(pad_setup.stopped_idle)`.
- **The game leaves the main menu or the shell is hidden**: the wizard cancels silently (logged); the next time Ember opens, the Controllers screen is where it was.

---

## 6. Messages and edge states

| State | What the player sees | Where |
|---|---|---|
| More than four recognised pads | Extra pads are Not in use. `controller.connected_full` when one connects; row value `controllers.value_full` and detail `controllers.detail_full`. When a slot frees, the first waiting pad takes it with `controller.connected`. | 3.4, 4.2 |
| Steam Input virtual pad present (28DE:11FF) | Ember serves the Steam pad and hides its physical twin. One row per physical pad: the original pad's name and brand when SDL knows them, else `controllers.steam_name`; value `controllers.value_steam`; detail `controllers.detail_steam`; setup disabled with `controllers.choice_setup_steam`. No other message; launch options are never mentioned. | 4.2 |
| Steam pairing rule | SDL3 already hides most physical pads Steam has taken. If SDL still reports a physical pad of the same SDL type as a Steam pad, Ember pairs them one to one in enumeration order, serves the Steam pad and does not open the physical one. Why the Steam pad: the player chose Steam by launching through it, Steam applies their Steam layout to it, and Steam can hide the physical pad at any moment. | Log only |
| Opt-out on (Let the game read controllers) | The game reads pads as before this feature. Controllers screen shows `controllers.game_reads` first and lists game devices read-only. Setup, input test and forget are not offered. Button prompts still apply to game XInput pads (useful with Steam Input); DirectInput pads show the generic circle with LP and LK again, and those buttons select and go back. | 4.2 |
| SDL failed to start | Same as opt-out for this launch, with `controllers.unavailable` as the first Controllers row. No notice or popup elsewhere. | 4.2 |
| Pad recognised, gameplay device is the keyboard | `controller.connected_keyboard` (passive hint or shell status). The pad is not adopted. Controllers screen offers `play` on it. | 3.4 |
| Pad disconnects during the wizard | 5.10. | 5 |
| Saved mapping rejected at startup (corrupt prefs) | Nothing for the player; the pad is unmapped and gets the prompt again. Logged. | 7 |
| Unmapped pad is the gameplay controller | Works as today: LP selects, LK goes back, Start opens Ember. Its row says so (`controllers.detail_unmapped`). | 4.2 |

---

## 7. Log lines

All at `spdlog::info` unless marked, each prefixed `Input:`. Names are quoted; GUIDs are 32 hex chars; `id` is a short identity tag (first 8 hex of a hash of serial or path) so logs can follow one pad without printing serials.

| When | Line |
|---|---|
| Layer start | `Input: SDL {version} started, hidapi {on/off}, background events on, {n} saved mappings loaded` |
| Saved mapping rejected | `Input: saved mapping for guid {guid} rejected: {SDL_GetError}` (warn) |
| SDL failed | `Input: SDL failed to start ({SDL_GetError}); the game reads controllers itself` (warn) |
| Opt-out | `Input: the game reads controllers itself (setting)` |
| Pad added, recognised | `Input: pad added '{name}' {VID:PID} type {sdl type} brand {brand} guid {guid} id {id} slot {n}` |
| Pad added, unmapped | `Input: pad added '{name}' {VID:PID} guid {guid} id {id} unmapped, left to DirectInput` |
| Pad added, no slot | `Input: pad added '{name}' id {id} not in use, four slots taken` |
| Steam pad | `Input: Steam virtual pad '{name}' as {sdl type} slot {n}` |
| Steam twin | `Input: physical pad '{name}' id {id} paired with Steam pad slot {n}; not opened` |
| DirectInput copy hidden | `Input: hiding DirectInput copy of '{name}' {VID:PID}` |
| Pad removed | `Input: pad removed '{name}' id {id} slot {n} reserved` |
| Reservation dropped | `Input: slot {n} reserved for id {old} given to '{name}' id {new}` |
| Waiting pad placed | `Input: '{name}' id {id} takes freed slot {n}` |
| Restore | `Input: saved controller '{name}' id {id} restored to slot {n}` / `Input: saved controller id {id} not connected; using '{name}' for now` |
| Gameplay controller set | `Input: gameplay controller '{name}' id {id} slot {n} by {capture/list/restore/only pad/migration}` |
| Brand change | `Input: prompts {brand} ({automatic/forced})`, only when it changes |
| Setup prompt | `Input: setup prompt for '{name}' guid {guid}` / `Input: setup prompt declined` |
| Wizard start | `Input: setup started for '{name}' guid {guid}: {b} buttons, {h} hats, {a} axes; rest {a0=..,a1=..}` |
| Stuck at start | `Input: setup ignores held {input}` |
| Step | `Input: setup step {name} = {b3/h0.1/-a1/a5~}` / `skipped` / `conflict with {step}` / `back` |
| Wizard end | `Input: setup saved for guid {guid}: {mapping string}` / `Input: setup cancelled ({hold/escape/confirm/disconnect/idle/left menu})` |
| Forget | `Input: setup for guid {guid} forgotten; applies next start` |
| Rumble | `Input: rumble unsupported on '{name}'` (once per pad per session) |

Existing lines stay: `Input: game pads ... | hid ... | steam overlay ...` (LogInventory) and `Input: P{n} plays with ...`, which now prints the SDL name for slots 0 to 3.

---

## 8. Localization (en.po)

New keys. Other languages fall back to English. Screen titles follow the existing all-caps title convention.

| Key | English |
|---|---|
| `controller.brand.xbox` | Xbox |
| `controller.brand.playstation` | PlayStation |
| `controller.brand.nintendo` | Nintendo |
| `controller.brand.stick` | Fight buttons |
| `controller.connected` | {0} connected. |
| `controller.connected_adopted` | {0} connected. You play with it. |
| `controller.connected_keyboard` | {0} connected. You play with the keyboard; to switch, use Change controller in Settings > Player & controller. |
| `controller.connected_unmapped` | {0} connected. Open Ember to set up its buttons. |
| `controller.connected_full` | {0} connected, but four controllers are already in use. Disconnect one to use it. |
| `controller.disconnected_named` | {0} disconnected. |
| `controller.reconnected` | {0} reconnected. |
| `controller.using_for_now` | {0} is not connected. Using {1} for now. |
| `controller.release_to_use` | Release the button to play with {0}. |
| `player.controllers` | Controllers |
| `player.controllers_value` | {0} connected |
| `player.controllers_detail` | You play with {0}. See every connected controller, test its buttons, and set up ones Ember does not know yet. |
| `player.button_prompts` | Button prompts |
| `player.prompts_auto` | Automatic |
| `player.prompts_auto_with` | Automatic ({0}) |
| `player.button_prompts_detail` | The button pictures Ember shows. Automatic matches the controller you play with. Which button does what stays the same. |
| `player.fight_buttons` | Fighting buttons |
| `player.fight_buttons_detail` | Ember closes. In the game's menu, choose Options, then the controller settings. Every controller Ember lists shows there as an Xbox pad with Xbox button names. The game keeps a layout only when every action has a button. |
| `player.game_reads` | Let the game read controllers |
| `player.game_reads_detail` | Off: Ember reads every controller and the game sees each one as an Xbox pad. On: the game reads controllers itself, as it does without Ember, and PlayStation and Switch pads then need Steam Input. Changes apply the next time the game starts. |
| `player.game_reads_pending` | Restart the game to apply this. |
| `controllers.title` | CONTROLLERS |
| `controllers.unavailable` | Controller support is off |
| `controllers.unavailable_detail` | Ember could not start its controller support, so the game reads controllers itself this time. Xbox pads work as usual; other pads may need Steam Input. sf4e.log has the details. |
| `controllers.game_reads` | The game reads controllers |
| `controllers.game_reads_detail` | Let the game read controllers is on, so Ember does not set up or test controllers. Turn it off in Player & controller and restart the game. |
| `controllers.none` | No controllers connected |
| `controllers.none_detail` | Connect a controller and it shows here. The keyboard works without one. |
| `controllers.value_yours` | Yours |
| `controllers.value_steam` | Steam Input |
| `controllers.value_unmapped` | Not set up |
| `controllers.value_full` | Not in use |
| `controllers.value_absent` | Not connected |
| `controllers.detail_yours` | You play with this controller. Game slot {0}. Press its buttons to test them. |
| `controllers.detail_other` | Game slot {0}. Press its buttons to test them. Select to play with it or set up its buttons. |
| `controllers.detail_custom` | Uses the button setup you saved. |
| `controllers.detail_steam` | Steam Input handles this controller, so its buttons follow your Steam settings. To let Ember handle it, start Ember outside Steam, or turn off Steam Input for Ultra Street Fighter IV in Steam. |
| `controllers.detail_unmapped` | Ember does not know this controller's buttons yet. Until it is set up, the game reads it directly and Ember's menus use the buttons set as LP and LK. Select to set it up. |
| `controllers.detail_full` | The game has room for four controllers. Disconnect another one to use this one. |
| `controllers.detail_absent` | Your saved controller. Connect it to play with it again. |
| `controllers.detail_game_read` | The game reads this controller itself. |
| `controllers.steam_name` | Steam controller |
| `controllers.test_heading` | Press buttons to test them |
| `controllers.test_raw` | Pressing: {0} |
| `controllers.test_nothing` | Pressing: nothing |
| `controllers.choice_play` | Play with this controller |
| `controllers.choice_play_detail` | Use it for online play and Ember's menus. |
| `controllers.choice_play_current` | You already play with this controller. |
| `controllers.choice_setup` | Set up buttons |
| `controllers.choice_setup_detail` | Press each button in turn to tell Ember what it does. |
| `controllers.choice_setup_steam` | Steam Input sets this controller's buttons. |
| `controllers.choice_forget` | Forget button setup |
| `controllers.choice_forget_detail` | Go back to how this controller worked before you set it up. Applies the next time the game starts. |
| `controllers.now_playing` | You play with {0}. |
| `controllers.forgotten` | Button setup forgotten. This applies the next time the game starts. |
| `pad_setup.title` | SET UP CONTROLLER |
| `pad_setup.prompt_heading` | New controller |
| `pad_setup.prompt_text` | Ember does not know the buttons of {0} yet. Press any button on it to set it up now. It takes about a minute. |
| `pad_setup.not_now` | Not now |
| `pad_setup.step_count` | Step {0} of {1} |
| `pad_setup.step.up` | Up |
| `pad_setup.step.down` | Down |
| `pad_setup.step.left` | Left |
| `pad_setup.step.right` | Right |
| `pad_setup.step.lp` | Light punch |
| `pad_setup.step.mp` | Medium punch |
| `pad_setup.step.hp` | Heavy punch |
| `pad_setup.step.lk` | Light kick |
| `pad_setup.step.mk` | Medium kick |
| `pad_setup.step.hk` | Heavy kick |
| `pad_setup.step.3p` | All three punches |
| `pad_setup.step.3k` | All three kicks |
| `pad_setup.step.start` | Start |
| `pad_setup.step.select` | Select |
| `pad_setup.press_direction` | Push the stick or press the button for this direction, then let go. |
| `pad_setup.press_button` | Press the button you want for this, then let go. |
| `pad_setup.optional` | If your controller has no button for this, skip it. |
| `pad_setup.lk_selects` | This button will also select in menus. |
| `pad_setup.mk_backs` | This button will also go back in menus. |
| `pad_setup.hold_cancel` | Keep holding to cancel setup. |
| `pad_setup.hold_to_cancel` | Hold any button: cancel |
| `pad_setup.back_step` | Back a step |
| `pad_setup.skip` | Skip |
| `pad_setup.required` | This one is needed. |
| `pad_setup.conflict` | {0} is already {1}. Press a different one. |
| `pad_setup.idle_hint` | Nothing pressed yet. Hold any button for 3 seconds to cancel, or press Esc. |
| `pad_setup.reconnect` | Controller disconnected. Reconnect it within {0} seconds to carry on. |
| `pad_setup.input.button` | Button {0} |
| `pad_setup.input.hat_up` | Hat up |
| `pad_setup.input.hat_right` | Hat right |
| `pad_setup.input.hat_down` | Hat down |
| `pad_setup.input.hat_left` | Hat left |
| `pad_setup.input.axis_plus` | Axis {0} + |
| `pad_setup.input.axis_minus` | Axis {0} - |
| `pad_setup.input.trigger` | Trigger axis {0} |
| `pad_setup.skipped` | Skipped |
| `pad_setup.summary_detail` | Press any button to see which step it is. Here, Light kick selects and Medium kick goes back, as they will in every menu. |
| `pad_setup.redo_detail` | Select to press this one again. |
| `pad_setup.save` | Save |
| `pad_setup.save_detail` | Save this setup and use it now. |
| `pad_setup.start_over` | Start over |
| `pad_setup.start_over_detail` | Clear every step and begin again. |
| `pad_setup.cancel` | Cancel setup |
| `pad_setup.cancel_detail` | Leave without saving. |
| `pad_setup.cancelled` | Setup cancelled. Nothing was saved. |
| `pad_setup.saved_heading` | Controller set up |
| `pad_setup.saved` | {0} is set up. If an attack does the wrong thing, check Fighting buttons in Player & controller. |
| `pad_setup.save_failed` | Ember could not use this setup, so nothing was saved. sf4e.log has the details. |
| `pad_setup.stopped_disconnect` | Setup stopped because {0} disconnected. Nothing was saved. |
| `pad_setup.stopped_idle` | Setup stopped because nothing was pressed for a minute. Nothing was saved. |

Changed keys (new English text; translators see them as changed):

| Key | English |
|---|---|
| `controller.disconnected` | Controller disconnected. Reconnect it, or choose another in Settings > Player & controller. |
| `runtime.match_input_blocked` | {0} disconnected. Your fighter gets no input until you reconnect it. |
| `about.controls_detail` | Xbox: A selects and B goes back. PlayStation: Cross selects and Circle goes back. Switch: B selects and A goes back, as in the game's own menus. A controller set up in Ember: Light kick selects and Medium kick goes back. Keyboard: arrows, Enter and Escape. The D-pad moves; Left and Right change a value. Going back at Home hides Ember. F10, or Start on a controller, opens it again at the game's main menu.\n\nIn a room, A on a table lets you pick a seat, A on your own seat readies up, and B leaves your seat or queue place (on other controllers, the buttons in the same places). On Xbox, X opens Fighter, Y opens Options and View opens Chat; press it again to return to the room. PlayStation uses Square, Triangle and Share or Create; Switch uses Y, X and Minus.\n\nIn training, F6 opens the controls, F5 hides the HUD, F7 records and F8 plays. On a controller, hold View (Share, Create or Minus) and press Start. |

New keys, replacing nothing:

| Key | English |
|---|---|
| `controller.disconnected_room` | Controller disconnected. Reconnect it, or leave the room to choose another. |
| `runtime.match_input_moved` | {0} is back, but in another game slot. It works again from the next game. |

Retired keys (remove from every .po): `player.native_menus`, `player.native_menus_detail`.

---

## 9. Out of scope, later

- The launcher's recovery window (`RecoveryController`, `RecoverySurface.cxx:150`) runs before the game exists and keeps its own XInput and DirectInput polling and its "1"/"2" generic glyphs. Unchanged.
- Remapping fighting buttons inside Ember (writing the game's binding tables or `.ctrlsav`). The game's own config stays the place for that.
- A rumble on/off setting, rumble strength, gyro, touchpad, LEDs, battery display.
- Per-pad Button prompts choice (the setting is global).
- Left stick deadzone or SOCD settings.
- Mapping the analog sticks of an unmapped pad to `leftx`/`lefty`.
- Shipping extra mappings beyond SDL's built-in database.
- Local versus with two pads beyond what the game already does: Ember only guarantees side 1.
- Players with more than four pads choosing which four are used.

---

## 10. Acceptance checklist

U = unit test of pure logic (no SDL, no game). R = UiRender harness shot. G = in-game check.

Glyphs and prompts

1. U: brand from SDL type follows the 2.1 table for every listed type, including Steam with and without a known original type.
2. U: glyph resolution (brand x Button prompts x device kind) follows 2.4; the glyph id for the south button is `"A"` for every recognised brand; `BrandGlyph("A", Stick)` gives the generic circle and the name `LK`; `BrandGlyph("RB", Stick)` gives `HP`.
3. U: `PromptAsset` returns the 2.6 file for every id and brand; Nintendo A id returns `switch_button_b`.
4. U: every file named in 2.6 exists in `assets/input/` and is listed in the README.
5. R: Player screen legend with each of Xbox, PlayStation (PS4 and PS5), Nintendo, Stick, keyboard; room screen with the X/Y/View shortcut hints shown for PlayStation and Nintendo.
6. R: training strip pill text per 2.5 for each brand.
7. G: with a DualSense, Cross selects and Circle goes back in Ember; Square, Triangle and Create open Fighter, Options and Chat in a room; replay controls work with L1, R1, L2, R2.
8. G: with a Switch Pro pad, the button printed B selects in Ember and in the game's main menu.

Slots, identity, restore

9. U: slot assignment rules 3.1 (start order, sticky slot, reservation, reservation reuse, fifth pad waits, waiting pad takes a freed slot).
10. U: identity match rule 3.2 (serial wins; guid plus path otherwise; USB and Bluetooth DS4 with the same serial match).
11. U: preferences round trip of saved identity, button prompts, game reads controllers, and `controllerMappings`; migration of an old XInput slot save.
12. U: restore table 3.3, every row.
13. G: restart the game with two pads plugged in a different order; the saved one is still the gameplay controller and is game slot 1.
14. G: in an online match, unplug and replug the gameplay pad; the warning shows the new text, then disappears and input resumes in the same match.

Settings and Controllers screens

15. R: Player screen rows in the 4.1 order, with labels, values and details; `controllers` value shows the count.
16. R: Controllers screen with one of each row state from 4.2, plus `pads-none`, `pads-unavailable` and `pads-game` variants.
17. R: Controllers detail pane input test with all squares up, and with A, LB and Up held (lit with underline).
18. R: unmapped row detail with `Pressing: Button 4, Hat up`.
19. U (shell journey): A on a recognised row opens the choice with `play`, `setup`, and `forget` only when a mapping is saved; `play` disabled with the right detail when already in use or in a room.
20. U (shell journey): Button prompts Left/Right cycles Automatic, Xbox, PlayStation, Nintendo and autosaves; Let the game read controllers shows the pending line after a change.
21. R: capture screen in Press with two pads listed (one lit) and in ReleaseSelected with the candidate picture.

Wizard

22. U: mapping string builder: a recorded step list gives exactly `<guid>,<name>,` + pairs in step order + `platform:Windows,`; skipped steps are absent; hats, half axes, full axes and inverted full axes are written as in 5.4.
23. U: input classifier with rest values: trigger resting at -32768 is not active at start; moving it to +32767 records `a<i>`; resting at +32767 records `a<i>~`; a stick axis records `+a`/`-a`; a diagonal hat waits.
24. U: tap and hold ladder 5.5 with fake time: tap under 1000 ms records; 1000 to 3000 ms release records nothing; 3000 ms cancels; from step 5 the Left input backs a step and the Right input skips only optional steps.
25. U: conflicts (5.7), required step skip refused, back a step clears the previous step, Start over clears all.
26. U: disconnect keeps progress 15 s and resumes on the same identity; 60 s idle stops; nothing is saved in either stop.
27. R: wizard capture mode on step 1, step 8 (LK, with its extra line), step 11 (optional, skip hints), hold ring, conflict line, idle hint, reconnect countdown; summary mode with Save enabled and with one row lit.
28. R: setup prompt notice with Not now.
29. G: with a hitbox or a stick SDL does not know: the prompt appears on its first press at Home; the wizard completes using only that device; after Save it shows as an Xbox pad in the game's controller settings; LP to HK do the right attacks in Training with the game's default layout.
30. G: the wizard can be cancelled with a 3 s hold, and with Esc.

Edge states

31. G: launched through Steam with Steam Input on and a DualShock 4: one controller row, value Steam Input, PlayStation prompts; log shows the Steam and twin lines; no duplicate.
32. G: Let the game read controllers on, restart: the log shows the setting line, the Controllers screen shows `pads-game`, a DirectInput pad selects with LP again.
33. G: five pads: the fifth is Not in use; unplug one and the fifth takes the freed slot with a hint.
34. G: keyboard chosen, then a pad is plugged in at Home: the keyboard hint shows and the keyboard stays the gameplay device.
35. G: change a button in the game's own controller settings with a DualSense, restart the game; the change is still there (the "layout won't save" report).
36. G: with Play in the background on and the game window behind another window, the DualSense still moves the fighter.
37. G: every `Input:` line in section 7 appears in sf4e.log for its event.

---

## Code notes

Where the code differs from the brief, or where an implementer must take care.

1. `sf4e__OverlayPrefs.cxx:111`: `Clamp` discards any saved device that is not the keyboard or an XInput slot 0 to 3, so a DirectInput pad can never be the saved controller, and an XInput save is a slot number, not a pad. The identity fields replace this (3.2), including the migration.
2. `sf4e__NetplayRuntime__Input.cxx:31`: restore matches `config.deviceType`/`deviceIdx` by slot, and `input::Device::Same` (`InputAssignment.hxx:15`) compares type, index and name. With SDL-served pads, `Same` must compare identity for slots 0 to 3, or a pad moving slot is treated as a new device.
3. `sf4e__NetplayRuntime__Status.cxx:466`: `canChangeController` is false in any room, so the controller cannot change in a room. Kept as is; the room copy points to leaving the room.
4. `sf4e__NetplayRuntime__Status.cxx:451` and `en.po` `runtime.match_input_blocked`: the warning exists only for GGPO matches; offline battles and Training show nothing from Ember (kept). Its current text says "If its slot changed, return to the room to reassign it", which slot reservation makes wrong; replaced in section 8.
5. `GameMenu.cxx:353`: room shortcut hints require `selectGlyph=="A"`. Keeping glyph ids positional (2.3) makes them appear for PlayStation and Nintendo with no change there.
6. `MenuGlyphs.hxx` `MenuLegend` `prefix()`: prints a glyph's name when its art is the generic circle. Stick ids rely on this.
7. `sf4e__ReplayPlayback.cxx:219`, `TrainingShortcuts.cxx:51`, `sf4e__Pad.cxx:127` (Go now) and `sf4e__Overlay.cxx:541` (`padBehind`) all test `PadXInput`. SDL-served pads pass these tests with no change. `TrainingShortcuts.cxx:53` hard-codes "Back"; it needs the brand name table 2.5. `TrainingInRoom.hxx:51` `GoNowGlyph` returns `"View"`, which the brand art then covers.
8. `sf4e__Overlay.cxx:541`: background play for pads needs XInput answers while the game is unfocused. The layer must keep answering from SDL regardless of focus (set `SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS` to 1) and leave the existing focus gating to Ember's code as it is today.
9. The brief cites `src/ui/ReplayPlayback.cxx ~219`; the file is `src/sf4e/sf4e__ReplayPlayback.cxx:219`, and the Xbox-only button reads are in `src/common/ReplayTransport.hxx:235`.
10. The brief cites `RecoverySurface.cxx ~150` as an in-game DirectInput case. It is the launcher's recovery window (`RecoveryController.hxx:10`), which runs before the game and is outside the layer (section 9).
11. Native validator (from the 2026-10-08 IDA notes, `0x4CBA40`): when any XInput pad is connected, the game saves its button config only if every XInput record 0 to 3 has all nine functions on physical buttons. Now that every recognised pad is XInput to the game, this applies to all of them; the Fighting buttons detail says so. Not verified in this checkout (the IDA server was unreachable).
12. Default XInput fight layout (`0xA5D078`): the 5.3 SDL targets assume X LP, Y MP, RB HP, LB 3P, A LK, B MK, RT HK, LT 3K. Not verified here; confirm before implementing.
13. The game applies saved XInput layouts to slots by position (`.ctrlsav`, loader `0x51BC30`), which is why 3.1 rule 1 puts the saved controller in slot 1.
14. After a wizard save, the game must stop seeing the pad's DirectInput copy and see a new XInput pad without a restart. The game re-enumerates DirectInput on device change (observed when Steam Input toggled). The layer must trigger the same re-enumeration; if it cannot, `pad_setup.saved` must add "Restart the game to use it." and checklist item 29 changes accordingly.
15. Sticks with a native game profile (14 DirectInput GUIDs matched at `0x517900`): when SDL's database knows them they become XInput pads, and the game's native stick profile no longer applies to them. Their buttons follow SDL's mapping instead.
16. `ApplicationShell.cxx:938`: during capture the legend is forced to keyboard glyphs. The wizard sets its own legend (5.2) and must restore the gameplay controller's glyphs when it closes.
17. `ShellView` carries none of the layer's state today. Add these, filled by the runtime snapshot, so the three screens share one shape:
    - `padLayer`: Active, GameReads (opt-out), Failed; plus `padLayerPending` (saved opt-out differs from running).
    - `pads`: per pad `{id, name, brand, artVariant (PS3/PS4/PS5, Joy-Con), picture, slot (-1 none), state (Recognised, Steam, Unmapped, NotInUse, Absent, GameRead), gameplay, customMapping, held (the 2.6 glyph ids held now), raw (held raw inputs for unmapped)}`.
    - `promptBrand`: the resolved brand for prompts (2.4).
    - `padSetup`: `{active, padId, step, mode (Capture, Summary), recorded[14], holdMs, feedback (None, Conflict, Required, Idle, Reconnect), reconnectSeconds}`.
    - `padSetupPrompt`: the pad id to prompt for, or none.
    - Pad hints (3.4): `padHint` text and its expiry, so `OverlayLayersView` and the shell status read the same message.

---

## Decisions after review (2026-10-12)

These override anything above that disagrees.

1. **Default fight layout verified.** IDA, table `0xA5D078` (one dword of function bits per physical XInput button): X = 0x10 (LP), Y = 0x20 (MP), RB = 0x400 (HP), LB = 0x430 (3P), A = 0x40 (LK), B = 0x80 (MK), RT = 0x800 (HK), LT = 0x8C0 (3K), View = 0x2000, Start = 0x1000. Section 5.3 is correct as written. Code notes 12 is closed.
2. **Sticks with a native game profile stay with the game** (Kate). The layer never serves these VID:PIDs. They stay DirectInput pads, keep the game's own profile and any layout saved for them, and are listed as game-read. The player can still choose `setup` on one, and once a wizard mapping exists for its GUID, it becomes a recognised pad like any other. The list comes from the guidProduct strings at `0x517900`, written as VID:PID:
   - 0079:0011
   - 05FD:262A
   - 046D:C20C
   - 0E8F:0003
   - 0E8F:3013
   - 0F0D:0032
   - 0F0D:0022
   - 0738:8838 (Madcatz FightStick TE)
   - 1A34:0802 (SpeedLink 360 Style Gamepad)
   - 0E8F:0012
   - 0079:0006
   - 046D:C216
   - 0079:0002
   - 0F0D:0037 (HORI Fighting Edge)

   Code notes 15 is replaced by this rule.
3. **No restart after setup (code notes 14).** The DirectInput wrapper from package P2 also wraps `CreateDevice`. Once SDL serves a device, its DirectInput device answers `GetDeviceState` and `GetDeviceData` with a neutral state, so the game stops reading the copy at once. Hiding it from `EnumDevices` covers later enumerations. The new XInput pad appears in its slot through the XInput answers, which the game polls every frame. `pad_setup.saved` keeps its text unless an in-game test shows the game does not pick up the new XInput slot.
4. **Steam rule (section 6) stands.** Ember serves the Steam virtual pad and does not slot the physical twin.
