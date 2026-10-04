# Range 2 notes (0x18e7f-0x1fef6)

166 functions, all named. 29 names are low confidence (role known, game meaning not): the `*_attr_*` getters (10), `creature_get_info_*` and `creature_get_gdat_attr4` (4), `gfx_offscreen_*_alt` (2), the teleporter link functions (3), `dun_find_adjacent_level_link`, `view_grid5_*` and `zone_id_for_view_*` (4), `system_debug_hook`, the minion functions (3), `dun_is_wall_thing_ornament_shown`.

## Data structures

### Layout zones (GRAPHICS.DAT, loaded by `zone_init`)

- Source: entry (1, 0, type 4, 0). The data starts with the magic word 0xfc0d (-0x3f3), then a count of zone ranges, then (first, last) zone id pairs, then 4 words per zone: anchor type, parent zone, x, y.
- `zone_unpack_layout` packs each range into a linked record: +0 next, +4 first id, +6 last id, +8 flags (bit 0 parent shared, bit 1 type shared, bit 2 type and parent as bytes, bit 3 x/y as int8, bit 4 x/y as uint8), +9 shared type, +0x0a shared parent, then the per-zone data. List head: `g_zone_list` (0x71796).
- Zone record after lookup (`zone_find_record`, 4 rotating buffers at 0x7f1da): +0 anchor type, +2 parent, +4 x, +6 y.
- Anchor types 0-8 position a rectangle on a 3 x 3 grid (0 center, 1 top-left, ..., 8). Type 9 is a size record (w = +4, h = +6). Types 10-0x12 place the zone relative to the parent rectangle.
- `zone_get_rect_clipped(zone, rect, w_scale, h_scale)`: the scales are in 1/10000 of the size record (10000 = full size). `zone_get_rect` passes 10000, 10000.

### Rectangle

int16[4]: x, y, w, h. `rect_temp` returns one of 4 rotating buffers at 0x7f1a8.

### Image reference (`image_ref_t`, 0x18 bytes)

+0 pixel pointer, +4 source flags (4 = GRAPHICS.DAT, 8 = image cache), +8 category, +9 index, +0x0b entry, +0x0c cache id, +0x0e x, +0x10 y, +0x12 width, +0x14 height, +0x16 bpp.

### Draw context (`draw_ctx_t`, 0x13a bytes, `gfx_ctx_init`)

Starts with an `image_ref_t`. +6 GRAPHICS.DAT entry index of the image, +0x12 w, +0x14 h, +0x18 zone (0xffff = none), +0x1a anchor override, +0x1c/+0x1e offset x/y (from type 0x0c entry 0xfe and `0x3eed3`), +0x20/+0x22 position, +0x24 rect (8 bytes), +0x2c destination bitmap (default `g_screen_bitmap`), +0x30 transparent color, +0x32 flags (bit 0 mirror x, bit 1 mirror y, bit 4 extra), +0x34/+0x36 scale (64 = 100%), +0x38 palette remap count (16 or -1), +0x3a remap table (16 bytes). `gfx_ctx_cache_key` packs scale and entry index into a cache key for scaled images (`0x3f186`).

### Off-screen buffer (`offscreen_t`)

+0 image cache id, +2 rect (x, y, w, h on screen), +0x0a dirty rect count (maximum 5, then flush), +0x0c dirty rects (8 bytes each). `gfx_offscreen_flush` blits the dirty rects to `g_screen_bitmap`.

### Square aspect (`dun_get_square_aspect`, uint16[8])

+0 element type (square type, 0x10/0x11 door front/side, 0x12/0x13 stairs), +2 raw square byte, +4 first object thing, +6..+0x0c wall ornament per side (front, right, back, left; 0xff none; bits 10+ animation frame), +0x0e ornament thing or 0xffff. Floor: +0x0a floor ornament. Random wall ornaments use the DM hash (x * 31417 / 2 + y * 11 + dungeon seed) / 4 mod 30.

### Thing records (confirmed fields)

- Text (type 2): word +2 bit 0 visible, bits 1-2 kind (0 text, 1 ornament action), bits 3-10 ornament or text index, bits 11-15 subtype.
- Sensor (type 3): +2 bits 0-6 sensor type, bits 7-15 data (target map for type 0x1c and 0x27); +4 bits 12-15 ornament ordinal (1-based) into the map ornament list.
- Teleporter (type 5 square): +2 bits 0-4 x, bits 5-9 y; +4 high byte target map, +4 bit 0 "closed".
- Group (type 4): +4 creature type, +0x0e bit 10 do not discard.
- Weapon +2: bits 0-6 type, bit 7 do not discard, bits 10-13 charges. Clothing: bits 9-12 charges. Potion: bits 0-7 power, bits 8-14 type, bit 15 do not discard. Misc (type 10): bits 0-6 type, bits 8-13 stack count - 1, bits 14-15 charges. Container +4: bits 1-2 kind (0 chest or money box, 1 minion map), bits 13-15 type; minion map +6 location (x bits 0-4, y bits 5-9, map bits 10-15).
- Item index (9 bits, `item_get_index`): base per thing type at 0x72274: weapon 0, clothing 0x80, misc 0x100, potion 0x180, group 0x1b0, container 0x1e0, scroll 0x1fc. 0x1ff = none.
- GRAPHICS.DAT category per thing type (0x72294): door 0x0e, teleporter 0x18, group 0x0f, weapon 0x10, clothing 0x11, scroll 0x12, potion 0x13, container 0x14, misc 0x15, explosion 0x0d. Wall ornaments 0x09, floor ornaments 0x0a, wall set 0x08, dialogs 0x1a.
- Item attributes are type 0x0b words: attr 1 base weight (+ attr 0x34 per charge), attr 2 base value (+ attr 0x35 per charge; potions scale with power).

### Map data

- After the squares of a map (`g_cur_map_extra_data`, 0x7f3c0): creature types (count = desc+0x0c bits 4-7), wall ornaments (desc+0x0a bits 0-3), floor ornaments (desc+0x0a bits 8-11), door ornaments (desc+0x0c bits 0-3). `dun_set_party_location` copies them to 0x7f2c8, 0x7f2d8, 0x7f410.
- Map desc +8 bits 0-5: level (depth). `dun_get_location_on_map` uses 0x7f3ec (offset per level) into 0x7f3d8 (lists of maps on that level, -1 terminated) to find the map that holds a square on another level.
- Map desc +2 bit 15 / +3 bit 0 enable door set 0 / 1; +0x0e bits 8-11 / 12-15 are door types (`dun_get_door_type`).

## Anchor corrections

- `zone_get_rect_clipped`: parameters 3 and 4 are size scales in 1/10000, not maximum sizes.
- `dun_get_location_on_map`: parameter 2 is a level delta; parameter 5 is a cursor into the map list (in and out). Returns the map index or -1.
- `dun_get_square`: outside the map it returns 0xe0 (type 7) or a wall/floor value derived from the border squares.
- Hub 0x1be6a is not specific to items or creatures: it draws any GRAPHICS.DAT image into a zone of an off-screen buffer.
- Hub 0x1d113 is a two-way teleporter link (both ends need a sensor type 0x27), not stairs.

## DM2-specific systems in this range

- Money: `money_is_moneybox` (container whose image (0x14, n, 5, 0x40) exists), `money_is_coin` (misc item with attr 0 bit 0x4000, stackable up to 64), `money_count_coins_in_box`, `money_add_coin_to_box`, `money_take_coin_from_box`. Coin item indices at 0x7f390, count 0x7f3fa. Callers 0x29598 and 0x296d5 are in the creature AI (merchants, probably shops), 0x42a35 and 0x4534f in the interface.
- Minions: `item_is_minion_map` (container kind 1), `minion_map_set_location` (called by champion action 0x2c/0x2f in 0x414a5), `minion_find_link_projectile` (a type 14 record in a group's possessions links the minion to its map item). Assumption based on SKWin names (`GET_MISSILE_REF_OF_MINION`); confirm in range 3/5.
- Alternative view position (0x7f230, 0x7f25c, 0x7f25e, 0x7f272, 0x7f278): `dun_set_current_map` uses it instead of the party position, and `dun_get_discarded_thing` protects that map. Probably the minion eye or remote view. Open.
- Not in this range: weather, outdoor view, shops UI, Skullkeep machines.

## Open questions

- Item attr 3 (`item_get_attr_3`), creature info +0x1d/+0x1e/+0x20, creature GRAPHICS.DAT attr 4, door attrs 0x0d-0x11, wall ornament attrs 0x0a-0x0c/0x13: meanings unknown.
- Sensor types 0x1c (level link used by creature AI), 0x27 (teleporter link), 0x2c, 0x30, 0x32, 0x3f, 0x41, 0x46, 0x4a, 0x7e have wall-view effects in `dun_apply_wall_things_to_aspect`.
- `gfx_offscreen_fill_rect_alt` calls 0x13745 (range 1); its difference from `gfx_fill_rect` is open.
- 0x1b2be did not decompile; the signature comes from the disassembly (4 stack args, `ret 0x10`).
- `ui_dialog_screen_mode` / `g_dialog_screen_mode` (0x717bc): the flag switches between direct screen drawing and the viewport path. Its source is open.
