# Module cleanup: plan

> **For agentic workers:** each task is self-contained. Read `docs/coding-standard.md` and this plan before you start. Work only in the files your task owns.

**Goal:** remove the `open: module cleanup` entries of `tests/standard_allowlist.txt`. Remove every violation of these types, not only the listed examples:

1. **Unnamed constants.** Hex and decimal literals, and small numbers that have a game meaning (for example a record word index `2` instead of `DOOR_DATA`). Rule 1.
2. **Hand-built packed fields.** Shifts, masks, and double casts at the call site that pack or unpack a field (for example the time and the map in `TimelineEvent.time_map`, a 16-bit word built from two bytes). Rule 10.
3. **One constant defined in more than one file.** Rule 1, last item. Task 9.

Plus the other open checks of a file: `long`, `bool`, `record`. The `prefix` and `duplicate` entries rename or move names across files, so task 9 does them.

**Order:** task 0, task 0b, tasks 1 to 8 in parallel, task 9.

**Out of scope:** the `layer` entries (`engine/` includes of `ui/` headers). They need a design for hooks or redraw flags. They stay in the allowlist and in `docs/todo.md`.

## Global constraints

- The behavior does not change. The differential tests prove it. Do not change a test to make it pass.
- `tools/test-suite` must pass before a task is done. `uv run pytest tests/test_standard.py -q -p no:xdist` must pass.
- `rm -f build/dm2 build/libdm2.dylib && make -C port 2>&1 | grep -c warning` gives 0.
- Preserve existing comments. New comments are one line and tell the game meaning.
- A table copied from `SKULL.EXE` (rule 6) keeps its literals. Give the table a one-line comment and an allowlist line with the reason `table: rule 6`.
- A constant goes into the header of the module that owns it. If the owner is a module of another task, define the constant in your own file and list it in your report as `move: NAME -> owner.h`. Task 9 moves it.
- Remove the allowlist lines of your files that you fixed. Do not add `open:` lines. A violation that must stay gets a line with a real reason.
- Commit at the end of the task: `refactor: <files> coding standard cleanup`.

## Task 0: Rule 10 and its check (main agent, before the parallel tasks)

- Add rule 10 to `docs/coding-standard.md`: packed fields have accessor functions or macros in the header of the module that owns the field. Call sites do not shift, mask, or cast to pack or unpack them.
- Add `test_no_packed_fields_at_call_sites` to `tests/test_standard.py`: shifts by 8, 16, or 24 and double integer casts in `.c` files, outside `le.h` and the accessor headers. Fill the allowlist with `open: module cleanup` lines.
- Add the shared accessors of the timeline event in `engine/timeline.h`: `EVENT_TIME_MASK`, `EVENT_MAP_SHIFT`, `timeline_time_map(time, map)`, `timeline_event_map(e)`, `timeline_set_event_map(e, map)`. The parallel tasks replace the call sites in their files.
- Update task 4 in `docs/todo.md` with the three types.

## Task 0b: Thing record accessors (one agent, before the parallel tasks)

Rule 2: field access of thing records goes through small named functions or macros in `engine/dungeon.h`. Today the call sites use `thing_word(thing, FIELD)` and `set_thing_word(...)` with masks and shifts, about 400 calls. The modules define the record bits again under different names, for example bit `0x0100` of `DOOR_DATA` is `DOOR_BURNABLE` in `ai_move.c` and `DOOR_BREAKABLE` in `sensor.c`.

- Make a list of every record field and bit that a module uses, per thing type (door, teleporter, text, sensor, group, weapon, armour, scroll, potion, container, misc, projectile, explosion). Find the game meaning in the code that uses it, `docs/skull-formats.md`, and `docs/skull-symbols.csv`. When two modules give one bit two names, read both uses and pick the name of the real meaning.
- Add the bit constants and the accessors to `engine/dungeon.h`, for example `door_has_button(t)`, `door_set_button_pressed(t, pressed)`, `sensor_type(t)`, `group_dir(t)`. Predicates return `bool`. Keep the accessors short and `static inline` or macros.
- Replace the call sites in all files. Remove the local definitions of the record bits.
- Out of scope: other literals, long functions, timeline events. The parallel tasks do them.
- Add a unit test for each non-trivial accessor (a packed field with a shift), or state in the report why the differential tests cover it.

## Parallel tasks (one agent each, own worktree)

| Task | Files |
|---|---|
| 1 | `ui/view.c`, `ui/view.h` |
| 2 | `ui/input.c`, `ui/input.h`, `ui/ui.c`, `ui/ui.h` |
| 3 | `engine/sensor.c`, `engine/sensor.h`, `engine/projectile.c`, `engine/projectile.h` |
| 4 | `engine/ai_move.c`, `engine/ai_move.h`, `engine/ai.c`, `engine/ai.h`, `engine/ai_data.c` |
| 5 | `engine/creature.c`, `engine/creature.h`, `engine/move.c`, `engine/move.h`, `engine/magic.c`, `engine/magic.h` |
| 6 | `engine/save.*`, `engine/champ_action.*`, `engine/action.*`, `engine/damage.*`, `engine/item.*`, `engine/inventory.*`, `engine/champion.*` |
| 7 | the other `engine/` files: `dungeon.*`, `weather.*`, `minion.*`, `game.*`, `merchant.*`, `recruit.c`, `money.*`, `timeline.*`, `sound_request.*`, `random.*`, `hooks.*` |
| 8 | `data/*`, `platform/*`, and the other `ui/` files (not `opl3.*`) |

## Task 9: Shared constants and prefixes (main agent, after the merge of tasks 1 to 8)

- Move each `move:` constant of the reports into its owner header.
- Remove the `duplicate` and `prefix` entries: one definition per constant, a `Prefixes` line in each header.
- Run the full suite and the reviewer.
