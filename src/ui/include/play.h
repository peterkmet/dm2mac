/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_PLAY_H
#define DM2_UI_PLAY_H
/* Prefixes: play_ */

/* The main loop: timer ticks, game steps, the view of the party and sleep. */

#define SLEEP_STEP_TICKS 1  /* a sleeping party runs a step on each tick */
#define TEXT_SLEEPING 0x11  /* GDAT text (1, 0, 0x11): the sleep message */
#define TEXT_PAUSED 0x12    /* GDAT text (1, 0, 0x12): the pause message */
#define ZONE_VIEW_TEXT 6    /* the layout zone of a message in the viewport */

/* One timer tick of a modal loop (menu, dialog): the platform reads the input and shows the screen. */
extern void (*play_frame_hook)(void);
void play_wait_frames(int count);
/* Prepares a loaded game for play: input mode, map, screen (the original game_start_session). */
void play_start_session(void);
/* One timer tick of the main loop: input, commands, and at the end of a step the next game step. */
void play_tick(void);
/* Shows the dungeon view again after a pause, sleep, or the inventory. */
void play_show_view(void);
/* Hides the dungeon view: the viewport shows a message (pause, sleep) until play_show_view. */
void play_hide_view(void);
/* Copies the view picture to the screen. */
void play_present_viewport(void);
/* The party falls asleep. */
void play_sleep(void);

#endif /* DM2_UI_PLAY_H */
