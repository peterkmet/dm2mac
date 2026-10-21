# Code in the style of the dm port: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task (the run of the agentic-workflow skill, with the parallel packages of this plan). Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The C code, the build, the tests and the Python tools of `dm2` follow the coding standard and the layout of the `dm` port, with the same behavior.

**Architecture:** Eight waves. A wave holds packages; a package is a sequence of tasks on its own branch in its own git worktree. The packages of a wave run in parallel; the controller rebases each finished package onto the feature branch, runs the full suite, and starts the next wave after a green run. File ownership per package keeps the merges small; two scripts (`tools/rename-symbol`, `tools/standard-allowlist`) resolve the two kinds of shared edits mechanically.

**Tech Stack:** C11, clang, GNU make, MinGW-w64 GCC, SDL3, clang-format 23, Python 3.14 with uv, pytest with pytest-xdist, ruff.

**Spec:** `docs/superpowers/2026-10-21-dm-style-code.md`

**Model:** `/Users/lego/github/dm` (written `../dm` in this plan; in a package worktree `../dm` does not resolve, use the absolute path). Read a `dm` file where a task names it; copy its structure, not its game rules.

**Paths:** a `file:line` with `port/src/` is the state before Task 1; after Task 1 the same file is in `src/` (headers in `src/<layer>/include/`).

**Precondition:** `feature/2026-10-10-turnbased-mode` is merged to `main` (PRD Decisions, "Order"). The run starts from `main` on `feature/2026-10-21-dm-style-code`.

## Global Constraints

- The game rules, the save files, the control protocol and the screen do not change (PRD Invariant 1). The control replies stay byte for byte the same, also the absolute `path` of `screenshot` (`port/src/platform/control.c:390`).
- Every existing test keeps its assertions (Invariant 2). A test may change its imports, its paths, the names of C functions it calls, and replace a literal by the same value from `dm2test.constants`.
- Each commit builds with 0 warnings and passes the tests it touches (Invariant 3). Before Task 3: `rm -rf build && make -C port 2>&1 | grep -c warning` prints `0`. From Task 3: `make` fails on a warning (`-Werror`). From Task 12: `make windows` too.
- `game/`, `reference/`, `tests/saves/`, `playthroughs/` stay git-ignored and unchanged (Invariant 4). The socket folder keeps mode `0700` (`SOCKET_DIR_MODE`, `port/src/platform/control.c:304`) (Invariant 5).
- The coding standard: until Task 28 `docs/coding-standard.md` (the 10 rules of `dm2`); from Task 28 the new text. Follow it in every change of `src/`.
- Layers: `data`, `engine`, `ui`, `control`, `platform`. A file includes only headers of its layer and of the layers before it; an include names only the file. Only `platform/` includes SDL or an operating system header.
- A rename of a public symbol uses `tools/rename-symbol OLD NEW` (from Task 31) and adds the line `Renamed: OLD -> NEW` to the body of its commit. It updates `src/`, `tests/`, `docs/skull-symbols.csv`, `CLAUDE.md` and `docs/*.md` in the same commit (Requirement 19). `docs/superpowers/`, `docs/skull-notes/` and `CHANGELOG.md` do not change.
- Each deviation of the port goes to `docs/skull-formats.md`.
- From Task 21 every commit that changes `src/` runs `make format` first. From Task 29 every new violation goes into `tests/standard/allowlist.txt` with the reason `open: module cleanup` (Requirement 18).
- Keep existing comments. New comments: one line, what the code does in game terms.
- The author runs the build and only the tests of the changed code, never the full suite: one module `uv run pytest -q tests/<path>.py`; several `uv run pytest -q -n auto <modules>`. Always also the standard check: `uv run pytest -q tests/test_standard.py` before Task 17, `uv run pytest -q tests/standard` from Task 17. Never `-p no:xdist`.
- Tests pass in any order and in parallel. Each test module loads its own library copy and resets the global state it reads. Socket paths: `tempfile.mkdtemp(prefix="dm2c", dir="/tmp")`.
- Wine: `WINE="/Users/lego/github/wine-cross/dist/Wine CX.app/Contents/Resources/wine/bin/wine"` with its own prefix `WINEPREFIX=/Users/lego/github/dm2/build/wineprefix` (first use: `wineboot --init` from the same `bin/`); never the prefix of `wine-cross`.
- Do not download or run third-party code (libraries, CDN files, packages outside the project's lock file) to check behavior; report what you could not verify. The SDL3 MinGW package comes from `/Users/lego/github/dm/build/sdl-mingw/` (Task 12).
- Conventional Commits, one type per commit (`refactor:`, `build:`, `style:`, `test:`, `docs:`, `chore:`).
- All docs in English, ASD-STE100 style.

Test list (CLAUDE.md §2). Write a test only for: a check against a reference (original program, golden snapshot), a parser or data format, a rule with more than one outcome that no reference check or type check covers (dates, parsing, prices, state machines), a regression of a real bug (one test per bug). No tests for: constants and data tables; wiring, scaffold, CLI arguments, logging; what the compiler, a linter or a standard check catches; UI and layout (the user tests them manually); what another test already covers. No mutation tests.

In this plan the existing differential, port and golden tests prove the same behavior. New tests: the constant parser of `dm2test.constants` (Task 19, a parser) and the checks of `tests/standard/test_standard.py` (Tasks 29-30, which are themselves the standard check). Build targets, hooks, the Windows start and the scripts are wiring: the manual test checklist covers them (Task 72).

**Weakened assertions:** none planned. The new `tests/standard/test_standard.py` replaces `tests/test_standard.py`; Task 30 maps each old check to a new check that is at least as strict (table in Task 30). Approved by the user on 2026-10-21 (the empty list).

## Parallel run

This section overrides the agentic-workflow run rules "work in the main checkout", "Do not invoke using-git-worktrees" and the SDD rule "Never dispatch multiple implementation subagents in parallel", on the user's request of 2026-10-21. All other run rules stay.

**Worktrees.** Once before wave 2: `printf 'reference\ntests/saves\ngame\n' >> .git/info/exclude` (the `.gitignore` patterns with a slash do not match a symlink; the exclude file is shared by all worktrees). At the start of a wave the controller notes `BASE=$(git rev-parse HEAD)` of the feature branch. For each package `P` of the wave:

```bash
git worktree add ../dm2-pkg/P -b pkg/P "$BASE"
cd ../dm2-pkg/P
ln -s /Users/lego/github/dm2/game game
ln -s /Users/lego/github/dm2/reference reference
ln -s /Users/lego/github/dm2/tests/saves tests/saves
uv sync
```

Each dispatch prompt of the package names the worktree as the working folder. Inside a package the SDD loop runs as usual: one task after the other, author, task review, ledger. Each package has its own section in the ledger.

**Concurrency.** All packages of a wave run at the same time. Only the authors and reviewers inside one package wait for each other.

**Merge.** The waves 1-6 merge a package when it is done; wave 7 merges in the order C1 to C10 (lower layers first: their renames reach the most callers). For a package:

1. `OLD=$(git rev-parse pkg/P)`, then `git -C ../dm2-pkg/P rebase --exec 'if [ -f Makefile ]; then make -s; else make -s -C port; fi' feature/2026-10-21-dm-style-code`. On a conflict, use the resolution rules below; then `git rebase --continue`.
2. `git range-diff "$BASE..$OLD" feature/2026-10-21-dm-style-code..pkg/P` shows each commit of the package once; a commit that the rebase dropped stops the merge for a resolver.
3. From wave 6 on, after the merge of SC: `tools/standard-allowlist` in the package worktree; a change gets the commit `test: regenerate the allowlist`.
4. The full suite in the package worktree (`tools/test-suite`, in the background). Red: debugger, fix author, one new run (agentic-workflow). Other packages can run steps 1-4 at the same time in their own worktrees.
5. Green: in the main checkout `git merge --ff-only pkg/P`. When the feature branch moved since step 1 (another package merged), repeat from step 1; the suite skips nothing then, because the tree is new.
6. `git worktree remove ../dm2-pkg/P && git branch -d pkg/P`.

The next wave starts after the last package of the wave is merged and the full suite is green.

**Conflict resolution** (a resolver subagent, opus effort medium, in the package worktree):

- `tests/standard/allowlist.txt`: take the feature version, then run `tools/standard-allowlist` (Task 30). It keeps each line that still matches a violation, drops the rest, and adds each new violation with `open: module cleanup`. A new line that neither side had is a violation of the package: the resolver fixes it in the package.
- A file that the package does not own: take the feature version (`git checkout --ours <file>` during the rebase), then replay the renames of the commit (`git log -1 --format=%b REBASE_HEAD | grep '^Renamed:'`) with `tools/rename-symbol --files <file> OLD NEW`, and replay the other edits of the commit in that file by hand (a caller of a changed signature).
- `docs/skull-symbols.csv`, `.clang-format`, `CLAUDE.md`: keep the lines of both sides.
- A file that the package owns: keep both changes; the build and the tests of both packages decide.

**Ownership.** A package owns the files that its tasks list under **Files**. It changes another file only for: a caller of a public symbol that it changes, a `tools/rename-symbol` run, its own lines of `tests/standard/allowlist.txt`, its rows of `docs/skull-symbols.csv`, an include category of `.clang-format`.

**Model selection.** As agentic-workflow, plus: the merge resolver opus, effort medium. The **Model** column of the wave map gives the author of each task.

## Wave map

| Wave | Package | Tasks | Author model | Needs |
|---|---|---|---|---|
| 1 | M | 1 | sonnet medium | precondition |
| 2 | B build | 2, 3 | sonnet low | wave 1 |
| 2 | T tooling | 4 | sonnet low | wave 1 |
| 3 | O os and control | 5, 6, 7 | opus medium (5, 6), sonnet low (7) | wave 2 |
| 3 | HD heads data, engine | 8, 9 | sonnet low | wave 2 |
| 3 | HU heads ui | 10 | sonnet low | wave 2 |
| 4 | W layers and Windows | 11, 12 | opus medium | wave 3 |
| 4 | TS tests | 13-19 | opus medium (14-16), sonnet low (13, 17-19) | wave 3 |
| 5 | F format | 20, 21 | sonnet low | wave 4; in the main checkout, merged before the other wave 5 packages rebase |
| 5 | K1 test constants | 22, 23, 24 | sonnet low | wave 4 |
| 5 | K2 test constants | 25, 26 | sonnet low | wave 4 |
| 5 | K3 test constants | 27 | sonnet low | wave 4 |
| 6 | SD standard text | 28 | opus medium | wave 5 |
| 6 | SC standard check | 29, 30 | opus medium | wave 5 |
| 6 | I rename and ids | 31, 32, 33 | sonnet low (31), opus medium (32, 33) | wave 5 |
| 7 | C1 data | 34-39 | per task | wave 6 and the allowlist regeneration |
| 7 | C2 engine world | 40-44 | per task | as C1 |
| 7 | C3 engine creatures | 45-47 | per task | as C1 |
| 7 | C4 engine champions | 48-52 | per task | as C1 |
| 7 | C5 engine movement and save | 53-56 | per task | as C1 |
| 7 | C6 engine items and magic | 57-60 | per task | as C1 |
| 7 | C7 ui view | 61-64 | per task | as C1 |
| 7 | C8 ui input | 65-67 | per task | as C1 |
| 7 | C9 ui session and audio | 68, 69 | per task | as C1 |
| 7 | C10 control and platform | 70, 71 | per task | as C1 |
| 8 | Z final | 72 | sonnet low | wave 7 |

The packages K1-K3 change different files: the helpers (Task 22) and the test modules (Tasks 23-27) do not depend on each other.

## Decisions of this plan

These follow from the PRD and the code; the plan reviewer and the user can reject one.

| Topic | Decision | Reason |
|---|---|---|
| Services of `os.h` | `os_log`, `os_fatal` and `os_get_full_path` with the hooks `g_os_log_hook`, `g_os_fatal_hook`, `g_os_full_path_hook`. No folder list, no make folder, no file time. | `data/`, `engine/`, `ui/` use only standard C file functions (`fopen`, `fread`, `fwrite`, `remove`, `rename`). `control.c` uses `realpath` for the absolute path of `screenshot` (`port/src/platform/control.c:390`), which the protocol must keep. `mkdir`, `lstat`, `unlink` belong to the socket and move to `platform/transport.c`. |
| Allocation failures | An allocation without a `NULL` handling now calls `os_fatal("out of memory")`: `port/src/platform/state_json.c:48-49`, `port/src/platform/turn.c:924`, `port/src/engine/timeline.c:66-67`, `port/src/engine/ai_move.c:2204,2308,2332`, `port/src/engine/creature.c:433`, `port/src/engine/seen.c:32`, `port/src/engine/save.c:805-806`, `port/src/engine/dungeon.c:436,491,494`. An allocation whose `NULL` gives an error result now keeps it. | Requirement 12; an error result that the caller reports is already a defined behavior. |
| Restart on Windows | `_execvp` of `<process.h>` and `_putenv_s` under `#ifdef _WIN32` in `platform/main.c`; POSIX keeps `execvp` and `setenv` (`port/src/platform/main.c:352-358`). | Smallest change that starts the game again; only `platform/` sees `_WIN32`. |
| Saves folder on Windows | No change. | The default save folder is `<data_dir>/DATA` (`port/src/engine/game.h:67`); `HOME` is read only for the control socket (`port/src/platform/main.c:278-289`), which Windows does not have. |
| Messages of `platform/main.c` | They go through `os_log`. | On Windows they reach `dm2.log` (Requirement 13). |
| Transport API | `platform/transport.h`: `bool transport_is_available(void)`, `bool transport_open(const char *path)`, `void transport_close(void)`, `void transport_poll(void)`, as `dm` (`src/platform/include/transport.h:13-21`). `control/control.h` gives the line protocol: `bool control_handle_line(const char *line, Json *reply)`, `bool control_end_turn(Json *reply)`, `bool control_is_turn_pending(void)`, `void control_drop_client(void)`, `void control_reset(void)`, `void control_frame(void)`. | Requirement 7 and the direction of `dm` (transport calls control, never the reverse). |
| Test folders | A module with at least one emulator test goes to `differential/`: also `test_gdat.py`, `test_automap.py` and `test_music.py` (own unicorn on `HMIMDRV.386`). Full list in Task 18. | The `dm` split: port against the original. |
| Helper modules | `tests/dm2test/`: `markers.py`, `lib.py`, `constants.py`, `scenario.py` (from `test_move`), `save.py`, `creature.py`, `oracle.py` (`creature_oracle`), `interface.py`, `view.py`, `input.py`, `dialog.py`, `control.py`, `modes.py`, `gdat.py`, `audio.py` (from `test_sound`, `test_music`). | One helper module per hub of the current imports; the name says the subject. |
| `ids.h` groups | `ZONE_` (zones), `ICON_` (icons), `TEXT_` (string ids), `GFX_` (graphic entry numbers of `GRAPHICS.DAT`). No `LAYOUT_` group. `CAT_` and `GDAT_` stay in `data/gdat.h`. | `dm` rule 3.7. The `LAYOUT_` names of `ui/zone.c:8-14` are offsets of the layout format, not ids; `CAT_` are categories of the file format. `TEXT_DATA`, `TEXT_VISIBLE` (`engine/dungeon.h:107,195`) are record fields and stay. |
| Rule numbers | The new standard keeps the section and rule numbers of `dm`. A rule that `dm2` cannot use keeps its number with the line "No rule in dm2: ..." | A developer finds the same rule under the same number in both ports. |
| Exception rules | `EXCEPTION_RULES = {"1.5", "1.7", "3.11", "3.12", "4.8", "5.8"}`: 1.5 the action area (current `dm2` rule 8), 1.7 `main.c`, 3.11 the save image (`engine/save.c`), 3.12 the tables of `SKULL.EXE` and the scan code tables, 4.8 and 5.8 as `dm`. | The exceptions of the current `tests/standard_allowlist.txt` and of `dm`. |
| Byte order helpers | `put_be32` of the PNG writer (`port/src/platform/control.c:322-325` `be32`) moves to `data/le.h`; rule 3.10 names `data/le.h` as the one owner of the byte order helpers. | `dm` rule 3.10: one owner header. |
| Merge of two tasks | Requirement 25 wave 2 has the warning fix (Task 2) before the `-Werror` Makefile (Task 3) in one package. | Each commit builds with 0 warnings. |

## Review Focus

1. A rebase that drops a caller edit in a file that the package does not own: expected the build or the full suite to fail, and `git range-diff` to show every commit. The merge procedure (steps 1-2) pins it.
2. A wrong layer include: expected `make` to fail with "file not found". Task 11 step 5 pins it with a probe file that it does not commit.
3. `screenshot` after the move of `realpath` behind `os_get_full_path`: expected the same absolute path in the reply (`/private/var/...` for `/var/...`). Task 5 runs `tests/test_control.py -k screenshot`; `dm2test.lib.load_lib` sets the hook (Task 13).
4. A client that closes the socket during a turn, and the reset of the lost game flag at close (commit `5648d12`): expected the same as before the transport split. Task 6 runs `tests/test_turnbased.py` and `tests/test_control.py` unchanged except the C function names.
5. The format commit after a rebase: expected its hash in `.git-blame-ignore-revs` to stay valid. Task 20 commits on the feature branch in the main checkout, and no later merge rebases it.

---

## Wave 1

### Task 1: Move the files

Package M. Requirement 25 wave 1, Requirements 2, 9, 10.

**Files:**
- Move: `src/dm2tools/` to `tools/dm2tools/`; `port/src/` to `src/`; each `src/<layer>/*.h` to `src/<layer>/include/`; `port/src/platform/{control,state_json,turn}.{c,h}` to `src/control/` (headers to `src/control/include/`); `port/src/ui/opl3.c`, `opl3.h`, `opl3-LICENSE` to `src/third_party/opl3/`
- Modify: `port/Makefile`, `pyproject.toml:18`, `tests/conftest.py:44`, `tests/test_standard.py:8-10`, `tests/standard_allowlist.txt`, `CLAUDE.md`, `docs/*.md` (only the paths)

**Interfaces:**
- Produces: the layout `src/{data,engine,ui,control,platform}/`, `src/<layer>/include/*.h`, `src/third_party/opl3/`, `tools/dm2tools/`.

- [ ] **Step 1:** `git mv src/dm2tools tools/dm2tools`; `rm -rf src/dm2tools/__pycache__; rmdir src` (stop when `src` still exists); then `git mv port/src src`, then the header, control and opl3 moves above with `git mv`.
- [ ] **Step 2:** `port/Makefile`: `LAYERS = data engine ui control platform`; the sources `../src/<layer>/*.c` and `../src/third_party/opl3/opl3.c`; one include path with `-I../src/<layer>/include` for each layer and `-I../src/third_party/opl3`; `GAME` excludes `../src/platform/main.c` and `../src/platform/platform.c`.
- [ ] **Step 3:** `pyproject.toml`: `packages = ["tools/dm2tools"]`; run `uv sync`.
- [ ] **Step 4:** `tests/conftest.py:44`: the bool parser reads `ROOT/"src"/"**"/"*.h"`. `tests/test_standard.py`: `SRC = ROOT/"src"`, the vendored files under `third_party/`, the layer list with `control` between `ui` and `platform`, and in `test_layers` (`tests/test_standard.py:190,193`) the layer of a file is `path.relative_to(SRC).parts[0]` (as `dm` `layer_of`), not `path.parent.name`. `tests/standard_allowlist.txt`: `platform/control.c` becomes `control/control.c`; the comment says "relative to src/".
- [ ] **Step 5:** Replace the paths `port/src/`, `src/dm2tools` in `CLAUDE.md` (Layout) and in `docs/*.md` (counts: `docs/skull-formats.md` 16, `docs/coding-standard.md` 3, `docs/llm-control.md` 1, `docs/reference-crosscheck.md` 1; the docstrings of `tests/test_play_client.py`, `test_automap`, `test_modes`, `test_state_json`, `test_pcx`, `test_turnbased`, `test_movie`, `test_mve`, `test_control`, `test_music.py:25`; the comment `port/src/engine/magic.h:47`; `grep -rn 'port/src\|src/dm2tools' --exclude-dir=superpowers --exclude-dir=skull-notes docs tests src tools CLAUDE.md` is empty after the step). The command `make -C port` stays until Task 3.
- [ ] **Step 6:** Verify: `rm -rf build && make -C port 2>&1 | grep -c warning` prints `0`; `uv run dm2-play --help` exits with 0; `uv run pytest -q -n auto tests/test_standard.py tests/test_control.py tests/test_le.py` passes.
- [ ] **Step 7:** `git log --follow --oneline src/engine/include/sensor.h | tail -1` shows the first commit of `port/src/engine/sensor.h`.
- [ ] **Step 8:** Commit `refactor: move the sources to src/ with a control layer and include folders`.

## Wave 2

### Task 2: Fix the warnings of the new flags

Package B. Requirement 25 wave 2.

**Files:**
- Modify: `src/engine/ai_move.c` (827, 2029, 2039), `src/engine/item.c` (174, 183), `src/engine/move.c` (470), `src/engine/projectile.c` (730), `src/engine/sensor.c` (1517), `src/ui/view.c` (579, 1940, 2379, 538), `src/ui/action_area.c` (135), `src/ui/draw.c` (129), `src/ui/input.c` (624, 644), `src/ui/ui.c` (173), `src/ui/include/video.h` (18, if the global is renamed)

- [ ] **Step 1:** List the warnings: `for f in src/*/*.c src/third_party/opl3/opl3.c; do clang -std=c11 -Wall -Wextra -Wshadow -Wmissing-prototypes -Wstrict-prototypes -Wvla -Wimplicit-fallthrough -fsyntax-only $(for l in data engine ui control platform; do echo -Isrc/$l/include; done) -Isrc/third_party/opl3 -I$(brew --prefix sdl3)/include $f; done 2>&1 | grep warning`. Expected 17 (11 `-Wimplicit-fallthrough`, 6 `-Wshadow`, line numbers above).
- [ ] **Step 2:** Each fallthrough gets `__attribute__((fallthrough));` in place of its `/* fall through ... */` comment line; the reason of the comment moves to the end of the statement before it (as `../dm/src/engine/sensor.c:229`). Example `engine/ai_move.c:826`. A fallthrough without a comment that the original does not have is a stop: report it.
- [ ] **Step 3:** The three shadows of the global `palette` (`ui/draw.c:129`, `ui/input.c:624`, `ui/ui.c:173`): rename the local or the parameter, not the global: `ui/draw.c:129` and `ui/input.c:624` `colors`, `ui/ui.c:173` `face_colors` (`ui.c:177` already has a local `colors`). The other three: rename the inner name (`ui/action_area.c:135` `slot_index`, `ui/input.c:644` `image`, `ui/view.c:538` `party_slot` or a name from its use).
- [ ] **Step 4:** Step 1 prints no warning. `make -C port` builds; `uv run pytest -q -n auto tests/test_view.py tests/test_input.py tests/test_move.py tests/test_projectile.py tests/test_ai_move.py` passes.
- [ ] **Step 5:** Commit `refactor: remove the fallthrough and shadow warnings`.

### Task 3: The root Makefile

Package B. Requirements 3, 4, 22.

**Files:**
- Create: `Makefile`
- Delete: `port/Makefile`, `port/`
- Modify: `tests/conftest.py:20-23`, `CLAUDE.md` (Commands), `docs/llm-control.md:14`, `docs/manual-test-checklist.md:55,63`, `docs/turnbased-mode.md:12`

**Interfaces:**
- Produces: `make` (all), `make lib`, `make test`, `make clean`; `build/obj/<layer>/*.o`; `build/dm2`, `build/libdm2.dylib`. Variables for later tasks: `SOURCES`, `SDL_SOURCES`, `LIB_SOURCES`, `INCLUDES`, `OBJ`, `C_FILES`.

- [ ] **Step 1:** Write `Makefile` after `../dm/Makefile:1-56` and its `clean`, `-include`, `FORCE` lines: `BASE_CFLAGS = -std=c11 -Wall -Wextra -Werror -Wshadow -Wmissing-prototypes -Wstrict-prototypes -Wvla -Wimplicit-fallthrough -O2 -g`, `CPPFLAGS = -isystem src/third_party -MMD -MP`, the flag stamp, `SANITIZE`, `CFLAGS_EXTRA`, `SDL_PREFIX ?= $(shell brew --prefix sdl3)`. Differences from `dm`: one include path `INCLUDES` with all five `src/<layer>/include` and `-Isrc/third_party/opl3` (the per layer path is Task 11); `SOURCES = $(wildcard src/*/*.c src/third_party/*/*.c)`; `SDL_SOURCES = src/platform/main.c src/platform/platform.c`; the `-include` and the stamp `rm` cover `$(OBJ)/*/*.d`, `$(OBJ)/*/*/*.d` and `*/*/*.o`; targets `all lib test clean`, no `windows`, `format`, `check`, `dist`, `play`.
- [ ] **Step 2:** `test: ; tools/test-suite`.
- [ ] **Step 3:** `tests/conftest.py`: `pytest_configure` runs `make` (`dm` `tests/conftest.py`: `make all` when SDL3 is installed, else `make lib`; `pytest.UsageError` on failure), still only in the controller.
- [ ] **Step 4:** `git rm -r port`; replace `make -C port` with `make` in `CLAUDE.md` and the three docs above.
- [ ] **Step 5:** Verify: `rm -rf build && make 2>&1 | grep -c warning` prints `0`; `touch src/data/include/le.h && make` recompiles the users of `le.h` only; `make CFLAGS_EXTRA=-DX` recompiles all objects; `uv run pytest -q tests/test_scale.py tests/test_control.py` passes.
- [ ] **Step 6:** Commit `build: add the root Makefile with the warning flags of dm`.

### Task 4: License, editor settings, dev group and ruff

Package T. Requirements 5, 8 (LICENSE).

**Files:**
- Create: `LICENSE` (copy of `../dm/LICENSE`), `.editorconfig` (copy of `../dm/.editorconfig`)
- Modify: `pyproject.toml` (dev group, `[tool.ruff]`), `uv.lock`, the Python files that ruff flags in `tests/` and `tools/dm2tools/` except `tests/conftest.py`

- [ ] **Step 1:** Copy the two files.
- [ ] **Step 2:** `uv add --dev 'clang-format>=23.1,<24' 'ruff>=0.17.1'`.
- [ ] **Step 3:** `pyproject.toml`: `[tool.ruff]` `line-length = 160`, `src = ["tests", "tools/dm2tools"]`; `[tool.ruff.lint]` `select = ["E4", "E9", "F"]` (`../dm/pyproject.toml`).
- [ ] **Step 4:** `uv run ruff check tests tools/dm2tools`; fix each finding without a change of behavior (unused imports, unused variables, `import *`). A finding in `tests/conftest.py` goes to Task 3's owner: leave it and report it.
- [ ] **Step 5:** Verify: `uv run ruff check tests tools/dm2tools` reports only `tests/conftest.py` or nothing; `uv run pytest -q -n auto` over the changed test modules passes.
- [ ] **Step 6:** Commit `chore: add the license, the editor settings and ruff` (one commit for files and settings), then `style: fix the ruff findings` (one commit for the fixes).

## Wave 3

### Task 5: The operating system layer

Package O. Requirements 11, 12.

**Files:**
- Create: `src/data/include/os.h`, `src/data/os.c`
- Modify: `src/engine/timeline.c:58,66-67,138`, `src/engine/{ai_move,creature,seen,save,dungeon}.c` (allocations), `src/data/include/le.h` and `src/control/control.c:322-325` (`put_be32`), `src/ui/advanced.c:110`, `src/control/state_json.c:48-49`, `src/control/turn.c:924`, `src/control/control.c:384-398` (screenshot), `src/platform/main.c` (hook setup), `tests/conftest.py` (lib fixture: the full path hook)

**Interfaces:**
- Produces (`os.h`, prefix `os_`):
  - `typedef void (*OsLogHook)(const char *message); extern OsLogHook g_os_log_hook;`
  - `typedef void (*OsFatalHook)(const char *message); extern OsFatalHook g_os_fatal_hook;`
  - `typedef bool (*OsFullPathHook)(const char *path, char *full, size_t size); extern OsFullPathHook g_os_full_path_hook;`
  - `void os_log(const char *format, ...);` the hook, or stderr with a newline when it is `NULL`
  - `_Noreturn void os_fatal(const char *format, ...);` the fatal hook (or stderr), then `exit(EXIT_FAILURE)`
  - `bool os_get_full_path(const char *path, char *full, size_t size);` the hook; `false` without a hook
- Consumes: `../dm/src/data/os.c:10-41` (`report`, 512 byte buffer).

- [ ] **Step 1:** Write `os.h` and `os.c` after `dm` with the three hooks above (the head procedure of Task 7).
- [ ] **Step 2:** `abort()` in `engine/timeline.c:58,138` becomes `os_fatal` with the message of the system error of the original (the comment next to each `abort` names it). `state_json.c:49` and `turn.c:924` become `os_fatal("out of memory")`; the allocations of the Decisions row "Allocation failures" in `engine/` get a `NULL` check with `os_fatal("out of memory")` (check each line first: `grep -n 'alloc' <file>`).
- [ ] **Step 3:** `ui/advanced.c:110` `fprintf(stderr, ...)` becomes `os_log(...)` with the same text.
- [ ] **Step 4:** `control.c` `screenshot`: `realpath(path, absolute)` becomes `os_get_full_path(path, absolute, sizeof absolute)`; `absolute` gets a named size (`FULL_PATH_SIZE` 1024, as `PATH_MAX` of macOS); `<limits.h>` goes when nothing else needs it.
- [ ] **Step 4b:** `control.c:322-325` `be32` moves to `data/include/le.h` as `put_be32` (Decisions "Byte order helpers").
- [ ] **Step 5:** `platform/main.c`: a static `full_path` function with `realpath`, set into `g_os_full_path_hook` before `control_open`.
- [ ] **Step 6:** `tests/conftest.py` lib fixture: a module level `ctypes.CFUNCTYPE(ctypes.c_bool, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t)` callback with `os.path.realpath` that writes the bytes and a 0 into `full` when they fit, set through `ctypes.c_void_p.in_dll(lib, "g_os_full_path_hook")` (`../dm/tests/dmtest/lib.py:33-57` shows the pattern).
- [ ] **Step 7:** `grep -rnE '\babort\(|\bexit\(|stderr|stdout|\bprintf\(|\bputs\(' src/{data,engine,ui,control}` shows only `src/data/os.c`.
- [ ] **Step 8:** Verify: `make`; `uv run pytest -q -n auto tests/test_control.py tests/test_turnbased.py tests/test_random_timeline.py tests/test_modes.py` and `uv run pytest -q tests/test_standard.py` pass; `uv run pytest -q tests/test_control.py -k screenshot` passes.
- [ ] **Step 9:** Commit `refactor: route logs, fatal errors and full paths through data/os.h`.

### Task 6: The socket in platform/transport.c

Package O. Requirement 7, Invariant 5.

**Files:**
- Create: `src/platform/include/transport.h`, `src/platform/transport.c`
- Modify: `src/control/control.c`, `src/control/include/control.h`, `src/control/turn.c` (uses of `control.h`), `src/platform/main.c` (145, 169-182, 211, 303, 328), `tests/test_control.py`, `tests/test_movie.py`, `tests/test_turnbased.py` (C function names only)

**Interfaces:**
- Consumes: `os.h` (Task 5).
- Produces: `transport.h` and the `control.h` API of the Decisions table. `transport_open` returns `true` on success (the old `control_open` returned an `int`; the tests call it with `assert`, `assert not`).

- [ ] **Step 1:** Read `../dm/src/platform/transport.c` (all) and `../dm/src/control/control.c` (the functions of `control.h`). Map the `dm2` functions of `control.c` (`port/src/platform/control.c`): socket `close_client` 214-222, `send_reply` 225-234, `prepare_connection` 642-649, `drop_closed_client` 652-658, `accept_clients` 661-674, the socket part of `read_requests` 677-702, `control_open` 704-728, the socket part of `control_close` 766-775, `SEND_FLAGS` 52-56, `socket_path` 59, the includes 2-12; protocol: everything else.
- [ ] **Step 2:** Move the socket part to `transport.c`. `transport_open` keeps the folder mode `SOCKET_DIR_MODE` 0700, the `lstat`/`S_ISSOCK` check and the `unlink` of a stale socket (`control.c:710-719`). The "busy" reply to a second client stays in `transport.c`.
- [ ] **Step 3:** `control.c` builds each reply into a `Json` (the writer of `state_json.h`) instead of `put`/`send_reply`; `control_handle_line` answers one line; it returns `false` when a turn or a wait holds the reply; `control_end_turn` gives that reply later. `control_drop_client` does the protocol part of `close_client` (`turn_drop_reply`, the wait). A new `void control_reset(void)` does the protocol part of `control_close` (`game_lost = 0`, `control.c:774`, commit `5648d12`); `transport_close` calls it. The lost game flag resets only when the socket closes, as now.
- [ ] **Step 4:** `control_frame` keeps the play ticks and no longer polls (`control.c:758`); `main.c` calls `transport_poll()` where it called `control_poll()` and before each `control_frame()` (`main.c:180`), `transport_open`/`transport_close` for `control_open`/`control_close`.
- [ ] **Step 5:** `control/` includes no `platform/` header and no socket header: `grep -n 'sys/\|unistd\|transport.h' src/control` is empty.
- [ ] **Step 6:** Tests: `control_open`, `control_poll`, `control_close`, `control_turn_pending` become `transport_open`, `transport_poll`, `transport_close`, `control_is_turn_pending`; the `restype` of `transport_open` is `c_bool`. The pump of `tests/test_turnbased.py:38,45,54` calls `transport_poll()` before `control_frame()` and sets `play_frame_hook` to `transport_poll` (an adaptation of the harness, as `main.c`). No assertion changes.
- [ ] **Step 7:** Verify: `make`; `uv run pytest -q -n auto tests/test_control.py tests/test_turnbased.py tests/test_movie.py tests/test_modes.py tests/test_state_json.py` and `uv run pytest -q tests/test_standard.py` pass; a short Python check (not committed) with `load_lib`-style ctypes calls `transport_open(b"<tmp>/sub/control.sock")` and `oct(os.stat("<tmp>/sub").st_mode & 0o777)` is `0o700`.
- [ ] **Step 8:** Commit `refactor: move the control socket to platform/transport.c`.

### Task 7: Heads of the control and platform files

Package O. Requirement 8.

**Files:**
- Modify: every `.c` and `.h` of `src/control/` and `src/platform/`, and `src/engine/timeline.c`, `src/ui/advanced.c` (the two files of Task 5 outside these layers); not `src/data/os.c`, `os.h`, `transport.*` (Tasks 5, 6 wrote their heads)

**Head procedure** (also Tasks 8-10):
- A header starts: `/* SPDX-License-Identifier: GPL-3.0-or-later */`, `#ifndef DM2_<LAYER>_<FILE>_H`, `#define DM2_<LAYER>_<FILE>_H`, `/* Prefixes: ... */` on line 4, a blank line, a purpose paragraph (one to three comment lines: what the module holds in game terms), the includes. `#endif /* DM2_<LAYER>_<FILE>_H */` at the end. Compare `head -12 ../dm/src/engine/include/sensor.h` for the exact form; `dm` uses the guard `DM_`.
- A `.c` file starts with the SPDX line; its current purpose comment follows.
- A header gets the includes it needs to compile alone: `clang -std=c11 -fsyntax-only -Wall -Wextra -Wno-unused-function $(INCLUDES) -x c src/<layer>/include/<file>.h` passes.

- [ ] **Step 1:** Apply the head procedure to each file.
- [ ] **Step 2:** Verify: each header of the package compiles alone (loop over the files); `make`; `uv run pytest -q -n auto tests/test_control.py tests/test_turnbased.py tests/test_scale.py` and `uv run pytest -q tests/test_standard.py` pass.
- [ ] **Step 3:** Commit `style: add the file heads of control/ and platform/`.

### Task 8: Heads of the data files

Package HD. Requirement 8.

**Files:**
- Modify: every `.c` and `.h` of `src/data/` except `os.c`, `os.h`

- [ ] **Step 1:** Apply the head procedure of Task 7 to each file.
- [ ] **Step 2:** Verify as Task 7 step 2 with `tests/test_gdat.py tests/test_gfx.py tests/test_mve.py tests/test_pcx.py`.
- [ ] **Step 3:** Commit `style: add the file heads of data/`.

### Task 9: Heads of the engine files

Package HD. Requirement 8.

**Files:**
- Modify: every `.c` and `.h` of `src/engine/` except `timeline.c`

- [ ] **Step 1:** Apply the head procedure of Task 7 to each file.
- [ ] **Step 2:** `engine/ai_move.h`, `engine/champ_action.h`, `engine/hooks.h` get a `Prefixes` line (the prefixes of their functions; `hooks.h`: `g_hooks` has no function prefix, the line names `Hooks`).
- [ ] **Step 3:** `engine/merchant.h` includes `<stdint.h>`; its comment at line 12 says that `money.c` defines the declarations: keep it (the module check of wave 7 decides the owner).
- [ ] **Step 4:** Verify as Task 7 step 2 with `tests/test_move.py tests/test_world.py tests/test_save.py`.
- [ ] **Step 5:** Commit `style: add the file heads of engine/`.

### Task 10: Heads of the ui files

Package HU. Requirement 8.

**Files:**
- Modify: every `.c` and `.h` of `src/ui/` except `advanced.c`

- [ ] **Step 1:** Apply the head procedure of Task 7 to each file.
- [ ] **Step 2:** Verify as Task 7 step 2 with `tests/test_interface.py tests/test_view.py tests/test_dialog.py tests/test_music.py`.
- [ ] **Step 3:** Commit `style: add the file heads of ui/`.

## Wave 4

### Task 11: Include path per layer and private headers

Package W. Requirements 3, 6, 9.

**Files:**
- Modify: `Makefile`, `src/ui/fm.c` (the include of `opl3.h`), headers that become private
- Create: `src/<layer>/<module>_internal.h` where step 2 finds one

**Interfaces:**
- Produces: `INC_data` ... `INC_platform` and `layer_inc` as `../dm/Makefile:9-15`.

- [ ] **Step 1:** `Makefile`: replace `INCLUDES` with `INC_<layer>` and `$(call layer_inc,$<)` in the object rule (`../dm/Makefile:9-15,50-52`). `src/third_party/opl3/opl3.c` compiles with `-isystem src/third_party` only; the `layer_inc` of a third party file is empty.
- [ ] **Step 2:** `src/ui/fm.c` includes `<opl3/opl3.h>`.
- [ ] **Step 3:** Private headers: a header that only the `.c` files of its own module include (the files that share its prefix) moves next to them as `<module>_internal.h` with `git mv`. Candidates from the include graph of 2026-10-21: none of the current headers is used only by its own module (`ui/automap.h` is used by `ui/advanced.c`, `ui/fm.h` by `ui/music.c`: other modules). Check the candidates `data/mve.h` (`mve.c`, `mve_video.c`, `ui/movie.h`) and `engine/ai_move.h`; move only a part that only the module uses. When no part qualifies, this step changes nothing.
- [ ] **Step 4:** Verify: `rm -rf build && make 2>&1 | grep -c warning` prints `0`.
- [ ] **Step 5:** Probe (not committed): add `#include "view.h"` to `src/engine/random.c`; `make` fails with "'view.h' file not found"; revert.
- [ ] **Step 6:** `uv run pytest -q -n auto tests/test_music.py tests/test_movie.py tests/test_mve.py` and `uv run pytest -q tests/test_standard.py` pass.
- [ ] **Step 7:** Commit `build: give each layer its own include path`.

### Task 12: The Windows build

Package W. Requirements 3, 13, Acceptance "Windows build", "turn-based mode on Windows".

**Files:**
- Create: `src/platform/transport_none.c`
- Modify: `Makefile`, `src/platform/main.c`, `.gitignore` (if `build/` is not ignored), `docs/manual-test-checklist.md` (Prerequisites, Build and start), `CLAUDE.md` (Commands)

**Interfaces:**
- Consumes: `transport.h` (Task 6), `os.h` (Task 5), `INC_<layer>` (Task 11).

- [ ] **Step 1:** `transport_none.c` after `../dm/src/platform/transport_none.c`: `transport_is_available` returns `false`, `transport_open` `false`, the others do nothing.
- [ ] **Step 2:** `Makefile` after `../dm/Makefile:65-91`: `WINDOWS_ONLY`, `POSIX_ONLY`, `GAME_SOURCES`, `LIB_SOURCES` (without `transport_none.c`), `WIN_SOURCES`, `WIN_OBJ`, the stamp, `windows: $(B)/dm2.exe`, `-mwindows`, `SDL_MINGW_VERSION = 3.4.18`. The `$(SDL_MINGW)` rule copies `/Users/lego/github/dm/build/sdl-mingw/` (a variable `DM_DIR ?= ../dm` relative to the main checkout; in a worktree the author passes `DM_DIR=/Users/lego/github/dm`) when it exists (`cp -R`), else it stops with "copy the SDL3 MinGW package to build/sdl-mingw"; no download.
- [ ] **Step 3:** `main.c`: `#ifdef _WIN32` opens `dm2.log` in the game folder (`<data>/dm2.log`, `data` of `main.c:310`) after `read_options` and sets `g_os_log_hook` and `g_os_fatal_hook` to write a line to it; the `fprintf(stderr` messages of `main.c` (54, 58, 79, 255, 273, 283, 296, 316, 329, 357) become `os_log`.
- [ ] **Step 4:** `main.c`: when `transport_is_available()` is `false`, `--mode turnbased` and `--control` stop with `os_log("the turn-based mode and --control need macOS")` and exit code 1, after the log opened, after `read_options` and `select_mode`, and before `default_control_path` (`main.c:313`; Windows has no `HOME`).
- [ ] **Step 5:** `main.c` restart (352-358): `#ifdef _WIN32` `_putenv_s(RESTART_ENV, "1")` and `_execvp`, else the current `setenv` and `execvp`. `<unistd.h>` only in the `#else` part; `<process.h>` in the `_WIN32` part.
- [ ] **Step 6:** Verify: `make windows 2>&1 | grep -c warning` prints `0`; `file build/dm2.exe` says "PE32+ executable (GUI) x86-64"; `rm -rf build && make` prints no warning; `uv run pytest -q -n auto tests/test_control.py tests/test_turnbased.py tests/test_args.py` passes; with `SDL3.dll` from the SDL3 MinGW package copied next to `build/dm2.exe`, `"$WINE" build/dm2.exe --mode turnbased` exits with code 1 and `game/dm2.log` holds the message of step 4 (the Wine settings of the Global Constraints).
- [ ] **Step 7:** `docs/manual-test-checklist.md`: a section "W. Windows" with the checks: `SDL3.dll` next to `dm2.exe`; under Wine (`"$WINE" build/dm2.exe`, the Wine settings of the plan) the program starts a new game with `game/`, with image and sound; `--mode turnbased` stops and `game/dm2.log` holds the message. `CLAUDE.md` Commands: `make windows`.
- [ ] **Step 8:** Commit `feat: build dm2.exe for Windows with MinGW-w64`.

### Task 13: The dm2test base and the pytest settings

Package TS. Requirements 20, 22, 24.

**Files:**
- Create: `tests/dm2test/__init__.py`, `tests/dm2test/markers.py`, `tests/dm2test/lib.py`
- Modify: `tests/conftest.py`, `pyproject.toml` (`[tool.pytest.ini_options]`), every test module that uses the `lib` fixture of `conftest.py` or `from conftest import ROOT`

**Interfaces:**
- Produces:
  - `dm2test.markers`: `ROOT`, `BUILD`, `GAME`, `SAVES` (`tests/saves`), `needs_game` (skip when `game/SKULL.EXE` is missing, the rule of `tests/conftest.py:12-17`).
  - `dm2test.lib`: `LIBRARY = BUILD/"libdm2.dylib"`; `load_lib() -> ctypes.CDLL` (copy to a `mkdtemp` folder, `CDLL`, the `bool` restypes parsed from `ROOT/"src"/**/*.h` minus `third_party`, the full path hook of Task 5, `rmtree` of the folder: `../dm/tests/dmtest/lib.py:21-30`); `FULL_PATH` (the callback object, kept alive).
  - A test module defines `@pytest.fixture(scope="module") def lib(): return load_lib()` (as `dm` `tests/differential/test_recruit.py:23-24`).

- [ ] **Step 1:** Write `markers.py` and `lib.py`.
- [ ] **Step 2:** `conftest.py` keeps `pytest_configure` (Task 3), `pytest_sessionfinish` (the green tree record, `tests/conftest.py:26-34`) and the `game_dir` fixture (as `needs_game`); the `lib` fixture moves into the test modules as above; `ROOT` comes from `dm2test.markers`.
- [ ] **Step 3:** `test_input.py:266-267,292-293` use `load_lib()`.
- [ ] **Step 4:** `pyproject.toml`: `addopts = "-m 'not network' --dist loadfile --import-mode=importlib"`, `pythonpath = ["tests"]`, `testpaths = ["tests"]`, `timeout = 120`.
- [ ] **Step 5:** `pytest_sessionfinish` still records a green tree: its check `session.config.args == ["tests"]` (`tests/conftest.py:28`) matches the run of `tools/test-suite`.
- [ ] **Step 6:** Verify: `uv run pytest -q -n auto tests/test_control.py tests/test_move.py tests/test_input.py tests/test_scale.py` passes; `uv run pytest -q tests/test_le.py` passes alone.
- [ ] **Step 7:** Commit `test: add the dm2test package with the library loader`.

### Task 14: Helpers of test_move, test_save and test_creature

Package TS. Requirement 23.

**Files:**
- Create: `tests/dm2test/scenario.py` (from `tests/test_move.py`), `tests/dm2test/save.py` (from `tests/test_save.py`), `tests/dm2test/creature.py` (from `tests/test_creature.py`), `tests/dm2test/oracle.py` (`git mv tests/creature_oracle.py`)
- Modify: `tests/test_move.py`, `tests/test_save.py`, `tests/test_creature.py` and each module that imports from them or from `creature_oracle`

**Interfaces:**
- Produces: the names that other modules import today, in the new modules: `scenario`: `gdat`, `start`, `scenario`, `Scenario`, `WorldGame`, `GameRecord`, `Party`, `Command`, `COMMAND_RECORD`, `TIMELINE_ADD_EVENT`, `ENDOFLIST`, `NONE`, `WALL_END_GAME`, `G_SKY_OBJECTS`, `LIGHT_UPDATE_PARTY_LIGHT`, `SKY_OBJECTS_SIZE`, `WEATHER`, `WORLD_UPDATE_DAY_NIGHT`, `WeatherState`, `sky_list`, `square_event`, `squares`, `sensor_squares`, `approaches`, `CHAMPION_CELL`, `G_CHAMPION_COUNT`, `G_GAME_TIME`, `G_MOVE_COOLDOWN`, `G_THROW_COOLDOWN`, `GLOBALS`, `ignore_stale`, `Sound` (when it is the same class as `test_sound.Sound`, its one home is `audio` of Task 16 and `scenario` imports it from there); `ROOT` of `test_move` comes from `dm2test.markers`; `save`: `GRAPHICS`, `START`, `Game`, `Timeline`, `original_load`, `original_save`, `RECORD_GLOBALS`, `GDAT_VALUE_TYPES`, `c_round_trip`, `Event`; `creature`: `creatures`, `CreatureScenario`; `oracle`: `CreatureOracle`, `G_RANDOM_SEED`.

- [ ] **Step 1:** Move each name of the list with the code it needs; the test functions stay in their modules.
- [ ] **Step 2:** Replace every import of these names (module level and inside functions; `import test_creature as tc`, `__import__("test_move").Party` at `tests/test_input.py:199`, `import test_save` at `tests/test_ai_move.py:855` and `tests/creature_oracle.py:130`) with `from dm2test.<module> import ...`.
- [ ] **Step 3:** A fixture moved into a helper module is imported by name in each test module that uses it, so pytest finds it.
- [ ] **Step 4:** `grep -rnE '(from|import) (test_move|test_save|test_creature|creature_oracle)\b|__import__\("test_' tests` is empty.
- [ ] **Step 5:** Verify: `uv run pytest -q -n auto tests/test_move.py tests/test_save.py tests/test_creature.py tests/test_ai_move.py tests/test_world.py tests/test_projectile.py tests/test_session_play.py` passes.
- [ ] **Step 6:** Commit `test: move the scenario, save and creature helpers to dm2test`.

### Task 15: Helpers of test_interface, test_view, test_input and test_dialog

Package TS. Requirement 23.

**Files:**
- Create: `tests/dm2test/interface.py`, `view.py`, `input.py`, `dialog.py`
- Modify: `tests/test_interface.py`, `test_view.py`, `test_input.py`, `test_dialog.py` and each module that imports from them

**Interfaces:**
- Produces: `interface`: `CHAMPION_SIZE`, `G_CHAMPIONS`, `GAME_CHAMPIONS`, `GAME_LEADER_HAND`, `GAME_MAGIC`, `G_LEADER_HAND`, `G_FONT_BITMAP`, `G_UI_COLORS`, `MONEY_INIT_COIN_TABLE`, `SCREEN_W`, `SCREEN_H`, `VIEW_W`, `VIEW_H`, `VGA`, `ActionArea`, `UiState`, `champion_bytes`, `champion_records`, `sync_champions`, `set_leader_hand`, `set_magic`, `set_action`, `open_hand`, `update_action_area`, `draw_game_screen`, `c_screen`, `c_view`, `diff`, `original`, `port`, `things_of`, `G_IOP_BUFFER`, the `IOP_SCROLL_*` names, `ZONE_GET_RECT` (`Game` of `test_interface` is the `save.Game` of Task 14 when it is the same class; else it moves too); `view`: `OriginalView`, `u8_p`, `Bitmap`, `HEAP`; `input`: `session`, `run_ticks`, `Loop`, `MainLoop`, `KEY_FORWARD`, `CHAMPION_UNCOMPARED`; `dialog`: `Session`, `DialogState`, `InputMatch`, `program_start_after`, `G_DIALOG_SCREEN_MODE`. (`gdat` of `test_interface` is the `scenario.gdat` of Task 14.)

- [ ] **Step 1:** Move the names as Task 14 step 1; `test_dialog.py:155` (`import test_dialog`) uses its own names directly.
- [ ] **Step 2:** Replace the imports as Task 14 step 2.
- [ ] **Step 3:** `grep -rnE '(from|import) (test_interface|test_view|test_input|test_dialog)\b' tests` is empty.
- [ ] **Step 4:** Verify: `uv run pytest -q -n auto tests/test_interface.py tests/test_view.py tests/test_input.py tests/test_dialog.py tests/test_magic.py tests/test_champions.py tests/test_champion_rules.py tests/test_automap.py tests/test_session.py` passes.
- [ ] **Step 5:** Commit `test: move the interface, view, input and dialog helpers to dm2test`.

### Task 16: The remaining helpers

Package TS. Requirement 23.

**Files:**
- Create: `tests/dm2test/control.py`, `modes.py`, `gdat.py`, `audio.py`
- Modify: `tests/test_control.py`, `test_modes.py`, `test_gdat.py`, `test_sound.py`, `test_music.py`, `test_session_play.py`, `test_world.py`, `test_champions.py`, `test_session.py` and their importers

**Interfaces:**
- Produces: `control`: `control`, `pumped`, `GameWithChampions`, `PALETTE_COLORS`, `RGB`; `modes`: `ADVANCED`, `VANILLA`, `INPUT_MODE_INVENTORY`, `INPUT_MODE_MOVE`, `KEY_F5`, `KEY_F9`, `KEY_PAUSE`, `SCAN_W`, `advanced`, `key_command`, `press`, `set_mode`; `gdat`: `OK`, `c_int_p`, `gd`; `audio`: `UPSAMPLE`, `Sound`, `mix`, `Music`; `dialog` gets `main_loop_quit` and `find_point` (from `test_session_play`); `view` gets `draw_both` (from `test_world`, used at `tests/test_creature.py:135`); `interface` gets `LeaderHand` (from `test_champions`, used at `tests/test_session_play.py:442`); `scenario` gets `square_things` (from `test_session`, used at `tests/test_session_play.py:344`).

- [ ] **Step 1:** Move the names as Task 14 step 1.
- [ ] **Step 2:** Replace the imports as Task 14 step 2.
- [ ] **Step 3:** `grep -rnE '^\s*(from|import) (test_|conftest)' tests` is empty.
- [ ] **Step 4:** Verify: `uv run pytest -q -n auto tests/test_control.py tests/test_modes.py tests/test_gdat.py tests/test_icon.py tests/test_sound.py tests/test_music.py tests/test_session_play.py tests/test_turnbased.py tests/test_state_json.py tests/test_zone.py` passes.
- [ ] **Step 5:** Commit `test: move the control, mode, gdat and audio helpers to dm2test`.

### Task 17: The test folders

Package TS. Requirement 20.

**Files:**
- Move (`git mv`):
  - `tests/differential/`: `test_ai_move`, `test_automap`, `test_champion_rules`, `test_champions`, `test_creature`, `test_dialog`, `test_dungeon`, `test_gdat`, `test_gfx`, `test_input`, `test_interface`, `test_magic`, `test_move`, `test_music`, `test_projectile`, `test_random_timeline`, `test_save`, `test_session`, `test_session_play`, `test_sound`, `test_view`, `test_world`, `test_zone`
  - `tests/port/`: `test_args`, `test_control`, `test_icon`, `test_modes`, `test_movie`, `test_mve`, `test_pcx`, `test_platform`, `test_scale`, `test_state_json`, `test_turnbased`
  - `tests/tools/`: `test_emu`, `test_le`, `test_ghidra`, `test_symbols`, `test_play_client`, `test_scaffold`
  - `tests/standard/`: `test_standard.py`; `tests/standard_allowlist.txt` to `tests/standard/allowlist.txt`
- Modify: `tests/tools/test_ghidra.py:10`, `tests/tools/test_symbols.py:7`, `tests/standard/test_standard.py:7-9` (`ROOT` from `dm2test.markers`, the allowlist path), `CLAUDE.md` (Commands, Layout), the Global Constraints paths in the docs that name `tests/test_*.py`

- [ ] **Step 1:** Note the test count: `uv run pytest -q --collect-only | tail -1`.
- [ ] **Step 2:** The moves above. A module that the list does not name (a new module after 2026-10-21) goes by the rule of the Decisions table.
- [ ] **Step 3:** Each `Path(__file__).parent.parent` becomes `ROOT` of `dm2test.markers`.
- [ ] **Step 4:** `CLAUDE.md`: one test file `uv run pytest -q tests/<folder>/test_x.py`; Layout lists the four folders and `tests/dm2test/`.
- [ ] **Step 5:** Verify: the count of step 1 is the same; `uv run pytest -q tests/standard` passes.
- [ ] **Step 6:** Commit `test: sort the tests into the folders of dm`.

### Task 18: Remove the old test paths

Package TS. Requirement 19.

**Files:**
- Modify: `docs/*.md` that name `tests/test_*.py` paths (not `docs/superpowers/`, `docs/skull-notes/`), `tools/test-suite` (if it names a path)

- [ ] **Step 1:** `grep -rn 'tests/test_\|tests/conftest\|creature_oracle' docs/*.md CLAUDE.md tools` lists the places; give each its new path.
- [ ] **Step 2:** Verify: the grep of step 1 finds no old path.
- [ ] **Step 3:** Commit `docs: name the new test paths`.

### Task 19: The constant parser

Package TS. Requirement 21.

**Files:**
- Create: `tests/dm2test/constants.py` (port of `../dm/tests/dmtest/constants.py`), `tests/tools/test_dm2test_constants.py` (port of `../dm/tests/tools/test_dmtest_constants.py`)

**Interfaces:**
- Produces: `from dm2test import constants as C`; `C.<NAME>` for each `#define` and `enum` constant of `src/**/*.h` (headers only, as `dm` `tests/dmtest/constants.py:99`), `src/third_party/` skipped; `evaluate(expression, known)`, `parse(texts)`; an unknown name raises `AttributeError`.

- [ ] **Step 1:** Copy the test of `dm`; change the module name and the examples to `dm2` constants: `C.THING_ENDOFLIST`, `C.SLOT_READY_HAND` (`port/src/engine/champion.h:11`), `C.ZONE_VIEWPORT` (7, `port/src/ui/ui.h:12` enum) and one shifted value of `engine/dungeon.h`; keep the parser cases of `dm` (literals, other constants, casts, shifts, parentheses, an unknown name).
- [ ] **Step 2:** Run `uv run pytest -q tests/tools/test_dm2test_constants.py`; expected FAIL: no module `dm2test.constants`.
- [ ] **Step 3:** Port `constants.py` with `_SRC = parents[2]/"src"`.
- [ ] **Step 4:** Run the test of step 2; expected PASS.
- [ ] **Step 5:** Commit `test: read the C constants of the tests from the headers`.

## Wave 5

### Task 20: The clang-format settings and the format target

Package F. Requirements 1, 2, 3. The author works in the main checkout on the feature branch, not in a worktree; the commits of this package are never rebased.

**Files:**
- Create: `.clang-format`
- Modify: `Makefile` (`format` target)

- [ ] **Step 1:** `.clang-format`: the settings of `../dm/.clang-format` with `IncludeCategories` for `dm2`: priority 1 to 5 the bare header names of `src/<layer>/include/` and the `*_internal.h` of each layer in the layer order; 6 `^<(opl3|SDL3)/`; 7 `^<`. Generate the five name lists with `ls src/<layer>/include/*.h`.
- [ ] **Step 2:** `Makefile`: `C_FILES = $(wildcard src/*/*.[ch] src/*/include/*.h)` (no `third_party`); `format: ; uv run clang-format -i $(C_FILES)`; add `format` to `.PHONY`.
- [ ] **Step 3:** Commit `build: add the clang-format settings of dm`.

### Task 21: The format commit

Package F. Requirement 2.

**Files:**
- Modify: all files of `C_FILES`; create `.git-blame-ignore-revs`

- [ ] **Step 1:** `make format`; `git diff --stat` touches only `src/` outside `src/third_party/`.
- [ ] **Step 2:** `rm -rf build && make 2>&1 | grep -c warning` prints `0`; `uv run pytest -q -n auto tests/port tests/standard` passes.
- [ ] **Step 3:** Commit `style: format the C sources with clang-format` (only the format change).
- [ ] **Step 4:** `git rev-parse HEAD > .git-blame-ignore-revs`; commit `chore: skip the format commit in git blame`.
- [ ] **Step 5:** `make format && git status --porcelain src` is empty.

### Task 22: Constants of the dm2test helpers

Package K1. Requirement 21.

**Files:**
- Modify: `tests/dm2test/*.py` except `constants.py`, `lib.py`, `markers.py`

**Constant procedure** (also Tasks 23a-e):
- A literal in a test that is the value of a C constant of `src/` (same meaning, for example a thing type, a slot, a key code, a zone, a command, a size of a C array) becomes `C.<NAME>`; a local Python name that only repeats a C constant (`SLOT_READY_HAND = 1`) becomes an alias of `C.<NAME>` (`SLOT_READY_HAND = C.SLOT_READY_HAND`); in `tests/dm2test/` a name never goes away, because other modules import it.
- Struct offsets of the original (`G_LEADER_HAND`, `offsetof` comments: `tests/test_champions.py:12`, `tests/test_interface.py:19`, `tests/test_session_play.py:18`) and addresses of `SKULL.EXE` stay literals: they are not C constants of the port.
- The assertion value stays the same number; only its source changes.

- [ ] **Step 1:** Apply the constant procedure to each file.
- [ ] **Step 2:** Verify: `uv run pytest -q -n auto tests/differential/test_move.py tests/differential/test_interface.py tests/port/test_control.py tests/port/test_modes.py` passes.
- [ ] **Step 3:** Commit `test: take the C constants of the helpers from the headers`.

### Task 23: Constants of the differential tests, part 1

Package K1. Requirement 21.

**Files:**
- Modify: `test_ai_move`, `test_automap`, `test_champion_rules`, `test_champions`, `test_creature`, `test_dialog` in `tests/differential/`

- [ ] **Step 1:** Apply the constant procedure of Task 22 to each module.
- [ ] **Step 2:** Run the modules with `uv run pytest -q -n auto <modules>`; expected PASS.
- [ ] **Step 3:** Commit `test: take the C constants of the differential tests, part 1 from the headers`.

### Task 24: Constants of the differential tests, part 2

Package K1. Requirement 21.

**Files:**
- Modify: `test_dungeon`, `test_gdat`, `test_gfx`, `test_input`, `test_interface`, `test_magic` in `tests/differential/`

- [ ] **Step 1:** Apply the constant procedure of Task 22 to each module.
- [ ] **Step 2:** Run the modules with `uv run pytest -q -n auto <modules>`; expected PASS.
- [ ] **Step 3:** Commit `test: take the C constants of the differential tests, part 2 from the headers`.

### Task 25: Constants of the differential tests, part 3

Package K2. Requirement 21.

**Files:**
- Modify: `test_move`, `test_music`, `test_projectile`, `test_random_timeline`, `test_save`, `test_session` in `tests/differential/`

- [ ] **Step 1:** Apply the constant procedure of Task 22 to each module.
- [ ] **Step 2:** Run the modules with `uv run pytest -q -n auto <modules>`; expected PASS.
- [ ] **Step 3:** Commit `test: take the C constants of the differential tests, part 3 from the headers`.

### Task 26: Constants of the differential tests, part 4

Package K2. Requirement 21.

**Files:**
- Modify: `test_session_play`, `test_sound`, `test_view`, `test_world`, `test_zone` in `tests/differential/`

- [ ] **Step 1:** Apply the constant procedure of Task 22 to each module.
- [ ] **Step 2:** Run the modules with `uv run pytest -q -n auto <modules>`; expected PASS.
- [ ] **Step 3:** Commit `test: take the C constants of the differential tests, part 4 from the headers`.

### Task 27: Constants of the port and tool tests

Package K3. Requirement 21.

**Files:**
- Modify: all modules of `tests/port/` and `tests/tools/`

- [ ] **Step 1:** Apply the constant procedure of Task 22 to each module.
- [ ] **Step 2:** Run the modules with `uv run pytest -q -n auto <modules>`; expected PASS.
- [ ] **Step 3:** Commit `test: take the C constants of the port and tool tests from the headers`.

## Wave 6

### Check keys

Tasks 28, 29 and 30 use this table. A rule of `docs/coding-standard.md` names its key in brackets; the check of `tests/standard/test_standard.py` reports the same key.

| Key | dm2 scope (differences from `dm`) |
|---|---|
| `layer`, `platform`, `engine-write`, `module`, `prefix`, `header`, `include`, `comment`, `dead-api`, `hex`, `decimal`, `enum`, `block`, `notation`, `duplicate`, `numbered`, `packed`, `name`, `static-prefix`, `bool`, `global`, `decompiler`, `ordinal`, `type`, `long`, `result`, `params`, `order`, `event-comment`, `todo`, `tests`, `format`, `allowlist` | As `dm`, with `src/`, the guard `DM2_`, `dm2test`, `docs/skull-symbols.csv`. `platform` exempts `data/os.c`. `engine-write`: the extern `g_*` of `engine/include/*.h` except `g_hooks` and `*_hook`. `comment`: no `rec_` exemption. `tests`: `tests/**/*.py` imports no `test_` module and no `conftest`. |
| `byteorder` | `le16`, `le32`, `put_le16`, `put_le32`, `put_be32`, `read_u32` (`tests/test_standard.py:131`) and shifts by 8, 16, 24 for bytes only in `data/include/le.h` (replaces the old `le` check). |
| `record` | `dungeon_thing_record(` only in `engine/dungeon.c` and `engine/include/dungeon.h` (the old `record` check). |
| (none) | `kaos`, `address`: removed (rule 3.6 has no `dm2` rule). |

### Task 28: The coding standard text

Package SD. Requirement 14.

**Files:**
- Modify: `docs/coding-standard.md` (rewrite)

- [ ] **Step 1:** Copy `../dm/docs/coding-standard.md`. Apply the changes of PRD Requirement 14, one list item after the other, and the Decisions "Rule numbers", "Exception rules", "Byte order helpers", "`ids.h` groups" of this plan.
- [ ] **Step 2:** Each rule names a key of the check key table, says "review only", or names its exception. The key table at the end lists the keys of the check key table.
- [ ] **Step 3:** The rules of the current `dm2` standard are in the new text: rule 1 (names, hex notation of ids, the hook zones in the engine header), 2 (record layout in `engine/dungeon.h`, accessors per thing type), 3, 4, 5, 6 (as 3.12), 7, 8 (layers, `g_hooks`, the action area as the 1.5 exception), 9 (as 3.10), 10 (as 3.9). List the mapping in the commit body.
- [ ] **Step 4:** References to `docs/development.md` point to `CLAUDE.md` (Commands, Layout).
- [ ] **Step 5:** Commit `docs: take the coding standard of dm`.

### Task 29: The standard check

Package SC. Requirement 15.

**Files:**
- Modify: `tests/standard/test_standard.py` (rewrite from `../dm/tests/standard/test_standard.py`), `tests/standard/allowlist.txt`

**Interfaces:**
- Produces: `DM2_STANDARD_DUMP=<file>` appends each violation line `<check> <target>` to the file; `DM2_STANDARD_IGNORE_ALLOWLIST=1` ignores the allowlist (`dm` `test_standard.py:4,68-93`). `EXCEPTION_RULES` of the Decisions table.

- [ ] **Step 1:** Copy the module of `dm`; change `ROOT`, `SRC`, `LAYERS`, the guard `DM2_`, the environment names, `EXCEPTION_RULES`, and the scopes of the check key table. Remove `kaos`, `address`. Replace `record`, `byteorder` as the table says. The `allowlist` check also accepts the reason `open: module cleanup` (Task 72 removes this).
- [ ] **Step 2:** `test_include_categories_name_the_headers_of_each_layer` reads the `.clang-format` of Task 20.
- [ ] **Step 3:** `DM2_STANDARD_IGNORE_ALLOWLIST=1 DM2_STANDARD_DUMP=/tmp/dm2-std.txt uv run pytest -q tests/standard/test_standard.py` runs every check to its end (no Python error; only assertion failures).
- [ ] **Step 4:** Write `tests/standard/allowlist.txt` from the dump of step 3: the two header comment lines of `dm` (with "relative to src/"), then each dump line in sorted order, with the reason of the old line of the same `<check> <target>`, else `open: module cleanup`. The old reasons become `rule <n.m>:` reasons: `table: rule 6 (...)` becomes `rule 3.12: <the text in parentheses, or "a table of SKULL.EXE">`; `table: PC keyboard scan codes` becomes `rule 3.12: the PC keyboard scan codes`; the `record engine/save.c` line becomes `rule 3.11: the save image packs whole thing records bit by bit`.
- [ ] **Step 5:** `uv run pytest -q tests/standard` passes.
- [ ] **Step 6:** Map each old check of `tests/standard/test_standard.py` before this task (`git show HEAD:tests/standard/test_standard.py`) to a new check and list it in the commit body: `hex`→`hex`, `decimal` (10 or more)→`decimal` (stricter), `duplicate`, `record`, `decompiler`, `bool`, `long`, `prefix`, `numbered`, `packed`, `le`→`byteorder`, `layer`. A check without a new check that is at least as strict is a stop: report it (a weakened assertion).
- [ ] **Step 7:** Commit `test: take the standard checks of dm with the open violations`.

### Task 30: The allowlist script and the check target

Package SC. Requirements 3 (`check`), 15, 18.

**Files:**
- Create: `tools/standard-allowlist`
- Modify: `Makefile` (`check`), `CLAUDE.md` (Commands)

**Interfaces:**
- Produces: `tools/standard-allowlist` (bash): runs the check of step 3 of Task 29 into a temporary dump; writes `tests/standard/allowlist.txt` with the two header comment lines, then for each dump line in sorted order the old line with the same `<check> <target>` (its reason), else `<check> <target> open: module cleanup`; prints the count of `open:` lines and the lines it added.

- [ ] **Step 1:** Write `tools/standard-allowlist`.
- [ ] **Step 2:** Run it; `git diff --stat tests/standard/allowlist.txt` is empty (it gives the file of Task 29); it prints the `open:` count.
- [ ] **Step 3:** `Makefile`: `check: ; uv run ruff check tests tools/dm2tools` + `uv run pytest -q tests/standard` + `$(MAKE) test`; `CLAUDE.md` Commands: `make check`, `make format`, `tools/standard-allowlist`.
- [ ] **Step 4:** Commit `test: add the allowlist script and make check`.

### Task 31: The rename script

Package I. Requirement 19.

**Files:**
- Create: `tools/rename-symbol`

**Interfaces:**
- Produces: `tools/rename-symbol OLD NEW`: replaces the whole word `OLD` by `NEW` (`perl -pi -e 's/\bOLD\b/NEW/g'`) in the git files `src/**/*.[ch]` (not `third_party`), `tests/**/*.py`, `docs/*.md`, `docs/skull-symbols.csv`, `CLAUDE.md`; stops with exit code 1 when `NEW` already occurs as a word in `src/`; prints the changed files. `tools/rename-symbol --files FILE... OLD NEW` renames only in the named files and skips the check of `NEW` (the replay of a merge).

- [ ] **Step 1:** Write the script (bash).
- [ ] **Step 2:** Check by hand: `tools/rename-symbol rand_2bits rand_two_bits`, `make`, `git diff --stat`, then `git checkout .`.
- [ ] **Step 3:** Commit `chore: add a script for the rename of a symbol`.

### Task 32: Zones and icons in data/ids.h

Package I. Requirements 16, 17.

**Files:**
- Create: `src/data/include/ids.h` (`/* Prefixes: GFX_ ZONE_ ICON_ TEXT_ */`)
- Modify: the files that define `ZONE_` and `ICON_` names (`src/ui/include/ui.h` 70, `ui/view.c` 12, `ui/dialog.c` 10, `ui/session.c` 7, `ui/input.c` 5, `ui/zone.c` 5, `ui/include/zone.h` 5, `ui/include/dialog.h` 2, `ui/minion_map.c` 21, `engine/item.c` 14, `ui/action_area.c` 5, `ui/icon.c` 4, one each in `engine/include/{action,champion,inventory,minion}.h`, `ui/include/{input,play,text,ui,icon}.h`, `ui/ui.c`, `ui/automap.c`, `platform/main.c`: counts of 2026-10-21), their includers, `.clang-format` (category 1: `ids`)

- [ ] **Step 1:** `ids.h` gets a `ZONE_` block and an `ICON_` block: `#define` only, sorted by value, one comment line above each block, hex as the current `dm2` rule 1 says (zone and icon ids are ids of the data files). Not a zone: a name whose value is a count or a size (`ZONE_COUNT`), it stays in its module.
- [ ] **Step 2:** Two names with the same value: keep one name (the one with more uses), `tools/rename-symbol` the other to it.
- [ ] **Step 3:** Remove the old definitions; each user includes `ids.h`. The engine hook zones (current `dm2` rule 1: "a zone that a rule passes to a hook is a named constant in the engine header of that rule") move too: `ids.h` is in `data/`, which the engine can include.
- [ ] **Step 4:** Verify: `make format && make`; `uv run pytest -q -n auto tests/differential/test_interface.py tests/differential/test_view.py tests/differential/test_dialog.py tests/differential/test_zone.py tests/differential/test_automap.py tests/port/test_icon.py`; `uv run pytest -q tests/standard`; `git grep -l 'define ZONE_\|define ICON_' src` names only `src/data/include/ids.h` and the modules of the step 1 exception.
- [ ] **Step 5:** Commit `refactor: move the zone and icon numbers to data/ids.h`.

### Task 33: Texts and graphics in data/ids.h

Package I. Requirements 16, 17.

**Files:**
- Modify: `src/data/include/ids.h`; the files with string id `TEXT_` names (`engine/game_text.c` 13, `engine/include/dungeon.h` 12 minus `TEXT_DATA`, `TEXT_VISIBLE`, `engine/sensor.c` 6, `engine/ai.c` 3, `ui/include/text.h` 3, `ui/ui.c` 3, `engine/include/game_text.h` 2, `ui/include/play.h` 2, one each in `data/gdat.c`, `engine/champ_action.c`, `engine/item.c`, `engine/move.c`, `engine/recruit.c`); the graphic entry numbers of `GRAPHICS.DAT` (`*_ENTRY`, `*_IMAGE`, `*_PICTURE` names that are entry ids: `engine/include/item.h:56` `ITEM_NAME_ENTRY`, `ui/ui.c:69-70`, `engine/creature.c`, `ui/view.c`, `engine/sensor.c`, `data/gdat.c`; not the image header offsets `data/gdat.c:34-38`)

- [ ] **Step 1:** A `TEXT_` block and a `GFX_` block in `ids.h` as Task 32 step 1. A graphic entry name gets the prefix `GFX_` with `tools/rename-symbol` (`ITEM_NAME_ENTRY` → `GFX_ITEM_NAME`, keep the rest of the name).
- [ ] **Step 2:** Same values: Task 32 step 2.
- [ ] **Step 3:** Remove the old definitions; each user includes `ids.h`.
- [ ] **Step 4:** Verify: `make format && make`; `uv run pytest -q -n auto tests/differential/test_interface.py tests/differential/test_view.py tests/differential/test_creature.py tests/differential/test_gdat.py tests/differential/test_world.py`.
- [ ] **Step 5:** Commit `refactor: move the text and graphic numbers to data/ids.h`.

**After wave 6:** merge step 3 regenerated the allowlist after each merge; the controller runs `tools/standard-allowlist` once more on the merged tree, commits `test: regenerate the allowlist after wave 6` when it changed, and notes the count of `open:` lines per module in the ledger. The wave 7 tasks start from this commit.

## Wave 7

### Clean-up procedure

Each task of wave 7 cleans its modules: its `.c` files and its headers (public `include/<module>.h`, private `<module>_internal.h`). Requirement 18, PRD Acceptance "standard check".

1. List the open lines of the module: the `open: module cleanup` lines whose target is a file of the module, a `<file>:<function>` of it, or a name that the module defines (`git grep -lw <name> src` shows the defining file).
2. Remove these lines from `tests/standard/allowlist.txt`; run `DM2_STANDARD_IGNORE_ALLOWLIST=1 uv run pytest -q tests/standard -x` only when you need the full message of a check.
3. Fix each violation by its rule in `docs/coding-standard.md`. Order: `enum` (`#define` only; the values of the `enum` stay), `block`, `notation`, `hex`, `decimal`, `duplicate`, then names (`static-prefix`, `name`, `global`, `ordinal`, `bool`), then `params`, `result`, `type`, `long`, `order`, `include`, `comment`, `dead-api`, `todo`, `event-comment`, `module`, `header`.
4. A public rename: `tools/rename-symbol` and the `Renamed:` line in the commit body. A changed signature: change all callers in the same commit.
5. A violation that a rule names as an exception gets its allowlist line with `rule <n.m>: <reason>`; nothing else stays.
6. `make format && make`; the tests of the module (the **Tests** line of the task); `uv run pytest -q tests/standard`.
7. Commit `refactor: clean up <layer>/<module>` (more than one commit when the module has more than about 500 changed lines; each builds).

A long function (`long`, more than 60 lines) splits along the parts of the game task; the original order of the calls stays. A changed name of a function in `docs/skull-symbols.csv` comes with the rename.

### Wave 7 tasks

Each task follows the clean-up procedure. Author: opus medium for more than 600 lines, else sonnet low. A task with more than one module commits one module at a time. The line counts are of 2026-10-21 (`.c` and `.h`); `control/` and `platform/` are estimates after Task 6.

### Task 34: Clean up data/gdat

Package C1. Author sonnet low.

**Files:** the `.c` files and the headers of `data/gdat` (533 lines).

**Tests:** `tests/differential/test_gdat.py`, `tests/port/test_icon.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 35: Clean up data/gfx

Package C1. Author sonnet low.

**Files:** the `.c` files and the headers of `data/gfx` (359 lines).

**Tests:** `tests/differential/test_gfx.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 36: Clean up data/hmp

Package C1. Author sonnet low.

**Files:** the `.c` files and the headers of `data/hmp` (367 lines).

**Tests:** `tests/differential/test_music.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 37: Clean up data/le, data/os

Package C1. Author sonnet low.

**Files:** the `.c` files and the headers of `data/le`, `data/os` (31 lines).

**Tests:** `tests/tools/test_le.py`, `tests/port/test_control.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 38: Clean up data/mve

Package C1. Author sonnet low.

**Files:** the `.c` files and the headers of `data/mve (mve.c`, `mve_video.c)` (482 lines).

**Tests:** `tests/port/test_mve.py`, `tests/port/test_movie.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 39: Clean up data/pcx, data/ids

Package C1. Author sonnet low.

**Files:** the `.c` files and the headers of `data/pcx`, `data/ids` (84 lines).

**Tests:** `tests/port/test_pcx.py`, `tests/port/test_movie.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 40: Clean up engine/dungeon

Package C2. Author opus medium.

**Files:** the `.c` files and the headers of `engine/dungeon` (1369 lines).

**Tests:** `tests/differential/test_dungeon.py`, `tests/differential/test_move.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 41: Clean up engine/sensor

Package C2. Author opus medium.

**Files:** the `.c` files and the headers of `engine/sensor` (1822 lines).

**Tests:** `tests/differential/test_move.py`, `tests/differential/test_world.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 42: Clean up engine/timeline, engine/random

Package C2. Author sonnet low.

**Files:** the `.c` files and the headers of `engine/timeline`, `engine/random` (325 lines).

**Tests:** `tests/differential/test_random_timeline.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 43: Clean up engine/seen, engine/hooks

Package C2. Author sonnet low.

**Files:** the `.c` files and the headers of `engine/seen`, `engine/hooks` (221 lines).

**Tests:** `tests/port/test_turnbased.py`, `tests/differential/test_automap.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 44: Clean up engine/weather

Package C2. Author sonnet low.

**Files:** the `.c` files and the headers of `engine/weather` (554 lines).

**Tests:** `tests/differential/test_world.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 45: Clean up engine/ai

Package C3. Author opus medium.

**Files:** the `.c` files and the headers of `engine/ai (ai.c`, `ai_data.c)` (3173 lines).

**Tests:** `tests/differential/test_ai_move.py`, `tests/differential/test_creature.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 46: Clean up engine/ai_move

Package C3. Author opus medium.

**Files:** the `.c` files and the headers of `engine/ai_move` (2616 lines).

**Tests:** `tests/differential/test_ai_move.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 47: Clean up engine/merchant, engine/minion

Package C3. Author sonnet low.

**Files:** the `.c` files and the headers of `engine/merchant`, `engine/minion` (489 lines).

**Tests:** `tests/differential/test_creature.py`, `tests/differential/test_ai_move.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 48: Clean up engine/creature

Package C4. Author opus medium.

**Files:** the `.c` files and the headers of `engine/creature` (1673 lines).

**Tests:** `tests/differential/test_creature.py`, `tests/differential/test_projectile.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 49: Clean up engine/champion, engine/recruit

Package C4. Author opus medium.

**Files:** the `.c` files and the headers of `engine/champion`, `engine/recruit` (703 lines).

**Tests:** `tests/differential/test_champions.py`, `tests/differential/test_champion_rules.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 50: Clean up engine/champ_action

Package C4. Author opus medium.

**Files:** the `.c` files and the headers of `engine/champ_action` (935 lines).

**Tests:** `tests/differential/test_magic.py`, `tests/differential/test_champion_rules.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 51: Clean up engine/damage

Package C4. Author opus medium.

**Files:** the `.c` files and the headers of `engine/damage` (635 lines).

**Tests:** `tests/differential/test_champion_rules.py`, `tests/differential/test_projectile.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 52: Clean up engine/inventory

Package C4. Author sonnet low.

**Files:** the `.c` files and the headers of `engine/inventory` (447 lines).

**Tests:** `tests/differential/test_interface.py`, `tests/differential/test_champions.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 53: Clean up engine/move

Package C5. Author opus medium.

**Files:** the `.c` files and the headers of `engine/move` (1373 lines).

**Tests:** `tests/differential/test_move.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 54: Clean up engine/projectile

Package C5. Author opus medium.

**Files:** the `.c` files and the headers of `engine/projectile` (1101 lines).

**Tests:** `tests/differential/test_projectile.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 55: Clean up engine/save

Package C5. Author opus medium.

**Files:** the `.c` files and the headers of `engine/save` (883 lines).

**Tests:** `tests/differential/test_save.py`, `tests/differential/test_session_play.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 56: Clean up engine/sound_request

Package C5. Author sonnet low.

**Files:** the `.c` files and the headers of `engine/sound_request` (328 lines).

**Tests:** `tests/differential/test_sound.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 57: Clean up engine/item, engine/money

Package C6. Author opus medium.

**Files:** the `.c` files and the headers of `engine/item`, `engine/money` (820 lines).

**Tests:** `tests/differential/test_interface.py`, `tests/differential/test_world.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 58: Clean up engine/magic

Package C6. Author sonnet low.

**Files:** the `.c` files and the headers of `engine/magic` (561 lines).

**Tests:** `tests/differential/test_magic.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 59: Clean up engine/action, engine/aspect

Package C6. Author opus medium.

**Files:** the `.c` files and the headers of `engine/action`, `engine/aspect` (628 lines).

**Tests:** `tests/differential/test_interface.py`, `tests/differential/test_view.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 60: Clean up engine/game, engine/game_text

Package C6. Author sonnet low.

**Files:** the `.c` files and the headers of `engine/game`, `engine/game_text` (464 lines).

**Tests:** `tests/differential/test_session.py`, `tests/differential/test_dialog.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 61: Clean up ui/view

Package C7. Author opus medium.

**Files:** the `.c` files and the headers of `ui/view` (2779 lines).

**Tests:** `tests/differential/test_view.py`, `tests/differential/test_world.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 62: Clean up ui/draw, ui/video, ui/icon

Package C7. Author sonnet low.

**Files:** the `.c` files and the headers of `ui/draw`, `ui/video`, `ui/icon` (373 lines).

**Tests:** `tests/differential/test_interface.py`, `tests/port/test_icon.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 63: Clean up ui/text, ui/zone

Package C7. Author opus medium.

**Files:** the `.c` files and the headers of `ui/text`, `ui/zone` (731 lines).

**Tests:** `tests/differential/test_interface.py`, `tests/differential/test_zone.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 64: Clean up ui/minion_map, ui/automap

Package C7. Author opus medium.

**Files:** the `.c` files and the headers of `ui/minion_map`, `ui/automap` (729 lines).

**Tests:** `tests/differential/test_automap.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 65: Clean up ui/input

Package C8. Author opus medium.

**Files:** the `.c` files and the headers of `ui/input` (1852 lines).

**Tests:** `tests/differential/test_input.py`, `tests/port/test_modes.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 66: Clean up ui/ui, ui/hook_table

Package C8. Author opus medium.

**Files:** the `.c` files and the headers of `ui/ui`, `ui/hook_table` (1376 lines).

**Tests:** `tests/differential/test_interface.py`, `tests/differential/test_champions.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 67: Clean up ui/action_area

Package C8. Author opus medium.

**Files:** the `.c` files and the headers of `ui/action_area` (681 lines).

**Tests:** `tests/differential/test_interface.py`, `tests/differential/test_magic.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 68: Clean up ui/session, ui/dialog, ui/play, ui/advanced

Package C9. Author opus medium.

**Files:** the `.c` files and the headers of `ui/session`, `ui/dialog`, `ui/play`, `ui/advanced` (1537 lines).

**Tests:** `tests/differential/test_session.py`, `tests/differential/test_session_play.py`, `tests/differential/test_dialog.py`, `tests/port/test_modes.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 69: Clean up ui/movie, ui/music, ui/fm, ui/sound

Package C9. Author opus medium.

**Files:** the `.c` files and the headers of `ui/movie`, `ui/music`, `ui/fm`, `ui/sound` (1029 lines).

**Tests:** `tests/port/test_movie.py`, `tests/differential/test_music.py`, `tests/differential/test_sound.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 70: Clean up control/control, control/state_json, control/turn

Package C10. Author opus medium.

**Files:** the `.c` files and the headers of `control/control`, `control/state_json`, `control/turn` (2900 lines).

**Tests:** `tests/port/test_control.py`, `tests/port/test_state_json.py`, `tests/port/test_turnbased.py`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

### Task 71: Clean up platform/main, platform/platform, platform/scale, platform/transport, platform/transport_none

Package C10. Author opus medium.

**Files:** the `.c` files and the headers of `platform/main`, `platform/platform`, `platform/scale`, `platform/transport`, `platform/transport_none` (700 lines).

**Tests:** `tests/port/test_scale.py`, `tests/port/test_args.py`, `tests/port/test_control.py`, `make windows`.

- [ ] **Step 1:** The clean-up procedure, steps 1-7.

The module check (`module`) of `engine/damage.c`, `engine/recruit.c`, `ui/hook_table.c` (no own header) is part of their task; `platform/main.c` keeps its `rule 1.7` line.

## Wave 8

### Task 72: Final checks

Package Z. PRD Acceptance, Requirement 8.

**Files:**
- Modify: `CLAUDE.md`, `docs/manual-test-checklist.md`, `tests/standard/test_standard.py` (the `allowlist` check), `docs/coding-standard.md` (only if a check key changed in wave 7)

- [ ] **Step 1:** `grep -c 'open: module cleanup' tests/standard/allowlist.txt` prints `0`; the `allowlist` check of `tests/standard/test_standard.py` no longer accepts `open: module cleanup`; each line names a rule of `EXCEPTION_RULES`.
- [ ] **Step 2:** `rm -rf build && make 2>&1 | grep -c warning` prints `0`; `make windows 2>&1 | grep -c warning` prints `0`; `make format && git status --porcelain src` is empty.
- [ ] **Step 3:** `make check` passes (ruff, `tests/standard`, the full suite).
- [ ] **Step 4:** `uv run dm2-play --help` exits with 0; `uv run dm2-le` prints its usage line.
- [ ] **Step 5:** `CLAUDE.md`: Commands (`make`, `make lib`, `make windows`, `make test`, `make check`, `make format`, `tools/standard-allowlist`, `tools/rename-symbol`, one test file `uv run pytest -q tests/<folder>/test_x.py`, full suite `make test`), Layout (`src/` five layers with `include/`, `src/third_party/`, `tools/dm2tools/`, the test folders, `tests/dm2test/`).
- [ ] **Step 6:** `docs/manual-test-checklist.md`: a dated section "Smoke test after the dm style refactor" with the checks of "Build and start", A, C, J, L, T and W.
- [ ] **Step 7:** Commit `docs: the commands and the layout after the dm style refactor`.
