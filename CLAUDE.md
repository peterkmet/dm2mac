# dm2

@~/.claude/skills/c-decomp-port/SKILL.md
@~/.claude/skills/python-uv/SKILL.md

Native C11/SDL3 macOS and Windows port of Dungeon Master II (PC DOS `SKULL.EXE`), built from its own Ghidra decompilation.

## Commands
Details: `docs/development.md`.
- Build: `make` (0 warnings) builds `build/dm2`; `make lib` builds `build/libdm2.dylib`.
- Windows build: `make windows` builds `build/dm2.exe` with MinGW-w64 and the SDL3 MinGW package of `../dm` (in a worktree: `make windows DM_DIR=<path to the dm folder>`).
- Run: `build/dm2` starts a new game; `build/dm2 --save game/DATA/SKSAVE0.DAT` loads a save.
- Turn-based mode: `build/dm2 --mode turnbased --saves DIR`; `uv run dm2-play --brief get_state` (`docs/llm/llm-mode.md`).
- One test file: `uv run pytest -q tests/<folder>/test_x.py`.
- Full suite: `make test` runs `tools/test-suite` (parallel; skips a clean tree that already passed; log in `build/test-suite.log`).
- Check: `make check` (ruff, the standard check, the full suite); `make format` (clang-format); `tools/standard-allowlist` (regenerates `tests/standard/allowlist.txt`); `tools/rename-symbol OLD NEW` (renames a public symbol in `src/`, `tests/`, the symbol table and the docs).

## Layout
- `src/`: the C port in five layers (`data/`, `engine/`, `ui/`, `control/`, `platform/`; headers in `<layer>/include/`, vendored OPL3 in `src/third_party/opl3/`); the root `Makefile` builds `build/dm2` and `build/libdm2.dylib`, and with `make windows` `build/dm2.exe`.
- `tools/dm2tools/`: Python tools: LE loader, emulator harness (unicorn), data formats.
- `tests/`: `differential/` (the port against the original in `dm2tools.emu`), `port/` (port only), `tools/` (the Python tools), `standard/` (`test_standard.py`, `test_docs.py`, `test_vocabulary.py` check the coding standard, the doc links and the protocol vocabulary); `dm2test/` holds the shared helpers.
- `docs/`: `manual.md` (player manual), `architecture.md`, `development.md`, `decompiling.md`; folders `formats/`, `rules/`, `llm/`, `reference/`, `images/`.
- Git-ignored data: `research/` (the private repository `dm2-research`: plans, the original files, the test saves), `game/` and `tests/saves/` (links into `research/`), `reference/` (incl. `reference/decomp/SKULL.c`, the Ghidra output of `tools/decompile-skull.sh`). Link them into a new worktree.

## Conventions
- `docs/coding-standard.md` is the coding standard of the C port; follow it for every change in `src/`.
- `docs/formats/`: data formats; `docs/rules/`: game rules. Each page ends with the deviations of the port in "Deviations from the original"; the deviations of the turn-based mode are in `docs/llm/llm-mode.md`. `docs/reference/skull-symbols.csv`: original addresses and names.
- `docs/architecture.md`: the layers; `docs/development.md`: build, test and check; `docs/decompiling.md`: the Ghidra output of `reference/`.
- Plans in `research/superpowers/` (private, optional); manual checks in `docs/manual-test-checklist.md`; open work in `docs/todo.md`.
