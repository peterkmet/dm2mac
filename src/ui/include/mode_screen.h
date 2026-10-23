/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_MODE_SCREEN_H
#define DM2_UI_MODE_SCREEN_H
/* Prefixes: mode_screen_ */

/* The mode screen of the program start: the cursor on VANILLA or ADVANCED, its keys, its clicks, and its picture.
   It does not set the game mode; platform/main.c sets the selected mode as --mode does. */

#include <stdbool.h>
#include <stdint.h>

/* The actions of the mode screen keys. */
#define MODE_SCREEN_UP    0
#define MODE_SCREEN_DOWN  1
#define MODE_SCREEN_ENTER 2

/* ---- Setup ---- */

/* Shows the screen with the cursor on ADVANCED, the default mode. */
void mode_screen_start(void);

/* ---- Queries ---- */

/* The game mode of the item at the cursor (GAME_MODE_VANILLA or GAME_MODE_ADVANCED). */
int mode_screen_cursor(void);
/* The player selected the mode at the cursor. */
bool mode_screen_is_done(void);
/* The screen rectangle of an item box; false for an item outside the list. */
bool mode_screen_item_box(int16_t item, int16_t *rect); /* exported for the tests */

/* ---- Commands ---- */

/* A key action (MODE_SCREEN_*): up and down move the cursor and stop at the list ends; enter selects. */
void mode_screen_key(int16_t action);
/* A left click at (x, y) of the screen: a click on an item box selects that item. */
void mode_screen_click(int16_t x, int16_t y);
/* Draws the screen into g_video_fb and sets the three palette colors that it uses. */
void mode_screen_draw(void);

#endif /* DM2_UI_MODE_SCREEN_H */
