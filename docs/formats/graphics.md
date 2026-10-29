# Graphics data files

The game data container `GRAPHICS.DAT` and the two optional files `GRAPHIC2.DAT` and `DUNGENB.DAT`. `GRAPHICS.DAT` holds the images, the sounds ([sound.md](sound.md)), the songs ([music.md](music.md)), the texts, and the layout data of the game. The addresses and the notation follow the conventions of [skull-map.md](../reference/skull-map.md).

## GRAPHICS.DAT container

The original routines are `FUN_0003d1c8` (open), `FUN_0003ce0a` (table load), `FUN_0003c4f2` (lookup), and `FUN_0003bef6` (read).

| Offset | Size | Content |
|---|---|---|
| `0x00` | u16 LE | Signature `0x8005`. Bit 15 must be set. Version = `0x0005`. |
| `0x02` | u16 LE | Item count `N` (`5624`). |
| `0x04` | u32 LE | Size of item 0 (the key table), `0x17284`. |
| `0x08` | `(N-1)` × u16 LE | Size of items 1 to `N-1`. |
| `0x2bf6` | | Item 0: the key table. |
| `0x19e7a` | | Item 1. Item `i` follows item `i-1` without a gap. |

The key table is big-endian (first word `0x8001` is read byte-swapped). It holds a row count (`11854`), a column count (`7`), and seven column descriptors (`T`, `I`, `D`, `S`, `F`, `G` with 1 byte, `P` with 2 bytes). Each row has 8 bytes. `T` is the category, `I` the index, `D` the type, `S` the entry. For types `0x0b` and `0x0c`, `P` is a value. For other types, `P & 0x7fff` is the item number.

The sum of all item sizes ends at byte `8639757`. This is the exact size of `game/DATA/GRAPHICS.DAT`.

### Language filter (field `F`)

Source: reDM2 `docs/formats.md`, "F IS THE LANGUAGE". The names of the symbol table agree: `gdat_key_row_matches_language` (`0x3cd2f`) and `g_language` (`0x7576c`).

- `gdat_load_key_table` keeps a row only if `F & 0xf0` is `0` or equal to `g_language`. Other rows are not installed.
- `g_language` is `0x10` by default. `main` sets `0x30` for `+GLG` and `0x40` for `+GLF`.
- High nibble of `F`: `0` = all languages (10102 rows), `1` = English, `3` = German, `4` = French (545 rows each), `0xf` = never installed (117 developer labels).
- Each setting installs 10647 rows with the same key set. Only the item numbers of the 478 localized keys change.

The port applies the same filter when the key table loads. The default is English (`0x10`).

### Image compression

Source: reDM2 `docs/formats.md`, section "Compression", and its comments on `0x3e867` and `0x3f71c`. `reference/decomp/SKULL.c` confirms the dispatch.

`gdat_decode_image` (`0x3e867`) reads the first u32 of the raw item as a signed value and shifts it right by 26 (the gate):

| Gate | Meaning | Decode |
|---|---|---|
| `-32` | Not compressed | The pixels start at item + `0x0a`. Item + 4 holds a ready image header. |
| Equal to `g_gdat_8bpp_marker` (`0x7fac8`) | 8-bit image | `image_decode_8bpp` (`0x3f71c`) |
| Other value | 4-bit image, nibble and run-length codes | `image_decode_4bpp` (`0x12e4f`) |
| (delta table present) | 4-bit image relative to a parent image | `image_decode_4bpp_delta` (`0x13258`) |

`gdat_load_index` sets `g_gdat_8bpp_marker` to 31 if bit `0x40` of the flag item `(0,0,0x0b,0)` is set, otherwise to 1000 (no gate can be 1000, so all images are 4-bit). The shipped flag item is `0x7b`, so the marker is 31.

`image_decode_8bpp` selects the decompressor. It reads the codec byte at item + 6, skips 8 bytes, and calls the first pointer of the entry `g_image_decoders[codec]` (`0x75798 + codec * 8`). The second pointer of the entry is an init function (`void (*)(void)`). Only codec 1 has one, `gdat_lzw_alloc_tables`, which allocates the LZW tables. `gdat_load_index` calls it once on startup through `0x757a4` when flag `0x40` of item `(0,0,0x0b,0)` is set. The function does not check the codec value.

| Codec | Function | Scheme |
|---|---|---|
| 0 | none (null pointer) | — |
| 1 | `gdat_lzw_decode` (`0x5aada`), allocator `gdat_lzw_alloc_tables` (`0x5acf4`) | LZW (9 to 12 bits) and `0x90` run-length |
| 2 | `gdat_lzss12_decode` (`0x5ad2b`) | LZSS, mask `0x0f`, 4/4 bit split, 12-bit distance |
| 3 | `gdat_lzss13_decode` (`0x5ad8d`) | LZSS, mask `0x1f`, 5/3 bit split, 13-bit distance |

`g_gdat_decompressors` (`0x757a0`) is `g_image_decoders[1]`.

reDM2 measurements for the shipped file:

- 3167 images are 8-bit and use codec 2 or 3. 1003 images are 4-bit. 6 images are not compressed.
- No shipped image uses codec 1 (LZW).
- The delta table key `(0,0,8,0)` is absent. `g_gdat_base_image_count` (`0x7fabe`) stays 0. Thus `image_decode_4bpp_delta` does not run with the shipped file.
- 1442 items have a gate outside `{0, 31, -32}` and are not images. The decode entry does not reject them.

Image sweep of `tests/differential/test_gdat.py` and the facts of the image items:

- The English image keys (type 1) point to 803 4-bit, 3166 8-bit, and 6 uncompressed images, and to 24 items with other gates. All 3969 compressed images decode byte-equal to the original decoders. 167 gate-0 items of types 2, 4, 7, 8, and 9 are not valid 4-bit streams.
- The 24 image items with other gates are 4-bit images. The gate (top 6 bits of word 1, the high half of the first u32) holds the y offset and the top 6 bits of word 0 hold the x offset of the image (`gdat_get_image_offset`, `0x3eed3`). An 8-bit image holds its offset in bytes 4 and 5. When the top 6 bits of word 0 are `-32`, the offset is the value key `(category, index, 0x0c, entry)`. All 827 4-bit images decode byte-equal.
- An uncompressed image holds u16 bits per pixel (4 or 8) at item + 4 and the pixels at item + 10. A 4-bit image (compressed or not) ends with its 16-byte color map.
- The palette item `(1,0,9,254)` has 256 entries of 4 bytes: color index, then 8-bit red, green, blue. The VGA DAC takes 6-bit values, so the port shifts each value right by 2 (`gdat_palette_color`).
- The layout item `(1,0,4,0)` is raw bytes: u16 magic `0xfc0d`, u16 group count, group ranges, then `{anchor type, parent, x, y}` records (`src/ui/include/zone.h`). `zone_unpack_layout` (`0x19d0e`) reads it.

## DUNGENB.DAT and GRAPHIC2.DAT

Both files are absent from `game/DATA`. The CD release does not need them.

### GRAPHIC2.DAT

- It is the continuation of `GRAPHICS.DAT` for a split (floppy) distribution. `FUN_0003d1c8` compares the file size of `GRAPHICS.DAT` with the end of the last item. If the file is shorter, it sets the split flags `DAT_0007fa74` and `DAT_0007fa78`.
- `FUN_0003bef6` (GDAT read): if `DAT_0007fa74` is set, it subtracts the size of `GRAPHICS.DAT` (`DAT_0007fa44`) from the item offset and reads the rest from the `GRAPHIC2.DAT` handle (`DAT_0007fac0`).
- `FUN_0003d111` opens `.Z026GRAPHIC2.DAT` (`0x700a5`) only if `DAT_0007fa74 != 0 && DAT_0007fa78 == 0`. The disk labels `SKDISKA`, `SKDISKB`, `SKDISKC` (`0x7051c`) and the disk prompt in `FUN_000111c7` belong to the same floppy logic.
- The `GRAPHICS.DAT` of `game/DATA` is complete ([GRAPHICS.DAT container](#graphicsdat-container)).

### DUNGENB.DAT

- It is an optional second new-game dungeon. `FUN_00037058` opens `.Z020DUNGENB.DAT` (`0x70082`) if `DAT_000803f8 != 0`. If the open fails, it opens `DUNGEON.DAT`.
- Command `0xd8` sets `DAT_000803f8 = 1` and the new-game flag `DAT_0007f280 = 1`. Command `0xd7` sets only the new-game flag (normal new game). Command `0xd9` clears it (load a game). See `FUN_00021d6c` (`command_execute`).
- The mouse table at `0x72388` binds `0x80d7` and `0x00d8` to the same zone `0x197` (the new-game button), with button masks `0x8002` and `0x4010`. The exact input that matches mask `0x4010` is not verified.
- `DUNGEON.FTL` (`0x70071`) is a similar fallback: the code at `0x37259` opens it as a new game when no save file exists.

### Compare with DUNGEON.DAT and GRAPHICS.DAT

`GRAPHIC2.DAT` holds the bytes after the end of a short `GRAPHICS.DAT`. `DUNGENB.DAT` has the same role as `DUNGEON.DAT` but is a different start dungeon.

The port keeps the `DUNGENB.DAT` fallback logic: it opens `DUNGENB.DAT` when it is chosen and present, otherwise `DUNGEON.DAT`. With the shipped files the result is the same as without command `0xd8`.

## Deviations from the original

- The port supports only the complete `GRAPHICS.DAT`; it does not read `GRAPHIC2.DAT`.
- The port decodes the 8-bit codecs 2 and 3 only; codec 1 (LZW) gives a data error. No shipped image uses codec 1.
- Fix: the port rejects a color ramp item (`(1, 0, 7, 2)`) whose ramp lengths reach past the item; the original reads on past it.
- Fix: `zone_get_icon_rect` gives the zero rect for a zone that places no rect (an unknown zone, which only a damaged `GRAPHICS.DAT` asks for, or a zone that the walk view hides); the original widens the rect that it did not set.
- Kept quirk: `zone_get_icon_rect` widens the cut rect also when the clip hides the icon, as in the original.
- Fix: the dimmed remap (`pal_apply_dim_remap`) skips a key color of 256 or more, which only a damaged `GRAPHICS.DAT` gives; the original writes it past the remap table.
- Kept quirk: the scaled 4 bpp blit (`gfx_blit_scaled_4bpp`) wraps the nibble index of the source at 16 bits, as in the original; only a source of more than 65535 pixels reaches the wrap.
- Kept quirk: `gdat_text_int_field` goes on with the digits of the first match when the key comes a second time in the text, as in the original.
