# Creatures, projectiles, magic, and minions

The rules of the creatures: the creature data, the updates of the creature groups and their AI, the projectiles, the explosions, the magic, and the minion map. The code is in `src/engine/creature.c`, `src/engine/ai.c`, `src/engine/ai_move.c`, `src/engine/ai_data.c`, `src/engine/projectile.c`, `src/engine/magic.c`, `src/engine/minion.c`, and `src/ui/minion_map.c`. The addresses and the notation follow the conventions of [skull-map.md](../reference/skull-map.md).

## Creature data

- The creature info table has 36 bytes for each creature type (`creature_infos` in `src/engine/creature.c`, generated from `SKULL.EXE`): flags, defense, regeneration, maximum health, attack, poison, dexterity, traits, movement, skills of items, attacks, doors, and actuators, speed, door flags, push resistance, possessions, possession zone, size, and hit size.
- An active group has 34 bytes: the animation state, the mood, the behavior, the action, the timing of the next update, the destination, and the plan state. The table has room for the groups of the dungeon plus 100. A group is active while the party is on its map.
- The AI script data is at `0x73020`-`0x74f7e` of `SKULL.EXE`: plan tables, behavior tables, class flags, action flags, the enabled commands, and the plan steps. The links in the data are `SKULL.EXE` addresses. `src/engine/ai_data.c` holds a copy.
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
- Uninitialized data of the original: data bytes 4 and 5 of projectile and explosion events; the data bytes after the word of light, invisibility, and aura events. The port sets them to 0. The oracle clears them with a hook on `timeline_add_event`.
- The AI data of a group (plan state and path) stays in a store of the port for each group. The port never evicts it. A load clears it. The original can lose it when the image cache evicts it; then the group plans again.
- The scratch blocks of the AI (LIFO allocations of the original memory layer) are ordinary buffers in the port.
- No free active group: the original discards a creature group of the dungeon and tries again. The port leaves the group inactive. The table has room for 100 more groups than the dungeon has.
- Stale registers: the action "move two steps" in phase 3 or more returns `0x100` (the original returns the low half of a pointer); the action "turn, then move" in phase 2 or more returns the destination row (the register that holds it). An `AT_COUNTER` argument out of range reads 0 (the original reads any word of the active group).
- The original tests the address of `rand_2bits` instead of calling it, so the extra delay always applies. The port keeps this.
- Bounds fixes:
  - reads past the creature info table, the animation frames, the item filter table, the AI level table, and the candidate records
  - a square read at the map edge (not a pit)
  - a division by 0 for an animation of length 0 and for a minion map strip of 1 icon
  - the minion map square read outside the map
  - a path step read without a stored path (address 0 in the original)
  - the active group of a stale active index of a loaded save (`creature_is_flying`, `creature_group_jumps`); the port reads none
  - `creature_deactivate_group` with an active index at or past the end of the table: the original reads one entry past it, the port returns
  - `creature_current_step_acts` and `ai_begin` for a non-static group without an active entry: the original reads a null entry, the port returns 0 and leaves the AI context without an active group
  - the cells of the possessions seen from side -1 (the own square): the original reads 64 KB past its table, the port gives all cells (the oracle applies the same fix with a hook)
  - the step of an AI command with direction -1: the original reads the words before its direction tables, the step (0, -1), and the port takes the step north (`step_dir` in `engine/ai_move.c`)
- `ai_search_area` always clears the first search map. The original clears it only for line targets or a cost.
- `magic_add_light` uses a delay of 0 for a light kind other than the three that exist (the original leaves it unset; no caller uses another kind).
- The create item spell skips the move of an invalid thing when no record is free; it still draws the random number.
- Kept quirks of the original:
  - the random wall ornaments of a square on another map (linked squares, minion map) use the ornament list of the map entered last, as the original global does
  - `explosion_get_attack` divides the attack on a creature that resists non-fire by 4, but each blast type that reaches a creature computes the attack again, so the division has no effect
  - a projectile compares directions without a wrap, so only flights to the north and east can face a champion
  - a projectile move also writes the direction into its freed event
  - the power that the sensors test after a door spell comes from the door record
  - `creature_cell_mask` and the movement table read kinds 5 to 15 from the bytes after their tables
  - the AI commands past `g_ai_command_enabled` read their enabled byte from the data after it: the commands `0x14` to `0x1f` from the distance map and light globals, the commands `0x20` to `0x5b` from the hit offset and hit radius tables of `creature_find_group_near_point` (`g_ai_command_enabled_overrun` in `src/engine/ai_data.c`); the port gives 0 for the commands after them
  - an AI command test that sets no result keeps the result of the command before (`AiCommand` in `src/engine/ai_internal.h`)
  - `dun_set_pit_teleporter_state` with `PIT_STATE_TOGGLE` closes an open pit or teleporter and opens a closed one
  - the loop schedule of a static creature (`schedule_loop` in `src/engine/creature.c`) adds the first frame of the sequence to the game time without the shift (`ANIM_START_MASK`, bits 6 to 11), so the loop phase differs from the first frame; one function in the port does the work of `0x25204` and `0x252c3`
  - the action "move two steps" in phase 2: when the fall of the group waits or the group leaves the dungeon, the AI position takes the destination of the last move inside that move (`g_party.dest_*`; for example a dropped possession or the removal of the group), not the square of the group
  - `magic_add_party_aura` with a new aura type removes the whole champion mask of the cast from the aura events, not only the bit of the champion whose aura ends
