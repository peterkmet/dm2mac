# Creatures, projectiles, magic, and minions

The rules of the creatures: the creature data, the updates of the creature groups and their AI, the projectiles, the explosions, the magic, and the minion map. The code is in `src/engine/creature.c`, `src/engine/ai.c`, `src/engine/ai_move.c`, `src/engine/ai_data.c`, `src/engine/projectile.c`, `src/engine/magic.c`, `src/engine/minion.c`, and `src/ui/minion_map.c`. The addresses and the notation follow the conventions of [skull-map.md](../reference/skull-map.md).

## Creature data

- The creature info table has 36 bytes for each creature type (`creature_infos` in `src/engine/creature.c`, generated from `SKULL.EXE`): flags, defense, regeneration, maximum health, attack, poison, dexterity, traits, movement, skills of items, attacks, doors, and sensors, speed, door flags, push resistance, possessions, possession zone, size, and hit size.
- An active group has 34 bytes: the animation state, the mood, the behavior, the action, the timing of the next update, the destination, and the plan state. The table has room for the groups of the dungeon plus 100. A group is active while the party is on its map.
- The AI script data is at `0x73020` to `0x74f7e` of `SKULL.EXE`: plan tables, behavior tables, class flags, action flags, the enabled commands, and the plan steps. The links in the data are `SKULL.EXE` addresses. `src/engine/ai_data.c` holds a copy.
- The AI context of the group that updates (`0x7f548`, 0x350 bytes) holds the group, its info, the scan results, the candidates (22 bytes each), the plan cache (14 bytes), and the move flags.

## Updates

- An update event of a group runs the animation step. When the animation is at its end, the AI runs: update the mood, select a behavior, choose a plan, run its commands and actions, and compute the time of the next update.
- An AI command returns `0xfc` (an action is issued), `0xfd` (fail), or `0xfe` (done).
- The plan state and the path of each group stay between its updates. The original keeps them in the image cache (keys `0x20000000 | group` and `0x30000000 | group`).

## Projectiles, explosions, and magic

- A projectile record links its payload (an object or a spell), and its event (type `0x1d` or `0x1e`) holds the energy, the attack, and the step. Each event moves the projectile one cell. At a wall, a closed door, a creature, or a champion the projectile hits (`hit_impact`).
- An explosion record holds its type and power. Its event (type `0x19`) applies the damage on its square and can trigger the wall sensors.
- A spell is a sequence of up to 4 runes (power, element, form, class). `magic_find_spell` finds it in the spell table of `SKULL.EXE`. The kinds are potions, projectiles, party auras, summons, and others.
- The minion map is a panel of the action area. It shows the squares around a minion or a marker, takes mana every 25 game steps, and a click on the grid sends the minion there.

## Deviations from the original

- The scenario tests stub the mouse areas of an input mode (`mouse_area_add`, `mouse_area_remove`): a creature attack wakes the party, and the original then sets the input mode.
- Uninitialized data of the original: data bytes 4 and 5 of projectile and explosion events; the data bytes after the word of light, invisibility, and aura events. The port sets them to 0. The oracle (the original in the emulator `dm2tools.emu`) clears them with a hook on `timeline_add_event`.
- The AI data of a group (plan state and path) stays in a store of the port for each group. The port never evicts it. A load clears it. The original can lose it when the image cache evicts it; then the group plans again.
- The scratch blocks of the AI (last-in, first-out allocations of the original memory layer) are ordinary buffers in the port. The port has 4 blocks (`SCRATCH_BLOCKS`): a fifth nested creature update calls `os_fatal` (`ai_scratch_push`); the original has no such limit.
- No free active group: the original discards a creature group of the dungeon and tries again. The port leaves the group inactive. The table has room for 100 more groups than the dungeon has.
- Stale registers: the action "move two steps" in phase 3 or more returns `0x100` (the original returns the low half of a pointer); the action "turn, then move" in phase 2 or more returns the destination row (the register that holds it). An `AT_COUNTER` argument out of range (not 0 or 1) reads 0: the original reads any word of the active group, and words past it; no target record of the AI tables uses the condition.
- The original tests the address of `rand_2bits` instead of calling it, so the extra delay always applies. The port keeps this.
- Bounds fixes:
  - reads past the creature info table, the animation frames, the item filter table, the search target kind flags (`g_ai_kind_flags`), and the candidate records
  - a creature type without its GDAT animation data (a damaged GRAPHICS.DAT): the original reads a null entry, the port calls `os_fatal` (`read_creature_data` in `src/engine/creature.c`)
  - an AI class of the GDAT creature attribute at or above `AI_CLASS_COUNT` (a damaged GRAPHICS.DAT): the original reads past its class tables, the port calls `os_fatal` (`ai_group_class`)
  - a square read at the map edge (not a pit)
  - a division by 0 for an animation of length 0 and for a minion map strip of 1 icon
  - the minion map square read outside the map: a wall square of another map of the level, and the pit one level up
  - a path step read without a stored path (address 0 in the original)
  - the active group of a stale active index of a loaded save (`creature_is_flying`, `creature_group_jumps`, `creature_alert`); the port reads none (`creature_alert` returns)
  - `creature_deactivate_group` with an active index at or past the end of the table: the original reads one entry past it, the port returns
  - `creature_active_group_event` with an active index at or past the end of the table (the arrival of a moved group, `arrive_group` in `src/engine/move.c`; no caller passes one): the original reads past it, the port gives no event
  - the active group table of a dungeon with more than 155 walking groups (`creature_alloc_active_groups` in `src/engine/creature.c`; no shipped dungeon has them): the original allocates an entry for each walking group plus 100 and gives index 255 (no active group) and wrapped indices to the groups past 254, the port allocates at most 255 entries
  - `creature_deactivate_all_groups` stops at the end of the active group table: the original loops while the active group count is not 0
  - `creature_get_party_distance` with a square outside the distance map (x below 0, y outside 0 to 31): the original checks only x below the map width and reads outside the map, the port returns -1
  - `creature_current_step_acts`, `ai_begin`, and `creature_process_event` for a non-static group without an active entry: the original reads a null entry, the port returns 0 and leaves the AI context without an active group (`creature_process_event` ends the update)
  - a new group that leaves the dungeon at once (`create_group` in `src/engine/creature.c`): the original frees a stale thing when the group links no projectile, the port frees only the projectile of a minion group
  - the cells of the possessions seen from side -1 (the own square): the original reads 64 KB past its table, the port gives all cells (the oracle applies the same fix with a hook)
  - the step of an AI command with direction -1: the original reads the words before its direction tables, the step (0, -1), and the port takes the step north without that read (`step_dir` in `src/engine/ai_move.c`)
  - an item filter without a scratch block of the AI: the original writes the class bitmap to address 0, the port parses the filter without a cache (`filter_bitmap`)
  - an item filter value outside the class bitmap (a negative or large index of `ai_thing_matches_filter`): the original reads outside the bitmap, the port finds no match (an inverted filter matches)
  - the level counts of the cost search (`search_entered`): the original counts past its 51 levels into its own variables, the port counts in a table of 256 levels
  - the move command of the path check of `ai_plan_path`: the original reads a byte after the path buffer that nothing writes, the port uses 0 (`PATH_CHECK_COMMAND`)
  - the back-off of a ranged attack along a line out of the distance map (`back_off_line` in `src/engine/ai_move.c`): the original reads before or after the distance map, the port stops the line on the last square inside the map; any cell before the map (x below 0, and x = 0 with y = -1) or after it (x = 32 on a map 32 wide, and x = 31 with y = 32) stops it
  - a melee attack of a creature info with random champions and two champions together (`melee_attack_party` in `src/engine/ai.c`; no shipped creature info has both): when no living champion is left to pick, the original attacks champion -1, the port skips the pick
  - the AI condition "health below" (`test_condition` in `src/engine/ai.c`) of a creature info with a maximum health of 0 (no shipped creature info has one): the original divides by 0, the port divides by 1
  - the cells in reach of a floor item (`floor_item_in_reach` in `src/engine/ai_move.c`) for another square and direction -1 (no caller passes both): the original writes the cell byte before its table, the port starts at cell 0
- `ai_search_area` always clears the first search map. The original clears it only for line targets or a cost.
- `magic_add_light` does nothing for a light kind other than the three that exist (the original adds a light event with an unset delay and reads past its light power table; no caller uses another kind).
- The create item spell skips the move of an invalid thing when no record is free; it still draws the random number.
- Kept quirks of the original:
  - a new group when the active group table is full (`create_group` in `src/engine/creature.c`): the group record that the creation took stays taken and is in no square, so no later creation or discard can use it
  - the random wall ornaments of a square on another map (linked squares, minion map) use the ornament list of the map entered last, as in the original, with the random ornament count, the size and the index of the map of the square (`random_ornament` in `src/engine/aspect.c`)
  - `explosion_get_attack` divides the attack on a creature that resists non-fire by 4, but each blast type that reaches a creature computes the attack again, so the division has no effect
  - a projectile compares directions without a wrap, so only flights to the north and east can face a champion
  - a projectile move also writes the direction into its freed event
  - a projectile that a missile wall turns back also writes the new direction into its freed event (`enter_next_square`)
  - a projectile that flies on leaves the last square ahead that a creature can notice it on in the destination globals of the party move (`g_party.dest_x`, `g_party.dest_y`)
  - the power that the sensors test after a door spell comes from the door record
  - `creature_cell_mask` and the movement table read kinds 5 to 15 from the bytes after their tables (`g_ai_possession_cells` in `src/engine/ai_data.c`: kinds 1 to 4 are the group cell masks, and kind 0 gives no cells without a read)
  - `creature_find_group_near_point` reads 4 offset pairs from an offset of 2 pairs per cell, so a cell reads into the row of the next cell; the port table keeps these pairs
  - the AI commands past `g_ai_command_enabled` read their enabled byte from the data after it: the commands `0x14` to `0x1f` from the distance map and light globals, the commands `0x20` to `0x5b` from the hit offset and hit radius tables of `creature_find_group_near_point` (`g_ai_command_enabled_overrun` in `src/engine/ai_data.c`); the port gives 0 for the commands after them
  - `get_item_counts` (`src/engine/ai.c`) does not flush the last wanted and extra run after its loop, so a reused count slot keeps the old counts of that run
  - an AI command test that sets no result keeps the result of the command before (`AiCommand` in `src/engine/ai_internal.h`)
  - the activation target of a search (`setup_target_kind`) compares the activation position with the party position in another order: its x with the party map, its y with the party x, its map with the party y
  - `ai_square_blocks_line` passes its square to `creature_find_group_near_point`: a group that the search finds next to the square moves the square that the next things of the list test
  - the party distance across maps (`far_party_distance`) wraps the dungeon positions to 16 bits
  - `dun_set_pit_teleporter_state` with `PIT_STATE_TOGGLE` closes an open pit or teleporter and opens a closed one
  - the loop schedule of a static creature (`schedule_loop` in `src/engine/creature.c`) adds the first frame of the sequence to the game time without the shift (`ANIM_START_MASK`, bits 6 to 11), so the loop phase differs from the first frame; one function in the port does the work of `0x25204` and `0x252c3`
  - the action "move two steps" in phase 2: when the fall of the group waits or the group leaves the dungeon, the AI position takes the destination of the last move inside that move (`g_party.dest_*`; for example a dropped possession or the removal of the group), not the square of the group
  - the back-off of a ranged attack along a line (`back_off_line` in `src/engine/ai_move.c`) has no map test: a step to y = -1 with x above 0 reads the last cell of the column before, and a step to x = map width on a map less than 32 wide reads a cell of the next column of the distance map, and a step to y = 32 on a map 32 high (x below 31) reads the first cell of the next column
  - `ai_free_cached_data` frees the plan state of the group whose index is the bitwise OR of the freed group and the group of the update in progress, so during a creature update it can free the plan state of another group, which then plans again
  - the action "pick up items" with `SUB_REPEAT` (`act_pick_up_items` in `src/engine/ai.c`) ends after one item: the item joins the end of the possessions before the loop takes the next thing
  - `magic_add_party_aura` (`0x4565a`) loops over all four champion slots, even those that hold no champion; a slot without a champion counts for the "big aura" test with its stale `aura_value`
  - `magic_add_party_aura` with a new aura type removes the whole champion mask of the cast from the aura events, not only the bit of the champion whose aura ends
