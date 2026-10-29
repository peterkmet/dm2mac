# Save file format

The save files `SKSAVE<n>.DAT` of Dungeon Master II and the port code that reads and writes them, and the quick save files and the settings file of the port. The original routines are `session_save` (`0x3502b`) and `session_load` (`0x370d2`). The port is `src/engine/save.c`. The addresses and the notation follow the conventions of [skull-map.md](../reference/skull-map.md).

## Container

| Part | Size | Content |
|---|---|---|
| 1 | 42 bytes | Save header |
| 2 | 43443 bytes | Dungeon image in the `DUNGEON.DAT` section format |
| 3 | rest of file | Bit-packed game state |

- Header: `+0x00` u16 = 1 (format marker), `+0x02` 32 bytes save name (up to 31 characters and a NUL, `"GAME 0"`). The meaning of `+0x22` to `+0x29` is unknown.
- `session_save` (`0x3502b`) first reads the old header, then changes only `+0x00` and the name, and writes it back. All other header bytes come from the previous save. The port keeps them byte for byte.
- `session_load` reads the 42 bytes, sets the dungeon file base `0x7f8e8` to `0x2a`, and calls `dungeon_load(0)`.
- The dungeon image stores counts that already include the load slack: tile roots + 300, and the per-thing-type slack table at `0x71818` (`0 0 0 0 75 100 60 0 12 5 200 0 0 0 60 50`). Thus the save path calls `dungeon_load` with `new_game` false, and the `DUNGEON.DAT` load calls it with `new_game` true.
- The dungeon image has no trailing 16-bit checksum. `DUNGEON.DAT` has one.

## Bit-packed records

`save_read_packed(dest, masks, size, count)` (`0x34536`) reads `count` records of `size` bytes from part 3. Each record byte has one mask byte. Only the bits set in the mask are stored, most significant bit first. The bit position continues from one call to the next. `save_write_packed` (`0x343be`) is the writer. `g_save_bit_total` (`0x7f8dc`) counts the stored bits.

| Order | Destination | Masks | Size × count |
|---|---|---|---|
| 1 | Local party and game record (to globals) | `0x75316` | 60 × 1 |
| 2 | `g_global_flags` (`0x7f100`) | `0x75312` | 1 × 8 |
| 3 | `g_global_bytes` (`0x7f0c0`) | `0x75312` | 1 × 64 |
| 4 | `g_global_words` (`0x7f108`) | `0x75312` | 2 × 64 |
| 5 | `g_champions` (`0x7fbd0`) | `0x75352` | 263 × `g_champion_count` |
| 6 | `g_magic_light` (`0x7ffec`) | `0x75459` | 6 × 1 |
| 7 | `g_timeline_events` (`0x760e8`) | `0x7545f` | 12 × `g_timeline_event_count` |
| 8 | Things, through `save_read_dungeon_things` (`0x35b97`) | `g_save_thing_masks` (`0x754d7`) | per thing type |

- Record 1: fields go to `g_game_time` (`0x7f22c`), `g_random_seed` (`0x70518`), `g_party_x`, `g_party_y`, `g_party_dir`, `g_party_map`, `g_champion_count`, `g_timeline_event_count`, and other globals.
- Record 5 does not store the hand and inventory slots (`+0xaf` to `+0xf0`). Record 8 rebuilds them.
- Record 7 stores timeline bytes `+0` to `+9`. Bytes `+0x0a` and `+0x0b` are not stored.
- Record 8 walks the squares and their thing lists. For each thing it reads the thing record with the mask of its type. The masks never store bytes 0 to 1 (the link word). Thing types 1 and 11 to 13 have no mask and are not stored.
- Record 8 first unlinks and frees all things of types 4 to 15. Then it creates them again from the bit stream. Types 0 to 3 keep their slots.

## Details of the records

- Order of the things: the things of the timeline events, then all squares of all maps, then the deferred containers and creatures.
- Values between the records (the more bit, 4 bits type, 2 bits cell, 7 bits creature type, 2 bits container kind, 9 bits sensor data, 10 bits record position) go through `save_write_packed` as a little-endian word with a little-endian mask. A 9-bit value thus stores bits 7 to 0 first, then bit 8.

Game record (60 bytes, mask `0x75316`):

| Offset | Content |
|---|---|
| `+0x00` | Game time (24 bits) |
| `+0x04` | Random seed (only bits 0 to 15 are stored) |
| `+0x08` | Champion count |
| `+0x0a`, `+0x0c`, `+0x0e`, `+0x10` | Party x, y, direction, map |
| `+0x12` | Leader |
| `+0x14` | Timeline event count |
| `+0x16`, `+0x1a` | Game times (`0x716a0`, `0x7f19c`) |
| `+0x1e` | Party move cooldown |
| `+0x20`, `+0x22` | Two values (`0x7f25a`, `0x7f270`) |
| `+0x24` | One bit that the writer always sets to 0 |
| `+0x28` | Sound volume (bits 0 to 3) and music volume (bits 4 to 7) |
| `+0x2a` | Weather visible |
| `+0x2e` to `+0x37` | Weather and storm bytes |
| `+0x38` | Next hour time |

- Thing list (`save_write_thing_list`, `save_read_thing_list`): things of types 4 to 15 start with a 1 bit and the type; a 0 bit ends the list. A list with a single thing (champion slot, leader hand, projectile payload, event group) has no end bit when it holds a thing, and also none when it is `THING_ENDOFLIST`.
- Creature groups whose creature info has flag bit 0 (static creatures, no active group) use the mask at `0x7548b` and store the cell of each possession. Minion maps (container kind 1) use the mask at `0x754b3`. A misc item inside a moneybox uses the mask at `0x754bf`. A projectile inside a creature group uses the mask at `0x754cf`. Minion maps and projectiles in groups are deferred: their link is stored after the squares as the position of the target group or container in the bit stream.
- A teleporter square whose target is a teleporter with a link sensor (type `0x27`) stores no square bits. When the target map is lower, the things of the square are stored with the target square, not here.
- Event types with a thing: `0x19` explosion (event `+8`), `0x1d`/`0x1e` projectile move (event `+6`), `0x3c`/`0x3d` move group (event `+8`, a single-thing list that only loads when the header format word is not 0).
- Header: `session_save` reads the old header from the `.BAK` file (the old `.DAT`, renamed just before). Without a `.BAK` file only `+0x26` to `+0x29` are set to 0; the other bytes are uninitialized stack. The name is copied with `str_copy`, so the bytes after the end mark keep the old header bytes.
- The original treats a short read at the end of the file as success, so it does not find a truncated bit stream.
- A damaged dungeon image in which two squares share a thing list (other than a linked teleporter) can load without an error. The written file then does not load again because the things of the shared list are stored under both squares. The original has the same weakness. The port does not check this.

## Reader and writer of the port

- The port reads and writes the records from the mask tables, with no separate format description in C.
- After a load, things of types 4 to 15 can sit in different slots from the file. A load followed by a save can thus give a different dungeon image. The tests compare the C writer with the original writer run in `dm2tools.emu` on the same memory state, and a C load with the original load by content.
- A save deactivates the creature groups before it writes and activates them again after the write, as in the original (`session_save`, `0x3502b`). The activation takes random numbers, so each save moves the random seed, and two saves in a row are not byte-equal.

## Compatibility with the original

Method (`tests/differential/test_save.py`, `original_round_trip`): `dm2tools.emu` runs the file part of `session_load` (`0x372d6` to `0x37586`: header, `dungeon_load(0)`, the packed records, `timeline_build_queue`, `save_read_dungeon_things`, `restore_timed_effects`, `relink_events`). Then it runs the file part of `session_save` (`0x35236` to `0x35674`: header, dungeon image, `creature_deactivate_all_groups`, `timeline_compact`, the packed records, the things, the last bits). The header buffer of `session_save` gets the header of the input file, as in a save into the slot that the game was loaded from. Stubs: `mem_alloc`, `weather_init`, `dungeon_read_level_table`, the file I/O wrappers, and `gdat_entry_index` (answered by the C `gdat_find`).

Result for `tests/saves/start.DAT` (50452 bytes, 1 champion, 3 timeline events):

| Part | Bytes that differ |
|---|---|
| Header, dungeon header, map descriptors, column table, text, map data | 0 |
| First-thing table | 357 |
| Thing records of types 1, 2, 3 (link words) | 5, 93, 56 |
| Thing records of types 4 to 10 | 1816, 490, 481, 15, 244, 115, 620 |
| Bit-packed state | 0 |

- The original round trip is not byte-equal: 4292 of 50452 bytes differ, all in the dungeon image. The file size and the bit stream stay equal. The load creates the groups and items again in the first free records, so the first-thing table and the link words change.
- A second round trip of the original output is byte-equal (a fixed point).
- The C port gives the same bytes as the original writer for `start.DAT`, and for the original output.
- A second test (`link_things` in `tests/differential/test_save.py`) changes the loaded state the same way in C and in the emulator memory: a group in a move-group event, a minion map linked to another group, a projectile, an explosion, and a gap in the timeline. The C writer gives the same bytes as the original writer. The group in the event gets a different bit stream position from its record index, so the deferred links use the written positions.

## AMAP and CHTS blocks

In the advanced mode, a slot save and the quick save `QUICK.DAT` end with two blocks after the original save data: the AMAP block, then the CHTS block. The turn-based mode writes only the AMAP block. The bytes before the blocks are the same as in the vanilla mode. The AMAP block holds the seen squares of the automap, the CHTS block the cheats ([manual.md](../manual.md#advanced-features)).

The AMAP block:

| Bytes | Content |
| --- | --- |
| 4 | `AMAP` |
| 1 | Version: 1 |
| 1 | Map count |
| 2 per map | Width and height of the map |
| 1 per square | 1: seen, 0: not seen. The maps one after another, each map column by column (x, then y). |

The CHTS block:

| Bytes | Content |
| --- | --- |
| 4 | `CHTS` |
| 1 | Version: 1 |
| 1 | Cheat bits: `0x01` full light, `0x02` immortal, `0x04` food and water, `0x08` one-hit kill, `0x10` walk through walls, `0x20` reveal map |

A load reads the blocks when they are there.

- AMAP: a missing block, a block with different map sizes, or a value other than 0 or 1 gives an empty automap. After the block, only the end of the file or a valid CHTS block can come; other bytes give an empty automap.
- CHTS: in the advanced mode, the cheats come from the block. A missing block, a wrong version, an unknown bit, bytes after the block, or a damaged AMAP block give no cheats. The vanilla and turn-based modes ignore the block and use no cheats.
- The game loads in all these cases. A save with only the original data (vanilla) loads with an empty automap and no cheats.

The original game loads a save with the blocks because its loader stops at the last bit of the game state. A save in the original game writes the file again without the blocks.

## Quick save files

The quick save `QUICK.DAT` of the advanced and the turn-based modes is in the `--saves` folder, otherwise in `<data>/DATA` ([manual.md](../manual.md#quick-save-and-quick-load)). A quick load uses a copy of the running game, `QUICK.TMP` next to `QUICK.DAT` (`advanced_quick_load` in `src/ui/advanced.c`):

- Before the load, the game writes the running game to `QUICK.TMP`. When this write fails, the game does not load.
- When `QUICK.DAT` does not load, the game loads `QUICK.TMP` back. After a load or a restore, the game deletes `QUICK.TMP`.
- When `QUICK.TMP` does not load either, the game is lost. `QUICK.TMP` stays, and the log gets an error line. The advanced mode ends the program (`EXIT_GAME_LOST`, [game-flow.md](../rules/game-flow.md#deviations-from-the-original)); the turn-based mode goes on without a game ([llm-mode.md](../llm/llm-mode.md#turn-based-commands)).

## SETTINGS.TXT

The settings file of the advanced mode keeps the upscale filter of the options menu ([manual.md](../manual.md#settingstxt)). It is in the folder of `QUICK.DAT`. The file is a text file of `key=value` lines (`src/data/settings.c`). It has one line:

```
filter=scale2x
```

- The values are `none`, `scale2x`, and `scale3x`.
- The game ignores a CR at the end of a line, an unknown key, a bad value, and a line of 64 characters or more.
- A file over 1024 bytes gives the defaults (no filter).
- The game writes `SETTINGS.TMP` first and replaces the file with it.

## Deviations from the original

- Without an old file, the header bytes after the name are 0 (the original writes uninitialized bytes).
- On a failed save (no file, a write error) the port renames the `.BAK` back to the slot file; the original leaves only the `.BAK` (`session_save`, `0x35696`), which the next load of the slot renames back.
- Damaged files give an error code and an empty dungeon. The causes are:
  - a truncated file
  - a dungeon image with a bad first-thing table or broken thing lists
  - more than 4 champions
  - more timeline events than the queue holds
  - an event or record index out of range
  - no free record for a new thing
  - a full first-thing table
  - more than 100 deferred things
- A poison event of a champion that does not exist is skipped on load (the original writes to the record past the last champion).
- A damage display event of a champion past the four champion records is not linked to a champion (the original writes past the last record).
- A creature group whose creature info index lies past the table stores the bits of a group that is not static (the original reads past the table).
- A deferred minion map or carried projectile whose link lies past the records of its target type writes the record position 0 (the original reads past its table).
- Kept quirk: the game record stores two bits of the leader (`+0x12`, mask `0x03`), as in the original (`session_load`, `0x370d2`). A party without a leader (`0xffff`) loads with leader 3; `champ_init_party_state` then gives the lead to champion 3 if it lives, otherwise the party stays without a leader.
- On save, the compaction of the timeline stops at the last event when the event count is higher than the number of events in use, for example after the load of an event without a type (the original reads past the events).
