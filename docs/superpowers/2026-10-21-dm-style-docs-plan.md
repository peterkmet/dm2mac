# Documentation in the style of the dm port: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task (the run of the agentic-workflow skill). Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The documentation of `dm2` has the structure, the page types and the link and vocabulary checks of the `dm` port, and describes the current state only.

**Architecture:** Fifteen tasks in the order of PRD Requirement 10. The link check comes first and guards every move. Pages move with `git mv` where the PRD keeps them 1:1; split and merged pages follow the section maps of this plan. A final audit pass applies the docs-audit skill to all pages.

**Tech Stack:** Markdown, Python 3.14 with uv, pytest.

**Spec:** `docs/superpowers/2026-10-21-dm-style-docs.md`

**Model:** `/Users/lego/github/dm` (written `../dm` below): `README.md`, `docs/architecture.md`, `docs/development.md`, `docs/decompiling.md`, `docs/manual.md`, `docs/formats/`, `docs/rules/`, `docs/llm/`, `docs/reference/`, `tests/standard/test_docs.py`, `tests/standard/test_vocabulary.py`. Copy the structure and the page types, not the DM facts.

**Precondition:** the run of `docs/superpowers/2026-10-21-dm-style-code-plan.md` is done and merged to `main`. The paths of this plan are the paths after it: `src/<layer>/`, `src/control/`, `tools/dm2tools/`, `tests/{differential,port,tools,standard}/`, `tests/dm2test/`, the root `Makefile`. Line numbers of `docs/*.md` are of 2026-10-21; the code PRD changed only paths in these pages, so a section heading finds its place when a line moved. A path or a line in `src/` or `tests/` is found with `git grep` at the time of the task.

## Global Constraints

- Only `docs/`, `README.md`, `CLAUDE.md`, `.claude/agents/*.md`, the two doc tests and the paths of Requirements 5 and 6 change (PRD Invariant 1). A change in `src/`, `tests/` or `tools/` changes only a path or a page name in a comment, a docstring or a string; no code changes.
- `docs/superpowers/` and `CHANGELOG.md` do not change (Invariant 2).
- The full suite passes after each task (Invariant 3; agentic-workflow runs it). The author runs only `uv run pytest -q -n auto tests/standard` and the test modules whose files the task changed.
- The author loads the `docs` skill before writing a page and follows its rules for the page type of the target structure table of the PRD. Task 14 and Task 15 load the `docs-audit` skill.
- ASD-STE100 style, one term per concept (the terms of the current pages and of `docs/coding-standard.md`), English.
- A fact is on one page only (Requirement 2). When two sources say the same fact, the target page keeps one text; a difference between them is a stop: report it.
- No new research (PRD Scope): a page holds what the current pages, the code and the tests say. A new page (architecture, development, decompiling, manual, README) describes the code as it is; each claim about the code names the file it comes from in the task report (not in the page).
- `test_docs.py` accepts only tracked files: `git add` each new and moved file before the verify step of a task.
- A move of a page changes every link and path that names the old page in the same commit (Requirement 6). After each task: `git grep -n -E '<old names of the task>' -- ':!docs/superpowers' ':!CHANGELOG.md'` shows only the new paths.
- No dates, no step names, no "not yet" in the new and moved pages outside `docs/reference/symbols/` (Acceptance "no history"); no email address and no `/Users/` path (Acceptance "no personal data").
- Do not download or run third-party code (libraries, CDN files, packages outside the project's lock file) to check behavior; report what you could not verify (agentic-workflow, "No external code").
- Conventional Commits, one type per commit (`docs:`, `test:`).

Test list (CLAUDE.md §2). Write a test only for: a check against a reference (original program, golden snapshot), a parser or data format, a rule with more than one outcome that no reference check or type check covers (dates, parsing, prices, state machines), a regression of a real bug (one test per bug). No tests for: constants and data tables; wiring, scaffold, CLI arguments, logging; what the compiler, a linter or a standard check catches; UI and layout (the user tests them manually); what another test already covers. No mutation tests.

In this plan the tests are the two repository checks that the PRD names: `tests/standard/test_docs.py` (Task 1) and `tests/standard/test_vocabulary.py` (Task 9). The other acceptance scenarios are searches that Task 15 runs.

**Weakened assertions:** none. Approved by the user on 2026-10-21 (the empty list).

## Review Focus

1. A link with an anchor (`turnbased-mode.md:29` `#play-from-the-shell-dm2-play`) to a heading that a merge renamed: expected a working anchor. `test_docs.py` ignores anchors; Task 15 step 1b checks them.
2. A relative link inside a page that moved one folder down (`docs/reference/symbols/`, `docs/llm/`): expected the link to resolve. `test_docs.py` (Task 1) pins it after each move.
3. A path to a page in a code comment, a docstring or the help text of `dm2-play`: expected the new path. The grep step of each move task and Task 15 step 4 pin it.
4. The protocol tables after the format commit of the code plan (one entry per line, aligned): expected `TABLE` and `ACTION` to find the same names as before. Task 9 step 4 runs the test; a missing table name in the test output is a stop.
5. The agents of the runs read `docs/llm/llm-playbook.md`: expected `.claude/agents/*.md` to name the new path, so a run after this plan finds the manual. Task 8 step 5 pins it.

## Decisions of this plan

| Topic | Decision | Reason |
|---|---|---|
| Run | One task after the other, no parallel packages. | Requirement 10 orders the moves; most tasks change the same pages and the links between them. |
| Address conventions | The "Conventions" list of `docs/skull-formats.md:5-10` goes to the head of `docs/reference/skull-map.md`; line 3 (the PRD and step of the first plan) goes. | The conventions describe the addresses of the reference pages; line 3 is history. |
| Answer and Evidence headings | The subsections "Answer" and "Evidence" of `skull-formats.md` §1, §2, §3, §6 become plain text under the topic heading: the answer first, the evidence after it. | Requirement 4. |
| "Measurement: original load, then original save" | Stays in `docs/formats/save.md` as the section "Compatibility with the original". | It is a fact of the format (the original reads the files of the port), not history. |
| "Open measurement" (§6) | Stays in `docs/formats/runtime.md` as the section "Unknown values"; the "real game rate" line of the checklist deviations is a duplicate. | A question about the original, as the open questions of `skull-map.md`. |
| Sound requests (§11) | Go to `docs/rules/world.md` with §11. | The PRD maps §11 to `world.md`; the sound requests are rules of the engine. |
| "Save block" of the advanced mode | Goes to `docs/formats/save.md` as the section "AMAP block"; `docs/manual.md` links to it. | It is the format of a save block; the manual describes the use. |
| "View a screenshot as an LLM" | Goes to `docs/llm/llm-playbook.md`. | It tells the player how to look at the screen. |
| The general checklist deviation "Fixed technical bugs ..." | Goes; each rule page keeps its own specific deviation lines. | Duplicate of the deviation sections of §8, §11-§14. |
| Screenshot | `build/dm2` with `--control` and `--save tests/saves/outside.DAT`, request `screenshot` through `dm2-play`, file `docs/images/screenshot.png`. | `--control` works in every mode (`src/platform/main.c`, `src/platform/main.c:328` on 2026-10-21); the outdoor view shows the most of the game. The PNG is the 320x200 screen: `src/control/control.c:384-397` writes `fb` as `SCREEN_W`×`SCREEN_H`, `src/ui/include/video.h:7-8`. |
| Old range notes | `docs/skull-notes/*.md` move to `docs/reference/symbols/` with `git mv`, names unchanged. | PRD target structure. |

## Section maps

### `docs/skull-formats.md`

| Lines | Section | Target |
|---|---|---|
| 1-3 | title, PRD and step note | removal: history |
| 5-10 | Conventions | `reference/skull-map.md`, head |
| 12-79 | GRAPHICS.DAT container, Language filter, Image compression | `formats/graphics.md` |
| 80-146 | §1 Digital sound effects (Answer, Evidence, Playback, Deviations) | `formats/sound.md` |
| 147-191 | §2 HMP songs and SONGLIST.DAT, Song change, HMP player | `formats/music.md` |
| 192-212 | §3 SVGA option | `formats/runtime.md` |
| 213-238 | §4 DUNGENB.DAT and GRAPHIC2.DAT | `formats/graphics.md` |
| 239-340 | §5 MIDI to OPL conversion (file format, BNK, register mapping, AdLib fallback, OPL2 driver logic, Deviations) | `formats/music.md`; "Decompilable" (260-271) becomes one sentence on how the driver code was read |
| 341-388 | §6 PIT divisor and tick rates, Open measurement | `formats/runtime.md` |
| 389-394 | Other finding: IBMIOP.EXE is a required runtime layer | `formats/runtime.md` (the effect on the port) |
| 395-408 | IBMIOP service details from reDM2 | merge into `reference/skull-map.md` "IBMIOP INT 0xFC service table" (71-109); a fact that the table has goes |
| 409-492 | §7 Save file format (Container, Bit-packed records, Impact on the port, Measurement, Details found while porting, Deviations) | `formats/save.md`; "Impact on the port" and "Details found while porting" become plain sections of the format |
| 493-547 | §8 Dungeon view | `rules/view.md` |
| 548-594 | §9 Interface panels and text | `rules/interface.md` |
| 595-655 | §10 Main loop, input, and movement | `rules/movement.md` |
| 656-709 | §11 Weather, day and night, light, Skullkeep machines, Sound requests | `rules/world.md` |
| 710-750 | §12 Creatures, projectiles, magic, and minions | `rules/creatures.md` |
| 751-806 | §13 Champions, inventory, items, money, and shops | `rules/champions.md` |
| 807-850 | §14 Start menu, save and load, dialogs, end of game | `rules/game-flow.md` |
| 851-905 | §15 Movies | `formats/movies.md` |
| 906-926 | §16 Turn-based mode, Deviations | `turnbased-mode.md` "Deviations from the original" (Task 4), then `llm/llm-mode.md` (Task 8) |

Each "Deviations of the C port (...)" subsection becomes part of the one section "Deviations from the original" at the end of its target page; the step names in the headings go.

### `docs/manual-test-checklist.md` "Known deviations" (314-334)

| Item | Target |
|---|---|
| default mode `advanced`, `--mode vanilla` for A to K | checklist "Prerequisites" |
| SVGA movies only, movie screen, no skip, RESTART without movies | `formats/movies.md` |
| no DOS layer; `--save PATH` start; RESTART and the drive path | `rules/game-flow.md` |
| cursor images | `rules/champions.md` (§13 Deviations has it: the checklist line goes) |
| message area scroll by 7 pixels | `rules/interface.md` |
| enter and leave events, drag through a fake wall; walking frame stretch; pit fall view | `rules/movement.md` |
| sound pan curve, 44100 Hz, pan overflow fix | `formats/sound.md` |
| every sound counts as loaded | `rules/world.md` (sound requests) |
| music: Nuked-OPL3 in OPL2 mode | `formats/music.md` |
| fixed technical bugs (general) | removal: duplicate (Decisions) |
| creature AI keeps its plan data; no free active group | `rules/creatures.md` |
| no SVGA mode | `formats/runtime.md` |
| real game rate is open | removal: duplicate of `formats/runtime.md` "Unknown values" |

A line that the target page already has in the same meaning goes; the checklist keeps one line: "Do not report a documented deviation as a bug: see the sections "Deviations from the original" of the pages in `docs/formats/`, `docs/rules/` and `docs/llm/llm-mode.md`."

### `docs/llm-control.md` and `docs/turnbased-mode.md`

| Source | Section | Target |
|---|---|---|
| `llm-control.md` 1-8 | intro | `llm/llm-mode.md` intro, merged with `turnbased-mode.md` 1-6 |
| `llm-control.md` 9-36 | Start the game (socket, dummy drivers, `--control`, `--save`) | `llm/llm-mode.md` "Start the game"; the options of a normal start (`--save`, `--data`, the movies) go to `manual.md` "Options", and `llm-mode.md` links to them |
| `turnbased-mode.md` 7-29 | Start the game (turn-based) | `llm/llm-mode.md` "Start the game", second part |
| `llm-control.md` 37-50, `turnbased-mode.md` 206-226 | `dm2-play` | `llm/llm-mode.md` "dm2-play", one text |
| `llm-control.md` 51-62, `turnbased-mode.md` 30-39 | Protocol | `llm/llm-mode.md` "Protocol" |
| `llm-control.md` 63-205 | Commands of the real-time modes | `llm/llm-mode.md` "Commands" |
| `turnbased-mode.md` 40-170 | Commands, Turns, Batches, Reply fields, State/view/map, Cells, Game over and modal loops | `llm/llm-mode.md`, the same headings under "The turn-based mode" |
| `turnbased-mode.md` 171-205 | Actions | `llm/llm-mode.md` "Actions" |
| `llm-control.md` 206-217 | Movies | `llm/llm-mode.md` "Movies" |
| `llm-control.md` 218-234 | Errors | `llm/llm-mode.md` "Errors" |
| the limits of both pages (socket path length, one client, no Windows) | | `llm/llm-mode.md` "Known limits" |
| `llm-control.md` 235-245 | View a screenshot as an LLM | `llm/llm-playbook.md` (Decisions) |
| `turnbased-mode.md` 227-250 | Runs and checkpoints | `llm/llm-runs.md`; the coordinator and player agents (231, 249) as the section "Agents" |
| `turnbased-mode.md` 251-266 | C API | `architecture.md` "The control socket and the turn" |
| `turnbased-mode.md` 267-277 | Integration in main.c | `architecture.md` "The frame loop" |
| `turnbased-mode.md` 278-288 | Tests | `development.md` "Tests" |
| (new) | Protocol vocabulary | `llm/llm-mode.md`, Task 9 |
| `turnbased-mode.md` "Deviations from the original" (from Task 4) | §16 | `llm/llm-mode.md` "Deviations from the original" |

### `docs/advanced-mode.md`

| Lines | Section | Target |
|---|---|---|
| 1-7 | intro | `manual.md` "Modes" |
| 8-22 | Select the mode | `manual.md` "Options" (`--mode`) and "Modes" |
| 23-46 | Keys | `manual.md` "Controls", a table of the advanced keys |
| 47-64 | Quick save and quick load | `manual.md` "Saved games" |
| 65-86 | Automap, Seen squares | `manual.md` "Modes" |
| 87-102 | Save block | `formats/save.md` "AMAP block" (Decisions) |
| 103-109 | Cheats | `manual.md` "Modes" |

### `docs/manual-test-checklist.md` (rest)

| Lines | Section | Target |
|---|---|---|
| 5-15 | Results of 2026-10-06 | removal: history |
| 16-30 | Smoke test after the module cleanup | removal: history |
| 31-49 | Smoke test after the layer cleanup | removal: history; an open item goes to `docs/todo.md` only when `todo.md` names it already |
| 277 | Movies (2026-10-10) | "Movies" |
| 3, 55 | the notes "(workflow step 6, plan Step 21)", "(plan Step 21)" | removal: history |
| 58-98 | Build and start: the options table (`--data`, `--save`, `--filter`) | removal: duplicate of `manual.md` "Options"; a link to it stays |
| every item | results ("Passed (manual, ...)", "(control socket, ...)", "Skipped by the user's decision"), checked boxes | removal: history; each box `[ ]` |
| items with "Covered by" | `[x]` with "Covered by <test>" | the item stays as a plain list item without a box, with its "Covered by" note; line 97 says that such an item needs no manual check |

The smoke test section that Task 72 of the code plan adds is a dated result section too: removal.

### `docs/todo.md`

The date "(2026-10-09)" goes; "Manual tests L10 ... of the layer cleanup smoke test" names the items L10, L12, L1 and L9 of the checklist without the smoke test.

---

### Task 1: The link check

PRD Requirement 10.1, Acceptance "links".

**Files:**
- Create: `tests/standard/test_docs.py` (port of `../dm/tests/standard/test_docs.py`)
- Modify: a page with a broken link

- [ ] **Step 1:** Copy the test of `dm`; `markdown_files()` skips `docs/superpowers/` (PRD Acceptance "links": docs outside `docs/superpowers/`).
- [ ] **Step 2:** Run `uv run pytest -q tests/standard/test_docs.py`. On 2026-10-21 the pages had 7 relative links and none was broken; fix each link that the run reports.
- [ ] **Step 3:** Run it again; expected PASS.
- [ ] **Step 4:** Commit `test: check the relative links of the docs`.

### Task 2: The reference pages

PRD Requirements 5, 9, 10.2.

**Files:**
- Move (`git mv`): `docs/skull-symbols.csv` → `docs/reference/skull-symbols.csv`; `docs/skull-map.md`, `docs/reference-crosscheck.md` → `docs/reference/`; `docs/skull-notes/*.md` → `docs/reference/symbols/`
- Modify: every file that the grep of step 2 lists: on 2026-10-21 `CLAUDE.md`, `docs/coding-standard.md`, `docs/skull-map.md`, `docs/reference-crosscheck.md`, `docs/skull-formats.md`, `src/engine/include/dungeon.h:210` (`docs/skull-notes/range-6.md`), `tests/test_symbols.py` (after the code plan `tests/tools/test_symbols.py`), `tools/decompile-skull.sh`; after the code plan also `tests/standard/test_standard.py`, `tests/standard/allowlist.txt`, `tools/rename-symbol`

- [ ] **Step 1:** The moves above.
- [ ] **Step 2:** `git grep -n -E 'skull-symbols\.csv|skull-map\.md|reference-crosscheck\.md|skull-notes' -- ':!docs/superpowers' ':!CHANGELOG.md'`; give each place the new path (a relative link: relative to its page).
- [ ] **Step 3:** The links inside the moved pages (for example `../skull-map.md` in a range note) point to the new places.
- [ ] **Step 4:** Verify: the grep of step 2 shows only new paths; `uv run pytest -q -n auto tests/standard tests/tools/test_symbols.py` passes; the file list of `tools/rename-symbol` (code plan Task 31) is `docs/reference/skull-symbols.csv` and `docs/**/*.md` except `docs/superpowers/` and `docs/reference/symbols/`: change it in this task.
- [ ] **Step 5:** Commit `docs: move the reference pages to docs/reference/`.

### Task 3: The format pages

PRD Requirements 3, 4, 10.3; section map of `docs/skull-formats.md`.

**Files:**
- Create: `docs/formats/graphics.md`, `sound.md`, `music.md`, `save.md`, `movies.md`, `runtime.md`
- Modify: `docs/skull-formats.md` (the moved sections go), `docs/reference/skull-map.md` (Conventions, IBMIOP table), `docs/manual-test-checklist.md` (the "Known deviations" items of the format pages)

- [ ] **Step 1:** Read `../dm/docs/formats/save.md` for the page form (reference: title, one paragraph, sections, "Deviations from the original" at the end).
- [ ] **Step 2:** Move the rows of the section map with the target `formats/*.md` and `reference/skull-map.md`; apply the Decisions "Address conventions", "Answer and Evidence headings", "Measurement", "Open measurement".
- [ ] **Step 3:** Move the checklist deviation items of the format pages (its table); remove each item from the checklist.
- [ ] **Step 4:** In the moved text: `docs/skull-formats.md` section references ("section 15", "§7") become links to the new page; the step names in headings go.
- [ ] **Step 5:** Verify: `uv run pytest -q tests/standard/test_docs.py` passes; each moved line of the section map is in its target (count the sections: 6 pages, the deviation lines of §1, §5, §7, §15).
- [ ] **Step 6:** Commit `docs: split the data formats into docs/formats/`.

### Task 4: The rule pages

PRD Requirements 3, 10.3; section map of `docs/skull-formats.md`.

**Files:**
- Create: `docs/rules/view.md`, `interface.md`, `movement.md`, `world.md`, `creatures.md`, `champions.md`, `game-flow.md`
- Delete: `docs/skull-formats.md`
- Modify: `docs/turnbased-mode.md` (a section "Deviations from the original" with §16), `docs/manual-test-checklist.md` (the rest of "Known deviations", the one line, "Prerequisites"), every file that names `skull-formats` (on 2026-10-21: `CLAUDE.md`, `docs/llm-control.md`, `docs/manual-test-checklist.md`, `docs/turnbased-mode.md`, `docs/reference/reference-crosscheck.md`, `docs/reference/skull-map.md`, `docs/reference/symbols/range-1.md`, `range-7.md`, comments in `src/control/state_json.c`, `src/data/include/mve.h`, `src/engine/save.c`, `src/platform/platform.c`, `src/ui/include/movie.h`, and `tests/test_gfx.py:115`, `tests/test_music.py:21,31`, `tests/test_champion_rules.py:204` in their new folders)

- [ ] **Step 1:** Read `../dm/docs/rules/movement.md` for the page form.
- [ ] **Step 2:** Move the rows of the section map with the target `rules/*.md`; §16 goes to `turnbased-mode.md` "Deviations from the original".
- [ ] **Step 3:** Move the rest of the checklist "Known deviations" by its table; the checklist keeps the one line; the first item goes to "Prerequisites".
- [ ] **Step 4:** `git rm docs/skull-formats.md`; it is empty except the removal rows.
- [ ] **Step 5:** `git grep -n 'skull-formats' -- ':!docs/superpowers' ':!CHANGELOG.md'`: each place names the page of its topic (`mve.h`, `movie.h` → `docs/formats/movies.md`; `save.c` → `docs/formats/save.md`; `state_json.c` → `docs/turnbased-mode.md` until Task 8; `platform.c` and the tests → the page of their topic; `range-1.md:93`, `range-7.md:67` → `../../formats/sound.md`, `range-7.md:26` → `../../formats/graphics.md` (PRD target structure); a doc that says "the deviations are in `docs/skull-formats.md`" names `docs/formats/`, `docs/rules/` and `docs/turnbased-mode.md`).
- [ ] **Step 6:** Verify: the grep of step 5 is empty; `uv run pytest -q -n auto tests/standard <the test modules whose comments changed>` passes.
- [ ] **Step 7:** Commit `docs: split the game rules into docs/rules/`.

### Task 5: The architecture page

PRD target structure `docs/architecture.md`.

**Files:**
- Create: `docs/architecture.md`

- [ ] **Step 1:** Read `../dm/docs/architecture.md`: the sections Layers, Modules, The frame loop, The tick, The control socket and the turn, Global state, Hooks between layers, and "Add a game rule" of `../dm/docs/development.md` (the PRD puts it into `architecture.md`).
- [ ] **Step 2:** Write the same sections for `dm2` from the code: the five layers and their include paths (root `Makefile`), one line per module from the purpose paragraph of each public header, the frame loop of `src/platform/main.c`, the timer tick and the game step (`ui/play.c`, `main_loop.tick_threshold`), `g_hooks` (`engine/include/hooks.h`, `ui/hook_table.c`), `data/os.h`, the global state (`game`, `session`, `party`, the records of `engine/dungeon.h`), the test seams (`build/libdm2.dylib`, the hooks the tests set), and a section "Add a game rule" (from `../dm`, with the `dm2` layers and the differential test).
- [ ] **Step 3:** "The control socket and the turn" describes `platform/transport.c` and `control/` as the code has them; Task 8 adds the C API and the integration in `main.c` from `turnbased-mode.md`, so this task does not repeat them.
- [ ] **Step 4:** Verify: `uv run pytest -q tests/standard/test_docs.py` passes; each module of `ls src/*/include/*.h` has its line.
- [ ] **Step 5:** Commit `docs: add the architecture page`.

### Task 6: The development page

PRD target structure `docs/development.md`.

**Files:**
- Create: `docs/development.md`
- Modify: `CLAUDE.md` (Commands and Layout keep the short lines and link to `docs/development.md`), `docs/coding-standard.md` (the references to `CLAUDE.md` (Commands, Layout) of the code plan point to `docs/development.md`)

- [ ] **Step 1:** Read `../dm/docs/development.md` (Build, Tests, Layout, Coding standard; not Release).
- [ ] **Step 2:** Write Build (macOS: `make`, `make lib`, the flags, `SANITIZE`, `CFLAGS_EXTRA`; Windows: `make windows`, MinGW-w64, the SDL3 MinGW package, `SDL3.dll`, Wine), Tests (the four folders, `tests/dm2test/`, one module, the full suite `make test` (it runs `tools/test-suite`) and what it skips, `make check`, `make format`, `tools/standard-allowlist`, `tools/rename-symbol`), Layout, Coding standard (link), Manual checks (link to the checklist), and a link to "Add a game rule" of `architecture.md`.
- [ ] **Step 3:** Verify: each command of the page runs or prints its usage (`make -n windows`, `tools/test-suite` is not run: name it only); `uv run pytest -q tests/standard/test_docs.py` passes.
- [ ] **Step 4:** Commit `docs: add the development page`.

### Task 7: The decompiling page

PRD target structure `docs/decompiling.md`.

**Files:**
- Create: `docs/decompiling.md`

- [ ] **Step 1:** Read `../dm/docs/decompiling.md` (Prerequisites, Quickstart, Tools, How the script finds functions, Known limitations, Related pages).
- [ ] **Step 2:** Write the same sections from `tools/decompile-skull.sh`, `tools/ghidra/*.java`, `tools/ghidra/decompile.sh`, `tools/dosbox-dm2.conf`, `tools/dosbox-nointro.sh` and `uv run dm2-le` (the flat image of `docs/reference/skull-map.md` Conventions). The output is `reference/decomp/SKULL.c`.
- [ ] **Step 3:** Verify: each path of the page exists; `uv run pytest -q tests/standard/test_docs.py` passes.
- [ ] **Step 4:** Commit `docs: add the decompiling page`.

### Task 8: The LLM pages

PRD Requirements 10.5; section map of `docs/llm-control.md` and `docs/turnbased-mode.md`.

**Files:**
- Create: `docs/llm/llm-mode.md`, `docs/llm/llm-runs.md`
- Move (`git mv`): `docs/llm-playbook.md` → `docs/llm/llm-playbook.md`
- Delete: `docs/llm-control.md`, `docs/turnbased-mode.md`
- Modify: `docs/architecture.md`, `docs/development.md`, `docs/advanced-mode.md`, `docs/manual-test-checklist.md`, `CLAUDE.md`, `.claude/agents/dm2-coordinator.md`, `.claude/agents/dm2-player.md`, every file that names the three old pages (on 2026-10-21 also `src/control/include/control.h:11,33`, `src/control/turn.c:736,853`, `tools/dm2tools/control_client.py:9`)

- [ ] **Step 1:** Read `../dm/docs/llm/llm-mode.md` and `llm-runs.md` for the form.
- [ ] **Step 2:** Write `llm-mode.md` and `llm-runs.md` by the section map; one text where both sources say the same (start, protocol, `dm2-play`). The options of a normal start stay in "Start the game" until Task 10 moves them to `docs/manual.md` "Options" and puts a link in their place.
- [ ] **Step 3:** The C API and the integration in `main.c` go to `docs/architecture.md`, the tests to `docs/development.md` (section map).
- [ ] **Step 4:** Move the playbook; add "View a screenshot as an LLM" to it.
- [ ] **Step 5:** `git grep -n -E 'llm-control|turnbased-mode\.md|llm-playbook' -- ':!docs/superpowers' ':!CHANGELOG.md'`: each place names the new page (the agents name `docs/llm/llm-playbook.md`; `playthroughs/` is git-ignored and does not change).
- [ ] **Step 6:** Verify: the grep of step 5 shows only new paths; `uv run pytest -q -n auto tests/standard tests/tools/test_play_client.py` passes; `uv run dm2-play --help` names no old page.
- [ ] **Step 7:** Commit `docs: merge the LLM pages into docs/llm/`.

### Task 9: The protocol vocabulary

PRD Requirement 7, Acceptance "protocol vocabulary".

**Files:**
- Create: `tests/standard/test_vocabulary.py` (port of `../dm/tests/standard/test_vocabulary.py`)
- Modify: `docs/llm/llm-mode.md` (section "Protocol vocabulary")

**Interfaces:**
- The tables: the file-scope `static const char *const <name>[...] = {...};` arrays of `src/control/state_json.c` (on 2026-10-21 `mode_names`, `direction_names`, `party_cell_names`, `statistic_names`, `skill_names`, `wound_names`, `slot_names`, `rune_names`, `hand_names`, `map_option_names`, `amount_names`, `side_names`, `explosion_names`; `creature_names` is `{0}` and gives no name), and the action table `{"name", function}` of `src/control/turn.c` (`src/control/turn.c:747-760` on 2026-10-21). Not `key_names` (a struct table in `src/control/control.c`) and not the function-local `turns`, `direct` arrays.

- [ ] **Step 1:** Copy the test of `dm`: `protocol_names()` reads `src/control/state_json.c` with `TABLE` and `src/control/turn.c` with `ACTION`; it skips `creature_names` by name.
- [ ] **Step 2:** Run `uv run pytest -q tests/standard/test_vocabulary.py`; expected FAIL: no section "Protocol vocabulary".
- [ ] **Step 3:** Add the section to `llm-mode.md`: one line per table with its names in backticks and a short meaning (from the text of the actions and the state fields of the page); the action table: each action once.
- [ ] **Step 4:** Run the test; expected PASS.
- [ ] **Step 5:** Commit `test: check the protocol vocabulary of the LLM mode page`.

### Task 10: The player manual

PRD Requirement 10.6; section map of `docs/advanced-mode.md`.

**Files:**
- Create: `docs/manual.md`
- Delete: `docs/advanced-mode.md`
- Modify: `docs/formats/save.md` ("AMAP block"), `docs/llm/llm-mode.md` (the link to the options), `docs/manual-test-checklist.md`, every file that names `advanced-mode.md`

- [ ] **Step 1:** Read `../dm/docs/manual.md` (Run the game, Options, Start screens, Saved games, Controls, Modes).
- [ ] **Step 2:** Write Run the game, Options (every option of `read_options` in `src/platform/main.c`: `--data`, `--filter`, `--save`, `--control`, `--mode`, `--saves`; the normal start options from `llm-control.md`), Start screens (splash, movies, start menu), Saved games (slots, quick save), Controls (the original controls from the checklist sections C and D only where they state the control, and the advanced keys), Modes (vanilla, advanced with automap, seen squares and cheats, turn-based with a link to `llm/llm-mode.md`).
- [ ] **Step 3:** The "Save block" goes to `formats/save.md`; `git rm docs/advanced-mode.md`.
- [ ] **Step 4:** `git grep -n 'advanced-mode' -- ':!docs/superpowers' ':!CHANGELOG.md'` is empty after the changes.
- [ ] **Step 5:** Verify: `uv run pytest -q tests/standard/test_docs.py` passes; each option of `read_options` is on the page.
- [ ] **Step 6:** Commit `docs: add the player manual with the advanced mode`.

### Task 11: The checklist and the todo list

PRD target structure `docs/manual-test-checklist.md`, `docs/todo.md`.

**Files:**
- Modify: `docs/manual-test-checklist.md`, `docs/todo.md`

- [ ] **Step 1:** Apply the section maps "manual-test-checklist.md (rest)" and "todo.md".
- [ ] **Step 2:** Verify: `grep -nE '\b202[0-9]-[0-9]{2}-[0-9]{2}\b|\[x\]|Passed|Skipped|\b[Ss]tep [0-9]+' docs/manual-test-checklist.md docs/todo.md` is empty; the section letters A to L, T, W and "Movies" stay.
- [ ] **Step 3:** Commit `docs: make the checklist ready for the next run`.

### Task 12: The README

PRD target structure `README.md`, Requirement 8, Acceptance "pages".

**Files:**
- Create: `README.md`, `docs/images/screenshot.png`

- [ ] **Step 1:** Make the screenshot: `make`; `d=$(mktemp -d /tmp/dm2shotXXXX)`; `build/dm2 --control $d/c.sock --save tests/saves/outside.DAT &`; `uv run dm2-play --socket $d/c.sock wait '{"ticks": 60}'`; `uv run dm2-play --socket $d/c.sock screenshot "{\"path\": \"$PWD/docs/images/screenshot.png\"}"`; `uv run dm2-play --socket $d/c.sock quit`. `file docs/images/screenshot.png` says "PNG image data, 320 x 200".
- [ ] **Step 2:** Read `../dm/README.md`; write the same sections for `dm2`: what the port is (a native macOS port of Dungeon Master II, PC DOS, from its own Ghidra decompilation), Features, game data (the files of `game/` are not in the repository; the user copies them), Play (build, run, the modes, a link to `docs/manual.md`), Let an LLM play (link to `docs/llm/llm-mode.md`), Build (link to `docs/development.md`), Engineering (links to every page of the Acceptance "pages"), License (GPL-3.0, `LICENSE`; `opl3` with its own license).
- [ ] **Step 3:** Verify: every page of `docs/` (top level), `docs/formats/`, `docs/rules/`, `docs/llm/`, `docs/reference/` except `symbols/` and the CSV has a link in the README (`for f in ...; do grep -q "($f)" README.md || echo $f; done` prints nothing); `grep -nE '\w@\w+\.\w|/Users/' README.md` is empty; `uv run pytest -q tests/standard/test_docs.py` passes.
- [ ] **Step 4:** Commit `docs: add the README with a screenshot`.

### Task 13: CLAUDE.md and the place of the deviations

PRD Requirements 3, 10.8, Scope 4.

**Files:**
- Modify: `CLAUDE.md` (Layout, Conventions), `docs/coding-standard.md` (the place of the deviations and the symbol table)

- [ ] **Step 1:** `CLAUDE.md` Conventions: the deviations are in the sections "Deviations from the original" of `docs/formats/`, `docs/rules/` and `docs/llm/llm-mode.md`; the symbol table is `docs/reference/skull-symbols.csv`; the coding standard, the checklist, the architecture and the development pages are linked. Layout names `docs/` with its folders.
- [ ] **Step 2:** `docs/coding-standard.md`: where it names the formats doc and the symbol table, the new places (no rule changes).
- [ ] **Step 3:** Verify: `uv run pytest -q -n auto tests/standard` passes.
- [ ] **Step 4:** Commit `docs: name the new pages in CLAUDE.md`.

### Task 14: Audit of the formats, rules and reference pages

PRD Scope 5, Requirements 1, 2, 9.

**Files:**
- Modify: `docs/formats/*.md`, `docs/rules/*.md`, `docs/reference/skull-map.md`, `docs/reference/reference-crosscheck.md`

- [ ] **Step 1:** Load the `docs-audit` skill; apply it to each page: page type, ASD-STE100, one term per concept, no step notes ("Step 8", "Step 10", "Step 16" of `reference-crosscheck.md` become plain text), no "not yet", no duplicate tables, no history.
- [ ] **Step 2:** A fact on two pages: keep it on the page of its topic, link from the other.
- [ ] **Step 3:** Verify: `grep -rnE '\b[Ss]tep [0-9]+\b|\b202[0-9]-[0-9]{2}-[0-9]{2}\b|\b[Nn]ot yet\b' docs/formats docs/rules docs/reference --include=*.md | grep -v '^docs/reference/symbols/'` is empty; `uv run pytest -q tests/standard/test_docs.py` passes.
- [ ] **Step 4:** Commit `docs: audit the format, rule and reference pages`.

### Task 15: Audit of the other pages and the acceptance searches

PRD Scope 5, Acceptance.

**Files:**
- Modify: `README.md`, `docs/*.md`, `docs/llm/*.md`

- [ ] **Step 1:** The audit of Task 14 step 1 for these pages.
- [ ] **Step 1b:** Anchors: for each link `(<page>#<anchor>)` and `(#<anchor>)` in `README.md` and `docs/` outside `docs/superpowers/`, the target page has a heading whose GitHub anchor is `<anchor>` (`test_docs.py` checks only the file). Fix each broken anchor.
- [ ] **Step 2:** Acceptance "no history": `grep -rnE '\b[Ss]tep [0-9]+\b|\b202[0-9]-[0-9]{2}-[0-9]{2}\b|\b[Nn]ot yet\b' README.md docs --include=*.md | grep -vE '^docs/(superpowers|reference/symbols)/'` is empty.
- [ ] **Step 3:** Acceptance "no personal data": `grep -rnE '\w@\w+\.\w|/Users/' README.md docs | grep -v '^docs/superpowers/'` is empty.
- [ ] **Step 4:** Acceptance "no old path": `git grep -n -E 'docs/skull-map\.md|docs/skull-symbols\.csv|docs/skull-notes/|docs/reference-crosscheck\.md|docs/llm-playbook\.md|skull-formats\.md|llm-control\.md|turnbased-mode\.md|advanced-mode\.md' -- ':!docs/superpowers' ':!CHANGELOG.md'` is empty.
- [ ] **Step 5:** Acceptance "pages": `ls docs/*.md` gives `architecture.md coding-standard.md decompiling.md development.md manual-test-checklist.md manual.md todo.md`; the folders are `formats images llm reference rules superpowers`.
- [ ] **Step 6:** Acceptance "no lost content": walk the section maps of this plan; each row is in its target or in a removal row. List the result in the task report.
- [ ] **Step 7:** `uv run pytest -q -n auto tests/standard` passes.
- [ ] **Step 8:** Commit `docs: audit the remaining pages`.
