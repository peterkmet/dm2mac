# Player manual

DM2Mac plays Dungeon Master II as the original PC game does. This manual covers only what is new: how to start
the game, the vanilla and advanced modes, and the advanced-mode extras. For the game itself, see the original game's
manual. Item values are in [items.md](items.md).

## Run the game

Install the game as the [README](../README.md#play) says, or [build it](development.md) and start `build/dm2`
(`build/dm2.exe` on Windows) from the folder that holds your copy of the game.

On first startup the game copies the 10 files it needs from your folder, ZIP archive, or CD image into `game/`
([Game files and install](formats/install.md)). On Windows, the game's messages go to `game/dm2.log`.

## Options

You need these only when you start the game from Terminal or a shortcut:

| Option | What it does |
|---|---|
| `--mode vanilla`, `--mode advanced` | Starts in that mode and skips the mode screen. `--mode turnbased` lets a program play ([Turn-based mode](llm/turn-based.md), macOS only). |
| `--save PATH` | Loads a save and starts it immediately, in advanced mode unless `--mode` says otherwise. |
| `--filter NAME` | The window's pixel-art filter: `none`, `scale2x`, or `scale3x`. The `DM2_FILTER` environment variable does the same. |
| `--data DIR` | Uses the game files in `DIR` instead of `game/`, and skips the check and the install. Saves go to `DIR/DATA`. |
| `--saves DIR` | Keeps the quick save and `SETTINGS.TXT` in `DIR` instead of `game/DATA`. The folder must exist. |
| `--control PATH` | Opens a control socket for a program (macOS only). |

```sh
build/dm2 --mode vanilla
build/dm2 --save game/DATA/SKSAVE0.DAT
```

An invalid option prints a message that says what is wrong. You can resize the window to any size.

## Start screens

Without `--mode` and `--save`, the game first asks for the mode: VANILLA or ADVANCED. Choose with the arrow keys and
Return, or click. Then the intro movies play (click to skip one), and the original game's start menu follows.
RESTART in the save menu keeps the mode.

## Modes

| Mode | What you get |
|---|---|
| `vanilla` | The original game, nothing added. |
| `advanced` | The default. The original game with the [advanced features](#advanced-mode). |
| `turnbased` | A program plays the game turn by turn ([Turn-based mode](llm/turn-based.md)). macOS only. |

The game rules are the same in all modes, except for the cheats and the few changes under
[Advanced mode](#advanced-mode).

## Controls

The mouse, arrow keys, and numeric keypad work as in the original game. Keys that are easy to miss:

| Key | Action |
|---|---|
| `1` to `4` | Open a champion's inventory |
| Shift+Left, Shift+Right | Step sideways |
| Alt+S (Option+S on a Mac) | Save menu |
| `Esc` | Pause (in advanced mode: the options menu) |

## Saved games

Save in a slot with Alt+S and load a slot from the start menu, as in the original game. The slots are in
`game/DATA`.

## Advanced mode

Advanced mode adds the advanced keys, the quick save, the automap, the cheats, and the options menu. It also
changes a few things:

- When you release a held key, the party stops. In vanilla mode the party keeps walking, as in the original.
- An antidote always cures poison. In vanilla mode it can fail, as in the original.
- A click on a champion's status box puts the object in your hand into that champion's inventory, or opens the
  inventory if your hand is empty.
- After a hit, the damage star stays a moment longer in the action area. A click ends it.
- Projectiles fly smoothly from square to square in the dungeon view.
- A save keeps the automap and the cheats.

The automap, the cheat screen, and the options menu stop game time while they are open. The exact rules are in
[Advanced mode rules](rules/advanced-mode.md).

### Advanced keys

| Key | Action |
|---|---|
| `W` `A` `S` `D` | Move forward, left, back, right |
| `Q`, `E` | Turn left, turn right |
| `F5`, `F9` | Quick save, quick load |
| `Tab` or `M` | Automap |
| `C` | Cheat screen |
| `Esc` | Options menu |
| `Tab` | Close the inventory |

The keys work only in the dungeon view. In the inventory, a dialog, or a menu they do nothing, except `Tab`, which
closes the inventory. `Esc` and a right click close the automap, the cheat screen, and the options menu.

### Quick save and quick load

In advanced mode, `F5` saves to one quick slot (`game/DATA/QUICK.DAT`) and `F9` loads it, without a dialog. The
message area says what happened: `GAME SAVED`, `GAME LOADED`, `NO QUICK SAVE`, or `QUICK SAVE DAMAGED`.
`UNABLE TO SAVE` means that the game could not write a file: after `F5` the earlier quick save stays, and after `F9`
the game does not load. The quick slot does not appear in the save menu, and RESTART GAME after the party's death
loads only the normal slots.

### Automap

`Tab` or `M` shows the squares of the current level that the party has seen, with north up and the party in the
middle (the red frame; its white side shows where the party faces). The top line gives the level, the position,
and the direction. Drag the map with the mouse to scroll it.

The map shows walls, floors, pits, stairs, doors, ornaments, teleporters, pressure plates, and the creatures that do
not move. A fake wall that you can walk through has a gray checkerboard. A white line marks the edge of the level.
In low light the party sees fewer squares, so fewer squares appear on the map.

### Cheats

The cheat screen (`C`) switches six cheats on and off. A save keeps them, and a new game turns them off.

- **Full light:** you see the dungeon as in full light. Creatures still see the real light.
- **Immortal:** a champion keeps at least 1 health.
- **Endless food and water:** food and water do not go down.
- **One-hit kill:** every hit kills, except creatures that cannot be hurt.
- **Walk through walls:** the party walks through walls, fake walls, and closed doors, but not through creatures or
  off the map.
- **Reveal map:** the automap shows the whole level.

### Cheat screen

Click a row, or choose it with the arrow keys (or `W`, `S`) and Return. `C` or `Esc` closes the screen.

- The six cheat rows show `ON` or `OFF`.
- QUICK SAVE F5 and QUICK LOAD F9 do the same as the keys.
- SPELLS lists all the game's spells with their runes, for example `FUL IR (X,4,4) FIREBALL`. A number is the
  rune's position in its row; `X` is any power. The spell names come from this port because the game has no names
  for them.

Click a spell to cast it at full power, with no mana cost and no chance of failure. The champion with the open spell
panel casts it, otherwise the leader. A potion spell still needs an empty flask in a hand.

### Options menu

`Esc` opens the options menu. FILTER switches the pixel-art filter (`NONE`, `SCALE2X`, `SCALE3X`), and the change
shows immediately. CONTINUE closes the menu. Sound and music volume stay in the original game's volume dialog (the
speaker button in the inventory).

The game remembers the filter in `game/DATA/SETTINGS.TXT` and uses it on the next startup. `--filter` overrides it.
If the file cannot be written, the message area shows `UNABLE TO SAVE OPTIONS`. The file is described in [Save files](formats/save.md#settingstxt).
