/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "turn.h"
#include "action.h"
#include "advanced.h"
#include "champion.h"
#include "control.h"
#include "creature.h"
#include "dungeon.h"
#include "game.h"
#include "input.h"
#include "item.h"
#include "magic.h"
#include "minion.h"
#include "money.h"
#include "move.h"
#include "os.h"
#include "projectile.h"
#include "text.h"
#include "ui.h"
#include "video.h"
#include "view.h"
#include "zone.h"

#define TURN_MIN_TICKS 2              /* an action shows its result before the turn ends */
#define TURN_MAX_TICKS 64
#define TURN_WAIT_MAX_TICKS 5000      /* the turn wait; the real-time wait keeps WAIT_MAX_TICKS 6000 of control.c */
#define FRAMES_PER_TICK_LIMIT 16      /* a game that does not tick (a dialog) ends a turn after this many frames per tick */
#define TURN_INPUT_MAX 4
#define ACTION_NAME_SIZE 32
#define TURN_ERROR_SIZE 64
#define WITNESSED_EXPLOSIONS_MAX 32
#define WITNESSED_GROUPS_MAX 16
#define GROUP_CREATURES 1             /* a DM2 group record holds one creature */
#define BATCH_DAMAGE_MAX 64
#define BATCH_ITEM_SIZE 1024          /* one item of an act_many list, a name or an object */
#define DROP_FRONT_CELL_FIRST 2       /* drop_object cells 2 and 3 are on the square ahead */
#define DROP_ALCOVE_CELL CELL_COUNT   /* drop_object cell 4: the alcove ahead */
#define CORNER_INSET 1                /* a click on a corner of a spell icon goes one pixel inside */

typedef enum { INPUT_KEY_PRESS, INPUT_CLICK, INPUT_PRESS, INPUT_RELEASE, INPUT_COMMAND } TurnInputKind;
typedef struct {
    TurnInputKind kind;
    uint16_t value;                   /* a key code, a command, or a mouse button bit */
    int16_t x, y;
} TurnInput;

static struct {
    bool pending, wait_only, reply_dropped;
    uint32_t start_time;
    long frames, min_ticks, max_ticks;
    TurnInput inputs[TURN_INPUT_MAX];
    int input_count, inputs_given;
    bool moving;                      /* the action was a step: forward and right in squares */
    int16_t step_forward, step_right, rotation;
    int16_t map, x, y, dir;           /* the party before the action */
    bool has_damage;                  /* choose_action: the reply has action_damage */
    bool looking;                     /* look: the turn records the eye panel text before the release */
    const char *stop;                 /* wait: the news that ended it early */
} turn;

/* A batch: act_many runs a list of actions; each item is one turn. */
static struct {
    bool active;
    int ran;
    int16_t damages[BATCH_DAMAGE_MAX];  /* the action damage of each choose_action, in order */
    int damage_count;
    char *actions;                    /* a copy of the request from its actions list; next is the rest */
    const char *next;
    uint32_t start_time;
    const char *stop, *error;
} batch;

/* The eye panel text of a look action, for the turn reply. */
static struct {
    bool shown;
    int count;
    char lines[OBJECT_INFO_LINES][OBJECT_INFO_SIZE];
} looked;

/* What the party knew before a turn: wait stops on news against it. */
static struct {
    int16_t health[CHAMPION_MAX];
    uint16_t groups[STATE_JSON_VISIBLE_GROUPS_MAX];
    int group_count;
    uint32_t message_expiry;
} seen;

/* What the party saw during a request, also when it was gone at the end: the explosions, and the creature groups with
   their creature count when first seen and when last seen. */
typedef struct {
    StateJsonSeenThing at;
    uint16_t type;
} WitnessedExplosion;
typedef struct {
    StateJsonSeenThing at;
    uint16_t creature;
    int16_t before, after;
    bool in_view, killed;
} WitnessedGroup;
static struct {
    int explosion_count, group_count;
    WitnessedExplosion explosions[WITNESSED_EXPLOSIONS_MAX];
    WitnessedGroup groups[WITNESSED_GROUPS_MAX];
    int16_t health[CHAMPION_MAX];     /* the health of each champion at the start of the request */
    uint16_t champion_count;
} witnessed;

bool turn_is_pending(void) { return turn.pending; }

void turn_drop_reply(void) { turn.reply_dropped = true; }

/* Inputs: an action lists them; the turn gives each one when the input of the one before is done. */

static void add_input(TurnInputKind kind, uint16_t value, int16_t x, int16_t y)
{
    if (turn.input_count < TURN_INPUT_MAX) turn.inputs[turn.input_count++] = (TurnInput){ kind, value, x, y };
}

static void add_key(uint16_t code) { add_input(INPUT_KEY_PRESS, code, 0, 0); }
static void add_command(uint16_t command) { add_input(INPUT_COMMAND, command, 0, 0); }
static void add_click(int16_t x, int16_t y, uint16_t button) { add_input(INPUT_CLICK, button, x, y); }
static void add_press(int16_t x, int16_t y) { add_input(INPUT_PRESS, INPUT_BUTTON_LEFT, x, y); }
static void add_release(int16_t x, int16_t y) { add_input(INPUT_RELEASE, INPUT_BUTTON_LEFT, x, y); }

/* Gives one input to the input queue as the player gives it: a key, a click or a press or a release of a mouse
   button at a point (the pointer moves there), or a click on the zone of a command. */
static void give_input(const TurnInput *in)
{
    switch (in->kind) {
    case INPUT_KEY_PRESS:
        input_key_down(in->value);
        return;
    case INPUT_COMMAND:
        input_simulate_command_click(in->value);
        return;
    case INPUT_CLICK:
    case INPUT_PRESS:
    case INPUT_RELEASE:
        input_mouse_x = in->x, input_mouse_y = in->y;
        if (in->kind != INPUT_RELEASE) input_mouse_down(in->x, in->y, in->value);
        if (in->kind != INPUT_PRESS) input_mouse_up(in->x, in->y, in->value);
        return;
    }
}

/* What the party saw during a request. */

/* Adds the explosions and the creature groups that the party sees now to the record of the request. */
static void witness(void)
{
    StateJsonSeenThing things[STATE_JSON_VISIBLE_GROUPS_MAX];
    int count = state_json_visible_things(THING_EXPLOSION, things, WITNESSED_EXPLOSIONS_MAX);
    for (int i = 0; i < count; i++) {
        uint16_t type = explosion_type(things[i].thing);
        bool known = false;
        for (int k = 0; k < witnessed.explosion_count; k++)
            known |= witnessed.explosions[k].type == type && witnessed.explosions[k].at.x == things[i].x
                     && witnessed.explosions[k].at.y == things[i].y;
        if (!known && witnessed.explosion_count < WITNESSED_EXPLOSIONS_MAX)
            witnessed.explosions[witnessed.explosion_count++] = (WitnessedExplosion){ things[i], type };
    }
    for (int k = 0; k < witnessed.group_count; k++) witnessed.groups[k].in_view = false;
    count = state_json_visible_things(THING_GROUP, things, STATE_JSON_VISIBLE_GROUPS_MAX);
    for (int i = 0; i < count; i++) {
        int k = 0;
        while (k < witnessed.group_count && witnessed.groups[k].at.thing != things[i].thing) k++;
        if (k == WITNESSED_GROUPS_MAX) continue;
        if (k == witnessed.group_count) {  /* a group seen first: count it once, not on each tick */
            witnessed.groups[k] = (WitnessedGroup){ .creature = group_creature(things[i].thing), .before = GROUP_CREATURES };
            witnessed.group_count++;
        }
        witnessed.groups[k].at = things[i];
        witnessed.groups[k].after = GROUP_CREATURES;
        witnessed.groups[k].in_view = true;
    }
    for (int k = 0; k < witnessed.group_count; k++)  /* a killed group frees its record: its link is THING_NONE */
        witnessed.groups[k].killed |= !witnessed.groups[k].in_view && dungeon_next_thing(witnessed.groups[k].at.thing) == THING_NONE;
}

/* Starts the record of what the party sees during a request. */
static void start_witnessing(void)
{
    memset(&witnessed, 0, sizeof witnessed);
    witnessed.champion_count = game.champion_count;
    for (uint16_t i = 0; i < game.champion_count; i++) witnessed.health[i] = champion(i)->health;
    witness();
}

static void seen_place_json(Json *j, const StateJsonSeenThing *at)
{
    json_key(j, "x");
    json_int(j, at->x);
    json_key(j, "y");
    json_int(j, at->y);
    json_key(j, "depth");
    json_int(j, at->depth);
    json_key(j, "side");
    json_str(j, state_json_side_name(at->side));
}

/* The explosions the party saw and the groups that lost creatures in view or were killed; nothing when none. */
static void seen_json(Json *j)
{
    int changes = 0;
    for (int k = 0; k < witnessed.group_count; k++)
        changes += witnessed.groups[k].killed || witnessed.groups[k].after < witnessed.groups[k].before;
    if (!changes && !witnessed.explosion_count) return;
    json_key(j, "seen");
    json_open(j, '{');
    json_key(j, "explosions");
    json_open(j, '[');
    for (int k = 0; k < witnessed.explosion_count; k++) {
        json_open(j, '{');
        json_key(j, "name");
        state_json_explosion_name(j, witnessed.explosions[k].type);
        seen_place_json(j, &witnessed.explosions[k].at);
        json_close(j, '}');
    }
    json_close(j, ']');
    json_key(j, "creature_changes");
    json_open(j, '[');
    for (int k = 0; k < witnessed.group_count; k++) {
        if (!witnessed.groups[k].killed && witnessed.groups[k].after >= witnessed.groups[k].before) continue;
        json_open(j, '{');
        json_key(j, "type");
        state_json_creature_name(j, witnessed.groups[k].creature);
        seen_place_json(j, &witnessed.groups[k].at);
        json_key(j, "before");
        json_int(j, witnessed.groups[k].before);
        json_key(j, "after");
        json_int(j, witnessed.groups[k].killed ? 0 : witnessed.groups[k].after);
        json_close(j, '}');
    }
    json_close(j, ']');
    json_close(j, '}');
}

/* The health that each champion lost since the start of the request, only when one lost health. */
static void health_lost_json(Json *j)
{
    bool any = false;
    for (uint16_t i = 0; i < witnessed.champion_count; i++) any |= champion(i)->health < witnessed.health[i];
    if (!any) return;
    json_key(j, "health_lost");
    json_open(j, '[');
    for (uint16_t i = 0; i < witnessed.champion_count; i++) {
        int lost = witnessed.health[i] - champion(i)->health;
        json_int(j, lost > 0 ? lost : 0);
    }
    json_close(j, ']');
}

/* News: what happened since the snapshot that the player must see. */

/* The latest expiry time of a message row: it grows when the game prints a message. */
static uint32_t latest_message_expiry(void)
{
    uint32_t latest = 0;
    for (int r = 0; r < MESSAGE_ROWS; r++)
        if (message_expiry[r] != MESSAGE_NEVER && message_expiry[r] > latest) latest = message_expiry[r];
    return latest;
}

/* Remembers what the news checks compare during and after the next turn. */
static void snapshot(void)
{
    for (uint16_t i = 0; i < game.champion_count; i++) seen.health[i] = champion(i)->health;
    seen.group_count = state_json_visible_groups(seen.groups);
    seen.message_expiry = latest_message_expiry();
}

/* A creature group is in view that was not in view at the snapshot. */
static bool new_group_in_view(void)
{
    uint16_t groups[STATE_JSON_VISIBLE_GROUPS_MAX];
    int count = state_json_visible_groups(groups);
    for (int i = 0; i < count; i++) {
        bool known = false;
        for (int k = 0; k < seen.group_count; k++) known |= groups[i] == seen.groups[k];
        if (!known) return true;
    }
    return false;
}

/* What happened since the snapshot that the player must see, or NULL. */
static const char *news(void)
{
    if (state_json_game_over()) return "game_over";
    for (uint16_t i = 0; i < game.champion_count; i++)
        if (champion(i)->health < seen.health[i]) return "damage";
    if (new_group_in_view()) return "creature";
    if (latest_message_expiry() > seen.message_expiry) return "message";
    return NULL;
}

/* Bump and displacement. */

/* The step of the turn left the party on its square: a wall, a closed door, or a creature is in the way. A
   teleporter that sends the party back to its square counts as a bump (DM2 has no teleport counter). */
static bool turn_bumped(void)
{
    return turn.moving && game.party_map == turn.map && game.party_x == turn.x && game.party_y == turn.y;
}

/* The party was turned or moved by something else than its action: a spinner, a teleporter, a pit, stairs. */
static const char *turn_displaced(void)
{
    int16_t x = turn.x, y = turn.y;
    if (turn.moving) dun_move_relative(turn.dir, turn.step_forward, turn.step_right, &x, &y);
    bool stayed = game.party_x == turn.x && game.party_y == turn.y;
    if (game.party_map != turn.map || (!stayed && (game.party_x != x || game.party_y != y))) return "moved";
    if (game.party_dir != turn.dir && game.party_dir != dir_turn(turn.dir, turn.rotation)) return "turned";
    return NULL;
}

/* Actions: each lists its inputs, or returns the reason why it cannot run now (it lists nothing then). */

typedef const char *(*ActionFn)(const char *request);

#define NO_CHAMPION "no living champion with this index"
#define PANEL_OPEN "close the open panel first (pass)"
#define HAND_FULL "the leader hand is not empty: drop or store the object first"
#define HAND_EMPTY "the leader hand is empty"

static bool hand_is_empty(void) { return game.leader_hand == THING_NONE; }
static bool has_leader(void) { return (int16_t)game.leader != -1; }

/* The champion of the request key: a living member of the party, not the candidate; -1 none. */
static int champion_arg(const char *request, const char *key)
{
    long c = control_get_int(request, key, -1);
    int members = game.champion_count - (ui.candidate ? 1 : 0);
    if (c < 0 || c >= members || c >= CHAMPION_MAX || champion((int)c)->health <= 0) return -1;
    return (int)c;
}

/* The cell of champion c relative to the party direction: the place of its icons on the screen. */
static int party_cell(int c) { return dir_relative(champion(c)->cell, game.party_dir); }

/* A left click on the center of a rect of the view picture; false when the screen has no viewport. */
static bool add_view_click(const int16_t *view_rect)
{
    int16_t origin[RECT_SIZE];
    if (!zone_get_rect(ZONE_VIEWPORT, origin)) return false;
    add_click(origin[RECT_X] + view_rect[RECT_X] + view_rect[RECT_W] / 2,
              origin[RECT_Y] + view_rect[RECT_Y] + view_rect[RECT_H] / 2, INPUT_BUTTON_LEFT);
    return true;
}

/* A click on the zone of command, when the current input mode has one. */
static bool add_shown_command(uint16_t command)
{
    int16_t r[RECT_SIZE];
    if (!input_command_rect(command, r)) return false;
    add_command(command);
    return true;
}

static const char *need_movement_input(void)
{
    if (ui.inventory) return "close the inventory first (close_inventory)";
    return NULL;
}

/* A click into the view needs the view of the movement mode. */
static const char *need_view(void)
{
    int16_t r[RECT_SIZE];
    if (ui.inventory) return "close the inventory first (close_inventory)";
    if (!input_command_rect(CMD_VIEWPORT, r)) return "the view takes no click now";
    return NULL;
}

/* The square ahead of the party is a wall (outside the map counts as a wall). */
static bool wall_ahead(void)
{
    int x = game.party_x + dir_dx[game.party_dir], y = game.party_y + dir_dy[game.party_dir];
    if (x < 0 || y < 0 || x >= dungeon_map_width(game.party_map) || y >= dungeon_map_height(game.party_map)) return true;
    return square_type(dungeon_square(game.party_map, x, y)) == SQUARE_WALL;
}

/* A step remembers where it goes, so that the turn result can tell a bump or a move by the dungeon. */
static const char *step(uint16_t key, int16_t forward, int16_t right)
{
    const char *error = need_movement_input();
    if (error) return error;
    turn.moving = true;
    turn.step_forward = forward, turn.step_right = right;
    add_key(key);
    return NULL;
}

static const char *rotate(uint16_t key, int16_t rotation)
{
    const char *error = need_movement_input();
    if (error) return error;
    turn.rotation = rotation;
    add_key(key);
    return NULL;
}

static const char *move_forward(const char *r) { (void)r; return step(KEYPAD_FORWARD, 1, 0); }
static const char *move_back(const char *r) { (void)r; return step(KEYPAD_BACK, -1, 0); }
static const char *move_left(const char *r) { (void)r; return step(KEYPAD_LEFT, 0, -1); }
static const char *move_right(const char *r) { (void)r; return step(KEYPAD_RIGHT, 0, 1); }
static const char *turn_left(const char *r) { (void)r; return rotate(KEYPAD_TURN_LEFT, -1); }
static const char *turn_right(const char *r) { (void)r; return rotate(KEYPAD_TURN_RIGHT, 1); }

/* The click zone of the wall ahead: a wall ornament or a door button of the square ahead, or the alcove; NULL none. */
static const ViewClickZone *front_click_zone(void)
{
    for (int i = VIEW_CLICK_ZONE_FIXED; i < view_click_zone_count; i++) {
        const ViewClickZone *z = &view_click_zones[i];
        if (z->cell == POS_FRONT && (z->kind == CLICK_WALL_ORNAMENT || z->kind == CLICK_DOOR_BUTTON)) return z;
    }
    const ViewClickZone *alcove = &view_click_zones[VIEW_CLICK_ZONE_ALCOVE];
    return alcove->kind == CLICK_ALCOVE ? alcove : NULL;
}

/* Clicks the switch, the lock, the mirror, the door button, or the alcove ahead; else touches the plain wall. */
static const char *click_wall_ahead(const char *r)
{
    (void)r;
    int16_t wall[RECT_SIZE];
    const char *error = need_view();
    if (error) return error;
    const ViewClickZone *z = front_click_zone();
    if (z) return add_view_click(z->rect) ? NULL : "the view takes no click now";
    if (!wall_ahead()) return "nothing to click on the wall ahead";
    if (!hand_is_empty()) return "the leader hand is not empty: nothing to use it on ahead";  /* a click would throw it */
    if (!zone_get_rect(ZONE_FRONT_WALL_LOW, wall) || !add_view_click(wall)) return "nothing to click on the wall ahead";
    return NULL;
}

/* The leader takes the top object of a cell of the view (the cells of get_view piles). */
static const char *take_object(const char *r)
{
    long cell = control_get_int(r, "cell", -1);
    const char *error = need_view();
    if (error) return error;
    if (cell < 0 || cell >= CELL_COUNT) return "cell must be 0-3 (see get_view piles)";
    if (!hand_is_empty()) return HAND_FULL;
    if (!has_leader()) return "no leader";
    const ViewClickZone *z = &view_click_zones[cell];
    if (z->kind != CLICK_OBJECT || z->thing == THING_NONE) return "no object at this cell";
    return add_view_click(z->rect) ? NULL : "the view takes no click now";
}

/* A static creature ahead whose floor takes no click hides the floor: a click there throws the object. */
static bool front_floor_clickable(void)
{
    int x = game.party_x + dir_dx[game.party_dir], y = game.party_y + dir_dy[game.party_dir];
    uint16_t group = creature_group_at(game.party_map, x, y);
    if (group == THING_NONE) return true;
    const CreatureInfo *info = creature_group_info(group);
    return !(info->flags & CREATURE_STATIC) || info->possessions & POSSESSIONS_FLOOR_CLICKABLE;
}

/* The leader drops the hand object into a cell of the party square (0, 1), of the square ahead (2, 3), or into the
   alcove ahead (4). */
static const char *drop_object(const char *r)
{
    long cell = control_get_int(r, "cell", -1);
    int16_t zone[RECT_SIZE];
    const char *error = need_view();
    if (error) return error;
    if (cell < 0 || cell > DROP_ALCOVE_CELL) return "cell must be 0-4";
    if (hand_is_empty()) return HAND_EMPTY;
    if (!has_leader()) return "no leader";
    if (cell == DROP_ALCOVE_CELL) {
        const ViewClickZone *alcove = &view_click_zones[VIEW_CLICK_ZONE_ALCOVE];
        if (alcove->kind != CLICK_ALCOVE) return "no alcove ahead";
        return add_view_click(alcove->rect) ? NULL : "the view takes no click now";
    }
    if (cell >= DROP_FRONT_CELL_FIRST && wall_ahead()) return "no floor ahead";
    if (cell >= DROP_FRONT_CELL_FIRST && !front_floor_clickable()) return "no floor to drop on ahead";
    if (!zone_get_rect(ZONE_DROP_CELL_FIRST + (int)cell, zone) || !add_view_click(zone)) return "the view takes no click now";
    return NULL;
}

static const char *open_inventory(const char *r)
{
    int c = champion_arg(r, "champion");
    if (c < 0) return NO_CHAMPION;
    if (ui.inventory == c + 1) return NULL;
    return add_shown_command(CMD_INVENTORY_FIRST + c) ? NULL : "the inventory cannot open now";
}

static const char *close_inventory(const char *r)
{
    (void)r;
    if (!ui.inventory) return NULL;
    return add_shown_command(CMD_INVENTORY_LEADER) ? NULL : "the inventory cannot close now";
}

/* The command of a click on a slot of champion c: a hand of a status box, a slot of the open inventory, or a slot of
   the open chest (30 to 37); NULL, or the reason why the slot cannot be clicked now. */
static const char *slot_command(int c, long slot, uint16_t *command)
{
    if (slot < 0 || slot >= SLOT_CONTAINER_FIRST + CONTAINER_SLOTS) return "slot must be 0-37";
    if (slot >= SLOT_CONTAINER_FIRST) {
        if (action.mode != ACTION_MODE_CONTAINER) return "no open chest";
        *command = CMD_SLOT_CHEST_FIRST + (uint16_t)(slot - SLOT_CONTAINER_FIRST);
    } else if (ui.inventory == c + 1) {
        *command = CMD_SLOT_INVENTORY_FIRST + (uint16_t)slot;
    } else if (slot < SLOT_HAND_COUNT) {
        *command = CMD_SLOT_FIRST + SLOT_HAND_COUNT * c + (uint16_t)slot;
    } else {
        return "open the inventory of this champion first (open_inventory)";
    }
    int16_t rect[RECT_SIZE];
    return input_command_rect(*command, rect) ? NULL : "this slot cannot be clicked now";
}

/* A click on a slot swaps the leader hand object with the slot object: put and take are the same click. */
static const char *click_slot(const char *r, bool put)
{
    uint16_t command;
    int c = champion_arg(r, "champion");
    if (c < 0) return NO_CHAMPION;
    if (!has_leader()) return "no leader";
    if (put && hand_is_empty()) return HAND_EMPTY;
    if (!put && !hand_is_empty()) return "the leader hand is not empty";
    const char *error = slot_command(c, control_get_int(r, "slot", -1), &command);
    if (error) return error;
    add_command(command);
    return NULL;
}

static const char *put_in_slot(const char *r) { return click_slot(r, true); }
static const char *take_from_slot(const char *r) { return click_slot(r, false); }

/* The champion of the open inventory eats or drinks the leader hand object. */
static const char *use_mouth(const char *r)
{
    (void)r;
    if (!ui.inventory) return "open the inventory of the champion who eats or drinks first";
    if (hand_is_empty()) return "take the food or drink in the leader hand first";
    return add_shown_command(CMD_MOUTH) ? NULL : "the mouth takes no click now";
}

/* A hand object that opens a panel instead of an action menu: a money box, a chest, a minion map. */
static bool opens_panel(uint16_t thing)
{
    return thing != THING_NONE && (item_is_moneybox(thing) || item_is_chest(thing) || item_is_minion_map(thing));
}

/* Opens the action menu of a hand and runs its action index; a money box, a chest, or a minion map opens its panel. */
static const char *choose_action(const char *r)
{
    char name[ACTION_NAME_SIZE] = "";
    int c = champion_arg(r, "champion");
    long index = control_get_int(r, "index", -1);
    if (c < 0) return NO_CHAMPION;
    control_get_string(r, "hand", name, sizeof name);
    int hand = !strcmp(name, "ready") ? SLOT_READY_HAND : !strcmp(name, "action") ? SLOT_ACTION_HAND : -1;
    if (hand < 0) return "hand must be ready or action";
    bool open = action.mode == ACTION_MODE_MENU && action.champion == c + 1 && action.hand == hand;
    if (!open && action.champion)
        return action.mode == ACTION_MODE_MENU ? "another champion's action menu is open: use pass first" : PANEL_OPEN;
    uint16_t thing = champion(c)->slots[hand];
    int count = open ? champ_action_count : state_json_hand_action_count(c, hand);
    if (champion(c)->action_delay[hand] || (!count && !opens_panel(thing))) return "this champion cannot act now";
    uint16_t hand_command = CMD_HAND_FIRST + SLOT_HAND_COUNT * party_cell(c) + hand;
    if (opens_panel(thing)) return add_shown_command(hand_command) ? NULL : "this hand cannot be clicked now";
    if (index < 0 || index >= count) return "index out of range of the action menu (see state champions hands)";
    if (!open && !add_shown_command(hand_command)) return "this hand cannot be clicked now";
    add_command(CMD_ACTION_CANCEL + 1 + (uint16_t)index);
    turn.has_damage = true;
    champ_action_result = 0;
    return NULL;
}

/* Closes the open action menu or panel. */
static const char *pass(const char *r)
{
    (void)r;
    if (!action.champion) return NULL;
    return add_shown_command(CMD_ACTION_CANCEL) ? NULL : "the open panel has no close button";
}

/* A click on a spell icon at the corner that input_refine_champion_click reads as the caster or as the leader choice:
   by party cell 0 to 3, the corner (right, bottom) of the caster, else of the leader. */
static const char *click_spell_icon(const char *r, bool caster)
{
    static const bool right[CELL_COUNT] = {false, true, true, false}, bottom[CELL_COUNT] = {true, true, false, false};
    int16_t rect[RECT_SIZE];
    int c = champion_arg(r, "champion");
    if (c < 0) return NO_CHAMPION;
    if (action.champion) return PANEL_OPEN;
    if (caster && champion(c)->action_delay[ACTION_SPELL]) return "this champion cannot cast now";
    int cell = party_cell(c);
    if (!input_command_rect(CMD_PARTY_ICON_FIRST + cell, rect)) return "the spell icons take no click now";
    bool at_right = right[cell] == caster, at_bottom = bottom[cell] == caster;
    add_click(at_right ? rect[RECT_X] + rect[RECT_W] - 1 - CORNER_INSET : rect[RECT_X] + CORNER_INSET,
              at_bottom ? rect[RECT_Y] + rect[RECT_H] - 1 - CORNER_INSET : rect[RECT_Y] + CORNER_INSET, INPUT_BUTTON_LEFT);
    return NULL;
}

static const char *set_caster(const char *r) { return click_spell_icon(r, true); }
static const char *set_leader(const char *r) { return click_spell_icon(r, false); }

/* The caster of the open spell panel, -1 none. */
static int spell_caster(void) { return action.mode == ACTION_MODE_SPELL && action.champion ? action.champion - 1 : -1; }

static const char *add_rune(const char *r)
{
    long symbol = control_get_int(r, "symbol", -1);
    int caster = spell_caster();
    if (symbol < 0 || symbol >= SPELL_RUNES_PER_SET) return "symbol must be 0-5";
    if (caster < 0) return "no spell panel (set_caster)";
    if (champion(caster)->rune_count >= CHAMPION_RUNES_SIZE) return "the spell has four runes";
    if (magic_rune_mana_cost(caster, (int)symbol) > champion(caster)->mana) return "not enough mana for this rune";
    return add_shown_command(CMD_RUNE_FIRST + (uint16_t)symbol) ? NULL : "the runes take no click now";
}

/* A spell panel button that needs an entered rune: delete the last rune, or cast. */
static const char *rune_button(uint16_t command)
{
    int caster = spell_caster();
    if (caster < 0) return "no spell panel (set_caster)";
    if (!champion(caster)->rune_count) return "no rune";
    return add_shown_command(command) ? NULL : "the spell panel takes no click now";
}

static const char *delete_rune(const char *r) { (void)r; return rune_button(CMD_REMOVE_RUNE); }
static const char *cast(const char *r) { (void)r; return rune_button(CMD_CAST_SPELL); }

/* The candidate of the mirror joins the party, or goes back to its mirror. */
static const char *recruit(const char *r)
{
    char choice[ACTION_NAME_SIZE] = "";
    if (!ui.candidate) return "no candidate: click a champion mirror first (click_wall_ahead)";
    control_get_string(r, "choice", choice, sizeof choice);
    bool join = !strcmp(choice, "join");
    if (!join && strcmp(choice, "cancel")) return "choice must be join or cancel";
    return add_shown_command(join ? CMD_RECRUIT : CMD_RECRUIT_CANCEL) ? NULL : "the recruit buttons take no click now";
}

/* The party sleeps: the inventory of the leader opens first when no inventory is open (the sleep button is there). */
static const char *sleep_party(const char *r)
{
    (void)r;
    if (!has_leader()) return "no leader";
    /* the status box of the leader: a click on the zone of CMD_LEADER_INVENTORY hits the status box of champion 0 */
    if (!ui.inventory && !add_shown_command(CMD_INVENTORY_FIRST + game.leader)) return "the inventory cannot open now";
    add_command(CMD_SLEEP);
    return NULL;
}

static const char *wake(const char *r)
{
    (void)r;
    if (!party_sleeping) return NULL;
    return add_shown_command(CMD_WAKE) ? NULL : "the party cannot wake now";
}

/* Clicks the face of champion a, then the face of champion b: the two champions swap cells. */
static const char *swap_champions(const char *r)
{
    int a = champion_arg(r, "a"), b = champion_arg(r, "b");
    if (a < 0 || b < 0) return "a and b must be living champions";
    if (a == b) return "a and b must be different champions";
    if (action.champion) return PANEL_OPEN;
    if (!add_shown_command(CMD_DRAG_FIRST + party_cell(a))) return "the champion faces take no click now";
    add_command(CMD_DRAG_FIRST + party_cell(b));
    return NULL;
}

/* Holds the eye on the leader hand object; with a slot, the hand takes the slot object first and puts it back after.
   The turn records the eye panel text before the release. */
static const char *look(const char *r)
{
    uint16_t command = 0;
    int16_t eye[RECT_SIZE];
    long slot = control_get_int(r, "slot", -1);
    if (!ui.inventory) return "open an inventory first (open_inventory): the eye is in the inventory";
    if (!input_command_rect(CMD_EYE, eye)) return "the eye takes no click now";
    if (slot < 0 && hand_is_empty()) return "the leader hand is empty: take an object, or give champion and slot";
    if (slot >= 0) {
        if (!hand_is_empty()) return "the leader hand must be empty to look at a slot";
        int c = control_find_value(r, "champion") ? champion_arg(r, "champion") : ui.inventory - 1;
        if (c < 0) return NO_CHAMPION;
        const char *error = slot_command(c, slot, &command);
        if (error) return error;
        uint16_t held = slot >= SLOT_CONTAINER_FIRST ? action.open_items[slot - SLOT_CONTAINER_FIRST] : champion(c)->slots[slot];
        if (held == THING_NONE) return "no object in this slot";
        add_command(command);
    }
    int16_t x = eye[RECT_X] + eye[RECT_W] / 2, y = eye[RECT_Y] + eye[RECT_H] / 2;
    add_press(x, y);
    add_release(x, y);
    if (command) add_command(command);
    turn.looking = true;
    return NULL;
}

/* One coin of a slot of the open money box goes to the leader hand. */
static const char *take_coin(const char *r)
{
    long slot = control_get_int(r, "slot", -1);
    if (action.mode != ACTION_MODE_MONEY_BOX || !action.champion)
        return "open a money box first (choose_action on the hand that holds it)";
    if (slot < 0 || slot > CMD_COIN_LAST - CMD_COIN_FIRST) return "slot must be 0-5";
    if (money_coin_type_for_slot(champion(action.champion - 1)->slots[action.hand], (int)slot) < 0) return "no coin in this slot";
    return add_shown_command(CMD_COIN_FIRST + (uint16_t)slot) ? NULL : "the money box takes no click now";
}

static const char *minion_map_option(const char *r)
{
    long option = control_get_int(r, "option", -1);
    if (action.mode != ACTION_MODE_MINION_MAP) return "open a minion map first";
    if (!(action.flags & MAP_HAS_OPTIONS)) return "this minion map has no options";
    if (option < 0 || option >= MAP_OPTION_COUNT) return "option must be 0-3";
    return add_shown_command(CMD_MAP_OPTION_FIRST + (uint16_t)option) ? NULL : "the minion map takes no click now";
}

/* A raw click of the left or the right button at a point of the screen. */
static const char *click(const char *r)
{
    static char error[TURN_ERROR_SIZE];
    char button[ACTION_NAME_SIZE] = "left";
    long x = control_get_int(r, "x", -1), y = control_get_int(r, "y", -1);
    if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) {
        snprintf(error, sizeof error, "x must be 0-%d and y 0-%d", SCREEN_W - 1, SCREEN_H - 1);
        return error;
    }
    control_get_string(r, "button", button, sizeof button);
    bool right = !strcmp(button, "right");
    if (!right && strcmp(button, "left")) return "button must be left or right";
    add_click((int16_t)x, (int16_t)y, right ? INPUT_BUTTON_RIGHT : INPUT_BUTTON_LEFT);
    return NULL;
}

/* A raw key: a key name of docs/llm-control.md or a key code. */
static const char *key(const char *r)
{
    char name[ACTION_NAME_SIZE];
    long code = control_get_int(r, "key", 0);
    if (control_get_string(r, "key", name, sizeof name)) code = control_key_code(name);
    if (code <= 0 || code > KEY_CODE_MAX) return "unknown key";
    add_key((uint16_t)code);
    return NULL;
}

static const struct {
    const char *name;
    ActionFn run;
} actions[] = {
    {"move_forward", move_forward}, {"move_back", move_back}, {"move_left", move_left}, {"move_right", move_right},
    {"turn_left", turn_left}, {"turn_right", turn_right}, {"click_wall_ahead", click_wall_ahead},
    {"take_object", take_object}, {"drop_object", drop_object}, {"open_inventory", open_inventory},
    {"close_inventory", close_inventory}, {"put_in_slot", put_in_slot}, {"take_from_slot", take_from_slot},
    {"eat", use_mouth}, {"drink", use_mouth}, {"choose_action", choose_action}, {"pass", pass},
    {"set_caster", set_caster}, {"add_rune", add_rune}, {"delete_rune", delete_rune}, {"cast", cast},
    {"recruit", recruit}, {"set_leader", set_leader}, {"sleep", sleep_party}, {"wake", wake},
    {"swap_champions", swap_champions}, {"look", look}, {"take_coin", take_coin},
    {"minion_map_option", minion_map_option}, {"click", click}, {"key", key},
};

/* Clears what the last action gave and changed. */
static void forget_action(void)
{
    turn.has_damage = turn.moving = turn.looking = false;
    turn.rotation = 0;
    turn.input_count = turn.inputs_given = 0;
    looked.shown = false;
}

/* Lists the inputs of action name with the args in request; NULL, or the reason why it cannot. A candidate on the
   screen allows only recruit and the raw input; a sleeping party only wake and the raw input. */
static const char *run_action(const char *name, const char *request)
{
    bool raw = !strcmp(name, "click") || !strcmp(name, "key");
    for (size_t k = 0; k < sizeof actions / sizeof actions[0]; k++) {
        if (strcmp(name, actions[k].name)) continue;
        if (ui.candidate && strcmp(name, "recruit") && !raw) return "a candidate is shown: answer with recruit first";
        if (party_sleeping && strcmp(name, "wake") && !raw) return "the party sleeps: use wake or wait";
        forget_action();
        turn.map = game.party_map, turn.x = game.party_x, turn.y = game.party_y, turn.dir = game.party_dir;
        return actions[k].run(request);
    }
    return "unknown action";
}

/* Turns. */

static void start_turn(long min_ticks, long max_ticks, bool wait_only)
{
    turn.pending = true;
    turn.wait_only = wait_only;
    turn.reply_dropped = false;
    turn.stop = NULL;
    turn.start_time = game.game_time;
    turn.frames = 0;
    turn.min_ticks = min_ticks;
    turn.max_ticks = max_ticks;
}

const char *turn_act(const char *request)
{
    char name[ACTION_NAME_SIZE];
    if (!control_get_string(request, "action", name, sizeof name)) return "missing action";
    start_witnessing();
    const char *error = run_action(name, request);
    if (error) return error;
    start_turn(TURN_MIN_TICKS, TURN_MAX_TICKS, false);
    return NULL;
}

const char *turn_wait(const char *request)
{
    static char error[TURN_ERROR_SIZE];
    long ticks = control_get_int(request, "ticks", 1);
    if (ticks < 1 || ticks > TURN_WAIT_MAX_TICKS) {
        snprintf(error, sizeof error, "ticks must be 1-%d", TURN_WAIT_MAX_TICKS);
        return error;
    }
    forget_action();
    snapshot();
    start_witnessing();
    start_turn(ticks, ticks, true);
    return NULL;
}

/* Batches. */

/* Ends the batch: the reply gives the stop and the error. */
static void batch_finish(const char *stop, const char *error)
{
    batch.stop = stop;
    batch.error = error;
}

/* Frees the batch after its reply, or when its client left. */
static void batch_clear(void)
{
    free(batch.actions);
    memset(&batch, 0, sizeof batch);
}

/* Why a batch stops after a turn, or NULL to go on. */
static const char *batch_stop_reason(void)
{
    if (state_json_game_over()) return news();
    if (turn_bumped()) return "bumped";  /* before damage: a bump into a wall hurts */
    const char *displaced = turn_displaced();  /* before damage: a pit fall hurts */
    return displaced ? displaced : news();
}

/* The next act_many item, a "name" or an {"action": "name", "args": {...}} object, copied to out. Returns 1, 0 at
   the end of the list, or -1 for an item that is not an action. Strings hold no '"' (docs/turnbased-mode.md). */
static int next_element(char *out, size_t size)
{
    const char *p = batch.next;
    while (*p == ' ' || *p == ',') p++;
    const char *start = p;
    if (!*p || *p == ']') return 0;
    if (*p == '"') {
        p = strchr(p + 1, '"');
        if (!p) return -1;
        p++;
    } else if (*p == '{') {
        for (int depth = 0; *p; p++) {
            if (*p == '"') p = strchr(p + 1, '"');
            if (!p) return -1;
            depth += (*p == '{') - (*p == '}');
            if (!depth) break;
        }
        if (!*p++) return -1;
    } else {
        return -1;
    }
    if ((size_t)(p - start) >= size) return -1;
    memcpy(out, start, (size_t)(p - start));
    out[p - start] = 0;
    batch.next = p;
    return 1;
}

/* Starts the turn of the next item of the batch; ends the batch when the list is done or the item cannot run.
   Returns true when a turn started. */
static bool batch_next(void)
{
    char element[BATCH_ITEM_SIZE], name[ACTION_NAME_SIZE];
    snapshot();
    int found = next_element(element, sizeof element);
    if (found <= 0) {
        batch_finish(found ? "failed" : "done", found ? "an item of actions is not an action name or object" : NULL);
        return false;
    }
    if (element[0] == '"') snprintf(name, sizeof name, "%.*s", (int)strlen(element) - 2, element + 1);
    else if (!control_get_string(element, "action", name, sizeof name)) name[0] = 0;
    const char *error = run_action(name, element);
    if (error) {
        batch_finish("failed", error);
        return false;
    }
    start_turn(TURN_MIN_TICKS, TURN_MAX_TICKS, false);
    return true;
}

/* After a turn of the batch: counts it and its action damage, stops on news, else starts the next item. Returns true
   when the next item started. */
static bool batch_after_turn(void)
{
    batch.ran++;
    if (turn.has_damage && batch.damage_count < BATCH_DAMAGE_MAX) batch.damages[batch.damage_count++] = champ_action_result;
    const char *stop = batch_stop_reason();
    if (stop) {
        batch_finish(stop, NULL);
        return false;
    }
    return batch_next();
}

const char *turn_act_many(const char *request)
{
    const char *list = control_find_value(request, "actions");
    if (!list || *list != '[') return "actions must be a list";
    batch_clear();
    batch.actions = strdup(list + 1);
    if (!batch.actions) os_fatal("out of memory");
    batch.next = batch.actions;
    batch.active = true;
    batch.start_time = game.game_time;
    forget_action();
    start_witnessing();
    batch_next();
    return NULL;
}

/* A turn ends when the party can act again after the minimum ticks, or at the maximum. A wait ends early on news. A
   game that does not tick ends the turn after a frame limit. */
static bool turn_finished(void)
{
    long ticks = (long)(game.game_time - turn.start_time);
    if (turn.wait_only && (turn.stop = news())) return true;
    if (state_json_game_over()) return true;
    if (ticks >= turn.max_ticks || turn.frames > turn.max_ticks * FRAMES_PER_TICK_LIMIT) return true;
    return !turn.wait_only && ticks >= turn.min_ticks && turn.inputs_given == turn.input_count && input_is_idle()
           && !game.party_move_cooldown && !party.move_steps && !input_is_waiting();
}

/* The eye panel text of the leader hand object while the eye is held. */
static void record_look(void)
{
    looked.count = ui_object_info_lines(game.leader_hand, looked.lines);
    looked.shown = true;
}

bool turn_poll(void)
{
    if (!turn.pending) return false;
    if (turn.inputs_given < turn.input_count && input_is_idle()) {
        const TurnInput *in = &turn.inputs[turn.inputs_given++];
        if (in->kind == INPUT_RELEASE && turn.looking) record_look();
        give_input(in);
    }
    witness();
    if (!turn_finished()) {
        turn.frames++;
        return false;
    }
    turn.pending = false;
    if (batch.active && !turn.reply_dropped && batch_after_turn()) return false;  /* the next action of the batch runs */
    if (turn.reply_dropped) batch_clear();
    return !turn.reply_dropped;
}

/* The stop of the turn: the end of a batch, the news of a wait, or how the dungeon displaced the party after an
   action. */
static void stop_json(Json *j)
{
    if (batch.active) {
        json_key(j, "ran");
        json_int(j, batch.ran);
        json_key(j, "stopped");
        json_str(j, batch.stop);
        if (batch.error) {
            json_key(j, "error");
            json_str(j, batch.error);
        }
        return;
    }
    const char *displaced = turn.wait_only || state_json_game_over() ? NULL : turn_displaced();
    if (!turn.wait_only && !displaced) return;
    json_key(j, "stopped");
    json_str(j, turn.wait_only ? (turn.stop ? turn.stop : "done") : displaced);
}

/* The result of choose_action: one value after act, the list of the choose_action items after act_many. */
static void action_damage_json(Json *j)
{
    if (batch.active ? !batch.damage_count : !turn.has_damage) return;
    json_key(j, "action_damage");
    if (!batch.active) {
        json_int(j, champ_action_result);
        return;
    }
    json_open(j, '[');
    for (int i = 0; i < batch.damage_count; i++) json_int(j, batch.damages[i]);
    json_close(j, ']');
}

/* The eye panel text of a look action: the object name and the other lines, only when the turn looked. */
static void look_json(Json *j)
{
    if (!looked.shown) return;
    json_key(j, "look");
    json_open(j, '{');
    json_key(j, "name");
    json_str(j, looked.count ? looked.lines[0] : "");
    json_key(j, "lines");
    json_open(j, '[');
    for (int i = 1; i < looked.count; i++) json_str(j, looked.lines[i]);
    json_close(j, ']');
    json_close(j, '}');
}

void turn_reply(Json *j)
{
    json_open(j, '{');
    json_key(j, "ok");
    json_bool(j, 1);
    json_key(j, "ticks");
    json_int(j, (long)(game.game_time - (batch.active ? batch.start_time : turn.start_time)));
    stop_json(j);
    json_key(j, "game_over");
    json_bool(j, state_json_game_over());
    if (turn_bumped()) {
        json_key(j, "bumped");
        json_bool(j, 1);
    }
    action_damage_json(j);
    health_lost_json(j);
    look_json(j);
    seen_json(j);
    json_key(j, "state");
    state_json_state(j);
    json_key(j, "view");
    state_json_view(j);
    json_close(j, '}');
    batch_clear();
}
