# SKULL.EXE address layout

Input: `reference/decomp/SKULL.c` (1362 functions), image `build/skull.bin` (code object 0x10000-0x695ac, data object 0x70000-0x828f0).

All 1195 direct call targets in the code are function starts in `reference/decomp/SKULL.c` (one exception: 0x68c0f, inside clib). Functions that are reached only through function pointers can be missing.

## Big blocks

| Block | Range | Functions | Content |
|---|---|---|---|
| Game | 0x10000-0x5a87c | 1037 | Game code, including its own platform glue (IOP, sound, file I/O). |
| IOP glue and HMI | 0x5a87c-0x62272 | 114 | INT 0xFC glue, bit decoder, HMI SOS (timer, digital, detection, MIDI), clib `malloc`/`free`. |
| Watcom clib and HMI low level | 0x62272-0x695ac | 211 | Startup, file I/O, string, heap, errno; HMI PIT/IRQ code, DPMI helpers, HMI driver loaders, HMI MIDI internals. |

Watcom links object files in order, and libraries after them. The game code calls only about 50 library functions (see `anchors.csv`). Module boundaries inside the game block are not visible in the binary (no padding, no alignment). The ranges below use semantic boundaries (call clusters, shared globals, strings).

## Module map of the game block (approximate)

| Range | Content | Key anchors |
|---|---|---|
| 0x10010-0x10d9e | Sound and music glue to HMI, system init and shutdown, timer ISR | `sound_init`, `sound_play_requests`, `sound_compute_volume_pan`, `music_play_song`, `music_update_for_map`, `system_init`, `timer_isr`, `system_shutdown` |
| 0x10d9f-0x110ff | Text drawing into a bitmap, `mem_copy`/`mem_zero`, file wrappers | `text_draw`, `file_open`, `file_read`, `file_write` |
| 0x1105f-0x114e9 | Drive and save disk check, keyboard via IOP, IOP message | `disk_detect_drives`, `disk_check_save_disk`, `kbd_read_key` |
| 0x11504-0x13745 | Blitters (4 and 8 bpp, flip, transparency, scaling), fill; 0x12e4f and 0x13258 are probably image decoders | `gfx_blit`, `gfx_fill_rect` |
| 0x13887-0x13be5 | VGA retrace, palette and present through IOP | `vga_wait_vblank`, `pal_set_palette`, `play_present_viewport` |
| 0x13ee3-0x14d39 | Mouse cursor images, input ISR, mouse hide/show/state | `input_event_isr`, `input_init`, `mouse_hide`, `mouse_show`, `mouse_get_state` |
| 0x14cf2-0x163c2 | Creature and item sound selection from GRAPHICS.DAT, sound request queue | `sound_request`, `sound_flush_requests`, 0x15baa (sound play wrapper, decompile failed) |
| 0x16457-0x18d9e | Projectiles and explosions (create, move, hit), distance and direction helpers | `projectile_create`, `explosion_create`, 0x1861b, 0x1863d |
| 0x18e7f-0x19f3d | Rectangles and layout zones | `rect_set`, `rect_contains`, `zone_get_rect` |
| 0x19fd2-0x1aa75 | Dialog boxes and menus (0x1a37d builds a dialog, 0x1a0a4 runs it) | 0x1a37d, 0x1a0a4 |
| 0x1ac92-0x1c440 | Picture descriptors and composited drawing (0x13a-byte draw context) | 0x1b3e8, 0x1b54a, 0x1b8e5, 0x1be6a |
| 0x1c472-0x1c710 | String, math, random, system error, delay | `str_copy`, `rand_next`, `rand_below`, `system_error`, `wait_vblanks` |
| 0x1c724-0x1f07c | Dungeon data: maps, squares, thing lists | `dun_set_current_map`, `dun_get_square`, `dun_get_thing_data`, `dun_get_next_thing`, `dun_get_unused_thing` |
| 0x1efa8-0x1feb5 | Item type database, creature info, map location helpers | 0x1efa8, 0x1f12f, 0x1f274, `creature_get_info` |
| 0x1fef6-0x2005b | End of game, quit | `session_end`, `session_quit` |
| 0x200b4-0x21301 | Input mode tables (0x72ed1), click and key to command mapping, input poll | `input_set_mode`, `input_mouse_click`, `input_poll`, `input_flush` |
| 0x21394-0x224a9 | Leader selection, command execution, command queue | `champ_set_leader`, `command_execute`, `command_process_queue` |
| 0x22544-0x24629 | Party actions and movement, stairs, turn (uses sensors and sounds) | 0x22a68, 0x235bf, 0x24328 |
| 0x24691-0x249db | Main loop and `main` | `game_loop`, `main` |
| 0x24a88-0x2f636 | Creature AI: group update (0x25420), AI command dispatcher 0x2b75e with handlers 0x2a088-0x2b724, movement and attack 0x2b96a-0x2f636; command tables at 0x74f7e | 0x24a88, 0x25420, 0x2b75e |
| 0x2f8d6-0x3188a | Creature groups: get group at square, damage, death; 0x3188a is the largest function (9583 bytes) | `creature_get_group_at`, 0x30d45, 0x31348 |
| 0x33df9-0x3502b | Creature counts on map entry, save stream (bit packed), save game | `gamefile_read`, `save_read_packed`, `session_save` |
| 0x3573a-0x37cfb | Dungeon loader, load game, save slot list | `dungeon_load`, `dungeon_open_file`, `session_load`, `session_list_slots` |
| 0x37d9f-0x3a464 | Save slot display, start menu pictures (0x386f5), 0x38877 (picture and wait), party light (0x389c2), hand item burn-down (0x39044), click handling for leader hand (0x39c3f) | `light_update_party_light`, `champ_burn_held_lights` |
| 0x3a6ab-0x3aadd | Text format with `.Z` escapes, GRAPHICS.DAT text, data path | `text_format`, `gdat_get_text`, `data_path_init` |
| 0x3ba23-0x3f8ff | Memory manager, GRAPHICS.DAT index and entry cache, image cache | `mem_alloc`, `gdat_load_index`, `gdat_entry_index`, `gdat_get_entry_data`, `image_cache_get` |
| 0x3f927-0x41171 | Champion action area and action timers | 0x3fe68, `0x40ee2` |
| 0x414a5-0x428a2 | Champion action execution (0x414a5, table 0x7fb3c), spell casting (0x422f5) | 0x414a5, 0x422f5 |
| 0x42ae4-0x45503 | Interface panels (spell area, action area, inventory panel drawing) | 0x42b8f, 0x43a7c |
| 0x45869-0x47c05 | Champion core: cells, slots, leader hand, stats, damage, stamina, load | `champ_put_object_in_leader_hand`, `champ_add_object_to_slot`, 0x4722a, 0x466ab |
| 0x47cc3-0x49d46 | Champion time effects, champion status drawing, recruit (name entry 0x496e5) | `champ_apply_time_effects`, `ui_draw_all_champion_states`, `champ_add_to_party` |
| 0x49ef8-0x4b108 | Move things between squares (0x4b108 is the central move function, 31 callers) | 0x4b108, 0x4a34a |
| 0x4bbe4-0x4df08 | Sensors and actuators (0x4c134 square sensors, 0x4cdcc wall sensors), probably Skullkeep machines | 0x4bbe4, 0x4c134, 0x4cdcc |
| 0x4db2e-0x54b3f | Dungeon view and outdoor view drawing | `view_draw_dungeon`, 0x4e502, 0x4f3df |
| 0x54e41-0x55f4f | Screen refresh, game init, message area | `session_init`, `text_draw_in_zone`, `text_clear_expired_rows` |
| 0x55fdc-0x5a073 | Timeline queue and event handlers | `timeline_add_event`, `timeline_delete_event`, `timeline_process` |
| 0x5a073-0x5a808 | Day and night (hour = time / 0x555 mod 24), probably weather | `world_update_day_night` |

DM2-specific subsystems without a confirmed location: outdoor view, weather, minions, money and shops, Skullkeep machines, magic details. Expected places: outdoor view and weather in 0x4db2e-0x54b3f and near 0x5a073; minions inside creature AI (0x24a88-0x3188a); shops and money near champion inventory (0x42ae4-0x49d46) and command execution; machines in the sensor block (0x4bbe4-0x4df08).

## Proposed split into 8 ranges

| # | Range (start inclusive, end exclusive) | Functions | Content | Key anchors |
|---|---|---|---|---|
| 1 | 0x00010000-0x00018e7f | 173 | Platform glue (sound, music, timer, file I/O, disk, keyboard), blitters, screen and palette, mouse and input ISR, sound requests, projectiles and explosions | `sound_init`, `music_play_song`, `timer_isr`, `file_read`, `gfx_blit`, `pal_set_palette`, `play_present_viewport`, `input_event_isr`, `sound_request`, `projectile_create` |
| 2 | 0x00018e7f-0x0001fef6 | 166 | Rectangles and zones, dialogs, picture drawing, string/math/random, system error, dungeon data access, item and creature info | `zone_get_rect`, `rand_below`, `system_error`, `dun_get_thing_data`, `dun_set_current_map`, `dun_get_unused_thing`, `creature_get_info` |
| 3 | 0x0001fef6-0x0002b96a | 181 | End of game, input and commands, party movement, main loop, `main`, creature AI part 1 (group update, AI command dispatcher 0x2b75e and its handlers) | `session_end`, `input_mouse_click`, `command_execute`, `command_process_queue`, `game_loop`, `main` |
| 4 | 0x0002b96a-0x0003ba23 | 145 | Creature AI part 2 (movement, attack), creature groups and damage, save and load, dungeon loader, save slots, start menu pictures, light, text format | `creature_get_group_at`, `session_save`, `session_load`, `dungeon_load`, `light_update_party_light`, `text_format` |
| 5 | 0x0003ba23-0x00049ef8 | 208 | Memory manager, GRAPHICS.DAT database and image cache, champion actions and spells, interface panels, champion core, champion display, recruiting | `mem_alloc`, `gdat_entry_index`, `gdat_get_entry_data`, `image_cache_get`, `champ_add_object_to_slot`, `champ_apply_time_effects`, `ui_draw_all_champion_states` |
| 6 | 0x00049ef8-0x0005a87c | 164 | Moving things, sensors and actuators (machines), dungeon and outdoor view, game init, message area, timeline, day and night | `view_draw_dungeon`, `session_init`, `timeline_add_event`, `timeline_process`, `world_update_day_night` |
| 7 | 0x0005a87c-0x00062272 | 114 | IOP glue, bit decoder, HMI SOS timer, digital, detection, MIDI; clib `malloc`/`free` | `iop_init_shared_buffer`, `hmi_timer_init_system`, `hmi_digi_init_driver`, `hmi_midi_init_song`, `clib_malloc` |
| 8 | 0x00062272-0x000695ac | 211 | Watcom clib (startup, I/O, string, heap, errno), HMI PIT and IRQ code, DPMI helpers, HMI driver loaders and MIDI internals | `clib_cstart`, `clib_cmain`, `clib_read`, `clib_setjmp`, `hmi_timer_install_isr`, `dpmi_lock_region` |

Notes on the split:

- Range 3 and range 4 both hold creature AI. The cut at 0x2b96a is after the AI command dispatcher 0x2b75e and its handlers. 0x2b96a is called from 0x26d16 (range 3) and 0x2d792 (range 4).
- Range 5 is the largest. The memory manager and the GRAPHICS.DAT loader share globals (0x7fa30-0x7fa7c), so they stay together.
- Ranges 7 and 8 are library code. The C port replaces it (no HMI, no DOS/4GW). Agents 7 and 8 give role names with the prefixes `clib_`, `hmi_`, `dpmi_`, `iop_`, and they do not analyze driver internals in depth.

## Call graph hubs

| Address | Callers | Name |
|---|---|---|
| `0x0001d2da` | 161 | dun_get_thing_data |
| `0x0001d30b` | 86 | dun_get_next_thing |
| `0x0003c5d3` | 66 | gdat_entry_index |
| `0x00010f43` | 59 | mem_copy |
| `0x0002fba9` | 47 | creature_get_group_at |
| `0x0001c6b7` | 46 | rand_below |
| `0x0001efa8` | 44 | (unnamed) thing to item database type and index |
| `0x0001c687` | 42 | math_max16 |
| `0x0001d391` | 42 | dun_get_square_first_thing |
| `0x0001c724` | 40 | dun_set_current_map |
| `0x0003bbba` | 39 | mem_alloc |
| `0x0001c6f6` | 39 | rand_2bits |
| `0x00056390` | 37 | timeline_add_event |
| `0x0001c67e` | 36 | math_min16 |
| `0x00019cfc` | 35 | zone_get_rect |
| `0x0001ca96` | 35 | dun_get_square |
| `0x0001c6a1` | 32 | rand_next |
| `0x0001f8d9` | 31 | creature_get_info |
| `0x0004b108` | 31 | (unnamed) move thing, central movement function |
| `0x00014c68` | 29 | mouse_show |
| `0x00014c55` | 29 | mouse_hide |
| `0x0001f12f` | 28 | (unnamed) item attribute word from GRAPHICS.DAT (type 0x0b) |
| `0x00015baa` | 27 | (unnamed) play sound at position (decompile failed) |
| `0x0001f9a3` | 27 | (unnamed) creature attribute word for group |
| `0x0001c6dc` | 27 | rand_bit |
| `0x0003f337` | 27 | image_cache_get |
| `0x00010f51` | 26 | mem_zero |
| `0x0001c690` | 25 | math_clamp16 |
| `0x0001f274` | 24 | (unnamed) GRAPHICS.DAT category of a thing |
| `0x0003ec2b` | 24 | gdat_get_image_index |
| `0x0003a921` | 23 | gdat_get_text |
| `0x0001d542` | 23 | dun_unlink_thing_from_square |
| `0x0003c92e` | 21 | gdat_entry_exists |
| `0x0003bca5` | 20 | mem_free_size |
| `0x0001d3db` | 20 | dun_link_thing_to_square |
| `0x0001d2cc` | 19 | dun_get_square_first_thing_data |
| `0x00012ad2` | 18 | gfx_blit |
| `0x000458f4` | 18 | champ_get_index_in_cell |
| `0x0001936f` | 18 | (unnamed) layout zone computation |
| `0x0003ee1d` | 18 | (unnamed) GRAPHICS.DAT image lookup |
| `0x0001be6a` | 18 | (unnamed) draw item or creature picture |
| `0x0001863d` | 16 | (unnamed) direction from one square to another |
| `0x0001d113` | 16 | (unnamed) stairs or teleport destination |
| `0x0002f636` | 16 | (unnamed) creature or object flag test |
| `0x0004e616` | 16 | (unnamed) flush view picture (draw context 0x8018a) |
