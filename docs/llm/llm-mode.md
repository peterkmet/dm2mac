# LLM mode: play over the control socket

`build/dm2 --control PATH` opens a Unix domain socket. A program or an LLM (for example Claude Code through Bash) sends requests over it and reads the replies: the game state, the view, and screenshots. The window shows the game, so a person can watch.

The game has three modes: `vanilla`, `advanced`, and `turnbased` ([Modes](../manual.md#modes)).

- In the `vanilla` and `advanced` modes the game runs in real time, and the requests do not stop the game time. A client sends raw input with the [Commands](#commands). Use them for manual-style UI tests.
- In the `turnbased` mode the game time stands still between requests. A request is one turn: the game runs until the party can act again, then the reply gives the state and the view as JSON. See [The turn-based mode](#the-turn-based-mode).
- `state`, `screenshot`, and `quit` work in all modes.

## Start the game

### Real-time modes

1. Build the port.

   ```sh
   make
   ```

2. Start the game with the socket. The dummy drivers run it without a window and without sound.

   ```sh
   SDL_AUDIO_DRIVER=dummy SDL_VIDEO_DRIVER=dummy build/dm2 --control ~/.dm2/control.sock --save tests/saves/start.DAT &
   ```

   - `--control PATH`: the socket file. The game replaces an old socket at `PATH`. When `PATH` is another file, the game does not start and the file stays. The game makes the last folder level of `PATH` only, not the folders above it.
   - The other options of a normal start (`--save`, `--data`, `--mode`, `--filter`) and the start movies are in [manual.md](../manual.md#options).
   - Without the two `SDL_*_DRIVER` variables, the window and the sound stay on.

3. Send requests with [dm2-play](#play-from-the-shell-dm2-play).

### Turn-based mode

1. Build the port.

   ```sh
   make
   ```

2. Start the game in the turn-based mode. A run plays without sound.

   ```sh
   SDL_AUDIO_DRIVER=dummy build/dm2 --mode turnbased --saves playthroughs/current/game
   ```

   - `--saves DIR`: the folder of the save file `QUICK.DAT`. The game does not make the folder; `save` fails when it is missing. Without it, the file is `game/DATA/QUICK.DAT` (the `--data` folder).
   - `--control PATH`: the socket file, as in the real-time modes. Default: `~/.dm2/control.sock`.
   - `--save FILE`: optional. The game loads this save slot file at the start, as in the other modes.
   - Without `--save`, the game starts a new game immediately: no start menu and no movie. The new game has one champion (Torham). The player recruits the other champions at the mirrors.
   - The keyboard and the mouse do not control the game. A closed window quits. The request `quit` quits and the exit code is 0.
   - `--mode turnbased` and the mode `advanced` use the same `QUICK.DAT` format. The advanced mode loads a file of the turn-based mode with F9 when it runs with the same `--saves DIR`.

3. Send requests with [dm2-play](#play-from-the-shell-dm2-play).

## Play from the shell: dm2-play

`dm2-play` sends one request to the socket and prints the reply as compact JSON on one line.

```text
uv run dm2-play [--socket PATH] [--brief] <cmd> ['<json-fields>']
uv run dm2-play [--socket PATH] [--brief] act <action> ['<json-args>']
uv run dm2-play [--socket PATH] [--brief] act_many '<json-list>'
```

- `<cmd>`: the request command, for example `state`.
- `<json-fields>`: the other fields of the request as one JSON object. For `act`: the action, then its `args` as one JSON object. For `act_many`: the `actions` as one JSON list.
- `--socket PATH`: the socket path. Without it, `dm2-play` uses `$DM2_CONTROL`, otherwise `~/.dm2/control.sock`.
- `--brief` (before the command) prints a short text summary instead of the JSON: the turn (`ticks`, `ran`, `stopped`, `error`, `game_over`, `action_damage`, `health_lost`, `path`, and `BUMPED`), the `look:` line, the `seen:` line, the party position, the leader hand, the candidate, each champion, the open panels, the messages, and the squares ahead (side words: `left` and `right`, `far left` and `far right` at 2 squares from the lane, `outer left` and `outer right` at 3 squares). For `get_map`, it prints the `grid`.
- The exit code is 1 when the reply has `"ok": false`, even when the game is not reachable.
- A relative screenshot `path` becomes an absolute path before it goes to the game, because the game runs in another directory.
- Call log: `dm2-play` appends one tab-separated line per call (time, action or command, `ok` or `fail`, map, x, y, game time) to `calls.log` in the folder `$DM2_PLAY_LOG_DIR`. The default folder is `playthroughs/current/coordinator`. The log is written only when the folder exists. `DM2_PLAY_LOG_DIR=none` turns it off. Run `dm2-play` in the repository root, otherwise the relative folder does not exist.

Python code can use `dm2tools.control_client.request(payload, path=None, timeout=150.0)`.

Examples of the real-time modes:

```sh
uv run dm2-play state
uv run dm2-play key '{"key": "up"}'
uv run dm2-play wait '{"ticks": 60}'
uv run dm2-play screenshot '{"path": "shot.png"}'
uv run dm2-play quit
```

Examples of the turn-based mode:

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

## Protocol

- A client sends one JSON object per line and gets one JSON object per line.
- Each request has a `cmd` field.
- A successful reply has `"ok": true`. A failed reply is `{"ok": false, "error": "<reason>"}`.
- One client at a time. A second client gets `{"ok":false,"error":"busy"}` and the game closes its connection, unless the first client has already closed its connection and has no unread byte. A client that connects again right after a reply does not get `busy`.
- While a turn or a wait runs, the game reads no next request.
- A request line can have up to 4095 bytes before its newline. A longer line gets one `request line too long` error. A line with a NUL byte gets a `request line has a NUL byte` error. Replies have no size limit.
- String values can use the JSON escapes, even `\uXXXX`. The game decodes them to UTF-8. Replies send UTF-8 as it is. In the turn-based commands, a string value cannot contain a `"` character.
- The parser is flat: it finds `"field": value` anywhere in the line. The keys in `args` of the turn-based mode are found the same way. Do not nest other objects.
- Positions are game screen pixels: `x` 0 to 319, `y` 0 to 199.

## Commands

The commands of the real-time modes. Each request runs on the main thread between two timer ticks: in the game loop and in the modal loops (start menu, dialogs).

### state

Reads the game state.

```json
{"cmd": "state"}
```

```json
{"ok":true,"time":33,"ticks":61,"mode":"move","dialog":null,"party":{"map":0,"x":1,"y":8,"dir":0},"champion_count":1,"leader":0,"champions":[{"name":"TORHAM","health":[83,83],"stamina":[770,770],"mana":[14,14],"load":67}],"inventory":-1,"candidate":-1,"hand":null,"sound_volume":7,"music_volume":7,"end":0,"quit":0,"game_mode":"advanced","automap":0,"cheat_immortal":0,"cheat_food":0,"cheat_light":0,"cheat_kill":0,"cheat_walls":0,"cheat_map":0}
```

| Field | Meaning |
| --- | --- |
| `time` | Game time in game steps (`g_game.game_time`). It stands still during a pause and in dialogs. |
| `ticks` | Timer ticks at 60 Hz since the program start (`g_main_loop.timer_ticks`). `wait` counts these. |
| `mode` | Input mode: `menu`, `picture`, `sleep`, `pause`, `dialog`, `move`, `push`, `recruit`, `inventory`, `name`; `movie` while the splash picture or a movie plays (see [Movies](#movies)). |
| `dialog` | The dialog id (`DialogState.id`, `src/ui/include/dialog.h`) while `mode` is `dialog`, otherwise `null`. |
| `party` | Map, x, y, and direction (0 north, 1 east, 2 south, 3 west). |
| `champion_count`, `leader` | Champions in the party; the leader index, -1 for none. |
| `champions` | Per champion: name, `[current, maximum]` of health, stamina, and mana (stored values; the screen shows stamina in tenths), and the load in 1/10 kg. |
| `inventory`, `candidate` | The champion of the open inventory and the champion that is being recruited, -1 for none. |
| `hand` | The leader hand object: `{"thing": n, "name": "..."}`, or `null`. |
| `sound_volume`, `music_volume` | Volume settings 0 to 7. |
| `end`, `quit` | The end flag of the game (party dead or quit) and the program end flag. |
| `game_mode` | The game mode: `vanilla`, `advanced`, or `turnbased` ([Modes](../manual.md#modes), [The turn-based mode](#the-turn-based-mode)). |
| `automap` | 1 while the automap is open, otherwise 0. |
| `cheat_immortal`, `cheat_food`, `cheat_light`, `cheat_kill`, `cheat_walls`, `cheat_map` | 1 while the cheat is on, otherwise 0 ([Cheats](../manual.md#cheats)). |

The message area text is not in the state. Use a screenshot to read it.

### key

Presses a key. `key` is a name or a key code of the original keyboard layer (PC scan code with the shift `0x200`, control `0x400`, and alt `0x800` bits).

```json
{"cmd": "key", "key": "up"}
```

```json
{"ok":true}
```

Key names:

- One character of the US layout: `a` to `z`, `0` to `9`, punctuation. An uppercase letter or a shifted character gives the shift bit.
- `esc`, `enter`, `space`, `backspace`, `tab`, `f1` to `f10`.
- `up`, `down`, `left`, `right`, `shift+left`, `shift+right`: the codes that the window gives for the arrow keys (forward, back, turn left, turn right, step left, step right).
- Keypad: `kp0` to `kp9`, `kp-`, `kp+`, `kp.`.
- Prefixes `shift+`, `ctrl+`, `alt+` add the modifier bit, for example `alt+a`.

Example of a key code: `{"cmd": "key", "key": 28}` presses Enter.

### click

Clicks a mouse button at a screen position: the button goes down and up. The pointer moves there. `button` is `left` (default) or `right`.

```json
{"cmd": "click", "x": 260, "y": 150, "button": "left"}
```

```json
{"ok":true}
```

### press and release

`press` holds a mouse button down at a screen position until a `release`. The pointer moves there. `button` is `left` (default) or `right`. Use them for the eye and the mouth of the inventory: the game shows the eye panel only while the left button is down, and the release ends it.

```json
{"cmd": "press", "x": 20, "y": 64}
```

```json
{"cmd": "release", "x": 20, "y": 64}
```

Both reply `{"ok":true}`.

### command

Clicks the zone of a command of the current input mode, as `input_simulate_command_click` does. Use it for a button whose position you do not know. `id` is the command number. The example clicks the first button of an open dialog (`CMD_DIALOG_BUTTON_FIRST`, `0xdb`).

```json
{"cmd": "command", "id": 219}
```

```json
{"ok":true}
```

### move

Moves the pointer without a click. The next screenshot shows the pointer there. A mouse move in the window also moves the pointer.

```json
{"cmd": "move", "x": 100, "y": 100}
```

```json
{"ok":true}
```

### wait

Lets `ticks` timer ticks (60 Hz, 1 to 6000) run, then replies. The game loop and the modal loops (menu, dialogs) both count ticks, so a wait also ends while a dialog is open. The reply gives the ticks that ran. When the client closes the connection during a wait and the game has no unread line of it, the game drops the wait and accepts the next client. A client that sends more lines and then closes during a wait keeps the socket busy until the wait ends.

```json
{"cmd": "wait", "ticks": 60}
```

```json
{"ok":true,"ticks":60}
```

A game step takes 8 ticks. A step forward needs about 40 ticks before the party stands on the next square.

### screenshot

Writes a PNG file of 320 × 200 pixels: the screen with the palette and the cursor at the pointer. The cursor is the original cursor image that the window shows there: the arrow, the hand, the held object with its shadow, or the dragged champion. The pointer is the window pointer until a click or move request moves it. In `build/dm2` the pointer starts at (0, 0), so the first screenshot has the cursor in the top left corner. The reply gives the absolute path.

```json
{"cmd": "screenshot", "path": "/tmp/shot.png"}
```

```json
{"ok":true,"path":"/private/tmp/shot.png"}
```

### quit

Ends the program the same way as a closed window. The exit code is 0 in the game. The game removes the socket file.

```json
{"cmd": "quit"}
```

```json
{"ok":true}
```

## The turn-based mode

### Turn-based commands

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
| `state` | none | The control state of the real-time modes ([state](#state)). |
| `screenshot` | `path` | `path`. |
| `quit` | none | `{"ok":true}` |

- A failed request changes nothing in the game, except a failed `load` (see [Deviations from the original](#deviations-from-the-original)).
- `key`, `click`, `press`, `release`, `command`, and `move` are refused: `use act in turnbased mode`. Use `act click` and `act key`.
- An unknown `cmd` gives `unknown cmd`. A request without `cmd` gives `missing cmd`.
- `act`, `act_many`, `wait`, and `save` give `game over` after a game over. The other commands still work.
- `save` gives `answer the candidate first (act recruit)` while a champion candidate shows, and `cannot write the save file` when the write fails. `save` is allowed with no champion.
- `load` gives `no saved game (use save first)` when `QUICK.DAT` does not exist, `the saved game is damaged` when it does not load, and `cannot write QUICK.TMP: nothing loaded` when the game cannot keep a copy of the running game. The game goes on unchanged. When the copy also does not load, the game is lost (see below).
- `load` and `new_game` also work after a game over. Each ends the old game without a question. After `load`, the game time is the time of the save plus 1 (see [Deviations from the original](#deviations-from-the-original)).
- `new_game` gives `cannot start the new game` when `DUNGEON.DAT` does not load. The game is lost then.
- A lost game (a failed restore or a failed new game): every command except `load`, `new_game`, and `quit` gives `no game is running: use load or new_game`.
- `save`, `load`, and `new_game` give `close the dialog first (act click or act key)` while a modal loop of the game runs (a menu, a picture, a dialog, or the name editor). Close it with `act click` or `act key`.

### Turns

An `act` turn does these steps:

1. It gives the inputs of the action to the input queue, one after the other (at most 4). This is the path of a player: a key, a click on a command zone, or a click at a screen point. The next input goes when the input pipeline is idle.
2. It runs the game one game step per frame (`g_game.game_time` grows by 1). One tick of the turn protocol is one game step.
3. It ends when at least `TURN_MIN_TICKS` (2) steps ran, all inputs are given, the input is idle, and the party can move again (no move cooldown, no walk in progress, no wait of the input). It also ends after `TURN_MAX_TICKS` (64) steps, and immediately when the game is over.

A game that does not tick (a dialog) ends a turn after `FRAMES_PER_TICK_LIMIT` (16) frames for each step of the maximum of the turn (`TURN_MAX_TICKS`, or the `ticks` of `wait`). The game rules do not change.

A `wait` turn runs `ticks` game steps (`TURN_WAIT_MAX_TICKS` is 5000). It ends early when news arrives. The real-time [wait](#wait) keeps its own limit of 6000 timer ticks.

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

`bumped` is `true` after a step that left the party on its square: a wall, a closed door, or a creature is in the way. A teleporter that sends the party back to its square counts as a bump (see [Deviations from the original](#deviations-from-the-original)).

### Batches

`act_many` takes `actions`, a list. An item is an action name (`"turn_left"`) or an object (`{"action":"take_object","args":{"cell":2}}`). Only spaces and commas separate the items. A string holds no `"` character.

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
| `dialog` | `{id, buttons}` while `mode` is `dialog`, otherwise `null`. |
| `party` | `map`, `level`, `x`, `y`, `facing`, `darkness`, `can_move`, `sleeping`, `dead`. |
| `leader`, `leader_hand` | The index of the leader and the object in its hand. |
| `candidate` | The champion that a mirror shows, with the fields of a champion, otherwise `null`. A candidate is not in `champions`. |
| `champions` | The members: `index`, `name`, `title`, `alive`, `health`, `stamina`, `mana` (each `[value, maximum]`), `food`, `water`, `load_kg`, `position`, `poisoned`, `statistics`, `skills`, `wounds`, `slots`, `hands`, `runes`, `busy`. |
| `action_menu`, `spell`, `inventory`, `chest`, `money_box`, `minion_map` | The open panel, otherwise `null`. |
| `messages` | The text of the message rows. |
| `outdoor`, `weather`, `day_time` | The sky, only outdoors. |

The slot names (`slots[].where`) are in slot number order in [Protocol vocabulary](#protocol-vocabulary). The chest slots are 30 to 37. The slot numbers are in `slots[].slot`; `put_in_slot`, `take_from_slot`, and `look` use them.

`get_view` gives the squares that the party sees now (the seen rule), with `facing`, `darkness`, `squares` (each with `depth`, `side`, `x`, `y`, `element`, and, where the view draws them, `creatures`, `objects`, `flying`, `door`, `button`, `floor_ornament`, `front_face`, `side_face`), `piles` (the objects of cells 0 to 3, top first), `alcove` (cell 4), and `wall_ahead_clickable`. It shows no creature health and nothing behind a wall.

`get_map` gives the explored squares of the party map: `map`, `level`, `width`, `height`, `party`, `facing`, `legend`, `rows`, and `grid` (the rows with an `x mod 10` header and the `y` in front). The explored squares are the seen squares of the automap ([Seen squares](../manual.md#seen-squares)).

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
| `click_wall_ahead` | none | Clicks the switch, lock, mirror, door button, or alcove ahead; otherwise touches the plain wall. | `the view takes no click now`, `nothing to click on the wall ahead`, `the leader hand is not empty: nothing to use it on ahead` |
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
| `look` | optional `slot`, `champion` (default the open inventory) | Holds the eye on the leader hand object. With `slot`, the hand takes the slot object first and puts it back after. The reply has `look`. | `open an inventory first (open_inventory): the eye is in the inventory`, `the leader hand is empty: take an object, or give champion and slot`, `the leader hand must be empty to look at a slot`, `no object in this slot` |
| `take_coin` | `slot` 0-5 | One coin of the open money box goes to the leader hand. | `open a money box first (choose_action on the hand that holds it)`, `slot must be 0-5`, `no coin in this slot` |
| `minion_map_option` | `option` 0-3 | Toggles an option of the open minion map. | `open a minion map first`, `this minion map has no options`, `option must be 0-3` |
| `click` | `x` 0-319, `y` 0-199, optional `button` (`left` or `right`) | A raw click at a screen point. | `x must be 0-319 and y 0-199`, `button must be left or right` |
| `key` | `key` (a name of [key](#key) or a code) | A raw key. | `unknown key` |

`click` and `key` also run during a candidate, a sleep, and a dialog.

## Movies

The program plays the start movies and the end movies of [Start screens](../manual.md#start-screens) ([movies.md](../formats/movies.md)). While one of them plays:

- `state` gives `"mode": "movie"` and `"dialog": null`.
- `click` and `press` skip as a click does ([movies.md](../formats/movies.md#deviations-from-the-original)). `release`, `key`, and `command` reply `{"ok":true}` and do nothing.
- `wait` counts game timer ticks. They stand still during a movie, so the reply comes after the movies.
- `screenshot` saves the game screen (320 × 200), not the movie.
- `quit` stops the movies and ends the program.

To reach the start menu fast: `click` until `state` gives another mode. In the `vanilla` mode, the splash and `FTL` take about 8.5 seconds.

## Errors

The errors of the real-time commands:

| Error | Cause |
| --- | --- |
| `missing cmd` | The line has no `cmd` field. |
| `unknown cmd` | `cmd` is not one of the commands above. |
| `unknown key` | The key name is not known, or the code is not 1 to 65535. |
| `x must be 0-319 and y 0-199` | A position is missing or off the screen. |
| `button must be left or right` | Another button name. |
| `missing id` | `command` without an `id`. |
| `ticks must be 1-6000` | A wait out of range. |
| `missing path` | `screenshot` without a `path`, or a `path` with a bad escape, a NUL character, or more than `PATH_MAX` bytes. |
| `cannot write the PNG file` | The game cannot write the file. |
| `request line too long` | A line of more than 4095 bytes before its newline. |
| `request line has a NUL byte` | The line has a byte 0. |
| `busy` | Another client is connected. |

## Known limits

- The socket path must be shorter than about 100 bytes.
- The game takes one client at a time ([Protocol](#protocol)).
- The game sends a reply with a blocking write. A client that sends requests but never reads the replies can stop the game. The socket is a local test tool.
- The Windows build (`make windows`, `build/dm2.exe`) has no control socket (`src/platform/transport_none.c`): `--mode turnbased` and `--control` stop with the message `the turn-based mode and --control need macOS` and exit code 1.

## Protocol vocabulary

Each line lists the names of one table of the protocol, in table order.

- Input modes (`mode`): `menu`, `picture`, `sleep`, `pause`, `dialog`, `move`, `push`, `recruit`, `inventory`, `name`.
- Directions (`facing`): `north`, `east`, `south`, `west`.
- Party cells (`position` of a champion): `front_left`, `front_right`, `back_right`, `back_left`.
- Statistics (`statistics`): `strength`, `dexterity`, `wisdom`, `vitality`, `antimagic`, `antifire`.
- Base skills (`skills`): `fighter`, `ninja`, `priest`, `wizard`.
- Wounds (`wounds`): `ready_hand`, `action_hand`, `head`, `torso`, `legs`, `feet`.
- Slots (`slots[].where`): `ready_hand`, `action_hand`, `head`, `torso`, `legs`, `feet`, `ready_quiver_2`, `action_quiver_2`, `action_quiver_3`, `action_quiver_4`, `neck`, `ready_quiver_1`, `action_quiver_1`, `backpack_1`, `backpack_2`, `backpack_3`, `backpack_4`, `backpack_5`, `backpack_6`, `backpack_7`, `backpack_8`, `backpack_9`, `backpack_10`, `backpack_11`, `backpack_12`, `backpack_13`, `backpack_14`, `backpack_15`, `backpack_16`, `backpack_17`.
- Hands (`hands`, and `hand` of `choose_action`): `ready`, `action`.
- Minion map options (`option` of `minion_map_option`): `creatures`, `details`, `items`, `hold`.
- Sky amounts (`weather`): `none`, `light`, `medium`, `heavy`.
- View sides (`side` in `seen`): `left_3`, `left_2`, `left`, `center`, `right`, `right_2`, `right_3`.
- Explosions (in `get_view`): `fireball`, `slime`, `lightning`, `harm_non_material`, `door_toggle`, `poison_bolt`, `poison_cloud`, `push`, `pull`, `door_set`, `missile_wall`, `death_cloud`, `storm_lightning`, `storm_blast`, `smoke`.
- Runes (`runes`): the power runes `lo`, `um`, `on`, `ee`, `pal`, `mon`, then the three rune sets `ya`, `vi`, `oh`, `ful`, `des`, `zo`, `ven`, `ew`, `kath`, `ir`, `bro`, `gor`, `ku`, `ros`, `dain`, `neta`, `ra`, `sar`.
- Actions (`act`): `move_forward`, `move_back`, `move_left`, `move_right`, `turn_left`, `turn_right`, `click_wall_ahead`, `take_object`, `drop_object`, `open_inventory`, `close_inventory`, `put_in_slot`, `take_from_slot`, `eat`, `drink`, `choose_action`, `pass`, `set_caster`, `add_rune`, `delete_rune`, `cast`, `recruit`, `set_leader`, `sleep`, `wake`, `swap_champions`, `look`, `take_coin`, `minion_map_option`, `click`, `key` (see [Actions](#actions)).

## Deviations from the original

- The names of the inventory zones `ZONE_EYE` (`0x221`) and `ZONE_MOUTH` (`0x222`) in `data/ids.h` are swapped against the input table: the input table gives zone `0x221` to the mouth command `0x46` and zone `0x222` to the eye command `0x47` ([champions.md](../rules/champions.md#deviations-from-the-original)). The port keeps the names. The `look` action presses the zone of the eye command in the input table (`input_command_rect`).
- A turn gives a command as a click on the top left pixel of its first zone in the current input mode (`input_simulate_command_click`). The zone of `CMD_LEADER_INVENTORY` (`0x52`) is the whole screen, and a click there hits the status box of champion 0 first. The `sleep` action therefore opens the inventory of the leader with its status box command (`0x07` + leader).
- The port keeps the text of each message row next to its pixels (`text_message_row`). The original has only the pixels and the expiry time of each row. The text moves up and clears with the row.
- `ui_object_info_lines` gives the eye panel of an object as text lines. The panel shows some parts only as pictures; the text uses these English words for them: `charges` (charges gauge), `strength` (weapon strength gauge), `throw` (weapon throw power gauge), `armour` (armour class gauge), `power` (potion power gauge), and `total` (the total value of a money box). A gauge line is `<word> <value>/<maximum>`.
- In these lines, the items of a chest and the coin counts of a money box (`<coin name> <count>`) go after each other, separated by a comma, as many to a line as fit. The panel shows them as icons and numbers in slots. A scroll gives its name and weight before its text; the panel shows only the text. More than 8 lines are cut off.
- The game state JSON (`state_json_state`) gives object, coin, and action names without the trailing spaces of their GDAT texts (`FINE ROBE ` gives `FINE ROBE`). Other characters stay as GDAT gives them: the rune glyph letters in a potion name (for example `` ` `` to `e` for the power runes) are not mapped.
- The skill ranks of the JSON are the texts of the skills panel. The panel shows the master ranks with a power rune glyph of the font (`` ` `` and `a` to `e`, characters `0x60` to `0x65`); the JSON gives the upper-case rune name for it: `LO`, `UM`, `ON`, `EE`, `PAL`, `MON` (`LO MASTER FIGHTER`).
- The slot dialogs (`0x80` load, `0x81` save) do not set the dialog id (`g_dialog.id`); the JSON gives the id and the button texts of the last dialog that `dialog_draw` drew.
- The JSON names the champion slots by their place in the inventory picture: `ready_hand`, `action_hand`, `head`, `torso`, `legs`, `feet`, `neck` (10); the slots that the ready hand loads from, `ready_quiver_1` (11) and `ready_quiver_2` (6); the slots that the action hand loads from, `action_quiver_1` (12) to `action_quiver_4` (7, 8, 9); the backpack, `backpack_1` (13, left of the second row) to `backpack_17` (14 to 21 in the first row, 22 to 29 in the second).
- The weather of the JSON uses the thresholds of the sky pictures:
  - `clouds` from the cloud cover: `light` from 64, `medium` from 128, `heavy` from 192;
  - `rain` from the sky darkness: `light` from 16, `medium` from 64, `heavy` from 128;
  - `wind`: the direction of `g_game.wind_dir`.
- The JSON gives only the weather that the player sees: rain or clouds are `none` when the sky set of the map has no rain or cloud picture, and `wind` is present only while falling rain shows over the view (its slant shows the wind).
- `day_time` comes from the darkness of the hour (`g_weather.hour_light`, 1 to 5): 1 is `day`, 5 is `night`, otherwise `dawn_dusk`.
- The view JSON (`state_json_view`) gives the squares of the seen rule (`seen_visible`), from the walk position while the party walks. It gives only what the view draws there (`view_draws`): wall faces as the wall face table of the view (`side_face`, `front_face`); creatures, objects, missiles, and explosions only on the view positions and square aspects that draw them (no things at depth 2 side 2; at depth 4 only creatures, only on a floor, pit, teleporter, or door seen from the front, nothing at side 3; nothing on a door seen from the front beside the party; no creature on the party square while the party walks); a floor ornament from depth 0 to 3 (the party row too, which the view draws), not ornament 0, and not at depth 3 side 2 when the ornament has the "not far side" key; a door button only to depth 3. The element, door state, and map glyph show at every depth, as the automap shows them.
- A wall face gives what its ornament (the one that `aspect_get` gives the face) holds, by the ornament kind and value keys: `mirror` (portrait kind), `alcove`, `shop`, `fountain` (the touch key). Otherwise `switch` (a click sensor with an ornament, or a hand switch text) and `lock` (the ornament index of a sensor or an item holder text that takes an object); otherwise `ornament` (the index). A text plate gives `text` on the front face and `null` on a side face or for a hidden text, as the view draws text only on a front plate. `shop` is the name of the item that the display shows, `true` before the display chose an item.
- Each creature group of a square is one entry with `count` 1: a DM2 group record holds one creature. The JSON has no creature names, wall ornament names, or floor ornament names (the game data has none); it gives the index.
- `piles` lists the object zones 0 to 3 of the last drawn view (cells seen from the party: 0 and 1 on the party square, 2 and 3 on the square ahead), `alcove` the alcove zone ahead as cell 4. While the inventory hides the view, `piles` is empty, `alcove` is `null`, and `wall_ahead_clickable` is false.
- The view JSON lists things that the view does not draw in these cases: the objects and missiles in the back cells of the party square (the view draws only its two front cells); the things in the far cells of a closed door, which the door hides. `button` shows even for a door seen from the side (`door_has_button`, as the minion map).
- The map JSON (`state_json_map`) marks a floor square with `_` when the minion map draws its floor ornament without the details option (`mmap_shows_floor_ornament`). It shows no floor sensor without an ornament and no wall feature.
- A turn tells a bump (`bumped`) from a displacement (`stopped` `moved` or `turned`) by the party square, map, and facing before and after the action. DM has a teleport counter for a teleporter that sends the party back to its square; DM2 has none, so such a teleporter counts as a bump.
- The seen record of a turn (`seen.creature_changes`) gives `before` 1 and `after` 0 for a creature group that the party saw and that the game killed: a DM2 group record holds one creature, and a group that left the view and whose record is free (its link is `THING_NONE`, as `creature_delete_group_at` leaves it) counts as killed.
- The turn-based `load` starts the loaded game with `play_start_session`, which runs one game step: the game time after a `load` is the time of the save plus 1. A damaged `QUICK.DAT` restores the running game from `QUICK.TMP` the same way, so the failed `load` also adds one game step. It is the one failed request that changes the game; every other failed request changes nothing.
