/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Minion maps: the mana of the map panel, its option buttons, and the clicks that send a minion. */
#include "minion.h"
#include "action.h"
#include "ai.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "gfx.h"
#include "hooks.h"
#include "ids.h"
#include "item.h"
#include "move.h"

/* Game ticks between the mana points of an open minion map. */
#define MINION_MAP_MANA_TICKS 25

MinionMap g_minion_map;

/* The mana of each option button. */
static const uint8_t option_mana[MAP_OPTION_COUNT] = {2, 3, 1, 1};

void minion_map_open(int champ)
{
    Champion *c = champion(champ);
    g_minion_map.mana_divisor = 1;
    g_minion_map.mana_timer = MINION_MAP_MANA_TICKS;
    if (!c->mana) {
        g_action.flags = MAP_OPEN;
    } else {
        c->mana--;
        c->redraw |= REDRAW_BARS;
        g_action.flags = MAP_OPEN | MAP_POWERED;
    }
    g_action.mode = ACTION_MODE_MINION_MAP;
}

void minion_map_drain_mana(void)
{
    Champion *c = champion(g_action.champion - 1);
    if (!(g_action.flags & MAP_POWERED)) {
        if (c->mana) {
            /* the champion has mana again: the map opens anew */
            g_action.redraw_delay = 1;
            g_action.flags = 0;
        }
        return;
    }
    if (--g_minion_map.mana_timer)
        return;
    if (!c->mana) {
        g_action.redraw_delay = 1;
        g_action.flags = 0;
        return;
    }
    c->mana--;
    c->redraw |= REDRAW_BARS;
    g_minion_map.mana_timer = (int16_t)(MINION_MAP_MANA_TICKS / g_minion_map.mana_divisor);
}

void minion_toggle_map_option(int option)
{
    int mana = option_mana[option];
    int bit = 1 << option;
    if (!(g_action.flags & bit)) {
        Champion *c = champion(g_action.champion - 1);
        if (c->mana < mana)
            return;
        c->mana = (int16_t)(c->mana - mana);
        c->redraw |= REDRAW_BARS;
    } else {
        mana = -mana;
    }
    g_action.flags ^= (uint16_t)bit;
    g_minion_map.mana_divisor = (int16_t)(g_minion_map.mana_divisor + mana);
}

bool minion_map_set_location(uint16_t thing, int16_t x, int16_t y, int16_t map)
{
    int saved = g_party.cur_map;
    dun_set_current_map(map);
    bool inside = x >= 0 && x < dungeon_map_width(g_party.cur_map) && y >= 0 && y < dungeon_map_height(g_party.cur_map);
    if (inside)
        thing_set_word(thing, MINION_MAP_LOCATION, (uint16_t)location_pack(x, y, map));
    dun_set_current_map(saved);
    return inside;
}

void minion_map_click_set_destination(int command, int16_t x, int16_t y)
{
    uint16_t thing = champion(g_action.champion - 1)->slots[g_action.hand];
    if (command != CMD_MINION_MAP || container_type(thing) != MAP_KIND_MINION)
        return;
    int16_t r[RECT_SIZE];
    g_hooks.zone_rect(ZONE_MAP_GRID, r);
    int16_t px = (int16_t)(x - (r[RECT_X] - g_minion_map.x_offset));
    int16_t py = (int16_t)(y - (r[RECT_Y] - g_minion_map.y_offset));
    if (px % (MAP_ICON_W + GAP_X) < GAP_X || py % (MAP_ICON_H + GAP_Y) < GAP_Y)
        return;
    uint16_t group = thing_word(thing, MINION_MAP_GROUP);
    if (!item_find_link_projectile(group, thing))
        return;
    int col = px / (MAP_ICON_W + GAP_X);
    int row = py / (MAP_ICON_H + GAP_Y);
    int16_t sx = g_minion_map.x;
    int16_t sy = g_minion_map.y;
    dun_move_relative(g_minion_map.dir, GRID_RADIUS - row, col - GRID_RADIUS, &sx, &sy);
    int map = dungeon_location_on_map(g_minion_map.map, 0, &sx, &sy);
    if (map < 0)
        return;
    int saved = g_party.cur_map;
    dun_set_current_map(map);
    uint8_t square = dun_get_square(sx, sy);
    int type = square_type(square);
    if (type != SQUARE_WALL && type != SQUARE_UNKNOWN &&
        (type != SQUARE_FAKE_WALL || square & (SQUARE_FAKE_WALL_OPEN | SQUARE_FLAG_0))) {
        if (sx == g_minion_map.marker_x && sy == g_minion_map.marker_y && map == g_minion_map.marker_map) {
            /* a click on the marker of a stairs or a pit goes to the level that it leads to */
            if (g_minion_map.marker_state == MARKER_SHOWN && g_minion_map.above_map >= 0) {
                sx = g_minion_map.above_x;
                sy = g_minion_map.above_y;
                map = g_minion_map.above_map;
            } else if (g_minion_map.below_map >= 0 &&
                       (g_minion_map.marker_state == MARKER_SHOWN || g_minion_map.marker_state == MARKER_BELOW)) {
                sx = g_minion_map.below_x;
                sy = g_minion_map.below_y;
                map = g_minion_map.below_map;
            }
        }
        minion_map_set_location(thing, sx, sy, map);
        ai_free_cached_data(group);
        g_hooks.update_action_area(0);
    }
    dun_set_current_map(saved);
}

void minion_dismiss(uint16_t group)
{
    uint16_t projectile = item_find_link_projectile(group, THING_NONE);
    int saved = g_party.cur_map;
    if (!projectile)
        return;
    uint16_t at = thing_word(projectile, PROJECTILE_LOCATION);
    dun_set_current_map(location_map(at));
    creature_set_next_action(group, location_x(at), location_y(at), CREATURE_ACTION_VANISH, 1);
    dun_set_current_map(saved);
}
