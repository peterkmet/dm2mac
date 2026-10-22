# Coding standard

The port is a readable reimplementation of the game. It is not a cleaned-up decompiler listing. The tests
prove that the behavior matches the original. A reader who knows Dungeon Master II can understand each function
without opening the decompiled code.

Every rule names its check in `tests/standard/test_standard.py` in brackets, or says "review only" when no
mechanical check is possible, or names its exception. `tests/standard/allowlist.txt` holds the accepted
exceptions, one per line: `<check> <target> <reason>`. A reason is `rule <n.m>: <text>` for an exception that
this document names. The rules keep the numbers of the standard of the Dungeon Master port `dm`; a rule of `dm`
that this port cannot use keeps its number with "No rule in dm2".

## 1. Layers and modules

1. The source is `src/` with five layers in dependency order: `data/` (file formats of the game files, byte
   order, the operating system services), `engine/` (game rules), `ui/` (screens, the dungeon view, input
   mapping, sound, music, movies), `control/` (the turn-based protocol for a program player), `platform/`
   (SDL, the control socket, the scale filters, `main`). A file includes only headers of its own layer and of
   the layers before it. [layer]
2. The public headers of a layer are in `src/<layer>/include/` (`src/engine/include/game.h`); a private
   `<module>_internal.h` is next to its `.c` files. The include path of a layer holds only the `include/`
   folders of its own layer and of the layers before it, and `-isystem src/third_party`, so the compiler rejects
   an include of a later layer. An include names only the file: `#include "game.h"`. Header names are unique
   across the layers. [layer]
3. Only `platform/` includes SDL and operating system headers (`_WIN32`, POSIX, `<sys/...>`, `<unistd.h>`,
   `<dirent.h>`). The other layers reach the operating system through `data/os.h` (`os_log`, `os_fatal`,
   `os_get_full_path`) and its hooks, which `platform/` and the tests set. The socket is
   `platform/transport.c`; the Windows build has no socket and links `platform/transport_none.c`. [platform]
4. Game rules in `engine/` do not call the user interface. They set redraw flags (`Champion.redraw`,
   `REDRAW_*`), and they call a member of `g_hooks` (`engine/hooks.h`) for an event that must run the user
   interface inside the rule (`g_hooks.print_message`, `g_hooks.play_sound`). `ui/ui_hooks.c` sets every
   member. [layer]
5. `ui/` changes game state only through engine functions. Exceptions: redraw flags and hooks. Exception: the
   action area (`ui/action_area.c`: `area_update`, `show_open_panel`, `show_hand_panel`, `haste_jitter`) keeps
   the order of the original, which mixes rules and drawing. It writes `g_action.mode` and `g_action.result`,
   closes the open hand, opens a chest or a minion map when the panel redraws, and `haste_jitter` takes
   `rand_2bits` while it draws. A split would change the order of the original or need many hooks. Its
   allowlist lines give `rule 1.5`. [engine-write]
6. `control/` is a client of `ui/` and `engine/`; it holds no socket. The socket is `platform/transport.c`; it
   calls `control/`, never the reverse. [platform]
7. A module is one `.c` file with one public header. A module with several `.c` files has one public header
   and one `<module>_internal.h` for the shared private declarations; both carry the prefix of the module.
   The entry point `platform/main.c` is the one exception. It has no header. [module]
8. Each header states its prefixes in one comment line, `/* Prefixes: sensor_ */`. A prefix belongs to one
   module; no prefix starts with the prefix of another module. The header that owns a prefix declares every
   function with that prefix; the line also covers function-like macros. [prefix]
9. `src/third_party/` is vendored code (`opl3/`), the one folder of `src/` outside the layers. The folder is
   outside every check, including the format check. [layer]

## 2. Headers and includes

1. A header starts with the SPDX line, the guard `DM2_<LAYER>_<FILE>_H`, the prefix line, one purpose
   paragraph (what the module owns and what it must not do), then the includes: [header]

   ```c
   /* SPDX-License-Identifier: GPL-3.0-or-later */
   #ifndef DM2_ENGINE_SENSOR_H
   #define DM2_ENGINE_SENSOR_H
   /* Prefixes: sensor_ */

   /* Sensors: the field accessors of a sensor record, and what a sensor does when the party or a thing acts. */

   #include <stdbool.h>
   #include <stdint.h>
   #include "dungeon.h"
   ```

2. A header includes only what its declarations need; a struct that appears only behind a pointer is
   forward-declared. [include]
3. A `.c` file includes exactly the headers it uses, in this order: its own header; project headers by layer
   order and alphabetical inside a layer; third-party headers; system headers. `.clang-format` sorts them.
   The include categories of `.clang-format` name the headers of each layer. There is no umbrella header
   per layer. [format, include]
4. The one-line comment of a public function is in the header above its prototype. [comment]
5. A symbol that exists only for the tests carries `/* exported for the tests */` on its declaration.
   [dead-api]

## 3. Constants

1. Name every game value. Plain numbers are allowed only for 0, 1, -1, and for 2 when the 2 halves or doubles a
   value. Every other number is a named constant, also in an array size and in a loop bound. The base
   argument of `strtol` and its siblings is a plain number. The names cover thing types (`THING_DOOR`), no
   thing (`THING_NONE`) and the end of a list (`THING_ENDOFLIST`), directions, cells, champion slots
   (`SLOT_HAND_READY`), statistics, skills, square types, sensor types, door states, timeline event types,
   commands, and the systems of Dungeon Master II: money types, minion types (`MINION_GUARD`), weather states,
   and the states of the Skullkeep machines. [hex, decimal]
2. A constant is `#define NAME value` with the value written out; a negative value is in parentheses:
   `#define COLOR_NONE (-1)`. No `enum` for constants; `enum` names a result type only (rule 6.1). [enum]
3. Related constants are in one block without blank lines, with one comment line above the block that
   names the set; `clang-format` aligns the values. [block]
4. Notation is per block. A block of bit flags, masks, packed layouts and file format bytes is hex in every
   line, with the same digit width. A block of ids from the data files and from `SKULL.EXE` is hex too: GDAT
   categories and entries, sound entries, priorities and volumes, event types, command codes, key codes. A
   block of counts, sizes, ticks, percentages, coordinates, distances, divisors and thresholds is decimal in
   every line. A block never mixes the two. [notation]

   ```c
   /* Door flags of the door record. */
   #define DOOR_VERTICAL           0x0020
   #define DOOR_BUTTON             0x0040
   #define DOOR_BREAKABLE_BY_MAGIC 0x0080
   #define DOOR_BREAKABLE          0x0100

   /* Skill counts of a champion. */
   #define SKILL_COUNT      20
   #define SKILL_BASE_COUNT 4
   #define SUB_SKILL_COUNT  4
   ```

5. Each constant has one definition, in the header of the module that owns it; other modules include that
   header. They do not define the value again under the same or another name. [duplicate]
6. No rule in dm2: the port reads no engine image, so it has no image addresses (`KAOS_`) and no image
   tables.
7. Graphic, zone, icon and string numbers live in `data/ids.h`, grouped by kind (`GFX_` for the graphic entry
   numbers of `GRAPHICS.DAT`, `ZONE_`, `ICON_`, `TEXT_`), sorted by value, one name per value. A zone that a
   rule passes to a hook is in `data/ids.h` too; `engine/` includes it. The categories of the file format
   (`CAT_`, `GDAT_`) stay in `data/gdat.h`. `GFX_` names every entry number of `GRAPHICS.DAT`, the text entries
   included; `TEXT_` names the string ids; one block per id space (category and type). [duplicate; one name per
   value: review only]
8. No byte offset, event number or original address in a name. [numbered]
9. A packed field (two or more values in one word) has one accessor in the module that owns it
   (`timeline_time_map()`, `timeline_event_map()` in `engine/timeline.h`). No shift by 8, 16 or 24 at a call
   site, and no shift by a `*_SHIFT` constant of another module. [packed]
10. The byte order helpers `le16`, `le32`, `put_le16`, `put_le32` (little-endian, the DOS file formats) and
    `put_be32` (big-endian, the PNG files) are `static inline` functions in `data/le.h` only. No other file
    defines a byte order helper. [byteorder]
11. The record layout belongs to `engine/dungeon.h`: the field offsets and bits. The named accessors of a thing
    type can be in the header of its module (`sensor.h`, `projectile.h`, `item.h`, `ai.h`); they use the field
    constants of `dungeon.h`. No file outside `engine/dungeon.c` and `engine/include/dungeon.h` calls
    `dungeon_thing_record(`. Exception: the save image (`engine/save.c`) packs and unpacks whole thing records
    bit by bit; its allowlist line gives `rule 3.11`. [record]
12. Read the data files (`DUNGEON.DAT`, `GRAPHICS.DAT`, saves) at run time; do not copy their content into the
    source. A table that exists only in `SKULL.EXE` goes into the source as a named `const` array with a
    one-line comment that tells its game meaning; no unexplained number or string. The PC keyboard scan code
    tables (`control/control.c`, `platform/main.c`) stay in the source too. The allowlist names each file of
    these tables with the reason `rule 3.12: <table>`. [decimal, hex, allowlist]

## 4. Naming

1. Functions: `<prefix><verb>_<object>` in `snake_case`, at most four words after the prefix, at most 32
   characters. [name]
2. A static function has no module prefix. [static-prefix]
3. Record accessors are nouns after the prefix of the thing type: `<type>_<field>` and `<type>_set_<field>`
   (`sensor_type(t)`, `door_state(t)`, `group_set_dir(t, dir)`). [prefix]
4. Predicates (`is_`, `has_`, `can_`), also static ones, return `bool`. [bool]
5. Globals have the prefix `g_`. File-static variables have no `g_` and a descriptive name. A short name is
   allowed only for a loop index and for a variable used in a few adjacent lines. [global]
6. Types: `PascalCase`. Constants: `UPPER_CASE`. [review only]
7. No decompiler names (`uVar1`, `iVar3`, `local_10`, `param_2`). Do not copy the register-call artifacts of
   the Watcom code (argument shuffling through EAX, EDX, EBX, ECX); use normal named parameters. [decompiler;
   the register calls review only]
8. No ordinal (index + 1, 0 for none) in a name or in a variable; an index is -1 for none. Exception: an
   ordinal stays where a game record stores it. [ordinal]
9. A name says the game task, not an event number, a byte offset or an original address:
   `group_process_reaction_event`, not `group_process_events_29_to_41`. [numbered]
10. One term per concept: a `location` is a square of the current map (`x`, `y`); a `position` is a square
    with its map (`map`, `x`, `y`); a `square` is the square byte; a `cell` is one of the four cells of a
    square; a `thing` is a record reference; a `champion_index` is 0–3, -1 for none. [review only]

## 5. Types and values

1. One value has one type on its whole path (field, parameter, local, return value), so that no cast is needed
   to pass it on. Game values keep the 16-bit types of the original. The type of each game value kind: [type]

   | Value kind | Type | None |
   |---|---|---|
   | `champion_index`, `cell`, `direction`, `map`, `slot`, `x`, `y` | `int16_t` | -1 |
   | pixel coordinates and sizes | `int16_t` | |
   | `thing` | `uint16_t` | `THING_NONE` |
   | `ticks` | `uint16_t` | |
   | game time | `uint32_t` | |
   | C sizes, C array counts and indices, file offsets, SDL values | `int` or `size_t` | |

2. Every truth value is `bool`: predicates, locals, parameters, return values. A flag in a game record or a
   save file keeps its stored type. [bool]
3. No `long`, because it has 32 bits on Windows and 64 bits on macOS. A value wider than `int` uses a fixed-width
   type with the `<inttypes.h>` formats. [review only]
4. A cast is allowed only where it changes the result (16-bit wrap, sign, truncation), with a short comment
   when the reason is not obvious. No cast that only repeats the x86 operand size of the Watcom code, no double
   integer cast. [packed; the rest review only]
5. The port assumes an arithmetic right shift of a negative value. [review only]
6. One statement per line: no comma operator outside the header of a `for`, no chained assignment, at most
   one variable with an initializer per declaration. [format; review only]
7. Use `for`, `while`, `switch`, and early `return`. `goto` only for one cleanup label in a function that
   frees memory. [review only]
8. One function does one game task; split a long original function where its parts have their own meaning. A
   function is at most 60 lines, or its allowlist line gives the reason (one short case for each type of a
   game table). [long]

## 6. Results and parameters

1. Two outcomes: `bool`. More outcomes: an `enum` type with named values (`enum MoveResult`). A lookup
   returns `NULL`. A count returns the count. No `0/-1`, no `1/0/-1`, no value encoded in a return value
   (`index + 2`). [result]
2. At most five parameters; a function that needs more takes a struct. [params]
3. A square passes as `Location {int16_t x, y;}`, a square with its map as `Position {int16_t map, x, y;}`.
   [review only]
4. No flags packed into a parameter (`skill | flags`), no mode number that means two things (a mode and a
   tick count). [review only]
5. Two or more output parameters become one returned struct. [review only]
6. A result does not pass through a global; it passes through the return value or a struct out-parameter.
   [review only]

## 7. State

1. A global holds state that lives across calls. Module-private state is `static`. [dead-api]
2. A file with four or more file-scope variables keeps them in one `static struct` named after the module.
   [review only]
3. Every exported function and global has a use outside its file, or the test marker of rule 2.5. [dead-api]
4. Data of one entity lives in the struct of the entity, not in parallel arrays. [review only]
5. Session state lives in the `Session` struct of `engine/game.h`; the calls of the rules into the user
   interface live in `g_hooks` (`engine/hooks.h`). [review only]

## 8. File order

1. A `.c` file has this order: includes; constants; types; file-static state; functions. [order]
2. The public functions are in the order of the header. The header lists them by topic: setup and reset
   first, then queries, then commands; a one-line section comment names each topic in the header and in the
   `.c` file. [order]
3. A static helper is directly above the first function that calls it; a helper that several sections
   share is at the top of the functions. A forward declaration of a static function is allowed only for
   mutual recursion, with a comment that names the pair. [order]

   ```c
   /* SPDX-License-Identifier: GPL-3.0-or-later */
   #include "sensor.h"
   #include "dungeon.h"
   #include <string.h>

   /* Effects of a sensor on its target. */
   #define EFFECT_SET    0
   #define EFFECT_CLEAR  1

   typedef struct { ... } SquareChange;

   static struct { ... } sensor;

   /* Setup */
   void sensor_reset(void) { ... }

   /* Floor sensors */
   static bool is_party_on(Location at) { ... }
   void sensor_process_square(Location at) { ... }
   ```

## 9. Comments

1. Each non-trivial function has a one-line comment that tells what it does in game terms: a public function
   in the header above its prototype, a static function above its definition, never both. [comment]
2. A comment does not repeat the code, and does not name an original symbol, address or event number. It
   names the constant. The original name and address belong in `docs/skull-symbols.csv`. [event-comment; the
   rest review only]
3. A kept quirk of the original and a fixed bug of the original (a stale register, an overflow) get a short
   comment at the place, and an entry in `docs/skull-formats.md`. [review only]
4. No TODO marker in code (`ponytail:`, `TODO`, `FIXME`); open work goes to `docs/todo.md`. [todo]
5. Keep existing comments. New comments only where needed, one line, about what the code does, never about
   its history. [review only]

## 10. Data and errors

1. `data/`, `engine/`, `ui/` and `control/` never write to stderr and never exit. They log through `os_log`
   and fail through `os_fatal` of `data/os.h`; `data/os.c` holds the default (stderr and `exit`), `platform/`
   and the tests set the hooks `g_os_log_hook` and `g_os_fatal_hook`; `os_fatal` never returns. [platform]
2. A loader returns `false` on a bad file. A system error of the original (`SYSTEM ERROR`) and a port bug call
   `os_fatal`. [review only]
3. An allocation failure calls `os_fatal("out of memory")`, unless the caller reports an error result.
   [review only]
4. A struct field that points to owned memory says so in its comment; a module that opens or allocates has a
   close or free; `malloc` and `free` of one buffer stay in one module. [review only]
5. Do not reproduce undefined register values, overflows or out-of-bounds reads of the original. Use a
   defined value, mark it with a short comment, record it as a deviation in `docs/skull-formats.md`, mask it
   in the tests. Keep a quirk of the original only when it changes what the player sees. [review only]
6. No rule in dm2: the port plays one game, so no code detects the adventure (`g_adventure`).

## 11. Tests

`CLAUDE.md` (Commands, Layout) describes the test layout: `tests/differential/` (the port against the
original in the emulator), `tests/port/` (the port alone), `tests/tools/` (the Python tools),
`tests/standard/` (this standard), and the helpers in `tests/dm2test/`. The rules that the standard check
enforces: [tests]

1. A test module imports helpers from `tests/dm2test/` only, never from another test module or from
   `conftest`.
2. A C constant comes from the headers through `dm2test.constants`; no hand-mirrored value.
3. A test calls the accessors of the port; it does not decode a record by bit shifts. [review only]
4. No machine path in a test (`/Users/`, `/private/tmp/`).
5. Each test module loads its own copy of the library (`dm2test.lib`); no test compiles the library.

## Format

`.clang-format` in the repository root is the format; `make format` runs `clang-format -i` over `src/`
before each commit. The check runs `clang-format --dry-run --Werror` with the `clang-format` of the `dev`
dependency group (version 23). [format]

## Checks before a commit

`make` (clang) and `make windows` (MinGW-w64 GCC) build with 0 warnings under `-Wall -Wextra -Werror
-Wshadow -Wmissing-prototypes -Wstrict-prototypes -Wvla -Wimplicit-fallthrough`.

`make check` runs the checks (`CLAUDE.md`, Commands). The checks of `tests/standard/test_standard.py`, by
their allowlist key:

| Key | Fails on |
|---|---|
| `layer` | an include that breaks the layer order, names a path or names no project header; SDL outside `platform/`; a file outside a layer folder, a public header outside `include/`, a private header in `include/`; a header name in two layers |
| `platform` | an operating system header or `_WIN32` outside `platform/`; `fprintf(stderr` or `exit(` outside `platform/` and `data/os.c` |
| `engine-write` | a write of `ui/` to an extern `g_*` of `engine/include/*.h`, except `g_hooks` and the `*_hook` globals |
| `module` | a `.c` file without its header, a second public header of one module |
| `prefix` | a header without a prefix line, a prefix that another header owns or extends, a name outside its prefixes |
| `header` | a header head out of order, a guard that is not `DM2_<LAYER>_<FILE>_H` |
| `include` | a header the file does not use |
| `comment` | a public prototype or a non-trivial static definition without a comment line above it, or a comment on both |
| `dead-api` | an exported name with no use outside its file and no test marker |
| `hex`, `decimal` | a number outside a constant definition, also a single digit in `[N]` and `< N;` |
| `enum` | an `enum` that is not a result type |
| `block` | a constant block without a comment line above it |
| `notation` | a block that mixes hex and decimal, a flag block in decimal, a count block in hex |
| `duplicate` | a constant name defined in more than one header |
| `numbered` | an event number, a byte offset or an address in a name |
| `packed` | a shift by 8, 16, 24 or a foreign `*_SHIFT` at a call site, a double integer cast |
| `byteorder` | a byte order helper (`le16`, `le32`, `put_le16`, `put_le32`, `put_be32`, `read_u32`) or a shift by 8, 16, 24 of bytes outside `data/le.h` |
| `record` | a `dungeon_thing_record(` call outside `engine/dungeon.c` and `engine/include/dungeon.h` |
| `name` | a function name above four words after the prefix or above 32 characters |
| `static-prefix` | a static function with a module prefix |
| `bool` | a predicate, a truth local or a "Returns 0/1" function that is not `bool` |
| `global` | an `extern` without `g_`, a file-static with `g_` |
| `decompiler` | a decompiler name |
| `ordinal` | `_ordinal` or `_plus_1` in a name |
| `type` | a parameter name with two types across the headers of a layer |
| `long` | a function above 60 lines without an allowlist reason |
| `result` | a function whose comment says "Returns 0" or "-1" for success |
| `params` | a prototype with more than five parameters |
| `order` | a `.c` file whose public definitions are not in header order, or a forward declaration without a recursion comment |
| `event-comment` | an event number in a comment (`event 11`) |
| `todo` | a TODO marker in code |
| `tests` | a `from test_` or `conftest` import, a machine path, a clang call in `tests/` |
| `format` | a file that `clang-format --dry-run --Werror` reports |
| `allowlist` | an allowlist line with a reason that this document does not name |
