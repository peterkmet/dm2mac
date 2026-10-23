# Range 4 notes (0x2b96a-0x3ba23)

145 functions. 21 names have low confidence (see "Low-confidence names").

## Creature AI working state

The AI loads the current group into globals before it runs a command handler.

| Global | Content |
|---|---|
| `g_ai_group_thing` 0x7f548 | group thing |
| `g_ai_group` 0x7f54a | group record (thing type 4) |
| `g_ai_creature_info` 0x7f54e | creature info record (0x24 bytes) |
| `g_ai_active_group` 0x7f552 | active group record (0x22 bytes) |
| `g_ai_group_x`, `_y`, `_map` 0x7f564, 0x7f565, 0x7f56d | group position (bytes) |
| `g_ai_command_result` 0x7f56b | 0xfc = command issued, 0xfd = failed, 0xfe = no move needed |
| 0x7f572, 0x7f574, 0x7f576, 0x7f578, 0x7f57a | permission masks for move, attack, door, item, wall actuator commands |
| `g_ai_class` 0x7f580 | index into `g_creature_class_flags` (0x7507a, 4 bytes per class) |
| 0x7f8a4-0x7f8c0 | cache of one square (`ai_cache_square`): x, y, map, square byte, first thing, first object (type >= 4), first group, cost |
| 0x7f8d0-0x7f8d6 | AI target: y, map, x, dir. `ai_set_map` sets it to the party position, or to 0x7f25c/0x7f25e/0x7f272 when the map is 0x7f278 |

Command builders (`ai_try_attack`, `_try_door_action`, `_try_take_item`, `_try_use_wall_actuator`, `_try_move`) take `mode` in bits 0-6 and "issue command" in bit 7. They write the active group: +0x18 destination (x bits 0-4, y bits 5-9, map bits 10-15), +0x1a command code, +0x1b direction, +0x1c cell or side, +0x1d facing, +0x1e sub-command (attack kind 0x80-0x8a), +0x20 mode.

### Active group (0x22 bytes, `g_active_groups`)

+0 group thing index (0xffff = free), +2 timeline event, +4 and +6 time bytes, +7 animation bits, +0xc position word, +0x16/+0x17 0xff, +0x1a (byte 0x17 >> 24 in Ghidra) state, +0x1f phase. `creature_activate_group` allocates one; `g_active_group_count` 0x7301c, `g_active_group_max` 0x7f89c (creature count + 100, capped by header +0x12).

### Group record (thing type 4) additions

+6 health, +8 word (0xffff selects event 0x21 instead of 0x22 and command 0x11), +0xa flags (bit 0x80 in byte +0xb = active), +0xc location word (x, y, map), +0xe bits 8-9 direction, bit 10 copied on activation.

### Item filters

`ai_thing_matches_filter(thing, filter)` tests an item class bit in a 0x40-byte bitmap per (creature type, filter). Bitmaps come from GRAPHICS.DAT through 0x1538d and are cached in `g_item_filter_bitmaps` 0x7f7ea. Filter bit 0x80 inverts the result. Filters 0x10-0x12 and 0x28 are offset by group +8 * 3.

### Path search

`ai_search_area` (0x3188a, 9583 bytes) is a breadth-first search over a 32x32 byte map with goal records (byte 7 = goal kind, for example 0x17, 0x19, 0x1b). It also computes light (`light_add_square_light`). `creature_refresh_party_maps` runs it on a map change for the party distance map (`g_party_distance_map` 0x7f8c8, 32 bytes per column) and for light. `creature_get_party_distance` reads the map; `sound_request` uses it for sound propagation.

Paths are cached in the image cache with keys `0x30000000 | group` (path steps) and `0x20000000 | group` (`ai_free_cached_data`).

## Save format (bit stream)

- `save_write_packed(src, mask, size, count)` writes only the bits set in `mask`. `save_write_bit` writes one bit (mask 0x75517). `save_flush_bits` pads the last byte. The bit counter `g_save_bit_total` (0x7f8dc) counts written bits.
- Thing list: per thing, 1 bit "more", 4 bits type; then 2 bits cell when the list is a square list; then the record through mask `g_save_thing_masks[type]` (0x754d7). Actuators with types 0x1b, 0x1d, 0x27, 0x2c, 0x2d, 0x30, 0x32, 0x41 save 9 bits of data separately. Groups save 7 bits creature type and recurse into possessions. Containers save 2 bits and recurse. Projectiles (type 14) recurse into their payload; explosions (type 15) save the timeline event index (event type 0x19).
- Square bytes are saved with a per-type mask: pit 8 (0x08), door 7, fake wall 4, teleporter 8 unless it leads to a lower map.
- Order: timeline things (events 0x3c, 0x3d), then all squares of all maps, then deferred containers and creatures (`save_write_deferred_things`).
- Save slot table: 10 entries of 0x2a bytes at `g_save_slots`; +2 name, +0x26 = 0xdeadbeef for an unused slot.

## Dungeon loader post-processing

`dungeon_link_things_after_load` walks all squares: type 1 (teleporter) pairs get their actuator ids numbered (`dun_number_linked_actuators`, actuator type 0x27) and object lists merged; type 2 texts with subtype 6-7 mark the next square (`dun_mark_square_bit1`), subtype 8 sets bit 0. `dungeon_read_level_table` decodes the text on the party start square into a digit per level (`g_level_digit_table` 0x7f3d0, index = map descriptor +8 bits 0-5).

## Champion offsets confirmed in this range

+0x1d cell, +0x1f poison count, +0x33 flags, +0x34 wounds, +0x36 health, +0x3a/+0x3c stamina and maximum, +0x3e mana, +0x44 food, +0x46 water, +0x48 poison amount, +0xc3 slots (+0xc5 second hand), +0x101 champion id, +0x103 (event 0x48 adds to it). Potion types in `champ_consume_object` (bits 8-14 of the potion word): 6-9 statistics, 10 cure poison, 11 stamina, 12 shield, 13 mana, 14 health and wounds, 15 water.

## DM2-specific systems found

- Minions: champion actions 0x2d-0x33 in 0x414a5 call `creature_summon_group` with creature types 0x30-0x35. The minion carries a type 14 record whose +2 holds the controlling item thing (`link_thing`). `minion_dismiss` (0x30b3d) kills the group linked to an item (action 0x30). Timeline handlers 0x58304, 0x59785, 0x59a5c also use `creature_summon_group` (generators).
- Money: `money_draw_coin_counts` (0x39409) shows coin counts of a coin container (10 kinds, values in `g_coin_values`, word table at 0x7f3a8 indexed by kind; the code reads the dword at 0x7f3a6 + 2*kind and takes the high word).
- Creature-operated wall actuators: actuator type 0x26 with data = creature type, used by `ai_try_use_wall_actuator` (commands 0x2f-0x31). Probably part of Skullkeep machines; confirm in range 6.
- Not in this range: weather, outdoor view, shops.

## Anchor corrections

None. All anchor names kept. Note: `session_save` and `session_load` call the slot dialogs `save_choose_save_slot` and `save_choose_load_slot`.

## Low-confidence names

`creature_thing_matches_filter9`, `creature_group_in_state5`, `creature_group_check_pass_flag`, `creature_group_current_frame_flag`, `creature_group_check_field8`, `creature_roll_armor_factor`, `ai_find_marker`, `ai_find_marker_by_id`, `ai_find_target_in_lines`, `ai_projectile_incoming`, `ai_wall_actuator_accepts`, `ai_find_wall_actuator_in_cell`, `creature_is_near_party_front`, `dun_mark_square_bit1`, `dun_merge_object_lists`, `dungeon_read_level_table`, `inv_draw_mouth_picture`, `inv_leader_hand_removed_stub`, `input_hold_right_button`, `zone_compute_icon_rect`, `zone_get_icon_rect`.

## Open questions

- Signature of `ai_search_area`: Ghidra mixes up the stack arguments. The caller 0x2f8f5 pushes 1, a goal record, and a result buffer. Check with the disassembly before porting.
- 0x7f278 with 0x7f25c/0x7f25e/0x7f272 is a second position that the AI treats as a target (alternate distance map `g_alt_distance_map` 0x7f8c4). Possibly a minion or remote view position.
- Square type 7: treated as an obstacle, never saved with a mask. Its DM2 meaning is open.
- Text things (type 2) with `(data & 6) == 4` are AI markers (`ai_find_marker`), level delta in bits 12-15, id in bits 3-7.
- `g_creature_class_flags` bits: 0x02 avoids explosions of type 0x0e, 0x04 ignores incoming projectiles, 0x10 turns before item actions, 0x40 stays near party, 0x800 and 0x8000 death behaviour. Names need range 3 context.
- `gdat_load_map_graphics` (0x3ab31) builds a preload key list (6 bytes per key) and calls 0x3d826. 0x803fa switches to a reduced list. Its loader is in range 5.
