# Development

Build the port with `make`, test it with `make test`, and check it with `make check`. For the structure of the code, see [Architecture](architecture.md) (layers and modules). Follow the [Coding standard](coding-standard.md) for every change in `src/`. The checks that no test covers are in [manual-test-checklist.md](manual-test-checklist.md) (turn-based mode: [section T](manual-test-checklist.md#t-turn-based-mode)). The pages by audience are in [docs/README.md](README.md).

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

Put your DOS game (its folder, ZIP archive, or CD image) into the project folder ([install.md](formats/install.md#sources)), then start the game:

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

`tests/dm2test/` has the shared helpers, for example the project paths and the skip marker `needs_game`. Tests that need the original files are skipped if `game/SKULL.EXE` is missing. Each test module owns its state, so the tests run in any order and in parallel; how each module gets its library copy is in [architecture.md](architecture.md#the-test-library).

## Tools

| Command | Result |
|---|---|
| `make check` | `ruff`, `tests/standard`, then the full suite. |
| `make format` | `clang-format` over the C files ([coding-standard.md](coding-standard.md#format)). |
| `tools/standard-allowlist` | Regenerates `tests/standard/allowlist.txt` from the violations that the standard check finds. |
| `tools/rename-symbol OLD NEW` | Renames a public symbol in `src/`, `tests/`, the symbol table, and the docs. |
| `uv run dm2-items docs/items.md` | Regenerates [items.md](items.md) from `game/DATA/GRAPHICS.DAT` through `build/libdm2.dylib` (`make lib` first). An `.html` file name gives the HTML page instead. |
| `dm2tools.emu` (Python, in `tools/dm2tools/emu.py`) | Run a `SKULL.EXE` function in an x86 emulator. `Emu.call(addr, *args)` runs one function. A hardware access stops the call with `EmuError`; `allow_io=True` permits port access. |
| `tools/dosbox-nointro.sh` | Start the original game in DOSBox without the intro movies, for reference runs. It needs [DOSBox](https://www.dosbox.com) as `/Applications/dosbox.app`. It links every file in `game/` except the start movies and the splash (`INTRO`, `FTL`, `INTRPLAY.PCX`) into `build/dos-nointro` and uses `tools/dosbox-dm2.conf` (`SB16`, 20000 cycles, `ibmiop skull.exe`). |

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

