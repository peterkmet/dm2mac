#ifndef SEEN_H
#define SEEN_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* Prefixes: seen_ */

/* The squares of the automap that the dungeon view has shown, one byte per square of every map. */

/* The rows of the dungeon view and the squares of each row (the middle one and 3 to each side). */
enum { VIEW_DEPTHS = 5, VIEW_HALF_WIDTH = 3, VIEW_WIDTH = 2 * VIEW_HALF_WIDTH + 1 };
#define SEEN_VISIBLE_MAX (VIEW_DEPTHS * VIEW_WIDTH)

/* A square that the dungeon view shows: its row (0 is the party row), its side (negative to the left), its position. */
typedef struct {
    int16_t depth, side, x, y;
} SeenSquare;

/* Forgets every seen square and sizes the storage from the loaded dungeon. */
void seen_reset(void);
/* Marks the squares that the dungeon view shows to a party at (x, y) of map looking to dir. */
void seen_mark(int map, int x, int y, int dir);
/* Writes to out (at most max) the squares that the dungeon view shows now to a party at (x, y) of map looking to dir,
   row by row, in each row the middle square, then +1, -1, +2, -2; returns the count. */
int seen_visible(int map, int x, int y, int dir, SeenSquare *out, int max);
bool seen_is_seen(int map, int x, int y);
/* Writes the AMAP block ("AMAP", version, map count, width and height of each map, one byte per square) to f;
   returns 0 on an error. */
int seen_write(FILE *f);
/* Reads the AMAP block from the rest of f; a missing or damaged block gives an empty automap. */
void seen_read(FILE *f);

#endif
