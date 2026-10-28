/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_MINION_MAP_H
#define DM2_UI_MINION_MAP_H
/* Prefixes: mmap_ */

/* The minion map panel of the action area. */

#include <stdbool.h>
#include <stdint.h>
#include "ui.h"

/* A square icon of the map: the square (sx, sy) of the current map and the screen position (x, y) of the icon;
   party_item >= 0: the party stands there, with the party icons of the map item party_item (category 0x14), and the
   side of its frame toward party_dir is white; flags: the MAP_ flags of the things that it draws (MAP_STATIC_CREATURES:
   the static creatures, 0: none). */
typedef struct {
    int16_t sx, sy;
    int16_t x, y;
    int party_item;
    int16_t party_dir;
    uint16_t flags;
} SquareIcon;

/* True when the map draws the floor ornament (an ornament index) without the details option. */
bool mmap_shows_floor_ornament(int ornament);
/* Draws the square icon in target, north up, clipped to clip, with the things of its flags. */
void mmap_draw_square_icon(Offscreen *target, const int16_t *clip, const SquareIcon *icon);
/* The first map item index of GRAPHICS.DAT with the party icons; -1 none. */
int mmap_party_icons_item(void);
/* Draws the option buttons; pressed: the buttons that show pressed (the click feedback). */
void mmap_draw_option_buttons(int pressed);
/* Draws the minion map panel of the map item thing. */
void mmap_draw(uint16_t thing);

#endif /* DM2_UI_MINION_MAP_H */
