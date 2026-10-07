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
#include "weather.h"
#include "le.h"

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
#define SENSOR_CREATURE_SLOT 0x26       /* a wall slot that a creature type uses */
#define SLOT_STATE_BUSY 2               /* the state of a creature slot (its sensor effect bits): 2 and 3 busy */
#define ACTUATOR_SKILL_USE 0x0001      /* ai.actuator_flags: the creature uses wall actuators */
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

/* Creature info words. */
#define info_attack_range(i) ((i)->unknown_14 >> 12)
#define info_sight(i) ((i)->unknown_14 & 0xf)
#define CREATURE_NOT_SEEN_THROUGH CREATURE_NOT_DOOR_HIT /* flag 0x20: a creature that hides the squares behind it */
/* The AI class flags (ai_class_flags). */
#define AI_CLASS_SEES_ALL_SIDES 0x0100
#define AI_CLASS_SEES_BEHIND 0x0400
#define AI_CLASS_TURNS_TO_TAKE 0x1000
#define AI_CLASS_STAYS_ON_MAPS 0x2000   /* targets only on its own map and on the party map */
#define AI_CLASS_AVOIDS_CLOUDS 0x0200
#define AI_CLASS_AVOIDS_PARTY_DOORS 0x4000
/* ai.attack_flags. */
#define ATTACK_HIT 0x0001
#define ATTACK_HIT_2 0x0002
#define ATTACK_THROW 0x0008
#define ATTACK_FIREBALL 0x0010
#define ATTACK_HARM_NON_MATERIAL 0x0020
#define ATTACK_LIGHTNING 0x0040
#define ATTACK_POISON_CLOUD 0x0080
#define ATTACK_POISON_BOLT 0x0100
#define ATTACK_SLIME 0x0200
#define ATTACK_PUSH 0x0400
#define ATTACK_PULL 0x0800
#define ATTACK_MELEE_MASK 0x0007
#define ATTACK_RANGED_MASK 0x0ff8
#define ATTACK_STEAL 0x0004
/* ai.item_flags. */
#define ITEM_SKILL_SAME_SQUARE 0x0001
#define ITEM_SKILL_WALL 0x0004
#define ITEM_SKILL_STATIC 0x0008
#define ITEM_SKILL_CHESTS 0x0010
#define ITEM_SKILL_GROUPS 0x0020
#define ITEM_SKILL_OTHER_SQUARE 0x002a
#define AI_ACTION_FLAG_MOVES 0x02
#define AI_ACTION_FLAG_BUSY 0x03
/* Sub-commands of the shoot action (ActiveGroup.sub). */
#define SHOT_STEAL 0x0b
/* The flags of the search target kinds (ai_kind_flags). */
#define KIND_TESTS_OPEN 0x01      /* the kind tests the squares that the search enters */
#define KIND_TESTS_BLOCKED 0x02   /* ... the squares that it cannot enter */
#define KIND_FIRST_BEST 0x04      /* a found target may still get a nearer better one */
#define KIND_CACHES_SQUARE 0x08
#define KIND_NO_LOCATION 0x10     /* the found target keeps its location */
#define KIND_PARTY_LOCATION 0x20  /* the found target is the party square; also: needs the party seen */
#define KIND_NO_BONUS 0x40
/* The actions of a take from the same square (0, 1) or from the square in front (2, 3) by a random bit. */
static const uint8_t take_actions[] = {ACTION_DROP_HERE, ACTION_DROP_HERE_2, ACTION_DROP_AHEAD_2, ACTION_DROP_AHEAD};

#define SEARCH_QUEUE_SIZE 256
#define SEARCH_QUEUE_MASK (SEARCH_QUEUE_SIZE - 1)
#define SEARCH_LEVEL_BLOCKED 0xff
#define SEARCH_DIRECTION_TAPS 0xb400    /* the feedback of the shift register of the first search direction */
#define VISIT_BLOCKED 0xff              /* the search cannot enter the square */
#define CELLS_NEAR 0x03                 /* ... the two cells on the near side */
#define CELLS_FAR 0x0c
#define SLOPE_VERTICAL 0x80             /* line slope: the main delta is 0 */
#define SLOPE_SHIFT 6                   /* line slope in 1/64 */
#define ROLL_LOOK_AROUND 7              /* random bits: a creature that does not look that way looks in 1 of 8 tests */
#define DOOR_COST_CHEAP 3               /* a door of this search cost or less costs little when half open */
#define COMMAND_PAST_TABLE 0x14         /* the first command past the enabled table: it reads globals */
#define COMMAND_AFTER_GLOBALS 0x20      /* ... the commands that read the bytes after the globals */
#define PATH_X_SHIFT 3                  /* path step: direction bits 0-2, x bits 3-7, y bits 8-12 */
#define PATH_Y_SHIFT 8
#define PATH_MAP_CHANGE_SHIFT 13
/* Random bits of the test whether a creature sees an invisible party or a party in the dark. */
#define ROLL_CHECK_ADJACENT 0x8000      /* clear: an adjacent party in front stays seen */
#define ROLL_INVISIBLE_HIDDEN 0x4000
#define ROLL_SIGHT 0xf
#define ROLL_KEEP_COST 0x70             /* all clear: a hidden party target keeps 1/8 of its cost */
#define SEARCH_LEVELS 0x33              /* the counts of the cost search, the last one blocks */
#define SEARCH_CELL_SIZE 4              /* a square of a search map: distance, direction, previous square */
#define SEARCH_COLUMN_SIZE (MAP_SIDE_MAX * SEARCH_CELL_SIZE)
#define SEARCH_MAPS 256                 /* one search map per map number (a byte) */
#define SEARCH_DISTANCES 256            /* the cost search counts the squares per distance (a byte) */
#define SEARCH_MARKS 256                /* the marks of the candidates */
#define SEARCH_MOVERS_DISTANCE 6        /* beyond it groups that move do not block the search */
#define SEARCH_STEPS 5                  /* the first direction and three turns; with level changes two more */
#define SEARCH_STEPS_LEVELS 7
#define HIDDEN_DISTANCE 4               /* beyond it an invisible party or a party in the dark stays hidden */
#define FLEE_LINE_DISTANCE 5            /* a flee target nearer to the party must be out of its line of sight */
#define ROLL_DARK_SHIFT 8               /* random bits 8-10: the dark hides the party by this much more */
#define ROLL_DARK_MASK 7
#define ROLL_LUCKY 7                    /* random bits that must all be clear for a lucky step */
#define PATH_DIR_MASK 7                 /* path step and search cell: the direction */
/* The bytes of a square of a search map. */
enum { SEARCH_AT_DIST, SEARCH_AT_DIR, SEARCH_AT_FROM };
/* The marks of a candidate: a target square and the best distance. */
enum { MARK_X, MARK_Y, MARK_MAP, MARK_BEST, MARK_SIZE };
/* The kinds of the party view target (AiCandidate.item). */
enum { VIEW_ON_PARTY, VIEW_PARTY_SIDE, VIEW_IN_FRONT, VIEW_PARTY_AT_PIT, VIEW_AT_DISTANCE };
/* The kinds of the door target (AiCandidate.item). */
enum { DOOR_TARGET_PARTY = 1, DOOR_TARGET_NEXT_TO_PARTY };
/* The kinds of the group in front target (AiCandidate.mask). */
enum { GROUP_TARGET_ANY = 1, GROUP_TARGET_CARRIES };
#define SEARCH_FAR 100
#define SEARCH_FAR_RANGE 0x7f
#define PATH_MAP_CHANGE 0x2000
#define PATH_KEEP_BITS 0xc000
/* The move command of the path check of creature_ai_plan_path: the original reads a byte after the path buffer
   that nothing writes. */
#define PATH_CHECK_COMMAND 0
/* AI marker levels: the markers whose id holds the sides of a square that a creature does not enter, and the
   markers of level 2 that count with them while their text shows. */
#define MARKER_LEVEL_SIDES 1
#define MARKER_LEVEL_SIDES_SHOWN 2

AiSquareScan ai_square_scan;
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

/* The first object (not a door, teleporter, text, or sensor) of (x, y) of the current map. */
static uint16_t first_object(int x, int y)
{
    uint16_t t = first_thing(x, y);
    while (thing_type(t) <= THING_SENSOR) t = dungeon_next_thing(t);
    return t;
}

/* A location word of (x, y) of map. */
static uint16_t pack_location(int x, int y, int map)
{
    return (uint16_t)location_pack(x, y, map);
}

static int abs16(int v) { return abs((int16_t)v); }

/* The distance between two squares along the axes. */
static int square_distance(int x1, int y1, int x2, int y2)
{
    return (int16_t)(abs16(x1 - x2) + abs16(y1 - y2));
}

/* A GDAT value of the door type on (x, y). */
static int door_value_at(int x, int y, int key)
{
    return gdat_entry_index(draw_gdat(), CAT_DOOR, dun_get_door_type(first_thing(x, y)), GDAT_TYPE_VALUE, key);
}

/* The search cost of the door on (x, y). */
static int door_pass_cost(int x, int y)
{
    int cost = door_value_at(x, y, DOOR_KEY_PASS_COST);
    if (cost) return cost;
    return door_value_at(x, y, DOOR_KEY_SOMETIMES_SEEN) ? 1 : DOOR_DEFAULT_PASS_COST;
}


static int group_x(void) { return ai.event.data[0]; }
static int group_y(void) { return ai.event.data[1]; }

/* True for the square of the group in the AI context. */
static int at_group(int x, int y)
{
    return (int16_t)x == group_x() && (int16_t)y == group_y();
}

/* True for the party square on the current map. */
static int at_party(int x, int y)
{
    return party.cur_map == ai_party.map && (int16_t)x == ai_party.x && (int16_t)y == ai_party.y;
}

static uint32_t class_flags(void) { return ai_class_flags[ai.class_index]; }

/* The enabled byte of a command; the commands past the table read the data after it in SKULL.EXE. */
static int command_enabled(int command)
{
    command = (uint8_t)command;
    if (command < (int)sizeof ai_command_enabled) return ai_command_enabled[command];
    if (command < COMMAND_AFTER_GLOBALS) {
        const int16_t words[] = {party_distance.map, alt_distance.map, cached_light_sum, cached_explosion_light_sum,
                                  last_party_map, -1};
        return (uint8_t)(words[(command - COMMAND_PAST_TABLE) / 2] >> ((command & 1) * BYTE_BITS));
    }
    if (command < COMMAND_AFTER_GLOBALS + (int)sizeof ai_after_command_table)
        return ai_after_command_table[command - COMMAND_AFTER_GLOBALS];
    return 0;  /* fix: further bytes are other globals of SKULL.EXE */
}

/* Sets the destination of the action to (x, y) of the current map. */
static void set_action_destination(int x, int y)
{
    ai.active->destination = pack_location(x, y, party.cur_map);
}

/* True for squares that the sight of a creature does not cross: walls, closed doors that hide, closed fake walls. */
bool creature_square_is_obstacle(int x, int y)
{
    uint8_t square = dun_get_square(x, y);
    int type = square_type(square);
    if (type == SQUARE_DOOR)
        return (square & SQUARE_DOOR_STATE) == DOOR_STATE_CLOSED && !door_value_at(x, y, DOOR_KEY_SEE_THROUGH);
    return type == SQUARE_WALL || type == SQUARE_UNKNOWN || (type == SQUARE_FAKE_WALL && !(square & SQUARE_FAKE_WALL_OPEN));
}

/* True when a square stops a line of sight or of fire: walls, doors, clouds, and creatures that hide. */
bool creature_square_blocks_line(int x, int y)
{
    int16_t sx = (int16_t)x, sy = (int16_t)y;
    uint8_t square = dun_get_square(sx, sy);
    int type = square_type(square);
    if (type == SQUARE_WALL) return 1;
    if (type == SQUARE_DOOR) {
        int state = square & SQUARE_DOOR_STATE;
        if ((state == DOOR_STATE_THREE_FOURTHS || state == DOOR_STATE_CLOSED) && (!door_value_at(sx, sy, DOOR_KEY_SOMETIMES_SEEN) || !rand_bit()))
            return 1;
    }
    if (type == SQUARE_FAKE_WALL && !(square & SQUARE_FAKE_WALL_OPEN)) return 1;
    if (!(square & SQUARE_HAS_THINGS)) return 0;
    for (uint16_t t = first_object(sx, sy); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        int ttype = thing_type(t);
        if (ttype == THING_EXPLOSION && explosion_type(t) == EXPLOSION_LASTING_CLOUD) return 1;
        if (ttype != THING_GROUP) continue;
        /* the original passes the square variables: a group found next to it moves them */
        uint16_t g = creature_find_group_near_point(&sx, &sy, CELL_ANY);
        if (g == THING_NONE) continue;
        uint16_t flags = creature_group_info(g)->flags;
        if (flags & CREATURE_STATIC) {
            if (creature_door_size(creature_group_info(g)) < DOOR_STATE_HALF) return 1;
        } else if (!(flags & CREATURE_NOT_SEEN_THROUGH)) {
            return 1;
        }
    }
    return 0;
}

/* A line toward the party: the party square blocks it (the line ends there). */
bool creature_square_blocks_line_to_target(int x, int y)
{
    if (at_party(x, y)) return 1;
    return creature_square_blocks_line(x, y);
}

/* A line test that ignores the square of the group in the AI context. */
bool creature_square_blocks_line_except_self(int x, int y)
{
    if ((uint16_t)x == group_x() && (uint16_t)y == group_y()) return 0;
    return creature_square_blocks_line(x, y);
}

/* The slope of the line from (x1, y1) to (x, y) in 1/64, steep: by y; SLOPE_VERTICAL when the main delta is 0. */
static int16_t line_slope(int steep, int x1, int y1, int x, int y)
{
    int16_t major = (int16_t)(steep ? y - y1 : x - x1);
    if (!major) return SLOPE_VERTICAL;
    return (int16_t)(((steep ? x - x1 : y - y1) << SLOPE_SHIFT) / major);
}

/* Walks the squares of the line from (x2, y2) toward (x1, y1); returns 0 when blocked() stops it, else the
   square distance of the ends (1 for adjacent squares). */
int creature_trace_line(int x1, int y1, int x2, int y2, bool (*blocked)(int x, int y))
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
    ai_square.first_thing = ai_square.square & SQUARE_HAS_THINGS ? THING_NONE : THING_ENDOFLIST;
    ai_square.first_object = ai_square.first_group = ai_square.first_thing;
    ai_square_scan.blocking = 0;
    ai_square_scan.passage = 0;
    ai_square_scan.items_elsewhere = -1;
    ai_square.cost = 1;
}

/* The first thing of the square in the AI cache, read once. */
static uint16_t cached_first_thing(void)
{
    if (ai_square.first_thing == THING_NONE) ai_square.first_thing = first_thing(ai_square.x, ai_square.y);
    return ai_square.first_thing;
}

/* The first object of the square in the AI cache, read once. */
uint16_t creature_ai_cached_first_object(void)
{
    if (ai_square.first_object != THING_NONE) return ai_square.first_object;
    uint16_t t = cached_first_thing();
    while (t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR) t = dungeon_next_thing(t);
    return ai_square.first_object = t;
}

/* The first creature group of the square in the AI cache, read once. */
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
    int facing = group_dir(ai.group_thing), turn;
    if (facing == ((dir + 2) & DIR_MASK)) {
        turn = rand_bit() ? 1 : -1;
    } else if (facing == (int16_t)dir) {
        ai.active->action = ACTION_NONE;
        ai.command_result = AI_RESULT_DONE;
        return 0;
    } else {
        turn = facing == ((dir - 1) & DIR_MASK) ? 1 : -1;
    }
    ai.active->facing = (uint8_t)((facing + turn) & DIR_MASK);
    ai.active->action = (uint8_t)(ACTION_TURN_LEFT + (turn != -1));
    ai.command_result = AI_RESULT_ISSUED;
    return 1;
}

/* The search map cell of (x, y). */
static uint8_t *search_cell(uint8_t *map, int x, int y)
{
    return map + (int16_t)x * SEARCH_COLUMN_SIZE + (int16_t)y * SEARCH_CELL_SIZE;
}

/* True when the wall face of (x, y) toward side shows an ornament that is a shooting target of filter. */
static int wall_shows_shoot_target(int x, int y, int side, int filter)
{
    for (uint16_t t = first_thing(x, y); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        int type = thing_type(t);
        if ((type != THING_SENSOR && type != THING_TEXT) || thing_cell(t) != side) continue;
        int ornament = item_gdat_index(t);
        if (ornament != GDAT_NO_INDEX
            && gdat_entry_index(draw_gdat(), CAT_WALL_ORNAMENT, ornament, GDAT_TYPE_VALUE, ORNAMENT_KEY_SHOOT_TARGET) == (uint16_t)filter)
            return 1;
    }
    return 0;
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
        d = DIR_NORTH;
        left = DIR_COUNT - 1;
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
            if (square & SQUARE_HAS_THINGS) {
                if (square_type(square) == SQUARE_WALL) {
                    if (!ranged) break;
                    found = wall_shows_shoot_target(tx, ty, (d + 2) & DIR_MASK, filter);
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
    int back = (d + 2) & DIR_MASK;
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
    int r = creature_ai_try_attack(ranged ? ATTACK_MODE_ANY : ATTACK_MODE_GROUP, sx, sy, tx, ty, (back + 2) & DIR_MASK);
    if (!r) return 0;
    creature_ai_cache_square(sx, sy);
    *out_location = pack_location(tx, ty, party.cur_map);
    return 1;
}

/* The champion that holds an item of FILTER_WEAPON_HANDS in a hand in the cells next to (x, y): its cell, -1 none. */
static int steal_target_cell(int x, int y)
{
    int cell = -1;
    for (int16_t i = 0; i < DIR_COUNT; i++) {
        int c = champ_get_nearest_at(x, y, i);
        if (c == -1) continue;
        uint16_t action = champion(c)->slots[SLOT_ACTION_HAND], ready = champion(c)->slots[SLOT_READY_HAND];
        if (!((action != THING_NONE && creature_thing_matches_filter(action, FILTER_WEAPON_HANDS))
              || (ready != THING_NONE && creature_thing_matches_filter(ready, FILTER_WEAPON_HANDS))))
            continue;
        if ((int16_t)cell < 0 || rand_bit()) cell = champion(c)->cell;
    }
    return cell;
}

/* True when a thing list holds an explosion of a type. */
static int square_has_explosion(uint16_t first, int kind)
{
    for (uint16_t t = first; t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        if (thing_type(t) == THING_EXPLOSION && explosion_type(t) == kind) return 1;
    return 0;
}

/* A random set bit of bits; 0 for none (the original reads the bits as 32-bit numbers). */
static uint32_t random_set_bit(uint32_t bits)
{
    int count = 0;
    for (uint32_t v = bits; v; v >>= 1) count += v & 1;
    int nth = rand_below((uint16_t)count) + 1;
    for (uint32_t bit = 1; bit && bit <= bits; bit <<= 1)
        if (bits & bit && !--nth) return bit;
    return 0;
}

/* Fails the command: with AI_EXECUTE the creature reports the failure. */
static int command_failed(int execute)
{
    if (execute) ai.command_result = AI_RESULT_FAIL;
    return 0;
}

/* True when the creature on (x, y) can step to a side square: then it does not attack its own square. */
static int can_step_off(int x, int y)
{
    for (int16_t d = 0; d < DIR_COUNT; d++)
        if (creature_ai_try_move(MOVE_KIND_PLAIN, x, y, dir_dx[d] + x, (int16_t)(y + dir_dy[d]), d)) return 1;
    return 0;
}

/* True when a creature that avoids doors near the party stands in such a door on (x, y) and stays out of the fight. */
static int party_door_stops_attack(int x, int y)
{
    if (ai_party.map != party.cur_map || !(class_flags() & AI_CLASS_AVOIDS_PARTY_DOORS) || ai.info->flags & CREATURE_NOT_SEEN_THROUGH
        || ((uint16_t)x == group_x() && (uint16_t)y == group_y() && party.cur_map == ai.map))
        return 0;
    return square_type(dun_get_square(x, y)) == SQUARE_DOOR && square_distance(x, y, ai_party.x, ai_party.y) < 2 && rand_2bits();
}

/* The cell of the target that the attack aims at; r: the random bit of the attack variant, a ranged attack may flip it. */
static int attack_cell(int mode, int x, int y, int dir, int melee, int *r)
{
    if (melee) {
        int c = mode > ATTACK_MODE_PARTY_RANGED ? -1 : champ_get_nearest_at(x, y, CELL_ANY);
        return c == -1 ? (*r + 2 + dir) & DIR_MASK : champion(c)->cell;
    }
    if (mode <= ATTACK_MODE_PARTY_RANGED && rand_2bits()) {
        *r = rand_bit();
        int c = *r ? (dir + 2) & DIR_MASK : dir;
        if (champ_get_index_in_cell(c) == -1 && champ_get_index_in_cell((c + 3) & DIR_MASK) == -1) *r = 1 - *r;
    }
    return (dir + *r) & DIR_MASK;
}

/* Sets the action (and the missile) of the chosen attack bit; r selects the variant of the action. */
static void set_attack_action(uint16_t attack, int r)
{
    static const struct { uint16_t attack; uint8_t sub; } shots[] = {
        {ATTACK_FIREBALL, (uint8_t)SPELL_FIREBALL}, {ATTACK_HARM_NON_MATERIAL, (uint8_t)SPELL_HARM_NON_MATERIAL},
        {ATTACK_LIGHTNING, (uint8_t)SPELL_LIGHTNING}, {ATTACK_POISON_CLOUD, (uint8_t)SPELL_POISON_CLOUD},
        {ATTACK_POISON_BOLT, (uint8_t)SPELL_POISON_BOLT}, {ATTACK_SLIME, (uint8_t)SPELL_SLIME}, {ATTACK_PUSH, (uint8_t)SPELL_PUSH},
        {ATTACK_PULL, (uint8_t)SPELL_PULL}
    };
    if (attack == ATTACK_HIT) ai.active->action = ACTION_MELEE;
    else if (attack == ATTACK_HIT_2) ai.active->action = ACTION_MELEE_2;
    else if (attack == ATTACK_STEAL) {
        ai.active->action = ACTION_STEAL;
        ai.active->sub = SHOT_STEAL;
    } else if (attack == ATTACK_THROW) {
        ai.active->action = (uint8_t)(ACTION_THROW + (r != 0));
    } else {
        for (int i = 0; i < (int)(sizeof shots / sizeof shots[0]); i++)
            if (shots[i].attack == attack) {
                ai.active->sub = (int8_t)shots[i].sub;
                ai.active->action = (uint8_t)(ACTION_SHOOT + (r != 0));
            }
    }
}

/* Tests (and with AI_EXECUTE starts) an attack (ATTACK_MODE_*) from (x, y) on (target_x, target_y) (-1: the square
   in dir). */
int creature_ai_try_attack(int mode, int x, int y, int target_x, int target_y, int dir)
{
    int execute = mode & AI_EXECUTE;
    mode &= AI_MODE_MASK;
    uint16_t flags = ai.attack_flags;
    int16_t tx = (int16_t)target_x, ty = (int16_t)target_y;
    if (!flags) return command_failed(execute);
    if (tx == -1) {
        tx = (int16_t)(dir_dx[(int16_t)dir] + x);
        ty = (int16_t)(y + dir_dy[(int16_t)dir]);
    }
    if (!on_map(tx, ty) || ((int16_t)x != tx && (int16_t)y != ty)) return command_failed(execute);
    int16_t distance = (int16_t)square_distance(x, y, tx, ty);
    if (distance > 1 && !(flags &= ATTACK_RANGED_MASK)) return command_failed(execute);
    if (distance == 0 && !(flags &= ATTACK_MELEE_MASK)) return command_failed(execute);
    if (distance > info_attack_range(ai.info)) return command_failed(execute);
    if (distance == 0 && can_step_off(x, y)) return command_failed(execute);
    if (mode <= ATTACK_MODE_PARTY_RANGED) {
        if (!at_party(tx, ty)) return command_failed(execute);
    } else if (mode == ATTACK_MODE_GROUP) {
        creature_ai_cache_square(tx, ty);
        if (creature_ai_cached_first_group() == THING_ENDOFLIST) return command_failed(execute);
    }
    if (distance > 1 && !(int16_t)creature_trace_line(x, y, tx, ty, creature_square_blocks_line)) return command_failed(execute);
    int steal_cell = -1;
    if (flags & ATTACK_STEAL) {
        steal_cell = steal_target_cell(x, y);
        if ((int16_t)steal_cell < 0 && !(flags &= (uint16_t)~ATTACK_STEAL)) return command_failed(execute);
    }
    if (class_flags() & AI_CLASS_AVOIDS_CLOUDS) {
        creature_ai_cache_square(tx, ty);
        if (square_has_explosion(creature_ai_cached_first_object(), EXPLOSION_LASTING_CLOUD) && !(flags &= ATTACK_MELEE_MASK))
            return command_failed(execute);
    }
    if (party_door_stops_attack(x, y)) return command_failed(execute);
    if (!execute) return 1;
    if ((int16_t)dir == -1)
        dir = distance == 0 && at_party(tx, ty) ? (game.party_dir + 2) & DIR_MASK : dun_get_direction_to(x, y, tx, ty);
    if (creature_ai_turn_toward(dir)) return 1;
    int r = rand_bit();
    int melee = distance <= 1 && flags & ATTACK_MELEE_MASK && (!(flags & ATTACK_RANGED_MASK) || rand_bit());
    flags &= melee ? ATTACK_MELEE_MASK : ATTACK_RANGED_MASK;
    int cell = attack_cell(mode, x, y, dir, melee, &r);
    set_attack_action((uint16_t)random_set_bit(flags), r);
    set_action_destination(tx, ty);
    ai.active->direction = (uint8_t)dir;
    if (ai.active->action == ACTION_STEAL) cell = steal_cell;
    ai.active->cell = (uint8_t)cell;
    ai.active->mode = (uint8_t)mode;
    ai.command_result = AI_RESULT_ISSUED;
    return 1;
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

/* The result of the door test: act on the door, the door is as wanted, or a spell is on the way. */
enum { DOOR_TEST_ACT, DOOR_TEST_AS_WANTED, DOOR_TEST_SPELL_ON_WAY };

/* True when the door skills reach the door at distance: a button, a break, or the handle next to it; a button shot or
   a spell from afar in attack range and in sight. */
static int door_skills_reach(uint16_t flags, int mode, uint16_t door, int x, int y, int tx, int ty, int16_t distance)
{
    bool button = door_has_button(door);
    bool by_magic = door_flag(door, DOOR_BREAKABLE_BY_MAGIC);
    if (distance == 1)
        return (flags & DOOR_SKILL_BUTTON && button)
               || (mode == DOOR_MODE_OPEN && ((flags & DOOR_SKILL_SPELL && by_magic) || (flags & DOOR_SKILL_BASH && door_flag(door, DOOR_BREAKABLE))))
               || flags & DOOR_SKILL_HANDLE;
    if (distance > info_attack_range(ai.info)) return 0;
    if (!((flags & DOOR_SKILL_SHOOT_BUTTON && button) || (mode == DOOR_MODE_OPEN && flags & DOOR_SKILL_SPELL && by_magic))) return 0;
    return (int16_t)creature_trace_line(x, y, tx, ty, creature_square_blocks_line) != 0;
}

/* Starts the door action that the skills allow: the handle or button, a button shot, or an attack on the door. */
static int start_door_action(int mode, uint16_t flags, uint16_t door, int x, int y, int tx, int ty, int d, int16_t distance)
{
    if (flags & DOOR_SKILL_MARK) {
        flags &= (uint16_t)~DOOR_SKILL_MARK;
        if (mode == DOOR_MODE_OPEN) door_set_flag(door, DOOR_MARKED, true);
    }
    if (distance > 1) flags &= DOOR_SKILL_SHOOT_BUTTON | DOOR_SKILL_SPELL;
    if (flags & (DOOR_SKILL_HANDLE | DOOR_SKILL_PRESS_BUTTON)
        && (!(flags & (uint16_t)~(DOOR_SKILL_HANDLE | DOOR_SKILL_PRESS_BUTTON)) || rand_2bits())) {
        ai.active->action = ACTION_OPERATE_DOOR;
    } else if (flags & DOOR_SKILL_SHOOT_BUTTON && (!(flags & (uint16_t)~(DOOR_SKILL_HANDLE | DOOR_SKILL_BUTTON)) || !rand_2bits())) {
        ai.active->action = (uint8_t)(ACTION_SHOOT + (rand_bit() != 0));
        ai.active->sub = (int8_t)(mode == DOOR_MODE_OPEN ? (uint8_t)SPELL_DOOR_SET : (uint8_t)SPELL_DOOR_TOGGLE);
    } else {
        uint16_t saved = ai.attack_flags;
        if (flags & DOOR_SKILL_BASH && saved & ATTACK_HIT
            && (!(flags & (uint16_t)~(DOOR_SKILL_HANDLE | DOOR_SKILL_BASH | DOOR_SKILL_BUTTON)) || rand_bit()))
            ai.attack_flags &= ATTACK_HIT;
        else if (!(flags & DOOR_SKILL_SPELL)) return command_failed(AI_EXECUTE);
        else ai.attack_flags &= ATTACK_FIREBALL | ATTACK_LIGHTNING;
        int r = creature_ai_try_attack(AI_EXECUTE | ATTACK_MODE_DOOR, x, y, tx, ty, d);
        ai.attack_flags = saved;
        return r;
    }
    set_action_destination(tx, ty);
    ai.active->facing = (uint8_t)d;
    ai.active->direction = (uint8_t)d;
    ai.active->cell = (uint8_t)((rand_bit() + d) & DIR_MASK);
    ai.active->mode = (uint8_t)mode;
    ai.command_result = AI_RESULT_ISSUED;
    return 1;
}

/* Tests (and with AI_EXECUTE starts) an action on the door at (door_x, door_y) (-1: the square in dir):
   DOOR_MODE_*. */
int creature_ai_try_door_action(int mode, int x, int y, int door_x, int door_y, int dir)
{
    int execute = mode & AI_EXECUTE;
    mode &= AI_MODE_MASK;
    uint16_t flags = (uint16_t)((mode == DOOR_MODE_OPEN ? DOOR_SKILLS_OPEN : DOOR_SKILLS_CLOSE) & ai.door_flags);
    int16_t tx = (int16_t)door_x, ty = (int16_t)door_y, d = (int16_t)dir;
    int state = DOOR_TEST_ACT;
    int16_t distance = 0;
    uint16_t door = THING_NONE;
    if (!flags) return command_failed(execute);
    if (tx == -1) {
        tx = (int16_t)(x + dir_dx[d]);
        ty = (int16_t)(y + dir_dy[d]);
    } else if (d == -1) {
        d = (int16_t)dun_get_direction_to(x, y, tx, ty);
    } else if (tx == (int16_t)x && (int16_t)y == ty) {
        return command_failed(execute);
    }
    if (!on_map(tx, ty) || ((int16_t)x != tx && (int16_t)y != ty)) return command_failed(execute);
    creature_ai_cache_square(tx, ty);
    if (square_type(ai_square.square) != SQUARE_DOOR) return command_failed(execute);
    int door_state = ai_square.square & SQUARE_DOOR_STATE;
    if (mode == DOOR_MODE_OPEN ? door_state == DOOR_STATE_OPEN : door_state == DOOR_STATE_CLOSED) {
        state = DOOR_TEST_AS_WANTED;
    } else if (mode != DOOR_MODE_OPEN && door_state == DOOR_STATE_BROKEN) {
        return command_failed(execute);
    } else {
        door = cached_first_thing();
        bool moving = door_flag(door, DOOR_MOVING), opening = door_flag(door, DOOR_OPENING);
        if (mode == DOOR_MODE_CLOSE_MARKED && !door_flag(door, DOOR_MARKED)) return command_failed(execute);
        if (moving && (opening ? mode == DOOR_MODE_OPEN : mode != DOOR_MODE_OPEN)) {
            state = DOOR_TEST_SPELL_ON_WAY;
        } else {
            if (moving && !opening && !(flags &= (uint16_t)~(DOOR_SKILL_SPELL | DOOR_SKILL_BASH)))
                return command_failed(execute);
            distance = (int16_t)square_distance(x, y, tx, ty);
            if (!door_skills_reach(flags, mode, door, x, y, tx, ty, distance)) return command_failed(execute);
            if (flags & DOOR_SKILL_SHOOT_BUTTON && door_spell_on_line(x, y, distance, d)) state = DOOR_TEST_SPELL_ON_WAY;
        }
    }
    if (state != DOOR_TEST_ACT && mode == DOOR_MODE_CLOSE_MARKED) return command_failed(execute);
    if (!execute) return 1;
    if (d == -1) d = (int16_t)dun_get_direction_to(x, y, tx, ty);
    if (state == DOOR_TEST_AS_WANTED) {
        ai.command_result = AI_RESULT_DONE;
        return 1;
    }
    if (state == DOOR_TEST_SPELL_ON_WAY) {
        ai.active->action = ACTION_NONE;
        ai.command_result = AI_RESULT_ISSUED;
        return 1;
    }
    if (creature_ai_turn_toward(d)) return 1;
    return start_door_action(mode, flags, door, x, y, tx, ty, d, distance);
}

#define MISSILE_WATCH_RANGE 3           /* the squares in which a creature sees the missiles that fly toward it */

/* True when a dangerous projectile flies toward (x, y) within 3 squares in a direction that the creature sees. */
int creature_ai_projectile_incoming(int x, int y)
{
    for (int16_t d = 0; d < DIR_COUNT; d++) {
        if (!(class_flags() & AI_CLASS_SEES_BEHIND)) {
            bool wanders = group_mood_flag(ai.group_thing, MOOD_WANDERS);
            int behind = !(ai.info->flags & CREATURE_FACES_PARTY) && at_group(x, y) && d == ((group_dir(ai.group_thing) + 2) & DIR_MASK);
            if ((wanders || behind) && rand_next() & ROLL_LOOK_AROUND) continue;
        }
        int16_t sx = (int16_t)x, sy = (int16_t)y;
        for (int16_t n = 0; n < MISSILE_WATCH_RANGE; n++) {
            sx += dir_dx[d];
            sy += dir_dy[d];
            if (!on_map(sx, sy)) break;
            for (uint16_t t = first_object(sx, sy); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
                if (thing_type(t) == THING_PROJECTILE && projectile_dir(t) == ((d + 2) & DIR_MASK)
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
    if (state == DOOR_STATE_HALF) ai_square.cost = cost <= DOOR_COST_CHEAP ? 1 : (int16_t)(half - (half >> 1));
    else ai_square.cost = cost <= DOOR_COST_CHEAP ? (int16_t)(cost - 1) : half;
    return 1;
}

/* The quick test of the cost search (MOVE_KIND_COST): may the search enter the square, at what cost. */
static int search_square_test(int command, int x, int y)
{
    creature_ai_cache_square(x, y);
    uint8_t square = ai_square.square;
    switch (square_type(square)) {
    case SQUARE_FAKE_WALL:
        if (square & (SQUARE_FAKE_WALL_OPEN | SQUARE_FLAG_0)) return 1;
        /* fall through: a closed fake wall costs like a wall */
    case SQUARE_WALL: {
        int cost = gdat_entry_index(draw_gdat(), CAT_WALLSET, dungeon_map_wallset(game.party_map),
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
            if (end.map != teleporter_map(link) || square_distance(end.x, end.y, teleporter_x(link), teleporter_y(link)) != 1)
                return 1;
            int map = teleporter_map(link);
            if (map != game.party_map && map != party.alt_view_map) return 1;
            int saved = party.cur_map;
            creature_ai_set_map(map);
            int tx = teleporter_x(link), ty = teleporter_y(link);
            int r = creature_ai_try_move(command, tx, ty, tx, ty, DIR_KEEP_FACING);
            creature_ai_set_map(saved);
            return r;
        }
        ai_square_scan.passage = (uint8_t)dun_get_linked_teleporter_dest(&ai_square_scan.end, x, y);
        if (!ai_square_scan.passage) return 1;
        return ai_square_scan.end.map == game.party_map || ai_square_scan.end.map == party.alt_view_map;
    default: return 0;
    }
}



/* The move kinds that avoid poison clouds and missiles. */
static int is_safe_kind(int kind) { return kind == MOVE_KIND_SAFE || kind == MOVE_KIND_SAFE_FLEE; }

/* The move flags that the square in the AI cache needs; 0: the creature cannot enter it. */
static uint16_t square_move_flags(int kind, int no_movers, int map_changed, int x, int y)
{
    uint8_t square = ai_square.square;
    switch (square_type(square)) {
    case SQUARE_WALL: return MOVE_WALL;
    case SQUARE_FLOOR: return MOVE_FLOOR;
    case SQUARE_PIT:
        if (!(square & SQUARE_PIT_OPEN)) return MOVE_CLOSED_PIT | MOVE_FLOOR;
        if (square & SQUARE_FLAG_0) return MOVE_FALLS | MOVE_INVISIBLE_PIT | MOVE_CLOSED_PIT;
        return map_changed ? MOVE_OPEN_PIT | MOVE_CLOSED_PIT : MOVE_FALLS | MOVE_OPEN_PIT | MOVE_CLOSED_PIT;
    case SQUARE_STAIRS: return MOVE_STAIRS;
    case SQUARE_DOOR: {
        int state = square & SQUARE_DOOR_STATE;
        if ((state == DOOR_STATE_OPEN && !is_safe_kind(kind)) || state == DOOR_STATE_BROKEN) return MOVE_FLOOR;
        if (state == DOOR_STATE_CLOSED) return MOVE_DOOR | MOVE_LOCKED_DOOR;
        uint16_t door = cached_first_thing();
        bool moving = door_flag(door, DOOR_MOVING), opening = door_flag(door, DOOR_OPENING);
        if (no_movers && moving) return opening ? MOVE_FLOOR : MOVE_DOOR | MOVE_LOCKED_DOOR;
        if (moving && !opening) return MOVE_LOCKED_DOOR;
        int limit = door_flag(door, DOOR_VERTICAL) ? creature_door_size(ai.info) : 1;
        return state > limit ? MOVE_DOOR | MOVE_LOCKED_DOOR : MOVE_FLOOR;
    }
    case SQUARE_TELEPORTER:
        if (!(square & SQUARE_TELEPORTER_ON)) {
            if (!dun_get_linked_teleporter_dest(&ai_square_scan.end, x, y)) return MOVE_TELEPORTER | MOVE_FLOOR;
            ai_square_scan.passage = 1;
            return creature_is_native_to_map(ai.group_thing, ai_square_scan.end.map) ? MOVE_LEVELS : 0;
        } else {
            return teleporter_scope(cached_first_thing()) & TELEPORTER_SCOPE_CREATURES ? MOVE_TELEPORTER : MOVE_FLOOR;
        }
    case SQUARE_FAKE_WALL:
        if (square & SQUARE_FAKE_WALL_OPEN) return MOVE_FLOOR;
        return square & SQUARE_FLAG_0 ? MOVE_FAKE_WALL : MOVE_HIDDEN_FAKE_WALL;
    default: return 0;
    }
}

/* The level change of a move up (DIR_LEVEL_UP) or down (DIR_LEVEL_DOWN) from (x, y): sets the action, the facing,
   and the target square on the other map; returns the map, -1 when the creature cannot go. */
static int level_change(int x, int y, int dir, int16_t *action, int16_t *facing, int16_t *tx, int16_t *ty)
{
    uint16_t need = 0, link = THING_NONE;
    uint8_t square = dun_get_square(x, y);
    int type = square_type(square);
    if (type == SQUARE_PIT && dir == DIR_LEVEL_DOWN && square & SQUARE_PIT_OPEN && !(square & SQUARE_FLAG_0)) {
        need = MOVE_OPEN_PIT;
        *action = ACTION_PIT;
        *facing = (int16_t)group_dir(ai.group_thing);
    } else if (type == SQUARE_FLOOR || type == SQUARE_PIT || type == SQUARE_TELEPORTER || type == SQUARE_FAKE_WALL) {
        if (square & SQUARE_LEVEL_LINK) {
            int16_t side = (int16_t)dun_find_adjacent_level_link(x, y, dir == DIR_LEVEL_UP ? LEVEL_UP : LEVEL_DOWN, &link);
            *facing = side;
            if (side >= 0) {
                *action = dir == DIR_LEVEL_UP ? ACTION_LINK_UP : ACTION_LINK_DOWN;
                need = MOVE_STAIRS;
            } else {
                *action = ACTION_FALL_IN_PIT;
                need = MOVE_FALL;
                *facing = (int16_t)group_dir(ai.group_thing);
            }
        }
    } else if (type == SQUARE_STAIRS && (square & SQUARE_STAIRS_UP ? dir == DIR_LEVEL_UP : dir == DIR_LEVEL_DOWN)) {
        *action = square & SQUARE_STAIRS_UP ? ACTION_STAIRS_UP : ACTION_STAIRS_DOWN;
        *facing = (int16_t)((dun_get_stairs_exit_direction(x, y) + 2) & DIR_MASK);
        need = MOVE_STAIRS;
    }
    if (!(ai.move_flags & need)) return -1;
    int map, nx = (int16_t)x, ny = (int16_t)y;
    if (link != THING_NONE && (*action == ACTION_LINK_UP || *action == ACTION_LINK_DOWN)) {
        nx = sensor_target_x(link);
        ny = sensor_target_y(link);
        map = sensor_data(link);
    } else {
        map = dungeon_location_on_map(party.cur_map, dir == DIR_LEVEL_UP ? LEVEL_UP : LEVEL_DOWN, &nx, &ny);
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

/* A move of the group: the squares, the directions, the action, and the result that the test sets for the execution. */
typedef struct {
    int16_t sx, sy, tx, ty, d, action, facing;
    int kind, result, map_changed, attack_group, same;
} Move;

/* The target square and facing of the move, a level change included; returns 0 when the move cannot start. */
static int move_target(Move *m)
{
    if (m->d == DIR_KEEP_FACING) {
        m->facing = (int16_t)group_dir(ai.group_thing);
    } else if (m->d >= DIR_LEVEL_UP) {
        if (!(ai.move_flags & MOVE_LEVELS)) return 0;
        int map = level_change(m->sx, m->sy, m->d, &m->action, &m->facing, &m->tx, &m->ty);
        if (map < 0) return 0;
        m->map_changed = 1;
        creature_ai_set_map(map);
        m->sx = m->sy = -1;
    } else if (m->tx == -1) {
        m->tx = (int16_t)(m->sx + dir_dx[m->d]);
        m->ty = (int16_t)(m->sy + dir_dy[m->d]);
    } else {
        m->same = m->tx == m->sx && m->sy == m->ty;
        if (!m->same && m->d == -1) m->facing = m->d = (int16_t)dun_get_direction_to(m->sx, m->sy, m->tx, m->ty);
    }
    return on_map(m->tx, m->ty);
}

/* The result of a quick test of the search on the target square. */
static int quick_test(Move *m)
{
    if (m->kind == MOVE_KIND_OPEN) {
        creature_ai_cache_square(m->tx, m->ty);
        uint8_t square = ai_square.square;
        return square_type(square) != SQUARE_WALL
               && !(square_type(square) == SQUARE_FAKE_WALL && !(square & (SQUARE_FAKE_WALL_OPEN | SQUARE_FLAG_0)));
    }
    if (m->kind == MOVE_KIND_SIGHT) {
        int r = !creature_square_is_obstacle(m->tx, m->ty);
        if (r) creature_ai_cache_square(m->tx, m->ty);
        return r;
    }
    return search_square_test(m->kind, m->tx, m->ty);
}

/* True when a safe move must avoid the target square: a poison cloud that hurts, a missile, or one on the way. */
static int square_is_dangerous(Move *m)
{
    int cloud = 0;
    for (uint16_t t = creature_ai_cached_first_object(); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        int type = thing_type(t);
        if (type == THING_EXPLOSION) {
            int k = explosion_type(t);
            if (k == EXPLOSION_POISON_CLOUD) {
                if (explosion_get_attack(t, ai.group_thing)) return 1;
            } else if (k == EXPLOSION_LASTING_CLOUD) {
                cloud = 1;
            }
        } else if (type == THING_PROJECTILE && !at_group(m->tx, m->ty)) {
            if (projectile_dir(t) != (m->d == DIR_KEEP_FACING ? m->facing : m->d) && projectile_get_attack(t, thing_word(t, PROJECTILE_PAYLOAD)))
                return 1;
        }
    }
    return m->kind == MOVE_KIND_SAFE_FLEE && !cloud && creature_ai_projectile_incoming(m->tx, m->ty);
}

/* True when a creature that avoids doors near the party must not enter the door square of the move. */
static int party_door_stops_move(Move *m)
{
    return !m->same && ai_party.map == party.cur_map && class_flags() & AI_CLASS_AVOIDS_PARTY_DOORS
           && !(ai.info->flags & CREATURE_NOT_SEEN_THROUGH) && square_type(ai_square.square) == SQUARE_DOOR
           && square_distance(m->tx, m->ty, ai_party.x, ai_party.y) < 2
           && ((uint16_t)m->sx != group_x() || (uint16_t)m->sy != group_y() || rand_2bits());
}

/* A move onto another map or into a teleporter: the move test goes on at the square where the creature arrives. */
static void test_move_transition(Move *m, int no_movers)
{
    move_follow_square_transitions(ai.group_thing, m->sx, m->sy, m->tx, m->ty, 0);
    if (party.cur_map == party.dest_map && m->tx == party.dest_x && m->ty == party.dest_y) return;
    int saved = party.cur_map;
    if (saved != party.dest_map) {
        if (!(ai.move_flags & MOVE_LEVELS) || !creature_is_native_to_map(ai.group_thing, (uint16_t)party.dest_map)) {
            m->result = 0;
            return;
        }
        creature_ai_set_map((uint16_t)party.dest_map);
    }
    m->result = creature_ai_try_move(m->kind | no_movers, (uint16_t)party.dest_x, (uint16_t)party.dest_y,
                                     (uint16_t)party.dest_x, (uint16_t)party.dest_y, DIR_KEEP_FACING);
    creature_ai_set_map(saved);
}

/* A move onto a pit: the creature falls through to the square beyond, or steps into the open pit. */
static void test_move_over_pit(Move *m, int no_movers)
{
    if (m->kind == MOVE_KIND_STEP_BACK) {
        m->result = 0;
    } else {
        uint16_t saved = ai.move_flags;
        ai.move_flags &= MOVE_FLAGS_NO_FALL;
        m->result = creature_ai_try_move(m->kind | no_movers, m->tx, m->ty, (int16_t)(m->tx + dir_dx[m->facing]), (int16_t)(m->ty + dir_dy[m->facing]), m->facing);
        ai.move_flags = saved;
    }
    if (m->result) {
        m->action = ACTION_FALL;
    } else if (ai.move_flags & MOVE_OPEN_PIT) {
        uint16_t saved = ai.move_flags;
        ai.move_flags &= (uint16_t)~MOVE_FALLS;
        m->result = creature_ai_try_move(m->kind | no_movers, m->sx, m->sy, m->tx, m->ty, m->facing);
        if (m->result) m->action = ACTION_PIT;
        ai.move_flags = saved;
    }
    if (!m->result) ai_square_scan.blocking = 1;
}

/* Tests the move; returns 1 when the move continues to the execution, 0 when the result is final. */
static int test_move(Move *m, int command)
{
    int execute = command & AI_EXECUTE, no_movers = command & AI_MOVE_NO_MOVERS, attack_groups = command & AI_MOVE_ATTACK_GROUPS;
    if (m->kind == MOVE_KIND_NONE) return 1;
    int enabled = command_enabled(m->kind);
    if (!enabled && !ai.move_flags) return 1;
    if (!move_target(m)) return 1;
    /* the quick tests of the searches: no wall, no obstacle to the sight, or the cost search */
    if (enabled && (m->kind == MOVE_KIND_OPEN || m->kind == MOVE_KIND_SIGHT || m->kind == MOVE_KIND_COST)) {
        m->result = quick_test(m);
        return 0;
    }
    creature_ai_cache_square(m->tx, m->ty);
    if (ai.attr7 && m->d != DIR_KEEP_FACING) {
        int16_t sides = (int16_t)creature_find_ai_marker(MARKER_LEVEL_SIDES, (uint16_t)ai.attr7);
        if (sides != -1 && (1 << ((m->d + 2) & DIR_MASK)) & sides) return 1;
    }
    if (is_safe_kind(m->kind) && ai.info->defense == CREATURE_NO_HURT) m->kind = MOVE_KIND_PLAIN;
    uint16_t need = square_move_flags(m->kind, no_movers, m->map_changed, m->tx, m->ty) & ai.move_flags;
    if (!need) return 1;
    if (m->kind == MOVE_KIND_KEEP_DISTANCE && abs16(m->tx - ai_party.x) + abs16(m->ty - ai_party.y) <= 1) return 1;
    uint16_t also = at_party(m->tx, m->ty) ? MOVE_PARTY : 0;
    if (!m->same) {
        uint16_t g = creature_ai_cached_first_group();
        if (g != THING_ENDOFLIST && !(no_movers && creature_group_info(g)->movement)
            && (!attack_groups || m->map_changed || !(m->attack_group = creature_thing_matches_filter(g, FILTER_ATTACKED_GROUPS))))
            also |= MOVE_GROUPS;
    }
    if ((ai.move_flags & also) != also) return 1;
    if (is_safe_kind(m->kind) && square_is_dangerous(m)) return 1;
    if (party_door_stops_move(m)) return 1;
    m->result = 1;
    if (m->map_changed || need == MOVE_TELEPORTER) {
        test_move_transition(m, no_movers);
        return 1;
    }
    if (m->same) return 1;
    if (need == MOVE_DOOR) {
        m->result = creature_ai_try_door_action(execute | DOOR_MODE_OPEN, m->sx, m->sy, m->tx, m->ty, m->facing);
        return 0;
    }
    if (need & MOVE_FALLS) test_move_over_pit(m, no_movers);
    return 1;
}

/* Starts the tested move: a failure, an attack on a group in the way, a door action, a turn, or the step. */
static void execute_move(Move *m)
{
    if (!m->result) {
        ai.command_result = AI_RESULT_FAIL;
        return;
    }
    if (m->same) {
        ai.command_result = AI_RESULT_DONE;
        return;
    }
    if (m->attack_group && creature_ai_try_attack(AI_EXECUTE | ATTACK_MODE_GROUP, m->sx, m->sy, m->tx, m->ty, -1)) return;
    if (ai.door_flags & DOOR_SKILL_ANY_SIDE)
        for (int16_t i = 0; i < DIR_COUNT; i++) {
            if (i == m->facing) continue;
            int16_t nx = (int16_t)(m->sx + dir_dx[i]), ny = (int16_t)(m->sy + dir_dy[i]);
            if (square_type(dun_get_square(nx, ny)) == SQUARE_DOOR
                && creature_ai_try_door_action(AI_EXECUTE | DOOR_MODE_CLOSE_MARKED, m->sx, m->sy, nx, ny, i))
                return;
        }
    if (m->kind == MOVE_KIND_STEP_BACK) {
        m->facing = (int16_t)((m->facing + 2) & DIR_MASK);
        m->action = ACTION_TURN_BACK;
    } else if (m->action == -1) {
        m->action = abs16(m->sx - m->tx) + abs16(m->sy - m->ty) <= 1 ? ACTION_MOVE_NEAR : ACTION_MOVE;
    }
    int current = group_dir(ai.group_thing);
    if (current != m->facing && (current == ((m->facing + 2) & DIR_MASK) || m->action != ACTION_MOVE_NEAR)) {
        if (is_safe_kind(m->kind) && !(class_flags() & AI_CLASS_SEES_ALL_SIDES) && rand_2bits()) {
            m->action = ACTION_TURN_BACK;
        } else {
            creature_ai_turn_toward(m->facing);
            return;
        }
    } else if (current != m->facing && m->action != ACTION_TURN_BACK) {
        m->action = ((current + 1) & DIR_MASK) == m->facing ? ACTION_STEP_LEFT : ACTION_STEP_RIGHT;
    } else if (ai_action_flag((int8_t)ai.previous_action) & AI_ACTION_FLAG_MOVES && m->kind != MOVE_KIND_STEP_BACK && !m->map_changed) {
        m->action = ACTION_NONE;
    }
    set_action_destination(m->tx, m->ty);
    ai.active->direction = (uint8_t)m->d;
    ai.active->facing = (uint8_t)m->facing;
    ai.active->mode = (uint8_t)m->kind;
    ai.active->action = (uint8_t)m->action;
    ai.command_result = AI_RESULT_ISSUED;
}

/* Tests (and with AI_EXECUTE starts) a move (MOVE_KIND_*) of the group from (x, y) to (dest_x, dest_y) (-1: the
   square in dir); dir DIR_LEVEL_UP and DIR_LEVEL_DOWN change the level, DIR_KEEP_FACING keeps the facing. */
int creature_ai_try_move(int command, int x, int y, int dest_x, int dest_y, int dir)
{
    int saved_map = party.cur_map;
    Move m = {(int16_t)x, (int16_t)y, (int16_t)dest_x, (int16_t)dest_y, (int16_t)dir, -1, (int16_t)dir,
              command & AI_MOVE_KIND_MASK, 0, 0, 0, 0};
    if (test_move(&m, command) && command & AI_EXECUTE) execute_move(&m);
    if (m.map_changed) creature_ai_set_map(saved_map);
    return m.result;
}

/* ---- Items ---- */

/* The cells of the possessions of a static creature seen from side. */
static int possession_cell_mask(uint16_t possessions, int side)
{
    if (!(possessions & STATIC_POSSESSIONS_SHOWN) || possessions & STATIC_POSSESSIONS_ALL) return CELLS_ALL;
    /* fix: for side -1 (the own square) the original reads 64 KB past the table; the port reaches all cells */
    if ((uint16_t)side >= DIR_COUNT) return CELLS_ALL;
    return ai_possession_cells[possessions & POSSESSIONS_KIND_MASK][side];
}

#define POSSESSION_CELL_BASE 8          /* a found item in the possessions of a group: this plus its cell */

/* An item of filter inside a chest or in the possessions of a group that the creature may take from: the chest
   cell (0 to 3), POSSESSION_CELL_BASE plus the cell of the possession, -1 none. */
int creature_find_matching_item_inside(uint16_t thing, int filter, int side)
{
    int16_t found_cell;
    uint16_t list;
    int mask = CELLS_ALL;
    if (ai.item_flags & ITEM_SKILL_CHESTS && item_is_chest(thing)) {
        found_cell = (int16_t)thing_cell(thing);
        list = thing_word(thing, CONTAINER_CONTENTS);
    } else {
        if (thing_type(thing) != THING_GROUP || !(ai.item_flags & (ITEM_SKILL_STATIC | ITEM_SKILL_GROUPS))) return -1;
        const CreatureInfo *info = creature_group_info(thing);
        int is_static = info->flags & CREATURE_STATIC;
        if (!(ai.item_flags & (is_static ? ITEM_SKILL_STATIC : ITEM_SKILL_GROUPS))) return -1;
        found_cell = POSSESSION_CELL_BASE;
        list = thing_word(thing, GROUP_POSSESSIONS);
        if (is_static) mask = possession_cell_mask(info->possessions, side);
    }
    for (uint16_t t = list; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (!(mask >> thing_cell(t) & 1)) continue;
        if (creature_thing_matches_filter(t, (int16_t)filter) || (int16_t)creature_find_matching_item_inside(t, filter, side) != -1) {
            if (found_cell == POSSESSION_CELL_BASE) found_cell = (int16_t)(POSSESSION_CELL_BASE + thing_cell(t));
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
        if (thing_cell(t) == ai_square_scan.cells[0]) ai_square_scan.facing_ornament = 1;
        else ai_square_scan.items_elsewhere = 1;
    }
    ai_square.first_object = t;
}

/* True when the alcove of the square in the AI cache holds an item of filter in cell, or a chest with one. */
static int alcove_has_item(int16_t cell, int d, int filter)
{
    for (uint16_t t = ai_square.first_object; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        int type = thing_type(t);
        if ((uint16_t)thing_cell(t) != (uint16_t)cell || type == THING_GROUP || type >= THING_PROJECTILE) continue;
        if (creature_thing_matches_filter(t, (int16_t)filter) || (int16_t)creature_find_matching_item_inside(t, filter, d) != -1)
            return 1;
    }
    return 0;
}

/* Finds an item of filter on the square in the AI cache in a cell that the creature reaches, or in the possessions of
   a group there (action: take from the group); returns 0 for none. */
static int floor_item_in_reach(int same, int d, int filter, int16_t *action, int16_t *cell)
{
    for (uint16_t t = creature_ai_cached_first_object(); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        *cell = (int16_t)thing_cell(t);
        int type = thing_type(t);
        if (type < THING_GROUP || type >= THING_PROJECTILE) continue;
        if (!(type != THING_GROUP && creature_thing_matches_filter(t, (int16_t)filter))) {
            *cell = (int16_t)creature_find_matching_item_inside(t, filter, d);
            if (*cell == -1) continue;
        }
        if ((uint16_t)*cell >= POSSESSION_CELL_BASE) {
            *action = ACTION_TAKE_FROM_GROUP;
            *cell &= 3;
            return 1;
        }
        if ((uint8_t)ai_square_scan.cells[0] == CELL_ANY) {
            /* the cells in reach: the near half of the square, or all of the own square */
            int bits = same ? (d == -1 ? CELLS_ALL : CELLS_NEAR) : CELLS_FAR, c = same && d == -1 ? 0 : d;
            for (int i = 0; i < DIR_COUNT; i++) {
                ai_square_scan.cells[c] = (int8_t)(bits & 1);
                c = (c + 1) & DIR_MASK;
                bits >>= 1;
            }
        }
        if (ai_square_scan.cells[(uint16_t)*cell]) return 1;
        ai_square_scan.items_elsewhere = 1;
    }
    return 0;
}

/* A random cell of the possessions of the group in the AI cache that the creature reaches from side d; -1 none. */
static int16_t give_cell(int d)
{
    uint16_t g = creature_ai_cached_first_group();
    int bits = (uint8_t)possession_cell_mask(creature_group_info(g)->possessions, d);
    if (!bits) return -1;
    int16_t bit = (int16_t)random_set_bit((uint32_t)bits);
    int16_t cell = 0;
    for (; !(bit & 1); bit >>= 1) cell++;
    return cell;
}

/* The alcove of the wall square in the AI cache as the target: the face toward the creature must show it; a take
   needs an item of filter in it. Returns 0 when it does not fit. */
static int wall_item_target(int mode, int same, int d, int filter, int16_t *action, int16_t *cell)
{
    if (ai_square_scan.items_elsewhere == -1) {
        ai_square_scan.items_elsewhere = 0;
        ai_square_scan.facing_ornament = 0;
        if (!same && ai.item_flags & ITEM_SKILL_WALL) scan_wall_ornaments((d + 2) & DIR_MASK);
    }
    if (!ai_square_scan.facing_ornament) return 0;
    if (mode == ITEM_MODE_GIVE) {
        *action = ACTION_PUT_IN_WALL;
        return 1;
    }
    *action = ACTION_TAKE_FROM_WALL;
    *cell = ai_square_scan.cells[0];
    return alcove_has_item(*cell, d, filter);
}

/* The cell and action of a give: into the alcove, to the group in front, or a drop here or ahead; -1 when the group
   has no cell for it. */
static int16_t give_place(int same, int d, int16_t *action)
{
    if (*action == ACTION_PUT_IN_WALL) return (int16_t)d;
    if (*action == ACTION_GIVE) return give_cell(d);
    int r = rand_bit();
    if (!same) r += 2;
    *action = take_actions[r];
    return (int16_t)((d + r) & DIR_MASK);
}

/* The take action by the side of the cell; on its own square the creature turns to the cell first when its class
   does. Returns 1 when it turns instead. */
static int take_floor_action(int same, int16_t cell, int16_t *action)
{
    int side = (cell - ai.active->facing + 1) & DIR_MASK;
    if (!same) {
        *action = side < 2 ? ACTION_TAKE_FRONT : ACTION_TAKE_SIDE;
        return 0;
    }
    *action = (int16_t)(ACTION_TAKE_LEFT + (side >= 2));
    if (side == 0) ai.active->facing = (uint8_t)((ai.active->facing - 1) & DIR_MASK);
    else if (side == 3) ai.active->facing = (uint8_t)((ai.active->facing + 1) & DIR_MASK);
    if (class_flags() & AI_CLASS_TURNS_TO_TAKE && ai.active->facing != group_dir(ai.group_thing)) {
        creature_ai_turn_toward(ai.active->facing);
        return 1;
    }
    return 0;
}

/* Tests (and with AI_EXECUTE starts) taking or giving (ITEM_MODE_*) an item of filter at (dest_x, dest_y) (-1: the
   square in dir): from the floor, an alcove, a chest, or a group. */
int creature_ai_try_take_item(int mode, int x, int y, int dest_x, int dest_y, int dir, int filter)
{
    int execute = mode & AI_EXECUTE;
    mode &= AI_MODE_MASK;
    int16_t tx = (int16_t)dest_x, ty = (int16_t)dest_y, d = (int16_t)dir, action = -1, cell = 0;
    int same = 0;
    if (!ai.item_flags) return command_failed(execute);
    if (tx == -1) {
        tx = (int16_t)(dir_dx[d] + x);
        ty = (int16_t)(dir_dy[d] + y);
    } else {
        same = (int16_t)x == tx && (int16_t)y == ty;
        if (!same && d == -1) d = (int16_t)dun_get_direction_to(x, y, tx, ty);
    }
    creature_ai_cache_square(tx, ty);
    if (mode == ITEM_MODE_TAKE && !(ai_square.square & SQUARE_HAS_THINGS)) return command_failed(execute);
    if (square_type(ai_square.square) == SQUARE_WALL) {
        if (!wall_item_target(mode, same, d, filter, &action, &cell)) return command_failed(execute);
    } else {
        if (ai_square_scan.items_elsewhere == -1) {
            ai_square_scan.items_elsewhere = 0;
            ai_square_scan.cells[0] = -1;
        }
        if (!same && at_party(tx, ty)) return command_failed(execute);
        if (!(ai.item_flags & (same ? ITEM_SKILL_SAME_SQUARE : ITEM_SKILL_OTHER_SQUARE))) return command_failed(execute);
        if (mode == ITEM_MODE_GIVE) {
            if (!same) action = ACTION_GIVE;
        } else if (!floor_item_in_reach(same, d, filter, &action, &cell)) {
            return command_failed(execute);
        }
    }
    if (!execute) return 1;
    if (d == -1) d = (int16_t)group_dir(ai.group_thing);
    if (!same && creature_ai_turn_toward(d)) return 1;
    ai.active->facing = (uint8_t)d;
    if (mode == ITEM_MODE_GIVE) {
        cell = give_place(same, d, &action);
        if (cell == -1) return command_failed(execute);
    } else if (action == -1 && take_floor_action(same, cell, &action)) {
        return 1;
    }
    ai.active->destination = pack_location(ai_square.x, ai_square.y, ai_square.map);
    ai.active->cell = (uint8_t)cell;
    ai.active->sub = (int8_t)filter;
    ai.active->mode = (uint8_t)mode;
    ai.active->action = (uint8_t)action;
    ai.command_result = AI_RESULT_ISSUED;
    return 1;
}

/* The wall actuator in cell of a square thing list for mode: the first slot that takes filter, else the last
   actuator of the cell; THING_NONE for none. */
uint16_t creature_find_wall_actuator_in_cell(uint16_t first_thing, int cell, int mode, int filter)
{
    uint16_t found = THING_NONE;
    for (uint16_t t = first_thing; t != THING_ENDOFLIST && t != THING_NONE; t = dungeon_next_thing(t)) {
        int type = thing_type(t);
        if (type > THING_SENSOR) break;
        if (type != THING_SENSOR || thing_cell(t) != (int16_t)cell) continue;
        int kind = sensor_type(t);
        if (kind == SENSOR_NONE || kind == SENSOR_CREATURE_SLOT) continue;
        found = t;
        if (kind != ACTUATOR_SLOT) continue;
        int wanted = sensor_flag(t, SENSOR_STATE) ? ACTUATOR_MODE_TAKE : ACTUATOR_MODE_PUT;
        if ((int16_t)mode == wanted && creature_thing_matches_filter(t, (int16_t)filter)) break;
    }
    return found;
}

/* True when the wall actuator reacts to the creature in mode (ACTUATOR_MODE_*) with an item of filter; state: the
   state bits of the creature slot. */
int creature_wall_actuator_accepts(uint16_t actuator, int mode, int filter, int state)
{
    if (actuator == THING_ENDOFLIST || actuator == THING_NONE) return 0;
    int kind = sensor_type(actuator);
    mode = (int16_t)mode;
    switch (kind) {
    case ACTUATOR_SWITCH:
    case ACTUATOR_BUTTON:
    case ACTUATOR_BUTTON_2: return mode == ACTUATOR_MODE_USE;
    case ACTUATOR_ANY_ITEM: return mode == ACTUATOR_MODE_USE && (int16_t)filter != -1;
    case ACTUATOR_ITEM: return mode == ACTUATOR_MODE_USE && creature_thing_matches_filter(actuator, (int16_t)filter);
    case ACTUATOR_TOGGLE: return mode == ACTUATOR_MODE_USE && (int16_t)state != !sensor_flag(actuator, SENSOR_STATE);
    case ACTUATOR_SLOT:
        if (sensor_flag(actuator, SENSOR_STATE) ? mode != ACTUATOR_MODE_TAKE : mode != ACTUATOR_MODE_PUT) return 0;
        return creature_thing_matches_filter(actuator, (int16_t)filter);
    case ACTUATOR_COUNTED_ITEM:
        if (mode != ACTUATOR_MODE_PUT || !sensor_data(actuator)) return 0;
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
    if (!(ai.actuator_flags & ACTUATOR_SKILL_USE)) return command_failed(execute);
    if (tx == -1) {
        tx = (int16_t)(dir_dx[d] + x);
        ty = (int16_t)(dir_dy[d] + y);
    } else if ((int16_t)x == tx && (int16_t)y == ty) {
        return command_failed(execute);
    }
    if (!on_map(tx, ty) || ((int16_t)x != tx && (int16_t)y != ty)) return command_failed(execute);
    if (d == -1) d = (int16_t)dun_get_direction_to(x, y, tx, ty);
    creature_ai_cache_square(tx, ty);
    if (!(ai_square.square & SQUARE_HAS_THINGS)) return command_failed(execute);
    uint16_t slot = THING_NONE;
    for (uint16_t t = cached_first_thing(); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR; t = dungeon_next_thing(t)) {
        if (thing_type(t) == THING_SENSOR && thing_cell(t) == ((d + 2) & DIR_MASK)
            && sensor_type(t) == SENSOR_CREATURE_SLOT) {
            slot = t;
            break;
        }
    }
    if (slot == THING_NONE || sensor_data(slot) != group_creature(ai.group_thing) || !sensor_flag(slot, SENSOR_STATE))
        return command_failed(execute);
    uint16_t actuator = creature_find_wall_actuator_in_cell(cached_first_thing(), (d + 2) & DIR_MASK, mode, filter);
    if (actuator == THING_NONE) return command_failed(execute);
    int slot_state = sensor_effect(slot);
    if (!creature_wall_actuator_accepts(actuator, mode, filter, slot_state)) return command_failed(execute);
    if (execute && !creature_ai_turn_toward(d)) {
        set_action_destination(tx, ty);
        ai.active->facing = (uint8_t)d;
        ai.active->direction = (uint8_t)d;
        ai.active->sub = (int8_t)filter;
        ai.active->mode = (uint8_t)mode;
        ai.active->action = (uint8_t)(ACTION_USE_ACTUATOR + (mode > ACTUATOR_MODE_TAKE ? ACTUATOR_MODE_TAKE : mode));
        ai.command_result = mode == ACTUATOR_MODE_USE && slot_state < SLOT_STATE_BUSY ? AI_RESULT_ISSUED : AI_RESULT_DONE;
    }
    return 1;
}

#define FILTER_INVERT 0x80
#define FILTER_MASK 0x3f
#define FILTER_ANY 0x3f
#define FILTER_FULL_MONEYBOX 0x3e
#define FILTER_EMPTY_MONEYBOX 0x29
#define FILTER_EMPTY_MONEYBOX_ELSE 7    /* the filter of filter 0x29 for a thing that is not a money box */
#define FILTER_BY_VARIANT 0x10          /* filters 0x10 to 0x12: one set for each of three variants of the group */
#define FILTER_BY_VARIANT_END 0x13
#define FILTER_VARIANTS 3
#define FILTER_MONEYBOX_OR_VARIANT 0x28

/* The item class bitmap of filter for the group in the AI context: parsed once per update into the scratch block.
   Returns NULL when the creature has no class text for it. */
static uint8_t *filter_bitmap(int filter, int creature_type, int is_group)
{
    static uint8_t uncached[FILTER_BITMAP_SIZE];
    if (filter >= FILTER_SLOTS || !ai.scratch) {
        /* fix: the original reads pointers after its table of FILTER_SLOTS filters, or writes the bitmap to address 0
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
        return item_is_moneybox(thing) && !moneybox_is_placed(thing) ? !invert : invert;
    if (f == FILTER_EMPTY_MONEYBOX) {
        if (item_is_moneybox(thing)) return moneybox_is_placed(thing) ? !invert : invert;
        f = FILTER_EMPTY_MONEYBOX_ELSE;
    }
    int type = thing_type(thing);
    if ((f >= FILTER_BY_VARIANT && f < FILTER_BY_VARIANT_END) || f == FILTER_MONEYBOX_OR_VARIANT) {
        if (f == FILTER_MONEYBOX_OR_VARIANT) {
            if (item_is_moneybox(thing) && !moneybox_is_placed(thing)) return !invert;
            f = FILTER_BY_VARIANT;
        }
        f = (int16_t)(f + (int16_t)thing_word(ai.group_thing, GROUP_ITEM_CLASS) * FILTER_VARIANTS);
    }
    uint8_t *bitmap = filter_bitmap(f, group_creature(ai.group_thing), type == THING_GROUP);
    if (!bitmap) return 0;
    int16_t value;
    if (type == THING_GROUP) {
        value = group_creature(thing);
    } else if (type == THING_SENSOR) {
        int kind = sensor_type(thing);
        if (kind == ACTUATOR_COUNTED_ITEM && !sensor_data(thing)) return 0;
        if (kind == ACTUATOR_SLOT || kind == ACTUATOR_COUNTED_ITEM)
            value = (int16_t)gdat_entry_index(draw_gdat(), CAT_WALL_ORNAMENT, item_gdat_index(thing), GDAT_TYPE_VALUE, ORNAMENT_KEY_TARGET);
        else
            value = (int16_t)kind;
    } else if (type >= THING_WEAPON && type <= THING_MISC) {
        value = (int16_t)item_get_index(thing);
    } else {
        return 0;
    }
    /* fix: the original reads outside the bitmap for a negative or large value */
    if (value < 0 || value >= FILTER_BITMAP_SIZE * BYTE_BITS) return invert;
    return bitmap[value / BYTE_BITS] >> (value % BYTE_BITS) & 1 ? !invert : invert;
}

/* True for the thing types that creatures carry. */
static int is_item(int type) { return (type > THING_GROUP && type < THING_PROJECTILE) || type == THING_CONTAINER; }

/* The first item of filter in a thing list (cell CELL_ANY: any cell), THING_ENDOFLIST for none. */
uint16_t creature_find_matching_item(int filter, uint16_t first_thing, int cell)
{
    uint16_t t = first_thing;
    for (; t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        if (is_item(thing_type(t)) && ((uint8_t)cell == CELL_ANY || (uint8_t)cell == thing_cell(t))
            && creature_thing_matches_filter(t, (int16_t)filter))
            break;
    return t;
}

/* Removes and frees the items of filter from a thing list (cell CELL_ANY: any cell); the matching items inside a
   chest go too, and a matching chest gives its other contents to the list. */
void creature_remove_matching_items(uint16_t *list_head, int filter, int cell)
{
    cell = (uint8_t)cell;
    uint16_t next;
    for (uint16_t t = *list_head; t != THING_ENDOFLIST; t = next) {
        next = dungeon_next_thing(t);
        int type = thing_type(t);
        if (!is_item(type) || (cell != CELL_ANY && cell != thing_cell(t))) continue;
        int match = creature_thing_matches_filter(t, (int16_t)filter);
        if (type == THING_CONTAINER && (!item_is_moneybox(t) || match)) {
            uint16_t *contents = thing_list(t, CONTAINER_CONTENTS);
            creature_remove_matching_items(contents, filter, CELL_ANY);
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
            set_thing_word(t, THING_NEXT, THING_NONE);
        }
    }
}

/* The number of items of filter in a thing list (cell CELL_ANY: any cell), with the possessions of groups and the
   contents of chests on request. */
int creature_count_matching_items(int filter, uint16_t first_thing, int cell, int into_groups, int into_containers)
{
    int count = 0;
    cell = (uint8_t)cell;
    for (uint16_t t = first_thing; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        int type = thing_type(t);
        if ((into_groups && type == THING_GROUP) || (into_containers && item_is_chest(t)))
            count += creature_count_matching_items(filter, thing_word(t, CONTAINER_CONTENTS), cell, into_groups, into_containers);
        if (type > THING_GROUP && type < THING_PROJECTILE && (cell == CELL_ANY || cell == thing_cell(t))
            && creature_thing_matches_filter(t, (int16_t)filter))
            count++;
    }
    return count;
}

/* The AI marker text on the square in the AI cache for the level delta (1: also the markers of level 2 with bit 0)
   whose kind is in id_mask: its id (0 to 31), -1 none. */
int creature_find_ai_marker(int level_delta, uint16_t id_mask)
{
    int16_t other = (int16_t)level_delta == MARKER_LEVEL_SIDES ? MARKER_LEVEL_SIDES_SHOWN : -1;
    for (uint16_t t = cached_first_thing(); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_TEXT) continue;
        if (text_kind(t) != TEXT_KIND_AI_MARKER) continue;
        int level = ai_marker_level(t);
        if ((level == (int16_t)level_delta || (level == other && text_is_visible(t))) && (1 << ai_marker_kind(t)) & id_mask)
            return ai_marker_id(t);
    }
    return -1;
}

/* The AI marker text on the square in the AI cache for the level delta with kind id: its id, -1 none. */
int creature_find_ai_marker_by_id(int level_delta, int id)
{
    for (uint16_t t = cached_first_thing(); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_TEXT) continue;
        if (text_kind(t) == TEXT_KIND_AI_MARKER && ai_marker_level(t) == (int16_t)level_delta && ai_marker_kind(t) == (int16_t)id)
            return ai_marker_id(t);
    }
    return -1;
}

/* ---- The path search ---- */

/* The state of a search (the locals of the original). */
typedef struct {
    int command, enabled, count;
    AiCandidate *cands;
    int8_t (*marks)[MARK_SIZE];     /* per candidate: a target square (x, y, map) and the best distance */
    uint8_t *maps[SEARCH_MAPS];     /* the search map of each map, NULL until the search enters it */
    uint8_t *map;                   /* ... of the current map */
    struct { uint8_t x, y, map; } queue[SEARCH_QUEUE_SIZE];
    int head, tail;
    uint8_t counts[SEARCH_DISTANCES]; /* the cost search: queued squares by distance */
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

/* Steps the shift register that picks the first search direction. */
static int next_direction_bits(void)
{
    search_direction_bits = search_direction_bits & 1 ? (search_direction_bits >> 1) ^ SEARCH_DIRECTION_TAPS : search_direction_bits >> 1;
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
        dark = view_light_level && save_magic_light() < 0
               && !(ai.info->flags & CREATURE_SEES_IN_DARK);
        if (!dark) return;
    }
    int16_t dist = ai.map == ai_party.map ? (int16_t)square_distance(group_x(), group_y(), ai_party.x, ai_party.y)
                                          : far_party_distance(group_x(), group_y(), ai.map, 0);
    uint32_t r = rand_next();
    if (!(r & ROLL_CHECK_ADJACENT)) {
        if (dist == 0) return;
        if (dist == 1 && dun_get_direction_to(group_x(), group_y(), ai_party.x, ai_party.y) == group_dir(ai.group_thing)) return;
    }
    int hide = dist > HIDDEN_DISTANCE;
    if (!hide) {
        if (invisible && ((int)(r & ROLL_SIGHT) - (HIDDEN_DISTANCE - dist) > info_sight(ai.info) || r & ROLL_INVISIBLE_HIDDEN)) hide = 1;
        else if (dark && dist * view_light_level - (int)(r >> ROLL_DARK_SHIFT & ROLL_DARK_MASK) > info_sight(ai.info)) hide = 1;
    }
    if (hide) c->cost = r & ROLL_KEEP_COST ? -1 : (int8_t)(c->cost >> 3);
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
        /* fix: the original reads past ai_kind_flags for the kinds after it */
        uint8_t flags = kind < sizeof ai_kind_flags ? ai_kind_flags[kind] : 0;
        c->unknown_10 = flags;
        s->or_flags |= flags;
        uint16_t home = thing_word(ai.group_thing, GROUP_HOME);
        switch (kind) {
        case TARGET_HOME:
            s->home_x = location_x(home);
            s->home_y = location_y(home);
            s->home_map = location_map(home);
            break;
        case TARGET_ACTIVATION:
            s->act_x = location_x(ai.active->position);
            s->act_y = location_y(ai.active->position);
            s->act_map = location_map(ai.active->position);
            /* the original compares the position with the party position in another order */
            if (s->act_x == ai_party.map && s->act_y == ai_party.x && s->act_map == ai_party.y)
                c->unknown_10 = flags ^ (KIND_TESTS_OPEN | KIND_TESTS_BLOCKED);
            break;
        case TARGET_FLEE: {
            int16_t d = party.cur_map == ai_party.map ? (int16_t)square_distance(group_x(), group_y(), ai_party.x, ai_party.y) : SEARCH_FAR;
            m[MARK_BEST] = (int8_t)max16(d, (int16_t)c->item);
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
            m[MARK_BEST] = (int8_t)(m[MARK_MAP] == party.cur_map ? max16(1, square_distance(group_x(), group_y(), m[MARK_X], m[MARK_Y]))
                                                                 : SEARCH_FAR);
            break;
        case TARGET_LIGHT:
            ai.move_flags = MOVE_FLAGS_LIGHT;
            light_source_sum = explosion_light_sum = 0;
            break;
        case TARGET_AI_MARKER: m[MARK_BEST] = SEARCH_FAR_RANGE; break;
        case TARGET_PARTY_DISTANCE: ai.move_flags = MOVE_FLAGS_PARTY_MAP; break;
        default: break;
        }
        if (kind == TARGET_HOME || kind == TARGET_ACTIVATION || kind == TARGET_MARKER || kind == TARGET_NEAR_MARKER)
            s->locations = 1;
        if (!s->enabled && flags & KIND_PARTY_LOCATION) hide_party_target(c);
        s->max_cost = max16(s->max_cost, c->cost + (c->unknown_10 & KIND_TESTS_BLOCKED ? 1 : 0));
    }
}

/* True when the search square, or the other side of its passage, is the target square. */
static int at_place(Search *s, int map, int x, int y, int16_t tx, int16_t ty, int16_t tmap)
{
    return ((int16_t)map == tmap && (int16_t)x == tx && (int16_t)y == ty)
           || (s->passage_map == tmap && s->passage_x == tx && s->passage_y == ty);
}

/* A target of a search mark: the marked square, or its other side. */
static int at_mark(Search *s, const int8_t *m)
{
    return at_place(s, s->check_map, s->x, s->y, m[MARK_X], m[MARK_Y], m[MARK_MAP]);
}

/* True for a candidate at the start, or by a chance of 1 in 8. */
static int lucky_step(const AiCandidate *c)
{
    return c->path_length <= 0 || !(next_direction_bits() & ROLL_LUCKY);
}

/* Writes the distance of the square into a party distance map (or adds bits with add). */
static void mark_distance(Search *s, DistanceMap *d, uint8_t value, int add)
{
    uint8_t *cell;
    if (s->check_map == d->map) cell = &d->cells[s->x * MAP_SIDE_MAX + s->y];
    else if (s->passage_map >= 0 && s->passage_map == d->map) cell = &d->cells[s->passage_x * MAP_SIDE_MAX + s->passage_y];
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

/* The party view target on an entered square: the party square, in front of the party in sight, or at a distance. */
static int entered_party_view(const AiCandidate *c, int16_t x, int16_t y)
{
    if (c->item == VIEW_ON_PARTY) return at_party(x, y);
    if (c->item == VIEW_IN_FRONT) {
        if (dun_get_direction_to(ai_party.x, ai_party.y, x, y) != ai_party.dir) return 0;
        int16_t d = (int16_t)creature_trace_line(ai_party.x, ai_party.y, x, y, creature_square_is_obstacle);
        return d > 0 && d <= (int16_t)c->mask;
    }
    if (c->item == VIEW_AT_DISTANCE) {
        if (square_distance(x, y, ai_party.x, ai_party.y) != (int16_t)c->mask) return 0;
        if (x != ai_party.x && y != ai_party.y) return 1;
        return !(int16_t)creature_trace_line(ai_party.x, ai_party.y, x, y, creature_square_blocks_line_except_self);
    }
    return 0;
}

/* The flee target on an entered square: farther from the party than the best square yet, and out of its sight. */
static int entered_flee(Search *s, const AiCandidate *c, int8_t *m, int16_t x, int16_t y, int on_party_map)
{
    if (c->marker != FOUND_NONE) return at_mark(s, m);
    if (!lucky_step(c)) return 0;
    int16_t d;
    if (on_party_map) {
        d = (int16_t)square_distance(x, y, ai_party.x, ai_party.y);
    } else {
        int shift = dungeon_map_level(s->check_map) != dungeon_map_level(ai_party.map);
        d = far_party_distance(x, y, s->check_map, shift);
    }
    if (m[MARK_BEST] >= d) return 0;
    if (on_party_map && d < FLEE_LINE_DISTANCE && (x == ai_party.x || y == ai_party.y)
        && (int16_t)creature_trace_line(ai_party.x, ai_party.y, x, y, creature_square_blocks_line_except_self))
        return 0;
    m[MARK_BEST] = (int8_t)d;
    return 1;
}

/* The attack test of a candidate with the attacks of its mask. */
static int candidate_attack(const Search *s, const AiCandidate *c, int mode, int x, int y, int tx, int ty, int dir)
{
    ai.attack_flags &= c->mask;
    int r = creature_ai_try_attack(mode, x, y, tx, ty, dir);
    ai.attack_flags = s->saved_attack;
    return r;
}

/* The near marker target on an entered square: nearer to the marked square than the best square yet. */
static int entered_near_marker(Search *s, AiCandidate *c, int8_t *m, int16_t x, int16_t y)
{
    int16_t d = m[MARK_MAP] == s->check_map ? (int16_t)square_distance(x, y, m[MARK_X], m[MARK_Y]) : m[MARK_BEST];
    if (m[MARK_BEST] <= d) {
        if (m[MARK_MAP] != s->passage_map) return 0;
        d = (int16_t)square_distance(s->passage_x, s->passage_y, m[MARK_X], m[MARK_Y]);
        if (d >= m[MARK_BEST]) return 0;
    }
    m[MARK_BEST] = (int8_t)d;
    if (!(uint8_t)d) c->unknown_10 &= (uint8_t)~KIND_FIRST_BEST;
    return 1;
}

/* The AI marker target on an entered square: a marker of the kind with a lower id than the best one yet. */
static int entered_ai_marker(AiCandidate *c, int8_t *m)
{
    if (!(ai_square.square & SQUARE_HAS_THINGS)) return 0;
    int16_t id = (int16_t)creature_find_ai_marker_by_id((int16_t)c->item, (int16_t)c->mask);
    if (id == -1 || id >= m[MARK_BEST]) return 0;
    if (id && m[MARK_BEST] >= SEARCH_FAR_RANGE && c->cost) {
        m[MARK_BEST] = (int8_t)id;
        return 0;
    }
    c->unknown_10 &= (uint8_t)~KIND_FIRST_BEST;
    return 1;
}

/* The damage group target: a group of the filter on (x, y) takes the damage kind of the candidate. */
static int damage_group_at(const AiCandidate *c, int16_t x, int16_t y)
{
    uint16_t g = creature_ai_cached_first_group();
    if (g == THING_ENDOFLIST || !creature_thing_matches_filter(g, (int16_t)c->item)) return 0;
    creature_apply_damage_kind(g, x, y, (int16_t)c->mask);
    return 1;
}

/* The test of a candidate on a square that the search entered. */
static int test_entered(Search *s, int i)
{
    AiCandidate *c = &s->cands[i];
    int8_t *m = s->marks[i];
    int16_t x = s->x, y = s->y;
    int on_party_map = s->check_map == ai_party.map;
    switch ((uint8_t)c->kind) {
    case TARGET_ANY: return 1;
    case TARGET_HOME: return at_place(s, s->check_map, x, y, s->home_x, s->home_y, s->home_map);
    case TARGET_PARTY_VIEW: return entered_party_view(c, x, y);
    case TARGET_ACTIVATION: return at_place(s, s->check_map, x, y, s->act_x, s->act_y, s->act_map);
    case TARGET_PARTY_RANGE_2: return on_party_map && square_distance(x, y, ai_party.x, ai_party.y) == 2;
    case TARGET_FLEE: return entered_flee(s, c, m, x, y, on_party_map);
    case TARGET_SHOOT_PARTY:
    case TARGET_MELEE_PARTY: {
        if (!on_party_map) return 0;
        int mode = c->kind == TARGET_SHOOT_PARTY ? ATTACK_MODE_PARTY_RANGED : ATTACK_MODE_PARTY;
        return candidate_attack(s, c, mode, x, y, ai_party.x, ai_party.y, -1);
    }
    case TARGET_MELEE_LINE:
    case TARGET_SHOOT_LINE:
        if (c->cost != s->dist) return 0;
        return line_target(s, c, x, y, -1, 1);
    case TARGET_STEAL:
        if (!on_party_map || ai_action_flag((int8_t)ai.previous_action) & AI_ACTION_FLAG_BUSY) return 0;
        return candidate_attack(s, c, ATTACK_MODE_PARTY, x, y, -1, -1, group_dir(ai.group_thing));
    case TARGET_MARKER: return at_mark(s, m);
    case TARGET_NEAR_MARKER: return entered_near_marker(s, c, m, x, y);
    case TARGET_WANDER:
        if (c->marker != FOUND_NONE) return at_mark(s, m);
        return s->dist > 0 && lucky_step(c);
    case TARGET_RANDOM: {
        int16_t chance = (int16_t)c->item;
        if (chance < 0) return rand_below((uint16_t)-chance) != 0;
        return chance > 0 && !rand_below((uint16_t)chance);
    }
    case TARGET_ITEM:
    case TARGET_ITEM_2: return creature_ai_try_take_item(ITEM_MODE_TAKE, x, y, x, y, -1, (int16_t)c->item);
    case TARGET_DAMAGE_GROUP:
        if (x == s->start_x && y == s->start_y) return 0;
        return damage_group_at(c, x, y);
    case TARGET_MOVE_TEST: return creature_ai_try_move((uint8_t)c->item, x, y, x, y, -1);
    case TARGET_LIGHT:
        if (ai_square.square & SQUARE_HAS_THINGS) light_add_square_light(s->dist, s->dir == -1 ? game.party_dir : s->dir, x, y, LIGHT_FLOOR | LIGHT_CREATURES);
        return 0;
    case TARGET_AI_MARKER: return entered_ai_marker(c, m);
    case TARGET_MARKER_SIDE: {
        if (s->dir == -1 || !(ai_square.square & SQUARE_HAS_THINGS)) return 0;
        int16_t sides = (int16_t)creature_find_ai_marker(MARKER_LEVEL_SIDES, (uint16_t)ai.attr7);
        return sides != -1 && !((1 << ((s->dir + 2) & DIR_MASK)) & sides);
    }
    case TARGET_PARTY_DISTANCE:
        mark_distance(s, &party_distance, (uint8_t)(s->dist + 1), 0);
        mark_distance(s, &alt_distance, (uint8_t)(s->dist + 1), 0);
        return 0;
    default: return 0;
    }
}

/* The party view target on the party square that the search could not enter: from a side of the mask, or with an
   open pit behind the party. */
static int blocked_party_view(Search *s, const AiCandidate *c, int16_t x, int16_t y)
{
    if (!at_party(x, y)) return 0;
    if (c->item == VIEW_PARTY_SIDE) {
        if (!c->mask || (1 << ((dun_get_direction_to(ai_party.x, ai_party.y, s->from_x, s->from_y) - ai_party.dir) & DIR_MASK)) & c->mask)
            return 1;
        s->visit = VISIT_BLOCKED;
    } else if (c->item == VIEW_PARTY_AT_PIT) {
        uint8_t square = dun_get_square(x + dir_dx[s->dir], y + dir_dy[s->dir]);
        if (square_type(square) == SQUARE_PIT && square & SQUARE_PIT_OPEN && !(square & SQUARE_FLAG_0)) return 1;
        s->visit = VISIT_BLOCKED;
    }
    return 0;
}

/* The group in front target: a group of the creature type that carries something, or with the party or a moving
   group behind it. */
static int blocked_group_in_front(Search *s, const AiCandidate *c, int16_t x, int16_t y)
{
    uint16_t g = creature_ai_cached_first_group();
    if (g == THING_ENDOFLIST || group_creature(g) != c->item) return 0;
    if (c->mask == GROUP_TARGET_ANY) return 1;
    if (c->mask == GROUP_TARGET_CARRIES) {
        for (uint16_t t = thing_word(g, GROUP_POSSESSIONS); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
            if (thing_type(t) != THING_PROJECTILE && thing_cell(t) != s->dir) return 1;
        return 0;
    }
    int16_t nx = (int16_t)(x + dir_dx[s->dir]), ny = (int16_t)(y + dir_dy[s->dir]);
    uint16_t other;
    int r = at_party(nx, ny)
            || ((other = creature_get_group_at((uint16_t)nx, (uint16_t)ny)) != THING_NONE
                && !(creature_group_info(other)->flags & CREATURE_STATIC));
    if (!r) s->visit = VISIT_BLOCKED;
    return r;
}

/* The door target: a door that the creature can open or close (DOOR_MODE_* in the mask) and that is not yet so. */
static int blocked_door(Search *s, const AiCandidate *c, int16_t x, int16_t y)
{
    if (c->item == DOOR_TARGET_PARTY && !at_party(x, y)) return 0;
    if (!creature_ai_try_door_action((uint8_t)c->mask, s->from_x, s->from_y, x, y, s->dir)) return 0;
    if (c->item == DOOR_TARGET_NEXT_TO_PARTY && square_distance(x, y, ai_party.x, ai_party.y) != 1) return 0;
    creature_ai_cache_square(x, y);
    if (c->mask == DOOR_MODE_OPEN && !(ai_square.square & SQUARE_DOOR_STATE)) return 0;
    if (c->mask == DOOR_MODE_CLOSE && (ai_square.square & SQUARE_DOOR_STATE) == DOOR_STATE_CLOSED) return 0;
    uint16_t door = cached_first_thing();
    if (!door_flag(door, DOOR_MOVING)) return 1;
    if (door_flag(door, DOOR_OPENING)) return c->mask != DOOR_MODE_OPEN;
    return c->mask != DOOR_MODE_CLOSE;
}

/* The test of a candidate on a square that the search could not enter (from the square it left). */
static int test_blocked(Search *s, int i)
{
    AiCandidate *c = &s->cands[i];
    int16_t x = s->x, y = s->y;
    switch ((uint8_t)c->kind) {
    case TARGET_PARTY_VIEW: return blocked_party_view(s, c, x, y);
    case TARGET_ACTIVATION: return at_party(x, y);
    case TARGET_MELEE_LINE:
        if (square_type(ai_square.square) == SQUARE_WALL) return 0;
        /* fall through */
    case TARGET_SHOOT_LINE: return line_target(s, c, s->from_x, s->from_y, s->dir, 0);
    case TARGET_ITEM:
    case TARGET_ITEM_WALL: {
        int r = creature_ai_try_take_item(ITEM_MODE_TAKE, s->from_x, s->from_y, x, y, s->dir, (int16_t)c->item);
        if (!r && ai_square_scan.items_elsewhere) s->visit = VISIT_BLOCKED;
        return r;
    }
    case TARGET_GROUP_FACING:
        if (group_dir(ai.group_thing) != s->dir) return 0;
        /* fall through */
    case TARGET_GROUP_IN_FRONT: return blocked_group_in_front(s, c, x, y);
    case TARGET_DAMAGE_GROUP: return damage_group_at(c, x, y);
    case TARGET_ACTUATOR: {
        if (!(ai_square.square & SQUARE_HAS_THINGS)) return 0;
        int r = creature_ai_try_use_wall_actuator((uint8_t)c->mask, s->from_x, s->from_y, x, y, s->dir, (int16_t)c->item);
        if (!r) s->visit = VISIT_BLOCKED;
        return r;
    }
    case TARGET_LIGHT:
        if (ai_square.square & SQUARE_HAS_THINGS) {
            light_add_square_light(s->dist, s->dir, x, y, LIGHT_WALLS);
            s->visit = VISIT_BLOCKED;
        }
        return 0;
    case TARGET_DOOR: return blocked_door(s, c, x, y);
    case TARGET_PARTY_DISTANCE: {
        uint8_t value = (uint8_t)(s->blocking ? 1 << s->dir | DISTANCE_NEXT_TO : s->dist + 1);
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
        while ((int8_t)s->cands[j].value < 0) {
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
    s->blocking = ai_square_scan.blocking;
    s->visit = (uint8_t)s->dist;
    int i = -1;
    if (s->or_flags & KIND_TESTS_BLOCKED) {
        if (s->or_flags & KIND_CACHES_SQUARE) creature_ai_cache_square(s->x, s->y);
        i = test_candidates(s, 1);
    }
    if (!s->blocking) search_cell(s->map, s->x, s->y)[SEARCH_AT_DIST] = s->visit;
    return i;
}

/* Makes the map of a queued square the current map of the search. */
static void search_enter_map(Search *s, int map)
{
    if (map == party.cur_map) return;
    creature_ai_set_map(map);
    s->map = s->maps[map];
}

enum { SEARCH_STEP_SKIP, SEARCH_STEP_BLOCKED, SEARCH_STEP_ENTERED };

/* Tries the step of the search in s->dir (DIR_LEVEL_UP, DIR_LEVEL_DOWN: the level changes) from the square it left. */
static int search_step(Search *s)
{
    search_enter_map(s, s->from_map);
    if (s->levels && s->steps_left <= 2) {
        if (s->steps_left == 2) {
            if (!s->down_ok) return SEARCH_STEP_SKIP;
            s->dir = DIR_LEVEL_DOWN;
        } else {
            if (!s->up_ok) return SEARCH_STEP_SKIP;
            s->dir = DIR_LEVEL_UP;
        }
        s->x = s->from_x;
        s->y = s->from_y;
    } else {
        s->x = (int16_t)(s->from_x + dir_dx[s->dir]);
        s->y = (int16_t)(s->from_y + dir_dy[s->dir]);
        if (!on_map(s->x, s->y)) return SEARCH_STEP_SKIP;
    }
    ai_square_scan.blocking = 0;
    s->visit = search_cell(s->map, s->x, s->y)[SEARCH_AT_DIST];
    if (!s->visit || s->dir >= DIR_LEVEL_UP) {
        int dir = s->dir;
        if (!creature_ai_try_move(s->command, s->from_x, s->from_y, s->x, s->y, dir)) {
            s->visit = VISIT_BLOCKED;
        } else {
            s->passage_map = -1;
            s->passage_x = s->x;
            s->passage_y = s->y;
            if (ai_square.map != party.cur_map) {
                if (!s->maps[ai_square.map]) s->maps[ai_square.map] = calloc(MAP_SIDE_MAX, SEARCH_COLUMN_SIZE);
                if (dir < DIR_LEVEL_UP) s->passage_map = party.cur_map;
                creature_ai_set_map(ai_square.map);
                s->map = s->maps[party.cur_map];
            } else if (dir < DIR_LEVEL_UP) {
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
    if (s->visit) return s->visit == VISIT_BLOCKED && s->dir < DIR_LEVEL_UP ? SEARCH_STEP_BLOCKED : SEARCH_STEP_SKIP;
    s->x = ai_square.x;
    s->y = ai_square.y;
    if (search_cell(s->map, s->x, s->y)[SEARCH_AT_DIST]) return SEARCH_STEP_SKIP;
    return SEARCH_STEP_ENTERED;
}

/* Takes the next square from the queue; returns 0 when the search ends. */
static int search_next_square(Search *s)
{
    for (;;) {
        if (s->tail == s->head) return 0;
        s->from_x = s->queue[s->head].x;
        s->from_y = s->queue[s->head].y;
        s->from_map = s->queue[s->head].map;
        s->head = (s->head + 1) & SEARCH_QUEUE_MASK;
        search_enter_map(s, s->from_map);
        s->dist = search_cell(s->map, s->from_x, s->from_y)[SEARCH_AT_DIST];
        if (!s->cost_search) break;
        int deferred = 0;
        while (s->level < s->dist) {
            if (s->counts[s->level]) {
                s->queue[s->tail].x = (uint8_t)s->from_x;
                s->queue[s->tail].y = (uint8_t)s->from_y;
                s->queue[s->tail].map = (uint8_t)s->from_map;
                s->tail = (s->tail + 1) & SEARCH_QUEUE_MASK;
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
    if (s->dist > SEARCH_MOVERS_DISTANCE) s->command |= AI_MOVE_NO_MOVERS;
    if (s->flee_once && s->dist > 1) {
        s->command = (s->command & AI_MOVE_FLAG_BITS) | MOVE_KIND_SAFE;
        s->flee_once = 0;
    }
    s->turn = search_direction_bits & 1 ? 1 : -1;
    s->dir = next_direction_bits() & DIR_MASK;
    s->steps_left = s->levels ? SEARCH_STEPS_LEVELS : SEARCH_STEPS;
    return 1;
}

/* Writes the steps from the start to the found candidate into path (2 bytes per step). */
static void search_write_path(Search *s, uint8_t *path)
{
    AiCandidate *c = &s->cands[s->result];
    int x = location_x(c->found), y = location_y(c->found), map = location_map(c->found);
    for (int16_t n = (int16_t)(c->path_length - 1); n >= 0; n--) {
        uint8_t *cell = search_map_cell(s, map, x, y);
        uint16_t from = le16(cell + SEARCH_AT_FROM);
        uint16_t step = le16(path + n * 2) & PATH_KEEP_BITS;
        step |= (uint16_t)((cell[SEARCH_AT_DIR] & PATH_DIR_MASK) | (x & LOCATION_COORD_MASK) << PATH_X_SHIFT
                           | (y & LOCATION_COORD_MASK) << PATH_Y_SHIFT);
        if (location_map(from) != map) {
            map = location_map(from);
            step |= PATH_MAP_CHANGE;
        }
        put_le16(path + n * 2, step);
        x = location_x(from);
        y = location_y(from);
    }
}

/* Starts a search from (x, y) of the current map: the candidates, the level changes, and the first search map. */
static void search_start(Search *s, int x, int y, int command, int count, AiCandidate *candidates)
{
    s->command = (uint8_t)command;
    s->count = (int16_t)count;
    s->cands = candidates;
    s->start_x = (int16_t)x;
    s->start_y = (int16_t)y;
    s->start_map = party.cur_map;
    s->result = -1;
    s->enabled = command_enabled(s->command);
    s->two_maps = !s->enabled && class_flags() & AI_CLASS_STAYS_ON_MAPS;
    s->flee_once = (s->command & AI_MOVE_KIND_MASK) == MOVE_KIND_SAFE_FLEE;
    search_setup(s);
    if (ai.move_flags & MOVE_LEVELS && ai.move_flags & MOVE_LEVEL_FLAGS) {
        s->levels = 1;
        s->up_ok = ai.move_flags & MOVE_DIR4_FLAGS;
        s->down_ok = ai.move_flags & MOVE_DIR5_FLAGS;
    }
    /* fix: the original clears the first search map only for line targets or a cost; the port always clears it */
    s->maps[party.cur_map] = s->map = calloc(MAP_SIDE_MAX, SEARCH_COLUMN_SIZE);
    s->cost_search = (s->command & AI_MOVE_KIND_MASK) == MOVE_KIND_COST;
    if (s->cost_search) s->counts[SEARCH_LEVELS - 1] = SEARCH_LEVEL_BLOCKED;
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
}

int creature_ai_search_area(int x, int y, int command, int count, AiCandidate *candidates, uint8_t *path)
{
    if (!candidates[0].kind) {
        candidates[0].destination = candidates[0].found = pack_location(x, y, party.cur_map);
        candidates[0].path_length = 0;
        return 0;
    }
    Search *s = calloc(1, sizeof *s);
    int8_t marks[SEARCH_MARKS][MARK_SIZE];
    s->marks = marks;
    search_start(s, x, y, command, count, candidates);
    int found = search_entered(s);
    for (;;) {
        if (found >= 0) {
            if (search_found(s, found)) break;
            found = -1;
        }
        s->dir = (int16_t)((s->dir + s->turn) & DIR_MASK);
        if (--s->steps_left && ((s->tail + 1) & SEARCH_QUEUE_MASK) != s->head) {
            int step = search_step(s);
            if (step == SEARCH_STEP_ENTERED) found = search_entered(s);
            else if (step == SEARCH_STEP_BLOCKED) found = search_blocked(s);
            continue;
        }
        if (!search_next_square(s)) break;
        s->dir = (int16_t)((s->dir - s->turn) & DIR_MASK);  /* the first direction has no turn */
    }
    if (s->result != -1) search_write_path(s, path);
    creature_ai_set_map(s->start_map);
    int result = s->result;
    for (int m = 0; m < SEARCH_MAPS; m++) free(s->maps[m]);
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

/* A step of the stored path. */
static uint16_t path_step(int index) { return le16(ai.cached_path + index * 2); }

/* The direction of the next step of the stored path into the action; returns the steps left (0: none). A group
   at the end of its path forgets the found square. */
int creature_ai_next_path_step(void)
{
    ActiveGroup *a = ai.active;
    creature_ai_load_cached_path();
    int left = 0;
    if (ai.cache.path_length && (left = ai.cache.path_left)) {
        a->direction = path_step(ai.cache.path_length - left) & PATH_DIR_MASK;
        return left;
    }
    uint16_t found = ai.cache.found;
    if (ai.map == location_map(found) && group_x() == location_x(found) && group_y() == location_y(found))
        ai.cache.found = FOUND_NONE;
    return left;
}

/* True when the target of the plan is still at the found square and the next steps of the stored path from the
   group on the map saved are free. */
static int path_holds(int saved)
{
    int16_t x = location_x(ai.cache.found), y = location_y(ai.cache.found);
    uint8_t path[AI_PATH_MAX * 2];
    if ((int16_t)creature_ai_search_area(x, y, MOVE_KIND_NONE, 1, ai.candidates, path) || ai.candidates[0].path_length) return 0;
    int16_t cx = (int16_t)group_x(), cy = (int16_t)group_y(), left = ai.cache.path_left;
    if (!left) return cx == x && cy == y && party.cur_map == saved;
    creature_ai_set_map(saved);
    creature_ai_load_cached_path();
    /* fix: without a stored path the original reads its steps at address 0 */
    if (!ai.cached_path) return 0;
    for (int16_t checks = PATH_CHECK_STEPS;;) {
        uint16_t step = path_step(ai.cache.path_length - left);
        if (!creature_ai_try_move(PATH_CHECK_COMMAND, cx, cy, -1, -1, step & PATH_DIR_MASK)) return 0;
        if ((step >> PATH_X_SHIFT & LOCATION_COORD_MASK) != ai_square.x
            || (step >> PATH_Y_SHIFT & LOCATION_COORD_MASK) != ai_square.y
            || (step >> PATH_MAP_CHANGE_SHIFT & 1) != (ai_square.map != party.cur_map))
            return 0;
        creature_ai_set_map(ai_square.map);
        cx = ai_square.x;
        cy = ai_square.y;
        if (--left <= 0 || --checks <= 0) break;
    }
    return 1;
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
    int saved = party.cur_map;
    creature_ai_set_map(location_map(ai.cache.found));
    int result = path_holds(saved);
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
        memset(party_distance.cells, 0, (size_t)party_distance.width * MAP_SIDE_MAX);
        alt_distance.map = party.alt_view_map;
        if (alt_distance.map >= 0) {
            alt_distance.width = (int16_t)dungeon_map_width(alt_distance.map);
            memset(alt_distance.cells, 0, (size_t)alt_distance.width * MAP_SIDE_MAX);
        }
        target.cost = PARTY_MAP_SEARCH_COST;
        target.value = 0;
        target.kind = TARGET_PARTY_DISTANCE;
        creature_ai_search_area(game.party_x, game.party_y, MOVE_KIND_COST, 1, &target, path);
    }
    if (party.redraw & REDRAW_VIEW) {
        int changed = game.party_map != last_party_map;
        int radius = gdat_entry_index(draw_gdat(), CAT_WALLSET, dungeon_map_wallset(game.party_map),
                                      GDAT_TYPE_VALUE, WALLSET_KEY_LIGHT);
        if (!radius) {
            light_source_sum = explosion_light_sum = 0;
        } else {
            if (radius > LIGHT_RADIUS_MAX) radius = LIGHT_RADIUS_MAX;
            target.cost = (int8_t)radius;
            target.value = 0;
            target.kind = TARGET_LIGHT;
            creature_ai_search_area(game.party_x, game.party_y, MOVE_KIND_SIGHT, 1, &target, path);
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
