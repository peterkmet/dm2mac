/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_GAME_H
#define DM2_ENGINE_GAME_H
/* Prefixes: game_ */

/* The main loop state of the game rules: the step timing, the map changes, and the party wake. */

#include <stdint.h>
#include "save.h"

#define STEP_TICKS 8        /* timer ticks (60 Hz) of a game step */

/* Main loop state. */
typedef struct {
    int tick_threshold;     /* timer ticks per game step */
    int tick_count;         /* timer ticks since the step started */
    uint32_t timer_ticks;   /* all timer ticks */
    int running;            /* 0 while the game is paused */
    int view_hidden;        /* the viewport shows no dungeon (pause, sleep) */
    int end;                /* the party died or the player quit: the loop stops (g_game_end_flag) */
    int quit;               /* the program ends (session_quit) */
    int closed;             /* the platform ended the program (closed window, control quit): no end movies */
} MainLoop;

extern MainLoop main_loop;

/* Interface state that the drawing reads. Later steps change it (Step 14: input, Step 15: champions). */
typedef struct {
    int16_t inventory;          /* champion ordinal (champion + 1) of the open inventory, 0 none */
    uint16_t candidate;         /* champion ordinal of a champion that is being recruited, 0 none */
    int16_t screen_needs_refresh;
    uint16_t buttons;           /* inventory buttons that show pressed */
    int16_t eye, mouth;         /* the eye or the mouth of the inventory is held */
    int16_t push_mode;          /* the movement arrows show the push arrows */
    int16_t panel_mode;         /* what the inventory panel shows */
} UiState;

extern UiState ui;

/* Input modes: the root of the input tree that maps clicks and keys to commands. */
enum input_mode {
    INPUT_MODE_MENU = 0, INPUT_MODE_PICTURE = 1, INPUT_MODE_SLEEP = 2, INPUT_MODE_PAUSE = 3, INPUT_MODE_DIALOG = 4,
    INPUT_MODE_MOVE = 5, INPUT_MODE_PUSH = 6, INPUT_MODE_RECRUIT = 7, INPUT_MODE_INVENTORY = 8, INPUT_MODE_NAME = 9
};
/* The last command stops the command processing of this tick (the original g_command_stop). */
extern int input_command_stop;
/* A champion portrait or an object is dragged (Step 15), 0 none. */
extern int input_drag_state;
/* Mouse event types: buttons (bit 1 left, bit 0 right), a released left button (only while the eye or the mouth is
   held or the automap is open), a key. */
enum { INPUT_BUTTON_RIGHT = 1, INPUT_BUTTON_LEFT = 2, INPUT_LEFT_RELEASED = 4, INPUT_KEY = 0x20 };
/* The mouse buttons that are down: INPUT_BUTTON_* bits (the original mouse_get_state). */
extern int input_mouse_buttons;

#define SAVE_SLOT_NONE (-1)
#define SAVE_DIR_SIZE 1024   /* a folder of the session: data_dir, save_dir */
#define SAVE_FILE_SIZE 28    /* the longest part after the folder: "/DATA/SKSAVE<int>.BAK" with an 11-character int */
#define SAVE_PATH_SIZE (SAVE_DIR_SIZE + SAVE_FILE_SIZE)  /* a folder and its longest file part, without cutting */
typedef struct {
    int new_game;               /* 1: the next session_load starts DUNGEON.DAT; 0: it loads a slot */
    int use_dungenb;            /* the new game reads DUNGENB.DAT when it exists */
    int loaded;                 /* a game is in memory: the end of the game can load the last slot again */
    int completed;              /* the party finished the game */
    int restart;                /* the player chose to restart the program */
    int offer_restore;          /* the end of the game offers the load of the last slot */
    int quit_code;              /* the exit code that session_quit gives a non-zero value: the party died */
    int slot;                   /* the slot of the last load or save, SAVE_SLOT_NONE */
    uint32_t load_time, save_time; /* the game time of the last load and save */
    int exit_code;
    char save_name[SAVE_NAME_SIZE]; /* the name of the slot in the save dialog */
    char data_dir[SAVE_DIR_SIZE];  /* the game folder: DATA holds the dungeon and the saves */
    char save_dir[SAVE_DIR_SIZE];  /* the folder of QUICK.DAT (--saves); empty: <data_dir>/DATA */
} Session;
extern Session session;

/* The game modes: vanilla is the original game; advanced adds the W A S D Q E keys, the quick save (F5) and quick
   load (F9), the automap (Tab or M), and its cheats; turnbased plays turn by turn over the control socket, with the
   seen squares and the AMAP block of advanced, without its keys and cheats. Without the cheats the game rules are the
   same in all modes. */
enum { GAME_MODE_VANILLA, GAME_MODE_ADVANCED, GAME_MODE_TURNBASED, GAME_MODE_COUNT };
extern int game_mode;                   /* GAME_MODE_VANILLA in the library; main.c sets the --mode value */
extern const char *const game_mode_names[GAME_MODE_COUNT];
/* The cheats, switched on the automap. Not saved; a new or loaded game turns them off. */
extern int cheat_light;                 /* the dungeon view draws in full light; the creatures keep the real light */
extern int cheat_immortal;              /* the health of a champion stops at 1 */
extern int cheat_food;                  /* food and water do not go down */

/* The party enters map at (x, y); leaving: the old map gets its exit sensors first. */
void game_enter_map(int x, int y, int map, int leaving);

#endif /* DM2_ENGINE_GAME_H */
