# Advanced mode

The C port has two game modes. Without the cheats, the game rules are the same in both modes.

- `advanced` (the default) adds keys, a quick save, an automap, and cheats that the original game does not have.
- `vanilla` is the original game without additions.

## Select the mode

Without `--mode`, the program shows the mode screen before the start movies. The screen shows the heading MODE and the items VANILLA and ADVANCED. The cursor (gold) starts on ADVANCED.

| Input | Action |
| --- | --- |
| Up, keypad 8, `W` | Move the cursor up |
| Down, keypad 2, `S` | Move the cursor down |
| Return, keypad Enter | Select the mode at the cursor |
| Left click on an item | Select that mode |

The cursor stops at the ends of the list. Close the window to end the program. The selected mode plays as with `--mode`; the start movies and the title menu follow.

`--mode` skips the screen:

```sh
build/dm2 --mode vanilla
build/dm2 --mode advanced
```

`--save` also skips the screen; without `--mode`, the mode is then `advanced`. A value other than `vanilla`, `advanced`, or `turnbased` stops the program with exit code 2 and the message `unknown mode VALUE: use vanilla, advanced, or turnbased`. RESTART skips the screen and keeps the mode, also the mode of the screen.

The turn-based mode (`--mode turnbased`, `docs/turnbased-mode.md`) is a third mode for play through the control socket. It shares the seen squares and the AMAP block with the advanced mode, but it has no extra keys and no cheats.

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

A letter key gives its movement only when the letter has no command of its own. In the slot name of the save dialog and in the name editor, the letters type text.

The dungeon view is the movement input mode with at least one champion: no inventory, dialog, menu, pause, sleep, or recruit. In other places `F5`, `F9`, `Tab`, and `M` do nothing.

## Quick save and quick load

The quick save has one slot: `<data>/DATA/QUICK.DAT`. `<data>` is the `--data` folder (default `game`). With `--saves DIR` the file is `DIR/QUICK.DAT` in all modes. The file has the format of the save slots.

`F5` writes the game to `QUICK.DAT`. The message area shows one of these lines:

- `GAME SAVED`: the file is written.
- `UNABLE TO SAVE`: the file cannot be written. An earlier quick save stays.

A quick save is not a saved game for the save menu. The QUIT question and RESTART GAME after the death of the party use only the save slots.

`F9` loads `QUICK.DAT` without a dialog. The message area shows one of these lines:

- `GAME LOADED`: the game goes on from the quick save, with the sound and music volumes of the quick save. A later QUIT asks before the quit, as after a game that is not saved.
- `NO QUICK SAVE`: the file does not exist. The game goes on.
- `QUICK SAVE DAMAGED`: the file does not load. The game goes on unchanged. If the copy in `QUICK.TMP` also does not load, the program prints an error and ends with exit code 4; `QUICK.TMP` stays.
- `UNABLE TO SAVE`: the game cannot keep a copy of the running game in `DATA/QUICK.TMP` before the load, so it does not load. The game goes on.

## Automap

The automap shows the squares of the party map that the dungeon view has shown. It covers the viewport. The party square is in the middle of the viewport, north is up.

- The squares use the icons of the minion map: walls, floors, pits, stairs, doors, wall and floor ornaments, teleporter fields, and the level digits. A red frame and the champion icons of a minion map item mark the party square.
- Only the seen squares show. Other squares stay black. With the reveal map cheat, all squares of the map show.
- The readout at the top left shows `LEVEL n  X x  Y y` and the direction of the party (`NORTH`, `EAST`, `SOUTH`, `WEST`).
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

In the advanced mode, a slot save and `QUICK.DAT` end with two blocks after the original save data: the AMAP block, then the CHTS block. The turn-based mode writes only the AMAP block. The bytes before the blocks are the same as in the vanilla mode.

The AMAP block:

| Bytes | Content |
| --- | --- |
| 4 | `AMAP` |
| 1 | Version: 1 |
| 1 | Map count |
| 2 per map | Width and height of the map |
| 1 per square | 1: seen, 0: not seen. The maps one after another, each map column by column (x, then y). |

The CHTS block:

| Bytes | Content |
| --- | --- |
| 4 | `CHTS` |
| 1 | Version: 1 |
| 1 | Cheat bits: 0x01 full light, 0x02 immortal, 0x04 food and water, 0x08 one-hit kill, 0x10 walk through walls, 0x20 reveal map |

A load reads the blocks when they are there.

- AMAP: a missing block, a block with other map sizes, or a value other than 0 or 1 gives an empty automap. After the block, only the end of the file or a valid CHTS block can come; other bytes give an empty automap.
- CHTS: in the advanced mode, the cheats come from the block. A missing block, a wrong version, an unknown bit, bytes after the block, or a damaged AMAP block give no cheats. The vanilla and turn-based modes ignore the block and use no cheats.
- The game loads in all these cases. A save with only the original data (vanilla) loads with an empty automap and no cheats.

The original game loads a save with the blocks: its loader stops at the last bit of the game state. A save in the original game writes the file again without the blocks.

## Cheats

The advanced mode has six cheats. They switch on the cheat screen (`C` key). A slot save and the quick save keep them in the CHTS block; a load and a quick load take them from the file. A new game turns them all off. The vanilla and turn-based modes use no cheats.

- Full light: the dungeon view draws in full light. The creatures and the fight rules keep the real light.
- Immortal: damage that brings a champion to 0 health or less leaves 1 health. The champion does not die.
- Food and water: food and water do not go down with the time. Eating and drinking still raise them.
- One-hit kill: each hit kills the creature, also a hit of a trap, a door, or another creature (a missile does not keep its launcher). A creature that cannot be hurt (defense 0xff) stays unhurt; a creature that others cannot hit (dexterity 0xff) stays so. The health that a creature loses by itself (over time, or by its own missile attack) does not kill it. A boss that dies only near the party front keeps that rule.
- Walk through walls: the party enters walls, closed fake walls, and closed doors inside the map. A square off the map and a creature still stop the party. Creatures and thrown objects do not pass. A door that closes on the party opens again without damage. The party in a wall square does not touch the sensors of that wall.
- Reveal map: the automap shows every square of the map. The seen squares do not change.
