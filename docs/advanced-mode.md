# Advanced mode

The C port has two game modes. Without the cheats, the game rules are the same in both modes.

- `advanced` (the default) adds keys, a quick save, an automap, and cheats that the original game does not have.
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
| `Tab` or `M` | Open the automap | Dungeon view only |
| `Tab`, `M`, or `Esc` | Close the automap | Open automap |
| `I` | Immortal cheat on or off | Open automap |
| `F` | Food and water cheat on or off | Open automap |
| `L` | Light cheat on or off | Open automap |

A letter key gives its movement only when the letter has no command of its own. In the slot name of the save dialog and in the name editor, the letters type text.

The dungeon view is the movement input mode with at least one champion: no inventory, dialog, menu, pause, sleep, or recruit. In other places `F5`, `F9`, `Tab`, and `M` do nothing.

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

## Automap

The automap shows the squares of the party map that the dungeon view has shown. It covers the viewport. The party square is in the middle of the viewport, north is up.

- The squares use the icons of the minion map: walls, floors, pits, stairs, doors, wall and floor ornaments, teleporter fields, and the level digits. A red frame and the champion icons of a minion map item mark the party square.
- Only the seen squares show. Other squares stay black.
- The readout at the top left shows `LEVEL n  X x  Y y`, the direction of the party (`NORTH`, `EAST`, `SOUTH`, `WEST`), and the cheat states `I IMMORTAL ON|OFF`, `F FOOD AND WATER ON|OFF`, and `L LIGHT ON|OFF`.
- The game time stops while the automap is open. Keys other than the automap keys do nothing.
- Drag the map with the left mouse button. The map moves in whole squares. The middle of the map stays on the map. The next automap opens on the party again.

### Seen squares

Each game step marks the squares of the dungeon view. The view has 5 rows: the party row and 4 rows ahead, with 3, 3, 5, 5, and 7 squares.

- The party square is seen.
- A square is seen when the seen square beside it toward the middle is open, or when the seen square in front of it (one row nearer) is open.
- The rows end at the first middle square that is not open.
- Not open: a wall, a closed fake wall, a closed door, and a square off the map.
- In dark light the view is shorter: light level 4 shows one row less, light level 5 two rows less. The light cheat shows all rows.

A new game starts with an empty automap.

### Save block

In the advanced mode, a slot save and `QUICK.DAT` end with the AMAP block. The block follows the original save data. The bytes before the block are the same as in the vanilla mode.

| Bytes | Content |
| --- | --- |
| 4 | `AMAP` |
| 1 | Version: 1 |
| 1 | Map count |
| 2 per map | Width and height of the map |
| 1 per square | 1: seen, 0: not seen. The maps one after another, each map column by column (x, then y). |

A load reads the block when it is there. A missing block, a block with other map sizes, a value other than 0 or 1, or bytes after the block give an empty automap. The game loads.

The original game loads a save with the block: its loader stops at the last bit of the game state. A save in the original game writes the file again without the block.

## Cheats

The cheats switch only on the open automap of the advanced mode. The automap draws the new state at once. The cheats are not saved. A new game, a load, and a quick load turn them off.

- `I` immortal: damage that brings a champion to 0 health or less leaves 1 health. The champion does not die.
- `F` food and water: food and water do not go down with the time. Eating and drinking still raise them.
- `L` light: the dungeon view draws in full light. The creatures and the fight rules keep the real light.
