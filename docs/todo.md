# TODO

- Manual test D15: take the bones of a dead champion to an altar (needs long play).
- Manual test J16: complete the final task and check the end of the game (needs long play).
- Manual test H6: an animated wall ornament with a sound (only on levels 2 and 4).
- Flaky test: `tests/port/test_control.py::test_a_second_client_gets_busy` failed once with `BrokenPipeError` under heavy machine load (7 parallel builds). It waits only `time.sleep(0.05)` for the first client. It passed 5 of 5 runs alone. `tests/port/test_modes.py::test_program_starts_in_advanced_unless_vanilla[advanced]` once got `{'error': 'busy'}` from the control socket in a parallel run (2026-10-09).
- Champion redraw bit 0x10 (`REDRAW_STEP_TIME` in `engine/champion.h`): the step time of `get_step_time` (`engine/move.c`) reads it, but no code of the port sets it. Find out what sets it in `SKULL.EXE` and name it after its meaning.
- Names without a verb (rule 7): `dialog_volume`. `sensor_door_value` reads the GDAT value of a door type, not a sensor value. Functions with one prefix that other modules define (for example `money_*` in `item.c`, `shop_*` in `money.c`) could move to the module of their prefix.
- Item name "f m": in `tests/saves/shell.DAT` the leader hand holds thing 10279 with the name "f m", and the message area sometimes shows a row "B %" (for example before "GAME SAVED"). `main` does the same. Find out if the name of this item is decoded wrong.
- Manual tests L10 (sound direction) and L12 (end of the game) of the layer cleanup smoke test; the sound of L1 (music) and L9 (thunder) is also not checked.
- `gdat_find` (`data/gdat.c`) scans the 11854 key rows linearly; sort or index the rows if lookups get hot.
- Automap visibility (`visible_depths` in `engine/seen.c`): a fixed cut by light level; a finer rule if a player needs it.
- Message area scroll: IBMIOP service `0x105` is not in `SKULL.EXE`. The port moves the rows up and keeps the bottom rows (`shift_message_area` in `src/ui/text.c`); check it against the original in DOSBox.
- Chest picture out of bounds: `pick_object_picture` (`src/ui/view.c`) reads `object_picture[open_state]` with chest open states 2 and 3, past the 2 rows of the table (rule 10.5). `main` does the same; the 4 bytes after the table are 0. Check the original, use a defined value, and record it in `docs/skull-formats.md`.
- `creature_is_flying` crashed on 3 maps in a check script of the dm style refactor, also with the library of `main`. Find the cause.
- `make` does not rebuild an object with the same modification time as its source (seen after scripted rebases). After a scripted rebase, build with `make -B`.
- `long` in `src/control/control.c` and `src/control/turn.c` (38 uses, rule 5.3).
- Naming: `advanced_automap_event` passes a key code in `x` for `INPUT_KEY` (rule 6.4); the in/out parameters of `zone_compute_rect` (`out_w`, `out_h`) and `dungeon_location_on_map` (`out_x`, `out_y`) have an `out_` name.
