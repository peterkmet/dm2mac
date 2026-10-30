# Development

Build the port with `make`, test it with `make test`, and check it with `make check`. For the structure of the code, see [Architecture](architecture.md).

The port is built in three steps:

1. **Decompilation:** Ghidra decompiles `SKULL.EXE` to C ([Decompiling](decompiling.md)).
2. **Reimplementation:** the game rules in C11 follow the decompiled code ([Architecture](architecture.md)).
3. **Differential verification:** a test runs a ported C function and the original code in an emulator with the
   same input and compares the state ([Tests](#tests)).

## Build

The port uses C11, `clang`, SDL3, and `uv`. Install the tools first.

```sh
brew install sdl3
uv sync
```

| Command | Result |
|---|---|
| `make` | Builds the game `build/dm2` and the test library `build/libdm2.dylib`. The build has 0 warnings. |
| `make lib` | Builds only the library, which has no SDL code. |
| `make SANITIZE=1` | Builds with the address and undefined-behavior sanitizers. |
| `make lib CFLAGS_EXTRA=-DNAME` | Builds the library with extra compiler flags. Changing the flags recompiles all objects. |
| `make windows` | Builds `build/dm2.exe` for Windows x86-64. |
| `make dist` | `build/DM2Mac-macos-arm64.zip`: the release zip for macOS. |
| `make dist-win` | `build/DM2Mac-windows-x64.zip`: the release zip for Windows (`DM_DIR` names the `dm` folder). |
| `make clean` | Removes `build/`. |

The flags are `-std=c11 -Wall -Wextra -Werror -Wshadow -Wmissing-prototypes -Wstrict-prototypes -Wvla -Wimplicit-fallthrough -O2 -g -fwrapv`.

Put your DOS game (its folder, ZIP archive, or CD image) into the project folder ([game-files.md](formats/game-files.md#sources)), then start the game:

```sh
build/dm2                                   # New game (mode screen)
build/dm2 --mode vanilla                    # Skip the mode screen
build/dm2 --save game/DATA/SKSAVE0.DAT      # Load a save
```

All options are in the [Player manual](manual.md).

### Windows build

1. Install MinGW-w64: `brew install mingw-w64`.
2. Unpack `SDL3-devel-3.4.18-mingw.tar.gz` from the [SDL releases](https://github.com/libsdl-org/SDL/releases) into `build/sdl-mingw/`. Otherwise, the `make` rule copies `build/sdl-mingw` from the `dm` folder (default `../dm`), a folder that already holds the package.
3. Build: `make windows`, or `make windows DM_DIR=<path to the dm folder>` to copy the SDL package from a different `dm` folder.
4. Copy `SDL3.dll` from `build/sdl-mingw/SDL3-3.4.18/x86_64-w64-mingw32/bin/` next to `build/dm2.exe`.

The Windows program has no control socket and no turn-based mode. For its log, see [manual.md](manual.md#run-the-game).

To test the program with Wine, first run `WINEPREFIX=build/wineprefix wineboot --init` once from the wine binary's
folder. Then start the program:

```sh
WINE=<path to the wine binary>
WINEPREFIX=build/wineprefix "$WINE" build/dm2.exe
```

## Tests

```sh
make test
```

`make test` runs `tools/test-suite`. The script runs `uv run pytest -n auto -q` and writes the log to `build/test-suite.log`. It skips the run if the git tree is clean and a green full run of the same tree is recorded. To run one file:

```sh
uv run pytest -q tests/<folder>/test_x.py
```

To run several files, add `-n auto`.

| Folder or file | What it proves |
|---|---|
| `tests/differential/` | The same input through the port (`ctypes` on `build/libdm2.dylib`) and through the original in the emulator `dm2tools.emu` gives the same state. |
| `tests/port/` | The port code by itself. |
| `tests/tools/` | The Python tools in `tools/dm2tools/`. |
| `tests/standard/` | The coding standard (`test_standard.py`), the relative links in the docs (`test_docs.py`), and the protocol vocabulary of [control-socket.md](llm/control-socket.md#protocol-vocabulary) (`test_vocabulary.py`). |
| `tests/differential/test_creature.py` | The views of running creatures against the original. The view tests give the original zeroed active groups because they draw without active groups. |

Tests of the control socket and the turn-based mode:

| File | What it proves |
|---|---|
| `tests/port/test_state_json.py` | The JSON for the state, the view, and the map. |
| `tests/port/test_turnbased.py` | The protocol, the turn-end rules, the stops, `act_many`, `save`, `load`, and `new_game`. |
| `tests/tools/test_play_client.py` | `dm2-play`: the act forms, `--brief`, and the call log. |

```sh
uv run pytest -q -n auto tests/port/test_state_json.py tests/port/test_turnbased.py tests/tools/test_play_client.py
```

`tests/dm2test/` has the shared helpers, for example the project paths and the skip marker `needs_game`. Tests that need the original files are skipped if `game/SKULL.EXE` is missing. Each test module owns its state, so the tests run in any order and in parallel; how each module gets its library copy is in [architecture.md](architecture.md#the-test-library).

| Command | Result |
|---|---|
| `make check` | `ruff`, `tests/standard`, then the full suite. |
| `make format` | `clang-format` over the C files ([coding-standard.md](coding-standard.md#format)). |
| `tools/standard-allowlist` | Regenerates `tests/standard/allowlist.txt` from the violations that the standard check finds. |
| `tools/rename-symbol OLD NEW` | Renames a public symbol in `src/`, `tests/`, the symbol table, and the docs. |
| `uv run dm2-items docs/reference/items.md` | Regenerates [items.md](reference/items.md) from `game/DATA/GRAPHICS.DAT` through `build/libdm2.dylib` (`make lib` first). An `.html` file name gives the HTML page instead. |

## Layout

- `src/`: the C port in five layers; see [Architecture](architecture.md).
- `tools/dm2tools/`: the Python tools.
- `tools/ghidra/`: the decompile scripts.
- `tests/`: see [Tests](#tests).
- `docs/`: the documentation.
- Git-ignored data: `game/` (the original files), `reference/` (the Ghidra output and original material), `tests/saves/`. Link them into a new worktree.

## Coding standard

Follow [Coding standard](coding-standard.md) for every change in `src/`.

## Add a game rule

1. Find the original function in [skull-symbols.csv](reference/skull-symbols.csv) and its code in
   `reference/decomp/SKULL.c`.
2. Write the rule in the engine module that owns the data.
3. Name every value (rule 3.1 of [coding-standard.md](coding-standard.md#3-constants)).
4. Change the user interface only through the redraw flags or a member of `g_hooks`.
5. Write a differential test in `tests/differential/` ([Tests](#tests)).
6. Record a deviation from the original in the "Deviations from the original" section of the rule page in
   [docs/rules/](rules/).
7. Run `make check`.

## Manual checks

The checks that no test covers are in [manual-test-checklist.md](manual-test-checklist.md); the turn-based mode checks are in its section [T](manual-test-checklist.md#t-turn-based-mode).

## Documentation

- [Player manual](manual.md): the options, the modes, the controls, and the cheats.
- [Architecture](architecture.md) and [Coding standard](coding-standard.md): the structure of the code.
- [Decompiling](decompiling.md), [SKULL.EXE map](reference/skull-map.md),
  [skull-symbols.csv](reference/skull-symbols.csv), [SKULL.EXE address layout](reference/symbols/ranges.md) with its
  range notes, and [Cross-check with reDM2](reference/reference-crosscheck.md): the original program.
- [Items](reference/items.md): the values of every item.
- [Manual test checklist](manual-test-checklist.md): the checks that no test covers.
- [Control socket](llm/control-socket.md), [Turn-based mode](llm/turn-based.md), [LLM runs](llm/llm-runs.md), and [LLM playbook](llm/llm-playbook.md): a program
  plays the game.
- [TODO](todo.md): the open work. [Changelog](../CHANGELOG.md): the changes.

Data formats:

- [Graphics data files](formats/graphics.md)
- [Movies](formats/movies.md)
- [Music](formats/music.md)
- [Digital sound effects](formats/sound.md)
- [Save file format](formats/save.md)
- [Runtime layer and timing](formats/runtime.md)
- [Game files and install](formats/game-files.md)
- [ZIP archives](formats/zip.md)
- [CD images](formats/cd-image.md)

Game rules:

- [Start menu, save and load, dialogs, and end of game](rules/game-flow.md)
- [Main loop, input, and movement](rules/movement.md)
- [Dungeon view](rules/view.md)
- [Interface panels and text](rules/interface.md)
- [Champions, inventory, items, money, and shops](rules/champions.md)
- [Creatures, projectiles, magic, and minions](rules/creatures.md)
- [Weather, day and night, light, and Skullkeep machines](rules/world.md)
- [Advanced mode](rules/advanced-mode.md)
