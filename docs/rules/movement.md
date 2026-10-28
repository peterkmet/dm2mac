# Main loop, input, and movement

The rules of the main loop, the input, and the movement: the timing and the game step, the input tree, the click feedback, the click zones of the view, the movement of the party, and the sensors and square events. The code is in `src/ui/play.c`, `src/ui/input.c`, `src/engine/move.c`, `src/engine/sensor.c`, and `src/engine/timeline.c`. The addresses and the notation follow the conventions of [skull-map.md](../reference/skull-map.md).

## Timing and the game step

- The PIT runs at 240 Hz. Every 4th PIT tick is a timer tick (60 Hz). A game step runs every 8 timer ticks (`g_main_loop.tick_threshold`), every timer tick while the party sleeps.
- `game_loop` of `SKULL.EXE` waits for input in the middle of a step. The port splits it in `ui/play.c` into `step_before_input` and `step_after_input`. What `play_tick` and the two parts run is in [architecture.md](../architecture.md#the-game-step).
- The calls of the rules into the interface: [architecture.md](../architecture.md#hooks-between-layers).
- Pause (`pause_game`) stops the game time until the resume command. The port keeps the main loop running and clears `g_main_loop.running`.

## The input tree

- The tables are in `SKULL.EXE` and in `src/ui/input.c`: the nodes (condition with bit 7 for a node with children, argument, child list or zone), the child lists (bit 7 starts a list), the root node of each of the 10 input modes, the zones (7 bytes: layout zone with bit 15 for the inventory panel and bit 14 for the action area, command list, key list, cursor area), the command lists at `0x72388` (6 bytes: command with bit 15 to start a list, zone, mouse buttons, flags), and the key lists at `0x729b8` (4 bytes: command with bit 15 to start a list, key code).
- A key code is the PC scan code, plus `0x200` for shift, `0x400` for control, `0x800` for alt. The arrow keys give the numpad codes: up `0x4c`, left `0x4b`, right `0x4d`, down `0x50`, shift and left `0x4f`, shift and right `0x51`. Esc (`0x01`) pauses.
- A click goes down the tree from the root node of the input mode. The first command whose zone holds the click and whose buttons match wins. The matched zone rectangle and the click position stay for the click feedback.
- The event ring holds 11 events. A new event goes in when fewer than 7 events wait (9 for a released left button and for the area events). A key goes into the ring only when the main loop reads it. `input_flush` keeps only the area events and the released left buttons.

## Click feedback

- Each command has a feedback script: bytecode that draws the pressed button, waits timer ticks, draws the released button, and then queues the command. The low 6 bits are the opcode, bit 6 runs the next opcode immediately, bit 7 ends the script.
- The port runs the opcodes 0 to `0x10`. Opcode `8` draws the spell icon, `9` the hand icon, `0xc` the action name, `0xf` and `0x10` the dialog buttons.
- Opcode `0xa` (rune commands `0x65` to `0x6a`) has no argument. It draws the pressed rune button (GDAT `(1, 5, 8)`, opaque) centered in the button rectangle. Then it draws the rune character of the button in the next rune set (`0x60` + 6 × rune count + command - `0x65`), centered, black on UI color 4 (`gfx_offscreen_draw_text_at`). The changed rectangle of that text starts at its base line.
- Opcode `0xb` (cast command `0x6c`) has no argument. It draws the entered runes with their background (`area_draw_entered_runes(1)`).
- Opcode `0xe` (map option commands `0x56` to `0x59`) has 1 argument byte: the option bits that show pressed (`mmap_draw_option_buttons`). The script draws the pressed button, executes the command, waits 4 ticks, and draws the buttons with argument 0.
- After opcodes `8`, `9`, `0xb`, `0xc`, and `0xe`, the script copies the action area bitmap to the screen and closes it.
- The movement arrows show pressed (`show_move_arrow`) until the move is done (`input_release_move_arrow`).
- Before the script lookup, `input_refine_champion_click` makes the command of a champion click. A hand in a status box (commands `0x74` to `0x7b`, cell = party direction + (command - `0x74`) / 2) gives `0x74` + 2 × champion + hand when the champion lives, the hand is ready, and the hand has actions; else no command. A champion icon of the party icons (`0x5f` to `0x62`) gives the leader command (`0x10` to `0x13`) when the click is nearer to the icon edges that face the party center; else it keeps the command when the spell is ready, and otherwise no command.

## Click zones of the view

- The view drawing records up to 32 click zones (`g_view_click_zones`, 12 bytes: rectangle, thing, cell, kind). The first 9 are fixed: the objects of the 4 cells (the party square, and the 2 near cells of the front square, when no walking creature stands on them), the creature possessions of 4 cells, and the alcove in front. `view_draw_dungeon` clears the thing and the kind of the fixed zones. Their rectangle and cell keep the values of an older view. The wall ornaments and the door buttons add zones after the fixed ones.
- A fixed zone of an object grows to hold all the objects of its cell, and is at least 14 × 11 pixels.
- `click_in_viewport` tests the zones in order: an object or alcove zone takes or places an object ([champions.md](champions.md#the-leader-hand)), a wall ornament goes to `sensor_process_wall_click`, a door button toggles the door. A click on no zone goes to the front wall or to a creature ([creatures.md](creatures.md)).

## Movement

- `g_cur_map` is the map of the dungeon reads (`dun_get_square`, `dun_move_relative`). The party map and the current map differ while things move on other maps.
- A slow party (`champ_get_move_delay`) walks: `g_party_move_steps` holds the view one square back for the steps of the walk.
- `move_follow_square_transitions` takes a thing through teleporters, open pits, and stairs until it stays on a square. A pit can lead to the map below or to a marker of the map.
- When the party arrives on a square with a creature that moves aside, the creature gets a loud alert. Its value is a quarter of the sum of the step times of the champions, plus a random part up to half of that quarter. The step time of a champion (`champ_get_move_time`) is 0 for a dead champion, 50 when the load / 10 plus the redraw bit `0x10` is not 0, and 40 otherwise. No code of `SKULL.EXE` sets the redraw bit `0x10` (`REDRAW_SLOW_STEP`); the port keeps the test.
- A move into a wall, a closed door, or a creature gives bump damage (`move_party_bump_damage`) to the champions on the front cells.

## Sensors and square events

- A square event (type 4) holds x, y, cell, and effect: 0 set, 1 clear, 2 toggle, 3 hold. The priority is 1 for set, 3 for clear, 2 for toggle.
- Floor sensors react when a thing (or the party) enters or leaves the square. Wall sensors react to clicks and to square events. A sensor with a target queues a square event at the target after its delay.
- The script variables (`game_global_var`): [skull-map.md](../reference/skull-map.md#global-variables-script-variables).
- Door events: type 1 opens or closes a door one step per event, type 2 breaks it.

## Deviations from the original

- Uninitialized data of the original: some events keep 2 to 4 data bytes or the high bits of a word from the stack (hold square events, sensor timers, item and wall events). The port sets them to 0. The reverse of a hold effect and the priority of a hold square event are uninitialized too: the port keeps the hold effect and uses priority 3.
- Bounds: a wall square event to a map outside the dungeon does nothing; at most 32 view click zones; a teleporter square without things has no teleporter link and is not a closed teleporter (`link_side`, `is_closed_teleporter`; the original reads the record of the end mark); a teleporter link to a map outside the dungeon is no link; `dungeon_unlink_thing_from_square` on a square without things does nothing (the original reads before the first-thing table); `dungeon_link_thing_to_square` returns false for a full first-thing table (the original stops with a system error). A create item sensor (`0x3c`) whose item index has no thing type creates nothing; the original asks for a free record of type `0xffff`. When the party falls from a trap teleport text (`fall_risk`) and the random champion is dead, the hurt sound takes the portrait of the leader; without a leader the port plays no hurt sound (the original reads the portrait of champion index `0xffff`, before the champion table).
- System errors of the timeline: when an event is not in the queue (`find_queue_slot`, `system_error(0x46)`) or the timeline is full (`timeline_add_event`, `system_error(0x2d)`), the original shows `SYSTEM ERROR 70` or `SYSTEM ERROR 45` on the screen, waits 300 vertical blanks or a key, and quits. The port calls `os_fatal` with the same text (`src/engine/timeline.c`): the fatal hook (or the error output without a hook) gets the text, then the program exits with `EXIT_FAILURE`.
- Out of memory: a failed allocation that has no error result calls `os_fatal("out of memory")` (`src/engine/` and `src/control/`). The original has no such exit.
- `creature_cell_mask` reads kinds 5 to 15 past its table: [creatures.md](creatures.md#deviations-from-the-original).
- The fall view of a pit goes into the viewport, and the next present shows it. The original presents it in the middle of the step.
- The push arrow under the mouse stays pressed in the arrow panel while the mouse selects it in push mode (`input_release_move_arrow`). The original draws it pressed on the screen and waits for a vertical blank, then releases it in the same game step. The port presents the screen only after the step, so the original order shows no pressed arrow.
- The video driver of the original (`IBMIOP.EXE`, service 0x109, mode 0x8008) shows the walking frame stretched to the full viewport. The port does not reimplement the driver. `play_present_viewport` stretches the walk rectangle of the frame to the viewport with nearest neighbour while `move_steps` is not 0. The exact rounding of the driver is unverified (manual check B7).
- The advanced mode takes the autorepeat of a held key only when no key, event, or command waits (`input_key_repeat`). In the original, the repeats of a held key fill the event ring, and the party walks on after the key goes up. The vanilla mode keeps that.
- A move without a destination (a removal) leaves a stale register in the destination map of the original (`g_party.dest_map`). The port writes the current map; the scenario tests skip `dest_map` after a removal.
- Kept quirks of the original:
  - `dungeon_location_on_map_from` compares the upper map bounds unsigned, so a location at x or y = -1 next to a map at offset 0 is not on that map
  - a pit to a marker (`transit_pit_marker`) sets the thing type of the transit to the type of the last thing that it reads on the pit square
  - `dungeon_door_type` returns `0xff` for a door type without a door, and the callers pass it to GDAT as the entry index
  - a pit to a marker without a pit target marker of its id sends the thing to map 0, at the square of the last position marker of the id, or else at the coordinates of the pit square
  - `click_in_viewport` tests the passable fake wall flags of the front square only when it is a wall, which never has them: a wall in front always takes the click of the leader hand object off the front cells, and a click on it always sounds
  - the dialog button opcodes `0xf` and `0x10` of a click feedback script forget the feedback bitmap that the script opened (`feedback_draw_op`)
- Not ported: the enter and leave events of the cursor zones ([champions.md](champions.md#the-cursor)); the mouse drag through a fake wall; `input_check_memory_cheat`, a key sequence that prints the free memory in the message area.
