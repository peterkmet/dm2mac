/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_ADVANCED_H
#define DM2_UI_ADVANCED_H
/* Prefixes: advanced_ */

/* The advanced mode: quick save and load, the automap key and the movement letter keys. */

#include "game.h"
#include <stdint.h>

/* PC scan codes of the quick save and quick load keys. */
enum { KEY_QUICK_SAVE = 0x3f, KEY_QUICK_LOAD = 0x43 }; /* F5, F9 */
/* PC scan codes of the keys that open the automap. */
enum { KEY_AUTOMAP = 0x0f, KEY_AUTOMAP_M = 0x32 }; /* Tab, M */
/* PC scan codes of the numeric keypad keys that move and turn the party. */
enum {
    KEYPAD_TURN_LEFT = 0x4b,
    KEYPAD_FORWARD = 0x4c,
    KEYPAD_TURN_RIGHT = 0x4d,
    KEYPAD_LEFT = 0x4f,
    KEYPAD_BACK = 0x50,
    KEYPAD_RIGHT = 0x51
};

extern int advanced_automap_open; /* 1: the automap covers the viewport and the game time stops */

/* The keypad key that a letter key W S A D Q E stands for in the advanced mode; 0 for other keys and outside the advanced mode. */
uint16_t advanced_movement_key(uint16_t key);
/* The results of a quick load. */
enum { QUICK_LOADED, QUICK_MISSING, QUICK_DAMAGED, QUICK_BLOCKED, QUICK_LOST };
/* Writes the game to QUICK.DAT in the save folder and prints the result in the message area. The saved-game state of
   the session (the QUIT question, RESTART) does not change. Returns 1 when the file was written. */
int advanced_quick_save(void);
/* Loads QUICK.DAT of the save folder without a dialog and prints the result. A missing or damaged file keeps the game;
   when the running game cannot come back either (QUICK_LOST), the program ends outside the turn-based mode. */
int advanced_quick_load(void);
/* Closes the automap and turns the cheats off: a new or loaded game starts. */
void advanced_reset(void);
/* Opens the automap in the viewport. */
void advanced_open_automap(void);
/* Draws the automap and its readout (level, position, direction, cheats) in the viewport. */
void advanced_draw_automap(void);
/* An input event while the automap is open (type INPUT_KEY with the key in x, a mouse button, INPUT_LEFT_RELEASED):
   Tab, M, and Esc close it, I, F, and L switch the cheats, a left drag on the viewport scrolls the map. Other keys do
   nothing. */
void advanced_automap_event(int type, int x, int y);
/* The mouse is at (x, y): during a drag the map follows it in whole squares. */
void advanced_automap_drag(int x, int y);

#endif /* DM2_UI_ADVANCED_H */
