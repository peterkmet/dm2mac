/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Minion maps: the mana of the map panel, its option buttons, and the clicks that send a minion. */
#include "minion.h"
#include "gfx.h"
#include "action.h"
#include "ai.h"
#include "champion.h"
#include "dungeon.h"
#include "hooks.h"
#include "item.h"
#include "move.h"

#define MINION_MAP_MANA_TICKS 25

MinionMap minion_map;

/* The mana of each option button. */
static const uint8_t option_mana[MAP_OPTION_COUNT] = {2, 3, 1, 1};

void minion_toggle_map_option(int option)
{
    int mana = option_mana[option], bit = 1 << option;
    if (!(action.flags & bit)) {
        Champion *c = champion(action.champion - 1);
        if (c->mana < mana)
            return;
        c->mana = (int16_t)(c->mana - mana);
        c->redraw |= REDRAW_BARS;
    } else {
        mana = -mana;
    }
    action.flags ^= (uint16_t)bit;
    minion_map.mana_divisor = (int16_t)(minion_map.mana_divisor + mana);
}

void minion_map_open(int champ)
{
    Champion *c = champion(champ);
    minion_map.mana_divisor = 1;
    minion_map.mana_timer = MINION_MAP_MANA_TICKS;
    if (!c->mana) {
        action.flags = MAP_OPEN;
    } else {
        c->mana--;
        c->redraw |= REDRAW_BARS;
        action.flags = MAP_OPEN | MAP_POWERED;
    }
    action.mode = ACTION_MODE_MINION_MAP;
}

void minion_map_drain_mana(void)
{
    Champion *c = champion(action.champion - 1);
    if (!(action.flags & MAP_POWERED)) {
        if (c->mana) {
            /* the champion has mana again: the map opens anew */
            action.redraw_delay = 1;
            action.flags = 0;
        }
        return;
    }
    if (--minion_map.mana_timer)
        return;
    if (!c->mana) {
        action.redraw_delay = 1;
        action.flags = 0;
        return;
    }
    c->mana--;
    c->redraw |= REDRAW_BARS;
    minion_map.mana_timer = (int16_t)(MINION_MAP_MANA_TICKS / minion_map.mana_divisor);
}

int minion_map_set_location(uint16_t thing, int x, int y, int map)
{
    int saved = party.cur_map;
    dun_set_current_map(map);
    int inside = x >= 0 && x < dungeon_map_width(party.cur_map) && y >= 0 && y < dungeon_map_height(party.cur_map);
    if (inside)
        set_thing_word(thing, MINION_MAP_LOCATION, (uint16_t)location_pack(x, y, map));
    dun_set_current_map(saved);
    return inside;
}

void minion_map_click_set_destination(int command, int x, int y)
{
    uint16_t thing = champion(action.champion - 1)->slots[action.hand];
    if (command != CMD_MINION_MAP || container_type(thing) != MAP_KIND_MINION)
        return;
    int16_t r[RECT_SIZE];
    g_hooks.zone_rect(ZONE_MAP_GRID, r);
    int16_t px = (int16_t)(x - (r[RECT_X] - minion_map.x_offset)),
            py = (int16_t)(y - (r[RECT_Y] - minion_map.y_offset));
    if (px % (MAP_ICON_W + GAP_X) < GAP_X || py % (MAP_ICON_H + GAP_Y) < GAP_Y)
        return;
    uint16_t group = thing_word(thing, MINION_MAP_GROUP);
    if (!minion_find_link_projectile(group, thing))
        return;
    int col = px / (MAP_ICON_W + GAP_X), row = py / (MAP_ICON_H + GAP_Y);
    int16_t sx = minion_map.x, sy = minion_map.y;
    dun_move_relative(minion_map.dir, GRID_RADIUS - row, col - GRID_RADIUS, &sx, &sy);
    int lx = sx, ly = sy;
    int map = dungeon_location_on_map(minion_map.map, 0, &lx, &ly);
    if (map < 0)
        return;
    sx = (int16_t)lx;
    sy = (int16_t)ly;
    int saved = party.cur_map;
    dun_set_current_map(map);
    uint8_t square = dun_get_square(sx, sy);
    int type = square_type(square);
    if (type != SQUARE_WALL && type != SQUARE_UNKNOWN &&
        (type != SQUARE_FAKE_WALL || square & (SQUARE_FAKE_WALL_OPEN | SQUARE_FLAG_0))) {
        if (sx == minion_map.marker_x && sy == minion_map.marker_y && map == minion_map.marker_map) {
            /* a click on the marker of a stairs or a pit goes to the level that it leads to */
            if (minion_map.marker_state == MARKER_SHOWN && minion_map.above_map >= 0) {
                sx = minion_map.above_x;
                sy = minion_map.above_y;
                map = minion_map.above_map;
            } else if (minion_map.below_map >= 0 &&
                       (minion_map.marker_state == MARKER_SHOWN || minion_map.marker_state == MARKER_BELOW)) {
                sx = minion_map.below_x;
                sy = minion_map.below_y;
                map = minion_map.below_map;
            }
        }
        minion_map_set_location(thing, sx, sy, map);
        ai_free_cached_data(group);
        g_hooks.update_action_area(0);
    }
    dun_set_current_map(saved);
}
