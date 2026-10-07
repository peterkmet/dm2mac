# TODO

- Manual test D15: take the bones of a dead champion to an altar (needs long play).
- Manual test J16: complete the final task and check the end of the game (needs long play).
- Manual test H6: an animated wall ornament with a sound (only on levels 2 and 4).
- Coding standard cleanup, module by module: remove the `open: module cleanup` entries of `tests/standard_allowlist.txt`. Plan: `docs/superpowers/2026-10-07-module-cleanup-plan.md`. Remove every violation of these types: unnamed constants (rule 1), hand-built packed fields (rule 10), one constant defined in more than one file (rule 1).
- Layer violations: `engine/` includes of `ui/` headers (the `layer` entries of `tests/standard_allowlist.txt`). They need a design for hooks or redraw flags.
- Flaky test: `tests/test_control.py::test_a_second_client_gets_busy` failed once with `BrokenPipeError` under heavy machine load (7 parallel builds). It waits only `time.sleep(0.05)` for the first client. It passed 5 of 5 runs alone.
- Champion redraw bit 4 (`REDRAW_SLOW_STEP` in `engine/move.c`): the step time of a champion reads it, but no code of the port sets it. Find the code of `SKULL.EXE` that sets it.
