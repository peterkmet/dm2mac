/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The seen squares of the automap and their save block. */
#include "seen.h"
#include <stdlib.h>
#include <string.h>
#include "dungeon.h"
#include "game.h"
#include "os.h"
#include "weather.h"

/* The block header: the magic, the version byte, the map count byte; then the width and height byte of each map. */
#define AMAP_MAGIC       "AMAP"
#define AMAP_MAGIC_SIZE  4
#define AMAP_VERSION     1
#define AMAP_COUNT_AT    (AMAP_MAGIC_SIZE + 1)
#define AMAP_HEADER_SIZE (AMAP_MAGIC_SIZE + 2)
#define AMAP_MAP_SIZE    2

/* The side squares of each row of the dungeon view: 3 squares at depth 0 and 1, 5 at depth 2 and 3, 7 at depth 4. */
static const int8_t row_half_width[VIEW_DEPTHS] = {1, 1, 2, 2, VIEW_HALF_WIDTH};

/* One byte per square of every map, the maps one after another, each map column by column. */
static uint8_t *seen;
static size_t seen_size;

/* The index of the first square of map in the storage. */
static size_t map_first_square(int16_t map)
{
    size_t offset = 0;
    for (int16_t m = 0; m < map; m++)
        offset += (size_t)dungeon_map_width(m) * (size_t)dungeon_map_height(m);
    return offset;
}

static bool on_map(int16_t map, int16_t x, int16_t y)
{
    return map >= 0 && map < dungeon_map_count() && x >= 0 && x < dungeon_map_width(map) && y >= 0 &&
           y < dungeon_map_height(map);
}

/* The byte of square (x, y) of map, NULL outside the map or outside a storage that seen_reset did not size. */
static uint8_t *known_byte(int16_t map, int16_t x, int16_t y)
{
    if (!seen || !on_map(map, x, y))
        return NULL;
    size_t at = map_first_square(map) + (size_t)x * (size_t)dungeon_map_height(map) + (size_t)y;
    return at < seen_size ? seen + at : NULL;
}

/* ---- Setup ---- */

void seen_reset(void)
{
    free(seen);
    seen_size = map_first_square((int16_t)dungeon_map_count());
    seen = calloc(seen_size ? seen_size : 1, 1);
    if (!seen)
        os_fatal("out of memory");
}

void seen_free(void)
{
    free(seen);
    seen = NULL;
    seen_size = 0;
}

/* ---- Queries ---- */

/* A square that the view cannot look through: a wall, a closed fake wall, a closed door, a square off the map. */
static bool blocks_sight(int16_t map, int16_t x, int16_t y)
{
    if (!on_map(map, x, y))
        return true;
    uint8_t square = dungeon_square(map, x, y);
    switch (square_type(square)) {
    case SQUARE_WALL:
        return true;
    case SQUARE_FAKE_WALL:
        return !(square & SQUARE_FAKE_WALL_OPEN);
    case SQUARE_DOOR:
        return (square & SQUARE_DOOR_STATE) == DOOR_STATE_CLOSED;
    default:
        return false;
    }
}

/* The view rows that the party makes out in the current light. */
static int visible_depths(void)
{
    /* A fixed cut by light level: one row less at DARKEST_LIGHT - 1, two rows less at DARKEST_LIGHT. */
    if (game_has_cheat(CHEAT_LIGHT))
        return VIEW_DEPTHS;
    if (g_view_light_level >= DARKEST_LIGHT)
        return VIEW_DEPTHS - 2;
    if (g_view_light_level >= DARKEST_LIGHT - 1)
        return VIEW_DEPTHS - 1;
    return VIEW_DEPTHS;
}

int seen_visible(Position at, int16_t dir, SeenSquare squares[SEEN_VISIBLE_MAX])
{
    bool open[VIEW_DEPTHS][VIEW_WIDTH] = {{false}}; /* seen, and the view looks on through it */
    int depths = visible_depths();
    int count = 0;
    for (int depth = 0; depth < depths; depth++) {
        for (int side = 0; side <= row_half_width[depth]; side++)
            for (int sign = 1; sign >= -1; sign -= 2) {
                if (!side && sign < 0)
                    continue;
                int column = VIEW_HALF_WIDTH + side * sign;
                /* a square is seen through the open square beside it toward the middle, or in front of it */
                bool visible =
                    (!depth && !side) || (side && open[depth][column - sign]) || (depth && open[depth - 1][column]);
                if (!visible)
                    continue;
                Location square = dungeon_move_relative((Location){at.x, at.y}, dir, depth, side * sign);
                squares[count++] = (SeenSquare){(int16_t)depth, (int16_t)(side * sign), square.x, square.y};
                open[depth][column] = !blocks_sight(at.map, square.x, square.y);
            }
        if (!open[depth][VIEW_HALF_WIDTH])
            break; /* the view ends at the first middle square that blocks sight */
    }
    return count;
}

bool seen_is_seen(Position at)
{
    uint8_t *square = known_byte(at.map, at.x, at.y);
    return square && *square;
}

/* ---- Commands ---- */

void seen_mark(Position at, int16_t dir)
{
    SeenSquare squares[SEEN_VISIBLE_MAX];
    int count = seen_visible(at, dir, squares);
    for (int i = 0; i < count; i++) {
        uint8_t *square = known_byte(at.map, squares[i].x, squares[i].y);
        if (square)
            *square = 1;
    }
}

/* ---- Save block ---- */

bool seen_write(FILE *f)
{
    int count = dungeon_map_count();
    if (!seen || seen_size != map_first_square((int16_t)count))
        return false; /* the storage does not fit the dungeon: seen_reset did not run after the load */
    uint8_t header[AMAP_HEADER_SIZE];
    memcpy(header, AMAP_MAGIC, AMAP_MAGIC_SIZE);
    header[AMAP_MAGIC_SIZE] = AMAP_VERSION;
    header[AMAP_COUNT_AT] = (uint8_t)count;
    bool ok = fwrite(header, 1, sizeof header, f) == sizeof header;
    for (int16_t map = 0; ok && map < count; map++)
        ok = fputc(dungeon_map_width(map), f) != EOF && fputc(dungeon_map_height(map), f) != EOF;
    return ok && fwrite(seen, 1, seen_size, f) == seen_size;
}

bool seen_read(FILE *f)
{
    seen_reset();
    int count = dungeon_map_count();
    size_t size = AMAP_HEADER_SIZE + AMAP_MAP_SIZE * (size_t)count + seen_size;
    uint8_t *block = malloc(size);
    if (!block)
        os_fatal("out of memory");
    bool ok = fread(block, 1, size, f) == size && !memcmp(block, AMAP_MAGIC, AMAP_MAGIC_SIZE) &&
              block[AMAP_MAGIC_SIZE] == AMAP_VERSION && block[AMAP_COUNT_AT] == count;
    const uint8_t *p = block + AMAP_HEADER_SIZE;
    for (int16_t map = 0; ok && map < count; map++, p += AMAP_MAP_SIZE)
        ok = p[0] == dungeon_map_width(map) && p[1] == dungeon_map_height(map);
    for (size_t i = 0; ok && i < seen_size; i++)
        ok = p[i] <= 1;
    if (ok)
        memcpy(seen, p, seen_size);
    free(block);
    return ok;
}
