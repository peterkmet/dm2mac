# Turn-based mode: PRD

## Goal

A program or an LLM plays Dungeon Master II through the control socket, turn by turn. The game time stands still between turns. The window shows the game, so a person can watch. LLM runs measure how much the LLM player gets better from session to session and from run to run, as the runs of the `dm` port do.

The source is the turn-based mode of the `dm` port (`../dm`): `docs/llm-mode.md`, `docs/llm-playbook.md`, `port/src/platform/control.c`, `port/src/platform/state_json.c`, `tools/dmtools/play.py`, `.claude/agents/dm-coordinator.md`, `.claude/agents/dm-player.md`, and `playthroughs/template/`. The port takes over its protocol, its rules of turns and stops, and its run structure, and adapts them to Dungeon Master II.

## Reader

The developer and the agents that write the implementation plan and the code.

## Scope

In scope:

- `--mode turnbased`: turns over the existing control socket (`--control PATH`).
- The JSON of the state, the view, and the explored map.
- The turn commands `act`, `act_many`, and `wait` with their stops, and `save`, `load`, `new_game`.
- The actions of `dm`, adapted to Dungeon Master II, plus the DM2 actions: both hands, the money box, and the minion map. The raw actions `click` and `key` as turns, for the rest (merchants, dialogs).
- `dm2-play`: the `act` and `act_many` forms, `--brief`, and the call log.
- The LLM runs: the player manual `docs/llm-playbook.md`, the agents `dm2-coordinator` and `dm2-player`, and `playthroughs/` with the template of the player files.

Out of scope:

- An MCP server. The LLM uses `dm2-play` through a shell.
- Path finding. The player plans routes from its own map.
- A throw action. The `dm` port has none.
- The movies (`docs/superpowers/2026-10-10-movies.md`). The turn-based mode plays no movie.
- Changes of the game rules.

## Acceptance

```gherkin
Feature: Turn-based mode
  Scenario: start
    When build/dm2 --mode turnbased --control PATH starts without --save
    Then the game starts a new game without the start menu
    And the game time stands still until a turn arrives
    And keys and clicks in the window do not control the game

  Scenario: one action
    Given the party stands in front of an open floor square
    When a client sends {"cmd": "act", "action": "move_forward"}
    Then the turn runs until the party can move again
    And the reply has ticks, game_over, state, and view
    And the party stands one square further

  Scenario: bump
    Given a wall in front of the party
    When the client sends act move_forward
    Then the reply has "bumped": true and the party square stays

  Scenario: batch stops early
    When the client sends act_many with five moves
    And a new creature group comes into view after the second move
    Then the reply has "ran": 2 and "stopped": "creature"

  Scenario: wait stops on news
    When the client sends {"cmd": "wait", "ticks": 5000}
    And a champion loses health after 300 game steps
    Then the reply has "stopped": "damage" and the ticks that ran

  Scenario: raw click as a turn
    Given the volume dialog is open
    When the client sends act click with x and y of a button
    Then the game gets the click and the turn runs

  Scenario: save and load
    When the client sends save, then a move, then load
    Then the party square and the game time are those of the save

  Scenario: party dies
    When the party dies during a turn
    Then the reply has "game_over": true and state.party.dead is true
    And act, act_many, wait, and save reply {"ok": false, "error": "game over"}
    And load and new_game start a game again

  Scenario: game completed
    When the party completes the game during a turn
    Then the reply has "game_over": true and state.party.dead is false
    And the program does not end

  Scenario: recruit
    Given a candidate from a mirror on the screen
    When the client sends act recruit with choice join
    Then the candidate is in state.champions

  Scenario: both hands
    Given a champion with an action in the ready hand
    When the client sends act choose_action with hand ready and index 0
    Then the action runs and the reply has action_damage

  Scenario: money box
    Given an open money box with a coin in slot 0
    When the client sends act take_coin with slot 0
    Then the leader hand holds that coin

  Scenario: direct commands refused
    When the client sends {"cmd": "key", "key": "up"}
    Then the reply is {"ok": false, "error": "use act in turnbased mode"}
    And the party square stays

  Scenario: brief output
    When the player runs uv run dm2-play --brief act move_forward
    Then dm2-play prints the short summary, not the JSON
    And it adds one line to the call log

  Scenario: a run session
    Given a run folder playthroughs/run1 and the game in the turn-based mode
    When the main session starts dm2-coordinator with session 1
    Then the coordinator starts dm2-player, makes the checkpoint, and logs the cost
```

## Invariants

1. The game rules, the save format, and the screen do not change. A turn gives its input through the same path as a player: the input queue of `ui/input.c`.
2. The `vanilla` and `advanced` modes do not change. The control socket in these modes keeps its commands and their behavior (`docs/llm-control.md`).
3. The turn-based mode has the features of the advanced mode except the keys: the AMAP block in the saves and the seen squares (`engine/seen.c`). The cheats are always off.
4. The view and the map show only what a player sees, plus the explored map. They give no creature health, no hidden square, and nothing behind a wall that blocks the sight.
5. The JSON code uses no SDL. The tests call it through `build/libdm2.dylib`.
6. Coding standard rule 8 holds: no file includes a header of a higher layer.
7. A failed request changes nothing in the game.
8. One tick of the turn protocol is one game step of `game.game_time`.

## Current state

- `platform/control.c` serves the control socket in all modes: `state`, `key`, `click`, `press`, `release`, `command`, `move`, `wait`, `screenshot`, `quit` (`docs/llm-control.md`). The game runs in real time; `wait` lets timer ticks run. The parser is flat. Replies are at most 4096 bytes.
- `platform/main.c` runs the PIT loop: `play_tick` every 4 PIT ticks (60 Hz), `pump_events` with `control_poll` every PIT tick, and `play_frame_hook = modal_frame` for the modal loops (menu, dialogs).
- `game_mode` (`engine/game.h`) has `GAME_MODE_VANILLA` and `GAME_MODE_ADVANCED`; `--mode` takes `vanilla` or `advanced`.
- `ui/advanced.c` has the quick save and quick load of `<data>/DATA/QUICK.DAT`. `ui/session.c` writes the slots `<data>/DATA/SKSAVE<n>.DAT`.
- `engine/seen.c` keeps the seen squares of every map; `ui/automap.c` draws them. The seen rule is in `docs/advanced-mode.md`.
- `game.party_move_cooldown` counts down in `play_tick` (`ui/play.c`); the party can move again at 0.
- `session_complete` ends the program. `session_end` offers the load of the last slot.
- A new game runs only through `session_init` and the menu; `load_new_game` is static in `ui/session.c`.
- `ui/advanced.c` `quick_path` builds `<data>/DATA/QUICK.<ext>` from `session.data_dir`; the quick load has three failures: no file, a damaged file (the game comes back from `QUICK.TMP`), and a failed restore (`session_quit`, exit code 4).
- The dialogs are blocking loops (`dialog_run`, `menu_run`, the slot dialogs, `session_edit_champion_name`) on `command_process_queue` and `play_wait_frames`, which calls `play_frame_hook`.
- `src/dm2tools/control_client.py` is `dm2-play`: one request, one JSON reply line, exit code 1 on `"ok": false`.
- The runs of `dm` start the game with `--saves` and use `dm-play`, its counterpart of `dm2-play`.

## Requirements

### Mode and start

1. **Mode.** `--mode turnbased` is a third value of `game_mode`. `--control PATH` is optional in this mode; the default is `~/.dm2/control.sock`. The unknown-mode message lists the three modes. The `game_mode` checks change:
   - `play.c:142` (`seen_mark`) and `save.c:810` (the AMAP block) test `game_mode != GAME_MODE_VANILLA`.
   - `input.c:491` and `advanced.c:40` (the advanced keys and the cheat keys) test `game_mode == GAME_MODE_ADVANCED`: the turn-based mode has no advanced key.
2. **Start.** `ui/session.c` gets `session_start_new_game()`: `load_new_game`, the volumes, `ui_reset`, `money_init_coin_table`, and `play_start_session`, as `session_init` without the menu. Without `--save`, the turn-based mode calls it. With `--save PATH`, it loads that save. The control socket opens before the first turn, and the game does not tick before a turn.
3. **Window.** The window shows each frame. Keys and clicks of the window do not reach the game; the quit event of the window ends the program. The window pointer does not set `input_mouse_x` and `input_mouse_y`; only `act click` moves the pointer.
4. **Save folder.** `--saves DIR` sets the folder of the quick save in all modes: `ui/advanced.c` builds its path from `session.save_dir`. The default is `<data>/DATA`. `save` writes `DIR/QUICK.DAT` with the quick save code (the old file stays on an error); `load` reads it with the quick load code. Both keep the AMAP block. The results of `load`:
   - No `QUICK.DAT`: the reply is `no saved game (use save first)`. Nothing changes.
   - A damaged `QUICK.DAT`: the reply is `the saved game is damaged`, and the running game goes on.
   - A damaged `QUICK.DAT` and a failed restore from `QUICK.TMP`: the same error. The program stays, and only `load` and `new_game` work until one succeeds (as `load_failed` in `dm`).

### Turns

5. **act.** An `act` turn gives the action as input to the input queue, then runs `play_tick` `STEP_TICKS` times per frame without `plat_wait_tick`: one game step per frame. It ends when at least 2 ticks ran, `game.party_move_cooldown` is 0, `party.move_steps` is 0, and `in.wait` is 0; else after 64 ticks, or at once at a game over. `ticks` in every turn request and reply counts game steps (`game.game_time`). The constants of `dm` (2, 64) are the start values; the plan checks them against DM2.
6. **act_many.** `actions` is a list of action names or `{"action", "args"}` objects. Each item is one `act` turn. After each turn the batch checks the stops in this order: `game_over`, `bumped`, `moved`, `turned`, `damage`, `creature`, `message`, as in `dm`. An action that cannot run ends the batch with `failed` and `error`; it is not in `ran`. `done` when all items ran. The reply has `ran` and `stopped`.
7. **wait.** A `wait` turn runs `ticks` game steps (1 to 5000) and checks the news after each step: `game_over`, `damage`, `creature`, `message`. The reply has the ticks that ran and `stopped`. In the turn-based mode this replaces the real-time `wait`.
8. **Bump and displacement.** As in `dm`: `bumped` when a move does not change the square; `moved` and `turned` when the dungeon changes the square, the map, or the facing in another way than the action (teleporter, pit, stairs, spinner).
9. **Reply of a turn.** `ticks`, `game_over`, `state`, `view`; `stopped` after a displacement or in a batch and a wait; `bumped`, `action_damage`, `health_lost`, `look`, and `seen` only when they apply, with the meaning of `docs/llm-playbook.md` of `dm`.
10. **Seen record.** Each turn request records after each tick the explosions and the creature groups that lose creatures in the view, as `seen` in `dm`.
11. **Modal loops.** In the turn-based mode `play_frame_hook` is a function that calls `control_poll` and presents the frame, without `plat_wait_tick`. The game time does not advance in a modal loop, so a turn in a modal loop ends after 16 frames per tick of its limit (the frame limit of `dm`). A dialog that the game opens stays open after the turn, and `state` shows it; the player answers it with `act click` or `act key`. While `state.mode` is `menu`, `picture`, `dialog`, or `name`, `save`, `load`, and `new_game` reply `close the dialog first (act click or act key)`.
12. **Game over.** A party death or a completed game ends the turn with `"game_over": true`. The program does not end and does not offer the load of the last slot:
    - `main.c` does not call `session_end` in the turn-based mode.
    - The hook `complete_game` sets `session.completed` and nothing else.
    - `main_loop.end` stays set until `load` or `new_game`.

    After that, `act`, `act_many`, `wait`, and `save` reply `game over`. `get_state`, `get_view`, `get_map`, `state`, and `screenshot` work. `load` and `new_game` start a game again.

### Actions

13. **The actions of `dm`.** `move_forward`, `move_back`, `move_left`, `move_right`, `turn_left`, `turn_right`, `click_wall_ahead`, `take_object`, `drop_object`, `open_inventory`, `close_inventory`, `put_in_slot`, `take_from_slot`, `eat`, `drink`, `choose_action`, `pass`, `set_caster`, `add_rune`, `delete_rune`, `cast`, `recruit`, `set_leader`, `sleep`, `wake`, `swap_champions`, `look`. Each gives the clicks or keys that a player gives. The plan maps each action to the commands of `ui/input.h` and the slot numbers of DM2, and lists the actions that DM2 does not have in the same form. `recruit` takes `choice`: `join` (`CMD_RECRUIT`) or `cancel` (`CMD_RECRUIT_CANCEL`): the DM2 candidate joins or goes back to its mirror.
14. **Both hands.** `choose_action` takes `champion`, `hand` (`ready` or `action`), and `index`. The state lists the action menu of each hand.
15. **Money box.** `take_coin` takes `slot` 0 to 5, the coin position in the money box (`CMD_COIN_FIRST` + `slot`), and moves one coin to the leader hand. The state lists each slot of the open money box with its coin name and count.
16. **Minion map.** `minion_map_option` takes `option` 0 to 3, as a click on a map option (`CMD_MAP_OPTION_FIRST` to `CMD_MAP_OPTION_LAST`). The state gives the open minion map and its options.
17. **Raw input.** `click` (`x`, `y`, `button`) and `key` (`key` as in `docs/llm-control.md`) give the input of the existing commands and run a turn. In the turn-based mode the direct commands `key`, `click`, `press`, `release`, `command`, and `move` reply `{"ok": false, "error": "use act in turnbased mode"}`.
18. **Errors.** An action that cannot run now replies an error and changes nothing. Examples:
    - `put_in_slot` with an empty leader hand.
    - A move while the inventory is open.
    - An action other than `recruit` while a candidate is on the screen.
    - An action other than `wake` while the party sleeps.

### JSON

19. **get_state.** The fields of `dm` that DM2 has, in the units of the screen:
    - `time`.
    - `party`: `map`, `x`, `y`, `facing`, `darkness`, `can_move`, `sleeping`, `dead`.
    - `leader` and `leader_hand`.
    - `candidate`.
    - `champions[]`: health, stamina, mana, food, water, load, position, statistics, skills, wounds, slots, the action menu of each hand, runes.
    - `action_menu`, `spell`, `inventory`, `messages`.

    DM2 additions:
    - `game_mode`, and the input mode and the dialog id as in `state`.
    - The open dialog: its id and the texts of its buttons. DM2 has no merchant panel: a merchant is a creature in the view (`get_view`).
    - The open money box: each slot with its coin name and count.
    - The open minion map and its options.
    - `outdoor`: the party map has a sky. `weather` and `day_time` only when `outdoor` is true, because the player sees them only in the sky.
20. **get_view.** The squares of the view by the seen rule of the automap (`docs/advanced-mode.md`: party row and 4 rows ahead, light limit). `depth` is 0 to 4; `side` is the column offset from the party lane, -3 to 3 (0 is the center). Each square has its element, door and its state, objects with their cell, flying objects and explosions, creatures (type and count; a merchant is a creature with its type), floor ornaments, and the wall faces (text, switch, lock, alcove, fountain, ornament) with names (see Decisions: names in the JSON); else the index. `piles[]` lists every object of each reachable cell, top first.
21. **get_map.** The seen squares of the party map as `rows` and `grid` (the format of `dm`): walls, floors, doors with their state, stairs, pits, teleporters, and floor squares with a floor ornament that the automap draws (for example a pressure pad). No wall features, no floor sensor without an ornament.
22. **Size.** A reply can be longer than 4096 bytes. All replies use one buffer that grows; the request lines keep their limit. The error `reply too long` goes away.

### dm2-play

23. **Forms.** `uv run dm2-play act <action> [json-args]`, `uv run dm2-play act_many '<json-list>'`, and the existing `<cmd> [json-fields]` form.
24. **--brief.** Before the command, `--brief` prints the summary of `dm`, adapted to DM2: the turn line, the party, the leader hand, the candidate, one line per champion, the open action menu, the dialog, the messages, the sides, the squares ahead up to `ahead 4`, the side passages, the piles, and the `look` and `seen` lines. For `get_map` it prints `grid`.
25. **Call log.** Each call appends one line to `<DM2_PLAY_LOG_DIR>/calls.log`. The default folder is `playthroughs/current/coordinator`; `DM2_PLAY_LOG_DIR=none` turns the log off. The tests log into a temporary folder. A call to a game that does not run is logged and returns exit code 1.

### LLM runs

26. **Player manual.** `docs/llm-playbook.md`: the manual of `dm`, rewritten for Dungeon Master II. Its game part (party, health, food, light, skills, magic, fights, money, merchants, minions, outside and weather, saves, when stuck, bugs) and its play part (start of a session, rules, commands, batches, bump, displacement, wait, seen, game over, actions, cells, slots, fields of the state, the view, and the map, end of a session). The goal of the game is the goal of DM2: the party completes the game.
27. **Run guide.** `docs/turnbased-mode.md`: how to start the mode, the protocol of the turn commands, the goal of the runs and the allowed changes of the player instructions (from `docs/llm-mode.md` of `dm`), the runs and checkpoints, the coordinator as a subagent, the C API, and the tests. `docs/llm-control.md` refers to it for the turn-based mode.
28. **Agents.** `.claude/agents/dm2-coordinator.md` and `.claude/agents/dm2-player.md`, from the agents of `dm`, with `dm2-play`, `QUICK.DAT`, and the paths of this repository.
29. **Run folders.** `playthroughs/template/` holds the player files with their headers (in git). A run folder `playthroughs/run<N>/` has `player/`, `coordinator/`, `game/`, `saves/`; `playthroughs/current` links to the current run. Only `playthroughs/template/` is in git. A run starts the game with `SDL_AUDIO_DRIVER=dummy build/dm2 --mode turnbased --saves playthroughs/current/game`, then `new_game` and `save` for a new run, or `load` for a resume.
30. **Docs.** `docs/advanced-mode.md` and `docs/llm-control.md` name the third mode; `docs/llm-control.md` loses the error `reply too long`. `docs/manual-test-checklist.md` gets the checks of the turn-based mode. `CLAUDE.md` (Commands) and `CHANGELOG.md` name the mode.

## Decisions

| Topic | Decision | Reason |
|---|---|---|
| MCP server | None. | User choice: the LLM uses the socket through `dm2-play`. |
| Run structure | The playbook, the agents, and `playthroughs/` as in `dm`. | User choice. |
| Action set | The actions of `dm`, adapted to DM2, plus both hands, the money box, and the minion map; `act click` and `act key` for the rest. | User choice. Dialogs have many screens; a raw click covers them with less code. |
| Merchants | No merchant panel in the state; a merchant is a creature of the view. | User choice. DM2 has no merchant panel: the trade puts goods and coins on the cell in front of the merchant; a shop is a wall with a display. |
| Floor marks in `get_map` | `_` marks a floor square with a floor ornament that the automap draws; no glyph for a floor sensor. | User choice. DM2 shows a pressure pad only by its floor ornament; the map shows what the automap shows (Invariant 4). |
| Socket | The existing `--control` socket; the turn-based mode adds the new commands to it. | One protocol and one client for UI tests and turns. |
| Command names | `get_state`, `get_view`, `get_map`, `act`, `act_many`, `save`, `load`, `new_game` as in `dm`; `state`, `screenshot`, `quit` stay. | The playbook and the agents of `dm` carry over with few changes. `state` stays for the UI tests. |
| `wait` | In the turn-based mode, `wait` is the turn wait with stops (1-5000 ticks). | The real-time wait has no meaning when the time stands still. |
| Direct input commands | Refused in the turn-based mode. | Input without a turn does nothing until the next turn and makes the turn result unclear. `act click` and `act key` replace them. |
| Window input | Only the quit event. | As in `dm`: the window is for watching. |
| Start | A new game without the start menu; `--save` loads. | As in `dm`: a run starts without clicks on menus. |
| Save slot | One slot, `QUICK.DAT` in `--saves DIR`, with the quick save code. | As in `dm` (one slot). The quick save code already keeps the AMAP block and the run needs its own folder. |
| Start of a run | `new_game`, then `save`; the player recruits the champions. | DM2 has no start party in the game files. The recruits are part of the play; the template has no save file. |
| Game completed | `game_over` with `dead` false; the program stays. | As in `dm`. The coordinator reads the result and ends the run. |
| Cheats and automap keys | Off. | As in `dm`: the runs measure the player, not the cheats. `get_map` gives the automap. |
| Turn end | `game.party_move_cooldown`, `party.move_steps`, and `in.wait` are 0 after at least 2 game steps; at most 64 game steps. | As in `dm` with the DM2 counters: the party square changes only at the end of the walk (`party.move_steps`). The plan checks the limits. |
| Tick unit | One tick of the turn protocol is one game step. | The game rules count game steps; a timer tick has no meaning when the time stands still. |
| Movies | None in the turn-based mode. | A run starts and ends without a click on a movie. |
| JSON layer | `platform/state_json.c`, no SDL, in the library. | As in `dm`; `platform/` may read `engine/` and `ui/`. |
| Reply size | One growing buffer for all replies; `reply too long` goes away. | A state with four champions and a view is longer than 4096 bytes. One buffer is less state than two; a reply that fits keeps its bytes. |
| Names in the JSON | Objects: `item_name` (GDAT). Creatures, explosions, floor and wall ornaments: a named `const` table in `platform/state_json.c`, as in `dm`; an entry without a name gives the index. | The original shows only pictures; a table is one place to extend. |
| `recruit` | `choice` `join` or `cancel`. | DM2 has two recruit buttons (`CMD_RECRUIT`, `CMD_RECRUIT_CANCEL`), not the resurrect and reincarnate of DM. |

## Known consequences

- The direct input commands do not work in the turn-based mode. A UI test that uses them must run in the `advanced` or `vanilla` mode.
- `QUICK.DAT` in the turn-based save folder is a normal quick save. The advanced mode loads it with F9 when it runs with the same `--saves DIR`.
- In the turn-based mode `wait` counts game steps, not timer ticks, and stops early on news; its limit is 5000.
- `press` and `release` do not work in the turn-based mode: `look` is the only way to the eye panel, `eat` and `drink` to the mouth.
- The game window does not react to keys and clicks in the turn-based mode.

## Risks

- DM2 screens without a named action (dialogs) and the trade with merchants need `act click` with positions or the drop actions. The player needs a screenshot or the button texts of the state to find them.
- The rules of the turn end and the stops come from DM. A DM2 action with a long animation (for example a minion) can need more than 64 ticks. The plan measures it.
- The name table of creatures and ornaments starts small. The view gives the index where no name exists.
