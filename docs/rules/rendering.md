# Drawing

The drawing code is in `src/data/gfx.c`, `src/ui/zone.c`, `src/ui/text.c`, and `src/ui/ui.c`. Addresses and notation follow the conventions of [skull-map.md](../reference/skull-map.md).

## Blitters (`gfx_blit` and its row functions)

- `gfx_blit` selects a row function by the source depth: a 4 bits per pixel (bpp) source goes to a 4 bpp or 8 bpp destination, an 8 bpp source always goes to an 8 bpp destination (mapped through the colormap if it is not `NULL`). Offsets are pixels; a 4 bpp row holds an even pixel count.
- Mirror 1 reads each row of the source window backwards, mirror 2 reads the rows from the bottom up, mirror 3 does both. A value above 3 works as 0.
- The transparent value is compared with the source pixel before the colormap.
- The mirrored 4 bpp to 4 bpp rows with a transparent color have quirks at the row ends: for example, the mirrored row writes its last pixel only if it is the transparent color. The port draws these rows like the other blits. The view does not use this case, and the text drawing uses `MIRROR_NONE` (Font and text drawing below).
- The 8 bpp scaler steps in 1/256 pixel, starting at half a step; the 4 bpp scaler steps in 1/128 pixel, starting at half a step. Both copy the previous destination row when the source row repeats.
- The masked tiled blit (`gfx_blit_tiled_4to8_masked`) skips the masked pixels at the start and at the end of each row; the tile advances for the skipped start pixels, not for the skipped end pixels.


## Layout zones (`zone_compute_rect`)

- A zone record is `{anchor type, parent, x, y}`. The parent chain ends at a zone with parent 0. A size zone (type 9) gives a width and height; the zones under it are placed in it and clipped to it.
- The image size comes from `*size` (a `ScreenSize`), or from the image header if it is 0. A zone with bit 15 (`ZONE_RELATIVE`) adds `size->w` and `size->h` to its position. On return, `*size` holds the pixels clipped at the left and the top: the source offset of the blit.
- `g_zone_clip_rect` (`0x7f1c8`) clips the next zones while its width is not 0. `zone_set_clip_margins` sets it to the viewport minus margins; the view sets its width to 0 after each picture.
- While the party walks (`g_party.move_steps` is not 0), zones of parent 3 (`WALK_PARENT`) clip to the walking view `{21, 8, 182, 110}`.


## Font and text drawing

- The font is [GDAT](../formats/graphics.md#gdat-items) `(1, 0, 7, 0)`: 6 rows of 128 bytes, one byte per character and row. Bits 4 to 0 are the 5 pixels of a glyph row. A character above 127 reads into the next row (the last row reads past the 768 bytes; the port reads zeros there). The font has no lowercase letters: the lowercase letter codes have other glyphs. For example, the name "f m" of item `(0x15, 62)` shows as "B %", as in the original.
- `build_glyph` makes a 4 bpp glyph of 6 × 6 pixels: the foreground value on the set bits, the background value on the other bits and in column 6. `text_draw` draws one glyph per character, 6 pixels apart, with the glyph rows from `y - 4` to `y + 1` (the base line is at `y`; one glyph row is below it).
- In an 8 bpp bitmap the glyph holds 0 and 1, and a 2-entry colormap gives the color and the background. A background with bit `0x4000` is transparent. Text uses `MIRROR_NONE`, so the mirrored 4 bpp quirks of the blitters (Blitters above) do not apply.
- `text_measure` gives the width `6 × length - 1` and the height 5. `text_wrap_line` breaks a line at the last space that fits; a word longer than the line breaks at the width.
- `text_draw_with_shadow` draws the text in UI color 0 one pixel down, and one pixel down and right, then in the text color. If the background does not have bit `0x4000`, it first fills a box 1 pixel larger than the text.


## Offscreen bitmaps

- An offscreen bitmap (`offscreen_create`) holds a screen rectangle: the image cache index, the rectangle, and up to 5 changed rectangles. A new rectangle inside a changed one is dropped; a new rectangle that contains a changed one replaces it; with a sixth rectangle, the 5 changed ones first go to the screen. `offscreen_flush` copies the changed rectangles to the screen.
- A champion's status box (zones `0xa1` + champion) and the action area (zone 11) draw into offscreen bitmaps. `status_box_begin` with `clear` false draws the box background but does not mark it as changed: only the parts that change later go to the screen.
- `fill_rect_dithered` fills the pixels whose `x + y` is even (for an even bitmap width) with a UI color. The port uses it for the dimmed hands and party faces of a busy or sleeping party, the dimmed pictures of the action area and of the minion map grid, and the portrait of an invisible party.


## Deviations from the original

- Offscreen bitmaps are frame bitmaps (`draw_temp_bitmap`), not image cache entries.
- Kept quirks of the original:
  - The scaled 4 bpp blit (`gfx_blit_scaled_4bpp`) wraps the nibble index of the source at 16 bits; only a source of more than 65535 pixels reaches the wrap.
  - `offscreen_draw_ctx` adds the clipped part of a picture to its size, not to its position, and `offscreen_draw_text_at` marks the changed rectangle from the base line down (`src/ui/ui.c`).
