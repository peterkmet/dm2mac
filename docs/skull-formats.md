# SKULL.EXE formats and runtime services

This page answers the open points of the PRD (`docs/superpowers/2026-10-04-dm2-c-port.md`, section "Open points for the analysis") for Step 5 of the plan.

Conventions:

- A linear address (`0x10cc2`) is an address in the flat image from `uv run dm2-le game/SKULL.EXE skull.bin` (code base `0x10000`, data base `0x70000`). `FUN_xxxxxxxx` is the function at that address. `docs/skull-symbols.csv` gives its name, and `docs/skull-map.md` lists the program structure.
- A file offset is written as `file+0x...`.
- A GDAT key is the tuple `(category, index, type, entry)` that the lookup function `FUN_0003c4f2` uses.
- HMI SOS function names (for example `sosDIGIStartSample`) are probable names. They come from the call shape and from public HMI SOS knowledge. The binary has no symbols.

## GRAPHICS.DAT container (needed by the answers)

Evidence: `FUN_0003d1c8` (open), `FUN_0003ce0a` (table load), `FUN_0003c4f2` (lookup), `FUN_0003bef6` (read).

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

The reDM2 project (`docs/formats.md`, section "GRAPHICS.DAT") gives the same header, item count, key table size (`0x17284` = 94852), and row layout. Its census also gives these facts. We did not repeat the census.

### Language filter (field `F`)

Source: reDM2 `docs/formats.md`, "F IS THE LANGUAGE". Our code names agree: `gdat_key_row_matches_language` (`0x3cd2f`) and `g_language` (`0x7576c`).

- `gdat_load_key_table` keeps a row only if `F & 0xf0` is `0` or equal to `g_language`. Other rows are not installed.
- `g_language` is `0x10` by default. `main` sets `0x30` for `+GLG` and `0x40` for `+GLF`.
- High nibble of `F`: `0` = all languages (10102 rows), `1` = English, `3` = German, `4` = French (545 rows each), `0xf` = never installed (117 developer labels).
- Each setting installs 10647 rows with the same key set. Only the item numbers of the 478 localized keys change.

Impact on the port: apply the same filter when the key table loads. The default is English (`0x10`).

### Image compression (Step 8)

Source: reDM2 `docs/formats.md`, section "Compression", and its comments on `0x3e867` and `0x3f71c`. We checked the dispatch in `decomp/SKULL.c`.

`gdat_decode_image` (`0x3e867`) reads the first u32 of the raw item as a signed value and shifts it right by 26 (the gate):

| Gate | Meaning | Decode |
|---|---|---|
| `-32` | Not compressed | The pixels start at item + `0x0a`. Item + 4 holds a ready image header. |
| Equal to `g_gdat_8bpp_marker` (`0x7fac8`) | 8-bit image | `image_decode_8bpp` (`0x3f71c`) |
| Other value | 4-bit image, nibble and run-length codes | `image_decode_4bpp` (`0x12e4f`) |
| (delta table present) | 4-bit image relative to a parent image | `image_decode_4bpp_delta` (`0x13258`) |

`gdat_load_index` sets `g_gdat_8bpp_marker` to 31 if bit `0x40` of the flag item `(0,0,0x0b,0)` is set, else to 1000 (no gate can be 1000, so all images are 4-bit). The shipped flag item is `0x7b`, so the marker is 31.

`image_decode_8bpp` selects the decompressor. It reads the codec byte at item + 6, skips 8 bytes, and calls the first pointer of the entry `g_image_decoders[codec]` (`0x75798 + codec * 8`). The second pointer of the entry is an init function (`void (*)(void)`). Only codec 1 has one, `gdat_lzw_alloc_tables`, which allocates the LZW tables. `gdat_load_index` calls it once at start through `0x757a4` when flag `0x40` of item `(0,0,0x0b,0)` is set. The function does not check the codec value.

| Codec | Function | Scheme |
|---|---|---|
| 0 | none (null pointer) | — |
| 1 | `gdat_lzw_decode` (`0x5aada`), allocator `gdat_lzw_alloc_tables` (`0x5acf4`) | LZW and `0x90` run-length |
| 2 | `gdat_lzss12_decode` (`0x5ad2b`) | LZSS, mask `0x0f`, 4/4 bit split |
| 3 | `gdat_lzss13_decode` (`0x5ad8d`) | LZSS, mask `0x1f`, 5/3 bit split |

This answers open question 2 of `docs/skull-map.md`: `g_gdat_decompressors` (`0x757a0`) is `g_image_decoders[1]`.

reDM2 measurements for the shipped file:

- 3167 images are 8-bit and use codec 2 or 3. 1003 images are 4-bit. 6 images are not compressed.
- No shipped image uses codec 1 (LZW). Do not port it first.
- The delta table key `(0,0,8,0)` is absent. `g_gdat_base_image_count` (`0x7fabe`) stays 0. Thus `image_decode_4bpp_delta` does not run with the shipped file.
- 1442 items have a gate outside `{0, 31, -32}` and are not images. The decode entry does not reject them. Do not decode them in the Step 8 test.
- Step 8 sweep (`tests/test_gdat.py`): the English image keys (type 1) point to 803 4-bit, 3166 8-bit, and 6 uncompressed images, and to 24 items with other gates. All 3969 compressed images decode byte-equal to the original decoders. 167 gate-0 items of types 2, 4, 7, 8, and 9 are not valid 4-bit streams.
- An uncompressed image holds u16 bits per pixel (4 or 8) at item + 4 and the pixels at item + 10. A 4-bit image (compressed or not) ends with its 16-byte color map.
- The palette item `(1,0,9,254)` has 256 entries of 4 bytes: color index, then 6-bit red, green, blue.
- The layout item `(1,0,4,0)` is raw bytes: u16 magic `0xfc0d`, u16 group count, group ranges, then `{x, y, w, h}` records. `zone_unpack_layout` (`0x19d0e`) reads it.

## 1. Digital sound effects

### Answer

- The samples are GDAT items of type `2` in `GRAPHICS.DAT`. There are no separate sound files. `TEST.RAW` is only for the HMI `SETUP.EXE` test.
- There are 292 sound items. Each item has a 6-byte header and raw PCM data.
- Header: u16 LE sample rate `0x2b77` (11127 Hz), then the bytes `08 01 00 00` (probably 8 bits, 1 channel). All 292 items have the same header.
- Data: 8-bit signed PCM, mono, not compressed. Length = item size − 6 (245 to 61713 bytes).
- At load time, `SKULL.EXE` converts the data in place to unsigned 8-bit (XOR `0x80`).
- The SOS digital driver mixes at 11025 Hz. The game stores the 11127 Hz rate but does not pass it to the driver. The samples play at 11025 Hz (0.9 % slower).

### Evidence

- GDAT flag item `(0,0,0x0b,0)` = `0x7b`. `FUN_0003d1c8` stores `0x7b & 0x20` in `DAT_00071698`.
- `FUN_0001623b` (sound cache): for each key `(cat, idx, 2, entry)` it sets the 16-byte descriptor at `DAT_0007f194 + n*16`:
  - `+0` data pointer = item + 6 (item + 2 if the `0x20` flag is clear),
  - `+4` u16 length = item size − 6,
  - `+6` u16 rate = first item word (constant `0x157c` = 5500 Hz if the flag is clear),
  - `+8` link for the cache list.
- `FUN_000104ea` XORs each byte with `0x80`. The descriptor field `+6` has no reader in the decompiled code.
- First sound item `(3,0,2,129)`, item 170, at `file+0x4bdd3`: `77 2b 08 01 00 00 7f 6b 20 cd ...`. Item 172 (`file+0x4ddcb`): `77 2b 08 01 00 00 0a 09 08 09 0a ...`. The mean of all data bytes read as signed is 1.02. Read as unsigned it is 119.7. Thus the data is signed.
- Driver init (`FUN_00010010`): `_DAT_00070482 = 0x2b11` (11025) in the SOS init structure at `0x70472` (buffer size word `0x1000`). Then `FUN_0005bc00` (`sosDIGIInitDriver`).
- Categories that hold sounds: 15 (192 items), 9 (24), 22 (21), 13 (16), 10 (8), 3 (7), and others.

### Playback, volume, and stereo position

Request: `FUN_00015ca9(cat, idx, entry, priority, volume, x, y, mode)`.

1. If the global flag `DAT_0007f234` is set, `volume = volume >> 1`.
2. The sound position is made relative to the party (`DAT_0007f26a`, `DAT_0007f26e`) and rotated by the party direction `DAT_0007f252`. The result is `lateral` (byte `+6`) and `forward` (byte `+7`).
3. `dist = |dx| + |dy|`. If `dist > 1`, `FUN_0002facc(x, y)` returns `n`. If `n < 0`, the sound is dropped. If `dist < n`, both components are scaled: `k = (n << 10) / dist`, `c = ±((|c| * k + 0x200) >> 10)`.
4. `mode`: `0` plays now, `1` queues to `DAT_0007f198`, `-1` queues to `DAT_0007f190`, `>= 2` schedules timeline event `0x15` at `now + mode − 1`. `FUN_0001616d` flushes a queue.

Volume and pan: `FUN_00010877` on the 12-byte request record.

```c
vol8 = (uint8_t)(((volume << 8) / (forward*forward + lateral*lateral + 8)) >> 5);
if (lateral == 0)      pan = 0x8000;                         /* center */
else if (forward == 0) pan = lateral < 0 ? 0x0800 : 0xf800;  /* full left or right */
else {
    r = (|lateral| << 11) / |forward|;
    i = first i with THRESH[i] <= r;       /* THRESH at 0x704de */
    pan = lateral < 0 ? PAN[i] : PAN[15 - i];  /* PAN at 0x704ee */
}
```

- `THRESH` (8 × u16 at `0x704de`): `0x25a0 0x11f0 0x0b00 0x0730 0x0490 0x0290 0x00d0 0x0000`.
- `PAN` (16 × u16 at `0x704ee`): `0x0800 0x1700 0x2600 0x3500 0x4400 0x5300 0x6200 0x7100 0x8f00 0x9e00 0xad00 0xbc00 0xcb00 0xda00 0xe900 0xf800`.
- `FUN_00010573` sorts the records by priority (byte `+4`), then by `vol8` (`FUN_0001084d`). It fills the SOS start-sample structure at `0x703f8`: `+0` data pointer, `+8` length, `+0x10` = 2, `+0x14` volume = `vol8 << 7 | vol8 >> 1`, `+0x18` slot, `+0x28` = 800, `+0x40` pan. Then it calls `FUN_000609b9` (`sosDIGIStartSample`). There are 64 slots (`DAT_0007eda0`).
- Master volume, `FUN_00010736(level 0..7, 0)`: `FUN_00061a45` gets `l | l<<3 | l<<6 | l<<9 | l<<12` (0 to `0x7fff`).

Confidence: high for the format and the formulas. Medium for the SOS structure field names.

Disagreement with reDM2: reDM2 (`docs/formats.md`, "There is no audio in GRAPHICS.DAT") states that the archive holds no digital samples. It lists the 292 type-2 rows as "image?, inferred, no reader". Our evidence is the reader `FUN_0001623b`, which loads exactly these 292 items as sound descriptors, and the constant 6-byte sample header. We keep our answer. reDM2 names `FUN_000104ea` `hilite_node_add` and marks its role as not verified. The caller passes the sample descriptors, and an XOR of bit 7 is the signed to unsigned PCM conversion. We keep `sound_prepare_sample`.

Impact on the port: load sounds from GDAT type `2`, skip 6 bytes, convert signed to SDL `S8` (or unsigned `U8`), and play at 11025 Hz. Port `FUN_00010877` and the scaling in `FUN_00015ca9` for the Step 18 differential test.

## 2. HMP songs and SONGLIST.DAT

### Answer

- The songs are 29 GDAT items with key `(4, song, 3, 0)`, `song` = 0 to 28. Each item is a complete HMP file (`HMIMIDIP013195`).
- `game/TEST.HMP` is byte-equal to song 10 (item 5602, 9645 bytes).
- `SONGLIST.DAT` is a byte array indexed by the current map number. Each byte is a song number. The file has 46 used bytes (indexes 0 to 45) and `0xff` padding to 63 bytes. The game reads at most `0x3f` bytes.

### Evidence

- `FUN_0001095d` at `0x10999`: `mov eax,4; mov edx,song; mov ebx,3; xor ecx,ecx; call 0x3c92e` (exists), then `0x3f573` (size) and `0x3ecc7` (pointer). It copies the item to `DAT_000704da` and calls `FUN_0005f6ad` (`sosMIDIInitSong`), `FUN_0005fed2` (`sosMIDIStartSong`), and `FUN_00062012(handle, 0x7f)` (song volume).
- Song items start at `file+0x7bd597` (song 0) to `file+0x83b356` (song 17). Every item begins with `HMIMIDIP013195`.
- `FUN_00010010` loads `DATA\SONGLIST.DAT` (`0x70019`) into `DAT_0007ef78`, at most `0x3f` bytes.
- The main loop calls `FUN_00010af6(DAT_0007f260 >> 16)` once per game step (`FUN_00024691`, `game_loop`). `DAT_0007f260 >> 16` is the current map index. It also indexes the 16-byte map records at `DAT_0007f3c4`.
- `SONGLIST.DAT` bytes 0 to 45: `2 17 14 27 4 12 12 18 15 13 12 12 16 6 21 14 17 17 17 17 3 8 17 14 2 23 22 20 17 0 2 2 2 9 2 3 14 16 28 22 19 9 22 3 17 2`.

### Song change

`FUN_00010af6`:

1. `new = SONGLIST[map]`. If `new` equals the current song (`DAT_0007050e`), nothing changes.
2. If no song plays, or a fade is active, the new song starts now.
3. Otherwise a fade starts: the fade counter is `0x7f`. Each call sets the song volume to the counter and decrements it. At counter `1`, the new song starts.
4. `FUN_0001095d(0)` starts no song. Song 0 is never started through this path.

Music master volume, `FUN_00010736(level 0..7, 10)`: `FUN_00060399(l*2 | l<<4)` (0 to 126). Level 0 stops the song.

Confidence: high. reDM2 (`docs/formats.md`, "T=0x04 IS MUSIC") gives the same key, the same 29 items, and the same match of item 5602 with `TEST.HMP`.

Impact on the port: read songs from GDAT `(4, n, 3, 0)`. Port `FUN_00010af6` with the per-step fade.

## 3. SVGA option

### Answer

- The SVGA option changes only the movies. It does not change the game. The game always runs in VGA mode `0x13` (320×200, 256 colors).
- `INSTALL.EXE` asks "Press S for SVGA animations, V for standard VGA". For SVGA it copies `INTRO`, `FTL`, `END`, and `CREDITS`. For VGA it copies `INTRO.VGA`, `FTL.VGA`, and `END.VGA` under the names `INTRO`, `FTL`, `END`, and `CREDITS.PCX` instead of the credits movie. It writes `DM2.BAT` with `IBMIOP SKULL.EXE` and, probably for SVGA only, the switch ` +VS`.
- The movies are Interplay MVE players with appended MVE data. SVGA movies use VESA mode `0x101` (640×480), video 54×40 blocks (432×320 pixels). VGA movies use mode `0x13` with 40×25 blocks (320×200).
- `IBMIOP.EXE` holds two launch scripts. SVGA: `splash intrplay.pcx 3`, `ftl noabort`, `intro 640x350 wide`, game, `end 640x350 wide`, `credits 640x350 wide`. VGA: the same with `noabort` and `splash credits.pcx 10` at the end.

### Evidence

- `INSTALL.EXE` and `IBMIOP.EXE` are LZEXE 0.91 (`LZ91`) files. Decompressed strings: the copy lists, the SVGA prompt, `IBMIOP SKULL.EXE`, ` +VS`, `DM2.BAT`.
- MVE opcode `0x0a` (set video mode) in each movie: `INTRO`, `END`, `FTL`, `CREDITS` = `(640, 480, 0x101)`. `*.VGA` = `(320, 200, 0x13)`.
- `IBMIOP.EXE` (decompressed `0x19fe`): `int 10h` with `AX=0x0013`. `SKULL.EXE` has no `int 10h` and no VESA call.
- `IBMIOP.EXE` script tables at decompressed `0x3f62` (SVGA) and `0x40a0` (VGA).

Confidence: high for the effect. Medium for the exact ` +VS` usage.

Impact on the port: the SVGA option gives no better game image. The port implements no SVGA mode. Movies are out of scope.

## 4. DUNGENB.DAT and GRAPHIC2.DAT

Both files are absent from `game/DATA`. The CD release does not need them.

### GRAPHIC2.DAT

- It is the continuation of `GRAPHICS.DAT` for a split (floppy) distribution. `FUN_0003d1c8` compares the file size of `GRAPHICS.DAT` with the end of the last item. If the file is shorter, it sets the split flags `DAT_0007fa74` and `DAT_0007fa78`.
- `FUN_0003bef6` (GDAT read): if `DAT_0007fa74` is set, it subtracts the size of `GRAPHICS.DAT` (`DAT_0007fa44`) from the item offset and reads the rest from the `GRAPHIC2.DAT` handle (`DAT_0007fac0`).
- `FUN_0003d111` opens `.Z026GRAPHIC2.DAT` (`0x700a5`) only if `DAT_0007fa74 != 0 && DAT_0007fa78 == 0`. The disk labels `SKDISKA`, `SKDISKB`, `SKDISKC` (`0x7051c`) and the disk prompt in `FUN_000111c7` belong to the same floppy logic.
- Our `GRAPHICS.DAT` is complete: the computed end is 8639757 bytes, equal to the file size.

### DUNGENB.DAT

- It is an optional second new-game dungeon. `FUN_00037058` opens `.Z020DUNGENB.DAT` (`0x70082`) if `DAT_000803f8 != 0`. If the open fails, it opens `DUNGEON.DAT`.
- Command `0xd8` sets `DAT_000803f8 = 1` and the new-game flag `DAT_0007f280 = 1`. Command `0xd7` sets only the new-game flag (normal new game). Command `0xd9` clears it (load a game). See `FUN_00021d6c` (`command_execute`).
- The mouse table at `0x72388` binds `0x80d7` and `0x00d8` to the same zone `0x197` (the new-game button), with button masks `0x8002` and `0x4010`. The exact input that matches mask `0x4010` is not verified.
- `DUNGEON.FTL` (`0x70071`) is a similar fallback: the code at `0x37259` opens it as a new game when no save file exists.

### Compare with DUNGEON.DAT and GRAPHICS.DAT

`GRAPHIC2.DAT` holds the bytes after the end of a short `GRAPHICS.DAT`. `DUNGENB.DAT` has the same role as `DUNGEON.DAT` but is a different start dungeon.

Confidence: high for `GRAPHIC2.DAT`. Medium for `DUNGENB.DAT` (the trigger input is not verified).

Impact on the port: support only the complete `GRAPHICS.DAT`. Keep the `DUNGENB.DAT` fallback logic (open it if present, else `DUNGEON.DAT`), or omit command `0xd8`. Both give the same result with the shipped files.

## 5. MIDI to OPL conversion in HMIMDRV.386

### File format

`HMIMDRV.386` is an HMI driver library, not a single program. The first u32 is `0x33`. Each driver has a header with a name, a size, and a device ID, then a flat 32-bit code blob.

| Name offset | Name | Code size | Device ID |
|---|---|---|---|
| `0x2c` | `fmmidi.com` (OPL2) | `0x318c` | `0xa002` |
| `0x31e8` | `fmmidi3.com` (OPL3) | `0x3850` | `0xa009` |
| `0x6a68` | `awemidi.com` | `0xd2b0` | `0xa008` |
| `0x13d48` | `smii.com` | `0x1f7` | `0xa000` |
| `0x13f6f` | `mpu401.com` | `0x2a5` | `0xa001` |
| `0x14244` | `mt32.com` | `0x298` | `0xa004` |
| `0x1450c` | `gusmidi.com` | `0x83e8` | `0xa00a` |
| `0x1c924` | `intspkr.com` | `0x304` | `0xa006` |

`SETUP.INI` maps Sound Blaster and Sound Blaster Pro to `0xa002` (OPL2), and Sound Blaster 16 to `0xa009` (OPL3). The README says that DM2 supports FM music only.

The OPL2 driver code starts at `file+0x5c` (`jmp 0x560`). All absolute data addresses are relative to `file+0x5c`. The entry at `file+0x560` dispatches through a function table at driver offset `0xbb`. The code is compiled C with `ebp` frames.

### Decompilable

Yes. The OPL2 driver is 12.7 KB of plain 32-bit code with no relocations. Load `file+0x5c .. file+0x31e8` in Ghidra as a raw x86-32 binary at base 0. The key routines are already found:

| File offset | Driver offset | Function |
|---|---|---|
| `0x1dc0` | `0x1d64` | OPL register write `(reg, value)`: port from driver `[0x2e5c]`, 6 status reads after the index, 35 after the data. |
| `0xd93` | `0xd37` | BNK packing: converts an AdLib BNK in memory to register bytes. |
| `0x2054` | `0x1ff8` | Instrument to voice: writes the packed bytes to the operator registers. |

The OPL3 driver contains the same BNK packing code (`file+0x3efe`).

### BNK files and register mapping

`MELODIC.BNK` and `DRUM.BNK` use the AdLib Visual Composer BNK layout with the signatures `AMLIB-` and `ANLIB-`:

- `0x08` u16 used = 128, `0x0a` u16 count = 128, `0x0c` u32 name offset = `0x1c`, `0x10` u32 data offset = `0x61c`.
- Names: 128 × 12 bytes (u16 index, u8 flag, 9-byte name). In `DRUM.BNK`, the flag byte holds the drum note (for example `35`, `38`, `41`).
- Data: 128 × 30 bytes (`0x61c + 30*128 = 5404` = file size).

Per 30-byte record, `b[0]` is the percussive flag and `b[1]` the voice number. The modulator fields are `b[2..14]` and the carrier fields are `b[15..27]`, in the order KSL, MULT, FB, AR, SL, EG, DR, RR, TL, AM, VIB, KSR, CON. `b[28]` and `b[29]` are the wave selects.

The packing routine (driver offset `0xd37`) overwrites the record in place. It sets header byte 2 to `'H'` and converts `used − 2` records (126, not 128):

| Packed byte | Formula | Register |
|---|---|---|
| `b[0x0b]` | `AM<<7 \| VIB<<6 \| EG<<5 \| KSR<<4 \| MULT` (`b[0xb] b[0xc] b[7] b[0xd] b[3]`) | `0x20 + mod` |
| `b[0x02]` | `KSL<<6 \| TL` (`b[2] b[0xa]`) | `0x40 + mod` |
| `b[0x05]` | `AR<<4 \| DR` (`b[5] b[8]`) | `0x60 + mod` |
| `b[0x06]` | `SL<<4 \| RR` (`b[6] b[9]`) | `0x80 + mod` |
| `b[0x0e]` | `FB<<1 \| CON` (`b[4] b[0xe]`, CON not inverted) | `0xC0 + channel` |
| `b[0x18]` | `AM<<7 \| VIB<<6 \| EG<<5 \| KSR<<4 \| MULT` (`b[0x18] b[0x19] b[0x14] b[0x1a] b[0x10]`) | `0x20 + car` |
| `b[0x0f]` | `KSL<<6 \| TL` (`b[0xf] b[0x17]`) | `0x40 + car` (shadowed, velocity applied later) |
| `b[0x12]` | `AR<<4 \| DR` (`b[0x12] b[0x15]`) | `0x60 + car` |
| `b[0x13]` | `SL<<4 \| RR` (`b[0x13] b[0x16]`) | `0x80 + car` |
| `b[0x1c]`, `b[0x1d]` | wave select | `0xE0 + mod`, `0xE0 + car` |

The operator offsets per channel come from the byte pairs at driver `[0x2ee5 + 2*ch]` (modulator) and `[0x2ee6 + 2*ch]` (carrier). `SKULL.EXE` passes both files to the driver unchanged: `FUN_0005f4ec(handle, 1, data)` at `0x10310` and `0x1035a`.

Not yet analyzed: the note to F-number table, the velocity and volume scaling of the carrier TL, the voice allocation, and the drum handling. These are in the same driver and belong to Step 19.

### Standard AdLib fallback

If Step 19 does not port the driver logic, use the standard AdLib mapping: the same register formulas as in the table above, 9 melodic voices, F-number from the MIDI note with block = octave, carrier TL scaled by velocity and channel volume, and drum notes played as melodic voices with the instrument from `DRUM.BNK[note]` at the pitch from its name flag byte.

Confidence: high for the file format, the BNK layout, and the register mapping. The driver is decompilable.

Impact on the port: decompile the OPL2 driver (`fmmidi.com`) for Step 19 and port its note, volume, and voice logic. The BNK packing is fully specified above. Decide which device the port reproduces: OPL2 `0xa002` (Sound Blaster, Sound Blaster Pro) or OPL3 `0xa009` (Sound Blaster 16, the DOSBox default).

## 6. PIT divisor and tick rates

### Answer

- `SKULL.EXE` programs PIT channel 0 with divisor `4971` (`0x136b`): 1193180 / 4971 = 240.02 Hz.
- `IBMIOP.EXE` programs the same divisor `0x136b`.
- Music: the HMP song timer event runs at 120 Hz. One HMP delta tick = 1/120 s at tempo 100 %.
- Digital driver service event: 120 Hz.
- Game time: `IBMIOP.EXE` calls the game timer callback (`INT 2Dh`, handler `FUN_00010cc2`) on every 4th of its timer interrupts: nominal 60 Hz. Each call adds 1 to `DAT_0007f256`. A game step is due when the sum reaches `DAT_0007f268` = 8. Nominal game step rate: 7.5 Hz (133 ms). `DAT_0007f268 = 1` in one special mode (`FUN_00021d6c`, `command_execute`).

### Evidence

PIT programming in the HMI timer library, `FUN_00062efd` and `FUN_00062f43`:

```text
00062f1e: 66ba4300   mov dx, 0x43
00062f22: b036       mov al, 0x36        ; channel 0, lo/hi, mode 3
00062f24: ee         out dx, al
00062f25: 66ba4000   mov dx, 0x40
00062f29: 8b4508     mov eax, [ebp+8]    ; divisor
00062f2c: ee         out dx, al
00062f2d: 8ac4       mov al, ah
00062f2f: ee         out dx, al
```

- `FUN_00010010` calls `FUN_0005ae04(0xf0, 0, 0)` (`sosTIMERInitSystem(240)`): divisor = `0x1234dc / 240` = 4971. It also registers a chain event to the old `INT 8` handler at the BIOS rate (`0xff00`).
- `FUN_0005af1a` (`sosTIMERRegisterEvent`) lowers the divisor only for a rate above the current rate. Registered rates: `0x78` (120 Hz, digital driver, `FUN_00010010`) and `song[+0x38]` (`sosMIDIStartSong`, `FUN_0005fed2`). `FUN_0005f52b` registers 1500 Hz, but nothing calls it.
- HMP header of every game song and of `TEST.HMP`: `+0x30` u32 tracks, `+0x34` u32 `0xc0`, `+0x38` u32 `0x78` (120), `+0x3c` u32 `0x7e` (126 s). The longest `TEST.HMP` track has 15211 delta ticks = 126.8 s at 120 Hz.
- `IBMIOP.EXE` (decompressed `0x2c58`): `mov word [bp-2], 0x136b` → `cs:[0x125e]`. Its `INT 8` handler at `0x2c73` writes `0x36`, then the divisor to ports `0x43` and `0x40`. Every 4th interrupt (`and al, 3`) it calls the registered far callback `[0xa9d0]`. That callback (`0x3007`) executes `int 2Dh`.
- `SKULL.EXE` `FUN_0005a8b1`: sets vector `0x2d` to `FUN_00010cc2`, then `int 0FCh` with `EAX=0x0f`. `IBMIOP.EXE` function `0x0f` stores the callback and returns 1. `FUN_00010d0f` first calls `int 0FCh` function `0x2a`, which reinstalls the `IBMIOP.EXE` timer handler.
- `FUN_00013887` (frame wait) polls VGA port `0x3da` bit 3. It does not use the PIT.

### Open measurement

Both programs hook `INT 8`: the HMI handler in protected mode, the `IBMIOP.EXE` handler in real mode. The HMI handler calls the old handler only at about 18.2 Hz. Thus the real rate of the `INT 2Dh` callback depends on how DOS/4GW routes `IRQ 0`. Static analysis cannot fix it. Measure it in DOSBox: compare the game clock in two saves with a known real time between them.

Confidence: high for divisor 4971 and the 120 Hz music tick. Medium for the 60 Hz and 7.5 Hz game rates.

reDM2 findings (`docs/memory.md`, "PART 1 — determinism audit", and the `IBMIOP.EXE` driver notes) agree and add these facts:

- `IBMIOP.EXE` function `0x0f` ends with `mov ax,1; retf`. Thus `g_timer_step` (`0x7efac`) is 1: each callback adds 1 tick.
- The tick from the keyboard poll (function `0x02`) runs only with the `IBMIOP.EXE` option `+D`. `DM2.BAT` passes `+VS`, so the tick comes from the `INT 8` handler on every 4th interrupt. This agrees with our reading.
- Function `0x30` (shutdown) clears the flag that function `0x0f` sets. After it, no tick arrives.
- Game code reads no clock: all `int 21h` calls are in the Watcom library, and there is no `int 1Ah`. `g_game_time` (`0x7f22c`) counts game steps. Timeline event times are game steps (24 bits).
- reDM2 did not measure the real rate either. The DOSBox measurement stays open.

Impact on the port: set `PIT_DIVISOR` in `port/src/platform.c` to `4971` (240.02 Hz). Derive the game tick as every 4th PIT tick (60 Hz) and the game step at 8 game ticks. Drive the HMP player at 120 Hz. Confirm the game rate with the DOSBox measurement.

## Other finding: IBMIOP.EXE is a required runtime layer

`DM2.BAT` starts `IBMIOP SKULL.EXE`. `IBMIOP.EXE` is a real-mode resident layer that `SKULL.EXE` calls through `int 0FCh` with a shared buffer (`DAT_0008048c`). It sets video mode `0x13`, writes the palette (port `0x3c8`), reads `skull.cfg`, handles the keyboard and timer, and runs the movie script. `SKULL.EXE` uses 34 call sites with function numbers `0x00` to `0x30` and `0x103` to `0x10a` (for example `0x0f` timer callback, `0x2a` timer reinstall, `0x2e`, `0x2f`, `0x30` at start and exit).

Impact on the port: `docs/skull-map.md` lists these `int 0FCh` functions. The platform layer replaces them (video, palette, input, timer).

### IBMIOP service details from reDM2

Source: reDM2 `docs/memory.md` (driver dispatcher, functions `0x0e`, `0x12`, `0x14`, `0x2e`, `0x2f`, `0x30`) and its comments on `0x10010`, `0x13898`, `0x14cb1`. reDM2 read the unpacked `IBMIOP.EXE` code. These facts add to or correct the table in `docs/skull-map.md`:

- The dispatcher accepts `EAX` from `0x00` to `0x30`. 28 functions do work. 21 numbers go to one exit that does nothing. Bit `0x100` selects a second table (the `0x103` to `0x10a` functions).
- `int 0FCh` keeps all registers. All results go through the shared buffer (`DGROUP:0x2342` in `IBMIOP.EXE`, `g_iop_buffer` in `SKULL.EXE`).
- `0x0e`: the first buffer word is an input. `0` gives the live pointer position. Other values give the position at the last event. The result is x, y, and a third word. reDM2 calls the third word "cursor height". We call it the button state (`g_mouse_buttons`, `0x7f0ae`). See the open conflicts in `docs/reference-crosscheck.md`.
- `0x12`: the driver does nothing. `SKULL.EXE` then reads buffer word 0, which the driver keeps up to date. Our name `disk_get_drive_status` stays, because the game uses that word as the drive status.
- `0x14`: the driver first writes a zero byte. If the drive has no volume label, the result is an empty string.
- `0x2e`: the result is the linear address of a 6-word sound configuration record, filled from `SKULL.CFG`: `+0` digital device ID, `+2` IRQ, `+4` DMA, `+6` port, `+8` MIDI device ID, `+0x0a` MIDI port. `sound_init` stores the address at `0x7ef2c`.
- `0x2f`: the result is `{linear address, size}` of a free DOS memory block. `system_init` adds it as one more memory pool.
- `0x30`: shutdown. It also stops the game timer callback.
- `0x10a`: `SKULL.EXE` sends 1024 bytes from GDAT item `(1,0,9,254)` and stores the first result word in `0x7f07c`. We read it as the palette upload (256 × 4 bytes). reDM2 reads it as a cursor image upload and marks this as an inference. This stays open.

## 7. Save file format (Step 10)

Source: reDM2 `docs/savegame.md`. We checked the call sequence in `game_load` (`0x370d2`) in `decomp/SKULL.c`. reDM2 verified the format by running the original load path in an emulator over three real saves; the bit stream ends exactly at the last byte of each file.

### Container

| Part | Size (reDM2 saves) | Content |
|---|---|---|
| 1 | 42 bytes | Save header |
| 2 | 43443 bytes | Dungeon image in the `DUNGEON.DAT` section format |
| 3 | rest of file | Bit-packed game state |

- Header: `+0x00` u16 = 1 (format marker), `+0x02` 16 bytes save name (`"GAME 0"`, NUL padded). The meaning of `+0x12` to `+0x29` is not known.
- `game_save` (`0x3502b`) first reads the old header, then changes only `+0x00` and the name, and writes it back. All other header bytes come from the previous save. The port must keep them byte for byte.
- `game_load` reads the 42 bytes, sets the dungeon file base `0x7f8e8` to `0x2a`, and calls `dungeon_load(0)`.
- The dungeon image stores counts that already include the load slack: tile roots + 300, and the per-thing-type slack table at `0x71818` (`0 0 0 0 75 100 60 0 12 5 200 0 0 0 60 50`). Thus the save path calls `dungeon_load` with 0, and `DUNGEON.DAT` with 1.
- The dungeon image has no trailing 16-bit checksum. `DUNGEON.DAT` has one.

### Bit-packed records (part 3)

`save_read_packed(dest, masks, size, count)` (`0x34536`) reads `count` records of `size` bytes. Each record byte has one mask byte. Only the bits set in the mask are stored, most significant bit first. The bit position continues from one call to the next. `save_write_packed` (`0x343be`) is the writer. `g_save_bit_total` (`0x7f8dc`) counts the stored bits.

| Order | Destination | Masks | Size × count |
|---|---|---|---|
| 1 | Local party and game record (to globals) | `0x75316` | 60 × 1 |
| 2 | `g_global_flags` (`0x7f100`) | `0x75312` | 1 × 8 |
| 3 | `g_global_bytes` (`0x7f0c0`) | `0x75312` | 1 × 64 |
| 4 | `g_global_words` (`0x7f108`) | `0x75312` | 2 × 64 |
| 5 | `g_champions` (`0x7fbd0`) | `0x75352` | 263 × `g_champion_count` |
| 6 | `g_magic_light` (`0x7ffec`) | `0x75459` | 6 × 1 |
| 7 | `g_timeline_events` (`0x760e8`) | `0x7545f` | 12 × `g_timeline_event_count` |
| 8 | Things, through `save_read_dungeon_things` (`0x35b97`) | `g_save_thing_masks` (`0x754d7`) | per thing type |

- Record 1: fields go to `g_game_time` (`0x7f22c`), `g_random_seed` (`0x70518`), `g_party_x`, `g_party_y`, `g_party_dir`, `g_party_map`, `g_champion_count`, `g_timeline_event_count`, and other globals.
- Record 5 does not store the hand and inventory slots (`+0xaf` to `+0xf0`). Record 8 rebuilds them.
- Record 7 stores timeline bytes `+0` to `+9`. Bytes `+0x0a` and `+0x0b` are not stored.
- Record 8 walks the squares and their thing lists. For each thing it reads the thing record with the mask of its type. The masks never store bytes 0-1 (the link word). Thing types 1 and 11-13 have no mask and are not stored.
- Record 8 first unlinks and frees all things of types 4 to 15. Then it creates them again from the bit stream. Types 0 to 3 keep their slots.

### Impact on the port (Step 10)

- Port the reader and writer from the mask tables. Do not write a separate format description in C.
- The byte-equal test of Step 10 needs care: after a load, things of types 4 to 15 can sit in other slots than in the file (reDM2 measured 492 of 941). A load followed by a save can thus give a different dungeon image. Compare the C writer with the original writer run in `dm2tools.emu` on the same memory state, and compare a C load with the original load by content.
- Keep the unknown header bytes.

### Measurement: original load, then original save (Step 10)

Method (`tests/test_save.py`, `original_round_trip`): `dm2tools.emu` runs the file part of `game_load` (`0x372d6` to `0x37586`: header, `dungeon_load(0)`, the packed records, `timeline_build_queue`, `save_read_dungeon_things`, `save_restore_timed_effects`, `timeline_relink_events`). Then it runs the file part of `game_save` (`0x35236` to `0x35674`: header, dungeon image, `creature_deactivate_all_groups`, `timeline_compact`, the packed records, the things, the last bits). The header buffer of `game_save` gets the header of the input file, as in a save into the slot that the game was loaded from. Stubs: `mem_alloc`, `weather_init`, `dungeon_read_level_table`, the file I/O wrappers, and `gdat_entry_index` (answered by the C `gdat_find`).

Result for `tests/saves/start.DAT` (50452 bytes, 1 champion, 3 timeline events):

| Part | Bytes that differ |
|---|---|
| Header, dungeon header, map descriptors, column table, text, map data | 0 |
| First-thing table | 357 |
| Thing records of types 1, 2, 3 (link words) | 5, 93, 56 |
| Thing records of types 4 to 10 | 1816, 490, 481, 15, 244, 115, 620 |
| Bit-packed state | 0 |

- The original round trip is not byte-equal: 4292 of 50452 bytes differ, all in the dungeon image. The file size and the bit stream stay equal. The load creates the groups and items again in the first free records, so the first-thing table and the link words change.
- A second round trip of the original output is byte-equal (a fixed point).
- The C port gives the same bytes as the original writer for `start.DAT`, and for the original output.

### Details found while porting (Step 10)

- Values between the records (the more bit, 4 bits type, 2 bits cell, 7 bits creature type, 2 bits container kind, 9 bits sensor data, 10 bits record position) go through `save_write_packed` as a little-endian word with a little-endian mask. A 9-bit value thus stores bits 7 to 0 first, then bit 8.
- Game record (60 bytes, mask `0x75316`): `+0x00` game time (24 bits), `+0x04` random seed (only bits 0 to 15 are stored), `+0x08` champion count, `+0x0a`/`+0x0c`/`+0x0e`/`+0x10` party x, y, direction, map, `+0x12` leader, `+0x14` timeline event count, `+0x16` and `+0x1a` game times (`0x716a0`, `0x7f19c`), `+0x1e` party move cooldown, `+0x20` and `+0x22` (`0x7f25a`, `0x7f270`), `+0x24` one bit that the writer always sets to 0, `+0x28` sound volume (bits 0 to 3) and music volume (bits 4 to 7), `+0x2a` weather visible, `+0x2e` to `+0x37` weather and storm bytes, `+0x38` next hour time.
- Thing list (`save_write_thing_list`, `save_read_thing_list`): things of types 4 to 15 start with a 1 bit and the type; a 0 bit ends the list. A list with a single thing (champion slot, leader hand, projectile payload, event group) has no end bit when it holds a thing, and also none when it is `THING_ENDOFLIST`.
- Creature groups whose creature info has flag bit 0 (static creatures, no active group) use the mask at `0x7548b` and store the cell of each possession. Minion maps (container kind 1) use the mask at `0x754b3`. A misc item inside a moneybox uses the mask at `0x754bf`. A projectile inside a creature group uses the mask at `0x754cf`. Minion maps and projectiles in groups are deferred: their link is stored after the squares as the position of the target group or container in the bit stream.
- A teleporter square whose target is a teleporter with a link sensor (type `0x27`) stores no square bits. When the target map is lower, the things of the square are stored with the target square, not here.
- Event types with a thing: `0x19` explosion (event `+8`), `0x1d`/`0x1e` projectile move (event `+6`), `0x3c`/`0x3d` move group (event `+8`, a single-thing list that only loads when the header format word is not 0).
- Header: `game_save` reads the old header from the `.BAK` file (the old `.DAT`, renamed just before). Without a `.BAK` file only `+0x26` to `+0x29` are set to 0; the other bytes are uninitialized stack. The name is copied with `str_copy`: the bytes after the end mark keep the old header bytes.
- The original treats a short read at the end of the file as success, so it does not find a truncated bit stream.

### Deviations of the C port (`port/src/save.c`)

- Without an old file, the header bytes after the name are 0 (the original writes uninitialized bytes).
- Damaged files give an error code: a truncated file, a dungeon image with a bad first-thing table or broken thing lists, more than 4 champions, more timeline events than room, an event or record index out of range, no free record for a new thing, a full first-thing table, more than 100 deferred things. The dungeon is then empty.
- Not ported yet, because they do not change the file bytes: the item effects of the champion slots (`champ_apply_item_effects`, inventory), `save_restore_timed_effects` and `champ_compute_load` (magic, champions), the champion part of `timeline_relink_events`, `creature_deactivate_all_groups` (no active groups exist in the port yet), and the `.BAK` fallback of the load slot dialog. The projectile part of `timeline_relink_events` is ported.
- When all records of a type are in use, the original reuses a discarded item (`dun_get_discarded_thing`); the port reports an error.
- The creature info flags (`0x71968`, 63 records of 36 bytes, word `+0`) are copied into `save.c` until the creature module reads them.
