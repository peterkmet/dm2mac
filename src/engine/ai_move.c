/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Creature perception and movement: what blocks a square or a line of sight, the item filters, and the tests of the AI
   commands (move, attack, doors, items, wall actuators). */
#include "ai.h"
#include <stdlib.h>
#include <string.h>
#include "ai_internal.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "gdat.h"
#include "item.h"
#include "le.h"
#include "money.h"
#include "move.h"
#include "num.h"
#include "projectile.h"
#include "random.h"
#include "save.h"
#include "sensor.h"

/* The GDAT values of a door type. */
#define DOOR_KEY_SEE_THROUGH      0x0d /* creatures see through the closed door */
#define DOOR_KEY_SOMETIMES_SEEN   0x10 /* creatures see through the moving door half of the time */
#define DOOR_KEY_PASS_COST        0x11 /* the search cost of a door square */
/* The GDAT values of a wall ornament. */
#define ORNAMENT_KEY_TARGET       0x0e /* the filter value of a wall actuator ornament */
#define ORNAMENT_KEY_SHOOT_TARGET 0xf0 /* a target that ranged creatures shoot at */
/* The GDAT values of a wall set. */
#define WALLSET_KEY_WALL_COST     0x72 /* the search cost of a wall square */

/* The search costs and the light radius of the squares. */
#define DOOR_DEFAULT_PASS_COST 6
#define DOOR_COST_CHEAP        3  /* a door of this search cost or less costs little when half open */
#define WALL_COST_BLOCKING     50 /* a wall of this search cost or more stops the search */

/* The state of a creature slot (its sensor effect bits) from which the slot is busy. */
#define SLOT_STATE_BUSY    2
/* g_ai.actuator_flags. */
#define ACTUATOR_SKILL_USE 0x0001 /* the creature uses wall actuators */

/* The explosion types that the AI notices. */
#define EXPLOSION_OPEN_DOOR     0x04
#define EXPLOSION_POISON_CLOUD  0x07
#define EXPLOSION_DOOR_SPELL    0x0d
#define EXPLOSION_LASTING_CLOUD 0x0e /* blocks the sight */
/* The payload of a missile that is an explosion: this plus the explosion type. */
#define EXPLOSION_THING_BASE    0xff80

/* g_ai.attack_flags. */
#define ATTACK_HIT               0x0001
#define ATTACK_HIT_2             0x0002
#define ATTACK_THROW             0x0008
#define ATTACK_FIREBALL          0x0010
#define ATTACK_HARM_NON_MATERIAL 0x0020
#define ATTACK_LIGHTNING         0x0040
#define ATTACK_POISON_CLOUD      0x0080
#define ATTACK_POISON_BOLT       0x0100
#define ATTACK_SLIME             0x0200
#define ATTACK_PUSH              0x0400
#define ATTACK_PULL              0x0800
#define ATTACK_MELEE_MASK        0x0007
#define ATTACK_RANGED_MASK       0x0ff8
#define ATTACK_STEAL             0x0004
/* g_ai.item_flags. */
#define ITEM_SKILL_SAME_SQUARE   0x0001
#define ITEM_SKILL_WALL          0x0004
#define ITEM_SKILL_STATIC        0x0008
#define ITEM_SKILL_CHESTS        0x0010
#define ITEM_SKILL_GROUPS        0x0020
#define ITEM_SKILL_OTHER_SQUARE  0x002a
/* Sub-commands of the shoot action (ActiveGroup.sub). */
#define SHOT_STEAL               0x0b

/* The item filters of the AI (the filter argument of the item tests). */
#define FILTER_INVERT              0x80
#define FILTER_MASK                0x3f
#define FILTER_ANY                 0x3f
#define FILTER_FULL_MONEYBOX       0x3e
#define FILTER_EMPTY_MONEYBOX      0x29
#define FILTER_EMPTY_MONEYBOX_ELSE 0x07 /* the filter of filter 0x29 for a thing that is not a money box */
#define FILTER_BY_VARIANT          0x10 /* filters 0x10 to 0x12: one set for each of three variants of the group */
#define FILTER_BY_VARIANT_END      0x13
#define FILTER_MONEYBOX_OR_VARIANT 0x28
/* The variants of a group that have their own item filter. */
#define FILTER_VARIANTS            3

/* The search counts and distances. */
#define MISSILE_WATCH_RANGE   3 /* the squares in which a creature sees the missiles that fly toward it */
/* The side of a cell as the creature that takes an item sees it (take_floor_action). */
#define TAKE_SIDE_BACK_LEFT   0
#define TAKE_SIDE_FRONT_LEFT  1
#define TAKE_SIDE_FRONT_RIGHT 2
#define TAKE_SIDE_BACK_RIGHT  3
/* The cells of a square that a creature reaches from the side. */
#define CELLS_NEAR            0x03 /* the two cells on the near side */
#define CELLS_FAR             0x0c

/* The slope of a line in 1/64. */
#define SLOPE_UNIT     64
#define SLOPE_VERTICAL 128 /* the main delta is 0 */

/* Random bits of the sight of a creature. */
#define ROLL_LOOK_AROUND 0x0007 /* a creature that does not look that way looks in 1 of 8 tests */

/* The commands past the enabled table read the globals and the data after it in SKULL.EXE. */
#define COMMAND_PAST_TABLE    0x14 /* the first command past the enabled table: it reads globals */
#define COMMAND_AFTER_GLOBALS 0x20 /* ... the commands that read the bytes after the globals */

/* A move of the group: the squares, the directions, the action, and the result that the test sets for the execution. */
typedef struct {
    int16_t sx, sy, tx, ty, d, action, facing;
    int kind;
    bool result, map_changed, attack_group, same;
} Move;

/* A door action of the group: the mode, the door skills that the test leaves, the door, the squares, the direction,
   and the distance to the door. */
typedef struct {
    int mode;
    uint16_t flags, door;
    Location from, to;
    int16_t d, distance;
} DoorAction;

/* The result of the door test: act on the door, the door is as wanted, a spell is on the way, or no action. */
typedef enum { DOOR_TEST_ACT, DOOR_TEST_AS_WANTED, DOOR_TEST_SPELL_ON_WAY, DOOR_TEST_FAIL } DoorTest;

/* A take or give of the group: the mode, the item filter, the side, and the action and cell that the test finds. */
typedef struct {
    int mode;
    int16_t filter;
    bool same; /* the item is on the square of the group */
    int16_t d, action, cell;
} ItemTest;

/* The actions of a take from the same square (0, 1) or from the square in front (2, 3) by a random bit. */
static const uint8_t drop_actions[] = {ACTION_DROP_HERE, ACTION_DROP_HERE_2, ACTION_DROP_AHEAD_2, ACTION_DROP_AHEAD};

AiSquareScan g_ai_square_scan;
int16_t g_cached_light_sum = -1;
int16_t g_cached_explosion_light_sum = -1;
int16_t g_last_party_map = -1;
/* The item class bitmap of a filter that the AI does not cache. */
static uint8_t uncached_bitmap[ITEM_CLASS_SET_SIZE];

/* The outcome of a command test that passed or not, with the command result that the AI context holds. */
static AiCommand test_outcome(bool passed)
{
    return (AiCommand){passed, g_ai.command_result};
}

/* Fails the command: when the test executes, the creature reports the failure. */
static AiCommand fail_command(bool execute)
{
    if (execute)
        g_ai.command_result = AI_RESULT_FAIL;
    return test_outcome(false);
}

uint8_t *ai_search_cell(uint8_t *cells, int16_t x, int16_t y)
{
    return cells + x * SEARCH_COLUMN_SIZE + y * SEARCH_CELL_SIZE;
}
/* ---- Squares ---- */

static int map_width(void)
{
    return dungeon_map_width(g_current_map.map);
}
static int map_height(void)
{
    return dungeon_map_height(g_current_map.map);
}

bool ai_on_map(int16_t x, int16_t y)
{
    return x >= 0 && x < map_width() && y >= 0 && y < map_height();
}

/* The index of dir into g_dir_dx and g_dir_dy; for dir -1 the original reads the step (0, -1), north. */
static int16_t step_dir(int16_t dir)
{
    return dir == -1 ? DIR_NORTH : dir;
}

/* The first thing of (x, y) of the current map; none away from the map. */
static uint16_t first_thing(int16_t x, int16_t y)
{
    return ai_on_map(x, y) ? dungeon_square_first_thing(g_current_map.map, x, y) : THING_ENDOFLIST;
}

/* The first object (not a door, teleporter, text, or sensor) of the thing list from t. */
static uint16_t skip_to_object(uint16_t t)
{
    while (t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR)
        t = dungeon_next_thing(t);
    return t;
}

/* The first object of (x, y) of the current map. */
static uint16_t first_object(int16_t x, int16_t y)
{
    return skip_to_object(first_thing(x, y));
}

uint16_t ai_pack_location(int16_t x, int16_t y, int16_t map)
{
    return (uint16_t)location_pack(x, y, map);
}

/* A GDAT value of the door type on (x, y). */
static int read_door_value(int16_t x, int16_t y, int key)
{
    return gdat_entry_index(gdat_current(), CAT_DOOR, dungeon_door_type(g_current_map.map, first_thing(x, y)),
                            GDAT_TYPE_VALUE, key);
}

/* The search cost of the door on (x, y). */
static int pass_cost(int16_t x, int16_t y)
{
    int cost = read_door_value(x, y, DOOR_KEY_PASS_COST);
    if (cost)
        return cost;
    return read_door_value(x, y, DOOR_KEY_SOMETIMES_SEEN) ? 1 : DOOR_DEFAULT_PASS_COST;
}

int16_t ai_own_x(void)
{
    return g_ai.event.data[EVENT_X];
}
int16_t ai_own_y(void)
{
    return g_ai.event.data[EVENT_Y];
}

/* True for the square of the group in the AI context. */
static bool at_group(int16_t x, int16_t y)
{
    return x == ai_own_x() && y == ai_own_y();
}

bool ai_at_party(int16_t x, int16_t y)
{
    return g_current_map.map == g_ai_party.map && x == g_ai_party.x && y == g_ai_party.y;
}

uint32_t ai_class_flags(void)
{
    return g_ai_class_flags[g_ai.class_index];
}

int ai_enabled_byte(int command)
{
    command = (uint8_t)command;
    if (command < (int)sizeof g_ai_command_enabled)
        return g_ai_command_enabled[command];
    if (command < COMMAND_AFTER_GLOBALS) {
        const int16_t words[] = {g_party_distance.map,         g_alt_distance.map, g_cached_light_sum,
                                 g_cached_explosion_light_sum, g_last_party_map,   -1};
        return (uint8_t)(words[(command - COMMAND_PAST_TABLE) / 2] >> ((command & 1) * BYTE_BIT_COUNT));
    }
    if (command < COMMAND_AFTER_GLOBALS + (int)sizeof g_ai_command_enabled_overrun)
        return g_ai_command_enabled_overrun[command - COMMAND_AFTER_GLOBALS];
    return 0; /* fix: further bytes are other globals of SKULL.EXE */
}

/* Sets the destination of the action to (x, y) of the current map. */
static void set_action_destination(int16_t x, int16_t y)
{
    g_ai.active->destination = ai_pack_location(x, y, g_current_map.map);
}

bool ai_square_is_obstacle(Location at)
{
    uint8_t square = dungeon_square_at(at);
    int type = square_type(square);
    if (type == SQUARE_DOOR)
        return (square & SQUARE_DOOR_STATE) == DOOR_STATE_CLOSED && !read_door_value(at.x, at.y, DOOR_KEY_SEE_THROUGH);
    return type == SQUARE_WALL || type == SQUARE_UNKNOWN ||
           (type == SQUARE_FAKE_WALL && !(square & SQUARE_FAKE_WALL_OPEN));
}

bool ai_square_blocks_line(Location at)
{
    int16_t sx = at.x;
    int16_t sy = at.y;
    uint8_t square = dungeon_square_at((Location){sx, sy});
    int type = square_type(square);
    if (type == SQUARE_WALL)
        return true;
    if (type == SQUARE_DOOR) {
        int state = square & SQUARE_DOOR_STATE;
        if ((state == DOOR_STATE_THREE_FOURTHS || state == DOOR_STATE_CLOSED) &&
            (!read_door_value(sx, sy, DOOR_KEY_SOMETIMES_SEEN) || !rand_bit()))
            return true;
    }
    if (type == SQUARE_FAKE_WALL && !(square & SQUARE_FAKE_WALL_OPEN))
        return true;
    if (!(square & SQUARE_HAS_THINGS))
        return false;
    for (uint16_t t = first_object(sx, sy); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        int ttype = thing_type(t);
        if (ttype == THING_EXPLOSION && explosion_type(t) == EXPLOSION_LASTING_CLOUD)
            return true;
        if (ttype != THING_GROUP)
            continue;
        /* the original passes the square variables: a group found next to it moves them */
        GroupNear near = creature_find_group_near_point((Location){sx, sy}, CELL_ANY);
        sx = near.at.x;
        sy = near.at.y;
        uint16_t g = near.group;
        if (g == THING_NONE)
            continue;
        uint16_t flags = creature_group_info(g)->flags;
        if (flags & CREATURE_STATIC) {
            if (creature_door_size(creature_group_info(g)) < DOOR_STATE_HALF)
                return true;
        } else if (!(flags & CREATURE_NON_MATERIAL)) {
            return true;
        }
    }
    return false;
}

bool ai_blocks_line_to_target(Location at)
{
    if (ai_at_party(at.x, at.y))
        return true;
    return ai_square_blocks_line(at);
}

bool ai_blocks_line_except_self(Location at)
{
    if (at_group(at.x, at.y))
        return false;
    return ai_square_blocks_line(at);
}

/* The slope of the line from (x1, y1) to (x, y) in 1/64, steep: by y; SLOPE_VERTICAL when the main delta is 0. */
static int16_t line_slope(bool steep, int16_t x1, int16_t y1, int16_t x, int16_t y)
{
    int16_t major = (int16_t)(steep ? y - y1 : x - x1);
    if (!major)
        return SLOPE_VERTICAL;
    return (int16_t)((steep ? x - x1 : y - y1) * SLOPE_UNIT / major);
}

int16_t ai_trace_line(Location viewer, Location target, bool (*blocked)(Location at))
{
    int16_t x1 = viewer.x;
    int16_t y1 = viewer.y;
    int16_t x2 = target.x;
    int16_t y2 = target.y;
    if (abs_int((int16_t)(x1 - x2)) + abs_int((int16_t)(y1 - y2)) <= 1)
        return 1;
    int16_t dx = abs_int((int16_t)(x2 - x1));
    int16_t dy = abs_int((int16_t)(y2 - y1));
    bool steep = dx < dy;
    bool diagonal = dx == dy;
    int16_t step_x = (int16_t)(x2 - x1) > 0 ? -1 : 1;
    int16_t step_y = (int16_t)(y2 - y1) > 0 ? -1 : 1;
    int16_t slope = line_slope(steep, x1, y1, x2, y2);
    int16_t x = x2;
    int16_t y = y2;
    do {
        if (diagonal) {
            if (blocked((Location){x + step_x, y}) && blocked((Location){x, y + step_y}))
                return 0;
            y += step_y;
            x += step_x;
            if (blocked((Location){x, y}))
                return 0;
        } else {
            int16_t err_x = abs_int((int16_t)(line_slope(steep, x1, y1, x + step_x, y) - slope));
            int16_t err_y = abs_int((int16_t)(line_slope(steep, x1, y1, x, y + step_y) - slope));
            if (err_x < err_y)
                x += step_x;
            else
                y += step_y;
            if (blocked((Location){x, y})) {
                if (err_x != err_y)
                    return 0;
                y -= step_y; /* a tie: the other step */
                x += step_x;
                if (blocked((Location){x, y}))
                    return 0;
            }
        }
    } while (abs_int((int16_t)(x - x1)) + abs_int((int16_t)(y - y1)) > 1);
    return ai_manhattan((Location){x1, y1}, (Location){x2, y2});
}

void ai_cache_square(Location at)
{
    if (at.x == g_ai_square.x && at.y == g_ai_square.y && g_current_map.map == g_ai_square.map)
        return;
    g_ai_square.map = g_current_map.map;
    g_ai_square.x = at.x;
    g_ai_square.y = at.y;
    g_ai_square.square = dungeon_square_at(at);
    g_ai_square.first_thing = g_ai_square.square & SQUARE_HAS_THINGS ? THING_NONE : THING_ENDOFLIST;
    g_ai_square.first_object = g_ai_square.first_thing;
    g_ai_square.first_group = g_ai_square.first_thing;
    g_ai_square_scan.blocking = 0;
    g_ai_square_scan.passage = 0;
    g_ai_square_scan.items_elsewhere = -1;
    g_ai_square.cost = 1;
}

uint16_t ai_cached_first_thing(void)
{
    if (g_ai_square.first_thing == THING_NONE)
        g_ai_square.first_thing = first_thing(g_ai_square.x, g_ai_square.y);
    return g_ai_square.first_thing;
}

uint16_t ai_cached_first_object(void)
{
    if (g_ai_square.first_object != THING_NONE)
        return g_ai_square.first_object;
    uint16_t t = skip_to_object(ai_cached_first_thing());
    g_ai_square.first_object = t;
    return t;
}

uint16_t ai_cached_first_group(void)
{
    if (g_ai_square.first_group != THING_NONE)
        return g_ai_square.first_group;
    uint16_t t = ai_cached_first_object();
    while (t != THING_ENDOFLIST && thing_type(t) != THING_GROUP)
        t = dungeon_next_thing(t);
    g_ai_square.first_group = t;
    return t;
}

AiCommand ai_turn_toward(int16_t dir)
{
    int16_t facing = group_dir(g_ai.group_thing);
    int turn;
    if (facing == dir_opposite(dir)) {
        turn = rand_bit() ? 1 : -1;
    } else if (facing == dir) {
        g_ai.active->action = ACTION_NONE;
        g_ai.command_result = AI_RESULT_DONE;
        return test_outcome(false);
    } else {
        turn = facing == dir_turn(dir, TURN_LEFT) ? 1 : -1;
    }
    g_ai.active->facing = (uint8_t)dir_turn(facing, turn);
    g_ai.active->action = (uint8_t)(ACTION_TURN_LEFT + (turn != -1));
    g_ai.command_result = AI_RESULT_ISSUED;
    return test_outcome(true);
}

/* True when the wall face of (x, y) toward side shows an ornament that is a shooting target of filter. */
static bool wall_shows_shoot_target(int16_t x, int16_t y, int16_t side, int16_t filter)
{
    for (uint16_t t = first_thing(x, y); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        int type = thing_type(t);
        if ((type != THING_SENSOR && type != THING_TEXT) || thing_cell(t) != side)
            continue;
        int ornament = item_gdat_index(t);
        if (ornament != GDAT_NO_INDEX && gdat_entry_index(gdat_current(), CAT_WALL_ORNAMENT, ornament, GDAT_TYPE_VALUE,
                                                          ORNAMENT_KEY_SHOOT_TARGET) == (uint16_t)filter)
            return true;
    }
    return false;
}

/* The square to attack from: from the square from back along the line in direction back, away from the target, while
   the search reached the squares no later and the attack range allows it; step: the step of the target. */
static Location back_off_line(uint8_t *distance_map, Location from, int16_t back, int step, int range)
{
    int16_t sx = from.x;
    int16_t sy = from.y;
    int limit = *ai_search_cell(distance_map, sx, sy);
    for (int i = step + 1; i < range; i++) {
        int16_t px = sx;
        int16_t py = sy;
        sx += g_dir_dx[back];
        sy += g_dir_dy[back];
        /* fix: no read outside the distance map; y = -1 with x > 0 and x = width below MAP_SIDE_MAX read another
           cell, as in the original */
        int offset = sx * SEARCH_COLUMN_SIZE + sy * SEARCH_CELL_SIZE;
        if (offset < 0 || offset >= MAP_SIDE_MAX * SEARCH_COLUMN_SIZE) {
            sx = px;
            sy = py;
            break;
        }
        uint8_t reached = *ai_search_cell(distance_map, sx, sy);
        if (!reached || reached > limit || ai_blocks_line_to_target((Location){sx, sy})) {
            sx = px;
            sy = py;
            break;
        }
    }
    return ai_location(sx, sy);
}

AiLineTarget ai_find_target_in_lines(int16_t filter, uint8_t *distance_map, Location from, int16_t dir, bool ranged)
{
    AiLineTarget none = {false, 0};
    int range = g_ai.attack_flags & ATTACK_RANGED_MASK ? creature_attack_range(g_ai.info) : 1;
    int16_t sx = from.x;
    int16_t sy = from.y;
    int16_t d = dir;
    int16_t left = 0;
    if (d == -1) {
        d = DIR_NORTH;
        left = DIR_COUNT - 1;
    }
    int16_t tx = 0;
    int16_t ty = 0;
    int16_t i;
    for (;; d++, left--) {
        if (left < 0)
            return none;
        tx = sx;
        ty = sy;
        bool found = false;
        for (i = 0; i < range; i++) {
            tx += g_dir_dx[d];
            ty += g_dir_dy[d];
            if (!ai_on_map(tx, ty))
                continue;
            uint8_t square = dungeon_square_at((Location){tx, ty});
            if (square & SQUARE_HAS_THINGS) {
                if (square_type(square) == SQUARE_WALL) {
                    if (!ranged)
                        break;
                    found = wall_shows_shoot_target(tx, ty, dir_opposite(d), filter);
                    if (found)
                        break;
                } else if (!ranged && !at_group(tx, ty)) {
                    uint16_t g = creature_get_group_at((Location){tx, ty});
                    if (g != THING_NONE && ai_thing_matches_filter(g, filter)) {
                        found = true;
                        break;
                    }
                }
            }
            if (*ai_search_cell(distance_map, tx, ty) || ai_blocks_line_to_target((Location){tx, ty}))
                break;
        }
        if (found)
            break;
    }
    int16_t back = dir_opposite(d);
    Location at = back_off_line(distance_map, from, back, i, range);
    if (!ai_try_attack((AiTest){ranged ? ATTACK_MODE_ANY : ATTACK_MODE_GROUP, false}, at, ai_location(tx, ty),
                       dir_opposite(back))
             .passed)
        return none;
    ai_cache_square(at);
    AiLineTarget target = {true, ai_pack_location(tx, ty, g_current_map.map)};
    return target;
}

/* The champion that holds an item of FILTER_WEAPON_HANDS in a hand in the cells next to (x, y): its cell, -1 none. */
static int16_t steal_target_cell(int16_t x, int16_t y)
{
    int16_t cell = -1;
    for (int16_t i = 0; i < DIR_COUNT; i++) {
        int c = move_nearest_champion_at((Location){x, y}, i);
        if (c == -1)
            continue;
        uint16_t action = champion(c)->slots[SLOT_ACTION_HAND];
        uint16_t ready = champion(c)->slots[SLOT_READY_HAND];
        if (!((action != THING_NONE && ai_thing_matches_filter(action, FILTER_WEAPON_HANDS)) ||
              (ready != THING_NONE && ai_thing_matches_filter(ready, FILTER_WEAPON_HANDS))))
            continue;
        if (cell < 0 || rand_bit())
            cell = champion(c)->cell;
    }
    return cell;
}

/* True when a thing list holds an explosion of a type. */
static bool has_explosion(uint16_t first, int kind)
{
    for (uint16_t t = first; t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        if (thing_type(t) == THING_EXPLOSION && explosion_type(t) == kind)
            return true;
    return false;
}

/* A random set bit of bits; 0 for none (the original reads the bits as 32-bit numbers). */
static uint32_t random_set_bit(uint32_t bits)
{
    int count = 0;
    for (uint32_t v = bits; v; v >>= 1)
        count += v & 1;
    int nth = rand_below((uint16_t)count) + 1;
    for (uint32_t bit = 1; bit && bit <= bits; bit <<= 1)
        if (bits & bit && !--nth)
            return bit;
    return 0;
}

/* True when the creature on the square at can step to a side square: then it does not attack its own square. */
static bool can_step_off(Location at)
{
    for (int16_t d = 0; d < DIR_COUNT; d++)
        if (ai_try_move((AiMoveTest){MOVE_KIND_PLAIN, false, false, false}, at,
                        ai_location(g_dir_dx[d] + at.x, at.y + g_dir_dy[d]), d)
                .passed)
            return true;
    return false;
}

/* True when a creature that avoids doors near the party stands in such a door on (x, y) and stays out of the fight. */
static bool party_door_stops_attack(int16_t x, int16_t y)
{
    if (g_ai_party.map != g_current_map.map || !(ai_class_flags() & AI_CLASS_AVOIDS_PARTY_DOORS) ||
        g_ai.info->flags & CREATURE_NON_MATERIAL ||
        (x == ai_own_x() && y == ai_own_y() && g_current_map.map == g_ai.map))
        return false;
    return square_type(dungeon_square_at((Location){x, y})) == SQUARE_DOOR &&
           ai_manhattan((Location){x, y}, (Location){g_ai_party.x, g_ai_party.y}) <= 1 && rand_2bits();
}

/* The cell of the target that the attack aims at; r: the random bit of the attack variant, a ranged attack may flip it. */
static int16_t attack_cell(int mode, Location from, int16_t dir, bool melee, int *r)
{
    if (melee) {
        int c = mode > ATTACK_MODE_PARTY_RANGED ? -1 : move_nearest_champion_at(from, CELL_ANY);
        return c == -1 ? dir_opposite(dir_turn(dir, *r)) : champion(c)->cell; /* no champion: a near cell */
    }
    if (mode <= ATTACK_MODE_PARTY_RANGED && rand_2bits()) {
        *r = rand_bit();
        int c = *r ? dir_opposite(dir) : dir;
        if (champ_get_index_in_cell(c) == -1 && champ_get_index_in_cell(dir_turn(c, TURN_LEFT)) == -1)
            *r = 1 - *r;
    }
    return dir_turn(dir, *r);
}

/* The attacks of flags that reach a target at distance from the square from: a melee attack on the own square only
   when the creature cannot step off it, ranged attacks farther, all in the attack range; 0 for none. */
static uint16_t attacks_in_range(uint16_t flags, Location from, int16_t distance)
{
    if (distance > 1)
        flags &= ATTACK_RANGED_MASK;
    else if (distance == 0)
        flags &= ATTACK_MELEE_MASK;
    if (!flags)
        return 0;
    if (distance > creature_attack_range(g_ai.info))
        return 0;
    if (distance == 0 && can_step_off(from))
        return 0;
    return flags;
}

/* True when the attack of mode has its target on (tx, ty): the party for an attack on the party, a group for an
   attack on a group. */
static bool has_attack_target(int mode, int16_t tx, int16_t ty)
{
    if (mode <= ATTACK_MODE_PARTY_RANGED)
        return ai_at_party(tx, ty);
    if (mode == ATTACK_MODE_GROUP) {
        ai_cache_square((Location){tx, ty});
        return ai_cached_first_group() != THING_ENDOFLIST;
    }
    return true;
}

/* The attacks of flags that reach the target square to: a creature that avoids clouds keeps only melee attacks on a
   target in a lasting cloud. */
static uint16_t attacks_through_cloud(uint16_t flags, Location to)
{
    if (!(ai_class_flags() & AI_CLASS_AVOIDS_CLOUDS))
        return flags;
    ai_cache_square(to);
    if (has_explosion(ai_cached_first_object(), EXPLOSION_LASTING_CLOUD))
        flags &= ATTACK_MELEE_MASK;
    return flags;
}

/* Sets the action (and the missile) of the chosen attack bit; r selects the variant of the action. */
static void set_attack_action(uint16_t attack, int r)
{
    static const struct {
        uint16_t attack;
        uint8_t sub;
    } shots[] = {
        {ATTACK_FIREBALL,          (uint8_t)SPELL_FIREBALL         },
        {ATTACK_HARM_NON_MATERIAL, (uint8_t)SPELL_HARM_NON_MATERIAL},
        {ATTACK_LIGHTNING,         (uint8_t)SPELL_LIGHTNING        },
        {ATTACK_POISON_CLOUD,      (uint8_t)SPELL_POISON_CLOUD     },
        {ATTACK_POISON_BOLT,       (uint8_t)SPELL_POISON_BOLT      },
        {ATTACK_SLIME,             (uint8_t)SPELL_SLIME            },
        {ATTACK_PUSH,              (uint8_t)SPELL_PUSH             },
        {ATTACK_PULL,              (uint8_t)SPELL_PULL             }
    };
    if (attack == ATTACK_HIT)
        g_ai.active->action = ACTION_MELEE;
    else if (attack == ATTACK_HIT_2)
        g_ai.active->action = ACTION_MELEE_2;
    else if (attack == ATTACK_STEAL) {
        g_ai.active->action = ACTION_STEAL;
        g_ai.active->sub = SHOT_STEAL;
    } else if (attack == ATTACK_THROW) {
        g_ai.active->action = (uint8_t)(ACTION_THROW + (r != 0));
    } else {
        for (int i = 0; i < (int)(sizeof shots / sizeof shots[0]); i++)
            if (shots[i].attack == attack) {
                g_ai.active->sub = (int8_t)shots[i].sub;
                g_ai.active->action = (uint8_t)(ACTION_SHOOT + (r != 0));
            }
    }
}

AiCommand ai_try_attack(AiTest test, Location from, Location to, int16_t dir)
{
    bool execute = test.execute;
    int mode = test.mode & AI_MODE_MASK;
    uint16_t flags = g_ai.attack_flags;
    int16_t x = from.x;
    int16_t y = from.y;
    int16_t tx = to.x;
    int16_t ty = to.y;
    if (!flags)
        return fail_command(execute);
    if (tx == -1) {
        tx = (int16_t)(g_dir_dx[step_dir(dir)] + x);
        ty = (int16_t)(y + g_dir_dy[step_dir(dir)]);
    }
    if (!ai_on_map(tx, ty) || (x != tx && y != ty))
        return fail_command(execute);
    int16_t distance = ai_manhattan((Location){x, y}, (Location){tx, ty});
    flags = attacks_in_range(flags, from, distance);
    if (!flags || !has_attack_target(mode, tx, ty))
        return fail_command(execute);
    if (distance > 1 && !ai_trace_line((Location){x, y}, (Location){tx, ty}, ai_square_blocks_line))
        return fail_command(execute);
    int16_t steal_cell = -1;
    if (flags & ATTACK_STEAL) {
        steal_cell = steal_target_cell(x, y);
        if (steal_cell < 0)
            flags &= (uint16_t)~ATTACK_STEAL;
        if (!flags)
            return fail_command(execute);
    }
    flags = attacks_through_cloud(flags, (Location){tx, ty});
    if (!flags)
        return fail_command(execute);
    if (party_door_stops_attack(x, y))
        return fail_command(execute);
    if (!execute)
        return test_outcome(true);
    if (dir == -1)
        dir =
            distance == 0 && ai_at_party(tx, ty) ? dir_opposite(g_game.party_dir) : dungeon_direction_to(x, y, tx, ty);
    if (ai_turn_toward(dir).passed)
        return test_outcome(true);
    int r = rand_bit();
    bool melee = distance <= 1 && flags & ATTACK_MELEE_MASK && (!(flags & ATTACK_RANGED_MASK) || rand_bit());
    flags &= melee ? ATTACK_MELEE_MASK : ATTACK_RANGED_MASK;
    int16_t cell = attack_cell(mode, from, dir, melee, &r);
    set_attack_action((uint16_t)random_set_bit(flags), r);
    set_action_destination(tx, ty);
    g_ai.active->direction = (uint8_t)dir;
    if (g_ai.active->action == ACTION_STEAL)
        cell = steal_cell;
    g_ai.active->cell = (uint8_t)cell;
    g_ai.active->mode = (uint8_t)mode;
    g_ai.command_result = AI_RESULT_ISSUED;
    return test_outcome(true);
}

/* A door spell flying toward the door or an explosion that opens it between (x, y) and the door. */
static bool spell_on_door_line(int16_t x, int16_t y, int16_t distance, int16_t dir)
{
    for (int16_t n = distance; n >= 0; n--) {
        for (uint16_t t = first_object(x, y); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
            int type = thing_type(t);
            if (type == THING_PROJECTILE) {
                uint16_t payload = thing_word(t, PROJECTILE_PAYLOAD);
                if ((payload == EXPLOSION_THING_BASE + EXPLOSION_DOOR_SPELL ||
                     payload == EXPLOSION_THING_BASE + EXPLOSION_OPEN_DOOR) &&
                    projectile_dir(t) == dir)
                    return true;
            } else if (type == THING_EXPLOSION) {
                int kind = explosion_type(t);
                if (kind == EXPLOSION_DOOR_SPELL || kind == EXPLOSION_OPEN_DOOR)
                    return true;
            }
        }
        x = (int16_t)(x + g_dir_dx[step_dir(dir)]);
        y = (int16_t)(y + g_dir_dy[step_dir(dir)]);
    }
    return false;
}

/* True when the door skills reach the door at its distance: a button, a break, or the handle next to it; a button
   shot or a spell from afar in attack range and in sight. */
static bool skills_reach_door(const DoorAction *a)
{
    uint16_t flags = a->flags;
    bool button = door_has_button(a->door);
    bool by_magic = door_flag(a->door, DOOR_BREAKABLE_BY_MAGIC);
    if (a->distance == 1)
        return (flags & DOOR_SKILL_BUTTON && button) ||
               (a->mode == DOOR_MODE_OPEN && ((flags & DOOR_SKILL_SPELL && by_magic) ||
                                              (flags & DOOR_SKILL_BASH && door_flag(a->door, DOOR_BREAKABLE)))) ||
               flags & DOOR_SKILL_HANDLE;
    if (a->distance > creature_attack_range(g_ai.info))
        return false;
    if (!((flags & DOOR_SKILL_SHOOT_BUTTON && button) ||
          (a->mode == DOOR_MODE_OPEN && flags & DOOR_SKILL_SPELL && by_magic)))
        return false;
    return ai_trace_line(a->from, a->to, ai_square_blocks_line) != 0;
}

/* Starts the door action that the skills allow: the handle or button, a button shot, or an attack on the door. */
static AiCommand start_door_action(const DoorAction *a)
{
    uint16_t flags = a->flags;
    if (flags & DOOR_SKILL_CLOSE_BEHIND) {
        flags &= (uint16_t)~DOOR_SKILL_CLOSE_BEHIND;
        if (a->mode == DOOR_MODE_OPEN)
            door_set_flag(a->door, DOOR_MARKED, true);
    }
    if (a->distance > 1)
        flags &= DOOR_SKILL_SHOOT_BUTTON | DOOR_SKILL_SPELL;
    if (flags & (DOOR_SKILL_HANDLE | DOOR_SKILL_PRESS_BUTTON) &&
        (!(flags & (uint16_t)~(DOOR_SKILL_HANDLE | DOOR_SKILL_PRESS_BUTTON)) || rand_2bits())) {
        g_ai.active->action = ACTION_OPERATE_DOOR;
    } else if (flags & DOOR_SKILL_SHOOT_BUTTON &&
               (!(flags & (uint16_t)~(DOOR_SKILL_HANDLE | DOOR_SKILL_BUTTON)) || !rand_2bits())) {
        g_ai.active->action = (uint8_t)(ACTION_SHOOT + (rand_bit() != 0));
        g_ai.active->sub = (int8_t)(a->mode == DOOR_MODE_OPEN ? (uint8_t)SPELL_DOOR_SET : (uint8_t)SPELL_DOOR_TOGGLE);
    } else {
        uint16_t saved = g_ai.attack_flags;
        if (flags & DOOR_SKILL_BASH && saved & ATTACK_HIT &&
            (!(flags & (uint16_t)~(DOOR_SKILL_HANDLE | DOOR_SKILL_BASH | DOOR_SKILL_BUTTON)) || rand_bit()))
            g_ai.attack_flags &= ATTACK_HIT;
        else if (!(flags & DOOR_SKILL_SPELL))
            return fail_command(true);
        else
            g_ai.attack_flags &= ATTACK_FIREBALL | ATTACK_LIGHTNING;
        AiCommand r = ai_try_attack((AiTest){ATTACK_MODE_DOOR, true}, a->from, a->to, a->d);
        g_ai.attack_flags = saved;
        return r;
    }
    set_action_destination(a->to.x, a->to.y);
    g_ai.active->facing = (uint8_t)a->d;
    g_ai.active->direction = (uint8_t)a->d;
    g_ai.active->cell = (uint8_t)dir_turn(a->d, rand_bit());
    g_ai.active->mode = (uint8_t)a->mode;
    g_ai.command_result = AI_RESULT_ISSUED;
    return test_outcome(true);
}

/* Tests the door on the target square for the action: the door state, the door skills, and the spells on the way. */
static DoorTest test_door(DoorAction *a)
{
    ai_cache_square(a->to);
    if (square_type(g_ai_square.square) != SQUARE_DOOR)
        return DOOR_TEST_FAIL;
    int door_state = g_ai_square.square & SQUARE_DOOR_STATE;
    if (a->mode == DOOR_MODE_OPEN ? door_state == DOOR_STATE_OPEN : door_state == DOOR_STATE_CLOSED)
        return DOOR_TEST_AS_WANTED;
    if (a->mode != DOOR_MODE_OPEN && door_state == DOOR_STATE_BROKEN)
        return DOOR_TEST_FAIL;
    a->door = ai_cached_first_thing();
    bool moving = door_flag(a->door, DOOR_MOVING);
    bool opening = door_flag(a->door, DOOR_OPENING);
    if (a->mode == DOOR_MODE_CLOSE_MARKED && !door_flag(a->door, DOOR_MARKED))
        return DOOR_TEST_FAIL;
    if (moving && (opening ? a->mode == DOOR_MODE_OPEN : a->mode != DOOR_MODE_OPEN))
        return DOOR_TEST_SPELL_ON_WAY;
    if (moving && !opening) {
        a->flags &= (uint16_t)~(DOOR_SKILL_SPELL | DOOR_SKILL_BASH);
        if (!a->flags)
            return DOOR_TEST_FAIL;
    }
    a->distance = ai_manhattan(a->from, a->to);
    if (!skills_reach_door(a))
        return DOOR_TEST_FAIL;
    if (a->flags & DOOR_SKILL_SHOOT_BUTTON && spell_on_door_line(a->from.x, a->from.y, a->distance, a->d))
        return DOOR_TEST_SPELL_ON_WAY;
    return DOOR_TEST_ACT;
}

AiCommand ai_try_door_action(AiTest test, Location from, Location to, int16_t dir)
{
    bool execute = test.execute;
    DoorAction a = {test.mode & AI_MODE_MASK, 0, THING_NONE, from, to, dir, 0};
    a.flags = (uint16_t)((a.mode == DOOR_MODE_OPEN ? DOOR_SKILLS_OPEN : DOOR_SKILLS_CLOSE) & g_ai.door_flags);
    if (!a.flags)
        return fail_command(execute);
    if (a.to.x == -1) {
        a.to.x = (int16_t)(from.x + g_dir_dx[step_dir(a.d)]);
        a.to.y = (int16_t)(from.y + g_dir_dy[step_dir(a.d)]);
    } else if (a.d == -1) {
        a.d = dungeon_direction_to(from.x, from.y, a.to.x, a.to.y);
    } else if (a.to.x == from.x && from.y == a.to.y) {
        return fail_command(execute);
    }
    if (!ai_on_map(a.to.x, a.to.y) || (from.x != a.to.x && from.y != a.to.y))
        return fail_command(execute);
    DoorTest state = test_door(&a);
    if (state == DOOR_TEST_FAIL || (state != DOOR_TEST_ACT && a.mode == DOOR_MODE_CLOSE_MARKED))
        return fail_command(execute);
    if (!execute)
        return test_outcome(true);
    if (a.d == -1)
        a.d = dungeon_direction_to(from.x, from.y, a.to.x, a.to.y);
    if (state == DOOR_TEST_AS_WANTED) {
        g_ai.command_result = AI_RESULT_DONE;
        return test_outcome(true);
    }
    if (state == DOOR_TEST_SPELL_ON_WAY) {
        g_ai.active->action = ACTION_NONE;
        g_ai.command_result = AI_RESULT_ISSUED;
        return test_outcome(true);
    }
    if (ai_turn_toward(a.d).passed)
        return test_outcome(true);
    return start_door_action(&a);
}

bool ai_projectile_incoming(Location at)
{
    int16_t x = at.x;
    int16_t y = at.y;
    for (int16_t d = 0; d < DIR_COUNT; d++) {
        if (!(ai_class_flags() & AI_CLASS_LAZY_GUARD)) {
            bool wanders = group_mood_flag(g_ai.group_thing, MOOD_WANDERS);
            bool behind = !(g_ai.info->flags & CREATURE_FACES_PARTY) && at_group(x, y) &&
                          d == dir_opposite(group_dir(g_ai.group_thing));
            if ((wanders || behind) && rand_next() & ROLL_LOOK_AROUND)
                continue;
        }
        int16_t sx = x;
        int16_t sy = y;
        for (int16_t n = 0; n < MISSILE_WATCH_RANGE; n++) {
            sx += g_dir_dx[d];
            sy += g_dir_dy[d];
            if (!ai_on_map(sx, sy))
                break;
            for (uint16_t t = first_object(sx, sy); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
                if (thing_type(t) == THING_PROJECTILE && projectile_dir(t) == dir_opposite(d) &&
                    projectile_get_attack(t, thing_word(t, PROJECTILE_PAYLOAD)))
                    return true;
            if (!at_group(sx, sy) && ai_square_blocks_line((Location){sx, sy}))
                break;
        }
    }
    return false;
}

/* Sets the search cost of a door square (g_ai_square.cost) by its state. */
static void set_door_cost(int16_t x, int16_t y)
{
    int state = g_ai_square.square & SQUARE_DOOR_STATE;
    if (state == DOOR_STATE_OPEN || state == DOOR_STATE_BROKEN || state == DOOR_STATE_ONE_FOURTH)
        return;
    int16_t cost = (int16_t)pass_cost(x, y);
    g_ai_square.cost = cost;
    if (state == DOOR_STATE_CLOSED || cost <= 1)
        return;
    int16_t half = (int16_t)(cost >> 1);
    if (state == DOOR_STATE_HALF)
        g_ai_square.cost = cost <= DOOR_COST_CHEAP ? 1 : (int16_t)(half - (half >> 1));
    else
        g_ai_square.cost = cost <= DOOR_COST_CHEAP ? (int16_t)(cost - 1) : half;
}

/* The quick test of the cost search (MOVE_KIND_COST): may the search enter the square, at what cost. */
static bool search_square_test(int command, int16_t x, int16_t y)
{
    ai_cache_square((Location){x, y});
    uint8_t square = g_ai_square.square;
    switch (square_type(square)) {
    case SQUARE_FAKE_WALL:
        if (square & (SQUARE_FAKE_WALL_OPEN | SQUARE_FLAG_0))
            return true; /* a closed fake wall costs like a wall */
        __attribute__((fallthrough));
    case SQUARE_WALL: {
        int cost = gdat_entry_index(gdat_current(), CAT_WALLSET, dungeon_map_wallset(g_game.party_map), GDAT_TYPE_VALUE,
                                    WALLSET_KEY_WALL_COST);
        if (!cost)
            cost = 1;
        if (cost >= WALL_COST_BLOCKING) {
            g_ai_square_scan.blocking = 1;
            return false;
        }
        g_ai_square.cost = (int16_t)cost;
        return true;
    }
    case SQUARE_FLOOR:
    case SQUARE_PIT:
        return true;
    case SQUARE_DOOR:
        set_door_cost(x, y);
        return true;
    case SQUARE_TELEPORTER:
        if (square & SQUARE_TELEPORTER_ON) {
            /* an open teleporter next to a seamless passage: test the square behind it */
            PassageEnd end;
            if (!dungeon_linked_teleporter_dest(&end, (Location){x + 1, y}) &&
                !dungeon_linked_teleporter_dest(&end, (Location){x - 1, y}) &&
                !dungeon_linked_teleporter_dest(&end, (Location){x, y + 1}) &&
                !dungeon_linked_teleporter_dest(&end, (Location){x, y - 1}))
                return true;
            uint16_t link = first_thing(x, y);
            if (thing_type(link) != THING_TELEPORTER) /* fix: a damaged dungeon; the original reads another record */
                return true;
            if (end.map != teleporter_map(link) ||
                ai_manhattan((Location){end.x, end.y}, (Location){teleporter_x(link), teleporter_y(link)}) != 1)
                return true;
            int16_t map = teleporter_map(link);
            if (map != g_game.party_map && map != g_current_map.alt_view_map)
                return true;
            int16_t saved = g_current_map.map;
            ai_set_map(map);
            Location at = ai_location(teleporter_x(link), teleporter_y(link));
            bool r = ai_try_move(ai_move_test(command), at, at, DIR_KEEP_FACING).passed;
            ai_set_map(saved);
            return r;
        }
        g_ai_square_scan.passage = (uint8_t)dungeon_linked_teleporter_dest(&g_ai_square_scan.end, (Location){x, y});
        if (!g_ai_square_scan.passage)
            return true;
        return g_ai_square_scan.end.map == g_game.party_map || g_ai_square_scan.end.map == g_current_map.alt_view_map;
    default:
        return false;
    }
}

/* The move kinds that avoid poison clouds and missiles. */
static bool is_safe_kind(int kind)
{
    return kind == MOVE_KIND_SAFE || kind == MOVE_KIND_SAFE_FLEE;
}

/* The move flags that the square in the AI cache needs; 0: the creature cannot enter it. */
static uint16_t needed_move_flags(int kind, bool no_movers, bool map_changed, int16_t x, int16_t y)
{
    uint8_t square = g_ai_square.square;
    switch (square_type(square)) {
    case SQUARE_WALL:
        return MOVE_WALL;
    case SQUARE_FLOOR:
        return MOVE_FLOOR;
    case SQUARE_PIT:
        if (!(square & SQUARE_PIT_OPEN))
            return MOVE_CLOSED_PIT | MOVE_FLOOR;
        if (square & SQUARE_FLAG_0)
            return MOVE_FALLS | MOVE_INVISIBLE_PIT | MOVE_CLOSED_PIT;
        return map_changed ? MOVE_OPEN_PIT | MOVE_CLOSED_PIT : MOVE_FALLS | MOVE_OPEN_PIT | MOVE_CLOSED_PIT;
    case SQUARE_STAIRS:
        return MOVE_STAIRS;
    case SQUARE_DOOR: {
        int state = square & SQUARE_DOOR_STATE;
        if ((state == DOOR_STATE_OPEN && !is_safe_kind(kind)) || state == DOOR_STATE_BROKEN)
            return MOVE_FLOOR;
        if (state == DOOR_STATE_CLOSED)
            return MOVE_DOOR | MOVE_LOCKED_DOOR;
        uint16_t door = ai_cached_first_thing();
        bool moving = door_flag(door, DOOR_MOVING);
        bool opening = door_flag(door, DOOR_OPENING);
        if (no_movers && moving)
            return opening ? MOVE_FLOOR : MOVE_DOOR | MOVE_LOCKED_DOOR;
        if (moving && !opening)
            return MOVE_LOCKED_DOOR;
        int limit = door_flag(door, DOOR_VERTICAL) ? creature_door_size(g_ai.info) : 1;
        return state > limit ? MOVE_DOOR | MOVE_LOCKED_DOOR : MOVE_FLOOR;
    }
    case SQUARE_TELEPORTER:
        if (!(square & SQUARE_TELEPORTER_ON)) {
            if (!dungeon_linked_teleporter_dest(&g_ai_square_scan.end, (Location){x, y}))
                return MOVE_TELEPORTER | MOVE_FLOOR;
            g_ai_square_scan.passage = 1;
            return creature_is_native_to_map(g_ai.group_thing, g_ai_square_scan.end.map) ? MOVE_LEVELS : 0;
        } else {
            uint16_t tele = ai_cached_first_thing();
            if (thing_type(tele) != THING_TELEPORTER) /* fix: a damaged dungeon; the original reads another record */
                return MOVE_FLOOR;
            return teleporter_scope(tele) & TELEPORTER_SCOPE_CREATURES ? MOVE_TELEPORTER : MOVE_FLOOR;
        }
    case SQUARE_FAKE_WALL:
        if (square & SQUARE_FAKE_WALL_OPEN)
            return MOVE_FLOOR;
        return square & SQUARE_FLAG_0 ? MOVE_FAKE_WALL : MOVE_HIDDEN_FAKE_WALL;
    default:
        return 0;
    }
}

/* The level change of a move up (DIR_LEVEL_UP) or down (DIR_LEVEL_DOWN): sets the action, the facing, and the
   target square on the other map; returns the map, -1 when the creature cannot go. */
static int16_t level_change(Move *m)
{
    int16_t x = m->sx;
    int16_t y = m->sy;
    int16_t dir = m->d;
    uint16_t need = 0;
    uint16_t link = THING_NONE;
    uint8_t square = dungeon_square_at((Location){x, y});
    int type = square_type(square);
    if (type == SQUARE_PIT && dir == DIR_LEVEL_DOWN && square & SQUARE_PIT_OPEN && !(square & SQUARE_FLAG_0)) {
        need = MOVE_OPEN_PIT;
        m->action = ACTION_PIT;
        m->facing = group_dir(g_ai.group_thing);
    } else if (type == SQUARE_FLOOR || type == SQUARE_PIT || type == SQUARE_TELEPORTER || type == SQUARE_FAKE_WALL) {
        if (square & SQUARE_LEVEL_LINK) {
            LevelLink found = dungeon_find_level_link((Location){x, y}, dir == DIR_LEVEL_UP ? LEVEL_UP : LEVEL_DOWN);
            int16_t side = found.dir;
            link = found.sensor;
            m->facing = side;
            if (side >= 0) {
                m->action = dir == DIR_LEVEL_UP ? ACTION_LINK_UP : ACTION_LINK_DOWN;
                need = MOVE_STAIRS;
            } else {
                m->action = ACTION_FALL_IN_PIT;
                need = MOVE_FALL;
                m->facing = group_dir(g_ai.group_thing);
            }
        }
    } else if (type == SQUARE_STAIRS && (square & SQUARE_STAIRS_UP ? dir == DIR_LEVEL_UP : dir == DIR_LEVEL_DOWN)) {
        m->action = square & SQUARE_STAIRS_UP ? ACTION_STAIRS_UP : ACTION_STAIRS_DOWN;
        m->facing = dir_opposite(dungeon_stairs_exit_direction((Location){x, y}));
        need = MOVE_STAIRS;
    }
    if (!(g_ai.move_flags & need))
        return -1;
    Position to;
    if (link != THING_NONE && (m->action == ACTION_LINK_UP || m->action == ACTION_LINK_DOWN)) {
        to = (Position){(int16_t)sensor_data(link), sensor_target_x(link), sensor_target_y(link)};
    } else {
        to = dungeon_location_on_map((Position){g_current_map.map, x, y}, dir == DIR_LEVEL_UP ? LEVEL_UP : LEVEL_DOWN);
        if (to.map < 0)
            return -1;
        if (m->action == ACTION_FALL_IN_PIT) {
            /* fix: the original reads the square without a bounds test; a square at the map edge is not a pit */
            if (to.x < 0 || to.x >= dungeon_map_width(to.map) || to.y < 0 || to.y >= dungeon_map_height(to.map))
                return -1;
            uint8_t below = dungeon_square(to.map, to.x, to.y);
            if (square_type(below) != SQUARE_PIT || !(below & SQUARE_PIT_OPEN))
                return -1;
        }
    }
    if (!creature_is_native_to_map(g_ai.group_thing, to.map))
        return -1;
    m->tx = to.x;
    m->ty = to.y;
    return to.map;
}

/* Sets the target square and facing of the move, a level change included; false when the move cannot start. */
static bool set_move_target(Move *m)
{
    if (m->d == DIR_KEEP_FACING) {
        m->facing = group_dir(g_ai.group_thing);
    } else if (m->d >= DIR_LEVEL_UP) {
        if (!(g_ai.move_flags & MOVE_LEVELS))
            return false;
        int16_t map = level_change(m);
        if (map < 0)
            return false;
        m->map_changed = true;
        ai_set_map(map);
        m->sx = -1;
        m->sy = -1;
    } else if (m->tx == -1) {
        m->tx = (int16_t)(m->sx + g_dir_dx[step_dir(m->d)]);
        m->ty = (int16_t)(m->sy + g_dir_dy[step_dir(m->d)]);
    } else {
        m->same = m->tx == m->sx && m->sy == m->ty;
        if (!m->same && m->d == -1) {
            m->d = dungeon_direction_to(m->sx, m->sy, m->tx, m->ty);
            m->facing = m->d;
        }
    }
    return ai_on_map(m->tx, m->ty);
}

/* The result of a quick test of the search on the target square. */
static bool quick_test(Move *m)
{
    if (m->kind == MOVE_KIND_OPEN) {
        ai_cache_square((Location){m->tx, m->ty});
        uint8_t square = g_ai_square.square;
        return square_type(square) != SQUARE_WALL &&
               !(square_type(square) == SQUARE_FAKE_WALL && !(square & (SQUARE_FAKE_WALL_OPEN | SQUARE_FLAG_0)));
    }
    if (m->kind == MOVE_KIND_SIGHT) {
        bool r = !ai_square_is_obstacle((Location){m->tx, m->ty});
        if (r)
            ai_cache_square((Location){m->tx, m->ty});
        return r;
    }
    return search_square_test(m->kind, m->tx, m->ty);
}

/* True when a safe move must avoid the target square: a poison cloud that hurts, a missile, or one on the way. */
static bool is_dangerous(Move *m)
{
    bool cloud = false;
    for (uint16_t t = ai_cached_first_object(); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        int type = thing_type(t);
        if (type == THING_EXPLOSION) {
            int k = explosion_type(t);
            if (k == EXPLOSION_POISON_CLOUD) {
                if (explosion_get_attack(t, g_ai.group_thing))
                    return true;
            } else if (k == EXPLOSION_LASTING_CLOUD) {
                cloud = true;
            }
        } else if (type == THING_PROJECTILE && !at_group(m->tx, m->ty)) {
            if (projectile_dir(t) != (m->d == DIR_KEEP_FACING ? m->facing : m->d) &&
                projectile_get_attack(t, thing_word(t, PROJECTILE_PAYLOAD)))
                return true;
        }
    }
    return m->kind == MOVE_KIND_SAFE_FLEE && !cloud && ai_projectile_incoming((Location){m->tx, m->ty});
}

/* True when a creature that avoids doors near the party must not enter the door square of the move. */
static bool party_door_stops_move(Move *m)
{
    return !m->same && g_ai_party.map == g_current_map.map && ai_class_flags() & AI_CLASS_AVOIDS_PARTY_DOORS &&
           !(g_ai.info->flags & CREATURE_NON_MATERIAL) && square_type(g_ai_square.square) == SQUARE_DOOR &&
           ai_manhattan((Location){m->tx, m->ty}, (Location){g_ai_party.x, g_ai_party.y}) <= 1 &&
           (m->sx != ai_own_x() || m->sy != ai_own_y() || rand_2bits());
}

/* The move test that goes on past the square of the move m: its kind, without the execution. */
static AiMoveTest onward_test(const Move *m, bool no_movers)
{
    return (AiMoveTest){(uint8_t)m->kind, false, no_movers, false};
}

/* A move onto another map or into a teleporter: the move test goes on at the square where the creature arrives. */
static void test_move_transition(Move *m, bool no_movers)
{
    MoveDest end = move_follow_square_transitions(g_ai.group_thing, (MovePath){m->sx, m->sy, m->tx, m->ty}, false).dest;
    if (g_current_map.map == end.map && m->tx == end.x && m->ty == end.y)
        return;
    int16_t saved = g_current_map.map;
    if (saved != end.map) {
        if (!(g_ai.move_flags & MOVE_LEVELS) || !creature_is_native_to_map(g_ai.group_thing, end.map)) {
            m->result = false;
            return;
        }
        ai_set_map(end.map);
    }
    Location dest = ai_location(end.x, end.y);
    m->result = ai_try_move(onward_test(m, no_movers), dest, dest, DIR_KEEP_FACING).passed;
    ai_set_map(saved);
}

/* A move onto a pit: the creature falls through to the square beyond, or steps into the open pit. */
static void test_move_over_pit(Move *m, bool no_movers)
{
    if (m->kind == MOVE_KIND_STEP_BACK) {
        m->result = false;
    } else {
        uint16_t saved = g_ai.move_flags;
        g_ai.move_flags &= MOVE_FLAGS_NO_FALL;
        m->result = ai_try_move(onward_test(m, no_movers), ai_location(m->tx, m->ty),
                                ai_location(m->tx + g_dir_dx[m->facing], m->ty + g_dir_dy[m->facing]), m->facing)
                        .passed;
        g_ai.move_flags = saved;
    }
    if (m->result) {
        m->action = ACTION_FALL;
    } else if (g_ai.move_flags & MOVE_OPEN_PIT) {
        uint16_t saved = g_ai.move_flags;
        g_ai.move_flags &= (uint16_t)~MOVE_FALLS;
        m->result =
            ai_try_move(onward_test(m, no_movers), ai_location(m->sx, m->sy), ai_location(m->tx, m->ty), m->facing)
                .passed;
        if (m->result)
            m->action = ACTION_PIT;
        g_ai.move_flags = saved;
    }
    if (!m->result)
        g_ai_square_scan.blocking = 1;
}

/* The move flags that the party or a group on the target square of the move needs; a group of
   FILTER_ATTACKED_GROUPS gets attacked instead when the test attacks groups. */
static uint16_t occupant_move_flags(Move *m, AiMoveTest test)
{
    uint16_t also = ai_at_party(m->tx, m->ty) ? MOVE_PARTY : 0;
    if (m->same)
        return also;
    uint16_t g = ai_cached_first_group();
    if (g == THING_ENDOFLIST || (test.no_movers && creature_group_info(g)->movement))
        return also;
    if (test.attack_groups && !m->map_changed)
        m->attack_group = ai_thing_matches_filter(g, FILTER_ATTACKED_GROUPS);
    return m->attack_group ? also : also | MOVE_GROUPS;
}

/* Tests the move; true when the move continues to the execution, false when the result is final. */
static bool test_move(Move *m, AiMoveTest test)
{
    bool no_movers = test.no_movers;
    if (m->kind == MOVE_KIND_NONE)
        return true;
    int enabled = ai_enabled_byte(m->kind);
    if (!enabled && !g_ai.move_flags)
        return true;
    if (!set_move_target(m))
        return true;
    /* the quick tests of the searches: no wall, no obstacle to the sight, or the cost search */
    if (enabled && (m->kind == MOVE_KIND_OPEN || m->kind == MOVE_KIND_SIGHT || m->kind == MOVE_KIND_COST)) {
        m->result = quick_test(m);
        return false;
    }
    ai_cache_square((Location){m->tx, m->ty});
    if (g_ai.marker_mask && m->d != DIR_KEEP_FACING) {
        int16_t sides = ai_find_marker(MARKER_LEVEL_SIDES, (uint16_t)g_ai.marker_mask);
        if (sides != -1 && (1 << dir_opposite(m->d)) & sides)
            return true;
    }
    if (is_safe_kind(m->kind) && g_ai.info->defense == CREATURE_NO_HURT)
        m->kind = MOVE_KIND_PLAIN;
    uint16_t need = needed_move_flags(m->kind, no_movers, m->map_changed, m->tx, m->ty) & g_ai.move_flags;
    if (!need)
        return true;
    if (m->kind == MOVE_KIND_KEEP_DISTANCE &&
        ai_manhattan((Location){m->tx, m->ty}, (Location){g_ai_party.x, g_ai_party.y}) <= 1)
        return true;
    uint16_t also = occupant_move_flags(m, test);
    if ((g_ai.move_flags & also) != also)
        return true;
    if (is_safe_kind(m->kind) && is_dangerous(m))
        return true;
    if (party_door_stops_move(m))
        return true;
    m->result = true;
    if (m->map_changed || need == MOVE_TELEPORTER) {
        test_move_transition(m, no_movers);
        return true;
    }
    if (m->same)
        return true;
    if (need == MOVE_DOOR) {
        Location door = ai_location(m->tx, m->ty);
        m->result =
            ai_try_door_action((AiTest){DOOR_MODE_OPEN, test.execute}, ai_location(m->sx, m->sy), door, m->facing)
                .passed;
        return false;
    }
    if (need & MOVE_FALLS)
        test_move_over_pit(m, no_movers);
    return true;
}

/* Starts the tested move: a failure, an attack on a group in the way, a door action, a turn, or the step. */
static void execute_move(Move *m)
{
    if (!m->result) {
        g_ai.command_result = AI_RESULT_FAIL;
        return;
    }
    if (m->same) {
        g_ai.command_result = AI_RESULT_DONE;
        return;
    }
    if (m->attack_group &&
        ai_try_attack((AiTest){ATTACK_MODE_GROUP, true}, ai_location(m->sx, m->sy), ai_location(m->tx, m->ty), -1)
            .passed)
        return;
    if (g_ai.door_flags & DOOR_SKILL_CLOSE_BEHIND)
        for (int16_t i = 0; i < DIR_COUNT; i++) {
            if (i == m->facing)
                continue;
            int16_t nx = (int16_t)(m->sx + g_dir_dx[i]);
            int16_t ny = (int16_t)(m->sy + g_dir_dy[i]);
            if (square_type(dungeon_square_at((Location){nx, ny})) == SQUARE_DOOR &&
                ai_try_door_action((AiTest){DOOR_MODE_CLOSE_MARKED, true}, ai_location(m->sx, m->sy),
                                   ai_location(nx, ny), i)
                    .passed)
                return;
        }
    if (m->kind == MOVE_KIND_STEP_BACK) {
        m->facing = (int16_t)dir_opposite(m->facing);
        m->action = ACTION_TURN_BACK;
    } else if (m->action == -1) {
        m->action =
            abs_int((int16_t)(m->sx - m->tx)) + abs_int((int16_t)(m->sy - m->ty)) <= 1 ? ACTION_MOVE_NEAR : ACTION_MOVE;
    }
    int16_t current = group_dir(g_ai.group_thing);
    if (current != m->facing && (current == dir_opposite(m->facing) || m->action != ACTION_MOVE_NEAR)) {
        if (is_safe_kind(m->kind) && !(ai_class_flags() & AI_CLASS_SEES_ALL_SIDES) && rand_2bits()) {
            m->action = ACTION_TURN_BACK;
        } else {
            ai_turn_toward(m->facing);
            return;
        }
    } else if (current != m->facing && m->action != ACTION_TURN_BACK) {
        m->action = dir_turn(current, TURN_RIGHT) == m->facing ? ACTION_STEP_LEFT : ACTION_STEP_RIGHT;
    } else if (ai_action_flag((int8_t)g_ai.previous_action) & AI_ACTION_FLAG_RANGED && m->kind != MOVE_KIND_STEP_BACK &&
               !m->map_changed) {
        m->action = ACTION_NONE;
    }
    set_action_destination(m->tx, m->ty);
    g_ai.active->direction = (uint8_t)m->d;
    g_ai.active->facing = (uint8_t)m->facing;
    g_ai.active->mode = (uint8_t)m->kind;
    g_ai.active->action = (uint8_t)m->action;
    g_ai.command_result = AI_RESULT_ISSUED;
}

AiMoveTest ai_move_test(int command)
{
    return (AiMoveTest){(uint8_t)(command & AI_MOVE_KIND_MASK), command & AI_EXECUTE, command & AI_MOVE_NO_MOVERS,
                        command & AI_MOVE_ATTACK_GROUPS};
}

AiCommand ai_try_move(AiMoveTest test, Location from, Location to, int16_t dir)
{
    int16_t saved_map = g_current_map.map;
    Move m = {from.x, from.y, to.x, to.y, dir, -1, dir, test.kind, false, false, false, false};
    if (test_move(&m, test) && test.execute)
        execute_move(&m);
    if (m.map_changed)
        ai_set_map(saved_map);
    return test_outcome(m.result);
}

/* ---- Items ---- */

AiInsideItem ai_find_matching_item_inside(uint16_t thing, int16_t filter, int16_t side)
{
    AiInsideItem none = {-1, false};
    AiInsideItem found;
    uint16_t list;
    int mask = CELLS_ALL;
    if (g_ai.item_flags & ITEM_SKILL_CHESTS && item_is_chest(thing)) {
        found = (AiInsideItem){(int16_t)thing_cell(thing), false};
        list = thing_word(thing, CONTAINER_CONTENTS);
    } else {
        if (thing_type(thing) != THING_GROUP || !(g_ai.item_flags & (ITEM_SKILL_STATIC | ITEM_SKILL_GROUPS)))
            return none;
        const CreatureInfo *info = creature_group_info(thing);
        bool is_static = info->flags & CREATURE_STATIC;
        if (!(g_ai.item_flags & (is_static ? ITEM_SKILL_STATIC : ITEM_SKILL_GROUPS)))
            return none;
        found = (AiInsideItem){-1, true};
        list = thing_word(thing, GROUP_POSSESSIONS);
        if (is_static)
            mask = creature_cell_mask(info->possessions, side);
    }
    for (uint16_t t = list; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (!(mask >> thing_cell(t) & 1))
            continue;
        if (ai_thing_matches_filter(t, filter) || ai_find_matching_item_inside(t, filter, side).cell != -1) {
            if (found.in_possessions)
                found.cell = (int16_t)thing_cell(t);
            return found;
        }
    }
    return none;
}

/* Reads which wall faces of the square in the AI cache show an alcove: the face toward the creature (side). */
static void scan_wall_ornaments(int16_t side)
{
    g_ai_square_scan.cells[0] = (int8_t)side;
    uint16_t first = ai_cached_first_thing();
    uint16_t t = first;
    for (; t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR; t = dungeon_next_thing(t)) {
        if (!sensor_is_alcove_shown(first, t))
            continue;
        if (thing_cell(t) == g_ai_square_scan.cells[0])
            g_ai_square_scan.facing_ornament = 1;
        else
            g_ai_square_scan.items_elsewhere = 1;
    }
    g_ai_square.first_object = t;
}

/* True when the alcove of the square in the AI cache holds an item of filter in cell, or a chest with one. */
static bool alcove_has_item(int16_t cell, int16_t d, int16_t filter)
{
    for (uint16_t t = g_ai_square.first_object; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        int type = thing_type(t);
        if (thing_cell(t) != cell || type == THING_GROUP || type >= THING_PROJECTILE)
            continue;
        if (ai_thing_matches_filter(t, filter) || ai_find_matching_item_inside(t, filter, d).cell != -1)
            return true;
    }
    return false;
}

/* Finds an item of filter on the square in the AI cache in a cell that the creature reaches, or in the possessions of
   a group there (action: take from the group); false for none. */
static bool floor_item_in_reach(ItemTest *it)
{
    bool same = it->same;
    int16_t d = it->d;
    int16_t filter = it->filter;
    for (uint16_t t = ai_cached_first_object(); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        it->cell = (int16_t)thing_cell(t);
        int type = thing_type(t);
        if (type < THING_GROUP || type >= THING_PROJECTILE)
            continue;
        if (!(type != THING_GROUP && ai_thing_matches_filter(t, filter))) {
            AiInsideItem inside = ai_find_matching_item_inside(t, filter, d);
            it->cell = inside.cell;
            if (it->cell == -1)
                continue;
            if (inside.in_possessions) {
                it->action = ACTION_TAKE_FROM_GROUP;
                return true;
            }
        }
        if ((uint8_t)g_ai_square_scan.cells[0] == CELL_ANY) {
            /* the cells in reach: the near half of the square, or all of the own square */
            int bits = same ? (d == -1 ? CELLS_ALL : CELLS_NEAR) : CELLS_FAR;
            int16_t c = d == -1 ? 0 : d; /* fix: the original writes the cell before the table for direction -1 */
            for (int i = 0; i < DIR_COUNT; i++) {
                g_ai_square_scan.cells[c] = (int8_t)(bits & 1);
                c = dir_turn(c, TURN_RIGHT);
                bits >>= 1;
            }
        }
        if (g_ai_square_scan.cells[(uint16_t)it->cell])
            return true;
        g_ai_square_scan.items_elsewhere = 1;
    }
    return false;
}

/* A random cell of the possessions of the group in the AI cache that the creature reaches from side d; -1 none. */
static int16_t give_cell(int16_t d)
{
    uint16_t g = ai_cached_first_group();
    int bits = (uint8_t)creature_cell_mask(creature_group_info(g)->possessions, d);
    if (!bits)
        return -1;
    int16_t bit = (int16_t)random_set_bit((uint32_t)bits);
    int16_t cell = 0;
    for (; !(bit & 1); bit >>= 1)
        cell++;
    return cell;
}

/* The alcove of the wall square in the AI cache as the target: the face toward the creature must show it; a take
   needs an item of filter in it. False when it does not fit. */
static bool wall_item_target(ItemTest *it)
{
    if (g_ai_square_scan.items_elsewhere == -1) {
        g_ai_square_scan.items_elsewhere = 0;
        g_ai_square_scan.facing_ornament = 0;
        if (!it->same && g_ai.item_flags & ITEM_SKILL_WALL)
            scan_wall_ornaments(dir_opposite(it->d));
    }
    if (!g_ai_square_scan.facing_ornament)
        return false;
    if (it->mode == ITEM_MODE_GIVE) {
        it->action = ACTION_PUT_IN_WALL;
        return true;
    }
    it->action = ACTION_TAKE_FROM_WALL;
    it->cell = g_ai_square_scan.cells[0];
    return alcove_has_item(it->cell, it->d, it->filter);
}

/* The cell and action of a give: into the alcove, to the group in front, or a drop here or ahead; -1 when the group
   has no cell for it. */
static int16_t give_place(ItemTest *it)
{
    if (it->action == ACTION_PUT_IN_WALL)
        return it->d;
    if (it->action == ACTION_GIVE)
        return give_cell(it->d);
    int r = rand_bit();
    if (!it->same)
        r += TURN_BACK;
    it->action = drop_actions[r];
    return (int16_t)dir_turn(it->d, r);
}

/* The take action by the side of the cell; on its own square the creature turns to the cell first when its class
   does. True when it turns instead. */
static bool take_floor_action(ItemTest *it)
{
    int side = dir_turn(it->cell - g_ai.active->facing, TURN_RIGHT);
    if (!it->same) {
        it->action = side <= TAKE_SIDE_FRONT_LEFT ? ACTION_TAKE_FRONT : ACTION_TAKE_SIDE;
        return false;
    }
    it->action = (int16_t)(ACTION_TAKE_LEFT + (side >= TAKE_SIDE_FRONT_RIGHT));
    if (side == TAKE_SIDE_BACK_LEFT)
        g_ai.active->facing = (uint8_t)dir_turn(g_ai.active->facing, TURN_LEFT);
    else if (side == TAKE_SIDE_BACK_RIGHT)
        g_ai.active->facing = (uint8_t)dir_turn(g_ai.active->facing, TURN_RIGHT);
    if (ai_class_flags() & AI_CLASS_TURNS_TO_TAKE && g_ai.active->facing != group_dir(g_ai.group_thing)) {
        ai_turn_toward(g_ai.active->facing);
        return true;
    }
    return false;
}

AiCommand ai_try_take_item(AiTest test, Location from, Location to, int16_t dir, int16_t filter)
{
    bool execute = test.execute;
    ItemTest it = {test.mode & AI_MODE_MASK, filter, false, dir, -1, 0};
    int16_t tx = to.x;
    int16_t ty = to.y;
    if (!g_ai.item_flags)
        return fail_command(execute);
    if (tx == -1) {
        tx = (int16_t)(g_dir_dx[step_dir(it.d)] + from.x);
        ty = (int16_t)(g_dir_dy[step_dir(it.d)] + from.y);
    } else {
        it.same = from.x == tx && from.y == ty;
        if (!it.same && it.d == -1)
            it.d = dungeon_direction_to(from.x, from.y, tx, ty);
    }
    ai_cache_square((Location){tx, ty});
    if (it.mode == ITEM_MODE_TAKE && !(g_ai_square.square & SQUARE_HAS_THINGS))
        return fail_command(execute);
    if (square_type(g_ai_square.square) == SQUARE_WALL) {
        if (!wall_item_target(&it))
            return fail_command(execute);
    } else {
        if (g_ai_square_scan.items_elsewhere == -1) {
            g_ai_square_scan.items_elsewhere = 0;
            g_ai_square_scan.cells[0] = -1;
        }
        if (!it.same && ai_at_party(tx, ty))
            return fail_command(execute);
        if (!(g_ai.item_flags & (it.same ? ITEM_SKILL_SAME_SQUARE : ITEM_SKILL_OTHER_SQUARE)))
            return fail_command(execute);
        if (it.mode == ITEM_MODE_GIVE) {
            if (!it.same)
                it.action = ACTION_GIVE;
        } else if (!floor_item_in_reach(&it)) {
            return fail_command(execute);
        }
    }
    if (!execute)
        return test_outcome(true);
    if (it.d == -1)
        it.d = group_dir(g_ai.group_thing);
    if (!it.same && ai_turn_toward(it.d).passed)
        return test_outcome(true);
    g_ai.active->facing = (uint8_t)it.d;
    if (it.mode == ITEM_MODE_GIVE) {
        it.cell = give_place(&it);
        if (it.cell == -1)
            return fail_command(execute);
    } else if (it.action == -1 && take_floor_action(&it)) {
        return test_outcome(true);
    }
    g_ai.active->destination = ai_pack_location(g_ai_square.x, g_ai_square.y, g_ai_square.map);
    g_ai.active->cell = (uint8_t)it.cell;
    g_ai.active->sub = (int8_t)filter;
    g_ai.active->mode = (uint8_t)it.mode;
    g_ai.active->action = (uint8_t)it.action;
    g_ai.command_result = AI_RESULT_ISSUED;
    return test_outcome(true);
}

uint16_t ai_find_wall_actuator(uint16_t first_thing, int16_t cell, int16_t mode, int16_t filter)
{
    uint16_t found = THING_NONE;
    for (uint16_t t = first_thing; t != THING_ENDOFLIST && t != THING_NONE; t = dungeon_next_thing(t)) {
        int type = thing_type(t);
        if (type > THING_SENSOR)
            break;
        if (type != THING_SENSOR || thing_cell(t) != cell)
            continue;
        int kind = sensor_type(t);
        if (kind == SENSOR_NONE || kind == SENSOR_CREATURE_SLOT)
            continue;
        found = t;
        if (kind != WALL_ITEM_HOLDER)
            continue;
        int wanted = sensor_flag(t, SENSOR_STATE) ? ACTUATOR_MODE_TAKE : ACTUATOR_MODE_PUT;
        if (mode == wanted && ai_thing_matches_filter(t, filter))
            break;
    }
    return found;
}

bool ai_wall_actuator_accepts(uint16_t actuator, int16_t mode, int16_t filter, uint16_t state)
{
    if (actuator == THING_ENDOFLIST || actuator == THING_NONE)
        return false;
    int kind = sensor_type(actuator);
    switch (kind) {
    case WALL_CLICK_ANY:
    case WALL_CLICK_BUTTON:
    case WALL_CLICK_BUTTON_2:
        return mode == ACTUATOR_MODE_USE;
    case WALL_CLICK_EMPTY_HAND:
        return mode == ACTUATOR_MODE_USE && filter != -1;
    case WALL_CLICK_ITEM:
        return mode == ACTUATOR_MODE_USE && ai_thing_matches_filter(actuator, filter);
    case WALL_CLICK_TOGGLE:
        return mode == ACTUATOR_MODE_USE && state != !sensor_flag(actuator, SENSOR_STATE);
    case WALL_ITEM_HOLDER:
        if (sensor_flag(actuator, SENSOR_STATE) ? mode != ACTUATOR_MODE_TAKE : mode != ACTUATOR_MODE_PUT)
            return false;
        return ai_thing_matches_filter(actuator, filter);
    case WALL_CLICK_COUNTED_ITEM:
        if (mode != ACTUATOR_MODE_PUT || !sensor_data(actuator))
            return false;
        return ai_thing_matches_filter(actuator, filter);
    default:
        return false;
    }
}

AiCommand ai_try_use_wall_actuator(AiTest test, Location from, Location to, int16_t dir, int16_t filter)
{
    bool execute = test.execute;
    int mode = test.mode & AI_MODE_MASK;
    int16_t x = from.x;
    int16_t y = from.y;
    int16_t tx = to.x;
    int16_t ty = to.y;
    int16_t d = dir;
    if (!(g_ai.actuator_flags & ACTUATOR_SKILL_USE))
        return fail_command(execute);
    if (tx == -1) {
        tx = (int16_t)(g_dir_dx[step_dir(d)] + x);
        ty = (int16_t)(g_dir_dy[step_dir(d)] + y);
    } else if (x == tx && y == ty) {
        return fail_command(execute);
    }
    if (!ai_on_map(tx, ty) || (x != tx && y != ty))
        return fail_command(execute);
    if (d == -1)
        d = dungeon_direction_to(x, y, tx, ty);
    ai_cache_square((Location){tx, ty});
    if (!(g_ai_square.square & SQUARE_HAS_THINGS))
        return fail_command(execute);
    uint16_t slot = THING_NONE;
    for (uint16_t t = ai_cached_first_thing(); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR;
         t = dungeon_next_thing(t)) {
        if (thing_type(t) == THING_SENSOR && thing_cell(t) == dir_opposite(d) &&
            sensor_type(t) == SENSOR_CREATURE_SLOT) {
            slot = t;
            break;
        }
    }
    if (slot == THING_NONE || sensor_data(slot) != group_creature(g_ai.group_thing) || !sensor_flag(slot, SENSOR_STATE))
        return fail_command(execute);
    uint16_t actuator = ai_find_wall_actuator(ai_cached_first_thing(), dir_opposite(d), mode, filter);
    if (actuator == THING_NONE)
        return fail_command(execute);
    uint16_t slot_state = sensor_effect(slot);
    if (!ai_wall_actuator_accepts(actuator, mode, filter, slot_state))
        return fail_command(execute);
    if (execute && !ai_turn_toward(d).passed) {
        set_action_destination(tx, ty);
        g_ai.active->facing = (uint8_t)d;
        g_ai.active->direction = (uint8_t)d;
        g_ai.active->sub = (int8_t)filter;
        g_ai.active->mode = (uint8_t)mode;
        g_ai.active->action = (uint8_t)(ACTION_USE_ACTUATOR + (mode > ACTUATOR_MODE_TAKE ? ACTUATOR_MODE_TAKE : mode));
        g_ai.command_result =
            mode == ACTUATOR_MODE_USE && slot_state < SLOT_STATE_BUSY ? AI_RESULT_ISSUED : AI_RESULT_DONE;
    }
    return test_outcome(true);
}

/* The item class bitmap of filter for the group in the AI context: parsed once per update into the scratch block.
   Returns NULL when the creature has no class text for it. A bitmap without a cache stays valid until the next call. */
static uint8_t *filter_bitmap(int filter, int creature_type, bool is_group)
{
    if (filter >= FILTER_SLOTS || !g_ai.scratch) {
        /* fix: the original reads pointers after its table of FILTER_SLOTS filters, or writes the bitmap to address 0
           without a scratch block; the port parses without a cache */
        return money_parse_item_class_set(creature_type, filter, uncached_bitmap, is_group) ? uncached_bitmap : NULL;
    }
    if (g_ai.filters[filter])
        return g_ai.filters[filter];
    g_ai.filter_free--;
    uint8_t *bitmap = g_ai.scratch + g_ai.scratch_used;
    g_ai.filters[filter] = bitmap;
    g_ai.scratch_used += ITEM_CLASS_SET_SIZE;
    return money_parse_item_class_set(creature_type, filter, bitmap, is_group) ? bitmap : NULL;
}

bool ai_thing_matches_filter(uint16_t thing, int16_t filter)
{
    bool invert = filter & FILTER_INVERT;
    int f = filter & FILTER_MASK;
    if (f == FILTER_ANY)
        return !invert;
    if (f == FILTER_FULL_MONEYBOX)
        return item_is_moneybox(thing) && !moneybox_is_placed(thing) ? !invert : invert;
    if (f == FILTER_EMPTY_MONEYBOX) {
        if (item_is_moneybox(thing))
            return moneybox_is_placed(thing) ? !invert : invert;
        f = FILTER_EMPTY_MONEYBOX_ELSE;
    }
    int type = thing_type(thing);
    if ((f >= FILTER_BY_VARIANT && f < FILTER_BY_VARIANT_END) || f == FILTER_MONEYBOX_OR_VARIANT) {
        if (f == FILTER_MONEYBOX_OR_VARIANT) {
            if (item_is_moneybox(thing) && !moneybox_is_placed(thing))
                return !invert;
            f = FILTER_BY_VARIANT;
        }
        f = (int16_t)(f + (int16_t)thing_word(g_ai.group_thing, GROUP_ITEM_CLASS) * FILTER_VARIANTS);
    }
    uint8_t *bitmap = filter_bitmap(f, group_creature(g_ai.group_thing), type == THING_GROUP);
    if (!bitmap)
        return false;
    int16_t value;
    if (type == THING_GROUP) {
        value = group_creature(thing);
    } else if (type == THING_SENSOR) {
        int kind = sensor_type(thing);
        if (kind == WALL_CLICK_COUNTED_ITEM && !sensor_data(thing))
            return false;
        if (kind == WALL_ITEM_HOLDER || kind == WALL_CLICK_COUNTED_ITEM)
            value = (int16_t)gdat_entry_index(gdat_current(), CAT_WALL_ORNAMENT, item_gdat_index(thing),
                                              GDAT_TYPE_VALUE, ORNAMENT_KEY_TARGET);
        else
            value = (int16_t)kind;
    } else if (type >= THING_WEAPON && type <= THING_MISC) {
        value = (int16_t)item_get_index(thing);
    } else {
        return false;
    }
    /* fix: the original reads outside the bitmap for a negative or large value */
    if (value < 0 || value >= ITEM_CLASS_SET_SIZE * BYTE_BIT_COUNT)
        return invert;
    return bitmap[value / BYTE_BIT_COUNT] >> (value % BYTE_BIT_COUNT) & 1 ? !invert : invert;
}

/* True for the thing types that creatures carry. */
static bool is_item(int type)
{
    return (type > THING_GROUP && type < THING_PROJECTILE) || type == THING_CONTAINER;
}

uint16_t ai_find_matching_item(int16_t filter, uint16_t first_thing, int16_t cell)
{
    uint16_t t = first_thing;
    for (; t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        if (is_item(thing_type(t)) && ((uint8_t)cell == CELL_ANY || (uint8_t)cell == thing_cell(t)) &&
            ai_thing_matches_filter(t, filter))
            break;
    return t;
}

void ai_remove_matching_items(uint16_t *list_head, int16_t filter, int16_t cell)
{
    cell = (uint8_t)cell;
    uint16_t next;
    for (uint16_t t = *list_head; t != THING_ENDOFLIST; t = next) {
        next = dungeon_next_thing(t);
        int type = thing_type(t);
        if (!is_item(type) || (cell != CELL_ANY && cell != thing_cell(t)))
            continue;
        bool match = ai_thing_matches_filter(t, filter);
        if (type == THING_CONTAINER && (!item_is_moneybox(t) || match)) {
            uint16_t *contents = container_contents(t);
            ai_remove_matching_items(contents, filter, CELL_ANY);
            if (match) {
                uint16_t c_next;
                for (uint16_t c = *contents; c != THING_ENDOFLIST; c = c_next) {
                    c_next = dungeon_next_thing(c);
                    dungeon_unlink_thing_from_list(c, contents);
                    dungeon_append_thing(thing_with_cell(c, cell), list_head);
                }
            }
        }
        if (match) {
            dungeon_unlink_thing_from_list(t, list_head);
            thing_set_word(t, THING_NEXT, THING_NONE);
        }
    }
}

int ai_count_matching_items(int16_t filter, uint16_t first_thing, int16_t cell, bool into_groups, bool into_containers)
{
    int count = 0;
    cell = (uint8_t)cell;
    for (uint16_t t = first_thing; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        int type = thing_type(t);
        if ((into_groups && type == THING_GROUP) || (into_containers && item_is_chest(t)))
            count +=
                ai_count_matching_items(filter, thing_word(t, CONTAINER_CONTENTS), cell, into_groups, into_containers);
        if (type > THING_GROUP && type < THING_PROJECTILE && (cell == CELL_ANY || cell == thing_cell(t)) &&
            ai_thing_matches_filter(t, filter))
            count++;
    }
    return count;
}

int16_t ai_find_marker(int16_t level_delta, uint16_t id_mask)
{
    int16_t other = level_delta == MARKER_LEVEL_SIDES ? MARKER_LEVEL_SIDES_SHOWN : -1;
    for (uint16_t t = ai_cached_first_thing(); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR;
         t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_TEXT)
            continue;
        if (textrec_kind(t) != TEXT_KIND_AI_MARKER)
            continue;
        int16_t level = ai_marker_level(t);
        if ((level == level_delta || (level == other && textrec_is_visible(t))) && (1 << ai_marker_kind(t)) & id_mask)
            return ai_marker_id(t);
    }
    return -1;
}

int16_t ai_find_marker_by_id(int16_t level_delta, int16_t id)
{
    for (uint16_t t = ai_cached_first_thing(); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR;
         t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_TEXT)
            continue;
        if (textrec_kind(t) == TEXT_KIND_AI_MARKER && ai_marker_level(t) == level_delta && ai_marker_kind(t) == id)
            return ai_marker_id(t);
    }
    return -1;
}
