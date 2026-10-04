# Cross-check with reDM2

This page compares our `SKULL.EXE` analysis with the reDM2 project (https://codeberg.org/CyCl0ne/reDM2). reDM2 analyzes the same `SKULL.EXE` with the same code base `0x10000`.

Rule (plan, section "Decisions"): we read reDM2 for names, formats, logic, and test methods. We do not copy its code. When the port follows reDM2 analysis, the port comment cites it.

Sources in reDM2:

- Names: `r2/skull/rc.r2` (`'af+` function starts, `'f` flags, `CCu` comments) and `include/dm2globals.h` (globals).
- Formats: `docs/formats.md`, `docs/savegame.md`.
- Evidence: `docs/memory.md`, `docs/equiv.md`, `README.md`, and `src/*.c` with `src/*.test.toml` (functions with a differential test).

Our sources: `docs/skull-symbols.csv`, `docs/skull-map.md`, `docs/skull-formats.md`, `decomp/SKULL.c`.

## Method

1. Join both name sets by address. Functions: our 1362 starts and the 1405 reDM2 `'af+` starts. Globals: our 626 globals and the 341 reDM2 `DM2_GLOBAL` addresses.
2. Remove reDM2 placeholder names (`fcn.*`, `crt_sub_*`, `hmi_sub_*`, `noop_*`, `loc.*`, `int.*`, and the encoded signature-match names).
3. Split each name into words and map common synonyms (`dun`/`dungeon`, `gfx`/`image`/`gdat`, `creature`/`actor`, `zone`/`region`, `timeline`/`event`, and others).
4. Shared word: "same meaning". No shared word: read both names, the reDM2 comment, and our decompiled body. Then classify as "compatible" (same role, other abstraction level) or "conflicting" (other role).

reDM2 names are mostly mechanical (`obj_type_value4`, `rec12_index_sift`, `type_attr_e_d`). Our names are mostly game-level (`creature_get_gdat_attr4`, `timeline_fix_queue_slot`, `dun_get_door_attr_0d`). Thus most "compatible" pairs differ only in the level of description.

## Summary

### Functions

| Class | Count |
|---|---|
| Same meaning | 473 |
| Different but compatible | 151 |
| Conflicting meaning | 27 (2 resolved by rename, 25 open) |
| Both have a start, reDM2 has only a placeholder name | 677 |
| reDM2-only starts (inside one of our functions) | 79 |
| Ours-only starts (no reDM2 start) | 34 |

Our 1362 starts = 473 + 151 + 27 + 677 + 34.

### Globals

| Class | Count |
|---|---|
| Same meaning | 145 |
| Different but compatible | 82 |
| Conflicting meaning | 26 (1 resolved by rename, 25 open) |
| reDM2-only | 88 |
| Ours-only | 373 |

## Renames applied

All renames are in `docs/skull-symbols.csv`. The function address set did not change.

| Address | Old name | New name | Reason |
|---|---|---|---|
| `0x46745` | `champ_reduce_by_stat` | `champ_get_stat_adjusted_attack` | reDM2 matches the body with DM1 `F0307_CHAMPION_GetStatisticAdjustedAttack` and tests it (1035 cases). The function scales an attack value by `170 - stat`. "Reduce" did not tell this. The third argument is now `attack`. |
| `0x1f347` | `money_is_coin` | `item_is_stackable` | reDM2: `object_is_stackable`. The body only tests "thing type 10 and attribute 0 bit `0x4000`". The callers (`money_count_coins_in_box`, `money_add_coin_to_box`) then test the coin table themselves. The stack count is in bits 8-13. Subsystem changed to `items`. |
| `0x7f8dc` | `g_save_checksum` | `g_save_bit_total` | reDM2: `g_packed_bit_ctr`. `save_write_packed` and `save_read_packed` add the number of stored bits. No code reads the value for a check. |

Not changed here (owned by other work): `docs/skull-notes/range-2.md` still uses `money_is_coin`. `decomp/SKULL.c` keeps the old names until the next Ghidra export.

## Conflicts left open

We keep our name in each case. The reason is in the last column.

### Functions

| Address | Ours | reDM2 | Why we keep ours |
|---|---|---|---|
| `0x104ea` | `sound_prepare_sample` | `hilite_node_add` | The only caller passes `g_sample_descs`. The XOR of bit 7 converts signed PCM to unsigned. reDM2 marks its role "not confirmed". |
| `0x13898` | `pal_set_palette` | `mouse_shape_upload` | Both are inferences. Payload is GDAT `(1,0,9,254)`, 1024 bytes, `IBMIOP` function `0x10a` in the video group (`0x103`-`0x10a`). Needs the `IBMIOP.EXE` handler body. |
| `0x159c3` | `view_position_to_cell` | `obj_type_remap` | Maps 6, 8, `0x12`, `0x10` to 0-3. reDM2 gives no role. |
| `0x1f180`, `0x1f1fc` | `item_index_to_thing_type`, `item_index_to_gdat_index` | `zone_classify`, `coord_band_normalize` | reDM2 names describe the arithmetic only. The input is the 9-bit global item index. |
| `0x1f309` | `item_is_chest` | `object_is_noncontainer_db9` | Body: type 9, not a money box, flags `& 6 == 0`. A chest is a container; the reDM2 name follows its own name for `money_is_moneybox`. |
| `0x22626` | `view_click_front_wall` | `cursor_suspend_toggle` | Argument 0 plays sound `0x84`. Other values start the right-button mouse capture. Both names cover one branch only. |
| `0x24d88`-`0x33df9` (13 functions) | `creature_ai_*` | `overlay_*`, `sound_enqueue`, `snd_emit_*`, `snd_event_code`, `cond_tally_lookup` | reDM2 reads the creature AI plan context as a "sound queue" and "overlay". Its own globals in the same block are `g_actor_*`. A dump of the plan tables (`0x74f7e`, `0x75248`) decides this. See open question 5 in `docs/skull-map.md`. |
| `0x4e733` | `weather_get_rain_image` | `compass_dir_char` | Body: letter `q` or `m` from wind direction relative to the party, plus 0-3 from `g_rain_intensity`. |
| `0x4e930` | `view_draw_wet_image` | `face_pattern_blit` | Both low confidence. reDM2 covers only the short branch. |
| `0x509e6` | `view_draw_sparkle_field` | `object_slot_draw` | Both low confidence. reDM2 calls the 16 regions "inventory regions", but the three callers are view functions. |
| `0x54f5b` | `mem_parse_config_entry` | `gfx_cmd_stream_step` | The `0xfe` count is the progress bar total in `mem_test_all_maps`, which loads each map once per entry. |
| `0x59026` | `timeline_event_hide_damage` | `champ_mark_dead` | Sets champion `+0x2e` to `0xffff`. Sets flag 8 only if the champion has health. "Dead" does not fit. |

### Globals

| Address | Ours | reDM2 | Note |
|---|---|---|---|
| `0x7054a` | `g_disk_no_swap` | `g_ztoken_savesw` | reDM2 comment: also the "saves are on the hard disk" flag. Compatible use, but the reDM2 name is a string token name. |
| `0x7ef34` | `g_prepared_samples` | `g_hilite_list` | Follows `0x104ea`. |
| `0x7f07c` | `g_palette_set_result` | `g_mouse_absent` | Follows `0x13898`. |
| `0x7f0ae` | `g_mouse_buttons` | `g_cursor_q_h` | reDM2 read the `IBMIOP` handler: the third word is "cursor height". The game tests bit 1 of it for the right button. Needs a DOSBox check. |
| `0x7f258` | `g_party_move_steps` | `g_ui_effect_count` | Our merge already chose between two own names (`docs/skull-map.md`, "Symbol merge decisions"). The reDM2 name is a third reading. Not checked in depth. |
| `0x7f280` | `g_new_game` | `g_rec12_relink_7f280` | Commands `0xd7`/`0xd8` set it, `0xd9` clears it. |
| `0x7f3c0` | `g_cur_map_extra_data` | `g_creature_list` | Not checked in depth. |
| `0x7f54e`-`0x7f570`, `0x7f670`, `0x7f7e6`, `0x7f892`, `0x7f894` (11 globals) | `g_ai_*`, `g_item_filter_free` | `g_sound_*`, `g_snd_*`, `g_overlay_*`, `g_occ_*` | Same cause as the creature AI functions. |
| `0x7f8d0`-`0x7f8d6` | `g_ai_target_y`, `_map`, `_x`, `_dir` | `g_view_y_save`, ... | reDM2: saved view position in `map_switch_save_view` (our `creature_ai_set_map`). Our merge dropped `g_ai_party_*` for these (`docs/skull-map.md`, "Symbol merge decisions"). The reDM2 reading supports "party position saved". Check before Step 16. |
| `0x7fad0` | `g_song_buffer_size` | `g_gfx_max_size_7fad0` | `gdat_find_max_song_size` scans type 3 items. `sound_alloc_buffers` uses the value for `g_song_buffer`. |
| `0x7fbce` | `g_leader_hand_weight` | `g_champ_bonus_7fbce` | Not checked in depth. |
| `0x7fff0` | `g_party_haste` | `g_stat_boost_shift` | The main loop decrements it. Spells add to it. When it is not 0, the action delay is shifted right by 2. |

## Function boundaries

`tests/test_symbols.py` requires that our function addresses are the Ghidra function starts. We keep them. reDM2 has 79 starts inside our functions, and 34 of our starts have no reDM2 start.

reDM2 starts inside our functions (79):

- Probable real entry points that Ghidra joined to the previous function (10): `0x2fdf0` and `0x2fe0a` (`word_sign_gate`, tested in reDM2) after `math_count_bits`; `0x3305a` (2789 bytes) in `creature_ai_search_area`; `0x540ff` in `view_draw_far_squares`; `0x43108` in `ui_draw_party_icons`; `0x48066` in `champ_apply_time_effects`; `0x4be76` in `item_party_possesses_type`; `0x2301d` in `view_click_in_viewport`; `0x24022` in `move_push_creature`; `0x37022` in `save_restore_timed_effects` (reDM2: the switch on the event type). Check these before the port splits or merges them.
- Second entry points of table-called handlers (26): 10 in the `input_cond_*` functions (`0x200c8`-`0x2027c`) and 16 in the `creature_ai_scan_*` and `creature_ai_cmd_*` functions (`0x2765c`-`0x281e9`). The dispatch tables can point into the middle of our functions.
- Not functions (10): `0x1d051` (loop latch, reDM2 says so), `0x3156e` (shared epilogue, reDM2 says so), `0x26e60` and `0x2703c` (switch targets), `0x3ae7a` and `0x3b0ee` (parts of `gdat_load_map_graphics`), `0x4710c`/`0x4710d`, `0x10d07` and `0x14726` (interrupt return points).
- Decoder inner loops (3): `0x5ab4f`, `0x5ad3c`, `0x5ad9e`. Our starts `0x5aada`, `0x5ad2b`, `0x5ad8d` are the pointers in `g_image_decoders`, so our starts are correct.
- Library and HMI code (10): `0x5b0fe`, `0x5efb5`, `0x5f35c`, `0x62001`, `0x62513`, `0x633b5`, `0x63661`, `0x637e9`, `0x64ff4`, `0x68c0f`.
- Short starts near a function end, probably shared tails, not checked (20): `0x140fb`, `0x1c4a1`, `0x1fe66` (reDM2: shared tail), `0x23d0d`, `0x24015`, `0x25c52`, `0x25d41`, `0x28671`, `0x2a351`, `0x2fecd`, `0x2fed3`, `0x3180d`, `0x35b77`, `0x3c0ca`, `0x3c3a5`, `0x3f547`, `0x40d33`, `0x509df`, `0x557a0`, `0x55f48`.

Our starts without a reDM2 start: `system_debug_break` and its alias, `iop_call_return_word`, `input_key_or_button_pressed`, `gfx_fill_end`, `zone_alloc_record`, `system_debug_hook`, 10 `input_cond_*` handlers, `view_click_drop_done`, `creature_square_blocks_line_except_self`, the four GDAT decoder functions, and 11 library functions.

The hidden functions in open question 9 of `docs/skull-map.md` (`0x113dd`, `0x114d2`, `0x14c93`, `0x5b577`, `0x62e81`) have no reDM2 start either.

## reDM2-only globals

88 reDM2 global addresses are not in our CSV. We did not add them. Main groups:

- Text token buffers `g_ztoken_*` (`0x70536`-`0x7f998`).
- Screen and font constants `0x71702`-`0x71746` (`g_font_height` = 6, `g_strip_height` = 7, and others).
- Selected action record `0x7f518`-`0x7f524`.
- Creature AI context bytes `0x7f562`-`0x7f589` and `0x7f7d4`-`0x7f7e8`.
- Image cache state `0x7f9c8`, `0x7fa10`, `0x7fa1c`, `0x7fa8c`, `0x7faba`.
- Memory test counters `0x803f4`, `0x803f6` (our `mem_parse_config_entry` writes them).

Add them when a port step needs them.

## Format findings

Details and citations are in `docs/skull-formats.md`.

Agreements:

- `GRAPHICS.DAT` header, item table, key table (94852 bytes, 11854 rows of 8 bytes), total size 8639757.
- Music: GDAT `(4, song, 3, 0)`, 29 HMP items, item 5602 = `TEST.HMP`, `SONGLIST.DAT` values 0-28.
- `g_timer_step` = 1, `g_tick_threshold` = 8, timer tick from `INT 8` on every 4th interrupt under `+VS`.
- `IBMIOP` functions `0x02`, `0x0e`, `0x0f`, `0x28`, `0x2e`, `0x2f`, `0x30`: same role. reDM2 adds the record layouts.
- Language switch `+GLx` and `g_language` (`0x7576c`).

Added to our document from reDM2:

- The `F` field is the language (1 English, 3 German, 4 French, 0 all, `0xf` never).
- Image compression: gate, codec table `g_image_decoders` (`0x75798`), codecs 2 and 3 are LZSS, codec 1 (LZW) is not used by the shipped file. This answers open question 2 of `docs/skull-map.md` (Step 8).
- The delta table key `(0,0,8,0)` is absent, so `image_decode_4bpp_delta` does not run.
- Save file format: 42-byte header, dungeon image, 7 bit-packed records, thing records (Step 10).
- `IBMIOP` dispatcher facts and the sound configuration record of function `0x2e`.

Disagreements:

- Digital samples: reDM2 says `GRAPHICS.DAT` has no samples. We keep our answer: the 292 type-2 items are 8-bit PCM samples (reader `FUN_0001623b`).
- `IBMIOP` function `0x0e`, third word: buttons (ours) or cursor height (reDM2). Open.
- `IBMIOP` function `0x10a`: palette (ours) or cursor image (reDM2). Open.

Corrections to our document: none of our statements was wrong. The game tick rate stays an open measurement in both projects.

Risk for Step 10: a load moves things of types 4-15 to new slots. A "load, then write" test can thus give a file that is not byte-equal to the input, also in the original game. Plan the Step 10 test with the original writer in `dm2tools.emu` as the reference.

## Test methods worth adopting

reDM2 verifies by functional equivalence, not by byte matching (`README.md`, "How verification works"; `docs/equiv.md`). These methods map to our `dm2tools.emu` oracle (`src/dm2tools/emu.py`).

| reDM2 method | What it does | How we use it with `dm2tools.emu` |
|---|---|---|
| Memory-write diffing (`tools/difftest.py`) | Runs the original and the port from the same state. Compares `EAX`, the full set of memory writes (Unicorn `UC_HOOK_MEM_WRITE`), the write order, the stack balance, and the saved registers. | Add an optional write recorder to `Emu.call` (one `UC_HOOK_MEM_WRITE` hook that stores address, size, value). The C port runs natively on a copy of the same memory buffer. The test compares the changed bytes of both runs, not only the return value. |
| Stack and buffer poison | Runs each case with a zero stack, a stack poisoned on one side, and a stack poisoned on both sides. A port that reads bytes the original never wrote fails. | Fill the `emu.py` stack and scratch area with a pattern (for example `0xa5`) in a second run of each oracle test. The result must not change. |
| State snapshots from the real load path (`tools/objstate.py`) | Runs the original `game_load` → `dungeon_load` → packed records → thing records in the emulator over a real save. Only file I/O and `int 0FCh` leaves are stubbed. The memory dump is the fixture for many tests. | For Steps 9 and 10, run `game_load` (`0x370d2`) or `dungeon_load` (`0x36909`) in `dm2tools.emu` with stubs for `clib_open`, `clib_read`, `clib_lseek`, and `clib_close`. Save the memory as a fixture. Compare the C loader result with it. No hand-made global state. |
| Mutation checks (bare `ret`, constant return) | Replaces the port body with `ret` or `return <constant>`. A good test must then fail. A test that still passes asserts nothing. | For each new oracle test, add one negative control: a broken C variant (or a patched original byte in the emulator) must make the test fail. |
| Whole-file sweeps (`src/gfx_sweep_*.test.toml`) | Decodes 5519 of 5623 `GRAPHICS.DAT` items with the original and the port and compares all writes. | This is the Step 8 test. Skip the 1442 items that are not images (gate outside `{0, 31, -32}`). |
| Sibling fixtures for state-gated branches | A branch that depends on the game state gets two fixtures. Removing the guarded call must make exactly one of them fail. | Use two saves (start of game and later game) for state-dependent functions in Steps 9-17. |
| Runnable "blocked" records (`src/*.blocked.toml`) | A function that cannot run in the emulator keeps a test that must stay red. If it turns green, the blocker is gone. | When `dm2tools.emu` cannot run a function (DOS/4GW selector load, port I/O), keep an expected-failure test (`pytest.mark.xfail(strict=True)`) instead of a note. |
