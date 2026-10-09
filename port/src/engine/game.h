#ifndef GAME_H
#define GAME_H
#include <stdint.h>
#include "save.h"

/* Prefixes: game_ */

#define STEP_TICKS 8        /* timer ticks (60 Hz) of a game step */
#define SLEEP_STEP_TICKS 1  /* a sleeping party runs a step on each tick */
#define TEXT_SLEEPING 0x11  /* GDAT text (1, 0, 0x11): the sleep message */
#define TEXT_PAUSED 0x12    /* GDAT text (1, 0, 0x12): the pause message */
#define ZONE_VIEW_TEXT 6    /* the layout zone of a message in the viewport */

/* Main loop state. */
typedef struct {
    int tick_threshold;     /* timer ticks per game step */
    int tick_count;         /* timer ticks since the step started */
    uint32_t timer_ticks;   /* all timer ticks */
    int running;            /* 0 while the game is paused */
    int view_hidden;        /* the viewport shows no dungeon (pause, sleep) */
    int end;                /* the party died or the player quit: the loop stops (g_game_end_flag) */
    int quit;               /* the program ends (session_quit) */
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

/* The last command stops the command processing of this tick (the original g_command_stop). */
extern int input_command_stop;
/* A champion portrait or an object is dragged (Step 15), 0 none. */
extern int input_drag_state;
/* The mouse buttons that are down: INPUT_BUTTON_* bits (the original mouse_get_state). */
extern int input_mouse_buttons;

#define SAVE_SLOT_NONE (-1)
#define SAVE_PATH_SIZE 1024
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
    char data_dir[SAVE_PATH_SIZE];  /* the game folder: DATA holds the dungeon and the saves */
} Session;
extern Session session;

/* The game modes: vanilla is the original game; advanced adds the W A S D Q E keys, the quick save (F5) and quick
   load (F9), the automap (Tab or M), and its cheats. Without the cheats the game rules are the same in both modes. */
enum { GAME_MODE_VANILLA, GAME_MODE_ADVANCED, GAME_MODE_COUNT };
extern int game_mode;                   /* GAME_MODE_VANILLA in the library; main.c sets the --mode value */
extern const char *const game_mode_names[GAME_MODE_COUNT];
/* The cheats, switched on the automap. Not saved; a new or loaded game turns them off. */
extern int cheat_light;                 /* the dungeon view draws in full light; the creatures keep the real light */
extern int cheat_immortal;              /* the health of a champion stops at 1 */
extern int cheat_food;                  /* food and water do not go down */

/* One timer tick of a modal loop (menu, dialog): the platform reads the input and shows the screen. */
extern void (*game_frame_hook)(void);
void game_wait_frames(int count);
/* Prepares a loaded game for play: input mode, map, screen (the original game_start_session). */
void game_start_session(void);
/* The party enters map at (x, y); leaving: the old map gets its exit sensors first. */
void game_enter_map(int x, int y, int map, int leaving);
/* One timer tick of the main loop: input, commands, and at the end of a step the next game step. */
void game_tick(void);
/* Shows the dungeon view again after a pause, sleep, or the inventory. */
void game_show_view(void);
/* Hides the dungeon view: the viewport shows a message (pause, sleep) until game_show_view. */
void game_hide_view(void);
/* Copies the view picture to the screen. */
void game_present_viewport(void);
/* The party falls asleep. */
void game_sleep(void);

#endif
