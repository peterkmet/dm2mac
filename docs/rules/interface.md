# Interface panels and text

The rules of the interface: the font and the text drawing, the UI colors and the text formats, the message area, the offscreen bitmaps, and the slots and panels. The code is in `src/ui/text.c`, `src/engine/game_text.c`, `src/ui/action_area.c`, and `src/ui/ui.c`. The addresses and the notation follow the conventions of [skull-map.md](../reference/skull-map.md).

## Font and text drawing

- The font is GDAT `(1, 0, 7, 0)`: 6 rows of 128 bytes, one byte per character and row. Bits 4 to 0 are the 5 pixels of a glyph row. A character above 127 reads into the next row (the last row reads past the 768 bytes; the port reads zeros there).
- `build_glyph` makes a 4 bpp glyph of 6 × 6 pixels: the foreground value on the set bits, the background value on the other bits and in column 6. `text_draw` draws one glyph per character, 6 pixels apart, with the glyph rows from `y - 4` to `y + 1` (the base line is at `y`, 1 row of a glyph is below it).
- In an 8 bpp bitmap the glyph holds 0 and 1, and a 2-entry colormap gives the color and the background. A background with bit `0x4000` is transparent. Text uses `MIRROR_NONE`, so the mirrored 4 bpp quirks of the blitters ([view.md](view.md#blitters-gfx_blit-and-its-row-functions)) do not apply.
- `text_measure` gives the width `6 × length - 1` and the height 5. `text_wrap_line` breaks a line at the last space that fits; a word longer than the line breaks at the width.
- `text_draw_with_shadow` draws the text in UI color 0 one pixel down, and one pixel down and right, then in the color. A background without bit `0x4000` first fills a box 1 pixel larger than the text.

## UI colors and text formats

- GDAT `(1, 0, 13, 0xfe)` holds 16 palette indices: the UI colors (`enum ui_color` in `src/ui/include/text.h`). The interface draws with UI color indices, not palette indices.
- `text_format` replaces format codes: `.Z` and 3 decimal digits, or byte 1 and the code + 32. The inserted text is formatted again. Codes: 2 and 27 texts `(1, 0xfe, 5, 0)` and `(1, 0xfe, 5, 6)`; 7 a champion name (word `0x7f988`); 10 to 14 the numbers in the words `0x7f996`, `0x7f98a`, `0x7f994`, `0x7f992`, `0x7f986`; 17 the text `(7, 0, 5, byte 0x7f990)`. The other codes (0, 1, 3, 4, 8, 9, 15, 20, 22 to 26, 28) give drive paths, disk sizes, error codes, and save names for the dialogs. An unknown code inserts an empty text.
- `text_gdat` (`gdat_get_text` in the original) decrypts a text entry and formats it. The inventory sets the numbers before it formats a text, for example `(7, 0, 5, 0x2a)` (load) and `(7, 0, 5, 0x1f)` (weight).

## Message area

- 3 rows in zone 15, 7 pixels apart, with `g_message_area_y = (zone height - 20) / 2` above the first row. A row clears 90 game steps after its last text (`g_message_expiry`).
- A text at the bottom row that needs a new row goes into the row bitmap (320 × 7, `g_message_scroll_bitmap`). Before the next text, IBMIOP service `0x105` moves the area up by one row; then the first 6 rows of the row bitmap go to the bottom 6 rows of the area.
- Text before a `\n` goes to the next row; a wrapped line goes on at column 12.

## Offscreen bitmaps

- An offscreen bitmap (`gfx_offscreen_create`) holds a screen rectangle: the image cache index, the rectangle, and up to 5 changed rectangles. A changed rectangle that holds a new one keeps it; a new one that holds a changed one replaces it; a sixth one first copies the 5 to the screen. `gfx_offscreen_flush` copies the changed rectangles to the screen.
- The status box of a champion (zones `0xa1` + champion) and the action area (zone 11) draw into offscreen bitmaps. `ui_status_box_begin_draw` with clear 0 draws the box background but forgets it as changed: only the parts that change later go to the screen.
- `fill_rect_dithered` fills the pixels whose `x + y` is even (for an even bitmap width) with a UI color: the dimmed hands of a busy or sleeping champion and the portrait of an invisible party.

## Slots and panels

- The slot table (`0x75538`, 55 records of 8 bytes): layout zone, icon of the empty slot, and the drawn state (icon, GDAT index, active and wounded frame, thing). Records 0 to 7 are the hands in the status boxes, 8 to 37 the inventory slots, 38 to 45 the open container, 46 the object info, 47 to 54 the contents of a chest in the object info.
- An item icon is GDAT `(category, index, 1, 0x18 + frame)`. Item attribute 6 selects the frame: bits 0 to 4 frame count, bits 8 to 12 the mode (game time, random, party direction, charges, charges and time, the same two per item), bits 5 to 7 the time steps of mode 4, bit 15 a first frame for a fitting slot, bit 14 a first frame in the open hand.
- The action area shows the hands of the party (mode 0), the action menu of a hand (1), the spell panel (2), a minion map (3), a money box (4), a chest (5), or the result of an action (6). A hand icon shows only when the hand has actions (`champ_list_actions`): the actions are the item texts 8 to 11 whose command, hand, charges, and skill level allow them.
- The skills panel keeps the raised count of each base skill that it shows (`0x7f968`). Opening the inventory clears these counts; closing it subtracts them from the champion counters (`0x7fff8` + champion × 4), so a raised skill shows green until the player sees it.

## Deviations from the original

- No hardware layer: the screen bitmap is `g_video_fb`; `screen_present_*`, `mouse_hide`, `mouse_show`, and `vga_wait_retrace` have no port. Offscreen bitmaps are frame bitmaps (`draw_temp_bitmap`), not image cache entries.
- IBMIOP service `0x105` is not in `SKULL.EXE`. The port moves the rows of zone 15 up by 7 pixels and keeps the bottom 7 rows (`shift_message_area`); manual check B18 compares it with DOSBox.
- The action area reads the wall set of the party map from the map descriptor; the original reads `g_map_gfx_set`, which `game_enter_map` sets from the same descriptor.
- `champ_get_load` adds the weight of the leader hand object when it is called; the original adds the weight that it keeps since the object went into the hand.
- `item_gdat_index` gives `0xff` for wall texts and sensors (the view has its own ornament code).
- `text_decode_dungeon` (`dun_decode_text`) and the view share one raw decoder, `text_decode_raw` in `engine/game_text.c`. `text_decode_dungeon` adds the format pass. The view reads the wall plate texts without the format codes.
- Fixes: the texts are bounded to 256 bytes; the coin table holds at most 10 coin types.
