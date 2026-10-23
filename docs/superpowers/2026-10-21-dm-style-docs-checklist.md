# Documentation in the style of the dm port: manual test checklist

Read the pages on GitHub or in a Markdown viewer. Each item names what to look at and the expected result.

## Structure

- [ ] D1. Open `README.md`. Expected: the screenshot shows, the sections follow the `dm` README (game data, Features, Play, Let an LLM play, Build, Engineering, License), and each link opens its page.
- [ ] D2. List `docs/`. Expected: the top-level pages `architecture.md`, `coding-standard.md`, `decompiling.md`, `development.md`, `manual-test-checklist.md`, `manual.md`, `todo.md`, and the folders `formats/`, `images/`, `llm/`, `reference/`, `rules/`, `superpowers/`.
- [ ] D3. Open three pages of `docs/formats/` and three of `docs/rules/`. Expected: a title, one paragraph, sections, and "Deviations from the original" as the last section.

## Content

- [ ] D4. Read `docs/manual.md`. Expected: you can start the game, choose a mode, use the advanced keys, quick save and quick load, the cheat screen and the options menu from this page alone, with links for the rules.
- [ ] D5. Read `docs/architecture.md` "The frame loop" and "The control socket and the turn". Expected: the text matches what you know of `src/platform/main.c` and `src/control/`.
- [ ] D6. Read `docs/development.md`. Expected: `make`, `make windows`, `make test`, `make check` and `make format` are as you use them; the Wine settings have no personal path.
- [ ] D7. Read `docs/llm/llm-mode.md` "Start the game" and run the turn-based start it shows. Expected: `uv run dm2-play --brief get_state` answers.
- [ ] D8. Read `docs/manual-test-checklist.md`. Expected: no dates, no results, every manual item has an empty box, "Covered by" items have no box.
- [ ] D9. Read `docs/todo.md`. Expected: no dates; the new items (driver offset of "Instrument to voice", the missing `CreateFunctionAfterTerminals.java`, `ai_move_internal.h`, the language nibble 0) are clear to you.

## Agents

- [ ] D10. Start a coordinator run or read `.claude/agents/dm2-coordinator.md` and `dm2-player.md`. Expected: they name `docs/llm/llm-playbook.md` and `docs/llm/llm-runs.md`, and the player finds everything it needs in the playbook.

## Local setting

- [ ] D11. `git check-ignore -v docs/reference/x.md`. Expected: no match. On this machine `.git/info/exclude` line `reference` still matches; change it to `/reference` if you want.
