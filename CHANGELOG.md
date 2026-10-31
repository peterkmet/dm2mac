# Changelog

All notable changes to this project are in this file. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- Mode screen: without `--mode`, the program first shows a screen with the items VANILLA and ADVANCED.
- Smooth window scaling with a live resize redraw, and the window title DM2Mac.
- Cheat screen (`C`) with six cheats: full light, immortal, endless food and water, one-hit kill, walk through walls, and reveal map. It also casts spells. The save slots and `QUICK.DAT` keep the cheats in a CHTS block.
- Quick save and quick load (`F5`, `F9`) and spells on the cheat screen.
- Automap: a frame, a checkerboard for imaginary walls, and a mark for the pressure pads that the dungeon view draws. The cheat keys of the automap are gone; use the cheat screen.
- SCREEN FILTER on the cheat screen with the upscale filter (a change shows immediately), saved in `SETTINGS.TXT`. `Esc` pauses in advanced mode as in vanilla mode.
- `Tab` closes the open inventory.
- The damage star of a hit stays one more game step in the action area.
- Projectiles fly smoothly between game steps.
- Window and Dock icon (a mummy from `GRAPHICS.DAT`).
- Tools: the LE loader `dm2-le` (`tools/dm2tools/le.py`), the Unicorn emulator harness `dm2tools.emu` with the Watcom register convention, and the Ghidra headless scripts that write `reference/decomp/SKULL.c`.
- Analysis: the symbol map `docs/reference/skull-symbols.csv` and `docs/reference/skull-map.md`, and the data formats and port deviations in `docs/formats/`.
- Platform layer: an SDL3 window with the 320×200 indexed framebuffer at scale 3, keyboard and mouse input in original coordinates, a 240 Hz PIT tick from the PIT divisor of `SKULL.EXE`, and the upscaling filters Scale2x and Scale3x (`--filter`, `DM2_FILTER`).
- Data layer: `GRAPHICS.DAT` with the image decoders, `DUNGEON.DAT`, the save format `SKSAVEn.DAT` (read and write in the DOS format), the random number generator, and the timeline queue.
- Dungeon view and outdoor view: walls, doors, stairs, pits, ornaments, objects, creatures, missiles, explosions, sparkle fields, the sky, rain, and clouds, with the drawing order of the original.
- Interface: the font and text, the message area, the champion status boxes, the action and spell areas, the inventory panel, the money box, and chests.
- Main loop and input: the input tree of `SKULL.EXE`, the click feedback, party movement and turning, sensors, doors, pits, teleporters, stairs, and fake walls.
- Champions, inventory, items, money, and shops: the leader hand, inventory slots, item effects, eating and drinking, damage and poison, actions and experience, throwing, recruiting, money boxes, shop displays, and merchant trading.
- Creatures and magic: active creature groups with animation and AI, path search, pushes, projectiles, explosions, missile launchers, runes and spells, light, auras, minions, and the minion map.
- World systems: weather, day and night, the party light, lightning, and the Skullkeep machines (object transfer sensors).
- Sound effects: 64 voices mixed at 44100 Hz with the volume and stereo position of the original, sound requests, delayed and ambient sounds.
- Music: Nuked-OPL3 (`src/third_party/opl3/`, LGPL-2.1-or-later), the BNK loader with the MIDI to OPL conversion of `HMIMDRV.386`, the HMP player, and the songs of `SONGLIST.DAT` with fades.
- Start menu, new game, save and load with the slot dialogs, the volume dialog, the name editor, and the end of the game (end sensor and death of the party). `--save PATH` starts a save immediately; `--data DIR` sets the game folder.
- Tests: `uv run pytest` compares the C port with the original code in the emulator (differential, data, and scenario tests on `tests/saves/start.DAT`).
- Manual test checklist: `docs/manual-test-checklist.md`.
- LLM control socket: `build/dm2 --control PATH` takes keys, clicks, commands, waits, and screenshots as JSON lines; the CLI `dm2-play` sends them. Details: [control-socket.md](docs/llm/control-socket.md).
- Parallel test runs: `tools/test-suite` runs the tests with pytest-xdist, loads one copy of the library per test module, and skips a git tree that already passed.
- `tools/dosbox-nointro.sh` starts DOSBox without the intro movies.
- Game modes: `--mode vanilla|advanced`, default `advanced`. The advanced mode adds `W` `A` `S` `D` `Q` `E` for movement and `F5` and `F9` for a quick save and quick load in `DATA/QUICK.DAT`. The control state gives the mode in `game_mode`. Details: [manual.md](docs/manual.md#modes).
- Automap in the advanced mode: `Tab` or `M` opens a map of the seen squares of the party level in the viewport, with the minion map icons and a readout of the position and the cheats. The game time stops while it is open; a mouse drag scrolls it. Slot saves and `QUICK.DAT` keep the seen squares in an AMAP block after the original save data.
- The control state gives `automap` and the `cheat_*` flags.
- Movies of the PC version, in the order of the DOS launcher: the splash picture `INTRPLAY.PCX`, then the SVGA movies `FTL` and `INTRO` before the start menu; `END` and `CREDITS` after a completed game; `CREDITS` after QUIT GAME of a dead party. A start with `--save` and a restart play no start movies. A click skips a movie; in the vanilla mode a click does not stop the splash and `FTL`. The movies show on a 4:3 screen with black borders. A missing or damaged movie is skipped. The control state gives `"mode": "movie"` while a movie plays. Details: [movies.md](docs/formats/movies.md).
- Turn-based mode: `--mode turnbased` plays through the control socket with the commands `get_state`, `get_view`, `get_map`, `act`, `act_many`, `wait`, `save`, `load`, and `new_game`. The game time stands still between requests; a turn runs until the party can act again. Details: [turn-based.md](docs/llm/turn-based.md).
- `--saves DIR` sets the folder of `QUICK.DAT` in all modes.
- `dm2-play`: the forms `act <action> [args]` and `act_many <list>`, `--brief` for a short text summary, and a call log in `playthroughs/current/coordinator/calls.log` (`DM2_PLAY_LOG_DIR`).
- Windows build: `make windows` builds `build/dm2.exe` with MinGW-w64 and the SDL3 MinGW package; the turn-based mode and `--control` need macOS.
- `LICENSE` (GPL-3.0-or-later) and an SPDX line in each source file.
- `tools/rename-symbol` renames a public symbol in `src/`, `tests/`, and the docs; `tools/standard-allowlist` regenerates `tests/standard/allowlist.txt`.

### Changed

- macOS release: a double click on `dm2` in the Finder starts the game, and `Dungeon Master II.command` is gone. A start from a folder without a `game` entry changes to the folder of the program; a relative path option keeps its meaning.
- Control replies have no size limit; the error `reply too long` is gone.
- The test of the way out of the first room takes the real path: the solid key comes from the cabinet of the first room.
- `docs/coding-standard.md` has rules 7 to 9 and a check list. `tests/standard/test_standard.py` checks the rules. `tests/standard/allowlist.txt` lists the open violations.
- `src/data/le.h` has the one definition of `le16`, `le32`, `put_le16`, and `put_le32` (rule 9 of `docs/coding-standard.md`). `tests/standard/test_standard.py` checks it.
- Module cleanup of the C port: named constants instead of literals, named accessors for the thing records and the packed fields (rule 10), one definition per constant in the header of its owner, a `Prefixes` line in each header with 104 renamed functions, function splits of all functions over 60 lines, and `bool` predicates. `tests/standard/allowlist.txt` keeps only tables of `SKULL.EXE`, the save record packing, and the layer violations.
- `docs/coding-standard.md`: rule 10 (packed fields), the notation of constants (hex or decimal), accessor headers (rule 2), and prefix owners (rule 7). The build uses `-Wmissing-prototypes`.
- The decompiled code is in `reference/decomp/`. `tools/test-suite` replaces `scripts/test-suite`. `docs/todo.md` replaces `TODO.md`.
- The dm style refactor: the C port moves from `port/src/` to `src/` with five layers (`data/`, `engine/`, `ui/`, `control/`, `platform/`); the Python tools move to `tools/dm2tools/` and the test helpers to `tests/dm2test/`; `docs/coding-standard.md` has the new rules, and `tests/standard/test_standard.py` checks them with no open violation; one type per parameter name; `ui/` changes engine state only through engine functions; `make` fails on a warning.
- Review cleanup: the coding standard has the new rules 7.6, 3.13, and 10.7 with checks, the changed rules 1.7, 1.8, 2.1, 3.1, 3.2, 4.1, 5.1, 8.3, and 9.1, and `-fwrapv`; one owner per prefix and module (`champion_inventory.c`, one public `ai.h`); enum types for the game, screen, input, panel, action, and effect modes and the GDAT result; the code of all modules follows the standard; the kept quirks of the original have comments and deviation entries.
- Review follow-up: the standard checks also read `control/`, the static functions, single digits and array sizes, one statement per line, `long`, the section order, determinism, internal headers, and byte casts; square parameters take `Location` or `Position`; packed results became enum and struct results (cast, text decode, object click, sound delay, move end); one name per value and constants in their owner headers; one set of int helpers (`num.h`) and path sizes (`OS_DIR_SIZE`, `OS_PATH_SIZE`); `make windows` runs before a merge, not after each commit.
- The docs in the style of the dm port: `README.md` with a screenshot, the new pages `docs/manual.md`, `architecture.md`, `development.md` and `decompiling.md`, and the folders `docs/formats/`, `rules/`, `llm/` and `reference/`; `docs/skull-formats.md`, `llm-control.md`, `turnbased-mode.md` and `advanced-mode.md` are gone. `tests/standard/test_docs.py` checks the doc links, `tests/standard/test_vocabulary.py` the protocol vocabulary.

### Fixed

- The first word of the AI enabled-byte table matches `SKULL.EXE`; the name editor cancel restores the whole name and title; a trap fall without a leader reads no champion out of bounds; a dungeon without maps loads; the Windows log closes on restart; the control socket retries a call that a signal stopped; color ramps past their item are rejected; the Ghidra test skips without a JDK.
- Advanced mode: a held key no longer fills the input queue; the party stops when the key goes up.
- A damaged save with a poison event for a missing champion no longer writes outside the champions; guards for inactive creature groups; a melee attack with bit 15 pierces as in the original; the item description breaks lines at the line start; the price table reads its prices byte-order safe; an unknown `--` option ends with a usage line; the image cache, the timeline, and the seen squares are freed at shutdown; the color ramps are checked against their bounds.
- `test_sleep_and_wake_match_original` failed on some parallel runs: the cursor test left a dragged champion in the shared test library.
- `tests/differential/test_ai_move.py` reads the `AiContext` fields at named offsets, so the struct needs no padding field.
- The inventory and the candidate panel of a recruit reach the screen.
- Dialogs after a load (screen mode) leave no remnants, and the input works.
- Cursor: the held object shows only over the cursor zones of the original. The original arrow, hand, held object with its shadow, and dragged champion images show.
- The save menu title shows the last folder of the data path in capitals as the drive path.
- A `--save` start has the party light, the level digits, and the saved volumes of a menu load.
- RESTART drops `--save` and shows the start menu.
- The window comes to the front on startup.
- The champion drag stops when the game view hides.
