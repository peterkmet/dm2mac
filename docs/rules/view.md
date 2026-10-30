# Dungeon view

The dungeon view code is in `src/data/gfx.c`, `src/ui/zone.c`, and `src/ui/view.c`. Addresses and notation follow the conventions of [skull-map.md](../reference/skull-map.md).

## Blitters (`gfx_blit` and its row functions)

- `gfx_blit` selects a row function by the source depth: a 4 bits per pixel (bpp) source goes to a 4 bpp or 8 bpp destination, an 8 bpp source always goes to an 8 bpp destination (mapped through the colormap if it is not `NULL`). Offsets are pixels; a 4 bpp row holds an even pixel count.
- Mirror 1 reads each row of the source window backwards, mirror 2 reads the rows from the bottom up, mirror 3 does both. A value above 3 works as 0.
- The transparent value is compared with the source pixel before the colormap.
- The mirrored 4 bpp to 4 bpp rows with a transparent color have quirks at the row ends: for example, the mirrored row writes its last pixel only if it is the transparent color. The port draws these rows like the other blits. The view does not use this case, and the text drawing uses `MIRROR_NONE` ([interface.md](interface.md#font-and-text-drawing)).
- The 8 bpp scaler steps in 1/256 pixel, starting at half a step; the 4 bpp scaler steps in 1/128 pixel, starting at half a step. Both copy the previous destination row when the source row repeats.
- The masked tiled blit (`gfx_blit_tiled_4to8_masked`) skips the masked pixels at the start and at the end of each row; the tile advances for the skipped start pixels, not for the skipped end pixels.

## Layout zones (`zone_compute_rect`)

- A zone record is `{anchor type, parent, x, y}`. The parent chain ends at a zone with parent 0. A size zone (type 9) gives a width and height; the zones under it are placed in it and clipped to it.
- The image size comes from `*size` (a `ScreenSize`), or from the image header if it is 0. A zone with bit 15 (`ZONE_RELATIVE`) adds `size->w` and `size->h` to its position. On return, `*size` holds the pixels clipped at the left and the top: the source offset of the blit.
- `g_zone_clip_rect` (`0x7f1c8`) clips the next zones while its width is not 0. `zone_set_clip_margins` sets it to the viewport minus margins; the view sets its width to 0 after each picture.
- While the party walks (`g_party.move_steps` is not 0), zones of parent 3 (`WALK_PARENT`) clip to the walking view `{21, 8, 182, 110}`.

## View square record and the view

- View square record (18 bytes, 23 records):
  - `+0`/`+1`: map x, y.
  - `+2`: aspect (0 wall, 1 floor, 2 open pit, 5 visible teleporter, 7 hidden, `0x10` door seen from the side, `0x11` door front, `0x12` stairs front, `0x13` stairs side).
  - `+4`: square byte.
  - `+6`: first thing that is not a wall thing.
  - `+8` to `+0x0e`, by aspect: the wall ornaments of the faces (`frame << 10 | ornament`, front face at `+0x0c`), or door state and door thing, or the up bit of stairs and pits, or the floor ornament at `+0x0c` (`frame * 10 << 8 | ornament`).
  - `+0x10`: wall text thing or portrait index.
- Squares off the map (`dun_get_square`) read as a wall with the bit of its face toward the map (`8 >> direction`, the random ornament bits of a wall) if they are next to an open edge square, otherwise as `0xe0`.
- Random wall ornaments: hash `(((((x * 0x20 + 2000 + (((dir + side) & 3) + 1) * (y + 1)) * 0x7ab9) >> 1) + (map * 0x40 + 3000 + width + height) * 11 + dungeon header word 0) >> 2) % 30`; a value below the random ornament count picks an ornament from the wall ornament list of the map entered last.
- Pictures go through `setup_picture`: [GDAT](../formats/graphics.md#gdat-items) image, scale (1/64), offset, layout zone, and a color remap. The remap is the depth shading table `(8, graphics set, 7, depth)` if it exists, otherwise a dimming by the depth light; the view shade (light level * 10) dims both.
- A door with an ornament or damage is composed in an offscreen bitmap of the door size (`render_offscreen`), then drawn. A door in states 1 to 3 draws two halves that slide apart, unless bit 5 of the door record is set.
- Wall text plates: the text is written into a 4 bpp bitmap of the plate size with the 8 × 8 font `(8, graphics set, 1, 3)`, with centered lines; then the bitmap is scaled and drawn like the plate.
- Outdoor maps: sky objects (GDAT category `0x17`) are text entries with fields `cd` (zone), `fw` (flip kind), `mv` (0 fixed, 1 landmark), `xl`, `yl` (landmark position on the level), `fd` (fade distance). A landmark is scaled by its distance; its screen offset is `lateral * 210 / distance`.

## Things in the view

- Thing grids (`g_view_creature_grid`, `g_view_object_grid`, 21 × 17 bytes each, index `(lateral + 10) * 17 + forward`): each square has 5 × 5 cells. Neighbor squares share their edge rows and columns, so a square is 4 grid cells wide.
- `view_load_square` writes the view position of each square up to depth 3 that is not a wall into the creature grid (4 rows of 5 cells), and the view position of each creature group into the object grid at its cell relative to the party eye (cell 22, 10, 2, or 14 of the party square by direction). A creature of size 1 to 3 (creature info `+0x22`) stands nearer to the party, by its size. If its object grid cell is occupied, the creature moves one cell nearer.
- Cell masks (`g_view_missile_masks`, `g_view_object_masks`, one 32-bit word per view position): bit n is set if grid cell n of the square holds a projectile or an item, respectively. A floor square with a shop stand (side word 3 is 0) does not set the object bits.
- `show_square_things` goes through the 25 cells (the party square: the far 15) in an order toward the view axis. For each cell it draws the objects, the creature whose object grid cell is there (then the projectiles of the 7 cells before), and the projectiles. Mask bit 30 limits the creatures to static creatures: the creatures behind a closed door.
- Creature pictures: the 8-byte animation frame at GDAT `(0x0f, type, 7, 0xfd)` (picture entry per side, grid cell, size row, shift, mirror bits) gives the picture, the cell, and the scale from `(0x0f, type, 7, 0xfe)` (4 bytes per size row). A missing picture falls back to the opposite side (mirrored), to the front, then to the pictures `0xfa` to `0xfd`. Static creatures (creature info flag bit 0) keep the animation state in their record (`+8` sequence start, `+0x0a` state); the other creatures keep it in their active group (`g_active_groups`, `+8`).
- The possessions of a static creature whose creature info word `+0x1e` has bits 0 to 3 not 0 lie above the floor (by the creature value key `0x41`; without it by 58 pixels for a creature that carries them high, 53 for one that carries them low) and one cell toward their side, so they can lie on a neighbor square. Cells that the cell mask leaves out wait (`0x75c00`, `0x80038`, `0x8003c`) for `show_pending_possessions`. A creature whose word `+0x1e` has neither bit 10 nor bits 4 to 7 set shows no possessions (only a click zone).
- Objects: the scale comes from a table by depth and row; the ordinal of the object in its cell adds a small offset (16 pairs). Chests (container GDAT index below 8) show picture 4 or 5 when open. Objects in an alcove are placed relative to the alcove picture (`view.alcove` in `src/ui/view.c`) and the offset key `(9, ornament, 0x0c, 0xfd)`.
- Projectiles: a payload without picture 8 is drawn as the object. Otherwise the pictures 8, 9, 10 (front, back) and 12 (side) are selected by the flight direction (bits 10 and 11 of data word 2 of the move event), the cell, and the square parity.
- Explosions: drawn in 5 rounds (the 4 cells in an order by the party direction, the explosions on the whole square in the middle). An explosion with picture `0x14` is a sparkle field. On the party square an explosion tints the whole view. Type `0x30` counts its views in global bytes `0x40` and `0x41`. The scale of an explosion in a near cell of the party square comes from the byte before the scale table (19).
- Sparkle fields (visible teleporters: `(0x18, 0)`) and rain tile a random part of their picture (`gfx_blit_tiled_4to8`) over the zone; the sparkles use a mask picture per position. Rain shows when the rain intensity is not 0 and the sky state `(8, graphics set, 0x0b, 0x66)` allows it (states 1 and 4). The rain picture (`0x6d` to `0x74`) depends on the rain intensity and on the wind relative to the party. A wet ornament takes a random part of the rain picture as its background.

## Seen squares

The advanced and the turn-based modes mark the seen squares of the automap (`seen_visible` in `src/engine/seen.c`). Each game step marks the squares of the dungeon view. The view has 5 rows: the party row and 4 rows ahead, with 3, 3, 5, 5, and 7 squares.

- The party square is seen.
- A square is seen if the seen square beside it toward the middle is open, or if the seen square in front of it (one row nearer) is open.
- The rows end at the first middle square that is not open.
- Not open: a wall, a closed fake wall, a closed door, and a square off the map.
- In low light the view is shorter: light level 4 shows one row less, light level 5 two rows less. The light cheat shows all rows.

## Automap

The automap of the advanced mode (`src/ui/automap.c`; the original game has none) draws each square with the icons of the minion map (`mmap_draw_square_icon`, [creatures.md](creatures.md#minion-map)).

- A square shows what a minion map without its options shows (`MAP_STATIC_CREATURES`): the static creatures with a map icon show on the seen squares; moving creatures, merchants, and items do not. The champion icons on the party square are the icons of a minion map item (`mmap_party_icons_item`).
- An edge wall is a square in the 1-square margin around a map (`MAP_MARGIN`), outside every other map of the level or on an unknown square of another map. There the dungeon view draws a wall (outdoors: trees) beside an open square of the map (`dungeon_square_edge`). The automap shows it if a square of that map next to it is seen (`automap_level_seen`).
- A pressure pad (`automap_is_pressure_pad`) is a floor square with a floor sensor whose floor ornament the dungeon view draws. The party, a creature, or an item triggers the sensor (sensor types any, party or creature, party, creature, party possesses an item, item). A floor trigger without an ornament stays hidden.

## Deviations from the original

- No image cache: each GDAT image is decoded once and kept; scaled images and offscreen bitmaps live until the end of the frame. The original cache key of a scaled image keeps only 5 bits of the height scale and 7 bits of the width scale, so two scales can share a cached image; the port always scales anew.
- `gdat_exists` is true for every key of the file. The original is false even for an image that the map did not preload. The view checks only images of the map graphics set, which the original preloads.
- Text entries: `gdat_text` decrypts the text but does not expand the `.Z` placeholders of `gtext_format` ([interface.md](interface.md#ui-colors-and-text-formats)).
- Bounds and division by 0, which the port guards. In the original:
  - `view_unhide_linked_squares` reads far past the view squares for missing neighbors.
  - `show_ceiling_pit` reads the square above without a bounds check.
  - `setup_picture` divides by 0 for a width scale key with a low byte of 0.
  - `inverse_scale` divides by 0 for a near door picture of 0 width or height (the port uses the full scale).
  - `get_shaded_palette` writes a 256-color table into a 16-byte buffer.
  - A wall text with more lines than its plate holds writes lines above or below the plate bitmap; the port skips each line that leaves the plate.
  - `view_hit_test_object` reads the possessions from the record of `THING_NONE` for a possessions click after the creature left the square; the port does nothing for that click.
  - `choose_shop_item` reads before a price table of 0 or 1 bytes; the port steps through the item indices as for a shop ornament without a price table.
  - `ornament_frame` divides by 0 for an ornament frames value of `0x8000`; the port shows frame 1, as the sensors do for a cycle of 1 ([world.md](world.md#deviations-from-the-original)).
- Fix: the dimmed remap (`pal_apply_dim_remap`) skips a key color of 256 or more, which only a damaged `GRAPHICS.DAT` gives; the original writes it past the remap table.
- Explosion types 100 and 101 in alcoves are ported, but GRAPHICS.DAT has no pictures for them.
- The mirrored sparkle mask of an odd width has an undefined last column in the original (an image cache bitmap that is not cleared); the port clears it.
- The rain bitmap of a wet ornament is rect width × rain height (all rain pictures are 224 × 20). Its tile row is the area minus 16 minus 0 to 31 pixels, from a start of `rand_below(area - 60)` (16-bit). If the tile row is not longer than its start, for example for a wet ornament 1 or 2 pixels wide, the original tiles from past the end of the rain bitmap; the port then fills the background with the color key. The rain over the view has the same tile row from a start of 0 to 255; with a rain picture of fewer than about 330 pixels (a damaged GRAPHICS.DAT) the row is not longer than its start, and the port draws no rain.
- The explosion tint over the view tiles a row of its picture minus 40 pixels; if the row is not longer than its start (a tint picture of at most 40 pixels, a damaged GRAPHICS.DAT), the port draws no tint.
- Reads past tables (creature info, animation frames, creature sizes, the rain flag of a sky state above 4) and a division by 0 for a looping animation of length 0 are guarded.
- `src/engine/save.c` reads the static flag from the creature info table of `src/engine/creature.c`.
- The advanced mode draws views between game steps (the projectile flight). These views change no game state: `show_projectile_flight` (`src/ui/play.c`) restores the random seed, explosions do not count their views again, and a shop display does not choose or create an item. They do not set the seen bit (`GROUP_SEEN`) of a group, because the original has no such views. Because the AI clears the bit at each mood update, the bit in an advanced-mode save can differ from the bit that a view on each tick would leave.
- Kept quirks, as in the original:
  - The scaled 4 bpp blit (`gfx_blit_scaled_4bpp`) wraps the nibble index of the source at 16 bits; only a source of more than 65535 pixels reaches the wrap.
  - In the frame text of an animated ornament (`ornament_frame`), a letter `A` to `Z` gives the frame `letter - 'K'`; the letters `A` to `J` give negative frames.
  - The landmarks of an outdoor map use the first sky object of `weather_sky_objects` as their work record. The weather update writes that record again before each use.
  - The explosion zones, the cell of the alcove items, and the side test of a passage read the party direction, not the view direction that `show_linked_squares` turns for the squares behind a passage.
  - A static creature on a door square seen from the front shows only the possessions in the near cells.
  - An item in an alcove gets the jitter of its spot twice: once for the alcove place and once as a floor item (`SKULL.c:52354-52358`, `52396-52401`).
  - The random wall ornaments of a square on another map (linked squares, minion map) use the wall ornament list of the map entered last, with the random ornament count, size, and index of the square's map (`random_ornament` in `src/engine/aspect.c`).
  - The hit test of a wet wall ornament on the front face draws the ornament with its rain and its contents into the view, while the hit test of the other ornaments only measures them (`SKULL.c:50988-51065`).
  - `show_far_squares` keeps the cell mask from square to square; a stairs square draws its things with the mask of the square drawn before it.
  - `aspect_get` clears the wall text of a square to 0, not `THING_TEXT_NONE` (`SKULL.c:12597`); only the wall things write `THING_TEXT_NONE`.
  - While the party walks, a wall on its square gets the text 0 (`load_walking_party_square`), and the wall squares beside it get `THING_TEXT_NONE`.
  - A creature of size 3 steps toward the party by (-2, 1) in two octants (`toward_party_diagonal`).
  - The dimming (`pal_apply_dim_remap`) replaces a key color by the nearest different step of its ramp; if every color of a ramp is a key color, it continues to read into the colors of the next ramp (`SKULL.c:9580`).
