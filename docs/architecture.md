# Architecture

The port reimplements Dungeon Master II (PC DOS, `SKULL.EXE`) in C11. The game rules follow the decompiled
original; the DOS technical layer is not reproduced. The source in `src/` has five layers. The build makes the game
`build/dm2` and a shared library `build/libdm2.dylib` for the tests.

## Layers

```
platform/   SDL window, sound output, control socket, upscale filters, main
   │
control/    control-socket line protocol, turns of the turn-based mode
   │
ui/         screens, dungeon view, input mapping, sound, music, movies, main loop
   │
engine/     game rules: dungeon, party, champions, creatures, timeline, saves
   │
data/       game file formats, byte order, operating system services
```

A file includes only headers of its own layer and of lower layers (below it in the diagram). The layer rules are in section 1 of [coding-standard.md](coding-standard.md#1-layers-and-modules). This page names a
header with its layer: `engine/game.h` is `src/engine/include/game.h`.

The root `Makefile` gives each layer its include path: the `include/` folders of the layer and of all lower layers.
The compiler therefore rejects an include from a higher layer:

| Layer | Include path |
|---|---|
| `data/` | `src/data/include` |
| `engine/` | the path of `data/`, `src/engine/include` |
| `ui/` | the path of `engine/`, `src/ui/include` |
| `control/` | the path of `ui/`, `src/control/include` |
| `platform/` | the path of `control/`, `src/platform/include` |

Outside the layers, `src/third_party/` holds the vendored `opl3/` (the OPL3 emulator) and `tinf/` (the deflate decompressor for ZIP archives). Every layer reaches it through
`-isystem src/third_party`.

Other folders: `tools/dm2tools/` holds the Python tools, `tools/ghidra/` the decompile scripts, `tests/` the tests ([Tests](development.md#tests)), and `docs/` the documentation. Git ignores `game/` (the original files), `reference/` (the Ghidra output and original material), and `tests/saves/`; link them into a new worktree.

## Modules

A module is one public header, the files `<module>.c` and `<module>_<part>.c`, and an optional `<module>_internal.h`. Each header states its prefixes and has one purpose
paragraph. A module with several `.c` files has one `<module>_internal.h` for its private declarations:
`data/mve` (`mve.c`, `mve_video.c`), `engine/ai` (`ai.c`, `ai_action.c`, `ai_data.c`, `ai_move.c`, `ai_path.c`), `engine/champion`
(`champion.c`, `champion_action.c`, `champion_damage.c`, `champion_inventory.c`), `ui/input` (`input.c`, `input_feedback.c`,
`input_tree.c`), `ui/ui` (`ui.c`, `ui_hooks.c`), and `ui/view` (`view.c`, `view_things.c`, `view_walls.c`). The tables do not show the internal headers.

### data/

| Header | Prefixes | Purpose |
|---|---|---|
| `data/crc32.h` | `crc32_` | CRC-32 for PNG and ZIP. |
| `data/gamefile.h` | `gamefile_` | The table of the 10 game files, and checking a file against it. |
| `data/gdat.h` | `gdat_` `image_` | GRAPHICS.DAT, the [GDAT container](formats/graphics.md#graphicsdat-container): the key table and the items. |
| `data/gfx.h` | `gfx_` `pal_` | Blitting, scaling, and palette handling for 8-bit pictures and the screen. |
| `data/hmp.h` | `hmp_` | The HMP song player from the HMI SOS MIDI library in SKULL.EXE: tracks, delta times, HMI loops, volumes. |
| `data/ids.h` | none | Graphic entry numbers of GRAPHICS.DAT, layout zone numbers, minion map icon numbers, and string numbers, one block per kind, sorted by value. |
| `data/iso.h` | `iso_` | ISO 9660 CD images: finds and reads the game files on a CD image. |
| `data/le.h` | `le16` `le32` `be16` `put_le16` `put_le32` `put_be32` | Little-endian reads and writes for the DOS file formats. |
| `data/mve.h` | `mve_` | Interplay MVE movies: chunks, opcodes, 8-bit video, audio; from memory only. |
| `data/num.h` | `abs_` `clamp_` `max_` `min_` | Integer helpers: absolute value, clamping, maximum, and minimum. |
| `data/os.h` | `os_` | The operating system services that `data/`, `engine/`, `ui/`, and `control/` need: the log, fatal errors, and full paths. |
| `data/pcx.h` | `pcx_` | PCX pictures: 8-bit, RLE (run-length encoded), palette at the end (the INTRPLAY.PCX splash); decodes from memory only. |
| `data/settings.h` | `settings_` | The advanced-mode settings file (SETTINGS.TXT in the saves folder): reads and writes the upscale filter, but does not apply it. |
| `data/zip.h` | `zip_` | ZIP archives: the central directory, and stored and deflated entries. |

### engine/

| Header | Prefixes | Purpose |
|---|---|---|
| `engine/action.h` | `action_` | The panel's action area: its modes, the action hands, and the spell and money box views. |
| `engine/ai.h` | `ai_` | The creature AI: its update context, plan commands, AI marker record fields, and the perception, tests, and path search for creature movement. |
| `engine/aspect.h` | `aspect_` `cell_grid_` | What the party sees of a square: the square's aspect and its cell grid. |
| `engine/champion.h` | `champ_` `champion` `inv_` | The champions: the leader hand, their slots, statistics, wounds, and the effects of their items and actions. |
| `engine/creature.h` | `creature_` | Creature groups: the creature info records and the group update. |
| `engine/discard.h` | `discard_` | The discard search: a full dungeon frees a record of a thing far from the party. |
| `engine/dungeon.h` | `dungeon_` `dir_` `square_` `thing_` `location_` `event_` `door_` `group_` `teleporter_` `container_` `misc_` `potion_` `scroll_` `weapon_` `armour_` `moneybox_` `textrec_` | The dungeon: the things, their record layout, the squares, and the maps. |
| `engine/game.h` | `game_` | The main loop state of the game rules: step timing, map changes, and timeline events. |
| `engine/game_text.h` | `gtext_` | Game texts and UI colors: decoding, formatting, and [GDAT](formats/graphics.md#gdat-items) text items. |
| `engine/hooks.h` | none | Calls from the game rules into the user interface, for events that must run the interface inside a rule. |
| `engine/interface_state.h` | none | The interface state that the rules and the drawing share: the panels, the input mode, the mouse. |
| `engine/item.h` | `item_` | The items: their record fields, their GDAT types, and what the party possesses. |
| `engine/magic.h` | `magic_` | Magic: the spell table, runes, spell casting, and the magic light. |
| `engine/merchant.h` | `shop_` | Merchant trading: the creature AI plan commands that a shopkeeper runs while the party stands in front of it. |
| `engine/minion.h` | `minion_` | Minion maps: the grid, the map panel's mana, and the clicks that send a minion. |
| `engine/money.h` | `money_` | Coins in money boxes, the money box panel, and the merchants' item class sets and prices. |
| `engine/move.h` | `move_` | Party movement and thing movement: the movement commands, the redraw flags, and the doors. |
| `engine/projectile.h` | `projectile_` `explosion_` | Projectiles and explosions: their record fields and their timeline events. |
| `engine/random.h` | `rand_` | The random number generator for the game rules. |
| `engine/recruit.h` | `recruit_` | Recruiting a champion from its mirror: the candidate joins the party or goes back. |
| `engine/save.h` | `save_` | The save game: its blocks, its layout, and reading and writing the file. |
| `engine/seen.h` | `seen_` | The automap squares that the dungeon view has shown: one byte per square of every map. |
| `engine/sensor.h` | `sensor_` | Floor and wall sensors: their record fields, their timeline events, and the missiles. |
| `engine/sound_request.h` | `sndreq_` | Sound requests from the game rules: the per-step queues and the delayed sounds. |
| `engine/timeline.h` | `timeline_` | The game timeline: the scheduled events and their handlers. |
| `engine/weather.h` | `weather_` `light_` `world_` | Weather, day and night, and the party light. |

### ui/

| Header | Prefixes | Purpose |
|---|---|---|
| `ui/action_area.h` | `area_` | The action area: the party's hands and spell icons, and the panels that open in it. |
| `ui/advanced.h` | `advanced_` | The advanced mode: quick save and load, the viewport screens (the automap, the cheat screen, and the options menu) and their keys, and the movement letter keys. |
| `ui/automap.h` | `automap_` | The advanced-mode automap: a map picture of the squares that the dungeon view has shown. |
| `ui/cheats.h` | `cheats_` | The advanced-mode cheat screen (not in the original game): turns the cheats on and off, offers quick save and quick load, and lists the spells with their runes. |
| `ui/dialog.h` | `dialog_` | Dialogs: message, question, and progress pictures with buttons, and the volume settings. |
| `ui/draw.h` | `draw_` | Drawing GDAT images into the frame buffer: blits, transparency, and the interface colors. |
| `ui/fm.h` | `fm_` | The OPL2 FM music driver from HMIMDRV.386, on an emulated OPL3 chip. |
| `ui/icon.h` | `icon_` | The window icon, built from a creature graphic. |
| `ui/input.h` | `input_` `command_` `kbd_` `rect_` | Mouse and keyboard input: key codes, rectangles, and the command queue. |
| `ui/minion_map.h` | `mmap_` | The minion map panel of the action area. |
| `ui/mode_screen.h` | `mode_screen_` | The mode screen on program start: the cursor on VANILLA or ADVANCED, its keys, clicks, and picture. |
| `ui/movie.h` | `movie_` | The movie script from the DOS launcher IBMIOP.EXE. |
| `ui/music.h` | `music_` | Game music: the song of each map from SONGLIST.DAT, played by the HMP player on the FM driver. |
| `ui/optmenu.h` | `optmenu_` | The advanced-mode options menu (not in the original game): the FILTER row and CONTINUE. |
| `ui/play.h` | `play_` | The main loop: timer ticks, game steps, the party's view, and sleep. |
| `ui/rows.h` | `rows_` | Text rows for the advanced-mode viewport screens (not in the original game). |
| `ui/session.h` | `session_` `args_` `menu_` | The start menu, new games, loads and saves through the slot dialogs, and the end of the game. |
| `ui/sound.h` | `sound_` | Sound output: the sample mixer, the voices, and the volume settings. |
| `ui/text.h` | `text_` | Text: the font, message wrapping, and printing to the screen. |
| `ui/ui.h` | `ui_` `offscreen_` | The offscreen bitmaps shared by the panels and the drawing of the interface pictures. |
| `ui/video.h` | `video_` | The frame buffer and palette of the 320 × 200 screen. |
| `ui/view.h` | `view_` | The dungeon view: the squares in sight and their drawing. |
| `ui/zone.h` | `zone_` | Layout zones: the GDAT record of each zone with its anchor type, parent zone, and position. |

`ui/ui_hooks.c` has no header of its own, because it defines `g_hooks` from `engine/hooks.h`.

### control/

| Header | Prefixes | Purpose |
|---|---|---|
| `control/control.h` | `control_` | The line protocol for controlling the running game: one JSON request per line, one JSON reply per line; no socket, no SDL. |
| `control/json.h` | `json_` | The JSON text writer of the control replies. |
| `control/state_json.h` | `state_json_` | The game state as JSON: party, champions, view, map. |
| `control/turn.h` | `turn_` | Turns of the turn-based mode: an action puts its inputs into the input queue, and the game runs one game step per frame until the party can act again. |

### platform/

| Header | Prefixes | Purpose |
|---|---|---|
| `platform/install.h` | `install_` | Installing the game files on the first start: checking the game folder and copying from a source. |
| `platform/platform.h` | `plat_` | The window, the sound output, and the input events: the SDL3 side of the game screen. |
| `platform/scale.h` | `scale` | The window's pixel upscale filters: none, Scale2x, and Scale3x. |
| `platform/source.h` | `source_` | Game file sources: a folder, a ZIP archive, or a CD image; read-only. |
| `platform/transport.h` | `transport_` | The control socket: a Unix domain socket with one request line in and one reply line out (POSIX). |

`platform/main.c` has no header, because it is the entry point. `platform/transport_none.c` implements
`transport.h` without a socket (Windows).

## The program

`main` (`platform/main.c`) runs these steps in order:

1. It reads the options and selects the game mode (`--mode`; otherwise the advanced mode).
2. It checks the game files and installs them from a source if needed ([install.md](formats/install.md#startup)). It loads GRAPHICS.DAT and the `--save` file, and opens the window, the `--control` socket, and the
   music. It sets the platform hooks: `g_os_full_path_hook`, `g_optmenu_filter_hook`, and
   `g_play_frame_hook`.
3. Without `--mode` and `--save`, it shows the mode screen.
4. Without `--save`, it plays the start movies and starts the game: the start menu, or a new game in the
   turn-based mode. After a RESTART (the save menu's RESTART choice), it skips the mode screen and the start movies.
5. It runs the mode's main loop (`run_realtime` or `run_turnbased`) until a quit.
6. It plays the end movies, closes the platform, and starts the program again after a RESTART.

The turn-based mode does not play movies. The game rules for the start of a run are in
[game-flow.md](rules/game-flow.md#start-of-a-run).

## The frame loop

`platform/platform.c` waits for each PIT tick (the original's programmable interval timer;
`plat_wait_tick`). The rates of the PIT tick, the timer tick, and the game step are in
[movement.md](rules/movement.md#timing-and-the-game-step).

`run_realtime` (`platform/main.c`) is the main loop of the vanilla and advanced modes. On each PIT tick:

1. `pump_events` passes the window events to the game input (`input_key_down`, `input_mouse_down`,
   `input_mouse_up`, the mouse position) and calls `transport_poll`.
2. On each timer tick, `play_tick` (`ui/play.c`) runs the input, the command queue, and, when it is due, a game
   step.
3. After the timer tick, `session_end` runs if the game has ended, and `present_with_cursor` shows `g_video_fb`
   with the mouse cursor in the window.
4. `plat_wait_tick` mixes the audio and waits for the next PIT tick.

`run_turnbased` is the main loop of the turn-based mode. Of the window events, it keeps only the quit event, because keys and
clicks do not reach the input queue. Each frame calls `transport_poll` and `control_frame`, and shows the screen.
While no turn is pending, the frame waits one timer tick. The loop never ends the session by itself; `load` and
`new_game` start the next game.

A menu or a dialog waits for the player inside a game rule (a modal loop). It calls `play_wait_frames`
(`ui/play.c`), which counts one timer tick and calls the frame hook `g_play_frame_hook` for each frame.
`platform/main.c` sets the hook to `modal_frame` (input, screen, and a wait of one timer tick) or, in the
turn-based mode, to `turnbased_modal_frame`, which polls the control socket and does not wait for a timer tick
while a turn runs.

## The game step

`play_tick` (`ui/play.c`) counts the step's timer ticks (`game_count_step_tick`, `engine/game.c`) and runs
the command queue (`command_process_queue`, `ui/input.c`). The command queue first reads the input
(`input_poll`), which also runs a command's click feedback. If the step is due and the game is running,
`play_tick` runs the end of the current step and the start of the next:

- `step_after_input`: a pending map change, the champions' repeated actions
  (`champ_process_repeated_actions`), and the music for the party's map.
- `step_before_input`: the timeline (`game_process_timeline`), day and night, the dungeon view, the input poll,
  the pending sounds and damage, the party clocks (`game_run_party_clocks`, `engine/game.c`), the champions'
  status boxes, and the game time (`game_advance_time`, `engine/game.c`).

An advanced-mode viewport screen (the automap, the cheat screen, the options menu) and the pause stop the
game step. The split of the original `game_loop` into these two parts is in
[movement.md](rules/movement.md#timing-and-the-game-step).

The rules request panel redraws with redraw flags, which `ui/` reads to draw the panels:

- `Champion.redraw` (`engine/champion.h`): the `REDRAW_*` bits for one champion's name, bars, load, title,
  status box, and inventory.
- `g_party.redraw` (`engine/move.h`): the `PARTY_REDRAW_*` bits for the view and the squares around the party.

## The control socket and the turn

A client controls the running game over a Unix domain socket (`--control PATH`). The commands are in
[control-socket.md](llm/control-socket.md), the turn-based mode in [turn-based.md](llm/turn-based.md). In the turn-based mode, `main` opens the socket before the
main loop (the default path is in [control-socket.md](llm/control-socket.md#turn-based-mode)) and stops with an error if this fails.

- `platform/transport.c` holds the socket. It accepts one client and answers a second client with a `busy` error.
  It splits the client's bytes into request lines and sends one reply line per request.
- `control_handle_line` (`control/control.c`) answers a request line. It returns false if the request starts
  a turn or a wait; the transport then holds the reply.
- While a reply is held, `transport_poll` calls `control_end_turn` once per frame. When the turn or the wait
  ends, the reply goes out and `transport_poll` reads the next request.
- While a turn is pending, `control_frame` runs `play_tick` until the game time advances, at most `STEP_TICKS`
  timer ticks per frame.
  `control/turn.c` holds the actions of a turn and their end rules.

In the vanilla and advanced modes, the game runs in real time, and `pump_events` polls the socket on each PIT tick.
In the turn-based mode, the game time does not advance between two turns, because only `control_frame` runs
`play_tick`.

The control code's interface, all in `build/libdm2.dylib`:

| Header | Functions |
|---|---|
| `platform/transport.h` (the socket, plain POSIX) | `transport_open(path)` listens on the socket, replaces an old socket file, and returns true on success. `transport_poll()`, one call per frame, accepts a client, answers requests, and ends turns. `transport_close()` closes the sockets and removes the socket file. |
| `control/control.h` (the line protocol) | `control_frame()` runs one game step of the pending turn. `control_is_turn_pending()` is true while a turn runs. |
| `control/turn.h` | `turn_is_pending`, `turn_act`, `turn_act_many`, `turn_wait`, `turn_poll`, `turn_reply`, and `turn_drop_reply`. The action table is in `turn.c`, one function per action. |
| `control/state_json.h` (no SDL) | `state_json_state`, `state_json_view`, and `state_json_map` append one JSON object to a `Json` buffer (`control/json.h`). `state_json_string(part)` returns a new string; free it with `free`. |

## Global state

The game's globals:

- The game state that the save file holds: `g_game` (`engine/save.h`; the party position, the game time, the
  champion records, and the global flags), the dungeon (`engine/dungeon.h`), the timeline `g_timeline`
  (`engine/timeline.h`), and the random seed `g_random_seed` (`engine/random.h`). The dungeon records are
  `static` in `engine/dungeon.c`; modules outside `engine/dungeon.c` read and write them through the `dungeon_` and `thing_`
  functions.
- `g_party` (`engine/move.h`): the party's movement state.
- `g_current_map` (`engine/dungeon.h`): the current map for square reads (`map`) and the party position as seen from it.
- `g_main_loop` (`engine/game.h`): the step timing, the pause, the end of the game, and the quit.
- `g_session` (`engine/game.h`): the state of one run: the folders, the slot of the last load or save, and the
  exit code. The save file does not hold it.
- `g_game_mode` (`engine/game.h`): the run's game mode.
- `g_video_fb` and `g_video_palette` (`ui/video.h`): the screen and its palette.

A module's state is `static` in its `.c` file ([coding-standard.md](coding-standard.md#7-state)).

## Hooks between layers

A hook is a function pointer that a higher layer sets, so that a lower layer can call it without an include.

`g_hooks` (`engine/hooks.h`) holds the calls from the game rules into the user interface: messages, sounds,
layout zones, the action area, input, the view, the champions' status boxes, and the end of the
game. `ui/ui_hooks.c` sets every member statically, so the game and the test library have the same members.

The other hooks:

| Hook | Header | Set by | Called by | A NULL hook means |
|---|---|---|---|---|
| `g_os_log_hook` | `data/os.h` | `platform/main.c` (Windows only) | `os_log` (`data/os.c`) | the message goes to stderr |
| `g_os_fatal_hook` | `data/os.h` | `platform/main.c` (Windows only) | `os_fatal` (`data/os.c`) | the message goes to stderr |
| `g_os_full_path_hook` | `data/os.h` | `platform/main.c`, `load_lib` (`tests/dm2test/lib.py`) | `os_get_full_path` (`data/os.c`) | no full path; the call returns false |
| `g_play_frame_hook` | `ui/play.h` | `platform/main.c`, the tests | `play_wait_frames` (`ui/play.c`) | a modal loop counts the timer ticks and shows no frame |
| `g_optmenu_filter_hook` | `ui/optmenu.h` | `platform/main.c` (`plat_set_filter`) | `ui/optmenu.c` | no window: the filter is not shown |

On Windows `g_os_log_hook` and `g_os_fatal_hook` write `dm2.log` in the data folder, because the Windows build has
no console.

## The test library

`build/libdm2.dylib` holds all sources except `platform/main.c` and `platform/platform.c` (the SDL sources) and
`platform/transport_none.c`. It includes the control socket from `platform/transport.c`, but no window.

- Each test module loads its own copy of the library (`load_lib` in `tests/dm2test/lib.py`), so each module has
  its own C globals.
- `g_game_mode` is the vanilla mode in the library.
- A test of a modal loop sets `g_play_frame_hook` to its own frame and resets it to NULL afterwards.
- A symbol that exists only for the tests carries `/* exported for the tests */` on its declaration (rule 2.5 of
  [coding-standard.md](coding-standard.md#2-headers-and-includes)).

The test folders are in [development.md](development.md#tests).
