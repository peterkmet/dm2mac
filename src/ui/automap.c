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

static int scroll_x, scroll_y; /* squares between the party and the middle of the drawn map */

static int clamp(int lo, int v, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* The middle square of the drawn map. */
static void middle_square(int *x, int *y)
{
    *x = g_game.party_x + scroll_x;
    *y = g_game.party_y + scroll_y;
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

/* The white frame just outside the squares of map, whose square (0, 0) icon is at (left, top). */
static void map_frame(Offscreen *o, const int16_t *clip, int left, int top, int map)
{
    int color = g_ui_colors[UI_WHITE];
    int right = left + dungeon_map_width(map) * MAP_CELL_SIZE - ICON_GAP;
    int bottom = top + dungeon_map_height(map) * MAP_CELL_SIZE - ICON_GAP;
    int x0 = left - ICON_GAP, y0 = top - ICON_GAP;
    int16_t w = (int16_t)(right - x0 + 1), h = (int16_t)(bottom - y0 + 1);
    fill_clipped(o, clip, (int16_t[RECT_SIZE]){(int16_t)x0, (int16_t)y0, w, 1}, color);
    fill_clipped(o, clip, (int16_t[RECT_SIZE]){(int16_t)x0, (int16_t)bottom, w, 1}, color);
    fill_clipped(o, clip, (int16_t[RECT_SIZE]){(int16_t)x0, (int16_t)y0, 1, h}, color);
    fill_clipped(o, clip, (int16_t[RECT_SIZE]){(int16_t)right, (int16_t)y0, 1, h}, color);
}

void automap_draw(Offscreen *o, const int16_t *clip)
{
    offscreen_fill_rect(o, clip, g_ui_colors[UI_BLACK]);
    int map = g_game.party_map, middle_x, middle_y;
    if (map < 0 || map >= dungeon_map_count())
        return;
    middle_square(&middle_x, &middle_y);
    int left = clip[RECT_X] + (clip[RECT_W] - MAP_CELL_SIZE) / 2 + ICON_GAP - middle_x * MAP_CELL_SIZE;
    int top = clip[RECT_Y] + (clip[RECT_H] - MAP_CELL_SIZE) / 2 + ICON_GAP - middle_y * MAP_CELL_SIZE;
    int saved = g_party.cur_map, party_item = mmap_party_icons_item();
    dun_set_current_map(map);
    for (int x = 0; x < dungeon_map_width(map); x++)
        for (int y = 0; y < dungeon_map_height(map); y++)
            if (game_has_cheat(CHEAT_MAP) || seen_is_seen(map, x, y)) {
                SquareIcon icon = {x,
                                   y,
                                   left + x * MAP_CELL_SIZE,
                                   top + y * MAP_CELL_SIZE,
                                   x == g_game.party_x && y == g_game.party_y ? party_item : -1,
                                   g_game.party_dir};
                mmap_draw_square_icon(o, clip, &icon);
                if (automap_is_imaginary_wall(dungeon_square(map, x, y)))
                    checker_overlay(o, clip, icon.x, icon.y);
                else if (automap_is_pressure_pad(map, x, y))
                    fill_clipped(o, clip,
                                 (int16_t[RECT_SIZE]){(int16_t)(icon.x + PAD_MARK_OFFSET),
                                                      (int16_t)(icon.y + PAD_MARK_OFFSET), PAD_MARK_SIZE,
                                                      PAD_MARK_SIZE},
                                 g_ui_colors[UI_GRAY]);
            }
    map_frame(o, clip, left, top, map);
    dun_set_current_map(saved);
}

void automap_scroll(int dx, int dy)
{
    int x, y, map = g_game.party_map;
    if (map < 0 || map >= dungeon_map_count())
        return;
    middle_square(&x, &y);
    scroll_x += clamp(0, x + dx, dungeon_map_width(map) - 1) - x;
    scroll_y += clamp(0, y + dy, dungeon_map_height(map) - 1) - y;
}

void automap_scroll_reset(void)
{
    scroll_x = 0;
    scroll_y = 0;
}
