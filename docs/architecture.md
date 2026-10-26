# Architecture

The port is a reimplementation of Dungeon Master II (PC DOS, `SKULL.EXE`) in C11. The game rules follow the
decompiled original; the port does not reproduce the DOS technical layer. The source in `src/` has five layers.
The build makes the game `build/dm2` and one shared library `build/libdm2.dylib` for the tests.

## Layers

```
platform/   SDL window, sound output, the control socket, the scale filters, main
   │
control/    the line protocol of the control socket and the turns of the turn-based mode
   │
ui/         screens, the dungeon view, input mapping, sound, music, movies, the main loop
   │
engine/     game rules: dungeon, party, champions, creatures, timeline, saves
   │
data/       file formats of the game files, byte order, the operating system services
```

A file includes only headers of its own layer and of the layers before it (below it in the diagram). The rules of the
layers are in section 1 of [coding-standard.md](coding-standard.md#1-layers-and-modules). This page names a
header with its layer: `engine/game.h` is `src/engine/include/game.h`.

The root `Makefile` gives each layer its include path. The path holds the `include/` folders of the layer and of
the layers before it, so the compiler rejects an include of a later layer:

| Layer | Include path |
|---|---|
| `data/` | `src/data/include` |
| `engine/` | the path of `data/`, `src/engine/include` |
| `ui/` | the path of `engine/`, `src/ui/include` |
| `control/` | the path of `ui/`, `src/control/include` |
| `platform/` | the path of `control/`, `src/platform/include` |

`src/third_party/opl3/` is the vendored OPL3 emulator, outside the layers. Every layer reaches it through
`-isystem src/third_party`.

## Modules

A module is one `.c` file with one public header. Each header states its prefixes and has one purpose
paragraph. A module with several `.c` files has one `<module>_internal.h` for its private declarations:
`data/mve` (`mve.c`, `mve_video.c`), `engine/ai` (`ai.c`, `ai_data.c`, `ai_move.c`), `engine/champion`
(`champion.c`, `champion_action.c`, `champion_damage.c`, `champion_inventory.c`), and `ui/ui` (`ui.c`,
`ui_hooks.c`). The tables do not show the internal headers.

### data/

| Header | Prefixes | Purpose |
|---|---|---|
| `data/gdat.h` | `gdat_` `image_` | GRAPHICS.DAT: a header with item sizes, a key table (item 0), then the items. |
| `data/gfx.h` | `gfx_` `pal_` | Blitting, scaling, and palette handling of 8 bpp pictures and the screen. |
| `data/hmp.h` | `hmp_` | The HMP song player of the HMI SOS MIDI library in SKULL.EXE: tracks, delta times, HMI loops, volumes. |
| `data/ids.h` | none | Graphic entry numbers of GRAPHICS.DAT, layout zone numbers, minion map icon numbers, and string numbers, one block per kind, sorted by value. |
| `data/le.h` | `le16` `le32` `put_le16` `put_le32` `put_be32` | Little-endian reads and writes of the DOS file formats. |
| `data/mve.h` | `mve_` | Interplay MVE movies: chunks, opcodes, 8-bit video, audio; from memory only. |
| `data/os.h` | `os_` | The operating system services that `data/`, `engine/`, `ui/`, and `control/` need: the log, fatal errors, and full paths. |
| `data/pcx.h` | `pcx_` | PCX pictures, 8 bits per pixel, RLE, palette at the end (the splash INTRPLAY.PCX); decodes from memory only. |
| `data/settings.h` | `settings_` | The settings file of the advanced mode (SETTINGS.TXT in the saves folder): reads and writes the upscale filter; it does not apply the value. |

### engine/

| Header | Prefixes | Purpose |
|---|---|---|
| `engine/action.h` | `action_` | The action area of the panel: its modes, the action hands, and the spell and money box views. |
| `engine/ai.h` | `ai_` | The creature AI: its update context, the plan commands, the record fields of the AI markers, and the perception, tests, and path search of creature movement. |
| `engine/aspect.h` | `aspect_` `cell_grid_` | What the party sees of a square: its aspect, and the cell grid of a square. |
| `engine/champion.h` | `champ_` `champion` `inv_` | The champions: the leader hand, their slots, statistics, wounds, and the effects of their items and actions. |
| `engine/creature.h` | `creature_` | Creature groups: the creature info records and the update of the groups. |
| `engine/dungeon.h` | `dungeon_` `dir_` `square_` `thing_` `location_` `event_` `door_` `group_` `teleporter_` `container_` `misc_` `potion_` `scroll_` `weapon_` `armour_` `moneybox_` `textrec_` | The dungeon: the things, their record layout, the squares, and the maps. |
| `engine/game.h` | `game_` | The main loop state of the game rules: the step timing, the map changes, and the timeline events. |
| `engine/game_text.h` | `gtext_` | The game texts and the UI colors: decoding, formatting, and GDAT text items. |
| `engine/hooks.h` | none | Calls of the game rules into the user interface, for events that must run the interface inside the rule. |
| `engine/item.h` | `item_` | The items: their record fields, their GDAT types, and what the party possesses. |
| `engine/magic.h` | `magic_` | Magic: the spell table, runes, spell casting, and the magic light. |
| `engine/merchant.h` | `shop_` | Merchant trading: the plan commands of the creature AI that a shopkeeper runs with the party in front of it. |
| `engine/minion.h` | `minion_` | Minion maps: the grid, the mana of the map panel, and the clicks that send a minion. |
| `engine/money.h` | `money_` | Coins in money boxes, the money box panel, and the item class sets and prices of the merchants. |
| `engine/move.h` | `move_` `dun_` | Party movement and thing movement: the movement commands, the redraw flags, and the doors. |
| `engine/projectile.h` | `projectile_` `explosion_` | Projectiles and explosions: their record fields and their timeline events. |
| `engine/random.h` | `rand_` | The random number generator of the game rules. |
| `engine/recruit.h` | `recruit_` | Recruiting a champion from its mirror: the candidate joins the party or goes back. |
| `engine/save.h` | `save_` | The save game: its blocks, its layout, and reading and writing the file. |
| `engine/seen.h` | `seen_` | The squares of the automap that the dungeon view has shown, one byte per square of every map. |
| `engine/sensor.h` | `sensor_` | Floor and wall sensors: their record fields, their timeline events, and the missiles. |
| `engine/sound_request.h` | `sndreq_` | The sound requests of the game rules: the queues of a game step and the delayed sounds. |
| `engine/timeline.h` | `timeline_` | The timeline of the game: the scheduled events and their handlers. |
| `engine/weather.h` | `weather_` `light_` `world_` | Weather, day and night, and the party light. |

### ui/

| Header | Prefixes | Purpose |
|---|---|---|
| `ui/action_area.h` | `area_` | The action area: the hands and spell icons of the party, and the panels that open in it. |
| `ui/advanced.h` | `advanced_` | The advanced mode: quick save and load, the viewport screens (the automap, the cheat screen, and the options menu) and their keys, and the movement letter keys. |
| `ui/automap.h` | `automap_` | The automap of the advanced mode: the map picture of the squares that the dungeon view has shown. |
| `ui/cheats.h` | `cheats_` | The cheat screen of the advanced mode (not in the original game): switches the cheats, offers quick save and quick load, and lists the spells with their runes. |
| `ui/dialog.h` | `dialog_` | Dialogs: message, question, and progress pictures with buttons, and the volume settings. |
| `ui/draw.h` | `draw_` | Drawing of GDAT images into the frame buffer: blits, transparency, and the interface colors. |
| `ui/fm.h` | `fm_` | The OPL2 FM music driver of HMIMDRV.386 on an emulated OPL3 chip. |
| `ui/icon.h` | `icon_` | The window icon, built from a creature graphic. |
| `ui/input.h` | `input_` `command_` `kbd_` `rect_` | Mouse and keyboard input: key codes, rectangles, and the command queue. |
| `ui/minion_map.h` | `mmap_` | The minion map panel of the action area. |
| `ui/mode_screen.h` | `mode_screen_` | The mode screen of the program start: the cursor on VANILLA or ADVANCED, its keys, its clicks, and its picture. |
| `ui/movie.h` | `movie_` | The movie script of the DOS launcher IBMIOP.EXE. |
| `ui/music.h` | `music_` | Game music: the song of each map from SONGLIST.DAT, played by the HMP player on the FM driver. |
| `ui/optmenu.h` | `optmenu_` | The options menu of the advanced mode (not in the original game): the FILTER row and CONTINUE. |
| `ui/play.h` | `play_` | The main loop: timer ticks, game steps, the view of the party, and sleep. |
| `ui/rows.h` | `rows_` | The rows of text of the viewport screens of the advanced mode (not in the original game). |
| `ui/session.h` | `session_` `args_` `menu_` | The start menu, new games, loads and saves through the slot dialogs, and the end of the game. |
| `ui/sound.h` | `sound_mix` `sound_play` `sound_set_` `sound_stop_` | Sound output: the sample mixer, the voices, and the volume settings. |
| `ui/text.h` | `text_clear_` `text_draw` `text_init` `text_measure` `text_message_` `text_print_` `text_set_message_` `text_wrap_` | Text: the font, message wrapping, and printing into the screen. |
| `ui/ui.h` | `ui_` `offscreen_` | The offscreen bitmaps shared by the panels and the drawing of the interface pictures. |
| `ui/video.h` | `video_` | The frame buffer and the palette of the 320 × 200 screen. |
| `ui/view.h` | `view_` | The dungeon view: the squares in sight and their drawing. |
| `ui/zone.h` | `zone_` | Layout zones: the GDAT record of each zone with its anchor type, parent zone, and position. |

`ui/ui_hooks.c` has no header of its own: it defines `g_hooks` of `engine/hooks.h`.

### control/

| Header | Prefixes | Purpose |
|---|---|---|
| `control/control.h` | `control_` | The line protocol of the control of the running game: one JSON request per line, one JSON reply per line; no socket, no SDL. |
| `control/state_json.h` | `json_` `state_json_` | The JSON text writer of the control replies, and the state of the game as JSON: party, champions, view, map. |
| `control/turn.h` | `turn_` | Turns of the turn-based mode: an action gives its inputs to the input queue, and the game runs one game step per frame until the party can act again. |

### platform/

| Header | Prefixes | Purpose |
|---|---|---|
| `platform/platform.h` | `plat_` | The window, the sound output, and the input events: the SDL3 side of the game screen. |
| `platform/scale.h` | `scale` | The pixel upscale filters of the window: none, Scale2x, and Scale3x. |
| `platform/transport.h` | `transport_` | The control socket: a Unix domain socket with one request line in and one reply line out (POSIX). |

`platform/main.c` has no header, because it is the entry point. `platform/transport_none.c` is the build of
`transport.h` without a socket (Windows).

## The program

`main` (`platform/main.c`) does these steps in this order:

1. It reads the options and selects the game mode (`--mode`, or the advanced mode).
2. It loads GRAPHICS.DAT and the save of `--save`, opens the window, the control socket of `--control`, and the
   music. It sets the hooks of the platform: `g_os_full_path_hook`, `g_optmenu_filter_hook`, and
   `g_play_frame_hook`.
3. Without `--mode` and `--save`, it shows the mode screen.
4. Without `--save`, it plays the start movies and starts the game: the start menu, or a new game in the
   turn-based mode. After a RESTART it shows no mode screen and no start movie.
5. It runs the main loop of the mode until a quit: `run_realtime` or `run_turnbased`.
6. It plays the end movies, closes the platform, and starts the program again after a RESTART of the game.

The turn-based mode plays no movie. The start of a run in the game rules is in
[game-flow.md](rules/game-flow.md#start-of-a-run).

## The frame loop

`platform/platform.c` waits for each tick of the timer interrupt of the original (PIT ticks,
`plat_wait_tick`). The rates of the PIT tick, the timer tick, and the game step are in
[movement.md](rules/movement.md#timing-and-the-game-step).

`run_realtime` (`platform/main.c`) is the main loop of the vanilla and advanced modes. On each PIT tick:

1. `pump_events` gives the window events to the input of the game (`input_key_down`, `input_mouse_down`,
   `input_mouse_up`, the mouse position) and calls `transport_poll`.
2. On each timer tick, `play_tick` (`ui/play.c`) runs: the input, the command queue, and a game step when it is
   due.
3. After the timer tick, `session_end` runs when the game ended, and `present_with_cursor` shows `g_video_fb`
   with the mouse cursor in the window.
4. `plat_wait_tick` mixes the audio and waits for the next PIT tick.

`run_turnbased` is the main loop of the turn-based mode. The window events keep only the quit event: keys and
clicks do not reach the input queue. Each frame calls `transport_poll` and `control_frame`, and shows the screen.
While no turn is pending, the frame waits one timer tick. The loop never ends the session by itself; `load` and
`new_game` start the next game.

A menu or a dialog waits for the player inside a rule (a modal loop). It calls `play_wait_frames`
(`ui/play.c`), which counts one timer tick and calls the frame hook `g_play_frame_hook` per frame.
`platform/main.c` sets the hook to `modal_frame` (the input, the screen, and the wait of one timer tick), or to
`turnbased_modal_frame` in the turn-based mode, which polls the control socket and does not wait for a timer tick
while a turn runs. A NULL hook counts the timer ticks and shows no frame.

## The game step

`play_tick` (`ui/play.c`) counts the timer ticks of the step (`game_count_step_tick`, `engine/game.c`) and runs
the command queue (`command_process_queue`, `ui/input.c`). The command queue first reads the input
(`input_poll`), which also runs the click feedback of a command. When the step is due and the game runs,
`play_tick` runs the end of the step and the start of the next:

- `step_after_input`: a pending map change, the repeated actions of the champions
  (`champ_process_repeated_actions`), and the music of the party map.
- `step_before_input`: the timeline (`game_process_timeline`), the day and night, the dungeon view, the input poll,
  the pending sounds and damage, the party clocks (`game_run_party_clocks`, `engine/game.c`), the status
  boxes of the champions, and the game time (`game_advance_time`, `engine/game.c`).

A viewport screen of the advanced mode (the automap, the cheat screen, the options menu) and the pause stop the
game step. The split of the original `game_loop` into the two parts is in
[movement.md](rules/movement.md#timing-and-the-game-step).

The rules do not draw the panels. They set redraw flags that `ui/` reads and draws:

- `Champion.redraw` (`engine/champion.h`): the `REDRAW_*` bits for the name, the bars, the load, the title, the
  status box, and the inventory of one champion.
- `g_party.redraw` (`engine/move.h`): the `REDRAW_*` bits for the view squares and the creature maps.

## The control socket and the turn

A client controls the running game over a Unix domain socket (`--control PATH`). The commands and the
turn-based mode are in [llm-mode.md](llm/llm-mode.md). In the turn-based mode `main` opens the socket before the
main loop, at `~/.dm2/control.sock` when `--control` is missing, and stops with an error when this fails.

- `platform/transport.c` holds the socket. It accepts one client and gives a second client a `busy` error. It
  cuts the bytes of the client into request lines and sends one reply line per request.
- `control_handle_line` (`control/control.c`) answers a request line. It returns false when the request starts
  a turn or a wait; the transport then holds the reply.
- While a reply is held, `transport_poll` calls `control_end_turn` once per frame. When the turn or the wait
  ends, the reply goes out, and `transport_poll` reads the next request.
- While a turn is pending, `control_frame` runs `play_tick` until the game time advances, at most `STEP_TICKS`
  timer ticks per frame.
  `control/turn.c` holds the actions of a turn and their end rules.

In the vanilla and advanced modes the game runs in real time, and `pump_events` polls the socket on each PIT tick.
In the turn-based mode the game time stands still between two turns, because only `control_frame` runs
`play_tick`.

The interface of the control code, all in `build/libdm2.dylib`:

| Header | Functions |
|---|---|
| `platform/transport.h` (the socket, plain POSIX) | `transport_open(path)` listens on the socket, replaces an old socket file, and returns true on success. `transport_poll()`, one call per frame, accepts a client, answers requests, and ends turns. `transport_close()` closes the sockets and removes the socket file. |
| `control/control.h` (the line protocol) | `control_frame()` runs one game step of the pending turn. `control_is_turn_pending()` is true while a turn runs. |
| `control/turn.h` | `turn_is_pending`, `turn_act`, `turn_act_many`, `turn_wait`, `turn_poll`, `turn_reply`, and `turn_drop_reply`. The action table is in `turn.c`, one function per action. |
| `control/state_json.h` (no SDL) | `state_json_state`, `state_json_view`, and `state_json_map` append one JSON object to a `Json` buffer. `state_json_string(part)` returns a new string; free it with `free`. |

## Global state

The globals of the game:

- The game state that the save file holds: `g_game` (`engine/save.h`; the party position, the game time, the
  champion records, the global flags), the dungeon (`engine/dungeon.h`), the timeline `g_timeline`
  (`engine/timeline.h`), and the random seed `g_random_seed` (`engine/random.h`). The dungeon records are
  `static` in `engine/dungeon.c`; the other modules read and write them through the `dungeon_` and `thing_`
  functions.
- `g_party` (`engine/move.h`): the movement state of the party, and the current map of the square reads (`cur_map`).
- `g_main_loop` (`engine/game.h`): the step timing, the pause, the end of the game, and the quit.
- `g_session` (`engine/game.h`): the state of one run: the folders, the slot of the last load or save, and the
  exit code. The save file does not hold it.
- `g_game_mode` (`engine/game.h`): the game mode of the run.
- `g_video_fb` and `g_video_palette` (`ui/video.h`): the screen and its palette.

The state of one module is `static` in its `.c` file ([coding-standard.md](coding-standard.md#7-state)).

## Hooks between layers

A hook is a function pointer that a later layer sets, so that a layer before it can call it without an include.

`g_hooks` (`engine/hooks.h`) holds the calls of the game rules into the user interface: the messages, the sounds,
the layout zones, the action area, the input, the view, the status boxes of the champions, and the end of the
game. `ui/ui_hooks.c` sets every member statically, so the game and the test library have the same members.

The other hooks:

| Hook | Header | Set by | Called by | A NULL hook means |
|---|---|---|---|---|
| `g_os_log_hook` | `data/os.h` | `platform/main.c` (Windows only) | `os_log` (`data/os.c`) | the message goes to stderr |
| `g_os_fatal_hook` | `data/os.h` | `platform/main.c` (Windows only) | `os_fatal` (`data/os.c`) | the message goes to stderr |
| `g_os_full_path_hook` | `data/os.h` | `platform/main.c`, `dm2test.lib` | `os_get_full_path` (`data/os.c`) | no full path; the call returns false |
| `g_play_frame_hook` | `ui/play.h` | `platform/main.c`, the tests | `play_wait_frames` (`ui/play.c`) | a modal loop counts the timer ticks and shows no frame |
| `g_optmenu_filter_hook` | `ui/optmenu.h` | `platform/main.c` (`plat_set_filter`) | `ui/optmenu.c` | no window: the filter is not shown |

On Windows `g_os_log_hook` and `g_os_fatal_hook` write `dm2.log` in the game folder, because the Windows build has
no console.

## The test library

`build/libdm2.dylib` holds all sources except `platform/main.c` and `platform/platform.c` (the SDL sources) and
`platform/transport_none.c`. It has the control socket of `platform/transport.c`, but no window.

- Each test module loads its own copy of the library (`load_lib` in `tests/dm2test/lib.py`), so each module has
  its own C globals. `load_lib` sets `g_os_full_path_hook`.
- `g_game_mode` is the vanilla mode in the library.
- A test of a modal loop sets `g_play_frame_hook` to its own frame and resets it to NULL after.
- A symbol that exists only for the tests carries `/* exported for the tests */` on its declaration (rule 2.5 of
  [coding-standard.md](coding-standard.md#2-headers-and-includes)).

The test folders are in [Tests](development.md#tests).
