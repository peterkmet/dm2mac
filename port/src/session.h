#ifndef SESSION_H
#define SESSION_H
#include <stdint.h>
#include "champion.h"
#include "save.h"

/* The start menu, new games, loads and saves through the slot dialogs, and the end of the game. */
#define MENU_WAITING 99                 /* session.new_game while the start menu waits */
#define MENU_REDRAW 0xda                /* session.new_game: the menu shows its title again */
#define SAVE_SLOT_COUNT 10
#define SAVE_SLOT_NONE (-1)
#define SAVE_PATH_SIZE 1024
/* Exit codes of the program (the original system_shutdown): the launcher of the DOS game reads them. */
enum { EXIT_QUIT = 0, EXIT_COMPLETED = 1, EXIT_PARTY_DEAD = 2, EXIT_RESTART = 3 };

typedef struct {
    int new_game;               /* 1: the next game_load starts DUNGEON.DAT; 0: it loads a slot */
    int use_dungenb;            /* the new game reads DUNGENB.DAT when it exists */
    int loaded;                 /* a game is in memory: the end of the game can load the last slot again */
    int completed;              /* the party finished the game */
    int restart;                /* the player chose to restart the program */
    int offer_restore;          /* the end of the game offers the load of the last slot */
    int quit_code;              /* the exit code that game_quit gives a non-zero value: the party died */
    int slot;                   /* the slot of the last load or save, SAVE_SLOT_NONE */
    uint32_t load_time, save_time; /* the game time of the last load and save */
    int exit_code;
    char save_name[SAVE_NAME_SIZE]; /* the name of the slot in the save dialog */
    char data_dir[SAVE_PATH_SIZE];  /* the game folder: DATA holds the dungeon and the saves */
} Session;
extern Session session;

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
int game_load(void);
/* The slot dialogs: the header of each slot (an empty slot gets the name "n UNUSED"); the dialog id (0x80 load,
   0x81 save) with its button count and the slot headers; the slot names with the chosen one (SAVE_SLOT_NONE: none)
   lit; the name field with its background and the cursor while the player types. */
void save_list_slots(void);
void save_open_slot_dialog(int id, int buttons);
void save_close_slot_dialog(void);
void save_draw_slot_selection(int chosen);
void save_draw_name_entry(const char *name, int cursor);
/* The save dialog of the game: save into a slot, quit, restart, or resume. */
void game_save(void);
/* The start of a run: the menu until a game loads, then the session of that game. Returns 0 when the player quit. */
int game_init(void);
/* The end of a game: the party died (party_dead 1) or the player quit (0). Offers the load of the last slot; returns
   when a game loaded again, else quits. */
void game_end(int party_dead);
/* The party finished the game: the screen goes black and the program ends. */
void game_complete(void);
/* Ends the program with the exit code of the session. */
void game_quit(void);
/* The player edits the name and the title of a champion in the inventory. */
void champ_edit_name(Champion *c);

/* Removes every "--save PATH" pair from argv, walking it like the option parser; returns the new argc. */
int args_drop_save(int argc, char **argv);

#endif
