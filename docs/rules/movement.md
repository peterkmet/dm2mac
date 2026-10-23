# Main loop, input, and movement

The rules of the main loop, the input, and the movement: the timing and the game step, the input tree, the click feedback, the click zones of the view, the movement of the party, and the sensors and square events. The code is in `src/ui/play.c`, `src/ui/input.c`, `src/engine/move.c`, `src/engine/sensor.c`, and `src/engine/timeline.c`. The addresses and the notation follow the conventions of [skull-map.md](../reference/skull-map.md).

## Timing and the game step

- The PIT runs at 240 Hz. Every 4th PIT tick is a timer tick (60 Hz). A game step runs every 8 timer ticks (`g_main_loop.tick_threshold`), every timer tick while the party sleeps.
- The names of the inventory zones `ZONE_EYE` (`0x221`) and `ZONE_MOUTH` (`0x222`) in `data/ids.h` are swapped against the input table: the input table gives zone `0x221` to the mouth command `0x46` and zone `0x222` to the eye command `0x47` ([champions.md](champions.md#deviations-from-the-original)). The port keeps the names. The `look` action presses the zone of the eye command in the input table (`input_command_rect`).
- A turn gives a command as a click on the top left pixel of its first zone in the current input mode (`input_simulate_command_click`). The zone of `CMD_LEADER_INVENTORY` (`0x52`) is the whole screen, and a click there hits the status box of champion 0 first. The `sleep` action therefore opens the inventory of the leader with its status box command (`0x07` + leader).
- `choose_action` on a hand that holds a money box, a chest, or a minion map gives only the hand click: the hand opens the panel of the item, not an action menu, and `index` is not used.
- `game_loop` of `SKULL.EXE` waits for input in the middle of a step. The port splits it in `ui/play.c`: `step_before_input` (the timeline, the view, the party clocks) and `step_after_input` (a map change, repeated actions, music). The party clocks and the game time are in `engine/game.c` (`game_run_party_clocks`, `game_advance_time`); `step_before_input` calls them. `play_tick` runs one timer tick: the input, the click feedback, the command queue, and a game step when it is due.
- The calls of the rules into the interface are members of `g_hooks` (`engine/hooks.h`, set in `ui/ui_hooks.c`).
- Pause (`pause_game`) stops the game time until the resume command. The port keeps the main loop running and clears `g_main_loop.running`.

## The input tree

- The tables are in `SKULL.EXE` and in `src/ui/input.c`: the nodes (condition with bit 7 for a node with children, argument, child list or zone), the child lists (bit 7 starts a list), the root node of each of the 10 input modes, the zones (7 bytes: layout zone with bit 15 for the inventory panel and bit 14 for the action area, command list, key list, cursor area), the command lists at `0x72388` (6 bytes: command with bit 15 to start a list, zone, mouse buttons, flags), and the key lists at `0x729b8` (4 bytes: command with bit 15 to start a list, key code).
- A key code is the PC scan code, plus `0x200` for shift, `0x400` for control, `0x800` for alt. The arrow keys give the numpad codes: up `0x4c`, left `0x4b`, right `0x4d`, down `0x50`, shift and left `0x4f`, shift and right `0x51`. Esc (`0x01`) pauses.
- A click goes down the tree from the root node of the input mode. The first command whose zone holds the click and whose buttons match wins. The matched zone rectangle and the click position stay for the click feedback.
- The event ring holds 11 events. A new event goes in when fewer than 7 events wait (9 for a released left button and for the area events). A key goes into the ring only when the main loop reads it. `input_flush` keeps only the area events and the released left buttons.

## Click feedback

- Each command has a feedback script: bytecode that draws the pressed button, waits timer ticks, draws the released button, and then queues the command. The low 6 bits are the opcode, bit 6 runs the next opcode at once, bit 7 ends the script.
- The port runs the opcodes 0 to `0x10`. Opcode `8` draws the spell icon, `9` the hand icon, `0xc` the action name, `0xf` and `0x10` the dialog buttons.
- Opcode `0xa` (rune commands `0x65` to `0x6a`) has no argument. It draws the pressed rune button (GDAT `(1, 5, 8)`, opaque) centered in the button rectangle. Then it draws the rune character of the button in the next rune set (`0x60` + 6 × rune count + command - `0x65`), centered, black on UI color 4 (`gfx_offscreen_draw_text_at`). The changed rectangle of that text starts at its base line.
- Opcode `0xb` (cast command `0x6c`) has no argument. It draws the entered runes with their background (`area_draw_entered_runes(1)`).
- Opcode `0xe` (map option commands `0x56` to `0x59`) has 1 argument byte: the option bits that show pressed (`mmap_draw_option_buttons`). The script draws the pressed button, executes the command, waits 4 ticks, and draws the buttons with argument 0.
- After opcodes `8`, `9`, `0xb`, `0xc`, and `0xe`, the script copies the action area bitmap to the screen and closes it.
- The movement arrows show pressed (`show_move_arrow`) until the move is done (`input_release_move_arrow`).
- Before the script lookup, `input_refine_champion_click` makes the command of a champion click. A hand in a status box (commands `0x74` to `0x7b`, cell = party direction + (command - `0x74`) / 2) gives `0x74` + 2 × champion + hand when the champion lives, the hand is ready, and the hand has actions; else no command. A champion icon of the party icons (`0x5f` to `0x62`) gives the leader command (`0x10` to `0x13`) when the click is nearer to the icon edges that face the party center; else it keeps the command when the spell is ready, and else no command.

## Click zones of the view

- The view drawing records up to 32 click zones (`g_view_click_zones`, 12 bytes: rectangle, thing, cell, kind). The first 9 are fixed: the objects of the 4 cells (the party square, and the 2 near cells of the front square, when no walking creature stands on them), the creature possessions of 4 cells, and the alcove in front. `view_draw_dungeon` clears the thing and the kind of the fixed zones. Their rectangle and cell keep the values of an older view. The wall ornaments and the door buttons add zones after the fixed ones.
- A fixed zone of an object grows to hold all the objects of its cell, and is at least 14 × 11 pixels.
- `view_click_in_viewport` tests the zones in order: an object or alcove zone takes or places an object ([champions.md](champions.md#the-leader-hand)), a wall ornament goes to `sensor_process_wall_click`, a door button toggles the door. A click on no zone goes to the front wall or to a creature ([creatures.md](creatures.md)).

## Movement

- `g_cur_map` is the map of the dungeon reads (`dun_get_square`, `dun_move_relative`). The party map and the current map differ while things move on other maps.
- A slow party (`champ_get_move_delay`) walks: `g_party_move_steps` holds the view one square back for the steps of the walk.
- `move_follow_square_transitions` takes a thing through teleporters, open pits, and stairs until it stays on a square. A pit can lead to the map below or to a marker of the map.
- A move into a wall, a closed door, or a creature gives bump damage (`move_party_bump_damage`) to the champions on the front cells.

## Sensors and square events

- A square event (type 4) holds x, y, cell, and effect: 0 set, 1 clear, 2 toggle, 3 hold. The priority is 1 for set, 3 for clear, 2 for toggle.
- Floor sensors react when a thing (or the party) enters or leaves the square. Wall sensors react to clicks and to square events. A sensor with a target queues a square event at the target after its delay.
- The script variables are 64 flags, 64 bytes, and 64 words (`dun_get_global_var`): the ops are set, clear, toggle, add, subtract, and assign.
- Door events: type 1 opens or closes a door one step per event, type 2 breaks it.

## Deviations from the original

- Uninitialized data of the original: some events keep 2 to 4 data bytes or the high bits of a word from the stack (hold square events, sensor timers, item and wall events). The port sets them to 0. The reverse of a hold effect and the priority of a hold square event are uninitialized too: the port keeps the hold effect and uses priority 3.
- Bounds: a wall square event to a map outside the dungeon does nothing; at most 32 view click zones.
- System errors of the timeline: when an event is not in the queue (`find_queue_slot`, `system_error(0x46)`) or the timeline is full (`timeline_add_event`, `system_error(0x2d)`), the original shows `SYSTEM ERROR 70` or `SYSTEM ERROR 45` on the screen, waits 300 vertical blanks or a key, and quits. The port calls `os_fatal` with the same text (`src/engine/timeline.c`): the fatal hook (or the error output without a hook) gets the text, then the program exits with `EXIT_FAILURE`.
- Out of memory: a failed allocation that has no error result calls `os_fatal("out of memory")` (`src/engine/` and `src/control/`). The original has no such exit.
- `creature_cell_mask` reads display kinds 5 to 15 from the bytes after its table in the original. The port keeps those bytes.
- The fall view of a pit goes into the viewport, and the next present shows it. The original presents it in the middle of the step.
- The video driver of the original (`IBMIOP.EXE`, service 0x109, mode 0x8008) shows the walking frame stretched to the full viewport. The port does not reimplement the driver. `play_present_viewport` stretches the walk rectangle of the frame to the viewport with nearest neighbour while `move_steps` is not 0. The exact rounding of the driver is unverified. Check it manually in DOSBox.
- Not ported: the enter and leave events of the cursor zones ([champions.md](champions.md#the-cursor)); the mouse drag through a fake wall; `input_check_memory_cheat`, a key sequence that prints the free memory in the message area.
