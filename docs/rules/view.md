# Dungeon view

The rules of the dungeon view: the blitters, the layout zones, the view squares, and the things in the view. The code is in `src/data/gfx.c`, `src/ui/zone.c`, and `src/ui/view.c`. The addresses and the notation follow the conventions of [skull-map.md](../reference/skull-map.md).

## Blitters (`gfx_blit` and its row functions)

- `gfx_blit` selects a row function by the source depth: a 4 bpp source goes to a 4 bpp or 8 bpp destination, an 8 bpp source always goes to an 8 bpp destination (mapped through the colormap when it is not `NULL`). Offsets are pixels; a 4 bpp row holds an even pixel count.
- Mirror 1 reads each row of the source window backwards, mirror 2 reads the rows from the bottom up, mirror 3 does both. A value above 3 works as 0.
- The transparent value is compared with the source pixel before the colormap.
- The mirrored 4 bpp to 4 bpp rows with a transparent color have quirks at the row ends: for example, the mirrored row writes its last pixel only when it is the transparent color. The port draws these rows like the other blits. The view does not use this case, and the text drawing uses `MIRROR_NONE` ([interface.md](interface.md#font-and-text-drawing)).
- The 8 bpp scaler steps in 1/256 pixel from half a step; the 4 bpp scaler steps in 1/128 pixel from half a step. Both copy the previous destination row when the source row repeats.
- The masked tiled blit (`gfx_blit_tiled_4to8_masked`) skips the masked pixels at the start and at the end of each row; the tile advances for the skipped start pixels, not for the skipped end pixels.

## Layout zones (`zone_compute_rect`)

- A zone record is `{anchor type, parent, x, y}`. The parent chain ends at a zone with parent 0. A size zone (type 9) gives a width and height; the zones under it are placed in it and clipped to it.
- The image size comes from `*w`, `*h`, or the image header. A zone with bit 15 (`ZONE_RELATIVE`) adds `*w` and `*h` to its position. On return, `*w` and `*h` hold the pixels clipped at the left and the top: the source offset of the blit.
- `g_zone_clip_rect` (`0x7f1c8`) clips the next zones while its width is not 0. `zone_set_clip_margins` sets it to the viewport minus margins; the view sets its width to 0 after each picture.
- While the party walks (`0x7f1d4` and `g_party_move_steps`), zones of parent 3 clip to the walking view `{21, 8, 182, 110}`.

## View square record and the view

- View square (18 bytes, 23 records): `+0`/`+1` map x, y; `+2` aspect (0 wall, 1 floor, 2 open pit, 5 visible teleporter, 7 hidden, `0x10` door seen from the side, `0x11` door front, `0x12` stairs front, `0x13` stairs side); `+4` square byte; `+6` first thing that is not a wall thing; `+8` to `+0x0e` by aspect: the wall ornaments of the faces (`frame << 10 | ornament`, front face at `+0x0c`), or door state and door thing, or the up bit of stairs and pits, or the floor ornament at `+0x0c` (`frame * 10 << 8 | ornament`); `+0x10` wall text thing or portrait index.
- Squares away from the map (`dun_get_square`): a wall with the bit of its face toward the map (`8 >> direction`, the random ornament bits of a wall) next to an open edge square, otherwise `0xe0`.
- Random wall ornaments: hash `((((x * 0x20 + 2000 + (((dir + side) & 3) + 1) * (y + 1)) * 0x7ab9) >> 1) + (map * 0x40 + 3000 + width + height) * 11 + dungeon header word 0) >> 2) % 30`; a value below the random ornament count picks an ornament of the party map's wall ornament list.
- Pictures go through `setup_picture`: GDAT image, scale (1/64), offset, layout zone, and a color remap. The remap is the depth shading table `(8, graphics set, 7, depth)` when it exists, otherwise a dimming by the depth light; the view shade (light level * 10) dims both.
- Doors with an ornament or damage are put together in an offscreen bitmap of the door size (`render_offscreen`), then drawn. A door in states 1 to 3 draws two halves that slide apart, unless bit 5 of the door record is set.
- Wall text plates: the text goes into a 4 bpp bitmap of the plate size with the 8 x 8 font `(8, graphics set, 1, 3)`, lines centered, then the bitmap is scaled and drawn like the plate.
- Outdoor maps: sky objects (GDAT category `0x17`) are text entries with fields `cd` (zone), `fw` (flip kind), `mv` (0 fixed, 1 landmark), `xl`, `yl` (landmark position on the level), `fd` (fade distance). A landmark is scaled by its distance; its screen offset is `lateral * 210 / distance`. Like the original, the landmarks use the first sky object record as their work record.

## Things in the view

- Thing grids (`g_view_creature_grid`, `g_view_object_grid`, 21 x 17 bytes each, index `(lateral + 10) * 17 + forward`): each square has 5 x 5 cells, and neighbour squares share their edge rows and columns, so a square is 4 grid cells wide. `view_load_square` writes the view position of each square up to depth 3 that is not a wall into the creature grid (4 rows of 5 cells), and the view position of each creature group into the object grid at its cell relative to the party eye (cell 22, 10, 2, or 14 of the party square by direction). A creature of size 1 to 3 (creature info `+0x22`) stands nearer to the party. An occupied object grid cell moves the creature one cell nearer.
- Cell masks (`g_view_missile_masks`, `g_view_object_masks`, one 32-bit word per view position): bit n is set when grid cell n of the square holds a projectile, or an item. A floor square with a shop stand (side word 3 is 0) does not set the object bits.
- `show_square_things` goes through the 25 cells (the party square: the far 15) in an order toward the view axis. For each cell it draws the objects, the creature whose object grid cell is there (then the missiles of the 7 cells before), and the missiles. Mask bit 30 limits the creatures to static creatures: the creatures behind a closed door.
- Creature pictures: the animation frame (GDAT `(0x0f, type, 7, 0xfd)`, 8 bytes: picture entry for each side, grid cell, size row, shift, mirror bits) gives the picture, the cell, and the scale (`(0x0f, type, 7, 0xfe)`, 4 bytes per size row). A missing picture falls back to the opposite side (mirrored), to the front, then to the pictures `0xfa` to `0xfd`. Static creatures (creature info flag bit 0) keep the animation state in their record (`+8` sequence start, `+0x0a` state); the other creatures keep it in their active group (`g_active_groups`, `+8`).
- Possessions of a static creature with creature info word `+0x1e` bits 0 to 3 not 0: they lie above the floor (creature value key `0x41`, or 58 or 53) and one cell toward their side, so they can lie on a neighbour square. Cells that the cell mask leaves out wait (`0x75c00`, `0x80038`, `0x8003c`) for `show_pending_possessions`. A creature whose word `+0x1e` has neither bit 10 nor bits 4 to 7 set shows no possessions (only a click zone).
- Objects: the scale comes from a table by depth and row; the ordinal of the object in its cell adds a small offset (16 pairs). Chests (container GDAT index below 8) show picture 4 or 5 when open. Objects in an alcove are placed relative to the alcove picture (`g_view_draw_context_backup`) and the offset key `(9, ornament, 0x0c, 0xfd)`.
- Missiles: a payload without picture 8 is drawn as the object. Otherwise the pictures 8, 9, 10 (front, back) and 12 (side) are selected by the flight direction (bits 10 and 11 of data word 2 of the move event), the cell, and the square parity.
- Explosions: drawn in 5 rounds (the 4 cells in an order by the party direction, the explosions on the whole square in the middle). An explosion with picture `0x14` is a sparkle field. On the party square an explosion tints the whole view. Type `0x30` counts its views in global bytes `0x40` and `0x41`. The scale of an explosion in a near cell of the party square comes from the byte before the scale table (19).
- Sparkle fields (visible teleporters: `(0x18, 0)`) and rain tile a random part of their picture (`gfx_blit_tiled_4to8`) over the zone; the sparkles use a mask picture per position. Rain shows when the rain intensity is not 0 and the sky state `(8, graphics set, 0x0b, 0x66)` allows it (states 1 and 4). The rain picture (`0x6d` to `0x74`) depends on the rain intensity and on the wind relative to the party. A wet ornament takes a random part of the rain picture as its background.

## Deviations from the original

- No image cache: each GDAT image is decoded once and kept; scaled images and offscreen bitmaps live until the end of the frame. The original cache key of a scaled image keeps only 5 bits of the height scale and 7 bits of the width scale, so two scales can share a cached image; the port always scales anew.
- `gdat_exists` is true for every key of the file. The original also says no for an image that the map did not preload. The view checks only images of the map graphics set, which the original preloads.
- Text entries: `gdat_text` decrypts the text but does not expand the `.Z` placeholders of `text_format` ([interface.md](interface.md#ui-colors-and-text-formats)).
- Fixes: `view_unhide_linked_squares` read far past the view squares for missing neighbours; `show_ceiling_pit` read the square above without a bounds check; `setup_picture` divided by 0 for a width scale key with a low byte of 0; `get_shaded_palette` wrote a 256-color table into a 16-byte buffer; `dun_is_open_square_raw` read the record of the end mark on a teleporter square without things.
- Explosion types 100 and 101 in alcoves are ported, but GRAPHICS.DAT has no pictures for them.
- Fixes: the mirrored sparkle mask of an odd width has an undefined last column in the original (an image cache bitmap that is not cleared); the port clears it. The rain bitmap of a wet ornament is rect width x rain height (all rain pictures are 224 x 20). Its tile row is that area - 16 - 0 to 31 pixels, from a start of `rand_below(area - 60)` (16-bit). When the tile row is not longer than its start, for example a wet ornament 1 or 2 pixels wide, the original tiles from past the end of the rain bitmap; the port then fills the background with the color key. Reads past tables (creature info, animation frames, creature sizes) and a division by 0 for a looping animation of length 0 are guarded.
- `save.c` reads the static flag from the creature info table of `creature.c`.

## Tests

- `tests/differential/test_creature.py` compares the views of running creatures with the original.
- The view tests give the original zeroed active groups, because they draw without active groups.
