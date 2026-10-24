/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The automap of the advanced mode: the map picture. */
#include "automap.h"
#include "aspect.h"
#include "dungeon.h"
#include "game.h"
#include "game_text.h"
#include "minion.h"
#include "move.h"
#include "save.h"
#include "seen.h"
#include "sensor.h"
#include "minion_map.h"

/* The gap left of and above each square icon. */
#define ICON_GAP 1

/* The pressure pad mark: a square this size in the middle of the icon. */
#define PAD_MARK_SIZE   3
#define PAD_MARK_OFFSET ((MAP_ICON_W - PAD_MARK_SIZE) / 2)

/* The squares around a map that the view draws as its edge walls (dungeon_square_edge). */
#define MAP_MARGIN 1

/* The first and the last level column (or row) that the automap draws. */
typedef struct {
    int first, last;
} Span;

static int scroll_x, scroll_y; /* squares between the party and the middle of the drawn map */

static int clamp(int lo, int v, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* The maps of the party level, ended by LEVEL_LIST_END. */
static const uint8_t *party_level_maps(void)
{
    return dungeon_level_maps(dungeon_map_level(g_game.party_map));
}

/* The level position of the party square. */
static Location party_position(void)
{
    int map = g_game.party_map;
    return (Location){dungeon_map_offset_x(map) + g_game.party_x, dungeon_map_offset_y(map) + g_game.party_y};
}

/* The level position at the middle of the drawn map. */
static Location middle_square(void)
{
    Location party = party_position();
    return (Location){party.x + scroll_x, party.y + scroll_y};
}

/* The smallest rect of level positions (level x, y, width, height) that holds all maps of the party level with their
   margins. */
static void level_rect(int16_t *rect)
{
    int map = g_game.party_map;
    int x0 = dungeon_map_offset_x(map) - MAP_MARGIN, y0 = dungeon_map_offset_y(map) - MAP_MARGIN;
    int x1 = x0 + dungeon_map_width(map) + 2 * MAP_MARGIN, y1 = y0 + dungeon_map_height(map) + 2 * MAP_MARGIN;
    for (const uint8_t *m = party_level_maps(); *m != LEVEL_LIST_END; m++) {
        int left = dungeon_map_offset_x(*m) - MAP_MARGIN, top = dungeon_map_offset_y(*m) - MAP_MARGIN;
        int right = left + dungeon_map_width(*m) + 2 * MAP_MARGIN,
            bottom = top + dungeon_map_height(*m) + 2 * MAP_MARGIN;
        x0 = left < x0 ? left : x0;
        y0 = top < y0 ? top : y0;
        x1 = right > x1 ? right : x1;
        y1 = bottom > y1 ? bottom : y1;
    }
    rect[RECT_X] = (int16_t)x0;
    rect[RECT_Y] = (int16_t)y0;
    rect[RECT_W] = (int16_t)(x1 - x0);
    rect[RECT_H] = (int16_t)(y1 - y0);
}

bool automap_is_imaginary_wall(uint8_t square)
{
    return square_type(square) == SQUARE_FAKE_WALL && square_flag(square, SQUARE_FLAG_0) &&
           !square_flag(square, SQUARE_FAKE_WALL_OPEN);
}

bool automap_is_pressure_pad(int16_t map, int16_t x, int16_t y)
{
    if (square_type(dungeon_square(map, x, y)) != SQUARE_FLOOR)
        return false;
    ViewSquare view;
    aspect_get(&view, map, DIR_NORTH, x, y);
    if ((view.side[SIDE_FLOOR_ORNAMENT] & ORNAMENT_INDEX_MASK) == ORNAMENT_NONE)
        return false; /* the view draws no pad: a hidden trigger stays hidden */
    for (uint16_t t = dungeon_square_first_thing(map, x, y); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR;
         t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_SENSOR || sensor_ornament_index(t) < 0)
            continue;
        switch (sensor_type(t)) {
        case FLOOR_ANY:
        case FLOOR_PARTY_OR_CREATURE:
        case FLOOR_PARTY:
        case FLOOR_CREATURE:
        case FLOOR_PARTY_POSSESSES:
        case FLOOR_ITEM:
            return true;
        }
    }
    return false;
}

/* True when map covers the level position level_at; *position is the square of map there. */
static bool covers(int16_t map, Location level_at, Position *position)
{
    *position = (Position){map, level_at.x - dungeon_map_offset_x(map), level_at.y - dungeon_map_offset_y(map)};
    return position->x >= 0 && position->x < dungeon_map_width(map) && position->y >= 0 &&
           position->y < dungeon_map_height(map);
}

/* True when the level position level_at is in the margin of map; *position is the margin square of map there. */
static bool in_margin(int16_t map, Location level_at, Position *position)
{
    if (covers(map, level_at, position))
        return false;
    return position->x >= -MAP_MARGIN && position->x < dungeon_map_width(map) + MAP_MARGIN &&
           position->y >= -MAP_MARGIN && position->y < dungeon_map_height(map) + MAP_MARGIN;
}

/* True when a map of the party level covers the level position level_at. */
static bool covered(Location level_at)
{
    Position position;
    for (const uint8_t *m = party_level_maps(); *m != LEVEL_LIST_END; m++)
        if (covers(*m, level_at, &position))
            return true;
    return false;
}

bool automap_level_square(Location level_at, Position *position)
{
    int16_t party_map = g_game.party_map;
    Location party = party_position();
    if (level_at.x == party.x && level_at.y == party.y) {
        *position = (Position){party_map, g_game.party_x, g_game.party_y};
        return true;
    }
    int next = 0;
    do {
        /* the search writes the square of the map that it finds into x, y */
        int16_t x = level_at.x - dungeon_map_offset_x(party_map), y = level_at.y - dungeon_map_offset_y(party_map);
        int map = dungeon_location_on_map_from(party_map, 0, &x, &y, &next);
        if (map >= 0 && covers(map, level_at, position))
            return true; /* a found square in the margin of its map does not count */
    } while (next != 0);
    for (const uint8_t *m = party_level_maps(); *m != LEVEL_LIST_END; m++)
        if (covers(*m, level_at, position))
            return true;
    /* no map covers it: the edge wall that the view draws in the margin of a map */
    for (const uint8_t *m = party_level_maps(); *m != LEVEL_LIST_END; m++)
        if (in_margin(*m, level_at, position) &&
            square_type(dungeon_square_edge(*m, position->x, position->y)) == SQUARE_WALL)
            return true;
    return false;
}

/* True when map has seen a square of its own next to its margin square position. */
static bool margin_seen(const Position *position)
{
    for (int dx = -1; dx <= 1; dx++)
        for (int dy = -1; dy <= 1; dy++)
            if (seen_is_seen(position->map, (int16_t)(position->x + dx), (int16_t)(position->y + dy)))
                return true;
    return false;
}

bool automap_level_seen(Location level_at)
{
    Position position;
    for (const uint8_t *m = party_level_maps(); *m != LEVEL_LIST_END; m++)
        if (covers(*m, level_at, &position) && seen_is_seen(position.map, position.x, position.y))
            return true;
    if (covered(level_at))
        return false;
    /* an edge wall counts as seen with a seen square next to it: the view shows it from there */
    for (const uint8_t *m = party_level_maps(); *m != LEVEL_LIST_END; m++)
        if (in_margin(*m, level_at, &position) && margin_seen(&position))
            return true;
    return false;
}

/* Fills rect cut to clip. */
static void fill_clipped(Offscreen *o, const int16_t *clip, const int16_t *rect, int color)
{
    int x0 = rect[RECT_X] < clip[RECT_X] ? clip[RECT_X] : rect[RECT_X];
    int y0 = rect[RECT_Y] < clip[RECT_Y] ? clip[RECT_Y] : rect[RECT_Y];
    int x1 = rect[RECT_X] + rect[RECT_W], y1 = rect[RECT_Y] + rect[RECT_H];
    x1 = x1 < clip[RECT_X] + clip[RECT_W] ? x1 : clip[RECT_X] + clip[RECT_W];
    y1 = y1 < clip[RECT_Y] + clip[RECT_H] ? y1 : clip[RECT_Y] + clip[RECT_H];
    if (x1 <= x0 || y1 <= y0)
        return;
    int16_t r[RECT_SIZE] = {(int16_t)x0, (int16_t)y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0)};
    offscreen_fill_rect(o, r, color);
}

/* Light gray on every pixel of the icon at (x, y) with an even x + y: the wall icon shows a checkerboard. */
static void checker_overlay(Offscreen *o, const int16_t *clip, int x, int y)
{
    for (int py = y; py < y + MAP_ICON_H; py++)
        for (int px = x; px < x + MAP_ICON_W; px++)
            if (!((px + py) & 1))
                fill_clipped(o, clip, (int16_t[RECT_SIZE]){(int16_t)px, (int16_t)py, 1, 1}, g_ui_colors[UI_LIGHT_GRAY]);
}

/* The white frame just outside the level rect, whose level position (0, 0) icon is at (left, top). */
static void map_frame(Offscreen *o, const int16_t *clip, int left, int top, const int16_t *rect)
{
    int color = g_ui_colors[UI_WHITE];
    int x0 = left + rect[RECT_X] * MAP_CELL_SIZE - ICON_GAP, y0 = top + rect[RECT_Y] * MAP_CELL_SIZE - ICON_GAP;
    int right = x0 + rect[RECT_W] * MAP_CELL_SIZE, bottom = y0 + rect[RECT_H] * MAP_CELL_SIZE;
    int16_t w = (int16_t)(right - x0 + 1), h = (int16_t)(bottom - y0 + 1);
    fill_clipped(o, clip, (int16_t[RECT_SIZE]){(int16_t)x0, (int16_t)y0, w, 1}, color);
    fill_clipped(o, clip, (int16_t[RECT_SIZE]){(int16_t)x0, (int16_t)bottom, w, 1}, color);
    fill_clipped(o, clip, (int16_t[RECT_SIZE]){(int16_t)x0, (int16_t)y0, 1, h}, color);
    fill_clipped(o, clip, (int16_t[RECT_SIZE]){(int16_t)right, (int16_t)y0, 1, h}, color);
}

/* The level columns (or rows) from start, count long, whose icons can show in the screen span from clip_start,
   clip_len long; origin is the screen place of level column 0. */
static Span visible_span(int origin, int start, int count, int clip_start, int clip_len)
{
    int from = clip_start - (origin + start * MAP_CELL_SIZE), to = from + clip_len - 1;
    Span span = {start + (from > 0 ? from / MAP_CELL_SIZE : 0), start + count - 1};
    if (to < 0)
        span.last = start - 1;
    else if (to / MAP_CELL_SIZE < count)
        span.last = start + to / MAP_CELL_SIZE;
    return span;
}

/* Draws the square that the level position level_at shows, with its icon at the screen place (x, y), when it is seen
   or the reveal map cheat is on. */
static void show_level_square(Offscreen *o, const int16_t *clip, Location level_at, int x, int y)
{
    Position position;
    if (!automap_level_square(level_at, &position) || !(game_has_cheat(CHEAT_MAP) || automap_level_seen(level_at)))
        return;
    bool party = position.map == g_game.party_map && position.x == g_game.party_x && position.y == g_game.party_y;
    dun_set_current_map(position.map);
    SquareIcon icon = {position.x, position.y, x, y, party ? mmap_party_icons_item() : -1, g_game.party_dir};
    mmap_draw_square_icon(o, clip, &icon);
    if (!covers(position.map, level_at, &position))
        return; /* an edge wall has no imaginary wall and no pad */
    if (automap_is_imaginary_wall(dungeon_square(position.map, position.x, position.y)))
        checker_overlay(o, clip, x, y);
    else if (automap_is_pressure_pad(position.map, position.x, position.y))
        fill_clipped(o, clip,
                     (int16_t[RECT_SIZE]){(int16_t)(x + PAD_MARK_OFFSET), (int16_t)(y + PAD_MARK_OFFSET), PAD_MARK_SIZE,
                                          PAD_MARK_SIZE},
                     g_ui_colors[UI_GRAY]);
}

void automap_draw(Offscreen *o, const int16_t *clip)
{
    offscreen_fill_rect(o, clip, g_ui_colors[UI_BLACK]);
    if (g_game.party_map < 0 || g_game.party_map >= dungeon_map_count())
        return;
    Location middle = middle_square();
    int16_t rect[RECT_SIZE];
    level_rect(rect);
    int left = clip[RECT_X] + (clip[RECT_W] - MAP_CELL_SIZE) / 2 + ICON_GAP - middle.x * MAP_CELL_SIZE;
    int top = clip[RECT_Y] + (clip[RECT_H] - MAP_CELL_SIZE) / 2 + ICON_GAP - middle.y * MAP_CELL_SIZE;
    Span xs = visible_span(left, rect[RECT_X], rect[RECT_W], clip[RECT_X], clip[RECT_W]);
    Span ys = visible_span(top, rect[RECT_Y], rect[RECT_H], clip[RECT_Y], clip[RECT_H]);
    int saved = g_party.cur_map;
    for (int x = xs.first; x <= xs.last; x++)
        for (int y = ys.first; y <= ys.last; y++)
            show_level_square(o, clip, (Location){x, y}, left + x * MAP_CELL_SIZE, top + y * MAP_CELL_SIZE);
    map_frame(o, clip, left, top, rect);
    dun_set_current_map(saved);
}

void automap_scroll(int dx, int dy)
{
    if (g_game.party_map < 0 || g_game.party_map >= dungeon_map_count())
        return;
    Location middle = middle_square();
    int16_t rect[RECT_SIZE];
    level_rect(rect);
    scroll_x += clamp(rect[RECT_X], middle.x + dx, rect[RECT_X] + rect[RECT_W] - 1) - middle.x;
    scroll_y += clamp(rect[RECT_Y], middle.y + dy, rect[RECT_Y] + rect[RECT_H] - 1) - middle.y;
}

void automap_scroll_reset(void)
{
    scroll_x = 0;
    scroll_y = 0;
}
