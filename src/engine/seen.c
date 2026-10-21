/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The seen squares of the automap and their save block. */
#include <stdlib.h>
#include <string.h>
#include "dungeon.h"
#include "game.h"
#include "move.h"
#include "os.h"
#include "seen.h"
#include "weather.h"

/* The side squares of each row of the dungeon view: 3 squares at depth 0 and 1, 5 at depth 2 and 3, 7 at depth 4. */
static const int8_t row_half_width[VIEW_DEPTHS] = {1, 1, 2, 2, 3};
static const char save_magic[] = "AMAP";
/* The block header: the magic, the version byte, the map count byte; then the width and height byte of each map. */
enum { AMAP_MAGIC_SIZE = sizeof save_magic - 1, AMAP_VERSION = 1, AMAP_COUNT_AT = AMAP_MAGIC_SIZE + 1,
       AMAP_HEADER_SIZE = AMAP_MAGIC_SIZE + 2, AMAP_MAP_SIZE = 2 };

/* One byte per square of every map, the maps one after another, each map column by column. */
static uint8_t *seen;
static size_t seen_size;

static size_t map_first_square(int map)
{
    size_t offset = 0;
    for (int m = 0; m < map; m++) offset += (size_t)dungeon_map_width(m) * (size_t)dungeon_map_height(m);
    return offset;
}

void seen_reset(void)
{
    free(seen);
    seen_size = map_first_square(dungeon_map_count());
    seen = calloc(seen_size ? seen_size : 1, 1);
    if (!seen) os_fatal("out of memory");
}

/* The storage fits the loaded dungeon; returns 0 when memory is out. */
static int sized(void)
{
    if (!seen || seen_size != map_first_square(dungeon_map_count())) seen_reset();
    return seen != NULL;
}

static int on_map(int map, int x, int y)
{
    return map >= 0 && map < dungeon_map_count() && x >= 0 && x < dungeon_map_width(map) && y >= 0
           && y < dungeon_map_height(map);
}

/* The byte of square (x, y) of map, NULL outside the map. */
static uint8_t *seen_square(int map, int x, int y)
{
    if (!sized() || !on_map(map, x, y)) return NULL;
    return seen + map_first_square(map) + (size_t)x * dungeon_map_height(map) + y;
}

bool seen_is_seen(int map, int x, int y)
{
    uint8_t *square = seen_square(map, x, y);
    return square && *square;
}

/* A square that the view cannot look through: a wall, a closed fake wall, a closed door, a square off the map. */
static int blocks_sight(int map, int x, int y)
{
    if (!on_map(map, x, y)) return 1;
    uint8_t square = dungeon_square(map, x, y);
    switch (square_type(square)) {
    case SQUARE_WALL: return 1;
    case SQUARE_FAKE_WALL: return !(square & SQUARE_FAKE_WALL_OPEN);
    case SQUARE_DOOR: return (square & SQUARE_DOOR_STATE) == DOOR_STATE_CLOSED;
    default: return 0;
    }
}

/* The view rows that the party makes out in the current light. */
static int light_depths(void)
{
    /* ponytail: a fixed cut by light level (4: one row less, 5: two rows less), as in the DM port; a finer rule if a
       player needs it */
    if (cheat_light) return VIEW_DEPTHS;
    if (view_light_level >= DARKEST_LIGHT) return VIEW_DEPTHS - 2;
    if (view_light_level >= DARKEST_LIGHT - 1) return VIEW_DEPTHS - 1;
    return VIEW_DEPTHS;
}

int seen_visible(int map, int x, int y, int dir, SeenSquare *out, int max)
{
    uint8_t open[VIEW_DEPTHS][VIEW_WIDTH] = {{0}};  /* seen, and the view looks on through it */
    int depths = light_depths(), count = 0;
    for (int depth = 0; depth < depths; depth++) {
        for (int side = 0; side <= row_half_width[depth]; side++)
            for (int sign = 1; sign >= -1; sign -= 2) {
                if (!side && sign < 0) continue;
                int column = VIEW_HALF_WIDTH + side * sign;
                /* a square is seen through the open square beside it toward the middle, or in front of it */
                int visible = (!depth && !side) || (side && open[depth][column - sign]) || (depth && open[depth - 1][column]);
                if (!visible) continue;
                int16_t sx = (int16_t)x, sy = (int16_t)y;
                dun_move_relative(dir, depth, side * sign, &sx, &sy);
                if (count < max) out[count++] = (SeenSquare){(int16_t)depth, (int16_t)(side * sign), sx, sy};
                open[depth][column] = !blocks_sight(map, sx, sy);
            }
        if (!open[depth][VIEW_HALF_WIDTH]) break;  /* the view ends at the first middle square that blocks sight */
    }
    return count;
}

void seen_mark(int map, int x, int y, int dir)
{
    SeenSquare squares[SEEN_VISIBLE_MAX];
    int count = seen_visible(map, x, y, dir, squares, SEEN_VISIBLE_MAX);
    for (int i = 0; i < count; i++) {
        uint8_t *square = seen_square(map, squares[i].x, squares[i].y);
        if (square) *square = 1;
    }
}

int seen_write(FILE *f)
{
    if (!sized()) return 0;
    int count = dungeon_map_count();
    uint8_t header[AMAP_HEADER_SIZE];
    memcpy(header, save_magic, AMAP_MAGIC_SIZE);
    header[AMAP_MAGIC_SIZE] = AMAP_VERSION;
    header[AMAP_COUNT_AT] = (uint8_t)count;
    int ok = fwrite(header, 1, sizeof header, f) == sizeof header;
    for (int map = 0; ok && map < count; map++)
        ok = fputc(dungeon_map_width(map), f) != EOF && fputc(dungeon_map_height(map), f) != EOF;
    return ok && fwrite(seen, 1, seen_size, f) == seen_size;
}

void seen_read(FILE *f)
{
    seen_reset();
    int count = dungeon_map_count();
    size_t size = AMAP_HEADER_SIZE + AMAP_MAP_SIZE * (size_t)count + seen_size;
    uint8_t *block = malloc(size + 1);
    /* the block ends the file: one byte more must not come */
    int ok = seen && block && fread(block, 1, size + 1, f) == size && !memcmp(block, save_magic, AMAP_MAGIC_SIZE)
             && block[AMAP_MAGIC_SIZE] == AMAP_VERSION && block[AMAP_COUNT_AT] == count;
    const uint8_t *p = block + AMAP_HEADER_SIZE;
    for (int map = 0; ok && map < count; map++, p += AMAP_MAP_SIZE) ok = p[0] == dungeon_map_width(map) && p[1] == dungeon_map_height(map);
    for (size_t i = 0; ok && i < seen_size; i++) ok = p[i] <= 1;
    if (ok) memcpy(seen, p, seen_size);
    free(block);
}
