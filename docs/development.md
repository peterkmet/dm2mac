# Development

The port builds with `make`, tests with `make test`, and checks with `make check`. For the structure of the code, see [Architecture](architecture.md).

## Build

The port uses C11, `clang`, SDL3, and `uv`. Install the tools first.

```sh
brew install sdl3
uv sync
```

| Command | Result |
|---|---|
| `make` | The game `build/dm2` and the test library `build/libdm2.dylib`. The build has 0 warnings. |
| `make lib` | The library only. It has no SDL code. |
| `make SANITIZE=1` | The build with the address and undefined-behavior sanitizers. |
| `make lib CFLAGS_EXTRA=-DNAME` | One build with extra compiler flags. A change of the flags recompiles all objects. |
| `make windows` | `build/dm2.exe` for Windows x86-64. |
| `make dist` | `build/DM2Mac-macos-arm64.zip`: the release zip for macOS. |
| `make dist-win` | `build/DM2Mac-windows-x64.zip`: the release zip for Windows (`DM_DIR` names the `dm` folder). |
| `make clean` | Remove `build/`. |

The flags are `-std=c11 -Wall -Wextra -Werror -Wshadow -Wmissing-prototypes -Wstrict-prototypes -Wvla -Wimplicit-fallthrough -O2 -g -fwrapv`.

Run the game with `build/dm2`. Run a save with `build/dm2 --save game/DATA/SKSAVE0.DAT`.

### Windows build

1. Install MinGW-w64: `brew install mingw-w64`.
2. Unpack `SDL3-devel-3.4.18-mingw.tar.gz` from the [SDL releases](https://github.com/libsdl-org/SDL/releases) into `build/sdl-mingw/`. Without it, the `make` rule copies `build/sdl-mingw` of the `dm` folder (default `../dm`):

   ```sh
   make windows DM_DIR=<path to the dm folder>
   ```

3. Copy `SDL3.dll` from `build/sdl-mingw/SDL3-3.4.18/x86_64-w64-mingw32/bin/` next to `build/dm2.exe`.

The Windows program has no control socket and no turn-based mode. It has no console: it writes its messages to `dm2.log` in the game folder.

Test the program with Wine:

```sh
WINE=<path to the wine binary>
WINEPREFIX=build/wineprefix
"$WINE" build/dm2.exe
```

On first use, run `wineboot --init` from the folder of the wine binary.

## Tests

```sh
make test
```

`make test` runs `tools/test-suite`. The script runs `uv run pytest -n auto -q` and writes the log to `build/test-suite.log`. It skips the run when the git tree is clean and a green full run of the same tree is recorded. To run one file:

```sh
uv run pytest -q tests/<folder>/test_x.py
```

To run several files, add `-n auto`.

| Folder | What it proves |
|---|---|
| `tests/differential/` | The same input through the port (`ctypes` on `build/libdm2.dylib`) and through the original in the emulator `dm2tools.emu` gives the same state. |
| `tests/port/` | The port code by itself. |
| `tests/tools/` | The Python tools in `tools/dm2tools/`. |
| `tests/standard/` | The coding standard (`test_standard.py`), the relative links of the docs (`test_docs.py`), and the protocol vocabulary of [llm-mode.md](llm/llm-mode.md) (`test_vocabulary.py`). |
| `tests/differential/test_creature.py` | The views of running creatures against the original. The view tests give the original zeroed active groups, because they draw without active groups. |

The tests of the control socket and the turn-based mode:

| File | What it proves |
|---|---|
| `tests/port/test_state_json.py` | The JSON of the state, the view, and the map. |
| `tests/port/test_turnbased.py` | The protocol, the turn end rules, the stops, `act_many`, `save`, `load`, and `new_game`. |
| `tests/tools/test_play_client.py` | `dm2-play`: the act forms, `--brief`, and the call log. |

```sh
uv run pytest -q -n auto tests/port/test_state_json.py tests/port/test_turnbased.py tests/tools/test_play_client.py
```

`tests/dm2test/` has the shared helpers, for example the paths of the project and the skip marker `needs_game`. Tests that need the original files are skipped when `game/SKULL.EXE` is missing. Each test module owns its state, so the tests run in any order and in parallel; the library copy of each module is in [architecture.md](architecture.md#the-test-library).

| Command | Result |
|---|---|
| `make check` | `ruff`, `tests/standard`, then the full suite. |
| `make format` | `clang-format` over the C files ([Format](coding-standard.md#format)). |
| `tools/standard-allowlist` | Regenerates `tests/standard/allowlist.txt` from the violations that the standard check finds. |
| `tools/rename-symbol OLD NEW` | Renames a public symbol in `src/`, `tests/`, the symbol table, and the docs. |
| `uv run dm2-items docs/reference/items.md` | Regenerates [items.md](reference/items.md) from `game/DATA/GRAPHICS.DAT` through `build/libdm2.dylib` (`make lib` first). An `.html` file name gives the HTML page instead. |

## Layout

- `src/`: the C port in five layers; see [Architecture](architecture.md).
- `tools/dm2tools/`: the Python tools. `tools/ghidra/`: the decompile scripts.
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
5. Write a differential test in `tests/differential/`: the same input through the port (the test library) and
   the original in the emulator (`dm2tools.emu`); compare the state.
6. Record a deviation from the original in the "Deviations from the original" section of the rule page in
   [docs/rules/](rules/).
7. Run `make check`.

## Manual checks

The checks that no test covers are in [manual-test-checklist.md](manual-test-checklist.md). The checks of the turn-based mode are in [section T](manual-test-checklist.md#t-turn-based-mode).
