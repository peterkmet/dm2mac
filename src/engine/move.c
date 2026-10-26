/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Party movement and thing movement through pits, teleporters, and stairs. */
#include "move.h"
#include <stdlib.h>
#include <string.h>
#include "aspect.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "game.h"
#include "gdat.h"
#include "hooks.h"
#include "item.h"
#include "projectile.h"
#include "random.h"
#include "save.h"
#include "sensor.h"
#include "sound_request.h"
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
#define STAMINA_REDRAW_LIMIT      10
#define LOAD_TIME_DIVISOR         10 /* the step time of a champion for a creature alert: load / 10 */
#define WALK_TIME_SLOW            50
#define WALK_TIME                 40
#define LOAD_HEAVY_PART           5 /* a load above 5/8 of the maximum load slows the champion */
#define LOAD_HEAVY_EIGHTHS        8
#define MOVE_DELAY_HEAVY          3
#define MOVE_DELAY_LIGHT          2
#define OVERLOAD_DELAY_FACTOR     4 /* an overloaded champion: (load - max) * 4 / max + 4 */
#define OVERLOAD_DELAY_BASE       4
/* the damage of a creature that lands on the party: 0 to 9, plus its push resistance / 8 */
#define DISPLACE_DAMAGE_ROLL      10
#define DISPLACE_RESISTANCE_SHIFT 3
#define FALL_ALERT_DAMAGE         20 /* the damage alert of a creature that falls into a pit */
#define ALERT_MOOD_BORED          5  /* alert flags: mood bit 5, MOOD_BORED */
#define BUMP_ALERT_CHANCE         5
#define ARRIVAL_ALERT_SHIFT 2 /* a creature that the party pushes aside: alert damage of a quarter of the step time */
#define PUSH_ALERT_SHIFT 2 /* a creature that another pushes aside: alert damage of a quarter of the push resistance */
#define HIT_OFFSETS_PER_CELL 2 /* creature_find_group_near_point: the squares to check by cell */
#define HIT_OFFSETS_READ     4

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
#define CELL_ORDER_BACK  2 /* champ_get_nearest_at: the first of the two back cells */
#define FREE_SQUARE_ROWS 4 /* find_free_adjacent_square: one row for each value of a 2-bit roll */
#define BACK_STEP        2 /* the step of CMD_MOVE_BACK, counted from CMD_MOVE_FORWARD */

/* The discard search. */
#define SENSOR_KIND_TABLE 44 /* the sensor kinds of sensor_blocks_discard */
#define DISCARD_NEAR      5  /* the things this near to the party on its map stay */

/* The step from a square to a square beside it. */
typedef struct {
    int16_t dx, dy;
} SquareStep;

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
    int dir;
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
    int cell;
    MovePath path;
    int map, start_map, start_dir, dest_dir;
    bool flying, same, stays;
} ThingMove;

/* How a group meets its target square: it enters (the square is free, or another group moved aside), it is done
   (it went elsewhere, or a retry is queued), or it waits. */
typedef enum { ARRIVAL_ENTER, ARRIVAL_DONE, ARRIVAL_WAIT } GroupArrival;

/* A square of the discard search, and the static creature there whose possessions the search reads (THING_NONE
   none). */
typedef struct {
    int map, x, y;
    uint16_t holder;
} DiscardSquare;

const int8_t g_dir_dx[DIR_COUNT] = {0, 1, 0, -1}, g_dir_dy[DIR_COUNT] = {-1, 0, 1, 0};
/* Steps of the move commands 3 to 6: forward and right parts. */
static const int8_t command_forward[DIR_COUNT] = {1, 0, -1, 0}, command_right[DIR_COUNT] = {0, 1, 0, -1};
/* The order in which find_free_adjacent_square tries the 4 sides, by a random row: for the party
   relative to its direction, for things absolute. */
static const uint8_t free_square_order_party[FREE_SQUARE_ROWS][DIR_COUNT] = {
    {2, 0, 1, 3},
    {2, 1, 0, 3},
    {2, 3, 0, 1},
    {2, 1, 3, 0}
};
static const uint8_t free_square_order_thing[FREE_SQUARE_ROWS][DIR_COUNT] = {
    {0, 2, 1, 3},
    {1, 0, 2, 3},
    {2, 3, 0, 1},
    {3, 1, 2, 0}
};
/* The sensor kinds whose square keeps its things. */
static const uint8_t sensor_blocks_discard[SENSOR_KIND_TABLE] = {
    0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1,
};

Party g_party = {.cur_map = -1, .new_map = -1, .push_pending_group = THING_NONE};
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
    g_party.cur_map = (int16_t)map;
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

void dun_set_party_location(int16_t x, int16_t y, int16_t map)
{
    g_game.party_map = (uint16_t)map;
    g_game.party_x = (uint16_t)x;
    g_game.party_y = (uint16_t)y;
    dun_set_current_map(map);
}

void dun_move_relative(int16_t dir, int forward, int right, int16_t *out_x, int16_t *out_y)
{
    *out_x = (int16_t)(*out_x + g_dir_dx[dir] * forward);
    *out_y = (int16_t)(*out_y + g_dir_dy[dir] * forward);
    int side = dir_turn(dir, TURN_RIGHT);
    *out_x = (int16_t)(*out_x + g_dir_dx[side] * right);
    *out_y = (int16_t)(*out_y + g_dir_dy[side] * right);
}

uint8_t dun_get_square(int16_t x, int16_t y)
{
    return dungeon_square_edge(g_party.cur_map, x, y);
}

bool dun_on_party_square(int16_t x, int16_t y)
{
    return g_party.cur_map == g_party.square_map && x == g_party.square_x && y == g_party.square_y;
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

int dun_get_stairs_exit_direction(int16_t x, int16_t y)
{
    bool along_y = !(dun_get_square(x, y) & SQUARE_STAIRS_ALONG_X);
    int probe = along_y ? DIR_EAST : DIR_NORTH;
    int type = square_type(dun_get_square(x + g_dir_dx[probe], y + g_dir_dy[probe]));
    return (type == SQUARE_WALL || type == SQUARE_STAIRS) * 2 + along_y;
}

bool dun_get_linked_teleporter_dest(PassageEnd *end, int16_t x, int16_t y)
{
    TeleporterLink link;
    if (!dungeon_teleporter_link(g_party.cur_map, x, y, &link))
        return false;
    *end = (PassageEnd){(uint8_t)link.side, (uint8_t)link.target_side, (uint8_t)link.x, (uint8_t)link.y,
                        (uint8_t)link.map};
    return true;
}

/* ---- Champions ---- */

void champ_set_party_direction(int16_t dir)
{
    if (dir == g_game.party_dir)
        return;
    int turn = dir - g_game.party_dir;
    if (turn < 0)
        turn += DIR_COUNT;
    for (int i = 0; i < g_game.champion_count; i++) {
        Champion *c = champion(i);
        c->cell = (uint8_t)dir_turn(c->cell, turn);
        c->dir = (uint8_t)dir_turn(c->dir, turn);
    }
    g_game.party_dir = (uint16_t)dir;
    g_party.ref_dir = (int16_t)dir;
    if (g_party.alt_view_active) {
        g_party.alt_view_dir = (int16_t)dir_turn(g_party.alt_view_dir, turn);
        g_party.ref_dir = g_party.alt_view_dir;
    }
}

int champ_get_move_delay(int champ)
{
    if (g_game.magic_light[MAGIC_HASTE])
        return 1;
    Champion *c = champion(champ);
    unsigned max = (uint16_t)champ_get_max_load(c), load = (uint16_t)champ_get_load(champ);
    int delay, wounded_extra;
    if (max > load) {
        delay = load * LOAD_HEAVY_EIGHTHS > max * LOAD_HEAVY_PART ? MOVE_DELAY_HEAVY : MOVE_DELAY_LIGHT;
        wounded_extra = 1;
    } else {
        delay = (int)((load - max) * OVERLOAD_DELAY_FACTOR) / (int)max + OVERLOAD_DELAY_BASE;
        wounded_extra = 2;
    }
    if (c->wounds & WOUND_FEET)
        delay += wounded_extra;
    delay = (int16_t)(delay - (int8_t)c->move_speed);
    if (delay < 1)
        delay = 1;
    if (delay > 2)
        delay = (delay + 1) & ~1;
    return delay;
}

void champ_decrease_stamina(int champ, int amount)
{
    if (champ == -1)
        return;
    Champion *c = champion(champ);
    c->stamina = (int16_t)(c->stamina - amount);
    if (c->stamina <= 0) {
        int16_t missing = c->stamina;
        c->stamina = 0;
        champ_take_damage(champ, (int16_t)(-missing >> 1), 0, 0);
    } else if (c->stamina > c->max_stamina) {
        c->stamina = c->max_stamina;
    }
    if (abs((int16_t)amount) >= STAMINA_REDRAW_LIMIT)
        c->redraw |= REDRAW_BARS;
}

int dun_get_direction_to(int x1, int y1, int x2, int y2)
{
    int dx = (int16_t)(x1 - x2), dy = (int16_t)(y1 - y2);
    int ax = abs(dx), ay = abs(dy);
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

/* Swaps two adjacent cells of a cell order. */
static void swap_cells(uint8_t *pair)
{
    uint8_t c = pair[0];
    pair[0] = pair[1];
    pair[1] = c;
}

/* The champion of the cell nearest to the side of the party square that faces (x, y), -1 none. */
int champ_get_nearest_at(int16_t x, int16_t y, int16_t cell)
{
    static const uint8_t attack_cell_orders[DIR_COUNT * 2][DIR_COUNT] = {
        {0, 1, 3, 2},
        {1, 0, 2, 3},
        {1, 2, 0, 3},
        {2, 1, 3, 0},
        {3, 2, 0, 1},
        {2, 3, 1, 0},
        {0, 3, 1, 2},
        {3, 0, 2, 1}
    };
    if (!g_game.champion_count)
        return -1;
    if (abs(x - g_party.square_x) + abs(y - g_party.square_y) >= 2)
        return -1;
    int dir = dun_get_direction_to(g_party.square_x, g_party.square_y, x, y);
    uint8_t cells[DIR_COUNT];
    if (cell == CELL_ANY) {
        /* any cell: the cells from the side toward (x, y) on, the two front and the two back cells in random order */
        for (int i = 0; i < DIR_COUNT; i++)
            cells[i] = (uint8_t)dir_turn(dir, i);
        if (rand_bit())
            swap_cells(cells);
        if (rand_bit())
            swap_cells(cells + CELL_ORDER_BACK);
    } else {
        if (!(dir * 2 & 2))
            cell++;
        memcpy(cells, attack_cell_orders[dir * 2 + (cell >> 1 & 1)], sizeof cells);
    }
    for (int i = 0; i < DIR_COUNT; i++) {
        int c = champ_get_index_in_cell(cells[i]);
        if (c >= 0)
            return c;
    }
    return -1;
}

/* A champion (-1 none) who bumps into (x, y) takes 1 damage; the push sound plays when it hurts. */
static void bump_champion(int champ, int x, int y)
{
    if (champ < 0)
        return;
    if (champ_take_damage(champ, 1, BUMP_WOUNDS, ATTACK_TYPE_SELF))
        sndreq_request_with_fallback((SoundCue){CAT_CHAMPION, champion(champ)->portrait, SOUND_ENTRY_PUSHED,
                                                SOUND_PRIORITY_PUSHED, SOUND_VOLUME_LOUD},
                                     SOUND_FALLBACK, x, y, 1);
}

BumpedChampions move_party_bump_damage(int16_t x, int16_t y, int16_t dir, int step)
{
    int side = step + dir + 2;
    BumpedChampions bumped;
    bumped.first = (int16_t)champ_get_nearest_at(x, y, side & DIR_MASK);
    bumped.second = (int16_t)champ_get_nearest_at(x, y, dir_turn(side, TURN_RIGHT));
    bump_champion(bumped.first, x, y);
    if (bumped.second != bumped.first)
        bump_champion(bumped.second, x, y);
    return bumped;
}

/* ---- Steps ---- */

uint16_t creature_find_group_near_point(int16_t *out_x, int16_t *out_y, int16_t cell)
{
    /* The squares to check after the first one: by cell, the original reads 4 pairs from an offset of 2 pairs per
       cell (its table has rows of 4 pairs), which this table keeps. */
    static const SquareStep hit_offsets[HIT_OFFSETS_PER_CELL * (DIR_COUNT - 1) + HIT_OFFSETS_READ] = {
        {-1, 0 },
        {0,  -1},
        {0,  -1},
        {1,  0 },
        {1,  0 },
        {0,  1 },
        {-1, 0 },
        {0,  1 },
        {0,  1 },
        {0,  -1}
    };
    static const SquareStep hit_offsets_any_cell[HIT_OFFSETS_READ] = {
        {0,  1 },
        {0,  -1},
        {1,  0 },
        {-1, 0 }
    };
    static const int16_t hit_radius_sq[] = {0, 3, 6, 6};
    /* the centers of the four cells of the square in the 5 x 5 grid */
    static const uint16_t cell_grid[DIR_COUNT] = {cell_grid_index(1, 1), cell_grid_index(1, 3), cell_grid_index(3, 3),
                                                  cell_grid_index(3, 1)};
    int16_t sx = *out_x, sy = *out_y, px, py, cx, cy;
    aspect_cell_to_xy(sx, sy, cell == CELL_ANY ? CELL_CENTER : cell_grid[cell], &px, &py);
    for (int i = 0; i <= HIT_OFFSETS_READ; i++) {
        uint16_t g = creature_group_at(g_party.cur_map, sx, sy);
        if (g != THING_NONE) {
            int size = (int8_t)creature_group_info(g)->hit_size;
            int zone = aspect_creature_cell(g, dir_relative(DIR_NORTH, group_dir(g)));
            aspect_cell_to_xy(sx, sy, (uint16_t)zone, &cx, &cy);
            cx = (int16_t)(cx - px);
            cy = (int16_t)(cy - py);
            if ((int16_t)(cy * cy + cx * cx) < hit_radius_sq[size]) {
                *out_x = sx;
                *out_y = sy;
                return g;
            }
        }
        if (i == HIT_OFFSETS_READ)
            break;
        if (cell == CELL_ANY) {
            sx = (int16_t)(*out_x + hit_offsets_any_cell[i].dx);
            sy = (int16_t)(*out_y + hit_offsets_any_cell[i].dy);
        } else {
            sx = (int16_t)(*out_x + hit_offsets[cell * HIT_OFFSETS_PER_CELL + i].dx);
            sy = (int16_t)(*out_y + hit_offsets[cell * HIT_OFFSETS_PER_CELL + i].dy);
        }
    }
    return THING_NONE;
}

/* True when the creature in front stands on a side cell that blocks the step. */
static bool is_side_blocked(uint16_t group, int dir)
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
    int16_t gx = s->x, gy = s->y;
    *group = creature_group_at(g_party.cur_map, s->x, s->y);
    if (*group == THING_NONE || creature_group_info(*group)->flags & CREATURE_NOT_SOLID) {
        uint16_t g = creature_find_group_near_point(&gx, &gy, CELL_ANY);
        if (g == THING_NONE || creature_group_info(g)->flags & CREATURE_NOT_SOLID)
            return STEP_FREE;
    } else if (!is_side_blocked(*group, g_game.party_dir)) {
        return STEP_CREATURE;
    }
    return STEP_CREATURE_SIDE;
}

/* The party takes the stairs up or down: it leaves the square and arrives on the map above or below. */
static void take_stairs(int up)
{
    move_thing(THING_NONE, g_game.party_x, g_game.party_y, -1, 0);
    int16_t x = g_game.party_x;
    int16_t y = g_game.party_y;
    g_party.new_map = (int16_t)dungeon_location_on_map(g_game.party_map, up ? -1 : 1, &x, &y);
    g_game.party_x = (uint16_t)x;
    g_game.party_y = (uint16_t)y;
    dun_set_current_map(g_party.new_map);
    champ_set_party_direction(dun_get_stairs_exit_direction(g_game.party_x, g_game.party_y));
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
    g_input_command_stop = 1;
    sync_champion_directions();
    uint8_t square = dun_get_square(g_game.party_x, g_game.party_y);
    if (square_type(square) == SQUARE_STAIRS) {
        take_stairs(square & SQUARE_STAIRS_UP);
        return;
    }
    PassageEnd end;
    if (dun_get_linked_teleporter_dest(&end, g_game.party_x, g_game.party_y)) {
        move_party_to(end.x, end.y, end.map, end.target_side);
        return;
    }
    sensor_leave_square(g_game.party_x, g_game.party_y, THING_NONE, true, false);
    champ_set_party_direction(dir_turn(g_game.party_dir, command == CMD_TURN_RIGHT ? TURN_RIGHT : TURN_LEFT));
    sensor_enter_square(g_game.party_x, g_game.party_y, THING_NONE, true, false);
}

/* The party steps onto the square ahead from the square of map: seamless passages, stairs, teleporter passages, then
   the move. */
static void take_free_step(int map, bool on_stairs, PartyStep s)
{
    if (g_party.alt_view_active && s.step == BACK_STEP) {
        dun_set_current_map(map);
        s.x = g_party.square_x;
        s.y = g_party.square_y;
        s.dir = g_party.ref_dir;
        dun_move_relative(s.dir, -1, 0, &s.x, &s.y);
    }
    if (on_stairs) {
        move_thing(THING_NONE, -1, 0, s.x, s.y);
        return;
    }
    PassageEnd end;
    if (dun_get_linked_teleporter_dest(&end, s.x, s.y) && dir_opposite(end.target_side) != s.dir) {
        move_party_to(end.x, end.y, end.map, end.target_side);
        return;
    }
    move_thing(THING_NONE, g_game.party_x, g_game.party_y, s.x, s.y);
}

/* The strength of a champion who bumps into a door. */
static int bash_strength(int champ)
{
    Champion *c = champion(champ);
    int r = rand_next() & BASH_ROLL_MASK;
    return champ_scale_by_stamina(c, (int16_t)(champ_get_stat(c, STAT_STRENGTH, 0) + r));
}

/* The two champions who bumped into the closed door on (x, y) try to break it with their strength. */
static void bash_door(int x, int y, BumpedChampions bumped)
{
    int strength = 0;
    if (bumped.first != -1)
        strength = bash_strength(bumped.first);
    if (bumped.second != bumped.first && bumped.second != -1)
        strength += bash_strength(bumped.second);
    sensor_door_try_destroy(x, y, (int16_t)strength, 0, 0);
}

/* The party walks into the creature group ahead: a bump, then a push when the group can be pushed; false when the
   party stays. */
static bool push_creature_ahead(const PartyStep *s, uint16_t group)
{
    if (!g_game.champion_count)
        return false;
    bool is_static = creature_group_info(group)->flags & CREATURE_STATIC;
    if (is_static || creature_group_info(group)->push_resistance == PUSH_WALL)
        move_party_bump_damage(s->x, s->y, s->dir, s->step);
    int push_dir = dir_turn(s->dir, s->step);
    if (!creature_can_be_pushed(s->x, s->y, push_dir, &group) || !move_party_can_push_group(group)) {
        if (!is_static)
            creature_alert(THING_NONE, (Location){s->x, s->y}, ALERT_ANGER | ALERT_MOOD_BORED, BUMP_ALERT_CHANCE, 0);
        return false;
    }
    creature_push_group(s->x, s->y, push_dir, PUSH_STRENGTH_PARTY);
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
    dun_move_relative(s.dir, command_forward[s.step], command_right[s.step], &s.x, &s.y);
    s.ahead = dun_get_square(s.x, s.y);
    g_input_command_stop = 1;
    pay_stamina_by_load(LOAD_PER_STEP_FACTOR);
    sync_champion_directions();
    uint16_t group;
    switch (get_step_result(&s, &group)) {
    case STEP_STAIRS_AHEAD:
        move_thing(THING_NONE, g_game.party_x, g_game.party_y, -1, 0);
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
        BumpedChampions bumped = move_party_bump_damage(s.x, s.y, s.dir, s.step);
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
    uint8_t here = dun_get_square(g_party.square_x, g_party.square_y);
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
        if (command != CMD_MOVE_FORWARD)
            dun_move_relative(g_game.party_dir, -1, 0, &g_party.view_x, &g_party.view_y);
    } else if (g_party.push_pending_group != THING_NONE) {
        creature_continue_push(command);
        g_party.push_pending_group = THING_NONE;
        if (!g_party.push_mode)
            g_party.push_arrow = PUSH_ARROW_NONE;
        return true;
    } else if (!step_party(command, here, on_stairs)) {
        g_hooks.flush_input();
        g_input_command_stop = 0;
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

void move_party_to(int16_t x, int16_t y, int16_t map, int16_t dir)
{
    bool other = map != g_game.party_map;
    if (other)
        dun_set_current_map(map);
    if (x < 0 || x >= dungeon_map_width(g_party.cur_map) || y < 0 || y >= dungeon_map_height(g_party.cur_map)) {
        if (other)
            dun_set_current_map(g_game.party_map);
        return;
    }
    int sx = g_game.party_x, sy = g_game.party_y;
    if (other) {
        dun_set_current_map(g_game.party_map);
        move_thing(THING_NONE, g_game.party_x, g_game.party_y, -1, 0);
        game_enter_map(x, y, map, 1);
        sx = -1;
        sy = 0;
    }
    move_thing(THING_NONE, sx, sy, x, y);
    champ_set_party_direction(dir);
}

void move_compute_map_arrival(int16_t x, int16_t y, int16_t map)
{
    PassageEnd end;
    int turn = 0;
    g_party.alt_view_map = -1;
    g_party.alt_view_active = dun_get_linked_teleporter_dest(&end, x, y);
    bool found = g_party.alt_view_active;
    if (found) {
        g_party.alt_view_x = end.x;
        g_party.alt_view_y = end.y;
        turn = end.target_side - end.side;
    }
    for (int d = 0; d < DIR_COUNT && !found; d++) {
        if (!dun_get_linked_teleporter_dest(&end, x + g_dir_dx[d], y + g_dir_dy[d]))
            continue;
        found = true;
        turn = end.target_side + 6 - end.side;
        int side = dir_turn(d, turn + 2);
        g_party.alt_view_x = (int16_t)(end.x + g_dir_dx[side]);
        g_party.alt_view_y = (int16_t)(end.y + g_dir_dy[side]);
    }
    if (found) {
        g_party.alt_view_map = end.map;
        g_party.alt_view_dir = (int16_t)dir_turn(g_game.party_dir, turn);
    }
    g_party.cur_map = -1;
    dun_set_current_map(map);
}

int dun_find_marker(int mode, int nth, int marker, int16_t *out_x, int16_t *out_y)
{
    int count = 0, found = -1;
    for (int map = 0; map < dungeon_map_count(); map++) {
        for (int mx = 0; mx < dungeon_map_width(map); mx++)
            for (int my = 0; my < dungeon_map_height(map); my++) {
                for (uint16_t t = dungeon_square_first_thing(map, mx, my);
                     t != THING_ENDOFLIST && thing_type(t) < THING_GROUP; t = dungeon_next_thing(t)) {
                    if (mode < MARKER_FIND_NTH || mode > MARKER_FIND_SET || thing_type(t) != THING_TEXT)
                        continue;
                    if (textrec_kind(t) != TEXT_KIND_ORNAMENT)
                        continue;
                    int kind = textrec_subtype(t), id = textrec_value(t);
                    bool hit = false;
                    if (kind == MARKER_TARGET && id == marker) {
                        if (mode == MARKER_COUNT)
                            count++;
                        else
                            hit = --nth == 0;
                    } else if (kind == MARKER_SWITCH) {
                        if (mode == MARKER_CLEAR_ALL)
                            textrec_set_visible(t, false);
                        else
                            hit = mode == MARKER_FIND_SET && textrec_is_visible(t);
                    } else if (kind == MARKER_POSITION && id == marker) {
                        *out_x = (int16_t)mx;
                        *out_y = (int16_t)my;
                        found = map;
                    }
                    if (hit) {
                        *out_x = (int16_t)mx;
                        *out_y = (int16_t)my;
                        return map;
                    }
                }
            }
    }
    dun_set_current_map(g_game.party_map);
    if (mode == MARKER_CLEAR_ALL)
        return found;
    return mode == MARKER_FIND_SET ? -1 : count;
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
static uint16_t first_thing(int x, int y)
{
    return dungeon_square_first_thing(g_party.cur_map, x, y);
}

/* Moves the party on the current map; the party square is read again. */
static void set_party_position(int x, int y)
{
    int map = g_party.cur_map;
    g_party.cur_map = -1;
    g_game.party_x = (uint16_t)x;
    g_game.party_y = (uint16_t)y;
    dun_set_current_map(map);
}

/* The teleporter at (x, y) moves the thing and turns it; false when the thing stays there. */
static bool transit_teleporter(Transit *t, uint8_t square)
{
    if (!(square & SQUARE_TELEPORTER_ON))
        return false;
    uint16_t tele = first_thing(t->x, t->y);
    int tele_scope = teleporter_scope(tele), rotation = teleporter_rotation(tele);
    bool absolute = teleporter_flag(tele, TELEPORTER_ABSOLUTE), audible = teleporter_flag(tele, TELEPORTER_AUDIBLE);
    if (tele_scope == SCOPE_CREATURES && t->type != THING_GROUP)
        return false;
    if (t->scope != SCOPE_ALL && !(tele_scope & t->scope))
        return false;
    int tx = teleporter_x(tele), ty = teleporter_y(tele), tmap = teleporter_map(tele);
    bool same = tx == t->x && ty == t->y && tmap == t->map;
    t->x = tx;
    t->y = ty;
    g_party.teleporter_flag = audible;
    t->map = tmap;
    dun_set_current_map(t->map);
    if (t->thing == THING_NONE) {
        set_party_position(t->x, t->y);
        if (audible)
            sndreq_request(
                (SoundCue){CAT_TELEPORTER, 0, SOUND_ENTRY_TELEPORT, SOUND_PRIORITY_MOVE, SOUND_VOLUME_NORMAL},
                g_game.party_x, g_game.party_y, -1);
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
                    t->src_x, t->src_y, 1);
                dun_set_current_map(saved);
            }
            sndreq_request(
                (SoundCue){CAT_TELEPORTER, 0, SOUND_ENTRY_TELEPORT, SOUND_PRIORITY_MOVE, SOUND_VOLUME_NORMAL}, t->x,
                t->y, 1);
        }
        creature_turn_group(t->thing, absolute, rotation);
    } else if (t->type == THING_PROJECTILE) {
        t->thing = rotate_projectile(tele, t->thing);
    } else if (!absolute && t->src_x != -2) {
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
                SOUND_FALLBACK, g_game.party_x, g_game.party_y, -1);
        }
        t->fell_hurt = true;
    }
}

/* A pit to a marker: the thing goes to a random pit target marker of the marker id on the pit square. */
static void transit_pit_marker(Transit *t)
{
    int marker = 0;
    for (uint16_t th = first_thing(t->x, t->y); th != THING_ENDOFLIST; th = dungeon_next_thing(th)) {
        t->type = thing_type(th); /* t->type keeps the type of the last thing read, as in the original */
        if (t->type > THING_SENSOR)
            break;
        if (t->type != THING_TEXT)
            continue;
        if (textrec_kind(th) == TEXT_KIND_ORNAMENT && textrec_subtype(th) == MARKER_PIT_TARGET) {
            marker = textrec_value(th);
            break;
        }
    }
    int16_t mx = (int16_t)t->x, my = (int16_t)t->y;
    int count = dun_find_marker(MARKER_COUNT, 0, marker, &mx, &my);
    t->map = dun_find_marker(MARKER_FIND_NTH, rand_below((uint16_t)count) + 1, marker, &mx, &my);
    t->x = mx;
    t->y = my;
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
            dun_set_party_location(t->x, t->y, t->map);
            g_hooks.enter_map_view(t->map);
            world_update_day_night(0);
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
        t->map = dungeon_location_on_map(t->map, 1, &t->x, &t->y);
    }
    dun_set_current_map(t->map);
    if (t->thing == THING_NONE) {
        set_party_position(t->x, t->y);
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
        t->map = dungeon_location_on_map(t->map, 1, &t->x, &t->y);
        dun_set_current_map(t->map);
    }
    int dir = dun_get_stairs_exit_direction(t->x, t->y);
    t->x += g_dir_dx[dir];
    t->y += g_dir_dy[dir];
    int back = dir_opposite(dir);
    int cell = dir_turn(back, (((thing_cell(t->thing) - back) + 1) & 2) / 2);
    t->thing = thing_with_cell(t->thing, cell);
}

/* The end of the way: g_party.dest_* get the end; an object that fell and champions that fell hurt make their sounds. */
static void transit_finish(const Transit *t)
{
    g_party.dest_x = (int16_t)t->x;
    g_party.dest_y = (int16_t)t->y;
    g_party.dest_map = (int16_t)t->map;
    g_party.dest_cell = (int16_t)thing_cell(t->thing);
    if (t->falls && t->creature_sound && t->thing != THING_NONE)
        sndreq_request_with_fallback((SoundCue){item_gdat_category(t->thing), item_gdat_index(t->thing),
                                                SOUND_ENTRY_DROP, SOUND_PRIORITY_DROP, SOUND_VOLUME_NORMAL},
                                     SOUND_FALLBACK, t->x, t->y, 1);
    if (!t->fell_hurt)
        return;
    int saved_map = g_party.cur_map, px = g_game.party_x, py = g_game.party_y, pmap = g_game.party_map;
    g_party.cur_map = -1;
    dun_set_party_location(t->x, t->y, t->map);
    for (int i = 0; i < g_game.champion_count; i++)
        if (champion(i)->health)
            sndreq_request_with_fallback(
                (SoundCue){CAT_CHAMPION, champion(i)->portrait, SOUND_HURT, SOUND_PRIORITY_HURT, SOUND_VOLUME_LOUD},
                SOUND_FALLBACK, t->x, t->y, 2);
    dun_set_party_location(px, py, pmap);
    dun_set_current_map(saved_map);
}

bool move_follow_square_transitions(uint16_t thing, MovePath route, bool creature_sound)
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
            g_party.push_mode = 0;
            g_ui.push_mode = 0;
            g_hooks.draw_movement_arrows();
        }
        set_party_position(route.dst_x, route.dst_y);
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
            g_party.dest_dir = (int16_t)projectile_dir(thing); /* fix: bits 10-11 of the position word */
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
    transit_finish(&t);
    dun_set_current_map(t.start_map);
    sndreq_flush_requests(1);
    return t.flying;
}

/* Finds a free square beside (x, y) for thing (THING_NONE: the party); false when there is none. */
static bool find_free_adjacent_square(uint16_t thing, int x, int y, int16_t *out_x, int16_t *out_y)
{
    int row = rand_2bits();
    const uint8_t *order = thing == THING_NONE ? free_square_order_party[row] : free_square_order_thing[row];
    int base = thing == THING_NONE ? g_game.party_dir : 0;
    for (int i = 0; i < DIR_COUNT; i++) {
        int dir = dir_turn(order[i], base);
        int nx = (int16_t)(x + g_dir_dx[dir]), ny = (int16_t)(y + g_dir_dy[dir]);
        uint8_t square = dun_get_square(nx, ny);
        if (dun_square_blocks_movement(square))
            continue;
        int type = square_type(square);
        if (type == SQUARE_STAIRS)
            continue;
        if (thing != THING_NONE && thing_type(thing) == THING_GROUP &&
            (type == SQUARE_TELEPORTER || type == SQUARE_PIT))
            continue;
        uint16_t g = creature_group_at(g_party.cur_map, nx, ny);
        if (g != THING_NONE && !(thing == THING_NONE && creature_group_info(g)->flags & CREATURE_NOT_SOLID))
            continue;
        *out_x = (int16_t)nx;
        *out_y = (int16_t)ny;
        return true;
    }
    return false;
}

/* A creature arrives on the party square: the party steps aside and takes damage; false when it cannot. */
static bool push_party_aside(uint16_t thing, int x, int y)
{
    int16_t nx, ny;
    if (!find_free_adjacent_square(THING_NONE, x, y, &nx, &ny))
        return false;
    int damage =
        rand_below(DISPLACE_DAMAGE_ROLL) + (creature_group_info(thing)->push_resistance >> DISPLACE_RESISTANCE_SHIFT);
    champ_damage_party((uint16_t)damage, WOUND_HEAD, ATTACK_TYPE_SELF);
    sndreq_request_with_fallback(
        (SoundCue){CAT_CHAMPION, SOUND_FALLBACK, SOUND_ENTRY_PUSHED, SOUND_PRIORITY_PUSHED, SOUND_VOLUME_LOUD},
        SOUND_FALLBACK, x, y, 1);
    move_thing(THING_NONE, x, y, nx, ny);
    return true;
}

/* The game ticks that a champion needs for a step, for the creature alert of a party arrival. */
static int get_step_time(int champ)
{
    Champion *c = champion(champ);
    if (!c->health)
        return 0;
    /* kept quirk: the original tests the sum of load / 10 and the flag, not the flag alone */
    return (uint16_t)champ_get_load(champ) / LOAD_TIME_DIVISOR + (c->redraw & REDRAW_SLOW_STEP) ? WALK_TIME_SLOW
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

/* Keeps the position of the projectiles that a group carries. */
static void update_carried_missiles(uint16_t holder, int16_t x, int16_t y, int16_t map, int flag)
{
    for (uint16_t t = thing_word(holder, GROUP_POSSESSIONS); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_PROJECTILE)
            continue;
        thing_set_word(t, PROJECTILE_LOCATION, (uint16_t)location_pack(x, y, map));
        projectile_set_holder_waits(t, flag & 1);
    }
}

/* Schedules a new try of a group move that a full square stopped. */
static void schedule_group_retry(uint16_t group, int16_t x, int16_t y, int16_t map, int audible)
{
    TimelineEvent e = {.time_map = timeline_time_map(g_game.game_time + GROUP_RETRY_DELAY, map)};
    e.type = audible ? EVENT_MOVE_GROUP_2 : EVENT_MOVE_GROUP;
    e.data[0] = (uint8_t)x;
    e.data[1] = (uint8_t)y;
    put_le16(e.data + EVENT_GROUP_WORD, group);
    update_carried_missiles(group, x, y, map, 1);
    timeline_add_event(&e);
}

/* The party arrives on the destination of m: a creature there moves aside or away, then the floor sensors react. */
static void arrive_party(const ThingMove *m)
{
    int map = m->map;
    int dst_x = m->path.dst_x;
    int dst_y = m->path.dst_y;
    dun_set_current_map(map);
    move_compute_map_arrival(dst_x, dst_y, map);
    uint16_t g = creature_group_at(g_party.cur_map, g_game.party_x, g_game.party_y);
    if (g != THING_NONE && !(creature_group_info(g)->flags & CREATURE_NOT_SOLID)) {
        int16_t nx, ny;
        const CreatureInfo *info = creature_group_info(g);
        bool displaced = false;
        if (info->flags & CREATURE_STATIC && info->push_resistance >= PUSH_IMMOVABLE) {
            if (find_free_adjacent_square(THING_NONE, dst_x, dst_y, &nx, &ny)) {
                dun_set_party_location(nx, ny, map);
                g_party.new_map = (int16_t)map;
                move_thing(THING_NONE, -1, 0, nx, ny);
                dun_set_party_location(g_game.party_x, g_game.party_y, m->start_map);
                if (g_party.new_map != m->start_map)
                    return;
                g_party.new_map = -1;
                displaced = true;
            }
        } else if (find_free_adjacent_square(g, dst_x, dst_y, &nx, &ny)) {
            int t = (uint16_t)get_party_step_time() >> ARRIVAL_ALERT_SHIFT;
            creature_alert(g, (Location){dst_x, dst_y}, ALERT_LOUD, 0,
                           (uint16_t)(rand_below((uint16_t)(((int16_t)t >> 1) + 1)) + t));
            move_thing(g, dst_x, dst_y, nx, ny);
            displaced = true;
        }
        if (!displaced) {
            creature_cancel_update(g);
            if (!move_thing(g, g_game.party_x, g_game.party_y, MOVE_RETRY, 0))
                schedule_group_retry(g, g_game.party_x, g_game.party_y, map, 0);
        }
    }
    if (map == m->start_map)
        sensor_enter_square(g_game.party_x, g_game.party_y, THING_NONE, m->same, false);
    else
        g_party.new_map = (int16_t)map;
}

/* A creature group meets the square (dst_x, dst_y) of map: the party or another group can block it or move aside. */
static GroupArrival meet_target_square(uint16_t thing, int map, int dst_x, int dst_y)
{
    if (map == g_party.square_map && dst_x == g_party.square_x && dst_y == g_party.square_y &&
        !(creature_group_info(thing)->flags & CREATURE_NOT_SOLID) && !push_party_aside(thing, dst_x, dst_y))
        return ARRIVAL_WAIT;
    uint16_t other = creature_group_at(g_party.cur_map, dst_x, dst_y);
    if (other == THING_NONE)
        return ARRIVAL_ENTER;
    int push = creature_group_info(thing)->push_resistance;
    const CreatureInfo *oi = creature_group_info(other), *ti = creature_group_info(thing);
    int16_t nx, ny;
    if (ti->flags & CREATURE_STATIC && push >= PUSH_IMMOVABLE) {
        if (oi->flags & CREATURE_STATIC && oi->push_resistance >= PUSH_IMMOVABLE)
            return ARRIVAL_WAIT;
        if (!find_free_adjacent_square(other, dst_x, dst_y, &nx, &ny)) {
            creature_cancel_update(thing);
            schedule_group_retry(thing, dst_x, dst_y, map, 0);
            return ARRIVAL_DONE;
        }
    } else if (oi->flags & CREATURE_STATIC && oi->push_resistance >= PUSH_IMMOVABLE) {
        if (!find_free_adjacent_square(thing, dst_x, dst_y, &nx, &ny))
            return ARRIVAL_WAIT;
        move_thing(thing, -1, 0, nx, ny);
        return ARRIVAL_DONE;
    } else if (!find_free_adjacent_square(other, dst_x, dst_y, &nx, &ny)) {
        return ARRIVAL_WAIT;
    }
    int r = rand_below((uint16_t)(((int16_t)(push >> PUSH_ALERT_SHIFT) >> 1) + 1));
    creature_alert(other, (Location){dst_x, dst_y}, ALERT_LOUD, 0, (uint16_t)((push >> PUSH_ALERT_SHIFT) + r));
    move_thing(other, dst_x, dst_y, nx, ny);
    return ARRIVAL_ENTER;
}

/* A creature group arrives on the destination of m; true when it must wait and retry. */
static bool arrive_group(const ThingMove *m)
{
    uint16_t thing = m->thing;
    int map = m->map;
    int dst_x = m->path.dst_x;
    int dst_y = m->path.dst_y;
    dun_set_current_map(map);
    int active = group_active(thing);
    GroupArrival arrival = meet_target_square(thing, map, dst_x, dst_y);
    if (arrival == ARRIVAL_WAIT) {
        if (active != GROUP_NOT_ACTIVE)
            creature_deactivate_group(active);
        schedule_group_retry(thing, dst_x, dst_y, map, g_party.teleporter_flag);
        return true;
    }
    if (arrival == ARRIVAL_DONE)
        return false;
    if (m->flying)
        dungeon_link_thing_to_square(thing, g_party.cur_map, dst_x, dst_y);
    else
        sensor_enter_square(dst_x, dst_y, thing, false, false);
    update_carried_missiles(thing, dst_x, dst_y, map, 0);
    if (active == GROUP_NOT_ACTIVE) {
        if (!(creature_group_info(thing)->flags & CREATURE_STATIC))
            creature_activate_group(thing, dst_x, dst_y);
    } else {
        int event = creature_active_group_event(active);
        if (event != -1) {
            TimelineEvent *e = &g_timeline.events[event];
            e->data[0] = (uint8_t)dst_x;
            e->data[1] = (uint8_t)dst_y;
            timeline_set_event_map(e, map);
        }
    }
    return false;
}

/* An object or projectile arrives on the destination of m: the floor sensors there react. */
static void arrive_object(const ThingMove *m)
{
    int dst_x = m->path.dst_x;
    int dst_y = m->path.dst_y;
    dun_set_current_map(m->map);
    if (m->type == THING_PROJECTILE) {
        dungeon_link_thing_to_square(m->thing, g_party.cur_map, dst_x, dst_y);
        return;
    }
    bool added = m->path.src_x < 0 && m->path.src_y < 0;
    bool under_static = added;
    if (!added && m->map != m->start_map) {
        uint16_t g = creature_group_at(g_party.cur_map, dst_x, dst_y);
        if (g != THING_NONE)
            under_static = creature_group_info(g)->flags & CREATURE_STATIC;
    }
    sensor_enter_square(dst_x, dst_y, m->thing, dun_on_party_square(dst_x, dst_y), under_static);
}

/* A thing (THING_NONE: the party) leaves the source of m: the floor sensors there react; a group that leaves the
   dungeon frees its active group. */
static void leave_square(const ThingMove *m)
{
    int src_x = m->path.src_x;
    int src_y = m->path.src_y;
    if (m->thing == THING_NONE) {
        sensor_leave_square(src_x, src_y, THING_NONE, m->same, false);
        return;
    }
    if (m->flying)
        dungeon_unlink_thing_from_square(m->thing, g_party.cur_map, src_x, src_y);
    else
        sensor_leave_square(src_x, src_y, m->thing, dun_on_party_square(src_x, src_y),
                            m->path.dst_x < 0 && m->path.dst_y < 0);
    if (thing_type(m->thing) == THING_GROUP && m->path.dst_x < -1) {
        int a = group_active(m->thing);
        if (a != GROUP_NOT_ACTIVE)
            creature_deactivate_group(a);
    }
}

/* The thing of m follows pits, teleporters, and stairs to its destination; false when a group that is not native to
   that map left the dungeon there. */
static bool reach_destination(ThingMove *m)
{
    m->flying = move_follow_square_transitions(m->thing, m->path, true);
    if (m->thing != THING_NONE) {
        if (item_attr(m->thing, ATTR_LIGHT_SOURCE) &&
            (m->start_map == g_game.party_map || g_party.dest_map == g_game.party_map))
            g_party.redraw |= REDRAW_VIEW;
        m->thing = thing_with_cell(m->thing, g_party.dest_cell);
    }
    m->path.dst_x = g_party.dest_x;
    m->path.dst_y = g_party.dest_y;
    m->dest_dir = g_party.dest_dir;
    m->map = g_party.dest_map;
    dun_set_current_map(m->map);
    if (m->type == THING_GROUP && !creature_is_native_to_map(m->thing, m->map)) {
        creature_drop_possessions(m->thing, m->path.dst_x, m->path.dst_y, DROP_ALL, 1);
        dun_set_current_map(m->start_map);
        if (m->path.src_x >= 0)
            creature_delete_group_at(m->path.src_x, m->path.src_y, DROP_NOTHING, -1);
        return false;
    }
    m->same = m->map == m->start_map && m->path.dst_x == m->path.src_x && m->path.dst_y == m->path.src_y;
    if (m->same) {
        m->stays = m->thing == THING_NONE ? g_game.party_dir == m->start_dir
                                          : g_party.dest_cell == m->cell && m->type != THING_PROJECTILE;
    } else if (m->map != m->start_map) {
        if (m->thing == THING_NONE && g_game.party_map != m->start_map)
            dun_set_party_location(g_game.party_x, g_game.party_y, m->start_map);
        else
            dun_set_current_map(m->start_map);
    }
    return true;
}

MoveResult move_thing(uint16_t thing, int src_x, int src_y, int dst_x, int dst_y)
{
    g_party.teleporter_flag = 0;
    ThingMove m = {
        .thing = thing,
        .type = -1,
        .path = {src_x, src_y, dst_x, dst_y},
        .start_map = g_party.cur_map,
        .start_dir = g_game.party_dir,
        .dest_dir = g_party.dest_dir
    };
    m.map = m.start_map;
    if (thing == THING_NONE) {
        g_party.redraw = REDRAW_ALL_VIEW;
        if (src_x >= 0)
            move_check_missiles_hit_party(src_x, src_y, dst_x, dst_y);
    } else {
        m.type = thing_type(thing);
        m.cell = thing_cell(thing);
    }
    if (dst_x >= 0 && !reach_destination(&m))
        return MOVE_LEFT_DUNGEON;
    if (!m.stays && src_x >= 0)
        leave_square(&m);
    if (!m.stays && m.path.dst_x >= 0) {
        if (thing == THING_NONE)
            arrive_party(&m);
        else if (m.type == THING_GROUP) {
            if (arrive_group(&m))
                return MOVE_WAIT;
        } else {
            arrive_object(&m);
        }
    }
    g_party.dest_dir = (int16_t)m.dest_dir;
    g_party.dest_x = m.path.dst_x;
    g_party.dest_y = m.path.dst_y;
    g_party.dest_map = (int16_t)m.map; /* the original writes a stale register when no destination is given */
    dun_set_current_map(m.start_map);
    return MOVE_DONE;
}

/* ---- Discarded things: the reuse of records when a thing table is full ---- */

/* The next map of the discard search after map: the party map comes last; alt (the map of a seamless passage, -1
   none) comes when the search is back at stop. */
static int discard_next_map(int map, int stop, int *alt)
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
        creature_delete_group_at(at.x, at.y, DROP_POSSESSIONS, -1);
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
    move_thing(t, at.x, at.y, -1, at.holder != THING_NONE ? -1 : 0);
    return true;
}

/* Searches map for a thing of type that may go and takes it; THING_NONE when there is none. The second pass on the
   party map lets a shop display keep one item of its kind and lets the other things of its square go. */
static uint16_t discard_scan_map(int type, int subtype, int map, bool second)
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
    int saved = g_party.cur_map, alt = g_party.alt_view_active ? g_party.alt_view_map : -1;
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
