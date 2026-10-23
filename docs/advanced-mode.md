# Advanced mode

The C port has two game modes. Without the cheats, the game rules are the same in both modes.

- `advanced` (the default) adds keys, a quick save, an automap, an options menu, and a cheat screen with cheats that the original game does not have.
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
| `C` | Open the cheat screen | Dungeon view only |
| `C` or `Esc` | Close the cheat screen | Open cheat screen |
| `Esc` | Open the options menu (instead of the pause) | Dungeon view only |
| `Tab` | Close the inventory | Open inventory; not in the name editor or the recruit dialog |
| `Esc` | Close the options menu | Open options menu |

A letter key gives its movement only when the letter has no command of its own. In the slot name of the save dialog and in the name editor, the letters type text.

The dungeon view is the movement input mode with at least one champion: no inventory, dialog, menu, pause, sleep, or recruit. In other places `F5`, `F9`, `Tab`, `M`, and `C` do nothing. `Esc` there opens the options menu; in other places `Esc` keeps its original use (it closes the inventory, and it pauses or resumes the game). The vanilla mode pauses. The automap, the cheat screen, and the options menu do not open together.

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
- A white frame of 1 pixel surrounds the whole map, just outside its last squares. It shows the end of the map, also for squares that are not seen.
- An imaginary wall (a closed fake wall that the party can walk through) shows as the wall icon with a light gray checkerboard, so it differs from a real wall.
- A pressure pad shows as a small gray square in the middle of its floor square. A pressure pad is a floor square with a floor sensor that the party or a creature triggers by stepping on it (sensor types any, party or creature, party, creature, party possesses an item) whose floor ornament the dungeon view draws. A floor trigger without an ornament stays hidden. The pad of an item sensor does not show.
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
- Walk through walls: the party enters walls, closed fake walls, and closed doors inside the map. A square off the map and a creature still stop the party. Creatures and thrown objects do not pass. A door that closes on the party opens again without damage. The party in a wall square does not touch the sensors of that wall. In a wall, a closed fake wall, or a closed door, the party cannot drop an object on its own square: the object stays in the hand.
- Reveal map: the automap shows every square of the map. The seen squares do not change.

## Cheat screen

`C` in the dungeon view opens the cheat screen in the viewport. The game time stops while it is open. Click a row, or select it with Up and Down (`W`, `S`) and press Return. `C` and `Esc` close the screen. The title is cyan, the rows are white, the selected row is yellow.

- FULL LIGHT, IMMORTAL, FOOD AND WATER, ONE-HIT KILL, WALK THROUGH WALLS, REVEAL MAP: each row shows `ON` or `OFF` and switches its cheat (see Cheats).
- QUICK SAVE F5: writes `QUICK.DAT`; the screen stays open. `F5` on the screen does the same.
- QUICK LOAD F9: closes the screen, then loads `QUICK.DAT` as `F9` in the dungeon view does. `F9` on the screen does the same.
- SPELLS: the spell pages, `SPELLS n/5`, with 8 spells per page, then NEXT, BACK, and CLOSE. NEXT after the last page shows the first page. BACK on the first page shows the cheat page.
- CLOSE: closes the screen.

A spell line shows the runes, their numbers, and the spell name, for example `FUL IR (X,4,4) FIREBALL`. A number is the place of the rune in its row (1 to 6); `X` means any power. A line wider than the viewport shows only the numbers and the name. The game files have no spell names: the names are port names. A potion spell is named by its drink effect, a missile by its explosion, a summon by its minion.

A click on a spell line (or Return) casts the spell and closes the screen. The caster is the champion of the open spell panel, else the leader. The power is the power rune of the spell, or MON for any power (all spells of the game take any power). The cast costs no mana, has no failure roll, and gives no experience; the action delay of the spell stays. A potion still needs an empty flask in a hand of the caster; without it the message area shows `NO EMPTY FLASK IN HAND`.

## Damage hold

The damage star of a hit stays one game step longer in the action area than in the original game. A left click in the action area during this extra step ends it and draws the hand icons; the click then acts as usual. The action delay and the timing of the champion do not change. Failures and hits of zero damage keep the original time.

## Smooth projectile flight

A projectile moves one cell (half a square) per game step, as in the original game. In the advanced mode the dungeon view shows the flight between the game steps: on each timer tick (60 Hz) the view is drawn again with each projectile part of the way from its place in the view of the game step before to its place now. At the game step the projectile shows at its old place; at the end of the step it reaches its new place. The flight shows one game step later than the game rules move the projectile.

- A projectile that is new or was not in the view of the game step before shows at its place now.
- The flight shows only when the party view is the same as at the step before: the same map, square, and direction. It does not show while the party walks or after a turn.
- It does not show in the inventory, during the sleep or the pause, while the automap, the cheat screen, or the options menu is open, and in the vanilla and turn-based modes.
- The extra views change no game state: the random seed stays, explosions do not count again, and a shop display does not choose or create an item.

## Options menu

`Esc` in the dungeon view opens the options menu in the viewport. The game time stops while it is open. The title is cyan, the rows are white, the selected row is yellow. The volume stays in the volume dialog of the original game.

- FILTER: the upscale filter of the window, `NONE`, `SCALE2X`, or `SCALE3X`. A change shows at once.
- CONTINUE: closes the menu.

| Input | Action |
| --- | --- |
| `Esc` | Close the menu |
| Up, Down, `W`, `S` | Select a row |
| Left, Right, `A`, `D` | Set the previous or next filter (after SCALE3X comes NONE) |
| Return | On FILTER: the next filter. On CONTINUE: close the menu |
| Left click on FILTER | The next filter |
| Left click on CONTINUE | Close the menu |

### SETTINGS.TXT

The menu keeps the filter in `SETTINGS.TXT` in the folder of `QUICK.DAT` (`--saves DIR`, else `<data>/DATA`). The file has one line:

```
filter=scale2x
```

The values are `none`, `scale2x`, and `scale3x`. The file is a text file of `key=value` lines. The game ignores a CR at the end of a line, an unknown key, a bad value, and a line of 64 characters or more. A file over 1024 bytes gives the defaults (no filter).

The advanced mode reads the file once at the start of the program and applies the filter. `--filter` and `DM2_FILTER` have priority at the start; the menu can still change the filter, and the change writes the file. The vanilla and turn-based modes do not read the file.

The game writes the file only when the filter changes. It writes `SETTINGS.TMP` first and replaces the file with it. When the write fails, the filter stays changed and the message area shows `UNABLE TO SAVE OPTIONS` once, until a write works.
