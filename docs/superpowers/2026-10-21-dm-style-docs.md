# Documentation in the style of the dm port: PRD

## Goal

The documentation of `dm2` has the structure, the page types and the checks of the `dm` port (`../dm`). A reader finds the pages of `dm` in the same places in both ports: a README, an architecture page, a development page, a player manual, the formats, the game rules, the LLM pages and the reference. `dm2` has two more pages: `docs/coding-standard.md` (which `dm` also has) and `docs/manual-test-checklist.md` (which `dm` keeps in its private research repository). The text describes the current state only, without the history of the steps that made it.

The source is `../dm`: `README.md`, `docs/architecture.md`, `docs/development.md`, `docs/decompiling.md`, `docs/manual.md`, `docs/coding-standard.md`, `docs/todo.md`, `docs/formats/`, `docs/rules/`, `docs/llm/`, `docs/reference/`, `docs/images/`, `tests/standard/test_docs.py`, `tests/standard/test_vocabulary.py`.

This PRD starts after the code PRD `docs/superpowers/2026-10-21-dm-style-code.md` is done: the pages describe the new layout (`src/` with five layers, `tools/dm2tools/`, the root `Makefile`, the test folders).

## Reader

The developer and the agents that write the implementation plan and the pages.

## Current state

`docs/` has 10 top-level Markdown pages (about 3 000 lines), the folder `skull-notes/` with 9 Markdown pages, and a symbol table:

| Page | Lines | Content |
|---|---|---|
| `skull-formats.md` | 926 | data formats, answers to research questions, deviations; headings name the steps of the first plan ("Step 10") |
| `skull-map.md` | 389 | the map of `SKULL.EXE`: addresses, call graph, data structures, open questions |
| `skull-notes/range-1.md` … `range-8.md`, `ranges.md` | | notes per address range of the decompilation |
| `skull-symbols.csv` | | original addresses and names |
| `reference-crosscheck.md` | 180 | the comparison with reDM2 |
| `llm-control.md` | 245 | the control socket, `dm2-play`, the protocol |
| `turnbased-mode.md` | 288 | the turn-based mode, its protocol, actions, runs, C API, the integration in `main.c`, tests |
| `llm-playbook.md` | 448 | the manual of the LLM player |
| `advanced-mode.md` | 109 | the keys and features of the advanced mode |
| `manual-test-checklist.md` | 334 | the manual checks, with dated result sections |
| `coding-standard.md` | 56 | the code PRD rewrites it |
| `todo.md` | 10 | open work |

There is no `README.md`, no architecture page, no development page and no player manual. No test checks the links of the docs.

## Scope

In scope:

1. The target structure of `docs/` below, with the content of the current pages moved, merged and cleaned.
2. New pages: `README.md`, `docs/architecture.md`, `docs/development.md`, `docs/decompiling.md`, `docs/manual.md`.
3. The doc checks: `tests/standard/test_docs.py` (every relative link resolves) and `tests/standard/test_vocabulary.py` (every name of the protocol tables of `src/control/` is in the protocol vocabulary section).
4. `CLAUDE.md`: the Layout and Conventions sections name the new pages.
5. The clean-up of every page by the docs and docs-audit skills: ASD-STE100 style, one term per concept, no step notes, no "not yet", no duplicate tables, no history.

Out of scope:

- The rules of `docs/coding-standard.md` (the code PRD). This PRD changes only the paths that it names.
- New research of the original. A page holds what the current pages, the code and the tests already say.
- `docs/superpowers/`: the plans and PRDs stay as they are.
- `CHANGELOG.md`: it stays as it is.
- Pictures for the README beyond one screenshot.

## Target structure

| Path | Type | Content and source |
|---|---|---|
| `README.md` | overview | what the port is, features, no game data, build, run, the modes, links to the docs. Model: `dm` `README.md`. |
| `docs/images/screenshot.png` | image | the screenshot of the README. |
| `docs/architecture.md` | concept | layers, modules per layer, the frame loop, the tick, the control socket and the turn, global state, hooks between layers, test seams, how to add a game rule. Sources: the code, `turnbased-mode.md` (C API, integration in `main.c`). |
| `docs/development.md` | how-to | build (macOS, Windows), tests (folders, commands, the suite script), layout, coding standard, manual checks. Sources: `CLAUDE.md` (Commands, Layout), `turnbased-mode.md` (Tests), `tools/test-suite`, the root `Makefile`. |
| `docs/decompiling.md` | how-to | how to make `reference/decomp/SKULL.c` with Ghidra, and how to run the original in DOSBox. Sources: `tools/decompile-skull.sh`, `tools/ghidra/`, `tools/dosbox-dm2.conf`, `tools/dosbox-nointro.sh`. |
| `docs/manual.md` | manual | run the game, options, movies, saves, controls, the vanilla and advanced modes. Sources: `advanced-mode.md`, `llm-control.md` (Start the game: the options of a normal start). The start of the turn-based mode and the socket options stay in `docs/llm/llm-mode.md`; the manual links to it. |
| `docs/coding-standard.md` | reference | the coding standard. The code PRD writes it; this PRD changes only the paths that it names. |
| `docs/manual-test-checklist.md` | checklist | the manual checks, ready for the next run. The sections "Results of 2026-10-06" and the two "Smoke test after ..." sections go; "Movies" stays without the date; the results of the items ("Passed (manual, 2026-10-06)", "(control socket, 2026-10-10)") go and each box is `[ ]` again. In "Known deviations", the first item (the default mode and `--mode vanilla` for the items A to K) moves to "Prerequisites"; the other items move to the deviation sections of the format, rule and LLM pages; the checklist keeps one line that links to them. |
| `docs/todo.md` | list | open work, without dates; the item about the smoke test of the layer clean-up names the L items of the checklist. |
| `docs/formats/*.md` | reference | one page per file format, from `skull-formats.md`: `graphics.md` (the `GRAPHICS.DAT` container, `DUNGENB.DAT` and `GRAPHIC2.DAT`, §4), `sound.md` (§1), `music.md` (§2 HMP and `SONGLIST.DAT`, §5 MIDI to OPL), `save.md` (§7), `movies.md` (§15), `runtime.md` (§3 SVGA option, §6 PIT divisor and tick rates, the effect of the `IBMIOP.EXE` runtime layer on the port, with a link to the service table). The service facts of "Other finding" merge into the table "IBMIOP INT 0xFC service table" of `docs/reference/skull-map.md`. A page with deviations ends with a section "Deviations from the original"; a page without deviations has no such section, as in `dm`. |
| `docs/rules/*.md` | reference | the game rules of `skull-formats.md`, one page per section: `view.md` (§8), `interface.md` (§9), `movement.md` (§10), `world.md` (§11), `creatures.md` (§12), `champions.md` (§13), `game-flow.md` (§14). A page with deviations ends with a section "Deviations from the original", as the format pages. |
| `docs/llm/llm-mode.md` | reference | the start of the turn-based mode, the control socket, the protocol, a section "Protocol vocabulary", `dm2-play`, known limits, and a section "Deviations from the original" (§16 of `skull-formats.md`). Sources: `llm-control.md`, `turnbased-mode.md`. |
| `docs/llm/llm-playbook.md` | manual | the manual of the LLM player. Source: `llm-playbook.md`. |
| `docs/llm/llm-runs.md` | how-to | runs and checkpoints, the coordinator and player agents. Source: `turnbased-mode.md` (Runs and checkpoints). |
| `docs/reference/skull-map.md` | reference | the map of `SKULL.EXE`, with its open questions about the original. |
| `docs/reference/skull-symbols.csv` | data | the symbol table. |
| `docs/reference/symbols/*.md` | reference | the files of `skull-notes/`, moved 1:1, names unchanged; only the paths of moved pages change: `range-1.md:93` and `range-7.md:67` name `skull-formats.md` and get `formats/sound.md`, `range-7.md:26` gets `formats/graphics.md`. |
| `docs/reference/reference-crosscheck.md` | reference | the comparison with reDM2. |

## Acceptance

```gherkin
Feature: Documentation structure
  Scenario: pages
    When a reader opens README.md
    Then it links to each top-level page of docs/ and to each page in docs/formats/, docs/rules/, docs/llm/ and docs/reference/, except the notes in docs/reference/symbols/ and the CSV
    And docs/ has the pages of the target structure and no other top-level page

  Scenario: links
    When the developer runs uv run pytest -q tests/standard/test_docs.py
    Then every relative link in README.md, CHANGELOG.md, CLAUDE.md and docs/ outside docs/superpowers/ resolves to a tracked file or folder
    And test_docs.py checks only the links, as in dm; the review at the end checks the other scenarios with these searches

  Scenario: protocol vocabulary
    When the developer runs uv run pytest -q tests/standard/test_vocabulary.py
    Then every name of the protocol tables of src/control/ is in the section "Protocol vocabulary" of docs/llm/llm-mode.md

  Scenario: no history
    When the developer searches README.md and docs/**/*.md outside docs/superpowers/ and docs/reference/symbols/ for the patterns \b[Ss]tep \d+\b, \b202\d-\d\d-\d\d\b and \b[Nn]ot yet\b
    Then no line matches

  Scenario: no personal data
    When the developer searches README.md and docs/ outside docs/superpowers/ for the patterns \w@\w+\.\w and /Users/
    Then no email address and no personal path matches

  Scenario: no lost content
    When the reviewer walks the section map of the plan for each old page
    Then each section maps to a section of a target page, or to the removal list of the plan with a reason

  Scenario: no old path
    When the developer searches the repository for the old paths of the moved files (docs/skull-map.md, docs/skull-symbols.csv, docs/skull-notes/, docs/reference-crosscheck.md, docs/llm-playbook.md) and for the names of the removed pages (skull-formats.md, llm-control.md, turnbased-mode.md, advanced-mode.md)
    Then no file outside docs/superpowers/ and CHANGELOG.md names them
```

## Requirements

1. Each page has one page type of the docs skill and follows its rules.
2. A fact is on one page only; other pages link to it.
3. The deviations of the port move from `skull-formats.md` and from the "Known deviations" section of `manual-test-checklist.md` to the section "Deviations from the original" of the page they belong to: a format page, a rule page, or `docs/llm/llm-mode.md` for the turn-based mode. `CLAUDE.md` and `docs/coding-standard.md` name `docs/formats/`, `docs/rules/` and `docs/llm/llm-mode.md` as the place of the deviations. The `c-decomp-port` skill does not change.
4. The answers to research questions of `skull-formats.md` ("needed by the answers", "Other finding") become plain reference text of the topic page.
5. `docs/skull-symbols.csv` moves to `docs/reference/skull-symbols.csv`. Every path that names it changes in the same commit: `CLAUDE.md`, `docs/coding-standard.md`, the tests (`tests/tools/test_symbols.py`, `tests/standard/test_standard.py`, `tests/standard/allowlist.txt`) and the tools.
6. A move of a page changes every link and every path that names the old page in the same commit, `CLAUDE.md` and `.claude/agents/*.md` included: docs, code comments, tests and tools (for example `skull-formats.md` in `state_json.c`, `engine/save.c`, `data/mve.h`, the tests of graphics, music and champion rules, `tests/standard/test_standard.py`, `tests/standard/allowlist.txt`, `tools/decompile-skull.sh`; the paths after the code PRD). `test_docs.py` checks the links; a search for the old paths checks the plain paths.
7. `test_vocabulary.py` reads the name tables of the protocol in `src/control/` after the code PRD: the file-scope name arrays of the state JSON and the action table of the turns. The plan names the files and tables. The test skips the key table (`key_names`, a struct table, as `dm` skips its key table) and `creature_names`, which the program fills at run time.
8. The README names no email address and no personal path.
9. `docs/reference/symbols/*.md` are research notes about the original: they move with only their paths changed, and the "no history" check skips them. `docs/reference/reference-crosscheck.md` gets no such exception: the docs-audit pass rewrites its step notes ("Step 8", "Step 10", "Step 16") as plain text.
10. The order of the work:
    1. `test_docs.py`, with a fix of each link that is broken now. From then on it checks each move.
    2. `skull-symbols.csv`, `skull-map.md`, `reference-crosscheck.md` and `skull-notes/` move to `docs/reference/`.
    3. `skull-formats.md` splits into `docs/formats/` and `docs/rules/`; §16 goes to a section "Deviations from the original" of `turnbased-mode.md` until step 5; the old page goes. The "Known deviations" of the checklist move in the same step.
    4. The new pages `docs/architecture.md`, `docs/development.md` and `docs/decompiling.md`.
    5. `llm-control.md`, `turnbased-mode.md` and `llm-playbook.md` become `docs/llm/llm-mode.md`, `docs/llm/llm-runs.md` and `docs/llm/llm-playbook.md`; the C API and the integration in `main.c` go to `docs/architecture.md`, the tests to `docs/development.md`; then `test_vocabulary.py`.
    6. `advanced-mode.md` goes into the new `docs/manual.md`; the checklist loses its dated sections.
    7. `README.md` with its screenshot.
    8. `CLAUDE.md` names the new pages (the moved paths changed in steps 2 to 6), then one docs-audit pass over all pages.

## Invariants

1. Only `docs/`, `README.md`, `CLAUDE.md`, `.claude/agents/*.md`, the doc tests and the paths of Requirements 5 and 6 change.
2. `docs/superpowers/` and `CHANGELOG.md` do not change.
3. The full suite passes after each commit.

## Known consequences

- The dated result sections of the checklist and the step names in the headings go; the git history keeps them.
- The plans in `docs/superpowers/` keep the old paths of the pages.

## Decisions

| Topic | Decision | Reason |
|---|---|---|
| Order | After the code PRD. | User answer: code first, docs in the next step. The pages describe the new layout. |
| Doc checks | `test_docs.py` and `test_vocabulary.py` are part of this PRD. | User answer. |
| `docs/rules/` | Only the rules that the current pages hold, no new research. | The goal is the structure of `dm`, not new content. |
| One LLM mode page | `llm-control.md` and `turnbased-mode.md` merge into `docs/llm/llm-mode.md`. | `dm` has one page; the two pages overlap (start, protocol, `dm2-play`). |
| Advanced mode | Moves into `docs/manual.md`. | `dm` describes the modes in its player manual. |
| Start of the game | A normal start in `docs/manual.md`; the start of the turn-based mode in `docs/llm/llm-mode.md`, with a link between them. | One fact on one page (Requirement 2). |
| Manual test checklist | Stays in `docs/`, without the dated result sections. | `dm2` has no private research repository; the results are history. |
| C API of the turn-based mode | Moves to `docs/architecture.md`. | The C API is architecture, not protocol. |
| `SKULL.EXE` (LE) | No format page; `docs/reference/skull-map.md` describes the program. | No current page describes the LE format; a page would be new research. |
| README links | Each top-level page and each page of `docs/formats/`, `docs/rules/`, `docs/llm/` and `docs/reference/`; no link to the range notes and the CSV. | `dm` links pages, not folders; the folders have no index page. |
| Checklist results | The results and dates of the items go; the boxes are `[ ]` again. | The checklist is for the next run; the results are history, and the git history keeps them. |
| Step notes of `reference-crosscheck.md` | The docs-audit pass rewrites them. | They name plan steps; the page is a current reference, not a raw note. |
| Open questions of `skull-map.md` | They stay in `docs/reference/skull-map.md`. | They are questions about the original, not open work of the port. |
| Screenshot | `docs/images/screenshot.png`, as `dm`. | |
| Order of the new pages | `architecture.md` and `development.md` come before the LLM pages. | The merge of `turnbased-mode.md` moves sections into them. |
| Style check | The docs-audit pass checks the style; no test. | ASD-STE100 and one term per concept need a reader, not a pattern. |

## Left to the plan

- The section map of each old page that splits, merges or loses sections (`skull-formats.md` with its preamble, `llm-control.md`, `turnbased-mode.md`, `advanced-mode.md`, `manual-test-checklist.md`): each section to a section of a target page, or to the removal list.
- The list of facts that the plan removes, with the reason (duplicate, history, wrong).
- The screenshot of the README.
