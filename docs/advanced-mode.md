# Advanced mode

The C port has two game modes. The game rules are the same in both modes.

- `advanced` (the default) adds keys that the original game does not have.
- `vanilla` is the original game without additions.

## Select the mode

Start the program with `--mode`:

```sh
build/dm2 --mode vanilla
build/dm2 --mode advanced
```

Without `--mode`, the mode is `advanced`. A different value stops the program with exit code 2 and the message `unknown mode VALUE: use vanilla or advanced`. RESTART keeps the mode.

The control socket state (`docs/llm-control.md`) gives the mode in the field `game_mode`.

## Keys

The keys of the original game work in both modes. The advanced mode adds these keys:

| Key | Action | Where |
| --- | --- | --- |
| `W` | Move forward | Where the arrow keys move the party |
| `S` | Move back | Where the arrow keys move the party |
| `A` | Move left (strafe) | Where the arrow keys move the party |
| `D` | Move right (strafe) | Where the arrow keys move the party |
| `Q` | Turn left | Where the arrow keys move the party |
| `E` | Turn right | Where the arrow keys move the party |
| `F5` | Quick save | Dungeon view only |
| `F9` | Quick load | Dungeon view only |

A letter key gives its movement only when the letter has no command of its own. In the slot name of the save dialog and in the name editor, the letters type text.

The dungeon view is the movement input mode with at least one champion: no inventory, dialog, menu, pause, sleep, or recruit. In other places `F5` and `F9` do nothing.

## Quick save and quick load

The quick save has one slot: `<data>/DATA/QUICK.DAT`. `<data>` is the `--data` folder (default `game`). The file has the format of the save slots.

`F5` writes the game to `QUICK.DAT`. The message area shows one of these lines:

- `GAME SAVED`: the file is written.
- `UNABLE TO SAVE`: the file cannot be written. An earlier quick save stays.

A quick save is not a saved game for the save menu. The QUIT question and RESTART GAME after the death of the party use only the save slots.

`F9` loads `QUICK.DAT` without a dialog. The message area shows one of these lines:

- `GAME LOADED`: the game goes on from the quick save. A later QUIT asks before the quit, as after a game that is not saved.
- `NO QUICK SAVE`: the file does not exist. The game goes on.
- `QUICK SAVE DAMAGED`: the file does not load. The game goes on unchanged.
- `UNABLE TO SAVE`: the game cannot keep a copy of the running game in `DATA/QUICK.TMP` before the load, so it does not load. The game goes on.
