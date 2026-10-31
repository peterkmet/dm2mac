/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Creature path search: the search of the squares around a creature for the targets of its plan, and the stored paths. */
#include "ai.h"
#include <stdlib.h>
#include <string.h>
#include "ai_internal.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "gdat.h"
#include "le.h"
#include "move.h"
#include "num.h"
#include "os.h"
#include "random.h"
#include "save.h"
#include "weather.h"

/* The GDAT values of a wall set. */
#define WALLSET_KEY_LIGHT 0x6d /* the light radius of the map */

/* The search costs and the light radius of the squares. */
#define LIGHT_RADIUS_MAX      8
#define PARTY_MAP_SEARCH_COST 25 /* the cost of the search of the party distance maps */

/* The flags of the search target kinds (g_ai_kind_flags). */
#define KIND_TESTS_OPEN     0x01 /* the kind tests the squares that the search enters */
#define KIND_TESTS_BLOCKED  0x02 /* ... the squares that it cannot enter */
#define KIND_FIRST_BEST     0x04 /* a found target may still get a nearer better one */
#define KIND_CACHES_SQUARE  0x08
#define KIND_NO_LOCATION    0x10 /* the found target keeps its location */
#define KIND_PARTY_LOCATION 0x20 /* the found target is the party square; also: needs the party seen */
#define KIND_NO_BONUS       0x40

/* The search queue and the search maps. */
#define SEARCH_QUEUE_SIZE      256
#define SEARCH_LEVELS          51  /* the counts of the cost search, the last one blocks */
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
#define PARTY_RANGE_DISTANCE   2 /* TARGET_PARTY_RANGE_2: the distance to the party along the axes */
#define PATH_CHECK_STEPS       5 /* the steps of a stored path that the path check tests */
/* The feedback of the shift register of the first search direction. */
#define SEARCH_DIRECTION_TAPS  0xb400

/* Random bits of the sight of a creature. */
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

uint16_t g_search_direction_bits = 1;

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

/* ---- The path search ---- */

static uint8_t *search_map_cell(Search *s, int16_t map, int16_t x, int16_t y)
{
    return ai_search_cell(s->maps[map], x, y);
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
    return (int16_t)(ai_manhattan(
                         (Location){(int16_t)(dungeon_map_offset_x(map) + x), (int16_t)(dungeon_map_offset_y(map) + y)},
                         (Location){(int16_t)(dungeon_map_offset_x(tm) + g_ai_party.x),
                                    (int16_t)(dungeon_map_offset_y(tm) + g_ai_party.y)})
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
    int16_t dist = g_ai.map == g_ai_party.map
                       ? ai_manhattan((Location){ai_own_x(), ai_own_y()}, (Location){g_ai_party.x, g_ai_party.y})
                       : far_party_distance(ai_own_x(), ai_own_y(), g_ai.map, false);
    uint32_t r = rand_next();
    if (!(r & ROLL_CHECK_ADJACENT)) {
        if (dist == 0)
            return;
        if (dist == 1 &&
            dungeon_direction_to(ai_own_x(), ai_own_y(), g_ai_party.x, g_ai_party.y) == group_dir(g_ai.group_thing))
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
        int16_t d = g_current_map.map == g_ai_party.map
                        ? ai_manhattan((Location){ai_own_x(), ai_own_y()}, (Location){g_ai_party.x, g_ai_party.y})
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
            (int8_t)(m[MARK_MAP] == g_current_map.map ? max_int(1, ai_manhattan((Location){ai_own_x(), ai_own_y()},
                                                                                (Location){m[MARK_X], m[MARK_Y]}))
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
        c->found = ai_pack_location(g_ai_square.x, g_ai_square.y, g_ai_square.map);
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
        return ai_at_party(x, y);
    if (c->item == VIEW_IN_FRONT) {
        if (dungeon_direction_to(g_ai_party.x, g_ai_party.y, x, y) != g_ai_party.dir)
            return false;
        int16_t d = ai_trace_line((Location){g_ai_party.x, g_ai_party.y}, (Location){x, y}, ai_square_is_obstacle);
        return d > 0 && d <= (int16_t)c->mask;
    }
    if (c->item == VIEW_AT_DISTANCE) {
        if (ai_manhattan((Location){x, y}, (Location){g_ai_party.x, g_ai_party.y}) != (int16_t)c->mask)
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
        d = ai_manhattan((Location){x, y}, (Location){g_ai_party.x, g_ai_party.y});
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
    int16_t d =
        m[MARK_MAP] == s->check_map ? ai_manhattan((Location){x, y}, (Location){m[MARK_X], m[MARK_Y]}) : m[MARK_BEST];
    if (m[MARK_BEST] <= d) {
        if (m[MARK_MAP] != s->passage_map)
            return false;
        d = ai_manhattan((Location){s->passage_x, s->passage_y}, (Location){m[MARK_X], m[MARK_Y]});
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
        return on_party_map &&
               ai_manhattan((Location){x, y}, (Location){g_ai_party.x, g_ai_party.y}) == PARTY_RANGE_DISTANCE;
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
        if (!on_party_map || ai_action_flag((int8_t)g_ai.previous_action) & AI_ACTION_FLAG_ATTACK)
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
            light_add_square_light(s->dist, s->dir == -1 ? g_game.party_dir : s->dir, ai_location(x, y),
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
    if (!ai_at_party(x, y))
        return false;
    if (c->item == VIEW_PARTY_SIDE) {
        if (!c->mask || (1 << dir_relative(dungeon_direction_to(g_ai_party.x, g_ai_party.y, s->from_x, s->from_y),
                                           g_ai_party.dir)) &
                            c->mask)
            return true;
        s->visit = VISIT_BLOCKED;
    } else if (c->item == VIEW_PARTY_AT_PIT) {
        uint8_t square = dungeon_square_at(dungeon_step((Location){x, y}, s->dir));
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
    bool r = ai_at_party(nx, ny);
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
    if (c->item == DOOR_TARGET_PARTY && !ai_at_party(x, y))
        return false;
    if (!ai_try_door_action(packed_test((uint8_t)c->mask), ai_location(s->from_x, s->from_y), ai_location(x, y), s->dir)
             .passed)
        return false;
    if (c->item == DOOR_TARGET_NEXT_TO_PARTY &&
        ai_manhattan((Location){x, y}, (Location){g_ai_party.x, g_ai_party.y}) != 1)
        return false;
    ai_cache_square((Location){x, y});
    if (c->mask == DOOR_MODE_OPEN && !(g_ai_square.square & SQUARE_DOOR_STATE))
        return false;
    if (c->mask == DOOR_MODE_CLOSE && (g_ai_square.square & SQUARE_DOOR_STATE) == DOOR_STATE_CLOSED)
        return false;
    uint16_t door = ai_cached_first_thing();
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
        return ai_at_party(x, y);
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
            light_add_square_light(s->dist, s->dir, ai_location(x, y), LIGHT_WALLS);
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
        if (s->two_maps && g_current_map.map != s->start_map && g_current_map.map != g_ai_party.map)
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
        c->destination = c->kind_flags & KIND_PARTY_LOCATION
                             ? ai_pack_location(g_ai_party.x, g_ai_party.y, g_ai_party.map)
                             : ai_pack_location(s->x, s->y, s->check_map);
        if (s->visit) {
            c->path_length = (int8_t)(s->dist - 1);
            s->x = s->from_x;
            s->y = s->from_y;
            s->check_map = s->from_map;
        } else {
            c->path_length = (int8_t)s->dist;
        }
        c->found = ai_pack_location(s->x, s->y, s->check_map);
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
    s->check_map = g_current_map.map;
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
    uint8_t *cell = ai_search_cell(s->map, s->x, s->y);
    uint16_t from = ai_pack_location(s->from_x, s->from_y, s->from_map);
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
        ai_search_cell(s->map, s->x, s->y)[SEARCH_AT_DIST] = s->visit;
    return i;
}

/* Makes the map of a queued square the current map of the search. */
static void search_enter_map(Search *s, int16_t map)
{
    if (map == g_current_map.map)
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
        if (!ai_on_map(s->x, s->y))
            return SEARCH_STEP_SKIP;
    }
    g_ai_square_scan.blocking = 0;
    s->visit = ai_search_cell(s->map, s->x, s->y)[SEARCH_AT_DIST];
    if (!s->visit || s->dir >= DIR_LEVEL_UP) {
        AiMoveTest move = ai_move_test(s->command);
        if (!ai_try_move(move, ai_location(s->from_x, s->from_y), ai_location(s->x, s->y), s->dir).passed) {
            s->visit = VISIT_BLOCKED;
        } else {
            s->passage_map = -1;
            s->passage_x = s->x;
            s->passage_y = s->y;
            if (g_ai_square.map != g_current_map.map) {
                if (!s->maps[g_ai_square.map])
                    s->maps[g_ai_square.map] = calloc(MAP_SIDE_MAX, SEARCH_COLUMN_SIZE);
                if (!s->maps[g_ai_square.map])
                    os_fatal("out of memory");
                if (s->dir < DIR_LEVEL_UP)
                    s->passage_map = g_current_map.map;
                ai_set_map(g_ai_square.map);
                s->map = s->maps[g_current_map.map];
            } else if (s->dir < DIR_LEVEL_UP) {
                if (g_ai_square_scan.passage) {
                    s->passage_map = g_ai_square_scan.end.map;
                    s->passage_x = g_ai_square_scan.end.x;
                    s->passage_y = g_ai_square_scan.end.y;
                } else if (g_ai_square.x != s->x || g_ai_square.y != s->y) {
                    s->passage_map = g_current_map.map;
                }
            }
            s->visit = 0;
        }
    }
    if (s->visit)
        return s->visit == VISIT_BLOCKED && s->dir < DIR_LEVEL_UP ? SEARCH_STEP_BLOCKED : SEARCH_STEP_SKIP;
    s->x = g_ai_square.x;
    s->y = g_ai_square.y;
    if (ai_search_cell(s->map, s->x, s->y)[SEARCH_AT_DIST])
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
        s->dist = ai_search_cell(s->map, s->from_x, s->from_y)[SEARCH_AT_DIST];
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

/* Writes the steps from the start to the found candidate into path. */
static void search_write_path(Search *s, uint8_t *path)
{
    AiCandidate *c = &s->cands[s->result];
    int16_t x = location_x(c->found);
    int16_t y = location_y(c->found);
    int16_t map = location_map(c->found);
    for (int16_t n = (int16_t)(c->path_length - 1); n >= 0; n--) {
        uint8_t *cell = search_map_cell(s, map, x, y);
        uint16_t from = le16(cell + SEARCH_AT_FROM);
        uint16_t step = le16(path + n * AI_PATH_STEP_SIZE) & PATH_KEEP_BITS;
        step |= path_step_pack(cell[SEARCH_AT_DIR], x, y);
        if (location_map(from) != map) {
            map = location_map(from);
            step |= PATH_MAP_CHANGE;
        }
        put_le16(path + n * AI_PATH_STEP_SIZE, step);
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
    s->start_map = g_current_map.map;
    s->result = -1;
    s->enabled = ai_enabled_byte(s->command);
    s->two_maps = !s->enabled && ai_class_flags() & AI_CLASS_STAYS_ON_MAPS;
    s->flee_once = (s->command & AI_MOVE_KIND_MASK) == MOVE_KIND_SAFE_FLEE;
    search_setup(s);
    if (g_ai.move_flags & MOVE_LEVELS && g_ai.move_flags & MOVE_LEVEL_FLAGS) {
        s->levels = true;
        s->up_ok = g_ai.move_flags & MOVE_LEVEL_UP_FLAGS;
        s->down_ok = g_ai.move_flags & MOVE_LEVEL_DOWN_FLAGS;
    }
    /* fix: the original clears the first search map only for line targets or a cost; the port always clears it */
    s->map = calloc(MAP_SIDE_MAX, SEARCH_COLUMN_SIZE);
    s->maps[g_current_map.map] = s->map;
    if (!s->map)
        os_fatal("out of memory");
    s->cost_search = (s->command & AI_MOVE_KIND_MASK) == MOVE_KIND_COST;
    if (s->cost_search)
        s->counts[SEARCH_LEVELS - 1] = SEARCH_LEVEL_BLOCKED;
    s->from_map = g_current_map.map;
    s->check_map = g_current_map.map;
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
    g_ai_square_scan.passage = (uint8_t)dungeon_linked_teleporter_dest(&g_ai_square_scan.end, (Location){x, y});
    if (g_ai_square_scan.passage) {
        s->passage_map = g_ai_square_scan.end.map;
        s->passage_x = g_ai_square_scan.end.x;
        s->passage_y = g_ai_square_scan.end.y;
    }
}

int ai_search_area(Location from, int command, int count, AiCandidate *candidates, uint8_t *out_path)
{
    if (candidates[0].kind == TARGET_ANY) {
        candidates[0].found = ai_pack_location(from.x, from.y, g_current_map.map);
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
    return le16(g_ai.cached_path + index * AI_PATH_STEP_SIZE);
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
    if (g_ai.map == location_map(found) && ai_own_x() == location_x(found) && ai_own_y() == location_y(found))
        g_ai.cache.found = FOUND_NONE;
    return left;
}

/* True when the target of the plan is still at the found square and the next steps of the stored path from the
   group on the map saved are free. */
static bool path_holds(int16_t saved)
{
    int16_t x = location_x(g_ai.cache.found);
    int16_t y = location_y(g_ai.cache.found);
    uint8_t path[AI_PATH_BYTES];
    /* the search of the one candidate (index 0) from the found square: the target must be on that square */
    if (ai_search_area(ai_location(x, y), MOVE_KIND_NONE, 1, g_ai.candidates, path) != 0 ||
        g_ai.candidates[0].path_length)
        return false;
    int16_t cx = ai_own_x();
    int16_t cy = ai_own_y();
    int16_t left = g_ai.cache.path_left;
    if (!left)
        return cx == x && cy == y && g_current_map.map == saved;
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
            path_step_map_change(step) != (g_ai_square.map != g_current_map.map))
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
    int16_t saved = g_current_map.map;
    ai_set_map(location_map(g_ai.cache.found));
    bool result = path_holds(saved);
    ai_set_map(saved);
    return result;
}

void ai_refresh_party_maps(void)
{
    int16_t saved = g_current_map.map;
    dungeon_set_current_map(g_game.party_map);
    AiCandidate target;
    uint8_t path[AI_PATH_BYTES];
    memset(&target, 0, sizeof target);
    if (g_party.redraw & PARTY_REDRAW_SQUARES) {
        g_party_distance.map = (int16_t)g_game.party_map;
        g_party_distance.width = (int16_t)dungeon_map_width(g_game.party_map);
        memset(g_party_distance.cells, 0, (size_t)g_party_distance.width * MAP_SIDE_MAX);
        g_alt_distance.map = g_current_map.alt_view_map;
        if (g_alt_distance.map >= 0) {
            g_alt_distance.width = (int16_t)dungeon_map_width(g_alt_distance.map);
            memset(g_alt_distance.cells, 0, (size_t)g_alt_distance.width * MAP_SIDE_MAX);
        }
        target.cost = PARTY_MAP_SEARCH_COST;
        target.value = 0;
        target.kind = TARGET_PARTY_DISTANCE;
        ai_search_area(ai_location(g_game.party_x, g_game.party_y), MOVE_KIND_COST, 1, &target, path);
    }
    if (g_party.redraw & PARTY_REDRAW_VIEW) {
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
    dungeon_set_current_map(saved);
    g_party.redraw = 0;
    g_last_party_map = (int16_t)g_game.party_map;
}
