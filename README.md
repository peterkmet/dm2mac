# DM2Mac: Dungeon Master II for macOS and Windows

DM2Mac lets you play **Dungeon Master II: The Legend of Skullkeep** (PC DOS, 1995) on macOS (Apple Silicon) and
Windows 10 or later, without DOSBox.

<p align="center">
  <img src="docs/images/screenshot.png" alt="Dungeon Master II viewport with the party of four champions outside">
</p>

## What you need

Your own copy of the PC DOS version of Dungeon Master II (English): its folder, its ZIP archive, or its CD image
(`.iso`, `.img`, `.bin`). This repository and the release contain no game files. Other versions (Amiga, FM Towns, other
languages) do not work.

## Play

1. Download the [latest release](https://github.com/peterkmet/dm2mac/releases/latest): `DM2Mac-macos-arm64.zip` or
   `DM2Mac-windows-x64.zip`. Extract it.
2. Copy your game (the folder, the ZIP archive, or the CD image) into the extracted `DM2Mac` folder.
3. Start the game:
   - **macOS:** open Terminal in the folder, remove the quarantine attribute, and start the launcher (or
     double-click it in Finder):

     ```sh
     xattr -d com.apple.quarantine dm2 "Dungeon Master II.command" libSDL3.0.dylib
     open "Dungeon Master II.command"
     ```

   - **Windows:** double-click `dm2.exe`. If SmartScreen warns about an unknown publisher, click "More info", then
     "Run anyway".

On first startup the game checks your files and copies the 10 files it needs into `game/`. If a file is missing
or wrong, a message names it. Your saved games go to `game/DATA`.

## Modes

On startup you choose how to play:

- **Vanilla**: the original game, unchanged.
- **Advanced** (the default): the original game with a few additions:
  - movement with the keyboard;
  - an automap of the squares you have seen;
  - quick save and quick load;
  - a cheat screen with six cheats and a list of all spells;
  - a pixel-art filter for the window, in the options menu.

In both modes the mouse and the arrow keys work as in the original game. The keys, the cheats, and the options are in the
[advanced mode section of the Player manual](docs/manual.md#advanced-mode).

## For developers

DM2Mac is a C11 and SDL3 reimplementation of `SKULL.EXE`, built from its own Ghidra decompilation and checked
against the original in an emulator. Build instructions, tests, and the documentation index are in
[Development](docs/development.md). On macOS, a program such as an LLM can play the game turn by turn
([Control socket](docs/llm/control-socket.md), [Turn-based mode](docs/llm/turn-based.md)).

## Acknowledgements

- [SDL3](https://github.com/libsdl-org/SDL): windowing, input, and audio output.
- [Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3) by Nuke.YKT: OPL3 emulation for the music.
- [tinf](https://github.com/jibsen/tinf) by Jørgen Ibsen: ZIP decompression.
- [Unicorn](https://www.unicorn-engine.org): CPU emulation for the differential tests.
- [reDM2](https://codeberg.org/CyCl0ne/reDM2): an analysis of the same `SKULL.EXE`, read for names, formats, and
  logic.

## License

GPL-3.0-or-later ([LICENSE](LICENSE)). Nuked-OPL3 in `src/third_party/opl3/` is under the LGPL-2.1-or-later license
([opl3-LICENSE](src/third_party/opl3/opl3-LICENSE)); tinf in `src/third_party/tinf/` is under the zlib license
([tinf-LICENSE](src/third_party/tinf/tinf-LICENSE)). Dungeon Master II is the property of its rights holders. This
project is not affiliated with or endorsed by them. The game data is not included and is not covered by this license.
