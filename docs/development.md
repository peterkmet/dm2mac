# Development

How to build, test, and check the port. For the structure of the code, see [Architecture](architecture.md).

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
| `make clean` | Remove `build/`. |

The flags are `-std=c11 -Wall -Wextra -Werror -Wshadow -Wmissing-prototypes -Wstrict-prototypes -Wvla -Wimplicit-fallthrough -O2 -g`.

Run the game with `build/dm2`. Run a save with `build/dm2 --save game/DATA/SKSAVE0.DAT`.

### Windows build

1. Install MinGW-w64: `brew install mingw-w64`.
2. Copy the SDL3 MinGW package into `build/sdl-mingw/`. The `make` rule copies it from `../dm/build/sdl-mingw`. In a worktree, name the `dm` folder:

   ```sh
   make windows DM_DIR=<path to the dm folder>
   ```

3. Copy `SDL3.dll` from `build/sdl-mingw/SDL3-3.4.18/x86_64-w64-mingw32/bin/` next to `build/dm2.exe`.

The Windows program has no control socket and no turn-based mode. It writes its messages to `dm2.log` in the game folder.

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
| `tests/standard/` | The coding standard (`test_standard.py`). |

`tests/dm2test/` has the shared helpers, for example the paths of the project and the skip marker `needs_game`. Tests that need the original files are skipped when `game/SKULL.EXE` is missing. Each test module loads its own copy of the library and owns its state, so the tests run in any order and in parallel.

| Command | Result |
|---|---|
| `make check` | `ruff`, `tests/standard`, then the full suite. |
| `make format` | `clang-format` over the C files. Run it before a commit. |
| `tools/standard-allowlist` | Regenerates `tests/standard/allowlist.txt` from the violations that the standard check finds. |
| `tools/rename-symbol OLD NEW` | Renames a public symbol in `src/`, `tests/`, the symbol table, and the docs. |

## Layout

- `src/`: the C port in five layers; see [Architecture](architecture.md).
- `tools/dm2tools/`: the Python tools. `tools/ghidra/`: the decompile scripts.
- `tests/`: see [Tests](#tests).
- `docs/`: the documentation.
- Git-ignored data: `game/` (the original files), `reference/` (the Ghidra output and original material), `tests/saves/`. Link them into a new worktree.

## Coding standard

Follow [Coding standard](coding-standard.md) for every change in `src/`.

## Add a game rule

See [Add a game rule](architecture.md#add-a-game-rule).

## Manual checks

The checks that no test covers are in [manual-test-checklist.md](manual-test-checklist.md).
