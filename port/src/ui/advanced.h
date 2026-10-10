#ifndef ADVANCED_H
#define ADVANCED_H
#include <stdint.h>
#include "game.h"

/* Prefixes: advanced_ */

/* PC scan codes of the quick save and quick load keys. */
enum { KEY_QUICK_SAVE = 0x3f, KEY_QUICK_LOAD = 0x43 };   /* F5, F9 */
/* PC scan codes of the keys that open the automap. */
enum { KEY_AUTOMAP = 0x0f, KEY_AUTOMAP_M = 0x32 };      /* Tab, M */
/* PC scan codes of the numeric keypad keys that move and turn the party. */
enum {
    KEYPAD_TURN_LEFT = 0x4b, KEYPAD_FORWARD = 0x4c, KEYPAD_TURN_RIGHT = 0x4d, KEYPAD_LEFT = 0x4f, KEYPAD_BACK = 0x50,
    KEYPAD_RIGHT = 0x51
};

extern int advanced_automap_open;       /* 1: the automap covers the viewport and the game time stops */

/* The keypad key that a letter key W S A D Q E stands for in the advanced mode; 0 for other keys and in vanilla. */
uint16_t advanced_movement_key(uint16_t key);
/* Writes the game to <data>/DATA/QUICK.DAT and prints the result in the message area. The saved-game state of the
   session (the QUIT question, RESTART) does not change. */
void advanced_quick_save(void);
/* Loads <data>/DATA/QUICK.DAT without a dialog and prints the result. A missing or damaged file keeps the game. */
void advanced_quick_load(void);
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

#endif
