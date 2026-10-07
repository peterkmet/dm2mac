/* Party movement and thing movement through pits, teleporters, and stairs. */
#include <stdlib.h>
#include <string.h>
#include "champion.h"
#include "creature.h"
#include "draw.h"
#include "dungeon.h"
#include "game.h"
#include "gdat.h"
#include "hooks.h"
#include "input.h"
#include "item.h"
#include "money.h"
#include "move.h"
#include "projectile.h"
#include "random.h"
#include "save.h"
#include "sensor.h"
#include "timeline.h"
#include "ui.h"
#include "view.h"
#include "weather.h"

#define MAP_KEY_PIT_TO_MARKER 0x6a  /* map graphics value: pits lead to a random marker, not one level down */
#define ATTR_TELEPORT_SCOPE 0x1e    /* creature attribute: teleporters that move the creature */
#define TRANSITION_MAX 50           /* the loop limit of move_follow_square_transitions */

/* Teleporter scopes: who uses the teleporter. */
#define SCOPE_ALL 3
#define SCOPE_CREATURES 1
#define SCOPE_PARTY 2

/* Text marker kinds (text record bits 11-15) that the pits read. */
#define MARKER_PIT_TARGET 0x0c          /* a pit square: the marker id of its targets */

/* Champion record values. */
#define LOAD_PER_STEP_FACTOR 3          /* stamina for a step: 3 * load / max load + 1 */
#define LOAD_PER_FALL_FACTOR 25
#define FALL_DAMAGE_MAX 0x11
#define STAMINA_REDRAW_LIMIT 10
#define LOAD_TIME_DIVISOR 10            /* the step time of a champion for a creature alert: load / 10 */
#define WALK_TIME_SLOW 50
#define WALK_TIME 40
#define LOAD_HEAVY_PART 5               /* a load above 5/8 of the maximum load slows the champion */
#define LOAD_HEAVY_EIGHTHS 8
#define MOVE_DELAY_HEAVY 3
#define MOVE_DELAY_LIGHT 2
#define OVERLOAD_DELAY_FACTOR 4         /* an overloaded champion: (load - max) * 4 / max + 4 */
#define OVERLOAD_DELAY_BASE 4
#define BUMP_WOUNDS (WOUND_TORSO | WOUND_LEGS) /* wound locations of a bump: the torso and the legs */
#define FALL_WOUNDS (WOUND_LEGS | WOUND_FEET) /* wound locations of a fall: the legs and the feet */
#define DISPLACE_DAMAGE_ROLL 10         /* the damage of a creature that lands on the party: 0 to 9, plus its push resistance / 8 */
#define DISPLACE_RESISTANCE_SHIFT 3
#define FALL_ALERT_DAMAGE 0x14          /* the damage alert of a creature that falls into a pit */
#define BASH_ROLL_MASK 0x0f             /* the random part of the strength of a champion who bashes a door */
#define ALERT_MOOD_BORED 5              /* alert flags: mood bit 5, MOOD_BORED */
#define BUMP_ALERT_CHANCE 5
#define ARRIVAL_ALERT_SHIFT 2           /* a creature that the party pushes aside: alert damage of a quarter of the step time */
#define PUSH_ALERT_SHIFT 2              /* a creature that another pushes aside: alert damage of a quarter of the push resistance */
#define DOOR_STATE_OPEN 0
#define DOOR_STATE_LOW 1                /* a door one step down: the party passes under it */
#define DOOR_CLOSED 4
#define DOOR_STATE_BROKEN 5
/* The 5 x 5 grid of the cells of a square and its neighbours (view_grid5_cell_to_xy). */
#define GRID5_SIDE 5
#define GRID5_MIDDLE 2
#define GRID5_CELL(row, col) ((row) * GRID5_SIDE + (col))
#define HIT_OFFSETS_PER_CELL 2          /* creature_find_group_near_point: the squares to check by cell */
#define HIT_OFFSETS_READ 4
/* A projectile event: bits 2-3 of data byte 3 (bits 10-11 of its position word) are its direction. */
#define MISSILE_EVENT_DIR_BYTE 3
#define MISSILE_EVENT_DIR_SHIFT 2

const int8_t dir_dx[DIR_COUNT] = {0, 1, 0, -1}, dir_dy[DIR_COUNT] = {-1, 0, 1, 0};
/* Steps of the move commands 3 to 6: forward and right parts. */
static const int8_t command_forward[4] = {1, 0, -1, 0}, command_right[4] = {0, 1, 0, -1};
/* The order in which move_find_free_adjacent_square tries the 4 sides, by a random row: for the party
   relative to its direction, for things absolute. */
static const uint8_t free_square_order_party[4][4] = {{2, 0, 1, 3}, {2, 1, 0, 3}, {2, 3, 0, 1}, {2, 1, 3, 0}};
static const uint8_t free_square_order_thing[4][4] = {{0, 2, 1, 3}, {1, 0, 2, 3}, {2, 3, 0, 1}, {3, 1, 2, 0}};

Party party = {.cur_map = -1, .new_map = -1, .push_pending_group = THING_NONE};

/* ---- The current map ---- */

void dun_set_current_map(int map)
{
    if (map == party.cur_map || map < 0) return;
    party.cur_map = (int16_t)map;
    if (!party.alt_view_active || map != party.alt_view_map) {
        party.square_x = (int16_t)game.party_x;
        party.square_y = (int16_t)game.party_y;
        party.square_map = (int16_t)game.party_map;
        party.ref_dir = (int16_t)game.party_dir;
    } else {
        party.square_x = party.alt_view_x;
        party.square_y = party.alt_view_y;
        party.square_map = party.alt_view_map;
        party.ref_dir = party.alt_view_dir;
    }
}

void dun_set_party_location(int x, int y, int map)
{
    game.party_map = (uint16_t)map;
    game.party_x = (uint16_t)x;
    game.party_y = (uint16_t)y;
    dun_set_current_map(map);
}

/* Moves the party on the current map; the party square is read again. */
static void move_set_party_position(int x, int y)
{
    int map = party.cur_map;
    party.cur_map = -1;
    game.party_x = (uint16_t)x;
    game.party_y = (uint16_t)y;
    dun_set_current_map(map);
}

void dun_move_relative(int dir, int forward, int right, int16_t *x, int16_t *y)
{
    *x = (int16_t)(*x + dir_dx[dir] * forward);
    *y = (int16_t)(*y + dir_dy[dir] * forward);
    int side = (dir + 1) & 3;
    *x = (int16_t)(*x + dir_dx[side] * right);
    *y = (int16_t)(*y + dir_dy[side] * right);
}

uint8_t dun_get_square(int x, int y)
{
    return dungeon_square_edge(party.cur_map, x, y);
}

static uint16_t first_thing(int x, int y)
{
    return dungeon_square_first_thing(party.cur_map, x, y);
}

int dun_on_party_square(int x, int y)
{
    return party.cur_map == party.square_map && x == party.square_x && y == party.square_y;
}

int dun_square_blocks_movement(uint8_t square)
{
    switch (square_type(square)) {
    case SQUARE_WALL: case SQUARE_UNKNOWN: return 1;
    case SQUARE_DOOR: {
        int state = square & SQUARE_DOOR_STATE;
        return state != DOOR_STATE_OPEN && state != DOOR_STATE_LOW && state != DOOR_STATE_BROKEN;
    }
    case SQUARE_FAKE_WALL: return !(square & SQUARE_FAKE_WALL_OPEN) && !(square & SQUARE_FLAG_0);
    default: return 0;
    }
}

int dun_get_stairs_exit_direction(int x, int y)
{
    int along_y = !(dun_get_square(x, y) & SQUARE_STAIRS_ALONG_X);
    int probe = along_y ? DIR_EAST : DIR_NORTH;
    int type = square_type(dun_get_square(x + dir_dx[probe], y + dir_dy[probe]));
    return (type == SQUARE_WALL || type == SQUARE_STAIRS) * 2 + along_y;
}

int dun_get_linked_teleporter_dest(PassageEnd *end, int x, int y)
{
    TeleporterLink link;
    if (!dungeon_teleporter_link(party.cur_map, x, y, &link)) return 0;
    *end = (PassageEnd){(uint8_t)link.side, (uint8_t)link.target_side, (uint8_t)link.x, (uint8_t)link.y,
                        (uint8_t)link.map};
    return 1;
}

/* ---- Champions ---- */

static void champ_sync_directions_to_party(void)
{
    for (int i = 0; i < game.champion_count; i++) champion(i)->dir = (uint8_t)game.party_dir;
}

void champ_set_party_direction(int dir)
{
    if (dir == game.party_dir) return;
    int turn = dir - game.party_dir;
    if (turn < 0) turn += DIR_COUNT;
    for (int i = 0; i < game.champion_count; i++) {
        Champion *c = champion(i);
        c->cell = (uint8_t)((c->cell + turn) & 3);
        c->dir = (uint8_t)((c->dir + turn) & 3);
    }
    game.party_dir = (uint16_t)dir;
    party.ref_dir = (int16_t)dir;
    if (party.alt_view_active) {
        party.alt_view_dir = (int16_t)((turn + party.alt_view_dir) & 3);
        party.ref_dir = party.alt_view_dir;
    }
}

int champ_get_move_delay(int champ)
{
    if (game.magic_light[MAGIC_HASTE]) return 1;
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
    if (c->wounds & WOUND_FEET) delay += wounded_extra;
    delay = (int16_t)(delay - (int8_t)c->move_speed);
    if (delay < 1) delay = 1;
    if (delay > 2) delay = (delay + 1) & ~1;
    return delay;
}

void champ_decrease_stamina(int champ, int amount)
{
    if (champ == -1) return;
    Champion *c = champion(champ);
    c->stamina = (int16_t)(c->stamina - amount);
    if (c->stamina <= 0) {
        int16_t missing = c->stamina;
        c->stamina = 0;
        champ_take_damage(champ, (int16_t)(-missing >> 1), 0, 0);
    } else if (c->stamina > c->max_stamina) {
        c->stamina = c->max_stamina;
    }
    if (abs((int16_t)amount) >= STAMINA_REDRAW_LIMIT) c->redraw |= REDRAW_BARS;
}

/* The game ticks that a champion needs for a step, for the creature alert of a party arrival. */
static int champ_get_move_time(int champ)
{
    Champion *c = champion(champ);
    if (!c->health) return 0;
    /* kept quirk: the original tests the sum of load / 10 and the flag, not the flag alone */
    return (uint16_t)champ_get_load(champ) / LOAD_TIME_DIVISOR + (c->redraw & REDRAW_STEP_TIME) ? WALK_TIME_SLOW : WALK_TIME;
}

static int champ_get_party_move_time(void)
{
    int sum = 0;
    for (int i = 0; i < game.champion_count; i++) sum += champ_get_move_time(i);
    return sum;
}

int dun_get_direction_to(int x1, int y1, int x2, int y2)
{
    int dx = (int16_t)(x1 - x2), dy = (int16_t)(y1 - y2);
    int ax = abs(dx), ay = abs(dy);
    if (ax == ay) {
        if (rand_bit()) ax++;
        else ay++;
    }
    if (ax < ay) return dy > 0 ? DIR_NORTH : DIR_SOUTH;
    return dx > 0 ? DIR_WEST : DIR_EAST;
}

/* The champion of the cell nearest to the side of the party square that faces (x, y), -1 none. */
int champ_get_nearest_at(int x, int y, int cell)
{
    static const uint8_t attack_cell_orders[8][4] = {
        {0, 1, 3, 2}, {1, 0, 2, 3}, {1, 2, 0, 3}, {2, 1, 3, 0}, {3, 2, 0, 1}, {2, 3, 1, 0}, {0, 3, 1, 2}, {3, 0, 2, 1}
    };
    if (!game.champion_count) return -1;
    if (abs(x - party.square_x) + abs(y - party.square_y) >= 2) return -1;
    int dir = dun_get_direction_to(party.square_x, party.square_y, x, y);
    uint8_t cells[DIR_COUNT];
    if (cell == CELL_ANY) {
        /* any cell: the cells from the side toward (x, y) on, the two front and the two back cells in random order */
        for (int i = 0; i < DIR_COUNT; i++) cells[i] = (uint8_t)((dir + i) & 3);
        if (rand_bit()) {
            uint8_t c = cells[0];
            cells[0] = cells[1];
            cells[1] = c;
        }
        if (rand_bit()) {
            uint8_t c = cells[2];
            cells[2] = cells[3];
            cells[3] = c;
        }
    } else {
        if (!(dir * 2 & 2)) cell++;
        memcpy(cells, attack_cell_orders[dir * 2 + (cell >> 1 & 1)], sizeof cells);
    }
    for (int i = 0; i < DIR_COUNT; i++) {
        int c = champ_get_index_in_cell(cells[i]);
        if (c >= 0) return c;
    }
    return -1;
}

/* Damage of a bump into a wall or a creature for the two champions on that side; returns 1 when one was hurt. */
int move_party_bump_damage(int x, int y, int16_t *first, int16_t *second, int dir, int step)
{
    int side = step + dir + 2;
    int16_t champs[2];
    champs[0] = *first = (int16_t)champ_get_nearest_at(x, y, side & 3);
    champs[1] = *second = (int16_t)champ_get_nearest_at(x, y, (side + 1) & 3);
    if (champs[0] == champs[1]) champs[1] = -1;
    int hurt = 0;
    for (int i = 0; i < 2; i++) {
        if (champs[i] < 0) continue;
        if (champ_take_damage(champs[i], 1, BUMP_WOUNDS, ATTACK_TYPE_SELF)) {
            hurt = 1;
            sound_play_with_fallback(CAT_CHAMPION, champion(champs[i])->portrait, SOUND_ENTRY_PUSHED, SOUND_FALLBACK, x, y,
                                     1, SOUND_PRIORITY_PUSHED, SOUND_VOLUME_LOUD);
        }
    }
    return hurt;
}

/* ---- Steps ---- */

enum step_result { STEP_STAIRS_HERE = 1, STEP_STAIRS_AHEAD, STEP_BLOCKED, STEP_CREATURE, STEP_CREATURE_SIDE, STEP_FREE };

uint16_t creature_find_group_near_point(int16_t *x, int16_t *y, int cell)
{
    /* The squares to check after the first one: by cell, the original reads 4 pairs from an offset of 2 pairs per
       cell (its table has rows of 4 pairs), which this table keeps. */
    static const int16_t hit_offsets[HIT_OFFSETS_PER_CELL * (DIR_COUNT - 1) + HIT_OFFSETS_READ][2] = {
        {-1, 0}, {0, -1}, {0, -1}, {1, 0}, {1, 0}, {0, 1}, {-1, 0}, {0, 1}, {0, 1}, {0, -1}
    };
    static const int16_t hit_offsets_any_cell[HIT_OFFSETS_READ][2] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
    static const int16_t hit_radius_sq[] = {0, 3, 6, 6};
    /* the centers of the four cells of the square in the 5 x 5 grid */
    static const uint16_t cell_grid[DIR_COUNT] = {GRID5_CELL(1, 1), GRID5_CELL(1, 3), GRID5_CELL(3, 3), GRID5_CELL(3, 1)};
    int16_t sx = *x, sy = *y, px, py, cx, cy;
    view_grid5_cell_to_xy(sx, sy, cell == CELL_ANY ? GRID5_CELL(GRID5_MIDDLE, GRID5_MIDDLE) : cell_grid[cell], &px, &py);
    for (int i = 0; i <= HIT_OFFSETS_READ; i++) {
        uint16_t g = creature_group_at(party.cur_map, sx, sy);
        if (g != THING_NONE) {
            int size = (int8_t)creature_group_info(g)->hit_size;
            int zone = creature_get_view_cell_zone(g, -group_dir(g) & 3);
            view_grid5_cell_to_xy(sx, sy, (uint16_t)zone, &cx, &cy);
            cx = (int16_t)(cx - px);
            cy = (int16_t)(cy - py);
            if ((int16_t)(cy * cy + cx * cx) < hit_radius_sq[size]) {
                *x = sx;
                *y = sy;
                return g;
            }
        }
        if (i == HIT_OFFSETS_READ) break;
        if (cell == CELL_ANY) {
            sx = (int16_t)(*x + hit_offsets_any_cell[i][0]);
            sy = (int16_t)(*y + hit_offsets_any_cell[i][1]);
        } else {
            sx = (int16_t)(*x + hit_offsets[cell * HIT_OFFSETS_PER_CELL + i][0]);
            sy = (int16_t)(*y + hit_offsets[cell * HIT_OFFSETS_PER_CELL + i][1]);
        }
    }
    return THING_NONE;
}

/* True when the creature in front stands on a side cell that blocks the step. */
static int move_group_side_blocks(uint16_t group, int dir)
{
    int side = (group_dir(group) + dir) & 3;
    if (side != DIR_EAST && side != DIR_WEST) return 0;
    return creature_get_view_cell_zone(group, side) % GRID5_SIDE != GRID5_MIDDLE;
}

/* What a step from a square (here) to the square (x, y) with the byte ahead meets. */
static int move_get_step_result(int step, uint8_t here, uint8_t ahead, int x, int y, uint16_t *group)
{
    *group = THING_NONE;
    if (square_type(here) == SQUARE_STAIRS && step == 2) return STEP_STAIRS_HERE;
    if (square_type(ahead) == SQUARE_STAIRS) return STEP_STAIRS_AHEAD;
    if (dun_square_blocks_movement(ahead)) return STEP_BLOCKED;
    int16_t gx = (int16_t)x, gy = (int16_t)y;
    *group = creature_group_at(party.cur_map, x, y);
    if (*group == THING_NONE || creature_group_info(*group)->flags & CREATURE_NOT_SOLID) {
        uint16_t g = creature_find_group_near_point(&gx, &gy, CELL_ANY);
        if (g == THING_NONE || creature_group_info(g)->flags & CREATURE_NOT_SOLID) return STEP_FREE;
    } else if (!move_group_side_blocks(*group, game.party_dir)) {
        return STEP_CREATURE;
    }
    return STEP_CREATURE_SIDE;
}

/* The party takes the stairs up or down: it leaves the square and arrives on the map above or below. */
static void move_party_take_stairs(int up)
{
    move_thing(THING_NONE, game.party_x, game.party_y, -1, 0);
    int x = game.party_x, y = game.party_y;
    party.new_map = (int16_t)dungeon_location_on_map(game.party_map, up ? -1 : 1, &x, &y);
    game.party_x = (uint16_t)x;
    game.party_y = (uint16_t)y;
    dun_set_current_map(party.new_map);
    champ_set_party_direction(dun_get_stairs_exit_direction(game.party_x, game.party_y));
    dun_set_current_map(game.party_map);
}

void move_party_turn(int command)
{
    if (!command) return;
    input_command_stop = 1;
    champ_sync_directions_to_party();
    uint8_t square = dun_get_square(game.party_x, game.party_y);
    if (square_type(square) == SQUARE_STAIRS) {
        move_party_take_stairs(square & SQUARE_STAIRS_UP);
        return;
    }
    PassageEnd end;
    if (dun_get_linked_teleporter_dest(&end, game.party_x, game.party_y)) {
        move_party_to(end.x, end.y, end.map, end.target_side);
        return;
    }
    sensor_process_floor(game.party_x, game.party_y, THING_NONE, 1, 0, 0);
    champ_set_party_direction((game.party_dir + (command == CMD_TURN_RIGHT ? 1 : 3)) & 3);
    sensor_process_floor(game.party_x, game.party_y, THING_NONE, 1, 1, 0);
}

/* The party steps onto (x, y) from the square of map: seamless passages, stairs, teleporter passages, then the move. */
static void move_party_free_step(int map, int step, int on_stairs, int16_t x, int16_t y, int dir)
{
    if (party.alt_view_active && step == 2) {
        dun_set_current_map(map);
        x = party.square_x;
        y = party.square_y;
        dir = party.ref_dir;
        dun_move_relative(dir, -1, 0, &x, &y);
    }
    if (on_stairs) {
        move_thing(THING_NONE, -1, 0, x, y);
        return;
    }
    PassageEnd end;
    if (dun_get_linked_teleporter_dest(&end, x, y) && ((end.target_side + 2) & 3) != dir) {
        move_party_to(end.x, end.y, end.map, end.target_side);
        return;
    }
    move_thing(THING_NONE, game.party_x, game.party_y, x, y);
}

/* The strength of a champion who bumps into a door. */
static int bash_strength(int champ)
{
    Champion *c = champion(champ);
    int r = rand_next() & BASH_ROLL_MASK;
    return champ_scale_by_stamina(c, (int16_t)(champ_get_stat(c, STAT_STRENGTH, 0) + r));
}

/* The two champions who bumped into the closed door on (x, y) try to break it with their strength. */
static void move_party_bash_door(int x, int y, int16_t first, int16_t second)
{
    int strength = 0;
    if (first != -1) strength = bash_strength(first);
    if (second != first && second != -1) strength += bash_strength(second);
    sensor_door_try_destroy(x, y, (int16_t)strength, 0, 0);
}

/* One step of the party for a move command: stairs, a bump into a wall, door, or creature, a push, or a free step.
   Returns 0 when the party cannot move. */
static int move_party_step(int command, uint8_t here, int on_stairs)
{
    int map = party.cur_map;
    int step = command - CMD_MOVE_FORWARD;
    if (party.alt_view_active && step == 2) dun_set_current_map(party.alt_view_map);
    int16_t x = party.square_x, y = party.square_y;
    int dir = party.ref_dir;
    dun_move_relative(dir, command_forward[step], command_right[step], &x, &y);
    uint8_t ahead = dun_get_square(x, y);
    input_command_stop = 1;
    for (int i = 0; i < game.champion_count; i++) {
        Champion *c = champion(i);
        if (!c->health) continue;
        int load = (uint16_t)champ_get_load(i) * LOAD_PER_STEP_FACTOR;
        champ_decrease_stamina(i, (int16_t)(load / (uint16_t)champ_get_max_load(c) + 1));
    }
    champ_sync_directions_to_party();
    uint16_t group;
    int16_t first, second;
    switch (move_get_step_result(step, here, ahead, x, y, &group)) {
    case STEP_STAIRS_AHEAD:
        move_thing(THING_NONE, game.party_x, game.party_y, -1, 0);
        game.party_x = (uint16_t)x;
        game.party_y = (uint16_t)y;
        here = ahead;
        /* fall through */
    case STEP_STAIRS_HERE:
        move_party_take_stairs(here & SQUARE_STAIRS_UP);
        return 1;
    case STEP_BLOCKED:
        if (!game.champion_count) return 0;
        move_party_bump_damage(x, y, &first, &second, dir, step);
        if (square_type(ahead) == SQUARE_DOOR && (ahead & SQUARE_DOOR_STATE) == DOOR_CLOSED) move_party_bash_door(x, y, first, second);
        return 0;
    case STEP_CREATURE: {
        if (!game.champion_count) return 0;
        int is_static = creature_group_info(group)->flags & CREATURE_STATIC;
        if (is_static || creature_group_info(group)->push_resistance == PUSH_WALL)
            move_party_bump_damage(x, y, &first, &second, dir, step);
        int push_dir = (dir + command - CMD_MOVE_FORWARD) & 3;
        if (!creature_can_be_pushed(x, y, push_dir, &group) || !move_party_can_push_group(group)) {
            if (!is_static) creature_alert(THING_NONE, x, y, ALERT_ANGER | ALERT_MOOD_BORED, BUMP_ALERT_CHANCE, 0);
            return 0;
        }
        creature_push_group(x, y, push_dir, PUSH_STRENGTH_PARTY);
        break;
    }
    case STEP_CREATURE_SIDE:
        return 0;
    case STEP_FREE:
        break;
    }
    move_party_free_step(map, step, on_stairs, x, y, dir);
    return 1;
}

int move_party(int command)
{
    uint8_t here = dun_get_square(party.square_x, party.square_y);
    int on_stairs = square_type(here) == SQUARE_STAIRS;
    int delay = 1;
    for (int i = 0; i < game.champion_count; i++)
        if (champion(i)->health) {
            int d = champ_get_move_delay(i);
            if (d > delay) delay = d;
        }
    int half = (int16_t)delay >> 1;
    if (delay > 1 && !party.move_steps
        && (command == CMD_MOVE_FORWARD || (command == CMD_MOVE_BACK && !party.alt_view_active && !on_stairs)
            || party.push_pending_group != THING_NONE)) {
        party.move_steps = (int16_t)half;
        party.view_x = (int16_t)game.party_x;
        party.view_y = (int16_t)game.party_y;
        party.view_dir = (int16_t)game.party_dir;
        party.move_command = (uint16_t)command;
        if (command != CMD_MOVE_FORWARD) dun_move_relative(game.party_dir, -1, 0, &party.view_x, &party.view_y);
    } else if (party.push_pending_group != THING_NONE) {
        creature_continue_push(command);
        party.push_pending_group = THING_NONE;
        if (!party.push_mode) party.push_arrow = 6;
        return 1;
    } else if (!move_party_step(command, here, on_stairs)) {
        input_flush();
        input_command_stop = 0;
        return 0;
    }
    int add = party.move_steps != 0;
    if (half > add) add = half;
    game.party_move_cooldown = (uint16_t)(game.party_move_cooldown + add);
    game.throw_cooldown = 0;
    return 1;
}

void move_party_to(int x, int y, int map, int dir)
{
    int other = map != game.party_map;
    if (other) dun_set_current_map(map);
    if (x < 0 || x >= dungeon_map_width(party.cur_map) || y < 0 || y >= dungeon_map_height(party.cur_map)) {
        if (other) dun_set_current_map(game.party_map);
        return;
    }
    int sx = game.party_x, sy = game.party_y;
    if (other) {
        dun_set_current_map(game.party_map);
        move_thing(THING_NONE, game.party_x, game.party_y, -1, 0);
        game_enter_map(x, y, map, 1);
        sx = -1;
        sy = 0;
    }
    move_thing(THING_NONE, sx, sy, x, y);
    champ_set_party_direction(dir);
}

void move_compute_map_arrival(int x, int y, int map)
{
    PassageEnd end;
    int turn = 0;
    party.alt_view_map = -1;
    party.alt_view_active = dun_get_linked_teleporter_dest(&end, x, y);
    int found = party.alt_view_active;
    if (found) {
        party.alt_view_x = end.x;
        party.alt_view_y = end.y;
        turn = end.target_side - end.side;
    }
    for (int d = 0; d < DIR_COUNT && !found; d++) {
        if (!dun_get_linked_teleporter_dest(&end, x + dir_dx[d], y + dir_dy[d])) continue;
        found = 1;
        turn = end.target_side + 6 - end.side;
        int side = (d + turn + 2) & 3;
        party.alt_view_x = (int16_t)(end.x + dir_dx[side]);
        party.alt_view_y = (int16_t)(end.y + dir_dy[side]);
    }
    if (found) {
        party.alt_view_map = end.map;
        party.alt_view_dir = (int16_t)((turn + game.party_dir) & 3);
    }
    party.cur_map = -1;
    dun_set_current_map(map);
}

int move_find_free_adjacent_square(uint16_t thing, int x, int y, int16_t *out_x, int16_t *out_y)
{
    int row = rand_2bits();
    const uint8_t *order = thing == THING_NONE ? free_square_order_party[row] : free_square_order_thing[row];
    int base = thing == THING_NONE ? game.party_dir : 0;
    for (int i = 0; i < 4; i++) {
        int dir = (order[i] + base) & 3;
        int nx = (int16_t)(x + dir_dx[dir]), ny = (int16_t)(y + dir_dy[dir]);
        uint8_t square = dun_get_square(nx, ny);
        if (dun_square_blocks_movement(square)) continue;
        int type = square_type(square);
        if (type == SQUARE_STAIRS) continue;
        if (thing != THING_NONE && thing_type(thing) == THING_GROUP && (type == SQUARE_TELEPORTER || type == SQUARE_PIT))
            continue;
        uint16_t g = creature_group_at(party.cur_map, nx, ny);
        if (g != THING_NONE && !(thing == THING_NONE && creature_group_info(g)->flags & CREATURE_NOT_SOLID)) continue;
        *out_x = (int16_t)nx;
        *out_y = (int16_t)ny;
        return 1;
    }
    return 0;
}

/* Marker modes of dun_find_marker. */
enum { MARKER_FIND_NTH = 2, MARKER_COUNT = 3, MARKER_CLEAR_ALL = 4, MARKER_FIND_SET = 5 };
#define MARKER_TARGET 0x0b      /* a pit target marker of a marker id */
#define MARKER_SWITCH 0x0f      /* a marker with an on bit */
#define MARKER_POSITION 0x10    /* the last marker of an id wins */

int dun_find_marker(int mode, int nth, int marker, int16_t *x, int16_t *y)
{
    int count = 0, found = -1;
    for (int map = 0; map < dungeon_map_count(); map++) {
        for (int mx = 0; mx < dungeon_map_width(map); mx++)
            for (int my = 0; my < dungeon_map_height(map); my++) {
                for (uint16_t t = dungeon_square_first_thing(map, mx, my); t != THING_ENDOFLIST && thing_type(t) < THING_GROUP;
                     t = dungeon_next_thing(t)) {
                    if (mode < MARKER_FIND_NTH || mode > MARKER_FIND_SET || thing_type(t) != THING_TEXT) continue;
                    if (text_kind(t) != TEXT_KIND_ORNAMENT) continue;
                    int kind = text_subtype(t), id = text_value(t), hit = 0;
                    if (kind == MARKER_TARGET && id == marker) {
                        if (mode == MARKER_COUNT) count++;
                        else hit = --nth == 0;
                    } else if (kind == MARKER_SWITCH) {
                        if (mode == MARKER_CLEAR_ALL) text_set_visible(t, false);
                        else hit = mode == MARKER_FIND_SET && text_is_visible(t);
                    } else if (kind == MARKER_POSITION && id == marker) {
                        *x = (int16_t)mx;
                        *y = (int16_t)my;
                        found = map;
                    }
                    if (hit) {
                        *x = (int16_t)mx;
                        *y = (int16_t)my;
                        return map;
                    }
                }
            }
    }
    dun_set_current_map(game.party_map);
    if (mode == MARKER_CLEAR_ALL) return found;
    return mode == MARKER_FIND_SET ? -1 : count;
}

/* ---- Moving things ---- */

/* True for things that fly over pits and do not press floor sensors. */
static int move_thing_is_flying(uint16_t thing)
{
    int type = thing_type(thing);
    if (type == THING_GROUP) return creature_is_flying(thing);
    return type == THING_PROJECTILE || type == THING_EXPLOSION;
}

/* A projectile through a teleporter: its cell and flight direction turn with the teleporter. */
static uint16_t move_rotate_through_teleporter(uint16_t teleporter, uint16_t thing)
{
    int rotation = teleporter_rotation(teleporter);
    if (!teleporter_flag(teleporter, TELEPORTER_ABSOLUTE)) {
        thing = move_set_cell(thing, (rotation + move_thing_cell(thing)) & 3);
        rotation = (party.dest_dir + rotation) & 3;
    }
    party.dest_dir = (int16_t)rotation;
    return thing;
}

/* A thing (THING_NONE: the party) on its way through pits, teleporters, and stairs. */
typedef struct {
    uint16_t thing;
    int type;                   /* the thing type, -1 for the party */
    int src_x, src_y, x, y, map, start_map;
    int scope;                  /* the teleporters that move it: SCOPE_* */
    int flying, draw_fall, falls, fell_hurt, creature_sound;
} Transit;

/* The teleporter at (x, y) moves the thing and turns it; returns 0 when the thing stays there. */
static int transit_teleporter(Transit *t, uint8_t square)
{
    if (!(square & SQUARE_TELEPORTER_ON)) return 0;
    uint16_t tele = first_thing(t->x, t->y);
    int tele_scope = teleporter_scope(tele), rotation = teleporter_rotation(tele);
    bool absolute = teleporter_flag(tele, TELEPORTER_ABSOLUTE), audible = teleporter_flag(tele, TELEPORTER_AUDIBLE);
    if (tele_scope == SCOPE_CREATURES && t->type != THING_GROUP) return 0;
    if (t->scope != SCOPE_ALL && !(tele_scope & t->scope)) return 0;
    int tx = teleporter_x(tele), ty = teleporter_y(tele), tmap = teleporter_map(tele);
    int same = tx == t->x && ty == t->y && tmap == t->map;
    t->x = tx;
    t->y = ty;
    party.teleporter_flag = audible;
    t->map = tmap;
    dun_set_current_map(t->map);
    if (t->thing == THING_NONE) {
        move_set_party_position(t->x, t->y);
        if (audible)
            sound_request(CAT_TELEPORTER, 0, SOUND_ENTRY_TELEPORT, SOUND_PRIORITY_MOVE, SOUND_VOLUME_NORMAL, game.party_x,
                          game.party_y, -1);
        if (t->start_map != t->map) t->draw_fall = 0;  /* the original keeps it only with its debug flag */
        int base = absolute ? 0 : game.party_dir;
        champ_set_party_direction((base + rotation) & 3);
    } else if (t->type == THING_GROUP && t->creature_sound) {
        if (audible) {
            if (party.cur_map != t->start_map) {
                int saved = party.cur_map;
                dun_set_current_map(t->start_map);
                sound_request(CAT_TELEPORTER, 0, SOUND_ENTRY_TELEPORT, SOUND_PRIORITY_MOVE, SOUND_VOLUME_NORMAL, t->src_x,
                              t->src_y, 1);
                dun_set_current_map(saved);
            }
            sound_request(CAT_TELEPORTER, 0, SOUND_ENTRY_TELEPORT, SOUND_PRIORITY_MOVE, SOUND_VOLUME_NORMAL, t->x, t->y, 1);
        }
        creature_turn_group(t->thing, absolute, rotation);
    } else if (t->type == THING_PROJECTILE) {
        t->thing = move_rotate_through_teleporter(tele, t->thing);
    } else if (!absolute && t->src_x != -2) {
        t->thing = move_set_cell(t->thing, (rotation + move_thing_cell(t->thing)) & 3);
    }
    return !same;
}

/* The party lands after a fall: a levitation costs stamina by load, a real fall hurts the legs and the feet. */
static void transit_party_lands(Transit *t)
{
    if (party.fell) {
        t->draw_fall = 0;  /* the original keeps it only with its debug flag */
        for (int i = 0; i < game.champion_count; i++) {
            Champion *c = champion(i);
            if (!c->health) continue;
            int load = (uint16_t)champ_get_load(i) * LOAD_PER_FALL_FACTOR;
            champ_decrease_stamina(i, (int16_t)(load / (uint16_t)champ_get_max_load(c) + 1));
        }
        return;
    }
    for (int i = 0; i < game.champion_count; i++) {
        Champion *c = champion(i);
        if (c->health) {
            int base = c->max_health >> 2;
            if (base > FALL_DAMAGE_MAX) base = FALL_DAMAGE_MAX;
            champ_take_damage(i, (int16_t)((rand_2bits() + base) * t->falls), FALL_WOUNDS, ATTACK_TYPE_SELF);
            sound_play_with_fallback(CAT_CHAMPION, c->portrait, SOUND_ENTRY_FALL, SOUND_FALLBACK, game.party_x,
                                     game.party_y, -1, SOUND_PRIORITY_FALL, SOUND_VOLUME_MAX);
        }
        t->fell_hurt = 1;
    }
}

/* A pit to a marker: the thing goes to a random pit target marker of the marker id on the pit square. */
static void transit_pit_marker(Transit *t)
{
    int marker = 0;
    for (uint16_t th = first_thing(t->x, t->y); th != THING_ENDOFLIST; th = dungeon_next_thing(th)) {
        t->type = thing_type(th);  /* the original reuses its thing type variable here */
        if (t->type > THING_SENSOR) break;
        if (t->type != THING_TEXT) continue;
        if (text_kind(th) == TEXT_KIND_ORNAMENT && text_subtype(th) == MARKER_PIT_TARGET) {
            marker = text_value(th);
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
    int to_marker = gdat_entry_index(draw_gdat(), CAT_WALLSET, dungeon_map_wallset(party.cur_map), GDAT_TYPE_VALUE,
                                     MAP_KEY_PIT_TO_MARKER);
    if (to_marker && t->type == THING_GROUP) {
        uint16_t home = thing_word(t->thing, GROUP_HOME);
        t->x = location_x(home);
        t->y = location_y(home);
        t->map = location_map(home);
        dun_set_current_map(t->map);
        return;
    }
    if (t->draw_fall && !to_marker && !party.fell) {
        t->draw_fall = 0;
        if (t->falls) {
            dun_set_party_location(t->x, t->y, t->map);
            view_enter_map(t->map);
            world_update_day_night(0);
        }
        int steps = party.move_steps;
        party.move_steps = 0;
        view_draw_dungeon(game.party_dir, t->x, t->y);
        party.move_steps = (int16_t)steps;
    }
    if (to_marker) {
        transit_pit_marker(t);
    } else {
        t->falls++;
        t->map = dungeon_location_on_map(t->map, 1, &t->x, &t->y);
    }
    dun_set_current_map(t->map);
    if (t->thing == THING_NONE) {
        move_set_party_position(t->x, t->y);
        if (game.champion_count) transit_party_lands(t);
        party.fell = 0;
        return;
    }
    if (t->type == THING_GROUP && t->creature_sound && t->src_x >= 0) {
        dun_set_current_map(t->start_map);
        creature_alert(t->thing, t->src_x, t->src_y, 0, 0, FALL_ALERT_DAMAGE);
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
    t->x += dir_dx[dir];
    t->y += dir_dy[dir];
    int back = (dir + 2) & 3;
    int cell = (back + (((move_thing_cell(t->thing) - back) + 1) & 2) / 2) & 3;
    t->thing = move_set_cell(t->thing, cell);
}

/* The end of the way: party.dest_* get the end; an object that fell and champions that fell hurt make their sounds. */
static void transit_finish(const Transit *t)
{
    party.dest_x = (int16_t)t->x;
    party.dest_y = (int16_t)t->y;
    party.dest_map = (int16_t)t->map;
    party.dest_cell = (int16_t)move_thing_cell(t->thing);
    if (t->falls && t->creature_sound && t->thing != THING_NONE)
        sound_play_with_fallback(item_gdat_category(t->thing), item_gdat_index(t->thing), SOUND_ENTRY_DROP, SOUND_FALLBACK, t->x,
                                 t->y, 1, SOUND_PRIORITY_DROP, SOUND_VOLUME_NORMAL);
    if (!t->fell_hurt) return;
    int saved_map = party.cur_map, px = game.party_x, py = game.party_y, pmap = game.party_map;
    party.cur_map = -1;
    dun_set_party_location(t->x, t->y, t->map);
    for (int i = 0; i < game.champion_count; i++)
        if (champion(i)->health)
            sound_play_with_fallback(CAT_CHAMPION, champion(i)->portrait, SOUND_HURT, SOUND_FALLBACK, t->x, t->y,
                                     2, SOUND_PRIORITY_HURT, SOUND_VOLUME_LOUD);
    dun_set_party_location(px, py, pmap);
    dun_set_current_map(saved_map);
}

/* Follows pits, teleporters, and stairs from (x, y) of the current map; leaves the end in party.dest_*.
   Returns 1 for a flying thing. */
int move_follow_square_transitions(uint16_t thing, int src_x, int src_y, int x, int y, int creature_sound)
{
    party.teleporter_flag = 0;
    Transit t = {.thing = thing, .src_x = src_x, .src_y = src_y, .x = x, .y = y, .map = party.cur_map,
                 .start_map = party.cur_map, .creature_sound = creature_sound};
    if (thing == THING_NONE) {
        if (party.push_mode) {
            party.push_mode = 0;
            ui.push_mode = 0;
            ui_draw_movement_arrows();
        }
        move_set_party_position(x, y);
        t.scope = SCOPE_PARTY;
        t.draw_fall = !ui.inventory && !party_sleeping;
        t.type = -1;
    } else {
        t.flying = move_thing_is_flying(thing);
        t.type = thing_type(thing);
        t.scope = SCOPE_ALL;
        if (t.type == THING_GROUP) t.scope = item_attr(thing, ATTR_TELEPORT_SCOPE) ? SCOPE_PARTY : SCOPE_CREATURES;
        else if (t.type == THING_PROJECTILE)
            party.dest_dir = (int16_t)(timeline.events[thing_word(thing, PROJECTILE_EVENT)].data[MISSILE_EVENT_DIR_BYTE]
                                       >> MISSILE_EVENT_DIR_SHIFT & 3  /* fix: bits 10-11 of the position word */);
    }
    for (int loop = TRANSITION_MAX; --loop;) {
        uint8_t square = dungeon_square(party.cur_map, t.x, t.y);
        int square_kind = square_type(square);
        if (square_kind == SQUARE_TELEPORTER) {
            if (!transit_teleporter(&t, square)) break;
        } else if (square_kind == SQUARE_PIT && !t.flying && square & SQUARE_PIT_OPEN && !(square & SQUARE_FLAG_0)) {
            transit_pit(&t);
        } else if (square_kind == SQUARE_STAIRS && t.thing != THING_NONE && t.type != THING_PROJECTILE && t.type != THING_GROUP) {
            transit_stairs(&t, square);
        } else {
            break;
        }
    }
    transit_finish(&t);
    dun_set_current_map(t.start_map);
    sound_flush_requests(1);
    return t.flying;
}

/* A creature arrives on the party square: the party steps aside and takes damage. */
static int creature_displaces_party(uint16_t thing, int x, int y)
{
    int16_t nx, ny;
    if (!move_find_free_adjacent_square(THING_NONE, x, y, &nx, &ny)) return 0;
    int damage = rand_below(DISPLACE_DAMAGE_ROLL) + (creature_group_info(thing)->push_resistance >> DISPLACE_RESISTANCE_SHIFT);
    champ_damage_party((uint16_t)damage, WOUND_HEAD, ATTACK_TYPE_SELF);
    sound_play_with_fallback(CAT_CHAMPION, SOUND_FALLBACK, SOUND_ENTRY_PUSHED, SOUND_FALLBACK, x, y, 1, SOUND_PRIORITY_PUSHED,
                             SOUND_VOLUME_LOUD);
    move_thing(THING_NONE, x, y, nx, ny);
    return 1;
}

/* The party arrives on (dst_x, dst_y) of map: a creature there moves aside or away, then the floor sensors react. */
static void move_party_arrive(int map, int start_map, int dst_x, int dst_y, int same)
{
    dun_set_current_map(map);
    move_compute_map_arrival(dst_x, dst_y, map);
    uint16_t g = creature_group_at(party.cur_map, game.party_x, game.party_y);
    if (g != THING_NONE && !(creature_group_info(g)->flags & CREATURE_NOT_SOLID)) {
        int16_t nx, ny;
        const CreatureInfo *info = creature_group_info(g);
        int displaced = 0;
        if (info->flags & CREATURE_STATIC && info->push_resistance >= PUSH_IMMOVABLE) {
            if (move_find_free_adjacent_square(THING_NONE, dst_x, dst_y, &nx, &ny)) {
                dun_set_party_location(nx, ny, map);
                party.new_map = (int16_t)map;
                move_thing(THING_NONE, -1, 0, nx, ny);
                dun_set_party_location(game.party_x, game.party_y, start_map);
                if (party.new_map != start_map) return;
                party.new_map = -1;
                displaced = 1;
            }
        } else if (move_find_free_adjacent_square(g, dst_x, dst_y, &nx, &ny)) {
            int t = (uint16_t)champ_get_party_move_time() >> ARRIVAL_ALERT_SHIFT;
            creature_alert(g, dst_x, dst_y, ALERT_LOUD, 0, (uint16_t)(rand_below((uint16_t)(((int16_t)t >> 1) + 1)) + t));
            move_thing(g, dst_x, dst_y, nx, ny);
            displaced = 1;
        }
        if (!displaced) {
            creature_cancel_update(g);
            if (!move_thing(g, game.party_x, game.party_y, MOVE_RETRY, 0))
                move_schedule_group_retry(g, game.party_x, game.party_y, map, 0);
        }
    }
    if (map == start_map) sensor_process_floor(game.party_x, game.party_y, THING_NONE, same, 1, 0);
    else party.new_map = (int16_t)map;
}

/* How a group meets its target square: it enters (the square is free, or another group moved aside), it is done
   (it went elsewhere, or a retry is queued), or it waits. */
enum group_arrival { ARRIVAL_ENTER, ARRIVAL_DONE, ARRIVAL_WAIT };

/* A creature group meets the square (dst_x, dst_y) of map: the party or another group can block it or move aside. */
static int move_group_meet_square(uint16_t thing, int map, int dst_x, int dst_y)
{
    if (map == party.square_map && dst_x == party.square_x && dst_y == party.square_y
        && !(creature_group_info(thing)->flags & CREATURE_NOT_SOLID) && !creature_displaces_party(thing, dst_x, dst_y))
        return ARRIVAL_WAIT;
    uint16_t other = creature_group_at(party.cur_map, dst_x, dst_y);
    if (other == THING_NONE) return ARRIVAL_ENTER;
    int push = creature_group_info(thing)->push_resistance;
    const CreatureInfo *oi = creature_group_info(other), *ti = creature_group_info(thing);
    int16_t nx, ny;
    if (ti->flags & CREATURE_STATIC && push >= PUSH_IMMOVABLE) {
        if (oi->flags & CREATURE_STATIC && oi->push_resistance >= PUSH_IMMOVABLE) return ARRIVAL_WAIT;
        if (!move_find_free_adjacent_square(other, dst_x, dst_y, &nx, &ny)) {
            creature_cancel_update(thing);
            move_schedule_group_retry(thing, dst_x, dst_y, map, 0);
            return ARRIVAL_DONE;
        }
    } else if (oi->flags & CREATURE_STATIC && oi->push_resistance >= PUSH_IMMOVABLE) {
        if (!move_find_free_adjacent_square(thing, dst_x, dst_y, &nx, &ny)) return ARRIVAL_WAIT;
        move_thing(thing, -1, 0, nx, ny);
        return ARRIVAL_DONE;
    } else if (!move_find_free_adjacent_square(other, dst_x, dst_y, &nx, &ny)) {
        return ARRIVAL_WAIT;
    }
    int r = rand_below((uint16_t)(((int16_t)(push >> PUSH_ALERT_SHIFT) >> 1) + 1));
    creature_alert(other, dst_x, dst_y, ALERT_LOUD, 0, (uint16_t)((push >> PUSH_ALERT_SHIFT) + r));
    move_thing(other, dst_x, dst_y, nx, ny);
    return ARRIVAL_ENTER;
}

/* A creature group arrives on (dst_x, dst_y) of map. Returns 2 when it must wait and retry, else 0. */
static int move_group_arrive(uint16_t thing, int map, int dst_x, int dst_y, int flying)
{
    dun_set_current_map(map);
    int active = group_active(thing);
    int arrival = move_group_meet_square(thing, map, dst_x, dst_y);
    if (arrival == ARRIVAL_WAIT) {
        if (active != GROUP_NOT_ACTIVE) creature_deactivate_group(active);
        move_schedule_group_retry(thing, dst_x, dst_y, map, party.teleporter_flag);
        return 2;
    }
    if (arrival == ARRIVAL_DONE) return 0;
    if (flying) dungeon_link_thing_to_square(thing, party.cur_map, dst_x, dst_y);
    else sensor_process_floor(dst_x, dst_y, thing, 0, 1, 0);
    move_update_carried_missiles(thing, dst_x, dst_y, map, 0);
    if (active == GROUP_NOT_ACTIVE) {
        if (!(creature_group_info(thing)->flags & CREATURE_STATIC)) creature_activate_group(thing, dst_x, dst_y);
    } else {
        int event = creature_active_group_event(active);
        if (event != -1) {
            TimelineEvent *e = &timeline.events[event];
            e->data[0] = (uint8_t)dst_x;
            e->data[1] = (uint8_t)dst_y;
            timeline_set_event_map(e, map);
        }
    }
    return 0;
}

/* An object or projectile arrives on (dst_x, dst_y) of map: the floor sensors there react. */
static void move_object_arrive(uint16_t thing, int type, int map, int start_map, int src_x, int src_y, int dst_x, int dst_y)
{
    dun_set_current_map(map);
    if (type == THING_PROJECTILE) {
        dungeon_link_thing_to_square(thing, party.cur_map, dst_x, dst_y);
        return;
    }
    int added = src_x < 0 && src_y < 0, under_static = added;
    if (!added && map != start_map) {
        uint16_t g = creature_group_at(party.cur_map, dst_x, dst_y);
        if (g != THING_NONE) under_static = creature_group_info(g)->flags & CREATURE_STATIC;
    }
    sensor_process_floor(dst_x, dst_y, thing, dun_on_party_square(dst_x, dst_y), 1, under_static);
}

/* A thing (THING_NONE: the party) leaves (src_x, src_y): the floor sensors there react; a group that leaves the
   dungeon frees its active group. */
static void move_thing_leave(uint16_t thing, int flying, int same, int src_x, int src_y, int dst_x, int dst_y)
{
    if (thing == THING_NONE) {
        sensor_process_floor(src_x, src_y, THING_NONE, same, 0, 0);
        return;
    }
    if (flying) dungeon_unlink_thing_from_square(thing, party.cur_map, src_x, src_y);
    else sensor_process_floor(src_x, src_y, thing, dun_on_party_square(src_x, src_y), 0, dst_x < 0 && dst_y < 0);
    if (thing_type(thing) == THING_GROUP && dst_x < -1) {
        int a = group_active(thing);
        if (a != GROUP_NOT_ACTIVE) creature_deactivate_group(a);
    }
}

int move_thing(uint16_t thing, int src_x, int src_y, int dst_x, int dst_y)
{
    party.teleporter_flag = 0;
    int flying = 0, same = 0, stays = 0;
    int start_map = party.cur_map, start_dir = game.party_dir;
    int map = start_map, type = -1, cell = 0, dest_dir = party.dest_dir;
    if (thing == THING_NONE) {
        party.redraw = REDRAW_ALL_VIEW;
        if (src_x >= 0) move_check_missiles_hit_party(src_x, src_y, dst_x, dst_y);
    } else {
        type = thing_type(thing);
        cell = move_thing_cell(thing);
    }
    if (dst_x >= 0) {
        flying = move_follow_square_transitions(thing, src_x, src_y, dst_x, dst_y, 1);
        if (thing != THING_NONE) {
            if (item_attr(thing, ATTR_LIGHT_SOURCE) && (start_map == game.party_map || party.dest_map == game.party_map))
                party.redraw |= REDRAW_VIEW;
            thing = move_set_cell(thing, party.dest_cell);
        }
        dst_x = party.dest_x;
        dst_y = party.dest_y;
        dest_dir = party.dest_dir;
        map = party.dest_map;
        dun_set_current_map(map);
        if (type == THING_GROUP && !creature_is_native_to_map(thing, map)) {
            creature_drop_possessions(thing, dst_x, dst_y, DROP_ALL, 1);
            dun_set_current_map(start_map);
            if (src_x >= 0) creature_delete_group_at(src_x, src_y, DROP_NOTHING, -1);
            return 1;
        }
        same = map == start_map && dst_x == src_x && dst_y == src_y;
        if (same) {
            stays = thing == THING_NONE ? game.party_dir == start_dir : party.dest_cell == cell && type != THING_PROJECTILE;
        } else if (map != start_map) {
            if (thing == THING_NONE && game.party_map != start_map) dun_set_party_location(game.party_x, game.party_y, start_map);
            else dun_set_current_map(start_map);
        }
    }
    if (!stays && src_x >= 0) move_thing_leave(thing, flying, same, src_x, src_y, dst_x, dst_y);
    if (!stays && dst_x >= 0) {
        if (thing == THING_NONE) move_party_arrive(map, start_map, dst_x, dst_y, same);
        else if (type == THING_GROUP) {
            if (move_group_arrive(thing, map, dst_x, dst_y, flying)) return 2;
        } else {
            move_object_arrive(thing, type, map, start_map, src_x, src_y, dst_x, dst_y);
        }
    }
    party.dest_dir = (int16_t)dest_dir;
    party.dest_x = (int16_t)dst_x;
    party.dest_y = (int16_t)dst_y;
    party.dest_map = (int16_t)map;  /* the original writes a stale register when no destination is given */
    dun_set_current_map(start_map);
    return 0;
}

/* ---- Discarded things: the reuse of records when a thing table is full ---- */

#define SENSOR_KIND_TABLE 0x2c
#define SENSOR_SHOP_DISPLAY 0x30
#define TEXT_KEEPS_SQUARE 4             /* a text ornament subtype that protects the things of its square */
#define DISCARD_NEAR 5                  /* the things this near to the party on its map stay */

#define DELETE_GROUP_DISCARD 1
/* The sensor kinds whose square keeps its things. */
static const uint8_t sensor_blocks_discard[SENSOR_KIND_TABLE] = {
    0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1,
};
uint8_t last_discarded_map[THING_TYPE_COUNT];

/* The next map of the discard search after map: the party map comes last; alt (the map of a seamless passage, -1
   none) comes when the search is back at stop. */
static int discard_next_map(int map, int stop, int *alt)
{
    int count = dungeon_map_count();
    if (*alt == -1) {
        do {
            if (++map >= count) map = 0;
        } while (map == game.party_map);
        return map == stop ? game.party_map : map;
    }
    do {
        if (++map >= count) map = 0;
        if (map == stop) {
            map = *alt;
            *alt = -1;
        }
    } while (map == *alt || map == game.party_map);
    return map;
}

/* Takes the thing t of type from the square (x, y) of map (holder: the static creature that carries it, THING_NONE
   none) when it may go; returns 1 then, and *reserved drops the item kind that a shop display keeps. */
static int discard_take(uint16_t t, int type, int subtype, int map, int x, int y, uint16_t holder, int *reserved)
{
    int kind;
    switch (type) {
    case THING_GROUP:
        if (group_flag(t, GROUP_DO_NOT_DISCARD) || thing_word(t, GROUP_POSSESSIONS) != THING_ENDOFLIST
            || (subtype != DISCARD_ANY_KIND && subtype != group_creature(t)))
            return 0;
        dun_set_current_map(map);
        creature_delete_group_at(x, y, DELETE_GROUP_DISCARD, -1);
        return 1;
    case THING_PROJECTILE:
        if (subtype != DISCARD_ANY_KIND && item_gdat_index(thing_word(t, PROJECTILE_PAYLOAD)) != subtype) return 0;
        projectile_cancel_event(t);
        dungeon_unlink_thing_from_square(t, map, x, y);  /* fix: the original unlinks from the current map */
        projectile_destroy(t, NULL, x, y);
        return 1;
    case THING_WEAPON: case THING_ARMOUR: case THING_MISC:
        if (item_is_kept(t)) return 0;
        kind = item_type(t);
        break;
    case THING_POTION:
        if (item_is_kept(t)) return 0;
        kind = potion_type(t);
        break;
    default:
        return 1;  /* kept quirk: the original returns the other types without taking them from their square */
    }
    if (kind == *reserved) {
        *reserved = DISCARD_ANY_KIND;
        return 0;
    }
    if (subtype != DISCARD_ANY_KIND && kind != subtype) return 0;
    dun_set_current_map(map);
    move_thing(t, x, y, -1, holder != THING_NONE ? -1 : 0);
    return 1;
}

/* Searches map for a thing of type that may go and takes it; THING_NONE when there is none. The second pass on the
   party map lets a shop display keep one item of its kind and lets the other things of its square go. */
static uint16_t discard_scan_map(int type, int subtype, int map, int second)
{
    for (int x = 0; x < dungeon_map_width(map); x++)
        for (int y = 0; y < dungeon_map_height(map); y++) {
            if (!(dungeon_square(map, x, y) & SQUARE_HAS_THINGS)) continue;
            int reserved = DISCARD_ANY_KIND;
            int near = map == game.party_map && abs(x - game.party_x) <= DISCARD_NEAR && abs(y - game.party_y) <= DISCARD_NEAR;
            uint16_t holder = THING_NONE;
            uint16_t t = dungeon_square_first_thing(map, x, y);
            for (;;) {
                int tt = thing_type(t), stop = 0;
                if (tt == THING_SENSOR) {
                    int kind = sensor_type(t);
                    if (kind < SENSOR_KIND_TABLE && sensor_blocks_discard[kind]) stop = 1;
                    else if (second && near && kind == SENSOR_SHOP_DISPLAY) {
                        near = 0;
                        if (item_index_to_thing_type(sensor_data(t)) == type) reserved = item_index_gdat_index(sensor_data(t));
                    }
                } else if (tt == THING_TEXT) {
                    stop = text_kind(t) == TEXT_KIND_ORNAMENT && text_subtype(t) == TEXT_KEEPS_SQUARE;
                } else if (!(tt > THING_SENSOR && near)) {
                    if (tt == type) {
                        if (discard_take(t, type, subtype, map, x, y, holder, &reserved)) return t;
                    } else if (tt == THING_GROUP && type != THING_PROJECTILE
                               && thing_word(t, GROUP_POSSESSIONS) != THING_ENDOFLIST
                               && creature_group_info(t)->flags & CREATURE_STATIC) {
                        holder = t;
                        t = thing_word(t, GROUP_POSSESSIONS);
                        continue;
                    }
                }
                t = stop ? THING_ENDOFLIST : dungeon_next_thing(t);
                while (t == THING_ENDOFLIST && holder != THING_NONE) {
                    t = dungeon_next_thing(holder);
                    holder = THING_NONE;
                }
                if (t == THING_ENDOFLIST) break;
            }
        }
    return THING_NONE;
}

uint16_t dun_get_discarded_thing(int type, int subtype)
{
    if (type == THING_EXPLOSION) return THING_NONE;
    int saved = party.cur_map, alt = party.alt_view_active ? party.alt_view_map : -1;
    int map = last_discarded_map[type];
    if (map == game.party_map || map == alt) map = discard_next_map(map, map, &alt);
    int start = map;
    for (int second = 0;;) {
        uint16_t t = discard_scan_map(type, subtype, map, second);
        if (t != THING_NONE) {
            dun_set_current_map(saved);
            last_discarded_map[type] = (uint8_t)map;
            return t & THING_ID_MASK;
        }
        if (map != game.party_map && dungeon_map_count() > 1) {
            map = discard_next_map(map, start, &alt);
        } else if (second) {
            last_discarded_map[type] = (uint8_t)map;
            return THING_NONE;
        } else {
            second = 1;
        }
    }
}
