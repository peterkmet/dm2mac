#ifndef GAME_H
#define GAME_H
#include <stdint.h>

#define STEP_TICKS 8        /* timer ticks (60 Hz) of a game step */
#define SLEEP_STEP_TICKS 1  /* a sleeping party runs a step on each tick */

/* Main loop state. */
typedef struct {
    int tick_threshold;     /* timer ticks per game step */
    int tick_count;         /* timer ticks since the step started */
    uint32_t timer_ticks;   /* all timer ticks */
    int running;            /* 0 while the game is paused */
    int view_hidden;        /* the viewport shows no dungeon (pause, sleep) */
    int end;                /* the game ended: the loop stops */
} MainLoop;

extern MainLoop main_loop;

/* Prepares a loaded game for play: input mode, map, screen (the original game_start_session). */
void game_start_session(void);
/* The party enters map at (x, y); leaving: the old map gets its exit sensors first. */
void game_enter_map(int x, int y, int map, int leaving);
/* Runs the due timeline events. */
void timeline_process(void);
/* One timer tick of the main loop: input, commands, and at the end of a step the next game step. */
void game_tick(void);
/* Shows the dungeon view again after a pause, sleep, or the inventory. */
void ui_show_game_view(void);
void ui_hide_game_view(void);
/* Copies the view picture to the screen. */
void screen_present_viewport(void);
/* The party wakes up. */
void champ_wake_party(void);
/* The party falls asleep. */
void game_sleep(void);

#endif
