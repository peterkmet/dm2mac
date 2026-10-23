# Range 8 notes (0x00062272-0x000695ac, 211 functions)

Method: names come from the disassembly (INT 21h and INT 31h function numbers, PIT ports, data tables) and from the call sites in range 7. 80 names are low confidence (see the end). Marker, clib, and DPMI names are firm unless listed there.

## Layout

| Range | Content |
|---|---|
| 0x62272-0x6258a | Watcom startup (`cstart_entry` 0x62284, `clib_cstart` 0x622fc), exit |
| 0x6258a-0x62efd | clib: memmove, memset, file I/O over INT 21h, setjmp/longjmp, hard error handler |
| 0x62efd-0x63138 | HMI PIT and IRQ 0 code (program PIT, install and restore ISR, mask IRQ 0) |
| 0x63189-0x63a11 | DPMI wrappers (INT 31h), VDS (INT 4Bh), HMI driver call stubs (call into a loaded HMI*.386 driver by function number) |
| 0x63a21-0x64114 | HMI driver loader and unloader for HMIDRV.386 (digital) and HMIDET.386 (detect) |
| 0x64114-0x64335 | far strcpy, DOS far read, MIDI driver info query |
| 0x64335-0x6444c | statically linked MIDI driver 0xa003 (forwards events to a user callback) |
| 0x64427-0x65a26 | statically linked MIDI driver 0xa005 (plays samples through the digital driver, HMIDIGIP patch banks) and the voice queue helpers |
| 0x65a3b-0x67200 | HMI MIDI sequencer tick (`hmi_midi_song_tick`) and track setup |
| 0x671b0-0x680f4 | statically linked MIDI driver 0xa007 (same design as 0xa005, with pitch and patch search) |
| 0x680f4-0x68198 | driver call stubs for the MIDI driver loader |
| 0x68198-0x68990 | clib heap (near heap: alloc, free, grow, DPMI or DOS segment allocation) |
| 0x68990-0x695ac | clib: errno, io mode, argv, environ, stdio streams, tmpnam, write |

The HMI MIDI driver ids are chosen in `hmi_midi_init_driver` (0x5dd45): 0xa003, 0xa005, and 0xa007 call the static drivers above. All other ids load `hmimdrv.386` through `hmi_load_midi_driver_file` (0x655e6, ids 0xa000-0xa200). Digital drivers load `hmidrv.386` (0x63adc, ids 0xe000-0xe200). The detect driver loads from `hmidet.386` (0x63dd7, ids 0x1000-0x1023).

## Structures

HMI driver function table (static MIDI drivers, 6 bytes per slot: offset, selector). One table per driver handle at `0x79188 + handle * 0x48`, 12 slots:

| Slot | Role | Called from |
|---|---|---|
| 0 | send MIDI event | sequencer (`hmi_midi_song_tick`, `hmi_midi_send_track_setup`) |
| 1 | init | `hmi_midi_init_driver` |
| 2 | uninit | `hmi_midi_uninit_driver` |
| 3 | reset | 0x5e3xx |
| 4 | set instrument data | `hmi_midi_set_ins_data` |
| 5-7 | set channel parameter, reset channel (0xa007 only), reset controllers (0xa007 only) | not seen |

Driver call stub convention (0x633ea-0x636f5, 0x680f4-0x6816f): `entry` is the far entry of a loaded HMI*.386 driver. The stub puts the function number in EAX (0-10) and calls `entry`. Function 0 init, 1 status, 2 and 8 copy a 0x6a byte info structure, 4 returns two tables, 10 starts a transfer. Roles are guesses from the register use.

Voice queue (`g_hmi_voice_queues`, 0x7948c, 0x200 bytes per driver handle, 32 entries of 16 bytes), ring buffer:

| Offset | Use |
|---|---|
| +0 | voice id (-1 = free) |
| +4 | sample or patch pointer |
| +8 | volume |
| +0xc | key (note) |

Head `g_hmi_voice_queue_head`, tail `g_hmi_voice_queue_tail`, count and capacity are int32 arrays indexed by handle (0x79e8c, 0x79ea0, 0x79ec8, 0x79eb4).

Digital patch bank (`hmi_mdrv_a005_set_instrument_data`): signature `HMIDIGIP` (0x7ae7c), 0x80 entries of 6 bytes (far pointers per program), entry record: +0x18 program, +0x24 root note, +0x2c next offset, +0x38 size, +0x74 header size.

Stdio stream (26 bytes = 0x1a, `g_clib_static_streams` 0x7eb30, 5 entries, end 0x7ebb2): +0 current ptr, +4 count, +8 buffer, +0xc flags (byte), +0xd flags2 (0x10 dirty, 0x08 temp file, 0x20 error, 0x40 static), +0x10 DOS handle, +0x19 temp file index. Open streams are a linked list at `g_clib_open_streams` (0x808e4), unused nodes at `g_clib_free_streams` (0x808cc).

DOS extender type: `g_clib_extender_type` (0x7eaae) is set in `clib_cstart` from the DOS/4GW detection (values 1 = "DX"/DOS/4G family check path 0x4458, 9 = other protected mode, 0 = plain). `clib_cstart` also finds `no87=` in the environment.

## PIT and timer (Step 5 open point)

- `hmi_timer_program_pit` (0x62efd): writes mode `0x36` to port 0x43 and the 16-bit divisor to port 0x40. `hmi_timer_install_isr` (0x62f43) does the same after it hooks IRQ 0 (INT 8): via INT 31h 0204/0205 when `g_hmi_use_dpmi_vectors` is set, otherwise INT 21h 35h/25h.
- The ISR is at 0x62e81. It lies inside the Ghidra function 0x62e52 (`clib_dos_setvect`). It calls `g_hmi_timer_callback` (0x761e8), sends EOI (port 0x20), and returns. `hmi_timer_leave_isr_stack` (0x630d9) restores the interrupted stack and chains to the old vector.
- `hmi_timer_restore_isr` (0x6305d) restores the old vector and programs divisor 0 (rate 18.2 Hz).
- The divisor value comes from the caller (range 7, `hmi_timer_set_pit_divisor`). `sound_init` passes 240 Hz, divisor 4971. This range adds nothing new.

## Ghidra boundary problems

- 0x62e52 (`clib_dos_setvect`) also holds the PIT ISR (0x62e81) and a callback wrapper.
- 0x637b4 holds a two-call driver stub (functions 0xb and 0xc) before `dpmi_get_free_memory_info`.
- 0x6338a (`dpmi_free_dos_memory`) is followed by `hmi_drv_call_fn8_copy_info` inside the same listing.
- 0x68e4f is `clib_abs`; the code from 0x68e53 on is a separate function (swap of the heap limit) and 0x68e65 is `sbrk`.
- 0x691cf starts with a debug break stub (`[0x7ed6c]` set), the stream allocator starts at 0x691f3.
- 0x69003 starts with `clib_runtime_fatal`; the environment builder (`__setenvp`, fills `g_clib_environ`) starts at 0x6901d.
- 0x69530 continues past 0x695ac in the listing; the function writes with INT 21h 40h and compares the count.
- 0x639d9 (`dpmi_free_selector`) is followed by tiny `get ds` and `get cs` functions.
- 0x68bf4 holds three stack check helpers (store SS, stack probe, SS compare).

## Anchor corrections

None renamed. Notes:
- `dpmi_simulate_real_int` (0x638a0) is VDS function 8107h (request DMA buffer) through INT 31h 0300 to INT 4Bh. The new name for the sibling at 0x6390c is `dpmi_simulate_real_int_vds_release` (8108h).
- `dpmi_map_physical_address` (0x63959) takes the address in the stack, returns a new selector. Signature in anchors.csv is wrong (it says no parameters and returns uint32_t).
- `hmi_load_digi_driver_file` (0x63adc) and `hmi_load_midi_driver_file` (0x655e6) take 9 stack-like parameters; signatures in anchors.csv show `void`. Step 5 needs only the names.

## DM2-specific systems

None in this range. Weather, minions, money and shops, Skullkeep machines, and the outdoor view are not here. This range holds only Watcom library code and HMI driver code. The port replaces all of it (no DOS/4GW, no HMI, no INT 21h).

## Open questions

- Which HMI function does driver call stub number N map to? Needs the HMI*.386 driver entry tables (in `HMIMDRV.386` and `HMIDRV.386`, not in `SKULL.EXE`).
- Meaning of ids 0xa003, 0xa005, 0xa007. The code shows: 0xa003 forwards to a callback, 0xa005 and 0xa007 are sample-based. Names use the numeric ids.
- Whether the static drivers 0xa005 and 0xa007 are used by the game at all. The game calls `hmi_midi_init_driver` with the id from the sound configuration; the OPL id probably goes to `hmimdrv.386`.
- Exact semantics of the 0x65a3b tick (fade modes 0x7e2a9, counters 0x7e309 and 0x7e329). Not needed for the port.

## Low confidence names

