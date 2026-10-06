#ifndef ADVANCED_H
#define ADVANCED_H
#include <stdint.h>

/* The game modes: vanilla is the original game; advanced adds the W A S D Q E keys and the quick save (F5) and
   quick load (F9). The game rules are the same in both modes. */
enum { GAME_MODE_VANILLA, GAME_MODE_ADVANCED, GAME_MODE_COUNT };
/* PC scan codes of the quick save and quick load keys. */
enum { KEY_QUICK_SAVE = 0x3f, KEY_QUICK_LOAD = 0x43 };   /* F5, F9 */

extern int game_mode;                   /* GAME_MODE_VANILLA in the library; main.c sets the --mode value */
extern const char *const game_mode_names[GAME_MODE_COUNT];

/* The keypad key that a letter key W S A D Q E stands for in the advanced mode; 0 for other keys and in vanilla. */
uint16_t advanced_movement_key(uint16_t key);
/* Writes the game to <data>/DATA/QUICK.DAT and prints the result in the message area. The saved-game state of the
   session (the QUIT question, RESTART) does not change. */
void advanced_quick_save(void);
/* Loads <data>/DATA/QUICK.DAT without a dialog and prints the result. A missing or damaged file keeps the game. */
void advanced_quick_load(void);

#endif
