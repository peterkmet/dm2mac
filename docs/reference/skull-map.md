# SKULL.EXE map

The reference map of `SKULL.EXE` for the C port: the program structure, the subsystems, the key data structures, and the open questions.

Related files:

- `docs/reference/skull-symbols.csv`: name, C signature, and subsystem of every function and of the main globals.
- `docs/formats/`: file formats, sound, music, the SVGA option, and the PIT rate.
- `docs/rules/`: the game rules of the port.
- `docs/reference/symbols/`: per-range analysis notes (layout, data structures, open questions).
- `docs/reference/reference-crosscheck.md`: comparison of these names and formats with the reDM2 project.
- `reference/decomp/SKULL.c`: Ghidra output with the names from `docs/reference/skull-symbols.csv` (`tools/decompile-skull.sh`).

Conventions:

- Addresses are linear addresses in the flat image from `uv run dm2-le game/SKULL.EXE skull.bin` (code base `0x10000`, data base `0x70000`): code object `0x10000`-`0x695ac`, data object `0x70000`-`0x828f0`. An address below `0x70000` is a function. Other addresses are globals. `FUN_xxxxxxxx` is the function at that address; `docs/reference/skull-symbols.csv` gives its name.
- A file offset is written as `file+0x...`.
- Signatures use the Watcom register order: EAX, EDX, EBX, ECX, then the stack.
- Thing: 16-bit reference. Bits 0-9 index, bits 10-13 type, bits 14-15 cell. `0xffff` = none, `0xfffe` = end of list.
- GDAT key: `(category, index, type, entry)` in `GRAPHICS.DAT`, the tuple that the lookup function `FUN_0003c4f2` uses.
- HMI SOS function names (for example `sosDIGIStartSample`) are probable names. They come from the call shape and from public HMI SOS knowledge. The binary has no symbols.

## Symbol counts

| Item | Count |
|---|---|
| Functions (every function start in `reference/decomp/SKULL.c`) | 1362 |
| Globals | 626 |
| Low-confidence function names | 221 (see [Low-confidence names](#low-confidence-names)) |

## Address layout

| Range | Functions | Content |
|---|---|---|
| `0x10000`-`0x18e7f` | 173 | Sound and music glue, system init, timer ISR, file I/O, disk, keyboard, blitters, image decoders, screen, palette, mouse, sound requests, money helpers, projectiles, explosions |
| `0x18e7f`-`0x1fef6` | 166 | Rectangles, layout zones, dialogs, draw context, string, math, random, dungeon data access, item and creature attributes |
| `0x1fef6`-`0x2b96a` | 181 | End of game, input and commands, party movement, `game_loop`, `main`, creature AI part 1 |
| `0x2b96a`-`0x3ba23` | 145 | Creature AI part 2, path search, creature groups, save and load, dungeon loader, start menu, light, text format |
| `0x3ba23`-`0x49ef8` | 208 | Memory manager, GDAT database, image cache, champion actions, spells, interface panels, champions, recruiting |
| `0x49ef8`-`0x5a87c` | 164 | Thing movement, sensors (Skullkeep machines), dungeon and outdoor view, game init, message area, timeline, weather, day and night |
| `0x5a87c`-`0x62272` | 114 | IOP glue, GDAT decompressors, HMI SOS (timer, digital, detection, MIDI), clib `malloc` and `free` |
| `0x62272`-`0x695ac` | 211 | Watcom clib, HMI PIT and IRQ code, DPMI helpers, HMI driver loaders, static HMI MIDI drivers |

The port replaces all code from `0x5a87c` on, except the GDAT decompressors (`gdat_lzw_decode`, `gdat_lzss12_decode`, `gdat_lzss13_decode`).

## Entry point and startup chain

1. `cstart_entry` (`0x62284`, LE entry point) -> `clib_cstart` (`0x622fc`) -> `clib_cmain` (`0x688ed`).
2. `main(argc, argv)` (`0x249db`): parses `+GLx` (language byte `0x7576c`), calls `clib_setjmp(g_main_jmpbuf)`, `timer_init`, and `session_init`.
3. `session_init` (`0x55310`):
   1. `system_init` (`0x10bd3`): `sound_init` (HMI timer at 240 Hz, digital and MIDI drivers, `SONGLIST.DAT`), memory pools.
   2. `disk_detect_drives`, `data_path_init`, `gdat_load_index` (`GRAPHICS.DAT` and `GRAPHIC2.DAT`).
   3. Sound buffers, palette from GDAT `(1,0,9,0xfe)`, viewport bitmap, `zone_init`, message area, `input_init`.
   4. Start menu loop: `run_menu` (`0x386f5`), then `session_load` until it returns 1. `session_load` calls `dungeon_load` for a new game.
   5. Volumes, creature counts, command queue reset.
4. `main` then loops forever: `game_loop`, then `session_end`.
5. `session_quit` (`0x2005b`) calls `system_shutdown` and `clib_longjmp(g_main_jmpbuf, 1)`.

## Main loop

`game_loop` (`0x24691`). `g_tick_threshold` (`0x7f268`) is set to 8 game ticks per step. One iteration is one game step:

1. `timeline_process` (`0x59a5c`): runs all due events.
2. `world_update_day_night(0)` (`0x5a073`): hour light and weather.
3. `view_draw_dungeon(dir, x, y)` (`0x54b3f`) and `play_present_viewport`, unless the party sleeps or the inventory is open.
4. `champ_apply_pending_damage` (`0x47113`); every 64 steps `champ_apply_time_effects`.
5. `sound_flush_requests`; `g_game_time` + 1; every 512 steps `champ_burn_held_lights`; `text_clear_expired_rows`.
6. `command_process_queue` (`0x224a9`) until the tick flag `g_tick_elapsed` (`0x717f2`) is set.
7. Map change through `g_new_map_index` (`0x717f4`) and `game_enter_map`; `music_update_for_map`.
8. On the game end flag: `input_reset_states`, then return.

Game tick: `timer_isr` (`0x10cc2`, `INT 0x2d` from `IBMIOP.EXE`) adds `g_timer_step` (`0x7efac`) to `g_timer_ticks` (`0x7f228`) and `g_tick_accumulator` (`0x7f256`). At `g_tick_threshold` it sets `g_tick_elapsed`. Rates: [runtime.md](../formats/runtime.md#pit-divisor-and-tick-rates).

## IBMIOP INT 0xFC service table

`SKULL.EXE` calls `INT 0xFC` with the function number in EAX and an argument in EBX. The real-mode program `IBMIOP.EXE` serves the call. The shared buffer is `g_iop_buffer` (`0x8048c`), set by `iop_init_shared_buffer` from `g_iop_info` (`0x7618c`) + `0x3ec`. The port platform layer replaces every function in this table.

The `IBMIOP.EXE` dispatcher accepts EAX `0x00`-`0x30`: 28 numbers do work, 21 go to one exit that does nothing. Bit `0x100` selects a second table (functions `0x103`-`0x10a`). The call keeps all registers; every result comes back through the shared buffer (`DGROUP:0x2342` in `IBMIOP.EXE`). Source for the driver side: reDM2 `docs/memory.md` (driver dispatcher, functions `0x0e`, `0x12`, `0x14`, `0x2e`, `0x2f`, `0x30`) and its comments on `0x10010`, `0x13898`, `0x14cb1`; reDM2 read the unpacked `IBMIOP.EXE` code ([reference-crosscheck.md](reference-crosscheck.md)).

| EAX | Function | Caller |
|---|---|---|
| `0x00` | Print a `0x100`-byte message | `iop_print_message` |
| `0x01` | Read key; extended keys `0x1048`, `0x104b`, `0x104d`, `0x1050` map to arrows | `kbd_read_key` |
| `0x02` | Key available | `kbd_key_available` (tail `iop_call_return_word`) |
| `0x03` | Install the input ISR on `INT 0x2e` | `iop_install_input_isr` |
| `0x04`, `0x05` | Lock and unlock input (around mouse area list changes, low confidence) | `iop_input_lock`, `iop_input_unlock` |
| `0x06`, `0x07` | Begin and end mouse capture (right button held, low confidence) | `mouse_capture_begin`, `mouse_capture_end` |
| `0x08`, `0x09` | Show and hide the mouse pointer (counter `g_mouse_hide_count`) | `mouse_show`, `mouse_hide` |
| `0x0a` | Set the cursor image, `0x144` bytes | `mouse_set_cursor_image` |
| `0x0b` | Select a cursor | `input_init`, `champ_drag_click` |
| `0x0c` | Set the hover rectangle | `mouse_set_hover_area`, `mouse_set_default_cursor` |
| `0x0d` | Set the mouse position | hidden function at `0x14c93` (inside `iop_input_unlock`) |
| `0x0e` | Get mouse x, y, and a third word. Buffer word 0 on entry: `0` = live position, other = position at the last event. Third word: buttons (the port reading: the game tests bit 1 for the right button) or cursor height (reDM2, from the driver code); open | `mouse_get_state` |
| `0x0f` | Install the game timer callback on `INT 0x2d`; returns the tick step | `iop_install_timer_isr` |
| `0x12`, `0x13`, `0x14` | Drive status, volume label (`0x10` bytes). `0x12` does nothing in the driver: the caller at `0x113ad` (`iop_call_return_word`) then reads buffer word 0, which the driver keeps up to date; the game uses that word as the drive status, so the name `disk_get_drive_status` stays. `0x14` first writes a zero byte, so the result is an empty string if the drive has no label | `disk_get_drive_status`, `disk_get_volume_label` |
| `0x15` | Drive information | `disk_query_drive_info` |
| `0x1b` | Key code to ASCII | `kbd_key_to_ascii` |
| `0x28` | 3 bytes of drive path data | `disk_get_drive_path` |
| `0x29` | Data path string | `data_path_init` |
| `0x2a` | Initialize the shared buffer; reinstall the IOP timer | `iop_init_shared_buffer`, `timer_init` |
| `0x2b` | Allocate a DOS memory buffer | `iop_alloc_dos_buffer` |
| `0x2e` | Sound hardware configuration: returns the linear address of a 6-word record from `SKULL.CFG` (`+0` digital device ID, `+2` IRQ, `+4` DMA, `+6` port, `+8` MIDI device ID, `+0x0a` MIDI port), stored at `0x7ef2c` | `sound_init` |
| `0x2f` | Free DOS memory block: returns `{linear address, size}`; the game adds it as one more memory pool | `system_init` |
| `0x30` | Shutdown; also stops the game timer callback | `system_shutdown` |
| `0x103` | Present an 8-byte rectangle of a game bitmap | `screen_present_rect` (`0x13a87`) |
| `0x105` | Scroll the message area by one line height | `shift_message_area` (`0x139e4`) |
| `0x106` | Video state on or off | `iop_set_video_state` |
| `0x109` | Present the viewport (flags 8 or `0x8008`) | `play_present_viewport` |
| `0x10a` | Upload 1024 bytes from GDAT `(1,0,9,254)`; the first result word goes to `0x7f07c`. The port reading: set the palette (256 × 4 bytes). reDM2: cursor image upload, marked as an inference. Open | `pal_set_palette` |

Other hardware access: `gfx_fill_rect` writes to `0xa0000` (pitch 320), and `vga_wait_vblank` polls port `0x3da`.

## Call graph between subsystems

Each cell counts distinct caller-callee function pairs across two subsystems, computed from the calls in `reference/decomp/SKULL.c`. Calls inside one subsystem are not counted.

| Caller | Callees (pair count) |
|---|---|
| main loop | input 16, graphics 10, runtime library 7, memory 7, interface 7, creatures 7, file I/O 6, text 5, sound 5, dungeon data 5, champions 5, save and load 2, palette 2, inventory 2, end of game 2, dungeon view 2, weather 1, timeline 1, start menu 1, music 1, items 1, Skullkeep machines 1 |
| start menu | input 9, graphics 9, memory 2, interface 1, file I/O 1 |
| end of game | input 7, graphics 5, sound 2, save and load 2, main loop 2, interface 2, runtime library 1, file I/O 1 |
| save and load | interface 23, dungeon data 18, file I/O 16, runtime library 12, input 9, text 7, memory 7, graphics 7, timeline 4, creatures 4, palette 2, money and shops 2, minions 2, end of game 2, items 1, inventory 1, champions 1, Skullkeep machines 1 |
| input | interface 28, champions 17, runtime library 10, graphics 8, text 7, memory 5, magic 3, inventory 3, dungeon view 2, dungeon data 2, start menu 1, save and load 1, palette 1, music 1, money and shops 1, minions 1, end of game 1, creatures 1 |
| timeline | Skullkeep machines 29, dungeon data 26, creatures 11, champions 6, runtime library 5, sound 4, items 3, text 2, projectiles 2, main loop 2, graphics 2, weather 1, memory 1, magic 1, input 1, end of game 1, day and night 1 |
| Skullkeep machines | dungeon data 61, items 13, timeline 10, creatures 10, runtime library 8, sound 6, graphics 5, champions 5, text 3, memory 2, inventory 2, projectiles 1, money and shops 1, input 1 |
| creatures | dungeon data 197, runtime library 120, graphics 29, champions 25, sound 10, projectiles 10, memory 9, money and shops 8, items 8, timeline 6, Skullkeep machines 4, dungeon view 2, day and night 2, main loop 1, inventory 1 |
| minions | dungeon data 13, creatures 3, interface 2 |
| projectiles | dungeon data 56, creatures 23, runtime library 22, champions 6, timeline 5, sound 4, items 4, Skullkeep machines 3 |
| magic | runtime library 9, dungeon data 9, timeline 5, champions 5, interface 4, inventory 3, projectiles 2, input 2, graphics 2, sound 1, items 1, day and night 1, creatures 1 |
| champions | dungeon data 57, runtime library 56, input 32, creatures 21, inventory 19, items 17, interface 15, text 12, graphics 12, timeline 6, sound 5, minions 5, magic 5, projectiles 4, memory 4, money and shops 1, main loop 1, dungeon view 1, Skullkeep machines 1 |
| inventory | items 20, graphics 19, dungeon data 18, interface 17, input 15, text 11, runtime library 8, champions 4, palette 3, money and shops 2, Skullkeep machines 2, timeline 1, sound 1, memory 1, dungeon view 1, day and night 1 |
| items | dungeon data 29, runtime library 6, text 3, inventory 3, graphics 3, money and shops 2, memory 2, main loop 1, day and night 1, champions 1, Skullkeep machines 1 |
| money and shops | dungeon data 29, items 19, runtime library 15, graphics 14, creatures 10, interface 6, text 4, memory 4, dungeon view 3, inventory 2, palette 1, main loop 1, champions 1 |
| dungeon data | creatures 20, runtime library 18, graphics 18, champions 8, items 6, sound 4, projectiles 4, text 3, main loop 3, timeline 2, memory 2, file I/O 2, Skullkeep machines 2, weather 1, save and load 1, interface 1, dungeon view 1 |
| dungeon view | graphics 80, dungeon data 51, creatures 25, runtime library 22, interface 11, palette 8, input 7, text 4, memory 4, inventory 4, weather 3, sound 3, money and shops 3, items 3, Skullkeep machines 3, timeline 1, outdoor view 1, champions 1 |
| outdoor view | text 3, dungeon view 3, runtime library 2, graphics 2 |
| weather | graphics 18, runtime library 10, dungeon data 7, interface 3, dungeon view 3, text 2, sound 2, creatures 2, timeline 1, projectiles 1, day and night 1, Skullkeep machines 1 |
| day and night | runtime library 5, dungeon data 4, items 3, graphics 2, creatures 1 |
| interface | graphics 104, text 26, dungeon data 21, runtime library 19, input 18, inventory 10, memory 9, champions 9, palette 7, items 7, creatures 5, minions 3, money and shops 2, sound 1, dungeon view 1 |
| text | graphics 15, runtime library 14, interface 10, input 6, memory 4, main loop 1, dungeon data 1 |
| graphics | runtime library 20, memory 20, interface 13, file I/O 8, sound 6, main loop 4, input 4, palette 3, dungeon data 2, creatures 2, weather 1, save and load 1, music 1, money and shops 1 |
| palette | graphics 11, runtime library 4, memory 1 |
| sound | memory 42, file I/O 31, runtime library 15, music 13, graphics 5, timeline 1, creatures 1 |
| music | sound 28, runtime library 10, memory 7, graphics 5, file I/O 5 |
| file I/O | runtime library 26, memory 3, main loop 2, text 1 |
| memory | runtime library 4, main loop 3, interface 3, graphics 1, end of game 1 |
| runtime library | file I/O 3, memory 2, main loop 1 |

Hub functions (most callers): `dun_get_thing_data` 161, `dun_get_next_thing` 86, `gdat_entry_index` 66, `mem_copy` 59, `creature_get_group_at` 47, `rand_below` 46, `dun_get_thing_gdat_index` 44, `math_max16` 42, `dun_get_square_first_thing` 42, `dun_set_current_map` 40, `mem_alloc` 39, `timeline_add_event` 37, `zone_get_rect` 35, `dun_get_square` 35, `move_thing` 31.

## Key data structures

### Dungeon (`DUNGEON.DAT`)

- `dungeon_load` (`0x36909`) reads 8 bytes (first word compared with `0x8104`), then the `0x2c`-byte header `g_dungeon_header` (`0x7f3e8`): `+4` map count (byte), `+8` party start (x bits 0-4, y bits 5-9, direction bits 10-11), `+0x0a` square thing count, `+0x0c` thing counts per type (uint16[16]), `+0x12` creature limit, `+0x14` creature group count.
- Map descriptor (16 bytes, `g_map_descs` `0x7f3c4`): `+2` bit 15 and `+3` bit 0 door sets, `+6` offset x, `+8` bits 0-5 level, bits 6-10 width - 1, bits 11-15 height - 1, `+0x0a` wall ornament count (bits 0-3), floor ornament count (bits 8-11), `+0x0c` door ornament count (bits 0-3), creature type count (bits 4-7), `+0x0d` high nibble flags (light, outdoor), `+0x0e` bits 8-11 and 12-15 door types.
- Square byte: bits 5-7 square type (0 wall, 1 floor, 2 pit, 3 stairs, 4 door, 5 teleporter, 6 fake wall, 7 unknown), bit 4 has things.
- `g_square_first_things` (`0x7f388`): first thing per square with things. `g_cur_map_extra_data` (`0x7f3c0`): creature types and ornament lists after the squares.
- `dun_get_location_on_map` finds the map at a level delta through `0x7f3ec` (offset per level) and `0x7f3d8` (map lists per level, -1 terminated).

### Thing records

Record address: `g_thing_data[type] + g_thing_size[type] * index` (`dun_get_thing_data`). Word `+0` of every record is the next thing.

| Type | Record | Fields |
|---|---|---|
| 0 | Door | `+2` bit 7 breakable by magic, `+3` bit 0 breakable by force |
| 1 | Teleporter | `+2` x (bits 0-4), y (bits 5-9); `+4` high byte target map, bit 0 closed |
| 2 | Text | `+2` bit 0 visible, bits 1-2 kind (0 text, 1 ornament, 2 marker, `(w & 6) == 4` AI marker), bits 3-10 index or id, bits 11-15 subtype or marker kind |
| 3 | Sensor | `+2` bits 0-6 sensor type, bits 7-15 data; `+4` bit 0 busy, bit 2 state, bits 3-4 effect, bit 5 inverted, bit 6 audible, bits 7-10 delay, bits 12-15 ornament ordinal; `+6` target cell (bits 4-5), x (bits 6-10), y (bits 11-15) |
| 4 | Creature group | `+2` possessions, `+4` creature type, `+5` active group index (`0xff` none), `+6` health, `+8` item class index, `+0x0a` mood flags (`+0x0b` bit 7 active), `+0x0c` home square, `+0x0e` bits 8-9 direction, bit 10 do not discard |
| 5 | Weapon | `+2` bits 0-6 type, bit 7 do not discard, bits 10-13 charges |
| 6 | Clothing | `+2` bits 9-12 charges |
| 7 | Scroll | |
| 8 | Potion | `+2` bits 0-7 power, bits 8-14 type, bit 15 do not discard |
| 9 | Container | `+4` bits 1-2 kind (0 chest or money box, 1 minion map), bits 13-15 type; minion map `+6` location (x bits 0-4, y bits 5-9, map bits 10-15) |
| 10 | Miscellaneous | `+2` bits 0-6 type, bits 8-13 stack count - 1, bits 14-15 charges |
| 11-13 | Unknown in DM2 | |
| 14 | Projectile | `+2` payload thing (minion control item for minions), `+4` kinetic energy, `+5` attack, `+6` timeline event |
| 15 | Explosion | `+2` bits 0-6 type (`0xff80` + n), bit 7 centered, bits 8-15 attack |

Item index (9 bits, `item_get_index`): base per thing type in `0x72274` (weapon 0, clothing `0x80`, misc `0x100`, potion `0x180`, group `0x1b0`, container `0x1e0`, scroll `0x1fc`). GDAT category per thing type in `0x72294` (door `0x0e`, teleporter `0x18`, group `0x0f`, weapon `0x10`, clothing `0x11`, scroll `0x12`, potion `0x13`, container `0x14`, misc `0x15`, explosion `0x0d`).

### Champion (0x107 bytes, `g_champions` `0x7fbd0`, 4 entries)

| Offset | Field |
|---|---|
| `+0x00` | Name (8 bytes) |
| `+0x08` | Title (`0x14` bytes) |
| `+0x1c` | Facing direction |
| `+0x1d` | Cell |
| `+0x1e` | Spell rune count |
| `+0x1f` | Poison event count |
| `+0x20`, `+0x21` | Last action command per hand |
| `+0x22` | Spell runes (4 bytes) |
| `+0x2a`-`+0x2c` | Action delay per hand and for spells |
| `+0x2e` | Timeline event of the damage display |
| `+0x30` | Last damage value |
| `+0x32` | Redraw flags (word: `0x4000` status box, `0x800` bars, `0x400` name, `0x1000` load) |
| `+0x34` | Wounds |
| `+0x36`, `+0x38` | Health, maximum health |
| `+0x3a`, `+0x3c` | Stamina, maximum stamina |
| `+0x3e`, `+0x40` | Mana, maximum mana |
| `+0x42`, `+0x43` | Defense bonus per hand |
| `+0x44`, `+0x46` | Food, water |
| `+0x48` | Poison total |
| `+0x4a` | 7 statistics × (current, maximum) bytes |
| `+0x58` | Statistic bonus bytes (7) |
| `+0x5f` | Experience per skill, uint32 × 20 |
| `+0xaf` | Skill bonus bytes (20) |
| `+0xc3` | Inventory, uint16 × 30 |
| `+0xff` | Load |
| `+0x101` | Portrait index (GDAT category `0x16`) |
| `+0x102` | Aura type (0-2 shields, 3-6 statistic boost) |
| `+0x103` | Aura value (uint16), removed by timeline event `0x48` |
| `+0x105` | Move speed modifier |

Party globals: `g_champion_count` (`0x7f276`), `g_leader_index` (`0x759a4`), `g_leader_hand_object` (`0x7fbb4`), `g_pending_damage` (`0x7fbac`), `g_pending_wounds` (`0x7fba4`), action timers (`0x7fad8`, 16 bytes per champion).

### Creatures

- Creature info (`0x24` bytes, `g_creature_info` `0x71968`): `+0` flags, `+3` regeneration, `+4` maximum health, `+6` attack, `+8` projectile power, `+9` flags (`0x10` self damage on shot, `0x20` attack two champions, `0x40` closes pits), `+0x16` aggression and think speed nibbles, `+0x1d` push resistance (`0xff` immovable), `+0x1e` possession slot count, `+0x23` death explosion size.
- Active group (`0x22` bytes, `g_active_groups` `0x7f898`): `+0` group thing index (`0xffff` free), `+2` timeline event, `+4` last action time, `+6` regeneration time, `+7` animation bits, `+0x0c`-`+0x10` counters and plan variables (merchant offer and payment sums), `+0x12` plan table index, `+0x13` plan step, `+0x14` accumulated damage, `+0x16` behavior entry, `+0x17` next action, `+0x18` destination (x bits 0-4, y bits 5-9, map bits 10-15), `+0x1a` action code, `+0x1b` direction, `+0x1c` cell or side, `+0x1d` facing, `+0x1e` sub-command, `+0x1f` phase, `+0x20` mode, `+0x21` interrupt. Count `g_active_group_count` (`0x7301c`), maximum `g_active_group_max` (`0x7f89c`).
- AI context (`0x350` bytes at `0x7f548`, saved and restored by `ai_begin` and `ai_end`): `g_ai_group_thing` `0x7f548`, `g_ai_group` `0x7f54a`, `g_ai_creature_info` `0x7f54e`, `g_ai_active_group` `0x7f552`, animation pointers `0x7f556`/`0x7f55a`, event copy `0x7f55e`, position `g_ai_group_x`/`_y`/`_map` (`0x7f564`, `0x7f565`, `0x7f56d`), `g_ai_command_result` `0x7f56b` (`0xfc` issued, `0xfd` failed, `0xfe` no move), permission masks `0x7f572`-`0x7f57a`, `g_ai_class` `0x7f580` (into `g_creature_class_flags` `0x7507a`), candidates `0x7f670`/`0x7f674`, item filter cache `g_item_filter_bitmaps` `0x7f7ea`, scratch `g_ai_scratch` `0x7f894`.
- AI action codes (`+0x1a`, dispatcher `execute_action` `0x2b75e`): 1, 2, 9 move; 6, 7 turn; 8, `0x26` melee; `0x0a` steal; `0x0e`, `0x0f` throw; `0x13` vanish; `0x17`, `0x18` give and take items; `0x27`, `0x28` shoot; `0x2f`-`0x31` use wall actuator; `0x35`-`0x3a` stairs or teleporter; `0x3b`-`0x40` transform, explode, spawn; `0x55` cloud explosion. Plan tables at `0x74f7e` (7-byte steps).
- Path search: `ai_search_area` (`0x3188a`) is a breadth-first search on a 32 × 32 map. It also fills `g_party_distance_map` (`0x7f8c8`), which `sound_request` uses for sound propagation.

### Timeline

- Event (12 bytes, `g_timeline_events` `0x760e8`): `+0` uint32 time (bits 0-23) and map (bits 24-31), `+4` type, `+5` priority, `+6` x or data, `+7` y, `+8` cell or data, `+9` effect (0 set, 1 clear, 2 toggle, 3 hold).
- `g_timeline_event_max` (`0x80428`) is the capacity; `g_timeline_event_count` (`0x8042a`) is the number of queued events. `g_timeline_queue` (`0x80420`) is a binary heap ordered by time, type, priority.

| Type | Handler |
|---|---|
| 1, 2 | Door animation, door destruction |
| 4 | Square event by square type (wall, floor, pit, door, teleporter, fake wall) |
| `0x0c` | Hide damage display |
| `0x0d` | Altar rebirth |
| `0x0e` | Apply item effect |
| `0x15` | `sound_request_delayed` |
| `0x19` | Explosion step |
| `0x1d`, `0x1e` | Projectile move |
| `0x21`, `0x22` | Creature animation, creature think (`creature_process_event`) |
| `0x3c`, `0x3d` | Move group |
| `0x46` | Magic light, `light_update_party_light` |
| `0x47` | Invisibility counter |
| `0x48` | Aura decrement |
| `0x4b` | Poison |
| `0x54` | Weather step (`world_update_day_night(1)`) |
| `0x55`-`0x5b` | Sensor animation, timer, busy bit, ambient sound |
| `0x5c` | Set marker bit |
| `0x5d` | Teleport party |
| `0x5e` | Spawn creature (`creature_create_group`) |

### Projectiles and explosions

- Projectile event data: `+6` projectile thing, `+8` `x | y << 5 | dir << 10 | step << 12`.
- `hit_impact(impact, x, y, cell, projectile)`: impact -1 creature, -2 champion, -3 party square, 0 wall, 4 door. Returns 1 when the projectile stops.
- `explosion_create(type, attack, x, y, cell)`: `cell == 0xff` = centered. Types: `0xff80` fireball, `0xff82`/`0xffb0`/`0xffb1` area damage (`0xffb0` lightning), `0xff83` non-material, `0xff86` poison, `0xff87` poison cloud, `0xff8e` lasting cloud, `0xffe4` smoke.

### Sound

- Request (12 bytes): `+0` sample pointer, `+4` priority, `+5` volume, `+6` lateral (int8), `+7` forward (int8), `+8` loudness, `+9` pan, `+0x0b` order. Formulas: [sound.md](../formats/sound.md#playback-volume-and-stereo-position).
- Sound table (7 bytes, `g_sound_table` `0x7f18c`): `+0` sample descriptor index, `+2` category, `+3` index, `+4` entry, `+5` raw GDAT entry (`0xffff` not loaded).
- Sample descriptor (16 bytes, `g_sample_descs` `0x7f194`): `+0` data, `+4` length, `+6` rate, `+8` next.
- Delayed sound (14 bytes, `g_delayed_sounds` `0x7f188`, 8 entries): `+4` category, `+5` index, `+6` entry, `+7` map, `+8` x, `+9` y, `+0x0a` priority, `+0x0c` volume.

### GDAT database in memory

- Key table rows (8 bytes, columns `TIDSPFG`). Rows with column F high nibble not equal to the language byte are skipped. Rows with G = `0xff` are preloaded.
- Lookup: `g_gdat_category_start` (`0x7f9dc`) -> `g_gdat_index_start` (`0x7f9e0`) -> entries (`0x7f9e4`, 4 bytes: `+0` type, `+1` entry, `+2` raw index or value).
- `g_gdat_entries` (`0x7fa54`): uint32 per raw entry. Bit 31 set = not loaded (low bits size). Else pointer to data, 2 bytes after a uint16 size.
- Categories 5, 6, `0x1b` are read from the file on every use (`gdat_category_is_raw`).
- Image buffer: 6-byte header before the pixels (`-6` bpp, `-4` width, `-2` height). 4-bpp rows are `(width + 1) / 2` bytes.
- Encoded image: word 0 bits 0-9 width, word 1 bits 0-9 height, top 6 bits of word 1 select raw, 8 bpp (`g_gdat_8bpp_marker` `0x7fac8`), or 4 bpp. Decoders: `image_decode_4bpp` (`0x12e4f`), `image_decode_4bpp_delta` (`0x13258`, base images from GDAT `(0,0,8,0)`), `image_decode_8bpp` (`0x3f71c`).
- Compressed items: `image_decode_8bpp` selects the codec through `g_image_decoders` (`0x75798`). The codecs: [graphics.md](../formats/graphics.md#image-compression).
- Text entries hold `XX=n` fields (`gdat_get_text_field`; names `SK LV CM BZ TR ST PA TA NC EX PB DM MS SD RP HN AT WH`). Encrypted text: `byte = (byte ^ 0xff) - counter`.

### Memory manager and image cache

- Pool block header (`0x1e` bytes, `g_mem_pool_list` `0x7f9d4`): `+0` next, `+4` size, `+8` top, `+0x0c` free, `+0x10` flags. `mem_alloc(size, flags, pool_flags)`: pool flag bit 15 zero fill, 1 pool block, 2 temporary.
- Image cache block (`0x12`-byte header): `+0` size (negative = used), `+4` usage (`0xffff` locked, `0xfffe` pinned), `+6`/`+8` LRU links, `+0x0a` id, `+0x0c` bpp, `+0x0e` width, `+0x10` height. 32-bit temporary keys: `0x20000000 | group` (AI data), `0x30000000 | group` (path).

### Drawing and layout

- Rectangle: int16[4] x, y, w, h.
- Layout zones: GDAT `(1,0,4,0)` ([graphics.md](../formats/graphics.md#image-compression)). Zone record: `+0` anchor type (0-8 on a 3 × 3 grid, 9 size, `0x0a`-`0x12` relative to parent), `+2` parent, `+4` x, `+6` y. `zone_get_rect_clipped(zone, rect, w_scale, h_scale)` scales in 1/10000.
- Image reference (`0x18` bytes): `+0` pixels, `+4` source flags (4 GDAT, 8 cache), `+8` category, `+9` index, `+0x0b` entry, `+0x0c` cache id, `+0x0e` x, `+0x10` y, `+0x12` w, `+0x14` h, `+0x16` bpp.
- Draw context (`0x13a` bytes, `draw_ctx_init`; view instance `g_view_draw_context` `0x8018a`): image reference, `+0x18` zone, `+0x1c`/`+0x1e` offset, `+0x24` rect, `+0x2c` destination bitmap, `+0x30` transparent color, `+0x32` flip flags, `+0x34`/`+0x36` scale (64 = 100 %), `+0x38` remap count, `+0x3a` remap table (16 bytes).
- `gfx_blit(src, dst, rect, src_x, src_y, src_width, dst_width, transparent, mirror, src_bpp, dst_bpp, colormap)`: `transparent < 0` = opaque; mirror 0 none, 1 horizontal, 2 vertical, 3 both.
- View square (`0x12` bytes, `g_view_squares` `0x80040`, 23 entries): [view.md](../rules/view.md#view-square-record-and-the-view).
- Viewport click zone (12 bytes, `g_view_click_zones` `0x7f2ec`): `+0` rect, `+8` thing, `+0x0a` cell or side, `+0x0b` kind.
- Mouse area (10 bytes, `0x722d4`): `+0` zone, `+2` cursor kind, `+3` flags, `+4`/`+5` enter and leave commands, `+6` node pointer.
- Text glyph: 6 × 6 pixels from `g_font_bitmap` (`0x80414`, GDAT `(1,0,7,0)`).

### Input and commands

- Event ring `g_input_events` (`0x7f48c`, 11 × 6 bytes): `+0` type, `+2` x or key, `+4` y.
- Command record (14 bytes): `+0` x, `+2` y, `+4` command, `+6` rect. Queue `g_command_queue` (`0x7f4dc`, 3 records).
- Commands (`command_execute`): 1-2 turn, 3-6 move, 7-`0x0b` inventory, `0x10`-`0x13` leader, `0x14`-`0x41` slots, `0x50` viewport click, `0x55` minion map click, `0x65`-`0x6c` spell area, `0x70`-`0x7b` action area and hands, `0x8c` save, `0x8e`/`0x8f` sleep and wake, `0x90`/`0x91` pause, `0xd7`-`0xda` start menu, `0xe0` quit.

### Save file

- Bit stream, records, and thing order: [save.md](../formats/save.md#bit-packed-records).
- Save slot table: 10 × `0x2a` bytes; `+2` name, `+0x26` = `0xdeadbeef` unused.

### Global variables (script variables)

`dun_get_global_var`, `dun_modify_global_var`: vars `0x00`-`0x3f` bits (`g_global_flags` `0x7f100`), `0x40`-`0x7f` bytes (`g_global_bytes` `0x7f0c0`), `0x80`-`0xbf` words (`g_global_words` `0x7f108`). Operations: 0 set, 1 clear, 2 toggle, 3 add, 4 subtract, 6 assign.

## DM2-specific systems

| System | Location | Notes |
|---|---|---|
| Money | `0x1538d`-`0x15958`, `0x1f2ab`-`0x1f50d`, `money_normalize_coins` `0x29598`, `money_draw_coin_counts` `0x39409`, `money_box_click_coin` `0x42a35`, `ui_draw_money_box` `0x4534f` | Coins are misc items with attribute 0 flag `0x4000`: `money_init_coin_table` builds `g_coin_item_types` from exactly this flag, and `is_coin` tests it (reDM2 calls the test `object_is_stackable`). `g_coin_values` `0x7f3a8`, `g_coin_item_types` `0x7f390`, `g_coin_type_count` `0x7f3fa`. `money_split_amount` is greedy. |
| Shops | `money_get_item_value` `0x15737`, merchant AI commands `0x27cd2`-`0x29caa`, `show_shop_display` `0x4ec19` | Merchants are creatures. Class sets from creature text (`W A J P C S` letters). Sensor type `0x30` is a shop display. |
| Minions | `item_is_minion_map` `0x1fa8f`, `minion_map_set_location`, `minion_find_link_projectile`, `minion_map_click_set_destination` (command `0x55`), `creature_summon_group` `0x30944`, `minion_dismiss` `0x30b3d`, `mmap_draw` `0x44fc6` | Champion actions `0x2c`-`0x33` summon creature types `0x30`-`0x35`. A type 14 record in the group possessions links the minion to its map item. The map panel drains mana. |
| Weather | `0x59f12`-`0x5a808`, `world_update_day_night` `0x5a073`, `show_rain` `0x4e79c`, globals `0x8046a`-`0x80480` | Storm curves (table `0x76104`), rain intensity `0x8047e`, wind `0x8047f`, lightning (`explosion_create(0xffb0)`), thunder. Images in GDAT category `0x17`. |
| Day and night | `world_update_day_night`, `sensor_process_map_entry` `0x59785` | Hour = `(time + g_day_time_offset) / 0x555 mod 24`; `g_light_by_hour`. Sensor type `0x21` and night creature generators. |
| Outdoor view | `outdoor_draw_sky` `0x54699`, `outdoor_relative_distance` `0x542f6`, `move_compute_map_arrival` | No separate renderer. `view_draw_dungeon` adds a sky layer. Sky objects are GDAT category `0x17` text entries. Map links draw squares of the linked map. |
| Skullkeep machines | `0x4bbe4`-`0x4cdcc`, `0x56b39`-`0x59785` | No separate machine code. Machines are sensor networks: relays, timers, counters, global flags, object transfer, missile launchers, creature generators, end-game sensor. |
| Creature pushing | `move_push_creature`, `creature_push_group`, input mode 6 | Party strength against creature info `+0x1d`. |

## Symbol merge decisions

Two ranges gave different names for the same global. The merge keeps the name from the range that holds the writer, or the more specific name:

| Address | Kept | Dropped |
|---|---|---|
| `0x71706`, `0x71708` | `g_viewport_width`, `g_viewport_height` | `g_screen_width`, `g_screen_height` |
| `0x717bc` | `g_dialog_screen_mode` | `g_game_over_input_state` |
| `0x7507a` | `g_creature_class_flags` | `g_ai_behavior_flags` |
| `0x7f1a4` | `g_missile_from_sensor` | `g_projectile_slow_event` |
| `0x7f23c` | `g_game_completed` | `g_game_won` |
| `0x7f258` | `g_party_move_steps` | `g_walk_anim_step` |
| `0x7f25c`, `0x7f25e`, `0x7f272` | `g_alt_view_x`, `g_alt_view_y`, `g_alt_view_dir` | `g_arrival_x`, `g_arrival_y`, `g_arrival_dir` |
| `0x7f27a`-`0x7f27e` | `g_party_square_y`, `_x`, `_map` | `g_ref_y`, `g_ref_x`, `g_ref_map` |
| `0x7f38c` | `g_view_redraw_flags` | `g_map_update_flags` |
| `0x7f390` | `g_coin_item_types` | `g_coin_item_indices` |
| `0x7f564`-`0x7f580` | `g_ai_group_x`, `_y`, `_map`, `g_ai_command_result`, `g_ai_class` | `g_ai_x`, `g_ai_y`, `g_ai_map`, `g_ai_cmd_result`, `g_ai_behavior` |
| `0x7f894` | `g_ai_scratch` | `g_item_filter_pool` |
| `0x7f8d0`-`0x7f8d4` | `g_ai_target_y`, `_map`, `_x` | `g_ai_party_y`, `_map`, `_x` |
| `0x7ffec` | `g_magic_light` | `g_spell_light` |
| `0x80410` | `g_message_scroll_bitmap` | `g_message_line_bitmap` |
| `0x80414` | `g_font_bitmap` | `g_message_font` |
| `0x80484` | `g_iop_input_info` | `g_iop_cursor_buffer` |
| `0x761a0` | `g_heap_segment_list` | `g_heap_free_list` |
| `0x7720c`, `0x77210` | `g_hmi_pit_enabled`, `g_hmi_use_dpmi_vectors` | `g_hmi_timer_irq_protect`, `g_hmi_timer_flag_2` |
| `0x77d43` | `g_hmi_timer_current_song` | `g_hmi_current_song` |
| `0x77e43`, `0x7c24e` | `g_hmi_driver_file_path`, `g_hmi_midi_driver_path` | `g_hmi_detect_path`, `g_hmi_midi_path` |
| `0x782a0`, `0x79188`, `0x79330`, `0x79370` | `g_hmi_midi_driver_active`, `g_hmi_midi_driver_call_table`, `g_hmi_midi_song_playing`, `g_hmi_midi_song_data` | `g_hmi_midi_driver_loaded`, `g_hmi_midi_function_tables`, `g_hmi_song_active`, `g_hmi_song_structs` |

Renamed because one name was used at two addresses, or because the anchor was wrong:

| Address | Name | Reason |
|---|---|---|
| `0x7f8a0`, `0x7f8a2` | `g_ai_dest_y`, `g_ai_dest_x` | `g_ai_target_*` is kept for `0x7f8d0`-`0x7f8d4` |
| `0x77aa3` | `g_hmi_digi_driver_info` | `g_hmi_digi_driver_table` is kept for `0x777a8` |
| `0x80428`, `0x8042a` | `g_timeline_event_max`, `g_timeline_event_count` | The anchor names were swapped |
| `0x139e4` | `shift_message_area` | Takes no rectangle; `screen_present_rect` is `0x13a87` |
| `0x3ec2b` | `gdat_get_image` | Returns the decoded image, not an index |

Function names had no clashes.

## Low-confidence names

The role is known from the code. The game meaning or the exact arguments are not confirmed.

- `0x10000`-`0x18e7f`: `disk_query_drive_info`, `disk_get_drive_path`, `disk_prompt_stub`, `input_stub_return_zero`, `mouse_capture_begin`, `mouse_capture_end`, `iop_input_lock`, `iop_input_unlock`, `dun_get_global_var`, `dun_modify_global_var`, `item_get_image_kind`, `creature_get_cell_mask`, `money_get_coin_type_for_slot`, `melee_attack_group`, `fill_rect_dithered`, `system_init_stub`.
- `0x18e7f`-`0x1fef6`: `aspect_cell_to_xy`, `zone_id_for_view_cell`, `element_zone`, `gfx_offscreen_fill_rect_alt`, `gfx_offscreen_draw_text_in_zone_alt`, `dun_find_adjacent_level_link`, `dun_get_teleporter_link_side`, `dun_get_linked_teleporter_dest`, `dun_find_open_teleporter_link`, `system_debug_hook`, `item_get_attr_3`, `creature_get_info_for_group`, `creature_get_info_word_1e`, `creature_get_info_word_20`, `creature_get_info_byte_1d`, `creature_get_gdat_attr4`, `item_is_minion_map`, `minion_map_set_location`, `minion_find_link_projectile`, `dun_get_wall_ornament_attr_0a`, `dun_is_alcove_shown`, `dun_get_wall_thing_ornament_attr_13`, `dun_get_thing_ornament_attr_0b`, `dun_get_thing_ornament_attr_0c`, `dun_get_door_attr_0d`, `dun_get_door_attr_10`, `dun_get_door_attr_0e`, `dun_get_door_attr_0f`, `dun_get_door_attr_11`.
- `0x1fef6`-`0x2b96a`: `input_cond_flag_7f0b4`, `input_cond_value_757b8`, `input_cond_dialog_kind`, `input_cond_action_panel_kind`, `input_cond_action_champion_mask`, `input_cond_action_panel_variant`, `input_cond_no_action_champion_in_cell`, `click_front_wall`, `view_click_drop_done`, `is_side_blocked`, `creature_alert`, `schedule_loop_start`, `schedule_loop_end`, `creature_ai_scan_idle`, `creature_ai_scan_kind0`, `creature_ai_scan_items_seen_2`, `creature_ai_scan_items_seen_4`, `creature_ai_scan_item_counts_1`, `creature_ai_scan_item_counts_3`, `creature_ai_scan_carried_items_2`, `creature_ai_scan_carried_items_4`, `creature_ai_scan_path_kind13`, `creature_ai_scan_path_kind5`, `creature_ai_scan_carried_kind_f`, `creature_ai_scan_carried_kind_10`, `creature_ai_scan_by_condition_15`, `creature_ai_cmd_follow_candidate_path`, `creature_ai_cmd_shoot_random`, `creature_ai_cmd_seek_item`, `ai_cmd_seek_with_carried`, `ai_cmd_front_group_carries`, `creature_ai_cmd_place_item_on_sensor`, `creature_ai_cmd_drop_then_seek`, `creature_ai_cmd_set_mood_flag`, `creature_ai_cmd_seek_kind11_targets`, `creature_ai_cmd_consume_carried_item`, `creature_ai_cmd_check_transform_marker`, `creature_ai_cmd_set_special_action`, `creature_ai_cmd_path_to_target_alt`, `creature_ai_act_trigger_square`, `creature_ai_act_explode_or_spawn`, `creature_ai_act_cloud_explosion`, `shop_cmd_wait_for_offer`, `shop_cmd_count_payment`, `dun_set_pit_teleporter_state`.
- `0x2b96a`-`0x3ba23`: `creature_thing_matches_filter9`, `creature_group_in_state5`, `creature_group_check_pass_flag`, `creature_group_current_frame_flag`, `creature_group_check_field8`, `creature_roll_armor_factor`, `ai_find_marker`, `ai_find_marker_by_id`, `ai_find_target_in_lines`, `ai_projectile_incoming`, `ai_wall_actuator_accepts`, `ai_find_wall_actuator`, `is_near_party_front`, `dun_mark_square_bit1`, `dun_merge_object_lists`, `dungeon_read_level_table`, `inv_draw_mouth_picture`, `inv_leader_hand_removed_stub`, `input_hold_right_button`, `zone_compute_icon_rect`, `zone_get_icon_rect`.
- `0x3ba23`-`0x49ef8`: `champ_find_hand_source_slot`, `throw_fatigue`, `gdat_index_last_type`, `gdat_get_image_offset`, `image_decode_8bpp`.
- `0x49ef8`-`0x5a87c`: `move_update_carried_missiles`, `move_check_missiles_hit_party`, `aspect_creature_cell`, `dun_map_gfx_flag20_clear`, `copy_mirrored`, `view_draw_wet_image`, `show_sparkle_field`, `view_offset_toward_party`, `view_mark_hidden_squares`, `view_unhide_linked_squares`, `show_stairs_side`, `show_stairs_front`, `show_ceiling_pit`, `show_floor_pit`, `mem_parse_config_entry`, `mem_test_all_maps`, `timeline_event_apply_item_effect`, `timeline_event_door_button_release`, `schedule_timer`, `run_activation`, `sensor_process_map_entry`, `move_party_warp_to_marker`.
- `0x5a87c`-`0x62272`: `hmi_timer_isr_entry`, `hmi_midi_register_branch_callback`, `hmi_midi_register_loop_callback`.
- `0x62272`-`0x695ac`: 80 HMI driver stub, voice queue, and static MIDI driver names (`hmi_drv_call_fn*`, `hmi_mdrv_a003_*`, `hmi_mdrv_a005_*`, `hmi_mdrv_a007_*`, `hmi_voice_queue_*`, and others). The port does not use this code.

Library signatures (clib, DPMI, HMI) are firm only for the names. `dpmi_map_physical_address`, `hmi_load_digi_driver_file`, and `hmi_load_midi_driver_file` have incomplete signatures.

## Open questions

1. Game tick rate: the real rate of the `INT 0x2d` callback under DOS/4GW ([runtime.md](../formats/runtime.md#unknown-values)).
2. Thing types 11-13 and square type 7 in DM2.
3. `0x7f278` with `0x7f25c`, `0x7f25e`, `0x7f272`: alternate view position or arrival position after a map link. Also the difference between `g_party_square_*` (`0x7f27a`-`0x7f27e`) and `g_party_x`/`g_party_y`.
4. Creature AI plan tables (`0x74f7e`) and scanner kinds: a table dump is necessary to give each plan command and scanner a game meaning.
5. Sensor types `0x2c`, `0x32`, `0x3f`, `0x41`, `0x46`, `0x4a`, `0x7e` and the wall-view effects in `dun_apply_wall_things_to_aspect`.
6. Functions that did not decompile: `sound_request_with_fallback` (`0x15baa`), `melee_attack_group` (`0x18a57`), `pal_get_dimmed_remap` (`0x1b2be`), `render_offscreen` (`0x4e620`). The disassembly gives their code.
7. Ghidra function boundaries: hidden functions at `0x113dd`, `0x114d2`, `0x14c93`, `0x5b577`, `0x62e81` (PIT ISR) are inside other functions.
8. Meaning of the GDAT text fields `BZ TR PA TA EX PB MS SD RP HN AT`, and of event `0x0e` (`timeline_event_apply_item_effect`).
