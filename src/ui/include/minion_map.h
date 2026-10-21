/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_MINION_MAP_H
#define DM2_UI_MINION_MAP_H
/* Prefixes: mmap_ */

/* The minion map panel of the action area. */

#include "ui.h"
#include <stdbool.h>
#include <stdint.h>

/* Draws the option buttons; pressed: the buttons that show pressed (the click feedback). */
void mmap_draw_option_buttons(int pressed);
/* Draws the minion map panel of the map item thing. */
void mmap_draw(uint16_t thing);
/* The square icon of (sx, sy) of the current map at (x, y) of the screen in target, north up, clipped to clip, without
   things; party_item >= 0: the party stands there, with the party icons of the map item party_item (category 0x14). */
void mmap_draw_square_icon(Offscreen *target, const int16_t *clip, int sx, int sy, int x, int y, int party_item);
/* True when the map draws the floor ornament (an ornament index) without the details option. */
bool mmap_shows_floor_ornament(int ornament);
/* The first map item index of GRAPHICS.DAT with the party icons; -1 none. */
int mmap_party_icons_item(void);

#endif /* DM2_UI_MINION_MAP_H */
