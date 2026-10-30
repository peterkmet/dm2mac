# Control socket

Start the game with the control socket, send requests with `dm2-play`, and use the protocol and the real-time commands. The turn-based commands, the actions, and the movies are in [Turn-based mode](turn-based.md). How an LLM run works: [LLM runs](llm-runs.md).

`build/dm2 --control PATH` opens a Unix domain socket. A program or an LLM (for example Claude Code through Bash) sends requests over it and reads the replies: the game state, the view, and screenshots. The window shows the game, so a person can watch.

The game has three modes: `vanilla`, `advanced`, and `turnbased` ([manual.md](../manual.md#modes)).

- In the `vanilla` and `advanced` modes the game runs in real time, and the requests do not stop the game time. A client sends raw input with the [commands](#commands). Use them for UI tests that a person would do by hand in the window.
- In the `turnbased` mode the game time does not advance between requests. A request is one turn: the game runs until the party can act again, then the reply gives the state and the view as JSON. See [Turn-based mode](turn-based.md).
- `state`, `screenshot`, and `quit` work in all modes.

## Start the game

Build the port with `make`, then start the game in one of the modes.

### Real-time modes

1. Start the game with the socket. The dummy drivers run it without a window and without sound.

   ```sh
   SDL_AUDIO_DRIVER=dummy SDL_VIDEO_DRIVER=dummy build/dm2 --control ~/.dm2/control.sock --save tests/saves/start.DAT &
   ```

   - `--control PATH`: the socket file. The game replaces an old socket at `PATH`. If a file that is not a socket exists at `PATH`, the game does not start and leaves the file in place. The game creates the folder that holds the socket if needed, but not the folders above it.
   - The other options of a normal start (`--save`, `--data`, `--mode`, `--filter`) and the start movies are in [manual.md](../manual.md#options).
   - Without the two `SDL_*_DRIVER` variables, the window and the sound stay on.

2. Send requests with [dm2-play](#play-from-the-shell-dm2-play).

### Turn-based mode

1. Start the game in turn-based mode. `SDL_AUDIO_DRIVER=dummy` turns the sound off.

   ```sh
   SDL_AUDIO_DRIVER=dummy build/dm2 --mode turnbased --saves playthroughs/current/game
   ```

   - `--saves DIR`: the folder that holds the save file `QUICK.DAT` ([manual.md](../manual.md#options)). `save` fails if the folder is missing.
   - `--control PATH`: the socket file. Without it, turn-based mode uses `~/.dm2/control.sock`.
   - `--save FILE`: optional. The game loads this save slot file on startup, as in the other modes.
   - Without `--save`, the game starts a new game immediately: no start menu and no movie. The new game has one champion (Torham). The player recruits the other champions at the mirrors.
   - The keyboard and the mouse do not control the game. Closing the window quits the game. The `quit` request quits with exit code 0.
   - The `turnbased` and `advanced` modes use the same `QUICK.DAT` format. In advanced mode, F9 loads a turn-based save if the game runs with the same `--saves DIR`.

2. Send requests with [dm2-play](#play-from-the-shell-dm2-play).

## Play from the shell: dm2-play

`dm2-play` sends one request to the socket and prints the reply as compact JSON on one line.

```text
uv run dm2-play [--socket PATH] [--brief] <cmd> ['<json-fields>']
uv run dm2-play [--socket PATH] [--brief] act <action> ['<json-args>']
uv run dm2-play [--socket PATH] [--brief] act_many '<json-list>'
```

- `<cmd>`: the request command, for example `state`.
- `<json-fields>`: the other request fields as one JSON object. For `act`: the action, then its `args` as one JSON object. For `act_many`: the `actions` as one JSON list.
- `--socket PATH`: the socket path. Without it, `dm2-play` uses `$DM2_CONTROL`, otherwise `~/.dm2/control.sock`.
- `--brief` (before the command) prints a text summary instead of the JSON: the turn (`ticks`, `ran`, `stopped`, `error`, `game_over`, `action_damage`, `health_lost`, `path`, `BUMPED`), the `look:` line, the `seen:` line, the party position, the leader's hand, the candidate, each champion, the open panels, the messages, and the squares ahead. Side words: `left` and `right` at 1 square from the lane, `far left` and `far right` at 2 squares, `outer left` and `outer right` at 3 squares. For `get_map`, it prints the `grid`.
- The exit code is 1 if the reply has `"ok": false` or the game is not reachable.
- A relative screenshot `path` becomes an absolute path before it goes to the game, because the game runs in a different directory.
- Call log: `dm2-play` appends one tab-separated line per call (time, action or command, `ok` or `fail`, map, x, y, game time) to `calls.log` in the folder `$DM2_PLAY_LOG_DIR`.
  - The default folder is `playthroughs/current/coordinator`, relative to the repository root: run `dm2-play` there.
  - `dm2-play` writes the log only if the folder exists. `DM2_PLAY_LOG_DIR=none` turns it off.

Python code can use `dm2tools.control_client.request(payload, path=None, timeout=150.0)`.

Examples for the real-time modes:

```sh
uv run dm2-play state
uv run dm2-play key '{"key": "up"}'
uv run dm2-play wait '{"ticks": 60}'
uv run dm2-play screenshot '{"path": "shot.png"}'
uv run dm2-play quit
```

Examples for turn-based mode:

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
- One client at a time. A second client gets `{"ok":false,"error":"busy"}` and the game closes its connection, unless the first client has already closed its connection and left no unread bytes. A client that connects again right after a reply does not get `busy`.
- While a turn or a wait runs, the game does not read the next request.
- A request line can have up to 4095 bytes before its newline. A longer line gets one `request line too long` error. A line with a NUL byte gets a `request line has a NUL byte` error. Replies have no size limit.
- String values can use the JSON escapes, even `\uXXXX`. The game decodes them to UTF-8. Replies send raw UTF-8.
- The parser is flat: it finds `"field": value` anywhere in the line, including the keys inside `args` in turn-based mode. Only `args` and the items of `actions` can be nested.
- Positions are game screen pixels: `x` 0 to 319, `y` 0 to 199.

## Commands

Commands for the real-time modes. Each request runs on the main thread between two timer ticks: in the game loop and in the modal loops (start menu, dialogs).

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
| `time` | Game time in game steps. It does not advance during a pause or in dialogs. |
| `ticks` | Timer ticks at 60 Hz since the program start. `wait` counts these. |
| `mode` | Input mode: `menu`, `picture`, `sleep`, `pause`, `dialog`, `move`, `push`, `recruit`, `inventory`, `name`; `movie` while the splash picture or a movie plays (see [Movies](turn-based.md#movies)). |
| `dialog` | The dialog id while `mode` is `dialog`, otherwise `null`. |
| `party` | Map, x, y, and direction (0 north, 1 east, 2 south, 3 west). |
| `champion_count`, `leader` | Champions in the party; the leader index, -1 for none. |
| `champions` | Per champion: name, `[current, maximum]` of health, stamina, and mana (stored values; the screen shows stamina in tenths), and the load in 1/10 kg. |
| `inventory`, `candidate` | The champion whose inventory is open and the champion being recruited, -1 for none. |
| `hand` | The object in the leader's hand: `{"thing": n, "name": "..."}`, or `null`. |
| `sound_volume`, `music_volume` | Volume settings 0 to 7. |
| `end`, `quit` | The game's end flag (party dead or quit) and the program's end flag. |
| `game_mode` | The game mode: `vanilla`, `advanced`, or `turnbased` ([manual.md](../manual.md#modes), [Turn-based mode](turn-based.md)). |
| `automap` | 1 while the automap is open, otherwise 0. |
| `cheat_immortal`, `cheat_food`, `cheat_light`, `cheat_kill`, `cheat_walls`, `cheat_map` | 1 while the cheat is on, otherwise 0 ([manual.md](../manual.md#cheats)). |

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

- One character of the US layout: `a` to `z`, `0` to `9`, punctuation. An uppercase letter or a shifted character sets the shift bit.
- `esc`, `enter`, `space`, `backspace`, `tab`, `f1` to `f10`.
- `up`, `down`, `left`, `right`, `shift+left`, `shift+right`: the codes of the window's arrow keys (forward, back, turn left, turn right, step left, step right).
- Keypad: `kp0` to `kp9`, `kp-`, `kp+`, `kp.`.
- Prefixes `shift+`, `ctrl+`, `alt+` add the modifier bit, for example `alt+a`.

Example of a key code: `{"cmd": "key", "key": 28}` presses Enter.

### click

Presses and releases a mouse button at a screen position. The pointer moves there. `button` is `left` (default) or `right`.

```json
{"cmd": "click", "x": 260, "y": 150, "button": "left"}
```

```json
{"ok":true}
```

### press and release

`press` holds a mouse button down at a screen position until a `release`. The pointer moves there. `button` is `left` (default) or `right`. Use them for the inventory's eye and mouth: the game shows the eye panel only while the left button is down, and the release closes it.

```json
{"cmd": "press", "x": 20, "y": 64}
```

```json
{"cmd": "release", "x": 20, "y": 64}
```

Both reply `{"ok":true}`.

### command

Clicks the zone of a command in the current input mode. Use it for a button whose position you do not know. `id` is the command number. The example clicks the first button of an open dialog (`0xdb`).

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

Lets `ticks` timer ticks (60 Hz, 1 to 6000) run, then replies. The game loop and the modal loops (menu, dialogs) both count ticks, so a wait also ends while a dialog is open. The reply gives the ticks that ran. If the client closes the connection during a wait and the game has no unread line from it, the game cancels the wait and accepts the next client. A client that sends more lines and then closes during a wait keeps the socket busy until the wait ends.

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

Ends the program as closing the window does, with exit code 0. The game removes the socket file.

```json
{"cmd": "quit"}
```

```json
{"ok":true}
```

## Errors

Errors of the real-time commands:

| Error | Cause |
| --- | --- |
| `missing cmd` | The line has no `cmd` field. |
| `unknown cmd` | `cmd` is not one of the commands above. |
| `unknown key` | The key name is not known, or the code is not 1 to 65535. |
| `x must be 0-319 and y 0-199` | A position is missing or off the screen. |
| `button must be left or right` | A different button name. |
| `missing id` | `command` without an `id`. |
| `ticks must be 1-6000` | A wait out of range. |
| `missing path` | `screenshot` without a `path`, or a `path` with a bad escape, a NUL character, or more than 1051 bytes (`OS_PATH_SIZE` minus 1). |
| `cannot write the PNG file` | The game cannot write the file. |
| `request line too long` | A line of more than 4095 bytes before its newline. |
| `request line has a NUL byte` | The line contains a zero byte. |
| `busy` | A different client is connected. |

## Known limits

- The socket path must be shorter than about 100 bytes.
- The game sends a reply with a blocking write. A client that sends requests but never reads the replies can stop the game. The socket is a local test tool.
- The Windows build (`make windows`, `build/dm2.exe`) has no control socket: `--mode turnbased` and `--control` stop with the message `the turn-based mode and --control need macOS` and exit code 1.

## Protocol vocabulary

Each line lists the names of one protocol table, in table order.

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
- Elements (`element` in `get_view`): `wall`, `floor`, `pit`, `stairs_up`, `stairs_down`, `door`, `teleporter`.
- Explosions (`name` in `seen.explosions` and in the `get_view` `flying` list): `fireball`, `slime`, `lightning`, `harm_non_material`, `door_toggle`, `poison_bolt`, `poison_cloud`, `push`, `pull`, `door_set`, `missile_wall`, `death_cloud`, `storm_lightning`, `storm_blast`, `smoke`.
- Runes (`runes`): the power runes `lo`, `um`, `on`, `ee`, `pal`, `mon`, then the three rune sets `ya`, `vi`, `oh`, `ful`, `des`, `zo`, `ven`, `ew`, `kath`, `ir`, `bro`, `gor`, `ku`, `ros`, `dain`, `neta`, `ra`, `sar`.
- Actions (`act`): `move_forward`, `move_back`, `move_left`, `move_right`, `turn_left`, `turn_right`, `click_wall_ahead`, `take_object`, `drop_object`, `open_inventory`, `close_inventory`, `put_in_slot`, `take_from_slot`, `eat`, `drink`, `choose_action`, `pass`, `set_caster`, `add_rune`, `delete_rune`, `cast`, `recruit`, `set_leader`, `sleep`, `wake`, `swap_champions`, `look`, `take_coin`, `minion_map_option`, `click`, `key` (see [Actions](turn-based.md#actions)).
