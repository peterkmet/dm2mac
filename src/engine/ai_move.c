/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Creature perception and movement: what blocks a square or a line of sight, the item filters, the tests of the AI
   commands (move, attack, doors, items, wall actuators), and the path search. */
#include <stdlib.h>
#include <string.h>
#include "ai.h"
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
#include "os.h"
#include "projectile.h"
#include "random.h"
#include "save.h"
#include "sensor.h"
#include "weather.h"

/* The GDAT values of a door type. */
#define DOOR_KEY_SEE_THROUGH      0x0d /* creatures see through the closed door */
#define DOOR_KEY_SOMETIMES_SEEN   0x10 /* creatures see through the moving door half of the time */
#define DOOR_KEY_PASS_COST        0x11 /* the search cost of a door square */
/* The GDAT values of a wall ornament. */
#define ORNAMENT_KEY_TARGET       0x0e /* the filter value of a wall actuator ornament */
#define ORNAMENT_KEY_LEVEL_LINK   0x11 /* a level link text: 0 leads down, else up */
#define ORNAMENT_KEY_SHOOT_TARGET 0xf0 /* a target that ranged creatures shoot at */
/* The GDAT values of a wall set. */
#define WALLSET_KEY_WALL_COST     0x72 /* the search cost of a wall square */
#define WALLSET_KEY_LIGHT         0x6d /* the light radius of the map */

/* The search costs and the light radius of the squares. */
#define DOOR_DEFAULT_PASS_COST 6
#define DOOR_COST_CHEAP        3  /* a door of this search cost or less costs little when half open */
#define WALL_COST_BLOCKING     50 /* a wall of this search cost or more stops the search */
#define LIGHT_RADIUS_MAX       8
#define PARTY_MAP_SEARCH_COST  25 /* the cost of the search of the party distance maps */

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

/* The AI class flags (g_ai_class_flags). */
#define AI_CLASS_SEES_ALL_SIDES     0x0100
#define AI_CLASS_SEES_BEHIND        0x0400
#define AI_CLASS_TURNS_TO_TAKE      0x1000
#define AI_CLASS_STAYS_ON_MAPS      0x2000 /* targets only on its own map and on the party map */
#define AI_CLASS_AVOIDS_CLOUDS      0x0200
#define AI_CLASS_AVOIDS_PARTY_DOORS 0x4000
/* g_ai.attack_flags. */
#define ATTACK_HIT                  0x0001
#define ATTACK_HIT_2                0x0002
#define ATTACK_THROW                0x0008
#define ATTACK_FIREBALL             0x0010
#define ATTACK_HARM_NON_MATERIAL    0x0020
#define ATTACK_LIGHTNING            0x0040
#define ATTACK_POISON_CLOUD         0x0080
#define ATTACK_POISON_BOLT          0x0100
#define ATTACK_SLIME                0x0200
#define ATTACK_PUSH                 0x0400
#define ATTACK_PULL                 0x0800
#define ATTACK_MELEE_MASK           0x0007
#define ATTACK_RANGED_MASK          0x0ff8
#define ATTACK_STEAL                0x0004
/* g_ai.item_flags. */
#define ITEM_SKILL_SAME_SQUARE      0x0001
#define ITEM_SKILL_WALL             0x0004
#define ITEM_SKILL_STATIC           0x0008
#define ITEM_SKILL_CHESTS           0x0010
#define ITEM_SKILL_GROUPS           0x0020
#define ITEM_SKILL_OTHER_SQUARE     0x002a
/* The flags of an action in the AI action table. */
#define AI_ACTION_FLAG_MOVES        0x02
#define AI_ACTION_FLAG_BUSY         0x03
/* Sub-commands of the shoot action (ActiveGroup.sub). */
#define SHOT_STEAL                  0x0b
/* The flags of the search target kinds (g_ai_kind_flags). */
#define KIND_TESTS_OPEN             0x01 /* the kind tests the squares that the search enters */
#define KIND_TESTS_BLOCKED          0x02 /* ... the squares that it cannot enter */
#define KIND_FIRST_BEST             0x04 /* a found target may still get a nearer better one */
#define KIND_CACHES_SQUARE          0x08
#define KIND_NO_LOCATION            0x10 /* the found target keeps its location */
#define KIND_PARTY_LOCATION         0x20 /* the found target is the party square; also: needs the party seen */
#define KIND_NO_BONUS               0x40

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

/* The search queue and the search maps. */
#define SEARCH_QUEUE_SIZE      256
#define SEARCH_LEVELS          51 /* the counts of the cost search, the last one blocks */
#define SEARCH_CELL_SIZE       4  /* a square of a search map: distance, direction, previous square */
#define SEARCH_COLUMN_SIZE     (MAP_SIDE_MAX * SEARCH_CELL_SIZE)
#define SEARCH_MAPS            256 /* one search map per map number (a byte) */
#define SEARCH_DISTANCES       256 /* the cost search counts the squares per distance (a byte) */
#define SEARCH_MARKS           256 /* the marks of the candidates */
/* The index bits of the search queue. */
#define SEARCH_QUEUE_MASK      (SEARCH_QUEUE_SIZE - 1)
/* The search counts and distances. */
#define SEARCH_LEVEL_BLOCKED   255 /* the count of the last level of the cost search */
#define VISIT_BLOCKED          255 /* the distance of a square that the search cannot enter */
#define SEARCH_MOVERS_DISTANCE 6   /* beyond it groups that move do not block the search */
#define SEARCH_STEPS           5   /* the first direction and three turns; with level changes two more */
#define SEARCH_STEPS_LEVELS    7
#define SEARCH_LEVEL_STEPS     2 /* the last steps of a square with level changes: down, then up */
#define SEARCH_FAR             100
#define SEARCH_FAR_RANGE       127
#define HIDDEN_DISTANCE        4 /* beyond it an invisible party or a party in the dark stays hidden */
#define FLEE_LINE_DISTANCE     5 /* a flee target nearer to the party must be out of its line of sight */
#define MISSILE_WATCH_RANGE    3 /* the squares in which a creature sees the missiles that fly toward it */
#define PATH_CHECK_STEPS       5 /* the steps of a stored path that the path check tests */
/* The feedback of the shift register of the first search direction. */
#define SEARCH_DIRECTION_TAPS  0xb400
/* The side of a cell as the creature that takes an item sees it (take_floor_action). */
#define TAKE_SIDE_BACK_LEFT    0
#define TAKE_SIDE_FRONT_LEFT   1
#define TAKE_SIDE_FRONT_RIGHT  2
#define TAKE_SIDE_BACK_RIGHT   3
/* The cells of a square that a creature reaches from the side. */
#define CELLS_NEAR             0x03 /* the two cells on the near side */
#define CELLS_FAR              0x0c

/* The slope of a line in 1/64. */
#define SLOPE_SHIFT    6
#define SLOPE_VERTICAL 128 /* the main delta is 0 */

/* Random bits of the sight of a creature. */
#define ROLL_LOOK_AROUND      0x0007 /* a creature that does not look that way looks in 1 of 8 tests */
#define ROLL_CHECK_ADJACENT   0x8000 /* clear: an adjacent party in front stays seen */
#define ROLL_INVISIBLE_HIDDEN 0x4000
#define ROLL_SIGHT            0x000f
#define ROLL_KEEP_COST        0x0070 /* all clear: a hidden party target keeps 1/8 of its cost */
#define ROLL_DARK_MASK        0x0007 /* random bits 8-10: the dark hides the party by this much more */
#define ROLL_LUCKY            0x0007 /* random bits that must all be clear for a lucky step */
/* The shift of the dark bits in the random bits. */
#define ROLL_DARK_SHIFT       8
/* A hidden party target keeps its cost shifted by this: 1/8. */
#define HIDDEN_COST_SHIFT     3

/* The commands past the enabled table read the globals and the data after it in SKULL.EXE. */
#define COMMAND_PAST_TABLE    0x14 /* the first command past the enabled table: it reads globals */
#define COMMAND_AFTER_GLOBALS 0x20 /* ... the commands that read the bytes after the globals */

/* A path step: direction bits 0-2, x bits 3-7, y bits 8-12, map change bit 13. */
#define PATH_DIR_MASK      0x0007 /* path step and search cell: the direction */
#define PATH_MAP_CHANGE    0x2000
#define PATH_KEEP_BITS     0xc000
/* The fields of a path step. */
#define PATH_X_SHIFT       3
#define PATH_Y_SHIFT       8
/* The move command of the path check of ai_plan_path: the original reads a byte after the path buffer
   that nothing writes. */
#define PATH_CHECK_COMMAND 0

/* The bytes of a square of a search map. */
#define SEARCH_AT_DIST            0
#define SEARCH_AT_DIR             1
#define SEARCH_AT_FROM            2
/* The marks of a candidate: a target square and the best distance. */
#define MARK_X                    0
#define MARK_Y                    1
#define MARK_MAP                  2
#define MARK_BEST                 3
#define MARK_SIZE                 4
/* The kinds of the party view target (AiCandidate.item). */
#define VIEW_ON_PARTY             0
#define VIEW_PARTY_SIDE           1
#define VIEW_IN_FRONT             2
#define VIEW_PARTY_AT_PIT         3
#define VIEW_AT_DISTANCE          4
/* The kinds of the door target (AiCandidate.item). */
#define DOOR_TARGET_PARTY         1
#define DOOR_TARGET_NEXT_TO_PARTY 2
/* The kinds of the group in front target (AiCandidate.mask). */
#define GROUP_TARGET_ANY          1
#define GROUP_TARGET_CARRIES      2
/* AI marker levels: the markers whose id holds the sides of a square that a creature does not enter, and the
   markers of level 2 that count with them while their text shows. */
#define MARKER_LEVEL_SIDES        1
#define MARKER_LEVEL_SIDES_SHOWN  2

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

/* The state of a search (the locals of the original). */
typedef struct {
    int command, enabled, count;
    AiCandidate *cands;
    int8_t (*marks)[MARK_SIZE]; /* per candidate: a target square (x, y, map) and the best distance */
    uint8_t *maps[SEARCH_MAPS]; /* the search map of each map, NULL until the search enters it */
    uint8_t *map;               /* ... of the current map */
    struct {
        uint8_t x, y, map;
    } queue[SEARCH_QUEUE_SIZE];
    int head, tail;
    uint8_t counts[SEARCH_DISTANCES]; /* the cost search: queued squares by distance */
    int level;
    bool cost_search;
    int16_t start_x, start_y, start_map;
    int16_t x, y;                     /* the square that the search tests */
    int16_t from_x, from_y, from_map; /* the square that the search left */
    int16_t check_map;                /* the map of the last square that the search entered */
    int16_t dist, max_cost, dir, turn, steps_left, result;
    int16_t passage_map, passage_x, passage_y; /* the other side of a passage that the search went through */
    int16_t home_x, home_y, home_map, act_x, act_y, act_map;
    uint8_t visit, blocking, or_flags;
    bool two_maps, flee_once, levels, down_ok, up_ok;
    bool locations; /* a target has a location: the start may be a passage */
    uint16_t saved_attack;
} Search;

/* The result of a step of the search. */
typedef enum { SEARCH_STEP_SKIP, SEARCH_STEP_BLOCKED, SEARCH_STEP_ENTERED } SearchStep;

/* The actions of a take from the same square (0, 1) or from the square in front (2, 3) by a random bit. */
static const uint8_t drop_actions[] = {ACTION_DROP_HERE, ACTION_DROP_HERE_2, ACTION_DROP_AHEAD_2, ACTION_DROP_AHEAD};

AiSquareScan g_ai_square_scan;
int16_t g_cached_light_sum = -1;
int16_t g_cached_explosion_light_sum = -1;
int16_t g_last_party_map = -1;
uint16_t g_search_direction_bits = 1;
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

/* The cell of (x, y) in the search map cells. */
static uint8_t *search_cell(uint8_t *cells, int16_t x, int16_t y)
{
    return cells + x * SEARCH_COLUMN_SIZE + y * SEARCH_CELL_SIZE;
}

/* The step of a path: the direction (bits 0-2) and the square (x, y) that the step reaches. */
static uint16_t path_step_pack(int16_t dir, int16_t x, int16_t y)
{
    return (uint16_t)((dir & PATH_DIR_MASK) | (x & LOCATION_COORD_MASK) << PATH_X_SHIFT |
                      (y & LOCATION_COORD_MASK) << PATH_Y_SHIFT);
}
/* The direction, square, and map change of a path step. */
static int16_t path_step_dir(uint16_t step)
{
    return step & PATH_DIR_MASK;
}
static int16_t path_step_x(uint16_t step)
{
    return step >> PATH_X_SHIFT & LOCATION_COORD_MASK;
}
static int16_t path_step_y(uint16_t step)
{
    return step >> PATH_Y_SHIFT & LOCATION_COORD_MASK;
}
static bool path_step_map_change(uint16_t step)
{
    return step & PATH_MAP_CHANGE;
}

/* ---- Squares ---- */

static int map_width(void)
{
    return dungeon_map_width(g_party.cur_map);
}
static int map_height(void)
{
    return dungeon_map_height(g_party.cur_map);
}

static bool on_map(int16_t x, int16_t y)
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
    return on_map(x, y) ? dungeon_square_first_thing(g_party.cur_map, x, y) : THING_ENDOFLIST;
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

/* A location word of (x, y) of map. */
static uint16_t pack_location(int16_t x, int16_t y, int16_t map)
{
    return (uint16_t)location_pack(x, y, map);
}

/* The distance between two squares along the axes. */
static int16_t axis_distance(int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
    return (int16_t)(abs_int((int16_t)(x1 - x2)) + abs_int((int16_t)(y1 - y2)));
}

/* A GDAT value of the door type on (x, y). */
static int read_door_value(int16_t x, int16_t y, int key)
{
    return gdat_entry_index(gdat_current(), CAT_DOOR, dun_get_door_type(first_thing(x, y)), GDAT_TYPE_VALUE, key);
}

/* The search cost of the door on (x, y). */
static int pass_cost(int16_t x, int16_t y)
{
    int cost = read_door_value(x, y, DOOR_KEY_PASS_COST);
    if (cost)
        return cost;
    return read_door_value(x, y, DOOR_KEY_SOMETIMES_SEEN) ? 1 : DOOR_DEFAULT_PASS_COST;
}

static int16_t own_x(void)
{
    return g_ai.event.data[EVENT_X];
}
static int16_t own_y(void)
{
    return g_ai.event.data[EVENT_Y];
}

/* True for the square of the group in the AI context. */
static bool at_group(int16_t x, int16_t y)
{
    return x == own_x() && y == own_y();
}

/* True for the party square on the current map. */
static bool at_party(int16_t x, int16_t y)
{
    return g_party.cur_map == g_ai_party.map && x == g_ai_party.x && y == g_ai_party.y;
}

static uint32_t class_flags(void)
{
    return g_ai_class_flags[g_ai.class_index];
}

/* The enabled byte of a command; the commands past the table read the data after it in SKULL.EXE. */
static int enabled_byte(int command)
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
    g_ai.active->destination = pack_location(x, y, g_party.cur_map);
}

bool ai_square_is_obstacle(Location at)
{
    uint8_t square = dun_get_square(at);
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
    uint8_t square = dun_get_square((Location){sx, sy});
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
    if (at_party(at.x, at.y))
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
    return (int16_t)(((steep ? x - x1 : y - y1) << SLOPE_SHIFT) / major);
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
    return axis_distance(x1, y1, x2, y2);
}

void ai_cache_square(Location at)
{
    if (at.x == g_ai_square.x && at.y == g_ai_square.y && g_party.cur_map == g_ai_square.map)
        return;
    g_ai_square.map = g_party.cur_map;
    g_ai_square.x = at.x;
    g_ai_square.y = at.y;
    g_ai_square.square = dun_get_square(at);
    g_ai_square.first_thing = g_ai_square.square & SQUARE_HAS_THINGS ? THING_NONE : THING_ENDOFLIST;
    g_ai_square.first_object = g_ai_square.first_thing;
    g_ai_square.first_group = g_ai_square.first_thing;
    g_ai_square_scan.blocking = 0;
    g_ai_square_scan.passage = 0;
    g_ai_square_scan.items_elsewhere = -1;
    g_ai_square.cost = 1;
}

/* The first thing of the square in the AI cache, read once. */
static uint16_t cached_first_thing(void)
{
    if (g_ai_square.first_thing == THING_NONE)
        g_ai_square.first_thing = first_thing(g_ai_square.x, g_ai_square.y);
    return g_ai_square.first_thing;
}

uint16_t ai_cached_first_object(void)
{
    if (g_ai_square.first_object != THING_NONE)
        return g_ai_square.first_object;
    uint16_t t = skip_to_object(cached_first_thing());
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
    int limit = *search_cell(distance_map, sx, sy);
    for (int i = step + 1; i < range; i++) {
        int16_t px = sx;
        int16_t py = sy;
        sx += g_dir_dx[back];
        sy += g_dir_dy[back];
        uint8_t reached = *search_cell(distance_map, sx, sy);
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
            if (!on_map(tx, ty))
                continue;
            uint8_t square = dun_get_square((Location){tx, ty});
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
            if (*search_cell(distance_map, tx, ty) || ai_blocks_line_to_target((Location){tx, ty}))
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
    AiLineTarget target = {true, pack_location(tx, ty, g_party.cur_map)};
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
    if (g_ai_party.map != g_party.cur_map || !(class_flags() & AI_CLASS_AVOIDS_PARTY_DOORS) ||
        g_ai.info->flags & CREATURE_NON_MATERIAL || (x == own_x() && y == own_y() && g_party.cur_map == g_ai.map))
        return false;
    return square_type(dun_get_square((Location){x, y})) == SQUARE_DOOR &&
           axis_distance(x, y, g_ai_party.x, g_ai_party.y) <= 1 && rand_2bits();
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
        return at_party(tx, ty);
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
    if (!(class_flags() & AI_CLASS_AVOIDS_CLOUDS))
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
    if (!on_map(tx, ty) || (x != tx && y != ty))
        return fail_command(execute);
    int16_t distance = axis_distance(x, y, tx, ty);
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
        dir = distance == 0 && at_party(tx, ty) ? dir_opposite(g_game.party_dir) : dun_get_direction_to(x, y, tx, ty);
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
    a->door = cached_first_thing();
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
    a->distance = axis_distance(a->from.x, a->from.y, a->to.x, a->to.y);
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
        a.d = dun_get_direction_to(from.x, from.y, a.to.x, a.to.y);
    } else if (a.to.x == from.x && from.y == a.to.y) {
        return fail_command(execute);
    }
    if (!on_map(a.to.x, a.to.y) || (from.x != a.to.x && from.y != a.to.y))
        return fail_command(execute);
    DoorTest state = test_door(&a);
    if (state == DOOR_TEST_FAIL || (state != DOOR_TEST_ACT && a.mode == DOOR_MODE_CLOSE_MARKED))
        return fail_command(execute);
    if (!execute)
        return test_outcome(true);
    if (a.d == -1)
        a.d = dun_get_direction_to(from.x, from.y, a.to.x, a.to.y);
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
        if (!(class_flags() & AI_CLASS_SEES_BEHIND)) {
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
            if (!on_map(sx, sy))
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
            uint16_t link = THING_NONE;
            /* an open teleporter next to a seamless passage: test the square behind it */
            PassageEnd end;
            if (!dun_get_linked_teleporter_dest(&end, (Location){x + 1, y}) &&
                !dun_get_linked_teleporter_dest(&end, (Location){x - 1, y}) &&
                !dun_get_linked_teleporter_dest(&end, (Location){x, y + 1}) &&
                !dun_get_linked_teleporter_dest(&end, (Location){x, y - 1}))
                return true;
            link = first_thing(x, y);
            if (end.map != teleporter_map(link) ||
                axis_distance(end.x, end.y, teleporter_x(link), teleporter_y(link)) != 1)
                return true;
            int16_t map = teleporter_map(link);
            if (map != g_game.party_map && map != g_party.alt_view_map)
                return true;
            int16_t saved = g_party.cur_map;
            ai_set_map(map);
            Location at = ai_location(teleporter_x(link), teleporter_y(link));
            bool r = ai_try_move(ai_move_test(command), at, at, DIR_KEEP_FACING).passed;
            ai_set_map(saved);
            return r;
        }
        g_ai_square_scan.passage = (uint8_t)dun_get_linked_teleporter_dest(&g_ai_square_scan.end, (Location){x, y});
        if (!g_ai_square_scan.passage)
            return true;
        return g_ai_square_scan.end.map == g_game.party_map || g_ai_square_scan.end.map == g_party.alt_view_map;
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
        uint16_t door = cached_first_thing();
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
            if (!dun_get_linked_teleporter_dest(&g_ai_square_scan.end, (Location){x, y}))
                return MOVE_TELEPORTER | MOVE_FLOOR;
            g_ai_square_scan.passage = 1;
            return creature_is_native_to_map(g_ai.group_thing, g_ai_square_scan.end.map) ? MOVE_LEVELS : 0;
        } else {
            return teleporter_scope(cached_first_thing()) & TELEPORTER_SCOPE_CREATURES ? MOVE_TELEPORTER : MOVE_FLOOR;
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
    uint8_t square = dun_get_square((Location){x, y});
    int type = square_type(square);
    if (type == SQUARE_PIT && dir == DIR_LEVEL_DOWN && square & SQUARE_PIT_OPEN && !(square & SQUARE_FLAG_0)) {
        need = MOVE_OPEN_PIT;
        m->action = ACTION_PIT;
        m->facing = group_dir(g_ai.group_thing);
    } else if (type == SQUARE_FLOOR || type == SQUARE_PIT || type == SQUARE_TELEPORTER || type == SQUARE_FAKE_WALL) {
        if (square & SQUARE_LEVEL_LINK) {
            int16_t side =
                dun_find_adjacent_level_link((Location){x, y}, dir == DIR_LEVEL_UP ? LEVEL_UP : LEVEL_DOWN, &link);
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
        m->facing = dir_opposite(dun_get_stairs_exit_direction((Location){x, y}));
        need = MOVE_STAIRS;
    }
    if (!(g_ai.move_flags & need))
        return -1;
    Position to;
    if (link != THING_NONE && (m->action == ACTION_LINK_UP || m->action == ACTION_LINK_DOWN)) {
        to = (Position){(int16_t)sensor_data(link), sensor_target_x(link), sensor_target_y(link)};
    } else {
        to = dungeon_location_on_map((Position){g_party.cur_map, x, y}, dir == DIR_LEVEL_UP ? LEVEL_UP : LEVEL_DOWN);
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
            m->d = dun_get_direction_to(m->sx, m->sy, m->tx, m->ty);
            m->facing = m->d;
        }
    }
    return on_map(m->tx, m->ty);
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
    return !m->same && g_ai_party.map == g_party.cur_map && class_flags() & AI_CLASS_AVOIDS_PARTY_DOORS &&
           !(g_ai.info->flags & CREATURE_NON_MATERIAL) && square_type(g_ai_square.square) == SQUARE_DOOR &&
           axis_distance(m->tx, m->ty, g_ai_party.x, g_ai_party.y) <= 1 &&
           (m->sx != own_x() || m->sy != own_y() || rand_2bits());
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
    if (g_party.cur_map == end.map && m->tx == end.x && m->ty == end.y)
        return;
    int16_t saved = g_party.cur_map;
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
    uint16_t also = at_party(m->tx, m->ty) ? MOVE_PARTY : 0;
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
    int enabled = enabled_byte(m->kind);
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
    if (m->kind == MOVE_KIND_KEEP_DISTANCE && axis_distance(m->tx, m->ty, g_ai_party.x, g_ai_party.y) <= 1)
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
            if (square_type(dun_get_square((Location){nx, ny})) == SQUARE_DOOR &&
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
        if (is_safe_kind(m->kind) && !(class_flags() & AI_CLASS_SEES_ALL_SIDES) && rand_2bits()) {
            m->action = ACTION_TURN_BACK;
        } else {
            ai_turn_toward(m->facing);
            return;
        }
    } else if (current != m->facing && m->action != ACTION_TURN_BACK) {
        m->action = dir_turn(current, TURN_RIGHT) == m->facing ? ACTION_STEP_LEFT : ACTION_STEP_RIGHT;
    } else if (ai_action_flag((int8_t)g_ai.previous_action) & AI_ACTION_FLAG_MOVES && m->kind != MOVE_KIND_STEP_BACK &&
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
    int16_t saved_map = g_party.cur_map;
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
    uint16_t first = cached_first_thing();
    uint16_t t = first;
    for (; t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR; t = dungeon_next_thing(t)) {
        if (!dun_is_alcove_shown(first, t))
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
            int16_t c = same && d == -1 ? 0 : d;
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
        r += 2;
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
    if (class_flags() & AI_CLASS_TURNS_TO_TAKE && g_ai.active->facing != group_dir(g_ai.group_thing)) {
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
            it.d = dun_get_direction_to(from.x, from.y, tx, ty);
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
        if (!it.same && at_party(tx, ty))
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
    g_ai.active->destination = pack_location(g_ai_square.x, g_ai_square.y, g_ai_square.map);
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
    if (!on_map(tx, ty) || (x != tx && y != ty))
        return fail_command(execute);
    if (d == -1)
        d = dun_get_direction_to(x, y, tx, ty);
    ai_cache_square((Location){tx, ty});
    if (!(g_ai_square.square & SQUARE_HAS_THINGS))
        return fail_command(execute);
    uint16_t slot = THING_NONE;
    for (uint16_t t = cached_first_thing(); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR;
         t = dungeon_next_thing(t)) {
        if (thing_type(t) == THING_SENSOR && thing_cell(t) == dir_opposite(d) &&
            sensor_type(t) == SENSOR_CREATURE_SLOT) {
            slot = t;
            break;
        }
    }
    if (slot == THING_NONE || sensor_data(slot) != group_creature(g_ai.group_thing) || !sensor_flag(slot, SENSOR_STATE))
        return fail_command(execute);
    uint16_t actuator = ai_find_wall_actuator(cached_first_thing(), dir_opposite(d), mode, filter);
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
    for (uint16_t t = cached_first_thing(); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR;
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
    for (uint16_t t = cached_first_thing(); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR;
         t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_TEXT)
            continue;
        if (textrec_kind(t) == TEXT_KIND_AI_MARKER && ai_marker_level(t) == level_delta && ai_marker_kind(t) == id)
            return ai_marker_id(t);
    }
    return -1;
}

/* ---- The path search ---- */

static uint8_t *search_map_cell(Search *s, int16_t map, int16_t x, int16_t y)
{
    return search_cell(s->maps[map], x, y);
}

/* Steps the shift register that picks the first search direction. */
static int next_direction_bits(void)
{
    g_search_direction_bits = g_search_direction_bits & 1 ? (g_search_direction_bits >> 1) ^ SEARCH_DIRECTION_TAPS
                                                          : g_search_direction_bits >> 1;
    return g_search_direction_bits;
}

/* The distance of the party from (x, y) of map, across maps by the dungeon position; doubled: twice the distance. */
static int16_t far_party_distance(int16_t x, int16_t y, int16_t map, bool doubled)
{
    int16_t tm = g_ai_party.map;
    /* the casts wrap the dungeon positions to 16 bits, as in the original */
    return (int16_t)(axis_distance((int16_t)(dungeon_map_offset_x(map) + x), (int16_t)(dungeon_map_offset_y(map) + y),
                                   (int16_t)(dungeon_map_offset_x(tm) + g_ai_party.x),
                                   (int16_t)(dungeon_map_offset_y(tm) + g_ai_party.y))
                     << doubled);
}

/* A target that needs the party seen: an invisible party or a party in the dark may stay hidden. */
static void hide_party_target(AiCandidate *c)
{
    bool invisible = g_game.magic_light[MAGIC_INVISIBILITY] && !(g_ai.info->flags & CREATURE_SEES_INVISIBLE);
    bool dark = false;
    if (!invisible) {
        dark = g_view_light_level && save_magic_light() < 0 && !(g_ai.info->flags & CREATURE_SEES_IN_DARK);
        if (!dark)
            return;
    }
    int16_t dist = g_ai.map == g_ai_party.map ? axis_distance(own_x(), own_y(), g_ai_party.x, g_ai_party.y)
                                              : far_party_distance(own_x(), own_y(), g_ai.map, false);
    uint32_t r = rand_next();
    if (!(r & ROLL_CHECK_ADJACENT)) {
        if (dist == 0)
            return;
        if (dist == 1 &&
            dun_get_direction_to(own_x(), own_y(), g_ai_party.x, g_ai_party.y) == group_dir(g_ai.group_thing))
            return;
    }
    bool hide = dist > HIDDEN_DISTANCE;
    if (!hide) {
        if (invisible &&
            ((int)(r & ROLL_SIGHT) - (HIDDEN_DISTANCE - dist) > creature_sight(g_ai.info) || r & ROLL_INVISIBLE_HIDDEN))
            hide = true;
        else if (dark &&
                 dist * g_view_light_level - (int)(r >> ROLL_DARK_SHIFT & ROLL_DARK_MASK) > creature_sight(g_ai.info))
            hide = true;
    }
    if (hide)
        c->cost = r & ROLL_KEEP_COST ? -1 : (int8_t)(c->cost >> HIDDEN_COST_SHIFT);
}

/* Prepares the search for the target kind of a candidate: the target square, the best distance of its mark, the saved
   attacks, and the move flags of the light and distance searches. */
static void setup_target_kind(Search *s, AiCandidate *c, int8_t *m, uint8_t flags)
{
    switch ((uint8_t)c->kind) {
    case TARGET_HOME: {
        uint16_t home = thing_word(g_ai.group_thing, GROUP_HOME);
        s->home_x = location_x(home);
        s->home_y = location_y(home);
        s->home_map = location_map(home);
        break;
    }
    case TARGET_ACTIVATION:
        s->act_x = location_x(g_ai.active->position);
        s->act_y = location_y(g_ai.active->position);
        s->act_map = location_map(g_ai.active->position);
        /* the original compares the position with the party position in another order */
        if (s->act_x == g_ai_party.map && s->act_y == g_ai_party.x && s->act_map == g_ai_party.y)
            c->kind_flags = flags ^ (KIND_TESTS_OPEN | KIND_TESTS_BLOCKED);
        break;
    case TARGET_FLEE: {
        int16_t d = g_party.cur_map == g_ai_party.map ? axis_distance(own_x(), own_y(), g_ai_party.x, g_ai_party.y)
                                                      : SEARCH_FAR;
        m[MARK_BEST] = (int8_t)max_int(d, (int16_t)c->item);
        break;
    }
    case TARGET_SHOOT_PARTY:
    case TARGET_MELEE_PARTY:
    case TARGET_STEAL:
    case TARGET_MELEE_LINE:
    case TARGET_SHOOT_LINE:
        s->saved_attack = g_ai.attack_flags;
        break;
    case TARGET_NEAR_MARKER:
        m[MARK_BEST] =
            (int8_t)(m[MARK_MAP] == g_party.cur_map ? max_int(1, axis_distance(own_x(), own_y(), m[MARK_X], m[MARK_Y]))
                                                    : SEARCH_FAR);
        break;
    case TARGET_LIGHT:
        g_ai.move_flags = MOVE_FLAGS_LIGHT;
        g_light_source_sum = 0;
        g_explosion_light_sum = 0;
        break;
    case TARGET_AI_MARKER:
        m[MARK_BEST] = SEARCH_FAR_RANGE;
        break;
    case TARGET_PARTY_DISTANCE:
        g_ai.move_flags = MOVE_FLAGS_PARTY_MAP;
        break;
    default:
        break;
    }
}

/* Prepares the candidates: their target squares, flags, and the largest cost that the search needs. */
static void search_setup(Search *s)
{
    for (int i = 0; i < s->count; i++) {
        AiCandidate *c = &s->cands[i];
        int8_t *m = s->marks[i];
        c->path_length = -1;
        m[MARK_X] = (int8_t)location_x(c->marker);
        m[MARK_Y] = (int8_t)location_y(c->marker);
        m[MARK_MAP] = (int8_t)location_map(c->marker);
        uint8_t kind = (uint8_t)c->kind;
        /* fix: the original reads past g_ai_kind_flags for the kinds after it */
        uint8_t flags = kind < sizeof g_ai_kind_flags ? g_ai_kind_flags[kind] : 0;
        c->kind_flags = flags;
        s->or_flags |= flags;
        setup_target_kind(s, c, m, flags);
        if (kind == TARGET_HOME || kind == TARGET_ACTIVATION || kind == TARGET_MARKER || kind == TARGET_NEAR_MARKER)
            s->locations = true;
        if (!s->enabled && flags & KIND_PARTY_LOCATION)
            hide_party_target(c);
        s->max_cost = max_int(s->max_cost, c->cost + (c->kind_flags & KIND_TESTS_BLOCKED ? 1 : 0));
    }
}

/* True when the search square, or the other side of its passage, is the target square of map tmap. */
static bool at_place(const Search *s, int16_t tx, int16_t ty, int16_t tmap)
{
    return (s->check_map == tmap && s->x == tx && s->y == ty) ||
           (s->passage_map == tmap && s->passage_x == tx && s->passage_y == ty);
}

/* A target of a search mark: the marked square, or its other side. */
static bool at_mark(const Search *s, const int8_t *m)
{
    return at_place(s, m[MARK_X], m[MARK_Y], m[MARK_MAP]);
}

/* True for a candidate at the start, or by a chance of 1 in 8. */
static bool lucky_step(const AiCandidate *c)
{
    return c->path_length <= 0 || !(next_direction_bits() & ROLL_LUCKY);
}

/* Writes the distance of the square into a party distance map (or adds bits with add). */
static void mark_distance(Search *s, DistanceMap *d, uint8_t value, bool add)
{
    uint8_t *cell;
    if (s->check_map == d->map)
        cell = &d->cells[s->x * MAP_SIDE_MAX + s->y];
    else if (s->passage_map >= 0 && s->passage_map == d->map)
        cell = &d->cells[s->passage_x * MAP_SIDE_MAX + s->passage_y];
    else
        return;
    if (add)
        *cell |= value;
    else
        *cell = value;
}

/* A line target found from the square that the search reached: the shooter square and its distance. */
static bool line_target(Search *s, AiCandidate *c, Location from, int16_t dir, bool at_entered)
{
    g_ai.attack_flags &= c->mask;
    AiLineTarget target = ai_find_target_in_lines((int16_t)c->item, s->map, from, dir, c->kind == TARGET_SHOOT_LINE);
    bool found = target.found;
    if (found) {
        c->destination = target.location;
        c->found = pack_location(g_ai_square.x, g_ai_square.y, g_ai_square.map);
        if (at_entered && g_ai_square.x == s->x && g_ai_square.y == s->y && g_ai_square.map == s->check_map)
            c->path_length = (int8_t)s->dist;
        else
            c->path_length = (int8_t)(*search_map_cell(s, g_ai_square.map, g_ai_square.x, g_ai_square.y) - 1);
    }
    g_ai.attack_flags = s->saved_attack;
    return found;
}

/* The party view target on an entered square: the party square, in front of the party in sight, or at a distance. */
static bool entered_party_view(const AiCandidate *c, int16_t x, int16_t y)
{
    if (c->item == VIEW_ON_PARTY)
        return at_party(x, y);
    if (c->item == VIEW_IN_FRONT) {
        if (dun_get_direction_to(g_ai_party.x, g_ai_party.y, x, y) != g_ai_party.dir)
            return false;
        int16_t d = ai_trace_line((Location){g_ai_party.x, g_ai_party.y}, (Location){x, y}, ai_square_is_obstacle);
        return d > 0 && d <= (int16_t)c->mask;
    }
    if (c->item == VIEW_AT_DISTANCE) {
        if (axis_distance(x, y, g_ai_party.x, g_ai_party.y) != (int16_t)c->mask)
            return false;
        if (x != g_ai_party.x && y != g_ai_party.y)
            return true;
        return !ai_trace_line((Location){g_ai_party.x, g_ai_party.y}, (Location){x, y}, ai_blocks_line_except_self);
    }
    return false;
}

/* The flee target on an entered square: farther from the party than the best square yet, and out of its sight. */
static bool entered_flee(Search *s, const AiCandidate *c, int8_t *m, bool on_party_map)
{
    int16_t x = s->x;
    int16_t y = s->y;
    if (c->marker != FOUND_NONE)
        return at_mark(s, m);
    if (!lucky_step(c))
        return false;
    int16_t d;
    if (on_party_map) {
        d = axis_distance(x, y, g_ai_party.x, g_ai_party.y);
    } else {
        bool other_level = dungeon_map_level(s->check_map) != dungeon_map_level(g_ai_party.map);
        d = far_party_distance(x, y, s->check_map, other_level);
    }
    if (m[MARK_BEST] >= d)
        return false;
    if (on_party_map && d < FLEE_LINE_DISTANCE && (x == g_ai_party.x || y == g_ai_party.y) &&
        ai_trace_line((Location){g_ai_party.x, g_ai_party.y}, (Location){x, y}, ai_blocks_line_except_self))
        return false;
    m[MARK_BEST] = (int8_t)d;
    return true;
}

/* The attack test of a candidate from the search square with the attacks of its mask. */
static bool candidate_attack(const Search *s, const AiCandidate *c, int mode, Location target, int16_t dir)
{
    g_ai.attack_flags &= c->mask;
    bool r = ai_try_attack((AiTest){(uint8_t)mode, false}, ai_location(s->x, s->y), target, dir).passed;
    g_ai.attack_flags = s->saved_attack;
    return r;
}

/* The near marker target on an entered square: nearer to the marked square than the best square yet. */
static bool entered_near_marker(Search *s, AiCandidate *c, int8_t *m, int16_t x, int16_t y)
{
    int16_t d = m[MARK_MAP] == s->check_map ? axis_distance(x, y, m[MARK_X], m[MARK_Y]) : m[MARK_BEST];
    if (m[MARK_BEST] <= d) {
        if (m[MARK_MAP] != s->passage_map)
            return false;
        d = axis_distance(s->passage_x, s->passage_y, m[MARK_X], m[MARK_Y]);
        if (d >= m[MARK_BEST])
            return false;
    }
    m[MARK_BEST] = (int8_t)d;
    if (!(uint8_t)d)
        c->kind_flags &= (uint8_t)~KIND_FIRST_BEST;
    return true;
}

/* The AI marker target on an entered square: a marker of the kind with a lower id than the best one yet. */
static bool entered_ai_marker(AiCandidate *c, int8_t *m)
{
    if (!(g_ai_square.square & SQUARE_HAS_THINGS))
        return false;
    int16_t id = ai_find_marker_by_id((int16_t)c->item, (int16_t)c->mask);
    if (id == -1 || id >= m[MARK_BEST])
        return false;
    if (id && m[MARK_BEST] >= SEARCH_FAR_RANGE && c->cost) {
        m[MARK_BEST] = (int8_t)id;
        return false;
    }
    c->kind_flags &= (uint8_t)~KIND_FIRST_BEST;
    return true;
}

/* The damage group target: a group of the filter on (x, y) takes the damage kind of the candidate. */
static bool damage_group_at(const AiCandidate *c, int16_t x, int16_t y)
{
    uint16_t g = ai_cached_first_group();
    if (g == THING_ENDOFLIST || !ai_thing_matches_filter(g, c->item))
        return false;
    creature_apply_damage_kind(g, (Location){x, y}, (int16_t)c->mask);
    return true;
}

/* The test of a candidate on a square that the search entered. */
static bool test_entered(Search *s, int i)
{
    AiCandidate *c = &s->cands[i];
    int8_t *m = s->marks[i];
    int16_t x = s->x;
    int16_t y = s->y;
    bool on_party_map = s->check_map == g_ai_party.map;
    switch ((uint8_t)c->kind) {
    case TARGET_ANY:
        return true;
    case TARGET_HOME:
        return at_place(s, s->home_x, s->home_y, s->home_map);
    case TARGET_PARTY_VIEW:
        return entered_party_view(c, x, y);
    case TARGET_ACTIVATION:
        return at_place(s, s->act_x, s->act_y, s->act_map);
    case TARGET_PARTY_RANGE_2:
        return on_party_map && axis_distance(x, y, g_ai_party.x, g_ai_party.y) == 2;
    case TARGET_FLEE:
        return entered_flee(s, c, m, on_party_map);
    case TARGET_SHOOT_PARTY:
    case TARGET_MELEE_PARTY: {
        if (!on_party_map)
            return false;
        int mode = c->kind == TARGET_SHOOT_PARTY ? ATTACK_MODE_PARTY_RANGED : ATTACK_MODE_PARTY;
        return candidate_attack(s, c, mode, ai_location(g_ai_party.x, g_ai_party.y), -1);
    }
    case TARGET_MELEE_LINE:
    case TARGET_SHOOT_LINE:
        if (c->cost != s->dist)
            return false;
        return line_target(s, c, ai_location(x, y), -1, true);
    case TARGET_STEAL:
        if (!on_party_map || ai_action_flag((int8_t)g_ai.previous_action) & AI_ACTION_FLAG_BUSY)
            return false;
        return candidate_attack(s, c, ATTACK_MODE_PARTY, AI_SQUARE_IN_DIR, group_dir(g_ai.group_thing));
    case TARGET_MARKER:
        return at_mark(s, m);
    case TARGET_NEAR_MARKER:
        return entered_near_marker(s, c, m, x, y);
    case TARGET_WANDER:
        if (c->marker != FOUND_NONE)
            return at_mark(s, m);
        return s->dist > 0 && lucky_step(c);
    case TARGET_RANDOM: {
        int16_t chance = (int16_t)c->item;
        if (chance < 0)
            return rand_below((uint16_t)-chance) != 0;
        return chance > 0 && !rand_below((uint16_t)chance);
    }
    case TARGET_ITEM:
    case TARGET_ITEM_2:
        return ai_try_take_item((AiTest){ITEM_MODE_TAKE, false}, ai_location(x, y), ai_location(x, y), -1,
                                (int16_t)c->item)
            .passed;
    case TARGET_DAMAGE_GROUP:
        if (x == s->start_x && y == s->start_y)
            return false;
        return damage_group_at(c, x, y);
    case TARGET_MOVE_TEST:
        return ai_try_move(ai_move_test((uint8_t)c->item), ai_location(x, y), ai_location(x, y), -1).passed;
    case TARGET_LIGHT:
        if (g_ai_square.square & SQUARE_HAS_THINGS)
            light_add_square_light(s->dist, s->dir == -1 ? g_game.party_dir : s->dir, x, y,
                                   LIGHT_FLOOR | LIGHT_CREATURES);
        return false;
    case TARGET_AI_MARKER:
        return entered_ai_marker(c, m);
    case TARGET_MARKER_SIDE: {
        if (s->dir == -1 || !(g_ai_square.square & SQUARE_HAS_THINGS))
            return false;
        int16_t sides = ai_find_marker(MARKER_LEVEL_SIDES, (uint16_t)g_ai.marker_mask);
        return sides != -1 && !((1 << dir_opposite(s->dir)) & sides);
    }
    case TARGET_PARTY_DISTANCE:
        mark_distance(s, &g_party_distance, (uint8_t)(s->dist + 1), false);
        mark_distance(s, &g_alt_distance, (uint8_t)(s->dist + 1), false);
        return false;
    default:
        return false;
    }
}

/* The party view target on the party square that the search could not enter: from a side of the mask, or with an
   open pit behind the party. */
static bool blocked_party_view(Search *s, const AiCandidate *c, int16_t x, int16_t y)
{
    if (!at_party(x, y))
        return false;
    if (c->item == VIEW_PARTY_SIDE) {
        if (!c->mask || (1 << dir_relative(dun_get_direction_to(g_ai_party.x, g_ai_party.y, s->from_x, s->from_y),
                                           g_ai_party.dir)) &
                            c->mask)
            return true;
        s->visit = VISIT_BLOCKED;
    } else if (c->item == VIEW_PARTY_AT_PIT) {
        uint8_t square = dun_get_square((Location){x + g_dir_dx[s->dir], y + g_dir_dy[s->dir]});
        if (square_type(square) == SQUARE_PIT && square & SQUARE_PIT_OPEN && !(square & SQUARE_FLAG_0))
            return true;
        s->visit = VISIT_BLOCKED;
    }
    return false;
}

/* The group in front target: a group of the creature type that carries something, or with the party or a moving
   group behind it. */
static bool blocked_group_in_front(Search *s, const AiCandidate *c, int16_t x, int16_t y)
{
    uint16_t g = ai_cached_first_group();
    if (g == THING_ENDOFLIST || group_creature(g) != c->item)
        return false;
    if (c->mask == GROUP_TARGET_ANY)
        return true;
    if (c->mask == GROUP_TARGET_CARRIES) {
        for (uint16_t t = thing_word(g, GROUP_POSSESSIONS); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
            if (thing_type(t) != THING_PROJECTILE && thing_cell(t) != s->dir)
                return true;
        return false;
    }
    int16_t nx = (int16_t)(x + g_dir_dx[s->dir]);
    int16_t ny = (int16_t)(y + g_dir_dy[s->dir]);
    bool r = at_party(nx, ny);
    if (!r) {
        uint16_t other = creature_get_group_at((Location){nx, ny});
        r = other != THING_NONE && !(creature_group_info(other)->flags & CREATURE_STATIC);
    }
    if (!r)
        s->visit = VISIT_BLOCKED;
    return r;
}

/* The command test of a packed mode byte of a candidate: the mode and the bit AI_EXECUTE. */
static AiTest packed_test(uint8_t mode)
{
    return (AiTest){mode, mode & AI_EXECUTE};
}

/* The door target: a door that the creature can open or close (DOOR_MODE_* in the mask) and that is not yet so. */
static bool blocked_door(Search *s, const AiCandidate *c, int16_t x, int16_t y)
{
    if (c->item == DOOR_TARGET_PARTY && !at_party(x, y))
        return false;
    if (!ai_try_door_action(packed_test((uint8_t)c->mask), ai_location(s->from_x, s->from_y), ai_location(x, y), s->dir)
             .passed)
        return false;
    if (c->item == DOOR_TARGET_NEXT_TO_PARTY && axis_distance(x, y, g_ai_party.x, g_ai_party.y) != 1)
        return false;
    ai_cache_square((Location){x, y});
    if (c->mask == DOOR_MODE_OPEN && !(g_ai_square.square & SQUARE_DOOR_STATE))
        return false;
    if (c->mask == DOOR_MODE_CLOSE && (g_ai_square.square & SQUARE_DOOR_STATE) == DOOR_STATE_CLOSED)
        return false;
    uint16_t door = cached_first_thing();
    if (!door_flag(door, DOOR_MOVING))
        return true;
    if (door_flag(door, DOOR_OPENING))
        return c->mask != DOOR_MODE_OPEN;
    return c->mask != DOOR_MODE_CLOSE;
}

/* The test of a candidate on a square that the search could not enter (from the square it left). */
static bool test_blocked(Search *s, int i)
{
    AiCandidate *c = &s->cands[i];
    int16_t x = s->x;
    int16_t y = s->y;
    switch ((uint8_t)c->kind) {
    case TARGET_PARTY_VIEW:
        return blocked_party_view(s, c, x, y);
    case TARGET_ACTIVATION:
        return at_party(x, y);
    case TARGET_MELEE_LINE:
        if (square_type(g_ai_square.square) == SQUARE_WALL)
            return false;
        __attribute__((fallthrough));
    case TARGET_SHOOT_LINE:
        return line_target(s, c, ai_location(s->from_x, s->from_y), s->dir, false);
    case TARGET_ITEM:
    case TARGET_ITEM_WALL: {
        AiCommand r = ai_try_take_item((AiTest){ITEM_MODE_TAKE, false}, ai_location(s->from_x, s->from_y),
                                       ai_location(x, y), s->dir, (int16_t)c->item);
        if (!r.passed && g_ai_square_scan.items_elsewhere)
            s->visit = VISIT_BLOCKED;
        return r.passed;
    }
    case TARGET_GROUP_FACING:
        if (group_dir(g_ai.group_thing) != s->dir)
            return false;
        __attribute__((fallthrough));
    case TARGET_GROUP_IN_FRONT:
        return blocked_group_in_front(s, c, x, y);
    case TARGET_DAMAGE_GROUP:
        return damage_group_at(c, x, y);
    case TARGET_ACTUATOR: {
        if (!(g_ai_square.square & SQUARE_HAS_THINGS))
            return false;
        AiCommand r = ai_try_use_wall_actuator(packed_test((uint8_t)c->mask), ai_location(s->from_x, s->from_y),
                                               ai_location(x, y), s->dir, (int16_t)c->item);
        if (!r.passed)
            s->visit = VISIT_BLOCKED;
        return r.passed;
    }
    case TARGET_LIGHT:
        if (g_ai_square.square & SQUARE_HAS_THINGS) {
            light_add_square_light(s->dist, s->dir, x, y, LIGHT_WALLS);
            s->visit = VISIT_BLOCKED;
        }
        return false;
    case TARGET_DOOR:
        return blocked_door(s, c, x, y);
    case TARGET_PARTY_DISTANCE: {
        uint8_t value = (uint8_t)(s->blocking ? 1 << s->dir | DISTANCE_NEXT_TO : s->dist + 1);
        mark_distance(s, &g_party_distance, value, true);
        mark_distance(s, &g_alt_distance, value, true);
        return false;
    }
    default:
        return false;
    }
}

/* Runs the tests of the candidates (entered or blocked) on the current square; returns the found candidate index, -1
   for none. */
static int test_candidates(Search *s, bool blocked)
{
    for (int i = 0; i < s->count; i++) {
        AiCandidate *c = &s->cands[i];
        if (blocked ? c->cost + 1 < s->dist : c->cost < s->dist)
            continue;
        if (!(c->kind_flags & (blocked ? KIND_TESTS_BLOCKED : KIND_TESTS_OPEN)))
            continue;
        if (!(blocked ? test_blocked(s, i) : test_entered(s, i)))
            continue;
        if (s->two_maps && g_party.cur_map != s->start_map && g_party.cur_map != g_ai_party.map)
            continue;
        return i;
    }
    return -1;
}

/* Stores a found candidate and narrows the search to the better candidates; true when the search ends. */
static bool search_found(Search *s, int i)
{
    AiCandidate *c = &s->cands[i];
    if (!(c->kind_flags & KIND_NO_LOCATION)) {
        c->destination = c->kind_flags & KIND_PARTY_LOCATION ? pack_location(g_ai_party.x, g_ai_party.y, g_ai_party.map)
                                                             : pack_location(s->x, s->y, s->check_map);
        if (s->visit) {
            c->path_length = (int8_t)(s->dist - 1);
            s->x = s->from_x;
            s->y = s->from_y;
            s->check_map = s->from_map;
        } else {
            c->path_length = (int8_t)s->dist;
        }
        c->found = pack_location(s->x, s->y, s->check_map);
    }
    s->result = (int16_t)i;
    if (i == 0 && (!(c->kind_flags & KIND_FIRST_BEST) || c->cost <= s->dist))
        return true;
    s->count = i + 1;
    /* fix: for i 0 the original reads the record before the candidates; with zero bytes there it changes nothing */
    int j = i - 1;
    if (j >= 0) {
        while ((int8_t)s->cands[j].value < 0) {
            if (j == 0)
                return true;
            j--;
        }
        if (!(c->kind_flags & KIND_FIRST_BEST))
            s->count = j + 1;
        if (!(c->kind_flags & KIND_NO_BONUS)) {
            int16_t bonus = 0;
            for (int k = j; k >= 0; k--) {
                int8_t v = (int8_t)s->cands[k].value;
                if (v > 0)
                    bonus = (int16_t)(bonus + v);
                if (bonus + s->dist < s->cands[k].cost)
                    s->cands[k].cost = (int8_t)(s->dist + bonus);
            }
        }
    }
    s->or_flags = 0;
    s->max_cost = 0;
    for (int k = 0; k < s->count; k++) {
        uint8_t flags = s->cands[k].kind_flags;
        s->or_flags |= flags;
        if (k == 0 || s->cands[k].path_length < 0)
            s->max_cost = max_int(s->cands[k].cost + (flags & KIND_TESTS_BLOCKED ? 1 : 0), s->max_cost);
    }
    return false;
}

/* A square that the search entered: its tests, then its distance and the way back in the search map; the square
   goes into the queue when its distance is in the range of the candidates. Returns the found candidate, -1 none. */
static int search_entered(Search *s)
{
    s->check_map = g_party.cur_map;
    int i = s->or_flags & KIND_TESTS_OPEN ? test_candidates(s, false) : -1;
    int16_t dist = (int16_t)(s->dist + g_ai_square.cost);
    if (dist <= s->max_cost) {
        s->queue[s->tail].x = (uint8_t)s->x;
        s->queue[s->tail].y = (uint8_t)s->y;
        s->queue[s->tail].map = (uint8_t)s->check_map;
        s->tail = (s->tail + 1) & SEARCH_QUEUE_MASK;
        /* fix: the original counts past its SEARCH_LEVELS levels into its own variables */
        s->counts[(uint8_t)dist]++;
    }
    uint8_t *cell = search_cell(s->map, s->x, s->y);
    uint16_t from = pack_location(s->from_x, s->from_y, s->from_map);
    cell[SEARCH_AT_DIST] = (uint8_t)dist;
    cell[SEARCH_AT_DIR] = (uint8_t)s->dir;
    put_le16(cell + SEARCH_AT_FROM, from);
    return i;
}

/* A square that the search could not enter: the tests of the candidates that look at such squares. */
static int search_blocked(Search *s)
{
    s->blocking = g_ai_square_scan.blocking;
    s->visit = (uint8_t)s->dist;
    int i = -1;
    if (s->or_flags & KIND_TESTS_BLOCKED) {
        if (s->or_flags & KIND_CACHES_SQUARE)
            ai_cache_square((Location){s->x, s->y});
        i = test_candidates(s, true);
    }
    if (!s->blocking)
        search_cell(s->map, s->x, s->y)[SEARCH_AT_DIST] = s->visit;
    return i;
}

/* Makes the map of a queued square the current map of the search. */
static void search_enter_map(Search *s, int16_t map)
{
    if (map == g_party.cur_map)
        return;
    ai_set_map(map);
    s->map = s->maps[map];
}

/* Tries the step of the search in s->dir (DIR_LEVEL_UP, DIR_LEVEL_DOWN: the level changes) from the square it left. */
static SearchStep search_step(Search *s)
{
    search_enter_map(s, s->from_map);
    if (s->levels && s->steps_left <= SEARCH_LEVEL_STEPS) {
        if (s->steps_left == SEARCH_LEVEL_STEPS) {
            if (!s->down_ok)
                return SEARCH_STEP_SKIP;
            s->dir = DIR_LEVEL_DOWN;
        } else {
            if (!s->up_ok)
                return SEARCH_STEP_SKIP;
            s->dir = DIR_LEVEL_UP;
        }
        s->x = s->from_x;
        s->y = s->from_y;
    } else {
        s->x = (int16_t)(s->from_x + g_dir_dx[s->dir]);
        s->y = (int16_t)(s->from_y + g_dir_dy[s->dir]);
        if (!on_map(s->x, s->y))
            return SEARCH_STEP_SKIP;
    }
    g_ai_square_scan.blocking = 0;
    s->visit = search_cell(s->map, s->x, s->y)[SEARCH_AT_DIST];
    if (!s->visit || s->dir >= DIR_LEVEL_UP) {
        AiMoveTest move = ai_move_test(s->command);
        if (!ai_try_move(move, ai_location(s->from_x, s->from_y), ai_location(s->x, s->y), s->dir).passed) {
            s->visit = VISIT_BLOCKED;
        } else {
            s->passage_map = -1;
            s->passage_x = s->x;
            s->passage_y = s->y;
            if (g_ai_square.map != g_party.cur_map) {
                if (!s->maps[g_ai_square.map])
                    s->maps[g_ai_square.map] = calloc(MAP_SIDE_MAX, SEARCH_COLUMN_SIZE);
                if (!s->maps[g_ai_square.map])
                    os_fatal("out of memory");
                if (s->dir < DIR_LEVEL_UP)
                    s->passage_map = g_party.cur_map;
                ai_set_map(g_ai_square.map);
                s->map = s->maps[g_party.cur_map];
            } else if (s->dir < DIR_LEVEL_UP) {
                if (g_ai_square_scan.passage) {
                    s->passage_map = g_ai_square_scan.end.map;
                    s->passage_x = g_ai_square_scan.end.x;
                    s->passage_y = g_ai_square_scan.end.y;
                } else if (g_ai_square.x != s->x || g_ai_square.y != s->y) {
                    s->passage_map = g_party.cur_map;
                }
            }
            s->visit = 0;
        }
    }
    if (s->visit)
        return s->visit == VISIT_BLOCKED && s->dir < DIR_LEVEL_UP ? SEARCH_STEP_BLOCKED : SEARCH_STEP_SKIP;
    s->x = g_ai_square.x;
    s->y = g_ai_square.y;
    if (search_cell(s->map, s->x, s->y)[SEARCH_AT_DIST])
        return SEARCH_STEP_SKIP;
    return SEARCH_STEP_ENTERED;
}

/* Takes the next square from the queue; false when the search ends. */
static bool search_next_square(Search *s)
{
    for (;;) {
        if (s->tail == s->head)
            return false;
        s->from_x = s->queue[s->head].x;
        s->from_y = s->queue[s->head].y;
        s->from_map = s->queue[s->head].map;
        s->head = (s->head + 1) & SEARCH_QUEUE_MASK;
        search_enter_map(s, s->from_map);
        s->dist = search_cell(s->map, s->from_x, s->from_y)[SEARCH_AT_DIST];
        if (!s->cost_search)
            break;
        bool deferred = false;
        while (s->level < s->dist) {
            if (s->counts[s->level]) {
                s->queue[s->tail].x = (uint8_t)s->from_x;
                s->queue[s->tail].y = (uint8_t)s->from_y;
                s->queue[s->tail].map = (uint8_t)s->from_map;
                s->tail = (s->tail + 1) & SEARCH_QUEUE_MASK;
                deferred = true;
                break;
            }
            s->level++;
        }
        if (deferred)
            continue;
        if (!--s->counts[s->level])
            s->level++;
        break;
    }
    if (s->dist > s->max_cost)
        return false;
    if (s->dist > SEARCH_MOVERS_DISTANCE)
        s->command |= AI_MOVE_NO_MOVERS;
    if (s->flee_once && s->dist > 1) {
        s->command = (s->command & AI_MOVE_FLAG_BITS) | MOVE_KIND_SAFE;
        s->flee_once = false;
    }
    s->turn = g_search_direction_bits & 1 ? 1 : -1;
    s->dir = next_direction_bits() & DIR_MASK;
    s->steps_left = s->levels ? SEARCH_STEPS_LEVELS : SEARCH_STEPS;
    return true;
}

/* Writes the steps from the start to the found candidate into path (2 bytes per step). */
static void search_write_path(Search *s, uint8_t *path)
{
    AiCandidate *c = &s->cands[s->result];
    int16_t x = location_x(c->found);
    int16_t y = location_y(c->found);
    int16_t map = location_map(c->found);
    for (int16_t n = (int16_t)(c->path_length - 1); n >= 0; n--) {
        uint8_t *cell = search_map_cell(s, map, x, y);
        uint16_t from = le16(cell + SEARCH_AT_FROM);
        uint16_t step = le16(path + n * 2) & PATH_KEEP_BITS;
        step |= path_step_pack(cell[SEARCH_AT_DIR], x, y);
        if (location_map(from) != map) {
            map = location_map(from);
            step |= PATH_MAP_CHANGE;
        }
        put_le16(path + n * 2, step);
        x = location_x(from);
        y = location_y(from);
    }
}

/* Starts a search from the square start of the current map: the candidates, the level changes, and the first search
   map. */
static void search_start(Search *s, Location start, int command, int count, AiCandidate *candidates)
{
    int16_t x = start.x;
    int16_t y = start.y;
    s->command = (uint8_t)command;
    s->count = count;
    s->cands = candidates;
    s->start_x = x;
    s->start_y = y;
    s->start_map = g_party.cur_map;
    s->result = -1;
    s->enabled = enabled_byte(s->command);
    s->two_maps = !s->enabled && class_flags() & AI_CLASS_STAYS_ON_MAPS;
    s->flee_once = (s->command & AI_MOVE_KIND_MASK) == MOVE_KIND_SAFE_FLEE;
    search_setup(s);
    if (g_ai.move_flags & MOVE_LEVELS && g_ai.move_flags & MOVE_LEVEL_FLAGS) {
        s->levels = true;
        s->up_ok = g_ai.move_flags & MOVE_LEVEL_UP_FLAGS;
        s->down_ok = g_ai.move_flags & MOVE_LEVEL_DOWN_FLAGS;
    }
    /* fix: the original clears the first search map only for line targets or a cost; the port always clears it */
    s->map = calloc(MAP_SIDE_MAX, SEARCH_COLUMN_SIZE);
    s->maps[g_party.cur_map] = s->map;
    if (!s->map)
        os_fatal("out of memory");
    s->cost_search = (s->command & AI_MOVE_KIND_MASK) == MOVE_KIND_COST;
    if (s->cost_search)
        s->counts[SEARCH_LEVELS - 1] = SEARCH_LEVEL_BLOCKED;
    s->from_map = g_party.cur_map;
    s->check_map = g_party.cur_map;
    s->from_x = x;
    s->x = s->from_x;
    s->from_y = y;
    s->y = s->from_y;
    s->steps_left = 1;
    s->dir = -1;
    s->passage_map = -1;
    ai_cache_square((Location){x, y});
    if (!s->locations)
        return;
    g_ai_square_scan.passage = (uint8_t)dun_get_linked_teleporter_dest(&g_ai_square_scan.end, (Location){x, y});
    if (g_ai_square_scan.passage) {
        s->passage_map = g_ai_square_scan.end.map;
        s->passage_x = g_ai_square_scan.end.x;
        s->passage_y = g_ai_square_scan.end.y;
    }
}

int ai_search_area(Location from, int command, int count, AiCandidate *candidates, uint8_t *out_path)
{
    if (candidates[0].kind == TARGET_ANY) {
        candidates[0].found = pack_location(from.x, from.y, g_party.cur_map);
        candidates[0].destination = candidates[0].found;
        candidates[0].path_length = 0;
        return 0;
    }
    Search *s = calloc(1, sizeof *s);
    if (!s)
        os_fatal("out of memory");
    int8_t marks[SEARCH_MARKS][MARK_SIZE];
    s->marks = marks;
    search_start(s, from, command, count, candidates);
    int found = search_entered(s);
    for (;;) {
        if (found >= 0) {
            if (search_found(s, found))
                break;
            found = -1;
        }
        s->dir = (int16_t)dir_turn(s->dir, s->turn);
        if (--s->steps_left && ((s->tail + 1) & SEARCH_QUEUE_MASK) != s->head) {
            SearchStep step = search_step(s);
            if (step == SEARCH_STEP_ENTERED)
                found = search_entered(s);
            else if (step == SEARCH_STEP_BLOCKED)
                found = search_blocked(s);
            continue;
        }
        if (!search_next_square(s))
            break;
        s->dir = (int16_t)dir_turn(s->dir, -s->turn); /* the first direction has no turn */
    }
    if (s->result != -1)
        search_write_path(s, out_path);
    ai_set_map(s->start_map);
    int result = s->result;
    for (int m = 0; m < SEARCH_MAPS; m++)
        free(s->maps[m]);
    free(s);
    return result;
}

/* ---- Paths ---- */

void ai_load_cached_path(void)
{
    g_ai.cached_path = NULL;
    if (!g_ai.active || !g_ai.cache.path_length)
        return;
    uint8_t *path = ai_find_path();
    if (!path) {
        g_ai.cache.path_left = 0;
        g_ai.cache.path_length = 0;
    } else {
        g_ai.cached_path = path;
    }
}

/* A step of the stored path. */
static uint16_t stored_step(int index)
{
    return le16(g_ai.cached_path + index * 2);
}

int ai_next_path_step(void)
{
    ActiveGroup *a = g_ai.active;
    ai_load_cached_path();
    int left = g_ai.cache.path_length ? g_ai.cache.path_left : 0;
    if (left) {
        a->direction = (uint8_t)path_step_dir(stored_step(g_ai.cache.path_length - left));
        return left;
    }
    uint16_t found = g_ai.cache.found;
    if (g_ai.map == location_map(found) && own_x() == location_x(found) && own_y() == location_y(found))
        g_ai.cache.found = FOUND_NONE;
    return left;
}

/* True when the target of the plan is still at the found square and the next steps of the stored path from the
   group on the map saved are free. */
static bool path_holds(int16_t saved)
{
    int16_t x = location_x(g_ai.cache.found);
    int16_t y = location_y(g_ai.cache.found);
    uint8_t path[AI_PATH_MAX * 2];
    /* the search of the one candidate (index 0) from the found square: the target must be on that square */
    if (ai_search_area(ai_location(x, y), MOVE_KIND_NONE, 1, g_ai.candidates, path) != 0 ||
        g_ai.candidates[0].path_length)
        return false;
    int16_t cx = own_x();
    int16_t cy = own_y();
    int16_t left = g_ai.cache.path_left;
    if (!left)
        return cx == x && cy == y && g_party.cur_map == saved;
    ai_set_map(saved);
    ai_load_cached_path();
    /* fix: without a stored path the original reads its steps at address 0 */
    if (!g_ai.cached_path)
        return false;
    for (int16_t checks = PATH_CHECK_STEPS;;) {
        uint16_t step = stored_step(g_ai.cache.path_length - left);
        if (!ai_try_move(ai_move_test(PATH_CHECK_COMMAND), ai_location(cx, cy), AI_SQUARE_IN_DIR, path_step_dir(step))
                 .passed)
            return false;
        if (path_step_x(step) != g_ai_square.x || path_step_y(step) != g_ai_square.y ||
            path_step_map_change(step) != (g_ai_square.map != g_party.cur_map))
            return false;
        ai_set_map(g_ai_square.map);
        cx = g_ai_square.x;
        cy = g_ai_square.y;
        if (--left <= 0 || --checks <= 0)
            break;
    }
    return true;
}

bool ai_plan_path(void)
{
    ai_load_cached_path();
    int16_t i = 0;
    while (i < g_ai.candidate_count &&
           !(g_ai.candidates[i].kind == g_ai.cache.kind && g_ai.candidates[i].item == g_ai.cache.item))
        i++;
    if (i >= g_ai.candidate_count)
        return false;
    if (i)
        g_ai.candidates[0] = g_ai.candidates[i];
    g_ai.candidates[0].cost = 0;
    int16_t saved = g_party.cur_map;
    ai_set_map(location_map(g_ai.cache.found));
    bool result = path_holds(saved);
    ai_set_map(saved);
    return result;
}

void ai_refresh_party_maps(void)
{
    int16_t saved = g_party.cur_map;
    dun_set_current_map(g_game.party_map);
    AiCandidate target;
    uint8_t path[AI_PATH_MAX * 2];
    memset(&target, 0, sizeof target);
    if (g_party.redraw & REDRAW_SQUARES) {
        g_party_distance.map = (int16_t)g_game.party_map;
        g_party_distance.width = (int16_t)dungeon_map_width(g_game.party_map);
        memset(g_party_distance.cells, 0, (size_t)g_party_distance.width * MAP_SIDE_MAX);
        g_alt_distance.map = g_party.alt_view_map;
        if (g_alt_distance.map >= 0) {
            g_alt_distance.width = (int16_t)dungeon_map_width(g_alt_distance.map);
            memset(g_alt_distance.cells, 0, (size_t)g_alt_distance.width * MAP_SIDE_MAX);
        }
        target.cost = PARTY_MAP_SEARCH_COST;
        target.value = 0;
        target.kind = TARGET_PARTY_DISTANCE;
        ai_search_area(ai_location(g_game.party_x, g_game.party_y), MOVE_KIND_COST, 1, &target, path);
    }
    if (g_party.redraw & REDRAW_VIEW) {
        bool changed = g_game.party_map != g_last_party_map;
        int radius = gdat_entry_index(gdat_current(), CAT_WALLSET, dungeon_map_wallset(g_game.party_map),
                                      GDAT_TYPE_VALUE, WALLSET_KEY_LIGHT);
        if (!radius) {
            g_light_source_sum = 0;
            g_explosion_light_sum = 0;
        } else {
            if (radius > LIGHT_RADIUS_MAX)
                radius = LIGHT_RADIUS_MAX;
            target.cost = (int8_t)radius;
            target.value = 0;
            target.kind = TARGET_LIGHT;
            ai_search_area(ai_location(g_game.party_x, g_game.party_y), MOVE_KIND_SIGHT, 1, &target, path);
            if (g_light_source_sum != g_cached_light_sum || g_explosion_light_sum != g_cached_explosion_light_sum)
                changed = true;
        }
        g_cached_light_sum = g_light_source_sum;
        g_cached_explosion_light_sum = g_explosion_light_sum;
        if (changed)
            light_update_party_light();
    }
    dun_set_current_map(saved);
    g_party.redraw = 0;
    g_last_party_map = (int16_t)g_game.party_map;
}
