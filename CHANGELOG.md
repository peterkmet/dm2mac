# Changelog

All notable changes to this project are in this file. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- Tools: the LE loader `dm2-le` (`src/dm2tools/le.py`), the Unicorn emulator harness `dm2tools.emu` with the Watcom register convention, and the Ghidra headless scripts that write `decomp/SKULL.c`.
- Analysis: the symbol map `docs/skull-symbols.csv` and `docs/skull-map.md`, and the data formats and port deviations in `docs/skull-formats.md`.
- Platform layer: an SDL3 window with the 320×200 indexed framebuffer at scale 3, keyboard and mouse input in original coordinates, a 240 Hz timer from the PIT divisor of `SKULL.EXE`, and the upscaling filters Scale2x and Scale3x (`--filter`, `DM2_FILTER`).
- Data layer: `GRAPHICS.DAT` with the image decoders, `DUNGEON.DAT`, the save format `SKSAVEn.DAT` (read and write in the DOS format), the random number generator, and the timeline queue.
- Dungeon view and outdoor view: walls, doors, stairs, pits, ornaments, objects, creatures, missiles, explosions, sparkle fields, the sky, rain, and clouds, with the drawing order of the original.
- Interface: the font and text, the message area, the champion status boxes, the action and spell areas, the inventory panel, the money box, and chests.
- Main loop and input: the input tree of `SKULL.EXE`, the click feedback, party movement and turning, sensors, doors, pits, teleporters, stairs, and fake walls.
- Champions, inventory, items, money, and shops: the leader hand, inventory slots, item effects, eating and drinking, damage and poison, actions and experience, throwing, recruiting, money boxes, shop displays, and merchant trading.
- Creatures and magic: active creature groups with animation and AI, path search, pushes, projectiles, explosions, missile launchers, runes and spells, light, auras, minions, and the minion map.
- World systems: weather, day and night, the party light, lightning, and the Skullkeep machines (object transfer sensors).
- Sound effects: 64 voices mixed at 44100 Hz with the volume and stereo position of the original, sound requests, delayed and ambient sounds.
- Music: Nuked-OPL3 (`port/src/opl3.c`, LGPL-2.1), the BNK loader with the MIDI to OPL conversion of `HMIMDRV.386`, the HMP player, and the songs of `SONGLIST.DAT` with fades.
- Start menu, new game, save and load with the slot dialogs, the volume dialog, the name editor, and the end of the game (end sensor and death of the party). `--save PATH` starts a save at once; `--data DIR` sets the game folder.
- Tests: `uv run pytest` compares the C port with the original code in the emulator (differential, data, and scenario tests on `tests/saves/start.DAT`).
- Manual test checklist: `docs/manual-test-checklist.md`.
- LLM control socket: `build/dm2 --control PATH` takes keys, clicks, commands, waits, and screenshots as JSON lines; the CLI `dm2-play` sends them. Details: `docs/llm-control.md`.
- Parallel test runs: `scripts/test-suite` runs the tests with pytest-xdist, loads one copy of the library per test module, and skips a git tree that already passed.
- `tools/dosbox-nointro.sh` starts DOSBox without the intro movies.

### Changed

- The test of the way out of the first room takes the real path: the solid key comes from the cabinet of the first room.

### Fixed

- The inventory and the candidate panel of a recruit reach the screen.
- Dialogs after a load (screen mode) leave no remnants, and the input works.
- Cursor: the held object shows only over the cursor zones of the original. The original arrow, hand, held object with its shadow, and dragged champion images show.
- The save menu title shows the last folder of the data path in capitals as the drive path.
- A `--save` start has the party light, the level digits, and the saved volumes of a menu load.
- RESTART drops `--save` and shows the start menu.
- The window comes to the front at the start.
- The champion drag stops when the game view hides.
