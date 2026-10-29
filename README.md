# DM2Mac: Dungeon Master II for macOS and Windows

DM2Mac is a native C11 and SDL3 port of **Dungeon Master II** (PC DOS `SKULL.EXE`) for macOS (Apple Silicon)
and Windows (x86-64). It is built from its own Ghidra decompilation of `SKULL.EXE`.

<p align="center">
  <img src="docs/images/screenshot.png" alt="Dungeon Master II viewport with the party of four champions outside">
</p>

This repository contains no game data. You must provide your own copy of Dungeon Master II (PC DOS, English) as a
folder, a ZIP archive, or a CD image; the game installs the 10 files it needs on first startup. See
[Play](#play) and the [Player manual](docs/manual.md#run-the-game).

## Features

- **Vanilla mode:** The original rules, timing, and interface.
- **Advanced mode:** A mode screen on startup, WASD movement, quick save and quick load, an automap, an options
  menu, and a cheat screen.
- **Display scaling:** Smooth window scaling with live resize, and the `scale2x` and `scale3x` pixel-art filters.
- **Turn-based mode (macOS only, `--mode turnbased`):** A program, for example an LLM, plays the game turn by turn
  through a control socket.

## Play

Download the [latest release](https://github.com/peterkmet/dm2mac/releases/latest) (`DM2Mac-macos-arm64.zip` or
`DM2Mac-windows-x64.zip`), extract it, and copy your own DOS files into the extracted folder (the program folder): the DOS folder, a ZIP archive,
or a CD image (`.iso`, `.img`, `.bin`).

- **macOS:** Remove the quarantine attribute and launch `Dungeon Master II.command` (or double-click it in
  Finder):

  ```sh
  xattr -d com.apple.quarantine dm2 "Dungeon Master II.command" libSDL3.0.dylib
  open "Dungeon Master II.command"
  ```

- **Windows 10+:** Run `dm2.exe`. If Windows SmartScreen warns about an unknown publisher, select "More info",
  then "Run anyway".

On first startup, the game checks the 10 files and installs them into `game/`. The saves go to `game/DATA`. The
sources and the checks are in [Game files and install](docs/formats/game-files.md).

To play from source, build the game (see [Build](#build)), put your own DOS files into the project folder, and
start it:

```sh
build/dm2                                   # New game (mode screen)
build/dm2 --mode vanilla                    # Skip the mode screen
build/dm2 --save game/DATA/SKSAVE0.DAT      # Load a save
```

On Windows, start `build/dm2.exe`. See the [Player manual](docs/manual.md) for all options, the modes, the
controls, the cheats, and the options menu.

## Let an LLM play

```sh
build/dm2 --mode turnbased --saves playthroughs/current/game
uv run dm2-play --brief get_state
uv run dm2-play act move_forward
```

[LLM mode: play over the control socket](docs/llm/llm-mode.md) has the protocol, the actions, and the setup.
[LLM runs](docs/llm/llm-runs.md) describes playthroughs with a coordinator and player agents, and
[LLM player manual](docs/llm/llm-playbook.md) is the manual of the player agent.

## Build

```sh
brew install sdl3
uv sync
make            # Build build/dm2 and the test library
make test       # Run the test suite
make check      # Check ruff, the coding standard, and the tests
make windows    # Cross-compile build/dm2.exe with MinGW-w64
```

See [Development](docs/development.md) for the build, the tests, and the Windows build,
[Architecture](docs/architecture.md) and [Coding standard](docs/coding-standard.md) for the structure of the code,
[Manual test checklist](docs/manual-test-checklist.md) for the manual checks, [TODO](docs/todo.md) for open work,
and [Changelog](CHANGELOG.md) for the changes.

## Engineering

The port is built in three parts:

1. **Decompilation:** Ghidra decompiles `SKULL.EXE` to C ([Decompiling](docs/decompiling.md)).
   [SKULL.EXE map](docs/reference/skull-map.md) is the map of the program, and
   [Cross-check with reDM2](docs/reference/reference-crosscheck.md) compares it with the reDM2 analysis.
2. **Reimplementation:** The game rules in C11 follow the decompiled code. The port does not reproduce the DOS
   technical layer.
3. **Differential verification:** Tests run a ported C function and the original code in an emulator with the
   same input and compare the state.

Data formats:

- [Graphics data files](docs/formats/graphics.md)
- [Movies](docs/formats/movies.md)
- [Music](docs/formats/music.md)
- [Digital sound effects](docs/formats/sound.md)
- [Save file format](docs/formats/save.md)
- [Runtime layer and timing](docs/formats/runtime.md)
- [Game files and install](docs/formats/game-files.md)
- [ZIP archives](docs/formats/zip.md)
- [CD images](docs/formats/cd-image.md)

Game rules:

- [Start menu, save and load, dialogs, and end of game](docs/rules/game-flow.md)
- [Main loop, input, and movement](docs/rules/movement.md)
- [Dungeon view](docs/rules/view.md)
- [Interface panels and text](docs/rules/interface.md)
- [Champions, inventory, items, money, and shops](docs/rules/champions.md)
- [Creatures, projectiles, magic, and minions](docs/rules/creatures.md)
- [Weather, day and night, light, and Skullkeep machines](docs/rules/world.md)

## Acknowledgements

- [SDL3](https://github.com/libsdl-org/SDL): windowing, input, and audio output.
- [Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3) by Nuke.YKT: OPL3 emulation for the music.
- [tinf](https://github.com/jibsen/tinf) by Jørgen Ibsen: the inflate of ZIP archives.
- [Unicorn](https://www.unicorn-engine.org): CPU emulation for the differential tests.
- [reDM2](https://codeberg.org/CyCl0ne/reDM2): an analysis of the same `SKULL.EXE`, read for names, formats, and
  logic.

## License

GPL-3.0-or-later ([LICENSE](LICENSE)). Nuked-OPL3 in `src/third_party/opl3/` has its own license, LGPL-2.1-or-later
([opl3-LICENSE](src/third_party/opl3/opl3-LICENSE)). tinf in `src/third_party/tinf/` has the zlib license
([tinf-LICENSE](src/third_party/tinf/tinf-LICENSE)). Dungeon Master II is a property of its rights holders. This
project is not affiliated with or endorsed by them. Original game data is not included and is not covered by this
license.
