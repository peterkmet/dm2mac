# Range 3 notes (0x1fef6-0x2b96a)

Analysis notes of the address range 0x1fef6-0x2b96a; the names are in [skull-symbols.csv](../skull-symbols.csv).

181 functions. All have names in `range-3.csv`. 77 new globals are in `range-3-globals.csv`.

## Module layout

| Range | Content |
|---|---|
| 0x1fef6-0x2005b | `session_end`, `session_quit` |
| 0x200b4-0x2028d | Input condition predicates, function table `g_input_cond_funcs` (0x722a4, 12 entries) |
| 0x2029e-0x20e41 | Input mode tree, zone and command lookup, event ring, mode stack |
| 0x20ee0-0x2186c | Click feedback scripts (button press animation), `input_poll` |
| 0x219c2-0x224a9 | Leader, minion map click, `command_execute`, `command_process_queue` |
| 0x22544-0x231b1 | Clicks in the viewport (walls, objects, throw, creatures) |
| 0x23233-0x24629 | Movement arrows, party turn and move, stairs, bump damage, pushing creatures, map change |
| 0x24691-0x249db | `game_loop`, `main` |
| 0x24a88-0x262f7 | Creature AI context, group update, behavior and plan selection |
| 0x26873-0x27bf6 | AI target scanners (table 0x75248, 17 entries) and condition test `creature_ai_test_condition` (23 conditions) |
| 0x27cd2-0x29caa | AI plan commands 0x3f-0x62 (dispatcher `creature_ai_exec_plan_command`). Merchant commands are in this block. |
| 0x29d0c-0x2b724 | AI action handlers for active group byte +0x1a |
| 0x2b75e | `ai_execute_action` (action dispatcher) |

## Key data structures

### Input

- Event ring `g_input_events` (0x7f48c): 11 entries of 6 bytes: +0 type (mouse buttons < 0x20, 0x20 key, 0x40, 0x60), +2 x or key, +4 y. Write index `g_input_event_tail` (0x72fe5), read index 0x7f506, count 0x7f508. Limit: 7 entries, 9 for some types.
- Command record (14 bytes, `g_current_command` 0x7f4ce): +0 x, +2 y, +4 command, +6 rect (x, y, w, h).
- Command queue `g_command_queue` (0x7f4dc): 3 records, head 0x7f50e, tail 0x7f516.
- Input zone (7 bytes, 0x72d1f, 0x3e entries): +0 child index or zone id, +2 command list index, +4 key list index, +6 flags (0x80 active, 0x40 marked, low 6 bits cursor zone index).
- Command list entry (6 bytes, 0x72386): +0 command (low 11 bits; bit 15 ends the list), +2 zone id (bit 15 = panel offset 7, bit 14 = offset 0x12), +4 mouse button mask, +5 flags (bit 3 disabled).
- Mode roots `g_input_mode_roots` (0x72ed1, 10 modes). Mode 1 = end screen, 2 = sleeping, 3 = paused, 5 = movement arrows, 6 = push arrows.
- Click feedback scripts (0x72ef9): byte code run by `input_run_click_feedback`. Opcode = low 6 bits, bit 6 = continue, bit 7 = end. 0 delay, 1 execute queued, 4/6 queue command, 5 execute now, 7 draw GDAT picture, 8-0xe redraw panels, 0xf/0x10 dialog.

### Command numbers (from `command_execute`)

1, 2 turn; 3-6 move (forward, right, back, left); 7-0xb inventory toggle (champion 0-3, 0xb = leader); 0x10-0x13 set leader; 0x14-0x41 inventory slots; 0x46, 0x47 leader hand; 0x48 rename; 0x50 viewport click; 0x52 leader inventory; 0x55 minion map click; 0x56-0x59 0x41081; 0x5d, 0x5e rotate champion +0x1c; 0x5f-0x62 portrait; 0x65-0x6a, 0x6b, 0x6c spell area; 0x70-0x73 action area; 0x74-0x7b hand action; 0x7d-0x81 cursor; 0x81 pause input key; 0x8c save; 0x8d options; 0x8e sleep; 0x8f wake; 0x90 pause; 0x91 resume; 0x92, 0x93 0x49a17; 0xd7-0xda start menu; 0xe0 quit; 0xe1 cancel push mode; 0xe3 cancel mouse capture; 0xe4-0xe9 0x42a35; 0xea-0xed 0x45d6b; 0xf0-0xf2 viewport object zones.

### Viewport click zone (12 bytes, `g_view_click_zones` 0x7f2ec)

+0 rect (8 bytes), +8 thing (uint16), +0x0a parameter (byte, cell or wall side), +0x0b kind (1 floor object, 2 object, 3 object in front, 4 wall with sensor, 6 wall ornament). Count `g_view_click_zone_count` (0x7f400). Coordinates are relative to `g_viewport_origin` (0x716f8).

### Creature AI context (0x350 bytes at 0x7f548)

`ai_begin` saves an active context to the heap (re-entrant AI), `ai_end` restores it.

| Address | Content |
|---|---|
| 0x7f548 | group thing |
| 0x7f54a | group record pointer |
| 0x7f54e | creature info pointer |
| 0x7f552 | active group pointer |
| 0x7f556 | animation entry pointer |
| 0x7f55a | animation state pointer (from 0x2f8d6) |
| 0x7f55e | timeline event (12 bytes): +4 type (0x21 animation, 0x22 think), +5 creature type, +6 x, +7 y |
| 0x7f56c | byte: group removed; 0x7f56d map |
| 0x7f56e, 0x7f570 | plan command arguments |
| 0x7f572-0x7f57a | copies of creature info words +0x0a, +0x0e, +0x10, +0x0c, +0x12 |
| 0x7f580 | behavior index (tables 0x7507a flags, 0x7518c lists) |
| 0x7f589 | alert flag |
| 0x7f58a | item count cache (4 entries x 0x20) |
| 0x7f60a | path buffer |
| 0x7f670, 0x7f674 | candidate count, candidates (0x16 bytes, max 16) |
| 0x7f7d4 | cached path header (0x0e bytes), image cache key 0x20000000 + group index |
| 0x7f7de | target list pointer |
| 0x7f7e6, 0x7f7e7 | context active, context prepared |
| 0x7f7ea | flag table cache (42 pointers) |
| 0x7f894 | 0x100-byte scratch buffer |

Other AI globals: target square 0x7f8a2 (x), 0x7f8a0 (y); party position seen by AI 0x7f8d4 (x), 0x7f8d0 (y), 0x7f8d2 (map).

### Active group (0x22 bytes, `g_active_groups`), new fields

+2 timeline event index, +4 last action time (byte), +6 regeneration time (byte), +0x0c/+0x0e/+0x10 counters (merchant uses them for offer and payment sums), +0x0e/+0x10 also plan variables (set by plan step -10), +0x12 plan table index (0xff none), +0x13 plan step, +0x14 accumulated damage (uint16), +0x16 behavior entry, +0x17 next action, +0x18 target square (x bits 0-4, y bits 5-9, map bits 10-15), +0x1b target direction or item flag, +0x1c target cell, +0x1d direction, +0x1e action argument, +0x1f action phase, +0x20 action argument 2, +0x21 interrupt.

An earlier note said otherwise; the AI state starts at +0x14, not +0x17. Ghidra reads bytes +0x17 and +0x1a as `*(int *)(p + 0x14) >> 24` and `*(int *)(p + 0x17) >> 24`.

### Group record (thing type 4), new fields

+6 health (uint16), +8 item class index (x3 into the flag cache), +0x0a mood flags (uint16), +0x0c home square (x, y, map like +0x18 above), +0x0e bits 8-9 direction.

### Creature info (0x24 bytes), new fields

+3 regeneration (signed byte), +4 maximum health, +6 attack, +8 projectile power, +9 flags (0x10 self-damage on shot, 0x40 closes pits under itself, 0x20 attack two champions), +0x16 nibbles (aggression, think speed), +0x1d push resistance (0xff immovable, 0xfe special), +0x1e low nibble count of possession slots, +0x23 death explosion size.

### AI action codes (active group +0x1a, dispatcher 0x2b75e)

0 none; 1, 2, 9 move; 3, 4 turn then move; 5 move two phases; 6, 7 turn; 8, 0x26 melee attack; 0x0a steal from champion; 0x0b trigger square sensor; 0x0c, 0x0d turn then pick up; 0x0e, 0x0f throw carried item; 0x13 vanish (die); 0x15, 0x16 turn target group; 0x17 give items to group; 0x18 take items from group; 0x19, 0x29, 0x2a, 0x2d, 0x2e drop items; 0x1a, 0x2b, 0x2c pick up items; 0x27, 0x28 shoot projectile; 0x2f-0x31 use wall sensor; 0x35-0x3a move through stairs or teleporter; 0x3b, 0x3c transform; 0x3d-0x40 explode, spawn, or transform; 0x55 cloud explosion. Flags per action: table 0x75136 (bit 2 interruptible, bit 4 delayed).

## DM2 systems found in this range

- Shops and money: merchants are creatures. Their AI plan commands evaluate goods on the counter and pay change: `shop_cmd_wait_for_offer`, `shop_cmd_evaluate_offer`, `shop_cmd_count_payment`, `shop_cmd_pay_change`, `shop_sum_items_value` (uses the item price function 0x15737 in range 1 and the change function 0x15958). Coin values `g_coin_values` (0x7f3a8), coin item types `g_coin_item_types` (0x7f390), count `g_coin_type_count` (0x7f3fa). `money_normalize_coins` (0x29598) re-packs the coins in a container into the fewest coins.
- Minions: `minion_map_click_set_destination` (command 0x55): a click on the 7x7 map grid in the action area (zone 99) writes the destination square into the minion's control record (thing type 14 in the group's possessions, found by 0x1fb62, written by 0x1fac8). The AI scanner `creature_ai_scan_minion_destination` reads it.
- Pushing creatures (DM2): `g_push_mode` (0x7f3a4) shows the push arrows (0x42ae4, input mode 6). `move_push_creature`, `creature_can_be_pushed`, `creature_push_group`, `move_party_can_push_group` (strength against creature info +0x1d).
- Weather, outdoor view, Skullkeep machines: not in this range.

## Anchor corrections

- `command_execute`: signature is `int command_execute(int16_t *command_record)` (returns 0 when the command must wait), not `void (void)`.
- `game_loop` calls `input_reset_states` (0x206c6) on the game end flag, then returns.
- `g_tick_elapsed._2_2_` (0x717f4) is `g_new_map_index`; `game_enter_map` (0x24629) handles it.

## Open questions

- 0x7f0b4 (condition 6), 0x757b8 (condition 11), 0x7f200 (dialog kind), and 0x7fb5e, 0x7fb76 (action panel) need range 5 context.
- 0x7f27a, 0x7f27c, 0x7f27e hold a second party square (y, x, map). Its difference to `g_party_x`/`g_party_y` is not clear.
- Many AI scanner and plan command names are role names (kind numbers). The plan tables at 0x74f7e (7-byte steps: +0 command or negative control code, +1/+2 next step on success or failure, +3/+4 arguments, +5 scanner and flags, +6 parameter) need a dump to give each kind a game meaning.
- `creature_alert` (0x24e62): flags 0x4005/0x4002 and the "fear" bit 2 of group +0x0a are guesses.
- `view_click_front_wall` and `g_wall_drag_active` (0x7f220): role not confirmed.

## Low-confidence names (45)

input_cond_flag_7f0b4, input_cond_value_757b8, input_cond_dialog_kind, input_cond_action_panel_kind, input_cond_action_champion_mask, input_cond_action_panel_variant, input_cond_no_action_champion_in_cell, view_click_front_wall, view_click_drop_done, move_group_side_blocks, creature_alert, creature_anim_schedule, creature_anim_schedule_alt, creature_ai_scan_idle, creature_ai_scan_kind0, creature_ai_scan_items_seen_2, creature_ai_scan_items_seen_4, creature_ai_scan_item_counts_1, creature_ai_scan_item_counts_3, creature_ai_scan_carried_items_2, creature_ai_scan_carried_items_4, creature_ai_scan_path_kind13, creature_ai_scan_path_kind5, creature_ai_scan_carried_kind_f, creature_ai_scan_carried_kind_10, creature_ai_scan_by_condition_15, creature_ai_cmd_follow_candidate_path, creature_ai_cmd_shoot_random, creature_ai_cmd_seek_item, ai_cmd_seek_with_carried_item, ai_cmd_front_group_carries, creature_ai_cmd_place_item_on_sensor, creature_ai_cmd_drop_then_seek, creature_ai_cmd_set_mood_flag, creature_ai_cmd_seek_kind11_targets, creature_ai_cmd_consume_carried_item, creature_ai_cmd_check_transform_marker, creature_ai_cmd_set_special_action, creature_ai_cmd_path_to_target_alt, creature_ai_act_trigger_square, creature_ai_act_explode_or_spawn, creature_ai_act_cloud_explosion, shop_cmd_wait_for_offer, shop_cmd_count_payment, dun_set_pit_or_teleporter_state.
