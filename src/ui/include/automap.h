/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_AUTOMAP_H
#define DM2_UI_AUTOMAP_H
/* Prefixes: automap_ */

/* The automap of the advanced mode: the map picture of the squares that the dungeon view has shown (seen.h). */

#include "dungeon.h"
#include "ui.h"
#include <stdbool.h>
#include <stdint.h>

/* True for a closed fake wall that the party walks through: the automap draws it as a checkered wall; exported for the tests. */
bool automap_is_imaginary_wall(uint8_t square);
/* True for a floor square with a sensor that the party, a creature, or an item triggers and that the view draws with a floor ornament; the automap marks it; exported for the tests. */
bool automap_is_pressure_pad(int16_t map, int16_t x, int16_t y);
/* The map square of the party level that the automap draws at the level position level_at; false when no map covers it; exported for the tests. */
bool automap_level_square(Location level_at, Position *position);
/* True when a map of the party level that covers the level position level_at has seen its square there; exported for the tests. */
bool automap_level_seen(Location level_at);
/* Draws the seen squares of the maps of the party level into the screen rect clip of o, with the party square at the
   middle, moved by the scroll. */
void automap_draw(Offscreen *o, const int16_t *clip);
/* Moves the middle of the drawn map by (dx, dy) squares; the middle stays inside the level rectangle. */
void automap_scroll(int dx, int dy);
/* Moves the middle of the drawn map back to the party square. */
void automap_scroll_reset(void);

#endif /* DM2_UI_AUTOMAP_H */
