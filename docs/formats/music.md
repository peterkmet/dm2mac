# Music

The songs of the game: the HMP song items in `GRAPHICS.DAT` ([graphics.md](graphics.md)), `SONGLIST.DAT`, the HMP player, and the conversion of MIDI to OPL register writes in the HMI driver library `HMIMDRV.386` with the instrument banks `MELODIC.BNK` and `DRUM.BNK`. The addresses and the notation follow the conventions of [skull-map.md](../reference/skull-map.md).

## HMP songs and SONGLIST.DAT

- The songs are 29 GDAT items with key `(4, song, 3, 0)`, `song` = 0 to 28. Each item is a complete HMP file (`HMIMIDIP013195`).
- `game/TEST.HMP` is byte-equal to song 10 (item 5602, 9645 bytes).
- `SONGLIST.DAT` is a byte array indexed by the current map number. Each byte is a song number; byte `0` plays no song ([Song change](#song-change)). The file has 46 used bytes (indexes 0 to 45) and `0xff` padding to 63 bytes. The game reads at most `0x3f` bytes.
- `music_play_song` (`0x1095d`) looks up the key `(4, song, 3, 0)` with `gdat_entry_exists`, `gdat_entry_size`, and `gdat_get_entry_data`, copies the item to `DAT_000704da`, and calls `hmi_midi_init_song`, `hmi_midi_start_song`, and `hmi_midi_set_song_volume(handle, 0x7f)`.
- In file order, the song items go from `file+0x7bd597` (song 0) to `file+0x83b356` (song 17). Every item begins with `HMIMIDIP013195`.
- `FUN_00010010` loads `DATA\SONGLIST.DAT` (`0x70019`) into `DAT_0007ef78`, at most `0x3f` bytes.
- The main loop calls `FUN_00010af6(DAT_0007f260 >> 16)` once per game step (`FUN_00024691`, `game_loop`). `DAT_0007f260 >> 16` is the current map index. It also indexes the 16-byte map records at `DAT_0007f3c4`.
- `SONGLIST.DAT` bytes 0 to 45: `2 17 14 27 4 12 12 18 15 13 12 12 16 6 21 14 17 17 17 17 3 8 17 14 2 23 22 20 17 0 2 2 2 9 2 3 14 16 28 22 19 9 22 3 17 2`.

## Song change

`FUN_00010af6`:

1. `new = SONGLIST[map]`. If `new` equals the current song (`DAT_0007050e`), nothing changes.
2. If no song plays, or a fade is active, the new song starts now.
3. Otherwise a fade starts: the fade counter is `0x7f`. Each call sets the song volume to the counter and decrements it. At counter `1`, the new song starts.
4. A `SONGLIST.DAT` byte 0 starts no song (`FUN_0001095d(0)`), so GDAT song 0 never plays through this path.

Music master volume, `FUN_00010736(level 0..7, 10)`: `FUN_00060399(l*2 | l<<4)` (0 to 126). Level 0 stops the song.

The port reads the songs from GDAT `(4, n, 3, 0)` and follows `FUN_00010af6` with the fade of each game step (`music_update_for_map`, `src/ui/music.c`).

## HMP player

Source: `hmi_midi_init_song` (`0x5f6ad`), `hmi_midi_song_tick` (`0x65a3b`), `hmi_midi_send_event` (`0x5e489`), `hmi_midi_send_track_setup` (`0x66fb8`). Port: `src/data/hmp.c`.

- Header: `+0x20` u32 offset of the loop table, `+0x30` u32 track count, `+0x38` u32 song timer rate ([runtime.md](runtime.md#pit-divisor-and-tick-rates)). Track chunks start at `+0x388`: u32 track number, u32 chunk size, u32 MIDI channel, then the data.
- Delta times: 7-bit groups, low group first. The last byte has bit 7 set (the opposite of standard MIDI).
- Timing: each tick adds 1 to the elapsed ticks of each track. A track plays its next event when its delta is not more than the elapsed ticks. After each event, the elapsed ticks start again at 0. Thus the first event plays at tick `max(d, 1)` (`d` = first delta), and each later event plays its delta after the event before it. An event with time `T` (sum of the deltas) plays at tick `T + 1` if the first delta is 0, otherwise at tick `T`. Tracks play in track order within a tick.
- Track 0 events are not sent. Controllers `0x67` to `0x79` are HMI commands and do not go to the driver. The songs use `0x6c` (no effect), `0x6d` (the value of the next event, an `0x6e` controller, sets the loop count of the loop point with id = value), and `0x6f` (loop). `0xff 0x2f` ends a track.
- Loop table: one record count per track, then 24-byte records of all tracks: `+0` u32 loop point in the track data, `+4` id, `+5` program, `+6` loop count (`0xff` = forever), `+7` byte count of controller pairs, `+0xc` u32 offset of the pairs.
- Loop `0x6f id`: if the count is not 0 or `0xff`, it decrements. If the count is not 0, all tracks 1 and up that still play jump to their loop point with the same id, read the delta there, and get a program change and the controller pairs. Later tracks count the loop tick as their first tick. All 29 songs loop forever.
- `hmi_midi_init_song` writes 1 to the 128 bytes at song `+0x300` (enable flags of the controllers `0x67`/`0x68`). This also sets `+0x36c`, the flag of the program change at a loop point. Thus the loop point always sends its program.
- Channel volume (`0x07`): the player stores the value per channel and sends `master * (value * song >> 7) >> 7`, or 0 when the song is muted. Song volume, mute, unmute, pause, and master volume send the stored volumes of the playing tracks again.
- All tracks of all songs play on the FM driver: their device lists are empty or contain `0xa002`, and the track map `0x70378` gives device 0 to tracks 16 to 31.

## MIDI to OPL conversion in HMIMDRV.386

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

The OPL2 driver code starts at `file+0x5c` (`jmp 0x560`). All absolute data addresses are relative to `file+0x5c`. The entry at `file+0x560` dispatches through a function table at driver offset `0xbb`. The code is compiled C with `ebp` frames. It is 12.7 KB of plain 32-bit code with `cdecl` frames and no relocations. The OPL3 driver contains the same BNK packing code (`file+0x3efe`).

| File offset | Driver offset | Function |
|---|---|---|
| `0x1dc0` | `0x1d64` | OPL register write `(reg, value)`: port from driver `[0x2e5c]`, 6 status reads after the index, 35 after the data. |
| `0xd93` | `0xd37` | BNK packing: converts an AdLib BNK in memory to register bytes. |
| `0x2054` | `0x1ff8` | Instrument to voice: writes the packed bytes to the operator registers. |

### BNK files and register mapping

`MELODIC.BNK` and `DRUM.BNK` use the AdLib Visual Composer BNK layout with the signatures `AMLIB-` and `ANLIB-`:

- `0x08` u16 used = 128, `0x0a` u16 count = 128, `0x0c` u32 name offset = `0x1c`, `0x10` u32 data offset = `0x61c`.
- Names: 128 × 12 bytes (u16 index, u8 flag, 9-byte name). In `DRUM.BNK`, the flag byte holds the drum note (for example `35`, `38`, `41`).
- Data: 128 × 30 bytes (`0x61c + 30*128 = 5404` = file size).

Per 30-byte record, `b[0]` is the percussive flag and `b[1]` the voice number. The modulator fields are `b[2..14]` and the carrier fields are `b[15..27]`, in the order KSL, MULT, FB, AR, SL, EG, DR, RR, TL, AM, VIB, KSR, CON (key scale level, frequency multiplier, feedback, attack rate, sustain level, envelope type, decay rate, release rate, total level, amplitude modulation, vibrato, key scale rate, connection). `b[28]` and `b[29]` are the wave selects.

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

### Standard AdLib mapping

The standard AdLib mapping uses the same register formulas as in the table above, 9 melodic voices, F-number from the MIDI note with block = octave, carrier TL scaled by velocity and channel volume, and drum notes played as melodic voices with the instrument from `DRUM.BNK[note]` at the pitch from its name flag byte. The port does not use it: it reproduces the driver logic ([OPL2 driver logic](#opl2-driver-logic)).

### OPL2 driver logic

Driver offsets below. Port: `src/ui/fm.c`. The port reproduces the OPL2 driver `0xa002` (Sound Blaster, Sound Blaster Pro), not the OPL3 driver `0xa009` (Sound Blaster 16, the DOSBox default).

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

- Note on: take a voice (first free voice, otherwise the first voice of the first channel without pitch bend, otherwise `channel mod 9`), key it off, write the release byte with release rate 15 five times (fast release time), load the instrument (drums: `DRUM.BNK[note]`; else `MELODIC.BNK[program]`), set the levels, write the frequency, key off and key on. Drums sound at the note in the flag byte of their name.
- Frequency: a table of block and F-number per note from note 12 at driver `0x2f4c`. Notes 0 to 11 read the attenuation table in front of it.
- Level: `level = ((volume << 7) / 127) * velocity >> 7`, `a = table_0x2f00[level >> 1]`, `TL = 64 - (64 - a) * (64 - instrument TL) / 64` with the instrument KSL. The modulator gets it too when the melodic instrument of the channel program is additive (also on the drum channel).
- Start state: channel volume 127, pitch bend 64, bend range 2, program 0, voice velocity 127.
- The sustain bytes of operators 16 to 21 share memory with the low F-number bytes of voices 0 to 5. Controller `0x79` sets the velocity of the voice with the number of the channel.

## Deviations from the original

- OPL chip: Nuked-OPL3 in OPL2 mode, register writes through its write buffer (2 samples per write, like the delay of the real chip). Output: 44100 Hz stereo, the same signal on both sides.
- `fmmidi.com` computes `(note - 12) mod 12` with repeated subtraction. For a note below 12 with pitch bend, this takes about 1.4 billion instructions in the timer interrupt (song 7 has one such note). The port computes the same value directly. Kept quirk: for a note below 12 the semitone is the unsigned wrap of the original (`bend_freq` in `src/ui/fm.c`).
- The frequency table index is clamped to the table; the driver reads outside it for notes below the bend range. MIDI data bytes are cut to 7 bits before the driver tables use them.
- A loop to an id that a track does not have does not move that track (the original searches past the loop records). A track that has no end event ends at its chunk end. A map outside `SONGLIST.DAT` keeps the song (the original reads past the buffer).
- Kept quirk: the program change and the controller pairs of a loop point go to the channel in the low nibble of the byte at the loop point (the status byte of the first event after it), not to the channel of the track chunk (`jump` in `src/data/hmp.c`, `hmi_midi_send_track_setup`).
- Not ported, because no song uses them: the HMI controllers `0x67`/`0x68` (controller enable flags), `0x72`, `0x75`, `0x76`, `0x79` (other loops), `0x77` (trigger callbacks), the HMI fade (`hmi_midi_fade_song`), tempo change, and the channel remap mode (the game turns it off). Like the original, the port does not send these controllers to the driver, but it does not run them.
- `main.c` calls `music_init` after `plat_init` (the original `system_init` calls `sound_init`) and `music_quit` before `plat_quit` (the original `sound_shutdown`). `plat_audio_music(music_render)` connects the music to the SDL audio stream. `plat_wait_tick` fills the audio stream on the main thread, so the music needs no lock. Without `MELODIC.BNK` or `DRUM.BNK`, the port logs a message and plays without music.
- The song calls are at the same places as in the original: `music_update_for_map` once per game step (`step_after_input`), `music_pause` on the pause command, and `music_set_volume` from the volume dialog and after the start menu (the original `sound_set_volume` with device 10). The start menu plays no song: the first song starts at the first game step.
