/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_GAME_H
#define DM2_ENGINE_GAME_H
/* Prefixes: game_ */

/* The main loop state of the game rules: the step timing, the map changes, and the timeline events. */

#include <stdbool.h>
#include <stdint.h>
#include "save.h"

/* The timing of the main loop. */
#define STEP_TICKS 8 /* timer ticks (60 Hz) of a game step */

/* Main loop state. */
typedef struct {
    int tick_threshold;   /* timer ticks per game step */
    int tick_count;       /* timer ticks since the step started */
    uint32_t timer_ticks; /* all timer ticks */
    bool running;         /* false while the game is paused */
    bool view_hidden;     /* the viewport shows no dungeon (pause, sleep) */
    bool end;             /* the party died or the player quit: the loop stops */
    bool quit;            /* the program ends (session_quit) */
    bool closed;          /* the platform ended the program (closed window, control quit): no end movies */
} MainLoop;

extern MainLoop g_main_loop;

/* Interface state that the drawing reads. */
typedef struct {
    int16_t inventory;         /* champion ordinal (champion + 1) of the open inventory, 0 none */
    uint16_t candidate;        /* champion ordinal of a champion that is being recruited, 0 none */
    bool screen_needs_refresh; /* the next drawing of the champion states redraws the whole open inventory */
    uint16_t buttons;          /* inventory buttons that show pressed */
    bool eye;                  /* the eye of the inventory is held */
    bool mouth;                /* the mouth of the inventory is held */
    bool push_mode;            /* the movement arrows show the push arrows */
    int16_t panel_mode;        /* what the inventory panel shows: a PanelMode */
} UiState;

/* What the inventory panel shows (g_ui.panel_mode). */
enum PanelMode {
    PANEL_MODE_NONE = 0,
    PANEL_MODE_FOOD = 1,
    PANEL_MODE_SKILLS = 2,
    PANEL_MODE_OBJECT = 3,
    PANEL_MODE_SCROLL = 5,
    PANEL_MODE_MOUTH = 7,
    PANEL_MODE_NAME = 8 /* the name and title fields while a name changes */
};

extern UiState g_ui;

/* Input modes: the root of the input tree that maps clicks and keys to commands. */
enum InputMode {
    INPUT_MODE_MENU,
    INPUT_MODE_PICTURE,
    INPUT_MODE_SLEEP,
    INPUT_MODE_PAUSE,
    INPUT_MODE_DIALOG,
    INPUT_MODE_MOVE,
    INPUT_MODE_PUSH,
    INPUT_MODE_RECRUIT,
    INPUT_MODE_INVENTORY,
    INPUT_MODE_NAME,
    INPUT_MODE_COUNT /* the number of input modes */
};
/* The last command stops the command processing of this tick. */
extern bool g_input_command_stop;
/* A champion portrait or an object is dragged, 0 none. */
extern int g_input_drag_state;
/* Mouse event types: buttons (bit 1 left, bit 0 right), a released left button (only while the eye or the mouth is
   held or the automap is open), a key. */
#define INPUT_BUTTON_RIGHT  0x01
#define INPUT_BUTTON_LEFT   0x02
#define INPUT_LEFT_RELEASED 0x04
#define INPUT_KEY           0x20
/* The mouse buttons that are down: INPUT_BUTTON_* bits. */
extern int g_input_mouse_buttons;

/* The save slot and the sizes of the session paths. */
#define SAVE_SLOT_NONE (-1)
#define SAVE_DIR_SIZE  1024 /* a folder of the session: data_dir, save_dir */
#define SAVE_FILE_SIZE 28   /* the longest part after the folder: "/DATA/SKSAVE<int>.BAK" with an 11-character int */
#define SAVE_PATH_SIZE (SAVE_DIR_SIZE + SAVE_FILE_SIZE) /* a folder and its longest file part, without cutting */
typedef struct {
    int new_game;       /* 1: the next session_load starts DUNGEON.DAT; 0: it loads a slot */
    bool use_dungenb;   /* the new game reads DUNGENB.DAT when it exists */
    bool loaded;        /* a game is in memory: the end of the game can load the last slot again */
    bool completed;     /* the party finished the game */
    bool restart;       /* the player chose to restart the program */
    bool offer_restore; /* the end of the game offers the load of the last slot */
    int quit_code;      /* the exit code that session_quit gives a non-zero value: the party died */
    int slot;           /* the slot of the last load or save, SAVE_SLOT_NONE */
    uint32_t load_time; /* the game time of the last load */
    uint32_t save_time; /* the game time of the last save */
    int exit_code;
    char save_name[SAVE_NAME_SIZE]; /* the name of the slot in the save dialog */
    char data_dir[SAVE_DIR_SIZE];   /* the game folder: DATA holds the dungeon and the saves */
    char save_dir[SAVE_DIR_SIZE];   /* the folder of QUICK.DAT (--saves); empty: <data_dir>/DATA */
} Session;
extern Session g_session;

/* The game modes: vanilla is the original game; advanced adds the W A S D Q E keys, the quick save (F5) and quick
   load (F9), the automap (Tab or M), and the cheats; turnbased plays turn by turn over the control socket, with the
   seen squares and the AMAP block of advanced, without its keys and cheats. Without the cheats the game rules are the
   same in all modes. */
enum GameMode { GAME_MODE_VANILLA, GAME_MODE_ADVANCED, GAME_MODE_TURNBASED, GAME_MODE_COUNT };
extern enum GameMode g_game_mode; /* GAME_MODE_VANILLA in the library; main.c sets the --mode value */
extern const char *const g_game_mode_names[GAME_MODE_COUNT];
/* The cheat bits of the advanced mode, as the CHTS block of the save file holds them (game_get_cheats). A new game
   turns them off; a load takes them from the save file. */
#define CHEAT_LIGHT    0x01 /* the dungeon view draws in full light; the creatures keep the real light */
#define CHEAT_IMMORTAL 0x02 /* the health of a champion stops at 1 */
#define CHEAT_FOOD     0x04 /* food and water do not go down */
#define CHEAT_KILL     0x08 /* a damage above 0 kills a creature that can be hurt */
#define CHEAT_WALLS    0x10 /* the party walks into walls, closed fake walls, and closed doors inside the map */
#define CHEAT_MAP      0x20 /* the automap shows every square of the map */
#define CHEAT_ALL      0x3f

/* The party enters map at (x, y); leaving: the old map gets its exit sensors first. */
void game_enter_map(int16_t x, int16_t y, int16_t map, bool leaving);
/* Runs the due timeline events. */
void game_process_timeline(void);

/* The game step */
/* The party arrives on the map that it entered in this step (g_party.new_map): the walk ends there. */
void game_enter_new_map(void);
/* The haste of the party runs down; the champion clocks run each 64 game steps (each 16 while the party sleeps). */
void game_run_party_clocks(void);
/* The game time goes on one step: held lights burn down, the action effect and the cooldowns run down. */
void game_advance_time(void);
/* Adds 1 to the global byte variable index (GLOBAL_BYTE_FIRST and up), up to GLOBAL_BYTE_MAX. */
void game_global_byte_add(int index);

/* The main loop */
/* The party is awake and pushes nothing; the main loop runs game steps at the normal speed. */
void game_start_play(void);
/* The game runs (on) or pauses. */
void game_set_running(bool on);
/* The viewport shows no dungeon (on: pause, sleep) or the dungeon. */
void game_set_view_hidden(bool on);
/* The timer ticks of a game step. */
void game_set_step_ticks(uint16_t ticks);
/* One timer tick (60 Hz) passes. */
void game_count_timer_tick(void);
/* One timer tick of the game step passes; true when the step is due. */
bool game_count_step_tick(void);
/* A new game step starts: no timer ticks of it passed. */
void game_reset_step_ticks(void);
/* A loaded or a new game runs: the end of the last game is over. */
void game_clear_end(void);
/* The program ends. */
void game_request_quit(void);

/* The interface state */
/* No inventory is open, nothing is held or pressed. */
void game_clear_ui_state(void);
/* The next drawing of the champion states redraws the whole open inventory (on). */
void game_set_screen_refresh(bool on);
/* The inventory buttons that show pressed. */
void game_set_inventory_buttons(uint16_t buttons);
/* What the inventory panel shows. */
void game_set_panel_mode(enum PanelMode mode);
/* The champion ordinal (champion + 1) of the open inventory, 0 none. */
void game_set_open_inventory(int inventory);
/* The eye of the inventory is held (on) or released. */
void game_set_eye_held(bool on);
/* The eye and the mouth of the inventory are released. */
void game_release_eye_and_mouth(void);

/* The input state */
/* The last command stops the command processing of this tick (on). */
void game_set_command_stop(bool on);
/* No champion portrait or object is dragged. */
void game_clear_drag(void);
/* The mouse buttons that are down: INPUT_BUTTON_* bits. */
void game_set_mouse_buttons(int pressed);

/* The session */
/* What the next session_load does: 1 starts DUNGEON.DAT, 0 loads a slot (or a start menu state). */
void game_set_new_game(int choice);
/* The new game reads DUNGENB.DAT when it exists (on). */
void game_set_dungenb(bool on);
/* A game is in memory (on): the end of the game can load the last slot again. */
void game_set_session_loaded(bool on);
/* The slot of the last load or save, SAVE_SLOT_NONE. */
void game_set_save_slot(int16_t slot);
/* A slot was loaded: the load time is now, a game is in memory. */
void game_note_slot_loaded(void);
/* A slot was saved: the save time is now. */
void game_note_slot_saved(void);
/* No load and no save count for the question at quit. */
void game_clear_save_times(void);
/* The player chose to restart the program (on). */
void game_set_restart(bool on);
/* The end of the game offers the load of the last slot (on). */
void game_set_offer_restore(bool on);
/* The party finished the game (on). */
void game_set_completed(bool on);
/* The exit code that session_quit gives a non-zero value: the party died. */
void game_set_quit_code(int code);
/* The exit code of the program. */
void game_set_exit_code(int code);
/* The session is again as session was (a copy from before a quick load). */
void game_restore_session(const Session *session);

/* The cheats */
/* The cheat bits that are on (CHEAT_*). */
uint8_t game_get_cheats(void);
/* Only the cheats of the bits cheats are on. */
void game_set_cheats(uint8_t cheats);
/* The cheat bit cheat switches on or off. */
void game_toggle_cheat(uint8_t cheat);
/* True when the cheat bit cheat is on. */
bool game_has_cheat(uint8_t cheat);

#endif /* DM2_ENGINE_GAME_H */
