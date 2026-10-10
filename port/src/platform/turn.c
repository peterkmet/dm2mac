#include <stdio.h>
#include <string.h>
#include "turn.h"
#include "advanced.h"
#include "champion.h"
#include "control.h"
#include "dungeon.h"
#include "game.h"
#include "input.h"
#include "move.h"
#include "projectile.h"
#include "text.h"
#include "ui.h"

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
    bool has_damage;
    const char *stop;                 /* wait: the news that ended it early */
} turn;

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

/* Gives one input to the input queue as the player gives it: a key, a click or a press or a release of the left
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

/* Actions: each lists its inputs, or returns the reason why it cannot run now. */

typedef const char *(*ActionFn)(const char *request);

static const char *need_movement_input(void)
{
    if (ui.inventory) return "close the inventory first (close_inventory)";
    return NULL;
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

static const struct {
    const char *name;
    ActionFn run;
} actions[] = {
    {"move_forward", move_forward}, {"move_back", move_back}, {"move_left", move_left}, {"move_right", move_right},
    {"turn_left", turn_left}, {"turn_right", turn_right},
};

/* Clears what the last action gave and changed. */
static void forget_action(void)
{
    turn.has_damage = turn.moving = false;
    turn.rotation = 0;
    turn.input_count = turn.inputs_given = 0;
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

bool turn_poll(void)
{
    if (!turn.pending) return false;
    if (turn.inputs_given < turn.input_count && input_is_idle()) give_input(&turn.inputs[turn.inputs_given++]);
    witness();
    if (!turn_finished()) {
        turn.frames++;
        return false;
    }
    turn.pending = false;
    return !turn.reply_dropped;
}

/* The stop of the turn: the news of a wait, or how the dungeon displaced the party after an action. */
static void stop_json(Json *j)
{
    const char *displaced = turn.wait_only || state_json_game_over() ? NULL : turn_displaced();
    if (!turn.wait_only && !displaced) return;
    json_key(j, "stopped");
    json_str(j, turn.wait_only ? (turn.stop ? turn.stop : "done") : displaced);
}

void turn_reply(Json *j)
{
    json_open(j, '{');
    json_key(j, "ok");
    json_bool(j, 1);
    json_key(j, "ticks");
    json_int(j, (long)(game.game_time - turn.start_time));
    stop_json(j);
    json_key(j, "game_over");
    json_bool(j, state_json_game_over());
    if (turn_bumped()) {
        json_key(j, "bumped");
        json_bool(j, 1);
    }
    health_lost_json(j);
    seen_json(j);
    json_key(j, "state");
    state_json_state(j);
    json_key(j, "view");
    state_json_view(j);
    json_close(j, '}');
}
