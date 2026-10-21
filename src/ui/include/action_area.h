#ifndef ACTION_AREA_H
#define ACTION_AREA_H
#include <stdint.h>
#include "ui.h"

/* Prefixes: area_ */

/* Clears the drawn state of the action area and its bitmap. */
void area_reset(void);
/* Clears the drawn state of the action area for champion champ. */
void area_reset_champion(int champ);
/* Redraws what changed in the action area (tick: one game tick passed). */
void area_update(int tick);
/* Opens the action area bitmap (clear: it starts black) and returns it. */
Offscreen *area_begin_draw(int clear);
/* Draws the runes that the champion entered in the spell panel (clear: their background first). */
void area_draw_entered_runes(int clear);
/* Draws the button and the name of action i of the action menu or of a minion map. */
void area_draw_name(int i, int highlight);
/* Draws the hand icon of champ in the action area (flash: pressed). */
void area_draw_hand_icon(int champ, int hand, int flash);
/* Draws the spell icon of champ in the party icons (flash: pressed). */
void area_draw_spell_icon(int champ, int flash);

#endif
