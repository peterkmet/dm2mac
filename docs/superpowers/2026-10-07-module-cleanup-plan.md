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
- A task agent runs only the tests that cover its change: `uv run pytest tests/test_standard.py -q -p no:xdist` and the test modules that exercise its files. It does not run `tools/test-suite`. The main agent runs the full suite once, after it merges the work of the agents.
- Do not run the same tests again on unchanged code.
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

## Task 0b: Thing record accessors (one agent, before the parallel tasks) — done

Rule 2: field access of thing records goes through small named functions or macros in `engine/dungeon.h`. Today the call sites use `thing_word(thing, FIELD)` and `set_thing_word(...)` with masks and shifts, about 400 calls. The modules define the record bits again under different names, for example bit `0x0100` of `DOOR_DATA` is `DOOR_BURNABLE` in `ai_move.c` and `DOOR_BREAKABLE` in `sensor.c`.

- Make a list of every record field and bit that a module uses, per thing type (door, teleporter, text, sensor, group, weapon, armour, scroll, potion, container, misc, projectile, explosion). Find the game meaning in the code that uses it, `docs/skull-formats.md`, and `docs/skull-symbols.csv`. When two modules give one bit two names, read both uses and pick the name of the real meaning.
- Add the bit constants and the accessors to `engine/dungeon.h`, for example `door_has_button(t)`, `door_set_button_pressed(t, pressed)`, `sensor_type(t)`, `group_dir(t)`. Predicates return `bool`. Keep the accessors short and `static inline` or macros.
- Replace the call sites in all files. Remove the local definitions of the record bits.
- Out of scope: other literals, long functions, timeline events. The parallel tasks do them.
- Add a unit test for each non-trivial accessor (a packed field with a shift), or state in the report why the differential tests cover it.

## Parallel tasks (one agent each, own worktree)

Thing record fields and bits have accessors in `engine/dungeon.h` (task 0b). Use them. Do not define record bits again.

Extra items per task, from the task 0b report:
- Task 3: the sub-fields of sensor data get accessors in `engine/sensor.h`: sensor 0x16 (map `& 0x3f`, cell `>> 6 & 3`), the wall teleport click (`data & 0x3f`), the creature turn sensor (`& 3`).
- Task 4: `DOOR_SKILL_BASH` (0x4) and `DOOR_SKILL_BURN` (0x8) in `engine/ai_move.h` look swapped: BASH selects fireball and lightning and tests `DOOR_BREAKABLE_BY_MAGIC`, BURN selects physical hits and tests `DOOR_BREAKABLE`. Read the uses and rename them to the real meaning.
- Task 7: the local `LOCATION_*` macros in `engine/minion.c` use `location_x()`, `location_y()`, `location_map()`, and `location_pack()` of `engine/dungeon.h`.
- Task 9: named list-head accessors (for example `group_possessions(g)`, `container_contents(c)`) instead of `thing_list(x, FIELD)` at the call sites.
- Separate fix after task 9: `void *layout_padding` in `AiContext` (`engine/ai.h`). Without it, and with the offsets of `tests/test_ai_move.py` shifted to the real `offsetof` values, 3 tests fail (`test_try_move_changes_level`, `test_door_spells_on_the_way`, `test_merchant_commands`). Find the root cause with systematic debugging.
- Task 9 (from task 8): shared names for the bits per pixel `4` and `8` (`Bitmap.bpp`, `gfx_blit`) in `data/gdat.h`; `tests/test_automap.py` sets `restype = ctypes.c_bool` for `automap_is_seen`; the keyboard scan code tables in `platform/control.c` and `platform/main.c` stay tables.
- Task 9: rename `timeline_event_clear_thing_flag8` in `docs/skull-symbols.csv` to the name of `EVENT_DOOR_BUTTON_RELEASE`.

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

## Task 9 inputs (from the reports of tasks 1 to 8)

**Move into the owner header (one definition, one name):**
- `engine/dungeon.h`: `DOOR_STATE_*` and `DOOR_CLOSED`/`DOOR_OPEN`/`DOOR_DESTROYED` copies (view.c, automap.c, champ_action.c, sensor.c, move.c, creature.c, ai_move.c), `MAP_RANDOM_WALL_SHIFT`, `MAP_SIDE_MAX`, `CELL_COUNT`, `TELEPORTER_SCOPE_CREATURES`, `TURN_LEFT`/`TURN_RIGHT`, and `DIR_MASK`, `dir_right`, `dir_back`, `dir_left`, `thing_at_cell`, `event_square_dir` (now in `sensor.h`). Use `DIR_MASK` for the `& 3` direction wrap.
- `engine/dungeon.h` accessors: `dungeon_map_door_type(map, slot)`, the random wall ornament count of a map, the creature type count of a map; replace the local helpers `map_door_type`, `map_random_wall_count`, `cell_of_thing`, `thing_in_cell_of` (view.c), `creature_group_thing`, `move_thing_cell`, `move_set_cell`, `map_creature_type_count` with `thing_cell`/`thing_with_cell` or new accessors.
- `engine/creature.h`: `ALERT_STARTLED`, `CREATURE_DOOR_SIZE_MASK`, `DOOR_HEIGHT_MASK`, `ANIM_MORE_STEPS`, `ANIM_SEQUENCE_END`, `POSSESSION_SLOT_MASK`, accessors for the nibbles of `CreatureInfo.speed` and the armour in `door_flags` (local macros in ai.c). Pick one name for door-flags bit 0x10 (`CREATURE_RESISTS_NON_FIRE` or `CREATURE_DOOR_HALF_DAMAGE`).
- `engine/sound_request.h`: one name for 0xfe (`SWALLOW_FALLBACK_INDEX`, `SOUND_FALLBACK`, `SOUND_ANY`), `SOUND_HURT`, `SOUND_PRIORITY_HURT`, `SOUND_VOLUME_NORMAL`, `SOUND_VOLUME_LOUD`, `SOUND_VOLUME`, `SOUND_VOLUME_MAX`, the click sound constants of input.c; one name each for `SOUND_ENTRY_DRINK`/`SOUND_SWALLOW`, `SOUND_PRIORITY_CRUSH`/`SOUND_PRIORITY_STEP`.
- `engine/champion.h`: `ATTACK_TYPE_SELF`/`ATTACK_SELF`/`DOOR_ATTACK_TYPE`, `BUMP_WOUNDS`, `FALL_WOUNDS`, `DISPLACE_WOUNDS`, `DOOR_WOUNDS`/`WOUND_HEAD`, `LAUNCH_COOLDOWN`/`THROW_COOLDOWN`, names for `Champion.unknown_26[2]` and `[3]` (local `CHAMP_HIT_DIR`, `CHAMP_HIT_DAMAGE` in ai.c), the push and fall sounds now in `creature.h`. Check `SLOT_BODY_FIRST`, `SLOT_BODY_LAST`, `SLOT_BODY_EXTRA` against the slot uses.
- `engine/item.h`: `ITEM_ATTR_POISON` as `ATTR_POISON`, `ACTION_TEXT_FIRST`/`ACTION_ENTRY_FIRST`.
- `engine/dungeon.h` text subtypes: `ORNAMENT_ITEM_*` (item.c) and `SUBTYPE_*` (view.c).
- `engine/sensor.h` names in other modules: `GLOBAL_VAR_*` (weather.c, champ_action.c, magic.c), `SENSOR_KIND_VANISH`/`SENSOR_KIND_ALERT` (creature.c), `sensor_turn_dir` (creature.c), `SENSOR_EVENT_THING` (input.c), `TELEPORT_EVENT_*`, `SPAWN_EVENT_CREATURE` (game.c), `ALTAR_EVENT_STEPS` (damage.c), `SENSOR_NONE`.
- `engine/save.h`: `save_magic_light()` in `magic.c` and `ai_move.c`; `TIMELINE_EVENT_SIZE`/`EVENT_SIZE`.
- `engine/projectile.h`: `projectile_dir()` (view.c local `missile_dir`); one name for the event direction word (`EVENT_MISSILE_DIR`/`MISSILE_DIR_SHIFT` in minion.c, `EVENT_DIR_WORD`/`EVENT_DIR_SHIFT` in view.c).
- `engine/ai_move.h`: `CANDIDATE_MELEE_LINE`, `CANDIDATE_SHOOT_LINE` and the static `TARGET_*` enum of ai_move.c.
- `ui/input.h`: `CMD_VOLUME_SETTING_*` (dialog.c copies), `CMD_NAME_EDITOR_LAST` (session.c copies); `ui/dialog.h`: `CMD_DIALOG_BUTTON_LAST`; `KEY_ENTER`, `KEY_TAB`, `KEY_BACKSPACE`, `INPUT_MODE_*`, `GDAT_TYPE_VALUE` copies in dialog.c and session.c.
- `data/gdat.h`: `GDAT_TYPE_REMAP`, one GDAT category name for `CAT_MISC_SOUND` (input.c, creature.c, ai.c), `CAT_MAP_SOUND`/`CAT_DUNGEON`, `CAT_ORNAMENT`; names for the bits per pixel 4 and 8.
- `ui/view.h`: `DARKEST_LIGHT`; one name for the 5×5 cell grid (`GRID5_*` in move.c, `CELL_GRID_SIDE`/`CELL_CENTER` in view.c); `DISTANCE_MAP_SIDE` -> `ai.h`.
- One shared `PERCENT` (merchant.c, weather.c, money.c, creature.c, ai.c, view.c, zone.c `PERCENT_FULL`).
- `dungeon_map_wallset()` in `move.c`, `ai.c` (still `>> 4 & 0xf`).
- `recruit.c:50` writes 0xff: use `COMMAND_NONE`.
- `ai.h`: `creature_is_near_party_front` returns `bool` like its definition; remove its `bool` allowlist line.

**Names that do not fit their use (review of tasks 1, 2, 8):**
- `ui/input.c`: `NODE_IF_ACTION_LIST` is the minion map layout condition (node 50 under `NODE_IF_ACTION_MODE` with `ACTION_MODE_MINION_MAP`); rename, for example `NODE_IF_MINION_MAP_LAYOUT`.
- `engine/creature.h`: `POSSESSIONS_HIDE_CLICKS` (0x2000) means the objects under the creature stay clickable and a drop goes into the front cells; rename, for example `POSSESSIONS_FLOOR_CLICKABLE`.
- `ui/view.c`: the comment of `CLICK_HALF_SIZE` covers only the width; the code uses it for the height too.
- `ui/input.c`: `EVENT_CLICK_TYPES` 0x13 is `INPUT_BUTTON_RIGHT | INPUT_BUTTON_LEFT |` a named bit 0x10.
- `ui/input.c`: `NODE_INDEX_MASK` as the node condition is `~NODE_HAS_CHILDREN`.
- `ui/session.c:350`: a `dungeon.h` accessor for the party start direction instead of `location_map(start) & (DIR_COUNT - 1)`.
- `ui/view.c:3067`: `memset(&s, ORNAMENT_NONE, sizeof s)` fills every byte; say so or set the fields.
- `ui/input.c`: `DIR_COUNT` used as the cell count (use `CELL_COUNT`), `HALF_CELLS` as the possession cell offset (use `CELL_POSSESSION_FIRST`).
- `ui/zone.c`: one comment per define for `LAYOUT_RANGE_COUNT` and `LAYOUT_RANGES`.
- `ui/input.c`: `POS_PARTY_SQUARE`, `POS_FRONT_SQUARE` repeat `POS_PARTY`, `POS_FRONT` of view.c -> `view.h`.
- One-line helpers with one call site: `face_word`, `map_random_wall_count`, `thing_in_cell_of`, `missile_dir` in view.c; inline them or replace them with owner accessors.
- `BYTE_BITS`, `BYTE_MASK`, `NIBBLE_*`, `DECIMAL_BASE` move from `data/gdat.h` to `data/le.h` (control.c, hmp.c, fm.c include gdat.h only for them); rename `le.h` if its name no longer fits.
- `engine/move.c:550` `party.push_arrow = 6` -> `PUSH_ARROW_NONE`.

**Tests:** `restype = ctypes.c_bool` for the `bool` functions that tests call through ctypes (`automap_is_seen`, `item_is_moneybox`, `item_is_chest`, `item_is_minion_map`, the line tests of ai_move.c).

**Flaky:** `tests/test_input.py::test_sleep_and_wake_match_original` failed 3 of 12 parallel runs in the task 1 worktree, once already on its first commit; serial runs pass. Debug it as a separate issue, like the `AiContext` padding.
