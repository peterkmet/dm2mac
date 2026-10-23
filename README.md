# DM2Mac: Dungeon Master II for macOS and Windows

DM2Mac is a native C11 and SDL3 port of **Dungeon Master II** (PC DOS `SKULL.EXE`) for macOS and Windows. It is built from its own Ghidra decompilation of `SKULL.EXE`.

This repository contains no game data. You must provide your own game files and copy them into `game/`.

## Features

- **Vanilla mode:** The original rules, timing, and interface.
- **Advanced mode:** A mode screen at the start, WASD movement, quick save and quick load, an automap, an options menu, and a cheat screen.
- **Display scaling:** Smooth window scaling with live resize, and the `scale2x` and `scale3x` pixel-art filters.
- **Turn-based mode (`--mode turnbased`):** A program, for example an LLM, plays the game turn by turn through a control socket.

## Keys of the advanced mode

| Key | Action |
| --- | --- |
| `W`, `A`, `S`, `D` | Move forward, left, back, right |
| `Q`, `E` | Turn left, turn right |
| `Tab` or `M` | Automap |
| `C` | Cheat screen |
| `Esc` | Options menu |
| `F5`, `F9` | Quick save, quick load |
| `Tab` | Close the open inventory |

[Player manual](docs/manual.md) has all keys, the cheats, and the options menu.

## Build and run

```sh
make            # Build build/dm2 (0 warnings)
make lib        # Build build/libdm2.dylib
make test       # Run the test suite
make check      # Run ruff, the coding standard check, and the tests
make windows    # Cross-compile build/dm2.exe with MinGW-w64
```

```sh
build/dm2                                   # New game (mode screen)
build/dm2 --mode vanilla                    # Skip the mode screen
build/dm2 --save game/DATA/SKSAVE0.DAT      # Load a save
build/dm2 --mode turnbased --saves DIR      # Turn-based mode
```

## More

- [Player manual](docs/manual.md)
- [LLM mode](docs/llm/llm-mode.md)
- [Coding standard](docs/coding-standard.md)
- [Changelog](CHANGELOG.md)
