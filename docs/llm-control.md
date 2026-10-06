# LLM control over a socket

`build/dm2 --control PATH` opens a Unix domain socket. A client sends raw input to the running game, reads a small game state, and saves screenshots. Use it for manual-style UI tests that an LLM (for example Claude Code through Bash) drives.

The game runs in real time. The control does not stop the game time. Each request runs on the main thread between two timer ticks: in the game loop and in the modal loops (start menu, dialogs).

## Start the game

1. Build the port.

   ```bash
   make -C port
   ```

2. Start the game with the socket. The dummy drivers run it without a window and without sound.

   ```bash
   SDL_AUDIO_DRIVER=dummy SDL_VIDEO_DRIVER=dummy build/dm2 --control ~/.dm2/control.sock --save tests/saves/start.DAT &
   ```

   - `--control PATH`: the socket file. The game removes an old file at `PATH` and makes its folder. The socket path must be shorter than about 100 bytes.
   - `--save PATH`: optional. Without it, the game starts with the start menu.
   - Without the two `SDL_*_DRIVER` variables, the window and the sound stay on.

3. Send requests with `dm2-play`.

   ```bash
   uv run dm2-play state
   uv run dm2-play key '{"key": "up"}'
   uv run dm2-play wait '{"ticks": 60}'
   uv run dm2-play screenshot '{"path": "shot.png"}'
   uv run dm2-play quit
   ```

## The dm2-play command

```text
uv run dm2-play [--socket PATH] <cmd> ['<json-fields>']
```

- `<cmd>`: the request command, for example `state`.
- `<json-fields>`: the other fields of the request as one JSON object.
- `--socket PATH`: the socket path. Without it, `dm2-play` uses `$DM2_CONTROL`, else `~/.dm2/control.sock`.

`dm2-play` prints the reply as compact JSON on one line. The exit code is 1 when the reply has `"ok": false`, also when the game is not reachable. A relative screenshot `path` becomes an absolute path before it goes to the game.

Python code can use `dm2tools.control_client.request(payload, path=None, timeout=150.0)`.

## Protocol

- One client at a time. A client sends one JSON object per line and gets one JSON object per line.
- Each request has a `cmd` field.
- A successful reply has `"ok": true`. A failed reply is `{"ok": false, "error": "<reason>"}`.
- The parser is flat: it finds `"field": value` anywhere in the line. Do not nest objects.
- Positions are game screen pixels: `x` 0 to 319, `y` 0 to 199.

## Commands

### state

Reads the game state.

```json
{"cmd": "state"}
```

```json
{"ok":true,"time":33,"ticks":61,"mode":"move","dialog":null,"party":{"map":0,"x":1,"y":8,"dir":0},"champion_count":1,"leader":0,"champions":[{"name":"TORHAM","health":[83,83],"stamina":[770,770],"mana":[14,14],"load":67}],"inventory":-1,"candidate":-1,"hand":null,"sound_volume":7,"music_volume":7,"end":0,"quit":0}
```

| Field | Meaning |
| --- | --- |
| `time` | Game time in game steps (`game.game_time`). It stands still during a pause and in dialogs. |
| `ticks` | Timer ticks at 60 Hz since the program start (`main_loop.timer_ticks`). `wait` counts these. |
| `mode` | Input mode: `menu`, `picture`, `sleep`, `pause`, `dialog`, `move`, `push`, `recruit`, `inventory`, `name`. |
| `dialog` | The dialog id (`DialogState.id`, `port/src/dialog.h`) while `mode` is `dialog`, else `null`. |
| `party` | Map, x, y, and direction (0 north, 1 east, 2 south, 3 west). |
| `champion_count`, `leader` | Champions in the party; the leader index, -1 for none. |
| `champions` | Per champion: name, `[current, maximum]` of health, stamina, and mana (stored values; the screen shows stamina in tenths), and the load in 1/10 kg. |
| `inventory`, `candidate` | The champion of the open inventory and the champion that is being recruited, -1 for none. |
| `hand` | The leader hand object: `{"thing": n, "name": "..."}`, or `null`. |
| `sound_volume`, `music_volume` | Volume settings 0 to 7. |
| `end`, `quit` | The end flag of the game (party dead or quit) and the program end flag. |

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

Clicks a mouse button at a screen position. The pointer moves there. `button` is `left` (default) or `right`. A right click also sends the release of the right button.

```json
{"cmd": "click", "x": 260, "y": 150, "button": "left"}
```

```json
{"ok":true}
```

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

Lets `ticks` timer ticks (60 Hz, 1 to 6000) run, then replies. The game loop and the modal loops (menu, dialogs) both count ticks, so a wait also ends while a dialog is open. The reply gives the ticks that ran. During a wait the game reads no other request of this client.

```json
{"cmd": "wait", "ticks": 60}
```

```json
{"ok":true,"ticks":60}
```

A game step takes 8 ticks. A step forward needs about 40 ticks before the party stands on the next square.

### screenshot

Writes a PNG file of 320 x 200 pixels: the screen with the palette and the pointer. Over a zone where the game shows the leader hand object, the pointer is that object, as in the window. In other places the pointer is a small white arrow with its tip at the pointer position. Before the first click or move, a screenshot has no pointer. The reply gives the absolute path.

```json
{"cmd": "screenshot", "path": "/tmp/shot.png"}
```

```json
{"ok":true,"path":"/private/tmp/shot.png"}
```

### quit

Ends the program the same way as a closed window. The exit code is 0 in the game. The socket file is removed.

```json
{"cmd": "quit"}
```

```json
{"ok":true}
```

## Errors

| Error | Cause |
| --- | --- |
| `missing cmd` | The line has no `cmd` field. |
| `unknown cmd` | `cmd` is not one of the commands above. |
| `unknown key` | The key name is not known, or the code is not 1 to 65535. |
| `x must be 0-319 and y 0-199` | A position is missing or off the screen. |
| `button must be left or right` | Another button name. |
| `missing id` | `command` without an `id`. |
| `ticks must be 1-6000` | A wait out of range. |
| `missing path` | `screenshot` without a `path`. |
| `cannot write the PNG file` | The game cannot write the file. |
| `request line too long` | A line of 4096 bytes or more. |

## View a screenshot as an LLM

1. Save the screenshot to a file.

   ```bash
   uv run dm2-play screenshot '{"path": "/tmp/dm2-shot.png"}'
   ```

2. Open the path of the reply with the Read tool of Claude Code. The Read tool shows a PNG file as an image.

A typical test loop: `screenshot`, read the image, `move` or `click` at a position in the image, `wait` some ticks, `screenshot` again. The image pixels are the game pixels, so a position in the image is the `x` and `y` of a request.
