# Range 6 notes (0x49ef8-0x5a87c)

Analysis notes of the address range 0x49ef8-0x5a87c; the names are in [skull-symbols.csv](../skull-symbols.csv).

164 functions. 21 names are low confidence (see "Low-confidence names").

## Anchor corrections

- `world_update_day_night` (0x5a073): the name is kept. The function is mainly the weather system: storm intensity, rain, clouds, lightning strikes (`explosion_create(0xffb0, ...)` on random squares), thunder sound, and sky objects. The hourly light update is a small part. The subsystem is changed to `weather`. Its parameter is "storm step" (1 = called from timeline event 0x54), not "force".
- `g_timeline_event_count` (0x80428) and `g_timeline_event_max` (0x8042a) are swapped. 0x80428 is the capacity (`system_error(0x2d)` when the count reaches it; `timeline_init` allocates `0x80428 * 12` bytes). 0x8042a is the number of queued events.
- In `g_timeline_events` +4 is the type and +5 the priority. Event layout: +0 uint32 time (bits 0-23) and map (bits 24-31), +4 type, +5 priority, +6 x or data word, +7 y, +8 cell or data word, +9 effect (0 set, 1 clear, 2 toggle, 3 hold).

## Key data structures

### View square (0x12 bytes, `g_view_squares` 0x80040, 23 entries; filled by `view_load_square`)

| Offset | Use |
|---|---|
| +0, +1 | map x, y (int8) |
| +2 | aspect type from `aspect_get` (0 wall, 1 floor, 2 pit, 5 teleporter, 7 hidden, 0x10 door side, 0x11 door front, 0x12 stairs front, 0x13 stairs side) |
| +4 | aspect flags |
| +6 | first thing on the square |
| +8 | stairs up/down or pit flag (depends on type) |
| +0x0a | door thing (door squares) |
| +0x0c | floor or wall ornament (byte), +0x0d frame offset |
| +0x10 | text thing for wall text |

Static per-position tables start at 0x75af7 (side offset), 0x75afa (lateral), 0x75b11 (depth), 0x75b6d (scale by depth), 0x75c1a/0x75c31 (floor ornament entries).

### Draw context (0x13a bytes, `g_view_draw_context` 0x8018a)

Set by `view_setup_picture` through 0x1b3e8 and flushed by `view_flush_picture` (0x1b8e5). Fields used here: +0x0c/+0x10 flags, +0x18 x, +0x1e/+0x22 offsets, +0x24 rect, +0x2c target bitmap, +0x30 zone, +0x32 flip, +0x34 scale x, +0x36 scale y, +0x3a palette (16 bytes), +0x38 palette size.

### Click zones (`g_view_click_zones` 0x7f2ec, 12 bytes, count 0x7f400)

+0 rect (x, y, w, h as int16), +8 thing (0xffff = none), +10 view position, +11 kind (2 creature possession, 4 door button, 6 wall ornament). Slots 0-8 are fixed per cell; extra entries are appended by `view_add_click_zone`.

### Sensor record (thing type 3, as used here)

+2 bits 0-6 sensor type, bits 7-15 data (object type, count, delay); +4 bit 0 active/busy, bit 2 state, bit 5 inverted, bits 3-4 effect (`0x18` = hold), bit 6 audible, bits 7-10 extra delay; +6 target: bits 4-5 cell, bits 6-10 x, bits 11-15 y. `sensor_trigger_target` schedules a type-4 square event at the target.

Sensor types seen: 1-3 party/creature/object floor triggers, 4 object type, 0x15 object with charges, 0x17 toggle, 0x18/0x46/0x4a timed, 0x1a object-holder (alcove that keeps an item), 0x1c teleport party, 0x1e/0x33-0x37 timers (`sensor_schedule_timer`), 0x20/0x45 relay, 0x21 day/night trigger, 0x27 flip state, 0x29-0x2b floor object tests, 0x2c animated ornament, 0x2e teleport party, 0x30 shop display, 0x32 activation with sound, 0x3a creature generator area, 0x3b/0x40/0x47-0x49 object transfer, 0x3d forward, 0x42 turn creatures, 0x43 set global flag, 0x44 test global flag, 0x7e resurrection altar. Wall square events add creature generator (0x30ba6), counter, random, missile launcher, cross-map event, item creator, and an end-of-game sensor (`iop_set_video_state(0)`, `g_game_completed = 1`, `session_quit`).

### Text thing (type 2) as a marker

When `(word2 & 6) == 2`, a text thing is a marker or a simple trigger: bits 3-10 id, bits 11-15 kind. Kind 4 item holder, 5 wall text toggle, 7 hidden ladder, 8 item holder that sets a marker, 9 random pit trigger, 10 trap teleport (event 0x5d), 0x0b/0x0c destination markers for pits and stairs (`dun_find_marker` modes 2-3), 0x0f/0x10 warp markers (`move_party_warp_to_marker`, modes 4-5), 0x13/0x16 creature generator (event 0x5e), 0x15 night generator, 0x17 sound.

### Timeline event types (dispatch in `timeline_process`)

| Type | Handler |
|---|---|
| 1 | `timeline_event_door_animation` |
| 2 | `timeline_event_door_destruction` |
| 4 | square event by square type: 0 wall `timeline_event_wall_square`, 1 floor `timeline_event_floor_square`, 2 pit, 4 door, 5 teleporter, 6 fake wall |
| 0x0c | `timeline_event_hide_damage` |
| 0x0d | `timeline_event_altar_rebirth` (bones item = item category 0x15 index 0, charges = champion) |
| 0x0e | `timeline_event_apply_item_effect` |
| 0x15 | `sound_request_delayed` |
| 0x19 | 0x18395 (explosion) |
| 0x1d, 0x1e | 0x17a7b (missile move) |
| 0x21, 0x22 | 0x257cc (creature) |
| 0x3c, 0x3d | `timeline_event_move_group` (0x3d plays a sound on failure) |
| 0x46 | `timeline_event_magic_light` + `light_update_party_light` |
| 0x47 | invisibility-like counter 0x7ffee |
| 0x48 | champion shield decrement (+0x103) |
| 0x4b | poison (`0x474fc`) |
| 0x54 | `world_update_day_night(1)` (storm step) |
| 0x55 | `timeline_event_sensor_animation_step` |
| 0x56 | `timeline_event_sensor_timer` |
| 0x57, 0x5b | clear sensor busy bit |
| 0x58 | `timeline_event_door_button_release` |
| 0x59 | `timeline_event_sensor_animation_end` |
| 0x5a | `timeline_event_sensor_ambient_sound` |
| 0x5c | set bit 0 of a text marker |
| 0x5d | teleport party (map, x, y, dir packed in the data word) |
| 0x5e | spawn creature (0x30ba6) |

The queue is a binary heap of event indices (`g_timeline_queue` 0x80420); `timeline_event_is_later` orders by time, then type, then priority.

### Movement result (globals 0x80020-0x8002a)

`move_follow_square_transitions` follows teleporters, open pits, and map links and writes the final position to `g_move_dest_x/y/map/cell/dir`. `move_thing` (DM `F0267`) then unlinks the thing, runs floor sensors at the source and destination, handles creatures already on the destination (`move_find_free_adjacent_square`, retry event 0x3c/0x3d), and updates projectile timeline events.

### Message area

3 rows, expiry times in `g_message_expiry` (0x80404). Text wraps by `text_wrap_line`; a new row scrolls `g_message_scroll_bitmap` (0x80410). The message font (0x300 bytes) is GRAPHICS.DAT (1,0,7,0).

## DM2-specific systems found

- Weather: 0x59f12-0x5a808 plus globals 0x8046a-0x80480. Storm intensity follows curves (table 0x76104, 32 steps, timeline event 0x54). Rain intensity 0x8047e and wind direction 0x8047f select rain images in GRAPHICS.DAT category 0x17 (entries 'm'/'q' + 0..3), drawn by `weather_draw_rain`. Lightning hits random squares of the map (`explosion_create(0xffb0)`) or a fixed square from GDAT (8, set, 0x0b, 0x6c). Global variables 0x40/0x41 (through 0x150ae/0x1512e) hold lightning state. Capability flags come from GDAT category 0x17 for the map graphics set (`g_gfx_has_*`, set in 0x3b2e4 area of range 4).
- Day and night: hour = (time + `g_day_time_offset`) / 0x555 mod 24; `g_hour_light` from `g_light_by_hour`; next hour at `g_next_hour_time`. `sensor_process_map_entry` triggers type-0x21 sensors and night creature generators.
- Outdoor view: no separate renderer. Outdoor maps use the same `view_draw_dungeon` path with a sky layer (`outdoor_draw_sky`): sky objects and landmarks are GDAT category 0x17 text entries (0-99) with key/value fields parsed by 0x3f791, placed by map position and distance (`outdoor_relative_distance` uses an integer square root). Map links ("seamless" teleporters, 0x1d113) let `view_draw_floor_ornament` draw squares of the linked map. `move_compute_map_arrival` computes the arrival position after such a link.
- Money and shops: `shop_draw_display` (0x4ec19) shows a sensor of type 0x30: it cycles through an item list from GDAT (9, ornament, 8), skips items already owned by the party (`item_party_possesses_type`) or on the shop squares, draws the item, its name (0x37e4e), and its price in coin images (0x15737, 0x15958 in range 1).
- Skullkeep machines: the sensor/actuator engine (0x4bbe4-0x4cdcc, 0x56b39-0x59785) with relays, timers, counters, global flags, object transfer, missile launchers, creature generators, and the end-game sensor. No separate machine code exists; machines are sensor networks.
- Minions: not found in this range. 0x4db9b and 0x27aad (range 3) may relate; open.

## Low-confidence names

`move_update_carried_missiles`, `move_check_missiles_hit_party` (detail), `aspect_creature_cell`, `dun_map_gfx_flag20_clear`, `view_copy_image_mode1`, `view_draw_wet_image`, `view_draw_sparkle_field`, `view_offset_creature_toward_party`, `view_mark_hidden_squares`, `view_unhide_linked_squares`, `view_draw_stairs_side`/`view_draw_stairs_front` (may be swapped), `view_draw_ceiling_pit`/`view_draw_floor_pit`, `mem_parse_config_entry`, `mem_test_all_maps`, `timeline_event_apply_item_effect`, `timeline_event_door_button_release`, `sensor_schedule_timer`, `sensor_play_activation`, `sensor_process_map_entry`, `move_party_warp_to_marker` (subsystem magic is a guess).

## Open questions

- Event 0x0e (`timeline_event_apply_item_effect`) copies a thing record, sets charges, and calls 0x45a9d. The exact game effect is open.
- 0x4e620 did not decompile; the disassembly shows it renders a picture into a temporary image and returns the image id.
- `g_party_square_x/y/map` (0x7f27c/0x7f27a/0x7f27e) are compared with sensor squares; they may be the party position used for collisions, separate from `g_party_x/y`.
- 0x54f5b/0x55045 are called from 0x3c1c3 (memory manager): a byte script of memory configurations, and a test that loads every map. The meaning of the 5 flags at 0x803fa is open.
- The weather sky state table 0x75be2 (`g_sky_state`) and `g_weather_random_period` need checks against DOSBox behavior.
