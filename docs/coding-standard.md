# C port coding standard

The C port is a readable reimplementation of the game. It is not a cleaned-up decompiler listing. The tests prove that the behavior matches the original. This standard makes the code readable.

## Rules

1. Name every game value. Use an `enum` or a `#define` with a name that tells the game meaning.
   - Thing types: `THING_DOOR`, `THING_SENSOR`, `THING_GROUP`, and so on, not `3`.
   - No thing: `THING_NONE`, not `0xffff`. End of list: `THING_ENDOFLIST`.
   - Directions, cells, champion slots (`SLOT_HAND_READY`, `SLOT_COUNT`), statistics, skills, square types, sensor types, door states, timeline event types, commands.
   - Message and text numbers: a named constant for each string id, for example `TEXT_RESURRECTED`.
   - Graphic numbers: named constants in `port/src/data/gfx.h`, for example `GFX_WALLSET_FIRST`. Layout zone numbers: named constants in `port/src/ui/ui.h`, for example `ZONE_VIEWPORT`. A zone that a rule passes to a hook is a named constant in the engine header of that rule.
   - DM2 systems: money types (`COIN_GOLD`), minion types (`MINION_GUARD`), weather states (`WEATHER_RAIN`), and Skullkeep machine states each get named constants. `docs/skull-symbols.csv` holds the names.
   - Plain numbers are allowed only for 0, 1, -1, and 2 when it halves or doubles a value. Every other number is a named constant, also decimal game values such as divisors, limits, and thresholds.
   - Notation: hex for bit masks and flags, for ids from the data files and from `SKULL.EXE` (GDAT categories and entries, sound entries, priorities and volumes, event types, command codes, key codes), and for byte values of file formats. Decimal for counts, sizes, times in ticks, percentages, distances, divisors, and thresholds.
   - Each constant has one definition, in the header of the module that owns it. Other modules include that header. They do not define the value again under the same or another name.
2. Do not index raw record bytes outside one accessor module. Put field access in small functions or macros with names, for example `sensor_type(t)`, `sensor_disable(t)`, `door_state(t)`, `group_count(t)`. `engine/dungeon.h` owns the record layout: the field offsets and bits. The named accessors of a thing type can be in the header of its module (`sensor.h`, `projectile.h`, `item.h`, `ai.h`). They use the field constants of `dungeon.h`.
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
   - Functions: `<prefix><verb>_<object>` in `snake_case`. Each header states its prefixes in one comment line, for example `/* Prefixes: gfx_ */` in `gfx.h`. A prefix belongs to one header only, and no prefix starts with the prefix of another header. The header that owns a prefix declares every function with that prefix, also when another `.c` file of the same layer defines it; the `.c` file includes that header. The `Prefixes` line also covers function-like macros.
   - Global variables: `g_` prefix. Module-private globals and functions are `static`.
   - Types: `PascalCase`. Constants and enum values: `UPPER_CASE`.
   - No event numbers or original addresses in names. Name the game task, not the number.
8. The source has four layers in subfolders of `port/src/`: `data/`, `engine/`, `ui/`, and `platform/`. A file includes only headers of its own layer and of the layers before it in this list. Game rules in `engine/` do not call the user interface: they set redraw flags. Exception: a hook, a member of `g_hooks` (`engine/hooks.h`) that `ui/hook_table.c` sets, for an event that must run the user interface inside the rule. Only `platform/` includes SDL.
9. Little-endian read and write helpers (`le16`, `le32`, `put_le16`, `put_le32`) have one definition in one header. Modules do not define their own copies.
10. A packed field (two or more values in one word) has accessor functions or macros in the header of the module that owns it, for example `timeline_time_map()` and `timeline_event_map()` in `engine/timeline.h`. Call sites do not shift, mask, or cast to pack or unpack it. A shift by a named `*_SHIFT` constant is allowed only in the module that owns the field. Other modules call its accessors.

## Check before a step is done

`tests/test_standard.py` checks `port/src/` and fails on:

- hex literals outside constant definitions,
- decimal literals of 10 or more outside constant definitions,
- constant names defined in more than one file,
- any `dungeon_thing_record(` call outside `dungeon.c` and `dungeon.h` (raw record access),
- decompiler names,
- predicates that do not return `bool`,
- functions longer than 60 lines,
- headers without a `Prefixes` line, prefix collisions, and functions outside the prefixes of their header,
- event numbers in function names,
- shifts by 8, 16, or 24, shifts by a `*_SHIFT` constant outside the module that owns it (the owner files are exempt), and double integer casts in `.c` files (packed fields at the call site),
- includes of a higher layer, and SDL outside `platform/`.

`tests/standard_allowlist.txt` lists the accepted exceptions, one per line with a reason. The reason `open: module cleanup` marks a violation that a later cleanup removes. `opl3.c` and `opl3.h` are vendored code (`opl3-LICENSE`), and the test does not check them.

A reader who knows Dungeon Master II can understand each function without opening the decompiled code.
