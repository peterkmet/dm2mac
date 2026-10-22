/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_SESSION_H
#define DM2_UI_SESSION_H
/* Prefixes: session_ args_ menu_ */

/* The start menu, new games, loads and saves through the slot dialogs, and the end of the game. */

#include "champion.h"
#include <stdbool.h>

/* g_session.new_game while the start menu waits. */
#define MENU_WAITING 99

/* g_session.new_game when the start menu shows its title again: the command of the menu picture. */
#define MENU_REDRAW 0xda

/* The save slots of the slot dialogs. */
#define SAVE_SLOT_COUNT 10

/* Exit codes of the program (the original system_shutdown): the launcher of the DOS game reads them. */
#define EXIT_QUIT       0
#define EXIT_COMPLETED  1
#define EXIT_PARTY_DEAD 2
#define EXIT_RESTART    3
#define EXIT_GAME_LOST  4

/* ---- Start menu ---- */
/* The title picture of the start menu on the screen. */
void menu_draw_title(void); /* exported for the tests */
/* The picture of the menu until a click or a time out. */
void menu_show_picture_and_wait(void);

/* ---- Slot dialogs ---- */
/* The slot names of the slot dialog with the chosen one (SAVE_SLOT_NONE: none) lit. */
void session_draw_slot_selection(int chosen); /* exported for the tests */
/* The name field of the save dialog with its background, and the cursor while the player types. */
void session_draw_name_entry(const char *name, int cursor); /* exported for the tests */
/* Opens the slot dialog id (0x80 load, 0x81 save) with its button count and the slot headers (an empty slot gets the
   name "n UNUSED"). */
void session_open_slot_dialog(int id, int buttons); /* exported for the tests */
/* Closes the slot dialog. */
void session_close_slot_dialog(void); /* exported for the tests */

/* ---- Load and save ---- */
/* Loads the game that g_session.new_game names: DUNGEON.DAT or a slot that the player chooses. Returns true when a game
   is ready, false when the player cancelled or the load failed. */
bool session_load(void); /* exported for the tests */
/* The save dialog of the game: save into a slot, quit, restart, or resume. */
void session_save(void);
/* The start of a run: the menu until a game loads, then the session of that game. Returns false when the player
   quit. */
bool session_init(void);
/* Starts a new game from DUNGEON.DAT at once, as the menu choice NEW GAME does, without the menu. Returns false when
   the dungeon does not load. */
bool session_start_new_game(void);
/* Loads the save file at path and starts its game at once, with the party view and light of a menu load. Returns
   false when the file does not load. */
bool session_start_save(const char *path);

/* ---- End of the game ---- */
/* Ends the program with the exit code of the session. */
void session_quit(void);
/* The party finished the game: the screen goes black and the program ends; the turn-based mode only sets
   g_session.completed. */
void session_complete(void);
/* The end of a game: the party died (party_dead 1) or the player quit (0). Offers the load of the last slot; returns
   when a game loaded again, else quits. */
void session_end(int party_dead);

/* ---- Champion names ---- */
/* The player edits the name and the title of a champion in the inventory. */
void session_edit_champion_name(Champion *c);

/* ---- Arguments ---- */
/* Removes every "--save PATH" pair from argv, walking it like the option parser (--mode, --saves and the other options stay);
   returns the new argc. */
int args_drop_save(int argc, char **argv);

#endif /* DM2_UI_SESSION_H */
