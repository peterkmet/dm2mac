# Turn-based mode

In the turn-based mode, a program or an LLM plays the game through the control socket. The game time stands still between requests. A request is one turn: the game runs until the party can act again, then the reply gives the state and the view as JSON. The window shows the game, so a person can watch.

This page is a run guide and a reference. The other modes and the raw commands are in [llm-control.md](llm-control.md). The design and the decisions are in [the PRD](superpowers/2026-10-10-turnbased-mode.md). The deviations of the port are in section 16 of [skull-formats.md](skull-formats.md).

## Start the game

1. Build the port.

   ```sh
   make
   ```

2. Start the game in the turn-based mode. A run plays without sound.

   ```sh
   SDL_AUDIO_DRIVER=dummy build/dm2 --mode turnbased --saves playthroughs/current/game
   ```

   - `--saves DIR`: the folder of the save file `QUICK.DAT`. The game does not make the folder; `save` fails when it is missing. Without it, the file is `game/DATA/QUICK.DAT` (the `--data` folder).
   - `--control PATH`: the socket file. Default: `~/.dm2/control.sock`. The game makes the last folder level of the path. `dm2-play` reads the same path from `--socket`, else from `DM2_CONTROL`, else `~/.dm2/control.sock`.
   - `--save FILE`: optional. The game loads this save slot file at the start, as in the other modes.
   - Without `--save`, the game starts a new game at once: no start menu and no movie. The new game has one champion (Torham). The player recruits the other champions at the mirrors.
   - The keyboard and the mouse do not control the game. A closed window quits. The request `quit` quits and the exit code is 0.
   - `--mode turnbased` and the mode `advanced` use the same `QUICK.DAT` format. The advanced mode loads a file of the turn-based mode with F9 when it runs with the same `--saves DIR`.

3. Send requests with `dm2-play` (see [Play from the shell](#play-from-the-shell-dm2-play)).

## Protocol

Each request is one JSON object on one line. Each reply is one JSON object on one line. The connection rules, the key names, and `screenshot` are in [llm-control.md](llm-control.md).

- Success: `{"ok":true, ...}`.
- Failure: `{"ok":false,"error":"<reason>"}`. A failed request changes nothing in the game.
- The game takes one client at a time. While a turn runs, it reads no next request. A client that connects again right after a reply is not refused with `busy`.
- Replies have no size limit.
- The request parser is a flat search for `"key":`. The keys in `args` are found the same way. A string value cannot contain a `"` character.

### Commands

| `cmd` | Fields | Reply |
|---|---|---|
| `get_state` | none | `state` |
| `get_view` | none | `view` |
| `get_map` | none | `map` |
| `act` | `action`, `args` | After the turn: `ticks`, `stopped` (after a displacement), `game_over`, `bumped`, `action_damage`, `health_lost`, `look`, `seen`, `state`, `view` |
| `act_many` | `actions` | After the batch: `ticks`, `ran`, `stopped`, `error`, `game_over`, `bumped`, `action_damage` (a list), `health_lost`, `look`, `seen`, `state`, `view` |
| `wait` | `ticks` (1-5000, default 1) | After the turn: `ticks`, `stopped`, `game_over`, `health_lost`, `seen`, `state`, `view` |
| `save` | none | `{"ok":true}`. The game writes `QUICK.DAT`. The old file stays when the write fails. |
| `load` | none | `state`. The game loads `QUICK.DAT`. |
| `new_game` | none | `state`. The game starts a new game from `DUNGEON.DAT`, on the start square. |
| `state` | none | The control state of the other modes (`docs/llm-control.md`). |
| `screenshot` | `path` | `path`. |
| `quit` | none | `{"ok":true}` |

- `key`, `click`, `press`, `release`, `command`, and `move` are refused: `use act in turnbased mode`. Use `act click` and `act key`.
- An unknown `cmd` gives `unknown cmd`. A request without `cmd` gives `missing cmd`.
- `act`, `act_many`, `wait`, and `save` give `game over` after a game over. `get_state`, `get_view`, `get_map`, `state`, `load`, and `new_game` still work.
- `save` gives `answer the candidate first (act recruit)` while a champion candidate shows, and `cannot write the save file` when the write fails. `save` is allowed with no champion.
- `load` gives `no saved game (use save first)` when `QUICK.DAT` does not exist, `the saved game is damaged` when it does not load, and `cannot write QUICK.TMP: nothing loaded` when the game cannot keep a copy of the running game. The game goes on unchanged. When the copy also does not load, the game is lost (see below).
- `load` and `new_game` also work after a game over. Each ends the old game without a question. After `load`, the game time is the time of the save plus 1 (the start of the session runs one game step). A failed `load` of a damaged `QUICK.DAT` also adds one game step: the game restores itself from its copy as the advanced mode does.
- `new_game` gives `cannot start the new game` when `DUNGEON.DAT` does not load. The game is lost then.
- A lost game (a failed restore or a failed new game): every command except `load`, `new_game`, and `quit` gives `no game is running: use load or new_game`.
- `save`, `load`, and `new_game` give `close the dialog first (act click or act key)` while a modal loop of the game runs (a menu, a picture, a dialog, or the name editor). Close it with `act click` or `act key`.

### Turns

An `act` turn does these steps:

1. It gives the inputs of the action to the input queue, one after the other (at most 4). This is the path of a player: a key, a click on a command zone, or a click at a screen point. The next input goes when the input pipeline is idle.
2. It runs the game one game step per frame (`game.game_time` grows by 1). One tick of the turn protocol is one game step.
3. It ends when at least `TURN_MIN_TICKS` (2) steps ran, all inputs are given, the input is idle, and the party can move again (no move cooldown, no walk in progress, no wait of the input). It also ends after `TURN_MAX_TICKS` (64) steps, and at once when the game is over.

A game that does not tick (a dialog) ends a turn after `FRAMES_PER_TICK_LIMIT` (16) frames for each tick of the limit. The game rules do not change.

A `wait` turn runs `ticks` game steps (`TURN_WAIT_MAX_TICKS` is 5000). It ends early when news arrives. The real-time `wait` of the other modes keeps its own limit of 6000 timer ticks.

`stopped` in the reply of `wait`:

| Value | Meaning |
|---|---|
| `done` | All `ticks` ran. |
| `game_over` | The party died or the game was finished. |
| `damage` | A champion lost health. |
| `creature` | A creature group came into view that was not in view before. |
| `message` | The game printed a new message. |

The checks run in this order: `game_over`, `damage`, `creature`, `message`.

The reply of `act` has `stopped` only when the dungeon displaced the party:

| Value | Meaning |
|---|---|
| `moved` | The party is not on the square of the step, or it is on another map (a teleporter, a pit, stairs). |
| `turned` | The party faces another way than the turn gives (a spinner). |

`bumped` is `true` after a step that left the party on its square: a wall, a closed door, or a creature is in the way. A teleporter that sends the party back to its square counts as a bump (DM2 has no teleport counter).

### Batches

`act_many` takes `actions`, a list. An item is an action name (`"turn_left"`) or an object (`{"action":"take_object","args":{"cell":2}}`). Items are separated by spaces and commas only. A string holds no `"` character.

The batch runs the items in order, one turn each. After each turn it stops when one of these is true. The checks run in this order:

| `stopped` | Cause |
|---|---|
| `game_over` | The game is over. |
| `bumped` | The step bumped. |
| `moved`, `turned` | The dungeon displaced the party. |
| `damage`, `creature`, `message` | News, as for `wait`. |
| `failed` | An item cannot run: `error` has the reason. The game does not change for this item. |
| `done` | All items ran. |

The reply of a batch has `ran` (the turns that ran), `stopped`, `error` (only with `failed`), and `ticks` (the game steps of all turns). `action_damage` is a list: one value for each `choose_action` that ran. `look` has the text of the last item only. A bad first item gives `ok` true, `ran` 0, `stopped` `failed`, and the `error`. An item that is not a name or an object gives `an item of actions is not an action name or object`. `actions` that is not a list gives `actions must be a list` (`ok` false).

### Reply fields

| Field | When | Content |
|---|---|---|
| `ticks` | always | The game steps that ran. |
| `stopped` | see above | The stop of the turn, the batch, or the wait. |
| `ran` | batch | The turns that ran. |
| `error` | batch, `failed` | The reason. |
| `game_over` | always | `true` when the party died or the game was finished. |
| `bumped` | after a bump | `true`. |
| `action_damage` | `choose_action` | The damage of the action (a number; a list in a batch). |
| `health_lost` | a champion lost health | A list: the health that each champion lost since the start of the request. |
| `look` | `look` | `{"name": "...", "lines": ["...", ...]}`: the text of the eye panel. |
| `seen` | see below | What the party saw during the request. |
| `state`, `view` | always | The state and the view at the end. |

`seen` is present only when the party saw an explosion or a creature group lose creatures during the request. It shows things that are gone at the end.

- `explosions`: a list of `{name, x, y, depth, side}`. `name` is a name such as `fireball`, or the number of the explosion type.
- `creature_changes`: a list of `{type, x, y, depth, side, before, after}`. `after` is 0 when the group was killed. A group has one creature, so `before` is 1.
- `side` is `center`, `left`, `right`, `left_2`, `right_2`, `left_3`, or `right_3`.

### State, view, and map

`get_state` gives these fields (the JSON shows only what a player sees):

| Field | Content |
|---|---|
| `time`, `game_mode`, `mode` | The game step, `turnbased`, and the input mode (`menu`, `picture`, `sleep`, `pause`, `dialog`, `move`, `push`, `recruit`, `inventory`, `name`). |
| `dialog` | `{id, buttons}` while `mode` is `dialog`, else `null`. |
| `party` | `map`, `level`, `x`, `y`, `facing`, `darkness`, `can_move`, `sleeping`, `dead`. |
| `leader`, `leader_hand` | The index of the leader and the object in its hand. |
| `candidate` | The champion that a mirror shows, with the fields of a champion, else `null`. A candidate is not in `champions`. |
| `champions` | The members: `index`, `name`, `title`, `alive`, `health`, `stamina`, `mana` (each `[value, maximum]`), `food`, `water`, `load_kg`, `position`, `poisoned`, `statistics`, `skills`, `wounds`, `slots`, `hands`, `runes`, `busy`. |
| `action_menu`, `spell`, `inventory`, `chest`, `money_box`, `minion_map` | The open panel, else `null`. |
| `messages` | The text of the message rows. |
| `outdoor`, `weather`, `day_time` | The sky, only outdoors. |

Slot names (`slots[].where`): `ready_hand` (0), `action_hand` (1), `head`, `torso`, `legs`, `feet`, `ready_quiver_1`, `ready_quiver_2`, `action_quiver_1` to `action_quiver_4`, `neck`, and `backpack_1` to `backpack_17`. The chest slots are 30 to 37. The slot numbers are in `slots[].slot`; `put_in_slot`, `take_from_slot`, and `look` use them.

`get_view` gives the squares that the party sees now (the seen rule), with `facing`, `darkness`, `squares` (each with `depth`, `side`, `x`, `y`, `element`, and, where the view draws them, `creatures`, `objects`, `flying`, `door`, `button`, `floor_ornament`, `front_face`, `side_face`), `piles` (the objects of cells 0 to 3, top first), `alcove` (cell 4), and `wall_ahead_clickable`. It shows no creature health and nothing behind a wall.

`get_map` gives the explored squares of the party map: `map`, `level`, `width`, `height`, `party`, `facing`, `legend`, `rows`, and `grid` (the rows with an `x mod 10` header and the `y` in front). The explored squares are the seen squares of the automap (`docs/advanced-mode.md`).

### Cells

A cell is a number from 0 to 4 in `take_object` and `drop_object`: cells 0 and 1 are on the party square, cells 2 and 3 on the square ahead, cell 4 is the alcove ahead. `get_view` `piles` uses the same numbers.

### Game over and the modal loops

- After a game over, the reply has `game_over` true. Use `load` or `new_game`.
- A save, a load, or a new game of the game itself (the buttons of the save menu) runs a modal loop. Do not click the save button of the game; use `save`. A raw click on it opens the save menu.
- While a dialog shows, `act click` and `act key` are the way out. `state.dialog` has the id and the button texts.

## Actions

`args` has the listed keys. A champion index is the `index` of `champions[]`; the champion must be alive. While a candidate shows, only `recruit`, `click`, and `key` run (`a candidate is shown: answer with recruit first`). While the party sleeps, only `wake`, `click`, and `key` run (`the party sleeps: use wake or wait`). An unknown name gives `unknown action`; a missing name gives `missing action`.

| Action | Args | What it does | Errors |
|---|---|---|---|
| `move_forward`, `move_back`, `move_left`, `move_right` | none | One step. | `close the inventory first (close_inventory)` |
| `turn_left`, `turn_right` | none | A turn on the spot. | same |
| `click_wall_ahead` | none | Clicks the switch, lock, mirror, door button, or alcove ahead; else touches the plain wall. | `the view takes no click now`, `nothing to click on the wall ahead`, `the leader hand is not empty: nothing to use it on ahead` |
| `take_object` | `cell` 0-3 | The leader takes the top object of the cell. | `cell must be 0-3 (see get_view piles)`, `the leader hand is not empty: drop or store the object first`, `no leader`, `no object at this cell` |
| `drop_object` | `cell` 0-4 | The leader drops the hand object. | `cell must be 0-4`, `the leader hand is empty`, `no floor ahead`, `no floor to drop on ahead`, `no alcove ahead` |
| `open_inventory` | `champion` | Opens the inventory of the champion. | `no living champion with this index`, `the inventory cannot open now` |
| `close_inventory` | none | Closes the inventory. | `the inventory cannot close now` |
| `put_in_slot` | `champion`, `slot` 0-37 | Clicks the slot with an object in the leader hand. | `the leader hand is empty`, `slot must be 0-37`, `open the inventory of this champion first (open_inventory)`, `no open chest`, `this slot cannot be clicked now` |
| `take_from_slot` | `champion`, `slot` 0-37 | Clicks the slot with an empty leader hand. | `the leader hand is not empty`, and as `put_in_slot` |
| `eat`, `drink` | none | The champion of the open inventory takes the hand object in its mouth. | `open the inventory of the champion who eats or drinks first`, `take the food or drink in the leader hand first` |
| `choose_action` | `champion`, `hand` (`ready` or `action`), `index` | Opens the action menu of the hand and runs the action `index`. A money box, a chest, or a minion map opens its panel instead. | `hand must be ready or action`, `this champion cannot act now`, `index out of range of the action menu (see state champions hands)`, `another champion's action menu is open: use pass first`, `close the open panel first (pass)` |
| `pass` | none | Closes the open action menu or panel. | `the open panel has no close button` |
| `set_caster` | `champion` | Chooses the caster of the spell panel. | `no living champion with this index`, `close the open panel first (pass)`, `this champion cannot cast now`, `the spell icons take no click now` |
| `set_leader` | `champion` | Chooses the leader. | `no living champion with this index`, `close the open panel first (pass)`, `the spell icons take no click now` |
| `add_rune` | `symbol` 0-5 | Adds a rune to the spell. | `symbol must be 0-5`, `no spell panel (set_caster)`, `the spell has four runes`, `not enough mana for this rune` |
| `delete_rune` | none | Deletes the last rune. | `no spell panel (set_caster)`, `no rune` |
| `cast` | none | Casts the spell. | same |
| `recruit` | `choice` (`join` or `cancel`) | The candidate joins or goes back to its mirror. | `no candidate: click a champion mirror first (click_wall_ahead)`, `choice must be join or cancel` |
| `sleep` | none | Opens the inventory of the leader through its status box, then sleeps. | `no leader`, `the inventory cannot open now` |
| `wake` | none | Wakes the party. | `the party cannot wake now` |
| `swap_champions` | `a`, `b` | The two champions swap cells. | `a and b must be living champions`, `a and b must be different champions` |
| `look` | optional `champion`, `slot` | Holds the eye on the leader hand object. With `slot`, the hand takes the slot object first and puts it back after. The reply has `look`. | `open an inventory first (open_inventory): the eye is in the inventory`, `the leader hand is empty: take an object, or give champion and slot`, `the leader hand must be empty to look at a slot`, `no object in this slot` |
| `take_coin` | `slot` 0-5 | One coin of the open money box goes to the leader hand. | `open a money box first (choose_action on the hand that holds it)`, `slot must be 0-5`, `no coin in this slot` |
| `minion_map_option` | `option` 0-3 | Toggles an option of the open minion map. | `open a minion map first`, `this minion map has no options`, `option must be 0-3` |
| `click` | `x` 0-319, `y` 0-199, optional `button` (`left` or `right`) | A raw click at a screen point. | `x must be 0-319 and y 0-199`, `button must be left or right` |
| `key` | `key` (a name of `docs/llm-control.md` or a code) | A raw key. | `unknown key` |

`click` and `key` also run during a candidate, a sleep, and a dialog.

## Play from the shell: dm2-play

`dm2-play` sends one request to the socket and prints the reply as compact JSON on one line. The exit code is 1 when the reply has `"ok": false`.

```sh
uv run dm2-play get_state
uv run dm2-play act move_forward
uv run dm2-play act take_object '{"cell": 2}'
uv run dm2-play act swap_champions '{"a": 0, "b": 1}'
uv run dm2-play act look '{"champion": 0, "slot": 1}'
uv run dm2-play act_many '["turn_left", "move_forward", {"action": "take_object", "args": {"cell": 2}}]'
uv run dm2-play wait '{"ticks": 20}'
uv run dm2-play save
uv run dm2-play load
uv run dm2-play new_game
```

- `--brief` (before the command) prints a short text summary instead of the JSON: the turn (`ticks`, `ran`, `stopped`, `error`, `game_over`, `action_damage`, `health_lost`, `path`, and `BUMPED`), the `look:` line, the `seen:` line, the party position, the leader hand, the candidate, each champion, the open panels, the messages, and the squares ahead (side words: `left` and `right`, `far left` and `far right` at 2 squares from the lane, `outer left` and `outer right` at 3 squares). For `get_map`, it prints the `grid`.
- A relative `path` becomes an absolute path, because the game runs in another directory.
- Call log: `dm2-play` appends one tab-separated line per call (time, action or command, `ok` or `fail`, map, x, y, game time) to `calls.log` in the folder `$DM2_PLAY_LOG_DIR`. The default folder is `playthroughs/current/coordinator`. The log is written only when the folder exists. `DM2_PLAY_LOG_DIR=none` turns it off. Run `dm2-play` in the repository root, else the relative folder does not exist.

## Runs and checkpoints

The goal of the runs is to measure how much an LLM player gets better: from session to session, and against earlier runs. To finish the game is the long-term goal, not the measure of one run. A person (the coordinator) starts the sessions and keeps the records.

The player manual is `docs/llm-playbook.md`. The agents are `.claude/agents/dm2-coordinator.md` and `.claude/agents/dm2-player.md`. The template of the player files is `playthroughs/template/`. The folder layout below is the convention that the call log and the commands expect.

```text
playthroughs/
  current -> run<N>             a link to the folder of the current run
  run<N>/
    player/                     the memory of the player (the player sees only this)
    coordinator/                the coordinator files; calls.log of dm2-play
    game/                       the live save folder: QUICK.DAT (--saves)
    saves/<name>/               a checkpoint: a copy of QUICK.DAT and of player/
```

- Allowed changes of the player instructions between sessions and runs: general rules about how to play and about the game, for example survival, fights, food and water, training, saves, the use of the commands, and how to keep the memory. Change them in small steps and write each change, its reason, and the session where it starts.
- Not allowed: hints for this dungeon, for example a position, a map, the solution of a puzzle, where an item lies, or the way to the next level. The memory of an earlier run (maps, lessons) is the player's own knowledge, not a hint.
- A new run: create `playthroughs/run<N>/` with `player/`, `coordinator/`, `game/`, and `saves/`. Point the link: `ln -sfn run<N> playthroughs/current`. Start the game with `--saves playthroughs/current/game`. Send `new_game`, then `save`. The player recruits the champions.
- A checkpoint after a session: copy `game/QUICK.DAT` and the files of `player/` to `saves/<name>/`.
- A resume: copy `QUICK.DAT` of the checkpoint to `game/` and its files to `player/`, then send `load`.
- The calls of the coordinator stay out of the log: `DM2_PLAY_LOG_DIR=none uv run dm2-play ...`.
- The coordinator runs as a background subagent of Claude Code with a clean context. It starts one player subagent per session with the goal and a call limit, makes the checkpoints, and keeps the records.

## C API

`transport.h` (the socket, plain POSIX) and `control.h` (the line protocol), both in `build/libdm2.dylib`:

| Function | Purpose |
|---|---|
| `transport_open(path)` | Listens on the socket. Replaces an old socket file. Returns `true` on success. |
| `transport_poll()` | One call per frame: accepts a client, answers requests, and ends turns. |
| `control_frame()` | One game step of the pending turn. |
| `control_is_turn_pending()` | `true` while a turn runs. |
| `transport_close()` | Closes the sockets and removes the socket file. |

`turn.h`: `turn_is_pending`, `turn_act`, `turn_act_many`, `turn_wait`, `turn_poll`, `turn_reply`, and `turn_drop_reply`. The action table is in `turn.c`, one function per action.

`state_json.h` (no SDL): `state_json_state`, `state_json_view`, and `state_json_map` append one JSON object to a `Json` buffer. `state_json_string(part)` returns a new string; free it with `free`.

## Integration in main.c

In the turn-based mode, `main.c` does these steps:

1. It opens the control socket before the main loop (the default path if `--control` is missing) and stops with an error when this fails.
2. It starts a new game, or loads `--save`, without the start menu and without a movie.
3. The window events keep only the quit event. Keys and clicks do not reach the input queue.
4. Each frame calls `transport_poll()` and `control_frame()`: they answer requests and run one game step while a turn is pending. The window shows each frame.
5. In a modal loop (menu, dialog), the frame hook polls the control socket and does not wait for a timer tick while a turn runs.
6. The program never ends the session by itself; `load` and `new_game` start the next game.

## Tests

- `tests/port/test_state_json.py`: the JSON of the state, the view, and the map.
- `tests/port/test_turnbased.py`: the protocol, the turn end rules, the stops, `act_many`, `save`, `load`, and `new_game`.
- `tests/tools/test_play_client.py`: `dm2-play` (the act forms, `--brief`, the call log).

```sh
uv run pytest -q -n auto tests/port/test_state_json.py tests/port/test_turnbased.py tests/tools/test_play_client.py
```

The manual checks are in section T of [manual-test-checklist.md](manual-test-checklist.md).
