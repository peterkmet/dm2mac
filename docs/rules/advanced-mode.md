# Advanced mode

The exact rules of the advanced mode, for developers. The advanced mode is port only; the player view is in the
[Player manual](../manual.md#advanced-mode).

## Advanced keys

The advanced mode adds these keys:

| Key | Action | Where |
| --- | --- | --- |
| `W`, `S` | Move forward, move back | Where the arrow keys move the party |
| `A`, `D` | Move left, move right (strafe) | Where the arrow keys move the party |
| `Q`, `E` | Turn left, turn right | Where the arrow keys move the party |
| `F5` | Quick save | Dungeon view and cheat screen |
| `F9` | Quick load | Dungeon view and cheat screen |
| `Tab` or `M` | Open the automap | Dungeon view only |
| `Tab`, `M`, or `Esc` | Close the automap | Open automap |
| `C` | Open the cheat screen | Dungeon view only |
| `C` or `Esc` | Close the cheat screen | Open cheat screen |
| `Tab` | Close the inventory | Open inventory; not in the name editor or the recruit dialog |

A right click closes the automap and the cheat screen; on a spell page of the cheat screen it shows the cheat page. In the save menu a right click is RESUME, in the save menu's slot list CANCEL, and in the question before a quit CANCEL. The other dialogs keep the original right click.

A letter key moves the party only if the letter has no command of its own. In the slot name of the save dialog and in the name editor, the letters type text.

The dungeon view is the screen where the party moves, with at least one champion and no inventory, dialog, menu, pause, sleep, or recruit.

- In the dungeon view the advanced keys work only without a recruit candidate and with an idle party: no held eye or mouth, no champion drag, and no push mode. Otherwise they do nothing.
- In other places `F5`, `F9`, `Tab`, `M`, and `C` do nothing.
- `Esc` keeps its original use in both modes: it closes the inventory, and it pauses or resumes the game. Only on the open automap and the open cheat screen it closes the screen.
- Only one of the automap and the cheat screen is open at a time.

## Quick save and quick load

The advanced mode has one quick save slot: `<data>/DATA/QUICK.DAT`. With `--saves DIR` the file is `DIR/QUICK.DAT`. The file has the same format as the save slots. [Advanced keys](#advanced-keys) says where `F5` and `F9` work.

`F5` writes the game to `QUICK.DAT`. The message area shows one of these lines:

- `GAME SAVED`: the game wrote the file.
- `UNABLE TO SAVE`: the game cannot write the file. An earlier quick save stays.

A quick save is not a saved game for the save menu. The QUIT question and RESTART GAME after the party's death use only the save slots.

`F9` loads `QUICK.DAT` without a dialog. The message area shows one of these lines:

- `GAME LOADED`: the game continues from the quick save, with the quick save's sound and music volumes. A later QUIT asks for confirmation, as for an unsaved game.
- `NO QUICK SAVE`: the file does not exist. The game continues.
- `QUICK SAVE DAMAGED`: the file does not load. The game continues unchanged. If the game cannot restore the running game either, the program ends ([save.md](../formats/save.md#quick-save-files)).
- `UNABLE TO SAVE`: the game cannot keep a copy of the running game before the load, so it does not load. The game continues.

## Changes to the original rules

- A left click on a status box outside the hands puts the leader hand object into the champion's inventory, as a click on the bars does. With an empty leader hand it opens the inventory, as a click on the name does. The hands keep their click.
- A held key repeats only when the game waits for input, so the party stops when you release the key. In the vanilla mode the party continues to walk for the queued repeats, as in the original ([movement.md](movement.md#deviations-from-the-original)).
- An antidote always cures the poison. In the vanilla mode it can miss the poison, as in the original ([champions.md](champions.md#deviations-from-the-original)).
- A slot save and the quick save keep the seen squares and the cheats.

The automap and the cheat screen open in the viewport and stop the game time. The title and the subtitles of the cheat screen are cyan, its rows are white, and the selected row or title item is yellow.

## Automap

The automap shows the seen squares of all maps of the party level, each at its place on the level. It covers the viewport. The party square is in the middle of the viewport, north is up.

- The squares use the icons of the minion map: walls, floors, pits, stairs, doors, wall and floor ornaments, teleporter fields, and the level digits. The automap shows the static creatures that have a map icon (trees and other creatures that do not move); moving creatures, merchants, and items do not show. A red frame and the champion icons mark the party square. The side of the frame that faces the party's direction is white.
- A wall that the dungeon view shows at the edge of a map shows as a wall ([view.md](view.md#automap)).
- A 1-pixel white frame surrounds the level with its maps' margins, just outside its last squares. It marks the end of the level, even where squares are not seen.
- An imaginary wall (a closed fake wall that the party can walk through) shows as the wall icon with a light gray checkerboard.
- A pressure pad (a floor plate that the dungeon view shows) shows as a small gray square in the middle of its square. A hidden floor trigger does not show.
- Unseen squares stay black. With the reveal map cheat, all squares of the level show.
- The readout at the top left shows `LEVEL n  X x  Y y` and the party's direction (`NORTH`, `EAST`, `SOUTH`, `WEST`).
- Keys other than the automap keys do nothing.
- A drag with the left mouse button moves the map. The map moves in whole squares. The middle of the map stays inside the level. The next automap opens on the party again.

## Seen squares

Each game step marks the squares that the dungeon view shows as seen. In low light the party sees fewer rows. The light cheat shows all rows. The rule is in [view.md](view.md#seen-squares).

A new game starts with an empty automap.

## Cheats

The advanced mode has six cheats. The cheat screen (`C`) switches each cheat on and off. A slot save and the quick save keep them. A load and a quick load take them from the file. A new game turns them all off. The vanilla and turn-based modes use no cheats.

- Full light: the dungeon view draws in full light. The creatures and the fight rules keep the real light.
- Immortal: damage that brings a champion to 0 health or less leaves 1 health. The champion does not die.
- Endless food and water: food and water do not go down over time. Eating and drinking still raise them.
- One-hit kill: each hit kills the creature, even a hit of a trap, a door, or another creature. A creature that cannot be hurt, or that others cannot hit, survives. Health that a creature loses by itself (over time, or by its own missile attack) does not become a kill. A boss that dies only near the party front keeps that rule.
- Walk through walls: the party enters walls, closed fake walls, and closed doors inside the map. A square off the map and a creature still stop the party. Creatures and thrown objects do not pass. A door that closes on the party opens again without damage. The party in a wall square does not touch the sensors of that wall. In a wall, a closed fake wall, or a closed door, the party cannot drop an object on its own square: the object stays in the hand.
- Reveal map: the automap shows every square of the level. The seen squares do not change.

## Cheat screen

`C` in the dungeon view opens the cheat screen in the viewport. The cheat page has the title CHEATS with CLOSE at the right of the title line, then 11 rows: the six cheat rows, the subtitle OPTIONS, SCREEN FILTER, QUICK SAVE F5, QUICK LOAD F9, and SPELLS. Click a row, or select it with Up and Down (`W`, `S`) and press Return. The selection skips the subtitle, stops at the first and the last item, and CLOSE is the last item. `C`, `Esc`, and a right click close the screen.

- FULL LIGHT, IMMORTAL, ENDLESS FOOD AND WATER, ONE-HIT KILL, WALK THROUGH WALLS, REVEAL MAP: each row shows `ON` or `OFF` and switches its cheat (see [Cheats](#cheats)).
- OPTIONS: a subtitle; a click on it does nothing.
- SCREEN FILTER: shows the window's upscale filter, `NONE`, `SCALE2X`, or `SCALE3X`, and changes to the next filter (after SCALE3X comes NONE). The change shows immediately and goes to [SETTINGS.TXT](#settingstxt). If the window cannot show the filter, the filter stays. The volume stays in the volume dialog of the original game.
- QUICK SAVE F5: writes `QUICK.DAT`; the screen stays open. `F5` on the screen does the same.
- QUICK LOAD F9: closes the screen, then loads `QUICK.DAT` as `F9` in the dungeon view does. `F9` on the screen does the same.
- SPELLS: the spell pages, `SPELLS n/5`, with 8 spells per page, then NEXT, BACK, and CLOSE. NEXT after the last page shows the first page. BACK on the first page and a right click on a spell page show the cheat page with SPELLS selected.
- CLOSE: closes the screen.

A spell line shows the runes, their numbers, and the spell name, for example `FUL IR (X,4,4) FIREBALL`. A number is the place of the rune in its row (1 to 6); `X` means any power. A line wider than the viewport shows only the numbers and the name. The spell names are names of the port, because the game files have none.

A click on a spell line (or Return) casts the spell and closes the screen. The caster is the champion of the open spell panel, otherwise the leader, otherwise the first living champion. Without a living champion the message area shows `NO CHAMPION CAN CAST`. No spell of the game has a power rune, so the cast uses MON, the highest power. The cast costs no mana, has no failure roll, and gives no experience; the spell's action delay stays. A potion still needs an empty flask in one of the caster's hands; without it the message area shows `NO EMPTY FLASK IN HAND`.

## Damage hold

The damage star of a hit stays one game step longer in the action area than in the original game. A left click in the action area during this extra step ends it and draws the hand icons. The click then acts as usual. The action delay and the timing of the champion do not change. Failures and hits of zero damage keep the original time.

## Smooth projectile flight

A projectile moves one cell (half a square) per game step, as in the original game. In the advanced mode the dungeon view shows the flight between the game steps. On each timer tick (60 Hz) the game draws the view again, with each projectile part of the way from its place at the last game step to its current place. The flight shows one game step later than the game rules move the projectile.

- A new projectile, or one not in the view at the last game step, shows at its current place.
- The flight shows only if the party view is the same as at the step before: the same map, square, and direction. It does not show while the party walks or after a turn.
- It does not show in the inventory, during sleep or pause, while the automap or the cheat screen is open, or in the vanilla and turn-based modes.
- The flight changes no game state ([view.md](view.md#deviations-from-the-original)).

## SETTINGS.TXT

The SCREEN FILTER row of the cheat screen keeps the filter in `SETTINGS.TXT` in the folder of `QUICK.DAT` (`--saves DIR`, otherwise `<data>/DATA`). The file format is in [save.md](../formats/save.md#settingstxt).

The advanced mode reads the file once on startup and applies the filter. `--filter` and `DM2_FILTER` have priority on startup. The SCREEN FILTER row can still change the filter, and the change writes the file. The vanilla and turn-based modes do not read the file.

The game writes the file only when the filter changes. If the write fails, the filter stays changed and the message area shows `UNABLE TO SAVE OPTIONS` once. It shows again only after a write works and a later write fails.

## Deviations from the original

- The whole page is port only: the original game has no advanced mode.
