# Code in the style of the dm port: PRD

## Goal

The C code, the build, the tests and the Python tools of `dm2` follow the coding standard and the layout of the `dm` port (`../dm`). A developer who knows one port finds the same layers, the same rules, the same checks and the same commands in the other. The game does not change: the tests prove that the behavior stays the same.

The source is `../dm`: `docs/coding-standard.md`, `docs/development.md` (sections Build, Tests, Layout), `Makefile`, `.clang-format`, `.editorconfig`, `.git-blame-ignore-revs`, `tests/standard/test_standard.py`, `tests/standard/allowlist.txt`, `tests/dmtest/`, `src/data/include/os.h`, `src/platform/transport.c`, `src/platform/transport_none.c`.

A second PRD, `docs/superpowers/2026-10-21-dm-style-docs.md`, covers the documentation. It starts after this PRD is done.

## Reader

The developer and the agents that write the implementation plan and the code.

## Current state

- C code: about 39 000 lines in `port/src/` with four layers (`data/`, `engine/`, `ui/`, `platform/`). The headers are next to the `.c` files; the include path holds all four layer folders, so a wrong layer include compiles.
- `port/Makefile` compiles all sources in one call with `-std=c11 -Wall -Wextra -Wmissing-prototypes -O2 -g`. No object files, no dependency files, no `format` or `check` target, no Windows build.
- No `.clang-format`, no `.editorconfig`. Headers have guards like `SENSOR_H`, no SPDX line; the `Prefixes` line comes after the includes.
- `docs/coding-standard.md`: 10 rules. `tests/test_standard.py` (203 lines) runs 11 checks over them. `tests/standard_allowlist.txt` has 26 exceptions (`hex`, `decimal`, `record`).
- Constants: both `enum` and `#define`, comments at the end of the line, hex and decimal mixed in one block.
- The control protocol (`control.c`, `state_json.c`, `turn.c`) is in `platform/`; `control.c` also holds the socket.
- `data/`, `engine/`, `ui/` reach the operating system directly: `abort()` in `engine/timeline.c`, `fprintf(stderr` in `ui/advanced.c`. The protocol files also call `abort()` (`platform/turn.c`, `platform/state_json.c`).
- Python: the package `src/dm2tools/` (`le.py`, `emu.py`, `control_client.py`). Hatchling builds it. No ruff.
- Tests: 41 modules in one level of `tests/`, shared fixtures in `tests/conftest.py`, helper `tests/creature_oracle.py`. About 20 modules import helpers and fixtures from other test modules (`from test_move import gdat, start`). `tools/test-suite` runs the full suite.
- The vendored OPL3 emulator is `port/src/ui/opl3.c`, `opl3.h`, `opl3-LICENSE`.

## Scope

In scope:

1. Tooling: `LICENSE` (GPL-3.0), `.clang-format`, `.editorconfig`, `.git-blame-ignore-revs`, a root `Makefile` with the warning flags and targets of `dm`, `clang-format` and `ruff` in the `dev` group, one format commit.
2. Layout: `port/src/` becomes `src/`; a fifth layer `control/`; public headers in `src/<layer>/include/`; private `<module>_internal.h` next to the `.c` files; vendored code in `src/third_party/`; `src/dm2tools/` becomes `tools/dm2tools/`.
3. The operating system layer: `data/os.h` with the log, fatal and file hooks of `dm`; the socket in `platform/transport.c`, `platform/transport_none.c` for Windows.
4. The Windows build: `make windows` with MinGW-w64 and the SDL3 MinGW package, as in `dm`.
5. The coding standard: `docs/coding-standard.md` takes the text of `dm`, adapted to `dm2`; `tests/standard/test_standard.py` takes the checks of `dm`, adapted to `dm2`.
6. The clean-up of all modules until the allowlist holds only the exceptions that the standard names.
7. Tests: the folders of `dm` (`differential/`, `port/`, `tools/`, `standard/`) and the helper package `tests/dm2test/`.
8. `CLAUDE.md`: the new commands and the new layout. `docs/coding-standard.md`. All other docs: only the paths and names that the move breaks (rule "A rename updates the docs").

Out of scope:

- The documentation structure and its checks (`test_docs.py`, `test_vocabulary.py`): the docs PRD.
- Release packages (`make dist`, `make dist-win`, zips) and a Windows release.
- The control socket on Windows. `--mode turnbased` stays macOS only, as in `dm`.
- Changes of the game rules, the save format, the control protocol or the screen.
- Tables that the port reads from `SKULL.EXE` at run time instead of from the source (the `dm` rule 3.12 with its engine image). `dm2` keeps its tables of `SKULL.EXE` in the source as named `const` arrays.
- A move of the plans out of the repository. `docs/superpowers/` stays.

## Acceptance

```gherkin
Feature: Build
  Scenario: macOS build
    When the developer runs make in the repository root
    Then build/dm2 and build/libdm2.dylib build with 0 warnings
    And the flags are -std=c11 -Wall -Wextra -Werror -Wshadow -Wmissing-prototypes -Wstrict-prototypes -Wvla -Wimplicit-fallthrough

  Scenario: Windows build
    When the developer runs make windows
    Then build/dm2.exe builds with MinGW-w64 GCC with 0 warnings
    And build/dm2.exe starts a new game under Wine with the game files

  Scenario: turn-based mode on Windows
    When build/dm2.exe starts with --mode turnbased or --control PATH
    Then it stops with an error message in dm2.log in the game folder

  Scenario: wrong layer include
    Given a file in src/engine/ that includes a header of src/ui/
    When the developer runs make
    Then the compiler fails because the header is not on the include path

  Scenario: checks
    When the developer runs make check
    Then ruff checks tests and tools/dm2tools
    And tests/standard passes
    And the full suite passes

Feature: Coding standard
  Scenario: standard check
    When the developer runs uv run pytest -q tests/standard
    Then each rule of docs/coding-standard.md with a check key passes
    And tests/standard/allowlist.txt has no line with the reason "open: module cleanup"

  Scenario: format
    When the developer runs make format
    Then clang-format changes no file in src/ outside src/third_party/

Feature: Same behavior
  Scenario: differential tests
    When the full suite runs after the refactor
    Then every test that passed before the refactor passes
    And no test assertion is weaker than before, except the list that the plan names and the user approves

  Scenario: game
    When the developer runs build/dm2 --save game/DATA/SKSAVE0.DAT
    Then the checks of docs/manual-test-checklist.md pass

  Scenario: turn-based mode
    When the tests of the turn-based mode and of the control socket run
    Then they pass with their assertions unchanged

  Scenario: entry points
    When the developer runs uv run dm2-play --help and uv run dm2-le without arguments
    Then dm2-play exits with 0 and dm2-le prints its usage line
```

## Requirements

### Tooling

1. `.clang-format` and `.editorconfig` take the settings of `dm`. Only the include categories change: they name the headers of the five layers of `dm2`, and the vendored and SDL headers. A commit that adds or renames a header adds it to its include category.
2. One commit runs `make format` over `src/` and changes only the format. `.git-blame-ignore-revs` holds its hash.
3. The root `Makefile` has the structure of `dm`: object files per layer in `build/obj/`, the flag stamp, `.d` dependency files, an include path per layer, `-isystem src/third_party`, SDL only for the SDL sources of `platform/`. The vendored `opl3.c` compiles with the same warning flags as the port. Targets: `all`, `lib`, `windows`, `test`, `format`, `check`, `clean`; each target comes with the step of Requirement 25 that makes it work (`windows` step 4, `format` step 5, `check` step 7). As a deviation from `dm`, the source list, the `.d` include and the flag stamp also cover the second folder level (`src/third_party/opl3/opl3.c`). `port/Makefile` and `port/` go away. `build/libdm2.dylib` holds all sources except the SDL sources of `platform/` (`main.c`, `platform.c`) and the Windows-only `platform/transport_none.c`; `make windows` links `transport_none.c` instead of `transport.c`; without a hook set, `data/os.c` logs to stderr, and the tests set their own hooks.
4. `make test` runs `tools/test-suite`. `tools/test-suite` stays: the suite takes more than 1 minute, and the script skips a clean tree that passed.
5. The `dev` group takes the `clang-format` and `ruff` version ranges of `../dm/pyproject.toml` (`clang-format>=23.1,<24`, `ruff>=0.17.1`). `ruff` checks `tests/` and `tools/dm2tools/` with the `[tool.ruff]` settings of `dm`.

### Layout

6. Five layers in this order: `data/`, `engine/`, `ui/`, `control/`, `platform/`. A file includes only headers of its own layer and of the layers before it. An include names only the file.
7. `control/` holds the protocol: the state JSON, the requests, the turns. It holds no socket. The socket is `platform/transport.c`; `platform/transport_none.c` replaces it on Windows. As in `dm`, `platform/` reads the socket and gives each request to `control/`, and sends the reply of `control/` back; `control/` includes no `platform/` header.
8. Header names are unique across the layers. The guard is `DM2_<LAYER>_<FILE>_H`. The head of each header is: SPDX line, guard, `Prefixes` line, purpose paragraph, includes. Each `.c` and `.h` file starts with the SPDX line `GPL-3.0-or-later`, as in `dm`. The root gets the file `LICENSE` with the GPL-3.0 text of `dm`.
9. `src/third_party/opl3/` holds `opl3.c`, `opl3.h` and their own license `opl3-LICENSE`, unchanged; they get no SPDX line. The standard check and the format skip it.
10. The Python package moves to `tools/dm2tools/`. `dm2-play` and every other entry point of `pyproject.toml` still work.

### Operating system layer

11. `data/os.h` and `data/os.c` take the hooks of `dm`: log, fatal, and the folder and file time services that `data/`, `engine/`, `ui/` and `control/` need. These four layers never write to stderr or stdout, never call `exit` or `abort`, and include no operating system header (POSIX, `<sys/...>`, `<unistd.h>`, `<dirent.h>`, `_WIN32`). The standard C file functions (`fopen`, `fread`, `fwrite`, `remove`) stay allowed, as in `dm` (`data/file.c`). `platform/` and the tests set the hooks.
12. An `abort()` that reproduces a system error of the original becomes `os_fatal`. An allocation failure calls `os_fatal` after the `NULL` check of `malloc`; there is no allocation wrapper, as in `dm`.
13. On Windows, `os_log` writes to `dm2.log` in the game folder (`game/` in the working folder, as `platform/main.c` sets it now) (`-mwindows` has no console). `--mode turnbased` and `--control` stop with an error message; `platform/main.c` reads all arguments and opens the log in the game folder before it rejects them. `SDL3.dll` goes next to `dm2.exe` by hand, as in `dm`; the run under Wine is a check of `docs/manual-test-checklist.md`.

### Coding standard

14. `docs/coding-standard.md` has the 11 sections, the format section, the check section and the check key table of `dm`. Each rule names its check key, says "review only", or names its exception. The text changes only where `dm2` differs:
    - the names of `dm2` modules, paths and examples;
    - the guard `DM2_`, the symbol table `docs/skull-symbols.csv`, the formats doc `docs/skull-formats.md`;
    - the little-endian helpers of `data/le.h` (`le16`, `le32`, `put_le16`, `put_le32`) instead of the big-endian helpers of `data/bytes.h`;
    - the record layout in `engine/dungeon.h`, with the named accessors of a thing type in the header of its module (the current `dm2` rule 2), instead of `engine/record.h`;
    - the hooks of `g_hooks` (`engine/hooks.h`, set by `ui/hook_table.c`);
    - the tables of `SKULL.EXE` in the source as named `const` arrays (the current `dm2` rule 6), instead of `dm` rule 3.12;
    - the exception of the action area (`ui/action_area.c`, the current `dm2` rule 8);
    - the hex notation for the ids of the data files and of `SKULL.EXE` (the current `dm2` rule 1);
    - the references to `docs/development.md` point to `CLAUDE.md` (Commands, Layout); the docs PRD moves them;
    - the rules of `dm` that `dm2` cannot use (image addresses `KAOS_`, `g_adventure`) go away.
15. `tests/standard/test_standard.py` takes every check of `dm` whose rule stays, with the paths and names of `dm2`. `tests/standard/allowlist.txt` replaces `tests/standard_allowlist.txt`.
16. Graphic, zone, layout, icon and string numbers move to `data/ids.h`, grouped by kind, sorted by value, one name per value (`dm` rule 3.7).
17. Constants are `#define` only. An `enum` names a result type only.

### Clean-up

18. While the refactor runs, a new violation goes into the allowlist with the reason `open: module cleanup`. When the refactor is done, no line has this reason; each remaining line names a rule of the standard.
19. A rename updates `docs/skull-symbols.csv`, `CLAUDE.md` and every doc that names the old name, in the same commit. `docs/superpowers/`, `docs/skull-notes/` and `CHANGELOG.md` do not change.

### Tests

20. Test folders as in `dm`: `tests/differential/` (port against the original in the emulator), `tests/port/` (port only), `tests/tools/` (Python tools), `tests/standard/`. `test_symbols.py` goes to `tests/tools/`, as in `dm`. `tests/dm2test/` holds the shared helpers (`load_lib()`, the emulator, the constants read from the headers, `creature_oracle`). A test module imports helpers only from `tests/dm2test/`.
21. A C constant in a test comes from the headers through `dm2test.constants`. It takes the parser of `dm` (`tests/dmtest/constants.py`: integer expressions over literals, other constants, casts and shifts).
22. The conftest builds with the root `Makefile`. `tests/saves/` stays.
23. All shared helpers and fixtures move to `tests/dm2test/`; no test module imports from another test module or from `conftest` (`ROOT` comes from `tests/dm2test/`).
24. `[tool.pytest.ini_options]` takes `pythonpath = ["tests"]`, `--import-mode=importlib` and `--dist loadfile` of `dm`, and keeps `-m 'not network'`.

### Order

25. The work runs in waves. A wave starts after the previous wave is merged and the full suite passes. The packages of a wave run in parallel, each in its own git worktree, and the controller merges them into the feature branch.
    1. One commit only moves files and changes only the paths in the build (`port/Makefile`), `pyproject.toml` (`packages`), `tests/conftest.py`, the tests and the docs, so that `git log --follow` finds each file: first `git mv src/dm2tools tools/dm2tools` (else `port/src` lands in `src/src/`), then `git mv port/src src`, the headers to `include/`, `control.c`, `state_json.c`, `turn.c` and their headers to `src/control/`, `src/ui/opl3*` to `src/third_party/opl3/`. The include path still holds all `include/` folders.
    2. In parallel: the root `Makefile` with all warning flags, one include path with all `include/` folders, and `tests/conftest.py` building with it, after the fix of the 17 new warnings (11 `-Wimplicit-fallthrough`, 6 `-Wshadow`, counted on 2026-10-21, `opl3.c` included); the file `LICENSE`, `.editorconfig`, the `dev` group and `ruff`.
    3. In parallel: `data/os.h` and the socket out of `control/control.c` into `platform/transport.c`; the header heads (SPDX, guard, purpose) per layer. Each header compiles alone, so that the sort of the includes in wave 5 cannot break the build.
    4. In parallel: the include path per layer, the private `*_internal.h` headers and the Windows build (`make windows`, `platform/transport_none.c`, `dm2.log`); the test folders, `tests/dm2test/` and the pytest settings.
    5. The format commit first, then in parallel: the constants of the tests from `dm2test.constants`.
    6. In parallel: the coding standard text; the standard check with the allowlist; `data/ids.h`.
    7. In parallel: the module clean-up, one package per group of modules, merged in the order of the layers.
    8. The final checks.

    `.clang-format` needs the five layers; the standard check needs `tests/dm2test/` (rule 11); a clean-up commit needs the format check.

## Invariants

1. The game rules, the save files, the control protocol and the screen do not change.
2. Every existing test keeps its assertions, except the list that the plan names under `Weakened assertions` and that the user approves.
3. Each commit builds with 0 warnings and passes the tests it touches; the full suite passes after each merge of a package. After the Windows task, `make windows` also builds with 0 warnings.
4. `game/`, `reference/`, `tests/saves/` and `playthroughs/` stay git-ignored and are not changed.
5. The socket folder and the socket keep their current access modes.

## Known consequences

- `port/` and `make -C port` stop working; old worktrees and links into `port/` break.
- `git blame` must follow the renames (`git blame -C`) and skip the format commit.
- `dm2.exe` has no turn-based mode.

## Decisions

| Topic | Decision | Reason |
|---|---|---|
| Waves | The work of Requirement 25 runs in waves of parallel packages; the LICENSE and tooling move to wave 2, the tests to wave 4, `data/ids.h` to wave 6. | User answer: the most parallel run. Each move keeps the dependencies of Requirement 25. |
| Order | The refactor starts after `feature/2026-10-10-turnbased-mode` is merged to `main`, on a new branch. | User answer: the moves of files would conflict with the open branch. |
| Python package | `tools/dm2tools/`, as `dm`. | User answer. `src/` belongs to the C code, as in `dm`. |
| Windows and `os.h` | Part of this refactor. | User answer. |
| Doc checks | `test_docs.py` and `test_vocabulary.py` go to the docs PRD. | User answer. |
| Guard prefix | `DM2_<LAYER>_<FILE>_H`. | The guard names the project. |
| Record accessors | `dm2` keeps the layout in `engine/dungeon.h` and the accessors per thing type in the module header; no `engine/record.h`. | The current `dm2` rule 2 already gives one owner per field. A move of all accessors changes many files for no gain in readability. |
| Tables of `SKULL.EXE` | They stay in the source. | `dm` reads its tables from the Amiga engine image through `kaos`; `dm2` has no such loader in C. A loader is a feature, not a style change. |
| `le.h` | It keeps its name and its helpers. | The byte order of `dm2` is little-endian; the `dm` rule needs only one owner header. |
| `tools/test-suite` | It stays; `make test` calls it. | Global rule: a suite slower than 1 minute gets a suite script. |
| Format commit | One commit with format only, its hash in `.git-blame-ignore-revs`. | As `dm`; `git blame` stays useful. |
| Plans | `docs/superpowers/` stays in the repository. | `dm2` has no private research repository. |
| License | `LICENSE` with GPL-3.0 and the SPDX line `GPL-3.0-or-later` in each file, as `dm`; `opl3` keeps its own license. | User answer. |
| Test helpers | All move to `tests/dm2test/` in this refactor. | `dm` rule 11.1; an allowlist reason for imports between test modules leaves the work half done. |
| Windows warnings | The Windows build uses the flags of the macOS build, without `-Wno-format-truncation`; the text buffers are sized for the MinGW truncation check. | Run ruling: "0 warnings with the warning flags". |
| Id spaces | Text entries of `GRAPHICS.DAT` keep `GFX_`; `TEXT_` names string ids; rule 3.7 says so. Each minion map strip has its own `ICON_` block. Zone ids have 4 hex digits. | Run ruling: one name per value inside each kind. |
| Parameter types | A parameter name has one type in all headers of a layer (F3, F4). An output pointer is `out_x`, `out_y`, `out_step`; a cell of the 5 x 5 grid is `grid_cell` (`int`, it keeps every `uint16_t` value of the original). | Run ruling: the `type` check spans modules, so one follow-up per layer did it. |
| Engine writes from `ui/` | `ui/` changes engine state only through engine functions (setters in `game.h`, `save.h`, `champion.h`, `move.h`, `action.h`, `game_text.h`). `control/` and `platform/` still write `g_session` and `g_main_loop`. | Run ruling: the check covers `ui/`; moving `Session` to `ui/` needs a decision (follow-up). |
| Packed flags | `champ_drag_cancel` passes `finish = false` to a static helper; no cancel bit in the cell. | The final review found that the `0x8000` bit in an `int16_t` cell wrote `champion(-1)` when a dragged champion died. |
| Limits | A `_MAX` name is a limit and stays decimal (`KEY_CODE_MAX` 65535). | The notation check and rule 3. |
| Bool results in tests | A test that compared `-1` or an `_OK` code now asserts `is False` or `is True` where the function returns `bool`. | Same strength; follows rule 6.1. |
| Left for later | The chest out-of-bounds read, the `creature_is_flying` script crash, the `make` equal-mtime rebuild, the remaining `long` in `control/`, naming minors: `docs/todo.md`. | They exist on `main`; the player sees no change. |

## Left to the plan

- The order of the module clean-up and the split into tasks (one module per task).
- Which file services `os.h` needs for `dm2` (the plan lists the calls of `fopen`, `mkdir`, `getenv` and others outside `platform/`).
- The restart of the program (`execvp` in `platform/main.c`) on Windows: `_execvp` of `<process.h>` in `platform/`, or another way that starts the game again.
- The default saves folder on Windows when `HOME` is not set.
- Which of the 41 test modules go to `differential/`, `port/` or `tools/`.
