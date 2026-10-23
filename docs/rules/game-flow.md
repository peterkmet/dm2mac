# Start menu, save and load, dialogs, and end of game

The rules of the game flow: the start of a run, the save and the load, the dialogs, and the end of the game. The code is in `src/ui/session.c`, `src/ui/dialog.c`, `src/engine/dungeon.c`, and `src/engine/save.c`. The save file is in [save.md](../formats/save.md). The addresses and the notation follow the conventions of [skull-map.md](../reference/skull-map.md).

## Start of a run

- `session_init` shows the start menu (`run_menu`, input mode 0) until the player chooses a game, then runs `session_load` until it returns true. The menu commands: `0xd7` new game, `0xd8` new game from `DUNGENB.DAT` (the file does not exist: `DUNGEON.DAT`), `0xd9` load, `0xda` the picture GDAT `(5, 0, 1, 1)` for up to 1800 vertical retraces or until a click (command `0xef` of input mode 1), `0xe0` quit. The title is GDAT `(5, 0, 1, 4)` with its 16 colors in screen zone 2.
- A new game reads `DUNGEON.DAT` with free records ([save.md](../formats/save.md#container)), puts the party on the start square of the header (map 0), starts the timeline and the weather, and runs the link pass (`dungeon_link_things_after_load`):
  - A teleporter whose target is a teleporter that leads back: the link sensors (`0x27`) of both squares without a number get the map + 1. With two numbered sensors both squares share one thing list: the fixed things of the target square go in reverse order in front of the list of the source square, and the first movable thing of the target square goes to its end; the movable things after it drop out.
  - Level link marks (square bit 1): the square in front of a text ornament of subtype 6 or 7 and of a sensor `0x1c` (cell direction of the thing); the square one level down below a pit, unless the wall set value `(8, set, 0x0b, 0x6a)` is not 0.
  - Start states: text ornaments of subtype 8 are visible; sensors `0x27` and `0x2c` that are on become busy; sensors `0x3f` are on and not busy; a sensor `0x46` copies its state to bit 13 of the door at its target.
  - Creature groups: full health; a moving group gets its square as home; a static group clears its animation words and gives its possessions random cells (one random number for each link), unless bit 7 of byte `0x0e` is set.
- `dungeon_read_level_table` reads the first text (kind 0) on the party start square of map 0 into 64 bytes filled with `'1'` and takes one digit per level, skipping end marks, new lines, and spaces. The minion map of a level shows the icons of this set (`dungeon_level_digit`). It runs at the first dungeon load of a run, also when that load is a save.
- `sensor_start_timers` (only for a new game): each timer sensor (`0x1e`, `0x33` to `0x37`) that is on starts its timer event; the others lose the busy bit. All 18 timers of `DUNGEON.DAT` are off.
- The champion of a new game: square (0, 0) of map 0 holds a mirror sensor (`0x7e`); `champ_init_party_state` recruits its champion with the commands of a join (`recruit_start`, then `recruit_finish` with command `0x92`) while the recruit flag `0x75a29` is set: the join opens no inventory, draws no mouth, plays no sound, and prints no message. The objects on the square in front of the mirror go into the slots. Then the champion faces the party direction, takes the party direction as its cell, and becomes the leader. The leader hand is empty.

## Save and load

- Saves are `<data>/DATA/SKSAVEn.DAT` (slots 0 to 9) and `.BAK`, as the DOS game finds them. The slot dialogs (`0x80` load, `0x81` save) list the 42-byte header of each slot; an empty slot shows "n UNUSED" and has `0xdeadbeef` at byte `0x26`. A click in the list chooses slot (y - list top) / 7. The save dialog names an empty slot "GAME n"; a click on the name field starts the typing of up to 31 characters (letters become capitals, Backspace, Enter ends).
- The save menu (dialog `0x1b`): SAVE, QUIT (the end picture GDAT `(6, 0, 1, 1)` until a click), RESTART (the program starts again: exit code 3), RESUME. A quit more than 100 game steps after the last load and the last save first asks dialog `0x0c`.
- A load restores the timed effects of the save (`restore_timed_effects`, [champions.md](champions.md#the-leader-hand)). The start of its session puts the saved object back into the leader hand (`champ_init_party_state`) with the leader index -1, so the object adds no item effects; its name shows in the message area.
- The load relinks the damage display event of each champion (`relink_events`): `0xffff`, or the index of the queued hide damage event (`0x0c`) of that champion.
- A load of a missing slot file takes the `.BAK` file and renames it back; a damaged file shows "THE GAME IS DAMAGED!" and ends the program when no game is in memory. After a load, "LOADING GAME..." stays until the session starts, and `play_start_session` shows "GAME LOADED, READY TO PLAY".
- When the records of a thing type run out, `dun_get_discarded_thing` frees one. It searches the maps from the map after the last search, the party map last, and a second time with one item of each shop display kind kept. It takes a thing of the type without the "do not discard" bit, not within 5 squares of the party on its map, and not on a square with a protecting sensor or text ornament subtype 4. It removes the thing from its square (or from the possessions of a static creature).

## Dialogs

- A dialog is GDAT `(0x1a, id)`: picture entry 0 in zone 4 (the viewport), text entries 0 to 19 the buttons (zones `0x1ce` to `0x1d3` by button count), text `0x14` the message, wrapped to a width near the message zone width and centered; an error message (dialog 0, entry `0x1b` to `0x1f`) shows below it or first in its own dialog. Screen mode 1 (at the program start, the "GAME LOADED" dialog of a load, and the end) draws on a black screen in zone 5; mode 0 in the game viewport. `game_loop` sets mode 0 at its start; `play_hide_view` and `play_show_view` do nothing in mode 1.
- `dialog_run` pushes input mode 4; the input conditions 1 and 2 select the button zones by screen mode and button count. The choice is the place of the matched command in its list. Enter clicks the first button.
- The pressed button picture is GDAT `(0x1a, 0xfe, 1, 0 or 1)` (click feedback opcodes `0x0f` and `0x10`), color 7 transparent, centered in the button.
- The volume dialog (`0x87`) sets the sound and music volumes (0 to 7, commands `0x95` to `0xa4`); a new sound volume plays the test sound `(3, 0, 0x8b)`. Both volumes are 7 in a new game and are part of the save.

## End of game

- The end sensor (wall sensor `0x12`) completes the game: the screen goes black and the program ends with exit code 1 (the DOS launcher then plays the ending).
- When the last champion dies, `kill_champion` sets the end flag (`g_game_end_flag`, `g_main_loop.end`); the game step stops before the game time goes on, and `session_end(1)` plays the scream of the first champion (`(0x16, portrait, 0x87)`, fallback `0xfe`), waits 240 retraces, and shows "THE END": RESTART GAME loads the last slot (no dialog) and the game goes on; QUIT GAME ends with exit code 2.

## Deviations from the original

- The DOS layer is not ported: disks, drives, volume labels, the disk dialogs, the memory errors, the mouse pointer counter, the video state, and the busy waits. A modal loop (menu, dialog, name editor) runs one timer tick of the platform per pass (`play_wait_frames` in `ui/play.c` calls `g_play_frame_hook`, which `platform/main.c` sets); the original loops spin. A dialog waits until the player chooses a button or the program ends; the tests choose through the frame hook (`g_play_frame_hook`).
- `session_quit` sets `g_main_loop.quit` and returns; the main loop then ends (the original exits the process immediately). After the end sensor the rest of that timeline step still runs. Exit code 3 (RESTART) starts the program again (`execvp`; a failed start prints an error). The restart drops `--save PATH`, so the program shows the start menu, as the original does.
- The drive path codes of the dialog texts (3, 4, 15) give the last folder name of the `--data` path in capitals (the font has no lowercase letters); the error, memory, and file name codes give an empty text.
- The volume setting scales the mix of the sound voices (0 is silent); the music volume goes to the music player.
- Fixes: a failed load of the last slot after the death of the party shows the error dialog (the original asks for the game disk); the link pass marks no square when no map lies below a pit (the original writes into map -1); the projectile that `dun_get_discarded_thing` frees is unlinked from its own map (the original unlinks from the current map); the name editor does not read before an empty field; slot names are bounded to 31 characters.
- Kept quirks: `dun_get_discarded_thing` returns a door, teleporter, scroll, or container record without taking it from its square; a RESTART that the question of dialog `0x0c` cancels stays set for the next quit; a cancelled load leaves slot 0.
- The `--save PATH` option starts the game of PATH immediately, without the menu and without the dialog after a load.
