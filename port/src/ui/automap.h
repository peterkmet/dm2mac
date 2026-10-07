#ifndef AUTOMAP_H
#define AUTOMAP_H
#include <stdint.h>
#include <stdio.h>
#include "ui.h"

/* The automap of the advanced mode: the squares that the dungeon view has shown, one byte per square of every map. */

/* Forgets every seen square and sizes the storage from the loaded dungeon. */
void automap_reset(void);
/* Marks the squares that the dungeon view shows to a party at (x, y) of map looking to dir. */
void automap_mark_seen(int map, int x, int y, int dir);
int automap_is_seen(int map, int x, int y);
/* Writes the AMAP block ("AMAP", version, map count, width and height of each map, one byte per square) to f;
   returns 0 on an error. */
int automap_write(FILE *f);
/* Reads the AMAP block from the rest of f; a missing or damaged block gives an empty automap. */
void automap_read(FILE *f);
/* Draws the seen squares of the party map into the screen rect clip of o, with the party square at the middle, moved
   by the scroll. */
void automap_draw(Offscreen *o, const int16_t *clip);
/* Moves the middle of the drawn map by (dx, dy) squares; the middle stays on the map. */
void automap_scroll(int dx, int dy);
void automap_scroll_reset(void);

#endif
