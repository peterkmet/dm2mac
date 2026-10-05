/* Party movement and thing movement through pits, teleporters, and stairs. */
#include <stdlib.h>
#include "champion.h"
#include "creature.h"
#include "draw.h"
#include "dungeon.h"
#include "game.h"
#include "gdat.h"
#include "hooks.h"
#include "input.h"
#include "item.h"
#include "move.h"
#include "random.h"
#include "save.h"
#include "sensor.h"
#include "timeline.h"
#include "ui.h"
#include "view.h"

#define CAT_WALLSET 8
#define MAP_KEY_PIT_TO_MARKER 0x6a  /* map graphics value: pits lead to a random marker, not one level down */
#define ATTR_LIGHT 0xf8             /* item attribute: the item gives light */
#define ATTR_TELEPORT_SCOPE 0x1e    /* creature attribute: teleporters that move the creature */
#define TRANSITION_MAX 50           /* the loop limit of move_follow_square_transitions */

/* Teleporter record bits. */
#define TELEPORTER_ROTATION_SHIFT 10
#define TELEPORTER_ABSOLUTE 0x1000      /* the rotation sets the direction, it does not add to it */
#define TELEPORTER_SCOPE_SHIFT 13       /* bits 13-14: who uses the teleporter */
#define TELEPORTER_SCOPE_MASK 3
#define TELEPORTER_CREATURES_ONLY 0x2000
#define TELEPORTER_AUDIBLE 0x8000
#define TELEPORTER_FLAG_SHIFT 15
#define SCOPE_ALL 3
#define SCOPE_CREATURES 1
#define SCOPE_PARTY 2

/* Text marker kinds (text record bits 11-15) that the pits read. */
#define TEXT_KIND_MASK 6
#define TEXT_KIND_ORNAMENT_DATA 2
#define TEXT_SUBTYPE_SHIFT 11
#define TEXT_VALUE_SHIFT 3
#define MARKER_PIT_TARGET 0x0c          /* a pit square: the marker id of its targets */

/* Champion record values. */
#define WOUND_FEET 0x20
#define LOAD_PER_STEP_FACTOR 3          /* stamina for a step: 3 * load / max load + 1 */
#define LOAD_PER_FALL_FACTOR 25
#define FALL_DAMAGE_MAX 0x11
#define STAMINA_REDRAW_LIMIT 10

static const int8_t dir_dx[DIR_COUNT] = {0, 1, 0, -1}, dir_dy[DIR_COUNT] = {-1, 0, 1, 0};
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
        return state != 0 && state != 1 && state != 5;
    }
    case SQUARE_FAKE_WALL: return !(square & SQUARE_FAKE_WALL_OPEN) && !(square & 1);
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
        delay = load * 8 > max * 5 ? 3 : 2;
        wounded_extra = 1;
    } else {
        delay = (int)((load - max) * 4) / (int)max + 4;
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
    return (uint16_t)champ_get_load(champ) / 10 + (c->redraw & 0x10) ? 50 : 40;
}

static int champ_get_party_move_time(void)
{
    int sum = 0;
    for (int i = 0; i < game.champion_count; i++) sum += champ_get_move_time(i);
    return sum;
}

/* The champion of the cell nearest to the side of the party square that faces (x, y), -1 none. */
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

static int champ_get_nearest_at(int x, int y, int cell)
{
    static const uint8_t attack_cell_orders[8][4] = {
        {0, 1, 3, 2}, {1, 0, 2, 3}, {1, 2, 0, 3}, {2, 1, 3, 0}, {3, 2, 0, 1}, {2, 3, 1, 0}, {0, 3, 1, 2}, {3, 0, 2, 1}
    };
    if (!game.champion_count) return -1;
    if (abs(x - party.square_x) + abs(y - party.square_y) >= 2) return -1;
    int dir = dun_get_direction_to(party.square_x, party.square_y, x, y);
    if (!(dir * 2 & 2)) cell++;
    const uint8_t *cells = attack_cell_orders[(dir * 2 & 0xffff) + (cell >> 1 & 1)];
    for (int i = 0; i < 4; i++) {
        int c = champ_get_index_in_cell(cells[i]);
        if (c >= 0) return c;
    }
    return -1;
}

/* Damage of a bump into a wall or a creature for the two champions on that side; returns 1 when one was hurt. */
static int move_party_bump_damage(int x, int y, int16_t *first, int16_t *second, int dir, int step)
{
    int side = step + dir + 2;
    int16_t champs[2];
    champs[0] = *first = (int16_t)champ_get_nearest_at(x, y, side & 3);
    champs[1] = *second = (int16_t)champ_get_nearest_at(x, y, (side + 1) & 3);
    if (champs[0] == champs[1]) champs[1] = -1;
    int hurt = 0;
    for (int i = 0; i < 2; i++) {
        if (champs[i] < 0) continue;
        if (champ_take_damage(champs[i], 1, 0x18, 2)) {
            hurt = 1;
            sound_play_with_fallback(GDAT_CHAMPION_CATEGORY, champion(champs[i])->portrait, 0x8a, 0xfe, x, y, 1, 100,
                                     200);
        }
    }
    return hurt;
}

/* ---- Steps ---- */

enum step_result { STEP_STAIRS_HERE = 1, STEP_STAIRS_AHEAD, STEP_BLOCKED, STEP_CREATURE, STEP_CREATURE_SIDE, STEP_FREE };

/* The creature group whose body covers the point of cell (0xff: any cell) of the square (*x, *y) or of a
   square next to it; moves *x, *y to that square. THING_NONE when there is none. */
static uint16_t creature_find_group_near_point(int16_t *x, int16_t *y, int cell)
{
    /* The squares to check after the first one: by cell, the original reads 4 pairs from an offset of 2 pairs per
       cell (its table has rows of 4 pairs), which this table keeps. */
    static const int16_t hit_offsets[10][2] = {
        {0, 0}, {0, -1}, {0, -1}, {1, 0}, {1, 0}, {0, 1}, {-1, 0}, {0, 1}, {0, 1}, {0, -1}
    };
    static const int16_t hit_offsets_any_cell[4][2] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
    static const int16_t hit_radius_sq[] = {0, 3, 6, 6};
    static const uint16_t cell_grid[4] = {6, 8, 18, 16};
    int16_t sx = *x, sy = *y, px, py, cx, cy;
    view_grid5_cell_to_xy(sx, sy, cell == 0xff ? 12 : cell_grid[cell], &px, &py);
    for (int i = 0; i <= 4; i++) {
        uint16_t g = creature_group_at(party.cur_map, sx, sy);
        if (g != THING_NONE) {
            const uint8_t *r = dungeon_thing_record(g);
            int size = (int8_t)creature_group_info(g)->hit_size;
            int zone = creature_get_view_cell_zone(g, -(r[GROUP_DIR + 1] & 3) & 3);
            view_grid5_cell_to_xy(sx, sy, (uint16_t)zone, &cx, &cy);
            cx = (int16_t)(cx - px);
            cy = (int16_t)(cy - py);
            if ((int16_t)(cy * cy + cx * cx) < hit_radius_sq[size]) {
                *x = sx;
                *y = sy;
                return g;
            }
        }
        if (i == 4) break;
        if (cell == 0xff) {
            sx = (int16_t)(*x + hit_offsets_any_cell[i][0]);
            sy = (int16_t)(*y + hit_offsets_any_cell[i][1]);
        } else {
            sx = (int16_t)(*x + hit_offsets[cell * 2 + i][0]);
            sy = (int16_t)(*y + hit_offsets[cell * 2 + i][1]);
        }
    }
    return THING_NONE;
}

/* True when the creature in front stands on a side cell that blocks the step. */
static int move_group_side_blocks(uint16_t group, int dir)
{
    const uint8_t *r = dungeon_thing_record(group);
    int side = ((r[GROUP_DIR + 1] & 3) + dir) & 3;
    if (side != DIR_EAST && side != DIR_WEST) return 0;
    return creature_get_view_cell_zone(group, side) % 5 != 2;
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
        uint16_t g = creature_find_group_near_point(&gx, &gy, 0xff);
        if (g == THING_NONE || creature_group_info(g)->flags & CREATURE_NOT_SOLID) return STEP_FREE;
    } else if (!move_group_side_blocks(*group, game.party_dir)) {
        return STEP_CREATURE;
    }
    return STEP_CREATURE_SIDE;
}

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
        /* Step 16 ports the push of a creature; until then no push is pending */
        party.push_pending_group = THING_NONE;
        if (!party.push_mode) party.push_arrow = 6;
        return 1;
    } else {
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
            break;
        case STEP_BLOCKED:
            if (!game.champion_count) goto refused;
            move_party_bump_damage(x, y, &first, &second, dir, step);
            if (square_type(ahead) == SQUARE_DOOR && (ahead & SQUARE_DOOR_STATE) == 4) {
                int strength = 0;
                if (first != -1) {
                    Champion *c = champion(first);
                    int r = rand_next() & 0xf;
                    strength = champ_scale_by_stamina(c, (int16_t)(champ_get_stat(c, STAT_STRENGTH, 0) + r));
                }
                if (second != first && second != -1) {
                    Champion *c = champion(second);
                    int r = rand_next() & 0xf;
                    strength += champ_scale_by_stamina(c, (int16_t)(champ_get_stat(c, STAT_STRENGTH, 0) + r));
                }
                sensor_door_try_destroy(x, y, (int16_t)strength, 0, 0);
            }
            goto refused;
        case STEP_CREATURE: {
            if (!game.champion_count) goto refused;
            int is_static = creature_group_info(group)->flags & CREATURE_STATIC;
            if (is_static || creature_group_info(group)->push_resistance == 0xff)
                move_party_bump_damage(x, y, &first, &second, dir, step);
            int push_dir = (dir + command - CMD_MOVE_FORWARD) & 3;
            if (creature_can_be_pushed(x, y, push_dir, &group) && move_party_can_push_group(group)) {
                creature_push_group(x, y, push_dir, 0xfe);
                goto free_step;
            }
            if (!is_static) creature_alert(THING_NONE, x, y, 0x4005, 5, 0);
            goto refused;
        }
        case STEP_CREATURE_SIDE:
            goto refused;
        case STEP_FREE:
        free_step:
            if (party.alt_view_active && step == 2) {
                dun_set_current_map(map);
                x = party.square_x;
                y = party.square_y;
                dir = party.ref_dir;
                dun_move_relative(dir, -1, 0, &x, &y);
            }
            if (on_stairs) {
                move_thing(THING_NONE, -1, 0, x, y);
                break;
            }
            PassageEnd end;
            if (dun_get_linked_teleporter_dest(&end, x, y) && ((end.target_side + 2) & 3) != dir) {
                move_party_to(end.x, end.y, end.map, end.target_side);
                break;
            }
            move_thing(THING_NONE, game.party_x, game.party_y, x, y);
            break;
        }
    }
    int add = party.move_steps != 0;
    if (half > add) add = half;
    game.party_move_cooldown = (uint16_t)(game.party_move_cooldown + add);
    game.throw_cooldown = 0;
    return 1;
refused:
    input_flush();
    input_command_stop = 0;
    return 0;
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
    int turn;
    party.alt_view_map = -1;
    party.alt_view_active = dun_get_linked_teleporter_dest(&end, x, y);
    if (party.alt_view_active) {
        party.alt_view_x = end.x;
        party.alt_view_y = end.y;
        turn = end.target_side - end.side;
    } else {
        int d;
        for (d = 0; d < DIR_COUNT; d++)
            if (dun_get_linked_teleporter_dest(&end, x + dir_dx[d], y + dir_dy[d])) break;
        if (d == DIR_COUNT) goto done;
        turn = end.target_side + 6 - end.side;
        int side = (d + turn + 2) & 3;
        party.alt_view_x = (int16_t)(end.x + dir_dx[side]);
        party.alt_view_y = (int16_t)(end.y + dir_dy[side]);
    }
    party.alt_view_map = end.map;
    party.alt_view_dir = (int16_t)((turn + game.party_dir) & 3);
done:
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
                    uint16_t w = thing_word(t, TEXT_DATA);
                    if ((w & TEXT_KIND_MASK) != TEXT_KIND_ORNAMENT_DATA) continue;
                    int kind = w >> TEXT_SUBTYPE_SHIFT, id = w >> TEXT_VALUE_SHIFT & 0xff;
                    if (kind == MARKER_TARGET && id == marker) {
                        if (mode == MARKER_COUNT) count++;
                        else if (--nth == 0) goto hit;
                    } else if (kind == MARKER_SWITCH) {
                        if (mode == MARKER_CLEAR_ALL) set_thing_word(t, TEXT_DATA, w & 0xfffe);
                        else if (mode == MARKER_FIND_SET && w & 1) goto hit;
                    } else if (kind == MARKER_POSITION && id == marker) {
                        *x = (int16_t)mx;
                        *y = (int16_t)my;
                        found = map;
                    }
                    continue;
                hit:
                    *x = (int16_t)mx;
                    *y = (int16_t)my;
                    return map;
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
    uint16_t w = thing_word(teleporter, TELEPORTER_TARGET);
    int rotation = w >> TELEPORTER_ROTATION_SHIFT & 3;
    if (!(w & TELEPORTER_ABSOLUTE)) {
        thing = (uint16_t)(((rotation + (thing >> THING_CELL_SHIFT)) & 3) << THING_CELL_SHIFT | (thing & 0x3fff));
        rotation = (party.dest_dir + rotation) & 3;
    }
    party.dest_dir = (int16_t)rotation;
    return thing;
}

/* Follows pits, teleporters, and stairs from (x, y) of the current map; leaves the end in party.dest_*.
   Returns 1 for a flying thing. */
static int move_follow_square_transitions(uint16_t thing, int src_x, int src_y, int x, int y, int creature_sound)
{
    party.teleporter_flag = 0;
    int start_map = party.cur_map, map = start_map;
    int fell_hurt = 0, falls = 0, flying, draw_fall, scope, type;
    if (thing == THING_NONE) {
        if (party.push_mode) {
            party.push_mode = 0;
            ui.push_mode = 0;
            ui_draw_movement_arrows();
        }
        move_set_party_position(x, y);
        scope = SCOPE_PARTY;
        draw_fall = !ui.inventory && !party_sleeping;
        type = -1;
        flying = 0;
    } else {
        flying = move_thing_is_flying(thing);
        type = thing_type(thing);
        scope = SCOPE_ALL;
        if (type == THING_GROUP) scope = item_attr(thing, ATTR_TELEPORT_SCOPE) ? SCOPE_PARTY : SCOPE_CREATURES;
        else if (type == THING_PROJECTILE)
            party.dest_dir = (int16_t)(timeline.events[thing_word(thing, PROJECTILE_EVENT)].data[2] >> 2 & 3);
        draw_fall = 0;
    }
    for (int loop = TRANSITION_MAX; --loop;) {
        uint8_t square = dungeon_square(party.cur_map, x, y);
        int square_kind = square_type(square);
        if (square_kind == SQUARE_TELEPORTER) {
            if (!(square & SQUARE_TELEPORTER_ON)) break;
            uint16_t tele = first_thing(x, y);
            uint16_t w = thing_word(tele, TELEPORTER_TARGET), m = thing_word(tele, TELEPORTER_MAP);
            if ((w & 0x6000) == TELEPORTER_CREATURES_ONLY && type != THING_GROUP) break;
            if (scope != SCOPE_ALL && !((w >> TELEPORTER_SCOPE_SHIFT & TELEPORTER_SCOPE_MASK) & scope)) break;
            int tx = w & 0x1f, ty = w >> 5 & 0x1f, tmap = m >> 8;
            int same = tx == x && ty == y && tmap == map;
            x = tx;
            y = ty;
            party.teleporter_flag = w >> TELEPORTER_FLAG_SHIFT;
            map = tmap;
            dun_set_current_map(map);
            if (thing == THING_NONE) {
                move_set_party_position(x, y);
                if (w & TELEPORTER_AUDIBLE) sound_request(0x18, 0, 0x89, 0x61, 0x80, game.party_x, game.party_y, -1);
                if (start_map != map) draw_fall = 0;  /* the original keeps it only with its debug flag */
                int base = w & TELEPORTER_ABSOLUTE ? 0 : game.party_dir;
                champ_set_party_direction((base + (w >> TELEPORTER_ROTATION_SHIFT & 3)) & 3);
            } else if (type == THING_GROUP && creature_sound) {
                if (w & TELEPORTER_AUDIBLE) {
                    if (party.cur_map != start_map) {
                        int saved = party.cur_map;
                        dun_set_current_map(start_map);
                        sound_request(0x18, 0, 0x89, 0x61, 0x80, src_x, src_y, 1);
                        dun_set_current_map(saved);
                    }
                    sound_request(0x18, 0, 0x89, 0x61, 0x80, x, y, 1);
                }
                creature_turn_group(thing, w >> 12 & 1, w >> TELEPORTER_ROTATION_SHIFT & 3);
            } else if (type == THING_PROJECTILE) {
                thing = move_rotate_through_teleporter(tele, thing);
            } else if (!(w & TELEPORTER_ABSOLUTE) && src_x != -2) {
                int cell = ((w >> TELEPORTER_ROTATION_SHIFT & 3) + (thing >> THING_CELL_SHIFT)) & 3;
                thing = (uint16_t)((thing & 0x3fff) | cell << THING_CELL_SHIFT);
            }
            if (same) break;
            continue;
        }
        if (square_kind == SQUARE_PIT && !flying && square & SQUARE_PIT_OPEN && !(square & 1)) {
            int to_marker = gdat_entry_index(draw_gdat(), CAT_WALLSET, dungeon_map_graphics(party.cur_map) >> 4 & 0xf,
                                             GDAT_TYPE_VALUE, MAP_KEY_PIT_TO_MARKER);
            if (to_marker && type == THING_GROUP) {
                uint16_t home = thing_word(thing, GROUP_HOME);
                x = home & 0x1f;
                y = home >> 5 & 0x1f;
                map = home >> 10;
                dun_set_current_map(map);
                continue;
            }
            if (draw_fall && !to_marker && !party.fell) {
                draw_fall = 0;
                if (falls) {
                    dun_set_party_location(x, y, map);
                    view_enter_map(map);
                }
                int steps = party.move_steps;
                party.move_steps = 0;
                view_draw_dungeon(game.party_dir, x, y);
                party.move_steps = (int16_t)steps;
            }
            if (to_marker) {
                int marker = 0;
                for (uint16_t t = first_thing(x, y); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
                    type = thing_type(t);  /* the original reuses its thing type variable here */
                    if (type > THING_SENSOR) break;
                    if (type != THING_TEXT) continue;
                    uint16_t w = thing_word(t, TEXT_DATA);
                    if ((w & TEXT_KIND_MASK) == TEXT_KIND_ORNAMENT_DATA && w >> TEXT_SUBTYPE_SHIFT == MARKER_PIT_TARGET) {
                        marker = w >> TEXT_VALUE_SHIFT & 0xff;
                        break;
                    }
                }
                int16_t mx = (int16_t)x, my = (int16_t)y;
                int count = dun_find_marker(MARKER_COUNT, 0, marker, &mx, &my);
                map = dun_find_marker(MARKER_FIND_NTH, rand_below((uint16_t)count) + 1, marker, &mx, &my);
                x = mx;
                y = my;
            } else {
                falls++;
                map = dungeon_location_on_map(map, 1, &x, &y);
            }
            dun_set_current_map(map);
            if (thing == THING_NONE) {
                move_set_party_position(x, y);
                if (game.champion_count) {
                    if (party.fell) {
                        draw_fall = 0;  /* the original keeps it only with its debug flag */
                        for (int i = 0; i < game.champion_count; i++) {
                            Champion *c = champion(i);
                            if (!c->health) continue;
                            int load = (uint16_t)champ_get_load(i) * LOAD_PER_FALL_FACTOR;
                            champ_decrease_stamina(i, (int16_t)(load / (uint16_t)champ_get_max_load(c) + 1));
                        }
                    } else {
                        for (int i = 0; i < game.champion_count; i++) {
                            Champion *c = champion(i);
                            if (c->health) {
                                int base = c->max_health >> 2;
                                if (base > FALL_DAMAGE_MAX) base = FALL_DAMAGE_MAX;
                                champ_take_damage(i, (int16_t)((rand_2bits() + base) * falls), 0x30, 2);
                                sound_play_with_fallback(GDAT_CHAMPION_CATEGORY, c->portrait, 0x87, 0xfe, game.party_x,
                                                         game.party_y, -1, 0xbe, 0xff);
                            }
                            fell_hurt = 1;
                        }
                    }
                }
                party.fell = 0;
                continue;
            }
            if (type == THING_GROUP && creature_sound && src_x >= 0) {
                dun_set_current_map(start_map);
                creature_alert(thing, src_x, src_y, 0, 0, 0x14);
            }
            dun_set_current_map(map);
            continue;
        }
        if (square_kind == SQUARE_STAIRS && thing != THING_NONE && type != THING_PROJECTILE && type != THING_GROUP) {
            if (!(square & SQUARE_STAIRS_UP)) {
                map = dungeon_location_on_map(map, 1, &x, &y);
                dun_set_current_map(map);
            }
            int dir = dun_get_stairs_exit_direction(x, y);
            x += dir_dx[dir];
            y += dir_dy[dir];
            int back = (dir + 2) & 3;
            int cell = (back + ((((thing >> THING_CELL_SHIFT) - back) + 1) & 2) / 2) & 3;
            thing = (uint16_t)((thing & 0x3fff) | cell << THING_CELL_SHIFT);
            continue;
        }
        break;
    }
    party.dest_x = (int16_t)x;
    party.dest_y = (int16_t)y;
    party.dest_map = (int16_t)map;
    party.dest_cell = (int16_t)(thing >> THING_CELL_SHIFT);
    if (falls && creature_sound && thing != THING_NONE)
        sound_play_with_fallback(item_gdat_category(thing), item_gdat_index(thing), 0x85, 0xfe, x, y, 1, 0x3a, 0x80);
    if (fell_hurt) {
        int saved_map = party.cur_map, px = game.party_x, py = game.party_y, pmap = game.party_map;
        party.cur_map = -1;
        dun_set_party_location(x, y, map);
        for (int i = 0; i < game.champion_count; i++)
            if (champion(i)->health)
                sound_play_with_fallback(GDAT_CHAMPION_CATEGORY, champion(i)->portrait, 0x82, 0xfe, x, y, 2, 0x69, 0xc8);
        dun_set_party_location(px, py, pmap);
        dun_set_current_map(saved_map);
    }
    dun_set_current_map(start_map);
    sound_flush_requests(1);
    return flying;
}

/* A creature arrives on the party square: the party steps aside and takes damage. */
static int creature_displaces_party(uint16_t thing, int x, int y)
{
    int16_t nx, ny;
    if (!move_find_free_adjacent_square(THING_NONE, x, y, &nx, &ny)) return 0;
    int damage = rand_below(10) + (creature_group_info(thing)->push_resistance >> 3);
    champ_damage_party((uint16_t)damage, 4, 2);
    sound_play_with_fallback(GDAT_CHAMPION_CATEGORY, 0xfe, 0x8a, 0xfe, x, y, 1, 100, 200);
    move_thing(THING_NONE, x, y, nx, ny);
    return 1;
}

int move_thing(uint16_t thing, int src_x, int src_y, int dst_x, int dst_y)
{
    party.teleporter_flag = 0;
    int flying = 0, same = 0;
    int start_map = party.cur_map, start_dir = game.party_dir;
    int map = start_map, type = -1, cell = 0, dest_dir = party.dest_dir;
    int active = 0xff;
    if (thing == THING_NONE) {
        party.redraw = REDRAW_ALL_VIEW;
        if (src_x >= 0) move_check_missiles_hit_party(src_x, src_y, dst_x, dst_y);
    } else {
        type = thing_type(thing);
        cell = thing >> THING_CELL_SHIFT;
    }
    if (dst_x >= 0) {
        flying = move_follow_square_transitions(thing, src_x, src_y, dst_x, dst_y, 1);
        if (thing != THING_NONE) {
            if (item_attr(thing, ATTR_LIGHT) && (start_map == game.party_map || party.dest_map == game.party_map))
                party.redraw |= REDRAW_VIEW;
            thing = (uint16_t)((thing & 0x3fff) | party.dest_cell << THING_CELL_SHIFT);
        }
        dst_x = party.dest_x;
        dst_y = party.dest_y;
        dest_dir = party.dest_dir;
        map = party.dest_map;
        dun_set_current_map(map);
        if (type == THING_GROUP && !creature_is_native_to_map(thing, map)) {
            creature_drop_possessions(thing, dst_x, dst_y, 0, 1);
            dun_set_current_map(start_map);
            if (src_x >= 0) creature_delete_group_at(src_x, src_y, 2, -1);
            return 1;
        }
        same = map == start_map && dst_x == src_x && dst_y == src_y;
        if (same) {
            if (thing == THING_NONE ? game.party_dir == start_dir : party.dest_cell == cell && type != THING_PROJECTILE)
                goto end;
        } else if (map != start_map) {
            if (thing == THING_NONE && game.party_map != start_map) dun_set_party_location(game.party_x, game.party_y, start_map);
            else dun_set_current_map(start_map);
        }
    }
    if (src_x >= 0) {
        if (thing == THING_NONE) {
            sensor_process_floor(src_x, src_y, THING_NONE, same, 0, 0);
        } else {
            if (flying) dungeon_unlink_thing_from_square(thing, party.cur_map, src_x, src_y);
            else sensor_process_floor(src_x, src_y, thing, dun_on_party_square(src_x, src_y), 0, dst_x < 0 && dst_y < 0);
            if (type == THING_GROUP && dst_x < -1) {
                int a = dungeon_thing_record(thing)[GROUP_ACTIVE];
                if (a != 0xff) creature_deactivate_group(a);
            }
        }
    }
    if (dst_x < 0) goto end;
    if (thing == THING_NONE) {
        dun_set_current_map(map);
        move_compute_map_arrival(dst_x, dst_y, map);
        uint16_t g = creature_group_at(party.cur_map, game.party_x, game.party_y);
        if (g != THING_NONE && !(creature_group_info(g)->flags & CREATURE_NOT_SOLID)) {
            int16_t nx, ny;
            const CreatureInfo *info = creature_group_info(g);
            int displaced = 0;
            if (info->flags & CREATURE_STATIC && info->push_resistance > 0xfd) {
                if (move_find_free_adjacent_square(THING_NONE, dst_x, dst_y, &nx, &ny)) {
                    dun_set_party_location(nx, ny, map);
                    party.new_map = (int16_t)map;
                    move_thing(THING_NONE, -1, 0, nx, ny);
                    dun_set_party_location(game.party_x, game.party_y, start_map);
                    if (party.new_map != start_map) goto end;
                    party.new_map = -1;
                    displaced = 1;
                }
            } else if (move_find_free_adjacent_square(g, dst_x, dst_y, &nx, &ny)) {
                int t = (uint16_t)champ_get_party_move_time() >> 2;
                creature_alert(g, dst_x, dst_y, 0x2000, 0, (uint16_t)(rand_below((uint16_t)(((int16_t)t >> 1) + 1)) + t));
                move_thing(g, dst_x, dst_y, nx, ny);
                displaced = 1;
            }
            if (!displaced) {
                creature_cancel_update(g);
                if (!move_thing(g, game.party_x, game.party_y, -3, 0))
                    move_schedule_group_retry(g, game.party_x, game.party_y, map, 0);
            }
        }
        if (map == start_map) sensor_process_floor(game.party_x, game.party_y, THING_NONE, same, 1, 0);
        else party.new_map = (int16_t)map;
        goto end;
    }
    if (type == THING_GROUP) {
        dun_set_current_map(map);
        active = dungeon_thing_record(thing)[GROUP_ACTIVE];
        if (map == party.square_map && dst_x == party.square_x && dst_y == party.square_y
            && !(creature_group_info(thing)->flags & CREATURE_NOT_SOLID) && !creature_displaces_party(thing, dst_x, dst_y))
            goto wait;
        uint16_t other = creature_group_at(party.cur_map, dst_x, dst_y);
        if (other != THING_NONE) {
            int push = creature_group_info(thing)->push_resistance;
            const CreatureInfo *oi = creature_group_info(other), *ti = creature_group_info(thing);
            int16_t nx, ny;
            if (ti->flags & CREATURE_STATIC && push > 0xfd) {
                if (oi->flags & CREATURE_STATIC && oi->push_resistance > 0xfd) goto wait;
                if (!move_find_free_adjacent_square(other, dst_x, dst_y, &nx, &ny)) {
                    creature_cancel_update(thing);
                    move_schedule_group_retry(thing, dst_x, dst_y, map, 0);
                    goto end;
                }
            } else if (oi->flags & CREATURE_STATIC && oi->push_resistance > 0xfd) {
                if (!move_find_free_adjacent_square(thing, dst_x, dst_y, &nx, &ny)) goto wait;
                move_thing(thing, -1, 0, nx, ny);
                goto end;
            } else if (!move_find_free_adjacent_square(other, dst_x, dst_y, &nx, &ny)) {
                goto wait;
            }
            int r = rand_below((uint16_t)(((int16_t)(push >> 2) >> 1) + 1));
            creature_alert(other, dst_x, dst_y, 0x2000, 0, (uint16_t)((push >> 2) + r));
            move_thing(other, dst_x, dst_y, nx, ny);
        }
        if (flying) dungeon_link_thing_to_square(thing, party.cur_map, dst_x, dst_y);
        else sensor_process_floor(dst_x, dst_y, thing, 0, 1, 0);
        move_update_carried_missiles(thing, dst_x, dst_y, map, 0);
        if (active == 0xff) {
            if (!(creature_group_info(thing)->flags & CREATURE_STATIC)) creature_activate_group(thing, dst_x, dst_y);
        } else {
            int event = creature_active_group_event(active);
            if (event != -1) {
                TimelineEvent *e = &timeline.events[event];
                e->data[0] = (uint8_t)dst_x;
                e->data[1] = (uint8_t)dst_y;
                e->time_map = (uint32_t)map << 24 | (e->time_map & 0xffffff);
            }
        }
        goto end;
    wait:
        if (active != 0xff) creature_deactivate_group(active);
        move_schedule_group_retry(thing, dst_x, dst_y, map, party.teleporter_flag);
        return 2;
    }
    dun_set_current_map(map);
    if (type == THING_PROJECTILE) {
        dungeon_link_thing_to_square(thing, party.cur_map, dst_x, dst_y);
        goto end;
    }
    int added = src_x < 0 && src_y < 0, under_static = added;
    if (!added && map != start_map) {
        uint16_t g = creature_group_at(party.cur_map, dst_x, dst_y);
        if (g != THING_NONE) under_static = creature_group_info(g)->flags & CREATURE_STATIC;
    }
    sensor_process_floor(dst_x, dst_y, thing, dun_on_party_square(dst_x, dst_y), 1, under_static);
end:
    party.dest_dir = (int16_t)dest_dir;
    party.dest_x = (int16_t)dst_x;
    party.dest_y = (int16_t)dst_y;
    party.dest_map = (int16_t)map;  /* the original writes a stale register when no destination is given */
    dun_set_current_map(start_map);
    return 0;
}
