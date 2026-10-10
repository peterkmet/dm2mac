# Turn-based mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task (the run of the agentic-workflow skill). Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `build/dm2 --mode turnbased` lets a program or an LLM play Dungeon Master II turn by turn over the control socket, with the JSON of the state, the view, and the map, the `dm2-play` client with `--brief` and the call log, and the run structure of the `dm` port (player manual, agents, `playthroughs/`).

**Architecture:** A third `game_mode` value. In this mode `main.c` runs a turn loop: the game time stands still until a turn request arrives; a turn gives its inputs to the input queue of `ui/input.c` and runs one game step per frame until the party can act again. Three platform modules carry the mode: `platform/state_json.c` (a growing JSON writer and the state, view, and map), `platform/turn.c` (actions, turns, batches, waits, stops, the seen record), and `platform/control.c` (socket, request parsing, dispatch, save, load, new game). Small accessors in `engine/` and `ui/` expose what the JSON and the turns read.

**Tech Stack:** C11, clang, `port/Makefile` (globbed sources), SDL3 only in `platform/main.c` and `platform/platform.c`, Python 3.14 with uv, pytest with ctypes on `build/libdm2.dylib`.

**Spec:** `docs/superpowers/2026-10-10-turnbased-mode.md`

**Source of the design:** the turn-based mode of the `dm` port in `../dm`: `port/src/platform/control.c`, `port/src/platform/state_json.c`, `tools/dmtools/play.py`, `docs/llm-mode.md`, `docs/llm-playbook.md`, `.claude/agents/dm-coordinator.md`, `.claude/agents/dm-player.md`, `playthroughs/template/`. Read them where a task names them. Do not copy DM rules that DM2 does not have.

## Global Constraints

- The game rules, the save format, and the screen do not change (PRD Invariant 1). A turn gives its input only through the input queue of `ui/input.c` (`input_key_down`, `input_mouse_down`, `input_mouse_up`, `input_mouse_click`, `input_simulate_command_click`).
- The `vanilla` and `advanced` modes keep their behavior and their control commands (Invariant 2). Every existing test passes unchanged, except `tests/test_control.py::test_too_long_reply_is_one_error_line`, which Task 4 replaces because PRD Requirement 22 removes the error `reply too long`. The controller stops for this change (weakened assertion); the user approves it once.
- The turn-based mode has the AMAP block and the seen squares, no advanced keys, and no cheats (Invariant 3).
- The JSON shows only what a player sees, plus the explored map: no creature health, no hidden square, nothing behind a wall that blocks sight (Invariant 4).
- No SDL outside `platform/main.c` and `platform/platform.c`. `state_json.c`, `turn.c`, and `control.c` are in `build/libdm2.dylib` (the Makefile globs `src/platform/*.c` except `main.c` and `platform.c`).
- `docs/coding-standard.md` is the coding standard; follow it for every change in `port/src/`. Rule 7: each new header has a `Prefixes` line; a prefix belongs to one header; no prefix starts with the prefix of another header. Rule 8: a file includes only headers of its own layer and the layers before it (`data`, `engine`, `ui`, `platform`). Rule 1: every number other than 0, 1, -1, 2 is a named constant with one definition in the header of its owner; a constant that `platform/` needs from a `.c` file of `ui/` moves to the `ui/` header.
- `rm -f build/dm2 build/libdm2.dylib && make -C port 2>&1 | grep -c warning` prints `0`.
- A function has at most 60 lines (coding standard rule 3, `tests/test_standard.py`). The JSON parts get one `static` helper per section; the turn-based dispatch is its own function `handle_turnbased_request` in `control.c`; the action table of `turn.c` has one function per action.
- A failed request changes nothing in the game (Invariant 7). One tick of the turn protocol is one game step of `game.game_time` (Invariant 8).
- Keep existing comments. New comments: one line, what the code does in game terms.
- A rename updates `docs/skull-symbols.csv` and the docs in the same commit. Every deviation of the port goes into `docs/skull-formats.md`.
- The author runs the build and only the tests of the changed code, never the full suite: one module `uv run pytest -q tests/test_x.py`; several modules `uv run pytest -q -n auto tests/test_x.py tests/test_y.py`. Always also `uv run pytest -q tests/test_standard.py`. Never `-p no:xdist`.
- Each test module loads its own library copy (the `lib` fixture of `tests/conftest.py`) and resets the global state it reads (`game_mode` back to vanilla, the hook pointers back). Socket paths: `tempfile.mkdtemp(prefix="dm2c", dir="/tmp")` (sun_path holds about 100 bytes). Tests pass in any order and in parallel.
- Do not download or run third-party code (libraries, CDN files, packages outside the project's lock file) to check behavior; report what you could not verify.
- Commit at the end of each task: Conventional Commits, one type per commit (`feat:`, `refactor:`, `test:`, `docs:`).
- All docs in English, ASD-STE100 style, as `docs/coding-standard.md` and the PRD are written.

Test list (CLAUDE.md §2). Write a test only for: a check against a reference (original program, golden snapshot), a parser or data format, a rule with more than one outcome that no reference check or type check covers (dates, parsing, prices, state machines), a regression of a real bug (one test per bug). No tests for: constants and data tables; wiring, scaffold, CLI arguments, logging; what the compiler, a linter or a standard check catches; UI and layout (the user tests them manually); what another test already covers. No mutation tests.

In this plan the tests cover: the JSON formats (Tasks 5, 6, 10), the request parser of `act_many` (Task 8), the turn state machine with its stops and end rules (Tasks 7, 8, 9), the result rules of `load` (Tasks 1, 9: missing, damaged, blocked, lost), the message rows (Task 3), and the `--brief` format of `dm2-play` (Task 10). The acceptance scenarios of the PRD map to these tests; the rest goes to the manual test checklist (Task 11).

## Decisions of this plan

These follow from the PRD and the code; the plan reviewer and the user can reject one.

| Topic | Decision | Reason |
|---|---|---|
| Turn constants | `TURN_MIN_TICKS` 2, `TURN_MAX_TICKS` 64, `TURN_WAIT_MAX_TICKS` 5000 (the real-time `wait` of the other modes keeps `WAIT_MAX_TICKS` 6000), `FRAMES_PER_TICK_LIMIT` 16, as in `dm`. | Measured in the code: a step forward with a light load takes 2 game steps until the party can move again, a heavy load about 4, a turn 0; the longest command wait (`in.wait` after the death of a boss) is 160 timer ticks = 20 game steps. 64 covers them. |
| One game step per frame | A turn frame calls `play_tick` until `game.game_time` changes, at most `STEP_TICKS` times. | `play_tick` is one timer tick; a game step takes `main_loop.tick_threshold` ticks (8, or 1 while the party sleeps). A dead party or the automap stop the time; the cap ends the frame. |
| Inputs of an action | An action gives a list of at most 4 inputs (key, click, press, release, command). The turn gives the next input when the input pipeline is idle (`input_is_idle`). | DM2 runs a click through a click feedback script (up to 6 timer ticks later), and a mode change flushes the queue. Choose an action (hand, then menu item), swap champions (two drag clicks), sleep (open the inventory, then sleep), and look (press, release) need their inputs one after the other. |
| Positions of clicks | A command with a zone in the current input mode uses `input_simulate_command_click`. Viewport clicks, the spell icon (caster), the leader choice, and the eye use computed screen points. | `input_simulate_command_click` clicks the top left pixel of the zone. A viewport click needs the point of an object or a wall. The spell icon means "leader" or "caster" by the point (`input_refine_champion_click`). The eye needs a held button. |
| Action damage | A new engine variable `champ_action_result` keeps `action.result` after `champ_perform_action`; `action_damage` is its value when it is not negative. | `area_update` clears `action.result` in the same command. |
| `look` text | A new `ui_object_info_lines` builds the text lines that the eye panel shows for an object, without drawing. | No function returns the eye panel text. |
| Message rows | `ui/text.c` keeps the text of each message row next to its pixels. | The message area has only pixels and `message_expiry`. |
| Visible squares | `engine/seen.c` gets `seen_visible`, the squares that the seen rule gives now; `seen_mark` uses it. | One rule for the view, the map, the news, and the seen record. |
| Names | Rune names, slot names, and explosion names: named `const` tables in `state_json.c`. Creature types, wall and floor ornaments: the index (the tables start empty; GDAT has no names for them). | The PRD decision "a named const table; an entry without a name gives the index". |
| Creature count | `count` is 1 for each creature group. | A DM2 group record holds one creature. |
| Outdoor | `outdoor` is `game.weather_visible`. | It is 1 exactly for the sky states that show the sky. |
| Raw input during a candidate or sleep | `act click` and `act key` run also while a candidate shows or the party sleeps. | They are the raw way for screens without a named action. |
| `save` with a candidate | Refused: `answer the candidate first (act recruit)`. | The quick save of the advanced mode also refuses it (`input.c` `CMD_QUICK_SAVE`). |
| `save` with no champion | Allowed. | The start of a run is `new_game`, then `save`, before any recruit (PRD Decisions). |
| The game save button | `act click` on it opens the save menu of the game; the playbook tells the player not to use it. | A raw click is a player click; the turn-based save is `save`. |

## PRD changes confirmed

The user confirmed two PRD changes on 2026-10-10; the PRD has them (Requirements 19 and 21, Decisions "Merchants" and "Floor marks in `get_map`"):

1. DM2 has no merchant panel. `get_state` gives the open dialog (id and button texts) only; merchants show in `get_view` as creatures with their type; the scenario "raw click as a turn" uses the volume dialog.
2. `get_map` marks with `_` a floor square with a floor ornament that the automap draws (the rule of `draw_floor`, `ui/minion_map.c:163-173`: an ornament that is not detail-only); no glyph for a floor sensor without an ornament.

## Review Focus

1. A client that closes during a turn or a batch: expected the turn to run to its end, no reply, the batch freed, and the next client served normally. Task 7 owns it; `test_client_that_leaves_during_a_turn_frees_the_turn` pins it.
2. A turn whose input opens a dialog of the game (a modal loop): expected the turn to end after `TURN_MAX_TICKS * FRAMES_PER_TICK_LIMIT` frames with the dialog still open, `state.mode` `dialog`, `save`/`load`/`new_game` refused, and `act click` on a button to close it. Task 9 owns it; `test_turn_in_a_dialog_ends_and_the_dialog_stays` pins it.
3. A move while the party cannot move yet (walk cooldown, `in.wait`): expected the command to wait in the queue and run in the same turn; the turn does not end before the input ran. Task 7 owns it; `test_move_during_cooldown_runs_in_the_same_turn` pins it.
4. `load` and `new_game` after a game over: expected `main_loop.end` and `session.completed` cleared, `game_over` false, turns work again. Task 9 owns it; `test_load_after_game_over_plays_again` pins it.
5. A damaged `QUICK.DAT` whose restore from `QUICK.TMP` also fails: expected the program to stay, every command except `load` and `new_game` to reply `no game is running: use load or new_game`, and a later `load` of a valid file or `new_game` to play. Task 9 owns it; `test_lost_game_allows_only_load_and_new_game` pins it.

---

### Task 1: The turn-based mode value, the save folder, and the quick save results

**Files:**
- Modify: `port/src/engine/game.h:68-73` (enum, comment), `port/src/engine/game.c:26` (names), `port/src/engine/game.h:52-65` (`Session.save_dir`)
- Modify: `port/src/ui/play.c:142` (`seen_mark` condition), `port/src/ui/input.c:491` (advanced keys), `port/src/ui/advanced.c:40` (letter keys)
- Modify: `port/src/ui/advanced.h`, `port/src/ui/advanced.c` (`quick_path`, `advanced_quick_save`, `advanced_quick_load` results)
- Modify: `port/src/ui/input.c` (the `CMD_QUICK_SAVE`/`CMD_QUICK_LOAD` call sites, if the return value changes their code)
- Modify: `port/src/ui/session.c:631-648` (`args_drop_save` knows `--saves`)
- Modify: `port/src/ui/session.c` (`session_complete` in the turn-based mode)
- Modify: `port/src/platform/main.c:154-183` (`--saves`, the unknown-mode message, `session.save_dir` default)
- Modify: `tests/test_dialog.py:34-38` (the `Session` mirror gets `save_dir`), and every other ctypes mirror of `Session` (`grep -n "data_dir" tests/*.py`)
- Test: `tests/test_modes.py`

**Interfaces:**
- Produces: `GAME_MODE_TURNBASED` (value 2), `game_mode_names[2] == "turnbased"`; `Session.save_dir[SAVE_PATH_SIZE]` (after `data_dir`); `enum { QUICK_LOADED, QUICK_MISSING, QUICK_DAMAGED, QUICK_BLOCKED, QUICK_LOST }`; `int advanced_quick_save(void)` (1 saved, 0 error); `int advanced_quick_load(void)` (a `QUICK_*` value).

- [ ] **Step 1: Add the mode**

In `engine/game.h` replace the game mode comment and enum:

```c
/* The game modes: vanilla is the original game; advanced adds the W A S D Q E keys, the quick save (F5) and quick
   load (F9), the automap (Tab or M), and its cheats; turnbased plays turn by turn over the control socket, with the
   seen squares and the AMAP block of advanced, without its keys and cheats. Without the cheats the game rules are the
   same in all modes. */
enum { GAME_MODE_VANILLA, GAME_MODE_ADVANCED, GAME_MODE_TURNBASED, GAME_MODE_COUNT };
```

In `engine/game.c:26`: `const char *const game_mode_names[GAME_MODE_COUNT] = {"vanilla", "advanced", "turnbased"};`

- [ ] **Step 2: Change the mode checks**

- `ui/play.c:142`: `if (game_mode != GAME_MODE_VANILLA) seen_mark(...)`.
- `engine/save.c:810` already tests `!= GAME_MODE_VANILLA`: no change.
- `ui/input.c:491`: `if (command || game_mode != GAME_MODE_ADVANCED) return command;` and update its comment ("In the advanced mode, ...": no change of text needed).
- `ui/advanced.c:40`: `if (game_mode != GAME_MODE_ADVANCED) return 0;` and its header comment in `advanced.h` ("0 for other keys and outside the advanced mode").

- [ ] **Step 3: Add the save folder**

In `engine/game.h` `Session`, after `data_dir`:

```c
    char save_dir[SAVE_PATH_SIZE];  /* the folder of QUICK.DAT (--saves); empty: <data_dir>/DATA */
```

In `ui/advanced.c` replace `quick_path`:

```c
/* <save folder>/QUICK.<extension>: the --saves folder, else the DATA folder of the game. */
static char *quick_path(char *path, const char *extension)
{
    if (session.save_dir[0]) snprintf(path, SAVE_PATH_SIZE, "%s/QUICK.%s", session.save_dir, extension);
    else snprintf(path, SAVE_PATH_SIZE, "%s/DATA/QUICK.%s", session.data_dir, extension);
    return path;
}
```

Update the comments of `advanced_quick_save` and `advanced_quick_load` in `advanced.h` (`<data>/DATA/QUICK.DAT` becomes "QUICK.DAT in the save folder").

In `platform/main.c`: `read_options` gets `const char **saves` and the option `--saves`; `main` copies it: `if (saves) snprintf(session.save_dir, sizeof session.save_dir, "%s", saves);`. In `ui/session.c` `args_drop_save`, add `|| !strcmp(argv[i], "--saves")` to `is_value_option`, and add `--saves` to its comment in `session.h`.

- [ ] **Step 4: Quick save and quick load results**

In `ui/advanced.h`:

```c
/* The results of a quick load. */
enum { QUICK_LOADED, QUICK_MISSING, QUICK_DAMAGED, QUICK_BLOCKED, QUICK_LOST };
/* Writes the game to QUICK.DAT in the save folder and prints the result in the message area. The saved-game state of
   the session (the QUIT question, RESTART) does not change. Returns 1 when the file was written. */
int advanced_quick_save(void);
/* Loads QUICK.DAT of the save folder without a dialog and prints the result. A missing or damaged file keeps the game;
   when the running game cannot come back either (QUICK_LOST), the program ends outside the turn-based mode. */
int advanced_quick_load(void);
```

In `ui/advanced.c`, `advanced_quick_save` returns `ok`. `advanced_quick_load` returns `QUICK_MISSING` where it prints `NO QUICK SAVE`, `QUICK_BLOCKED` where the running game cannot go to `QUICK.TMP` (`UNABLE TO SAVE`: nothing loaded, the game goes on), and at the end:

```c
    if (!restored) {
        /* the dungeon is empty: the game cannot go on */
        fprintf(stderr, "cannot load %s or the running game in %s\n", path, keep);
        if (game_mode != GAME_MODE_TURNBASED) {
            session_quit();
            session.exit_code = EXIT_GAME_LOST;
        }
        return QUICK_LOST;
    }
    ...
    print_line(loaded ? "GAME LOADED" : "QUICK SAVE DAMAGED");
    return loaded ? QUICK_LOADED : QUICK_DAMAGED;
```

A lost game has no running game to keep: a `static int lost` in `advanced.c` is set when the result is `QUICK_LOST`; while it is set, `advanced_quick_load` does not write `QUICK.TMP` and does not try the restore (a failed load gives `QUICK_LOST` again); a loaded game and `advanced_reset` (a new or loaded session) clear it. The callers in `ui/input.c` ignore the result.

- [ ] **Step 5: Game completed in the turn-based mode**

In `ui/session.c`:

```c
void session_complete(void)
{
    session.completed = 1;
    if (game_mode != GAME_MODE_TURNBASED) session_quit();  /* the turn-based mode stays for the next load or new game */
}
```

Update its comment in `session.h`: "The party finished the game: the screen goes black and the program ends; the turn-based mode only sets session.completed."

- [ ] **Step 6: The unknown-mode message**

`platform/main.c`: `fprintf(stderr, "unknown mode %s: use vanilla, advanced, or turnbased\n", mode);`. Update `tests/test_modes.py::test_bad_mode_stops_the_program` if it checks the full text (it checks `"unknown mode turbo"`; keep the assertion as it is if it still matches). Update `docs/manual-test-checklist.md` L12 to the new text.

- [ ] **Step 7: Mirrors of `Session`**

Find each ctypes mirror of `Session` (`grep -n "data_dir" tests/*.py`) and add `("save_dir", ctypes.c_char * SAVE_PATH_SIZE)` after `data_dir`.

- [ ] **Step 8: Write the tests**

In `tests/test_modes.py` (use its `advanced` fixture pattern; it sets `session.data_dir` to `tmp_path`):

```python
TURNBASED = 2


def test_quick_save_goes_to_the_save_folder(lib, advanced, tmp_path):
    """--saves: QUICK.DAT goes to the save folder, not to <data>/DATA."""
    saves = tmp_path / "saves"
    saves.mkdir()
    session = Session.in_dll(lib, "session")  # the mirror of tests/test_dialog.py, as test_modes.py uses it
    session.save_dir = str(saves).encode()
    try:
        assert lib.advanced_quick_save() == 1
        assert (saves / "QUICK.DAT").exists() and not (tmp_path / "DATA" / "QUICK.DAT").exists()
    finally:
        session.save_dir = b""


@pytest.mark.parametrize("case, result", [("missing", 1), ("damaged", 2), ("blocked", 3)])
def test_quick_load_results(lib, advanced, tmp_path, case, result):
    """No QUICK.DAT gives QUICK_MISSING, a damaged one QUICK_DAMAGED, an unwritable QUICK.TMP (a folder of that name)
    QUICK_BLOCKED; the game time stays in all three."""
    ...  # write a damaged QUICK.DAT for "damaged" as test_failed_restore_... does; compare game.game_time before and after


def test_lost_game_stays_in_the_turnbased_mode(lib, advanced, tmp_path):
    """A damaged quick save whose restore also fails: QUICK_LOST; the turn-based mode does not quit."""
    ...  # the setup of test_failed_restore_ends_the_game_without_game_steps, with game_mode = TURNBASED;
         # assert result == 4, main_loop.quit == 0, session.exit_code == 0
```

Write the bodies from the setup of `test_failed_restore_ends_the_game_without_game_steps` in the same module. Each test that sets `game_mode` puts it back (the `advanced` fixture does it at teardown; check).

- [ ] **Step 9: Build and run**

Run: `rm -f build/dm2 build/libdm2.dylib && make -C port 2>&1 | grep -c warning` → `0`.
Run: `uv run pytest -q -n auto tests/test_modes.py tests/test_dialog.py tests/test_args.py tests/test_session_play.py tests/test_standard.py` → all pass.

- [ ] **Step 10: Commit**

```bash
git add port/src tests docs/manual-test-checklist.md
git commit -m "feat: turnbased game mode, --saves folder, and quick load results"
```

---

### Task 2: A new game without the menu

**Files:**
- Modify: `port/src/ui/session.c` (new `session_start_new_game`), `port/src/ui/session.h`
- Modify: `port/src/platform/main.c` (the turn-based start, the default socket)

**Interfaces:**
- Consumes: `GAME_MODE_TURNBASED` (Task 1).
- Produces: `int session_start_new_game(void)` — 1 when the new game runs, 0 on a read error (the old game, if any, is gone: the caller treats 0 as "no game").

- [ ] **Step 1: Write `session_start_new_game`**

In `ui/session.c`, after `session_init`. It does what `session_load` does for a new game, then the tail of `session_init`, without the menu and the dialogs:

```c
int session_start_new_game(void)
{
    session.new_game = 1;
    session.use_dungenb = 0;
    if (!load_new_game()) return 0;
    sensor_start_timers();
    main_loop.end = 0;
    session.completed = 0;
    move_compute_map_arrival(game.party_x, game.party_y, game.party_map);
    party.redraw = REDRAW_ALL_VIEW;
    sound_set_volume_and_test(game.sound_volume, DEVICE_SOUND);
    sound_set_volume_and_test(game.music_volume, DEVICE_MUSIC);
    ui_reset();
    money_init_coin_table();
    play_start_session();
    input_flush();
    return 1;
}
```

Check against `session_load` (`session.c:362-401`) and `session_init` (`session.c:457-471`) that no step is missing (for example `dialog.progress_enabled`); `play_start_session` with `session.new_game` set clears the screen without a dialog. In `session.h`:

```c
/* Starts a new game from DUNGEON.DAT at once, as the menu choice NEW GAME does, without the menu. Returns 0 when the
   dungeon does not load. */
int session_start_new_game(void);
```

- [ ] **Step 2: Start the turn-based mode in `main.c`**

In `main`, before `plat_init`:
- In the turn-based mode, `control` defaults to `$HOME/.dm2/control.sock` (build it in a `static char` buffer of `PATH_SIZE`; without `HOME`, print `HOME is not set: use --control PATH` and return `EXIT_USAGE`).
- Replace `if (!save && !session_init()) main_loop.quit = 1;` by:

```c
    if (!save && game_mode == GAME_MODE_TURNBASED && !session_start_new_game()) {
        fprintf(stderr, "cannot start a new game from %s/DATA/DUNGEON.DAT\n", data);
        return 1;
    }
    if (!save && game_mode != GAME_MODE_TURNBASED && !session_init()) main_loop.quit = 1;
```

The turn loop itself comes in Task 7; until then the turn-based mode runs the normal loop.

- [ ] **Step 3: Build and check by hand**

Run: `make -C port 2>&1 | grep -c warning` → `0`.
Run: `SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy build/dm2 --mode turnbased --control /tmp/dm2tb.sock & sleep 3; uv run dm2-play --socket /tmp/dm2tb.sock state; uv run dm2-play --socket /tmp/dm2tb.sock quit`
Expected: `state` shows `"mode":"move"`, `"champion_count":0`, `"game_mode":"turnbased"`. (No test: this is wiring; the manual checklist M1 covers the start.)

- [ ] **Step 4: Commit**

```bash
git add port/src
git commit -m "feat: turnbased mode starts a new game without the menu"
```

---

### Task 3: Engine and interface accessors for the JSON and the turns

**Files:**
- Modify: `port/src/engine/seen.h`, `port/src/engine/seen.c` (`seen_visible`; the enum `VIEW_DEPTHS`, `VIEW_HALF_WIDTH`, `VIEW_WIDTH` of `seen.c:10` moves to `seen.h`)
- Modify: `port/src/engine/magic.h`, `port/src/engine/magic.c` (`magic_rune_mana_cost`)
- Modify: `port/src/engine/champ_action.c:785-807` and the header that declares `champ_execute_selected_action` (`champ_action_result`)
- Modify: `port/src/ui/input.h`, `port/src/ui/input.c` (`input_is_waiting`, `input_is_idle`), `port/src/ui/ui.h` (`ZONE_DROP_CELL_FIRST` and the front wall zones)
- Modify: `port/src/ui/text.h`, `port/src/ui/text.c` (`text_message_row`)
- Modify: `port/src/ui/ui.h`, `port/src/ui/ui.c` (`ui_object_info_lines`)
- Test: `tests/test_automap.py` (or the module that tests `seen_mark`; find it with `grep -ln seen_mark tests/*.py`), `tests/test_interface.py` (message rows)

**Interfaces:**
- Produces:
  - `typedef struct { int16_t depth, side, x, y; } SeenSquare;` and `int seen_visible(int map, int x, int y, int dir, SeenSquare *out, int max);` — the squares of the seen rule now, in the order of the seen rule (depth 0 to 4; in each row side 0, then +1, -1, +2, -2, ...), `side` negative to the left; returns the count. `#define SEEN_VISIBLE_MAX` (`VIEW_DEPTHS * VIEW_WIDTH`).
  - `uint16_t magic_rune_mana_cost(int champ, int rune)` — the mana that the rune (0 to 5) of the next rune set costs the champion now, as `magic_add_rune` takes it.
  - `extern int16_t champ_action_result;` — `action.result` after the last `champ_perform_action` (damage >= 0, failure < 0); `champ_execute_selected_action` sets it, nothing else clears it.
  - `bool input_is_waiting(void)` — `in.wait` is not 0. `bool input_is_idle(void)` — no pending key, event, deferred click, queued command, running click feedback script, and `!input_feedback_busy`.
  - `ZONE_DROP_CELL_FIRST` (0x2f8) and the front wall zones (`ZONE_FRONT_WALL_LOW`/`HIGH`, today private in `input.c:1147-1149`) in `ui/ui.h`, next to the other `ZONE_*` constants.
  - `#define MESSAGE_TEXT_SIZE` and `const char *text_message_row(int row)` — the text of message row 0 (top) to `MESSAGE_ROWS - 1`, "" when the row is clear.
  - `#define OBJECT_INFO_LINES 8`, `#define OBJECT_INFO_SIZE 64`, `int ui_object_info_lines(uint16_t thing, char lines[OBJECT_INFO_LINES][OBJECT_INFO_SIZE])` — the lines of the eye panel for an object: name, weight line, and each gauge or content line as text (`draw_object_info`, `ui.c:778`); returns the count.

- [ ] **Step 1: `seen_visible`**

Move the loop of `seen_mark` (`seen.c:86-106`) into `seen_visible`; it writes each visible square into `out` instead of `*square = 1`. `seen_mark` becomes:

```c
void seen_mark(int map, int x, int y, int dir)
{
    SeenSquare squares[SEEN_VISIBLE_MAX];
    int count = seen_visible(map, x, y, dir, squares, SEEN_VISIBLE_MAX);
    for (int i = 0; i < count; i++) {
        uint8_t *square = seen_square(map, squares[i].x, squares[i].y);
        if (square) *square = 1;
    }
}
```

A square off the map is in the list (its `blocks_sight` is 1); `seen_square` returns NULL for it. The existing `seen_mark` tests prove the same result.

- [ ] **Step 2: `magic_rune_mana_cost`**

Extract the cost formula of `magic_add_rune` (`magic.c:450-469`: `rune_mana[set*6+i]`, times `power_rune_mana_factor[runes[0]-0x60] >> 3` when the set is not 0) into `magic_rune_mana_cost`; `magic_add_rune` calls it. Same result, no test (the existing magic tests cover `magic_add_rune`).

- [ ] **Step 3: `champ_action_result`**

In `champ_execute_selected_action` after `done = champ_perform_action(champ, index);` add `champ_action_result = action.result;`. Define `int16_t champ_action_result;` in `champ_action.c` with the one-line comment of the interface and declare it in the header of `champ_execute_selected_action`.

- [ ] **Step 4: `input_is_waiting` and `input_is_idle`**

In `ui/input.c`:

```c
bool input_is_waiting(void) { return in.wait != 0; }

bool input_is_idle(void)
{
    return !in.key_count && !in.event_count && !in.deferred && in.queue_head == in.queue_tail && !in.script
           && !in.feedback_waiting && !input_feedback_busy;
}
```

Move `ZONE_DROP_CELL_FIRST` (`input.c:1149`) and the front wall zone constants (`input.c:1147-1148`) to `ui/ui.h` (Rule 1: layout zone numbers live in `ui.h`).

- [ ] **Step 5: Message row texts**

In `ui/text.c` keep `static char message_text[MESSAGE_ROWS][MESSAGE_TEXT_SIZE]` next to the pixels:
- `text_message_draw_line` appends the line to the row of the cursor (the pending row while `scroll_pending`);
- `text_message_new_line` moves the rows up when the area scrolls, the same way as `message_expiry`;
- `text_clear_expired_rows` clears the text of each row it clears;
- `text_init` clears all rows.

`text_message_row(row)` returns the row. Add `text_message_` to the `Prefixes` line of `text.h` (check that no other prefix starts with it).

Test in `tests/test_interface.py` (a rule with more than one outcome: rows, scroll, expiry):

```python
def test_message_rows_follow_print_scroll_and_expiry(lib, ...):
    """Four printed lines: the first scrolls out; each row clears at its expiry time."""
    # print "\nONE", "\nTWO", "\nTHREE", "\nFOUR" with text_print_message; read text_message_row(0..2)
    # == ["TWO", "THREE", "FOUR"]; set game.game_time past MESSAGE_SHOW_TIME; text_clear_expired_rows();
    # all rows == ""
```

Use the setup of the existing message tests in that module (find them with `grep -n text_print_message tests/*.py`).

- [ ] **Step 6: `ui_object_info_lines`**

In `ui/ui.c`, write the text side of `draw_object_info` (`ui.c:778`) and its gauges (`draw_object_gauges`): line 0 is `item_name(thing)`; then the weight line (`TEXT_WEIGHT` with `text_args`), each gauge as `<label> <value>/<max>` with the labels that the panel draws (charges, strength or throw power, armour class, potion power), a scroll's text (`text_decode_dungeon(..., SCROLL_TEXT_MODE)`), a chest's content names, and a money box's coin counts. Do not change `draw_object_info`. If a label exists only as a picture, use a short English word and record it in `docs/skull-formats.md` under the turn-based deviations. No test (UI text; the manual checklist covers `look`).

- [ ] **Step 7: Build and run**

Run: `make -C port 2>&1 | grep -c warning` → `0`.
Run: `uv run pytest -q -n auto tests/test_automap.py tests/test_interface.py tests/test_magic.py tests/test_input.py tests/test_standard.py` → pass.

- [ ] **Step 8: Commit**

```bash
git add port/src tests docs
git commit -m "feat: accessors for visible squares, rune cost, action result, input state, message rows, object info"
```

---

### Task 4: The growing JSON writer and replies without a size limit

**Files:**
- Create: `port/src/platform/state_json.h`, `port/src/platform/state_json.c` (the writer part)
- Modify: `port/src/platform/control.c` (the reply buffer, `put`, `put_string`, `send_reply`), `port/src/platform/control.h` (request accessors)
- Modify: `tests/test_control.py:345-356` (replace `test_too_long_reply_is_one_error_line`)
- Modify: `docs/llm-control.md` (remove the error `reply too long`)

**Interfaces:**
- Produces in `state_json.h` (`/* Prefixes: json_ state_json_ */`):

```c
/* A growing JSON text. Start with {0}; free s when done. */
typedef struct {
    char *s;
    size_t len, cap;
} Json;
void json_raw(Json *j, const char *text);
void json_rawn(Json *j, const char *text, size_t n);
void json_key(Json *j, const char *key);
void json_int(Json *j, long value);
void json_bool(Json *j, int value);
void json_null(Json *j);
void json_str(Json *j, const char *text);
void json_strn(Json *j, const char *text, size_t n);   /* the first n bytes, up to an end mark */
void json_open(Json *j, char bracket);
void json_close(Json *j, char bracket);
```

- Produces in `control.h`: `const char *control_find_value(const char *line, const char *key)`, `int control_get_string(const char *line, const char *key, char *out, size_t size)`, `long control_get_int(const char *line, const char *key, long missing)` — the existing static `find_value`, `get_string`, `get_int`, renamed and exported for `turn.c`.

- [ ] **Step 1: Write the writer**

Copy the writer of `../dm/port/src/platform/state_json.c:53-119` (`put`, `separate`, `json_*`) with these changes: the start size is a named constant `JSON_START_SIZE`; `json_str` and `json_strn` escape `"`, `\`, and bytes below 0x20 as `\u00XX` (with `\n` as `\\n`), and pass bytes from 0x80 as they are (UTF-8, as `control.c` `put_string` does today); `abort()` on an allocation failure.

- [ ] **Step 2: Move `control.c` to the writer**

Replace `static char reply[REPLY_SIZE]`, `reply_length`, `reply_overflow`, and `put` with one `static Json reply;`. `put(format, ...)` stays as a `vsnprintf` into a local buffer of `REPLY_LINE_SIZE` then `json_raw` (it writes only short pieces), or call `json_*` directly; `put_string(text, n)` becomes `json_strn(&reply, text, n)`. `send_reply` sends `reply.s` plus the newline and sets `reply.len = 0` (keep the buffer). Remove `REPLY_SIZE`, `REPLY_RESERVE`, `reply_overflow`, and the `reply too long` branch. Rename `find_value`, `get_string`, `get_int` to the `control_` names and declare them in `control.h` with their existing comments.

Note: `put` in `control.c` adds no commas; keep the hand-written JSON of `send_state` as it is (it writes `"key":value` pieces with `put`). Do not mix `json_key` and `put` in one object.

- [ ] **Step 3: Replace the test**

In `tests/test_control.py`, replace `test_too_long_reply_is_one_error_line` (PRD Requirement 22 removes the error):

```python
def test_a_long_reply_arrives_whole(lib):
    """The JSON writer grows past 4096 bytes and keeps valid JSON (no reply size limit)."""
    class Json(ctypes.Structure):
        _fields_ = [("s", ctypes.c_void_p), ("len", ctypes.c_size_t), ("cap", ctypes.c_size_t)]
    j = Json()
    lib.json_open(ctypes.byref(j), ord("["))
    for _ in range(1000):
        lib.json_str(ctypes.byref(j), "caf\u00e9 \"q\"\n".encode())
    lib.json_close(ctypes.byref(j), ord("]"))
    text = ctypes.string_at(j.s, j.len).decode()
    assert len(text) > 4096 and json.loads(text) == ["caf\u00e9 \"q\"\n"] * 1000
    ctypes.CDLL(None).free(ctypes.c_void_p(j.s))
```

- [ ] **Step 4: Build and run**

Run: `make -C port 2>&1 | grep -c warning` → `0`.
Run: `uv run pytest -q -n auto tests/test_control.py tests/test_standard.py` → pass.

- [ ] **Step 5: Docs and commit**

Remove the error `reply too long` from `docs/llm-control.md` (the Errors section).

```bash
git add port/src tests docs/llm-control.md
git commit -m "feat: growing JSON writer; control replies without a size limit"
```

---

### Task 5: get_state as JSON

**Files:**
- Modify: `port/src/platform/state_json.h`, `port/src/platform/state_json.c`
- Test: `tests/test_state_json.py` (new)

**Interfaces:**
- Consumes: the writer (Task 4); `champ_action_result`, `magic_rune_mana_cost`, `text_message_row`, `input_is_waiting` (Task 3); `GAME_MODE_TURNBASED` (Task 1).
- Produces: `void state_json_state(Json *j)`; `enum { STATE_JSON_STATE, STATE_JSON_VIEW, STATE_JSON_MAP };` `char *state_json_string(int part)` (a new string for the tests; free it with `free`, exported through the library; the tests call `libc.free` on it); `bool state_json_game_over(void)` — `main_loop.end || session.completed`; `const char *state_json_mode_name(int mode)` — the input mode names of `control.c` (move `mode_names` here; `control.c` `send_state` uses it).

The structure follows `../dm/port/src/platform/state_json.c:373-424` (`state_json_state`) with the DM2 sources below. Field names and units are the contract of the playbook (Task 12): write them exactly.

- [ ] **Step 1: Write the test first**

`tests/test_state_json.py`, with a module fixture that loads `tests/saves/start.DAT` as `tests/test_input.py::session` does, sets `game_mode` to 2, and resets it to 0 at teardown:

```python
import json, ctypes

STATE, VIEW, MAP = 0, 1, 2


def part(lib, which):
    lib.state_json_string.restype = ctypes.c_void_p
    p = lib.state_json_string(which)
    text = ctypes.string_at(p).decode()
    ctypes.CDLL(None).free(ctypes.c_void_p(p))
    return json.loads(text)


def test_state_has_the_fields_of_the_playbook(lib, game):
    state = part(lib, STATE)
    assert set(state) >= {"time", "game_mode", "mode", "dialog", "party", "leader", "leader_hand", "candidate",
                          "champions", "action_menu", "spell", "inventory", "money_box", "minion_map", "messages",
                          "outdoor"}
    assert state["game_mode"] == "turnbased" and state["mode"] == "move"
    assert set(state["party"]) == {"map", "level", "x", "y", "facing", "darkness", "can_move", "sleeping", "dead"}


def test_state_matches_the_game_record(lib, game):
    """time, party square, and each champion's bars come from the game record of start.DAT."""
    state = part(lib, STATE)
    # compare with the ctypes mirrors of test_move.WorldGame / test_control.GameWithChampions:
    # time == game.game_time, party x/y/map, facing name of game.party_dir, len(champions) == champion_count,
    # health == [health, max_health], stamina == [stamina // 10, max_stamina // 10]


def test_hand_menus_do_not_change_the_open_menu(lib, game):
    """Listing the action menus of both hands for the state keeps the open menu of the action area."""
    # open a hand menu (action_area_open_hand via its command as tests/test_interface.py does), read champ_actions
    # and champ_action_count, call state_json_string(STATE), and compare again


def test_outdoor_fields_only_outside(lib, game):
    """weather and day_time are present only while outdoor is true (outside.DAT)."""
```

Run: `uv run pytest -q tests/test_state_json.py` → FAIL (no `state_json_string`).

- [ ] **Step 2: Write `state_json_state`**

The object, in this order:
- `time`: `game.game_time`.
- `game_mode`: `game_mode_names[game_mode]`. `mode`: `state_json_mode_name(input_get_mode())`. `dialog`: `null`, or while the mode is `dialog`: `{"id": dialog.id, "buttons": [...]}` with the non-empty texts `text_gdat(CAT_DIALOG, id, e, buf)` for e = 0 to 19 (`dialog.c:127-137`); the slot dialogs (they do not set `dialog.id`) give `{"id": null, "buttons": []}` — detect them by `session` slot dialog state if it exists, else accept the id as it is and record it in `docs/skull-formats.md`.
- `party`: `map`, `level` (`dungeon_map_level`), `x`, `y` (`game.party_x/y`), `facing` (`north`/`east`/`south`/`west` of `game.party_dir`), `darkness` (`view_light_level`, 0 bright to 5), `can_move` (`!game.party_move_cooldown && !party.move_steps && !input_is_waiting() && (mode == move || mode == push)`), `sleeping` (`party_sleeping`), `dead` (`main_loop.end != 0`).
- `leader`: `null` when `(int16_t)game.leader == -1` or it is the candidate; else the index. `leader_hand`: an object (below) or `null`.
- `candidate`: when `ui.candidate`: the champion object of `ui.candidate - 1` (the last record), else `null`. `champions`: the members (all records except a shown candidate).
- A champion: `index`, `name`, `title` (`%.*s` of the fixed arrays), `alive`, `health`/`stamina`/`mana` as `[current, max]` (stamina divided by the display unit 10: split the private `TENTHS` of `ui.c:26` into `STAMINA_DISPLAY_UNIT` and `WEIGHT_TENTHS` in `ui.h` and use each at its places in `ui.c`), `food`, `water`, `load_kg` `[load, max]` as decimal numbers with one decimal (`champ_get_load`, `champ_get_max_load`, in `WEIGHT_TENTHS`), `position` (`front_left`, `front_right`, `back_right`, `back_left` of `dir_relative(c->cell, game.party_dir)`), `poisoned`, `statistics` (`strength`, `dexterity`, `wisdom`, `vitality`, `antimagic`, `antifire`, each `[current, max]` of `champ_get_stat`, as the panel shows; luck is not shown), `skills` (`fighter`, `ninja`, `priest`, `wizard`: `{"level", "rank"}`, rank from the panel texts as `ui.c:103` builds them, `null` below level 2; map the rune font glyphs of the master ranks to plain text and record the mapping in `docs/skull-formats.md`), `wounds` (names of the set bits: `ready_hand`, `action_hand`, `head`, `torso`, `legs`, `feet`), `slots` (each filled slot: `{"slot": n, "where": name, "object": ...}`; slot names: a `const` table of 30 names from `champion.h:10-13,115-125`: `ready_hand`, `action_hand`, `head`, `torso`, `legs`, `feet`, `quiver_1` to `quiver_4` (6-9), `neck` (10), `quiver_5` (11), `quiver_6` (12), `pouch_1`, `pouch_2`, `backpack_1` to `backpack_15` — check the real order of 11-29 in `champion.h` and `item.c:43-45` and name them after the inventory picture), `hands` (`{"ready": [names], "action": [names]}`: `champ_list_actions` for each hand, then list the open menu again when `action.mode == ACTION_MODE_MENU`, as `champ_action.c:217-218` does), `runes` (the chosen rune names), `busy` (`[ready, action, spell]` from `action_delay`, true when not 0).
- An object: `{"name": item_name(thing), "kind": weapon|armour|scroll|potion|container|misc}`; a held scroll also `text` (`text_decode_dungeon` with the scroll mode).
- `action_menu`: while `action.mode == ACTION_MODE_MENU`: `{"champion": action.champion - 1, "hand": "ready"|"action", "actions": [names of champ_actions]}`, else `null`.
- `spell`: while the spell panel is open: `{"caster", "chosen": [rune names], "available": [{"rune": 0-5, "name", "mana": magic_rune_mana_cost(caster, i)}]}`, else `null`. Rune names: a `const` table of the 24 runes in four sets (`lo um on ee pal mon`, `ya vi oh ful des zo`, `ven ew kath ir bro gor`, `ku ros dain neta ra sar`), indexed by `glyph - 0x60` (name the base glyph constant).
- `inventory`: while `ui.inventory`: `{"champion": ui.inventory - 1, "panel": name of ui.panel_mode}`, else `null`. `chest`: while `action.mode == ACTION_MODE_CONTAINER`: `[{"slot": 30 + i, "object"}]` of `action.open_items`, else `null`.
- `money_box`: while `action.mode == ACTION_MODE_MONEY_BOX`: `[{"slot": 0-5, "coin": name, "count": n}]` for each slot with a coin type (`money_coin_type_for_slot`, `money_count_coins_in_box`, the name from `money_coin_item` as `ui/action_area.c:395-399` does), else `null`.
- `minion_map`: while `action.mode == ACTION_MODE_MINION_MAP`: `{"powered": bool, "options": [{"option": 0-3, "name": creatures|details|items|hold, "on": bool}]}` (empty list without `MAP_HAS_OPTIONS`), else `null`.
- `messages`: the non-empty `text_message_row` rows, top first.
- `outdoor`: `game.weather_visible != 0`; only when true: `weather` `{"clouds", "rain", "wind"}` from the weather fields with the thresholds of `weather.c:45-51` as words (name each threshold in `weather.h` if it is private), and `day_time` (`day`, `dawn_dusk`, `night` from `weather.hour_light`).

Prefixes: every function of `state_json.c` that is not `static` starts with `json_` or `state_json_`.

- [ ] **Step 3: Run the tests**

Run: `uv run pytest -q tests/test_state_json.py` → PASS.
Run: `make -C port 2>&1 | grep -c warning` → `0`; `uv run pytest -q tests/test_standard.py` → PASS.

- [ ] **Step 4: Commit**

```bash
git add port/src tests/test_state_json.py docs/skull-formats.md
git commit -m "feat: the game state as JSON for the turn-based mode"
```

---

### Task 6: get_view and get_map as JSON

**Files:**
- Modify: `port/src/platform/state_json.h`, `port/src/platform/state_json.c`, `port/src/ui/minion_map.h`, `port/src/ui/minion_map.c` (`mmap_shows_floor_ornament`)
- Test: `tests/test_state_json.py`

**Interfaces:**
- Consumes: `seen_visible`, `SeenSquare` (Task 3); the writer and the state (Tasks 4, 5).
- Produces:
  - `void state_json_view(Json *j)`, `void state_json_map(Json *j)`.
  - `typedef struct { int16_t depth, side, x, y; uint16_t thing; } StateJsonSeenThing;` `int state_json_visible_things(uint16_t type, StateJsonSeenThing *out, int max);` — the things of a thing type on the visible non-wall squares, in view order.
  - `const char *state_json_side_name(int side)` — `"center"` for 0, `"left"`/`"right"` for ±1, `"left_2"`, `"right_2"`, `"left_3"`, `"right_3"`.
  - `#define STATE_JSON_VISIBLE_GROUPS_MAX` (the size of the visible square list of `seen_visible`) and `int state_json_visible_groups(uint16_t groups[STATE_JSON_VISIBLE_GROUPS_MAX])` — the creature group things on the visible squares.
  - `void state_json_explosion_name(Json *j, uint16_t type)` and `void state_json_creature_name(Json *j, uint16_t type)` — each appends a name string, or the number while the table has no name.

- [ ] **Step 1: Write the tests first**

```python
def test_view_shows_only_the_seen_squares(lib, game):
    """The view squares are exactly the squares of the seen rule from the party square (seen_visible)."""
    view = part(lib, VIEW)
    # call seen_visible through ctypes with an array of SeenSquare; compare {(depth, side, x, y)}


def test_view_square_fields(lib, game):
    view = part(lib, VIEW)
    assert set(view) >= {"facing", "darkness", "squares", "piles", "wall_ahead_clickable"}
    for s in view["squares"]:
        assert 0 <= s["depth"] <= 4 and -3 <= s["side"] <= 3
        assert s["element"] in {"wall", "floor", "pit", "stairs_up", "stairs_down", "door", "teleporter"}


def test_map_rows_show_only_seen_squares(lib, game):
    """get_map: a square that seen_is_seen does not know and that is not in view is a space; @ is the party."""
    m = part(lib, MAP)
    # for each (x, y): char != " " exactly when seen_is_seen(map, x, y) or (x, y) in the visible squares


def test_map_glyphs(lib, game):
    """Each glyph follows the square type: # wall, . floor, D closed door, d open door, < > stairs, O open pit,
    T visible teleporter, _ a floor with a floor ornament that the automap draws; a closed fake wall is #, an open one
    is ."""
```

Run: `uv run pytest -q tests/test_state_json.py` → the new tests FAIL.

- [ ] **Step 2: Write `state_json_view`**

Follow `../dm/port/src/platform/state_json.c:426-723` with the DM2 sources:
- The squares: `seen_visible(game.party_map, view x, view y, view dir)`; during a walk (`party.move_steps`) the view stands at `party.view_x/view_y/view_dir` (`move.h:31`); use them then. Each square: `depth`, `side` (a number -3 to 3, as the PRD says; `state_json_side_name` is only for the seen record and `dm2-play`), `x`, `y`, `element`.
- `element` from the square byte (`dungeon.h:15-45`): wall; floor; `pit` only when `SQUARE_PIT_OPEN`, else floor; `stairs_up`/`stairs_down` by `STAIRS_UP`; `door`; `teleporter` only when on and visible, else floor; a fake wall is wall until `FAKE_WALL_OPEN`, then floor. Off the map: wall.
- A door square: `door` (`open`, `partly_open`, `closed`, `broken` from `DOOR_STATE`), `button` (`door_has_button`).
- A non-wall square: `creatures` `{"type": name or index, "count": 1}` for each group (an array: DM2 squares can hold more than one group record; check `dungeon_next_thing`), `objects` `[{"cell": relative cell name, "name"}]`, `flying` (projectiles with the item name or `spell`; explosions with `type` and `name`), `floor_ornament` from depth 1 (the index of `aspect_get`'s floor ornament; no names).
- A wall square: the faces that the view draws (`wall_faces_json` rule of dm: the front face of the center lane from depth 1, the lane side of a side wall from depth 1, the front of a side wall from depth 2). Each face: `facing`, `text` (a visible text thing, `text_decode_dungeon`), `mirror` (`KIND_PORTRAIT`), `switch` (wall click sensor), `lock` (object sensor; the ornament index), `alcove` (`KIND_ALCOVE`), `fountain` (`ORNAMENT_KEY_TOUCH`), `shop` (`KIND_SHOP`, with the shown item name when the display sensor gives one, read-only: never call `shop_draw_display`), `ornament` (the index). Use `aspect_get(&s, map, dir, x, y)` (`aspect.c:296`) for the faces; it computes without drawing. Move the private ornament kinds (`KIND_ALCOVE`, `KIND_SHOP`, `KIND_PORTRAIT`, `view.c:63`) and the wall sensor types that you need to their owner headers.
- `piles`: for each view click zone 0 to 3 (`view_click_zones`, kind `CLICK_OBJECT`) with a thing: `{"cell": k, "top": name, "objects": [names, top first]}`: the objects of that square and absolute cell (the zone's square and cell: read `view.c:1140-1146`). `alcove`: the objects of zone 8 when it is an alcove, same form.
- `wall_ahead_clickable`: a front click zone exists (kind `CLICK_WALL_ORNAMENT` or `CLICK_DOOR_BUTTON` at the front position, or zone 8).

The view is stale while the inventory is open (`play.c:136` draws no view then): `get_view` uses the dungeon data, not the drawn view, so it is correct also then, except `piles` and `wall_ahead_clickable`, which read the click zones of the last drawn view. Give `piles: []` and `wall_ahead_clickable: false` while `ui.inventory` is open.

- [ ] **Step 3: Write `state_json_map`**

As `../dm/port/src/platform/state_json.c:749-819` with: `seen_is_seen` or in view now (the view squares are marked at the next game step); glyphs `#`, `.`, `D`, `d`, `<`, `>`, `O`, `T`, `@`, space, and `_` for a floor square with a floor ornament that the automap draws: export the rule of `draw_floor` (`ui/minion_map.c:163-173`: ornament not `ORNAMENT_NONE` and not `detail_only`) as `bool mmap_shows_floor_ornament(int ornament)` in `ui/minion_map.h`, and read the ornament with `aspect_get`; the legend says `_ floor ornament (for example a pressure pad)`; `rows`, `grid`, `map`, `level`, `width`, `height`, `party`, `facing`. `MAP_SIDE_MAX` is 32, so the grid digits fit.

- [ ] **Step 4: `state_json_visible_things`, names**

`state_json_visible_things` walks the visible non-wall squares of Step 2. Explosion names: a `const` table from `projectile.h` (`fireball`, `slime`, `lightning`, `harm_non_material`, `door_toggle`, `poison_bolt`, `poison_cloud`, `push`, `pull`, `door_set`, `missile_wall`, `death_cloud`, `storm_lightning`, `storm_blast`, `smoke` at their enum values); other values give the number. Creature names: an empty table with its comment ("no names in the game data; add a name here when a player names it") — `state_json_creature_name` gives the index.

- [ ] **Step 5: Run the tests**

Run: `uv run pytest -q tests/test_state_json.py` → PASS. `make -C port 2>&1 | grep -c warning` → `0`. `uv run pytest -q tests/test_standard.py` → PASS.

- [ ] **Step 6: Commit**

```bash
git add port/src tests/test_state_json.py docs/skull-formats.md
git commit -m "feat: the view and the explored map as JSON"
```

---

### Task 7: Turns: the turn loop, act with movement, wait, stops, game over

**Files:**
- Create: `port/src/platform/turn.h`, `port/src/platform/turn.c`
- Modify: `port/src/platform/control.c` (dispatch in the turn-based mode), `port/src/platform/control.h` (`control_frame`, `control_turn_pending`)
- Modify: `port/src/platform/main.c` (the turn loop, the modal frame, the window input)
- Test: `tests/test_turnbased.py` (new)

**Interfaces:**
- Consumes: Tasks 1-6.
- Produces in `turn.h` (`/* Prefixes: turn_ */`):

```c
/* Turns of the turn-based mode: an action gives its inputs to the input queue, the game runs one game step per frame
   until the party can act again; a batch runs a list of actions and a wait runs game steps, both stop on news. */
bool turn_is_pending(void);
/* Each starts a turn, or returns the reason why it cannot (the game does not change then). */
const char *turn_act(const char *request);
const char *turn_wait(const char *request);
/* One frame of the pending turn: gives the next input when the input is idle, records what the party sees, and checks
   the end. Returns true when the reply is ready (turn_reply writes it). */
bool turn_poll(void);
/* The reply of the finished turn or batch: ticks, stops, game_over, damage, look, seen, state, view. */
void turn_reply(Json *j);
/* The client left: the turn runs on to its end without a reply. */
void turn_drop_reply(void);
```

- Produces in `control.h`: `bool control_turn_pending(void)`; `void control_frame(void)` — one frame of the turn-based main loop: the requests, and one game step while a turn runs.

- [ ] **Step 1: Write the tests first**

`tests/test_turnbased.py`. Fixture `tb`: the `control` setup of `tests/test_control.py` (start.DAT, palette, socket under `/tmp`), plus `game_mode = 2`; teardown puts `game_mode = 0`. A pump thread calls `lib.control_frame()` in a loop (no sleep needed while a turn runs; `time.sleep(0.0005)` between frames). Requests with `dm2tools.control_client.request(payload, path, timeout=30)`.

```python
def test_act_move_forward_moves_one_square(tb, lib):
    """A step on an open square: the turn runs until the party can move again; the reply has ticks, game_over, state,
    view; the party stands one square further."""
    # start.DAT: find a facing with an open floor square ahead from get_view (turn first with act turn_right)
    r = request({"cmd": "act", "action": "move_forward"}, path)
    assert r["ok"] and r["ticks"] >= 2 and r["game_over"] is False and "state" in r and "view" in r
    assert (r["state"]["party"]["x"], r["state"]["party"]["y"]) == ahead and r["state"]["party"]["can_move"]


def test_bump_keeps_the_square(tb, lib):
    """move_forward into a wall: "bumped": true and the square stays."""


def test_turn_does_not_tick_between_turns(tb, lib):
    """Between turns the game time stands still: two get_state 0.2 s apart have the same time."""


def test_wait_stops_on_damage(tb, lib):
    """wait 5000: a champion loses health (set its health lower from the test thread between frames is not a player
    path; instead place a projectile or set poison as the damage tests do) -> stopped damage, ticks < 5000."""


def test_wait_runs_its_ticks_without_news(tb, lib):
    """wait 50 in a quiet place: ticks == 50, stopped done."""


def test_direct_commands_are_refused(tb, lib):
    for cmd in ({"cmd": "key", "key": "up"}, {"cmd": "click", "x": 1, "y": 1}, {"cmd": "press", "x": 1, "y": 1},
                {"cmd": "release", "x": 1, "y": 1}, {"cmd": "command", "id": 3}, {"cmd": "move", "x": 1, "y": 1}):
        assert request(cmd, path) == {"ok": False, "error": "use act in turnbased mode"}
    # the party square stays


def test_game_over_refuses_turns(tb, lib):
    """Set main_loop.end = 1 (the party died): act, act_many, wait, save reply "game over"; get_state works and
    state.party.dead is true."""


def test_move_during_cooldown_runs_in_the_same_turn(tb, lib):
    """Set game.party_move_cooldown to 3 before act move_forward: the step happens in the same turn."""


def test_client_that_leaves_during_a_turn_frees_the_turn(tb, lib):
    """A client sends wait 2000 and closes: the turn ends without a reply; a new client gets get_state."""

```

Write the bodies; for damage use a path that the existing damage tests use (`grep -n "health" tests/test_champion_rules.py`), set from the test thread only while no turn runs (between requests), for example a poison amount on a champion that hurts within the wait.

Run: `uv run pytest -q tests/test_turnbased.py` → FAIL.

- [ ] **Step 2: Write `turn.c` — state, inputs, and the end rule**

```c
#define TURN_MIN_TICKS 2              /* an action shows its result before the turn ends */
#define TURN_MAX_TICKS 64
#define TURN_WAIT_MAX_TICKS 5000        /* the turn wait; the real-time wait keeps WAIT_MAX_TICKS 6000 of control.c */
#define FRAMES_PER_TICK_LIMIT 16      /* a game that does not tick (a dialog) ends a turn after this many frames per tick */
#define TURN_INPUT_MAX 4
#define ACTION_NAME_SIZE 32

typedef enum { INPUT_KEY_PRESS, INPUT_CLICK, INPUT_PRESS, INPUT_RELEASE, INPUT_COMMAND } TurnInputKind;
typedef struct {
    TurnInputKind kind;
    uint16_t value;                   /* a key code, a command, or a mouse button bit */
    int16_t x, y;
} TurnInput;

static struct {
    bool pending, wait_only, reply_dropped;
    uint32_t start_time;
    long frames, min_ticks, max_ticks;
    TurnInput inputs[TURN_INPUT_MAX];
    int input_count, inputs_given;
    bool moving;                      /* the action was a step: forward and right in squares */
    int16_t step_forward, step_right, rotation;
    int16_t map, x, y, dir;           /* the party before the action */
    bool has_damage;
    const char *stop;                 /* wait: the news that ended it early */
} turn;
```

Helpers: `add_key(code)`, `add_command(cmd)`, `add_click(x, y)`, `add_press(x, y)`, `add_release(x, y)`. `give_input(const TurnInput *in)` calls `input_key_down`, `input_simulate_command_click`, `input_mouse_down(x, y, INPUT_BUTTON_LEFT)` + `input_mouse_up(...)` for a click (and sets `input_mouse_x/y`, as `control.c` `handle_button` does), `input_mouse_down` alone for a press, `input_mouse_up` alone for a release.

`turn_poll`:

```c
bool turn_poll(void)
{
    if (!turn.pending) return false;
    if (turn.inputs_given < turn.input_count && input_is_idle()) give_input(&turn.inputs[turn.inputs_given++]);
    witness();
    if (!turn_finished()) {
        turn.frames++;
        return false;
    }
    turn.pending = false;
    return !turn.reply_dropped;
}
```

`turn_finished` (the PRD rule 5 with the DM2 counters):

```c
static bool turn_finished(void)
{
    long ticks = (long)(game.game_time - turn.start_time);
    if (turn.wait_only && (turn.stop = news())) return true;
    if (state_json_game_over()) return true;
    if (ticks >= turn.max_ticks || turn.frames > turn.max_ticks * FRAMES_PER_TICK_LIMIT) return true;
    return !turn.wait_only && ticks >= turn.min_ticks && turn.inputs_given == turn.input_count && input_is_idle()
           && !game.party_move_cooldown && !party.move_steps && !input_is_waiting();
}
```

- [ ] **Step 3: Write the movement actions, bump, displacement**

```c
static const char *step(uint16_t key, int16_t forward, int16_t right)
{
    if (ui.inventory) return "close the inventory first (close_inventory)";
    turn.moving = true;
    turn.step_forward = forward, turn.step_right = right;
    add_key(key);
    return NULL;
}
static const char *move_forward(const char *r) { (void)r; return step(KEYPAD_FORWARD, 1, 0); }
static const char *move_back(const char *r) { (void)r; return step(KEYPAD_BACK, -1, 0); }
static const char *move_left(const char *r) { (void)r; return step(KEYPAD_LEFT, 0, -1); }
static const char *move_right(const char *r) { (void)r; return step(KEYPAD_RIGHT, 0, 1); }
```

`turn_left`/`turn_right`: `KEYPAD_TURN_LEFT`/`RIGHT` with `turn.rotation` -1/1, same inventory check. `turn_bumped`: `turn.moving` and the map, x, y did not change. `turn_displaced`: as dm `turn_displaced` (`../dm/port/src/platform/control.c:279`) with `dun_move_relative(turn.dir, forward, right, &x, &y)` and `dir_turn`; DM2 has no teleport counter, so a teleporter that sends the party back to its square counts as a bump (record this in `docs/skull-formats.md`).

`run_action(name, request)`: the table lookup of dm (`run_action`), with the checks of PRD Requirement 18: a candidate on the screen allows only `recruit`, `click`, `key`; a sleeping party allows only `wake`, `click`, `key`. It resets `turn.moving`, `rotation`, `has_damage`, the look text, and the inputs; it saves the party square and direction.

- [ ] **Step 4: Write `wait`, news, the seen record, and the reply**

Copy from `../dm/port/src/platform/control.c`: `witness`, `start_witnessing`, `seen_place_json`, `seen_json`, `health_lost_json`, `latest_message_expiry` (with `message_expiry[]` and `MESSAGE_NEVER`), `snapshot`, `new_group_in_view`, `news`, `send_turn_result` (as `turn_reply`), `handle_wait` (as `turn_wait`, ticks 1 to `TURN_WAIT_MAX_TICKS`, error `ticks must be 1-5000`). Changes: the copied code uses the Task 6 forms: `StateJsonSeenThing` has `depth`, `side` (a number), `x`, `y`, `thing`; `state_json_creature_name(j, type)` appends the name itself (replace `json_str(j, state_json_creature_name(...))`); `state_json_visible_groups` as in dm. `g_champions[i].current_health` → `champion(i)->health`; creature count → 1 per group, a killed group is one that left the view and whose thing is no longer on the map (`dungeon_next_thing` rule of dm → check the DM2 way a group record is freed); `seen_place_json` writes `side` with `state_json_side_name`; the reply `ticks` counts `game.game_time`. `action_damage` comes in Task 8.

- [ ] **Step 5: Dispatch in `control.c`**

A new `handle_turnbased_request(const char *request)`, which `handle_request` calls in the turn-based mode; it holds:
- `key`, `click`, `press`, `release`, `command`, `move` → `send_error("use act in turnbased mode")`.
- `wait` → `turn_wait`.
- `act` → `turn_act`. (Task 8 adds `act_many`.)
- `get_state`, `get_view`, `get_map` → `{"ok":true,"state"|"view"|"map": ...}` through a new static helper `send_part(const char *key, void (*part)(Json *))` (as dm `control.c:825`); Task 9 uses it too.
- After a game over (`state_json_game_over()`): `act`, `act_many`, `wait`, `save` → `game over`.
- `state`, `screenshot`, `quit` stay. `save`, `load`, `new_game` come in Task 9.

`control_poll` in the turn-based mode: when `turn_is_pending()`: if the client is gone, `turn_drop_reply()`; if `turn_poll()` returns true, write `turn_reply` into the reply and send it. Read new requests only while no turn is pending. The real-time `waiting` of the other modes stays as it is.

```c
void control_frame(void)
{
    control_poll();
    if (!turn_is_pending()) return;
    uint32_t time = game.game_time;
    for (int i = 0; i < STEP_TICKS && game.game_time == time && !main_loop.end && !main_loop.quit; i++) play_tick();
}

bool control_turn_pending(void) { return turn_is_pending(); }
```

- [ ] **Step 6: The turn loop in `main.c`**

```c
/* The window in the turn-based mode: only the quit event; keys, clicks, and the pointer do not reach the game. */
static void pump_quit_events(void)
{
    PlatEvent e;
    while (plat_poll(&e))
        if (e.type == PLAT_QUIT) main_loop.quit = 1;
}

/* One frame of a modal loop (menu, dialog) in the turn-based mode: the requests and the screen; no wait while a turn
   runs. */
static void turnbased_modal_frame(void)
{
    pump_quit_events();
    control_poll();
    present_with_cursor();
    if (!control_turn_pending())
        for (int i = 0; i < PIT_TICKS_PER_GAME_TICK; i++) plat_wait_tick();
}

/* The main loop of the turn-based mode: the game runs only during a turn, one game step per frame. */
static void run_turnbased(void)
{
    play_frame_hook = turnbased_modal_frame;
    while (!main_loop.quit) {
        pump_quit_events();
        control_frame();
        present_with_cursor();
        if (!control_turn_pending())
            for (int i = 0; i < PIT_TICKS_PER_GAME_TICK; i++) plat_wait_tick();
    }
}
```

In `main`, after the start: `if (game_mode == GAME_MODE_TURNBASED) run_turnbased(); else { play_frame_hook = modal_frame; ...existing loop... }`. The turn-based mode never calls `session_end`.

- [ ] **Step 7: Run the tests**

Run: `make -C port 2>&1 | grep -c warning` → `0`.
Run: `uv run pytest -q -n auto tests/test_turnbased.py tests/test_control.py tests/test_standard.py` → PASS.

- [ ] **Step 8: Commit**

```bash
git add port/src tests/test_turnbased.py docs/skull-formats.md
git commit -m "feat: turns with movement, wait, stops, and game over"
```

---

### Task 8: act_many and the actions of DM2

**Files:**
- Modify: `port/src/platform/turn.c`, `port/src/platform/turn.h`, `port/src/platform/control.c`
- Test: `tests/test_turnbased.py`

**Interfaces:**
- Consumes: Task 7 (`TurnInput`, `add_*`, `run_action`, the batch hook in `turn_poll`), Task 3 (`champ_action_result`, `ZONE_DROP_CELL_FIRST`, `ui_object_info_lines`, `magic_rune_mana_cost`).
- Produces: the actions of PRD Requirements 13-17 and `act_many` (Requirement 6): `const char *turn_act_many(const char *request);` in `turn.h`, the `act_many` dispatch in `handle_turnbased_request`, and the batch branch of `turn_poll`:

```c
    turn.pending = false;
    if (batch.active && !turn.reply_dropped && batch_after_turn()) return false;  /* the next action of the batch runs */
    return !turn.reply_dropped;
```

A dropped reply during a batch frees the batch (as dm `control_poll` does when the client is gone).

- [ ] **Step 1: Write the tests first**

```python
def test_act_many_stops_on_a_new_creature(tb, lib):
    """Five moves; a creature group comes into view after the second (start.DAT: a corridor with a group ahead, or
    place one with the creature placement helper of tests/test_creature.py): ran 2, stopped creature."""


def test_act_many_parses_names_and_objects(tb, lib):
    """["turn_left", {"action": "turn_right"}] runs both: ran 2, stopped done, facing back to the start."""


def test_act_many_bad_item_fails_without_running_it(tb, lib):
    """[ "turn_left", 7 ]: ran 1, stopped failed, an error text."""


def test_act_many_stops_on_bump(tb, lib):
    """["move_forward", "move_forward"] facing a wall: ran 1, stopped bumped."""


def test_raw_click_runs_a_turn_in_a_dialog(tb, lib):
    """act click on the volume button of an open inventory opens the volume dialog (state.mode dialog); act click on
    its first button closes it (state.mode inventory)."""


def test_recruit_join_adds_the_candidate(tb, lib):
    """A candidate shown (start a recruit as tests/test_champions.py does): act recruit choice join -> the candidate is
    in state.champions; any other named action before it -> error."""


def test_choose_action_with_both_hands(tb, lib):
    """act choose_action champion 0 hand ready index 0 runs the action: the reply has action_damage (>= 0 or a
    failure code)."""


def test_take_coin_moves_a_coin_to_the_hand(tb, lib):
    """An open money box with a coin in slot 0 (give the champion a money box as tests/test_interface.py does):
    act take_coin slot 0 -> state.leader_hand is that coin."""


def test_put_in_slot_needs_an_object_in_hand(tb, lib):
    """put_in_slot with an empty leader hand: ok false, nothing changes."""
```

Run: `uv run pytest -q tests/test_turnbased.py` → the new tests FAIL.

- [ ] **Step 2: The `act_many` parser and the batch**

Copy `next_element`, `batch_next`, `batch_after_turn` (it returns true when the next action started), `batch_finish`, `batch_stop_reason` from `../dm/port/src/platform/control.c`. A list item is a string or an object `{"action": ..., "args": {...}}`; strings hold no `"` (document it in `docs/turnbased-mode.md`, Task 11). Stop order after each turn: `game_over`, `bumped`, `moved`, `turned`, `damage`, `creature`, `message`. An item that is not an action, or an action that cannot run: `stopped` `failed` with `error`; it is not in `ran`. `action_damage` in a batch is the list of the results of the `choose_action` items.

- [ ] **Step 3: The actions**

Each action returns `NULL` after it added its inputs, or an error text (then it added nothing and the game does not change). Positions: `zone_get_rect(zone, r)` for a screen zone; a viewport-relative rect plus the origin of `zone_get_rect(ZONE_VIEWPORT)`. The champion argument: `champion` is a living member (`health > 0`, not the candidate), else `no living champion with this index`. `rel(c)` is `dir_relative(champion(c)->cell, game.party_dir)`.

| Action | Args | Checks (error text) | Inputs |
|---|---|---|---|
| `move_*`, `turn_*` | – | inventory open: `close the inventory first (close_inventory)` | Task 7 |
| `click_wall_ahead` | – | inventory open (same text); no front click zone and no wall ahead: `nothing to click on the wall ahead` | click the center of the front click zone (`view_click_zones` from 9 with kind `CLICK_WALL_ORNAMENT` or `CLICK_DOOR_BUTTON` at the front position, or zone 8); else, with an empty leader hand and a wall ahead, the center of the front wall zone (a click there with an object would throw it: refuse `the leader hand is not empty: nothing to use it on ahead`) |
| `take_object` | `cell` 0-3 | inventory open; cell range `cell must be 0-3 (see get_view piles)`; hand not empty `the leader hand is not empty: drop or store the object first`; no leader `no leader`; zone `cell` empty `no object at this cell` | click the center of `view_click_zones[cell].rect` |
| `drop_object` | `cell` 0-4 | inventory open; hand empty `the leader hand is empty`; cell 2-3 with a wall ahead `no floor ahead`; cell 4 without an alcove zone `no alcove ahead` | click the center of `ZONE_DROP_CELL_FIRST + cell` (0-3) or of zone 8 (4) |
| `open_inventory` | `champion` | – (open already: no input) | command `CMD_INVENTORY_FIRST + c` |
| `close_inventory` | – | – (closed: no input) | command `CMD_INVENTORY_LEADER` |
| `put_in_slot`, `take_from_slot` | `champion`, `slot` | `put_in_slot` with an empty hand: `the leader hand is empty`; `take_from_slot` with a full hand: `the leader hand is not empty`; slot range; a slot other than a hand while that inventory is closed: `open the inventory of this champion first (open_inventory)`; chest slots 30-37 only while a chest is open `no open chest` | hands from the status box (inventory of another champion or closed): command `CMD_SLOT_FIRST + 2*c + slot`; inventory slot: `CMD_SLOT_FIRST + SLOT_HAND_COUNT * CHAMPION_MAX + slot` (0x1c + slot; name the constant); chest: `CMD_SLOT_FIRST + 0x26 + (slot - 30)` (0x3a; name it) |
| `eat`, `drink` | – | inventory closed `open the inventory of the champion who eats or drinks first`; hand empty `take the food or drink in the leader hand first` | command `CMD_MOUTH` |
| `choose_action` | `champion`, `hand` ready/action, `index` | another menu open `another champion's action menu is open: use pass first`; `action_delay[hand]` set or no actions `this champion cannot act now`; `index` out of the listed menu `index out of range of the action menu (see state champions hands)` | command `CMD_HAND_FIRST + 2*rel(c) + hand`, then command `CMD_ACTION_CANCEL + 1 + index`; set `turn.has_damage`; the reply `action_damage` is `champ_action_result` |
| `pass` | – | – (no menu: no input) | command `CMD_ACTION_CANCEL` |
| `set_caster` | `champion` | a panel open `close the open panel first (pass)`; `action_delay[2]` set `this champion cannot cast now` | click the spell icon `ZONE_SPELL_ICON_FIRST + rel(c)` at the corner that `input_refine_champion_click` reads as "spell": cell 0 (left, bottom), 1 (right, bottom), 2 (right, top), 3 (left, top), one pixel inside |
| `add_rune` | `symbol` 0-5 | no spell panel `no spell panel (set_caster)`; four runes `the spell has four runes`; `magic_rune_mana_cost > mana` `not enough mana for this rune` | command `CMD_RUNE_FIRST + symbol` |
| `delete_rune`, `cast` | – | no spell panel; no rune `no rune` | command `CMD_REMOVE_RUNE` / `CMD_CAST_SPELL` |
| `recruit` | `choice` join/cancel | no candidate `no candidate: click a champion mirror first (click_wall_ahead)`; bad choice `choice must be join or cancel` | command `CMD_RECRUIT` / `CMD_RECRUIT_CANCEL` |
| `set_leader` | `champion` | a panel open (same as `set_caster`) | click `ZONE_SPELL_ICON_FIRST + rel(c)` at the "leader" corner: cell 0 (right, top), 1 (left, top), 2 (left, bottom), 3 (right, bottom) |
| `sleep` | – | no leader `no leader`; a candidate | inventory closed: command `CMD_LEADER_INVENTORY` first; then command `CMD_SLEEP` |
| `wake` | – | – (awake: no input) | command `CMD_WAKE` |
| `swap_champions` | `a`, `b` living, different | a panel open | command `CMD_DRAG_FIRST + rel(a)`, then `CMD_DRAG_FIRST + rel(b)` |
| `look` | optional `champion`, `slot` | inventory closed `open an inventory first (open_inventory): the eye is in the inventory`; no slot and empty hand `the leader hand is empty: take an object, or give champion and slot`; slot and full hand `the leader hand must be empty to look at a slot`; empty slot `no object in this slot` | slot: the slot input (take), press on the eye zone (the input zone of `CMD_EYE`), release, the slot input (put back); else press, release. The look text: after the press input ran, `ui_object_info_lines(game.leader_hand)` (record it when the turn gives the release input) |
| `take_coin` | `slot` 0-5 | no open money box `open a money box first (choose_action on the hand that holds it)`; slot without a coin type `no coin in this slot` | command `CMD_COIN_FIRST + slot` |
| `minion_map_option` | `option` 0-3 | no open minion map `open a minion map first`; no options `this minion map has no options` | command `CMD_MAP_OPTION_FIRST + option` |
| `click` | `x`, `y`, `button` left/right | the position checks of `control.c` `get_position` | click (button right: `INPUT_BUTTON_RIGHT`) |
| `key` | `key` (a name or a code, as `docs/llm-control.md`) | `unknown key` | key (reuse the `key_code` of `control.c`: export it as `control_key_code`) |

`input_simulate_command_click` uses the first zone of the command in the current input mode; check each command above against the mode it needs (the research table in this plan's source: the inventory mode has no viewport and no movement keys; the recruit mode has no status-box hands). If a command has no zone in the mode where the action allows it, refuse with a precise error instead.

The eye press: the zone of `CMD_EYE` is 0x222 in the input table (the names in `ui.h` look swapped: `ZONE_EYE` 0x221 is the mouth zone of the input table). Use the input table: find the rect with `input_find_command`-style lookup, or check by hand that a press at the chosen point gives `ui.eye == 1`, and record the swapped names in `docs/skull-formats.md`.

- [ ] **Step 4: Run the tests**

Run: `uv run pytest -q tests/test_turnbased.py` → PASS. `make -C port 2>&1 | grep -c warning` → `0`. `uv run pytest -q tests/test_standard.py` → PASS.

- [ ] **Step 5: Commit**

```bash
git add port/src tests/test_turnbased.py docs/skull-formats.md
git commit -m "feat: act_many and the actions of the turn-based mode"
```

---

### Task 9: save, load, new_game, modal loops, and the process

**Files:**
- Modify: `port/src/platform/control.c`
- Test: `tests/test_turnbased.py`

**Interfaces:**
- Consumes: `advanced_quick_save`, `advanced_quick_load`, `QUICK_*` (Task 1); `session_start_new_game` (Task 2); `turn_*` (Tasks 7, 8).
- Produces: the commands `save`, `load`, `new_game` in the turn-based mode.

- [ ] **Step 1: Write the tests first**

```python
def test_save_move_load_returns_to_the_save(tb, lib, tmp_path):
    """save, act move_forward, load: the party square and the game time are those of the save."""
    # session.save_dir = tmp_path; check QUICK.DAT exists in tmp_path after save


def test_load_without_a_save(tb, lib, tmp_path):
    """No QUICK.DAT: error "no saved game (use save first)", nothing changes."""


def test_load_of_a_damaged_save_keeps_the_game(tb, lib, tmp_path):
    """A damaged QUICK.DAT: error "the saved game is damaged", the game time and square stay."""


def test_lost_game_allows_only_load_and_new_game(tb, lib, tmp_path):
    """A damaged QUICK.DAT and a failed restore: get_state, act, save reply "no game is running: use load or
    new_game"; new_game starts a game and get_state works again; the program does not quit."""


def test_load_after_a_lost_game_plays_again(tb, lib, tmp_path):
    """A lost game, then a valid QUICK.DAT: load replies ok and get_state works again."""


def test_load_after_game_over_plays_again(tb, lib, tmp_path):
    """save; set main_loop.end = 1; act -> "game over"; load -> ok, game_over false; act turn_left works."""


def test_turn_in_a_dialog_ends_and_the_dialog_stays(tb, lib):
    """A turn that opens a dialog (act click on the volume button) ends; state.mode is dialog; save, load, and
    new_game reply "close the dialog first (act click or act key)"; act key enter closes it."""
    # the library has no modal frame hook: set play_frame_hook to control_poll through ctypes
    # (ctypes.c_void_p.in_dll(lib, "play_frame_hook").value = ctypes.cast(lib.control_poll, ctypes.c_void_p).value)
    # and put it back to None at the end


def test_completed_game_is_game_over_and_the_program_stays(tb, lib):
    """Call session_complete in the turn-based mode: game_over true, state.party.dead false, main_loop.quit 0."""

```

Run: `uv run pytest -q tests/test_turnbased.py` → the new tests FAIL.

- [ ] **Step 2: Write the commands**

In `control.c`, turn-based mode, before the other commands (`load` and `new_game` work after a game over and after a lost game):

```c
static int game_lost;                 /* a damaged save and a failed restore left no game to play */

/* The input modes in which the game runs a modal loop: save, load, and a new game wait until it closes. */
static bool modal_mode(void)
{
    int mode = input_get_mode();
    return mode == INPUT_MODE_MENU || mode == INPUT_MODE_PICTURE || mode == INPUT_MODE_DIALOG || mode == INPUT_MODE_NAME;
}

/* Replies the error and returns true while a modal loop of the game runs. */
static bool refuse_in_modal_loop(void)
{
    if (!modal_mode()) return false;
    send_error("close the dialog first (act click or act key)");
    return true;
}

/* After a load or a new game: the end of the last game is over. */
static void game_restarts(void)
{
    main_loop.end = 0;
    session.completed = 0;
    game_lost = 0;
}

static void handle_load(void)
{
    if (refuse_in_modal_loop()) return;
    int result = advanced_quick_load();
    if (result == QUICK_LOST) game_lost = 1;
    if (result != QUICK_LOADED) {
        send_error(result == QUICK_MISSING ? "no saved game (use save first)"
                   : result == QUICK_BLOCKED ? "cannot write QUICK.TMP: nothing loaded" : "the saved game is damaged");
        return;
    }
    game_restarts();
    send_part("state", state_json_state);
}

static void handle_new_game(void)
{
    if (refuse_in_modal_loop()) return;
    if (!session_start_new_game()) {
        game_lost = 1;
        send_error("cannot start the new game");
        return;
    }
    game_restarts();
    send_part("state", state_json_state);
}

static void handle_save(void)
{
    if (refuse_in_modal_loop()) return;
    if (ui.candidate) send_error("answer the candidate first (act recruit)");
    else if (!advanced_quick_save()) send_error("cannot write the save file");
    else send_ok();
}
```

After `game_lost`, every command except `load`, `new_game`, and `quit` replies `no game is running: use load or new_game`. Order in the turn-based dispatch: `quit`, `load`, `new_game`, the `game_lost` check, the direct-input refusal, `get_*`, `state`, `screenshot`, the game-over check, `save`, `act`, `act_many`, `wait`.

The quick load prints `GAME LOADED` in the message area; the next turn's news compares against the snapshot of that turn, so it does not stop a batch.

- [ ] **Step 3: Run the tests**

Run: `make -C port 2>&1 | grep -c warning` → `0`.
Run: `uv run pytest -q -n auto tests/test_turnbased.py tests/test_modes.py tests/test_control.py tests/test_standard.py` → PASS.

- [ ] **Step 4: Commit**

```bash
git add port/src tests/test_turnbased.py
git commit -m "feat: save, load, and new_game in the turn-based mode"
```

---

### Task 10: dm2-play: act forms, --brief, and the call log

**Files:**
- Modify: `src/dm2tools/control_client.py`
- Test: `tests/test_play_client.py` (new)

**Interfaces:**
- Consumes: the JSON fields of Tasks 5-9.
- Produces: `main(argv)` with `--brief`, `act <action> [json-args]`, `act_many '<json-list>'`; `brief(response) -> str`; `log_call(path, payload, response)`; `LOG_DIR` from `DM2_PLAY_LOG_DIR` (default `playthroughs/current/coordinator`, `none` turns it off).

- [ ] **Step 1: Write the tests first**

```python
from dm2tools import control_client as cc

STATE = {...}  # a small state dict with the fields of Task 5: two champions, a hand, a message, a dialog
VIEW = {...}   # squares at depth 0-2 with a door, a creature, a pile


def test_brief_of_a_turn():
    text = cc.brief({"ok": True, "ticks": 3, "game_over": False, "bumped": True, "state": STATE, "view": VIEW})
    assert text.splitlines()[0] == "ticks 3 | game_over False | BUMPED"
    assert "ahead: door closed" in text and "piles: cell 2: Torch (top first)" in text


def test_brief_of_an_error():
    assert cc.brief({"ok": False, "error": "game over"}) == "error: game over"


def test_brief_of_a_map_prints_the_grid():
    assert cc.brief({"ok": True, "map": {"grid": ["   0", " 0 @"]}}) == "   0\n 0 @"


def test_call_log_line(tmp_path, monkeypatch):
    """One call appends one tab-separated line: time, action or cmd, ok/fail, map, x, y, game time."""


def test_unreachable_game_is_logged_and_exits_1(tmp_path, monkeypatch, capsys):
    monkeypatch.setenv("DM2_CONTROL", str(tmp_path / "none.sock"))
    monkeypatch.setattr(cc, "LOG_DIR", str(tmp_path))
    assert cc.main(["act", "move_forward"]) == 1
    assert (tmp_path / "calls.log").read_text().count("\n") == 1

```

Run: `uv run pytest -q tests/test_play_client.py` → FAIL.

- [ ] **Step 2: Write the client**

Port `../dm/tools/dmtools/play.py` into `control_client.py` (one module; keep `request`, `socket_path`, `--socket`, `DEFAULT_SOCKET`, `TIMEOUT`, the absolute `path`, exit code 1 on `"ok": false`), with:
- `DM2_PLAY_LOG_DIR`; the value `none` turns the log off; the log goes to `<dir>/calls.log` when the folder exists; a call that cannot reach the game is logged too.
- `square_text`: the DM2 fields (`floor_ornament` index, `door` with `button`, the faces with `mirror`, `switch`, `lock`, `alcove`, `fountain`, `shop`, `ornament`, `text`, creatures `CREATURES <count> <type>`, objects, flying).
- `LANE = ["here", "ahead", "ahead 2", "ahead 3", "ahead 4"]`; side squares by the numeric `side` (-1 left, 1 right; ±2, ±3 only when not a wall, as "far left"/"far right").
- `state_lines`: the turn line, `time | level x y facing | darkness | outdoor weather day_time`, leader and hand, candidate, one line per champion (bars, food, water, notes), the open action menu, the spell panel, the money box, the minion map, the dialog (`dialog <id>: <buttons>`), the messages.
- `brief`: the head keys `ticks`, `ran`, `stopped`, `error`, `game_over`, `action_damage`, `health_lost`, `path`, plus `BUMPED`; then `look`, `seen`, the state, the view, the map grid.

The docstring of the module states the forms and the environment variables.

- [ ] **Step 3: Run the tests**

Run: `uv run pytest -q tests/test_play_client.py` → PASS.

- [ ] **Step 4: Commit**

```bash
git add src/dm2tools/control_client.py tests/test_play_client.py
git commit -m "feat: dm2-play act forms, --brief summary, and call log"
```

---

### Task 11: The run guide and the reference docs

**Files:**
- Create: `docs/turnbased-mode.md`
- Modify: `docs/llm-control.md`, `docs/advanced-mode.md`, `docs/manual-test-checklist.md`, `docs/skull-formats.md`, `CLAUDE.md` (Commands), `CHANGELOG.md`

No code; no test. Follow the developer-documentation skill and the ASD-STE100 style of the existing docs.

- [ ] **Step 1: `docs/turnbased-mode.md`**

From `../dm/docs/llm-mode.md`, rewritten for this repository (no MCP):
- Start: `SDL_AUDIO_DRIVER=dummy build/dm2 --mode turnbased --saves playthroughs/current/game [--control PATH] [--save FILE]`; the default socket `~/.dm2/control.sock`; `DM2_CONTROL`.
- Protocol: the commands (`get_state`, `get_view`, `get_map`, `act`, `act_many`, `wait`, `save`, `load`, `new_game`, `state`, `screenshot`, `quit`), the refused direct commands, the reply fields, the turn end rule with the constants, the stops and their order, displacement, bump, seen, game over, the modal loops and `close the dialog first`, the lost game; strings in `act_many` hold no `"`.
- The actions table (from Task 8) with args and errors.
- Goal of the runs and the allowed changes of the player instructions; runs and checkpoints (`playthroughs/run<N>/` with `player/`, `coordinator/`, `game/`, `saves/`; `playthroughs/current`; a new run: `new_game`, `save`; a resume: `load`); the coordinator as a subagent.
- C API: `control_frame`, `turn_*`, `state_json_*`; integration in `main.c`.
- Tests: `tests/test_state_json.py`, `tests/test_turnbased.py`, `tests/test_play_client.py`.

- [ ] **Step 2: The other docs**

- `docs/llm-control.md`: name the third mode; for the turn-based mode refer to `docs/turnbased-mode.md`; the direct commands are refused there.
- `docs/advanced-mode.md`: the turn-based mode shares the seen squares and the AMAP block; `--saves DIR` sets the folder of `QUICK.DAT` in all modes.
- `docs/manual-test-checklist.md`: a new section `## M. Turn-based mode` before `## Known deviations`, items `M1.` ... in the existing form: start without the menu (`--mode turnbased --saves DIR`: no menu, no champions, a `key` request refused, `quit` exits with 0), `new_game` on the start square, `act` with an unknown action changes nothing, `dm2-play act take_object '{"cell": 2}'` and `act_many` send the right payload, window input ignored, act moves with the window watching, look text against the eye panel, a dialog through act click, save/load/new_game, game over and load, `dm2-play --brief`, the call log.
- `docs/skull-formats.md`: the deviations recorded in Tasks 3-8 (labels of `ui_object_info_lines`, master rank glyphs, the teleporter-bump, the swapped eye/mouth zone names, dialog ids of the slot dialogs).
- `CLAUDE.md` Commands: `build/dm2 --mode turnbased --saves DIR` and `uv run dm2-play --brief get_state`.
- `CHANGELOG.md` `### Added`: the turn-based mode, `--saves`, `dm2-play` act forms, `--brief`, call log; `### Changed`: control replies have no size limit.

- [ ] **Step 3: Commit**

```bash
git add docs CLAUDE.md CHANGELOG.md
git commit -m "docs: turn-based mode guide and reference updates"
```

---

### Task 12: The player manual

**Files:**
- Create: `docs/llm-playbook.md`

No code; no test.

- [ ] **Step 1: Write the manual**

From `../dm/docs/llm-playbook.md`, rewritten for Dungeon Master II; keep its section order. Game part: your notes, the party (recruit from mirrors with `click_wall_ahead` and `recruit join`), health, stamina, mana, food, water, sleep, light and load, skills (the four base skills and their sub-skills), magic (the rune sets with names, `set_caster`, `add_rune`, `cast`, the mana per rune in `state.spell`), fights (both hands, `choose_action` with `hand`, `action_damage`), money (the money box, `take_coin`, coin names and counts), merchants (creatures in the view; trade by putting goods and coins on the floor cell in front of them; shops on walls), minions (the minion map, `minion_map_option`), outside and weather (`outdoor`, `weather`, `day_time`), saves (`save`, `load`; do not use the save button of the game), when stuck, bugs. Play part: start of a session, rules, the commands, `dm2-play` forms and `--brief`, batches, bump, displacement, wait, seen, game over, the actions table, cells (DM2 cells: piles 0-1 on the party square, 2-3 on the square ahead, 4 the alcove), slots (the 30 slot names and 30-37 for an open chest), the fields of the state, the view, and the map, end of a session. The goal: the party completes the game. Every field name and action name must match Tasks 5-9 exactly; check each with `grep` in `port/src/platform/`.

- [ ] **Step 2: Commit**

```bash
git add docs/llm-playbook.md
git commit -m "docs: player manual for the turn-based mode"
```

---

### Task 13: The agents and the run folders

**Files:**
- Create: `.claude/agents/dm2-coordinator.md`, `.claude/agents/dm2-player.md`
- Create: `playthroughs/template/` (`bestiary.md`, `bugs.md`, `items.md`, `lessons.md`, `map_L1.md`, `party.md`, `progress.md`, `spells.md`)
- Modify: `.gitignore`

No code; no test.

- [ ] **Step 1: The agents**

From `../dm/.claude/agents/dm-coordinator.md` and `dm-player.md` with: `dm2-play` and `DM2_PLAY_LOG_DIR`; `docs/turnbased-mode.md` instead of `docs/llm-mode.md`; checkpoints copy `QUICK.DAT` from `game/`; the game process: `build/dm2 --mode turnbased --saves playthroughs/current/game` (never touch another dm2 process); the game is Dungeon Master II (PC DOS port); the goal is to complete the game; commits name no author email (git config sets the author; CLAUDE.md §3).

- [ ] **Step 2: The template**

Copy the headers and the "What to write" comments of `../dm/playthroughs/template/*.md`, adapted to DM2 (money in `party.md`, merchants and shops in `items.md`, minions in `bestiary.md`, outside areas in `progress.md`). No save file in the template (PRD Decision "Start of a run").

- [ ] **Step 3: `.gitignore`**

```
/playthroughs/*
!/playthroughs/template/
```

- [ ] **Step 4: Check and commit**

Run: `git status --short playthroughs` → only `playthroughs/template/` files.

```bash
git add .claude/agents/dm2-coordinator.md .claude/agents/dm2-player.md playthroughs/template .gitignore
git commit -m "docs: run agents and player file template for the turn-based mode"
```
