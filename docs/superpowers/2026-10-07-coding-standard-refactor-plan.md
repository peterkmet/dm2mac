# Coding standard and source layers: plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task by task. Steps use checkbox (`- [ ]`) syntax for tracking.

Approved by the user on 2026-10-07 (design in chat, no PRD, variant 1). Branch: `feature/2026-10-07-coding-standard-refactor`.

**Goal:** Give dm2 the folder layout, the coding standard, and the automated standard test of the `dm` repository. Record all open violations in an allowlist. The cleanup of the violations is not in this plan.

**Architecture:** Four source layers in subfolders of `port/src/`: `data/` ⊂ `engine/` ⊂ `ui/` ⊂ `platform/`. A layer includes only its own headers and the headers of lower layers. `tests/test_standard.py` checks the rules of `docs/coding-standard.md` on each run. `tests/standard_allowlist.txt` holds every open violation. Later cleanup plans remove the entries module by module.

**Tech stack:** C11, clang, SDL3, Make. Python 3.14, uv, pytest, ctypes.

**Spec:** this plan and the reference implementation in `../dm`: `docs/coding-standard.md`, `tests/test_standard.py`, `port/Makefile`. Decisions from the chat on 2026-10-07:

- Variant 1: folders, standard, standard test, and allowlist. No cleanup of hex literals, decimal literals, names, or `engine/` → `ui/` includes.
- `src/dm2tools` stays. CLAUDE.md requires the `src/` layout for Python.
- `decomp/` moves to `reference/decomp/` (git-ignored), as in `dm`.
- `scripts/test-suite` moves to `tools/test-suite`. `TODO.md` moves to `docs/todo.md`.

## Global constraints

- Baseline: `uv run pytest -q` gives `401 passed` on 2026-10-07. Each task ends with the full suite green. New tests add to this count.
- No change of C code other than file moves. No change of game behavior. If a task needs a code change to pass, stop and report.
- Keep existing comments (CLAUDE.md section 4).
- Use `git mv` for tracked files, so that `git log --follow` keeps the history.
- Test failure handling (CLAUDE.md section 10): use superpowers:systematic-debugging. Hard stop after 3 failed attempts in one task. When a change breaks existing tests, revert to the last commit of the task before a new approach.
- Commits: Conventional Commits. Author email `55005408+peterkmet@users.noreply.github.com`. No co-author. No push.
- After each task, the `reviewer` agent checks the task. The next task starts only after the user confirms.

## Review focus

The tests do not cover these risks well. The reviewer checks them on each task:

1. `make -C port` builds from stale objects or misses a moved file. Expected: a build after `rm -f build/dm2 build/libdm2.dylib` builds `build/dm2` and `build/libdm2.dylib` with no warnings that were not there before.
2. `build/dm2` links SDL only through `platform/`. Expected: `main.c` and `platform.c` are the only files outside `$(GAME)`, as before.
3. `tests/test_symbols.py::test_functions_match_decomp` skips because `decomp/SKULL.c` moved. Expected: the test runs and passes when `reference/decomp/SKULL.c` exists.
4. `tools/test-suite` runs pytest in the wrong folder after the move. Expected: it runs from the repository root.
5. The allowlist hides a violation class that the test cannot see, for example a header with no functions. Expected: each allowlist key comes from a real test output.

## File map

The target folder of each existing file in `port/src/`:

| Folder | Files |
|---|---|
| `port/src/data/` | `gdat.c/.h`, `gfx.c/.h`, `hmp.c/.h` |
| `port/src/engine/` | `action.c/.h`, `ai.c/.h`, `ai_data.c`, `ai_move.c/.h`, `champ_action.c/.h`, `champion.c/.h`, `creature.c/.h`, `damage.c`, `dungeon.c/.h`, `game.c/.h`, `hooks.c/.h`, `inventory.c/.h`, `item.c/.h`, `magic.c/.h`, `merchant.c/.h`, `minion.c/.h`, `money.c/.h`, `move.c/.h`, `projectile.c/.h`, `random.c/.h`, `recruit.c`, `save.c/.h`, `sensor.c/.h`, `sound_request.c/.h`, `timeline.c/.h`, `weather.c/.h` |
| `port/src/ui/` | `advanced.c/.h`, `automap.c/.h`, `dialog.c/.h`, `draw.c/.h`, `fm.c/.h`, `icon.c/.h`, `input.c/.h`, `music.c/.h`, `opl3.c/.h`, `opl3-LICENSE`, `session.c/.h`, `sound.c/.h`, `text.c/.h`, `ui.c/.h`, `video.c/.h`, `view.c/.h`, `zone.c/.h` |
| `port/src/platform/` | `main.c`, `platform.c/.h`, `scale.c/.h`, `control.c/.h` |

`ui/` is the presentation layer: picture, sound output, music, input, menus, and the advanced mode. `platform/` holds SDL, the program entry, the scaler, and the control socket.

---

### Task 1: Folder cleanup

**Files:**
- Move: `scripts/test-suite` → `tools/test-suite`, `TODO.md` → `docs/todo.md`
- Move (not tracked): `decomp/` → `reference/decomp/`
- Modify: `.gitignore`, `tools/decompile-skull.sh`, `tests/test_symbols.py:39`, `tests/conftest.py:26`, `docs/reference-crosscheck.md`, `docs/skull-formats.md`, `docs/skull-map.md`, `docs/skull-notes/ranges.md`
- Test: `tests/test_symbols.py`

- [ ] **Step 1: Create the branch.** `git switch -c feature/2026-10-07-coding-standard-refactor`
- [ ] **Step 2: Move the tracked files.**

  ```sh
  git mv scripts/test-suite tools/test-suite
  git mv TODO.md docs/todo.md
  ```

  `tools/test-suite` starts with `cd "$(dirname "$0")/.."`. The folder depth is the same, so the line stays.
- [ ] **Step 3: Move the decompiled code.** `mkdir -p reference && mv decomp reference/decomp`
- [ ] **Step 4: Update `.gitignore`.** Replace the line `decomp` with `reference/`.
- [ ] **Step 5: Update the paths.** Replace `decomp/SKULL.c` with `reference/decomp/SKULL.c` in these files:
  - `tools/decompile-skull.sh`: the comment line 2 and the output argument on line 11. On line 6, replace `mkdir -p build decomp` with `mkdir -p build reference/decomp`.
  - `tests/test_symbols.py:39`: `decomp = ROOT / "reference/decomp/SKULL.c"`.
  - `docs/reference-crosscheck.md`, `docs/skull-formats.md`, `docs/skull-map.md`, `docs/skull-notes/ranges.md`.

  Do not change `docs/superpowers/` and `CHANGELOG.md`. They record history.
- [ ] **Step 6: Update the comment in `tests/conftest.py:26`.** Replace `scripts/test-suite` with `tools/test-suite`.
- [ ] **Step 7: Check that no old path is left.**

  ```sh
  grep -rn 'scripts/test-suite\|[^/]decomp/SKULL\|TODO\.md' --exclude-dir=.venv --exclude-dir=.git --exclude-dir=reference --exclude-dir=superpowers --exclude=CHANGELOG.md .
  ```

  Expected: no output.
- [ ] **Step 8: Run the symbol test.** `uv run pytest tests/test_symbols.py -v -rs -p no:xdist`. Expected: `test_functions_match_decomp` PASSES. It must not skip.
- [ ] **Step 9: Run the full suite.** `tools/test-suite`. Expected: `401 passed`.
- [ ] **Step 10: Commit.** `git add -A && git commit -m "chore: decompiled code in reference/, test-suite in tools/, todo in docs/"`

---

### Task 2: Source layers and the layer check

**Files:**
- Move: every file of `port/src/` to its folder in the file map
- Modify: `port/Makefile`, docstrings in `tests/test_automap.py:1`, `tests/test_control.py:1`, `tests/test_modes.py:1`, `tests/test_music.py:25`
- Create: `tests/test_standard.py`, `tests/standard_allowlist.txt`

**Interfaces:**
- Produces: allowlist line format `<check> <target> <reason>`. For `layer`, the target is `<path relative to port/src>:<header>`, for example `layer engine/move.c:ui.h`. Open violations have the reason `open: module cleanup`.
- Produces: `tests/test_standard.py` with the helpers `sources()`, `rel()`, `allowed()`, `report()`. Task 3 adds the other checks to this file.

- [ ] **Step 1: Write `tests/test_standard.py` with the layer check.**

  ```python
  """Checks port/src against docs/coding-standard.md. Open exceptions: tests/standard_allowlist.txt."""

  import re
  from pathlib import Path

  ROOT = Path(__file__).resolve().parent.parent
  SRC = ROOT / "port" / "src"
  ALLOWLIST = Path(__file__).with_name("standard_allowlist.txt")
  VENDORED = {"opl3.c", "opl3.h"}
  LAYERS = ["data", "engine", "ui", "platform"]
  INCLUDE = re.compile(r'^\s*#include\s+"([\w./]+)"', re.M)


  def sources():
      return sorted(p for p in SRC.rglob("*.[ch]") if p.name not in VENDORED)


  def rel(path):
      return path.relative_to(SRC).as_posix()


  def allowed():
      """The (check, target) pairs of the allowlist. Each line: check target reason."""
      pairs = set()
      for line in ALLOWLIST.read_text().splitlines():
          if line.strip() and not line.startswith("#"):
              check, target, _reason = line.split(maxsplit=2)
              pairs.add((check, target))
      return pairs


  def report(check, found):
      """Fails with one allowlist key and the evidence for each violation."""
      assert not found, f"{check} violations (allowlist key, evidence):\n" + "\n".join(found)


  def test_layers():
      ok = allowed()
      home = {p.name: p.parent.name for p in SRC.rglob("*.h")}
      found = []
      for path in sources():
          layer = path.parent.name
          assert layer in LAYERS, f"{rel(path)} is not in a layer folder"
          text = path.read_text(errors="replace")
          for header in INCLUDE.findall(text):
              target = home.get(header)
              key = f"{rel(path)}:{header}"
              if target and LAYERS.index(target) > LAYERS.index(layer) and ("layer", key) not in ok:
                  found.append(f"layer {key}  | {layer} includes {target}")
          if layer != "platform" and re.search(r"#include\s+<SDL", text):
              found.append(f"layer {rel(path)}:SDL  | SDL outside platform/")
      report("layer", found)
  ```

  Create `tests/standard_allowlist.txt` with the header only:

  ```text
  # Open exceptions of tests/test_standard.py. One per line: <check> <target> <reason>.
  # Targets are paths relative to port/src/, path:header, constant names, or function names.
  ```

- [ ] **Step 2: Run it.** `uv run pytest tests/test_standard.py -q -p no:xdist`. Expected: FAIL with `port/src/... is not in a layer folder`. Then record the warning count of the flat layout: `rm -f build/dm2 build/libdm2.dylib && make -C port 2>&1 | grep -c warning`.
- [ ] **Step 3: Move the files.** Run this script from the repository root. It moves each file of the file map with `git mv`.

  ```sh
  cd port/src
  mkdir -p data engine ui platform
  git mv gdat.c gdat.h gfx.c gfx.h hmp.c hmp.h data/
  git mv action.c action.h ai.c ai.h ai_data.c ai_move.c ai_move.h champ_action.c champ_action.h \
         champion.c champion.h creature.c creature.h damage.c dungeon.c dungeon.h game.c game.h hooks.c hooks.h \
         inventory.c inventory.h item.c item.h magic.c magic.h merchant.c merchant.h minion.c minion.h \
         money.c money.h move.c move.h projectile.c projectile.h random.c random.h recruit.c save.c save.h \
         sensor.c sensor.h sound_request.c sound_request.h timeline.c timeline.h weather.c weather.h engine/
  git mv advanced.c advanced.h automap.c automap.h dialog.c dialog.h draw.c draw.h fm.c fm.h icon.c icon.h \
         input.c input.h music.c music.h opl3.c opl3.h opl3-LICENSE session.c session.h sound.c sound.h \
         text.c text.h ui.c ui.h video.c video.h view.c view.h zone.c zone.h ui/
  git mv main.c platform.c platform.h scale.c scale.h control.c control.h platform/
  ls
  cd ../..
  ```

  Expected: the last `ls` shows only `data engine platform ui`.
- [ ] **Step 4: Update `port/Makefile`.** The `#include "x.h"` lines stay. The Makefile adds one `-I` for each layer.

  ```make
  CC = clang
  CFLAGS = -std=c11 -Wall -Wextra -O2 -g
  SDL = $(shell brew --prefix sdl3)
  B = ../build
  LAYERS = data engine ui platform
  INCLUDES = $(addprefix -Isrc/,$(LAYERS))
  GAME = $(filter-out src/platform/main.c src/platform/platform.c,$(wildcard $(addsuffix /*.c,$(addprefix src/,$(LAYERS)))))
  SRC = $(wildcard $(addsuffix /*.[ch],$(addprefix src/,$(LAYERS))))

  all: $(B)/dm2 $(B)/libdm2.dylib

  $(B)/dm2: $(SRC)
  	@mkdir -p $(B)
  	$(CC) $(CFLAGS) $(INCLUDES) -I$(SDL)/include -o $@ src/platform/main.c src/platform/platform.c $(GAME) -L$(SDL)/lib -lSDL3

  $(B)/libdm2.dylib: $(SRC)
  	@mkdir -p $(B)
  	$(CC) $(CFLAGS) $(INCLUDES) -dynamiclib -o $@ $(GAME)

  clean:
  	rm -rf $(B)

  .PHONY: all clean
  ```

  Recipe lines start with a tab.
- [ ] **Step 5: Build from clean.** `rm -f build/dm2 build/libdm2.dylib && make -C port 2>&1 | grep -c warning`. Expected: the same number as in Step 2.

  Do not run `make -C port clean`. It deletes all of `build/`, also `build/dos-nointro/` and the logs.
- [ ] **Step 6: Update the test docstrings.** Replace `port/src/automap.c` with `port/src/ui/automap.c`, `advanced.c` with `ui/advanced.c` in `tests/test_automap.py:1` and `tests/test_modes.py:1`. Replace `port/src/control.c` with `port/src/platform/control.c` in `tests/test_control.py:1`. Replace `port/src/hmp.c` with `port/src/data/hmp.c` in `tests/test_music.py:25`.
- [ ] **Step 7: Run the layer test.** `uv run pytest tests/test_standard.py -q -p no:xdist`. Expected: FAIL with `layer violations`. The list shows each `engine/` file that includes a `ui/` header.
- [ ] **Step 8: Fill the allowlist from the test output.** Run this from the repository root. It runs each check without the allowlist entries and appends one line per key.

  ```sh
  uv run python - <<'EOF' >> tests/standard_allowlist.txt
  import importlib.util
  spec = importlib.util.spec_from_file_location("std", "tests/test_standard.py")
  std = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(std)
  keys = []
  for name in sorted(n for n in dir(std) if n.startswith("test_")):
      try:
          getattr(std, name)()
      except AssertionError as error:
          keys += [line.split("  |")[0] for line in str(error).splitlines()[1:]]
  print("\n".join(f"{key} open: module cleanup" for key in dict.fromkeys(keys)))
  EOF
  ```

  Expected: no line `layer <file>:SDL`. If such a line comes, stop and report: a file outside `platform/` includes SDL.
- [ ] **Step 9: Run the layer test.** `uv run pytest tests/test_standard.py -q -p no:xdist`. Expected: PASS.
- [ ] **Step 10: Run the full suite.** `tools/test-suite`. Expected: `402 passed`.
- [ ] **Step 11: Commit.** `git add -A && git commit -m "refactor: port sources in data, engine, ui, and platform layers"`

---

### Task 3: Coding standard and the other checks

**Files:**
- Modify: `docs/coding-standard.md`, `tests/test_standard.py`, `tests/standard_allowlist.txt`, `docs/todo.md`

**Interfaces:**
- Consumes: `sources()`, `rel()`, `allowed()`, `report()`, and the allowlist format of Task 2.
- Produces: allowlist checks and targets:

| Check | Target | Rule |
|---|---|---|
| `hex` | file path relative to `port/src/` | 1 |
| `decimal` | file path | 1 |
| `duplicate` | constant name | 1 |
| `record` | file path | 2 |
| `decompiler` | file path | 3 |
| `bool` | header path | 3 |
| `long` | `path:function` | 3 |
| `prefix` | header path | 7 |
| `numbered` | function name | 7 |
| `layer` | `path:header` | 8 |

- [ ] **Step 1: Write the new `docs/coding-standard.md`.** Replace the whole file with:

  ```markdown
  # C port coding standard

  The C port is a readable reimplementation of the game. It is not a cleaned-up decompiler listing. The tests prove that the behavior matches the original. This standard makes the code readable.

  ## Rules

  1. Name every game value. Use an `enum` or a `#define` with a name that tells the game meaning.
     - Thing types: `THING_DOOR`, `THING_SENSOR`, `THING_GROUP`, and so on, not `3`.
     - No thing: `THING_NONE`, not `0xffff`. End of list: `THING_ENDOFLIST`.
     - Directions, cells, champion slots (`SLOT_HAND_READY`, `SLOT_COUNT`), statistics, skills, square types, sensor types, door states, timeline event types, commands.
     - Message and text numbers: a named constant for each string id, for example `TEXT_RESURRECTED`.
     - Graphic numbers: named constants in `port/src/data/gfx.h`, for example `GFX_WALLSET_FIRST`. Layout zone numbers: named constants in `port/src/ui/ui.h`, for example `ZONE_VIEWPORT`.
     - DM2 systems: money types (`COIN_GOLD`), minion types (`MINION_GUARD`), weather states (`WEATHER_RAIN`), and Skullkeep machine states each get named constants. `docs/skull-symbols.csv` holds the names.
     - Plain numbers are allowed only for 0, 1, -1, and 2 when it halves or doubles a value. Every other number is a named constant, also decimal game values such as divisors, limits, and thresholds.
     - Each constant has one definition, in the header of the module that owns it. Other modules include that header. They do not define the value again under the same or another name.
  2. Do not index raw record bytes outside one accessor module. Put field access in small functions or macros with names, for example `sensor_type(t)`, `sensor_disable(t)`, `door_state(t)`, `group_count(t)`. One module (`dungeon.c` and `dungeon.h`) owns the record layout.
  3. Write C, not decompiler output.
     - No names like `uVar1`, `iVar3`, `local_10`, `param_2`.
     - Game values keep the 16-bit types of the original (`int16_t`, `uint16_t`). One value has one type on its whole path: field, parameter, local variable, and return value. Then no cast is needed to pass it on. Use `int` only for values that are not game values, for example sizes, file offsets, and loop counters over C arrays.
     - No casts that only repeat the x86 operand size of the Watcom code. A cast is allowed only where it changes the result (16-bit wrap, sign, truncation). Add a short comment when the reason is not obvious.
     - Do not copy Watcom register-call artifacts (EAX, EDX, EBX, ECX argument shuffling) into the C code. Use normal named parameters.
     - Predicates (`is_*`, `has_*`, `can_*`) return `bool` from `<stdbool.h>`. Flags in game records keep their stored type.
     - Use `for`, `while`, `switch`, and early `return`. Do not use `goto`, except for one cleanup label in a function that frees memory.
     - One function does one game task. Split a long original function when parts have their own meaning. A function longer than 60 lines needs a reason in its comment, or a split.
  4. Each non-trivial function has a one-line comment that tells what it does in game terms. The comment does not repeat the code, and does not give the original address. The address belongs in `docs/skull-symbols.csv`.
  5. Keep original quirks only when they change what the player sees. Fix the original bugs that are clearly bugs (stale registers, overflows), and mark each fix with a short comment.
  6. Read the data files (`DUNGEON.DAT`, `GRAPHICS.DAT`, saves) at run time. Do not copy their content into the source. A table that exists only in `SKULL.EXE` goes into the source as a named `const` array with a one-line comment that tells its game meaning. No unexplained numbers or strings.
  7. Names follow one scheme.
     - Functions: `<prefix><verb>_<object>` in `snake_case`. Each header states its prefixes in one comment line, for example `/* Prefixes: gfx_ */` in `gfx.h`. A prefix belongs to one header only, and no prefix starts with the prefix of another header.
     - Global variables: `g_` prefix. Module-private globals and functions are `static`.
     - Types: `PascalCase`. Constants and enum values: `UPPER_CASE`.
     - No event numbers or original addresses in names. Name the game task, not the number.
  8. The source has four layers in subfolders of `port/src/`: `data/`, `engine/`, `ui/`, and `platform/`. A file includes only headers of its own layer and of the layers before it in this list. Game rules in `engine/` do not call the user interface: they set redraw flags. Exception: a hook that `ui/` sets, for an event that must run the user interface inside the rule. Only `platform/` includes SDL.
  9. Little-endian read helpers (`le16`, `le32`) have one definition in one header. Modules do not define their own copies.

  ## Check before a step is done

  `tests/test_standard.py` checks `port/src/` and fails on:

  - hex literals outside constant definitions,
  - decimal literals of 10 or more outside constant definitions,
  - constant names defined in more than one file,
  - raw record access (`dungeon_thing_record(...)[`) outside `dungeon.c` and `dungeon.h`,
  - decompiler names,
  - predicates that do not return `bool`,
  - functions longer than 60 lines,
  - headers without a `Prefixes` line, prefix collisions, and functions outside the prefixes of their header,
  - event numbers in function names,
  - includes of a higher layer, and SDL outside `platform/`.

  `tests/standard_allowlist.txt` lists the accepted exceptions, one per line with a reason. The reason `open: module cleanup` marks a violation that a later cleanup removes. `opl3.c` and `opl3.h` are vendored code (`opl3-LICENSE`), and the test does not check them.

  A reader who knows Dungeon Master II can understand each function without opening the decompiled code.
  ```

- [ ] **Step 2: Add the checks to `tests/test_standard.py`.** Add `from collections import defaultdict` to the imports. Add these constants after `INCLUDE`:

  ```python
  MAX_FUNCTION_LINES = 60

  HEX = re.compile(r"\b0x[0-9a-fA-F]{2,}\b")
  DECIMAL = re.compile(r"(?<![\w.])[1-9][0-9]+(?![\w.])")
  DECOMPILER = re.compile(r"\b(?:[uisbcp]Var\d+|local_[0-9a-f]+|arg_[0-9a-f]+|param_\d+|in_[AD][0-7]\w*|CONCAT\d+|DAT_[0-9a-f]+|FUN_[0-9a-f]+)\b")
  RECORD = re.compile(r"dungeon_thing_record\([^()]*\)\s*\[")
  CONST_DEF = re.compile(r"\s*(?:static\s+)?const\s+\w+\s+[A-Z][A-Z0-9_]*\s*=")
  ENUM = re.compile(r"\benum\b[^{};]*\{([^}]*)\}")
  DEFINE = re.compile(r"^\s*#define\s+([A-Z][A-Z0-9_]*)\b", re.M)
  DECL = re.compile(r"^((?:(?:static|inline|const|struct|unsigned)\s+)*[A-Za-z_]\w*[\s*]+)([a-z_]\w*)\s*\(", re.M)
  PREFIXES = re.compile(r"^/\* Prefixes: ([\w ]+?) \*/$", re.M)
  NUMBERED = re.compile(r"events?_?\d|_\d+_to_\d+")
  PREDICATE = re.compile(r"(?:^|_)(?:is|has|can)_")
  FUNC_START = re.compile(r"^(?:static\s+)?[A-Za-z_][\w *]*?\b([a-z_]\w*)\s*\([^;]*$")
  ```

  Add these functions after `report`:

  ```python
  def strip(text):
      """The text without comments and string or character literals; line numbers stay."""
      text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group().count("\n"), text, flags=re.S)
      text = re.sub(r"//[^\n]*", "", text)
      return re.sub(r"\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'", '""', text)


  def code_lines(path):
      """Numbered lines that are not constant definitions."""
      text = ENUM.sub(lambda m: "\n" * m.group().count("\n"), strip(path.read_text(errors="replace")))
      for number, line in enumerate(text.splitlines(), 1):
          if not line.lstrip().startswith("#") and not CONST_DEF.match(line):
              yield number, line


  def line_check(check, pattern, owners=()):
      ok = allowed()
      found = []
      for path in sources():
          if (check, rel(path)) in ok or path.name in owners:
              continue
          found += [f"{check} {rel(path)}  | {n}: {line.strip()}" for n, line in code_lines(path) if pattern.search(line)]
      report(check, found)


  def declarations(path):
      """(return type, name) of each function that a header declares or defines inline."""
      text = strip(path.read_text(errors="replace"))
      return [(ret.replace("static", "").replace("inline", "").strip(), name) for ret, name in DECL.findall(text)
              if not ret.lstrip().startswith(("typedef", "return", "extern"))]


  def test_no_hex_literals():
      line_check("hex", HEX)


  def test_no_decimal_literals():
      line_check("decimal", DECIMAL)


  def test_no_decompiler_names():
      line_check("decompiler", DECOMPILER)


  def test_raw_record_access_only_in_accessors():
      line_check("record", RECORD, owners={"dungeon.c", "dungeon.h"})


  def test_each_constant_has_one_definition():
      where = defaultdict(set)
      for path in sources():
          text = strip(path.read_text(errors="replace"))
          names = set(DEFINE.findall(text))
          for body in ENUM.findall(text):
              names.update(m.group(1) for item in body.split(",") if (m := re.match(r"\s*([A-Z][A-Z0-9_]*)\b", item)))
          for name in names:
              where[name].add(rel(path))
      ok = allowed()
      report("duplicate", [f"duplicate {name}  | {sorted(paths)}" for name, paths in sorted(where.items())
                           if len(paths) > 1 and ("duplicate", name) not in ok])


  def test_headers_state_unique_prefixes():
      ok = allowed()
      owner, found = {}, []
      for path in sources():
          if path.suffix != ".h" or ("prefix", rel(path)) in ok:
              continue
          names = [name for _, name in declarations(path)]
          match = PREFIXES.search(path.read_text(errors="replace"))
          if not match:
              if names:
                  found.append(f"prefix {rel(path)}  | no Prefixes line")
              continue
          prefixes = match.group(1).split()
          for prefix in prefixes:
              for other, other_path in owner.items():
                  if prefix.startswith(other) or other.startswith(prefix):
                      found.append(f"prefix {rel(path)}  | {prefix} collides with {other} of {other_path}")
              owner[prefix] = rel(path)
          found += [f"prefix {rel(path)}  | {name}" for name in names if not name.startswith(tuple(prefixes))]
      report("prefix", found)


  def test_no_event_numbers_in_function_names():
      ok = allowed()
      report("numbered", [f"numbered {name}  | {rel(path)}" for path in sources() if path.suffix == ".h"
                          for _, name in declarations(path) if NUMBERED.search(name) and ("numbered", name) not in ok])


  def test_predicates_return_bool():
      ok = allowed()
      report("bool", [f"bool {rel(path)}  | {ret} {name}" for path in sources() if path.suffix == ".h"
                      and ("bool", rel(path)) not in ok
                      for ret, name in declarations(path) if PREDICATE.search(name) and ret != "bool"])


  def test_functions_are_short():
      ok = allowed()
      found = []
      for path in sources():
          if path.suffix != ".c":
              continue
          name = start = None
          for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
              match = FUNC_START.match(line)
              if match and name is None:
                  name, start = match.group(1), number
              elif line.startswith("}") and name:
                  key = f"{rel(path)}:{name}"
                  if number - start + 1 > MAX_FUNCTION_LINES and ("long", key) not in ok:
                      found.append(f"long {key}  | {number - start + 1} lines")
                  name = None
      report("long", found)
  ```

- [ ] **Step 3: Run the new checks.** `uv run pytest tests/test_standard.py -q -p no:xdist`. Expected: `test_layers` PASSES. The other checks FAIL with their violation lists, except `test_no_decompiler_names`, which PASSES (no decompiler names today).
- [ ] **Step 4: Check the test for false hits.** Read 10 sample lines of each failing check. A false hit is a line that the rule does not forbid, for example a hex value in a constant definition over two lines. If a check has more than a few false hits, stop and report the samples. Do not change the rule to hide real violations.
- [ ] **Step 5: Fill the allowlist.** Run the script of Task 2, Step 8 again. It appends the keys of the new checks. The `layer` keys are already in the allowlist, so they do not come again.
- [ ] **Step 6: Run the standard test.** `uv run pytest tests/test_standard.py -q -p no:xdist`. Expected: all PASS.
- [ ] **Step 7: Add the cleanup to `docs/todo.md`.** Append:

  ```markdown
  - Coding standard cleanup, module by module: remove the `open: module cleanup` entries of `tests/standard_allowlist.txt` (hex and decimal literals, prefixes, long functions, `engine/` includes of `ui/` headers).
  - Rule 9 of `docs/coding-standard.md`: one header for `le16` and `le32`. Today `gdat.c`, `zone.c`, `save.c`, `recruit.c`, and `dungeon.c` each define their own copy.
  ```

- [ ] **Step 8: Run the full suite.** `tools/test-suite`. Expected: `411 passed`.
- [ ] **Step 9: Commit.** `git add -A && git commit -m "test: coding standard checks with an allowlist of the open violations"`

---

### Task 4: Release notes

**Files:**
- Modify: `CHANGELOG.md`

- [ ] **Step 1: Add an entry to `CHANGELOG.md`.** Use the format of the existing entries. Content:
  - `port/src/` has the layers `data/`, `engine/`, `ui/`, and `platform/`.
  - `docs/coding-standard.md` has rules 7 to 9 and a check list. `tests/test_standard.py` checks the rules. `tests/standard_allowlist.txt` lists the open violations.
  - The decompiled code is in `reference/decomp/`. `tools/test-suite` replaces `scripts/test-suite`. `docs/todo.md` replaces `TODO.md`.
- [ ] **Step 2: Run the full suite.** `tools/test-suite`. Expected: the tree already passed, or all pass.
- [ ] **Step 3: Commit.** `git commit -am "docs: changelog for the coding standard and the source layers"`
- [ ] **Step 4: Wait.** Merge to `main` and push only after the user confirms (CLAUDE.md section 3, step 7).
