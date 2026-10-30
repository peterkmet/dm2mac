# Player manual

DM2Mac plays Dungeon Master II as the original PC game does. This manual covers only what is new: how to start
the game, the two game modes, and the extras of the advanced mode. For the game itself, use the manual of the
original game. The items and their values are in [items.md](reference/items.md).

## Run the game

Install the game as the [README](../README.md#play) says, or [build it](development.md) and start `build/dm2`
(`build/dm2.exe` on Windows) from the folder that holds your copy of the game.

On first startup the game copies the 10 files it needs from your folder, ZIP archive, or CD image into `game/`
([Game files and install](formats/game-files.md)). On Windows, the messages of the game go to `game/dm2.log`.

## Options

You need these only when you start the game from Terminal or a shortcut:

| Option | What it does |
|---|---|
| `--mode vanilla`, `--mode advanced` | Starts in that mode and skips the mode screen. `--mode turnbased` lets a program play ([LLM mode](llm/llm-mode.md), macOS only). |
| `--save PATH` | Loads a save and starts it immediately, in the advanced mode unless `--mode` says otherwise. |
| `--filter NAME` | The pixel-art filter of the window: `none`, `scale2x`, or `scale3x`. The `DM2_FILTER` environment variable does the same. |
| `--data DIR` | Uses the game files in `DIR` instead of `game/`, without the check and the install. The saves are in `DIR/DATA`. |
| `--saves DIR` | Keeps the quick save and `SETTINGS.TXT` in `DIR` instead of `game/DATA`. The folder must exist. |
| `--control PATH` | Opens a control socket for a program (macOS only). |

```sh
build/dm2 --mode vanilla
build/dm2 --save game/DATA/SKSAVE0.DAT
```

A wrong option prints a message that says what is wrong. You can resize the window to any size.

## Start screens

Without `--mode` and `--save`, the game first asks for the mode: VANILLA or ADVANCED. Choose with the arrow keys and
Return, or click. Then the intro movies play (click to skip one), and the start menu of the original game follows.
RESTART in the save menu keeps the mode.

## Modes

| Mode | What you get |
|---|---|
| `vanilla` | The original game, nothing added. |
| `advanced` | The default. The original game with the [advanced features](#advanced-features). |
| `turnbased` | A program plays the game turn by turn ([LLM mode](llm/llm-mode.md)). macOS only. |

The game rules are the same in all modes, except the cheats and the few changes under
[Advanced features](#advanced-features).

## Controls

The mouse, the arrow keys, and the numeric keypad work as in the original game. Keys that are easy to miss:

| Key | Action |
|---|---|
| `1` to `4` | Open the inventory of a champion |
| Shift+Left, Shift+Right | Step sideways |
| Alt+S (Option+S on a Mac) | Save menu |
| `Esc` | Pause (in the advanced mode: the options menu) |

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

The keys work while the party is in the dungeon view. In the inventory, a dialog, or a menu they do nothing. `Esc`
and a right click close the automap, the cheat screen, and the options menu.

## Saved games

Save in a slot with Alt+S, and load a slot from the start menu, as in the original game. The slots are in
`game/DATA`.

### Quick save and quick load

In the advanced mode, `F5` saves to one quick slot (`game/DATA/QUICK.DAT`) and `F9` loads it, without a dialog. The
message area says what happened: `GAME SAVED`, `GAME LOADED`, `NO QUICK SAVE`, or `QUICK SAVE DAMAGED`.
`UNABLE TO SAVE` means that the game could not write a file: an old quick save stays, and a quick load does not
happen. The quick slot does not show in the save menu, and RESTART GAME after the death of the party loads only
the normal slots.

## Advanced features

The advanced mode adds the advanced keys, the quick save, the automap, the cheats, and the options menu. It also
changes a few things:

- A held key stops the party when you release it. In the vanilla mode the party walks on, as in the original.
- An antidote always cures the poison. In the vanilla mode it can fail, as in the original.
- A click on the status box of a champion puts the object in your hand into the inventory of that champion, or
  opens the inventory when your hand is empty.
- The star of a hit stays a moment longer in the action area. A click ends it.
- Projectiles fly smoothly from square to square in the dungeon view.
- A save keeps the automap and the cheats.

The automap, the cheat screen, and the options menu stop the game time while they are open. The exact rules are in
[Advanced mode](rules/advanced-mode.md).

### Automap

`Tab` or `M` shows the squares of the current level that the party has seen, with north up and the party in the
middle (the red frame; its white side is where the party looks). The line at the top gives the level, the position,
and the direction. Drag the map with the mouse to scroll it.

The map shows walls, floors, pits, stairs, doors, ornaments, teleporters, pressure plates, and the creatures that do
not move. A fake wall that you can walk through has a gray checkerboard. A white line marks the edge of the level.
In low light the party sees fewer squares, so fewer squares get on the map.

### Cheats

The cheat screen (`C`) switches six cheats on and off. A save keeps them, and a new game turns them off.

- **Full light:** you see the dungeon as in full light. The creatures still see the real light.
- **Immortal:** a champion keeps at least 1 health.
- **Food and water:** food and water do not go down.
- **One-hit kill:** every hit kills, except on creatures that cannot be hurt.
- **Walk through walls:** the party walks through walls, fake walls, and closed doors, but not through creatures or
  off the map.
- **Reveal map:** the automap shows the whole level.

### Cheat screen

Click a row, or choose it with the arrow keys (or `W`, `S`) and Return. `C` or `Esc` closes the screen.

- The six cheat rows show `ON` or `OFF`.
- QUICK SAVE F5 and QUICK LOAD F9 do the same as the keys.
- SPELLS lists all spells of the game with their runes, for example `FUL IR (X,4,4) FIREBALL`. A number is the
  position of the rune in its row; `X` is any power. The spell names come from this port, because the game has no
  names for them.

Click a spell to cast it at full power, without mana and without a failure. The caster is the champion with the open
spell panel, otherwise the leader. A potion spell still needs an empty flask in a hand.

### Options menu

`Esc` opens the options menu. FILTER switches the pixel-art filter (`NONE`, `SCALE2X`, `SCALE3X`), and the change
shows immediately. CONTINUE closes the menu. The sound and music volume stay in the volume dialog of the original
game (the speaker button in the inventory).

#### SETTINGS.TXT

The game remembers the filter in `game/DATA/SETTINGS.TXT` and uses it on the next start. `--filter` overrides it.
When the file cannot be written, the message area shows `UNABLE TO SAVE OPTIONS`.
