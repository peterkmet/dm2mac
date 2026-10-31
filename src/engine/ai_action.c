/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Creature AI commands and actions: the plan commands of a plan step, and what a creature does at the acting step of
   its animation. */
#include "ai.h"
#include <stddef.h>
#include <string.h>
#include "ai_internal.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "item.h"
#include "merchant.h"
#include "move.h"
#include "num.h"
#include "projectile.h"
#include "random.h"
#include "save.h"
#include "sensor.h"
#include "sound_request.h"

/* The attack skill bits of a creature that the candidates and the plan commands test. */
#define ATTACK_SKILL_ITEMS 0x08

/* The scan kinds of the target records that the scanners read. */
#define TARGET_SCAN_SEEK_ITEMS 0x11
#define TARGET_SCAN_MOOD_SET   0x13 /* the target records that set mood bits */
#define TARGET_SCAN_MOOD_CLEAR 0x14 /* ... that clear them */

/* The plan commands of a plan step. */
#define PLAN_CMD_MOVE_FORWARD          0x3f
#define PLAN_CMD_TURN_SIDEWAYS         0x40
#define PLAN_CMD_VANISH                0x41
#define PLAN_CMD_SEEK_ITEM             0x42
#define PLAN_CMD_STOP                  0x43
#define PLAN_CMD_NOP                   0x44
#define PLAN_CMD_PLACE_ON_SENSOR       0x45
#define PLAN_CMD_FRONT_GROUP_CARRIES   0x46
#define PLAN_CMD_SEEK_WITH_CARRIED     0x47
#define PLAN_CMD_CHECK_PARTY_AHEAD     0x48
#define PLAN_CMD_SHOP_WAIT             0x49
#define PLAN_CMD_SHOP_EVALUATE         0x4a
#define PLAN_CMD_SHOP_PAYMENT          0x4b
#define PLAN_CMD_TURN_FRONT_GROUP      0x4c
#define PLAN_CMD_TAKE_FROM_FRONT_GROUP 0x4d
#define PLAN_CMD_DROP_THEN_SEEK        0x4e
#define PLAN_CMD_SET_ACTION_A          0x4f
#define PLAN_CMD_SET_MOOD_FLAG         0x50
#define PLAN_CMD_GO_TO_TARGET          0x51
#define PLAN_CMD_FOLLOW_PATH           0x52
#define PLAN_CMD_SEEK_CACHED_ITEM      0x53
#define PLAN_CMD_SEEK_TARGET_ITEMS     0x54
#define PLAN_CMD_TURN_TO_PARTY         0x55
#define PLAN_CMD_SHOOT_RANDOM          0x56
#define PLAN_CMD_MOVE_RELATIVE         0x57
#define PLAN_CMD_USE_ACTUATOR          0x58
#define PLAN_CMD_SHOP_PAY              0x59
#define PLAN_CMD_SET_ACTION_BY_FLAG    0x5a
#define PLAN_CMD_CONSUME               0x5b
#define PLAN_CMD_CHECK_TRANSFORM       0x5c
#define PLAN_CMD_SET_SPECIAL           0x5d
#define PLAN_CMD_SET_ACTION_B          0x5e
#define PLAN_CMD_SET_ACTION_C          0x5f
#define PLAN_CMD_DOOR_ACTION           0x60
#define PLAN_CMD_RANDOM_CHANCE         0x61
#define PLAN_CMD_CARRIES_ITEM          0x62

/* The missile kind of the random shot command. */
#define SHOT_KIND_RANDOM 0x82

/* Random bits: a creature without food looks for it in 1 of 8 updates. */
#define ROLL_SEEK_FOOD 0x07

/* The item filters of the plan commands. */
#define FILTER_FOOD      0x06
#define FILTER_PLACEABLE 0x10
#define FILTER_ANY_ITEM  0x3f

/* Plan command arguments: any item (a possession test; a drop that removes nothing), and the front group (a test of
   its item). */
#define PLAN_ARG_ANY_ITEM    (-2)
#define PLAN_ARG_FRONT_GROUP 2
#define STAND_VARIANT_LAST   2 /* the last of the three stands of cmd_stand_by_mood */

/* The item skill bits of the plan commands that take and place items. */
#define ITEM_SKILL_TAKE_CARRIED 0x08
#define ITEM_SKILL_PLACE        0x77

/* The ornament subtype of a transform marker. */
#define TEXT_TRANSFORM 0x01

/* A position word without a square. */
#define LOCATION_NONE 0xffff

/* The operations of the set mood flag command; MOOD_OP_QUIET: no rescan. */
#define MOOD_OP_CLEAR            0x00
#define MOOD_OP_SET              0x01
#define MOOD_OP_TEST             0x02
#define MOOD_OP_SET_BY_TARGETS   0x03
#define MOOD_OP_CLEAR_BY_TARGETS 0x04
#define MOOD_OP_MASK             0x0f
#define MOOD_OP_QUIET            0x10
#define MOOD_OP_QUIET_LAST       0x12

/* The checks ahead before a creature waits to place its item again. */
#define PLACE_WAIT_CHECKS 3

/* The cell of a melee attack on the nearest champion. */
#define CELL_NEAREST 4

/* The modes of the explode or spawn action. */
#define MODE_EXPLODE 0
#define MODE_SPAWN   1
#define MODE_BECOME  2

/* The phase of a fall or jump after its landing: the fall is done (the earlier phases are in creature.h). */
#define FALL_PHASE_DONE 3

/* The sparks of a transform. */
#define SPARK_CELLS      5 /* the four cells and the center */
#define TRANSFORM_PHASES 3 /* the phases of sparks before the creature becomes the new type */

/* The item filter bit of an action that repeats for all matching items. */
#define SUB_REPEAT 0x40

/* The push of a melee hit by a creature. */
#define PUSH_BY_CREATURE 0xfe

/* Above this push resistance a melee hit pushes the creature only now and then. */
#define PUSH_RESISTANCE_HEAVY 100

/* Random bits of the actions. */
#define ROLL_PUSH_HEAVY     0x0f /* a heavy creature stays only when all are clear and 2 more bits too */
#define ROLL_STEAL          0x70 /* a steal fails the luck test only when all are clear */
#define ROLL_STEAL_BY_VALUE 0x03 /* when any is set the thief takes the hand item of more value */
#define ROLL_STEAL_HELD     0x04 /* ... else this bit takes the item of the action hand */

/* Above this ninja level plus a random 0-15 a theft wakes the sleeping party. */
#define STEAL_WAKE_THRESHOLD 7

/* The stale register of the original that an action returns. */
#define STALE_REGISTER 0x100

/* The powers of the missiles, throws, sparks, and clouds of the actions. */
#define SHOT_POWER_MIN          4
#define SHOT_POWER_WEAK         8  /* a missile up to this power flies at speed 1, above it 2 */
#define SHOT_POWER_MEDIUM       16 /* ... above this SHOT_SPEED_MEDIUM or one more */
#define SHOT_POWER_STRONG       32 /* ... above this SHOT_SPEED_STRONG to 3 more */
#define SHOT_SPEED_MEDIUM       3
#define SHOT_SPEED_STRONG       7
#define POISON_BOLT_POWER_SCALE 4 /* the poison bolt of a creature has 4 times its power */
#define SPELL_SHOT_POWER_SHIFT  3 /* the poison cloud, push, and pull: 8 times */
#define THROW_SPEED             8
#define NEW_TYPE_HEALTH_SHIFT   3 /* a creature that becomes a new type gets up to an eighth more health */
#define THROW_POWER_MIN         20
#define SPARK_POWER_MIN         100
#define SPARK_POWER_RANGE       156
#define TRANSFORM_POWER_STEP    85 /* the death cloud of a transform grows by this per phase */
#define DODGE_CLOUD_POWER_MIN   20
#define DODGE_CLOUD_POWER_RANGE 40

/* The AI state of this file. */
static struct {
    int16_t dest_x, dest_y; /* the target square of the action that runs */
} ai_state;

static int dest_x(void)
{
    return location_x(g_ai.active->destination);
}

static int dest_y(void)
{
    return location_y(g_ai.active->destination);
}

static Location dest_square(void)
{
    return ai_location(dest_x(), dest_y());
}

/* Sets the destination square of the action on the current map. */
static void set_destination(int16_t x, int16_t y)
{
    g_ai.active->destination = (uint16_t)location_set_xy(g_ai.active->destination, x, y);
}

/* The executed command test of mode. */
static AiTest executed_test(int mode)
{
    return (AiTest){(uint8_t)mode, true};
}

/* Tests and starts the take or give (ITEM_MODE_*) of an item of filter by the group on the square to. */
static AiCommand take_or_give_item(int mode, Location to, int16_t dir, int filter)
{
    return ai_try_take_item(executed_test(mode), ai_group_square(), to, dir, filter);
}

/* The executed move test of a packed move command byte. */
static AiMoveTest executed_move(int command)
{
    AiMoveTest move = ai_move_test(command);
    move.execute = true;
    return move;
}

/* The creature flees: dodges a missile, or steps away in up to two rounds of four directions; true when it moved. */
static bool flee_or_dodge(bool second_round)
{
    ActiveGroup *a = g_ai.active;
    uint32_t class_flags = g_ai_class_flags[g_ai.class_index];
    group_set_mood_flag(g_ai.group_thing, MOOD_LAZY, true);
    for (;;) {
        if (class_flags & AI_CLASS_DODGES) {
            uint16_t mood = group_mood(g_ai.group_thing);
            bool look = (mood & MOOD_FEAR && rand_2bits()) || (mood & MOOD_QUICK && rand_bit()) || !rand_2bits();
            if (look && ai_projectile_incoming(ai_group_square())) {
                a->plan = -1;
                a->plan_step = 0;
                a->action = ACTION_DODGE;
                return true;
            }
        }
        int dir;
        if (ai_next_path_step() > 0)
            dir = a->direction;
        else if (rand_bit())
            dir = dir_opposite(group_dir(g_ai.group_thing));
        else
            dir = rand_2bits();
        int turn = rand_bit() ? 1 : -1;
        for (int tries = DIR_COUNT; tries; tries--) {
            AiMoveTest move = executed_move(second_round ? MOVE_KIND_PLAIN : g_ai.search_flags);
            if (ai_try_move(move, ai_group_square(), AI_SQUARE_IN_DIR, dir).passed) {
                a->plan = -1;
                a->plan_step = 0;
                return true;
            }
            dir = dir_turn(dir, turn);
        }
        if (second_round || !rand_bit())
            return false;
        second_round = true;
    }
}

bool ai_try_flee(uint32_t class_flags)
{
    bool second_round = rand_2bits() == 0;
    Location at = ai_group_square();
    if (class_flags & AI_CLASS_NO_FLEE || ai_try_move(ai_move_test(g_ai.search_flags), at, at, -1).passed)
        return false;
    bool flee =
        (g_ai.search_flags == MOVE_KIND_SAFE_FLEE && ai_projectile_incoming(at)) || !g_ai.alert ||
        !ai_try_attack((AiTest){ATTACK_MODE_PARTY, false}, at, AI_SQUARE_IN_DIR, group_dir(g_ai.group_thing)).passed ||
        rand_below((uint16_t)((creature_armor(g_ai.info) >> 2) + 1)) <= 1;
    return flee && flee_or_dodge(second_round);
}

/* Plan commands: each returns AI_RESULT_DONE, AI_RESULT_FAIL, or AI_RESULT_ISSUED when it set the action. */

/* A floor sensor of a type on (x, y), at cell (CELL_ANY: any); THING_NONE none. */
static uint16_t find_floor_sensor(int16_t x, int16_t y, int16_t cell, int type)
{
    for (uint16_t t = dungeon_first_thing_at((Location){x, y}); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR;
         t = dungeon_next_thing(t)) {
        if ((thing_type(t) == THING_SENSOR && cell == CELL_ANY) || thing_cell(t) == cell) {
            if (sensor_type(t) == type)
                return t;
        }
    }
    return THING_NONE;
}

/* True when a group carries an item of index item at cell (CELL_ANY: any; item PLAN_ARG_ANY_ITEM: any item). */
static bool has_possession(uint16_t group, int item, int16_t cell)
{
    if (group == THING_NONE)
        return false;
    for (uint16_t t = thing_word(group, GROUP_POSSESSIONS); t != THING_ENDOFLIST && t != THING_NONE;
         t = dungeon_next_thing(t))
        if ((cell == CELL_ANY || thing_cell(t) == cell) &&
            ((int16_t)item == PLAN_ARG_ANY_ITEM || item_get_index(t) == (uint16_t)item))
            return true;
    return false;
}

/* Plan command: the creature steps forward; done when it cannot. */
static int cmd_move_forward(void)
{
    return ai_try_move(executed_move(MOVE_KIND_PLAIN), ai_group_square(), AI_SQUARE_IN_DIR, ai_facing()).passed
               ? AI_RESULT_ISSUED
               : AI_RESULT_DONE;
}

/* Plan command: the creature steps to a random side, or turns to it when both sides are blocked. */
static void cmd_turn_sideways(void)
{
    int turn = rand_bit() ? 1 : -1;
    int f = ai_facing();
    int first = dir_turn(f, turn);
    if (ai_try_move(executed_move(MOVE_KIND_PLAIN), ai_group_square(), AI_SQUARE_IN_DIR, first).passed)
        return;
    if (ai_try_move(executed_move(MOVE_KIND_PLAIN), ai_group_square(), AI_SQUARE_IN_DIR, dir_turn(f, -turn)).passed)
        return;
    ai_turn_toward(first);
}

/* Plan command: the creature goes to take an item of the filter, unless it already carries one. */
static int cmd_seek_item(void)
{
    int16_t filter = g_ai.arg1 == -1 ? (int16_t)g_ai.cache.item : g_ai.arg1;
    if (g_ai.arg2 &&
        ai_find_matching_item(filter, thing_word(g_ai.group_thing, GROUP_POSSESSIONS), CELL_ANY) != THING_ENDOFLIST)
        return AI_RESULT_DONE;
    return take_or_give_item(ITEM_MODE_TAKE, dest_square(), -1, filter).result;
}

/* Plan command: the creature places the item that a floor sensor wants on the sensor square. */
static int cmd_place_on_sensor(void)
{
    uint16_t saved = g_ai.item_flags;
    if (!(saved & ITEM_SKILL_PLACE))
        return AI_RESULT_FAIL;
    if (g_ai.arg2 == 1 && ai_find_matching_item(FILTER_PLACEABLE, thing_word(g_ai.group_thing, GROUP_POSSESSIONS),
                                                CELL_ANY) != THING_ENDOFLIST)
        return AI_RESULT_DONE;
    int sx = g_ai.arg1 ? g_ai.event.data[EVENT_X] : dest_x();
    int sy = g_ai.arg1 ? g_ai.event.data[EVENT_Y] : dest_y();
    uint16_t sensor = find_floor_sensor(sx, sy, CELL_ANY, SENSOR_SHOP);
    if (sensor == THING_NONE)
        return AI_RESULT_FAIL;
    uint16_t item = sensor_data(sensor);
    if (g_ai.arg2 == PLAN_ARG_FRONT_GROUP) {
        int f = ai_facing();
        uint16_t g =
            creature_get_group_at(dungeon_step((Location){g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y]}, f));
        if (g == THING_NONE)
            return AI_RESULT_FAIL;
        return has_possession(g, item, dir_opposite(f)) ? AI_RESULT_DONE : AI_RESULT_FAIL;
    }
    bool found = false;
    uint16_t t = dungeon_square_first_object(g_current_map.map, sx, sy);
    while (t != THING_ENDOFLIST && thing_type(t) < THING_WEAPON)
        t = dungeon_next_thing(t);
    if (t != THING_ENDOFLIST) {
        if (item_get_index(t) == item) {
            found = true;
        } else {
            uint16_t first = t;
            while (t != THING_ENDOFLIST && thing_type(t) <= THING_MISC && item_get_index(t) != item)
                t = dungeon_next_thing(t);
            if (t != THING_ENDOFLIST) {
                found = true;
                while (first != THING_ENDOFLIST && thing_type(first) <= THING_MISC && item_get_index(first) != item) {
                    uint16_t next = dungeon_next_thing(first);
                    dungeon_unlink_thing_from_square(first, g_current_map.map, sx, sy);
                    dungeon_link_thing_to_square(first, g_current_map.map, sx, sy);
                    first = next;
                }
            }
        }
    }
    if (!found) {
        int16_t c = g_ai.active->counters[0];
        g_ai.active->counters[0] = (int16_t)(c - 1);
        if (c <= 0) {
            g_ai.active->counters[0] = PLACE_WAIT_CHECKS;
            g_ai.active->action = ACTION_PLACE_WAIT;
        } else {
            g_ai.active->action = ACTION_CHECK_AHEAD;
        }
        return AI_RESULT_FAIL;
    }
    g_ai.item_flags &= ITEM_SKILL_PLACE;
    AiCommand placed = take_or_give_item(ITEM_MODE_TAKE, ai_location(sx, sy), -1, FILTER_PLACEABLE);
    g_ai.item_flags = saved;
    return placed.result;
}

int ai_cmd_front_group_carries(void)
{
    if (g_ai.arg1 == -1)
        return AI_RESULT_FAIL;
    int16_t cell = (uint8_t)g_ai.arg2;
    if (cell != CELL_ANY)
        cell = dir_turn(cell, ai_facing() + TURN_BACK);
    int f = ai_facing();
    uint16_t g = creature_get_group_at(dungeon_step((Location){g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y]}, f));
    if (g == THING_NONE)
        return AI_RESULT_FAIL;
    return ai_find_matching_item(g_ai.arg1, thing_word(g, GROUP_POSSESSIONS), cell) != THING_ENDOFLIST ? AI_RESULT_DONE
                                                                                                       : AI_RESULT_FAIL;
}

int ai_cmd_seek_with_carried(void)
{
    uint16_t saved = g_ai.item_flags;
    if (thing_word(g_ai.group_thing, GROUP_POSSESSIONS) == THING_ENDOFLIST || !(saved & ITEM_SKILL_TAKE_CARRIED))
        return AI_RESULT_FAIL;
    int16_t filter = g_ai.arg1 == -1 ? FILTER_ANY_ITEM : g_ai.arg1;
    if (ai_find_matching_item(filter, thing_word(g_ai.group_thing, GROUP_POSSESSIONS), CELL_ANY) == THING_ENDOFLIST)
        return AI_RESULT_FAIL;
    g_ai.item_flags &= ITEM_SKILL_TAKE_CARRIED;
    AiCommand given = take_or_give_item(ITEM_MODE_GIVE, AI_SQUARE_IN_DIR, ai_facing(), filter);
    g_ai.item_flags = saved;
    return given.result;
}

/* Plan command: done when a creature or the party stands two squares ahead; else the creature looks. */
static int cmd_check_party_ahead(void)
{
    g_ai.active->position = LOCATION_NONE;
    int f = ai_facing();
    uint16_t x = (uint16_t)(g_dir_dx[f] * 2 + g_ai.event.data[EVENT_X]);
    uint16_t y = (uint16_t)(g_dir_dy[f] * 2 + g_ai.event.data[EVENT_Y]);
    uint16_t g = creature_get_group_at((Location){x, y});
    if (g != THING_NONE && !(creature_group_info(g)->flags & CREATURE_STATIC))
        return AI_RESULT_DONE;
    if (g_current_map.map == g_ai_party.map && (int16_t)x == g_ai_party.x && (int16_t)y == g_ai_party.y)
        return AI_RESULT_DONE;
    g_ai.active->action = ACTION_CHECK_AHEAD;
    return AI_RESULT_ISSUED;
}

/* Plan command: the creature turns the group in front. */
static void cmd_turn_front_group(void)
{
    int f = ai_facing();
    set_destination(g_ai.event.data[EVENT_X] + g_dir_dx[f], g_ai.event.data[EVENT_Y] + g_dir_dy[f]);
    g_ai.active->facing = (uint8_t)g_ai.arg1;
    g_ai.active->action = (uint8_t)((g_ai.arg1 == 1) + ACTION_TURN_GROUP);
}

/* Plan command: the creature takes items of the filter from the group in front. */
static int cmd_take_from_front(void)
{
    int16_t filter = g_ai.arg1 == -1 ? FILTER_ANY_ITEM : g_ai.arg1;
    int f = ai_facing();
    set_destination(g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y]);
    g_ai.active->cell = (uint8_t)(dir_opposite(f));
    g_ai.active->sub = (int8_t)filter;
    set_destination(dest_x() + g_dir_dx[f], dest_y() + g_dir_dy[f]);
    uint16_t g = creature_get_group_at((Location){dest_x(), dest_y()});
    if (g == THING_NONE)
        return AI_RESULT_FAIL;
    if (ai_find_matching_item(filter, thing_word(g, GROUP_POSSESSIONS), g_ai.active->cell) == THING_ENDOFLIST)
        return AI_RESULT_DONE;
    g_ai.active->action = ACTION_TAKE_FROM_GROUP;
    return AI_RESULT_ISSUED;
}

/* Plan command: the creature drops what it does not want, then looks for an item of the filter. */
static int cmd_drop_then_seek(void)
{
    uint16_t *list = group_possessions(g_ai.group_thing);
    if (g_ai.arg2 != PLAN_ARG_ANY_ITEM) {
        int16_t filter = g_ai.arg2 == -1 ? (int16_t)g_ai.cache.mask : g_ai.arg2;
        if ((g_ai.arg2 != -1 || g_ai.cache.mask != CANDIDATE_MASK_ALL) && *list != THING_ENDOFLIST)
            ai_remove_matching_items(list, filter, CELL_ANY);
    }
    if (*list == THING_ENDOFLIST)
        return AI_RESULT_FAIL;
    int16_t filter = g_ai.arg1;
    if (filter == -1) {
        if (g_ai.cache.item == TARGET_NO_ITEM)
            return AI_RESULT_DONE;
        filter = (int16_t)g_ai.cache.item;
    }
    if (ai_find_matching_item(filter, *list, CELL_ANY) == THING_ENDOFLIST)
        return AI_RESULT_FAIL;
    return take_or_give_item(ITEM_MODE_GIVE, ai_group_square(), -1, filter).result;
}

/* Plan command: sets, clears, or tests a mood bit; a change makes the creature scan again. */
static int cmd_set_mood_flag(void)
{
    uint16_t op = (uint16_t)g_ai.arg2;
    uint16_t old = group_mood(g_ai.group_thing);
    if (op <= MOOD_OP_TEST || (op >= MOOD_OP_QUIET && op <= MOOD_OP_QUIET_LAST)) {
        uint16_t bit = (uint16_t)(1 << (g_ai.arg1 & BIT_INDEX_MASK));
        bool was_set = (old & bit) == bit;
        if ((op & MOOD_OP_MASK) == MOOD_OP_CLEAR)
            group_set_mood(g_ai.group_thing, old & ~bit);
        else if ((op & MOOD_OP_MASK) == MOOD_OP_SET)
            group_set_mood(g_ai.group_thing, old | bit);
        if (!(op & MOOD_OP_QUIET) && old != group_mood(g_ai.group_thing))
            g_ai.active->action = ACTION_RESCAN;
        return was_set ? AI_RESULT_DONE : AI_RESULT_FAIL;
    }
    if (op > MOOD_OP_CLEAR_BY_TARGETS || !g_ai.cache.targets)
        return AI_RESULT_FAIL;
    int kind = op == MOOD_OP_SET_BY_TARGETS ? TARGET_SCAN_MOOD_SET : TARGET_SCAN_MOOD_CLEAR;
    for (uint32_t list = g_ai.cache.targets;; list += AI_TARGET_SIZE) {
        AiTarget t = ai_read_target(list);
        if (t.scan == kind) {
            uint16_t mood = group_mood(g_ai.group_thing);
            uint16_t bit = (uint16_t)(1 << (t.mask & BIT_INDEX_MASK));
            if (!t.item)
                group_set_mood(g_ai.group_thing, mood | bit);
            else if (t.item == 1)
                group_set_mood(g_ai.group_thing, mood & ~bit);
        }
        if (!t.more)
            break;
    }
    bool changed = old != group_mood(g_ai.group_thing);
    if (changed)
        g_ai.active->action = ACTION_RESCAN;
    return changed ? AI_RESULT_DONE : AI_RESULT_FAIL;
}

/* Plan command: the creature walks the next step of its path, or turns toward its destination. */
static int cmd_go_to_target(void)
{
    ActiveGroup *a = g_ai.active;
    int16_t n = (int16_t)creature_turn_chance(g_ai.info);
    bool hurry = false;
    if (n) {
        if (group_mood_flag(g_ai.group_thing, MOOD_LAZY))
            n = (int16_t)(n >> 2);
        hurry = (int)(rand_next() & 0xf) < n;
    }
    if (!ai_next_path_step()) {
        int16_t x = g_ai.event.data[EVENT_X];
        int16_t y = g_ai.event.data[EVENT_Y];
        if (x == dest_x() && y == dest_y() && location_map(a->destination) == g_current_map.map)
            return AI_RESULT_DONE;
        int dir = dungeon_direction_to(x, y, dest_x(), dest_y());
        if (ai_facing() == dir)
            return AI_RESULT_DONE;
        if (hurry && rand_bit()) {
            a->action = ACTION_NONE;
            return AI_RESULT_ISSUED;
        }
        return ai_turn_toward(dir).result;
    }
    if (hurry) {
        a->action = ACTION_NONE;
        return AI_RESULT_ISSUED;
    }
    AiMoveTest move = executed_move(MOVE_KIND_PLAIN);
    move.attack_groups = group_mood_flag(g_ai.group_thing, MOOD_STAYS);
    return ai_try_move(move, ai_group_square(), AI_SQUARE_IN_DIR, a->direction).result;
}

/* Plan command: the creature attacks its target with the attacks that its plan allows. */
static int cmd_follow_path(void)
{
    AiItemCounts *c = ai_get_item_counts(g_ai.cache.targets, g_ai.cache.scan_kind);
    int mode = g_ai.cache.kind == TARGET_MELEE_LINE   ? ATTACK_MODE_GROUP
               : g_ai.cache.kind == TARGET_SHOOT_LINE ? ATTACK_MODE_ANY
                                                      : ATTACK_MODE_PARTY;
    g_ai.active->sub = (int8_t)g_ai.cache.item;
    uint16_t saved = g_ai.attack_flags;
    if (saved & ATTACK_SKILL_ITEMS && c->total <= 0)
        g_ai.attack_flags &= ~ATTACK_SKILL_ITEMS;
    g_ai.attack_flags &= g_ai.cache.mask;
    AiCommand attack = ai_try_attack(executed_test(mode), ai_group_square(), dest_square(), -1);
    g_ai.attack_flags = saved;
    return attack.result;
}

/* Plan command: the creature searches the area for the item targets of its plan: groups that it can damage. */
static int cmd_seek_target_items(void)
{
    g_ai.candidate_count = 0;
    if (!g_ai.cache.targets)
        return AI_RESULT_FAIL;
    TargetScan scan = {-1, TARGET_SCAN_SEEK_ITEMS, 0};
    for (uint32_t list = g_ai.cache.targets;; list += AI_TARGET_SIZE) {
        AiTarget t = ai_read_target(list);
        if (t.scan == TARGET_SCAN_SEEK_ITEMS && t.item != TARGET_NO_ITEM) {
            AiTarget copy = t;
            copy.kind = TARGET_DAMAGE_GROUP;
            ai_add_candidate(&scan, list, &copy, 0, FOUND_NONE);
        }
        if (!t.more)
            break;
    }
    if (!g_ai.candidate_count)
        return AI_RESULT_FAIL;
    g_ai.chosen = (int8_t)ai_search_area(ai_group_square(), MOVE_KIND_OPEN, (int8_t)g_ai.candidate_count,
                                         g_ai.candidates, g_ai.path);
    return g_ai.chosen != -1 ? AI_RESULT_DONE : AI_RESULT_FAIL;
}

/* Plan command: the creature turns toward the party. */
static int cmd_turn_to_party(void)
{
    g_ai.command_result = AI_RESULT_FAIL;
    if (g_current_map.map != g_ai_party.map)
        return AI_RESULT_FAIL;
    int16_t x = g_ai.event.data[EVENT_X];
    int16_t y = g_ai.event.data[EVENT_Y];
    int16_t dir = dungeon_direction_to(x, y, g_ai_party.x, g_ai_party.y);
    if (square_type(dungeon_square_at(dungeon_step((Location){x, y}, dir))))
        return ai_turn_toward(dir).result;
    return AI_RESULT_FAIL;
}

/* Plan command: the creature shoots a random missile in a random direction. */
static void cmd_shoot_random(void)
{
    ActiveGroup *a = g_ai.active;
    int b = rand_bit();
    int r = rand_2bits();
    a->sub = (int8_t)SHOT_KIND_RANDOM;
    a->action = (uint8_t)((b != 0) + ACTION_SHOOT);
    a->direction = (uint8_t)r;
    a->cell = (uint8_t)dir_turn(r, b);
    a->mode = ATTACK_MODE_PARTY;
}

/* Plan command: the creature moves forward or to the side, through the party and other groups. */
static int cmd_move_relative(void)
{
    uint16_t saved = g_ai.move_flags;
    g_ai.move_flags |= MOVE_PARTY | MOVE_GROUPS;
    AiCommand move = ai_try_move(executed_move(g_ai.arg1 ? MOVE_KIND_STEP_BACK : MOVE_KIND_PLAIN), ai_group_square(),
                                 AI_SQUARE_IN_DIR, dir_turn(ai_facing(), g_ai.arg1));
    g_ai.move_flags = saved;
    return move.result;
}

/* Plan command: the creature stands in one of three ways, by its wander mood. */
static int cmd_stand_by_mood(void)
{
    bool roams = group_mood_flag(g_ai.group_thing, MOOD_WANDERS);
    if (!roams && !g_ai.arg1)
        return AI_RESULT_FAIL;
    int16_t v = g_ai.arg1 < 0 ? 0 : g_ai.arg1 > STAND_VARIANT_LAST ? STAND_VARIANT_LAST : g_ai.arg1;
    g_ai.active->action = (uint8_t)(v + ACTION_STAND);
    return roams && g_ai.arg1 == 1 ? AI_RESULT_ISSUED : AI_RESULT_DONE;
}

/* The creature eats or uses the item it carries first, or looks for food. */
static int cmd_consume(void)
{
    uint16_t t = thing_word(g_ai.group_thing, GROUP_POSSESSIONS);
    if (t == THING_ENDOFLIST)
        return AI_RESULT_FAIL;
    bool used; /* a creature does not consume an item that the dungeon keeps */
    switch (thing_type(t)) {
    case THING_WEAPON:
    case THING_ARMOUR:
    case THING_POTION:
        used = !item_is_kept(t);
        break;
    case THING_MISC:
        used = !item_is_kept(t) || item_attr(t, ATTR_FOOD) != 0;
        break;
    default:
        used = false;
    }
    if (used) {
        dungeon_unlink_thing_from_list(t, group_possessions(g_ai.group_thing));
        thing_set_word(t, THING_NEXT, THING_NONE);
        g_ai.active->action = ACTION_NONE;
        return AI_RESULT_DONE;
    }
    if (!(rand_next() & ROLL_SEEK_FOOD)) {
        take_or_give_item(ITEM_MODE_GIVE, ai_group_square(), -1, FILTER_FOOD);
        g_ai.active->counters[0] = (int16_t)location_pack(g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y], g_ai.map);
    } else if (!(rand_next() & 0xf)) {
        g_ai.active->counters[0] = -1;
    }
    return AI_RESULT_FAIL;
}

/* Plan command: the creature transforms at a transform marker on its square, else it vanishes. */
static int cmd_check_transform(void)
{
    for (uint16_t t = dungeon_first_thing_at((Location){g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y]});
         t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_TEXT)
            continue;
        if (textrec_kind(t) != TEXT_KIND_ORNAMENT || textrec_subtype(t) != TEXT_TRANSFORM)
            continue;
        g_ai.active->sub = (int8_t)textrec_value(t);
        g_ai.active->action = ACTION_TRANSFORM;
        return AI_RESULT_DONE;
    }
    creature_set_next_action(g_ai.group_thing, (Location){g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y]},
                             ACTION_VANISH, true);
    g_ai.active->action = ACTION_RESCAN;
    return AI_RESULT_FAIL;
}

int ai_exec_plan_command(int command)
{
    ActiveGroup *a = g_ai.active;
    switch ((uint8_t)command) {
    case PLAN_CMD_MOVE_FORWARD:
        return cmd_move_forward();
    case PLAN_CMD_TURN_SIDEWAYS:
        cmd_turn_sideways();
        return AI_RESULT_DONE;
    case PLAN_CMD_VANISH:
        a->action = ACTION_VANISH;
        return AI_RESULT_DONE;
    case PLAN_CMD_SEEK_CACHED_ITEM:
        g_ai.arg1 = -1;
        g_ai.arg2 = 0;
        return cmd_seek_item();
    case PLAN_CMD_SEEK_ITEM:
        return cmd_seek_item();
    case PLAN_CMD_STOP:
        a->action = ACTION_NONE;
        return AI_RESULT_DONE;
    case PLAN_CMD_PLACE_ON_SENSOR:
        return cmd_place_on_sensor();
    case PLAN_CMD_FRONT_GROUP_CARRIES:
        return ai_cmd_front_group_carries();
    case PLAN_CMD_SEEK_WITH_CARRIED:
        return ai_cmd_seek_with_carried();
    case PLAN_CMD_CHECK_PARTY_AHEAD:
        return cmd_check_party_ahead();
    case PLAN_CMD_SHOP_WAIT:
        return shop_cmd_wait_for_offer();
    case PLAN_CMD_SHOP_EVALUATE:
        return shop_cmd_evaluate_offer();
    case PLAN_CMD_SHOP_PAYMENT:
        return shop_cmd_count_payment();
    case PLAN_CMD_TURN_FRONT_GROUP:
        cmd_turn_front_group();
        return AI_RESULT_DONE;
    case PLAN_CMD_TAKE_FROM_FRONT_GROUP:
        return cmd_take_from_front();
    case PLAN_CMD_DROP_THEN_SEEK:
        return cmd_drop_then_seek();
    case PLAN_CMD_SET_MOOD_FLAG:
        return cmd_set_mood_flag();
    case PLAN_CMD_GO_TO_TARGET:
        return cmd_go_to_target();
    case PLAN_CMD_FOLLOW_PATH:
        return cmd_follow_path();
    case PLAN_CMD_SEEK_TARGET_ITEMS:
        return cmd_seek_target_items();
    case PLAN_CMD_TURN_TO_PARTY:
        return cmd_turn_to_party();
    case PLAN_CMD_SHOOT_RANDOM:
        cmd_shoot_random();
        return AI_RESULT_DONE;
    case PLAN_CMD_MOVE_RELATIVE:
        return cmd_move_relative();
    case PLAN_CMD_USE_ACTUATOR:
        return ai_try_use_wall_actuator(executed_test(g_ai.cache.mask), ai_group_square(), dest_square(), -1,
                                        (int16_t)g_ai.cache.item)
            .result;
    case PLAN_CMD_SHOP_PAY:
        return shop_cmd_pay_change();
    case PLAN_CMD_SET_ACTION_BY_FLAG:
        return cmd_stand_by_mood();
    case PLAN_CMD_CONSUME:
        return cmd_consume();
    case PLAN_CMD_CHECK_TRANSFORM:
        return cmd_check_transform();
    case PLAN_CMD_SET_SPECIAL:
        a->mode = (uint8_t)g_ai.cache.item;
        a->sub = (int8_t)g_ai.cache.mask;
        a->action = (uint8_t)(g_ai.arg1 + ACTION_SPECIAL);
        return AI_RESULT_DONE;
    case PLAN_CMD_SET_ACTION_A:
    case PLAN_CMD_SET_ACTION_B:
    case PLAN_CMD_SET_ACTION_C:
        a->action = (uint8_t)((int8_t)g_ai.arg1 == -1 ? (uint8_t)g_ai.cache.item : (uint8_t)g_ai.arg1);
        return AI_RESULT_DONE;
    case PLAN_CMD_DOOR_ACTION:
        return ai_try_door_action(executed_test(g_ai.cache.mask), ai_group_square(), dest_square(), -1).result;
    case PLAN_CMD_RANDOM_CHANCE:
        return g_ai.arg1 > (int)rand_below(PERCENT) ? AI_RESULT_DONE : AI_RESULT_FAIL;
    case PLAN_CMD_CARRIES_ITEM:
        if (ai_find_matching_item(g_ai.arg1, thing_word(g_ai.group_thing, GROUP_POSSESSIONS), CELL_ANY) !=
                THING_ENDOFLIST ||
            ai_find_matching_item(g_ai.arg2, thing_word(g_ai.group_thing, GROUP_POSSESSIONS), CELL_ANY) !=
                THING_ENDOFLIST)
            return AI_RESULT_DONE;
        return AI_RESULT_FAIL;
    default:
        return AI_RESULT_DONE;
    }
}

/* Actions: what a creature does at the acting step of its animation. Each returns true when the action failed. */

/* Action: the creature turns to its new facing. */
static void act_turn(void)
{
    int f = g_ai.active->facing;
    if (dir_relative(ai_facing(), f) == TURN_BACK)
        f = dir_turn(f, (rand_next() & 2) + 1);
    group_set_dir(g_ai.group_thing, f);
}

/* The creature arrives on its new square dest: the AI position follows the move. */
static void act_arrive(MoveDest dest, bool closes_pits)
{
    g_ai.event.data[EVENT_X] = (uint8_t)dest.x;
    g_ai.event.data[EVENT_Y] = (uint8_t)dest.y;
    g_ai.map = (uint8_t)dest.map;
    ai_set_map(g_ai.map);
    if (closes_pits)
        move_set_pit_teleporter_state((Location){g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y]}, PIT_STATE_OPEN);
    if (g_ai.cache.path_left)
        g_ai.cache.path_left--;
}

/* The champions that the creature on (x, y) hits on the party square (dx, dy); true when it hit one. */
static bool melee_attack_party(int16_t x, int16_t y, int dx, int dy)
{
    ActiveGroup *a = g_ai.active;
    const CreatureInfo *info = g_ai.info;
    g_game.last_combat_time = g_game.game_time;
    bool hit = false;
    bool random = info->flags & CREATURE_ATTACKS_RANDOM;
    int8_t list[CHAMPION_MAX];
    int16_t n = 0;
    for (int c = 0; c < g_game.champion_count; c++)
        if (champion(c)->health)
            list[n++] = (int8_t)c;
    if (!n)
        return false;
    int16_t count;
    if (info->flags & CREATURE_ATTACKS_ALL)
        count = random ? (int16_t)(rand_below((uint16_t)n) + 1) : (int16_t)g_game.champion_count;
    else
        count = info->traits & CREATURE_TRAIT_TWO_CHAMPIONS ? 2 : 1;
    if (a->cell == CELL_NEAREST) {
        int c = move_nearest_champion_at((Location){x, y}, CELL_ANY);
        if (c != -1)
            a->cell = champion(c)->cell;
    }
    int16_t cell = a->cell;
    if (count > (int16_t)g_game.champion_count)
        count = (int16_t)g_game.champion_count;
    while (--count != -1) {
        int c;
        if (random) {
            int16_t i = (int16_t)rand_below((uint16_t)n);
            if (list[i] < 0)
                for (int16_t k = 0; k < n; k++) {
                    if (++i >= n)
                        i = 0;
                    if (list[i] >= 0)
                        break;
                }
            c = list[i];
            list[i] = -1;
        } else {
            c = champ_get_index_in_cell(cell);
            int d = dungeon_direction_to(dx, dy, x, y);
            cell = dir_turn(cell, (d + cell) & 1 ? -1 : 1);
            if (c == -1)
                c = move_nearest_champion_at((Location){x, y}, CELL_ANY);
        }
        if (c == -1) /* fix: a random pick finds no champion left; the original attacks champion -1 */
            continue;
        hit = true;
        int16_t damage = (int16_t)(projectile_attack_champion(g_ai.group_thing, c) + 1);
        Champion *ch = champion(c);
        if (damage > ch->hit_damage) {
            ch->hit_damage = (uint8_t)damage;
            ch->hit_dir = (uint8_t)dir_opposite(dungeon_direction_to(x, y, g_ai_party.x, g_ai_party.y));
        }
    }
    return hit;
}

/* The power roll of an attack of the creature: a quarter of its attack plus one, raised twice by a random part. */
static uint16_t roll_attack_power(void)
{
    uint16_t p = (uint16_t)((g_ai.info->attack >> 2) + 1);
    p = (uint16_t)(p + rand_below(p));
    return (uint16_t)(p + rand_below(p));
}

/* The creature hits the party, a door, or another creature in front of it. */
static bool act_melee_attack(void)
{
    ActiveGroup *a = g_ai.active;
    const CreatureInfo *info = g_ai.info;
    int16_t dx = ai_state.dest_x;
    int16_t dy = ai_state.dest_y;
    int16_t x = g_ai.event.data[EVENT_X];
    int16_t y = g_ai.event.data[EVENT_Y];
    int16_t dir = ai_facing();
    if (a->mode == MOVE_KIND_STEP_BACK)
        dir = dir_opposite(dir);
    bool at_self = x == dx && y == dy;
    if (!at_self) {
        if (ai_manhattan((Location){x, y}, (Location){dx, dy}) > 1)
            return true;
        /* a creature that always shows its front attacks in any direction */
        if (dungeon_direction_to(x, y, dx, dy) != dir && !(info->flags & CREATURE_FACES_PARTY))
            return true;
    }
    if (g_current_map.map == g_ai_party.map && dx == g_ai_party.x && dy == g_ai_party.y) {
        if (!melee_attack_party(x, y, dx, dy))
            return true;
    } else {
        if (square_type(dungeon_square_at((Location){dx, dy})) == SQUARE_DOOR) {
            int strength = rand_below((uint16_t)(info->attack + (info->attack >> 1)));
            if (sensor_door_try_destroy((Location){dx, dy}, (int16_t)strength, false, 0))
                return true;
        }
        if (at_self)
            return true;
        if (!creature_attack_group_at((Location){dx, dy}, NULL))
            return true;
    }
    if (a->action == ACTION_MELEE_2) {
        bool push;
        if (creature_group_info(g_ai.group_thing)->push_resistance > PUSH_RESISTANCE_HEAVY)
            push = (rand_next() & ROLL_PUSH_HEAVY) || rand_2bits();
        else
            push = rand_2bits() != 0;
        if (push)
            creature_push_group((Location){dx, dy}, dir, PUSH_BY_CREATURE);
    }
    return false;
}

/* Action: the creature moves to its destination square, after an attack if it attacks first. */
static bool act_move(void)
{
    ActiveGroup *a = g_ai.active;
    if (g_ai.info->flags & CREATURE_ATTACKS_FIRST) {
        a->action = ACTION_MELEE_2;
        a->cell = CELL_NEAREST;
        act_melee_attack();
    }
    if (!ai_try_move(executed_move(a->mode), ai_group_square(), ai_location(ai_state.dest_x, ai_state.dest_y),
                     a->direction)
             .passed ||
        !(ai_action_flag((int8_t)a->action) & AI_ACTION_FLAG_PATH))
        return true;
    bool closes = g_ai.info->traits & CREATURE_TRAIT_CLOSES_PITS;
    if (closes)
        move_set_pit_teleporter_state((Location){g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y]}, PIT_STATE_CLOSED);
    MoveOutcome moved =
        move_thing_dest(g_ai.group_thing, g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y], dest_x(), dest_y());
    if (moved.result == MOVE_DONE)
        act_arrive(moved.dest, closes);
    else
        g_ai.removed = true;
    return false;
}

/* Action: the creature falls or jumps two squares, one per phase; then the result is a stale register. */
static int act_move_two_steps(void)
{
    ActiveGroup *a = g_ai.active;
    int r = 0;
    if (a->phase == 0) {
        a->phase = 1;
        uint8_t left = g_ai.cache.path_left;
        r = act_move();
        g_ai.cache.path_left = left;
        set_destination(dest_x() + g_dir_dx[a->direction], dest_y() + g_dir_dy[a->direction]);
    } else if (a->phase == FALL_PHASE_FIRST_STEP) {
        r = act_move();
        a->phase = r ? FALL_PHASE_SECOND_STEP : FALL_PHASE_DONE;
    } else if (a->phase == FALL_PHASE_SECOND_STEP) {
        a->phase = FALL_PHASE_DONE;
        MoveOutcome moved = move_thing_dest(g_ai.group_thing, g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y],
                                            g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y]);
        MoveDest dest = moved.dest;
        if (moved.result != MOVE_DONE) {
            g_ai.removed = true;
            /* as in the original: a group that waits or leaves takes the square of the last move inside its move */
            dest = (MoveDest){g_party.dest_x, g_party.dest_y, g_party.dest_map, g_party.dest_cell, g_party.dest_dir};
        }
        g_ai.event.data[EVENT_X] = (uint8_t)dest.x;
        g_ai.event.data[EVENT_Y] = (uint8_t)dest.y;
        g_ai.map = (uint8_t)dest.map;
        ai_set_map(g_ai.map);
    } else {
        r = STALE_REGISTER; /* the original returns a stale register (the low half of a pointer): nonzero, low byte 0 here */
    }
    return r;
}

/* Action: the creature takes stairs, a pit, or a level link to the next map. */
static bool act_move_stairs(void)
{
    ActiveGroup *a = g_ai.active;
    if (!ai_try_move(executed_move(a->mode), ai_group_square(), ai_location(ai_state.dest_x, ai_state.dest_y),
                     a->direction)
             .passed ||
        !(ai_action_flag((int8_t)a->action) & AI_ACTION_FLAG_PATH))
        return true;
    int dir = ai_facing();
    ai_state.dest_x = (int16_t)dest_x();
    ai_state.dest_y = (int16_t)dest_y();
    if (a->action == ACTION_LINK_UP || a->action == ACTION_LINK_DOWN) {
        int delta = a->action == ACTION_LINK_UP ? -1 : 1;
        LevelLink link = dungeon_find_level_link((Location){g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y]}, delta);
        if (link.sensor != THING_NONE) {
            int exit = sensor_effect(link.sensor);
            /* inverted: a fixed exit */
            dir = sensor_flag(link.sensor, SENSOR_INVERTED) ? exit : dir_turn(exit, link.dir);
        } else {
            int16_t saved = g_current_map.map;
            Position to =
                dungeon_location_on_map((Position){saved, g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y]}, delta);
            dungeon_set_current_map(to.map);
            int16_t d2 = dungeon_find_level_link((Location){to.x, to.y}, -delta).dir;
            dungeon_set_current_map(saved);
            dir = d2 == -1 ? rand_2bits() : d2;
        }
    }
    bool closes = g_ai.info->traits & CREATURE_TRAIT_CLOSES_PITS;
    if (closes)
        move_set_pit_teleporter_state((Location){g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y]}, PIT_STATE_CLOSED);
    move_thing(g_ai.group_thing, g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y], MOVE_NO_SQUARE, 0);
    ai_set_map(location_map(a->destination));
    if (a->action == ACTION_STAIRS_UP || a->action == ACTION_STAIRS_DOWN)
        dir = dungeon_stairs_exit_direction((Location){ai_state.dest_x, ai_state.dest_y});
    group_set_dir(g_ai.group_thing, dir);
    MoveOutcome moved = move_thing_dest(g_ai.group_thing, MOVE_NO_SQUARE, 0, ai_state.dest_x, ai_state.dest_y);
    if (moved.result != MOVE_DONE)
        g_ai.removed = true;
    else
        act_arrive(moved.dest, closes);
    return false;
}

/* Action: the creature turns, then moves; after the move the result is a stale register. */
static int act_turn_then_move(void)
{
    ActiveGroup *a = g_ai.active;
    int r;
    if (a->phase == 0) {
        act_turn();
        r = 0;
    } else if (a->phase == 1) {
        r = act_move();
    } else {
        r = ai_state.dest_y; /* the original returns the register that still holds the destination row */
    }
    a->phase++;
    return r;
}

/* Action: the creature shoots a missile, with a power from its attack. */
static bool act_shoot(void)
{
    ActiveGroup *a = g_ai.active;
    const CreatureInfo *info = g_ai.info;
    if (a->mode <= ATTACK_MODE_PARTY_RANGED)
        g_game.last_combat_time = g_game.game_time;
    /* a creature that always shows its front shoots in any direction */
    if (ai_facing() != (int8_t)a->direction && !(info->flags & CREATURE_FACES_PARTY))
        return true;
    uint16_t p = roll_attack_power();
    uint8_t sub = (uint8_t)a->sub;
    if (sub == (uint8_t)SPELL_POISON_BOLT)
        p = (uint16_t)(p * POISON_BOLT_POWER_SCALE);
    else if (sub == (uint8_t)SPELL_POISON_CLOUD || sub == (uint8_t)SPELL_PUSH || sub == (uint8_t)SPELL_PULL)
        p = (uint16_t)(p << SPELL_SHOT_POWER_SHIFT);
    int16_t power = (int16_t)clamp_int(SHOT_POWER_MIN, (int16_t)p, POWER_MAX);
    int speed;
    if (power <= SHOT_POWER_WEAK)
        speed = 1;
    else if (power <= SHOT_POWER_MEDIUM)
        speed = 2;
    else if (power <= SHOT_POWER_STRONG)
        speed = rand_bit() + SHOT_SPEED_MEDIUM;
    else
        speed = rand_2bits() + SHOT_SPEED_STRONG;
    projectile_create(&(ProjectileLaunch){.payload = (uint16_t)a->sub,
                                          .x = g_ai.event.data[EVENT_X],
                                          .y = g_ai.event.data[EVENT_Y],
                                          .cell = a->cell,
                                          .dir = (int8_t)a->direction,
                                          .energy = (uint8_t)power,
                                          .hit_chance = info->dexterity,
                                          .step = (uint8_t)speed});
    if (info->traits & CREATURE_TRAIT_SHOT_HURTS) {
        int hurt = (power >> 1) + rand_bit();
        if (creature_damage_current_group((uint16_t)hurt)) {
            g_ai.removed = true;
            return true;
        }
    }
    return false;
}

/* Action: the creature steals an item from the hands of a champion. */
static bool act_steal(void)
{
    if (g_current_map.map != g_ai_party.map || ai_state.dest_x != g_ai_party.x || ai_state.dest_y != g_ai_party.y)
        return true;
    int c = champ_get_index_in_cell(g_ai.active->cell);
    if (c == -1)
        return true;
    Champion *ch = champion(c);
    uint32_t r = rand_next();
    bool stolen = false;
    if (!champ_test_luck(champion(c), (uint16_t)(PERCENT - champ_get_dexterity(c))) || r & ROLL_STEAL) {
        uint16_t ready = ch->slots[SLOT_READY_HAND];
        uint16_t held = ch->slots[SLOT_ACTION_HAND];
        int16_t v0 = ready != THING_NONE && ai_thing_matches_filter(ready, g_ai.active->sub)
                         ? (int16_t)item_get_value(ready)
                         : -1;
        int16_t v1 =
            held != THING_NONE && ai_thing_matches_filter(held, g_ai.active->sub) ? (int16_t)item_get_value(held) : -1;
        bool take_held = r & ROLL_STEAL_BY_VALUE ? v1 > v0 : (r & ROLL_STEAL_HELD) != 0;
        uint16_t t = take_held ? held : ready;
        if (t != THING_NONE && ai_thing_matches_filter(t, g_ai.active->sub)) {
            uint16_t item = champ_remove_object_from_slot(c, take_held ? SLOT_ACTION_HAND : SLOT_READY_HAND);
            dungeon_append_thing(item, group_possessions(g_ai.group_thing));
            stolen = true;
            if (g_party_sleeping) {
                g_party_sleeping = false; /* the ninja level without the sleep penalty */
                int roll = rand_next() & 0xf;
                int level = champ_get_skill_level(c, SKILL_NINJA, true);
                g_party_sleeping = true;
                if (level + roll > STEAL_WAKE_THRESHOLD)
                    champ_wake_party();
            }
        }
    }
    return !stolen;
}

/* Action: the creature operates the door on its destination square. */
static bool act_trigger_square(void)
{
    if (ai_try_door_action(executed_test(g_ai.active->mode), ai_group_square(),
                           ai_location(ai_state.dest_x, ai_state.dest_y), ai_facing())
            .passed &&
        g_ai.active->action == ACTION_OPERATE_DOOR) {
        sensor_schedule_square_event(ai_location(ai_state.dest_x, ai_state.dest_y), 0, EFFECT_TOGGLE, g_game.game_time);
        return false;
    }
    return true;
}

/* Action: the creature picks up the items of the filter on its destination square. */
static bool act_pick_up_items(void)
{
    int n = 0;
    for (uint16_t t = dungeon_square_first_object(g_current_map.map, ai_state.dest_x, ai_state.dest_y);
         t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (thing_type(t) == THING_GROUP)
            continue;
        if (g_ai.active->cell != CELL_ANY && thing_cell(t) != g_ai.active->cell)
            continue;
        if (!ai_thing_matches_filter(t, g_ai.active->sub))
            continue;
        move_thing(t, ai_state.dest_x, ai_state.dest_y, MOVE_NO_SQUARE, 0);
        /* as in the original, the next thing is the end of the possessions, so SUB_REPEAT picks up one item */
        dungeon_append_thing(t, group_possessions(g_ai.group_thing));
        n++;
        if (!(g_ai.active->sub & SUB_REPEAT))
            break;
    }
    return n == 0;
}

/* Action: the creature turns to the items, picks them up, and turns back. */
static bool act_turn_pick_up(void)
{
    ActiveGroup *a = g_ai.active;
    bool failed = false;
    if (a->phase == 0) {
        int f = ai_facing();
        act_turn();
        a->facing = (uint8_t)f;
    } else if (a->phase == 1) {
        act_turn();
        failed = act_pick_up_items();
    }
    a->phase++;
    return failed;
}

/* The sound of the first item that a creature drops. */
static void drop_sound(uint16_t t, int n)
{
    if (n == 1)
        sndreq_request_with_fallback((SoundCue){item_gdat_category(t), item_gdat_index(t), SOUND_ENTRY_DROP,
                                                SOUND_PRIORITY_DROP, SOUND_VOLUME_NORMAL},
                                     SOUND_FALLBACK, ai_state.dest_x, ai_state.dest_y, SOUND_STEP_END);
}

/* Action: the creature drops its items of the filter on its destination square. */
static bool act_drop_items(void)
{
    int n = 0;
    do {
        uint16_t t = ai_find_matching_item(g_ai.active->sub, thing_word(g_ai.group_thing, GROUP_POSSESSIONS), CELL_ANY);
        if (t == THING_ENDOFLIST)
            break;
        n++;
        dungeon_unlink_thing_from_list(t, group_possessions(g_ai.group_thing));
        t = thing_with_cell(t, g_ai.active->cell);
        move_thing(t, MOVE_NO_SQUARE, 0, ai_state.dest_x, ai_state.dest_y);
        drop_sound(t, n);
    } while (g_ai.active->sub & SUB_REPEAT);
    return n == 0;
}

/* Action: the creature leaves the dungeon. */
static void act_vanish(void)
{
    creature_delete_group_at((Location){g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y]}, DROP_ALL, true);
    g_ai.removed = true;
}

/* Action: the creature throws a carried item of the filter. */
static bool act_throw_item(void)
{
    uint16_t t = ai_find_matching_item(g_ai.active->sub, thing_word(g_ai.group_thing, GROUP_POSSESSIONS), CELL_ANY);
    if (t == THING_ENDOFLIST)
        return true;
    dungeon_unlink_thing_from_list(t, group_possessions(g_ai.group_thing));
    int16_t power = (int16_t)clamp_int(THROW_POWER_MIN, (int16_t)roll_attack_power(), POWER_MAX);
    projectile_create(&(ProjectileLaunch){.payload = t,
                                          .x = g_ai.event.data[EVENT_X],
                                          .y = g_ai.event.data[EVENT_Y],
                                          .cell = g_ai.active->cell,
                                          .dir = (int8_t)g_ai.active->direction,
                                          .energy = (uint8_t)power,
                                          .hit_chance = g_ai.info->dexterity,
                                          .step = THROW_SPEED});
    return false;
}

/* Action: the creature turns the group on its destination square. */
static bool act_turn_target_group(void)
{
    uint16_t g = creature_get_group_at((Location){ai_state.dest_x, ai_state.dest_y});
    if (g == THING_NONE)
        return true;
    creature_turn_group(g, false, g_ai.active->facing);
    return false;
}

/* Action: the creature gives its items of the filter to the static creature on its destination square. */
static bool act_give_items(void)
{
    int n = 0;
    uint16_t g = creature_get_group_at((Location){ai_state.dest_x, ai_state.dest_y});
    if (g == THING_NONE || !(creature_group_info(g)->flags & CREATURE_STATIC))
        return true;
    do {
        uint16_t t = ai_find_matching_item(g_ai.active->sub, thing_word(g_ai.group_thing, GROUP_POSSESSIONS), CELL_ANY);
        if (t == THING_ENDOFLIST)
            break;
        n++;
        dungeon_unlink_thing_from_list(t, group_possessions(g_ai.group_thing));
        int16_t cell = g_ai.active->cell;
        if (cell == CELL_ANY)
            cell = rand_2bits();
        t = thing_with_cell(t, cell);
        if (item_is_moneybox(t))
            moneybox_set_placed(t, true);
        move_thing(t, MOVE_NO_SQUARE, MOVE_IN_POSSESSIONS, ai_state.dest_x, ai_state.dest_y);
        drop_sound(t, n);
    } while (g_ai.active->sub & SUB_REPEAT);
    return n == 0;
}

/* Action: the creature takes items of the filter from the static creature on its destination square. */
static bool act_take_items(void)
{
    int n = 0;
    uint16_t g = creature_get_group_at((Location){ai_state.dest_x, ai_state.dest_y});
    if (g == THING_NONE || !(creature_group_info(g)->flags & CREATURE_STATIC))
        return true;
    do {
        uint16_t t = ai_find_matching_item(g_ai.active->sub, thing_word(g, GROUP_POSSESSIONS), g_ai.active->cell);
        if (t == THING_ENDOFLIST)
            break;
        n++;
        move_thing(t, ai_state.dest_x, ai_state.dest_y, MOVE_NO_SQUARE, MOVE_IN_POSSESSIONS);
        dungeon_append_thing(t & THING_ID_MASK, group_possessions(g_ai.group_thing));
    } while (g_ai.active->sub & SUB_REPEAT);
    return n == 0;
}

/* Action: the creature uses a wall actuator, with a carried item of the filter. */
static bool act_use_wall_sensor(void)
{
    ActiveGroup *a = g_ai.active;
    int8_t sub = a->sub;
    if (!ai_try_use_wall_actuator(executed_test(a->mode), ai_group_square(),
                                  ai_location(ai_state.dest_x, ai_state.dest_y), ai_facing(), sub)
             .passed)
        return true;
    uint16_t hand = THING_NONE;
    if (a->mode != ACTUATOR_MODE_TAKE && sub != -1) {
        hand = ai_find_matching_item(sub, thing_word(g_ai.group_thing, GROUP_POSSESSIONS), CELL_ANY);
        if (hand == THING_ENDOFLIST)
            return true;
        dungeon_unlink_thing_from_list(hand, group_possessions(g_ai.group_thing));
    }
    sensor_process_wall_click(dest_square(), dir_opposite(a->facing), g_ai.group_thing, hand);
    return false;
}

/* The creature becomes another creature type: its health, animation, and map. */
static void become_creature(int type)
{
    group_set_creature(g_ai.group_thing, (uint8_t)type);
    g_ai.info = creature_info(gdat_current(), type);
    int16_t max = (int16_t)g_ai.info->max_health;
    thing_set_word(g_ai.group_thing, GROUP_HEALTH,
                   (uint16_t)(max + rand_below((uint16_t)((max >> NEW_TYPE_HEALTH_SHIFT) + 1))));
    ai_set_animation(creature_anim_start(type, (int8_t)g_ai.active->action, 0));
    if (!creature_is_native_to_map(g_ai.group_thing, g_ai.map)) {
        creature_delete_group_at((Location){g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y]}, DROP_ALL, true);
        g_ai.removed = true;
    }
}

/* Action: the creature transforms into another creature type, with sparks and clouds. */
static void act_transform(void)
{
    ActiveGroup *a = g_ai.active;
    int16_t x = g_ai.event.data[EVENT_X];
    int16_t y = g_ai.event.data[EVENT_Y];
    int8_t phase = (int8_t)a->phase;
    a->phase++;
    int sound = SOUND_ENTRY_TRANSFORMED;
    if (a->action == ACTION_TRANSFORM) {
        for (int n = rand_2bits() + 1; n; n--) {
            int cell = rand_below(SPARK_CELLS);
            if (cell == SPARK_CELLS - 1)
                cell = EXPLOSION_AT_CENTER;
            /* the sparks: the explosion of the harm non-material spell */
            explosion_create(SPELL_HARM_NON_MATERIAL, rand_below(SPARK_POWER_RANGE) + SPARK_POWER_MIN, x, y, cell);
        }
        if (phase >= 1)
            explosion_create(EXPLOSION_DEATH_CLOUD, (uint16_t)(phase * TRANSFORM_POWER_STEP), x, y,
                             EXPLOSION_AT_CENTER);
        if (phase < TRANSFORM_PHASES) {
            sound = SOUND_ENTRY_TRANSFORM;
        } else {
            a->action = ACTION_MORPH;
            a->phase = 0;
            group_set_mood(g_ai.group_thing, 0);
            become_creature((uint8_t)a->sub);
        }
    } else {
        explosion_create(EXPLOSION_DEATH_CLOUD, POWER_MAX, x, y, EXPLOSION_AT_CENTER);
    }
    sndreq_request((SoundCue){CAT_DUNGEON, 0, sound, SOUND_PRIORITY_TRANSFORM, SOUND_VOLUME_LOUD}, x, y,
                   SOUND_STEP_END);
}

/* Action: the creature explodes, spawns a creature, or becomes another creature type. */
static bool act_explode_or_spawn(void)
{
    ActiveGroup *a = g_ai.active;
    if (a->mode == MODE_EXPLODE) {
        int16_t power = (int16_t)clamp_int(THROW_POWER_MIN, (int16_t)roll_attack_power(), POWER_MAX);
        explosion_create(SPELL_FIREBALL, (uint16_t)power, g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y],
                         EXPLOSION_AT_CENTER);
        if (a->sub)
            act_vanish();
    } else if (a->mode == MODE_SPAWN) {
        int dir = rand_2bits();
        CreatureSummon summon = {
            .creature_type = a->sub,
            .count = GROUP_COUNT_FULL_HEALTH,
            .dir = dir,
            .at = {g_ai.map, g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y]},
            .link = THING_NONE,
            .search_dir = (int8_t)ai_facing()
        };
        if (creature_summon_group(summon).group == THING_NONE)
            return true;
    } else if (a->mode == MODE_BECOME) {
        a->action = ACTION_IDLE;
        a->phase = 0;
        become_creature((uint8_t)a->sub);
    }
    return false;
}

/* The act of an action code at the acting step; the result is 0 when it was done. */
static int run_act(uint8_t action)
{
    int r = 0;
    if (action == ACTION_MOVE || action == ACTION_MOVE_NEAR || action == ACTION_TURN_BACK)
        r = act_move();
    else if (action == ACTION_STEP_RIGHT || action == ACTION_STEP_LEFT)
        r = act_turn_then_move();
    else if (action == ACTION_FALL)
        r = act_move_two_steps();
    else if (action == ACTION_TURN_LEFT || action == ACTION_TURN_RIGHT)
        act_turn();
    else if (action == ACTION_MELEE || action == ACTION_MELEE_2)
        r = act_melee_attack();
    else if (action == ACTION_STEAL)
        r = act_steal();
    else if (action == ACTION_OPERATE_DOOR)
        r = act_trigger_square();
    else if (action == ACTION_TAKE_LEFT || action == ACTION_TAKE_RIGHT)
        r = act_turn_pick_up();
    else if (action == ACTION_THROW || action == ACTION_THROW_2)
        r = act_throw_item();
    else if (action == ACTION_VANISH)
        act_vanish();
    else if (action == ACTION_TURN_GROUP || action == ACTION_TURN_GROUP_2)
        r = act_turn_target_group();
    else if (action == ACTION_GIVE)
        r = act_give_items();
    else if (action == ACTION_TAKE_FROM_GROUP)
        r = act_take_items();
    else if (action == ACTION_PUT_IN_WALL || action == ACTION_DROP_HERE || action == ACTION_DROP_HERE_2 ||
             action == ACTION_DROP_AHEAD || action == ACTION_DROP_AHEAD_2)
        r = act_drop_items();
    else if (action == ACTION_TAKE_FROM_WALL || action == ACTION_TAKE_FRONT || action == ACTION_TAKE_SIDE)
        r = act_pick_up_items();
    else if (action == ACTION_SHOOT || action == ACTION_SHOOT_2)
        r = act_shoot();
    else if (action >= ACTION_USE_ACTUATOR && action <= ACTION_USE_ACTUATOR_LAST)
        r = act_use_wall_sensor();
    else if (action >= ACTION_STAIRS_UP && action <= ACTION_LINK_DOWN)
        r = act_move_stairs();
    else if (action == ACTION_TRANSFORM || action == ACTION_MORPH)
        act_transform();
    else if (action >= ACTION_SPECIAL && action <= ACTION_SPECIAL_LAST)
        r = act_explode_or_spawn();
    else if (action == ACTION_DODGE) /* the dodge cloud: the explosion of the missile wall spell */
        explosion_create(SPELL_MISSILE_WALL, rand_below(DODGE_CLOUD_POWER_RANGE) + DODGE_CLOUD_POWER_MIN,
                         g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y], EXPLOSION_AT_CENTER);
    return r;
}

int ai_execute_action(void)
{
    ActiveGroup *a = g_ai.active;
    ai_scratch_push();
    ai_state.dest_x = (int16_t)dest_x();
    ai_state.dest_y = (int16_t)dest_y();
    int r = run_act(a->action);
    if (!(int16_t)r) {
        if (ai_action_flag((int8_t)a->action) & AI_ACTION_FLAG_ATTACK)
            a->last_action_time = (uint8_t)g_game.game_time;
    } else {
        g_ai.cache.path_left = 0;
    }
    ai_scratch_pop();
    return r;
}
