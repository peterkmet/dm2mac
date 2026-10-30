/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The discard search: a full dungeon frees a record of a thing far from the party. */
#include "discard.h"
#include <stdlib.h>
#include "creature.h"
#include "dungeon.h"
#include "item.h"
#include "move.h"
#include "projectile.h"
#include "save.h"
#include "sensor.h"

/* The discard search. */
#define SENSOR_KIND_TABLE 44 /* the sensor kinds of sensor_blocks_discard */
#define DISCARD_NEAR      5  /* the things this near to the party on its map stay */

/* A square of the discard search, and the static creature there whose possessions the search reads (THING_NONE
   none). */
typedef struct {
    int16_t map, x, y;
    uint16_t holder;
} DiscardSquare;

/* The sensor kinds whose square keeps its things. */
static const uint8_t sensor_blocks_discard[SENSOR_KIND_TABLE] = {
    0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1,
};

uint8_t g_last_discarded_map[THING_TYPE_COUNT];

/* The next map of the discard search after map: the party map comes last; alt (the map of a seamless passage, -1
   none) comes when the search is back at stop. */
static int16_t next_map(int16_t map, int stop, int *alt)
{
    int count = dungeon_map_count();
    if (*alt == -1) {
        do {
            if (++map >= count)
                map = 0;
        } while (map == g_game.party_map);
        return map == stop ? g_game.party_map : map;
    }
    do {
        if (++map >= count)
            map = 0;
        if (map == stop) {
            map = *alt;
            *alt = -1;
        }
    } while (map == *alt || map == g_game.party_map);
    return map;
}

/* Takes the thing t of type from the square at (at.holder: the static creature that carries it) when it may go;
   true then, and *reserved drops the item kind that a shop display keeps. */
static bool take(uint16_t t, int type, int subtype, DiscardSquare at, int *reserved)
{
    int kind;
    switch (type) {
    case THING_GROUP:
        if (group_flag(t, GROUP_DO_NOT_DISCARD) || thing_word(t, GROUP_POSSESSIONS) != THING_ENDOFLIST ||
            (subtype != DISCARD_ANY_KIND && subtype != group_creature(t)))
            return false;
        dungeon_set_current_map(at.map);
        creature_delete_group_at((Location){at.x, at.y}, DROP_POSSESSIONS, false);
        return true;
    case THING_PROJECTILE:
        if (subtype != DISCARD_ANY_KIND && item_gdat_index(thing_word(t, PROJECTILE_PAYLOAD)) != subtype)
            return false;
        projectile_cancel_event(t);
        dungeon_unlink_thing_from_square(t, at.map, at.x, at.y); /* fix: the original unlinks from the current map */
        projectile_destroy(t, NULL, at.x, at.y);
        return true;
    case THING_WEAPON:
    case THING_ARMOUR:
    case THING_MISC:
        if (item_is_kept(t))
            return false;
        kind = item_type(t);
        break;
    case THING_POTION:
        if (item_is_kept(t))
            return false;
        kind = potion_type(t);
        break;
    default:
        return true; /* kept quirk: the original returns the other types without taking them from their square */
    }
    if (kind == *reserved) {
        *reserved = DISCARD_ANY_KIND;
        return false;
    }
    if (subtype != DISCARD_ANY_KIND && kind != subtype)
        return false;
    dungeon_set_current_map(at.map);
    move_thing(t, at.x, at.y, MOVE_NO_SQUARE, at.holder != THING_NONE ? MOVE_IN_POSSESSIONS : 0);
    return true;
}

/* Searches map for a thing of type that may go and takes it; THING_NONE when there is none. The second pass on the
   party map lets a shop display keep one item of its kind and lets the other things of its square go. */
static uint16_t scan_map(int type, int subtype, int16_t map, bool second)
{
    for (int x = 0; x < dungeon_map_width(map); x++)
        for (int y = 0; y < dungeon_map_height(map); y++) {
            if (!(dungeon_square(map, x, y) & SQUARE_HAS_THINGS))
                continue;
            int reserved = DISCARD_ANY_KIND;
            bool near = map == g_game.party_map && abs(x - g_game.party_x) <= DISCARD_NEAR &&
                        abs(y - g_game.party_y) <= DISCARD_NEAR;
            DiscardSquare at = {map, x, y, THING_NONE};
            uint16_t t = dungeon_square_first_thing(map, x, y);
            for (;;) {
                int tt = thing_type(t);
                bool stop = false;
                if (tt == THING_SENSOR) {
                    int kind = sensor_type(t);
                    if (kind < SENSOR_KIND_TABLE && sensor_blocks_discard[kind])
                        stop = true;
                    else if (second && near && kind == SENSOR_SHOP) {
                        near = false;
                        if (item_index_to_thing_type(sensor_data(t)) == type)
                            reserved = item_index_gdat_index(sensor_data(t));
                    }
                } else if (tt == THING_TEXT) {
                    /* an item holder protects the things of its square */
                    stop = textrec_kind(t) == TEXT_KIND_ORNAMENT && textrec_subtype(t) == SUBTYPE_ITEM_HOLDER;
                } else if (!(tt > THING_SENSOR && near)) {
                    if (tt == type) {
                        if (take(t, type, subtype, at, &reserved))
                            return t;
                    } else if (tt == THING_GROUP && type != THING_PROJECTILE &&
                               thing_word(t, GROUP_POSSESSIONS) != THING_ENDOFLIST &&
                               creature_group_info(t)->flags & CREATURE_STATIC) {
                        at.holder = t;
                        t = thing_word(t, GROUP_POSSESSIONS);
                        continue;
                    }
                }
                t = stop ? THING_ENDOFLIST : dungeon_next_thing(t);
                while (t == THING_ENDOFLIST && at.holder != THING_NONE) {
                    t = dungeon_next_thing(at.holder);
                    at.holder = THING_NONE;
                }
                if (t == THING_ENDOFLIST)
                    break;
            }
        }
    return THING_NONE;
}

uint16_t discard_find_thing(int type, int subtype)
{
    if (type == THING_EXPLOSION)
        return THING_NONE;
    int saved = g_current_map.map;
    int alt = g_current_map.alt_view_active ? g_current_map.alt_view_map : -1;
    int map = g_last_discarded_map[type];
    /* fix: the original loops forever in next_map on a dungeon of one map */
    if ((map == g_game.party_map || map == alt) && dungeon_map_count() > 1)
        map = next_map(map, map, &alt);
    int start = map;
    for (bool second = false;;) {
        uint16_t t = scan_map(type, subtype, map, second);
        if (t != THING_NONE) {
            dungeon_set_current_map(saved);
            g_last_discarded_map[type] = (uint8_t)map;
            return t & THING_ID_MASK;
        }
        if (map != g_game.party_map && dungeon_map_count() > 1) {
            map = next_map(map, start, &alt);
        } else if (second) {
            g_last_discarded_map[type] = (uint8_t)map;
            return THING_NONE;
        } else {
            second = true;
        }
    }
}
