# Digital sound effects

The sound effects are sample items in `GRAPHICS.DAT` ([graphics.md](graphics.md)). The game plays them through the Human Machine Interfaces Sound Operating System (HMI SOS) digital driver and sets each sound's volume and stereo position. Addresses and notation follow the conventions of [skull-map.md](../reference/skull-map.md).

## Sound items

- The samples are [GDAT](graphics.md#gdat-items) items of type `2` in `GRAPHICS.DAT`. There are no separate sound files. `TEST.RAW` is only for the HMI `SETUP.EXE` test.
- There are 292 sound items. Each item has a 6-byte header and raw pulse-code modulation (PCM) data.
- Header: u16 LE sample rate `0x2b77` (11127 Hz), then the bytes `08 01 00 00` (8 bits, 1 channel; unverified, because no code reads these bytes). All 292 items have the same header.
- Data: 8-bit signed PCM, mono, not compressed. Length = item size - 6 (245 to 61713 bytes).
- The SOS digital driver mixes at 11025 Hz. The game stores the 11127 Hz rate but does not pass it to the driver. The samples play at 11025 Hz (0.9 % slower).
- GDAT flag item `(0, 0, 0x0b, 0)` is `0x7b`. `FUN_0003d1c8` (`gdat_load_index`) stores `0x7b & 0x20` in `DAT_00071698`, so the `0x20` flag is set.
- `FUN_0001623b` (sound cache): for each key `(cat, idx, 2, entry)` it fills the 16-byte descriptor at `DAT_0007f194 + n*16`:
  - `+0` data pointer = item + 6 (item + 2 if the `0x20` flag is clear),
  - `+4` u16 length = item size - 6,
  - `+6` u16 rate = first word of the item (constant `0x157c` = 5500 Hz if the flag is clear),
  - `+8` link for the cache list.
- `FUN_000104ea` converts the data in place to unsigned 8-bit (XOR `0x80`) on load. No code in the decompilation reads descriptor field `+6`.
- Driver init (`FUN_00010010`) sets `_DAT_00070482 = 0x2b11` (11025) in the SOS init structure at `0x70472` (buffer size word `0x1000`), then calls `FUN_0005bc00` (`sosDIGIInitDriver`).
- Categories that hold sounds: 15 (192 items), 9 (24), 22 (21), 13 (16), 10 (8), 3 (7), and others.

## Playback, volume, and stereo position

Request: `FUN_00015ca9(cat, idx, entry, priority, volume, x, y, mode)`.

1. If the global flag `DAT_0007f234` is set, `volume = volume >> 1`.
2. The sound position is made relative to the party (`DAT_0007f26a`, `DAT_0007f26e`) and rotated by the party direction `DAT_0007f252`. The result is `lateral` (byte `+6`) and `forward` (byte `+7`).
3. `dist = |dx| + |dy|`. If `dist > 1`, `FUN_0002facc` (`creature_get_party_distance`) returns the distance `n` from (x, y) to the party in the distance map. If `n < 0`, the sound is dropped. If `dist < n`, both `lateral` and `forward` are scaled: `k = (n << 10) / dist`, and each component `c` becomes `±((|c| * k + 0x200) >> 10)`.
4. `mode` is the delay: `0` plays now, `1` queues to `DAT_0007f198`, a negative value queues to `DAT_0007f190`, `>= 2` uses timeline event `0x15`. `FUN_0001616d` flushes a queue. [world.md](../rules/world.md#sound-requests) gives the delay rules.

`FUN_00010877` computes the volume and pan of the 12-byte request record.

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
- `FUN_00010573` (`start_requests`) sorts the records by priority (byte `+4`), then by `vol8` (`FUN_0001084d`). It fills the SOS start-sample structure at `0x703f8`: `+0` data pointer, `+8` length, `+0x10` = 2, `+0x14` volume = `vol8 << 7 | vol8 >> 1`, `+0x18` slot, `+0x28` = 800, `+0x40` pan. Then it calls `FUN_000609b9` (`sosDIGIStartSample`). There are 64 slots (`DAT_0007eda0`).
- Master volume: `FUN_00010736` (`sound_set_volume`) takes the level `l` (0 to 7) and 0, and passes `l | l<<3 | l<<6 | l<<9 | l<<12` (0 to `0x7fff`) to `FUN_00061a45`.

The SOS structure field names are unverified, because the binary has no HMI SOS symbols.

## Deviations from the original

`src/ui/sound.c` is the port's sound output.

- The port loads the sounds from GDAT type `2` and skips the 6-byte header (`sndreq_load`, `src/engine/sound_request.c`).
- The pan ratio overflow is fixed. `FUN_00010877` divides `|lateral| << 11` by `|forward|` in 32 bits, but compares only the low 16 bits with `THRESH`. For `|lateral| >= 32` and a small `|forward|`, the ratio wraps and the sound pans near the center (example: lateral 32, forward 1 gives `0x8f00`, not `0xf800`). The port compares the full ratio. The differential test skips these inputs and checks the fix separately.
- Mixing: the port mixes at 44100 Hz (4 × 11025) in interleaved signed 16-bit stereo. Each sample repeats 4 times. SDL3 converts to the device rate.
- Pan law: the SOS pan curve is unknown. The port uses a linear balance with the center at full volume on both sides: left gain = `min(0x8000, 0x10000 - pan) / 0x8000`, right gain = `min(0x8000, pan) / 0x8000`, times the SOS volume `vol8 << 7 | vol8 >> 1`.
- Voices: 64, as in the original. A new sound takes the first free voice. If all voices play, the new sound is dropped.
- The port keeps the samples signed and skips the XOR `0x80` of `FUN_000104ea`.
- `sndreq_party_offset` takes the party direction 0 to 3. For other values the original keeps stale locals; the port treats them as north.
- The audio output (`src/platform/platform.c`) fills the SDL audio stream from `plat_wait_tick` on the main thread, up to 2048 frames ahead. `plat_audio_music` adds a music source, which is mixed before the sound effects.
