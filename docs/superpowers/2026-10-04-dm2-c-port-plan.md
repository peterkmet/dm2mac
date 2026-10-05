# Dungeon Master II C port: implementation plan

## Goal

Produce a native macOS port of Dungeon Master II: The Legend of Skullkeep (PC DOS, English, 1995) in C11 with SDL3. The C code is a readable reimplementation of the game code in `SKULL.EXE`. It is derived from our own Ghidra decompilation.

PRD: `docs/superpowers/2026-10-04-dm2-c-port.md`. Each executor reads the PRD and this plan.

## Decisions

- Basis: our own decompilation of `SKULL.EXE`. ReDMCSB, SKWin, reDM2 (https://codeberg.org/CyCl0ne/reDM2), and similar projects may be read and cited for names, data formats, game logic, and test methods. Do not copy their code 1:1. Cite the source in a comment when the port follows their analysis.
- Reuse from `../dm`: copy and adapt tools, Ghidra scripts, the Makefile, and `docs/coding-standard.md`. `dm2` has no run-time or build dependency on `../dm`.
- Fidelity: a reimplementation, not an emulation. Game rules follow the decompiled code exactly (PRD, section "Fidelity rules"). The DOS technical layer is not reproduced: no DOS/4GW, memory manager, image cache, HMI drivers, VGA registers, Mode X, or busy waits.
- Scope: the game in `SKULL.EXE` from the start menu to the end of the game. Intro, ending movie, and credits (`INTRO`, `END`, `CREDITS`, `FTL`, `SPLASH`) are out of scope.
- Language: C11, `clang`, plain `Makefile`. SDL3 from Homebrew (`brew install sdl3`). No CMake.
- Integer semantics: the original compiler (Watcom C/C++32) uses 32-bit `int`. Use `int16_t`/`uint16_t` only where the original uses 16-bit values. Keep overflow behavior.
- Data files are little-endian. Read them with explicit byte order.
- Screen: 320×200 indexed framebuffer (`uint8_t`, palette index per pixel), 256-color VGA palette. The platform layer converts it to RGB and scales it to a window with scale 3.
- Python: `uv` only, `pyproject.toml` + hatchling, `requires-python >= 3.14`, `src/` layout, package `dm2tools`.
- Oracle for tests: the original code, run function by function in the Unicorn x86 CPU emulator (`dm2tools.emu`). Arguments follow the Watcom register convention: EAX, EDX, EBX, ECX, then the stack. Use it for pure functions: same input, same output.
- Code style: `docs/coding-standard.md`. All C code follows it from Step 6.
- Tests: `uv run pytest` is the single test runner. The C code is built as `build/libdm2.dylib` and tested through `ctypes`. Each step adds at least one automated test.
- Game files stay in `game/` (git-ignored). Tests that need game files skip when `game/` is missing. DOS saves for tests are in `tests/saves/` (git-ignored).
- Branch: `feature/2026-10-04-dm2-c-port`. Commits follow Conventional Commits.

## Repository layout (target)

```text
game/                    Game files (git-ignored)
decomp/                  Ghidra output (git-ignored)
build/                   Build output (git-ignored)
src/dm2tools/            Python tools: le.py (LE loader), emu.py (Unicorn harness)
tools/ghidra/            Ghidra headless scripts
docs/                    Coding standard, symbol map, data formats
docs/skull-symbols.csv   Address, name, signature, subsystem for SKULL functions and globals
docs/skull-map.md        Entry point, main loop, call graph, key data structures
docs/superpowers/        PRD, plan, status
port/Makefile
port/src/*.c, *.h        Game code by subsystem, platform layer, opl3.c
tests/                   pytest tests (Python tools and C port)
tests/saves/             DOS saves from DOSBox (git-ignored)
```

## Manual inputs from the user

These inputs come from the user. The steps that need them name them.

- DOS saves from DOSBox in `tests/saves/`: a save at the start of the game (Step 10), saves before and after a short command sequence (Steps 14 to 17), a save near the end of the game (Step 20).
- Ghidra and OpenJDK 21 from Homebrew (Step 4).

## Steps

Each step ends with green tests and a commit. A fresh subagent executes each step. The `reviewer` agent reviews each step. From Step 8, each step lists the original functions it ports from `docs/skull-symbols.csv`.

### Step 1: Repository scaffold

- Run `git init` in `dm2/` and create the branch `feature/2026-10-04-dm2-c-port`.
- Move `Dungeon Master II for PC English/` to `game/`.
- Add `.gitignore`: `game/`, `decomp/`, `build/`, `.venv/`, `__pycache__/`, `.pytest_cache/`, `.DS_Store`, `tests/saves/`.
- Add `pyproject.toml` (based on `../dm/pyproject.toml`): package `dm2tools` in `src/dm2tools/`, dependency `unicorn`, dev group `pytest`, `pytest-timeout`. Script `dm2-le = "dm2tools.le:main"`.
- Add `tests/conftest.py` with a `game_dir` fixture. The fixture skips the test when `game/SKULL.EXE` is missing.
- Copy `../dm/docs/coding-standard.md` to `docs/coding-standard.md`. Adapt it for x86: remove 68000 references, use `docs/skull-symbols.csv`, use DM2 examples.
- Test: `tests/test_scaffold.py` checks that `game_dir` contains `SKULL.EXE` and `DATA/GRAPHICS.DAT`, and skips without `game/`.

### Step 2: LE loader

- Add `src/dm2tools/le.py`. It parses the MZ stub and the LE header, the object table, the page table, and the fixup tables. It places each object at its base address, applies all fixups, and writes a flat memory image.
- `uv run dm2-le game/SKULL.EXE build/skull.bin` writes the image and prints the base address and size of each object and the entry point (`CS:EIP`).
- Expose `load_image(path) -> Image` with `data: bytes`, `base: int`, `objects: list`, and `entry: int` for Steps 3 and 4.
- Tests:
  - A synthetic LE file built in the test (one code object, one data object, one 32-bit offset fixup) loads with the correct bytes and fixup value.
  - With `game/`: the entry point is inside the code object, and the image contains a known string from `SKULL.EXE` (for example `SKSAVE`) at a data object address.

### Step 3: Emulator harness

- Add `src/dm2tools/emu.py`. It loads the image from `dm2tools.le.load_image` into Unicorn (`UC_ARCH_X86`, `UC_MODE_32`), sets up a flat stack, and calls a function at an address. Arguments go in EAX, EDX, EBX, ECX, and the rest on the stack (Watcom convention). It returns EAX and gives read and write access to memory.
- Stop on the return address with a hook. Raise an error on `IN`, `OUT`, `INT`, and on calls into DOS/4GW, unless the caller allows it.
- Test: run a small pure function from `SKULL.EXE` (for example a runtime library helper such as `strlen` or a 64-bit divide) and check the result for several inputs. Test that an `INT` instruction raises the error.

### Step 4: Ghidra decompilation

- Copy `../dm/tools/ghidra/` to `tools/ghidra/`. Adapt `decompile.sh` to the processor `x86:LE:32:default` and the base address from `dm2-le`.
- Keep `ApplySymbols.java` (reads `docs/skull-symbols.csv`) and `ExportC.java`. Remove `InferParams.java` and `ResolveJumpTable.java` if they are 68000-specific. Add a pre-script that sets the Watcom register calling convention on all functions, so Ghidra shows parameters in EAX, EDX, EBX, ECX.
- Output: `decomp/SKULL.c` with C code for each function.
- Test: `tests/test_ghidra.py` (skips without Ghidra or `game/`) decompiles the image and checks that the entry point function is in `decomp/SKULL.c` and that a test symbol from a small CSV is applied.

### Step 5: SKULL map and analysis

- Classify all functions of `SKULL.EXE` and the main globals by subsystem: runtime library, memory, file I/O, graphics, palette, text, input, sound, music, dungeon data, dungeon view, outdoor view, weather, day and night, interface, champions, inventory, items, money and shops, creatures, minions, projectiles, magic, timeline, Skullkeep machines, main loop, save and load, start menu, end of game.
- Give each function a descriptive name and a C signature. Write `docs/skull-symbols.csv` and `docs/skull-map.md` (entry point, main loop, call graph between subsystems, key data structures with offsets).
- Answer the open points of the PRD and record the answers in `docs/`: format of digital sounds, location of HMP songs, effect of the SVGA option, purpose of `DUNGENB.DAT` and `GRAPHIC2.DAT`, the MIDI to OPL conversion in `HMIMDRV.386`, the PIT divisor that SKULL.EXE programs; update `PIT_DIVISOR` in `port/src/platform.c` if it differs from the provisional 65536.
- Re-run the decompiler with `ApplySymbols.java`.
- Test: a pytest validates the CSV (unique addresses, every address is a function start in `decomp/SKULL.c`, no empty names, each subsystem from the list is valid).

### Step 6: C scaffold and platform layer

- `port/Makefile` (based on `../dm/port/Makefile`) builds `build/dm2` (game) and `build/libdm2.dylib` (for tests), with `-Wall -Wextra` and no warnings.
- Platform layer `port/src/platform.c`: SDL3 window, 320×200 framebuffer with 256-color palette (VGA 6-bit values converted to 8-bit), scale 3 (integer scale, nearest), keyboard and mouse events in original coordinates, timer tick at the PIT rate of the original (from Step 5), audio stub.
- `main.c` reads `--data DIR` (default `game`), shows a test pattern, and quits on Escape.
- Test: pytest builds the library and checks the palette conversion through `ctypes` (6-bit 0, 32, 63 give the 8-bit values of the conversion that the original uses).

### Step 7: Upscaling

- Add Scale2x (EPX) and Scale3x in the platform layer. `--filter NAME` and the `DM2_FILTER` environment variable select `none`, `scale2x`, or `scale3x`. The command-line option wins over the variable. An unknown name prints an error and the program exits with code 2.
- Test: Scale2x and Scale3x on a small known image give the known output. A test runs `build/dm2 --filter bad` and checks exit code 2.

### Step 8: Graphics data

- Load `GRAPHICS.DAT` (header, item table, compressed images, palettes). Port the image decompression from `SKULL.EXE`.
- Test: decode every image item and compare it with the original decode function run in `dm2tools.emu`. A truncated or wrong file gives an error code, not a crash.

### Step 9: Dungeon data

- Load `DUNGEON.DAT` (header, maps, object lists, text). Port the loader logic from `SKULL.EXE`.
- Test: map count, map sizes, square data, and object counts are equal to the values that the original loader gives in `dm2tools.emu`.
- Deferred: the two post-load passes of a new game, `dungeon_link_things_after_load` (teleporter actuator numbering, object list merge, text square marks) and `dungeon_read_level_table` (level digits from the text on the party start square). The C loader does not do them yet. `dungeon_square` and the thing records return the file data before these passes. Owner: open, the user decides. Proposal: Step 14 (first step that moves the party and needs linked squares). Step 10 checked: the save file bytes do not need them. A save load calls `dungeon_load(0)`, which skips the link pass. But `dungeon_load` reads the level digit table on the first load of a run (flag `0x75529`), also when that load is a save, and the table is not in the save file. Thus whoever ports `dungeon_read_level_table` also calls it after the first `save_load`.

### Step 10: Save format

- Port the save file reader and writer (`SKSAVE*.DAT`, `SKSAVE*.BAK`) in the original format.
- Input: a DOS save from DOSBox in `tests/saves/`.
- Test: load the DOS save and write it again. The written file is byte-equal to the original file. A damaged save gives an error code, not a crash.
- Done. Measurement (`docs/skull-formats.md` section 7): the original load followed by the original save is not byte-equal to `start.DAT` (4292 bytes differ in the dungeon image, the bit stream is equal); a second original round trip is byte-equal. The test thus compares the C output with the original writer output (`tests/test_save.py`), not with the input file.
- Added: `dungeon_read`/`dungeon_write` and the thing list functions in `dungeon.c`, `timeline_build_queue`/`timeline_compact` in `timeline.c`, `Emu.run` and the `file_write` capture in `dm2tools.emu`.
- Deviations and the parts that later steps add (item effects, timed effects, active groups): `docs/skull-formats.md` section 7.

### Step 11: Random numbers and timeline

- Port the random number generator and the timeline queue (add, remove, next event).
- Test: with the same seed, the C port and the original code in `dm2tools.emu` generate the same 1000 random numbers. Timeline add and remove give the same queue order as the original.

### Step 12: Dungeon view and outdoor view

Reimplement the dungeon view and the outdoor view on the framebuffer. Use the drawing order and coordinate tables of the original. The outdoor view has no separate renderer: `view_draw_dungeon` adds a sky layer (`docs/skull-map.md`, section "DM2-specific systems"). Step 12 has two parts. Each part ends with green tests, a commit, and a `reviewer` pass.

Common rules for 12a and 12b:

- The view state comes from a loaded save. `build/dm2 --save PATH` loads the save with `save_load` and draws the view at the party position. Without `--save`, `build/dm2` shows the test pattern. A save load skips the link pass (Step 9, "Deferred"), but the save data is already linked.
- The light level and the weather state are read from the save. Their update logic is Step 17.
- Not in Step 12: click zones and hit tests (`view_click_*`, `view_hit_test_*`, `view_add_click_zone`, `view_set_click_zone`, `zone_id_for_view_*`): Step 14. Weather and light simulation (`weather_init`, `weather_schedule_event`, `world_update_day_night`, `light_update_party_light`, `light_add_square_light`): Step 17.

#### Step 12a: Drawing primitives, view squares, walls, and sky

- Original functions: `gfx_blit` with the row functions `gfx_blit_row_*` and `gfx_blit_4bpp_to_4bpp`, `gfx_blit_4bpp_to_8bpp`, `gfx_blit_8bpp_to_8bpp`, `gfx_blit_8bpp_mapped`, `gfx_blit_scaled_4bpp`, `gfx_blit_scaled_8bpp`, `gfx_blit_tiled_4to8`, `gfx_blit_tiled_4to8_masked`; `zone_get_rect`, `zone_get_rect_clipped`; `gfx_ctx_init`, `gfx_ctx_prepare`, `gfx_ctx_draw`, `gfx_blit_image`, `gfx_blit_to_viewport`; `viewport_alloc`, `view_clear_viewport`, `view_setup_picture`, `view_flush_picture`, `view_draw_picture_in_zone`, `view_copy_image_mode1`, `view_get_depth_shading`, `view_get_shaded_palette`, `view_inverse_scale`, `view_get_flip_parity`; `view_load_square`, `view_mark_hidden_squares`, `view_unhide_linked_squares`; `view_draw_dungeon`, `view_draw_far_squares`, `view_draw_party_square`, `view_draw_wall_square`, `view_draw_wall_block`, `view_draw_wall_ornament`, `view_wall_text_glyph`, `view_draw_floor_ornament`, `view_draw_ceiling_pit`, `view_draw_floor_pit`, `view_draw_door`, `view_draw_door_front`, `view_draw_door_frame`, `view_draw_door_ornament`, `view_draw_stairs_side`, `view_draw_stairs_front`; `outdoor_draw_sky`, `outdoor_draw_sky_object`, `outdoor_place_sky_object`, `outdoor_relative_distance`, `weather_load_sky_object`.
- `view_draw_dungeon` calls the thing drawing of 12b. In 12a, this call is an empty function.
- Test: unit tests for `gfx_blit` (opaque, transparent, mirror 1, 2, and 3, 4bpp to 8bpp, colormap). A differential test of `zone_get_rect_clipped` against `dm2tools.emu`. A differential test of `view_load_square` and `view_mark_hidden_squares` on `tests/saves/start.DAT`: the 23 view square records are equal to the original records. A manual visual check: `build/dm2 --save tests/saves/start.DAT` and the same save in DOSBox.
- Done. Modules: `gfx.c` (blitters, scalers, color dimming), `zone.c` (layout zones), `draw.c` (draw context, decoded images), `view.c` (view squares, walls, doors, stairs, pits, ornaments, sky); additions in `dungeon.c` and `gdat.c`. Tests: `tests/test_gfx.py`, `tests/test_zone.py`, `tests/test_view.py`. Beyond the planned tests, `tests/test_view.py` runs the original `view_draw_dungeon` in `dm2tools.emu` (the parts of 12b and later as no-ops) and compares the whole view picture pixel by pixel: 120 views of all maps, 40 outdoor views with rain and clouds, and 10 doors in the half open and broken states. Sweeps of all 49016 view square sets and of 1500 view pictures matched too. When 12b ports a function, remove it from `NO_OPS` in `tests/test_view.py`.
- Deviations (details: `docs/skull-formats.md` section 8):
  - `view_load_square` fills the 23 records. The creature grid, the object grid, and the missile and object masks are thing data: 12b fills them. 12b also ports `view_get_thing_position`, `view_grid5_cell_to_xy`, and `view_offset_creature_toward_party`, which only that part uses.
  - No DOS memory layer: `viewport_alloc` is the static bitmap `view_viewport`; `gfx_blit_to_viewport` is `gfx_blit_to_bitmap` with the bitmap as a parameter.
  - `view_rotate_grid5_cell` (12b list) comes forward. `zone_id_for_view_cell` and `zone_id_for_view_element` are in the Step 14 list ("Not in Step 12": `zone_id_for_view_*`); they come forward because the view zones need them for drawing. Their click use stays in Step 14. `view_render_picture_offscreen` (no decompilation) is ported from the disassembly.
  - The light level is not in the save file: `light_update_party_light` computes it (Step 17). Until then `view_light_level` stays 0 (full light).
  - `view_enter_map` holds the view part of `game_enter_map`: map graphics, door types, wall color key, flip flags, the cleared band of the viewport, and the rain and clouds of the saved weather (the sky part of `world_update_day_night` without its update and lightning, Step 17).
  - Empty until their steps: things, creatures, explosions, sparkles, rain (12b), alcove contents (12b), the wet image (12b: the offscreen bitmap starts clear), click zones (Step 14), shop display (Step 15).
  - The walking view (`g_party_move_steps`) is ported but stays off until Step 14; no test covers it yet.
  - Fix in Step 8 code: a 4 bpp image whose format bits hold its y offset (24 images) now decodes; `gdat.c` treated it as no image. `tests/test_gdat.py` now checks all 827 4 bpp images of the type 1 keys (before: 803), because the original sends these offset-format images to the 4 bpp decoder.
  - `dungeon.h` names the square types; `save.c` had the names of stairs and door swapped (values unchanged).

#### Step 12b: Things in the view and weather drawing

- Original functions: `view_draw_square_things`, `view_draw_objects_in_cell`, `view_draw_object`, `view_shift_object_cell`, `view_draw_alcove_contents`, `view_draw_creatures`, `view_draw_creature`, `view_offset_creature_toward_party`, `creature_get_view_cell_zone`, `view_draw_creature_possessions`, `view_draw_pending_possessions`, `view_draw_missiles`, `view_draw_explosions`, `view_setup_explosion_picture`, `view_draw_sparkle_field`, `view_draw_centered_label`, `view_position_to_cell`, `view_get_thing_position`, `view_grid5_cell_to_xy`, `view_rotate_grid5_cell`; `weather_get_rain_image`, `weather_draw_rain`, `view_draw_wet_image`.
- Test: differential tests of `view_grid5_cell_to_xy`, `view_rotate_grid5_cell`, and `view_offset_creature_toward_party` against `dm2tools.emu`. A unit test: a square with one object draws the object image in the expected cell rectangle. A manual visual check as in 12a, on a save with objects and creatures in the view.
- Input: a DOS save with objects and creatures in the view, if `start.DAT` has none. The user makes it in DOSBox.

### Step 13: Interface panels and text

Reimplement the screen layout, champion status boxes, action area, spell area, message area, inventory panel, money display, and text drawing. Test: unit tests for text drawing and layout zones; a manual visual check.

- Original functions:
  - Text: `text_build_glyph`, `gfx_draw_text`, `text_measure`, `text_wrap_line`, `text_draw_with_shadow`, `text_draw_on_screen`, `text_draw_in_viewport`, `text_draw_in_zone`, `text_draw_in_zone_with_shadow`, `text_format_number`, `text_format`, `gdat_get_text`, `text_get_gdat_label`, `gdat_get_text_field`, `dun_decode_text`, `gfx_offscreen_draw_text_in_zone`, `gfx_offscreen_draw_text_in_zone_alt`.
  - Message area: `text_init_message_area`, `text_set_message_cursor`, `text_clear_expired_rows`, `text_scroll_message_area`, `text_message_new_line`, `text_message_draw_line`, `text_print_message`, `text_print_char`, `text_print_number`, `text_print_newline`.
  - Drawing helpers: `gfx_offscreen_create`, `gfx_offscreen_flush`, `gfx_offscreen_add_dirty_rect`, `gfx_offscreen_local_rect`, `gfx_offscreen_fill_rect`, `gfx_offscreen_fill_rect_alt`, `gfx_offscreen_blit`, `gfx_offscreen_draw_gdat_image`, `gfx_offscreen_draw_ctx`, `gfx_fill_rect_dithered`, `gfx_fill_zone_screen`, `gfx_fill_rect_viewport`, `gfx_fill_rect_screen`, `gfx_draw_gdat_in_viewport_zone`, `gfx_blit_to_viewport`, `gfx_blit_forward`, `rect_center_in`, `rect_inflate`, `zone_get_position`, `zone_compute_icon_rect`, `zone_get_icon_rect`.
  - Screen and status boxes: `ui_draw_movement_arrows`, `champ_draw_all_states`, `champ_draw_state`, `champ_set_redraw_flags`, `ui_status_box_begin_draw`, `ui_draw_champion_bars`, `ui_draw_damage_number`, `ui_draw_value_pair`, `ui_draw_champion_values`, `ui_draw_panel_icon`, `ui_draw_champion_portrait`, `ui_draw_inventory_button`, `ui_draw_champion_slot`, `inv_draw_slot`, `ui_draw_champion_face_icon`.
  - Action and spell area: `ui_update_action_area`, `ui_action_area_close`, `ui_action_area_begin_draw`, `ui_draw_action_result`, `ui_draw_hand_icon`, `ui_draw_action_hand_icon`, `ui_draw_party_icons`, `ui_draw_spell_icon`, `ui_draw_action_menu_header`, `ui_draw_action_champion_face`, `ui_draw_entered_runes`, `ui_draw_spell_panel`, `ui_draw_action_name`, `ui_draw_action_menu`, `ui_draw_money_box`, `ui_draw_container`, `inv_open_container`, `inv_close_container`.
  - Inventory panel: `inv_toggle`, `inv_draw_object_info`, `inv_draw_description_text`, `inv_draw_scroll`, `inv_draw_scroll_line`, `inv_draw_object_gauge`, `ui_draw_gauge_bar`, `inv_draw_food_water_panel`, `inv_draw_mouth_picture`, `inv_draw_skills_and_statistics`, `money_draw_coin_counts`, `inv_draw_container_contents`.
  - Data reads that the drawing needs: `dun_get_thing_gdat_index`, `dun_get_thing_gdat_category`, `item_get_attr`, `item_add_charges` (read), `item_get_max_charges`, `item_get_icon_index`, `inv_item_fits_slot`, `item_get_weight`, `item_sum_attr`, `item_get_name`, `text_get_object_name`, `item_has_charged_action`, `money_is_moneybox`, `item_is_chest`, `item_is_minion_map`, `minion_find_link_projectile`, `item_get_index`, `item_index_to_gdat_index`, `item_index_get_gdat_category`, `item_index_to_thing_type`, `money_init_coin_table`, `money_is_coin`, `money_count_coins_in_box`, `money_get_coin_type_for_slot`, `champ_get_stat`, `champ_get_skill_level`, `champ_scale_by_stamina`, `champ_get_max_load`, `champ_get_load`, `champ_find_hand_source_slot`, `champ_list_actions`, `minion_map_allows_action`, `champ_get_index_in_cell`, `champ_set_leader`, `champ_init_party_state` (the part of a loaded game).
- Not in Step 13: click zones and input feedback (`ui_draw_move_arrow`, `ui_release_move_arrow`, `ui_show_game_view`, `ui_hide_game_view`, `ui_draw_sleep_message`): Step 14. Hand reload (`champ_reload_hand`) and the leader hand object (`champ_put_object_in_leader_hand`, `ui_show_object_name`): Step 15. Minion map (`ui_draw_minion_map`, `ui_map_*`, `ui_draw_map_option_buttons`): Step 16. Dialogs (`ui_dialog_*`, `ui_draw_progress_bar`, `ui_volume_dialog`): Step 20.
- Done. Modules: `text.c` (font, text, format codes, message area), `ui.c` (offscreen bitmaps, screen, status boxes, inventory panel), `action.c` (action and spell area, money box, chest), `item.c` (item attributes, icons, weights, coins), `champion.c` (champion record, statistics, skill levels, loads, action lists). `build/dm2 --save PATH` draws the view and the interface. Test: `tests/test_interface.py` runs the original drawing in `dm2tools.emu` and compares the screen and the view picture pixel by pixel: the screen after the load, the inventory, 37 inventory panels (skills; weapons, clothing, scrolls, potions, miscellaneous items, chests, and money boxes), the two hand menus and the 5 rune sets of the spell panel, 14 action results, money box and chest panels, 13 party states (turned, invisible, wounded, asleep, busy, aura, haste, damage, hungry, poisoned, dead). Differential tests of `gfx_draw_text` (300 cases, 8 bpp and 4 bpp), `text_measure` and `text_wrap_line` (300 texts), `text_format` (100 texts), the message area (4 messages and the expired rows), and the icon rectangle and position of 258 layout zones. The harness sends `gdat_get_text` through the original `text_format`.
- Deviations (details: `docs/skull-formats.md` section 9):
  - The IBMIOP message area scroll (service `0x105`) is not in `SKULL.EXE`: the port moves the rows up by 7 pixels. A unit test covers the port behavior; the manual check in DOSBox confirms it or not.
  - `text_format` gives an empty text for the dialog codes (drive paths, disk sizes, errors) until Step 20.
  - Parts that later steps add: the hand reload countdown, the leader hand object (Step 15); the minion map panel and its mana (Step 16); input modes, move arrow feedback, and the view redraw when the inventory closes (Step 14); the new game party (Step 20).
  - `champ_get_load` adds the leader hand weight when it is called, not a kept value. The action area reads the wall set from the party map descriptor.
  - `start.DAT` has 0 (not `0xffff`) in the damage event field of Torham: the port, like the original, shows the damage star until the timeline (Step 14) clears it.
  - `text_decode_dungeon` repeats the text decoder of `view.c`; the view still reads plate texts without format codes.

### Step 14: Main loop, input, movement

Port the main loop, the command queue, mouse and keyboard handling, party movement and turning, sensors, doors, pits, teleporters, and stairs. Pure rule functions get differential tests against `dm2tools.emu`. Stateful logic gets scenario tests: a DOS save plus a command sequence gives the expected party position and state. The expected state comes from a DOS save that the user makes in DOSBox after the same command sequence.

### Step 15: Champions, inventory, items, money, shops

Port champion recruiting, stats, inventory, item use, eating and drinking, actions, money, and shops. Tests as in Step 14.

### Step 16: Creatures, minions, projectiles, magic, combat

Port creature groups and AI, minions, projectiles, explosions, spells, damage, and death. Tests as in Step 14.

### Step 17: World systems

Port weather, day and night, and the Skullkeep machines. Each system has at least one automated test (differential or scenario, as in Step 14).

### Step 18: Sound effects

Port the digital sound playback through SDL3 audio. Sample data and format come from Step 5. Port the volume and stereo position calculation. Test: a differential test of the volume and stereo calculation against `dm2tools.emu`; a test that decodes one sound and checks the sample count.

### Step 19: Music

- Add Nuked-OPL3 as `port/src/opl3.c` and `opl3.h` with its LGPL-2.1 license file.
- Add the BNK loader (`MELODIC.BNK`, `DRUM.BNK`), the HMP parser, and the HMP player. Use the MIDI to OPL conversion from Step 5. If `HMIMDRV.386` cannot be decompiled, use a standard AdLib conversion.
- Read `SONGLIST.DAT` and start the songs where the original starts them.
- Test: render the first seconds of `TEST.HMP` and of one game song. The output is not silent, and the event times are equal to the times in the HMP file.

### Step 20: Start menu, save and load, end of game

Port the start menu, new game, load, save, and the end sequence of `SKULL.EXE`. Tests: a scenario test saves a game, loads it, and checks the same state; the near-end DOS save from `tests/saves/` loads and reaches the end-of-game state after the final command sequence.

### Step 21: Full test and release

- Run `make -C port` (no warnings) and `uv run pytest`. All tests pass.
- Write a manual test checklist: compare the port with the DOS version in DOSBox side by side (image, sound effects, music, input, save in the port and load in DOS, save in DOS and load in the port).
- Update `CHANGELOG.md`. Merge to `main`. Push.
