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
- Step 12: the 24 image items with other gates are 4-bit images. The gate (top 6 bits of word 1) holds the y offset and the top 6 bits of word 0 hold the x offset of the image (`gdat_get_image_offset`, `0x3eed3`). An 8-bit image holds its offset in bytes 4 and 5. When the top 6 bits of word 0 are `-32`, the offset is the value key `(category, index, 0x0c, entry)`. All 827 4-bit images decode byte-equal.
- An uncompressed image holds u16 bits per pixel (4 or 8) at item + 4 and the pixels at item + 10. A 4-bit image (compressed or not) ends with its 16-byte color map.
- The palette item `(1,0,9,254)` has 256 entries of 4 bytes: color index, then 8-bit red, green, blue. The VGA DAC takes 6-bit values, so the port shifts each value right by 2 (`gdat_palette_color`).
- The layout item `(1,0,4,0)` is raw bytes: u16 magic `0xfc0d`, u16 group count, group ranges, then `{anchor type, parent, x, y}` records (`port/src/zone.h`). `zone_unpack_layout` (`0x19d0e`) reads it.

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

### Deviations of the C port (`port/src/sound.c`, Step 18)

- Pan ratio overflow fixed. `FUN_00010877` divides `|lateral| << 11` by `|forward|` in 32 bits, but compares only the low 16 bits with `THRESH`. For `|lateral| >= 32` and a small `|forward|`, the ratio wraps and the sound pans near the center (example: lateral 32, forward 1 gives `0x8f00`, not `0xf800`). The port compares the full ratio. The differential test skips these inputs and checks the fix separately.
- Mixing: the port mixes at 44100 Hz (4 × 11025) in interleaved signed 16-bit stereo. Each sample repeats 4 times. SDL3 converts to the device rate.
- Pan law: the SOS pan curve is not known. The port uses a linear balance with the center at full volume on both sides: left gain = `min(0x8000, 0x10000 − pan) / 0x8000`, right gain = `min(0x8000, pan) / 0x8000`, times the SOS volume `vol8 << 7 | vol8 >> 1`.
- Voices: 64, as in the original. A new sound takes the first free voice. When all play, the sound is dropped. The priority sort of `FUN_00010573` is not ported yet; it belongs to the request queues.
- The port keeps the samples signed and skips the XOR `0x80` of `FUN_000104ea`.
- `sound_party_offset` takes the party direction 0 to 3. For other values the original keeps stale locals; the port treats them as north.
- The audio output (`port/src/platform.c`) fills the SDL audio stream from `plat_audio_pump` on the main thread, up to 2048 frames ahead. A music source can be added with `plat_audio_music`. It is mixed before the sound effects.

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

### HMP player (Step 19)

Source: `hmi_midi_init_song` (`0x5f6ad`), `hmi_midi_song_tick` (`0x65a3b`), `hmi_midi_send_event` (`0x5e489`), `hmi_midi_send_track_setup` (`0x66fb8`). Port: `port/src/hmp.c`.

- Header: `+0x20` u32 offset of the loop table, `+0x30` u32 track count, `+0x38` u32 song timer rate (120 Hz). Track chunks start at `+0x388`: u32 track number, u32 chunk size, u32 MIDI channel, then the data.
- Delta times: 7-bit groups, low group first. The last byte has bit 7 set (the opposite of standard MIDI).
- Timing: each tick adds 1 to the elapsed ticks of each track. A track plays its next event when its delta is not more than the elapsed ticks. After each event, the elapsed ticks start again at 0. Thus the first event plays at tick `max(d, 1)` (`d` = first delta), and each later event plays its delta after the event before it. An event with time `T` (sum of the deltas) plays at tick `T + 1` if the first delta is 0, else at tick `T`. Tracks play in track order within a tick.
- Track 0 events are not sent. Controllers `0x67` to `0x79` are HMI commands and do not go to the driver. The songs use `0x6c` (no effect), `0x6d` (the value of the next event, an `0x6e` controller, sets the loop count of the loop point with id = value), and `0x6f` (loop). `0xff 0x2f` ends a track.
- Loop table: one record count per track, then 24-byte records of all tracks: `+0` u32 loop point in the track data, `+4` id, `+5` program, `+6` loop count (`0xff` = forever), `+7` byte count of controller pairs, `+0xc` u32 offset of the pairs.
- Loop `0x6f id`: if the count is not 0 or `0xff`, it decrements. If the count is not 0, all tracks 1 and up that still play jump to their loop point with the same id, read the delta there, and get a program change and the controller pairs. Later tracks count the loop tick as their first tick. All 29 songs loop forever.
- `hmi_midi_init_song` writes 1 to the 128 bytes at song `+0x300` (enable flags of the controllers `0x67`/`0x68`). This also sets `+0x36c`, the flag of the program change at a loop point. Thus the loop point always sends its program.
- Channel volume (`0x07`): the player stores the value per channel and sends `master * (value * song >> 7) >> 7`, or 0 when the song is muted. Song volume, mute, unmute, pause, and master volume send the stored volumes of the playing tracks again.
- All tracks of all songs play on the FM driver: their device lists are empty or contain `0xa002`, and the track map `0x70378` gives device 0 to tracks 16 to 31.

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

Step 19 analyzed the rest of the driver. See "OPL2 driver logic (Step 19)" below.

### Standard AdLib fallback

If Step 19 does not port the driver logic, use the standard AdLib mapping: the same register formulas as in the table above, 9 melodic voices, F-number from the MIDI note with block = octave, carrier TL scaled by velocity and channel volume, and drum notes played as melodic voices with the instrument from `DRUM.BNK[note]` at the pitch from its name flag byte.

Confidence: high for the file format, the BNK layout, and the register mapping. The driver is decompilable.

Impact on the port: decompile the OPL2 driver (`fmmidi.com`) for Step 19 and port its note, volume, and voice logic. The BNK packing is fully specified above. Decide which device the port reproduces: OPL2 `0xa002` (Sound Blaster, Sound Blaster Pro) or OPL3 `0xa009` (Sound Blaster 16, the DOSBox default).

### OPL2 driver logic (Step 19)

Decompiled with `tools/ghidra/decompile.sh` (without `Watcall.java`: the driver uses `cdecl`) from the driver bytes `file+0x5c .. file+0x31e8` at base 0. Driver offsets below. Port: `port/src/fm.c`. The port reproduces the OPL2 driver `0xa002`.

| Offset | Function |
|---|---|
| `0x80d` | MIDI dispatch: note off, note on (velocity 0 = note off), controller, program change, pitch bend (high byte only). Aftertouch and channel pressure are ignored. |
| `0xf6b` | Note on `(note, velocity, channel)`. |
| `0x157b` | Note off; with sustain on, the note goes to a 32-entry queue per channel. |
| `0x1846` | Controllers: `7` volume, `0x40` sustain (0 plays the queued note offs), `0x66` pitch bend range, `0x79` reset, `0x7b` all notes off. |
| `0x19bb`, `0x1a7d` | Pitch bend and the bent frequency. |
| `0x1ed7` | Voice allocation. |
| `0x201e` | Instrument to voice. |
| `0x237f` | Channel volume change. |

- Note on: take a voice (first free voice, else the first voice of the first channel without pitch bend, else `channel mod 9`), key it off, write the release byte with release rate 15 five times (fast release time), load the instrument (drums: `DRUM.BNK[note]`; else `MELODIC.BNK[program]`), set the levels, write the frequency, key off and key on. Drums sound at the note in the flag byte of their name.
- Frequency: a table of block and F-number per note from note 12 at driver `0x2f4c`. Notes 0 to 11 read the attenuation table in front of it.
- Level: `level = ((volume << 7) / 127) * velocity >> 7`, `a = table_0x2f00[level >> 1]`, `TL = 64 - (64 - a) * (64 - instrument TL) / 64` with the instrument KSL. The modulator gets it too when the melodic instrument of the channel program is additive (also on the drum channel).
- Start state: channel volume 127, pitch bend 64, bend range 2, program 0, voice velocity 127.
- The sustain bytes of operators 16 to 21 share memory with the low F-number bytes of voices 0 to 5. Controller `0x79` sets the velocity of the voice with the number of the channel.
- Differential test (`tests/test_music.py`): the driver runs in Unicorn and gets the same events as the port. All 115842 note, controller, program, and bend events of the 29 songs give the same OPL register writes. The test uses the first 1000 events of each song and a list of edge events.

### Deviations of the C port (Step 19)

- OPL chip: Nuked-OPL3 in OPL2 mode, register writes through its write buffer (2 samples per write, like the delay of the real chip). Output: 44100 Hz stereo, the same signal on both sides.
- `fmmidi.com` computes `(note - 12) mod 12` with repeated subtraction. For a note below 12 with pitch bend, this takes about 1.4 billion instructions in the timer interrupt (song 7 has one such note). The port computes the same value at once.
- The frequency table index is clamped to the table; the driver reads outside it for notes below the bend range. MIDI data bytes are cut to 7 bits before the driver tables use them.
- A loop to an id that a track does not have does not move that track (the original searches past the loop records). A track that has no end event ends at its chunk end. A map outside `SONGLIST.DAT` keeps the song (the original reads past the buffer).
- Not ported, because no song uses them: the HMI controllers `0x67`/`0x68` (controller enable flags), `0x72`, `0x75`, `0x76`, `0x79` (other loops), `0x77` (trigger callbacks), the HMI fade (`hmi_midi_fade_song`), tempo change, and the channel remap mode (the game turns it off). Like the original, the port does not send these controllers to the driver, but it does not run them.
- `music_render` (Step 19) is not yet connected to SDL audio. The connection, a lock between the audio thread and `music_update_for_map`, and the calls from the game loop (`music_update_for_map` once per game step, `music_pause` on the pause command) come after the merge with Step 18.

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
- A second test (`link_things` in `tests/test_save.py`) changes the loaded state the same way in C and in the emulator memory: a group in a move-group event, a minion map linked to another group, a projectile, an explosion, and a gap in the timeline. The C writer gives the same bytes as the original writer. The group in the event gets another bit stream position than its record index, so the deferred links use the written positions.

### Details found while porting (Step 10)

- Values between the records (the more bit, 4 bits type, 2 bits cell, 7 bits creature type, 2 bits container kind, 9 bits sensor data, 10 bits record position) go through `save_write_packed` as a little-endian word with a little-endian mask. A 9-bit value thus stores bits 7 to 0 first, then bit 8.
- Game record (60 bytes, mask `0x75316`): `+0x00` game time (24 bits), `+0x04` random seed (only bits 0 to 15 are stored), `+0x08` champion count, `+0x0a`/`+0x0c`/`+0x0e`/`+0x10` party x, y, direction, map, `+0x12` leader, `+0x14` timeline event count, `+0x16` and `+0x1a` game times (`0x716a0`, `0x7f19c`), `+0x1e` party move cooldown, `+0x20` and `+0x22` (`0x7f25a`, `0x7f270`), `+0x24` one bit that the writer always sets to 0, `+0x28` sound volume (bits 0 to 3) and music volume (bits 4 to 7), `+0x2a` weather visible, `+0x2e` to `+0x37` weather and storm bytes, `+0x38` next hour time.
- Thing list (`save_write_thing_list`, `save_read_thing_list`): things of types 4 to 15 start with a 1 bit and the type; a 0 bit ends the list. A list with a single thing (champion slot, leader hand, projectile payload, event group) has no end bit when it holds a thing, and also none when it is `THING_ENDOFLIST`.
- Creature groups whose creature info has flag bit 0 (static creatures, no active group) use the mask at `0x7548b` and store the cell of each possession. Minion maps (container kind 1) use the mask at `0x754b3`. A misc item inside a moneybox uses the mask at `0x754bf`. A projectile inside a creature group uses the mask at `0x754cf`. Minion maps and projectiles in groups are deferred: their link is stored after the squares as the position of the target group or container in the bit stream.
- A teleporter square whose target is a teleporter with a link sensor (type `0x27`) stores no square bits. When the target map is lower, the things of the square are stored with the target square, not here.
- Event types with a thing: `0x19` explosion (event `+8`), `0x1d`/`0x1e` projectile move (event `+6`), `0x3c`/`0x3d` move group (event `+8`, a single-thing list that only loads when the header format word is not 0).
- Header: `game_save` reads the old header from the `.BAK` file (the old `.DAT`, renamed just before). Without a `.BAK` file only `+0x26` to `+0x29` are set to 0; the other bytes are uninitialized stack. The name is copied with `str_copy`: the bytes after the end mark keep the old header bytes.
- The original treats a short read at the end of the file as success, so it does not find a truncated bit stream.
- A damaged dungeon image in which two squares share a thing list (other than a linked teleporter) can load without an error. The written file then does not load again: the things of the shared list are stored under both squares. The original has the same weakness. The C port does not check this yet.

### Deviations of the C port (`port/src/save.c`)

- Without an old file, the header bytes after the name are 0 (the original writes uninitialized bytes).
- Damaged files give an error code: a truncated file, a dungeon image with a bad first-thing table or broken thing lists, more than 4 champions, more timeline events than room, an event or record index out of range, no free record for a new thing, a full first-thing table, more than 100 deferred things. The dungeon is then empty.
- Not ported yet, because they do not change the file bytes: the item effects of the champion slots (`champ_apply_item_effects`, inventory), `save_restore_timed_effects` and `champ_compute_load` (magic, champions), the champion part of `timeline_relink_events`, `creature_deactivate_all_groups` (no active groups exist in the port yet), and the `.BAK` fallback of the load slot dialog. The projectile part of `timeline_relink_events` is ported.
- When all records of a type are in use, the original reuses a discarded item (`dun_get_discarded_thing`); the port reports an error.
- The creature info flags (`0x71968`, 63 records of 36 bytes, word `+0`) are copied into `save.c` until the creature module reads them.

## 8. Dungeon view (Step 12)

### Blitters (`gfx_blit` and its row functions)

- `gfx_blit` selects a row function by the source depth: a 4 bpp source goes to a 4 bpp or 8 bpp destination, an 8 bpp source always goes to an 8 bpp destination (mapped through the colormap when it is not `NULL`). Offsets are pixels; a 4 bpp row holds an even pixel count.
- Mirror 1 reads each row of the source window backwards, mirror 2 reads the rows from the bottom up, mirror 3 does both. A value above 3 works as 0.
- The transparent value is compared with the source pixel before the colormap.
- The mirrored 4 bpp to 4 bpp rows with a transparent color have quirks at the row ends: for example, the mirrored row writes its last pixel only when it is the transparent color. The port draws these rows like the other blits. The view does not use this case; text drawing (Step 13) must check it.
- The 8 bpp scaler steps in 1/256 pixel from half a step; the 4 bpp scaler steps in 1/128 pixel from half a step. Both copy the previous destination row when the source row repeats.
- The masked tiled blit (`gfx_blit_tiled_4to8_masked`) skips the masked pixels at the start and at the end of each row; the tile advances for the skipped start pixels, not for the skipped end pixels.

### Layout zones (`zone_compute_rect`)

- A zone record is `{anchor type, parent, x, y}`. The parent chain ends at a zone with parent 0. A size zone (type 9) gives a width and height; the zones under it are placed in it and clipped to it.
- The image size comes from `*w`, `*h`, or the image header. A zone with bit 15 (`ZONE_RELATIVE`) adds `*w` and `*h` to its position. On return, `*w` and `*h` hold the pixels clipped at the left and the top: the source offset of the blit.
- `g_zone_clip_rect` (`0x7f1c8`) clips the next zones while its width is not 0. `zone_set_clip_margins` sets it to the viewport minus margins; the view sets its width to 0 after each picture.
- While the party walks (`0x7f1d4` and `g_party_move_steps`), zones of parent 3 clip to the walking view `{21, 8, 182, 110}`.

### View square record and the view

- View square (18 bytes, 23 records): `+0`/`+1` map x, y; `+2` aspect (0 wall, 1 floor, 2 open pit, 5 visible teleporter, 7 hidden, `0x10` door seen from the side, `0x11` door front, `0x12` stairs front, `0x13` stairs side); `+4` square byte; `+6` first thing that is not a wall thing; `+8` to `+0x0e` by aspect: the wall ornaments of the faces (`frame << 10 | ornament`, front face at `+0x0c`), or door state and door thing, or the up bit of stairs and pits, or the floor ornament at `+0x0c` (`frame * 10 << 8 | ornament`); `+0x10` wall text thing or portrait index.
- Squares away from the map (`dun_get_square`): a wall with the bit of its face toward the map (`8 >> direction`, the random ornament bits of a wall) next to an open edge square, else `0xe0`.
- Random wall ornaments: hash `((((x * 0x20 + 2000 + (((dir + side) & 3) + 1) * (y + 1)) * 0x7ab9) >> 1) + (map * 0x40 + 3000 + width + height) * 11 + dungeon header word 0) >> 2) % 30`; a value below the random ornament count picks an ornament of the party map's wall ornament list.
- Pictures go through `view_setup_picture`: GDAT image, scale (1/64), offset, layout zone, and a color remap. The remap is the depth shading table `(8, graphics set, 7, depth)` when it exists, else a dimming by the depth light; the view shade (light level * 10) dims both.
- Doors with an ornament or damage are put together in an offscreen bitmap of the door size (`view_render_picture_offscreen`), then drawn. A door in states 1 to 3 draws two halves that slide apart, unless bit 5 of the door record is set.
- Wall text plates: the text goes into a 4 bpp bitmap of the plate size with the 8 x 8 font `(8, graphics set, 1, 3)`, lines centered, then the bitmap is scaled and drawn like the plate.
- Outdoor maps: sky objects (GDAT category `0x17`) are text entries with fields `cd` (zone), `fw` (flip kind), `mv` (0 fixed, 1 landmark), `xl`, `yl` (landmark position on the level), `fd` (fade distance). A landmark is scaled by its distance; its screen offset is `lateral * 210 / distance`. Like the original, the landmarks use the first sky object record as their work record.

### Deviations of the C port (Step 12a)

- No image cache: each GDAT image is decoded once and kept; scaled images and offscreen bitmaps live until the end of the frame. The original cache key of a scaled image keeps only 5 bits of the height scale and 7 bits of the width scale, so two scales can share a cached image; the port always scales anew.
- `gdat_exists` is true for every key of the file. The original also says no for an image that the map did not preload. The view checks only images of the map graphics set, which the original preloads.
- Text entries: `gdat_text` decrypts the text but does not expand the `.Z` placeholders of `text_format` (Step 13).
- Fixes: `view_unhide_linked_squares` read far past the view squares for missing neighbours; `view_draw_ceiling_pit` read the square above without a bounds check; `view_setup_picture` divided by 0 for a width scale key with a low byte of 0; `view_get_shaded_palette` wrote a 256-color table into a 16-byte buffer; `dun_is_open_square_raw` read the record of the end mark on a teleporter square without things.

### Things in the view (Step 12b)

- Thing grids (`g_view_creature_grid`, `g_view_object_grid`, 21 x 17 bytes each, index `(lateral + 10) * 17 + forward`): each square has 5 x 5 cells, and neighbour squares share their edge rows and columns, so a square is 4 grid cells wide. `view_load_square` writes the view position of each square up to depth 3 that is not a wall into the creature grid (4 rows of 5 cells), and the view position of each creature group into the object grid at its cell relative to the party eye (cell 22, 10, 2, or 14 of the party square by direction). A creature of size 1 to 3 (creature info `+0x22`) stands nearer to the party. An occupied object grid cell moves the creature one cell nearer.
- Cell masks (`g_view_missile_masks`, `g_view_object_masks`, one 32-bit word per view position): bit n is set when grid cell n of the square holds a projectile, or an item. A floor square with a shop stand (side word 3 is 0) does not set the object bits.
- `view_draw_square_things` goes through the 25 cells (the party square: the far 15) in an order toward the view axis. For each cell it draws the objects, the creature whose object grid cell is there (then the missiles of the 7 cells before), and the missiles. Mask bit 30 limits the creatures to static creatures: the creatures behind a closed door.
- Creature pictures: the animation frame (GDAT `(0x0f, type, 7, 0xfd)`, 8 bytes: picture entry for each side, grid cell, size row, shift, mirror bits) gives the picture, the cell, and the scale (`(0x0f, type, 7, 0xfe)`, 4 bytes per size row). A missing picture falls back to the opposite side (mirrored), to the front, then to the pictures `0xfa` to `0xfd`. Static creatures (creature info flag bit 0) keep the animation state in their record (`+8` sequence start, `+0x0a` state); the other creatures keep it in their active group (`g_active_groups`, `+8`).
- Possessions of a static creature with creature info word `+0x1e` bits 0 to 3 not 0: they lie above the floor (creature value key `0x41`, or 58 or 53) and one cell toward their side, so they can lie on a neighbour square. Cells that the cell mask leaves out wait (`0x75c00`, `0x80038`, `0x8003c`) for `view_draw_pending_possessions`. A creature whose word `+0x1e` has neither bit 10 nor bits 4 to 7 set shows no possessions (only a click zone).
- Objects: the scale comes from a table by depth and row; the ordinal of the object in its cell adds a small offset (16 pairs). Chests (container GDAT index below 8) show picture 4 or 5 when open. Objects in an alcove are placed relative to the alcove picture (`g_view_draw_context_backup`) and the offset key `(9, ornament, 0x0c, 0xfd)`.
- Missiles: a payload without picture 8 is drawn as the object. Otherwise the pictures 8, 9, 10 (front, back) and 12 (side) are selected by the flight direction (bits 10 and 11 of data word 2 of the move event), the cell, and the square parity.
- Explosions: drawn in 5 rounds (the 4 cells in an order by the party direction, the explosions on the whole square in the middle). An explosion with picture `0x14` is a sparkle field. On the party square an explosion tints the whole view. Type `0x30` counts its views in global bytes `0x40` and `0x41`. The scale of an explosion in a near cell of the party square comes from the byte before the scale table (19).
- Sparkle fields (visible teleporters: `(0x18, 0)`) and rain tile a random part of their picture (`gfx_blit_tiled_4to8`) over the zone; the sparkles use a mask picture per position. Rain shows when the rain intensity is not 0 and the sky state `(8, graphics set, 0x0b, 0x66)` allows it (states 1 and 4). The rain picture (`0x6d` to `0x74`) depends on the rain intensity and on the wind relative to the party. A wet ornament takes a random part of the rain picture as its background.

### Deviations of the C port (Step 12b)

- No active groups until Step 16: the creatures that are not static show the first frame of their first animation sequence, without the random shift and mirror of the active group. After a load the original also has no active groups until the party enters the map. The test oracle gives these creatures a zeroed active group, which gives the same frame.
- Not ported until their steps: the push arrow cell and scale of a creature in front of the party, and all click zones (Step 14); `view_draw_centered_label`, whose only caller is the shop display (Step 15), and which needs the text drawing (Step 13).
- Explosion types 100 and 101 in alcoves are ported, but GRAPHICS.DAT has no pictures for them.
- Fixes: the mirrored sparkle mask of an odd width has an undefined last column in the original (an image cache bitmap that is not cleared); the port clears it. The rain bitmap of a wet ornament is rect width x rain height (all rain pictures are 224 x 20). Its tile row is that area - 16 - 0 to 31 pixels, from a start of `rand_below(area - 60)` (16-bit). When the tile row is not longer than its start, for example a wet ornament 1 or 2 pixels wide, the original tiles from past the end of the rain bitmap; the port then fills the background with the color key. Reads past tables (creature info, animation frames, creature sizes) and a division by 0 for a looping animation of length 0 are guarded.
- `save.c` reads the static flag from the creature info table of `creature.c` (the table it had copied).

## 9. Interface panels and text (Step 13)

### Font and text drawing

- The font is GDAT `(1, 0, 7, 0)`: 6 rows of 128 bytes, one byte per character and row. Bits 4 to 0 are the 5 pixels of a glyph row. A character above 127 reads into the next row (the last row reads past the 768 bytes; the port reads zeros there).
- `text_build_glyph` makes a 4 bpp glyph of 6 × 6 pixels: the foreground value on the set bits, the background value on the other bits and in column 6. `gfx_draw_text` draws one glyph per character, 6 pixels apart, with the glyph rows from `y - 4` to `y + 1` (the base line is at `y`, 1 row of a glyph is below it).
- In an 8 bpp bitmap the glyph holds 0 and 1, and a 2-entry colormap gives the color and the background. A background with bit `0x4000` is transparent. Text uses `MIRROR_NONE`, so the mirrored 4 bpp quirks of section 8 do not apply.
- `text_measure` gives the width `6 × length - 1` and the height 5. `text_wrap_line` breaks a line at the last space that fits; a word longer than the line breaks at the width.
- `text_draw_with_shadow` draws the text in UI color 0 one pixel down, and one pixel down and right, then in the color. A background without bit `0x4000` first fills a box 1 pixel larger than the text.

### UI colors and text formats

- GDAT `(1, 0, 13, 0xfe)` holds 16 palette indices: the UI colors (`enum ui_color` in `port/src/text.h`). The interface draws with UI color indices, not palette indices.
- `text_format` replaces format codes: `.Z` and 3 decimal digits, or byte 1 and the code + 32. The inserted text is formatted again. Codes: 2 and 27 texts `(1, 0xfe, 5, 0)` and `(1, 0xfe, 5, 6)`; 7 a champion name (word `0x7f988`); 10 to 14 the numbers in the words `0x7f996`, `0x7f98a`, `0x7f994`, `0x7f992`, `0x7f986`; 17 the text `(7, 0, 5, byte 0x7f990)`. The other codes (0, 1, 3, 4, 8, 9, 15, 20, 22 to 26, 28) give drive paths, disk sizes, error codes, and save names for the dialogs. An unknown code inserts an empty text.
- `gdat_get_text` decrypts a text entry and formats it. The inventory sets the numbers before it formats a text, for example `(7, 0, 5, 0x2a)` (load) and `(7, 0, 5, 0x1f)` (weight).

### Message area

- 3 rows in zone 15, 7 pixels apart, with `g_message_area_y = (zone height - 20) / 2` above the first row. A row clears 90 game ticks after its last text (`g_message_expiry`).
- A text at the bottom row that needs a new row goes into the row bitmap (320 × 7, `g_message_scroll_bitmap`). Before the next text, IBMIOP service `0x105` moves the area up by one row; then the first 6 rows of the row bitmap go to the bottom 6 rows of the area.
- Text before a `\n` goes to the next row; a wrapped line goes on at column 12.

### Offscreen bitmaps

- An offscreen bitmap (`gfx_offscreen_create`) holds a screen rectangle: the image cache index, the rectangle, and up to 5 changed rectangles. A changed rectangle that holds a new one keeps it; a new one that holds a changed one replaces it; a sixth one first copies the 5 to the screen. `gfx_offscreen_flush` copies the changed rectangles to the screen.
- The status box of a champion (zones `0xa1` + champion) and the action area (zone 11) draw into offscreen bitmaps. `ui_status_box_begin_draw` with clear 0 draws the box background but forgets it as changed: only the parts that change later go to the screen.
- `gfx_fill_rect_dithered` fills the pixels whose `x + y` is even (for an even bitmap width) with a UI color: the dimmed hands of a busy or sleeping champion and the portrait of an invisible party.

### Slots and panels

- The slot table (`0x75538`, 55 records of 8 bytes): layout zone, icon of the empty slot, and the drawn state (icon, GDAT index, active and wounded frame, thing). Records 0 to 7 are the hands in the status boxes, 8 to 37 the inventory slots, 38 to 45 the open container, 46 the object info, 47 to 54 the contents of a chest in the object info.
- An item icon is GDAT `(category, index, 1, 0x18 + frame)`. Item attribute 6 selects the frame: bits 0 to 4 frame count, bits 8 to 12 the mode (game time, random, party direction, charges, charges and time, the same two per item), bits 5 to 7 the time steps of mode 4, bit 15 a first frame for a fitting slot, bit 14 a first frame in the open hand.
- The action area shows the hands of the party (mode 0), the action menu of a hand (1), the spell panel (2), a minion map (3), a money box (4), a chest (5), or the result of an action (6). A hand icon shows only when the hand has actions (`champ_list_actions`): the actions are the item texts 8 to 11 whose command, hand, charges, and skill level allow them.
- The skills panel keeps the raised count of each base skill that it shows (`0x7f968`). Opening the inventory clears these counts; closing it subtracts them from the champion counters (`0x7fff8` + champion × 4), so a raised skill shows green until the player saw it.

### Deviations of the C port (Step 13)

- No hardware layer: the screen bitmap is `fb`; `screen_present_*`, `mouse_hide`, `mouse_show`, and `vga_wait_retrace` have no port. Offscreen bitmaps are frame bitmaps (`draw_temp_bitmap`), not image cache entries.
- IBMIOP service `0x105` is not in `SKULL.EXE`. The port moves the rows of zone 15 up by 7 pixels and keeps the bottom 7 rows (`text_scroll_message_area`); a manual check in DOSBox confirms it or not.
- `text_format`: the dialog codes give an empty text until Step 20.
- The action area reads the wall set of the party map from the map descriptor; the original reads `g_map_gfx_set`, which `game_enter_map` sets from the same descriptor.
- `champ_get_load` adds the weight of the leader hand object when it is called; the original adds the weight that it keeps since the object went into the hand.
- Later steps: the hand reload countdown of `ui_update_action_area` and the leader hand object of `champ_init_party_state` (Step 15); the minion map panel and its mana (Step 16); input modes (`input_set_mode`, `input_refresh_mode`), the move arrow feedback, and the view redraw when the inventory closes (Step 14); the new game party of `champ_init_party_state` (Step 20).
- `item_gdat_index` gives `0xff` for wall texts and sensors (the view has its own ornament code).
- `text_decode_dungeon` (`dun_decode_text`) and the view share one raw decoder, `text_decode_raw` in `text.c`. `text_decode_dungeon` adds the format pass. The view reads the wall plate texts without the format codes.
- Fixes: the texts are bounded to 256 bytes; the coin table holds at most 10 coin types.

## 10. Main loop, input, and movement (Step 14)

### Timing and the game step

- The PIT runs at 240 Hz. Every 4th PIT tick is a timer tick (60 Hz). A game step runs every 8 timer ticks (`main_loop.tick_threshold`), every timer tick while the party sleeps.
- `game_loop` of `SKULL.EXE` waits for input in the middle of a step. The port splits it: `game_step` (the timeline, the view, the party clocks) and `game_step_end` (a map change, repeated actions, music). `game_tick` runs one timer tick: the input, the click feedback, the command queue, and a game step when it is due.
- Pause (`game_pause`) stops the game time until the resume command. The port keeps the main loop running and clears `main_loop.running`.

### The input tree

- The tables are in `SKULL.EXE` and in `port/src/input.c`: the nodes (condition with bit 7 for a node with children, argument, child list or zone), the child lists (bit 7 starts a list), the root node of each of the 10 input modes, the zones (7 bytes: layout zone with bit 15 for the inventory panel and bit 14 for the action area, command list, key list, cursor area), the command lists at `0x72388` (6 bytes: command with bit 15 to start a list, zone, mouse buttons, flags), and the key lists at `0x729b8` (4 bytes: command with bit 15 to start a list, key code).
- A key code is the PC scan code, plus `0x200` for shift, `0x400` for control, `0x800` for alt. The arrow keys give the numpad codes: up `0x4c`, left `0x4b`, right `0x4d`, down `0x50`, shift and left `0x4f`, shift and right `0x51`. Esc (`0x01`) pauses.
- A click goes down the tree from the root node of the input mode. The first command whose zone holds the click and whose buttons match wins. The matched zone rectangle and the click position stay for the click feedback.
- The event ring holds 11 events. A new event goes in when fewer than 7 events wait (9 for a released right button and for the area events). A key goes into the ring only when the main loop reads it. `input_flush` keeps only the area events and the released right buttons.

### Click feedback

- Each command has a feedback script: bytecode that draws the pressed button, waits timer ticks, draws the released button, and then queues the command. The low 6 bits are the opcode, bit 6 runs the next opcode at once, bit 7 ends the script.
- The port runs the opcodes 0 to 7 and `0xd`. The other opcodes draw panels of later steps; the port skips their argument bytes.
- The movement arrows show pressed (`ui_draw_move_arrow`) until the move is done (`ui_release_move_arrow`).
- Before the script lookup, `input_refine_champion_click` makes the command of a champion click. A hand in a status box (commands `0x74` to `0x7b`, cell = party direction + (command - `0x74`) / 2) gives `0x74` + 2 × champion + hand when the champion lives, the hand is ready, and the hand has actions; else no command. A champion icon of the party icons (`0x5f` to `0x62`) gives the leader command (`0x10` to `0x13`) when the click is nearer to the icon edges that face the party center; else it keeps the command when the spell is ready, and else no command.

### Click zones of the view

- The view drawing records up to 32 click zones (`g_view_click_zones`, 12 bytes: rectangle, thing, cell, kind). The first 9 are fixed: the objects of the 4 cells (the party square, and the 2 near cells of the front square, when no walking creature stands on them), the creature possessions of 4 cells, and the alcove in front. `view_draw_dungeon` clears the thing and the kind of the fixed zones. Their rectangle and cell keep the values of an older view. The wall ornaments and the door buttons add zones after the fixed ones.
- A fixed zone of an object grows to hold all the objects of its cell, and is at least 14 × 11 pixels.
- `view_click_in_viewport` tests the zones in order: an object or alcove zone takes or places an object (Step 15), a wall ornament goes to `sensor_process_wall_click`, a door button toggles the door. A click on no zone goes to the front wall or to a creature (Step 16).

### Movement

- `g_cur_map` is the map of the dungeon reads (`dun_get_square`, `dun_move_relative`). The party map and the current map differ while things move on other maps.
- A slow party (`champ_get_move_delay`) walks: `g_party_move_steps` holds the view one square back for the steps of the walk.
- `move_follow_square_transitions` takes a thing through teleporters, open pits, and stairs until it stays on a square. A pit can lead to the map below or to a marker of the map.
- A move into a wall, a closed door, or a creature gives bump damage (`move_party_bump_damage`) to the champions on the front cells.

### Sensors and square events

- A square event (type 4) holds x, y, cell, and effect: 0 set, 1 clear, 2 toggle, 3 hold. The priority is 1 for set, 3 for clear, 2 for toggle.
- Floor sensors react when a thing (or the party) enters or leaves the square. Wall sensors react to clicks and to square events. A sensor with a target queues a square event at the target after its delay.
- The script variables are 64 flags, 64 bytes, and 64 words (`dun_get_global_var`): the ops are set, clear, toggle, add, subtract, and assign.
- Door events: type 1 opens or closes a door one step per event, type 2 breaks it.

### Deviations of the C port (Step 14)

- Uninitialized data of the original: some events keep 2 to 4 data bytes or the high bits of a word from the stack (hold square events, sensor timers, item and wall events). The port sets them to 0. The reverse of a hold effect and the priority of a hold square event are uninitialized too: the port keeps the hold effect and uses priority 3.
- Bounds: a wall square event to a map outside the dungeon does nothing; at most 32 view click zones.
- `creature_cell_mask` reads display kinds 5 to 15 from the bytes after its table in the original. The port keeps those bytes.
- The fall view of a pit goes into the viewport, and the next present shows it. The original presents it in the middle of the step.
- Not ported: the mouse areas of the cursor (enter and leave events, cursor shapes); the mouse drag through a fake wall; `input_check_memory_cheat`, a key sequence that prints the free memory in the message area.
- The original load relinks the damage display event of each champion (`timeline_relink_events`): `0xffff`, or the index of the queued hide damage event (`0x0c`) of that champion. The C load did not; `save.c` now does.
- Later steps (empty hooks in `port/src/hooks.c`): the leader hand object, item creation, and recruiting (Step 15); active creatures, the push of a creature, the push arrow (`input_update_viewport_arrow`), and missiles (Step 16); the weather update and the object transfer of a sensor (Step 17); sounds (Step 18); the end of the game, the dialogs (also the dialog after a load in `game_start_session`), `dungeon_link_things_after_load`, and `dungeon_read_level_table` (Step 20).

## 11. Champions, inventory, items, money, and shops (Step 15)

### The leader hand

- `g_leader_hand_object` holds the object of the leader hand. Beside it the original keeps its attribute word, its weight, and its 16 × 16 icon. The port keeps them in `leader_hand` (`port/src/inventory.h`) and draws the icon at the mouse pointer.
- A click on an object zone of the view takes the object (`view_click_take_object`). With an object in the hand, the click places the object in the cell (`view_click_place_object`), in an alcove, or into an open chest. A click in the upper view throws the hand object (`view_click_throw_object`). The commands `0x14` to `0x41` click the inventory slots, `0xea` to `0xed` put the hand object into a free slot.
- `champ_apply_item_effects` adds or removes the stat effects of an item in a slot. Mode 0 is the load, 1 adds, -1 removes, 2 and -2 are the timed effects of eaten items, 3 restores. The load applies mode 0 to all slots. Then `save_restore_timed_effects` restores the light power and the timed effects.

### Damage and time effects

- `champ_take_damage` adds the damage to `g_pending_damage` and the wounds to `g_pending_wounds`. `champ_apply_pending_damage` applies them at the end of the step: health, the damage star (event `0x0c`), and death (`champ_kill`, bones in the champion cell).
- `champ_apply_time_effects` runs every game step for each living champion: food, water, stamina, health and mana regeneration (mana: maximum mana / 40 + 1 per gain), the decay of raised stats to their base, and the poison.

### Actions

- `g_action_repeat` has 2 records of 8 bytes for each champion: count, steps left, a fresh flag, the action, and the thing. `champ_process_repeated_actions` runs them each step.
- `champ_perform_action` runs the command of an action (GDAT text field `CM`) with its skill, stamina cost, delay, and charges. A melee action at a closed door plays the door sound `(0x0e, door type, 0x8d)`; a hit creature plays `(0x0f, creature, 0x8d)`. Then the action plays its sound (text field 13) at the party, except after a reload and after a shot. The commands of creatures, spells, minions, and missiles call Step 16 functions.
- `champ_add_experience` uses the last combat time and the experience factor of the map. A level gain prints a message in the color of the skill class (green, yellow, red, blue).

### Money and shops

- The coin types are the miscellaneous items with the coin flag (`ATTR_FLAGS` bit 14), sorted by value. `money_split_amount` takes the most valuable coins first.
- A merchant creature has price lists 0 to 4. `shop_parse_item_class_set(creature, n)` reads GDAT text `(0x0f, creature, 5, 0x10 + n)`: letters choose the item kind (`W` weapons, `A` clothing, `J` miscellaneous, `P` potions, `C` containers, `S` scroll), numbers and ranges choose the items. The shop display uses n = `0x10` + list × 3 (the items of the list). The prices use n = `0x11` and `0x12` + list × 3, the item sets of two price factors (creature values `0xa1` and `0xa2` + list × 3, in percent). The margin is creature value `0xa0` + list × 3 (default 10 %).
- A shop is a wall sensor of kind `0x3f` with a shop ornament (ornament value `0x0a` = 2). Its data byte is the merchant creature. Its flags: bit 0 "item chosen", bit 2 "step forward", bits 7 to 10 the price list. Its target square holds the display sensor (kind `0x30`). The data of the display sensor is the shown item index.
- `shop_draw_display` chooses the item when bit 0 is clear or bit 2 is set: the next item (bit 2, or a click above the center of the ornament) or the last item (a click below the center) of the item order table `(9, ornament, 8, 0x13)` that is in the item set of the list. A unique item (`ATTR_FLAGS` bit 12) shows only when it lies on the square, when a creature there holds it, or when the party has it. A table word with bit 15 makes a new item on the target square when the square does not have it. Then the function draws the item (offset value `0xfd`), the empty display (entry `0x11`) when no item is there, and the overlay (entry `0x0f`). In front of the party it also draws the item name (offset `0xf2`) and the sell and buy prices as coin pictures (entry `0x10`, 8 × 7 pixels for each coin type, offsets `0xf1` and `0xf0`).
- The map graphics load has all entries of the map ornaments, also the item order table. `gdat_get_entry_data_or_null` gives entries of other categories only when they are loaded.
- `g_view_hit_offset` (`0x802d4`) is the place of the last click in a view picture: the vertical and the horizontal distance from the picture center, in percent of half the picture size. The port keeps the vertical value in `view_hit_offset_y`.

### Item creation by a sensor

- A wall sensor of kind `0x3c` makes a new item when a square event reaches it: with the effect set, or with the effect clear when the sensor is inverted (flags bit 5). Its data is the item index. A new record of the thing type gets the GDAT index. Flags bits 3 and 4 both set keep the item from reuse (`dun_set_thing_do_not_discard`).
- `sensor_transfer_object` sets the charges by flags bits 3 and 4: 0 full, 1 empty, 2 random up to full, 3 an uninitialized word. Miscellaneous item 0 keeps its charges. The item goes to the target square (target word: x bits 6 to 10, y bits 11 to 15, cell bits 4 and 5), to a random cell when the target is not a wall. Flags bit 2 selects the move mode of `move_thing`.

### Deviations of the C port (Step 15)

- Fixes: the original leaves data bytes of some events uninitialized (the timed effects of eaten items, poison, the hide damage event, invisibility). The port sets them to 0. `champ_add_to_party` reads the champion data without a test that the GDAT entry exists; the port stops when it does not exist. `champ_start_recruit` without a mirror on the wall takes an uninitialized portrait; the port does not recruit. `shop_parse_item_class_set` writes outside the set for bad numbers; the port writes only inside it. `shop_draw_display` loops for ever when the merchant sells no item of the list, and reads a NULL record without a display sensor; the port stops after 512 tries, and draws nothing without a display sensor. `item_create_by_index` and the item creation sensor ask for a record of type `0xffff` for an item index without a thing type; the port gives no item. The item creation sensor with charge mode 3 adds an uninitialized stack word to the charges; the port fills the charge field. A coin type below 0 reads before the coin table; the port gives no coin.
- Fixes of earlier steps: the slot mask table of `item.c` was shifted by one slot (Step 13). `ui_draw_movement_arrows` did not set the input mode (Step 14).
- The port draws the leader hand icon at the mouse pointer in each present. The original sets it as the mouse cursor.
- The load clears the pending damage and wounds, the regeneration counter, and the leader hand of the last game (`champ_reset_step_state`).
- Not ported: the mouth or eye held with an empty hand (it needs the mouse capture of the original); `dun_get_discarded_thing`, the reuse of discarded items when the records run out.
- Open Step 15 work: merchant trading (the merchant AI commands), after the merge of step/16-creatures.
- Later steps: the creature parts of the actions, spells, minions, and missiles (Step 16, hooks); the sound layer behind the sound hooks that `champ_perform_action` calls (Steps 17 and 18); `champ_edit_name` and the party of a new game (Step 20).
- The coin of `start.DAT`: the gold coin on the table of the first room does not open the door. No wall slot takes it, also in the original. The way out: the party pushes the table (static creature `0x19`, push resistance `0x2d`) east or south (`creature_can_be_pushed`, `move_push_creature`, Step 16), takes the solid key from the shelf (1, 5), puts it into the keyhole (4, 5) cell 3, and presses the door button (4, 4) (sensor kind `0x46`), which toggles the door (3, 4).
