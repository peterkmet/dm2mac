# Layer cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task (the run of the agentic-workflow skill). Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove all 77 `layer` entries of `tests/standard_allowlist.txt`: no file in `port/src/engine/` includes a header of `port/src/ui/`.

**Architecture:** Data and state that the rules read move down to `data/` or `engine/`. Drawing code moves up to `ui/`. The main loop moves to the new module `ui/play.c`. A rule that must run the user interface inside the rule calls a function pointer of `g_hooks` (`engine/hooks.h`); a `ui/` file defines `g_hooks` with a static initializer.

**Tech Stack:** C11, clang, `port/Makefile`, Python tests with ctypes (`build/libdm2.dylib`) and the unicorn emulator (`dm2tools.emu`), uv, pytest.

**Spec:** `docs/superpowers/2026-10-09-layer-cleanup.md`

## Global Constraints

- The behavior of the game does not change. The differential tests prove it. Do not change a test to make it pass; a test changes only for a new name, a new import path, or a moved struct field.
- `docs/coding-standard.md` is the coding standard. Follow it for every change in `port/src/`. Rule 7: a moved function takes a prefix of its new header; a prefix belongs to one header; no prefix starts with the prefix of another header. Rule 8: a file includes only headers of its own layer and of the layers before it (`data`, `engine`, `ui`, `platform`).
- A header name is unique over all layers. The layer test maps a header name to one folder.
- Hook form: `engine/hooks.h` declares `typedef struct {...} Hooks;` and `extern Hooks g_hooks;`. `ui/hook_table.c` defines `Hooks g_hooks = {...};` with a static initializer that sets every member. A hook call does not check for `NULL`. A hook stays in the same place as the call today. A redraw flag replaces a call only where the drawing pass of the same game step draws the same result before the screen shows, and the differential tests prove it.
- Moved global variables keep their names (the tests read them with `in_dll`). The `g_` rename of old globals is out of scope.
- The save format does not change.
- Keep existing comments. New comments are one line and tell the game meaning.
- `rm -f build/dm2 build/libdm2.dylib && make -C port 2>&1 | grep -c warning` gives `0`.
- The author runs the build and only the tests of the changed code, never the full suite. Always run `uv run pytest -q -p no:xdist tests/test_standard.py` and the test modules that call the moved or renamed functions (find them with `grep -ln "<name>" tests/*.py`).
- Each task removes the allowlist lines `layer <file>:<header>` of the includes that it removes. A task does not add an `open:` line.
- Commit at the end of each task with a Conventional Commit message, one type per commit (`refactor:` for moves, `docs:` for docs only).

Test list (CLAUDE.md §2). Write a test only for: a check against a reference (original program, golden snapshot), a parser or data format, a rule with more than one outcome that no reference check or type check covers (dates, parsing, prices, state machines), a regression of a real bug (one test per bug). No tests for: constants and data tables; wiring, scaffold, CLI arguments, logging; what the compiler, a linter or a standard check catches; UI and layout (the user tests them manually); what another test already covers. No mutation tests.

This plan moves code and adds wiring. It adds no new tests. The existing differential tests and `tests/test_standard.py` are the checks.

## Review Focus

1. A hook that is called before `ui/` sets the state it reads (for example a message before `text_init`): expected the same output as before, because the static initializer only points at the same functions that the rule called directly.
2. The GDAT handle changes during a load (`save_load` with a second `GRAPHICS.DAT`): expected the image cache of `draw.c` to drop the images of the old file, as `draw_init` does today. Task 2 owns it; the existing tests of `save_load` with a different `gdat` cover it.
3. A ctypes mirror of a moved struct (`Session`, `UiState`, `MainLoop`) that does not follow the new layout: expected the tests that use it to fail loudly. Task 3 checks every mirror with `grep`.
4. The square aspect of a map that is not the view map (`view_get_square_aspect` sets `view.map` to `party.cur_map`): expected the same aspect from the engine function with the map as an argument. Task 6 owns it; `tests/test_view.py` and the weather tests cover it.
5. The sky picture check after the move to `engine/weather.c`: expected the same wallset as `view.gfx_set` of the view. Task 6 owns it; the weather differential tests cover it.

No line needs a new test: the existing differential tests cover each input class.

---

### Task 1: Hook table and the message hooks

**Files:**
- Modify: `port/src/engine/hooks.h`
- Delete: `port/src/engine/hooks.c`
- Create: `port/src/ui/hook_table.c`
- Modify: `port/src/ui/input.c` (the stub `command_execute_later`), `port/src/ui/input.h:145` (remove its declaration)
- Modify: `port/src/engine/champ_action.c:341-344`, `port/src/engine/inventory.c:152-153`, `port/src/engine/recruit.c:167-169`, `port/src/engine/sensor.c:287`, `port/src/engine/sensor.c:979`
- Modify: `docs/coding-standard.md` rule 8

**Interfaces:**
- Produces: `Hooks`, `g_hooks` with the members `print_message(int color, const char *text)` and `print_newline(void)`. Later tasks add members to the same struct and the same initializer.

- [ ] **Step 1: Replace `engine/hooks.h`**

Keep the includes that other files get from `hooks.h` today only if a file needs them: remove `sound_request.h`, `timeline.h`, `projectile.h` from `hooks.h`, build, and add the missing includes to the files that fail.

```c
#ifndef HOOKS_H
#define HOOKS_H

/* Calls of the game rules into the user interface, for events that must run the interface inside the rule.
   ui/hook_table.c sets every member. */
typedef struct {
    void (*print_message)(int color, const char *text);  /* a line in the message area */
    void (*print_newline)(void);                         /* the next message starts on a new row */
} Hooks;

extern Hooks g_hooks;

#endif
```

- [ ] **Step 2: Create `ui/hook_table.c`**

```c
/* The user interface functions that the game rules call (engine/hooks.h). */
#include "hooks.h"
#include "text.h"

Hooks g_hooks = {
    .print_message = text_print_message,
    .print_newline = text_print_newline,
};
```

If the signature of `text_print_message` differs from the member (for example `uint8_t color`), change the member to the exact signature of the function. Do not cast function pointers.

- [ ] **Step 3: Move `command_execute_later`**

Delete `engine/hooks.c`. In `ui/input.c`, add the stub as a static function above its caller (line 1594) and remove its declaration from `ui/input.h`:

```c
/* A command that the original runs later; the port runs none. */
static void command_execute_later(const Command *c) { (void)c; }
```

- [ ] **Step 4: Call the hooks**

Replace `text_print_message(` with `g_hooks.print_message(` and `text_print_newline()` with `g_hooks.print_newline()` in the five call sites above. Each file includes `hooks.h`.

- [ ] **Step 5: Rule 8 in the coding standard**

In `docs/coding-standard.md` rule 8, replace "Exception: a hook that `ui/` sets, for an event that must run the user interface inside the rule." with: "Exception: a hook, a member of `g_hooks` (`engine/hooks.h`) that `ui/hook_table.c` sets, for an event that must run the user interface inside the rule."

- [ ] **Step 6: Build and test**

Run: `rm -f build/dm2 build/libdm2.dylib && make -C port 2>&1 | grep -c warning` → `0`.
Run: `uv run pytest -q -p no:xdist tests/test_standard.py tests/test_interface.py tests/test_sensor.py tests/test_inventory.py` (use the modules that exist and cover the five call sites; find them with `grep -ln "text_print_message\|recruit\|inv_show_object_name" tests/*.py`).
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add -A port/src docs/coding-standard.md tests/standard_allowlist.txt
git commit -m "refactor: hook table for the calls of the rules into the interface"
```

### Task 2: GDAT handle and the game texts

**Files:**
- Modify: `port/src/data/gdat.h`, `port/src/data/gdat.c`
- Modify: `port/src/ui/draw.h`, `port/src/ui/draw.c`
- Create: `port/src/engine/game_text.h`, `port/src/engine/game_text.c`
- Modify: `port/src/ui/text.h`, `port/src/ui/text.c`
- Modify: every file that calls `draw_gdat()` (`grep -ln "draw_gdat()" port/src/*/*.c`), `port/src/engine/save.c:776`
- Modify: tests that call `draw_gdat` (`grep -ln "draw_gdat" tests/*.py`)

**Interfaces:**
- Produces: `const gdat_t *gdat_current(void)`, `void gdat_select(const gdat_t *g)` in `data/gdat.h`. `engine/game_text.h` with the prefixes `text_format text_gdat text_decode_` and: `TEXT_SIZE`, `TextArgs`, `text_args`, `text_drive_path`, `text_format_number`, `text_format`, `text_gdat`, `text_gdat_label`, `text_gdat_field` and its field enum, `text_decode_raw`, `text_decode_dungeon`, and the interface color index enum (`UI_BLACK` to `UI_COLOR_COUNT`).
- `ui_colors` (the palette table) stays in `ui/text.h`.

- [ ] **Step 1: The GDAT handle in `data/gdat`**

```c
/* gdat.h */
/* The GRAPHICS.DAT that the game reads; NULL before the first gdat_select. */
const gdat_t *gdat_current(void);
void gdat_select(const gdat_t *g);
```

```c
/* gdat.c */
static const gdat_t *current;

const gdat_t *gdat_current(void) { return current; }
void gdat_select(const gdat_t *g) { current = g; }
```

- [ ] **Step 2: `draw.c` follows the handle**

`draw_init(g)` calls `gdat_select(g)` and builds the image cache for `g`. The cache remembers its file in `static const gdat_t *cache_owner`. `decoded()` (and every other reader of `images`) first calls a helper that rebuilds the cache when `gdat_current() != cache_owner`, so a `gdat_select` from `save_load` drops the images of the old file, as `draw_init` does today. Remove `draw_gdat()`; replace each call in `data/`, `engine/`, `ui/`, `platform/`, and the tests with `gdat_current()`.

In `engine/save.c:776` write:

```c
        if (gdat_current() != gdat) gdat_select(gdat);  /* the item effects of the champions read the item attributes */
```

- [ ] **Step 3: Move the game texts to `engine/game_text`**

Move the declarations and definitions listed under Produces from `ui/text.h` and `ui/text.c` to `engine/game_text.h` and `engine/game_text.c`, with their static helpers (`append`, the field names, the dungeon text decoder constants and tables). Remove the moved prefixes from the `Prefixes` line of `ui/text.h` and put them on the `Prefixes` line of `game_text.h`. `ui/text.h` includes `game_text.h`.

- [ ] **Step 4: Remove the includes**

In each `engine/` file, remove `#include "draw.h"` and `#include "text.h"` where the file now uses only `gdat_current()` and `game_text.h`; include `gdat.h` and `game_text.h` instead. Remove the matching `layer` lines of the allowlist (`engine/dungeon.c:draw.h`, `engine/dungeon.c:text.h`, `engine/item.c:draw.h`, `engine/item.c:text.h`, `engine/money.c:draw.h`, `engine/money.c:text.h`, `engine/save.c:draw.h`, and the others that the build allows).

- [ ] **Step 5: Build and test**

Run: `rm -f build/dm2 build/libdm2.dylib && make -C port 2>&1 | grep -c warning` → `0`.
Run: `uv run pytest -q -p no:xdist tests/test_standard.py` and the modules of `grep -ln "draw_gdat\|text_gdat\|text_format\|text_decode\|save_load\|draw_init" tests/*.py`.
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add -A port/src tests
git commit -m "refactor: GDAT handle in data, game texts in engine"
```

### Task 3: Shared state in `engine/game.h`

**Files:**
- Modify: `port/src/engine/game.h`, `port/src/engine/game.c`
- Modify: `port/src/ui/ui.h`, `port/src/ui/ui.c` (`UiState`, `ui`), `port/src/ui/input.h`, `port/src/ui/input.c` (`input_drag_state`, `input_command_stop`, `input_mouse_buttons`), `port/src/ui/session.h`, `port/src/ui/session.c` (`Session`, `session`, `SAVE_PATH_SIZE`), `port/src/ui/advanced.h`, `port/src/ui/advanced.c` (`game_mode`, `GAME_MODE_*`, `game_mode_names`, `cheat_light`, `cheat_immortal`, `cheat_food`), `port/src/ui/view.h`, `port/src/ui/view.c` (`view_light_level`, `DARKEST_LIGHT`)
- Modify: `port/src/engine/weather.h`, `port/src/engine/weather.c`
- Modify: the `engine/` files that include `ui.h`, `session.h`, `advanced.h`, `input.h`, `view.h` only for this state
- Test: the ctypes mirrors `Session` (`tests/test_dialog.py:34` and its users), `UiState` (`tests/test_interface.py:38`), `MainLoop` (`tests/test_dialog.py:49`, `tests/test_session_play.py:110`)

**Interfaces:**
- Produces in `engine/game.h`: `UiState` and `extern UiState ui;` (the whole struct, same field order); `extern int input_drag_state;`, `extern int input_command_stop;`, `extern int input_mouse_buttons;`; `SAVE_PATH_SIZE`, `Session` and `extern Session session;` (the whole struct, same field order); `GAME_MODE_*`, `extern int game_mode;`, `extern const char *const game_mode_names[GAME_MODE_COUNT];`, `extern int cheat_light, cheat_immortal, cheat_food;`. The definitions move to `engine/game.c`.
- Produces in `engine/weather.h`: `extern int view_light_level;`, `DARKEST_LIGHT`. The definition moves to `engine/weather.c`, next to `light_update_party_light`.
- The functions (`ui_*`, `session_*`, `advanced_*`, `input_*`) stay in `ui/`.

- [ ] **Step 1: Move the types, globals, and constants**

Move each item under Produces with its comment. The `ui/` headers include `game.h` or `weather.h` where they used the item. Before you move `input_mouse_buttons`, read its writers: if it is a function and not a variable, keep it in `ui/` and add a hook `int (*mouse_buttons)(void)` to `g_hooks` instead.

- [ ] **Step 2: Remove the includes**

In each `engine/` file, remove the includes of `ui.h`, `session.h`, `advanced.h`, `input.h`, and `view.h` that only served this state. Remove the matching allowlist lines (for example `engine/ai.c:ui.h`, `engine/creature.c:ui.h`, `engine/weather.c:ui.h`, `engine/champion.c:session.h`, `engine/damage.c:session.h`, `engine/damage.c:advanced.h`, `engine/save.c:advanced.h`, `engine/ai_move.c:view.h`, `engine/projectile.c:view.h`).

- [ ] **Step 3: Check the ctypes mirrors**

Run: `grep -n "class Session\|class UiState\|class MainLoop\|in_dll(lib, \"ui\")\|in_dll(lib, \"session\")\|in_dll(lib, \"main_loop\")" tests/*.py`
The struct layouts do not change, so the mirrors stay. If the build changed a layout, stop and report it.

- [ ] **Step 4: Build and test**

Run: `rm -f build/dm2 build/libdm2.dylib && make -C port 2>&1 | grep -c warning` → `0`.
Run: `uv run pytest -q -p no:xdist tests/test_standard.py tests/test_dialog.py tests/test_interface.py tests/test_session_play.py tests/test_modes.py tests/test_automap.py tests/test_world.py tests/test_move.py`.
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add -A port/src tests
git commit -m "refactor: state of the rules and the interface in engine/game.h"
```

### Task 4: Seen squares of the automap in `engine/seen`

**Files:**
- Create: `port/src/engine/seen.h`, `port/src/engine/seen.c`
- Modify: `port/src/ui/automap.h`, `port/src/ui/automap.c`, `port/src/engine/save.c:780,810`, `port/src/engine/game.c` (`automap_mark_seen`), `port/src/ui/session.c` and `port/src/ui/advanced.c` (the callers of `automap_reset`)
- Modify: `tests/test_automap.py` (the renamed functions)
- Modify: `docs/advanced-mode.md` (the function names)

**Interfaces:**
- Produces in `engine/seen.h` (`/* Prefixes: seen_ */`): `void seen_reset(void)`, `void seen_mark(int map, int x, int y, int dir)`, `bool seen_is_seen(int map, int x, int y)`, `int seen_write(FILE *f)`, `void seen_read(FILE *f)`. They are the old `automap_reset`, `automap_mark_seen`, `automap_is_seen`, `automap_write`, `automap_read` with their data and constants.
- `ui/automap.h` keeps `automap_draw`, `automap_scroll`, `automap_scroll_reset` and includes `seen.h`.

- [ ] **Step 1: Move the seen squares and the save block**

Move the five functions, the seen square bytes, and the block constants to `engine/seen.c`. Rename them as listed. `ui/automap.c` reads the seen squares through `seen_is_seen`.

- [ ] **Step 2: Callers and tests**

Replace the old names in `engine/save.c`, `engine/game.c`, `ui/`, `tests/test_automap.py` (`lib.automap_is_seen` → `lib.seen_is_seen`, keep `restype = ctypes.c_bool`), and `docs/advanced-mode.md`. Remove `engine/save.c:automap.h` and `engine/game.c:automap.h` from the allowlist.

- [ ] **Step 3: Build and test**

Run: `rm -f build/dm2 build/libdm2.dylib && make -C port 2>&1 | grep -c warning` → `0`.
Run: `uv run pytest -q -p no:xdist tests/test_standard.py tests/test_automap.py tests/test_modes.py`.
Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add -A port/src tests docs
git commit -m "refactor: seen squares and their save block in engine/seen"
```

### Task 5: Sound placement in `engine/sound_request`

**Files:**
- Modify: `port/src/ui/sound.h`, `port/src/ui/sound.c`
- Modify: `port/src/engine/sound_request.h`, `port/src/engine/sound_request.c`
- Modify: `port/src/engine/hooks.h`, `port/src/ui/hook_table.c`

**Interfaces:**
- Produces in `engine/sound_request.h`: `Sound`, `sound_load`, `sound_party_offset`, `sound_volume_pan` with their static helpers and tables (`scale_away`, `pan_thresholds`, `pan_values`) and the constants they use. Add `sound_load sound_party_ sound_volume_` to its `Prefixes` line; remove them from `ui/sound.h`.
- Produces in `g_hooks`: `int (*play_sound)(const Sound *s, uint8_t volume, int8_t lateral, int8_t forward);` set to `sound_play`.

- [ ] **Step 1: Move the functions**

Move the items under Produces. `ui/sound.h` includes `sound_request.h`. `sound_play` stays in `ui/sound.c`.

- [ ] **Step 2: The play hook**

Add the member to `Hooks` (`hooks.h` includes `sound_request.h` for `Sound`) and to the initializer. In `engine/sound_request.c:71` call `g_hooks.play_sound(...)`. Remove `engine/sound_request.c:sound.h` and `engine/sound_request.c:draw.h` from the allowlist.

- [ ] **Step 3: Build and test**

Run: `rm -f build/dm2 build/libdm2.dylib && make -C port 2>&1 | grep -c warning` → `0`.
Run: `uv run pytest -q -p no:xdist tests/test_standard.py` and the modules of `grep -ln "sound_" tests/*.py`.
Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add -A port/src tests
git commit -m "refactor: sound placement rules in engine/sound_request"
```

### Task 6: Square aspect, cell grid, and sky objects in `engine/`

**Files:**
- Create: `port/src/engine/aspect.h`, `port/src/engine/aspect.c`
- Modify: `port/src/ui/view.h`, `port/src/ui/view.c` (lines about 515-525 `first_thing`, 759-925 the square aspect, 945-990 the cell grid, 551-563 the sky objects, 2959-2968 `view_wall_face_ornament`)
- Modify: `port/src/engine/weather.h`, `port/src/engine/weather.c`, `port/src/engine/move.c`, `port/src/engine/minion.c`, `port/src/engine/sensor.c:1381`
- Modify: `port/src/engine/hooks.h`, `port/src/ui/hook_table.c`
- Modify: the tests that call the renamed functions (`grep -ln "view_get_square_aspect\|view_get_creature_cell_zone\|view_grid5_cell_to_xy\|view_rotate_grid5_cell\|view_wall_face_ornament\|view_load_sky_object\|view_sky_objects" tests/*.py`)

**Interfaces:**
- Produces in `engine/aspect.h` (`/* Prefixes: aspect_ cell_grid_ */`):
  - `ViewSquare`, `VIEW_SQUARE_RECORD_SIZE`, `VIEW_SIDE_COUNT`, the aspect enum (`ASPECT_*`), the side and ornament word helpers that the square aspect code uses (`view_face_word`, `view_face_frame`, `view_floor_frame`, `ORNAMENT_NONE`, `ORNAMENT_INDEX_MASK`, `SIDE_*`); rename the function-like macros to the `aspect_` prefix (`aspect_face_word`, `aspect_face_frame`, `aspect_floor_frame`).
  - `void aspect_get(ViewSquare *s, int map, int dir, int x, int y)`: the old static `get_square_aspect` with `view.map` replaced by the argument `map`. `view.c` calls `aspect_get(s, view.map, dir, x, y)`; the old `view_get_square_aspect(s, dir, x, y)` becomes `aspect_get(s, party.cur_map, dir, x, y)` at its callers.
  - `int aspect_wall_face_ornament(int map, uint16_t first, int dir)`: the old `view_wall_face_ornament`.
  - `CELL_GRID_SIDE`, `CELL_GRID_HALF`, `cell_grid_index`, `CELL_CENTER`; `int aspect_creature_cell(uint16_t group, int dir)` (old `view_get_creature_cell_zone`), `int aspect_rotate_cell(int cell, int dir)` (old `view_rotate_grid5_cell`), `void aspect_cell_to_xy(int base_x, int base_y, uint16_t cell, int16_t *x, int16_t *y)` (old `view_grid5_cell_to_xy`).
- Produces in `engine/weather.h`: `SkyObject`, `SKY_OBJECT_END`, `SkyObject *weather_sky_objects(void)`, `int weather_load_sky_object(SkyObject *object)`. The sky list moves from `view.sky` to a static array of `weather.c`. `weather_load_sky_object` reads the wallset of the party map (`dungeon_map_wallset(game.party_map)`) where `view_load_sky_object` read `view.gfx_set`.
- Produces in `g_hooks`: `int (*zone_offset)(int from, int to, int percent, int16_t *dx, int16_t *dy);` set to `zone_interpolate_offset`.

- [ ] **Step 1: Move the square aspect**

Move the static helpers of the square aspect (`first_thing`, `word_ornament`, `apply_wall_things`, `apply_floor_things`, `skip_wall_things`, `wall_square_aspect`, `set_map_floor_ornament`, `get_square_aspect`, `value_key` if only they use it, and the tables they read) to `engine/aspect.c`. Each helper that read `view.map` takes `int map` as an argument. `view.c` keeps a static `value_key` copy only if its own code still needs it; then name the engine one `aspect_value_key` and declare it in `aspect.h` instead of two copies.

- [ ] **Step 2: Move the cell grid**

Move the cell grid macros, `view_get_creature_cell_zone`, `view_rotate_grid5_cell`, `view_grid5_cell_to_xy` with their tables, renamed as listed. Remove `cell_grid_` from the `Prefixes` line of `view.h`.

- [ ] **Step 3: Check the sky wallset, then move the sky objects**

Before the move, check that `view.gfx_set == dungeon_map_wallset(game.party_map)` at each call of `view_load_sky_object`: add a temporary `assert` in `view_load_sky_object`, run `uv run pytest -q -p no:xdist` on the modules of `grep -ln "weather\|lightning\|sky" tests/*.py`, and remove the assert. If it fails, stop and report it: the sky check then needs the view map as an argument.
Move the sky objects as listed. `view.c` draws from `weather_sky_objects()`.

- [ ] **Step 4: The zone offset hook**

Add `zone_offset` to `Hooks` and the initializer; `weather.c:296` calls `g_hooks.zone_offset(...)`.

- [ ] **Step 5: Callers, tests, allowlist**

Replace the old names in `engine/`, `ui/`, the tests, and `docs/skull-formats.md`. Remove the allowlist lines that the build no longer needs (`engine/move.c:view.h` stays until Task 9: `transit_pit` still calls the view; `engine/weather.c:view.h`, `engine/weather.c:zone.h`, `engine/sensor.c:view.h` go).

- [ ] **Step 6: Build and test**

Run: `rm -f build/dm2 build/libdm2.dylib && make -C port 2>&1 | grep -c warning` → `0`.
Run: `uv run pytest -q -p no:xdist tests/test_standard.py tests/test_view.py` and the modules of the grep in Files.
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add -A port/src tests docs
git commit -m "refactor: square aspect, cell grid, and sky objects in engine"
```

### Task 7: Drawing code to `ui/`

**Files:**
- Create: `port/src/ui/action_area.h`, `port/src/ui/action_area.c`, `port/src/ui/minion_map.h`, `port/src/ui/minion_map.c`
- Modify: `port/src/engine/action.h`, `port/src/engine/action.c`, `port/src/engine/minion.h`, `port/src/engine/minion.c`, `port/src/engine/inventory.c` (`render_hand_icon`), `port/src/engine/magic.c` (`draw_in_screen_zone`)
- Modify: `port/src/ui/ui.c`, `port/src/ui/input.c`, `port/src/ui/automap.c`, `port/src/ui/session.c` (the callers)
- Modify: `port/src/engine/hooks.h`, `port/src/ui/hook_table.c`
- Modify: the tests that call the renamed functions (`grep -ln "action_update_area\|action_draw_\|minion_draw_map\|minion_map_draw_square_icon\|minion_map_party_icons_item" tests/*.py`)

**Interfaces:**
- Produces in `ui/action_area.h` (`/* Prefixes: area_ */`): the drawing functions of `action.c`: `begin_draw`, `draw_action_result`, `action_hand_icon`, `draw_hand_icon`, `draw_aura`, `face_bitmap`, `draw_party_icons`, `draw_spell_icon`, `draw_menu_header`, `draw_action_champion_face`, `draw_spell_panel`, `draw_action_name`, `draw_money_box`, `draw_container`, and the drawing part of `action_update_area`. Public names: `area_update(int tick)` (old `action_update_area`), `area_draw_entered_runes(int clear)`, `area_draw_name(int i, int highlight)`, `area_draw_hand_icon(int champ, int hand, int flash)`, `area_draw_spell_icon(int champ, int flash)`. The rest stay static.
- Produces in `ui/minion_map.h` (`/* Prefixes: mmap_ */`): the drawing functions of `minion.c` from `map_icon_strip` to `minion_draw_map`, with `mmap_draw(uint16_t thing)` (old `minion_draw_map`), `mmap_draw_option_buttons(int pressed)`, `mmap_draw_square_icon(Offscreen *target, const int16_t *clip, int sx, int sy, int x, int y, int party_item)`, `mmap_party_icons_item(void)`. `minion_map_click_set_destination` and the rules stay in `engine/minion.c`.
- `render_hand_icon` moves to `ui/ui.c` (static if only `ui.c` calls it after the move, else `ui_render_hand_icon` in `ui.h`).
- Produces in `g_hooks`: `void (*update_action_area)(int tick);`, `void (*draw_action_name)(int i, int highlight);`, `void (*draw_hand_icon)(int champ, int hand, int flash);`, `void (*draw_spell_icon)(int champ, int flash);`, `void (*draw_entered_runes)(int clear);`, `void (*draw_minion_map)(uint16_t thing);`, `void (*draw_map_option_buttons)(int pressed);`, `void (*draw_interface_image)(int category, int index, int entry, int zone);` (the flask picture of `magic_report_cast_failure`), and a member for each other call from `engine/` into the moved code that the build reports.
- `engine/action.h` and `engine/minion.h` do not name `Offscreen`.

- [ ] **Step 1: Split `action.c`**

Rule: a function that calls a drawing function of `ui/` (`offscreen_*`, `ui_blit_*`, `zone_*`, `draw_*`, `text_draw*`, `ui_draw_*`) moves to `ui/action_area.c`. A function that only changes `action` state stays. A function with both parts splits: the state part stays in `engine/action.c`, the drawing part moves. Keep the call order of the original inside a split function: the engine part calls the drawing part through a hook at the same place.

- [ ] **Step 2: Split `minion.c`**

Same rule for the minion map. `minion_map_click_set_destination` reads `zone_get_rect`: if it is the only `ui/` call left in `engine/minion.c`, add a hook `const int16_t *(*zone_rect)(int zone, int16_t *rect)` set to `zone_get_rect`.

- [ ] **Step 3: `inventory.c` and `magic.c`**

Move `render_hand_icon` to `ui/ui.c`. Replace `draw_in_screen_zone` in `magic.c` with `g_hooks.draw_interface_image(...)`, and define its target in `ui/ui.c` as `ui_draw_interface_image` with the body of `draw_in_screen_zone`.

- [ ] **Step 4: Hooks, callers, tests, allowlist**

Engine callers of the moved public functions call the hooks. `ui/` callers call the new names directly. Rename the calls in the tests. Remove the allowlist lines of `engine/action.c`, `engine/action.h`, `engine/minion.c`, `engine/minion.h`, `engine/inventory.c:draw.h`, `engine/magic.c:draw.h` that the build no longer needs.

- [ ] **Step 5: Build and test**

Run: `rm -f build/dm2 build/libdm2.dylib && make -C port 2>&1 | grep -c warning` → `0`.
Run: `uv run pytest -q -p no:xdist tests/test_standard.py tests/test_interface.py tests/test_automap.py` and the modules of the grep in Files.
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add -A port/src tests
git commit -m "refactor: action area and minion map drawing in ui"
```

### Task 8: Main loop in `ui/play`

**Files:**
- Create: `port/src/ui/play.h`, `port/src/ui/play.c`
- Modify: `port/src/engine/game.h`, `port/src/engine/game.c`
- Modify: `port/src/platform/main.c`, `port/src/ui/dialog.c`, `port/src/ui/input.c`, `port/src/ui/session.c`, `port/src/ui/ui.c`, `port/src/ui/advanced.c`, `port/src/engine/recruit.c` (the callers)
- Modify: `port/src/engine/hooks.h`, `port/src/ui/hook_table.c`
- Modify: the tests that call the renamed functions (`grep -ln "game_tick\|game_start_session\|game_hide_view\|game_show_view\|game_present_viewport\|game_frame_hook\|game_wait_frames\|game_sleep" tests/*.py`)
- Modify: `docs/skull-formats.md` (the modal loop text names `game_frame_hook` and `main.c`)

**Interfaces:**
- Produces in `ui/play.h` (`/* Prefixes: play_ */`): `extern void (*play_frame_hook)(void);`, `void play_wait_frames(int count)`, `void play_start_session(void)`, `void play_tick(void)`, `void play_show_view(void)`, `void play_hide_view(void)`, `void play_present_viewport(void)`, `void play_sleep(void)`. They are the old `game_frame_hook`, `game_wait_frames`, `game_start_session`, `game_tick`, `game_show_view`, `game_hide_view`, `game_present_viewport`, `game_sleep`, with the static `game_step`, `game_step_view`, `game_step_end`, `ui_draw_sleep_message` (rename it `draw_sleep_message`), and `tick_elapsed`.
- Stays in `engine/game.c`: `main_loop`, `game_enter_map`, `champ_wake_party`, `timeline_process` and its event helpers, the constants of `game.h` that they use.
- Produces in `g_hooks`: `void (*enter_map_view)(int map);` (set to `view_enter_map`), `void (*flush_input)(void);` (`input_flush`), `void (*poll_input)(void);` (`input_poll`), `void (*clear_viewport)(void);` (`view_clear_viewport`), `void (*set_input_mode)(int mode);` (`input_set_mode`), `void (*show_view)(void);`, `void (*hide_view)(void);`, `void (*present_viewport)(void);` (the `play_` functions, for `engine/recruit.c`).

- [ ] **Step 1: Move the main loop**

Move the functions listed under Produces to `ui/play.c` with their names. `game_step_view` still calls `game_enter_map`, `timeline_process`, `world_update_day_night`, `move_party`, `move_thing` of `engine/`: `ui/` may call them.

- [ ] **Step 2: Hooks in the rules of `game.c`**

`game_enter_map` calls `g_hooks.enter_map_view(map)` and `g_hooks.flush_input()`. `champ_wake_party` sets `input_command_stop` (engine state since Task 3) and calls `g_hooks.clear_viewport()` and `g_hooks.set_input_mode(INPUT_MODE_MOVE)`. `timeline_process` calls `g_hooks.poll_input()`. `INPUT_MODE_*` moves to `engine/game.h` if `engine/` still needs it; `ui/input.h` includes `game.h`.

- [ ] **Step 3: Callers and tests**

`platform/main.c` sets `play_frame_hook` and calls `play_tick`. `ui/` callers use the new names. `engine/recruit.c` calls `g_hooks.show_view()`, `g_hooks.hide_view()`, `g_hooks.present_viewport()`. Rename the calls and `in_dll(lib, "game_frame_hook")` in the tests. Update `docs/skull-formats.md`.

- [ ] **Step 4: Allowlist**

Remove every `engine/game.c:*` line of the allowlist. `engine/game.c` includes no `ui/` header.

- [ ] **Step 5: Build and test**

Run: `rm -f build/dm2 build/libdm2.dylib && make -C port 2>&1 | grep -c warning` → `0`.
Run: `uv run pytest -q -p no:xdist tests/test_standard.py tests/test_input.py tests/test_dialog.py tests/test_session_play.py tests/test_modes.py tests/test_control.py` and the modules of the grep in Files.
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add -A port/src tests docs
git commit -m "refactor: main loop in ui/play"
```

### Task 9: The other calls of the rules into the interface

**Files:**
- Modify: `port/src/engine/champ_action.c`, `port/src/engine/champion.c`, `port/src/engine/creature.c`, `port/src/engine/damage.c`, `port/src/engine/inventory.c`, `port/src/engine/magic.c`, `port/src/engine/move.c`, `port/src/engine/recruit.c`, `port/src/engine/sensor.c`, `port/src/engine/ai.c`, `port/src/engine/ai_move.c`, and every other `engine/` file with a `layer` line left in the allowlist
- Modify: `port/src/engine/hooks.h`, `port/src/ui/hook_table.c`
- Modify: `port/src/ui/ui.h`, `port/src/ui/ui.c`, `port/src/engine/champion.h`, `port/src/engine/champion.c` (`ui_set_champion_redraw_flags`)

**Interfaces:**
- Produces in `engine/champion.h`: `void champ_set_redraw_flags(uint16_t flags)` (old `ui_set_champion_redraw_flags`: it only sets flags on the champions). Rename its callers in `ui/` and the tests.
- Produces in `g_hooks`, one member per remaining call, set to the function of today: `refresh_input_mode` (`input_refresh_mode`), `set_wait` (`input_set_wait`), `set_champion_cursor` (`input_set_champion_cursor`), `toggle_inventory` (`ui_toggle_inventory`), `draw_champion_state` (`ui_draw_champion_state`), `draw_all_champion_states` (`ui_draw_all_champion_states`), `draw_movement_arrows` (`ui_draw_movement_arrows`), `fill_screen_zone` (`ui_fill_zone_screen`), `draw_viewport_image` (`ui_draw_gdat_in_viewport_zone`), `complete_game` (`session_complete`), `draw_dungeon` (`view_draw_dungeon`), and any further call that the build reports. Each member has the exact signature of its function.

- [ ] **Step 1: List the remaining violations**

Run: `grep "^layer engine" tests/standard_allowlist.txt` and, for each line, `grep -n` the symbols of that header in that file.

- [ ] **Step 2: Hook or flag per call**

Default: replace the call with the hook at the same place. Use a redraw flag only where the drawing pass of the same game step draws the same result before the screen shows, and the differential tests prove it; record each such case in the commit message.

- [ ] **Step 3: Remove the includes and the allowlist lines**

Remove every `#include` of a `ui/` header from `engine/` and every `layer` line from `tests/standard_allowlist.txt`.

- [ ] **Step 4: Build and test**

Run: `rm -f build/dm2 build/libdm2.dylib && make -C port 2>&1 | grep -c warning` → `0`.
Run: `grep -c "^layer" tests/standard_allowlist.txt` → `0`.
Run: `uv run pytest -q -p no:xdist tests/test_standard.py` and the modules of `grep -ln "<each renamed or hooked function>" tests/*.py`.
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add -A port/src tests
git commit -m "refactor: hooks for the calls of the rules into the interface"
```

### Task 10: Acceptance checks and docs

**Files:**
- Modify: `docs/skull-symbols.csv` (the renamed functions), `docs/skull-formats.md`, `docs/todo.md` (remove the `layer` item), `CHANGELOG.md` (one line under the next version: the layers of the source have no exceptions)

- [ ] **Step 1: Acceptance greps**

Run: `grep -nE "offscreen_|draw_ctx_|ui_blit_|text_draw_" port/src/engine/*.c` → no output.
Run: `grep -nE "Offscreen|DrawCtx" port/src/engine/*.h` → no output.
Run: `for f in port/src/engine/*.[ch]; do grep -H '#include "' "$f"; done | while IFS=: read f inc; do h=$(echo "$inc" | sed 's/.*"\(.*\)".*/\1/'); [ -f "port/src/ui/$h" ] && echo "$f $h"; done` → no output.

- [ ] **Step 2: Docs**

Rename each moved function in `docs/skull-symbols.csv` (find them with `git diff main --stat` and the `Produces` lists of Tasks 4 to 9). Remove the layer item from `docs/todo.md`. Add the changelog line.

- [ ] **Step 3: Test**

Run: `uv run pytest -q -p no:xdist tests/test_standard.py` (the symbol test reads `docs/skull-symbols.csv`).
Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add docs CHANGELOG.md
git commit -m "docs: layer cleanup names, todo, and changelog"
```
