/* The automap of the advanced mode: the seen squares, their save block, and the map picture. */
#include <stdlib.h>
#include <string.h>
#include "advanced.h"
#include "automap.h"
#include "dungeon.h"
#include "minion.h"
#include "move.h"
#include "save.h"
#include "text.h"
#include "view.h"

enum { VIEW_DEPTHS = 5, VIEW_HALF_WIDTH = 3, VIEW_WIDTH = 2 * VIEW_HALF_WIDTH + 1 };
/* The side squares of each row of the dungeon view: 3 squares at depth 0 and 1, 5 at depth 2 and 3, 7 at depth 4. */
static const int8_t row_half_width[VIEW_DEPTHS] = {1, 1, 2, 2, 3};
#define ICON_GAP 1                  /* the gap left of and above each square icon */
static const char save_magic[] = "AMAP";
/* The block header: the magic, the version byte, the map count byte; then the width and height byte of each map. */
enum { SAVE_MAGIC_SIZE = sizeof save_magic - 1, SAVE_VERSION = 1, SAVE_COUNT_AT = SAVE_MAGIC_SIZE + 1,
       SAVE_HEADER_SIZE = SAVE_MAGIC_SIZE + 2, SAVE_MAP_SIZE = 2 };

/* One byte per square of every map, the maps one after another, each map column by column. */
static uint8_t *seen;
static size_t seen_size;
static int scroll_x, scroll_y;      /* squares between the party and the middle of the drawn map */

static size_t map_first_square(int map)
{
    size_t offset = 0;
    for (int m = 0; m < map; m++) offset += (size_t)dungeon_map_width(m) * (size_t)dungeon_map_height(m);
    return offset;
}

void automap_reset(void)
{
    free(seen);
    seen_size = map_first_square(dungeon_map_count());
    seen = calloc(seen_size ? seen_size : 1, 1);
}

/* The storage fits the loaded dungeon; returns 0 when memory is out. */
static int sized(void)
{
    if (!seen || seen_size != map_first_square(dungeon_map_count())) automap_reset();
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

bool automap_is_seen(int map, int x, int y)
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

void automap_mark_seen(int map, int x, int y, int dir)
{
    uint8_t open[VIEW_DEPTHS][VIEW_WIDTH] = {{0}};  /* seen, and the view looks on through it */
    int depths = light_depths();
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
                uint8_t *square = seen_square(map, sx, sy);
                if (square) *square = 1;
                open[depth][column] = !blocks_sight(map, sx, sy);
            }
        if (!open[depth][VIEW_HALF_WIDTH]) break;  /* the view ends at the first middle square that blocks sight */
    }
}

int automap_write(FILE *f)
{
    if (!sized()) return 0;
    int count = dungeon_map_count();
    uint8_t header[SAVE_HEADER_SIZE];
    memcpy(header, save_magic, SAVE_MAGIC_SIZE);
    header[SAVE_MAGIC_SIZE] = SAVE_VERSION;
    header[SAVE_COUNT_AT] = (uint8_t)count;
    int ok = fwrite(header, 1, sizeof header, f) == sizeof header;
    for (int map = 0; ok && map < count; map++)
        ok = fputc(dungeon_map_width(map), f) != EOF && fputc(dungeon_map_height(map), f) != EOF;
    return ok && fwrite(seen, 1, seen_size, f) == seen_size;
}

void automap_read(FILE *f)
{
    automap_reset();
    int count = dungeon_map_count();
    size_t size = SAVE_HEADER_SIZE + SAVE_MAP_SIZE * (size_t)count + seen_size;
    uint8_t *block = malloc(size + 1);
    /* the block ends the file: one byte more must not come */
    int ok = seen && block && fread(block, 1, size + 1, f) == size && !memcmp(block, save_magic, SAVE_MAGIC_SIZE)
             && block[SAVE_MAGIC_SIZE] == SAVE_VERSION && block[SAVE_COUNT_AT] == count;
    const uint8_t *p = block + SAVE_HEADER_SIZE;
    for (int map = 0; ok && map < count; map++, p += SAVE_MAP_SIZE) ok = p[0] == dungeon_map_width(map) && p[1] == dungeon_map_height(map);
    for (size_t i = 0; ok && i < seen_size; i++) ok = p[i] <= 1;
    if (ok) memcpy(seen, p, seen_size);
    free(block);
}

static int clamp(int lo, int v, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* The middle square of the drawn map. */
static void draw_middle(int *x, int *y)
{
    *x = game.party_x + scroll_x;
    *y = game.party_y + scroll_y;
}

void automap_scroll(int dx, int dy)
{
    int x, y, map = game.party_map;
    if (map < 0 || map >= dungeon_map_count()) return;
    draw_middle(&x, &y);
    scroll_x += clamp(0, x + dx, dungeon_map_width(map) - 1) - x;
    scroll_y += clamp(0, y + dy, dungeon_map_height(map) - 1) - y;
}

void automap_scroll_reset(void)
{
    scroll_x = scroll_y = 0;
}

void automap_draw(Offscreen *o, const int16_t *clip)
{
    offscreen_fill_rect(o, clip, ui_colors[UI_BLACK]);
    int map = game.party_map, middle_x, middle_y;
    if (map < 0 || map >= dungeon_map_count()) return;
    draw_middle(&middle_x, &middle_y);
    int left = clip[RECT_X] + (clip[RECT_W] - MAP_CELL_SIZE) / 2 + ICON_GAP - middle_x * MAP_CELL_SIZE;
    int top = clip[RECT_Y] + (clip[RECT_H] - MAP_CELL_SIZE) / 2 + ICON_GAP - middle_y * MAP_CELL_SIZE;
    int saved = party.cur_map, party_item = minion_map_party_icons_item();
    dun_set_current_map(map);
    for (int x = 0; x < dungeon_map_width(map); x++)
        for (int y = 0; y < dungeon_map_height(map); y++)
            if (automap_is_seen(map, x, y))
                minion_map_draw_square_icon(o, clip, x, y, left + x * MAP_CELL_SIZE, top + y * MAP_CELL_SIZE,
                                            x == game.party_x && y == game.party_y ? party_item : -1);
    dun_set_current_map(saved);
}
