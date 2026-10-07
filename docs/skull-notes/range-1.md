# Range 1 notes (0x10000-0x18e7f)

173 functions. 16 names have low confidence (see "Open questions").

## Anchor corrections

- `0x139e4`: the anchor name `screen_present_rect` is wrong. The function takes no rectangle. It gets zone 0x0f (message area), calls IOP 0x105 with the zone rectangle and the line height (`0x71742`), then blits the new message line bitmap `g_message_line_bitmap` (0x80410) into `g_screen_bitmap` (0x71550). New name: `text_scroll_message_area`. Callers: 0x55844 and its neighbours (message area code).
- `0x13a87` is the real "present rectangle" (IOP 0x103, 8-byte rectangle). It now carries the name `screen_present_rect`. `0x13b7c` (`screen_present_zone`) calls it.
- `sound_request` (0x15ca9): the anchor signature swaps volume and priority. Correct order: `(category, index, entry, priority, volume, x, y, delay)`. Byte +4 of the request gets argument 4 (priority). Byte +5 gets argument 5 (volume, halved while the party sleeps). Example: `sound_set_volume_and_test` calls `(3, 0, 0x8b, 0x6c, 200, party_x, party_y, 0)`.
- `explosion_create` (0x16746): the anchor signature order is wrong. Correct order: `(type, attack, x, y, cell)`. `cell == 0xff` = centered explosion (bit 7 of byte +2). If `x > 0xff`, the high byte holds `x + 1` and the low bytes of x and y are cells or offsets (packed form).
- `game_present_viewport` (0x138d9) takes no argument. It reads zone 7, checks if the mouse pointer (IOP 0x0e) is inside, hides it, calls IOP 0x109 with flags 8 or 0x8008.
- IOP table in `notes.md`: function 0x1b is not a disk call. It converts a key code to an ASCII character (`kbd_key_to_ascii`, 0x114c5; used by name entry). Function 0x15 (`disk_query_drive_info`, 0x111db) is missing from the table. Function 0x28 (`disk_get_drive_path`, 0x113fa) returns 3 bytes for a drive (written to 0x70528 and 0x7052c).
- Ghidra boundaries: 0x113ad is the shared tail of 0x113a5 (IOP 0x12) and `kbd_key_available`. 0x113dd-0x113f6 is a hidden function (save disk status == 3); Ghidra shows only its tail 0x113f7 (`return 0`). 0x114d2 is a hidden helper that prints one character through `iop_print_message`. 0x14c93 (mouse set position, IOP 0x0d) is inside 0x14c87. 0x149a2 is a separate entry in front of 0x149ba.

## Image decoders (confirmed)

Both decoders use the nibble reader `image_read_nibble` (0x12d7a, position `g_decode_nibble_pos` 0x7f090, source `g_blit_src` 0x7f078) and `image_read_run_length` (0x12ddb). Output is 4 bpp.

`image_decode_4bpp(src, dst)` (0x12e4f):

- Header: word 0 & 0x3ff = width, word 1 & 0x3ff = height. Rows are padded to an even width. Nibble data starts at nibble 8 (byte 4).
- 6 nibbles: local palette (6 colors).
- Then one code nibble per run: bits 0-2 = command, bit 3 = run flag.
  - 0-5: color from the local palette.
  - 7: color = next nibble (literal).
  - 6: copy pixels from the row above (`dst_x - stride`).
  - Run flag clear = 1 pixel. Run flag set = length from `image_read_run_length`.
- Run length: nibble n; n != 15 gives n + 2. Else two nibbles b; b != 0xff gives b + 17. Else four nibbles give a 16-bit length.
- Odd widths: runs wrap at the row end and skip the padding nibble.

`image_decode_4bpp_delta(base, src, dst)` (0x13258): same format, but the local palette has 5 colors, and command 5 copies pixels from the same position of `base` (`g_decode_base` 0x7f080). Used for images that change only parts of a base image.

## Blitter

- `gfx_blit` (0x12ad2) arguments: `(src, dst, rect, src_x, src_y, src_width, dst_width, transparent, mirror, src_bpp, dst_bpp, colormap)`. Rect = `{x, y, width, height}` int16. `transparent < 0` = opaque. `mirror`: 0 none, 1 horizontal, 2 vertical, 3 both. Dispatch: 4->4 (`gfx_blit_4bpp_to_4bpp`), 4->8 with colormap (`gfx_blit_4bpp_to_8bpp`), 8->8 (`gfx_blit_8bpp_to_8bpp`), 8->8 with colormap (`gfx_blit_8bpp_mapped`).
- 4-bpp rows are `(width + 1) & ~1` pixels. 4->4 transparent copies use the mask table `g_nibble_transparent_masks` (0x70550, 16 x 256 bytes).
- Scaling: `gfx_blit_scaled_4bpp` / `gfx_blit_scaled_8bpp` (7.7 fixed point, row reuse when the source row repeats). Callers near 0x1b8xx (picture drawing).
- Tiled fill: `gfx_blit_tiled_4to8_masked` repeats a 4-bpp pattern with wrap, optional 1-bit mask; used by the dungeon view (0x4f..0x51).
- `ui_fill_rect_dithered` draws a checkerboard of one color (disabled UI look).
- `gfx_alloc_dos_bitmap`: image buffer header is 6 bytes (bpp at -6, width at -4, height at -2), as in `notes.md`.

## Text glyphs

`text_build_glyph(ch, fg, bg)` builds a 6 x 6 pixel 4-bpp glyph (18 bytes at 0x7efb4) from the font at `g_font_bitmap` (0x80414): 6 rows of 0x80 bytes, one byte per character per row. Bit 4 of a font byte selects fg or bg for the high nibble, bit 3 for the low nibble (2 pixels per step, 3 steps per row). `text_draw` blits each glyph with `gfx_blit` (src width 5, src bpp 4).

## Mouse areas

- Mouse area record (10 bytes, table at 0x722d4, 0x12 entries): +0 zone (bits 0-13; bit 15 = relative to offset 0x716f8), +2 cursor kind (byte), +3 flags (bit 7 linked, bit 6 node allocated, bits 0-3 priority), +4 command on enter (byte), +5 command on leave (byte), +6 pointer to node `{next pointer, rect[4] int16}` (allocated with `mem_alloc(0xc, 1, 0x200)`).
- `g_mouse_areas` (0x7f098) is the list head, sorted by priority. `mouse_move_event` finds the area under the pointer, sends enter and leave commands through 0x20c5d, and sets the hover rectangle with IOP 0x0c. Its return value is the cursor kind (0 arrow, 1/2 hand without or with item, 3 special).
- Cursor kinds for `mouse_set_cursor_image`: 0 arrow (12 x 16), 1 hand (16 x 16), 2 leader hand item (18 x 18, 8 bpp), 3 dragged champion (portrait size from 0x71726).

## Creature animation (category 0x0f)

- GRAPHICS.DAT `(0x0f, creature_type, 7, 0xfc)`: 4-byte animation steps. Byte 1 bits 4-7 = "more frames follow" or branch count, bits 0-3 = random threshold (15 = always). Byte 2 bits 0-5 = relative jump to the next step. Byte 3 bits 2-7 = sound or event fields.
- `(0x0f, creature_type, 7, 0xfd)`: 8-byte frame records, same index.
- `(0x0f, creature_type, 8, 0xfb)`: list of `(command, first_step)` word pairs, terminated by -1.
- Animation state word: bit 15 = looping by game time, bit 12 = reset phase, bits 6-11 = phase, bits 0-5 = frame count or current frame.

## DM2-specific systems found

- **Money and shops** (0x1538d-0x15958):
  - `money_init_coin_table` scans miscellaneous items `(0x15, i, 0x0b, 0)` for flag 0x4000 (coin). It sorts coin values into `g_coin_values` (0x7f3a8) and coin item types into `g_coin_item_types` (0x7f390); count in `g_coin_type_count` (0x7f3fa).
  - `money_split_amount` splits an amount into the largest coins first (greedy).
  - `shop_parse_item_class_set` reads a creature text `(0x0f, creature_type, entry + 0x10)` with item ranges, for example `3-7W`, into a 512-bit set. Class letters: `W` weapons (+0), `A` clothing (+0x80), `J` miscellaneous (+0x100), `P` potions (+0x180), `C` containers (+0x1e0), `S` scrolls (+0x1fc).
  - `shop_get_item_value(thing, creature_type, list, buying, apply_margin, charges)`: base value from item attribute `(cat, idx, 0x0b, 2)`, percentage factors from creature attributes (`0x1f8ff`, entries `list*3 + 0x11/0x12`) when the item is in the creature's class set, plus attribute 0x35 per charge, then a buy or sell margin (creature attribute `list*3 + 0x10`, default 10 %). Callers: the merchant panel drawing (0x4e..., prices for buy and sell) and creature AI (0x20645, value of items).
- **Global variables** (`dun_get_global_var`, `dun_modify_global_var`): vars 0x00-0x3f are bits (`g_global_flags` 0x7f100), 0x40-0x7f bytes (`g_global_bytes` 0x7f0c0), 0x80-0xbf words (`g_global_words` 0x7f108). Operations: 0 set to 1, 1 clear, 2 toggle (set if 0), 3 add, 4 subtract, 6 assign. Callers: champion actions (0x40079, var 0x42 += 1), creature AI (0x18441), timeline (0x55607, actuator data `>> 7`). These are probably the DM2 script variables used by actuators and Skullkeep machines. Subsystem is set to dungeon data until range 6 confirms.
- **Inventory slot rules**: `inv_item_fits_slot` uses item attribute 4 (slot mask) and `g_slot_item_masks` (0x7166c, 13 slots). Slots 0x1e-0x25 check the container in the leader's hand.
- Weather, minions, outdoor view, Skullkeep machines: not in this range.

## Projectiles and explosions

- Projectile record (thing type 14): +2 payload thing, +4 kinetic energy (byte), +5 attack (byte), +6 timeline event index. Event type 0x1d (0x1e for the next steps; 0x1e directly when `g_projectile_slow_event` 0x7f1a4 is set). Event data: +6 projectile thing, +8 `x | y << 5 | dir << 10 | step << 12`.
- Explosion record (type 15): +2 bits 0-6 explosion type (0xff80 + n), bit 7 centered, bits 8-15 attack. Event type 0x19. Explosion types seen: 0xff80 fireball, 0xff82/0xffb0/0xffb1 damage area types, 0xff83 non-material, 0xff84 and 0xff8d door or square effects, 0xff86 poison, 0xff87 poison cloud, 0xff89-0xff8e push or special effects, 0xffe4 smoke (duration 5), 0xff8e lasting cloud (duration attack/2).
- `projectile_hit(impact, x, y, cell, projectile)`: impact -1 creature, -2 champion, -3 party square, 0 wall, 4 door. Returns 1 when the projectile stops.
- Sensor codes used: 0x22 and 0x23 (wall sensors hit by projectiles; 0x23 is a projectile teleport target), 0x26 (square sensor triggered by an explosion type, `explosion_trigger_sensors`), 0x0e (reflector on a square, turns the projectile around).
- `dun_door_try_destroy(x, y, attack, magic, delay)`: door byte +3 bit 0 = can be broken physically, byte +2 bit 7 = can be broken by magic. Door strength from 0x1fe1c/0x1fe82. Closed door (square state 4) becomes broken (5), or timeline event 2 is queued after `delay`.

## Sound

- Sound table entry (7 bytes, `g_sound_table` 0x7f18c): +0 sample descriptor index, +2 category, +3 index, +4 entry, +5 raw GDAT entry (0xffff = not loaded). `sound_register` adds entries; `sound_load_registered` loads them; `sound_unload_all` frees them.
- Sample descriptor (16 bytes, `g_sample_descs` 0x7f194): +0 data pointer, +4 length, +6 rate, +8 next prepared sample.
- Delayed sound (14 bytes, `g_delayed_sounds` 0x7f188, 8 entries): +0 used, +4 category, +5 index, +6 entry, +7 map, +8 x, +9 y, +0x0a priority, +0x0c volume. Timeline event type 0x15 plays it (`sound_request_delayed`).
- `sound_request_with_fallback` (0x15baa, Ghidra failed): uses `index` if `(category, index, entry)` is registered, else `fallback_index` (usually 0xfe = default sound set). Stack arguments: x, y, delay, priority, volume.

## Champion position drag (0x149a2, 0x149ba)

Clicking a champion cell in the party formation picks the champion (`champ_get_index_in_cell`), draws its icon (0x4807f) as cursor kind 3, and stores the cell + 1 in `g_champ_drag_state` (0x7f0b4). The second click swaps the cell bytes (+0x1d) of the two champions and, without bit 15 in the argument, sets the facing byte (+0x1c) to the party direction. `champ_drag_cancel` drops the drag at the stored cell.

## Open questions

- Low-confidence names: `disk_query_drive_info` (IOP 0x15), `disk_get_drive_path` (IOP 0x28), `disk_prompt_stub` (empty, called before save disk prompts), `input_stub_return_zero` (always 0, called with a key code in the dialog loop 0x1a1e0), `mouse_capture_begin` / `mouse_capture_end` (IOP 0x06 / 0x07, used while the right button is held), `iop_input_lock` / `iop_input_unlock` (IOP 0x04 / 0x05, around mouse area list changes), `dun_get_global_var` / `dun_modify_global_var` (subsystem), `item_get_image_kind` (checks GDAT types 8, 9, 0x0a, 0x0c of an item), `creature_get_cell_mask`, `money_get_coin_type_for_slot`, `champ_melee_attack_creature` (argument roles after the third), `ui_fill_rect_dithered`, `system_init_stub`.
- Merchant price formula: the role of `list` (0 or the shop list index from `(item >> 7) & 0x0f`) needs range 6 (merchant panel code at 0x4e...).
- `g_samples_have_rate` (0x71698) comes from GDAT flag 0x20 (see `docs/skull-formats.md`); its writer is outside this range.
- 0x7f27a/0x7f27c/0x7f27e are compared as a map and position in projectile and explosion code (probably the party square for damage checks). Not named here.
