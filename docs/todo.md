# TODO

## Manual checks

- Manual test D15: take the bones of a dead champion to an altar (needs long play).
- Manual test J16: complete the final task and check the end of the game (needs long play).
- Manual test H6: an animated wall ornament with a sound (only on levels 2 and 4).
- Manual tests: the sound direction next to creatures and doors, and the end of the game with the end sensor. The music and the thunder sound are not checked (dummy audio).
- Manual check of the walk-through-walls cheat (`docs/manual-test-checklist.md`): walk into a closed door, look around, open and close it with its button, walk out; walk into a wall (view, automap position, attack of a creature from a neighbor square), try to drop an item, step out, switch the cheat off.

## Checks against the original

- Message area scroll: IBMIOP service `0x105` is not in `SKULL.EXE`. The port moves the rows up and keeps the bottom rows (`shift_message_area` in `src/ui/text.c`); check it against the original in DOSBox.
- Check the driver offset of "Instrument to voice" in `HMIMDRV.386`: `docs/formats/music.md` has `0x1ff8` and `0x201e`.
- Check in `SKULL.EXE` whether the key table rows with language nibble 0 load for every language (`docs/reference/skull-map.md`, "GDAT database in memory").
- Automap visibility (`visible_depths` in `engine/seen.c`): a fixed cut by light level; a finer rule if a player needs it.
- Scroll color: a scroll is white on a table and yellowish in the hand, in the inventory, and while it is read. Compare with the original.
- Dark dots in letters: with `--filter scale2x` and `scale3x`, the "D" of READY and the "O" of TO in "GAME LOADED, READY TO PLAY" have a dark dot; the "O" of OK has none. Compare with `--filter none` to find out if the filter or the background causes it.
- End picture: after QUIT the end picture shows until a click and goes immediately, with no fade in or fade out (the port `dm` fades). Check if `SKULL.EXE` fades it.
- Sound under Wine: `build/dm2.exe` under Wine CX crackles, the music too; the port `dm` crackles the same way. Not checked on real Windows.
- Esc (the pause) cancels a champion drag, a right click does not.
- Cheat screen spell names that are port guesses: HASTE (OH IR ROS), MISSILE WALL (ZO BRO ROS), YA EW ITEM, SCOUT, CARRIER, ARMY MINION (ZO EW KU/NETA/ROS). Check them against the DM2 manual.
- Creature names in the control replies: `state_json_creature_name` writes the creature type number, because the game data has no creature names; a name table needs names from the manual.
- Item handling (proposal): while the leader hand holds an item, a click on the name of a champion opens the inventory of that champion, and the item stays in the leader hand. Check first what the click does in the original and in the port, and decide if the change is an advanced-mode feature.

## Code style

- Names without a verb (rule 4.1): `dialog_volume`. `sensor_door_value` reads the GDAT value of a door type, not a sensor value.
- `gdat_find` (`data/gdat.c`) scans the 11854 key rows linearly; sort or index the rows if lookups get hot.
- `long` in `src/control/control.c` and `src/control/turn.c` (38 uses, rule 5.3).
- Naming: `advanced_screen_event` passes a key code in `x` for `INPUT_KEY` (rule 6.4).
- Types and interfaces: the zone-id locals of `ui/view.c`, `ui/view_things.c` and `ui/view_walls.c` (`PicturePlace.zone`, `int zone` locals) and the `int zone` parameters of the hooks `fill_screen_zone` and `draw_viewport_image` (zone ids are `uint16_t` elsewhere; two `int16_t` locals use `< 0` for none). `start_tracks`, `outdoor_draw_sky` and `outdoor_draw_sky_object` still take (x, y); `recruit_start_champion` keeps (x, y, dir, map). The cast `(int16_t)sensor_data(link)` in `engine/ai_move.c` stays while `sensor_data` returns `uint16_t`. Callers in `ai_action.c:831,856` and `ai_move.c:573,635` store `move_nearest_champion_at` into `int`; `AiSquareScan.unused` is dead.
- Style items for new checks. Each item is a candidate check of `tests/standard/test_standard.py`. The line numbers can be out of date; grep for the sites before the work.
  - A hand-written clamp beside `clamp_int`/`min_int` (`data/include/num.h`): `engine/game.c:278`, `engine/item.c:356,398`, `ui/music.c:152`, `ui/sound.c:37,82-85`. The same form is also in `ui/zone.c:432`, `ui/text.c:240`, `engine/ai_action.c:633`.
  - `abs(` where `abs_int` exists, and an unused C header: `abs(` is in `ui/input_feedback.c:603-610`, `ui/view.c:287,292,339`, `ui/view_things.c:254,255` and `ui/view_walls.c:613,668`; the unused `#include <stdlib.h>` is at `engine/champion_damage.c:4` and `data/gfx.c:3`.
  - `0`/`1` assigned to a `bool` field or passed to a `bool` setter: `engine/champion.c:376`, `platform/main.c:262,263,686,692`.
  - `&` beside `||`, `&&` or `?` without parentheses: `ui/input_feedback.c:174-176`, `ui/minion_map.c:203,205,240,360,478`, `ui/ui.c:1349`.
  - `++`/`--` inside a call argument: `ui/input_tree.c:982`, `ui/input_feedback.c:327-341`.
  - A `#define` that only renames another constant (rule 3.5): `engine/include/aspect.h:63`, `ui/view_internal.h:176,177`, `control/include/state_json.h:18`.
  - A raw record write outside its accessor: `time_map++` outside `timeline.h` at `engine/sensor.c:1362,1465,1474,1557`, `engine/timeline.c:285`, `engine/projectile.c:870,970`; `thing_set_word(…, THING_NEXT, THING_NONE)` outside `dungeon.c`, 18 sites in `ai_action.c`, `ai_move.c`, `champion_action.c`, `champion_inventory.c`, `creature.c`, `money.c`, `projectile.c`, `save.c`, `sensor.c`, `timeline.c`.
  - A hand-written hand-slot test beside `action_is_hand_slot`: `engine/item.c:509`, `engine/champion_inventory.c:107,265`.
  - One section comment form per file: `engine/move.c` mixes `/* ---- X ---- */` (lines 184, 274, 332, 714, 1355) with `/* X. */` (lines 1507, 1540, 1596, 1631, 1662).
  - A `CMD_` block out of value order: `ui/include/input.h:23-85`.
  - A header that includes `gdat.h` only for `const Gdat *`: `ui/include/icon.h:10`, `ui/include/zone.h:12`.
  - Engine setters for the writes of `ui/minion_map.c` and `ui/ui.c:697-698` (`g_leader_hand.cursor`) into engine globals, so that their `rule 1.5` lines can go.

## Docs

- `docs/reference/reference-crosscheck.md`: the 1405 reDM2 function starts do not equal the table sum 473 + 151 + 27 + 677 + 79 = 1407. Check the count against the reDM2 data.
- Docs vocabulary: one name per concept across the docs. Object, item, and thing name the same game things in `docs/rules/champions.md`, `docs/rules/creatures.md`, and `docs/llm/llm-playbook.md`; tick, game step, and game time name the same time unit in `docs/llm/llm-playbook.md`. Define each term once (a thing is any dungeon record, an object or item what the party can carry; a timer tick differs from a game step), use it in prose, and keep the other words only in code names such as `take_object` and `move_thing`.

## Flaky tests

- Flaky test: `tests/port/test_modes.py::test_program_starts_in_advanced_unless_vanilla[advanced]` once got `{'error': 'busy'}` from the control socket in a parallel run.
- A one-off failure in a run of `test_automap`, `test_modes`, and `test_control` (73 tests counted instead of 72); not reproduced.
- `make` does not rebuild an object with the same modification time as its source (seen after scripted rebases). After a scripted rebase, build with `make -B`.
