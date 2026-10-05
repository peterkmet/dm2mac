/* Creature perception and movement: what blocks a square or a line of sight, the item filters, the tests of the AI
   commands (move, attack, doors, items, wall actuators), and the path search. */
#include <stdlib.h>
#include <string.h>
#include "ai_move.h"
#include "champion.h"
#include "creature.h"
#include "draw.h"
#include "dungeon.h"
#include "hooks.h"
#include "item.h"
#include "move.h"
#include "projectile.h"
#include "random.h"
#include "save.h"
#include "sensor.h"
#include "view.h"

#define CAT_WALLSET 8
#define CAT_ORNAMENT 9
#define CAT_DOOR 0x0e
#define CAT_CREATURE 0x0f
#define DOOR_KEY_SEE_THROUGH 0x0d       /* door value: creatures see through the closed door */
#define DOOR_KEY_SOMETIMES_SEEN 0x10    /* ... creatures see through the moving door half of the time */
#define DOOR_KEY_PASS_COST 0x11         /* ... the search cost of a door square */
#define DOOR_DEFAULT_PASS_COST 6
#define ORNAMENT_KEY_TARGET 0x0e        /* ornament value: the filter value of a wall actuator ornament */
#define ORNAMENT_KEY_LEVEL_LINK 0x11    /* ... a level link text: 0 leads down, else up */
#define ORNAMENT_KEY_SHOOT_TARGET 0xf0  /* ... a target that ranged creatures shoot at */
#define WALLSET_KEY_WALL_COST 0x72      /* wall set value: the search cost of a wall square */
#define WALLSET_KEY_LIGHT 0x6d          /* ... the light radius of the map */
#define WALL_COST_BLOCKING 0x32
#define LIGHT_RADIUS_MAX 8
#define TEXT_KIND_MASK 0x0006
#define TEXT_KIND_ORNAMENT 0x0002
#define TEXT_KIND_MARKER 0x0004
#define TEXT_SUBTYPE_SHIFT 11
#define TEXT_LADDER 6
#define TEXT_LADDER_HIDDEN 7
#define MARKER_LEVEL_SHIFT 12           /* AI marker texts: data >> 3 holds id bits 0-4, kind 5-8, level 9-12 */
#define SENSOR_LEVEL_LINK 0x1c
#define SENSOR_CREATURE_SLOT 0x26       /* a wall slot that a creature type uses */
#define SENSOR_FLAG_ACTIVE 0x0004       /* sensor word +4 */
#define SENSOR_STATE_SHIFT 3            /* sensor word +4 bits 3-4: state */
#define SENSOR_STATE_MASK 0x18
#define SENSOR_STATE_BUSY 0x10
#define ACTUATOR_SWITCH 1
#define ACTUATOR_ANY_ITEM 2
#define ACTUATOR_ITEM 3
#define ACTUATOR_TOGGLE 0x17
#define ACTUATOR_BUTTON 0x18
#define ACTUATOR_SLOT 0x1a
#define ACTUATOR_COUNTED_ITEM 0x1b
#define ACTUATOR_BUTTON_2 0x4a
#define EXPLOSION_OPEN_DOOR 0x04
#define EXPLOSION_POISON_CLOUD 0x07
#define EXPLOSION_DOOR_SPELL 0x0d
#define EXPLOSION_LASTING_CLOUD 0x0e    /* blocks the sight */
#define EXPLOSION_THING_BASE 0xff80
/* Door record word +2 bits that the AI reads. */
#define DOOR_BUTTON 0x0040
#define DOOR_BREAKABLE 0x0080
#define DOOR_BURNABLE 0x0100
#define DOOR_LOCK_OPEN 0x0200
#define DOOR_LOCKED 0x0400
#define DOOR_MARKED 0x1000
#define DOOR_OPENER 0x2000
#define DOOR_STATE_OPEN 0
#define DOOR_STATE_ONE_FOURTH 1
#define DOOR_STATE_HALF 2
#define DOOR_STATE_CLOSED 4
#define DOOR_STATE_BROKEN 5
#define DOOR_HEIGHT_PASSABLE_SHIFT 6    /* creature flags bits 6-7: how far a door may be closed for the creature */
#define TELEPORTER_TARGET_KIND_SHIFT 13
#define STATIC_POSSESSIONS_SHOWN 0x400
#define STATIC_POSSESSIONS_ALL 0x200

/* Creature info words. */
#define info_attack_range(i) ((i)->unknown_14 >> 12)
#define info_sight(i) ((i)->unknown_14 & 0xf)
#define CREATURE_SEES_INVISIBLE 0x0400
#define CREATURE_SEES_IN_DARK 0x0800
#define CREATURE_NOT_SEEN_THROUGH CREATURE_NOT_DOOR_HIT /* flag 0x20: a creature that hides the squares behind it */
/* The AI class flags (ai_class_flags). */
#define AI_CLASS_SEES_ALL_SIDES 0x0100
#define AI_CLASS_SEES_BEHIND 0x0400
#define AI_CLASS_TURNS_TO_TAKE 0x1000
#define AI_CLASS_STAYS_ON_MAPS 0x2000   /* targets only on its own map and on the party map */
#define AI_CLASS_AVOIDS_CLOUDS 0x0200
#define AI_CLASS_AVOIDS_PARTY_DOORS 0x4000
/* ai.move_flags: the squares that the creature may enter. */
#define MOVE_WALL 0x0001
#define MOVE_FLOOR 0x0002
#define MOVE_CLOSED_PIT 0x0004
#define MOVE_OPEN_PIT 0x0008
#define MOVE_FALL 0x0010
#define MOVE_INVISIBLE_PIT 0x0020
#define MOVE_HIDDEN_FAKE_WALL 0x0040
#define MOVE_FAKE_WALL 0x0080
#define MOVE_STAIRS 0x0100
#define MOVE_LOCKED_DOOR 0x0200
#define MOVE_TELEPORTER 0x0400
#define MOVE_PARTY 0x0800
#define MOVE_GROUPS 0x1000
#define MOVE_LEVELS 0x2000
#define MOVE_DOOR 0x4000
#define MOVE_FALLS 0x8000
#define MOVE_LEVEL_FLAGS 0x0118
#define MOVE_DIR4_FLAGS 0x0110         /* the moves of a level change up (dir 4) */
#define MOVE_DIR5_FLAGS 0x0108         /* ... down (dir 5) */
#define MOVE_FLAGS_LIGHT 0x0227
#define MOVE_FLAGS_PARTY_MAP 0x36e7
#define MOVE_FLAGS_NO_FALL 0x7db7
/* ai.attack_flags. */
#define ATTACK_MELEE_MASK 0x0007
#define ATTACK_RANGED_MASK 0x0ff8
#define ATTACK_STEAL 0x0004
/* ai.door_flags. */
#define DOOR_SKILLS_OPEN 0x6f
#define DOOR_SKILLS_CLOSE 0x73
#define DOOR_SKILL_BUTTON 0x0003
#define DOOR_SKILL_SHOOT_BUTTON 0x0001
#define DOOR_SKILL_BASH 0x0004
#define DOOR_SKILL_BURN 0x0008
#define DOOR_SKILL_MARK 0x0020
#define DOOR_SKILL_HANDLE 0x0040
#define DOOR_SKILL_ANY_SIDE 0x0020
/* ai.item_flags. */
#define ITEM_SKILL_SAME_SQUARE 0x0001
#define ITEM_SKILL_WALL 0x0004
#define ITEM_SKILL_STATIC 0x0008
#define ITEM_SKILL_CHESTS 0x0010
#define ITEM_SKILL_GROUPS 0x0020
#define ITEM_SKILL_OTHER_SQUARE 0x002a
/* AI actions (ActiveGroup.action). */
enum {
    ACTION_NONE = 0, ACTION_MOVE = 1, ACTION_MOVE_NEAR = 2, ACTION_STEP_RIGHT = 3, ACTION_STEP_LEFT = 4,
    ACTION_FALL = 5, ACTION_TURN_LEFT = 6, ACTION_MELEE = 8, ACTION_TURN_BACK = 9, ACTION_STEAL = 0x0a,
    ACTION_BASH_DOOR = 0x0b, ACTION_TAKE_LEFT = 0x0c, ACTION_THROW = 0x0e, ACTION_GIVE = 0x17,
    ACTION_TAKE_FROM_GROUP = 0x18, ACTION_TAKE_FROM_WALL = 0x19, ACTION_PUT_IN_WALL = 0x1a, ACTION_MELEE_2 = 0x26,
    ACTION_SHOOT = 0x27, ACTION_TAKE_FRONT = 0x2b, ACTION_TAKE_SIDE = 0x2c, ACTION_USE_ACTUATOR = 0x2f,
    ACTION_STAIRS_UP = 0x35, ACTION_STAIRS_DOWN = 0x36, ACTION_FALL_IN_PIT = 0x37, ACTION_PIT = 0x38,
    ACTION_LINK_UP = 0x39
};
#define AI_ACTION_FLAG_MOVES 0x02
#define AI_ACTION_FLAG_BUSY 0x03
/* Sub-commands of the shoot action (ActiveGroup.sub). */
#define SHOT_STEAL 0x0b
/* Search target kinds (AiCandidate.kind) and their flags (kind_flags). */
enum {
    TARGET_ANY, TARGET_HOME, TARGET_PARTY_VIEW, TARGET_ACTIVATION, TARGET_PARTY_RANGE_2, TARGET_FLEE,
    TARGET_SHOOT_PARTY, TARGET_MELEE_PARTY, TARGET_MELEE_LINE, TARGET_SHOOT_LINE, TARGET_STEAL, TARGET_MARKER,
    TARGET_NEAR_MARKER, TARGET_WANDER, TARGET_RANDOM, TARGET_ITEM, TARGET_ITEM_2, TARGET_ITEM_WALL,
    TARGET_GROUP_IN_FRONT, TARGET_GROUP_FACING, TARGET_DAMAGE_GROUP, TARGET_MOVE_TEST, TARGET_ACTUATOR,
    TARGET_LIGHT, TARGET_AI_MARKER, TARGET_MARKER_SIDE, TARGET_DOOR, TARGET_PARTY_DISTANCE
};
#define KIND_TESTS_OPEN 0x01      /* the kind tests the squares that the search enters */
#define KIND_TESTS_BLOCKED 0x02   /* ... the squares that it cannot enter */
#define KIND_FIRST_BEST 0x04      /* a found target may still get a nearer better one */
#define KIND_CACHES_SQUARE 0x08
#define KIND_NO_LOCATION 0x10     /* the found target keeps its location */
#define KIND_PARTY_LOCATION 0x20  /* the found target is the party square; also: needs the party seen */
#define KIND_NO_BONUS 0x40
/* The flags of each target kind (0x752ea of SKULL.EXE). */
static const uint8_t kind_flags[28] = {
    0x41, 0x01, 0x23, 0x21, 0x21, 0x25, 0x21, 0x21, 0x1b, 0x1b, 0x21, 0x01, 0x05, 0x45, 0x01, 0x03,
    0x01, 0x02, 0x0a, 0x0a, 0x4f, 0x01, 0x0a, 0x4f, 0x05, 0x01, 0x02, 0x47
};
/* The cells of the possessions of a static creature by its display kind and the side it is seen from
   (0x71658 of SKULL.EXE; kinds 5 to 15 are the data after the table that the original reads). */
static const uint8_t possession_cells[16][4] = {
    {0, 0, 0, 0}, {4, 8, 1, 2}, {0x0c, 9, 3, 6}, {0x0e, 0x0d, 0x0b, 7}, {0x0f, 0x0f, 0x0f, 0x0f}, {0, 2, 0, 1},
    {1, 0, 4, 0}, {8, 0, 0x10, 0}, {0x20, 0, 0x40, 0}, {0x40, 0, 0x40, 0}, {2, 0, 0x20, 0}, {0x80, 0, 0, 0},
    {0, 0, 6, 8}, {0x12, 0x10, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}
};
/* The actions of a take from the same square (0, 1) or from the square in front (2, 3) by a random bit. */
static const uint8_t take_actions[4] = {0x29, 0x2a, 0x2e, 0x2d};
/* The bytes after ai_command_enabled in SKULL.EXE (0x752ac to 0x752e7): commands 0x20 and up read them. */
static const uint8_t after_command_table[60] = {
    0, 0, 0, 0, 0, 0, 0xff, 0xff, 0, 0, 0xff, 0xff, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0xff, 0xff, 0, 0, 0, 0,
    1, 0, 0, 0, 1, 0, 0, 0, 0xff, 0xff, 1, 0, 0, 0, 0xff, 0xff, 0, 0, 0, 0, 3, 0, 6, 0, 6, 0, 0, 3, 2, 1
};

#define SEARCH_QUEUE_SIZE 256
#define SEARCH_LEVELS 0x33              /* the counts of the cost search, the last one blocks */
#define SEARCH_CELL_SIZE 4              /* a square of a search map: distance, direction, previous square */
#define SEARCH_COLUMN_SIZE (32 * SEARCH_CELL_SIZE)
#define SEARCH_FAR 100
#define SEARCH_FAR_RANGE 0x7f
#define PATH_MAP_CHANGE 0x2000
#define PATH_KEEP_BITS 0xc000
#define FOUND_NONE 0xffff
/* The move command of the path check of creature_ai_plan_path: the original reads a byte after the path buffer
   that nothing writes. */
#define PATH_CHECK_COMMAND 0

AiSquareScan ai_square_scan;
int16_t light_source_sum, explosion_light_sum;
int16_t cached_light_sum = -1, cached_explosion_light_sum = -1, last_party_map = -1;
uint16_t search_direction_bits = 1;

/* ---- Squares ---- */

static int map_width(void) { return dungeon_map_width(party.cur_map); }
static int map_height(void) { return dungeon_map_height(party.cur_map); }

static int on_map(int x, int y)
{
    return (int16_t)x >= 0 && (int16_t)x < map_width() && (int16_t)y >= 0 && (int16_t)y < map_height();
}

/* The first thing of (x, y) of the current map; none away from the map. */
static uint16_t first_thing(int x, int y)
{
    return on_map(x, y) ? dungeon_square_first_thing(party.cur_map, x, y) : THING_ENDOFLIST;
}

static uint16_t first_object(int x, int y)
{
    uint16_t t = first_thing(x, y);
    while (thing_type(t) <= THING_SENSOR) t = dungeon_next_thing(t);
    return t;
}

static uint16_t pack_location(int x, int y, int map)
{
    return (uint16_t)((x & 0x1f) | (y & 0x1f) << 5 | (map & 0x3f) << 10);
}

static int abs16(int v) { return abs((int16_t)v); }

static int square_distance(int x1, int y1, int x2, int y2)
{
    return (int16_t)(abs16(x1 - x2) + abs16(y1 - y2));
}

/* The door type (0 or 1 of the current map) of a door thing, 0xff for none. */
static int door_type(uint16_t door)
{
    uint16_t flags = dungeon_map_flags(party.cur_map), graphics = dungeon_map_graphics(party.cur_map);
    if (!(thing_word(door, DOOR_DATA) & 1)) return flags & 0x80 ? graphics >> 8 & 0xf : 0xff;
    return flags & 0x100 ? graphics >> 12 : 0xff;
}

static int door_value_at(int x, int y, int key)
{
    return gdat_entry_index(draw_gdat(), CAT_DOOR, door_type(first_thing(x, y)), GDAT_TYPE_VALUE, key);
}

static int door_pass_cost(int x, int y)
{
    int cost = door_value_at(x, y, DOOR_KEY_PASS_COST);
    if (cost) return cost;
    return door_value_at(x, y, DOOR_KEY_SOMETIMES_SEEN) ? 1 : DOOR_DEFAULT_PASS_COST;
}

static int explosion_type(uint16_t t) { return thing_word(t, EXPLOSION_DATA) & EXPLOSION_TYPE_MASK; }

/* The direction in which a projectile flies (its move event). */
static int projectile_dir(uint16_t projectile)
{
    return timeline.events[thing_word(projectile, PROJECTILE_EVENT)].data[3] >> 2 & 3;
}

static int group_dir(void) { return ai.group[GROUP_DIR + 1] & 3; }
static int group_x(void) { return ai.event.data[0]; }
static int group_y(void) { return ai.event.data[1]; }

static int at_group(int x, int y)
{
    return (int16_t)x == group_x() && (int16_t)y == group_y();
}

static int at_party(int x, int y)
{
    return party.cur_map == ai_party.map && (int16_t)x == ai_party.x && (int16_t)y == ai_party.y;
}

static uint32_t class_flags(void) { return ai_class_flags[ai.class_index]; }

/* The enabled byte of a command; the commands past the table read the data after it in SKULL.EXE. */
static int command_enabled(int command)
{
    command &= 0xff;
    if (command < (int)sizeof ai_command_enabled) return ai_command_enabled[command];
    if (command < 0x20) {
        const int16_t words[6] = {party_distance.map, alt_distance.map, cached_light_sum, cached_explosion_light_sum,
                                  last_party_map, -1};
        return (uint8_t)(words[(command - 0x14) / 2] >> ((command & 1) * 8));
    }
    if (command < 0x20 + (int)sizeof after_command_table) return after_command_table[command - 0x20];
    return 0;  /* fix: further bytes are other globals of SKULL.EXE */
}

static void set_action_destination(int x, int y)
{
    ai.active->destination = pack_location(x, y, party.cur_map);
}

/* True for squares that the sight of a creature does not cross: walls, closed doors that hide, closed fake walls. */
int creature_square_is_obstacle(int x, int y)
{
    uint8_t square = dun_get_square(x, y);
    int type = square_type(square);
    if (type == SQUARE_DOOR)
        return (square & SQUARE_DOOR_STATE) == DOOR_STATE_CLOSED && !door_value_at(x, y, DOOR_KEY_SEE_THROUGH);
    return type == SQUARE_WALL || type == SQUARE_UNKNOWN || (type == SQUARE_FAKE_WALL && !(square & SQUARE_FAKE_WALL_OPEN));
}

/* True when a square stops a line of sight or of fire: walls, doors, clouds, and creatures that hide. */
int creature_square_blocks_line(int x, int y)
{
    int16_t sx = (int16_t)x, sy = (int16_t)y;
    uint8_t square = dun_get_square(sx, sy);
    int type = square_type(square);
    if (type == SQUARE_WALL) return 1;
    if (type == SQUARE_DOOR) {
        int state = square & SQUARE_DOOR_STATE;
        if ((state == 3 || state == DOOR_STATE_CLOSED) && (!door_value_at(sx, sy, DOOR_KEY_SOMETIMES_SEEN) || !rand_bit()))
            return 1;
    }
    if (type == SQUARE_FAKE_WALL && !(square & SQUARE_FAKE_WALL_OPEN)) return 1;
    if (!(square & 0x10)) return 0;
    for (uint16_t t = first_object(sx, sy); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        int ttype = thing_type(t);
        if (ttype == THING_EXPLOSION && explosion_type(t) == EXPLOSION_LASTING_CLOUD) return 1;
        if (ttype != THING_GROUP) continue;
        /* the original passes the square variables: a group found next to it moves them */
        uint16_t g = creature_find_group_near_point(&sx, &sy, 0xff);
        if (g == THING_NONE) continue;
        uint16_t flags = creature_group_info(g)->flags;
        if (flags & CREATURE_STATIC) {
            if ((flags >> DOOR_HEIGHT_PASSABLE_SHIFT & 3) < 2) return 1;
        } else if (!(flags & CREATURE_NOT_SEEN_THROUGH)) {
            return 1;
        }
    }
    return 0;
}

/* A line toward the party: the party square blocks it (the line ends there). */
int creature_square_blocks_line_to_target(int x, int y)
{
    if (at_party(x, y)) return 1;
    return creature_square_blocks_line(x, y);
}

/* A line test that ignores the square of the group in the AI context. */
int creature_square_blocks_line_except_self(int x, int y)
{
    if ((uint16_t)x == group_x() && (uint16_t)y == group_y()) return 0;
    return creature_square_blocks_line(x, y);
}

/* The slope of the line from (x1, y1) to (x, y) in 1/64, steep: by y; 0x80 when the main delta is 0. */
static int16_t line_slope(int steep, int x1, int y1, int x, int y)
{
    int16_t major = (int16_t)(steep ? y - y1 : x - x1);
    if (!major) return 0x80;
    return (int16_t)(((steep ? x - x1 : y - y1) << 6) / major);
}

/* Walks the squares of the line from (x2, y2) toward (x1, y1); returns 0 when blocked() stops it, else the
   square distance of the ends (1 for adjacent squares). */
int creature_trace_line(int x1, int y1, int x2, int y2, int (*blocked)(int x, int y))
{
    if (abs16(x1 - x2) + abs16(y1 - y2) <= 1) return 1;
    int16_t dx = (int16_t)abs16(x2 - x1), dy = (int16_t)abs16(y2 - y1);
    int steep = dx < dy, diagonal = dx == dy;
    int16_t step_x = (int16_t)(x2 - x1) > 0 ? -1 : 1, step_y = (int16_t)(y2 - y1) > 0 ? -1 : 1;
    int16_t slope = line_slope(steep, x1, y1, x2, y2);
    int16_t x = (int16_t)x2, y = (int16_t)y2;
    do {
        if (diagonal) {
            if (blocked((int16_t)(x + step_x), y) && blocked(x, (int16_t)(y + step_y))) return 0;
            y += step_y;
            x += step_x;
            if (blocked(x, y)) return 0;
        } else {
            int16_t err_x = (int16_t)abs16(line_slope(steep, x1, y1, x + step_x, y) - slope);
            int16_t err_y = (int16_t)abs16(line_slope(steep, x1, y1, x, y + step_y) - slope);
            if (err_x < err_y) x += step_x;
            else y += step_y;
            if (blocked(x, y)) {
                if (err_x != err_y) return 0;
                y -= step_y;  /* a tie: the other step */
                x += step_x;
                if (blocked(x, y)) return 0;
            }
        }
    } while (abs16(x - x1) + abs16(y - y1) > 1);
    return square_distance(x1, y1, x2, y2);
}

/* Makes (x, y) of the current map the square that the AI reads; keeps the cache when it is the same square. */
void creature_ai_cache_square(int x, int y)
{
    if ((int16_t)x == ai_square.x && (int16_t)y == ai_square.y && party.cur_map == ai_square.map) return;
    ai_square.map = party.cur_map;
    ai_square.x = (int16_t)x;
    ai_square.y = (int16_t)y;
    ai_square.square = dun_get_square(x, y);
    ai_square.first_thing = ai_square.square & 0x10 ? THING_NONE : THING_ENDOFLIST;
    ai_square.first_object = ai_square.first_group = ai_square.first_thing;
    ai_square_scan.blocking = 0;
    ai_square_scan.passage = 0;
    ai_square_scan.items_elsewhere = -1;
    ai_square.cost = 1;
}

static uint16_t cached_first_thing(void)
{
    if (ai_square.first_thing == THING_NONE) ai_square.first_thing = first_thing(ai_square.x, ai_square.y);
    return ai_square.first_thing;
}

uint16_t creature_ai_cached_first_object(void)
{
    if (ai_square.first_object != THING_NONE) return ai_square.first_object;
    uint16_t t = cached_first_thing();
    while (t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR) t = dungeon_next_thing(t);
    return ai_square.first_object = t;
}

uint16_t creature_ai_cached_first_group(void)
{
    if (ai_square.first_group != THING_NONE) return ai_square.first_group;
    uint16_t t = creature_ai_cached_first_object();
    while (t != THING_ENDOFLIST && thing_type(t) != THING_GROUP) t = dungeon_next_thing(t);
    return ai_square.first_group = t;
}

/* Turns the group toward dir (action turn left or right); returns 0 when it already faces dir. */
int creature_ai_turn_toward(int dir)
{
    int facing = group_dir(), turn;
    if (facing == ((dir + 2) & 3)) {
        turn = rand_bit() ? 1 : -1;
    } else if (facing == (int16_t)dir) {
        ai.active->action = ACTION_NONE;
        ai.command_result = AI_RESULT_DONE;
        return 0;
    } else {
        turn = facing == ((dir - 1) & 3) ? 1 : -1;
    }
    ai.active->facing = (uint8_t)((facing + turn) & 3);
    ai.active->action = (uint8_t)(ACTION_TURN_LEFT + (turn != -1));
    ai.command_result = AI_RESULT_ACTING;
    return 1;
}

static uint8_t *search_cell(uint8_t *map, int x, int y)
{
    return map + (int16_t)x * SEARCH_COLUMN_SIZE + (int16_t)y * SEARCH_CELL_SIZE;
}

/* Looks along the lines from (x, y) (dir -1: all four) for a target of filter in attack range: a group, or for a
   ranged attack a wall ornament that is a shooting target; then tests the attack from the farthest square of the
   line that the search reached no later. */
int creature_ai_find_target_in_lines(int filter, uint8_t *distance_map, uint16_t *out_location, int x, int y, int dir, int ranged)
{
    int range = ai.attack_flags & ATTACK_RANGED_MASK ? info_attack_range(ai.info) : 1;
    int16_t sx = (int16_t)x, sy = (int16_t)y, d = (int16_t)dir;
    int16_t left = 0;
    if (d == -1) {
        d = 0;
        left = 3;
    }
    int16_t tx = 0, ty = 0;
    int16_t i;
    for (;; d++, left--) {
        if (left < 0) return 0;
        tx = sx;
        ty = sy;
        int found = 0;
        for (i = 0; i < range; i++) {
            tx += dir_dx[d];
            ty += dir_dy[d];
            if (!on_map(tx, ty)) continue;
            uint8_t square = dun_get_square(tx, ty);
            if (square & 0x10) {
                if (square_type(square) == SQUARE_WALL) {
                    if (!ranged) break;
                    for (uint16_t t = first_thing(tx, ty); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
                        int type = thing_type(t);
                        if ((type != THING_SENSOR && type != THING_TEXT) || t >> THING_CELL_SHIFT != ((d + 2) & 3)) continue;
                        int ornament = item_gdat_index(t);
                        if (ornament != 0xff
                            && gdat_entry_index(draw_gdat(), CAT_ORNAMENT, ornament, GDAT_TYPE_VALUE, ORNAMENT_KEY_SHOOT_TARGET)
                                   == (uint16_t)filter) {
                            found = 1;
                            break;
                        }
                    }
                    if (found) break;
                } else if (!ranged && !at_group(tx, ty)) {
                    uint16_t g = creature_get_group_at((uint16_t)tx, (uint16_t)ty);
                    if (g != THING_NONE && creature_thing_matches_filter(g, (int16_t)filter)) {
                        found = 1;
                        break;
                    }
                }
            }
            if (*search_cell(distance_map, tx, ty) || creature_square_blocks_line_to_target(tx, ty)) break;
        }
        if (found) break;
    }
    /* back off along the line, away from the target, to squares that the search reached no later */
    int limit = *search_cell(distance_map, sx, sy);
    int back = (d + 2) & 3;
    for (i++; i < range; i++) {
        int16_t px = sx, py = sy;
        sx += dir_dx[back];
        sy += dir_dy[back];
        uint8_t reached = *search_cell(distance_map, sx, sy);
        if (!reached || reached > limit || creature_square_blocks_line_to_target(sx, sy)) {
            sx = px;
            sy = py;
            break;
        }
    }
    int r = creature_ai_try_attack(ranged ? 3 : 2, sx, sy, tx, ty, (back + 2) & 3);
    if (!r) return 0;
    creature_ai_cache_square(sx, sy);
    *out_location = pack_location(tx, ty, party.cur_map);
    return 1;
}

/* The champion that holds an item of filter 0x0b in a hand in the cells next to (x, y): its cell, -1 none. */
static int steal_target_cell(int x, int y)
{
    int cell = -1;
    for (int16_t i = 0; i < 4; i++) {
        int c = champ_get_nearest_at(x, y, i);
        if (c == -1) continue;
        uint16_t action = champion(c)->slots[SLOT_ACTION_HAND], ready = champion(c)->slots[SLOT_READY_HAND];
        if (!((action != THING_NONE && creature_thing_matches_filter(action, 0x0b))
              || (ready != THING_NONE && creature_thing_matches_filter(ready, 0x0b))))
            continue;
        if ((int16_t)cell < 0 || rand_bit()) cell = champion(c)->cell;
    }
    return cell;
}

static int square_has_explosion(uint16_t first, int kind)
{
    for (uint16_t t = first; t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        if (thing_type(t) == THING_EXPLOSION && explosion_type(t) == kind) return 1;
    return 0;
}

/* Tests (and with AI_EXECUTE starts) an attack from (x, y) on (target_x, target_y) (-1: the square in dir).
   mode 0, 1: the party; 2: a group; 3: anything. */
int creature_ai_try_attack(int mode, int x, int y, int target_x, int target_y, int dir)
{
    int execute = mode & AI_EXECUTE;
    mode &= AI_MODE_MASK;
    uint16_t flags = ai.attack_flags;
    int16_t tx = (int16_t)target_x, ty = (int16_t)target_y;
    int cell = -1;
    if (!flags) goto fail;
    if (tx == -1) {
        tx = (int16_t)(dir_dx[(int16_t)dir] + x);
        ty = (int16_t)(y + dir_dy[(int16_t)dir]);
    }
    if (!on_map(tx, ty) || ((int16_t)x != tx && (int16_t)y != ty)) goto fail;
    int16_t distance = (int16_t)square_distance(x, y, tx, ty);
    if (distance > 1 && !(flags &= ATTACK_RANGED_MASK)) goto fail;
    if (distance == 0 && !(flags &= ATTACK_MELEE_MASK)) goto fail;
    if (distance > info_attack_range(ai.info)) goto fail;
    if (distance == 0) {
        int16_t d = 0;
        while (d < 4 && !creature_ai_try_move(0, x, y, dir_dx[d] + x, (int16_t)(y + dir_dy[d]), d)) d++;
        if (d < 4) goto fail;
    }
    if (mode <= 1) {
        if (!at_party(tx, ty)) goto fail;
    } else if (mode == 2) {
        creature_ai_cache_square(tx, ty);
        if (creature_ai_cached_first_group() == THING_ENDOFLIST) goto fail;
    }
    if (distance > 1 && !(int16_t)creature_trace_line(x, y, tx, ty, creature_square_blocks_line)) goto fail;
    int steal_cell = -1;
    if (flags & ATTACK_STEAL) {
        steal_cell = steal_target_cell(x, y);
        if ((int16_t)steal_cell < 0 && !(flags &= (uint16_t)~ATTACK_STEAL)) goto fail;
    }
    if (class_flags() & AI_CLASS_AVOIDS_CLOUDS) {
        creature_ai_cache_square(tx, ty);
        if (square_has_explosion(creature_ai_cached_first_object(), EXPLOSION_LASTING_CLOUD) && !(flags &= ATTACK_MELEE_MASK))
            goto fail;
    }
    if (ai_party.map == party.cur_map && class_flags() & AI_CLASS_AVOIDS_PARTY_DOORS
        && !(ai.info->flags & CREATURE_NOT_SEEN_THROUGH)
        && !((uint16_t)x == group_x() && (uint16_t)y == group_y() && party.cur_map == ai.map)) {
        if (square_type(dun_get_square(x, y)) == SQUARE_DOOR && square_distance(x, y, ai_party.x, ai_party.y) < 2
            && rand_2bits())
            goto fail;
    }
    if (!execute) return 1;
    if ((int16_t)dir == -1)
        dir = distance == 0 && at_party(tx, ty) ? (game.party_dir + 2) & 3 : dun_get_direction_to(x, y, tx, ty);
    if (creature_ai_turn_toward(dir)) return 1;
    int r = rand_bit();
    if (distance <= 1 && flags & ATTACK_MELEE_MASK && (!(flags & ATTACK_RANGED_MASK) || rand_bit())) {
        flags &= ATTACK_MELEE_MASK;
        int c = mode > 1 ? -1 : champ_get_nearest_at(x, y, 0xff);
        cell = c == -1 ? (r + 2 + dir) & 3 : champion(c)->cell;
    } else {
        flags &= ATTACK_RANGED_MASK;
        if (mode <= 1 && rand_2bits()) {
            r = rand_bit();
            int c = r ? (dir + 2) & 3 : dir;
            if (champ_get_index_in_cell(c) == -1 && champ_get_index_in_cell((c + 3) & 3) == -1) r = 1 - r;
        }
        cell = (dir + r) & 3;
    }
    /* the original reads the bits as 32-bit numbers: no set bit gives 0 */
    int bits = 0;
    for (uint32_t v = flags; v; v >>= 1) bits += v & 1;
    int nth = rand_below((uint16_t)bits) + 1;
    uint16_t attack = 0;
    for (int b = 0; b < 32; b++)
        if (flags >> b & 1 && !--nth) {
            attack = (uint16_t)(1u << b);
            break;
        }
    static const struct { uint16_t attack; uint8_t sub; } shots[] = {
        {0x10, 0x80}, {0x20, 0x83}, {0x40, 0x82}, {0x80, 0x87}, {0x100, 0x86}, {0x200, 0x81}, {0x400, 0x89}, {0x800, 0x8a}
    };
    if (attack == 1) ai.active->action = ACTION_MELEE;
    else if (attack == 2) ai.active->action = ACTION_MELEE_2;
    else if (attack == ATTACK_STEAL) {
        ai.active->action = ACTION_STEAL;
        ai.active->sub = SHOT_STEAL;
    } else if (attack == 8) {
        ai.active->action = (uint8_t)(ACTION_THROW + (r != 0));
    } else {
        for (int i = 0; i < (int)(sizeof shots / sizeof shots[0]); i++)
            if (shots[i].attack == attack) {
                ai.active->sub = (int8_t)shots[i].sub;
                ai.active->action = (uint8_t)(ACTION_SHOOT + (r != 0));
            }
    }
    set_action_destination(tx, ty);
    ai.active->direction = (uint8_t)dir;
    if (ai.active->action == ACTION_STEAL) cell = steal_cell;
    ai.active->cell = (uint8_t)cell;
    ai.active->mode = (uint8_t)mode;
    ai.command_result = AI_RESULT_ACTING;
    return 1;
fail:
    if (execute) ai.command_result = AI_RESULT_FAILED;
    return 0;
}

/* A door spell flying toward the door or an explosion that opens it between (x, y) and the door. */
static int door_spell_on_line(int x, int y, int distance, int dir)
{
    for (int16_t n = (int16_t)distance; n >= 0; n--) {
        for (uint16_t t = first_object(x, y); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
            int type = thing_type(t);
            if (type == THING_PROJECTILE) {
                uint16_t payload = thing_word(t, PROJECTILE_PAYLOAD);
                if ((payload == EXPLOSION_THING_BASE + EXPLOSION_DOOR_SPELL || payload == EXPLOSION_THING_BASE + EXPLOSION_OPEN_DOOR)
                    && projectile_dir(t) == (int16_t)dir)
                    return 1;
            } else if (type == THING_EXPLOSION) {
                int kind = explosion_type(t);
                if (kind == EXPLOSION_DOOR_SPELL || kind == EXPLOSION_OPEN_DOOR) return 1;
            }
        }
        x = (int16_t)(x + dir_dx[(int16_t)dir]);
        y = (int16_t)(y + dir_dy[(int16_t)dir]);
    }
    return 0;
}

/* Tests (and with AI_EXECUTE starts) an action on the door at (door_x, door_y) (-1: the square in dir):
   mode 0 opens it, 1 closes it, 2 closes a marked door. */
int creature_ai_try_door_action(int mode, int x, int y, int door_x, int door_y, int dir)
{
    int execute = mode & AI_EXECUTE;
    mode &= AI_MODE_MASK;
    uint16_t flags = (uint16_t)((mode == 0 ? DOOR_SKILLS_OPEN : DOOR_SKILLS_CLOSE) & ai.door_flags);
    int16_t tx = (int16_t)door_x, ty = (int16_t)door_y, d = (int16_t)dir;
    int state = 0;  /* 1: the door is as wanted; 2: a spell is on the way */
    int16_t distance = 0;
    uint8_t *door = NULL;
    if (!flags) goto fail;
    if (tx == -1) {
        tx = (int16_t)(x + dir_dx[d]);
        ty = (int16_t)(y + dir_dy[d]);
    } else if (d == -1) {
        d = (int16_t)dun_get_direction_to(x, y, tx, ty);
    } else if (tx == (int16_t)x && (int16_t)y == ty) {
        goto fail;
    }
    if (!on_map(tx, ty) || ((int16_t)x != tx && (int16_t)y != ty)) goto fail;
    creature_ai_cache_square(tx, ty);
    if (square_type(ai_square.square) != SQUARE_DOOR) goto fail;
    int door_state = ai_square.square & SQUARE_DOOR_STATE;
    if (mode == 0 ? door_state == DOOR_STATE_OPEN : door_state == DOOR_STATE_CLOSED) {
        state = 1;
    } else if (mode != 0 && door_state == DOOR_STATE_BROKEN) {
        goto fail;
    } else {
        door = dungeon_thing_record(cached_first_thing());
        uint16_t bits = (uint16_t)(door[2] | door[3] << 8);
        if (mode == 2 && !(bits & DOOR_MARKED)) goto fail;
        if (bits & DOOR_LOCKED) {
            if (bits & DOOR_LOCK_OPEN ? mode == 0 : mode != 0) {
                state = 2;
                goto decided;
            }
            if (!(bits & DOOR_LOCK_OPEN) && !(flags &= 0xfff3)) goto fail;
        }
        distance = (int16_t)square_distance(x, y, tx, ty);
        int button = (bits & DOOR_BUTTON) || (bits & DOOR_OPENER);
        if (distance == 1) {
            if (!((flags & DOOR_SKILL_BUTTON && button)
                  || (mode == 0 && ((flags & DOOR_SKILL_BASH && bits & DOOR_BREAKABLE) || (flags & DOOR_SKILL_BURN && bits & DOOR_BURNABLE)))
                  || flags & DOOR_SKILL_HANDLE))
                goto fail;
        } else {
            if (distance > info_attack_range(ai.info)) goto fail;
            if (!((flags & DOOR_SKILL_SHOOT_BUTTON && button) || (mode == 0 && flags & DOOR_SKILL_BASH && bits & DOOR_BREAKABLE)))
                goto fail;
            if (!(int16_t)creature_trace_line(x, y, tx, ty, creature_square_blocks_line)) goto fail;
        }
        if (flags & DOOR_SKILL_SHOOT_BUTTON && door_spell_on_line(x, y, distance, d)) state = 2;
    }
decided:
    if (state && mode == 2) goto fail;
    if (!execute) return 1;
    if (d == -1) d = (int16_t)dun_get_direction_to(x, y, tx, ty);
    if (state == 1) {
        ai.command_result = AI_RESULT_DONE;
        return 1;
    }
    if (state == 2) {
        ai.active->action = ACTION_NONE;
        ai.command_result = AI_RESULT_ACTING;
        return 1;
    }
    if (creature_ai_turn_toward(d)) return 1;
    if (flags & DOOR_SKILL_MARK) {
        flags &= (uint16_t)~DOOR_SKILL_MARK;
        if (mode == 0) door[3] |= DOOR_MARKED >> 8;
    }
    if (distance > 1) flags &= 5;
    if (flags & (DOOR_SKILL_HANDLE | 2) && (!(flags & 0xffbd) || rand_2bits())) {
        ai.active->action = ACTION_BASH_DOOR;
    } else if (flags & DOOR_SKILL_SHOOT_BUTTON && (!(flags & 0xffbc) || !rand_2bits())) {
        ai.active->action = (uint8_t)(ACTION_SHOOT + (rand_bit() != 0));
        ai.active->sub = (int8_t)(mode == 0 ? 0x8d : 0x84);
    } else {
        uint16_t saved = ai.attack_flags;
        if (flags & DOOR_SKILL_BURN && saved & 1 && (!(flags & 0xffb4) || rand_bit())) ai.attack_flags &= 1;
        else if (!(flags & DOOR_SKILL_BASH)) goto fail;
        else ai.attack_flags &= 0x50;
        int r = creature_ai_try_attack(AI_EXECUTE | 4, x, y, tx, ty, d);
        ai.attack_flags = saved;
        return r;
    }
    set_action_destination(tx, ty);
    ai.active->facing = (uint8_t)d;
    ai.active->direction = (uint8_t)d;
    ai.active->cell = (uint8_t)((rand_bit() + d) & 3);
    ai.active->mode = (uint8_t)mode;
    ai.command_result = AI_RESULT_ACTING;
    return 1;
fail:
    if (execute) ai.command_result = AI_RESULT_FAILED;
    return 0;
}

/* True when a dangerous projectile flies toward (x, y) within 3 squares in a direction that the creature sees. */
int creature_ai_projectile_incoming(int x, int y)
{
    for (int16_t d = 0; d < 4; d++) {
        if (!(class_flags() & AI_CLASS_SEES_BEHIND)) {
            int asleep = ai.group[GROUP_FLAGS] & 0x80;
            int behind = !(ai.info->flags & CREATURE_FACES_PARTY) && at_group(x, y) && d == ((group_dir() + 2) & 3);
            if ((asleep || behind) && rand_next() & 7) continue;
        }
        int16_t sx = (int16_t)x, sy = (int16_t)y;
        for (int16_t n = 0; n < 3; n++) {
            sx += dir_dx[d];
            sy += dir_dy[d];
            if (!on_map(sx, sy)) break;
            for (uint16_t t = first_object(sx, sy); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
                if (thing_type(t) == THING_PROJECTILE && projectile_dir(t) == ((d + 2) & 3)
                    && projectile_get_attack(t, thing_word(t, PROJECTILE_PAYLOAD)))
                    return 1;
            if (!at_group(sx, sy) && creature_square_blocks_line(sx, sy)) break;
        }
    }
    return 0;
}

/* The search cost of a door square (ai_square.cost) by its state; returns 1. */
static int door_search_cost(int x, int y)
{
    int state = ai_square.square & SQUARE_DOOR_STATE;
    if (state == DOOR_STATE_OPEN || state == DOOR_STATE_BROKEN || state == DOOR_STATE_ONE_FOURTH) return 1;
    int16_t cost = (int16_t)door_pass_cost(x, y);
    ai_square.cost = cost;
    if (state == DOOR_STATE_CLOSED || cost <= 1) return 1;
    int16_t half = (int16_t)(cost >> 1);
    if (state == DOOR_STATE_HALF) ai_square.cost = cost <= 3 ? 1 : (int16_t)(half - (half >> 1));
    else ai_square.cost = cost <= 3 ? (int16_t)(cost - 1) : half;
    return 1;
}

/* The quick test of the cost search (move kind 8): may the search enter the square, at what cost. */
static int search_square_test(int command, int x, int y)
{
    creature_ai_cache_square(x, y);
    uint8_t square = ai_square.square;
    switch (square_type(square)) {
    case SQUARE_FAKE_WALL:
        if (square & 5) return 1;
        /* fall through: a closed fake wall costs like a wall */
    case SQUARE_WALL: {
        int cost = gdat_entry_index(draw_gdat(), CAT_WALLSET, dungeon_map_graphics(game.party_map) >> 4 & 0xf,
                                    GDAT_TYPE_VALUE, WALLSET_KEY_WALL_COST);
        if (!cost) cost = 1;
        if (cost >= WALL_COST_BLOCKING) {
            ai_square_scan.blocking = 1;
            return 0;
        }
        ai_square.cost = (int16_t)cost;
        return 1;
    }
    case SQUARE_FLOOR:
    case SQUARE_PIT: return 1;
    case SQUARE_DOOR: return door_search_cost(x, y);
    case SQUARE_TELEPORTER:
        if (square & SQUARE_TELEPORTER_ON) {
            uint16_t link = THING_NONE;
            /* an open teleporter next to a seamless passage: test the square behind it */
            PassageEnd end;
            if (!dun_get_linked_teleporter_dest(&end, x + 1, y) && !dun_get_linked_teleporter_dest(&end, x - 1, y)
                && !dun_get_linked_teleporter_dest(&end, x, y + 1) && !dun_get_linked_teleporter_dest(&end, x, y - 1))
                return 1;
            link = first_thing(x, y);
            uint16_t target = thing_word(link, TELEPORTER_TARGET);
            if (end.map != thing_word(link, TELEPORTER_MAP) >> 8
                || square_distance(end.x, end.y, target & 0x1f, target >> 5 & 0x1f) != 1)
                return 1;
            int map = thing_word(link, TELEPORTER_MAP) >> 8;
            if (map != game.party_map && map != party.alt_view_map) return 1;
            int saved = party.cur_map;
            creature_ai_set_map(map);
            int r = creature_ai_try_move(command, target & 0x1f, target >> 5 & 0x1f, target & 0x1f, target >> 5 & 0x1f, 6);
            creature_ai_set_map(saved);
            return r;
        }
        ai_square_scan.passage = (uint8_t)dun_get_linked_teleporter_dest(&ai_square_scan.end, x, y);
        if (!ai_square_scan.passage) return 1;
        return ai_square_scan.end.map == game.party_map || ai_square_scan.end.map == party.alt_view_map;
    default: return 0;
    }
}


#define DOOR_LOW 0x0020                 /* door word +2: the door blocks only creatures taller than its state */

/* The move flags that the square in the AI cache needs; 0: the creature cannot enter it. */
static uint16_t square_move_flags(int kind, int no_movers, int map_changed, int x, int y)
{
    uint8_t square = ai_square.square;
    switch (square_type(square)) {
    case SQUARE_WALL: return MOVE_WALL;
    case SQUARE_FLOOR: return MOVE_FLOOR;
    case SQUARE_PIT:
        if (!(square & SQUARE_PIT_OPEN)) return MOVE_CLOSED_PIT | MOVE_FLOOR;
        if (square & 1) return MOVE_FALLS | MOVE_INVISIBLE_PIT | MOVE_CLOSED_PIT;
        return map_changed ? MOVE_OPEN_PIT | MOVE_CLOSED_PIT : MOVE_FALLS | MOVE_OPEN_PIT | MOVE_CLOSED_PIT;
    case SQUARE_STAIRS: return MOVE_STAIRS;
    case SQUARE_DOOR: {
        int state = square & SQUARE_DOOR_STATE;
        if ((state == DOOR_STATE_OPEN && kind != 4 && kind != 5) || state == DOOR_STATE_BROKEN) return MOVE_FLOOR;
        if (state == DOOR_STATE_CLOSED) return MOVE_DOOR | MOVE_LOCKED_DOOR;
        uint16_t bits = thing_word(cached_first_thing(), DOOR_DATA);
        if (no_movers && bits & DOOR_LOCKED) return bits & DOOR_LOCK_OPEN ? MOVE_FLOOR : MOVE_DOOR | MOVE_LOCKED_DOOR;
        if (bits & DOOR_LOCKED && !(bits & DOOR_LOCK_OPEN)) return MOVE_LOCKED_DOOR;
        int limit = bits & DOOR_LOW ? ai.info->flags >> DOOR_HEIGHT_PASSABLE_SHIFT & 3 : 1;
        return state > limit ? MOVE_DOOR | MOVE_LOCKED_DOOR : MOVE_FLOOR;
    }
    case SQUARE_TELEPORTER:
        if (!(square & SQUARE_TELEPORTER_ON)) {
            if (!dun_get_linked_teleporter_dest(&ai_square_scan.end, x, y)) return MOVE_TELEPORTER | MOVE_FLOOR;
            ai_square_scan.passage = 1;
            return creature_is_native_to_map(ai.group_thing, ai_square_scan.end.map) ? MOVE_LEVELS : 0;
        } else {
            int target_kind = thing_word(cached_first_thing(), TELEPORTER_TARGET) >> TELEPORTER_TARGET_KIND_SHIFT & 3;
            return target_kind == 1 || target_kind == 3 ? MOVE_TELEPORTER : MOVE_FLOOR;
        }
    case SQUARE_FAKE_WALL:
        if (square & SQUARE_FAKE_WALL_OPEN) return MOVE_FLOOR;
        return square & 1 ? MOVE_FAKE_WALL : MOVE_HIDDEN_FAKE_WALL;
    default: return 0;
    }
}

/* The level change of a move up (dir 4) or down (dir 5) from (x, y): sets the action, the facing, and the target
   square on the other map; returns the map, -1 when the creature cannot go. */
static int level_change(int x, int y, int dir, int16_t *action, int16_t *facing, int16_t *tx, int16_t *ty)
{
    uint16_t need = 0, link = THING_NONE;
    uint8_t square = dun_get_square(x, y);
    int type = square_type(square);
    if (type == SQUARE_PIT && dir == 5 && square & SQUARE_PIT_OPEN && !(square & 1)) {
        need = MOVE_FALL;
        *action = ACTION_PIT;
        *facing = (int16_t)group_dir();
    } else if (type == SQUARE_FLOOR || type == SQUARE_PIT || type == SQUARE_TELEPORTER || type == SQUARE_FAKE_WALL) {
        if (square & 2) {
            int16_t side = (int16_t)dun_find_adjacent_level_link(x, y, dir == 4 ? -1 : 1, &link);
            *facing = side;
            if (side >= 0) {
                *action = (int16_t)(ACTION_LINK_UP + (dir != 4));
                need = MOVE_STAIRS;
            } else {
                *action = ACTION_FALL_IN_PIT;
                need = MOVE_FALL;
                *facing = (int16_t)group_dir();
            }
        }
    } else if (type == SQUARE_STAIRS && (square & SQUARE_STAIRS_UP ? dir == 4 : dir == 5)) {
        *action = square & SQUARE_STAIRS_UP ? ACTION_STAIRS_UP : ACTION_STAIRS_DOWN;
        *facing = (int16_t)((dun_get_stairs_exit_direction(x, y) + 2) & 3);
        need = MOVE_STAIRS;
    }
    if (!(ai.move_flags & need)) return -1;
    int map, nx = (int16_t)x, ny = (int16_t)y;
    if (link != THING_NONE && (*action == ACTION_LINK_UP || *action == ACTION_LINK_UP + 1)) {
        uint16_t target = thing_word(link, 6);
        nx = target >> 6 & 0x1f;
        ny = target >> 11;
        map = thing_word(link, SENSOR_TYPE_DATA) >> SENSOR_DATA_SHIFT;
    } else {
        map = dungeon_location_on_map(party.cur_map, dir == 4 ? -1 : 1, &nx, &ny);
        if ((int16_t)map < 0) return -1;
        if (*action == ACTION_FALL_IN_PIT) {
            /* fix: the original reads the square without a bounds test; a square at the map edge is not a pit */
            if (nx < 0 || nx >= dungeon_map_width(map) || ny < 0 || ny >= dungeon_map_height(map)) return -1;
            uint8_t below = dungeon_square(map, nx, ny);
            if (square_type(below) != SQUARE_PIT || !(below & SQUARE_PIT_OPEN)) return -1;
        }
    }
    if (!creature_is_native_to_map(ai.group_thing, map)) return -1;
    *tx = (int16_t)nx;
    *ty = (int16_t)ny;
    return map;
}

/* Tests (and with AI_EXECUTE starts) a move of the group from (x, y) to (dest_x, dest_y) (-1: the square in dir).
   dir 4 and 5: a level change up and down; dir 6: a move to the given square that keeps the facing. Kinds 3, 7, and
   8 are the quick tests of the search; kinds 4 and 5 avoid dangers, kind 6 is a step back, kind 1 keeps a distance
   to the party. */
int creature_ai_try_move(int command, int x, int y, int dest_x, int dest_y, int dir)
{
    int execute = command & AI_EXECUTE, no_movers = command & AI_MOVE_NO_MOVERS, attack_groups = command & AI_MOVE_ATTACK_GROUPS;
    int kind = command & AI_MOVE_KIND_MASK;
    int16_t sx = (int16_t)x, sy = (int16_t)y, tx = (int16_t)dest_x, ty = (int16_t)dest_y, d = (int16_t)dir;
    int16_t action = -1, facing = d;
    int result = 0, map_changed = 0, attack_group = 0, same = 0, saved_map = party.cur_map;
    if (kind == 2) goto finish;
    int enabled = command_enabled(kind);
    if (!enabled && !ai.move_flags) goto finish;
    if (d == 6) {
        facing = (int16_t)group_dir();
    } else if (d >= 4) {
        if (!(ai.move_flags & MOVE_LEVELS)) goto finish;
        int map = level_change(sx, sy, d, &action, &facing, &tx, &ty);
        if (map < 0) goto finish;
        map_changed = 1;
        creature_ai_set_map(map);
        sx = sy = -1;
    } else if (tx == -1) {
        tx = (int16_t)(sx + dir_dx[d]);
        ty = (int16_t)(sy + dir_dy[d]);
    } else {
        same = tx == sx && sy == ty;
        if (!same && d == -1) facing = d = (int16_t)dun_get_direction_to(sx, sy, tx, ty);
    }
    if (!on_map(tx, ty)) goto finish;
    if (enabled && (kind == 3 || kind == 7 || kind == 8)) {
        if (kind == 3) {
            creature_ai_cache_square(tx, ty);
            uint8_t square = ai_square.square;
            result = square_type(square) != SQUARE_WALL && !(square_type(square) == SQUARE_FAKE_WALL && !(square & 5));
        } else if (kind == 7) {
            result = !creature_square_is_obstacle(tx, ty);
            if (result) creature_ai_cache_square(tx, ty);
        } else {
            result = search_square_test(kind, tx, ty);
        }
        goto done;
    }
    creature_ai_cache_square(tx, ty);
    if (ai.attr7 && d != 6) {
        int16_t sides = (int16_t)creature_find_ai_marker(1, (uint16_t)ai.attr7);
        if (sides != -1 && (1 << ((d + 2) & 3)) & sides) goto finish;
    }
    if ((kind == 4 || kind == 5) && ai.info->defense == CREATURE_NO_HURT) kind = 0;
    uint16_t need = square_move_flags(kind, no_movers, map_changed, tx, ty) & ai.move_flags;
    if (!need) goto finish;
    if (kind == 1 && abs16(tx - ai_party.x) + abs16(ty - ai_party.y) <= 1) goto finish;
    uint16_t also = at_party(tx, ty) ? MOVE_PARTY : 0;
    if (!same) {
        uint16_t g = creature_ai_cached_first_group();
        if (g != THING_ENDOFLIST && !(no_movers && creature_group_info(g)->movement)
            && (!attack_groups || map_changed || !(attack_group = creature_thing_matches_filter(g, 9))))
            also |= MOVE_GROUPS;
    }
    if ((ai.move_flags & also) != also) goto finish;
    if (kind == 4 || kind == 5) {
        int cloud = 0;
        for (uint16_t t = creature_ai_cached_first_object(); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
            int type = thing_type(t);
            if (type == THING_EXPLOSION) {
                int k = explosion_type(t);
                if (k == EXPLOSION_POISON_CLOUD) {
                    if (explosion_get_attack(t, ai.group_thing)) goto finish;
                } else if (k == EXPLOSION_LASTING_CLOUD) {
                    cloud = 1;
                }
            } else if (type == THING_PROJECTILE && !at_group(tx, ty)) {
                if (projectile_dir(t) != (d == 6 ? facing : d) && projectile_get_attack(t, thing_word(t, PROJECTILE_PAYLOAD)))
                    goto finish;
            }
        }
        if (kind == 5 && !cloud && creature_ai_projectile_incoming(tx, ty)) goto finish;
    }
    if (!same && ai_party.map == party.cur_map && class_flags() & AI_CLASS_AVOIDS_PARTY_DOORS
        && !(ai.info->flags & CREATURE_NOT_SEEN_THROUGH) && square_type(ai_square.square) == SQUARE_DOOR
        && square_distance(tx, ty, ai_party.x, ai_party.y) < 2
        && ((uint16_t)sx != group_x() || (uint16_t)sy != group_y() || rand_2bits()))
        goto finish;
    result = 1;
    if (map_changed || need == MOVE_TELEPORTER) {
        move_follow_square_transitions(ai.group_thing, sx, sy, tx, ty, 0);
        if (party.cur_map == party.dest_map && tx == party.dest_x && ty == party.dest_y) goto finish;
        int saved = party.cur_map;
        if (saved != party.dest_map) {
            if (!(ai.move_flags & MOVE_LEVELS) || !creature_is_native_to_map(ai.group_thing, (uint16_t)party.dest_map)) {
                result = 0;
                goto finish;
            }
            creature_ai_set_map((uint16_t)party.dest_map);
        }
        result = creature_ai_try_move(kind | no_movers, (uint16_t)party.dest_x, (uint16_t)party.dest_y,
                                      (uint16_t)party.dest_x, (uint16_t)party.dest_y, 6);
        creature_ai_set_map(saved);
        goto finish;
    }
    if (same) goto finish;
    if (need == MOVE_DOOR) return creature_ai_try_door_action(execute, sx, sy, tx, ty, facing);
    if (need & MOVE_FALLS) {
        if (kind == 6) {
            result = 0;
        } else {
            uint16_t saved = ai.move_flags;
            ai.move_flags &= MOVE_FLAGS_NO_FALL;
            result = creature_ai_try_move(kind | no_movers, tx, ty, (int16_t)(tx + dir_dx[facing]), (int16_t)(ty + dir_dy[facing]), facing);
            ai.move_flags = saved;
        }
        if (result) {
            action = ACTION_FALL;
        } else if (ai.move_flags & MOVE_OPEN_PIT) {
            uint16_t saved = ai.move_flags;
            ai.move_flags &= (uint16_t)~MOVE_FALLS;
            result = creature_ai_try_move(kind | no_movers, sx, sy, tx, ty, facing);
            if (result) action = ACTION_PIT;
            ai.move_flags = saved;
        }
        if (!result) ai_square_scan.blocking = 1;
    }
finish:
    if (!execute) goto done;
    if (!result) {
        ai.command_result = AI_RESULT_FAILED;
        goto done;
    }
    if (same) {
        ai.command_result = AI_RESULT_DONE;
        goto done;
    }
    if (attack_group && creature_ai_try_attack(AI_EXECUTE | 2, sx, sy, tx, ty, -1)) goto done;
    if (ai.door_flags & DOOR_SKILL_ANY_SIDE)
        for (int16_t i = 0; i <= 3; i++) {
            if (i == facing) continue;
            int16_t nx = (int16_t)(sx + dir_dx[i]), ny = (int16_t)(sy + dir_dy[i]);
            if (square_type(dun_get_square(nx, ny)) == SQUARE_DOOR && creature_ai_try_door_action(AI_EXECUTE | 2, sx, sy, nx, ny, i))
                goto done;
        }
    if (kind == 6) {
        facing = (int16_t)((facing + 2) & 3);
        action = ACTION_TURN_BACK;
    } else if (action == -1) {
        action = abs16(sx - tx) + abs16(sy - ty) <= 1 ? ACTION_MOVE_NEAR : ACTION_MOVE;
    }
    int current = group_dir();
    if (current != facing && (current == ((facing + 2) & 3) || action != ACTION_MOVE_NEAR)) {
        if ((kind == 4 || kind == 5) && !(class_flags() & AI_CLASS_SEES_ALL_SIDES) && rand_2bits()) {
            action = ACTION_TURN_BACK;
        } else {
            creature_ai_turn_toward(facing);
            goto done;
        }
    } else if (current != facing && action != ACTION_TURN_BACK) {
        action = ((current + 1) & 3) == facing ? ACTION_STEP_LEFT : ACTION_STEP_RIGHT;
    } else if (ai_action_flag((int8_t)ai.previous_action) & AI_ACTION_FLAG_MOVES && kind != 6 && !map_changed) {
        action = ACTION_NONE;
    }
    set_action_destination(tx, ty);
    ai.active->direction = (uint8_t)d;
    ai.active->facing = (uint8_t)facing;
    ai.active->mode = (uint8_t)kind;
    ai.active->action = (uint8_t)action;
    ai.command_result = AI_RESULT_ACTING;
done:
    if (map_changed) creature_ai_set_map(saved_map);
    return result;
}

/* ---- Items ---- */

/* The cells of the possessions of a static creature seen from side. */
static int possession_cell_mask(uint16_t possessions, int side)
{
    if (!(possessions & STATIC_POSSESSIONS_SHOWN) || possessions & STATIC_POSSESSIONS_ALL) return 0x0f;
    /* fix: for side -1 (the own square) the original reads 64 KB past the table; the port reaches all cells */
    if ((uint16_t)side > 3) return 0x0f;
    return possession_cells[possessions & 0x0f][side];
}

/* An item of filter inside a chest or in the possessions of a group that the creature may take from: the chest
   cell (0 to 3), 8 plus the cell of the possession, -1 none. */
int creature_find_matching_item_inside(uint16_t thing, int filter, int side)
{
    int16_t found_cell;
    uint16_t list;
    int mask = 0x0f;
    if (ai.item_flags & ITEM_SKILL_CHESTS && item_is_chest(thing)) {
        found_cell = (int16_t)(thing >> THING_CELL_SHIFT);
        list = thing_word(thing, CONTAINER_CONTENTS);
    } else {
        if (thing_type(thing) != THING_GROUP || !(ai.item_flags & (ITEM_SKILL_STATIC | ITEM_SKILL_GROUPS))) return -1;
        const CreatureInfo *info = creature_group_info(thing);
        int is_static = info->flags & CREATURE_STATIC;
        if (!(ai.item_flags & (is_static ? ITEM_SKILL_STATIC : ITEM_SKILL_GROUPS))) return -1;
        found_cell = 8;
        list = thing_word(thing, GROUP_POSSESSIONS);
        if (is_static) mask = possession_cell_mask(info->possessions, side);
    }
    for (uint16_t t = list; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (!(mask >> (t >> THING_CELL_SHIFT) & 1)) continue;
        if (creature_thing_matches_filter(t, (int16_t)filter) || (int16_t)creature_find_matching_item_inside(t, filter, side) != -1) {
            if (found_cell == 8) found_cell = (int16_t)(8 + (t >> THING_CELL_SHIFT));
            return found_cell;
        }
    }
    return -1;
}

/* Reads which wall faces of the square in the AI cache show an alcove: the face toward the creature (side). */
static void scan_wall_ornaments(int side)
{
    ai_square_scan.cells[0] = (int8_t)side;
    uint16_t first = cached_first_thing(), t = first;
    for (; t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR; t = dungeon_next_thing(t)) {
        if (!dun_is_wall_thing_ornament_shown(first, t)) continue;
        if (t >> THING_CELL_SHIFT == ai_square_scan.cells[0]) ai_square_scan.facing_ornament = 1;
        else ai_square_scan.items_elsewhere = 1;
    }
    ai_square.first_object = t;
}

/* Tests (and with AI_EXECUTE starts) taking (mode 0) or giving (mode 1) an item of filter at (dest_x, dest_y)
   (-1: the square in dir): from the floor, an alcove, a chest, or a group. */
int creature_ai_try_take_item(int mode, int x, int y, int dest_x, int dest_y, int dir, int filter)
{
    int execute = mode & AI_EXECUTE;
    mode &= AI_MODE_MASK;
    int16_t tx = (int16_t)dest_x, ty = (int16_t)dest_y, d = (int16_t)dir, action = -1, cell = 0;
    int same = 0;
    if (!ai.item_flags) goto fail;
    if (tx == -1) {
        tx = (int16_t)(dir_dx[d] + x);
        ty = (int16_t)(dir_dy[d] + y);
    } else {
        same = (int16_t)x == tx && (int16_t)y == ty;
        if (!same && d == -1) d = (int16_t)dun_get_direction_to(x, y, tx, ty);
    }
    creature_ai_cache_square(tx, ty);
    if (mode == 0 && !(ai_square.square & 0x10)) goto fail;
    if (square_type(ai_square.square) == SQUARE_WALL) {
        if (ai_square_scan.items_elsewhere == -1) {
            ai_square_scan.items_elsewhere = 0;
            ai_square_scan.facing_ornament = 0;
            if (!same && ai.item_flags & ITEM_SKILL_WALL) scan_wall_ornaments((d + 2) & 3);
        }
        if (!ai_square_scan.facing_ornament) goto fail;
        if (mode == 1) {
            action = ACTION_TAKE_FROM_WALL;
        } else {
            action = ACTION_PUT_IN_WALL;
            cell = ai_square_scan.cells[0];
            uint16_t t = ai_square.first_object;
            for (; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
                int type = thing_type(t);
                if ((uint16_t)(t >> THING_CELL_SHIFT) != (uint16_t)cell || type == THING_GROUP || type >= THING_PROJECTILE) continue;
                if (creature_thing_matches_filter(t, (int16_t)filter) || (int16_t)creature_find_matching_item_inside(t, filter, d) != -1)
                    break;
            }
            if (t == THING_ENDOFLIST) goto fail;
        }
    } else {
        if (ai_square_scan.items_elsewhere == -1) {
            ai_square_scan.items_elsewhere = 0;
            ai_square_scan.cells[0] = -1;
        }
        if (!same && at_party(tx, ty)) goto fail;
        if (!(ai.item_flags & (same ? ITEM_SKILL_SAME_SQUARE : ITEM_SKILL_OTHER_SQUARE))) goto fail;
        if (mode == 1) {
            if (!same) action = ACTION_GIVE;
        } else {
            uint16_t t = creature_ai_cached_first_object();
            for (; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
                cell = (int16_t)(t >> THING_CELL_SHIFT);
                int type = thing_type(t);
                if (type < THING_GROUP || type >= THING_PROJECTILE) continue;
                if (!(type != THING_GROUP && creature_thing_matches_filter(t, (int16_t)filter))) {
                    cell = (int16_t)creature_find_matching_item_inside(t, filter, d);
                    if (cell == -1) continue;
                }
                if ((uint16_t)cell >= 8) {
                    action = ACTION_TAKE_FROM_GROUP;
                    cell &= 3;
                    break;
                }
                if ((uint8_t)ai_square_scan.cells[0] == 0xff) {
                    /* the cells in reach: the near half of the square, or all of the own square */
                    int bits = same ? (d == -1 ? 0x0f : 3) : 0x0c, c = same && d == -1 ? 0 : d;
                    for (int i = 0; i < 4; i++) {
                        ai_square_scan.cells[c] = (int8_t)(bits & 1);
                        c = (c + 1) & 3;
                        bits >>= 1;
                    }
                }
                if (ai_square_scan.cells[(uint16_t)cell]) break;
                ai_square_scan.items_elsewhere = 1;
            }
            if (t == THING_ENDOFLIST) goto fail;
        }
    }
    if (!execute) return 1;
    if (d == -1) d = (int16_t)group_dir();
    if (!same && creature_ai_turn_toward(d)) return 1;
    ai.active->facing = (uint8_t)d;
    if (mode == 1) {
        if (action == ACTION_TAKE_FROM_WALL) {
            cell = d;
        } else if (action == ACTION_GIVE) {
            uint16_t g = creature_ai_cached_first_group();
            int bits = possession_cell_mask(creature_group_info(g)->possessions, d) & 0xff;
            if (!bits) goto fail;
            int count = 0;
            for (int b = bits; b; b >>= 1) count += b & 1;
            int nth = rand_below((uint16_t)count) + 1;
            int16_t bit = 0;
            for (int b = 0; b < 32; b++)
                if (bits >> b & 1 && !--nth) {
                    bit = (int16_t)(1 << b);
                    break;
                }
            for (cell = 0; !(bit & 1); bit >>= 1) cell++;
        } else {
            int r = rand_bit();
            if (!same) r += 2;
            cell = (int16_t)((d + r) & 3);
            action = take_actions[r];
        }
    } else if (action == -1) {
        int side = (cell - ai.active->facing + 1) & 3;
        if (!same) {
            action = side < 2 ? ACTION_TAKE_FRONT : ACTION_TAKE_SIDE;
        } else {
            action = (int16_t)(ACTION_TAKE_LEFT + (side >= 2));
            if (side == 0) ai.active->facing = (uint8_t)((ai.active->facing - 1) & 3);
            else if (side == 3) ai.active->facing = (uint8_t)((ai.active->facing + 1) & 3);
            if (class_flags() & AI_CLASS_TURNS_TO_TAKE && ai.active->facing != group_dir()) {
                creature_ai_turn_toward(ai.active->facing);
                return 1;
            }
        }
    }
    ai.active->destination = pack_location(ai_square.x, ai_square.y, ai_square.map);
    ai.active->cell = (uint8_t)cell;
    ai.active->sub = (int8_t)filter;
    ai.active->mode = (uint8_t)mode;
    ai.active->action = (uint8_t)action;
    ai.command_result = AI_RESULT_ACTING;
    return 1;
fail:
    if (execute) ai.command_result = AI_RESULT_FAILED;
    return 0;
}

/* The wall actuator in cell of a square thing list for mode: the first slot that takes filter, else the last
   actuator of the cell; THING_NONE for none. */
uint16_t creature_find_wall_actuator_in_cell(uint16_t first_thing, int cell, int mode, int filter)
{
    uint16_t found = THING_NONE;
    for (uint16_t t = first_thing; t != THING_ENDOFLIST && t != THING_NONE; t = dungeon_next_thing(t)) {
        int type = thing_type(t);
        if (type > THING_SENSOR) break;
        if (type != THING_SENSOR || t >> THING_CELL_SHIFT != (int16_t)cell) continue;
        int kind = thing_word(t, SENSOR_TYPE_DATA) & SENSOR_TYPE_MASK;
        if (kind == 0 || kind == SENSOR_CREATURE_SLOT) continue;
        found = t;
        if (kind != ACTUATOR_SLOT) continue;
        int wanted = thing_word(t, SENSOR_FLAGS) & SENSOR_FLAG_ACTIVE ? 2 : 1;
        if ((int16_t)mode == wanted && creature_thing_matches_filter(t, (int16_t)filter)) break;
    }
    return found;
}

/* True when the wall actuator reacts to the creature: mode 0 use, 1 put an item of filter, 2 take one;
   state: the state bits of the creature slot. */
int creature_wall_actuator_accepts(uint16_t actuator, int mode, int filter, int state)
{
    if (actuator == THING_ENDOFLIST || actuator == THING_NONE) return 0;
    int kind = thing_word(actuator, SENSOR_TYPE_DATA) & SENSOR_TYPE_MASK;
    mode = (int16_t)mode;
    switch (kind) {
    case ACTUATOR_SWITCH:
    case ACTUATOR_BUTTON:
    case ACTUATOR_BUTTON_2: return mode == 0;
    case ACTUATOR_ANY_ITEM: return mode == 0 && (int16_t)filter != -1;
    case ACTUATOR_ITEM: return mode == 0 && creature_thing_matches_filter(actuator, (int16_t)filter);
    case ACTUATOR_TOGGLE: return mode == 0 && (int16_t)state != !(thing_word(actuator, SENSOR_FLAGS) & SENSOR_FLAG_ACTIVE);
    case ACTUATOR_SLOT:
        if (thing_word(actuator, SENSOR_FLAGS) & SENSOR_FLAG_ACTIVE ? mode != 2 : mode != 1) return 0;
        return creature_thing_matches_filter(actuator, (int16_t)filter);
    case ACTUATOR_COUNTED_ITEM:
        if (mode != 1 || !(thing_word(actuator, SENSOR_TYPE_DATA) & ~SENSOR_TYPE_MASK)) return 0;
        return creature_thing_matches_filter(actuator, (int16_t)filter);
    default: return 0;
    }
}

/* Tests (and with AI_EXECUTE starts) the use of a wall actuator at (dest_x, dest_y) (-1: the square in dir) from
   the slot of the creature type on that wall face. */
int creature_ai_try_use_wall_actuator(int mode, int x, int y, int dest_x, int dest_y, int dir, int filter)
{
    int execute = mode & AI_EXECUTE;
    mode &= AI_MODE_MASK;
    int16_t tx = (int16_t)dest_x, ty = (int16_t)dest_y, d = (int16_t)dir;
    if (!(ai.actuator_flags & 1)) goto fail;
    if (tx == -1) {
        tx = (int16_t)(dir_dx[d] + x);
        ty = (int16_t)(dir_dy[d] + y);
    } else if ((int16_t)x == tx && (int16_t)y == ty) {
        goto fail;
    }
    if (!on_map(tx, ty) || ((int16_t)x != tx && (int16_t)y != ty)) goto fail;
    if (d == -1) d = (int16_t)dun_get_direction_to(x, y, tx, ty);
    creature_ai_cache_square(tx, ty);
    if (!(ai_square.square & 0x10)) goto fail;
    uint16_t slot = THING_NONE;
    for (uint16_t t = cached_first_thing(); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR; t = dungeon_next_thing(t)) {
        if (thing_type(t) == THING_SENSOR && t >> THING_CELL_SHIFT == ((d + 2) & 3)
            && (thing_word(t, SENSOR_TYPE_DATA) & SENSOR_TYPE_MASK) == SENSOR_CREATURE_SLOT) {
            slot = t;
            break;
        }
    }
    if (slot == THING_NONE || thing_word(slot, SENSOR_TYPE_DATA) >> SENSOR_DATA_SHIFT != ai.group[GROUP_CREATURE]
        || !(thing_word(slot, SENSOR_FLAGS) & SENSOR_FLAG_ACTIVE))
        goto fail;
    uint16_t actuator = creature_find_wall_actuator_in_cell(cached_first_thing(), (d + 2) & 3, mode, filter);
    if (actuator == THING_NONE) goto fail;
    uint16_t slot_flags = thing_word(slot, SENSOR_FLAGS);
    if (!creature_wall_actuator_accepts(actuator, mode, filter, (slot_flags & SENSOR_STATE_MASK) >> SENSOR_STATE_SHIFT)) goto fail;
    if (execute && !creature_ai_turn_toward(d)) {
        set_action_destination(tx, ty);
        ai.active->facing = (uint8_t)d;
        ai.active->direction = (uint8_t)d;
        ai.active->sub = (int8_t)filter;
        ai.active->mode = (uint8_t)mode;
        ai.active->action = (uint8_t)(ACTION_USE_ACTUATOR + (mode > 1 ? 2 : mode));
        ai.command_result = mode == 0 && (slot_flags & SENSOR_STATE_MASK) < SENSOR_STATE_BUSY ? AI_RESULT_ACTING : AI_RESULT_DONE;
    }
    return 1;
fail:
    if (execute) ai.command_result = AI_RESULT_FAILED;
    return 0;
}

#define FILTER_INVERT 0x80
#define FILTER_MASK 0x3f
#define FILTER_ANY 0x3f
#define FILTER_FULL_MONEYBOX 0x3e
#define FILTER_EMPTY_MONEYBOX 0x29
#define FILTER_EMPTY_MONEYBOX_ELSE 7    /* the filter of filter 0x29 for a thing that is not a money box */
#define FILTER_BY_VARIANT 0x10          /* filters 0x10 to 0x12: one set for each of three variants of the group */
#define FILTER_BY_VARIANT_END 0x13
#define FILTER_MONEYBOX_OR_VARIANT 0x28
#define FILTER_SLOTS 0x2a
#define FILTER_BITMAP_SIZE 0x40
#define FILTER_TEXT_ENTRY 0x10          /* GDAT text (creature type, entry filter + 0x10): the item classes */
#define CLASS_TEXT_SIZE 0x80
#define MONEYBOX_EMPTY 0x04             /* container record byte 7 */
#define GROUP_VARIANT 8                 /* group word +8: the variant of the item sets */

/* True when a money box holds no coin. */
static int moneybox_empty(uint16_t box) { return dungeon_thing_record(box)[7] & MONEYBOX_EMPTY; }

/* The item class bitmap of filter for the group in the AI context: parsed once per update into the scratch block.
   Returns NULL when the creature has no class text for it. */
static uint8_t *filter_bitmap(int filter, int creature_type, int is_group)
{
    static uint8_t uncached[FILTER_BITMAP_SIZE];
    if (filter >= FILTER_SLOTS || !ai.scratch) {
        /* fix: the original reads pointers after its table of 0x2a filters, or writes the bitmap to address 0
           without a scratch block; the port parses without a cache */
        return shop_parse_item_class_set(creature_type, filter, uncached, is_group) ? uncached : NULL;
    }
    if (ai.filters[filter]) return ai.filters[filter];
    ai.filter_free--;
    uint8_t *bitmap = ai.scratch + ai.scratch_used;
    ai.filters[filter] = bitmap;
    ai.scratch_used += FILTER_BITMAP_SIZE;
    return shop_parse_item_class_set(creature_type, filter, bitmap, is_group) ? bitmap : NULL;
}

/* True (1, or FILTER_INVERT for an inverted filter) when thing belongs to the item filter of the group in the AI
   context: classes of items, creatures, and wall actuators that the creature text lists, or money boxes. */
int creature_thing_matches_filter(uint16_t thing, int filter)
{
    int invert = filter & FILTER_INVERT;
    int f = filter & FILTER_MASK;
    if (f == FILTER_ANY) return !invert;
    if (f == FILTER_FULL_MONEYBOX)
        return item_is_moneybox(thing) && !moneybox_empty(thing) ? !invert : invert;
    if (f == FILTER_EMPTY_MONEYBOX) {
        if (item_is_moneybox(thing)) return moneybox_empty(thing) ? !invert : invert;
        f = FILTER_EMPTY_MONEYBOX_ELSE;
    }
    int type = thing_type(thing);
    if ((f >= FILTER_BY_VARIANT && f < FILTER_BY_VARIANT_END) || f == FILTER_MONEYBOX_OR_VARIANT) {
        if (f == FILTER_MONEYBOX_OR_VARIANT) {
            if (item_is_moneybox(thing) && !moneybox_empty(thing)) return !invert;
            f = FILTER_BY_VARIANT;
        }
        f = (int16_t)(f + (int16_t)(ai.group[GROUP_VARIANT] | ai.group[GROUP_VARIANT + 1] << 8) * 3);
    }
    uint8_t *bitmap = filter_bitmap(f, ai.group[GROUP_CREATURE], type == THING_GROUP);
    if (!bitmap) return 0;
    int16_t value;
    if (type == THING_GROUP) {
        value = dungeon_thing_record(thing)[GROUP_CREATURE];
    } else if (type == THING_SENSOR) {
        int kind = thing_word(thing, SENSOR_TYPE_DATA) & SENSOR_TYPE_MASK;
        if (kind == ACTUATOR_COUNTED_ITEM && !(thing_word(thing, SENSOR_TYPE_DATA) & ~SENSOR_TYPE_MASK)) return 0;
        if (kind == ACTUATOR_SLOT || kind == ACTUATOR_COUNTED_ITEM)
            value = (int16_t)gdat_entry_index(draw_gdat(), CAT_ORNAMENT, item_gdat_index(thing), GDAT_TYPE_VALUE, ORNAMENT_KEY_TARGET);
        else
            value = (int16_t)kind;
    } else if (type >= THING_WEAPON && type <= THING_MISC) {
        value = (int16_t)item_get_index(thing);
    } else {
        return 0;
    }
    /* fix: the original reads outside the bitmap for a negative or large value */
    if (value < 0 || value >= FILTER_BITMAP_SIZE * 8) return invert;
    return bitmap[value >> 3] >> (value & 7) & 1 ? !invert : invert;
}

/* Parses the item class text (category 0x0f, creature type, entry filter + 0x10) into a 512-bit set of item
   indices: numbers and ranges ("3-7") after a class letter (A clothing, C containers, J misc, P potions, S scrolls,
   W weapons); skip_containers makes C the weapons. Returns 0 when the text is empty. */
int shop_parse_item_class_set(int creature_type, int filter, uint8_t *bitset, int skip_containers)
{
    char text[CLASS_TEXT_SIZE];
    memset(bitset, 0, FILTER_BITMAP_SIZE);
    gdat_text(draw_gdat(), CAT_CREATURE, creature_type & 0xff, (filter + FILTER_TEXT_ENTRY) & 0xff, text, sizeof text);
    if (!text[0]) return 0;
    int16_t number = 0, first = -1, base = -1;
    int have_number = 0;
    const char *p = text;
    uint8_t c;
    do {
        c = (uint8_t)*p++;
        if (c >= '0' && c <= '9') {
            number = (int16_t)(number * 10 + (c - '0'));
            have_number = 1;
            continue;
        }
        if (c == '-') {
            first = number;
            number = 0;
            continue;
        }
        if (have_number) {
            if (first < 0) first = number;
            for (; first <= number; first++) {
                int16_t bit = (int16_t)(first + base);
                /* fix: the original writes outside the set for a number without a class letter */
                if (bit >= 0 && bit < FILTER_BITMAP_SIZE * 8) bitset[bit >> 3] |= (uint8_t)(1 << (bit & 7));
            }
            number = 0;
            first = -1;
            base = -1;
            have_number = 0;
        }
        switch (c) {
        case 'A': base = 0x80; break;
        case 'C': base = skip_containers ? 0 : 0x1e0; break;
        case 'J': base = 0x100; break;
        case 'P': base = 0x180; break;
        case 'S': base = 0x1fc; break;
        case 'W': base = 0; break;
        default: break;
        }
    } while (c);
    return 1;
}

static int is_item(int type) { return (type > THING_GROUP && type < THING_PROJECTILE) || type == THING_CONTAINER; }

/* The first item of filter in a thing list (cell 0xff: any cell), THING_ENDOFLIST for none. */
uint16_t creature_find_matching_item(int filter, uint16_t first_thing, int cell)
{
    uint16_t t = first_thing;
    for (; t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        if (is_item(thing_type(t)) && ((cell & 0xff) == 0xff || (cell & 0xff) == t >> THING_CELL_SHIFT)
            && creature_thing_matches_filter(t, (int16_t)filter))
            break;
    return t;
}

/* Removes and frees the items of filter from a thing list (cell 0xff: any cell); the matching items inside a
   chest go too, and a matching chest gives its other contents to the list. */
void creature_remove_matching_items(uint16_t *list_head, int filter, int cell)
{
    cell &= 0xff;
    uint16_t next;
    for (uint16_t t = *list_head; t != THING_ENDOFLIST; t = next) {
        next = dungeon_next_thing(t);
        int type = thing_type(t);
        if (!is_item(type) || (cell != 0xff && cell != t >> THING_CELL_SHIFT)) continue;
        int match = creature_thing_matches_filter(t, (int16_t)filter);
        if (type == THING_CONTAINER && (!item_is_moneybox(t) || match)) {
            uint8_t *record = dungeon_thing_record(t);
            uint16_t *contents = (uint16_t *)(record + CONTAINER_CONTENTS);
            creature_remove_matching_items(contents, filter, 0xff);
            if (match) {
                uint16_t c_next;
                for (uint16_t c = *contents; c != THING_ENDOFLIST; c = c_next) {
                    c_next = dungeon_next_thing(c);
                    dungeon_unlink_thing_from_list(c, record + CONTAINER_CONTENTS);
                    dungeon_append_thing((uint16_t)((c & 0x3fff) | cell << THING_CELL_SHIFT), list_head);
                }
            }
        }
        if (match) {
            dungeon_unlink_thing_from_list(t, (uint8_t *)list_head);
            set_thing_word(t, THING_NEXT, THING_NONE);
        }
    }
}

/* The number of items of filter in a thing list (cell 0xff: any cell), with the possessions of groups and the
   contents of chests on request. */
int creature_count_matching_items(int filter, uint16_t first_thing, int cell, int into_groups, int into_containers)
{
    int count = 0;
    cell &= 0xff;
    for (uint16_t t = first_thing; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        int type = thing_type(t);
        if ((into_groups && type == THING_GROUP) || (into_containers && item_is_chest(t)))
            count += creature_count_matching_items(filter, thing_word(t, CONTAINER_CONTENTS), cell, into_groups, into_containers);
        if (type > THING_GROUP && type < THING_PROJECTILE && (cell == 0xff || cell == t >> THING_CELL_SHIFT)
            && creature_thing_matches_filter(t, (int16_t)filter))
            count++;
    }
    return count;
}

/* The AI marker text on the square in the AI cache for the level delta (1: also the markers of level 2 with bit 0)
   whose kind is in id_mask: its id (0 to 31), -1 none. */
int creature_find_ai_marker(int level_delta, uint16_t id_mask)
{
    int16_t other = (int16_t)level_delta == 1 ? 2 : -1;
    for (uint16_t t = cached_first_thing(); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_TEXT) continue;
        uint16_t data = thing_word(t, TEXT_DATA);
        if ((data & TEXT_KIND_MASK) != TEXT_KIND_MARKER) continue;
        int level = data >> MARKER_LEVEL_SHIFT;
        if ((level == (int16_t)level_delta || (level == other && data & 1)) && (1 << (data >> 8 & 0xf)) & id_mask)
            return data >> 3 & 0x1f;
    }
    return -1;
}

/* The AI marker text on the square in the AI cache for the level delta with kind id: its id, -1 none. */
int creature_find_ai_marker_by_id(int level_delta, int id)
{
    for (uint16_t t = cached_first_thing(); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_TEXT) continue;
        uint16_t data = thing_word(t, TEXT_DATA);
        if ((data & TEXT_KIND_MASK) == TEXT_KIND_MARKER && data >> MARKER_LEVEL_SHIFT == (int16_t)level_delta
            && (data >> 8 & 0xf) == (int16_t)id)
            return data >> 3 & 0x1f;
    }
    return -1;
}

/* ---- The path search ---- */

/* The state of a search (the locals of the original). */
typedef struct {
    int command, enabled, count;
    AiCandidate *cands;
    int8_t (*marks)[4];             /* per candidate: a target square (x, y, map) and the best distance */
    uint8_t *maps[256];             /* the search map of each map, NULL until the search enters it */
    uint8_t *map;                   /* ... of the current map */
    struct { uint8_t x, y, map; } queue[SEARCH_QUEUE_SIZE];
    int head, tail;
    uint8_t counts[256];            /* the cost search: queued squares by distance */
    int level, cost_search;
    int16_t start_x, start_y, start_map;
    int16_t x, y;                   /* the square that the search tests */
    int16_t from_x, from_y, from_map; /* the square that the search left */
    int16_t check_map;              /* the map of the last square that the search entered */
    int16_t dist, max_cost, dir, turn, steps_left, result;
    int16_t passage_map, passage_x, passage_y; /* the other side of a passage that the search went through */
    int16_t home_x, home_y, home_map, act_x, act_y, act_map;
    uint8_t visit, blocking, or_flags;
    int found, two_maps, flee_once, levels, down_ok, up_ok;
    int locations;                  /* a target has a location: the start may be a passage */
    uint16_t saved_attack;
} Search;

static uint8_t *search_map_cell(Search *s, int map, int x, int y) { return search_cell(s->maps[map], x, y); }

static int16_t max16(int a, int b) { return (int16_t)a > (int16_t)b ? (int16_t)a : (int16_t)b; }

static int next_direction_bits(void)
{
    search_direction_bits = search_direction_bits & 1 ? (search_direction_bits >> 1) ^ 0xb400 : search_direction_bits >> 1;
    return search_direction_bits;
}

/* The distance of the party from (x, y) of map, across maps by the dungeon position; shift doubles it. */
static int16_t far_party_distance(int x, int y, int map, int shift)
{
    int tm = ai_party.map;
    return (int16_t)(square_distance((int16_t)(dungeon_map_offset_x(map) + x), (int16_t)(dungeon_map_offset_y(map) + y),
                                     (int16_t)(dungeon_map_offset_x(tm) + ai_party.x), (int16_t)(dungeon_map_offset_y(tm) + ai_party.y))
                     << shift);
}

/* A target that needs the party seen: an invisible party or a party in the dark may stay hidden. */
static void hide_party_target(AiCandidate *c)
{
    int invisible = game.magic_light[MAGIC_INVISIBILITY] && !(ai.info->flags & CREATURE_SEES_INVISIBLE);
    int dark = 0;
    if (!invisible) {
        dark = view_light_level && (int16_t)(game.magic_light[0] | game.magic_light[1] << 8) < 0
               && !(ai.info->flags & CREATURE_SEES_IN_DARK);
        if (!dark) return;
    }
    int16_t dist = ai.map == ai_party.map ? (int16_t)square_distance(group_x(), group_y(), ai_party.x, ai_party.y)
                                          : far_party_distance(group_x(), group_y(), ai.map, 0);
    uint32_t r = rand_next();
    if (!(r & 0x8000)) {
        if (dist == 0) return;
        if (dist == 1 && dun_get_direction_to(group_x(), group_y(), ai_party.x, ai_party.y) == group_dir()) return;
    }
    int hide = dist > 4;
    if (!hide) {
        if (invisible && ((int)(r & 0xf) - (4 - dist) > info_sight(ai.info) || r & 0x4000)) hide = 1;
        else if (dark && dist * view_light_level - (int)(r >> 8 & 7) > info_sight(ai.info)) hide = 1;
    }
    if (hide) c->cost = r & 0x70 ? -1 : (int8_t)(c->cost >> 3);
}

/* Prepares the candidates: their target squares, flags, and the largest cost that the search needs. */
static void search_setup(Search *s)
{
    for (int i = 0; i < s->count; i++) {
        AiCandidate *c = &s->cands[i];
        int8_t *m = s->marks[i];
        c->path_length = -1;
        m[0] = (int8_t)(c->marker & 0x1f);
        m[1] = (int8_t)(c->marker >> 5 & 0x1f);
        m[2] = (int8_t)(c->marker >> 10);
        uint8_t kind = (uint8_t)c->kind;
        /* fix: the original reads past its table for kinds above 0x1b */
        uint8_t flags = kind < sizeof kind_flags ? kind_flags[kind] : 0;
        c->unknown_10 = flags;
        s->or_flags |= flags;
        uint16_t home = (uint16_t)(ai.group[GROUP_HOME] | ai.group[GROUP_HOME + 1] << 8);
        switch (kind) {
        case TARGET_HOME:
            s->home_x = home & 0x1f;
            s->home_y = home >> 5 & 0x1f;
            s->home_map = home >> 10;
            break;
        case TARGET_ACTIVATION:
            s->act_x = ai.active->position & 0x1f;
            s->act_y = ai.active->position >> 5 & 0x1f;
            s->act_map = ai.active->position >> 10;
            /* the original compares the position with the party position in another order */
            if (s->act_x == ai_party.map && s->act_y == ai_party.x && s->act_map == ai_party.y) c->unknown_10 = flags ^ 3;
            break;
        case TARGET_FLEE: {
            int16_t d = party.cur_map == ai_party.map ? (int16_t)square_distance(group_x(), group_y(), ai_party.x, ai_party.y) : SEARCH_FAR;
            m[3] = (int8_t)max16(d, (int16_t)c->item);
            break;
        }
        case TARGET_SHOOT_PARTY:
        case TARGET_MELEE_PARTY:
        case TARGET_STEAL: s->saved_attack = ai.attack_flags; break;
        case TARGET_MELEE_LINE:
        case TARGET_SHOOT_LINE:
            s->saved_attack = ai.attack_flags;
            break;
        case TARGET_NEAR_MARKER:
            m[3] = (int8_t)(m[2] == party.cur_map ? max16(1, square_distance(group_x(), group_y(), m[0], m[1])) : SEARCH_FAR);
            break;
        case TARGET_LIGHT:
            ai.move_flags = MOVE_FLAGS_LIGHT;
            light_source_sum = explosion_light_sum = 0;
            break;
        case TARGET_AI_MARKER: m[3] = SEARCH_FAR_RANGE; break;
        case TARGET_PARTY_DISTANCE: ai.move_flags = MOVE_FLAGS_PARTY_MAP; break;
        default: break;
        }
        if (kind == TARGET_HOME || kind == TARGET_ACTIVATION || kind == TARGET_MARKER || kind == TARGET_NEAR_MARKER)
            s->locations = 1;
        if (!s->enabled && flags & KIND_PARTY_LOCATION) hide_party_target(c);
        s->max_cost = max16(s->max_cost, c->cost + (c->unknown_10 & KIND_TESTS_BLOCKED ? 1 : 0));
    }
}

static int at_place(Search *s, int map, int x, int y, int16_t tx, int16_t ty, int16_t tmap)
{
    return ((int16_t)map == tmap && (int16_t)x == tx && (int16_t)y == ty)
           || (s->passage_map == tmap && s->passage_x == tx && s->passage_y == ty);
}

/* A target of a search mark: the marked square, or its other side. */
static int at_mark(Search *s, const int8_t *m)
{
    return at_place(s, s->check_map, s->x, s->y, m[0], m[1], m[2]);
}

static int lucky_step(const AiCandidate *c)
{
    return c->path_length <= 0 || !(next_direction_bits() & 7);
}

/* Writes the distance of the square into a party distance map (or adds bits with add). */
static void mark_distance(Search *s, DistanceMap *d, uint8_t value, int add)
{
    uint8_t *cell;
    if (s->check_map == d->map) cell = &d->cells[s->x * 32 + s->y];
    else if (s->passage_map >= 0 && s->passage_map == d->map) cell = &d->cells[s->passage_x * 32 + s->passage_y];
    else return;
    if (add) *cell |= value;
    else *cell = value;
}

/* A line target found from the square that the search reached: the shooter square and its distance. */
static int line_target(Search *s, AiCandidate *c, int x, int y, int dir, int at_entered)
{
    uint16_t location = c->destination;
    ai.attack_flags &= c->mask;
    int found = creature_ai_find_target_in_lines((int16_t)c->item, s->map, &location, x, y, dir, c->kind == TARGET_SHOOT_LINE);
    c->destination = location;
    if (found) {
        c->found = pack_location(ai_square.x, ai_square.y, ai_square.map);
        if (at_entered && ai_square.x == s->x && ai_square.y == s->y && ai_square.map == s->check_map)
            c->path_length = (int8_t)s->dist;
        else
            c->path_length = (int8_t)(*search_map_cell(s, ai_square.map, ai_square.x, ai_square.y) - 1);
    }
    ai.attack_flags = s->saved_attack;
    return found;
}

/* The test of a candidate on a square that the search entered. */
static int test_entered(Search *s, int i)
{
    AiCandidate *c = &s->cands[i];
    int8_t *m = s->marks[i];
    int16_t x = s->x, y = s->y, d;
    int on_party_map = s->check_map == ai_party.map;
    switch ((uint8_t)c->kind) {
    case TARGET_ANY: return 1;
    case TARGET_HOME: return at_place(s, s->check_map, x, y, s->home_x, s->home_y, s->home_map);
    case TARGET_PARTY_VIEW:
        if (c->item == 0) return at_party(x, y);
        if (c->item == 2) {
            if (dun_get_direction_to(ai_party.x, ai_party.y, x, y) != ai_party.dir) return 0;
            d = (int16_t)creature_trace_line(ai_party.x, ai_party.y, x, y, creature_square_is_obstacle);
            return d > 0 && d <= (int16_t)c->mask;
        }
        if (c->item == 4) {
            if (square_distance(x, y, ai_party.x, ai_party.y) != (int16_t)c->mask) return 0;
            if (x != ai_party.x && y != ai_party.y) return 1;
            return !(int16_t)creature_trace_line(ai_party.x, ai_party.y, x, y, creature_square_blocks_line_except_self);
        }
        return 0;
    case TARGET_ACTIVATION: return at_place(s, s->check_map, x, y, s->act_x, s->act_y, s->act_map);
    case TARGET_PARTY_RANGE_2: return on_party_map && square_distance(x, y, ai_party.x, ai_party.y) == 2;
    case TARGET_FLEE:
        if (c->marker != FOUND_NONE) return at_mark(s, m);
        if (!lucky_step(c)) return 0;
        if (on_party_map) {
            d = (int16_t)square_distance(x, y, ai_party.x, ai_party.y);
        } else {
            int shift = dungeon_map_level(s->check_map) != dungeon_map_level(ai_party.map);
            d = far_party_distance(x, y, s->check_map, shift);
        }
        if (m[3] >= d) return 0;
        if (on_party_map && d < 5 && (x == ai_party.x || y == ai_party.y)
            && (int16_t)creature_trace_line(ai_party.x, ai_party.y, x, y, creature_square_blocks_line_except_self))
            return 0;
        m[3] = (int8_t)d;
        return 1;
    case TARGET_SHOOT_PARTY:
    case TARGET_MELEE_PARTY: {
        if (!on_party_map) return 0;
        ai.attack_flags &= c->mask;
        int r = creature_ai_try_attack(c->kind == TARGET_SHOOT_PARTY, x, y, ai_party.x, ai_party.y, -1);
        ai.attack_flags = s->saved_attack;
        return r;
    }
    case TARGET_MELEE_LINE:
    case TARGET_SHOOT_LINE:
        if (c->cost != s->dist) return 0;
        return line_target(s, c, x, y, -1, 1);
    case TARGET_STEAL: {
        if (!on_party_map || ai_action_flag((int8_t)ai.previous_action) & AI_ACTION_FLAG_BUSY) return 0;
        ai.attack_flags &= c->mask;
        int r = creature_ai_try_attack(0, x, y, -1, -1, group_dir());
        ai.attack_flags = s->saved_attack;
        return r;
    }
    case TARGET_MARKER: return at_mark(s, m);
    case TARGET_NEAR_MARKER:
        if (m[2] == s->check_map) {
            d = (int16_t)square_distance(x, y, m[0], m[1]);
            if (m[3] > d) goto nearer;
        }
        if (m[2] != s->passage_map) return 0;
        d = (int16_t)square_distance(s->passage_x, s->passage_y, m[0], m[1]);
        if (d >= m[3]) return 0;
    nearer:
        m[3] = (int8_t)d;
        if (!(uint8_t)d) c->unknown_10 &= (uint8_t)~KIND_FIRST_BEST;
        return 1;
    case TARGET_WANDER:
        if (c->marker != FOUND_NONE) return at_mark(s, m);
        return s->dist > 0 && lucky_step(c);
    case TARGET_RANDOM: {
        int16_t chance = (int16_t)c->item;
        if (chance < 0) return rand_below((uint16_t)-chance) != 0;
        return chance > 0 && !rand_below((uint16_t)chance);
    }
    case TARGET_ITEM:
    case TARGET_ITEM_2: return creature_ai_try_take_item(0, x, y, x, y, -1, (int16_t)c->item);
    case TARGET_DAMAGE_GROUP: {
        if (x == s->start_x && y == s->start_y) return 0;
        uint16_t g = creature_ai_cached_first_group();
        if (g == THING_ENDOFLIST || !creature_thing_matches_filter(g, (int16_t)c->item)) return 0;
        creature_apply_damage_kind(g, x, y, (int16_t)c->mask);
        return 1;
    }
    case TARGET_MOVE_TEST: return creature_ai_try_move((uint8_t)c->item, x, y, x, y, -1);
    case TARGET_LIGHT:
        if (ai_square.square & 0x10) light_add_square_light(s->dist, s->dir == -1 ? game.party_dir : s->dir, x, y, 3);
        return 0;
    case TARGET_AI_MARKER: {
        if (!(ai_square.square & 0x10)) return 0;
        int16_t id = (int16_t)creature_find_ai_marker_by_id((int16_t)c->item, (int16_t)c->mask);
        if (id == -1 || id >= m[3]) return 0;
        if (id && m[3] >= SEARCH_FAR_RANGE && c->cost) {
            m[3] = (int8_t)id;
            return 0;
        }
        c->unknown_10 &= (uint8_t)~KIND_FIRST_BEST;
        return 1;
    }
    case TARGET_MARKER_SIDE: {
        if (s->dir == -1 || !(ai_square.square & 0x10)) return 0;
        int16_t sides = (int16_t)creature_find_ai_marker(1, (uint16_t)ai.attr7);
        return sides != -1 && !((1 << ((s->dir + 2) & 3)) & sides);
    }
    case TARGET_PARTY_DISTANCE:
        mark_distance(s, &party_distance, (uint8_t)(s->dist + 1), 0);
        mark_distance(s, &alt_distance, (uint8_t)(s->dist + 1), 0);
        return 0;
    default: return 0;
    }
}

/* The test of a candidate on a square that the search could not enter (from the square it left). */
static int test_blocked(Search *s, int i)
{
    AiCandidate *c = &s->cands[i];
    int16_t x = s->x, y = s->y;
    switch ((uint8_t)c->kind) {
    case TARGET_PARTY_VIEW:
        if (!at_party(x, y)) return 0;
        if (c->item == 1) {
            if (!c->mask || (1 << ((dun_get_direction_to(ai_party.x, ai_party.y, s->from_x, s->from_y) - ai_party.dir) & 3)) & c->mask)
                return 1;
            s->visit = 0xff;
        } else if (c->item == 3) {
            uint8_t square = dun_get_square(x + dir_dx[s->dir], y + dir_dy[s->dir]);
            if (square_type(square) == SQUARE_PIT && square & SQUARE_PIT_OPEN && !(square & 1)) return 1;
            s->visit = 0xff;
        }
        return 0;
    case TARGET_ACTIVATION: return at_party(x, y);
    case TARGET_MELEE_LINE:
        if (square_type(ai_square.square) == SQUARE_WALL) return 0;
        /* fall through */
    case TARGET_SHOOT_LINE: return line_target(s, c, s->from_x, s->from_y, s->dir, 0);
    case TARGET_ITEM:
    case TARGET_ITEM_WALL: {
        int r = creature_ai_try_take_item(0, s->from_x, s->from_y, x, y, s->dir, (int16_t)c->item);
        if (!r && ai_square_scan.items_elsewhere) s->visit = 0xff;
        return r;
    }
    case TARGET_GROUP_FACING:
        if (group_dir() != s->dir) return 0;
        /* fall through */
    case TARGET_GROUP_IN_FRONT: {
        uint16_t g = creature_ai_cached_first_group();
        if (g == THING_ENDOFLIST || dungeon_thing_record(g)[GROUP_CREATURE] != c->item) return 0;
        if (c->mask == 1) return 1;
        if (c->mask == 2) {
            for (uint16_t t = thing_word(g, GROUP_POSSESSIONS); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
                if (thing_type(t) != THING_PROJECTILE && t >> THING_CELL_SHIFT != s->dir) return 1;
            return 0;
        }
        int16_t nx = (int16_t)(x + dir_dx[s->dir]), ny = (int16_t)(y + dir_dy[s->dir]);
        uint16_t other;
        int r = at_party(nx, ny)
                || ((other = creature_get_group_at((uint16_t)nx, (uint16_t)ny)) != THING_NONE
                    && !(creature_group_info(other)->flags & CREATURE_STATIC));
        if (!r) s->visit = 0xff;
        return r;
    }
    case TARGET_DAMAGE_GROUP: {
        uint16_t g = creature_ai_cached_first_group();
        if (g == THING_ENDOFLIST || !creature_thing_matches_filter(g, (int16_t)c->item)) return 0;
        creature_apply_damage_kind(g, x, y, (int16_t)c->mask);
        return 1;
    }
    case TARGET_ACTUATOR: {
        if (!(ai_square.square & 0x10)) return 0;
        int r = creature_ai_try_use_wall_actuator((uint8_t)c->mask, s->from_x, s->from_y, x, y, s->dir, (int16_t)c->item);
        if (!r) s->visit = 0xff;
        return r;
    }
    case TARGET_LIGHT:
        if (ai_square.square & 0x10) {
            light_add_square_light(s->dist, s->dir, x, y, 4);
            s->visit = 0xff;
        }
        return 0;
    case TARGET_DOOR: {
        if (c->item == 1 && !at_party(x, y)) return 0;
        if (!creature_ai_try_door_action((uint8_t)c->mask, s->from_x, s->from_y, x, y, s->dir)) return 0;
        if (c->item == 2 && square_distance(x, y, ai_party.x, ai_party.y) != 1) return 0;
        creature_ai_cache_square(x, y);
        if (c->mask == 0 && !(ai_square.square & SQUARE_DOOR_STATE)) return 0;
        if (c->mask == 1 && (ai_square.square & SQUARE_DOOR_STATE) == DOOR_STATE_CLOSED) return 0;
        uint8_t bits = dungeon_thing_record(cached_first_thing())[3];
        if (!(bits & DOOR_LOCKED >> 8)) return 1;
        if (bits & DOOR_LOCK_OPEN >> 8) return c->mask != 0;
        return c->mask != 1;
    }
    case TARGET_PARTY_DISTANCE: {
        uint8_t value = (uint8_t)(s->blocking ? 1 << s->dir | 0x80 : s->dist + 1);
        mark_distance(s, &party_distance, value, 1);
        mark_distance(s, &alt_distance, value, 1);
        return 0;
    }
    default: return 0;
    }
}

/* Runs the tests of the candidates (entered or blocked) on the current square; leaves the found one in s->result
   candidate index via the return value, -1 for none. */
static int test_candidates(Search *s, int blocked)
{
    for (int i = 0; i < s->count; i++) {
        AiCandidate *c = &s->cands[i];
        if (blocked ? c->cost + 1 < s->dist : c->cost < s->dist) continue;
        if (!(c->unknown_10 & (blocked ? KIND_TESTS_BLOCKED : KIND_TESTS_OPEN))) continue;
        if (!(blocked ? test_blocked(s, i) : test_entered(s, i))) continue;
        if (s->two_maps && party.cur_map != s->start_map && party.cur_map != ai_party.map) continue;
        return i;
    }
    return -1;
}

/* Stores a found candidate and narrows the search to the better candidates; returns 1 when the search ends. */
static int search_found(Search *s, int i)
{
    AiCandidate *c = &s->cands[i];
    if (!(c->unknown_10 & KIND_NO_LOCATION)) {
        c->destination = c->unknown_10 & KIND_PARTY_LOCATION ? pack_location(ai_party.x, ai_party.y, ai_party.map)
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
    if (i == 0 && (!(c->unknown_10 & KIND_FIRST_BEST) || c->cost <= s->dist)) return 1;
    s->count = i + 1;
    /* fix: for i 0 the original reads the record before the candidates; with zero bytes there it changes nothing */
    int j = i - 1;
    if (j >= 0) {
        while (s->cands[j].value >= 0x80) {
            if (j == 0) return 1;
            j--;
        }
        if (!(c->unknown_10 & KIND_FIRST_BEST)) s->count = j + 1;
        if (!(c->unknown_10 & KIND_NO_BONUS)) {
            int16_t bonus = 0;
            for (int k = j; k >= 0; k--) {
                int8_t v = (int8_t)s->cands[k].value;
                if (v > 0) bonus = (int16_t)(bonus + v);
                if (bonus + s->dist < s->cands[k].cost) s->cands[k].cost = (int8_t)(s->dist + bonus);
            }
        }
    }
    s->or_flags = 0;
    s->max_cost = 0;
    for (int k = 0; k < s->count; k++) {
        uint8_t flags = s->cands[k].unknown_10;
        s->or_flags |= flags;
        if (k == 0 || s->cands[k].path_length < 0)
            s->max_cost = max16(s->cands[k].cost + (flags & KIND_TESTS_BLOCKED ? 1 : 0), s->max_cost);
    }
    return 0;
}

/* A square that the search entered: its tests, then its distance and the way back in the search map; the square
   goes into the queue when its distance is in the range of the candidates. Returns the found candidate, -1 none. */
static int search_entered(Search *s)
{
    s->check_map = party.cur_map;
    int i = s->or_flags & KIND_TESTS_OPEN ? test_candidates(s, 0) : -1;
    int16_t dist = (int16_t)(s->dist + ai_square.cost);
    if (dist <= s->max_cost) {
        s->queue[s->tail].x = (uint8_t)s->x;
        s->queue[s->tail].y = (uint8_t)s->y;
        s->queue[s->tail].map = (uint8_t)s->check_map;
        s->tail = (s->tail + 1) & 0xff;
        /* fix: the original counts past its 0x33 levels into its own variables */
        s->counts[(uint8_t)dist]++;
    }
    uint8_t *cell = search_cell(s->map, s->x, s->y);
    uint16_t from = pack_location(s->from_x, s->from_y, s->from_map);
    cell[0] = (uint8_t)dist;
    cell[1] = (uint8_t)s->dir;
    cell[2] = (uint8_t)from;
    cell[3] = (uint8_t)(from >> 8);
    return i;
}

/* A square that the search could not enter: the tests of the candidates that look at such squares. */
static int search_blocked(Search *s)
{
    s->blocking = ai_square_scan.blocking;
    s->visit = (uint8_t)s->dist;
    int i = -1;
    if (s->or_flags & KIND_TESTS_BLOCKED) {
        if (s->or_flags & KIND_CACHES_SQUARE) creature_ai_cache_square(s->x, s->y);
        i = test_candidates(s, 1);
    }
    if (!s->blocking) search_cell(s->map, s->x, s->y)[0] = s->visit;
    return i;
}

static void search_enter_map(Search *s, int map)
{
    if (map == party.cur_map) return;
    creature_ai_set_map(map);
    s->map = s->maps[map];
}

enum { STEP_SKIP, STEP_BLOCKED, STEP_ENTERED };

/* Tries the step of the search in s->dir (4 and 5: the level changes) from the square it left. */
static int search_step(Search *s)
{
    search_enter_map(s, s->from_map);
    if (s->levels && s->steps_left <= 2) {
        if (s->steps_left == 2) {
            if (!s->down_ok) return STEP_SKIP;
            s->dir = 5;
        } else {
            if (!s->up_ok) return STEP_SKIP;
            s->dir = 4;
        }
        s->x = s->from_x;
        s->y = s->from_y;
    } else {
        s->x = (int16_t)(s->from_x + dir_dx[s->dir]);
        s->y = (int16_t)(s->from_y + dir_dy[s->dir]);
        if (!on_map(s->x, s->y)) return STEP_SKIP;
    }
    ai_square_scan.blocking = 0;
    s->visit = search_cell(s->map, s->x, s->y)[0];
    if (!s->visit || s->dir >= 4) {
        int dir = s->dir;
        if (!creature_ai_try_move(s->command, s->from_x, s->from_y, s->x, s->y, dir)) {
            s->visit = 0xff;
        } else {
            s->passage_map = -1;
            s->passage_x = s->x;
            s->passage_y = s->y;
            if (ai_square.map != party.cur_map) {
                if (!s->maps[ai_square.map]) s->maps[ai_square.map] = calloc(32, SEARCH_COLUMN_SIZE);
                if (dir < 4) s->passage_map = party.cur_map;
                creature_ai_set_map(ai_square.map);
                s->map = s->maps[party.cur_map];
            } else if (dir < 4) {
                if (ai_square_scan.passage) {
                    s->passage_map = ai_square_scan.end.map;
                    s->passage_x = ai_square_scan.end.x;
                    s->passage_y = ai_square_scan.end.y;
                } else if (ai_square.x != s->x || ai_square.y != s->y) {
                    s->passage_map = party.cur_map;
                }
            }
            s->visit = 0;
        }
    }
    if (s->visit) return s->visit == 0xff && s->dir < 4 ? STEP_BLOCKED : STEP_SKIP;
    s->x = ai_square.x;
    s->y = ai_square.y;
    if (search_cell(s->map, s->x, s->y)[0]) return STEP_SKIP;
    return STEP_ENTERED;
}

/* Takes the next square from the queue; returns 0 when the search ends. */
static int search_next_square(Search *s)
{
    for (;;) {
        if (s->tail == s->head) return 0;
        s->from_x = s->queue[s->head].x;
        s->from_y = s->queue[s->head].y;
        s->from_map = s->queue[s->head].map;
        s->head = (s->head + 1) & 0xff;
        search_enter_map(s, s->from_map);
        s->dist = search_cell(s->map, s->from_x, s->from_y)[0];
        if (!s->cost_search) break;
        int deferred = 0;
        while (s->level < s->dist) {
            if (s->counts[s->level]) {
                s->queue[s->tail].x = (uint8_t)s->from_x;
                s->queue[s->tail].y = (uint8_t)s->from_y;
                s->queue[s->tail].map = (uint8_t)s->from_map;
                s->tail = (s->tail + 1) & 0xff;
                deferred = 1;
                break;
            }
            s->level++;
        }
        if (deferred) continue;
        if (!--s->counts[s->level]) s->level++;
        break;
    }
    if (s->dist > s->max_cost) return 0;
    if (s->dist > 6) s->command |= AI_MOVE_NO_MOVERS;
    if (s->flee_once && s->dist > 1) {
        s->command = (s->command & 0xe0) | 4;
        s->flee_once = 0;
    }
    s->turn = search_direction_bits & 1 ? 1 : -1;
    s->dir = next_direction_bits() & 3;
    s->steps_left = s->levels ? 7 : 5;
    return 1;
}

/* Writes the steps from the start to the found candidate into path (2 bytes per step). */
static void search_write_path(Search *s, uint8_t *path)
{
    AiCandidate *c = &s->cands[s->result];
    int x = c->found & 0x1f, y = c->found >> 5 & 0x1f, map = c->found >> 10;
    for (int16_t n = (int16_t)(c->path_length - 1); n >= 0; n--) {
        uint8_t *cell = search_map_cell(s, map, x, y);
        uint16_t from = (uint16_t)(cell[2] | cell[3] << 8);
        uint16_t step = (uint16_t)((path[n * 2] | path[n * 2 + 1] << 8) & PATH_KEEP_BITS);
        step |= (uint16_t)((cell[1] & 7) | (x & 0x1f) << 3 | (y & 0x1f) << 8);
        if (from >> 10 != map) {
            map = from >> 10;
            step |= PATH_MAP_CHANGE;
        }
        path[n * 2] = (uint8_t)step;
        path[n * 2 + 1] = (uint8_t)(step >> 8);
        x = from & 0x1f;
        y = from >> 5 & 0x1f;
    }
}

int creature_ai_search_area(int x, int y, int command, int count, AiCandidate *candidates, uint8_t *path)
{
    if (!candidates[0].kind) {
        candidates[0].destination = candidates[0].found = pack_location(x, y, party.cur_map);
        candidates[0].path_length = 0;
        return 0;
    }
    Search *s = calloc(1, sizeof *s);
    int8_t marks[256][4];
    s->marks = marks;
    s->command = command & 0xff;
    s->count = (int16_t)count;
    s->cands = candidates;
    s->start_x = (int16_t)x;
    s->start_y = (int16_t)y;
    s->start_map = party.cur_map;
    s->result = -1;
    s->enabled = command_enabled(s->command);
    s->two_maps = !s->enabled && class_flags() & AI_CLASS_STAYS_ON_MAPS;
    s->flee_once = (s->command & AI_MOVE_KIND_MASK) == 5;
    search_setup(s);
    if (ai.move_flags & MOVE_LEVELS && ai.move_flags & MOVE_LEVEL_FLAGS) {
        s->levels = 1;
        s->up_ok = ai.move_flags & MOVE_DIR4_FLAGS;
        s->down_ok = ai.move_flags & MOVE_DIR5_FLAGS;
    }
    /* fix: the original clears the first search map only for line targets or a cost; the port always clears it */
    s->maps[party.cur_map] = s->map = calloc(32, SEARCH_COLUMN_SIZE);
    s->cost_search = (s->command & AI_MOVE_KIND_MASK) == 8;
    if (s->cost_search) s->counts[SEARCH_LEVELS - 1] = 0xff;
    s->from_map = s->check_map = party.cur_map;
    s->x = s->from_x = (int16_t)x;
    s->y = s->from_y = (int16_t)y;
    s->steps_left = 1;
    s->dir = -1;
    s->passage_map = -1;
    creature_ai_cache_square(x, y);
    if (s->locations && (ai_square_scan.passage = (uint8_t)dun_get_linked_teleporter_dest(&ai_square_scan.end, x, y))) {
        s->passage_map = ai_square_scan.end.map;
        s->passage_x = ai_square_scan.end.x;
        s->passage_y = ai_square_scan.end.y;
    }
    int found = search_entered(s);
    for (;;) {
        if (found >= 0) {
            if (search_found(s, found)) break;
            found = -1;
        }
        s->dir = (int16_t)((s->dir + s->turn) & 3);
        if (--s->steps_left && ((s->tail + 1) & 0xff) != s->head) {
            int step = search_step(s);
            if (step == STEP_ENTERED) found = search_entered(s);
            else if (step == STEP_BLOCKED) found = search_blocked(s);
            continue;
        }
        if (!search_next_square(s)) break;
        s->dir = (int16_t)((s->dir - s->turn) & 3);  /* the first direction has no turn */
    }
    if (s->result != -1) search_write_path(s, path);
    creature_ai_set_map(s->start_map);
    int result = s->result;
    for (int m = 0; m < 256; m++) free(s->maps[m]);
    free(s);
    return result;
}

/* ---- Paths ---- */

#define PATH_CHECK_STEPS 5
#define PARTY_MAP_SEARCH_COST 0x19

/* Points ai.cached_path to the stored path of the group; a lost path clears the path state. */
void creature_ai_load_cached_path(void)
{
    ai.cached_path = NULL;
    if (!ai.active || !ai.cache.path_length) return;
    uint8_t *path = ai_find_path();
    if (!path) {
        ai.cache.path_left = 0;
        ai.cache.path_length = 0;
    } else {
        ai.cached_path = path;
    }
}

static uint16_t path_step(int index) { return (uint16_t)(ai.cached_path[index * 2] | ai.cached_path[index * 2 + 1] << 8); }

/* The direction of the next step of the stored path into the action; returns the steps left (0: none). A group
   at the end of its path forgets the found square. */
int creature_ai_next_path_step(void)
{
    ActiveGroup *a = ai.active;
    creature_ai_load_cached_path();
    int left = 0;
    if (ai.cache.path_length && (left = ai.cache.path_left)) {
        a->direction = path_step(ai.cache.path_length - left) & 7;
        return left;
    }
    uint16_t found = ai.cache.found;
    if (ai.map == found >> 10 && group_x() == (found & 0x1f) && group_y() == (found >> 5 & 0x1f)) ai.cache.found = FOUND_NONE;
    return left;
}

/* Checks that the stored path still leads to the target of the plan: the target is still where the search found it,
   and the next steps (at most 5) of the path are free. Returns 1 when the path holds. */
int creature_ai_plan_path(void)
{
    creature_ai_load_cached_path();
    int16_t i = 0;
    while (i < (int8_t)ai.candidate_count
           && !(ai.candidates[i].kind == ai.cache.kind && ai.candidates[i].item == ai.cache.item))
        i++;
    if (i >= (int8_t)ai.candidate_count) return 0;
    if (i) ai.candidates[0] = ai.candidates[i];
    ai.candidates[0].cost = 0;
    int saved = party.cur_map, result = 0;
    creature_ai_set_map(ai.cache.found >> 10);
    int16_t x = ai.cache.found & 0x1f, y = ai.cache.found >> 5 & 0x1f;
    uint8_t path[AI_PATH_MAX * 2];
    if ((int16_t)creature_ai_search_area(x, y, 2, 1, ai.candidates, path) || ai.candidates[0].path_length) goto end;
    int16_t cx = (int16_t)group_x(), cy = (int16_t)group_y(), left = ai.cache.path_left;
    if (!left) {
        result = cx == x && cy == y && party.cur_map == saved;
        goto end;
    }
    creature_ai_set_map(saved);
    creature_ai_load_cached_path();
    /* fix: without a stored path the original reads its steps at address 0 */
    if (!ai.cached_path) goto end;
    for (int16_t checks = PATH_CHECK_STEPS;;) {
        uint16_t step = path_step(ai.cache.path_length - left);
        if (!creature_ai_try_move(PATH_CHECK_COMMAND, cx, cy, -1, -1, step & 7)) goto end;
        if ((step >> 3 & 0x1f) != ai_square.x || (step >> 8 & 0x1f) != ai_square.y
            || (step >> 13 & 1) != (ai_square.map != party.cur_map))
            goto end;
        creature_ai_set_map(ai_square.map);
        cx = ai_square.x;
        cy = ai_square.y;
        if (--left <= 0 || --checks <= 0) break;
    }
    result = 1;
end:
    creature_ai_set_map(saved);
    return result;
}

/* After the party moved or the squares changed: the distance maps of the squares around the party on the party map
   and on the map of the alternate view; after a view change also the light sums of the light sources near the party. */
void creature_refresh_party_maps(void)
{
    int saved = party.cur_map;
    dun_set_current_map(game.party_map);
    AiCandidate target;
    uint8_t path[AI_PATH_MAX * 2];
    memset(&target, 0, sizeof target);
    if (party.redraw & REDRAW_SQUARES) {
        party_distance.map = (int16_t)game.party_map;
        party_distance.width = (int16_t)dungeon_map_width(game.party_map);
        memset(party_distance.cells, 0, (size_t)party_distance.width * 32);
        alt_distance.map = party.alt_view_map;
        if (alt_distance.map >= 0) {
            alt_distance.width = (int16_t)dungeon_map_width(alt_distance.map);
            memset(alt_distance.cells, 0, (size_t)alt_distance.width * 32);
        }
        target.cost = PARTY_MAP_SEARCH_COST;
        target.value = 0;
        target.kind = TARGET_PARTY_DISTANCE;
        creature_ai_search_area(game.party_x, game.party_y, 8, 1, &target, path);
    }
    if (party.redraw & REDRAW_VIEW) {
        int changed = game.party_map != last_party_map;
        int radius = gdat_entry_index(draw_gdat(), CAT_WALLSET, dungeon_map_graphics(game.party_map) >> 4 & 0xf,
                                      GDAT_TYPE_VALUE, WALLSET_KEY_LIGHT);
        if (!radius) {
            light_source_sum = explosion_light_sum = 0;
        } else {
            if (radius > LIGHT_RADIUS_MAX) radius = LIGHT_RADIUS_MAX;
            target.cost = (int8_t)radius;
            target.value = 0;
            target.kind = TARGET_LIGHT;
            creature_ai_search_area(game.party_x, game.party_y, 7, 1, &target, path);
            if (light_source_sum != cached_light_sum || explosion_light_sum != cached_explosion_light_sum) changed = 1;
        }
        cached_light_sum = light_source_sum;
        cached_explosion_light_sum = explosion_light_sum;
        if (changed) light_update_party_light();
    }
    dun_set_current_map(saved);
    party.redraw = 0;
    last_party_map = (int16_t)game.party_map;
}
