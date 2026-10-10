# Layer cleanup: PRD

## Goal

Remove all `layer` entries of `tests/standard_allowlist.txt`. On 2026-10-09 the allowlist has 77 of them. After the change, no file in `port/src/engine/` includes a header of `port/src/ui/`, and coding standard rule 8 holds without allowlist entries. The behavior of the game does not change.

## Reader

The developer and the agents that write the implementation plan and the code.

## Scope

In scope: every `layer` entry of the allowlist, in all four groups below.

Out of scope:

- New features and changes of behavior.
- The other open items of `docs/todo.md`: the redraw bit `0x10`, the flaky control test, names without a verb.
- The `g_` prefix for old global variables, also for the ones that this change moves. A moved global keeps its name, because the tests read it with `in_dll`.

## Acceptance

```gherkin
Feature: Layer cleanup
  Scenario: engine does not include ui
    Given the tree after the change
    When tests/test_standard.py::test_layers runs with an allowlist that has no "layer" lines
    Then the test passes
    And make -C port gives 0 warnings
    And tools/test-suite passes with no weakened assertion

  Scenario: no drawing code in engine
    Given the tree after the change
    When grep searches port/src/engine/*.c for calls of "offscreen_", "draw_ctx_", "ui_blit_", "text_draw_"
    Then it finds no line
```

## Invariants

1. No `#include` in `port/src/engine/` resolves to a file in `port/src/ui/`.
2. Every member of `g_hooks` is non-NULL in `build/libdm2.dylib` and in `build/dm2`.
3. No header in `port/src/engine/` names `Offscreen` or `DrawCtx`.
4. A save that the port writes from one game state is byte-identical before and after the change.

## Current state

The table shows the parts of `ui/` that `engine/` uses. The plan classifies each symbol again from the code. This table is the starting point.

| Group | Symbols | Where |
|---|---|---|
| 1. Data and state in the wrong layer | `draw_gdat()` and `draw_init` in `save.c`; `text_gdat`, `text_gdat_field`, `text_gdat_label`, `text_decode_dungeon`, `text_args`, `text_format`, `TEXT_SIZE`; the `UI_*` color index names (hook arguments); the whole `UiState ui`; `view_light_level`, `DARKEST_LIGHT`; `input_drag_state`, `input_mouse_buttons`; `session.new_game`, `session.completed`, `session.loaded`, `session.offer_restore`; `game_mode`, `cheat_immortal`, `cheat_food`; the seen squares and the save block of the automap (`automap_mark_seen`, `automap_read`, `automap_write`); `Sound`, `sound_load`, `sound_party_offset`, `sound_volume_pan`; `view_get_square_aspect`, `ViewSquare`, `ASPECT_*`, `view_get_creature_cell_zone`, `view_grid5_cell_to_xy`, `cell_grid_index`, `CELL_GRID_*`, `view_wall_face_ornament`; the sky object list of `weather.c` (`SkyObject`, `view_sky_objects`, the picture check of `view_load_sky_object`); `zone_interpolate_offset` | almost all `engine/` files |
| 2. Drawing code in `engine/` | the action area, spell panel, money box, and container drawing (`action.c`: `begin_draw` to `draw_container`, `action_hand_icon`, `action_draw_entered_runes`, the drawing part of `action_update_area`); the minion map drawing (`minion.c`: `map_icon_strip` to `minion_draw_map`, without `minion_map_click_set_destination`); the hand icon of `inventory.c` (`render_hand_icon`); the flask picture of a failed cast (`magic.c`: `draw_in_screen_zone`); `action.h` and `minion.h` use `Offscreen` | `action.c`, `minion.c`, `inventory.c`, `magic.c` |
| 3. The main loop in `engine/game.c` | `game_tick`, `game_step`, `game_start_session`, `game_show_view`, `game_hide_view`, `game_present_viewport`, `ui_draw_sleep_message`, `game_step_view`, `game_step_end`, `game_wait_frames`, `game_frame_hook`, `advanced_reset`, `advanced_automap_open`: they call `input_poll`, `command_process_queue`, `dialog_run`, `view_draw_dungeon`, `music_update_for_map`, the automap, and the colors and viewport size of the screen | `game.c` |
| 4. Rules that call the user interface | `text_print_message`, `text_print_newline`; `input_flush`, `input_command_stop`, `input_refresh_mode`, `input_set_mode`, `input_set_wait`, `input_set_champion_cursor`, `input_poll`; `ui_toggle_inventory`, `ui_draw_champion_state`, `ui_draw_all_champion_states`, `ui_draw_movement_arrows`, `ui_set_champion_redraw_flags`, `ui_fill_zone_screen`, `ui_draw_gdat_in_viewport_zone`; `view_clear_viewport`, `view_draw_dungeon`, `view_enter_map`; `session_complete`; `sound_play`. In `game.c`: `game_enter_map` (`view_enter_map`, `input_flush`), `champ_wake_party` (`view_clear_viewport`, `input_set_mode`, `input_command_stop`), `game_sleep` (`ui_toggle_inventory`, `input_set_mode`, `input_flush`), `timeline_process` (`input_poll`) | `champ_action.c`, `champion.c`, `creature.c`, `damage.c`, `game.c`, `inventory.c`, `magic.c`, `move.c`, `recruit.c`, `sensor.c`, `sound_request.c` |

## Requirements

1. **Group 1: move down.** Each symbol moves to the lowest layer that has all its dependencies, into the module that owns its meaning. The whole `UiState ui` becomes engine state, with `input_drag_state`. `ui/` reads it from there. Game options that the rules read (`game_mode`, the cheats) move to `engine/`. The session fields that the rules read move to `engine/`. The session file handling (load, save, menus) stays in `ui/`. The sky object list becomes engine state: `weather.c` checks in GDAT that a sky picture exists, and `view.c` decodes the picture when it draws.
2. **Group 2: move up.** Drawing functions move to `ui/`, into an existing module or a new one. The rules part stays in `engine/`. An engine header does not use `Offscreen` or other `ui/` types.
3. **Group 3: split the main loop.** The rules part of a game step (timeline, champions, creatures, movement) stays in `engine/game.c`. The parts that poll input, run dialogs, draw the view, play music, and wait for frames move to the new module `ui/play.c` and `ui/play.h` with the prefix `play_`. `game_frame_hook` moves with `game_wait_frames` into `ui/play.c`. `main_loop` stays in `engine/game.h`, because the rules read `end`, `running`, and `view_hidden`. `platform/main.c` calls `ui/play.h`.
4. **Group 4: hooks or redraw flags.** Default: a hook in the same place as the call today. A redraw flag only where the drawing pass of the same game step draws the same result before the screen shows, and the differential tests prove it. Each hook is a function pointer in `g_hooks`.
5. **Names.** A moved function takes a prefix of its new header (rule 7). Tests, `docs/skull-symbols.csv`, `docs/skull-formats.md`, and the other docs follow the new names.
6. **Allowlist.** The plan removes all `layer` lines. The plan adds no line of another type in their place, unless the line has a real reason.

## Decisions

| Topic | Decision | Reason |
|---|---|---|
| Scope | All four groups in one PRD and one plan. | User decision, 2026-10-09. |
| Hook form | `engine/hooks.h` declares one struct of function pointers, `g_hooks`. A `ui/` file defines `g_hooks` with a static initializer, so the program and the test library have the hooks without an init call. A test can replace one pointer. | Rule 8: "a hook that `ui/` sets". The tests use many different init sequences. A static initializer needs none of them. |
| Hook or flag | Default: a hook in the same place as the call today. A redraw flag only where the drawing pass of the same game step draws the same result before the screen shows, and the differential tests prove it. | Keeps the order of drawing, messages, and input of the original. |
| Hook calls | A hook call does not check for `NULL`. The static initializer sets every pointer. | One definition, no dead branch. |
| `engine/hooks.c` | `ui/input.c` calls the empty stub `command_execute_later`. The stub moves into `ui/input.c` as a static function. The plan deletes `engine/hooks.c`. | The call goes from `ui/` into `engine/`; a hook is for the other direction. |
| `UiState` | The whole struct moves to `engine/`, not only the 5 fields that the rules read. | One struct and one global. The ctypes mirror `UiState` keeps its layout. |
| Main loop module | `ui/play.c`, `ui/play.h`, prefix `play_`. `game_frame_hook` moves there and is not a member of `g_hooks`. | After the split, only `ui/` calls `game_wait_frames`, so its frame hook is not an engine hook. |
| Sky objects | Engine state; the picture check reads GDAT in `engine/`; `view.c` decodes the pictures. | The lightning roll, the thunder sound, and the sky picture choice are one rule. |
| Header names | A header name is unique over all layers. The layer test maps a header name to one folder. A module split in two layers takes two names and two prefixes, and neither prefix starts with the other (rule 7). Example: the seen squares in `engine/` and the automap drawing in `ui/`. | `tests/test_standard.py::test_layers` and `test_headers_state_unique_prefixes`. |
| Save format | No change. The automap block stays after the original data in the advanced mode. | Out of scope: behavior does not change. |
| Tests | No new tests for moves and renames. The differential tests and `test_standard.py` cover them. A test changes only for a new name, a new import path, or a moved struct field: the ctypes mirrors `Session`, `UiState`, and `MainLoop` follow the new layouts. `MainLoop` keeps its field order. A hook gets no unit test. | CLAUDE.md §2 test list: wiring is not on it. |
| Order | Group 1 first, then group 2, then group 3, then group 4. | Groups 2 to 4 need the state of group 1 in `engine/`. |
| Docs | `docs/coding-standard.md` rule 8 names `g_hooks` as the hook form. `docs/skull-formats.md` and `docs/skull-symbols.csv` follow the new names. The plan removes the `layer` item from `docs/todo.md`. | One source of truth for the hook form. |
| Hooks without a caller | A hook exists only where an `engine/` caller remains. The drawing functions that only `ui/` calls after the split get no member. | A member without a caller is dead wiring. Ruling of the run, 2026-10-09. |
| Random wall ornaments | `engine/aspect.c` keeps the ornament list of the map that `view_enter_map` entered last; `view_enter_map` sets it with `aspect_enter_map`. | The original reads one global list; reading the list of the map argument changes linked squares and the minion map. Kept quirk in `docs/skull-formats.md`. |
| Shared aspect helpers | `aspect_first_thing`, `aspect_value_key`, and the constants that `view.c` and `aspect.c` both use have one definition in `engine/aspect.h`. | One definition per item. |
| Allowlist table lines | A `hex` or `decimal` table line moves with its table to the new file. | The same exception for the same table, not a new one. |
| Action area | `area_update` and `draw_hand_panel` in `ui/action_area.c` still write some rule state. A split is an item in `docs/todo.md`. | The plan moved the whole `action_update_area`; the interleaved order needs many hooks. Behavior does not change. |
| Zone constants | A zone that a rule passes to a hook is a named constant in the engine header of that rule (coding standard rule 1). | One definition; `engine/` does not include `ui/ui.h`. |
| AI move without a direction | `engine/ai_move.c` maps direction -1 to north before it reads the step tables. | The original reads out of bounds; north is the step it reads. A data layout change of the cleanup exposed the read. Bounds fix in `docs/skull-formats.md`. |

## Known consequences

- The ctypes mirrors of `Session`, `UiState`, and `MainLoop` in the tests change with the moved fields.
- Rows of `docs/skull-symbols.csv` get new names.
- `engine/hooks.h` loses its old comment ("systems of later steps") and its includes of `sound_request.h`, `timeline.h`, and `projectile.h`. `engine/hooks.c` disappears.
- `platform/main.c` loses most of its `ui/` includes.
- `docs/skull-formats.md` names `game_frame_hook` and `main.c` for the modal loop; the text follows `ui/play.c`.
