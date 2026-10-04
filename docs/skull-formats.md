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

Confidence: high.

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

Impact on the port: set `PIT_DIVISOR` in `port/src/platform.c` to `4971` (240.02 Hz). Derive the game tick as every 4th PIT tick (60 Hz) and the game step at 8 game ticks. Drive the HMP player at 120 Hz. Confirm the game rate with the DOSBox measurement.

## Other finding: IBMIOP.EXE is a required runtime layer

`DM2.BAT` starts `IBMIOP SKULL.EXE`. `IBMIOP.EXE` is a real-mode resident layer that `SKULL.EXE` calls through `int 0FCh` with a shared buffer (`DAT_0008048c`). It sets video mode `0x13`, writes the palette (port `0x3c8`), reads `skull.cfg`, handles the keyboard and timer, and runs the movie script. `SKULL.EXE` uses 34 call sites with function numbers `0x00` to `0x30` and `0x103` to `0x10a` (for example `0x0f` timer callback, `0x2a` timer reinstall, `0x2e`, `0x2f`, `0x30` at start and exit).

Impact on the port: `docs/skull-map.md` lists these `int 0FCh` functions. The platform layer replaces them (video, palette, input, timer).
