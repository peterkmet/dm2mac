/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_ACTION_AREA_H
#define DM2_UI_ACTION_AREA_H
/* Prefixes: area_ */

/* The action area: the hands and spell icons of the party, and the panels that open in it. */

#include <stdbool.h>
#include "ui.h"

/* The size of the text of one rune: a rune glyph and the end of the text. */
#define RUNE_TEXT_SIZE 2

/* ---- Setup ---- */

/* Clears the drawn state of the action area and its bitmap. */
void area_reset(void);
/* Clears the drawn state of the action area for champion champion_index. */
void area_reset_champion(int16_t champion_index);

/* ---- Drawing ---- */

/* Opens the action area bitmap (clear: it starts black) and returns it. */
Offscreen *area_begin_draw(bool clear);
/* Draws the hand of champion_index in the action area: its back, its item icon, dimmed while it is busy
   (flash: pressed). */
void area_draw_hand_icon(int16_t champion_index, int16_t hand, bool flash);
/* Draws the spell hand and the rune icon of champion_index in the action area (flash: pressed). */
void area_draw_spell_icon(int16_t champion_index, bool flash);
/* Draws the runes that the champion entered in the spell panel (clear: their background first). */
void area_draw_entered_runes(bool clear);
/* Draws the button and the name of action i of the action menu or of a minion map. */
void area_draw_name(int i, bool highlight);

/* ---- Redraw ---- */

/* Redraws what changed in the action area (tick: one game tick passed). */
void area_update(bool tick);
/* Ends the extra hold of a hit's damage of the advanced mode when the click at (x, y) is in the action area, and draws
   the hand icons; returns whether it did. */
bool area_end_damage_hold(int16_t x, int16_t y);

#endif /* DM2_UI_ACTION_AREA_H */
