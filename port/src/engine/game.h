#ifndef GAME_H
#define GAME_H
#include <stdint.h>

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
