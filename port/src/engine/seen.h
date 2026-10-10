#ifndef SEEN_H
#define SEEN_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* Prefixes: seen_ */

/* The squares of the automap that the dungeon view has shown, one byte per square of every map. */

/* Forgets every seen square and sizes the storage from the loaded dungeon. */
void seen_reset(void);
/* Marks the squares that the dungeon view shows to a party at (x, y) of map looking to dir. */
void seen_mark(int map, int x, int y, int dir);
bool seen_is_seen(int map, int x, int y);
/* Writes the AMAP block ("AMAP", version, map count, width and height of each map, one byte per square) to f;
   returns 0 on an error. */
int seen_write(FILE *f);
/* Reads the AMAP block from the rest of f; a missing or damaged block gives an empty automap. */
void seen_read(FILE *f);

#endif
