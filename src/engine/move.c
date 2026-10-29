/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Party movement and thing movement through pits, teleporters, and stairs. */
#include "move.h"
#include <stdlib.h>
#include "aspect.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "game.h"
#include "gdat.h"
#include "hooks.h"
#include "item.h"
#include "le.h"
#include "projectile.h"
#include "random.h"
#include "save.h"
#include "sensor.h"
#include "sound_request.h"
#include "timeline.h"
#include "weather.h"

/* Graphics and creature values that the moves read. */
#define MAP_KEY_PIT_TO_MARKER 0x6a /* map graphics value: pits lead to a random marker, not one level down */
#define ATTR_TELEPORT_SCOPE   0x1e /* creature attribute: teleporters that move the creature */

/* Limits of the moves. */
#define TRANSITION_MAX 50 /* the loop limit of move_follow_square_transitions */

/* Teleporter scopes: who uses the teleporter. */
#define SCOPE_ALL       3
#define SCOPE_CREATURES 1
#define SCOPE_PARTY     2

/* Text marker kinds (text record bits 11-15) that the pits and the marker search read. */
#define MARKER_TARGET     0x0b /* a pit target marker of a marker id */
#define MARKER_PIT_TARGET 0x0c /* a pit square: the marker id of its targets */
#define MARKER_SWITCH     0x0f /* a marker with an on bit */
#define MARKER_POSITION   0x10 /* the last marker of an id wins */

/* Champion record values. */
#define LOAD_PER_STEP_FACTOR      3 /* stamina for a step: 3 * load / max load + 1 */
#define LOAD_PER_FALL_FACTOR      25
#define FALL_DAMAGE_MAX           17
#define LOAD_TIME_DIVISOR         10 /* the step time of a champion for a creature alert: load / 10 */
#define WALK_TIME_SLOW            50
#define WALK_TIME                 40
/* the damage of a creature that lands on the party: 0 to 9, plus its push resistance / 8 */
#define DISPLACE_DAMAGE_ROLL      10
#define DISPLACE_RESISTANCE_SHIFT 3
#define FALL_ALERT_DAMAGE         20 /* the damage alert of a creature that falls into a pit */
#define BUMP_ALERT_CHANCE         5
#define ARRIVAL_ALERT_SHIFT 2 /* a creature that the party pushes aside: alert damage of a quarter of the step time */
#define PUSH_ALERT_SHIFT 2 /* a creature that another pushes aside: alert damage of a quarter of the push resistance */

/* The ornament subtypes of the stairs markers. */
#define TEXT_LINK_FIRST 0x06 /* text kinds 6 and 7: stairs markers */
#define TEXT_LINK_LAST  0x07

/* Random parts of the champion values. */
#define BASH_ROLL_MASK 0x0f /* the random part of the strength of a champion who bashes a door */

/* The retry of a group move. */
#define GROUP_RETRY_DELAY 5

/* Pushing creatures. */
#define PUSH_FAIL_STAMINA  5
#define PUSH_LIGHT         46 /* creatures below this resistance are easy to push */
#define PUSH_LEADER_SHIFT  3  /* the leader pushes with 1/8 more strength */
#define PUSH_LIGHT_SHIFT   2  /* a light creature: 1/4 more strength */
#define PUSH_STAMINA_SHIFT 4  /* a push costs stamina when a champion has more than 1/16 of the maximum */
#define PUSH_COST_SHIFT    2  /* a push costs a quarter of the resistance, at least PUSH_FAIL_STAMINA */

/* Places in the orders of cells and sides. */
#define FREE_SQUARE_ROWS 4 /* find_free_adjacent_square: one row for each value of a 2-bit roll */
#define BACK_STEP        2 /* the step of CMD_MOVE_BACK, counted from CMD_MOVE_FORWARD */

/* The discard search. */
#define SENSOR_KIND_TABLE 44 /* the sensor kinds of sensor_blocks_discard */
#define DISCARD_NEAR      5  /* the things this near to the party on its map stay */

/* What a step of the party meets. */
typedef enum {
    STEP_STAIRS_HERE = 1,
    STEP_STAIRS_AHEAD,
    STEP_BLOCKED,
    STEP_CREATURE,
    STEP_CREATURE_SIDE,
    STEP_FREE
} StepResult;

/* A step of the party from the square byte here to the square (x, y) ahead in the direction dir. */
typedef struct {
    int step; /* the move command after CMD_MOVE_FORWARD: forward, right, back, left */
    uint8_t here, ahead;
    int16_t x, y;
    int16_t dir;
} PartyStep;

/* A thing (THING_NONE: the party) on its way through pits, teleporters, and stairs. */
typedef struct {
    uint16_t thing;
    int type; /* the thing type, -1 for the party */
    int16_t src_x, src_y, x, y, map, start_map;
    int scope; /* the teleporters that move it: SCOPE_* */
    bool flying, draw_fall;
    int falls;
    bool fell_hurt, creature_sound;
} Transit;

/* A move of a thing (THING_NONE: the party) in move_thing. */
typedef struct {
    uint16_t thing;
    int type; /* the thing type, -1 for the party */
    int16_t cell;
    MovePath path;
    int16_t map, start_map, start_dir, dest_dir;
    int16_t dest_cell; /* the cell where the transitions put the thing, -1 without a destination */
    bool flying, same, stays;
} ThingMove;

/* How a group meets its target square: it enters (the square is free, or another group moved aside), it is done
   (it went elsewhere, or a retry is queued), or it waits. */
typedef enum { ARRIVAL_ENTER, ARRIVAL_DONE, ARRIVAL_WAIT } GroupArrival;

/* A marker search over all maps: the marker id, the pit target markers still to pass, their count, and the square
   that the search finds (also the last position marker of the id). */
typedef struct {
    int marker;
    int nth;
    int count;
    Position at;
} MarkerSearch;

/* What a marker search does at a marker of a kind and an id; true when the search stops there. */
typedef bool (*MarkerVisit)(MarkerSearch *s, uint16_t text, int kind, int id);

/* A square of the discard search, and the static creature there whose possessions the search reads (THING_NONE
   none). */
typedef struct {
    int16_t map, x, y;
    uint16_t holder;
} DiscardSquare;

const int8_t g_dir_dx[DIR_COUNT] = {0, 1, 0, -1};
const int8_t g_dir_dy[DIR_COUNT] = {-1, 0, 1, 0};
/* Steps of the move commands 3 to 6: forward and right parts. */
static const int8_t command_forward[DIR_COUNT] = {1, 0, -1, 0};
static const int8_t command_right[DIR_COUNT] = {0, 1, 0, -1};
/* The order in which find_free_adjacent_square tries the 4 sides, by a random row: for the party
   relative to its direction (DIR_NORTH: ahead), for things absolute. */
static const uint8_t free_square_order_party[FREE_SQUARE_ROWS][DIR_COUNT] = {
    {DIR_SOUTH, DIR_NORTH, DIR_EAST,  DIR_WEST },
    {DIR_SOUTH, DIR_EAST,  DIR_NORTH, DIR_WEST },
    {DIR_SOUTH, DIR_WEST,  DIR_NORTH, DIR_EAST },
    {DIR_SOUTH, DIR_EAST,  DIR_WEST,  DIR_NORTH}
};
static const uint8_t free_square_order_thing[FREE_SQUARE_ROWS][DIR_COUNT] = {
    {DIR_NORTH, DIR_SOUTH, DIR_EAST,  DIR_WEST },
    {DIR_EAST,  DIR_NORTH, DIR_SOUTH, DIR_WEST },
    {DIR_SOUTH, DIR_WEST,  DIR_NORTH, DIR_EAST },
    {DIR_WEST,  DIR_EAST,  DIR_SOUTH, DIR_NORTH}
};
/* The sensor kinds whose square keeps its things. */
static const uint8_t sensor_blocks_discard[SENSOR_KIND_TABLE] = {
    0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1,
};

Party g_party = {.cur_map = CUR_MAP_NONE, .new_map = -1, .push_pending_group = THING_NONE};
uint8_t g_last_discarded_map[THING_TYPE_COUNT];

/* Each living champion pays stamina by load for a step or a levitated fall: factor * load / max load + 1. */
static void pay_stamina_by_load(int factor)
{
    for (int i = 0; i < g_game.champion_count; i++) {
        Champion *c = champion(i);
        if (!c->health)
            continue;
        int load = (uint16_t)champ_get_load(i) * factor;
        champ_decrease_stamina(i, (int16_t)(load / (uint16_t)champ_get_max_load(c) + 1));
    }
}

/* ---- The current map ---- */

void dun_set_current_map(int16_t map)
{
    if (map == g_party.cur_map || map < 0)
        return;
    g_party.cur_map = map;
    if (!g_party.alt_view_active || map != g_party.alt_view_map) {
        g_party.square_x = (int16_t)g_game.party_x;
        g_party.square_y = (int16_t)g_game.party_y;
        g_party.square_map = (int16_t)g_game.party_map;
        g_party.ref_dir = (int16_t)g_game.party_dir;
    } else {
        g_party.square_x = g_party.alt_view_x;
        g_party.square_y = g_party.alt_view_y;
        g_party.square_map = g_party.alt_view_map;
        g_party.ref_dir = g_party.alt_view_dir;
    }
}

/* Makes map current and reads the party position as seen from it again, also when map is current already. */
static void reload_current_map(int16_t map)
{
    g_party.cur_map = CUR_MAP_NONE;
    dun_set_current_map(map);
}

void dun_set_party_position(Position at)
{
    g_game.party_map = (uint16_t)at.map;
    g_game.party_x = (uint16_t)at.x;
    g_game.party_y = (uint16_t)at.y;
    dun_set_current_map(at.map);
}

Location dun_move_relative(Location from, int16_t dir, int forward, int right)
{
    from.x = (int16_t)(from.x + g_dir_dx[dir] * forward);
    from.y = (int16_t)(from.y + g_dir_dy[dir] * forward);
    int side = dir_turn(dir, TURN_RIGHT);
    from.x = (int16_t)(from.x + g_dir_dx[side] * right);
    from.y = (int16_t)(from.y + g_dir_dy[side] * right);
    return from;
}

uint8_t dun_get_square(Location at)
{
    return dungeon_square_edge(g_party.cur_map, at.x, at.y);
}

bool dun_on_party_square(Location at)
{
    return g_party.cur_map == g_party.square_map && at.x == g_party.square_x && at.y == g_party.square_y;
}

bool dun_square_blocks_movement(uint8_t square)
{
    switch (square_type(square)) {
    case SQUARE_WALL:
    case SQUARE_UNKNOWN:
        return true;
    case SQUARE_DOOR: {
        int state = square & SQUARE_DOOR_STATE;
        return state != DOOR_STATE_OPEN && state != DOOR_STATE_ONE_FOURTH && state != DOOR_STATE_BROKEN;
    }
    case SQUARE_FAKE_WALL:
        return !(square & SQUARE_FAKE_WALL_OPEN) && !(square & SQUARE_FLAG_0);
    default:
        return false;
    }
}

int16_t dun_get_stairs_exit_direction(Location at)
{
    bool along_y = !(dun_get_square(at) & SQUARE_STAIRS_ALONG_X);
    int probe = along_y ? DIR_EAST : DIR_NORTH;
    int type =
        square_type(dun_get_square((Location){(int16_t)(at.x + g_dir_dx[probe]), (int16_t)(at.y + g_dir_dy[probe])}));
    return type == SQUARE_WALL || type == SQUARE_STAIRS ? dir_opposite(probe) : probe;
}

bool dun_get_linked_teleporter_dest(PassageEnd *end, Location at)
{
    TeleporterLink link;
    if (!dungeon_teleporter_link(g_party.cur_map, at.x, at.y, &link))
        return false;
    *end = (PassageEnd){link.side, link.target_side, link.x, link.y, link.map};
    return true;
}

/* ---- Champions ---- */

int16_t dun_get_direction_to(int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
    int dx = (int16_t)(x1 - x2);
    int dy = (int16_t)(y1 - y2);
    int ax = abs(dx);
    int ay = abs(dy);
    if (ax == ay) {
        if (rand_bit())
            ax++;
        else
            ay++;
    }
    if (ax < ay)
        return dy > 0 ? DIR_NORTH : DIR_SOUTH;
    return dx > 0 ? DIR_WEST : DIR_EAST;
}

int16_t move_nearest_champion_at(Location at, int16_t cell)
{
    if (!g_game.champion_count)
        return -1;
    if (abs(at.x - g_party.square_x) + abs(at.y - g_party.square_y) >= 2)
        return -1;
    uint8_t cells[CELL_COUNT];
    projectile_order_attack_cells(cells, (Location){g_party.square_x, g_party.square_y}, at, cell);
    for (int i = 0; i < DIR_COUNT; i++) {
        int16_t champion_index = champ_get_index_in_cell(cells[i]);
        if (champion_index >= 0)
            return champion_index;
    }
    return -1;
}

/* A champion (-1 none) who bumps into (x, y) takes 1 damage; the push sound plays when it hurts. */
static void bump_champion(int16_t champion_index, int16_t x, int16_t y)
{
    if (champion_index < 0)
        return;
    if (champ_take_damage(champion_index, 1, BUMP_WOUNDS, ATTACK_TYPE_SELF))
        sndreq_request_with_fallback((SoundCue){CAT_CHAMPION, champion(champion_index)->portrait, SOUND_ENTRY_PUSHED,
                                                SOUND_PRIORITY_PUSHED, SOUND_VOLUME_LOUD},
                                     SOUND_FALLBACK, x, y, SOUND_STEP_END);
}

BumpedChampions move_party_bump_damage(Location at, int16_t dir, int step)
{
    int side = dir_opposite(dir_turn(dir, step));
    BumpedChampions bumped;
    bumped.first = move_nearest_champion_at(at, side);
    bumped.second = move_nearest_champion_at(at, dir_turn(side, TURN_RIGHT));
    bump_champion(bumped.first, at.x, at.y);
    if (bumped.second != bumped.first)
        bump_champion(bumped.second, at.x, at.y);
    return bumped;
}

/* ---- Steps ---- */

/* True when the creature in front stands on a side cell that blocks the step. */
static bool is_side_blocked(uint16_t group, int16_t dir)
{
    int side = dir_turn(group_dir(group), dir);
    if (side != DIR_EAST && side != DIR_WEST)
        return false;
    return aspect_creature_cell(group, side) % CELL_GRID_SIDE != CELL_GRID_HALF;
}

/* The walk-through-walls cheat: the party enters a wall, a closed fake wall, or a closed door inside the map. */
static bool can_walk_through(const PartyStep *s)
{
    int type = square_type(s->ahead);
    return game_has_cheat(CHEAT_WALLS) && (type == SQUARE_WALL || type == SQUARE_FAKE_WALL || type == SQUARE_DOOR) &&
           s->x >= 0 && s->x < dungeon_map_width(g_party.cur_map) && s->y >= 0 &&
           s->y < dungeon_map_height(g_party.cur_map);
}

/* What a step of the party meets on the square ahead; *group: the creature group there. */
static StepResult get_step_result(const PartyStep *s, uint16_t *group)
{
    *group = THING_NONE;
    if (square_type(s->here) == SQUARE_STAIRS && s->step == BACK_STEP)
        return STEP_STAIRS_HERE;
    if (square_type(s->ahead) == SQUARE_STAIRS)
        return STEP_STAIRS_AHEAD;
    if (dun_square_blocks_movement(s->ahead) && !can_walk_through(s))
        return STEP_BLOCKED;
    *group = creature_group_at((Position){g_party.cur_map, s->x, s->y});
    if (*group == THING_NONE || creature_group_info(*group)->flags & CREATURE_NOT_SOLID) {
        uint16_t g = creature_find_group_near_point((Location){s->x, s->y}, CELL_ANY).group;
        if (g == THING_NONE || creature_group_info(g)->flags & CREATURE_NOT_SOLID)
            return STEP_FREE;
    } else if (!is_side_blocked(*group, g_game.party_dir)) {
        return STEP_CREATURE;
    }
    return STEP_CREATURE_SIDE;
}

/* The party takes the stairs up or down: it leaves the square and arrives on the map above or below. */
static void take_stairs(bool up)
{
    move_thing(THING_NONE, g_game.party_x, g_game.party_y, MOVE_NO_SQUARE, 0);
    Position to = dungeon_location_on_map((Position){g_game.party_map, g_game.party_x, g_game.party_y}, up ? -1 : 1);
    g_party.new_map = to.map;
    g_game.party_x = (uint16_t)to.x;
    g_game.party_y = (uint16_t)to.y;
    dun_set_current_map(g_party.new_map);
    champ_set_party_direction(dun_get_stairs_exit_direction((Location){g_game.party_x, g_game.party_y}));
    dun_set_current_map(g_game.party_map);
}

/* The champions face the direction of the party again. */
static void sync_champion_directions(void)
{
    for (int i = 0; i < g_game.champion_count; i++)
        champion(i)->dir = (uint8_t)g_game.party_dir;
}

void move_party_turn(int command)
{
    if (!command)
        return;
    g_input_command_stop = true;
    sync_champion_directions();
    uint8_t square = dun_get_square((Location){g_game.party_x, g_game.party_y});
    if (square_type(square) == SQUARE_STAIRS) {
        take_stairs(square & SQUARE_STAIRS_UP);
        return;
    }
    PassageEnd end;
    if (dun_get_linked_teleporter_dest(&end, (Location){g_game.party_x, g_game.party_y})) {
        move_party_to((Position){end.map, end.x, end.y}, end.target_side);
        return;
    }
    sensor_leave_square((Location){g_game.party_x, g_game.party_y}, THING_NONE, true, false);
    champ_set_party_direction(dir_turn(g_game.party_dir, command == CMD_TURN_RIGHT ? TURN_RIGHT : TURN_LEFT));
    sensor_enter_square((Location){g_game.party_x, g_game.party_y}, THING_NONE, true, false);
}

/* The party steps onto the square ahead from the square of map: seamless passages, stairs, teleporter passages, then
   the move. */
static void take_free_step(int16_t map, bool on_stairs, PartyStep s)
{
    if (g_party.alt_view_active && s.step == BACK_STEP) {
        dun_set_current_map(map);
        s.x = g_party.square_x;
        s.y = g_party.square_y;
        s.dir = g_party.ref_dir;
        Location back = dun_move_relative((Location){s.x, s.y}, s.dir, -1, 0);
        s.x = back.x;
        s.y = back.y;
    }
    if (on_stairs) {
        move_thing(THING_NONE, MOVE_NO_SQUARE, 0, s.x, s.y);
        return;
    }
    PassageEnd end;
    if (dun_get_linked_teleporter_dest(&end, (Location){s.x, s.y}) && dir_opposite(end.target_side) != s.dir) {
        move_party_to((Position){end.map, end.x, end.y}, end.target_side);
        return;
    }
    move_thing(THING_NONE, g_game.party_x, g_game.party_y, s.x, s.y);
}

/* The strength of a champion who bumps into a door. */
static int bash_strength(int16_t champion_index)
{
    Champion *c = champion(champion_index);
    int r = rand_next() & BASH_ROLL_MASK;
    return champ_scale_by_stamina(c, (int16_t)(champ_get_stat(c, STAT_STRENGTH, 0) + r));
}

/* The two champions who bumped into the closed door on (x, y) try to break it with their strength. */
static void bash_door(int16_t x, int16_t y, BumpedChampions bumped)
{
    int strength = 0;
    if (bumped.first != -1)
        strength = bash_strength(bumped.first);
    if (bumped.second != bumped.first && bumped.second != -1)
        strength += bash_strength(bumped.second);
    sensor_door_try_destroy((Location){x, y}, (int16_t)strength, false, 0);
}

/* The party walks into the creature group ahead: a bump, then a push when the group can be pushed; false when the
   party stays. */
static bool push_creature_ahead(const PartyStep *s, uint16_t group)
{
    if (!g_game.champion_count)
        return false;
    bool is_static = creature_group_info(group)->flags & CREATURE_STATIC;
    if (is_static || creature_group_info(group)->push_resistance == PUSH_WALL)
        move_party_bump_damage((Location){s->x, s->y}, s->dir, s->step);
    int push_dir = dir_turn(s->dir, s->step);
    if (!creature_can_be_pushed((Location){s->x, s->y}, push_dir, true, &group) || !move_party_can_push_group(group)) {
        if (!is_static)
            creature_alert(THING_NONE, (Location){s->x, s->y}, ALERT_ANGER | ALERT_MOOD_BORED, BUMP_ALERT_CHANCE, 0);
        return false;
    }
    creature_push_group((Location){s->x, s->y}, push_dir, PUSH_STRENGTH_PARTY);
    return true;
}

/* One step of the party for a move command: stairs, a bump into a wall, door, or creature, a push, or a free step.
   False when the party cannot move. */
static bool step_party(int command, uint8_t here, bool on_stairs)
{
    int map = g_party.cur_map;
    PartyStep s = {.step = command - CMD_MOVE_FORWARD, .here = here};
    if (g_party.alt_view_active && s.step == BACK_STEP)
        dun_set_current_map(g_party.alt_view_map);
    s.x = g_party.square_x;
    s.y = g_party.square_y;
    s.dir = g_party.ref_dir;
    Location ahead = dun_move_relative((Location){s.x, s.y}, s.dir, command_forward[s.step], command_right[s.step]);
    s.x = ahead.x;
    s.y = ahead.y;
    s.ahead = dun_get_square(ahead);
    g_input_command_stop = true;
    pay_stamina_by_load(LOAD_PER_STEP_FACTOR);
    sync_champion_directions();
    uint16_t group;
    switch (get_step_result(&s, &group)) {
    case STEP_STAIRS_AHEAD:
        move_thing(THING_NONE, g_game.party_x, g_game.party_y, MOVE_NO_SQUARE, 0);
        g_game.party_x = (uint16_t)s.x;
        g_game.party_y = (uint16_t)s.y;
        here = s.ahead;
        __attribute__((fallthrough));
    case STEP_STAIRS_HERE:
        take_stairs(here & SQUARE_STAIRS_UP);
        return true;
    case STEP_BLOCKED: {
        if (!g_game.champion_count)
            return false;
        BumpedChampions bumped = move_party_bump_damage((Location){s.x, s.y}, s.dir, s.step);
        if (square_type(s.ahead) == SQUARE_DOOR && (s.ahead & SQUARE_DOOR_STATE) == DOOR_STATE_CLOSED)
            bash_door(s.x, s.y, bumped);
        return false;
    }
    case STEP_CREATURE:
        if (!push_creature_ahead(&s, group))
            return false;
        break;
    case STEP_CREATURE_SIDE:
        return false;
    case STEP_FREE:
        break;
    }
    take_free_step(map, on_stairs, s);
    return true;
}

bool move_party(int command)
{
    uint8_t here = dun_get_square((Location){g_party.square_x, g_party.square_y});
    bool on_stairs = square_type(here) == SQUARE_STAIRS;
    int delay = 1;
    for (int i = 0; i < g_game.champion_count; i++)
        if (champion(i)->health) {
            int d = champ_get_move_delay(i);
            if (d > delay)
                delay = d;
        }
    int half = (int16_t)delay >> 1;
    if (delay > 1 && !g_party.move_steps &&
        (command == CMD_MOVE_FORWARD || (command == CMD_MOVE_BACK && !g_party.alt_view_active && !on_stairs) ||
         g_party.push_pending_group != THING_NONE)) {
        g_party.move_steps = (int16_t)half;
        g_party.view_x = (int16_t)g_game.party_x;
        g_party.view_y = (int16_t)g_game.party_y;
        g_party.view_dir = (int16_t)g_game.party_dir;
        g_party.move_command = (uint16_t)command;
        if (command != CMD_MOVE_FORWARD) {
            Location back = dun_move_relative((Location){g_party.view_x, g_party.view_y}, g_game.party_dir, -1, 0);
            g_party.view_x = back.x;
            g_party.view_y = back.y;
        }
    } else if (g_party.push_pending_group != THING_NONE) {
        creature_continue_push(command);
        g_party.push_pending_group = THING_NONE;
        if (!g_party.push_mode)
            g_party.push_arrow = PUSH_ARROW_NONE;
        return true;
    } else if (!step_party(command, here, on_stairs)) {
        g_hooks.flush_input();
        g_input_command_stop = false;
        return false;
    }
    /* the cooldown: half the delay, at least 1 while the party walks */
    int add = g_party.move_steps ? 1 : 0;
    if (half > add)
        add = half;
    g_game.party_move_cooldown = (uint16_t)(g_game.party_move_cooldown + add);
    g_game.throw_cooldown = 0;
    return true;
}

void move_party_to(Position at, int16_t dir)
{
    int16_t x = at.x;
    int16_t y = at.y;
    int16_t map = at.map;
    bool other = map != g_game.party_map;
    if (other)
        dun_set_current_map(map);
    if (x < 0 || x >= dungeon_map_width(g_party.cur_map) || y < 0 || y >= dungeon_map_height(g_party.cur_map)) {
        if (other)
            dun_set_current_map(g_game.party_map);
        return;
    }
    int sx = g_game.party_x;
    int sy = g_game.party_y;
    if (other) {
        dun_set_current_map(g_game.party_map);
        move_thing(THING_NONE, g_game.party_x, g_game.party_y, MOVE_NO_SQUARE, 0);
        game_enter_map((Position){map, x, y}, true);
        sx = MOVE_NO_SQUARE;
        sy = 0;
    }
    move_thing(THING_NONE, sx, sy, x, y);
    champ_set_party_direction(dir);
}

void move_compute_map_arrival(Position at)
{
    PassageEnd end;
    int turn = 0;
    g_party.alt_view_map = -1;
    g_party.alt_view_active = dun_get_linked_teleporter_dest(&end, (Location){at.x, at.y});
    bool found = g_party.alt_view_active;
    if (found) {
        g_party.alt_view_x = end.x;
        g_party.alt_view_y = end.y;
        turn = end.target_side - end.side;
    }
    for (int d = 0; d < DIR_COUNT && !found; d++) {
        if (!dun_get_linked_teleporter_dest(&end,
                                            (Location){(int16_t)(at.x + g_dir_dx[d]), (int16_t)(at.y + g_dir_dy[d])}))
            continue;
        found = true;
        turn = dir_opposite(end.target_side) - end.side;
        int side = dir_turn(d, dir_opposite(turn));
        g_party.alt_view_x = (int16_t)(end.x + g_dir_dx[side]);
        g_party.alt_view_y = (int16_t)(end.y + g_dir_dy[side]);
    }
    if (found) {
        g_party.alt_view_map = end.map;
        g_party.alt_view_dir = (int16_t)dir_turn(g_game.party_dir, turn);
    }
    reload_current_map(at.map);
}

/* Visits the ornament markers of all maps: a position marker of the id sets s->at; true when visit stops at a
   marker, with its square in s->at. A search to the end sets the current map to the party map. */
static bool search_markers(MarkerSearch *s, MarkerVisit visit)
{
    for (int16_t map = 0; map < dungeon_map_count(); map++) {
        for (int16_t mx = 0; mx < dungeon_map_width(map); mx++)
            for (int16_t my = 0; my < dungeon_map_height(map); my++) {
                for (uint16_t t = dungeon_square_first_thing(map, mx, my);
                     t != THING_ENDOFLIST && thing_type(t) < THING_GROUP; t = dungeon_next_thing(t)) {
                    if (thing_type(t) != THING_TEXT || textrec_kind(t) != TEXT_KIND_ORNAMENT)
                        continue;
                    int kind = textrec_subtype(t);
                    int id = textrec_value(t);
                    if (kind == MARKER_POSITION && id == s->marker) {
                        s->at = (Position){map, mx, my};
                    } else if (visit(s, t, kind, id)) {
                        s->at = (Position){map, mx, my};
                        return true;
                    }
                }
            }
    }
    dun_set_current_map(g_game.party_map);
    return false;
}

/* Counts a pit target marker of the id. */
static bool count_marker(MarkerSearch *s, uint16_t text, int kind, int id)
{
    (void)text;
    if (kind == MARKER_TARGET && id == s->marker)
        s->count++;
    return false;
}

/* The number of the pit target markers of s->marker. */
static int count_markers(MarkerSearch *s)
{
    search_markers(s, count_marker);
    return s->count;
}

/* True at the pit target marker nth of the id. */
static bool is_nth_marker(MarkerSearch *s, uint16_t text, int kind, int id)
{
    (void)text;
    return kind == MARKER_TARGET && id == s->marker && --s->nth == 0;
}

/* Finds the pit target marker nth (1 for the first) of s->marker; true with its square in s->at. */
static bool find_nth_marker(MarkerSearch *s, int nth)
{
    s->nth = nth;
    return search_markers(s, is_nth_marker);
}

/* Turns off a switch marker. */
static bool clear_switch_marker(MarkerSearch *s, uint16_t text, int kind, int id)
{
    (void)s;
    (void)id;
    if (kind == MARKER_SWITCH)
        textrec_set_visible(text, false);
    return false;
}

/* Turns off the switch markers of all maps; true with the last position marker of s->marker in s->at. */
static bool clear_switch_markers(MarkerSearch *s)
{
    search_markers(s, clear_switch_marker);
    return s->at.map >= 0;
}

/* True at a switch marker that is on. */
static bool is_set_switch_marker(MarkerSearch *s, uint16_t text, int kind, int id)
{
    (void)s;
    (void)id;
    return kind == MARKER_SWITCH && textrec_is_visible(text);
}

/* Finds the first switch marker that is on; true with its square in s->at. */
static bool find_set_switch_marker(MarkerSearch *s)
{
    return search_markers(s, is_set_switch_marker);
}

/* ---- Moving things ---- */

/* True for things that fly over pits and do not press floor sensors. */
static bool is_flying_thing(uint16_t thing)
{
    int type = thing_type(thing);
    if (type == THING_GROUP)
        return creature_is_flying(thing);
    return type == THING_PROJECTILE || type == THING_EXPLOSION;
}

/* A projectile through a teleporter: its cell and flight direction turn with the teleporter. */
static uint16_t rotate_projectile(uint16_t teleporter, uint16_t thing)
{
    int rotation = teleporter_rotation(teleporter);
    if (!teleporter_flag(teleporter, TELEPORTER_ABSOLUTE)) {
        thing = thing_with_cell(thing, dir_turn(thing_cell(thing), rotation));
        rotation = dir_turn(g_party.dest_dir, rotation);
    }
    g_party.dest_dir = (int16_t)rotation;
    return thing;
}

/* The first thing of (x, y) of the current map. */
static uint16_t first_thing(int16_t x, int16_t y)
{
    return dungeon_square_first_thing(g_party.cur_map, x, y);
}

/* Moves the party to at on the current map; the party square is read again. */
static void set_party_square(Location at)
{
    g_game.party_x = (uint16_t)at.x;
    g_game.party_y = (uint16_t)at.y;
    reload_current_map(g_party.cur_map);
}

/* The teleporter at (x, y) moves the thing and turns it; false when the thing stays there. */
static bool transit_teleporter(Transit *t, uint8_t square)
{
    if (!(square & SQUARE_TELEPORTER_ON))
        return false;
    uint16_t tele = first_thing(t->x, t->y);
    int tele_scope = teleporter_scope(tele);
    int rotation = teleporter_rotation(tele);
    bool absolute = teleporter_flag(tele, TELEPORTER_ABSOLUTE);
    bool audible = teleporter_flag(tele, TELEPORTER_AUDIBLE);
    if (tele_scope == SCOPE_CREATURES && t->type != THING_GROUP)
        return false;
    if (t->scope != SCOPE_ALL && !(tele_scope & t->scope))
        return false;
    int tx = teleporter_x(tele);
    int ty = teleporter_y(tele);
    int tmap = teleporter_map(tele);
    bool same = tx == t->x && ty == t->y && tmap == t->map;
    t->x = tx;
    t->y = ty;
    g_party.teleporter_flag = audible;
    t->map = tmap;
    dun_set_current_map(t->map);
    if (t->thing == THING_NONE) {
        set_party_square((Location){t->x, t->y});
        if (audible)
            sndreq_request(
                (SoundCue){CAT_TELEPORTER, 0, SOUND_ENTRY_TELEPORT, SOUND_PRIORITY_MOVE, SOUND_VOLUME_NORMAL},
                g_game.party_x, g_game.party_y, SOUND_DEFERRED);
        if (t->start_map != t->map)
            t->draw_fall = false; /* the original keeps it only with its debug flag */
        int base = absolute ? 0 : g_game.party_dir;
        champ_set_party_direction(dir_turn(base, rotation));
    } else if (t->type == THING_GROUP && t->creature_sound) {
        if (audible) {
            if (g_party.cur_map != t->start_map) {
                int saved = g_party.cur_map;
                dun_set_current_map(t->start_map);
                sndreq_request(
                    (SoundCue){CAT_TELEPORTER, 0, SOUND_ENTRY_TELEPORT, SOUND_PRIORITY_MOVE, SOUND_VOLUME_NORMAL},
                    t->src_x, t->src_y, SOUND_STEP_END);
                dun_set_current_map(saved);
            }
            sndreq_request(
                (SoundCue){CAT_TELEPORTER, 0, SOUND_ENTRY_TELEPORT, SOUND_PRIORITY_MOVE, SOUND_VOLUME_NORMAL}, t->x,
                t->y, SOUND_STEP_END);
        }
        creature_turn_group(t->thing, absolute, rotation);
    } else if (t->type == THING_PROJECTILE) {
        t->thing = rotate_projectile(tele, t->thing);
    } else if (!absolute && t->src_x != MOVE_DROPPED_PROJECTILE) {
        t->thing = thing_with_cell(t->thing, dir_turn(thing_cell(t->thing), rotation));
    }
    return !same;
}

/* The party lands after a fall: a levitation costs stamina by load, a real fall hurts the legs and the feet. */
static void transit_party_lands(Transit *t)
{
    if (g_party.fell) {
        t->draw_fall = false; /* the original keeps it only with its debug flag */
        pay_stamina_by_load(LOAD_PER_FALL_FACTOR);
        return;
    }
    for (int i = 0; i < g_game.champion_count; i++) {
        Champion *c = champion(i);
        if (c->health) {
            int base = c->max_health >> 2;
            if (base > FALL_DAMAGE_MAX)
                base = FALL_DAMAGE_MAX;
            champ_take_damage(i, (int16_t)((rand_2bits() + base) * t->falls), FALL_WOUNDS, ATTACK_TYPE_SELF);
            sndreq_request_with_fallback(
                (SoundCue){CAT_CHAMPION, c->portrait, SOUND_ENTRY_SCREAM, SOUND_PRIORITY_FALL, SOUND_VOLUME_FULL},
                SOUND_FALLBACK, g_game.party_x, g_game.party_y, SOUND_DEFERRED);
        }
        t->fell_hurt = true;
    }
}

/* A pit to a marker: the thing goes to a random pit target marker of the marker id on the pit square. */
static void transit_pit_marker(Transit *t)
{
    int marker = 0;
    for (uint16_t th = first_thing(t->x, t->y); th != THING_ENDOFLIST; th = dungeon_next_thing(th)) {
        /* as in the original, t->type keeps the type of the last thing read; the stairs check reads it */
        t->type = thing_type(th);
        if (t->type > THING_SENSOR)
            break;
        if (t->type != THING_TEXT)
            continue;
        if (textrec_kind(th) == TEXT_KIND_ORNAMENT && textrec_subtype(th) == MARKER_PIT_TARGET) {
            marker = textrec_value(th);
            break;
        }
    }
    MarkerSearch s = {
        .marker = marker, .at = {-1, t->x, t->y}
    };
    int count = count_markers(&s);
    /* without the target marker, the thing goes to map 0, as in the original */
    t->map = find_nth_marker(&s, rand_below((uint16_t)count) + 1) ? s.at.map : 0;
    t->x = s.at.x;
    t->y = s.at.y;
}

/* An open pit at (x, y): the thing falls to the map below or to a pit target marker; a creature of a marker pit goes home. */
static void transit_pit(Transit *t)
{
    int to_marker = gdat_entry_index(gdat_current(), CAT_WALLSET, dungeon_map_wallset(g_party.cur_map), GDAT_TYPE_VALUE,
                                     MAP_KEY_PIT_TO_MARKER);
    if (to_marker && t->type == THING_GROUP) {
        uint16_t home = thing_word(t->thing, GROUP_HOME);
        t->x = location_x(home);
        t->y = location_y(home);
        t->map = location_map(home);
        dun_set_current_map(t->map);
        return;
    }
    if (t->draw_fall && !to_marker && !g_party.fell) {
        t->draw_fall = false;
        if (t->falls) {
            dun_set_party_position((Position){t->map, t->x, t->y});
            g_hooks.enter_map_view(t->map);
            world_update_day_night(false);
        }
        int steps = g_party.move_steps;
        g_party.move_steps = 0;
        g_hooks.draw_dungeon(g_game.party_dir, t->x, t->y);
        g_party.move_steps = (int16_t)steps;
    }
    if (to_marker) {
        transit_pit_marker(t);
    } else {
        t->falls++;
        Position below = dungeon_location_on_map((Position){t->map, t->x, t->y}, LEVEL_DOWN);
        t->map = below.map;
        t->x = below.x;
        t->y = below.y;
    }
    dun_set_current_map(t->map);
    if (t->thing == THING_NONE) {
        set_party_square((Location){t->x, t->y});
        if (g_game.champion_count)
            transit_party_lands(t);
        g_party.fell = 0;
        return;
    }
    if (t->type == THING_GROUP && t->creature_sound && t->src_x >= 0) {
        dun_set_current_map(t->start_map);
        creature_alert(t->thing, (Location){t->src_x, t->src_y}, 0, 0, FALL_ALERT_DAMAGE);
    }
    dun_set_current_map(t->map);
}

/* Stairs move an object to the square at their exit, on the map below for stairs down. */
static void transit_stairs(Transit *t, uint8_t square)
{
    if (!(square & SQUARE_STAIRS_UP)) {
        Position below = dungeon_location_on_map((Position){t->map, t->x, t->y}, LEVEL_DOWN);
        t->map = below.map;
        t->x = below.x;
        t->y = below.y;
        dun_set_current_map(t->map);
    }
    int dir = dun_get_stairs_exit_direction((Location){t->x, t->y});
    t->x += g_dir_dx[dir];
    t->y += g_dir_dy[dir];
    t->thing = thing_with_cell(t->thing, dir_side_cell((int16_t)thing_cell(t->thing), (int16_t)dir_opposite(dir)));
}

/* The end of the way, also in g_party.dest_*; an object that fell and champions that fell hurt make their sounds. */
static MoveDest transit_finish(const Transit *t)
{
    MoveDest dest = {t->x, t->y, t->map, (int16_t)thing_cell(t->thing), g_party.dest_dir};
    g_party.dest_x = dest.x;
    g_party.dest_y = dest.y;
    g_party.dest_map = dest.map;
    g_party.dest_cell = dest.cell;
    if (t->falls && t->creature_sound && t->thing != THING_NONE)
        sndreq_request_with_fallback((SoundCue){item_gdat_category(t->thing), item_gdat_index(t->thing),
                                                SOUND_ENTRY_DROP, SOUND_PRIORITY_DROP, SOUND_VOLUME_NORMAL},
                                     SOUND_FALLBACK, t->x, t->y, SOUND_STEP_END);
    if (!t->fell_hurt)
        return dest;
    int saved_map = g_party.cur_map;
    int px = g_game.party_x;
    int py = g_game.party_y;
    int pmap = g_game.party_map;
    g_party.cur_map = CUR_MAP_NONE; /* dun_set_party_position reads the party position again */
    dun_set_party_position((Position){t->map, t->x, t->y});
    for (int i = 0; i < g_game.champion_count; i++)
        if (champion(i)->health)
            sndreq_request_after(sndreq_cue_with_fallback((SoundCue){CAT_CHAMPION, champion(i)->portrait, SOUND_HURT,
                                                                     SOUND_PRIORITY_HURT, SOUND_VOLUME_LOUD},
                                                          SOUND_FALLBACK),
                                 t->x, t->y, SOUND_DELAY_HURT);
    dun_set_party_position((Position){(int16_t)pmap, (int16_t)px, (int16_t)py});
    dun_set_current_map(saved_map);
    return dest;
}

Transition move_follow_square_transitions(uint16_t thing, MovePath route, bool creature_sound)
{
    g_party.teleporter_flag = 0;
    Transit t = {.thing = thing,
                 .src_x = route.src_x,
                 .src_y = route.src_y,
                 .x = route.dst_x,
                 .y = route.dst_y,
                 .map = g_party.cur_map,
                 .start_map = g_party.cur_map,
                 .creature_sound = creature_sound};
    if (thing == THING_NONE) {
        if (g_party.push_mode) {
            g_party.push_mode = false;
            g_ui.push_mode = false;
            g_hooks.draw_movement_arrows();
        }
        set_party_square((Location){route.dst_x, route.dst_y});
        t.scope = SCOPE_PARTY;
        t.draw_fall = !g_ui.inventory && !g_party_sleeping;
        t.type = -1;
    } else {
        t.flying = is_flying_thing(thing);
        t.type = thing_type(thing);
        t.scope = SCOPE_ALL;
        if (t.type == THING_GROUP)
            t.scope = item_attr(thing, ATTR_TELEPORT_SCOPE) ? SCOPE_PARTY : SCOPE_CREATURES;
        else if (t.type == THING_PROJECTILE)
            g_party.dest_dir =
                (int16_t)projectile_dir(thing); /* the flight direction: bits 10-11 of the position word */
    }
    for (int loop = TRANSITION_MAX; --loop;) {
        uint8_t square = dungeon_square(g_party.cur_map, t.x, t.y);
        int square_kind = square_type(square);
        if (square_kind == SQUARE_TELEPORTER) {
            if (!transit_teleporter(&t, square))
                break;
        } else if (square_kind == SQUARE_PIT && !t.flying && square & SQUARE_PIT_OPEN && !(square & SQUARE_FLAG_0)) {
            transit_pit(&t);
        } else if (square_kind == SQUARE_STAIRS && t.thing != THING_NONE && t.type != THING_PROJECTILE &&
                   t.type != THING_GROUP) {
            transit_stairs(&t, square);
        } else {
            break;
        }
    }
    MoveDest dest = transit_finish(&t);
    dun_set_current_map(t.start_map);
    sndreq_flush_requests(true);
    return (Transition){dest, t.flying};
}

/* A free square beside at for thing (THING_NONE: the party); x -1 when there is none. */
static Location find_free_adjacent_square(uint16_t thing, Location at)
{
    int row = rand_2bits();
    const uint8_t *order = thing == THING_NONE ? free_square_order_party[row] : free_square_order_thing[row];
    int base = thing == THING_NONE ? g_game.party_dir : 0;
    for (int i = 0; i < DIR_COUNT; i++) {
        int dir = dir_turn(order[i], base);
        Location next = {(int16_t)(at.x + g_dir_dx[dir]), (int16_t)(at.y + g_dir_dy[dir])};
        uint8_t square = dun_get_square(next);
        if (dun_square_blocks_movement(square))
            continue;
        int type = square_type(square);
        if (type == SQUARE_STAIRS)
            continue;
        if (thing != THING_NONE && thing_type(thing) == THING_GROUP &&
            (type == SQUARE_TELEPORTER || type == SQUARE_PIT))
            continue;
        uint16_t g = creature_group_at((Position){g_party.cur_map, next.x, next.y});
        if (g != THING_NONE && !(thing == THING_NONE && creature_group_info(g)->flags & CREATURE_NOT_SOLID))
            continue;
        return next;
    }
    return (Location){-1, -1};
}

/* A creature arrives on the party square at: the party steps aside and takes damage; false when it cannot. */
static bool push_party_aside(uint16_t thing, Location at)
{
    Location aside = find_free_adjacent_square(THING_NONE, at);
    if (aside.x < 0)
        return false;
    int damage =
        rand_below(DISPLACE_DAMAGE_ROLL) + (creature_group_info(thing)->push_resistance >> DISPLACE_RESISTANCE_SHIFT);
    champ_damage_party((uint16_t)damage, WOUND_HEAD, ATTACK_TYPE_SELF);
    sndreq_request_with_fallback(
        (SoundCue){CAT_CHAMPION, SOUND_FALLBACK, SOUND_ENTRY_PUSHED, SOUND_PRIORITY_PUSHED, SOUND_VOLUME_LOUD},
        SOUND_FALLBACK, at.x, at.y, SOUND_STEP_END);
    move_thing(THING_NONE, at.x, at.y, aside.x, aside.y);
    return true;
}

/* The game ticks that a champion needs for a step, for the creature alert of a party arrival. */
static int get_step_time(int16_t champion_index)
{
    Champion *c = champion(champion_index);
    if (!c->health)
        return 0;
    /* kept quirk: the original tests the sum of load / 10 and the flag, not the flag alone */
    return (uint16_t)champ_get_load(champion_index) / LOAD_TIME_DIVISOR + (c->redraw & REDRAW_SLOW_STEP)
               ? WALK_TIME_SLOW
               : WALK_TIME;
}

/* The sum of the step times of the champions, for the creature alert of a party arrival. */
static int get_party_step_time(void)
{
    int sum = 0;
    for (int i = 0; i < g_game.champion_count; i++)
        sum += get_step_time(i);
    return sum;
}

/* Keeps the position of the projectiles that a group carries; waits: the group waits for a retry of its move. */
static void update_carried_missiles(uint16_t holder, Position at, bool waits)
{
    for (uint16_t t = thing_word(holder, GROUP_POSSESSIONS); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_PROJECTILE)
            continue;
        thing_set_word(t, PROJECTILE_LOCATION, (uint16_t)location_pack(at.x, at.y, at.map));
        projectile_set_holder_waits(t, waits);
    }
}

/* Schedules a new try of a group move that a full square stopped. */
static void schedule_group_retry(uint16_t group, Position at, bool audible)
{
    TimelineEvent e = {.time_map = timeline_time_map(g_game.game_time + GROUP_RETRY_DELAY, at.map)};
    e.type = audible ? EVENT_MOVE_GROUP_2 : EVENT_MOVE_GROUP;
    e.data[EVENT_X] = (uint8_t)at.x;
    e.data[EVENT_Y] = (uint8_t)at.y;
    put_le16(e.data + EVENT_GROUP_WORD, group);
    update_carried_missiles(group, at, true);
    timeline_add_event(&e);
}

/* The party arrives on the destination of m: a creature there moves aside or away, then the floor sensors react. */
static void arrive_party(const ThingMove *m)
{
    int16_t map = m->map;
    int16_t dst_x = m->path.dst_x;
    int16_t dst_y = m->path.dst_y;
    dun_set_current_map(map);
    move_compute_map_arrival((Position){map, dst_x, dst_y});
    uint16_t g = creature_group_at((Position){g_party.cur_map, g_game.party_x, g_game.party_y});
    if (g != THING_NONE && !(creature_group_info(g)->flags & CREATURE_NOT_SOLID)) {
        Location dst = {dst_x, dst_y};
        const CreatureInfo *info = creature_group_info(g);
        bool displaced = false;
        Location aside;
        if (info->flags & CREATURE_STATIC && info->push_resistance >= PUSH_IMMOVABLE) {
            aside = find_free_adjacent_square(THING_NONE, dst);
            if (aside.x >= 0) {
                dun_set_party_position((Position){map, aside.x, aside.y});
                g_party.new_map = map;
                move_thing(THING_NONE, MOVE_NO_SQUARE, 0, aside.x, aside.y);
                dun_set_party_position((Position){m->start_map, g_game.party_x, g_game.party_y});
                if (g_party.new_map != m->start_map)
                    return;
                g_party.new_map = -1;
                displaced = true;
            }
        } else {
            aside = find_free_adjacent_square(g, dst);
            if (aside.x >= 0) {
                int t = (uint16_t)get_party_step_time() >> ARRIVAL_ALERT_SHIFT;
                creature_alert(g, dst, ALERT_LOUD, 0, (uint16_t)(rand_below((uint16_t)(((int16_t)t >> 1) + 1)) + t));
                move_thing(g, dst_x, dst_y, aside.x, aside.y);
                displaced = true;
            }
        }
        if (!displaced) {
            creature_cancel_update(g);
            if (!move_thing(g, g_game.party_x, g_game.party_y, MOVE_RETRY, 0))
                schedule_group_retry(g, (Position){map, g_game.party_x, g_game.party_y}, false);
        }
    }
    if (map == m->start_map)
        sensor_enter_square((Location){g_game.party_x, g_game.party_y}, THING_NONE, m->same, false);
    else
        g_party.new_map = map;
}

/* A creature group meets the square to: the party or another group can block it or move aside. */
static GroupArrival meet_target_square(uint16_t thing, Position to)
{
    Location dst = {to.x, to.y};
    if (to.map == g_party.square_map && to.x == g_party.square_x && to.y == g_party.square_y &&
        !(creature_group_info(thing)->flags & CREATURE_NOT_SOLID) && !push_party_aside(thing, dst))
        return ARRIVAL_WAIT;
    uint16_t other = creature_group_at((Position){g_party.cur_map, to.x, to.y});
    if (other == THING_NONE)
        return ARRIVAL_ENTER;
    int push = creature_group_info(thing)->push_resistance;
    const CreatureInfo *oi = creature_group_info(other);
    const CreatureInfo *ti = creature_group_info(thing);
    Location aside;
    if (ti->flags & CREATURE_STATIC && push >= PUSH_IMMOVABLE) {
        if (oi->flags & CREATURE_STATIC && oi->push_resistance >= PUSH_IMMOVABLE)
            return ARRIVAL_WAIT;
        aside = find_free_adjacent_square(other, dst);
        if (aside.x < 0) {
            creature_cancel_update(thing);
            schedule_group_retry(thing, to, false);
            return ARRIVAL_DONE;
        }
    } else if (oi->flags & CREATURE_STATIC && oi->push_resistance >= PUSH_IMMOVABLE) {
        aside = find_free_adjacent_square(thing, dst);
        if (aside.x < 0)
            return ARRIVAL_WAIT;
        move_thing(thing, MOVE_NO_SQUARE, 0, aside.x, aside.y);
        return ARRIVAL_DONE;
    } else {
        aside = find_free_adjacent_square(other, dst);
        if (aside.x < 0)
            return ARRIVAL_WAIT;
    }
    int r = rand_below((uint16_t)(((int16_t)(push >> PUSH_ALERT_SHIFT) >> 1) + 1));
    creature_alert(other, dst, ALERT_LOUD, 0, (uint16_t)((push >> PUSH_ALERT_SHIFT) + r));
    move_thing(other, to.x, to.y, aside.x, aside.y);
    return ARRIVAL_ENTER;
}

/* A creature group arrives on the destination of m; true when it must wait and retry. */
static bool arrive_group(const ThingMove *m)
{
    uint16_t thing = m->thing;
    int16_t map = m->map;
    int16_t dst_x = m->path.dst_x;
    int16_t dst_y = m->path.dst_y;
    dun_set_current_map(map);
    int active = group_active(thing);
    GroupArrival arrival = meet_target_square(thing, (Position){map, dst_x, dst_y});
    if (arrival == ARRIVAL_WAIT) {
        if (active != GROUP_NOT_ACTIVE)
            creature_deactivate_group(active);
        schedule_group_retry(thing, (Position){map, dst_x, dst_y}, g_party.teleporter_flag);
        return true;
    }
    if (arrival == ARRIVAL_DONE)
        return false;
    if (m->flying)
        dungeon_link_thing_to_square(thing, g_party.cur_map, dst_x, dst_y);
    else
        sensor_enter_square((Location){dst_x, dst_y}, thing, false, false);
    update_carried_missiles(thing, (Position){map, dst_x, dst_y}, false);
    if (active == GROUP_NOT_ACTIVE) {
        if (!(creature_group_info(thing)->flags & CREATURE_STATIC))
            creature_activate_group(thing, (Location){dst_x, dst_y});
    } else {
        int event = creature_active_group_event(active);
        if (event != -1) {
            TimelineEvent *e = &g_timeline.events[event];
            e->data[EVENT_X] = (uint8_t)dst_x;
            e->data[EVENT_Y] = (uint8_t)dst_y;
            timeline_set_event_map(e, map);
        }
    }
    return false;
}

/* An object or projectile arrives on the destination of m: the floor sensors there react. */
static void arrive_object(const ThingMove *m)
{
    int16_t dst_x = m->path.dst_x;
    int16_t dst_y = m->path.dst_y;
    dun_set_current_map(m->map);
    if (m->type == THING_PROJECTILE) {
        dungeon_link_thing_to_square(m->thing, g_party.cur_map, dst_x, dst_y);
        return;
    }
    bool added = m->path.src_x < 0 && m->path.src_y == MOVE_IN_POSSESSIONS;
    bool under_static = added;
    if (!added && m->map != m->start_map) {
        uint16_t g = creature_group_at((Position){g_party.cur_map, dst_x, dst_y});
        if (g != THING_NONE)
            under_static = creature_group_info(g)->flags & CREATURE_STATIC;
    }
    sensor_enter_square((Location){dst_x, dst_y}, m->thing, dun_on_party_square((Location){dst_x, dst_y}),
                        under_static);
}

/* A thing (THING_NONE: the party) leaves the source of m: the floor sensors there react; a group that leaves the
   dungeon frees its active group. */
static void leave_square(const ThingMove *m)
{
    int16_t src_x = m->path.src_x;
    int16_t src_y = m->path.src_y;
    if (m->thing == THING_NONE) {
        sensor_leave_square((Location){src_x, src_y}, THING_NONE, m->same, false);
        return;
    }
    if (m->flying)
        dungeon_unlink_thing_from_square(m->thing, g_party.cur_map, src_x, src_y);
    else
        sensor_leave_square((Location){src_x, src_y}, m->thing, dun_on_party_square((Location){src_x, src_y}),
                            m->path.dst_x < 0 && m->path.dst_y == MOVE_IN_POSSESSIONS);
    if (thing_type(m->thing) == THING_GROUP && m->path.dst_x < MOVE_NO_SQUARE) {
        int a = group_active(m->thing);
        if (a != GROUP_NOT_ACTIVE)
            creature_deactivate_group(a);
    }
}

/* The thing of m follows pits, teleporters, and stairs to its destination; false when a group that is not native to
   that map left the dungeon there. */
static bool reach_destination(ThingMove *m)
{
    Transition way = move_follow_square_transitions(m->thing, m->path, true);
    m->flying = way.flying;
    m->dest_cell = way.dest.cell;
    if (m->thing != THING_NONE) {
        if (item_attr(m->thing, ATTR_LIGHT_SOURCE) &&
            (m->start_map == g_game.party_map || way.dest.map == g_game.party_map))
            g_party.redraw |= PARTY_REDRAW_VIEW;
        m->thing = thing_with_cell(m->thing, way.dest.cell);
    }
    m->path.dst_x = way.dest.x;
    m->path.dst_y = way.dest.y;
    m->dest_dir = way.dest.dir;
    m->map = way.dest.map;
    dun_set_current_map(m->map);
    if (m->type == THING_GROUP && !creature_is_native_to_map(m->thing, m->map)) {
        creature_drop_possessions(m->thing, (Location){m->path.dst_x, m->path.dst_y}, DROP_ALL, true);
        dun_set_current_map(m->start_map);
        if (m->path.src_x >= 0)
            creature_delete_group_at((Location){m->path.src_x, m->path.src_y}, DROP_NOTHING, false);
        return false;
    }
    m->same = m->map == m->start_map && m->path.dst_x == m->path.src_x && m->path.dst_y == m->path.src_y;
    if (m->same) {
        m->stays = m->thing == THING_NONE ? g_game.party_dir == m->start_dir
                                          : m->dest_cell == m->cell && m->type != THING_PROJECTILE;
    } else if (m->map != m->start_map) {
        if (m->thing == THING_NONE && g_game.party_map != m->start_map)
            dun_set_party_position((Position){m->start_map, g_game.party_x, g_game.party_y});
        else
            dun_set_current_map(m->start_map);
    }
    return true;
}

/* The result of a move and where its thing ends. */
static MoveOutcome outcome_of(const ThingMove *m, MoveResult result)
{
    return (MoveOutcome){
        result, {m->path.dst_x, m->path.dst_y, m->map, m->dest_cell, m->dest_dir}
    };
}

MoveResult move_thing(uint16_t thing, int src_x, int src_y, int dst_x, int dst_y)
{
    return move_thing_dest(thing, src_x, src_y, dst_x, dst_y).result;
}

MoveOutcome move_thing_dest(uint16_t thing, int src_x, int src_y, int dst_x, int dst_y)
{
    g_party.teleporter_flag = 0;
    ThingMove m = {
        .thing = thing,
        .type = -1,
        .path = {src_x, src_y, dst_x, dst_y},
        .start_map = g_party.cur_map,
        .start_dir = g_game.party_dir,
        .dest_dir = g_party.dest_dir,
        .dest_cell = -1
    };
    m.map = m.start_map;
    if (thing == THING_NONE) {
        g_party.redraw = PARTY_REDRAW_ALL;
        if (src_x >= 0)
            projectile_check_missiles_hit_party((Location){(int16_t)src_x, (int16_t)src_y},
                                                (Location){(int16_t)dst_x, (int16_t)dst_y});
    } else {
        m.type = thing_type(thing);
        m.cell = thing_cell(thing);
    }
    if (dst_x >= 0 && !reach_destination(&m))
        return outcome_of(&m, MOVE_LEFT_DUNGEON);
    if (!m.stays && src_x >= 0)
        leave_square(&m);
    if (!m.stays && m.path.dst_x >= 0) {
        if (thing == THING_NONE)
            arrive_party(&m);
        else if (m.type == THING_GROUP) {
            if (arrive_group(&m))
                return outcome_of(&m, MOVE_WAIT);
        } else {
            arrive_object(&m);
        }
    }
    g_party.dest_dir = m.dest_dir;
    g_party.dest_x = m.path.dst_x;
    g_party.dest_y = m.path.dst_y;
    g_party.dest_map = m.map; /* the original writes a stale register when no destination is given */
    dun_set_current_map(m.start_map);
    return outcome_of(&m, MOVE_DONE);
}

/* ---- Discarded things: the reuse of records when a thing table is full ---- */

/* The next map of the discard search after map: the party map comes last; alt (the map of a seamless passage, -1
   none) comes when the search is back at stop. */
static int16_t discard_next_map(int16_t map, int stop, int *alt)
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
static bool discard_take(uint16_t t, int type, int subtype, DiscardSquare at, int *reserved)
{
    int kind;
    switch (type) {
    case THING_GROUP:
        if (group_flag(t, GROUP_DO_NOT_DISCARD) || thing_word(t, GROUP_POSSESSIONS) != THING_ENDOFLIST ||
            (subtype != DISCARD_ANY_KIND && subtype != group_creature(t)))
            return false;
        dun_set_current_map(at.map);
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
    dun_set_current_map(at.map);
    move_thing(t, at.x, at.y, MOVE_NO_SQUARE, at.holder != THING_NONE ? MOVE_IN_POSSESSIONS : 0);
    return true;
}

/* Searches map for a thing of type that may go and takes it; THING_NONE when there is none. The second pass on the
   party map lets a shop display keep one item of its kind and lets the other things of its square go. */
static uint16_t discard_scan_map(int type, int subtype, int16_t map, bool second)
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
                        if (discard_take(t, type, subtype, at, &reserved))
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

uint16_t dun_get_discarded_thing(int type, int subtype)
{
    if (type == THING_EXPLOSION)
        return THING_NONE;
    int saved = g_party.cur_map;
    int alt = g_party.alt_view_active ? g_party.alt_view_map : -1;
    int map = g_last_discarded_map[type];
    if (map == g_game.party_map || map == alt)
        map = discard_next_map(map, map, &alt);
    int start = map;
    for (bool second = false;;) {
        uint16_t t = discard_scan_map(type, subtype, map, second);
        if (t != THING_NONE) {
            dun_set_current_map(saved);
            g_last_discarded_map[type] = (uint8_t)map;
            return t & THING_ID_MASK;
        }
        if (map != g_game.party_map && dungeon_map_count() > 1) {
            map = discard_next_map(map, start, &alt);
        } else if (second) {
            g_last_discarded_map[type] = (uint8_t)map;
            return THING_NONE;
        } else {
            second = true;
        }
    }
}

/* Level links, pits, and teleporters. */

LevelLink dun_find_adjacent_level_link(Location at, int level_delta)
{
    for (int d = 0; d < DIR_COUNT; d++) {
        int16_t sx = (int16_t)(at.x + g_dir_dx[d]);
        int16_t sy = (int16_t)(at.y + g_dir_dy[d]);
        for (uint16_t t = dungeon_square_first_thing(g_party.cur_map, sx, sy); t != THING_ENDOFLIST;
             t = dungeon_next_thing(t)) {
            int type = thing_type(t);
            if (type > THING_SENSOR)
                break;
            if (type == THING_SENSOR) {
                if (sensor_type(t) == SENSOR_LEVEL_LINK &&
                    level_delta == (dungeon_map_level(sensor_data(t)) & MAP_LEVEL_MASK) -
                                       (dungeon_map_level(g_party.cur_map) & MAP_LEVEL_MASK)) {
                    return (LevelLink){(int16_t)d, t};
                }
            } else if (type == THING_TEXT && textrec_kind(t) == TEXT_KIND_ORNAMENT) {
                int kind = textrec_subtype(t);
                if (kind < TEXT_LINK_FIRST || kind > TEXT_LINK_LAST)
                    continue;
                int16_t up = (int16_t)gdat_entry_index(gdat_current(), CAT_WALL_ORNAMENT, textrec_value(t),
                                                       GDAT_TYPE_VALUE, ORNAMENT_KEY_UP);
                if ((level_delta == LEVEL_UP && up) || (level_delta == LEVEL_DOWN && !up)) {
                    return (LevelLink){(int16_t)d, THING_NONE};
                }
            }
        }
    }
    return (LevelLink){-1, THING_NONE};
}

/* The things on at fall or teleport through its open pit or teleporter. */
static void pass_square_contents(Location at)
{
    int16_t x = at.x;
    int16_t y = at.y;
    if (dun_on_party_square(at))
        move_thing(THING_NONE, x, y, x, y);
    uint16_t g = creature_group_at((Position){g_party.cur_map, x, y});
    if (g != THING_NONE)
        move_thing(g, x, y, x, y);
    uint16_t t = dungeon_square_first_object(g_party.cur_map, x, y);
    int count = 0;
    for (uint16_t u = t; u != THING_ENDOFLIST; u = dungeon_next_thing(u))
        if (thing_type(u) > THING_GROUP)
            count++;
    while (t != THING_ENDOFLIST && count) {
        uint16_t thing = t;
        t = dungeon_next_thing(thing);
        int type = thing_type(thing);
        if (type <= THING_GROUP)
            continue;
        count--;
        MoveDest dest = move_thing_dest(thing, x, y, x, y).dest;
        if (type == THING_PROJECTILE)
            projectile_move_event(thing, dest);
        else if (type == THING_EXPLOSION)
            explosion_move_events(thing, dest);
    }
}

bool dun_set_pit_teleporter_state(Location at, int mode)
{
    uint8_t *square = dungeon_square_byte(g_party.cur_map, at.x, at.y);
    uint8_t on_flag;
    if (square_type(*square) == SQUARE_PIT) {
        on_flag = SQUARE_PIT_OPEN;
    } else if (square_type(*square) == SQUARE_TELEPORTER) {
        if (teleporter_is_fixed(dungeon_square_first_thing(g_party.cur_map, at.x, at.y)))
            return false;
        on_flag = SQUARE_TELEPORTER_ON;
    } else {
        return false;
    }
    bool open = (*square & on_flag) != 0;
    /* PIT_STATE_TOGGLE closes an open square and opens a closed one: it toggles as in the original. */
    if (mode == PIT_STATE_TOGGLE)
        mode = open ? PIT_STATE_CLOSED : PIT_STATE_OPEN;
    if (mode == PIT_STATE_OPEN) {
        *square |= on_flag;
        pass_square_contents(at);
        return !open;
    }
    *square &= (uint8_t)~on_flag;
    return open;
}

/* Pushing creature groups. */

bool move_party_can_push_group(uint16_t group)
{
    int16_t resistance = creature_group_info(group)->push_resistance;
    if (resistance >= PUSH_IMMOVABLE)
        return false;
    bool pushed = false;
    for (int i = 0; i < g_game.champion_count; i++) {
        Champion *c = champion(i);
        if (!c->health)
            continue;
        int16_t s = (int16_t)champ_get_stat(c, STAT_STRENGTH, 0);
        if (i == g_game.leader)
            s = (int16_t)(s + (s >> PUSH_LEADER_SHIFT));
        if (resistance < PUSH_LIGHT)
            s = (int16_t)(s + (s >> PUSH_LIGHT_SHIFT));
        s = (int16_t)champ_scale_by_stamina(c, s);
        if (resistance <= s || !rand_below((uint16_t)(((resistance - s) >> 1) + 1))) {
            pushed = true;
            break;
        }
    }
    for (int i = 0; i < g_game.champion_count; i++) {
        Champion *c = champion(i);
        if (c->health && c->max_stamina >> PUSH_STAMINA_SHIFT < c->stamina) {
            int16_t cost = PUSH_FAIL_STAMINA;
            if (pushed && resistance >> PUSH_COST_SHIFT > PUSH_FAIL_STAMINA)
                cost = (int16_t)(resistance >> PUSH_COST_SHIFT);
            champ_decrease_stamina(i, cost);
        }
    }
    return pushed;
}

/* The warp of the party. */

bool move_party_warp_to_marker(void)
{
    for (uint16_t t = dungeon_square_first_thing(g_party.cur_map, g_game.party_x, g_game.party_y);
         t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_TEXT)
            continue;
        if (textrec_kind(t) != TEXT_KIND_ORNAMENT)
            continue;
        int subtype = textrec_subtype(t);
        if (subtype != SUBTYPE_WARP_ONCE && subtype != SUBTYPE_WARP)
            continue;
        bool once = subtype == SUBTYPE_WARP_ONCE;
        uint8_t marker = textrec_value(t);
        MarkerSearch s = {
            .marker = marker, .at = {-1, 0, 0}
        };
        if (!(once ? clear_switch_markers(&s) : find_set_switch_marker(&s)))
            continue;
        move_party_to(s.at, g_game.party_dir);
        sndreq_request_with_fallback(
            (SoundCue){CAT_FLOOR_ORNAMENT, marker, SOUND_ENTRY_ACTIVATE, SOUND_PRIORITY_CLICK, SOUND_VOLUME_NORMAL},
            SOUND_FALLBACK, g_game.party_x, g_game.party_y, SOUND_STEP_END);
        if (once)
            textrec_set_visible(t, true);
        return true;
    }
    return false;
}

/* Party state that the interface sets. */

void move_set_push_mode(bool on)
{
    g_party.push_mode = on;
    g_ui.push_mode = on;
}

void move_set_push_arrow(int arrow)
{
    g_party.push_arrow = (int16_t)arrow;
}

void move_set_walk_steps(int steps)
{
    g_party.move_steps = (int16_t)steps;
}
