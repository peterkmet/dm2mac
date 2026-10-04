# Range 7 notes (0x0005a87c - 0x00062272)

114 functions. All of them are library code. The port replaces it, except the decompressors (rows 1-5).

## Anchor corrections

None. All 38 anchors kept.

## Boundary notes

- 0x5b57c is the middle of a timer ISR entry stub. The real start is 0x5b577 (`push ebx` ... `call 0x630d9` ... `retf`). The Ghidra address is kept in the CSV.
- 0x5b4b2 ends with a `retf` at 0x5b576. It is the ISR body `hmi_timer_dispatch_events`.
- 0x5b57c, 0x5b596 and 0x5b5ab are three different empty or tiny stubs. 0x5b596 and 0x5b5ab are empty.

## GRAPHICS.DAT decompressors (0x5a95b-0x5ad8d)

Table `g_gdat_decompressors` at 0x757a0, 8 bytes per entry: `decode`, `init`.

| Entry | decode | init | Method |
|---|---|---|---|
| 0 | 0x5aada `gdat_lzw_decode` | 0x5acf4 `gdat_lzw_alloc_tables` | LZW with 9 to 12 bit codes (code 0x100 = clear, 0x101 = first free code), followed by RLE with escape byte 0x90 (0x90 0x00 = literal 0x90, 0x90 n = repeat last byte) |
| 1 | 0x5ad2b `gdat_lzss12_decode` | none | LZSS, 8 flags per control byte (bit 1 = literal). Match: 2 bytes, length = (b0 & 0x0f) + 3, distance = b1 * 16 + (b0 >> 4) |
| 2 | 0x5ad8d `gdat_lzss13_decode` | none | LZSS. Match: length = (b0 & 0x1f) + 3, distance = b1 * 8 + (b0 >> 5) |

- Signature of all three decoders: `(src, dst, src_size)`, return value = output size.
- `gdat_open` (0x3d3xx) calls the init function `[0x757a4]` when the GDAT flag 0x40 of item `(0,0,0x0b,0)` is set. Callers of the decode entries use the table with an index; no direct call exists. The caller, which picks the entry by the compression type, is still open (step 8 needs it).
- `gdat_lzw_alloc_tables` allocates two buffers with `mem_alloc`: sizes 0x3aa1 (prefix table, 16-bit entries) and 0x138b (suffix table).
- LZW decoder state: `g_lzw_*` (0x80490-0x804ac). `gdat_lzw_read_code` reads variable width codes from the source. Width grows when `g_lzw_next_code` passes `g_lzw_max_code`.
- 0x5a95b has no direct caller besides 0x5aada. This answers the open point in notes.md.

## IOP glue (0x5a87c-0x5a8f3)

Four functions, same as anchors. Offsets in `g_iop_info` (0x7618c): +0x3dc input info pointer (stored in `g_iop_input_info` 0x80484), +0x3e0 input event pointer (`g_iop_input_event` 0x80488), +0x3ec shared buffer. Pointers use the form `(x >> 12 & 0xffff0) + (x & 0xffff)` (real mode seg:off to linear).

## HMI structures (SOS, probable names)

### Timer (0x5ae04-0x5b5ab)

- 16 event slots. `g_hmi_timer_callbacks` (0x77c13, 6 bytes: offset + selector), `g_hmi_timer_event_rates` (0x77c73), `g_hmi_timer_event_steps` (0x77cb3, 16.16 step per PIT tick), `g_hmi_timer_event_accumulators` (0x77cf3).
- `g_hmi_timer_divisor` (0x77c0f): current PIT divisor (0xffff = 18.2 Hz). The divisor goes down only when a new event has a higher rate (see `hmi_timer_register_event`, `hmi_timer_change_event_rate`).
- `hmi_timer_dispatch_events` adds step to the accumulator of each active slot. Bit 16 of the sum fires the callback. `g_hmi_timer_event_song` (0x77d33) sets `g_hmi_timer_current_song` (0x77d43) for song events.
- Rate 0xff00 = chain to the old INT 8 handler at the BIOS rate.

### Digital driver (0x5bb47-0x61aa5)

- Up to 4 drivers (`g_hmi_digi_driver_ids` 0x773b6, 4 bytes each). `g_hmi_digi_sample_table` (0x773e8): per driver 0xc0 bytes = 32 samples x 6 bytes (offset + selector pointer to the sample record).
- Sample record (accessed with GS-relative far pointers):

| Offset | Use |
|---|---|
| +0x00 / +0x08 / +0x10 | start pointer, copy of start pointer, end pointer (start + length) |
| +0x18 / +0x1c / +0x20 / +0x2c | current length / loop length data (set in `hmi_digi_start_sample`) |
| +0x30 | flags (0x8000 = active, 0x2000, 0x1000 = paused, bit 0x40 of the start struct +0x28 = loop mode) |
| +0x32 | volume (from start struct +0x14) |
| +0x34 | sample id (from start struct +0x18) |
| +0x36 | channel (from start struct +0x10, SKULL passes 2) |
| +0x38 | loop count (start struct +0x0c) |
| +0x3c / +0x40 | start struct +0x1c / +0x20 |
| +0x44 | rate or similar (start struct +0x38) |
| +0x4a | start struct +0x24 |
| +0x4c | cleared at start, readable (position or progress) |
| +0x50 | start struct +0x08 (length) |
| +0x54 | pan (start struct +0x40) |
| +0x56 | pan speed (start struct +0x44) |
| +0x58..+0x60 | start struct +0x48..+0x58 |

- Start struct (SKULL fills it at 0x703f8): +0 pointer, +8 length, +0x10 channel 2, +0x14 volume, +0x18 id, +0x28 flags 800, +0x40 pan. See `docs/skull-formats.md`.
- `g_hmi_digi_driver_table` (0x777a8) holds driver structs. `hmi_digi_set_master_volume` writes +4.
- Names for field +0x36, +0x38, +0x44, +0x4c, +0x56 are low confidence.

### Detection (0x5c97a-0x5d568)

- `HMIDET.386` (string at 0x70148, 12 chars with `.386`) is opened by `hmi_detect_init`. Header 0x2c bytes (`g_hmi_detect_header` 0x77f46, device count at +0x20 = 0x77f66), then device records of 0x30 bytes (record fields: +0x24 id (0x77f9a), +0x29 flags (0x77f9f), +0x24 offsets).
- `hmi_detect_find_hardware` checks ids 0xe000-0xe200 (digital cards). `hmi_detect_select_hardware`, `hmi_detect_find_first_hardware`, `hmi_detect_find_next_hardware` are the same loop in three variants.
- Result of `hmi_detect_get_hardware_caps`: port, irq, dma, drq written to the caller struct (port +0, irq +4, dma +8, +0xc). IRQ must be 2-15.

### MIDI driver and song (0x5db99-0x62160)

- Max 5 drivers (`g_hmi_midi_driver_ids` 0x782dc, loop `< 5`). Driver ids 0xa003, 0xa005, 0xa007 and 0xa00a select a built-in call table (`0x7942c`, `0x79edc`, `0x7e345`). Id 0xa00a also registers a 120 Hz timer event.
- Driver call table `g_hmi_midi_driver_call_table` (0x79188): 0x48 bytes per driver = 12 far pointers. Slot 0 send MIDI bytes, slot 1 init, slot 2 uninit, slot 3 (0x7919a) set channel/volume data, slot 4 (0x791a0) set instrument data.
- Max 8 songs. `g_hmi_midi_song_data` (0x79370) points to the HMP header. Per song 0xc0 bytes of track pointers (`g_hmi_midi_track_cursors` 0x78318, 32 tracks x 6 bytes), track table pointers at `g_hmi_midi_song_track_devices` (0x79118), counts at 0x79148 and 0x79168.
- HMP header fields used: +0x30 track count, +0x38 timer rate, +0x380/0x384 branch table pointer, +0x388 track directory. Track entry: +4 length, +8 channel, +0x18 stride for branch records (key at +4, count at +7, data pointer at +8).
- Channel mapping (`g_hmi_midi_channel_map` 0x7b485, owner tables 0x7b755/0x7b7a5, instrument tables 0x7b7f5-0x7b7f9, 5 bytes per entry) allocates device channels when `g_hmi_midi_channel_remap` (0x7c0c9) is set.
- Volume: `hmi_midi_set_song_volume` stores `g_hmi_midi_song_volumes[song]` and sends controller 7 for each active track. Value = channel volume * song volume >> 7 * master >> 7.
- `hmi_midi_fade_song` stores fade state in 0x7e2a9-0x7e309 (8 entries each). The timer callback at 0x65a36 (range 8) processes it.

## DM2-specific systems

None in this range. There is no weather, minion, money, shop, Skullkeep or outdoor view code here. The only game-visible links are `sound_init`, `sound_shutdown`, `sound_play_requests`, `sound_set_volume`, `music_play_song`, `music_stop`, `music_update_for_map`, `music_pause` (0x10a86) and the GRAPHICS.DAT loader (`gdat_open` for the decompressors).

## Open questions

1. Which function selects the decompressor entry by GDAT item compression type (callers of `[0x757a0 + n * 8]` use an index form that the byte search did not find).
2. Fields +0x36, +0x38, +0x44, +0x4c of the digital sample record. Names are guesses.
3. `hmi_midi_register_branch_callback` and `hmi_midi_register_loop_callback`: the two table addresses (0x7caa8 and 0x7ca78) are consumed by 0x65a3b in range 8. The assignment is a guess.
4. `hmi_detect_get_hardware_caps` field offsets in the caller struct.
5. `hmi_timer_isr_entry` name: the function 0x5b57c is the inner part of a stub that starts at 0x5b577.
