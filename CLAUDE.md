# dm2

@~/.claude/skills/c-decomp-port/SKILL.md
@~/.claude/skills/python-uv/SKILL.md

Native C11/SDL3 macOS port of Dungeon Master II (PC DOS `SKULL.EXE`), built from its own Ghidra decompilation.

## Commands
- Build: `make -C port` (0 warnings).
- Run: `build/dm2` starts a new game; `build/dm2 --save game/DATA/SKSAVE0.DAT` loads a save.
- Turn-based mode: `build/dm2 --mode turnbased --saves DIR`; `uv run dm2-play --brief get_state` (`docs/turnbased-mode.md`).
- One test file: `uv run pytest -q tests/test_x.py`.
- Full suite: `tools/test-suite` (parallel; skips a clean tree that already passed; log in `build/test-suite.log`).

## Layout
- `port/src/`: the C port in four layers (`data/`, `engine/`, `ui/`, `platform/`); `port/Makefile` builds `build/dm2` and `build/libdm2.dylib`.
- `src/dm2tools/`: Python tools: LE loader, emulator harness (unicorn), data formats.
- `tests/`: differential tests against the original in `dm2tools.emu`; `tests/test_standard.py` checks the coding standard.
- Git-ignored data: `game/` (original files), `reference/` (incl. `reference/decomp/SKULL.c`, the Ghidra output of `tools/decompile-skull.sh`), `tests/saves/`. Link them into a new worktree.

## Conventions
- `docs/coding-standard.md` is the coding standard of the C port; follow it for every change in `port/src/`.
- `docs/skull-formats.md`: data formats and every deviation of the port. `docs/skull-symbols.csv`: original addresses and names.
- Plans in `docs/superpowers/`; manual checks in `docs/manual-test-checklist.md`.
