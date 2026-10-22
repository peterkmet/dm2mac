/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_ADVANCED_H
#define DM2_UI_ADVANCED_H
/* Prefixes: advanced_ */

/* The advanced mode: quick save and load, the viewport screens (the automap, the cheat screen, and the options menu) and their keys,
   and the movement letter keys. */

#include <stdbool.h>
#include <stdint.h>

/* PC scan codes of the quick save and quick load keys. */
#define KEY_QUICK_SAVE 0x3f /* F5 */
#define KEY_QUICK_LOAD 0x43 /* F9 */

/* PC scan codes of the keys that open the automap, the cheat screen, and the options menu. */
#define KEY_AUTOMAP   0x0f        /* Tab */
#define KEY_AUTOMAP_M 0x32        /* M */
#define KEY_CHEATS    0x2e        /* C */
#define KEY_OPTIONS   SCAN_ESCAPE /* in the movement mode of the advanced mode */

/* PC scan codes of the numeric keypad keys that move and turn the party. */
#define KEYPAD_TURN_LEFT  0x4b
#define KEYPAD_FORWARD    0x4c
#define KEYPAD_TURN_RIGHT 0x4d
#define KEYPAD_LEFT       0x4f
#define KEYPAD_BACK       0x50
#define KEYPAD_RIGHT      0x51

/* PC scan codes of the letter keys that move and turn the party, and of Esc and Return on the viewport screens. */
#define SCAN_Q      0x10
#define SCAN_W      0x11
#define SCAN_E      0x12
#define SCAN_A      0x1e
#define SCAN_S      0x1f
#define SCAN_D      0x20
#define SCAN_ESCAPE 0x01
#define SCAN_RETURN 0x1c

/* The results of a quick load. */
enum QuickLoadResult { QUICK_LOADED, QUICK_MISSING, QUICK_DAMAGED, QUICK_BLOCKED, QUICK_LOST };

/* The screens that cover the viewport, one at a time; the game time stops while one is open. */
#define ADVANCED_SCREEN_NONE    0
#define ADVANCED_SCREEN_AUTOMAP 1
#define ADVANCED_SCREEN_CHEATS  2
#define ADVANCED_SCREEN_OPTIONS 3

extern int g_advanced_screen; /* the open viewport screen (ADVANCED_SCREEN_*) */

/* Setup */
/* Closes the open viewport screen: a new or loaded game starts. */
void advanced_reset(void);

/* Movement keys */
/* The keypad key that a letter key W S A D Q E stands for in the advanced mode; 0 for other keys and outside the advanced mode. */
uint16_t advanced_movement_key(uint16_t key);

/* Quick save and load */
/* Writes the game to QUICK.DAT in the save folder and prints the result in the message area. The saved-game state of
   the session (the QUIT question, RESTART) does not change. Returns true when the file was written. */
bool advanced_quick_save(void);
/* Loads QUICK.DAT of the save folder without a dialog and prints the result. A missing or damaged file keeps the game;
   when the running game cannot come back either (QUICK_LOST), the program ends outside the turn-based mode. */
enum QuickLoadResult advanced_quick_load(void);

/* The automap */
/* Opens the automap in the viewport. */
void advanced_open_automap(void);
/* Draws the automap and its readout (level, position, direction) in the viewport. */
void advanced_draw_automap(void); /* exported for the tests */
/* The mouse is at (x, y): during a drag the map follows it in whole squares. */
void advanced_automap_drag(int16_t x, int16_t y);

/* Viewport screens */
/* Opens the cheat screen in the viewport. */
void advanced_open_cheats(void);
/* Opens the options menu in the viewport. */
void advanced_open_options(void);
/* An input event while a viewport screen is open (type INPUT_KEY with the key in x, a mouse button,
   INPUT_LEFT_RELEASED). On the automap Tab, M, and Esc close it, a left drag on the viewport scrolls the map. On the
   cheat screen C and Esc close it, Up, Down, W, and S select a row, Return and a left click run a row, F5 and F9
   quick save and quick load (a quick load closes the screen first). On the options menu Esc closes it, the rest is
   optmenu.h. Other keys do nothing. */
void advanced_screen_event(int type, int16_t x, int16_t y);

#endif /* DM2_UI_ADVANCED_H */
