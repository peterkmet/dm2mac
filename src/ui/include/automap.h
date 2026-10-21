#ifndef AUTOMAP_H
#define AUTOMAP_H
#include <stdint.h>
#include "seen.h"
#include "ui.h"

/* Prefixes: automap_ */

/* The automap of the advanced mode: the map picture of the squares that the dungeon view has shown (seen.h). */

/* Draws the seen squares of the party map into the screen rect clip of o, with the party square at the middle, moved
   by the scroll. */
void automap_draw(Offscreen *o, const int16_t *clip);
/* Moves the middle of the drawn map by (dx, dy) squares; the middle stays on the map. */
void automap_scroll(int dx, int dy);
void automap_scroll_reset(void);

#endif
