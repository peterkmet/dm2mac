/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_SESSION_H
#define DM2_UI_SESSION_H
/* Prefixes: session_ args_ menu_ */

/* The start menu, new games, loads and saves through the slot dialogs, and the end of the game. */

#include <stdint.h>
#include "champion.h"
#include "game.h"
#include "save.h"

#define MENU_WAITING 99                 /* session.new_game while the start menu waits */
#define MENU_REDRAW 0xda                /* session.new_game: the menu shows its title again */
#define SAVE_SLOT_COUNT 10
/* Exit codes of the program (the original system_shutdown): the launcher of the DOS game reads them. */
enum { EXIT_QUIT = 0, EXIT_COMPLETED = 1, EXIT_PARTY_DEAD = 2, EXIT_RESTART = 3, EXIT_GAME_LOST = 4 };

/* The path of slot in the save folder; bak: the backup file. */
char *session_save_path(char *path, int slot, int bak);
/* The start menu: waits until the player chooses a new game or a load. */
void menu_run(void);
/* The title picture of the start menu on the screen. */
void menu_draw_title(void);
/* The picture of the menu until a click or a time out. */
void menu_show_picture_and_wait(void);
/* Loads the game that session.new_game names: DUNGEON.DAT or a slot that the player chooses. Returns 1 when a game
   is ready, -1 when the player cancelled or the load failed. */
int session_load(void);
/* The slot dialogs: the header of each slot (an empty slot gets the name "n UNUSED"); the dialog id (0x80 load,
   0x81 save) with its button count and the slot headers; the slot names with the chosen one (SAVE_SLOT_NONE: none)
   lit; the name field with its background and the cursor while the player types. */
void session_list_slots(void);
void session_open_slot_dialog(int id, int buttons);
void session_close_slot_dialog(void);
void session_draw_slot_selection(int chosen);
void session_draw_name_entry(const char *name, int cursor);
/* The save dialog of the game: save into a slot, quit, restart, or resume. */
void session_save(void);
/* The start of a run: the menu until a game loads, then the session of that game. Returns 0 when the player quit. */
int session_init(void);

/* Starts a new game from DUNGEON.DAT at once, as the menu choice NEW GAME does, without the menu. Returns 0 when the
   dungeon does not load. */
int session_start_new_game(void);
/* Loads the save file at path and starts its game at once, with the party view and light of a menu load. Returns 0
   when the file does not load. */
int session_start_save(const char *path);
/* The end of a game: the party died (party_dead 1) or the player quit (0). Offers the load of the last slot; returns
   when a game loaded again, else quits. */
void session_end(int party_dead);
/* The party finished the game: the screen goes black and the program ends; the turn-based mode only sets
   session.completed. */
void session_complete(void);
/* Ends the program with the exit code of the session. */
void session_quit(void);
/* The player edits the name and the title of a champion in the inventory. */
void session_edit_champion_name(Champion *c);

/* Removes every "--save PATH" pair from argv, walking it like the option parser (--mode, --saves and the other options stay);
   returns the new argc. */
int args_drop_save(int argc, char **argv);

#endif /* DM2_UI_SESSION_H */
