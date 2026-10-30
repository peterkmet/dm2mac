# Interface panels and text

The code is in `src/ui/text.c`, `src/engine/game_text.c`, `src/ui/action_area.c`, and `src/ui/ui.c`. Addresses and notation follow the conventions of [skull-map.md](../reference/skull-map.md). Font drawing and offscreen bitmaps are in [rendering.md](rendering.md).

## UI colors and text formats

- GDAT `(1, 0, 13, 0xfe)` holds 16 palette indices: the UI colors (`enum ui_color` in `src/ui/include/text.h`). The interface draws with UI color indices, not palette indices.
- `gtext_format` replaces format codes. A code is `.Z` followed by 3 decimal digits, or the byte 1 followed by a byte with the value code + 32. The inserted text is formatted again. An unknown code inserts an empty text. The codes:
  - 2 and 27: the texts `(1, 0xfe, 5, 0)` and `(1, 0xfe, 5, 6)`.
  - 7: a champion name (word `0x7f988`).
  - 10 to 14: the numbers in the words `0x7f996`, `0x7f98a`, `0x7f994`, `0x7f992`, `0x7f986`.
  - 17: the text `(7, 0, 5, byte 0x7f990)`.
  - 0, 1, 3, 4, 8, 9, 15, 20, 22 to 26, 28: drive paths, disk sizes, error codes, and save names for the dialogs.
- `gtext_gdat` (`gdat_get_text` in the original) decrypts a text entry and formats it. The inventory sets the numbers before it formats a text, for example `(7, 0, 5, 0x2a)` (load) and `(7, 0, 5, 0x1f)` (weight).

## Message area

- 3 rows in zone 15, 7 pixels apart, with `g_message_area_y = (zone height - 20) / 2` above the first row. A row clears 90 game steps after its last text (`g_message_expiry`).
- A text at the bottom row that needs a new row goes into the row bitmap (320 × 7, `g_message_scroll_bitmap`). Before the next text, function `0x105` of the runtime layer `IBMIOP.EXE` ([runtime.md](../formats/runtime.md#ibmiopexe-runtime-layer)) moves the area up by one row; then the first 6 pixel rows of the row bitmap go to the bottom 6 pixel rows of the area.
- Text before a `\n` goes to the next row; a wrapped line continues at column 12.

## Slots and panels

- The slot table (`0x75538`, 55 records of 8 bytes): layout zone, icon of the empty slot, and the drawn state (icon, GDAT index, active and wounded frame, thing). Records 0 to 7 are the hands in the status boxes, 8 to 37 the inventory slots, 38 to 45 the open container, 46 the object info, 47 to 54 the contents of a chest in the object info.
- An item icon is GDAT `(category, index, 1, 0x18 + frame)`. Item attribute 6 selects the frame: bits 0 to 4 frame count, bits 8 to 12 the mode (game time, random, party direction, charges, charges and time, the same two per item), bits 5 to 7 the time steps of mode 4, bit 15 a first frame for a fitting slot, bit 14 a first frame in the open hand.
- The action area shows the party's hands (mode 0), the action menu of a hand (1), the spell panel (2), a minion map (3), a money box (4), a chest (5), or the result of an action (6). A hand icon shows only if the hand has actions (`champ_list_actions`): the actions are the item texts 8 to 11 whose command, hand, charges, and skill level allow them.
- The skills panel keeps the raised count of each base skill that it shows (`0x7f968`). Opening the inventory clears these counts; closing it subtracts them from the champion counters (`0x7fff8` + champion × 4), so a raised skill shows green until the player sees it.

## Deviations from the original

- No hardware layer: the screen bitmap is `g_video_fb`; `screen_present_*`, `mouse_hide`, `mouse_show`, and `vga_wait_retrace` have no port.
- Function `0x105` is in `IBMIOP.EXE`, which is not decompiled. The port moves the rows of zone 15 up by 7 pixels and keeps the bottom 7 rows (`shift_message_area`).
- The action area reads the party map's wall set from the map descriptor; the original reads `g_map_gfx_set`, which `game_enter_map` sets from the same descriptor.
- `champ_get_load` adds the current weight of the leader hand object; the original adds the weight that it stores when the object goes into the hand.
- `item_gdat_index` gives `0xff` for wall texts and sensors (the view has its own ornament code).
- `gtext_decode_dungeon` (`dun_decode_text`) and the view share one raw decoder, `gtext_decode_raw` in `src/engine/game_text.c`. `gtext_decode_dungeon` adds the format pass. The view reads the wall plate texts without the format codes.
- Fixes:
  - The texts are bounded to 256 bytes.
  - The coin table holds at most 10 coin types.
  - The dungeon text decoder stops at the end of the text data (the original reads past it in a damaged dungeon).
  - A `.Z` format code that a text cuts after fewer than 3 digits ends at the end of the text, and a text that ends in the format code byte 1 ends there (`gtext_format`; the original reads the digits or the code past the end).
  - A description line without a space breaks at 18 characters and continues with the rest (`show_description` in `src/ui/ui.c`; the original `inv_draw_description_text` scans back before the start of the text).
  - The hand icons of the action area draw without the shadow remap if its GDAT item `(1, 0, 7, 1)` is shorter than 256 bytes (a damaged GRAPHICS.DAT; `make_hand_icon` in `src/ui/action_area.c`). The original reads past it.
- Kept quirks of the original:
  - The action area keeps the champion of the open panel and of the flashing icon as an ordinal (index + 1, 0 none): the input tree compares it with node arguments (`ActionArea` in `src/engine/include/action.h`).
  - The minion map functions (`minion_map_set_location`, `minion_map_click_set_destination`, `minion_dismiss` in `src/engine/minion.c`) switch the current map to the map of the square and back with `dungeon_set_current_map`, which also sets `g_current_map.square_x`, `square_y`, `square_map`, and `ref_dir` again.
  - A gauge bar scales the 16-bit difference of the value and the low end (`show_gauge_bar` in `src/ui/ui.c`, `ui_draw_gauge_bar`).
  - A wall square of the minion map also shows the wall ornaments and the things of the first other map of the level that has a wall on the same square; the search leaves the square on that map, and `show_square` (`src/ui/minion_map.c`) switches the current map to it and back.
  - `select_command_in_list` and `input_find_command_entry` (`src/ui/input_tree.c`) walk an entry list until the first entry without a command, like `input_select_command_in_list` and `input_find_command_entry` in the original; a list without that end entry is read past its end.
