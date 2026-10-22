# TODO

- Manual test D15: take the bones of a dead champion to an altar (needs long play).
- Manual test J16: complete the final task and check the end of the game (needs long play).
- Manual test H6: an animated wall ornament with a sound (only on levels 2 and 4).
- Flaky test: `tests/port/test_control.py::test_a_second_client_gets_busy` failed once with `BrokenPipeError` under heavy machine load (7 parallel builds). It waits only `time.sleep(0.05)` for the first client. It passed 5 of 5 runs alone. `tests/port/test_modes.py::test_program_starts_in_advanced_unless_vanilla[advanced]` once got `{'error': 'busy'}` from the control socket in a parallel run (2026-10-09).
- Champion redraw bit 0x10 (`REDRAW_STEP_TIME` in `engine/champion.h`): the step time of `champ_get_move_time` (`engine/move.c`) reads it, but no code of the port sets it. Find out what sets it in `SKULL.EXE` and name it after its meaning.
- Names without a verb (rule 7): `dialog_volume`. `sensor_door_value` reads the GDAT value of a door type, not a sensor value. Functions with one prefix that other modules define (for example `money_*` in `item.c`, `shop_*` in `money.c`) could move to the module of their prefix.
- Item name "f m": in `tests/saves/shell.DAT` the leader hand holds thing 10279 with the name "f m", and the message area sometimes shows a row "B %" (for example before "GAME SAVED"). `main` does the same. Find out if the name of this item is decoded wrong.
- Manual tests L10 (sound direction) and L12 (end of the game) of the layer cleanup smoke test; the sound of L1 (music) and L9 (thunder) is also not checked.
- `gdat_find` (`data/gdat.c`) scans the 11854 key rows linearly; sort or index the rows if lookups get hot.
- Automap visibility (`visible_depths` in `engine/seen.c`): a fixed cut by light level; a finer rule if a player needs it.
