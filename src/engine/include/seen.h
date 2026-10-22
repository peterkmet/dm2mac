/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_SEEN_H
#define DM2_ENGINE_SEEN_H
/* Prefixes: seen_ */

/* The squares of the automap that the dungeon view has shown, one byte per square of every map. */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* The rows of the dungeon view and the squares of each row (the middle one and 3 to each side). */
#define VIEW_DEPTHS      5
#define VIEW_HALF_WIDTH  3
#define VIEW_WIDTH       (2 * VIEW_HALF_WIDTH + 1)
#define SEEN_VISIBLE_MAX (VIEW_DEPTHS * VIEW_WIDTH)

/* A square that the dungeon view shows: its row (0 is the party row), its side (negative to the left), its position. */
typedef struct {
    int16_t depth, side, x, y;
} SeenSquare;

/* Forgets every seen square and sizes the storage from the loaded dungeon. */
void seen_reset(void);
/* Marks the squares that the dungeon view shows to a party at (x, y) of map looking to dir. */
void seen_mark(int16_t map, int16_t x, int16_t y, int16_t dir);
/* Writes to squares the squares that the dungeon view shows now to a party at (x, y) of map looking to dir,
   row by row, in each row the middle square, then +1, -1, +2, -2; returns the count. */
int seen_visible(int16_t map, int16_t x, int16_t y, int16_t dir, SeenSquare squares[SEEN_VISIBLE_MAX]);
/* True when the dungeon view has shown the square (x, y) of map. */
bool seen_is_seen(int16_t map, int16_t x, int16_t y);
/* Writes the AMAP block ("AMAP", version, map count, width and height of each map, one byte per square) to f;
   returns false on an error. */
bool seen_write(FILE *f);
/* Reads the AMAP block from f; a missing or damaged block gives an empty automap and false. */
bool seen_read(FILE *f);

#endif /* DM2_ENGINE_SEEN_H */
