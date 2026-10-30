# Main loop, input, and movement

The code is in `src/ui/play.c`, `src/ui/input.c`, `src/engine/move.c`, `src/engine/sensor.c`, and `src/engine/timeline.c`. Addresses and notation follow the conventions of [skull-map.md](../reference/skull-map.md).

## Timing and the game step

- The programmable interval timer (PIT) runs at 240 Hz. Every 4th PIT tick is a timer tick (60 Hz). A game step runs every 8 timer ticks (`g_main_loop.tick_threshold`), or every timer tick while the party sleeps.
- `game_loop` of `SKULL.EXE` waits for input in the middle of a step. The port splits it in `src/ui/play.c` into `step_before_input` and `step_after_input`. [architecture.md](../architecture.md#the-game-step) says what `play_tick` and the two parts run, and [architecture.md](../architecture.md#hooks-between-layers) lists the calls from the rules into the interface.
- Pause (`pause_game`) stops the game time until the resume command. The port keeps the main loop alive and clears `g_main_loop.running`, the flag that lets the game time run.

## The input tree

- The tables in `SKULL.EXE` and `src/ui/input.c`:
  - Nodes: condition (bit 7 for a node with children), argument, child list or zone.
  - Child lists: bit 7 starts a list.
  - A root node for each of the 10 input modes.
  - Zones (7 bytes): layout zone with bit 15 for the inventory panel and bit 14 for the action area, command list, key list, cursor zone.
  - Command lists at `0x72388` (6 bytes): command with bit 15 to start a list, zone, mouse buttons, flags.
  - Key lists at `0x729b8` (4 bytes): command with bit 15 to start a list, key code.
- A key code is the PC scan code, plus `0x200` for shift, `0x400` for control, `0x800` for alt. Esc (`0x01`) pauses. The arrow keys give the numpad codes:
  - up: `0x4c` (forward)
  - left: `0x4b` (turn left)
  - right: `0x4d` (turn right)
  - down: `0x50` (backward)
  - shift + left: `0x4f` (sidestep left)
  - shift + right: `0x51` (sidestep right)
- A click goes down the tree from the root node of the input mode. The first command whose zone holds the click and whose buttons match wins. The matched zone rectangle and the click position are kept for the click feedback.
- The event ring holds 11 events. A new event goes in if fewer than 7 events wait (9 for a released left button and for area events, the enter and leave events of the cursor zones, which the port does not make; see the last item of [Deviations from the original](#deviations-from-the-original)). A key goes into the ring only when the main loop reads it. `input_flush` keeps only the area events and the released left buttons.

## Click feedback

- Each command has a feedback script: bytecode that draws the pressed button, waits timer ticks, draws the released button, and then queues the command. The low 6 bits are the opcode, bit 6 runs the next opcode immediately, bit 7 ends the script.
- The port runs the opcodes 0 to `0x10`. Opcode `8` draws the spell icon, `9` the hand icon, `0xc` the action name, `0xf` and `0x10` the dialog buttons.
- Opcode `0xa` (rune commands `0x65` to `0x6a`) has no argument. It draws the pressed rune button ([GDAT](../formats/graphics.md#gdat-items) `(1, 5, 8)`, opaque) centered in the button rectangle. Then it draws the button's rune character in the next rune set (`0x60` + 6 × rune count + command - `0x65`), centered, black on UI color 4 (`offscreen_draw_text_at`). The changed rectangle of that text starts at its baseline.
- Opcode `0xb` (cast command `0x6c`) has no argument. It draws the entered runes with their background (`area_draw_entered_runes(true)`).
- Opcode `0xe` (map option commands `0x56` to `0x59`) has 1 argument byte: the option bits that show pressed (`mmap_draw_option_buttons`). The script draws the pressed button, executes the command, waits 4 timer ticks, and draws the buttons with argument 0.
- After opcodes `8`, `9`, `0xb`, `0xc`, and `0xe`, the script copies the action area bitmap to the screen and closes the bitmap.
- The movement arrows show pressed (`show_move_arrow`) until the move is done (`input_release_move_arrow`).
- Before the script lookup, `input_refine_champion_click` derives the command of a champion click:
  - A hand in a status box (commands `0x74` to `0x7b`, cell = party direction + (command - `0x74`) / 2) gives `0x74` + 2 × champion + hand if the champion lives, the hand is ready, and the hand has actions. Otherwise it gives no command.
  - A champion icon of the party icons (`0x5f` to `0x62`) gives the leader command (`0x10` to `0x13`) if the click is nearer to the icon edges that face the party center. Otherwise it keeps the command if that champion's spell action has no delay, and gives no command if it has one.

## Click zones of the view

- The view drawing records up to 32 click zones (`g_view_click_zones`, 12 bytes: rectangle, thing, pos, kind). The first 9 are fixed: the objects of 4 cells (the party square, and the 2 near cells of the front square if no walking creature is on them), the creature possessions of 4 cells, and the alcove in front. `view_draw_dungeon` clears the thing and the kind of the fixed zones; their rectangle and cell keep the values of an older view. The wall ornaments and the door buttons add zones after the fixed ones.
- A fixed object zone grows to hold all the objects of its cell, and is at least 14 × 11 pixels.
- `click_in_viewport` tests the zones in order: an object or alcove zone takes or places an object ([champions.md](champions.md#the-leader-hand)), a wall ornament goes to `sensor_process_wall_click`, a door button toggles the door. A click on no zone goes to the front wall or to a creature ([creatures.md](creatures.md)).
- `g_view_hit_offset` (`0x802d4`) is the position of the last click in a view picture: the vertical and horizontal distance from the picture center, in percent of half the picture size. The port keeps the vertical value in `g_view_hit_offset_y`.

## Movement

- `g_current_map.map` is the map that dungeon reads use (`dungeon_square_at`, `dungeon_move_relative`). The party map and the current map differ while things move on other maps.
- A slow party (`champ_get_move_delay`) walks: `g_party.move_steps` holds the view one square back for the steps of the walk.
- `move_follow_square_transitions` takes a thing through teleporters, open pits, and stairs until it stays on a square. A pit can lead to the map below or to a marker of the map.
- If the party arrives on a square with a creature that moves aside, the creature gets a loud alert. Its value is a quarter of the sum of the champions' step times, plus a random part up to half of that quarter. A champion's step time (`get_step_time` in `src/engine/move.c`):
  - 0 for a dead champion
  - 50 if the load / 10 plus the redraw bit `0x10` is not 0, that is, if the load is at least 10 or the bit is set; no code of `SKULL.EXE` sets the redraw bit `0x10` (`REDRAW_SLOW_STEP`), and the port keeps the test
  - 40 otherwise
- A move into a wall, a closed door, or a creature gives bump damage (`move_party_bump_damage`) to the champions on the front cells.

## Sensors and square events

- A square event (type 4) holds x, y, cell, and effect: 0 set, 1 clear, 2 toggle, 3 hold. The priority is 1 for set, 3 for clear, 2 for toggle.
- Floor sensors react when a thing (or the party) enters or leaves the square. Wall sensors react to clicks and to square events. A sensor with a target queues a square event at the target after its delay.
- [skull-map.md](../reference/skull-map.md#global-variables-script-variables) lists the script variables (`game_global_var`).
- Door events: type 1 opens or closes a door one step per event, type 2 breaks it.

## Deviations from the original

- Uninitialized data in the original: some events keep 2 to 4 data bytes or the high bits of a word from the stack (hold square events, sensor timers, item and wall events). The port sets them to 0. The reverse of a hold effect and the priority of a hold square event are uninitialized too: the port keeps the hold effect and uses priority 3.
- Bounds and invalid records:
  - A wall square event to a map outside the dungeon does nothing.
  - The view records at most 32 click zones.
  - A teleporter square without things has no teleporter link and is not a closed teleporter (`link_side`, `is_closed_teleporter`; the original reads the record of the end mark).
  - A teleporter link to a map outside the dungeon counts as no link.
  - An active teleporter square whose first thing is not a teleporter (a damaged dungeon) does not move the party, an object, or a creature, and the AI sees it as floor without a passage behind it (`transit_teleporter`, `needed_move_flags`, `search_square_test`). The original reads the record of that thing as a teleporter.
  - `dungeon_unlink_thing_from_square` on a square without things does nothing (the original reads before the first-thing table).
  - `dungeon_link_thing_to_square` returns false for a full first-thing table (the original stops with a system error).
  - If the party falls from a trap teleport text (`fall_risk`) and the random champion is dead, the hurt sound takes the leader's portrait; without a leader the port plays no hurt sound (the original reads the portrait of champion index `0xffff`, before the champion table).
- Timeline system errors: if an event is not in the queue (`find_queue_slot`, `system_error(0x46)`) or the timeline is full (`timeline_add_event`, `system_error(0x2d)`), the original shows `SYSTEM ERROR 70` or `SYSTEM ERROR 45` on the screen, waits 300 vertical retraces or a key, and quits. The port calls `os_fatal` with the same text (`src/engine/timeline.c`): the fatal hook (or the error output without a hook) gets the text, then the program exits with `EXIT_FAILURE`.
- Out of memory: a failed allocation that has no error result calls `os_fatal("out of memory")` (`src/engine/` and `src/control/`). The original has no such exit.
- `creature_cell_mask` reads kinds 5 to 15 past its table: [creatures.md](creatures.md#deviations-from-the-original).
- The fall view of a pit goes into the viewport, and the next present shows it. The original presents it in the middle of the step.
- In push mode, the push arrow under the mouse stays pressed in the arrow panel while the mouse selects it (`input_release_move_arrow`). The original draws it pressed on the screen, waits for a vertical retrace, and releases it in the same game step. The port presents the screen only after the step, so with the original order the pressed arrow would never show.
- A load and a new game clear the push that waits for a creature group (`g_party.push_pending_group`, `game_start_play`). The original keeps the group of the last game, and the push arrows of the new game wait for it.
- The video driver of the original (`IBMIOP.EXE`, service `0x109`, mode `0x8008`) shows the walking frame stretched to the full viewport. The port does not reimplement the driver. `play_present_viewport` stretches the walk rectangle of the frame to the viewport with nearest neighbor while `move_steps` is not 0. The exact rounding of the driver is unverified.
- The advanced mode takes the autorepeat of a held key only if no key, event, or command waits (`input_key_repeat`). In the original, the repeats of a held key fill the event ring, and the party continues to walk after the key is released. The vanilla mode keeps that behavior.
- In the original, a move without a destination (a removal) leaves a stale register value in the destination map (`g_party.dest_map`). The port writes the current map.
- The search for a discarded record (`discard_find_thing` in `src/engine/move.c`) on a dungeon of one map (a test or damaged file) scans the party map and stops; the original loops forever in its search for the next map.
- Kept quirks of the original:
  - `move_follow_square_transitions` follows at most 49 square transitions (teleporters, pits, stairs) in one move: its loop limit `TRANSITION_MAX` is 50, but the loop pre-decrements the counter.
  - `dungeon_location_on_map_from` compares the upper map bounds unsigned, so a location at x or y = -1 next to a map at offset 0 is not on that map.
  - A pit to a marker (`transit_pit_marker`) sets the transit's thing type to the type of the last thing that it reads on the pit square; the stairs check of the same step (`move_follow_square_transitions`) then reads that type.
  - `dungeon_door_type` returns `0xff` for a door type without a door, and the callers pass it to GDAT as the entry index.
  - A pit to a marker without a pit target marker of its id sends the thing to map 0, to the square of the id's last position marker, or otherwise to the coordinates of the pit square.
  - `click_in_viewport` tests the passable fake wall flags of the front square only if the square is a wall, which never has them. So a wall in front always takes the click of the leader hand object away from the front cells, and a click on it always plays a sound.
  - The dialog button opcodes `0xf` and `0x10` of a click feedback script forget the feedback bitmap that the script opened (`feedback_draw_op`).
  - A frame step sensor (`0x41`, `SENSOR_FRAME_STEP`) that steps back from frame 0 wraps to frame `0xffff` before the modulo by its ornament's frame count, so it goes to the last frame only if the frame count divides `0x10000` (`wall_sensor_event`).
- Not ported: the enter and leave events of the cursor zones ([champions.md](champions.md#the-cursor)); the mouse drag through a fake wall; `input_check_memory_cheat`, a key sequence that prints the free memory in the message area.
