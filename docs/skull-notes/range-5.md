# Range 5 notes (0x3ba23-0x49ef8)

208 functions. Names are in `range-5.csv`, new globals in `range-5-globals.csv`.

## Anchor corrections

- `0x3ec2b` `gdat_get_image_index` is wrong. The function returns a pointer to the decoded image (it calls `gdat_decode_image` 0x3e867; callers read `image[-4]` width and `image[-2]` height). New name: `gdat_get_image`, returns `uint8_t *`.
- `champ_add_to_party` (0x49242): the second argument is the new champion index (`g_champion_count`), the third is the facing direction (EBX). Signature: `(uint8_t portrait, int16_t index, uint8_t dir)`.
- `mem_alloc` third argument is a pool flag mask (16 bit), not a pool pointer. Flags: bit 15 = zero fill, low 15 bits: 1 = from a pool block, 2 = temporary (top not moved).
- Notes `+0x102/+0x103` "action state" is wrong: it is the champion aura (type byte, value word). See below.
- Notes `+0x33` "flags": the redraw flags are the word at `+0x32` (bit 0x4000 status box, 0x800 bars, 0x400 name, 0x1000 load). `+0x33` is its high byte.

## GRAPHICS.DAT database (in memory)

- Key table (item 0) is read by `gdat_load_key_table` (0x3ce0a). Column letters `TIDSPFG` at 0x75775. Column offsets and widths go to 0x7f9ae/0x7f9bc. Rows with column F high nibble not equal to the language byte (0x7576c) are skipped (`gdat_key_row_matches_language`). Rows with G = 0xff are preloaded (`gdat_preload_resident_entries`).
- Lookup tables (`gdat_build_lookup_tables`): `g_gdat_category_start[cat]` (0x7f9dc) -> index start; `g_gdat_index_start[]` (0x7f9e0) -> entry start; entries (0x7f9e4) are 4 bytes: `+0 type, +1 entry, +2 uint16 raw index or value` (bit 15 of byte 3 = "load on demand"). Categories < 0x1d, indexes < 0x0f in the counting pass.
- `gdat_category_is_raw`: categories 5, 6, 0x1b are read directly from file each time (not cached).
- `g_gdat_entries` (0x7fa54): when loaded, the pointer points 2 bytes after a uint16 size.
- Entry (0,0,8,0): sorted table of (raw index, base raw index) pairs (`g_gdat_base_images` 0x7fa60). An image with a base is a delta image: `gdat_decode_image` decodes it with 0x13258 using the base image. Otherwise 0x12e4f decodes a 4-bpp image and 0x3f71c dispatches an 8-bpp decoder by byte `+6` (table 0x75798).
- Encoded image header: word 0 bits 0-9 width, word 1 bits 0-9 height. The top 6 bits of word 1: `0x20` (-0x20 as signed) = raw block, value of `g_gdat_8bpp_marker` (0x7fac8, 1000 or 0x1f) = 8 bpp, other = 4 bpp. 4-bpp images keep their 16-byte local palette at the end of the item (`gdat_get_image_palette`).
- Entry (0,0,0x0b,0) = flags (0x7facc). Bit 0x20 sound header format, bit 0x40 calls the function pointer at 0x757a4 and sets the 8-bpp marker to 0x1f.
- Reads go through a 1 KB block buffer (`gdat_read_raw_entry`, `g_gdat_block_buffer` 0x7fa64). `gdat_file_read` splits a read between GRAPHICS.DAT and GRAPHIC2.DAT.
- `gdat_load_entry_sets` (0x3d826, called from 0x3ab31 on map load): marks the key ranges that the map needs and loads them into two arenas (`g_gdat_arena_main` 0x7fa90, `g_gdat_arena_extra` 0x7fa9e; 14 bytes: top, bottom, current, flags). It also refreshes sound buffers (0x1623b) and shows progress (0x1aa09).
- Text fields: GDAT text entries contain `XX=n` pairs. `gdat_get_text_field(cat, idx, entry, field)` reads field names from 0x7590e: `SK LV CM BZ TR ST PA TA NC EX PB DM MS SD RP HN AT WH` (index 0-17). Action definitions use them: SK skill, LV level, CM command, NC charges (8), WH hand (17), and others (meaning of BZ, TR, PA, TA, EX, PB, MS, SD, RP, HN, AT is not yet confirmed).

## Memory manager and image cache

- Pool block header (0x1e bytes, list `g_mem_pool_list` 0x7f9d4): `+0 next, +4 size, +8 top, +0xc free, +0x10 flags, +0x12 in use, +0x16/+0x1a saved top/free`.
- Main heap grows down from `g_mem_heap_top` (0x7fa48); free bytes in `g_mem_free_bytes` (0x7fa7c). Error 0x28 = out of memory.
- `image_cache_init` (0x3c1c3) sizes the cache. Cache block header 0x12 bytes: `+0 size (negative = used), +4 usage count (0xffff = locked, 0xfffe = pinned), +6 prev slot, +8 next slot, +0xa id (bit 15 = temporary key), +0xc bpp, +0xe width, +0x10 height`, pixels at `+0x12`. LRU list head 0x7fa20, tail 0x7f9d0.
- Temporary cache entries have 32-bit keys kept in a sorted array (`image_cache_find_key`, 0x7f9d8 keys, 0x7fa14 order). Other code (creature AI 0x25420, view 0x4e79c) uses `image_cache_lookup` / `image_cache_alloc_image` for scaled or composed pictures.

## Champion record additions (0x107 bytes)

| Offset | Use |
|---|---|
| +0x00 | name (8 bytes) |
| +0x08 | title (0x14 bytes) |
| +0x1c | facing direction |
| +0x1e | number of spell runes entered |
| +0x1f | number of poison events |
| +0x20, +0x21 | last action command per hand |
| +0x22 | spell runes (4 bytes, first = power rune) |
| +0x2a..+0x2c | action delay per hand and for spells |
| +0x2e | timeline event index of the damage display |
| +0x30 | last damage value (shown by `ui_draw_damage_number`) |
| +0x32 | redraw flags (word) |
| +0x34 | wounds |
| +0x36, +0x38 | health, maximum health |
| +0x3a, +0x3c | stamina, maximum stamina |
| +0x3e, +0x40 | mana, maximum mana |
| +0x42, +0x43 | per hand defense bonus of the last action |
| +0x44, +0x46 | food, water |
| +0x48 | poison total |
| +0x4a | 7 statistics, 2 bytes each (current, maximum); stat 0 luck, 1 strength, 2 dexterity, 3 wisdom, 4 vitality, 5 anti-magic, 6 anti-fire (DM order, assumption) |
| +0x58 | statistic bonus byte per statistic (7) |
| +0x5f | uint32 experience per skill, 20 skills (4 base + 16 sub skills) |
| +0xaf | skill bonus byte per skill |
| +0xc3 | inventory, uint16[30] |
| +0xff | load |
| +0x101 | portrait index (GDAT category 0x16 index) |
| +0x102 | aura type (0..2 shields, 3..6 statistic boost) |
| +0x103 | aura value (uint16), removed by timeline event 0x48 |
| +0x105 | move speed modifier |

## Actions and spells

- Action list: `champ_list_actions` reads text entries (category of item, index, type 5, entry 8..0x0b) and keeps up to 3 actions in `g_action_list` (0x7fb3c). Empty hand uses category 0x16 (champion).
- `champ_perform_action` (0x414a5) switches on the CM field: 2 invisibility (event 0x47), 3 projectile spell, 4/8 melee attack or door, 5 frighten, 6/0x26/0x27 light, 7 and 0x31-0x33 summon or explosion, 9 haste (0x7fff0), 0x0c-0x0f and 0x21-0x23 party auras, 0x10 consume, 0x11 swap from source slot, 0x20 shoot, 0x24 heal, 0x2a throw, 0x2c-0x30 minion map commands, 0x36 machine call 0x4bfd4.
- Spells: `magic_find_spell` searches 34 spells of 8 bytes at 0x757fe (rune code, 3 or 4 runes). Byte `+4` difficulty, `+5` skill, word `+6`: bits 0-3 kind (1 potion, 2 projectile, 3 creature or summon, 4 other), bits 4-9 subtype, bits 10-15 duration factor.
- Rune mana cost table 0x757dc (6 per power level), power multiplier 0x75794.

## DM2-specific systems in this range

- Money: `money_box_click_coin` (0x42a35) and `ui_draw_money_box` (0x4534f). Coin piles are drawn with offset table 0x71756. Coin logic itself is in range 2 (0x1f2ab-0x1f50d).
- Minions: the minion map (container subtype, `item_is_minion_map` 0x1fa8f in range 2) shows a map panel: `mmap_draw` (0x44fc6), `ui_map_draw_grid` (0x449be), `ui_map_draw_square` (0x43a7c). It drains mana while open (`g_map_mana_timer`). Option buttons (`minion_toggle_map_option`) cost mana from 0x757fa. Actions 0x2c-0x30 summon, guide, or dismiss minions (0x30944, 0x30b3d in range 4).
- Weather, outdoor view, shops, Skullkeep machines: not in this range. Action 0x36 calls 0x4bfd4 (range 6, sensor block), probably a machine.

## Open questions

- `input_check_memory_cheat` (0x3f62d): typing `mem` prints memory and the debug flags G/S/E (0x803fa-0x803fc). Check whether the port needs it.
- Exact meaning of `g_party_action_effect` (0x7ffef, action 0x0b) and of several text fields.
- `champ_find_hand_source_slot` (0x3fc6d) and `item_get_throw_fatigue` (0x4663a) are low-confidence names.
- `gdat_index_last_type` (0x3cf67), `gdat_get_image_offset` (0x3eed3) and `image_decode_8bpp` (0x3f71c) need confirmation in Step 8.
- Other ranges name 0x7f284, 0x7f390, 0x7f958, 0x7faca, 0x7fad0, 0x7fad2, 0x7fb70, 0x7fb72, 0x7ffec; they are left out of `range-5-globals.csv`. 0x7fad2 is the count of type-2 (sound) entries and 0x7fad0 the largest song size (from `gdat_count_sound_entries`, `gdat_find_max_song_size`).
