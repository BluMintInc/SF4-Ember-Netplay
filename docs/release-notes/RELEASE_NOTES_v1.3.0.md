# SF4 Ember Netplay v1.3.0

v1.3.0 adds controller setup and button layouts, controls for leaving a watched game, and a Classic frame meter. Rooms show ping and country flags, Training has more recording options, and private rooms can let each fighter use their own input delay. Ember also offers updates at launch and fixes several crashes reported in 1.2.0.

Experimental unofficial rollback netplay for Ultra Street Fighter IV, based on [sf4e by Anthony Danducci and contributors](https://codeberg.org/adanducci/sf4e). Preserve upstream attribution and bundled licenses.

## Updating

- **From 1.2.x, run setup.exe once.** The 1.2 Launcher does not list the new controller pictures, button pictures and country flags in its package inventory, so an in-app update fails with a validation error. Close SF4 and run `sf4-ember-netplay-1.3.0-setup.exe` over your current install. Settings, recordings and replays stay. From 1.3.0 on, in-app updates work again.
- **The zip is another way to update.** With SF4 closed, extract `sf4-ember-netplay-1.3.0.zip` over your Ember folder. The zip, setup.exe and their `.sha256` files are on the [release page](https://github.com/Confetti3/SF4-Ember-Netplay/releases/tag/v1.3.0). New installs can use setup.exe too.
- **Everyone in a room needs 1.3.0.** This includes spectators. 1.2.0 and the 1.3.0 Nightlies cannot join these rooms. Private rooms and invitations work at once.
- **Manual input delay is now 0 to 5 frames.** A saved value of 6 to 10 loads as 5. Auto stays between 1 and 3 frames.
- **Replays saved on 1.3.0 may not open in 1.2.0** if you return to that version.

## Changes since v1.2.0

### Controllers

- **Play with supported pads without Steam Input.** Ember reads PlayStation, Switch and other supported controllers and presents them to the game as Xbox pads. Pads and sticks the game already supports stay with the game until you set them up in Ember. Let the game read controllers, followed by a restart, returns controller handling to the game.
- **Choose and test a controller in Ember.** Settings > Player & controller > Controllers lists connected pads with a live button test. Ember remembers your choice and can use a lone replacement for the session without overwriting it. Up to four controllers are used, with messages for connection, disconnection and reconnection, and rumble on supported non-Xbox pads.
- **Save a button layout for each controller.** Bind any button to an attack, Throw, Focus attack or Red Focus attack. Change one button can combine attacks, duplicate one or turn a button off. Presets include Default, Triggers for all three, and Focus and throw. Layouts affect only your input and can change between games in a room.
- **Keep your layout when moving a pad.** Layouts follow the controller across game slots and USB ports, except when two identical pads have no serial numbers. Menu confirm and Menu back choose Ember's menu buttons; until set, they use the game's Light kick and Medium kick.
- **Set up an unknown pad or stick.** Set up buttons walks through directions, six attacks and Start, with optional buttons after that. A setup applies to every pad of that model. A controller Ember cannot map and the game has no profile for stays with the game except during setup. Choosing a gameplay pad and running setup require leaving the room; Steam Input pads cannot be set up.
- **Button pictures match your controller.** Prompts follow the pad's style, or a style chosen in Player & controller. Steam Input pads are marked and follow your Steam settings. Game's button settings, formerly Open native menus, remains the place for keyboard bindings and controllers read by the game.

Please report unrecognised controllers or incorrect buttons with the controller's name and your `sf4e.log`.

### Spectating

- **Stop watching before the game ends.** Start or F10 opens Ember over the fight at Stop watching. Confirm to return to the room while the fighters continue. For a queued spectator, Leave this game ends the view and keeps the queue place. This fixes [#23](https://github.com/Confetti3/SF4-Ember-Netplay/issues/23). (FRaccie. Thanks to JimboFGC for #43.)
- **Keep the fight visible behind Ember.** The room board and Chat are translucent over a watched game, and an opening hint names the keys. Chat can send messages during the game even when they had been waiting for the room to catch up.
- **Watch the game in a corner of the menu.** Settings > Training and replays > Watched game in a corner makes the menu opaque and puts the live fight in its top right quarter. It starts off. (FRaccie)
- **Read a HUD made for spectators.** Delay and Behind replace the empty Ping and Rollback readings, alongside your queue place and unread Chat. A missed or dropped stream says you will watch the next game. Joining a queue or choosing another table ends the current view and returns you to the room.
- **Return sooner after a spectator drop.** A dropped spectator goes back to the room in about two seconds instead of waiting 30 seconds on the loading screen. Stopping normally no longer produces a false dropped lock-in message or tells the fighters that a spectator disconnected.

### Rooms and input delay

- **See your ping to room members.** Member cards and table portraits show estimated ping with a `~` prefix. Measurements pause while either player is playing or watching a live game.
- **Show a country flag beside your name.** Flags appear before names on table and member cards and on your player card. Settings > Country follows your Windows region by default, or lets you choose a country or Hidden. Changes appear when you next join a room.
- **Open a member's Steam profile.** The profile row opens the Steam overlay, with a browser fallback. Profile > Show my Steam profile controls sharing and starts on; changes apply when you next join a room.
- **Choose separate input delays in private rooms.** At room creation, Same for both remains the default and uses the higher of the fighters' delays. Each player's own uses each fighter's chosen delay, with rollback handling the difference. The choice lasts for the room's lifetime, and its table cards show Own delay.
- **Manual delay is 0 to 5, and Auto is 1 to 3.** Auto previously went up to 4 frames. Delay still never changes during a fight.
- **Put the match HUD above the timer.** Settings > Interface > Match HUD position adds Top center. Create room, Join room and Public rooms also explain what blocks them instead of saying Unavailable.

### Frame meter and Training

- **Choose a Classic frame meter.** Alongside Angled and Flat, Classic has a data line for each fighter, input lanes, frame-counting dots and optional SF6 colors with meaty markers. Its settings control bar height, the data line, input lanes, colors, key and markers, and slanted cells. Input lanes show both fighters in Training and replays, and stay hidden during live matches.
- **Move the meter and keep more history.** Meter settings lets you move the bars vertically, with the replay controls following them. History holds 4, 10, 20 or 60 seconds, with 20 as the default, instead of about two seconds. After a pause can clear the bars or continue the history. These settings also apply to replays and watched matches.
- **Choose Pages or Smooth scrolling.** The mouse wheel looks back through history while the meter is held. Bars fill from the left and hold on the exchange's last active frame after four quiet frames, keeping shorter gaps in the same exchange.
- **Read focus cancels correctly.** The cancelled move keeps its startup and active frames. Frame data identifies the forward or back dash, marks recovery as Cancelled and shows advantage after the dash.
- **Show both fighters' inputs beside the meter.** HUD > Inputs on HUD offers Stick, Arrows or Hybrid, with P1 on the left and P2 on the right. This is available in offline Training only. (FRaccie)
- **Position the dummy before recording.** Record enters Standby, where P1 controls the dummy. F7 starts recording, or hold View, Share, Create or Minus and press Start. F7 again, or opening the controls, stops and keeps it. Slots can be turned on or off and played Random or In order; loop playback chooses again on each repeat. Recordings stay with the dummy's fighter until the game closes.
- **Keep the ready window during a Training call.** Choosing a fighter no longer uses up the 15 seconds to ready, and the countdown pauses while Ready is pending. Previously, a fighter swap could cost you the seat and set score. The Training Lab and dummy reply guides also cover the new controls, with a quick start, examples and notation help.

### Custom content

- **Select installed custom costumes and stages in Ember.** Content installed by a separate setup program appears with the costume, color and stage names it supplies. Only complete installations are offered. (#40, oopswhats)
- **Use custom colors 30 to 99.** They work on the game's costumes and installed custom costumes. Installed custom stages also join Random unless removed in Random stage pool.
- **Fall back when content is missing.** A player without the content sees a standard costume or color, or a fallback stage. Saved choices reset when their custom content has been removed.

### Menus, updates and fixes

- **Get an update prompt at launch.** Ember offers Update now, Later and Skip this version, with Later selected. It waits until Home is free of a room, match, Training or controller setup. Update now closes SF4 and opens the updater. Help & about > Check for updates at launch starts on and checks at most once every six hours.
- **Avoid crashes at launch and from extra keyboards.** The overlay waits for a Direct3D device instead of crashing without one. Ember also keeps the game's keyboard list to eight entries, fixing crashes or exits from pressing L or D with many keyboards or keyboard-like devices connected.
- **Prevent a reported rollback crash.** Ember repairs the invalid state behind a mid-match crash before saving it for rollback. Its underlying cause is still unknown. Please send your log if it contains a line starting "System memento:".
- **Keep the game's menu from taking input behind Ember.** It stays locked during rooms and matches, preventing held buttons from opening Benchmark after a match and crashing. Back at Home no longer hides Ember while you are in a room, and Return to SF4 waits until you leave.
- **New logo, background and clearer labels.** Ember has new artwork, and Launcher.exe, Updater.exe and Sidecar.dll carry the new logo icon. The Home fighter selection hint puts labels above its values. Russian wording distinguishes tables, sets and recording slots, and the Polish Key and markers label now fits its row.

## Known issues

- **Shared position save and reset are unavailable in rollback matches,** including Practice rooms. Offline Training keeps them.
- **Video export can stay unfinished** if the game stops rendering during export. Later replay requests can stay blocked until the game restarts.
- **Picking your fighter, costume, color or Ultra does not count as activity for the 90 second Ready timeout.** A chat message does count.
- **A message sent just as its sender leaves can be missed.** Messages already received stay in Chat.
- **A two-player private room cannot survive its host's game crashing.** The remaining player needs a new room. Leaving normally is fine.
- **Browser links need a fallback on Linux and Steam Deck.** Copy the link and paste it into Ember instead of using Open in Ember.
- **"Practice ended" also appears when a player's link drops.** It does not distinguish a dropped connection from choosing End practice.

Report problems in [Discord](https://discord.gg/uPNqF5A5uq) with `sf4e.log` and `launcher.log` from `%APPDATA%\sf4e\logs`, the time it happened and what you were doing. For installer or update problems, include `%TEMP%\sf4-netplay-update.log` too.

## Testing

The full build passed all 189 automated tests. Every change went through code review until it was approved.

In game, the same code was played as Nightly 1.3.0-nightly20261016.3. This build adds only a build fix so the program icons show the new logo.
