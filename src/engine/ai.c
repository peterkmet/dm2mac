/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Creature AI: the update context, moods, plans, and the update of a creature that acts. */
#include "ai.h"
#include <stddef.h>
#include <string.h>
#include "ai_internal.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "game.h"
#include "interface_state.h"
#include "item.h"
#include "le.h"
#include "money.h"
#include "num.h"
#include "projectile.h"
#include "random.h"
#include "save.h"
#include "sensor.h"

/* The free item filter count at the start of an update (the bitmaps of one 0x100-byte scratch block of SKULL.EXE). */
#define FILTER_FREE_START 4

/* The sizes of the AI state of the groups and of the nested updates. */
#define AI_GROUP_KEY_COUNT 1024 /* group record indices */
#define SCRATCH_BLOCKS     4    /* nested creature updates that hold a scratch block */

/* The byte offsets of the fields of the AI table records. */
#define STEP_AT_COMMAND         0
#define STEP_AT_ON_DONE         1
#define STEP_AT_ON_FAIL         2
#define STEP_AT_ARG1            3
#define STEP_AT_ARG2            4
#define STEP_AT_SCAN            5
#define STEP_AT_SCAN_ARG        6
/* ... of a target record. */
#define TARGET_AT_KIND          0
#define TARGET_AT_CONDITION     1
#define TARGET_AT_CONDITION_ARG 2
#define TARGET_AT_ITEM          4
#define TARGET_AT_MASK_WORD     6
#define TARGET_AT_COST          8
#define TARGET_AT_VALUE         9
#define TARGET_AT_WANTED        10
#define TARGET_AT_EXTRA         11
#define TARGET_AT_SCAN          12
#define TARGET_AT_MORE          13
/* ... of a plan choice and of a mood entry. */
#define CHOICE_AT_PLAN          0
#define CHOICE_AT_CHANCE        1
#define CHOICE_AT_TARGETS       2
#define CHOICE_AT_MORE          6
#define MOOD_AT_MASK_WORD       0
#define MOOD_AT_CHOICES         2

/* The jump byte of an animation step. */
#define STEP_INTERRUPTIBLE 0x40 /* the step may end the animation at an interrupt */
#define STEP_ACTS          0x80 /* the creature acts at this step */

/* Game steps that a party action effect delays a creature. */
#define ACTION_DELAY_EFFECT 4

/* The power of the death cloud of a vanishing creature by its hit size. */
#define DEATH_CLOUD_POWER_SMALL  110 /* the death cloud of a creature of hit size 0 */
#define DEATH_CLOUD_POWER_MEDIUM 190

/* The mood entry mask of a mood entry. */
#define MOOD_GLOBAL_TEST 0xc000 /* mood entry mask: a global variable selects the entry */
#define MOOD_GLOBAL_MASK 0x3fff

/* Plan step directives and the next step values of a plan step. */
#define PLAN_STEP_DIRECTIVE_COUNTER (-10) /* plan step: sets a counter of the active group */
#define PLAN_NEXT_END               (-2)
#define PLAN_NEXT_FAIL              (-3)
#define PLAN_NEXT_FORWARD           (-5)
#define PLAN_NEXT_BACK              (-6)
#define PLAN_NEXT_STAY              (-7)
#define PLAN_NEXT_SKIP              (-8)

/* The scan byte of a plan step. */
#define PLAN_SCAN_KIND           0x1f
#define PLAN_SCAN_ONLY_PARTY_MAP 0x40
#define PLAN_SCAN_PARTY_MAP      0x60 /* scan flags 0x40 without 0x20: only on the party map */
#define PLAN_SCAN_ENDS           0x80 /* plan flags: a plan that ends when nothing is found */
#define PLAN_SCAN_FLAGS          0xe0

/* The plan of a creature that has nothing to do. */
#define AI_IDLE_PLAN 0x11

/* The plan steps that one creature update runs at most. */
#define AI_STEP_LIMIT 32

/* Mood bits that let the search flee. */
#define SEARCH_FLAGS_FLEE 0x2048

/* The attack skill bits of a creature that the candidates and the plan commands test. */
#define CANDIDATE_MASK_NO_ATTACK 0x08

/* Random bits that must all be clear for a mood change: the more bits, the rarer the change. */
#define ROLL_FAR_NO_PLAN 0x0030
#define ROLL_LAZY        0x0038
#define ROLL_FAR         0x0070
#define ROLL_HURRY_FEAR  0x0380
#define ROLL_HURRY       0x0f80
#define ROLL_RESTLESS    0x3000
#define ROLL_STAYS       0x8008
#define ROLL_QUICK       0x0003

/* The low bits of the group record that set how often a creature checks its fear: in 1 of 2, 6, 10, or 14 updates. */
#define FEAR_PERIOD_BITS 0x3

/* The mood changes by speed and by health. */
#define SPEED_MOOD_STEPS    16 /* speed bits 4-7: 15 changes the bored mood on each update */
#define FEAR_HEALTH_PERCENT 25

/* The scanner kinds of a plan step (PlanStep.scan bits 0-4). */
#define SCANNER_IDLE                0x00
#define SCANNER_TARGETS             0x01
#define SCANNER_ITEMS_SEEN          0x02
#define SCANNER_ITEMS_SEEN_2        0x03
#define SCANNER_ITEM_COUNTS         0x04
#define SCANNER_ITEM_COUNTS_2       0x05
#define SCANNER_ITEMS_SEEN_ATTACK   0x06
#define SCANNER_ITEMS_SEEN_ATTACK_2 0x07
#define SCANNER_WANDER_PATH         0x08
#define SCANNER_HOME                0x09
#define SCANNER_MINION              0x0a
#define SCANNER_FLEE_PATH           0x0b
#define SCANNER_CARRIED             0x0c
#define SCANNER_CARRIED_2           0x0d
#define SCANNER_WANDER              0x0e
#define SCANNER_ITEM_CLASS          0x0f
#define SCANNER_PARTY               0x10

/* The target kind that the item counts skip. */
#define TARGET_KIND_UNCOUNTED 0x08

/* The scan kinds of the target records that the scanners read. */
#define TARGET_SCAN_WANDER_PATH 0x05 /* the targets of a wander that keep the square that it found */
#define TARGET_SCAN_HOME        0x06
#define TARGET_MINION           0x07
#define TARGET_SCAN_CARRIED     0x0f
#define TARGET_SCAN_CARRIED_2   0x10
#define TARGET_SCAN_FLEE_PATH   0x12 /* ... of a flight */
#define TARGET_SCAN_ITEM_CLASS  0x15
#define TARGET_PARTY            0x16
/* The target records of the item scanners: the wanted items and the items seen, counted against them. */
#define TARGET_SCAN_WANTED      0x01
#define TARGET_SCAN_SEEN        0x02
#define TARGET_SCAN_WANTED_2    0x03
#define TARGET_SCAN_SEEN_2      0x04

/* The GDAT value of a map graphics set that keeps creatures from wandering. */
#define MAP_KEY_NO_WANDER      0x20 /* GDAT (8, map graphics set, 0x0b, 0x65) bit: creatures do not wander */
#define MAP_GRAPHICS_KEY_FLAGS 0x65

/* Random bits of the wander scanner. */
#define ROLL_WANDER_STOP  0x1f
#define ROLL_WANDER_START 0x3f /* random bits: a creature starts to wander in 1 of 64 updates */
/* The roll of a creature that wanders at random: 0 turns it, 1 to WANDER_ROLL_STEP moves it one square forward,
   a higher roll makes it stand. */
#define WANDER_ROLL_MASK  0x07
/* The highest wander roll that steps forward. */
#define WANDER_ROLL_STEP  3

/* The bytes of a count entry: the carried items and the items still wanted of each count. */
#define COUNT_AT_CARRIED 0
#define COUNT_AT_WANTED  1
#define COUNT_AT_EXTRA   2

/* The conditions of the target records. */
#define COND_ALWAYS               0x00
#define COND_SEES_PARTY_FRONT     0x01
#define COND_ON_PARTY_SQUARE      0x02
#define COND_CARRIES              0x03
#define COND_ALERT                0x04
#define COND_AT_HOME              0x05
#define COND_MOOD_BIT             0x06
#define COND_ON_PARTY_MAP         0x07
#define COND_PARTY_HOLDS          0x08
#define COND_PARTY_LEVEL          0x09
#define COND_PARTY_AT_PIT         0x0a
#define COND_DOOR_BEHIND          0x0b
#define COND_STATIC_AHEAD         0x0c
#define COND_AT_HOME_CARRIES      0x0d
#define COND_HEALTH_BELOW         0x0e
#define COND_FEW_OF_TYPE          0x0f
#define COND_ON_HOME_MAP          0x10
#define COND_MARKER               0x11
#define COND_ON_GAME_MAP          0x12
#define COND_MOOD_BIT_CLEAR_AWAY  0x13
#define COND_MOOD_BIT_ON_GAME_MAP 0x14
#define COND_AT_COUNTER           0x15
#define COND_SEES_PARTY           0x16
/* The bits of the condition byte. */
#define COND_PLAN_TEST            0x40 /* true at once when the plan is the current plan */
#define COND_MASK                 0x7f
#define COND_NOT                  0x80

/* The most groups of one creature type that the few-of-type condition allows. */
#define FEW_OF_TYPE_MAX 4

/* The hit size of the strongest death cloud and the path margin of a candidate. */
#define HIT_SIZE_LARGE  2 /* creature hit size that makes the strongest death cloud */
#define PATH_END_MARGIN 2 /* path steps that stay out of a candidate cost */

/* A step of a plan (7 bytes). */
typedef struct {
    int8_t command; /* PLAN_CMD_* (0x3f and up), a step index, or a directive (negative) */
    int8_t on_done; /* the next step when the command is done: index or PLAN_NEXT_* */
    int8_t on_fail; /* ... when it failed */
    int8_t arg1, arg2;
    uint8_t scan; /* bits 0-4 scanner kind, bits 5-7 PLAN_SCAN_* */
    int8_t scan_arg;
} PlanStep;

/* A count of wanted items over the count entries of a target list: its first entry, the items it wants, and the
   carried items counted against them. */
typedef struct {
    int8_t *first;
    int16_t want;
    int16_t used;
} WantCount;

/* A plan choice (7 bytes). */
typedef struct {
    int8_t plan, chance;
    uint32_t targets; /* address of the target records */
    uint8_t more;
} AiChoice;

/* The plan state and path of a group between its updates (image cache keys 0x20000000 and 0x30000000 | group
   in SKULL.EXE); the port never evicts them. */
typedef struct {
    bool present;
    AiPlanCache cache;
    int path_size;
    uint8_t path[AI_PATH_BYTES];
} GroupStore;

AiContext g_ai;
AiPartyView g_ai_party;
AiSquare g_ai_square;
DistanceMap g_party_distance = {.map = -1};
DistanceMap g_alt_distance = {.map = -1};

/* The AI state of this file. */
static struct {
    GroupStore groups[AI_GROUP_KEY_COUNT]; /* the plan state and path of each group */
    /* The scratch blocks of the updates in progress: item filter bitmaps (the 0x100-byte blocks of SKULL.EXE). */
    uint8_t scratch_blocks[SCRATCH_BLOCKS][FILTER_SLOTS * ITEM_CLASS_SET_SIZE];
    int scratch_depth;
} ai_state;

/* AI tables and helpers that several sections share. */

static const uint8_t *script(uint32_t addr)
{
    return g_ai_scripts + (addr - AI_SCRIPTS_BASE);
}

/* A step of a plan from the AI tables. */
static PlanStep read_plan_step(int plan, int step)
{
    const uint8_t *p = script(g_ai_plans[plan] + (uint32_t)(step * AI_PLAN_STEP_SIZE));
    return (PlanStep){(int8_t)p[STEP_AT_COMMAND], (int8_t)p[STEP_AT_ON_DONE], (int8_t)p[STEP_AT_ON_FAIL],
                      (int8_t)p[STEP_AT_ARG1],    (int8_t)p[STEP_AT_ARG2],    p[STEP_AT_SCAN],
                      (int8_t)p[STEP_AT_SCAN_ARG]};
}

AiTarget ai_read_target(uint32_t addr)
{
    const uint8_t *p = script(addr);
    return (AiTarget){(int8_t)p[TARGET_AT_KIND],
                      p[TARGET_AT_CONDITION],
                      (int16_t)le16(p + TARGET_AT_CONDITION_ARG),
                      le16(p + TARGET_AT_ITEM),
                      le16(p + TARGET_AT_MASK_WORD),
                      (int8_t)p[TARGET_AT_COST],
                      p[TARGET_AT_VALUE],
                      (int8_t)p[TARGET_AT_WANTED],
                      (int8_t)p[TARGET_AT_EXTRA],
                      (int8_t)p[TARGET_AT_SCAN],
                      p[TARGET_AT_MORE]};
}

/* A plan choice from the AI tables. */
static AiChoice read_choice(uint32_t addr)
{
    const uint8_t *p = script(addr);
    return (AiChoice){(int8_t)p[CHOICE_AT_PLAN], (int8_t)p[CHOICE_AT_CHANCE], le32(p + CHOICE_AT_TARGETS),
                      p[CHOICE_AT_MORE]};
}

static uint16_t read_mood_mask(uint32_t addr)
{
    return le16(script(addr) + MOOD_AT_MASK_WORD);
}

/* The plan choices of a mood entry. */
static uint32_t read_mood_choices(uint32_t addr)
{
    return le32(script(addr) + MOOD_AT_CHOICES);
}

/* The AI class of the creature, read once per update. */
static int load_class(void)
{
    if (g_ai.class_index == -1)
        g_ai.class_index = ai_group_class(g_ai.group_thing);
    return g_ai.class_index;
}

int16_t ai_manhattan(Location a, Location b)
{
    int16_t dx = (int16_t)(a.x - b.x);
    int16_t dy = (int16_t)(a.y - b.y);
    return (int16_t)(abs_int(dx) + abs_int(dy));
}

static AiCandidate *candidate_at(int i)
{
    return &g_ai.candidates[i];
}

int ai_facing(void)
{
    return group_dir(g_ai.group_thing);
}

AnimStepResult ai_set_animation(AnimStep step)
{
    g_ai.anim[0] = step.sequence;
    g_ai.anim[1] = step.state;
    if (step.frame)
        g_ai.anim_step = step.frame;
    return step.result;
}

/* Setup */

void ai_forget_all(void)
{
    memset(ai_state.groups, 0, sizeof ai_state.groups);
    memset(&g_ai, 0, sizeof g_ai);
    ai_state.scratch_depth = 0;
}

void ai_free_cached_data(uint16_t group)
{
    int g = thing_index(group) | thing_index(g_ai.group_thing); /* the original ORs both indices into the key */
    ai_state.groups[g].present = false;
    ai_state.groups[g].path_size = 0;
}

/* The update context */

void ai_set_map(int16_t map)
{
    if (map == g_current_map.map)
        return;
    dungeon_set_current_map(map);
    if (map == g_current_map.alt_view_map)
        g_ai_party = (AiPartyView){g_current_map.alt_view_x, g_current_map.alt_view_y, g_current_map.alt_view_map,
                                   g_current_map.alt_view_dir};
    else
        g_ai_party = (AiPartyView){(int16_t)g_game.party_x, (int16_t)g_game.party_y, (int16_t)g_game.party_map,
                                   (int16_t)g_game.party_dir};
}

bool ai_begin(uint16_t group, Location at, int type, AiContext *saved)
{
    int16_t map = g_current_map.map;
    g_current_map.map = CUR_MAP_NONE; /* ai_set_map reads the party position again */
    ai_set_map(map);
    g_ai_square.x = -1;
    bool restored = false;
    if (g_ai.active_flag) {
        *saved = g_ai;
        restored = true;
    }
    g_ai.active_flag = true;
    g_ai.prepared = false;
    g_ai.group_thing = group;
    g_ai.active = creature_active(group);
    if (!g_ai.active)
        return restored;
    g_ai.info = creature_group_info(group);
    g_ai.anim = creature_anim_words(group);
    g_ai.map = (uint8_t)map;
    g_ai.event = (TimelineEvent){.time_map = timeline_time_map(g_game.game_time, map)};
    g_ai.event.priority = group_creature(g_ai.group_thing);
    g_ai.event.type = (uint8_t)type;
    g_ai.event.data[EVENT_X] = (uint8_t)at.x;
    g_ai.event.data[EVENT_Y] = (uint8_t)at.y;
    g_ai.anim_step = NULL;
    g_ai.removed = false;
    g_ai.class_index = -1;
    if (type == EVENT_CREATURE_THINK) {
        g_ai.arg1 = 0;
        g_ai.arg2 = 0;
        g_ai.previous_action = g_ai.active->action == ACTIVE_ACTION_NONE ? 0 : g_ai.active->action;
        g_ai.active->destination = 0;
        g_ai.active->direction = 0;
        g_ai.active->cell = 0;
        g_ai.active->facing = 0;
        g_ai.active->sub = 0;
        g_ai.active->phase = 0;
        g_ai.active->mode = 0;
        g_ai.active->interrupt = 0;
        g_ai.active->action = ACTIVE_ACTION_NONE;
    }
    return restored;
}

void ai_end(bool restored, const AiContext *saved)
{
    if (!restored) {
        g_ai.active_flag = false;
        return;
    }
    g_ai = *saved;
}

uint8_t *ai_scratch_push(void)
{
    if (ai_state.scratch_depth >= SCRATCH_BLOCKS) /* fix: a port limit; the original has no fixed depth */
        os_fatal("creature updates nest deeper than %d scratch blocks", SCRATCH_BLOCKS);
    g_ai.scratch = ai_state.scratch_blocks[ai_state.scratch_depth++];
    g_ai.scratch_used = 0;
    return g_ai.scratch;
}

void ai_scratch_pop(void)
{
    g_ai.scratch = NULL;
    if (ai_state.scratch_depth)
        ai_state.scratch_depth--;
}

void ai_prepare(void)
{
    if (g_ai.prepared)
        return;
    g_ai.prepared = true;
    load_class();
    g_ai.move_flags = g_ai.info->movement;
    g_ai.attack_flags = g_ai.info->attack_skills;
    g_ai.door_flags = g_ai.info->door_skills;
    g_ai.item_flags = g_ai.info->item_skills;
    g_ai.actuator_flags = g_ai.info->actuator_skills;
    if (creature_group_jumps(g_ai.group_thing))
        g_ai.move_flags &= (uint16_t)~MOVE_FALLS;
    g_ai.counts_dirty = true;
    memset(g_ai.filters, 0, sizeof g_ai.filters);
    g_ai.scratch = NULL;
    g_ai.filter_free = FILTER_FREE_START;
    g_ai.plan_flags = 0;
    g_ai.marker_mask = (int16_t)creature_gdat_attr(group_creature(g_ai.group_thing), CREATURE_ATTR_MARKER_MASK);
    int16_t elapsed = (int8_t)((uint8_t)g_game.game_time - g_ai.active->last_action_time);
    if (elapsed < 0)
        elapsed = (int16_t)(elapsed + TIME_BYTE_WRAP);
    int16_t n = (int16_t)((NIBBLE_MASK - creature_think_speed(g_ai.info)) * 2);
    /* the original tests the address of rand_2bits instead of calling it: the extra delay always applies */
    g_ai.alert = (int16_t)(rand_below((uint16_t)(n + 1)) + (n >> 2)) <= elapsed;
    int g = thing_index(g_ai.group_thing);
    if (!ai_state.groups[g].present) {
        memset(&g_ai.cache, 0, sizeof g_ai.cache);
        g_ai.cache.kind = -1;
        g_ai.cache.found = FOUND_NONE;
    } else {
        g_ai.cache = ai_state.groups[g].cache;
    }
}

uint8_t *ai_store_path(int path_size)
{
    int g = thing_index(g_ai.group_thing);
    if (ai_state.groups[g].path_size < path_size)
        ai_state.groups[g].path_size = path_size;
    return ai_state.groups[g].path;
}

uint8_t *ai_find_path(void)
{
    int g = thing_index(g_ai.group_thing);
    return ai_state.groups[g].path_size ? ai_state.groups[g].path : NULL;
}

uint8_t ai_action_flag(int action)
{
    return action < 0 ? 0 : g_ai_action_flags[action]; /* action -1 reads the byte before the table: 0 */
}

/* Conditions of the target records. */

/* True when a wall square around the party has a reset wall sensor (WALL_CLICK_RESET). */
static bool party_beside_special_wall(void)
{
    for (int d = 0; d < DIR_COUNT; d++) {
        int16_t x = (int16_t)(g_ai_party.x + g_dir_dx[d]);
        int16_t y = (int16_t)(g_ai_party.y + g_dir_dy[d]);
        uint8_t square = dungeon_square_at((Location){x, y});
        if (square_type(square) || !(square & SQUARE_HAS_THINGS))
            continue;
        for (uint16_t t = dungeon_square_first_thing(g_current_map.map, x, y); t != THING_ENDOFLIST;
             t = dungeon_next_thing(t)) {
            int type = thing_type(t);
            if (type == THING_SENSOR && sensor_type(t) == WALL_CLICK_RESET)
                return true;
            if (type > THING_SENSOR)
                break;
        }
    }
    return false;
}

/* True when the party looks at the creature on (x, y): in front of it, or (not front_only) in range and in sight. */
static bool cond_sees_party(int16_t x, int16_t y, bool front_only, int16_t range)
{
    if (g_ai.map != g_ai_party.map || dungeon_direction_to(g_ai_party.x, g_ai_party.y, x, y) != g_ai_party.dir)
        return false;
    if (front_only)
        return true;
    int16_t d = ai_manhattan((Location){g_ai_party.x, g_ai_party.y}, (Location){x, y});
    if (d <= 1)
        return true;
    return d <= range && !g_ui.inventory &&
           ai_trace_line((Location){g_ai_party.x, g_ai_party.y}, (Location){x, y}, ai_square_is_obstacle) != 0;
}

/* True when a champion holds an item of FILTER_WEAPON_HANDS in a hand. */
static bool cond_party_holds_weapon(void)
{
    for (int cell = 0; cell < CELL_COUNT; cell++) {
        int ch = champ_get_index_in_cell(cell);
        if (ch == -1)
            continue;
        uint16_t hand = champion(ch)->slots[SLOT_ACTION_HAND];
        if (hand != THING_NONE && ai_thing_matches_filter(hand, FILTER_WEAPON_HANDS))
            return true;
        hand = champion(ch)->slots[SLOT_READY_HAND];
        if (hand != THING_NONE && ai_thing_matches_filter(hand, FILTER_WEAPON_HANDS))
            return true;
    }
    return false;
}

/* True when the party stands next to the creature on (x, y) with an open pit behind it. */
static bool cond_party_at_pit(int16_t x, int16_t y)
{
    if (g_ai.map != g_ai_party.map || (x == g_ai_party.x && y == g_ai_party.y))
        return false;
    if (ai_manhattan((Location){x, y}, (Location){g_ai_party.x, g_ai_party.y}) > 1)
        return false;
    int16_t d = dungeon_direction_to(x, y, g_ai_party.x, g_ai_party.y);
    uint8_t square =
        dungeon_square_at((Location){(int16_t)(g_ai_party.x + g_dir_dx[d]), (int16_t)(g_ai_party.y + g_dir_dy[d])});
    return square_type(square) == SQUARE_PIT && square & SQUARE_PIT_OPEN && !(square & SQUARE_FLAG_0);
}

/* True when the creature on (x, y) can close the door behind it. */
static bool cond_door_behind(int16_t x, int16_t y)
{
    int16_t d = dir_opposite(group_dir(g_ai.group_thing));
    if (!(g_ai.door_flags & DOOR_SKILL_CLOSE_BEHIND))
        return false;
    if (square_type(dungeon_square_at((Location){(int16_t)(x + g_dir_dx[d]), (int16_t)(y + g_dir_dy[d])})) !=
        SQUARE_DOOR)
        return false;
    return ai_try_door_action((AiTest){DOOR_MODE_CLOSE, false}, ai_location(x, y), AI_SQUARE_IN_DIR, d).passed;
}

/* True when a static creature stands in front of the creature on (x, y) and the party is beside a special wall. */
static bool cond_static_ahead(int16_t x, int16_t y)
{
    if (g_ai.map != g_ai_party.map)
        return false;
    int16_t d = group_dir(g_ai.group_thing);
    uint16_t g = creature_get_group_at((Location){(x + g_dir_dx[d]), (y + g_dir_dy[d])});
    return g != THING_NONE && creature_group_info(g)->flags & CREATURE_STATIC && party_beside_special_wall();
}

/* True when the current map has few groups of the creature type: fewer than the scout and carrier minions plus one,
   at most 4. */
static bool cond_few_of_type(int16_t creature_type)
{
    int16_t same = 0;
    int16_t others = 0;
    int map = g_current_map.map;
    for (int sx = 0; sx < dungeon_map_width(map); sx++)
        for (int sy = 0; sy < dungeon_map_height(map); sy++) {
            uint16_t g = creature_group_at((Position){map, sx, sy});
            if (g == THING_NONE)
                continue;
            int type = group_creature(g);
            if (type == creature_type)
                same++;
            else if (type == MINION_CARRIER || type == MINION_SCOUT)
                others++;
        }
    return same < (others + 1 < FEW_OF_TYPE_MAX ? others + 1 : FEW_OF_TYPE_MAX);
}

/* True when the location in plan counter index of the active group is the square (x, y) of the creature. */
static bool cond_at_counter(int16_t x, int16_t y, int16_t index)
{
    uint16_t at = 0; /* fix: the original reads any word of the active group for an argument out of range */
    if (index >= 0 && index < PLAN_COUNTER_COUNT)
        at = (uint16_t)g_ai.active->counters[index];
    return location_x(at) == x && location_y(at) == y && location_map(at) == g_ai.map;
}

/* True when the creature carries an item of filter. */
static bool cond_carries(int16_t filter)
{
    return ai_find_matching_item(filter, thing_word(g_ai.group_thing, GROUP_POSSESSIONS), CELL_ANY) != THING_ENDOFLIST;
}

/* True when the condition of a target record holds for the creature. */
static bool test_condition(int condition, int arg, int plan)
{
    uint8_t c = (uint8_t)condition;
    int16_t a = (int16_t)arg;
    if (c & COND_PLAN_TEST) {
        if ((int8_t)plan == g_ai.active->plan)
            return true;
        c &= ~COND_PLAN_TEST;
    }
    bool negate = c & COND_NOT;
    bool r = false;
    c &= COND_MASK;
    int16_t x = g_ai.event.data[EVENT_X];
    int16_t y = g_ai.event.data[EVENT_Y];
    uint16_t home = thing_word(g_ai.group_thing, GROUP_HOME);
    uint16_t mood = group_mood(g_ai.group_thing);
    bool at_home = g_ai.map == location_map(home) && x == location_x(home) && y == location_y(home);
    switch (c) {
    case COND_ALWAYS:
        r = true;
        break;
    case COND_SEES_PARTY_FRONT:
    case COND_SEES_PARTY:
        r = cond_sees_party(x, y, c == COND_SEES_PARTY_FRONT, a);
        break;
    case COND_ON_PARTY_SQUARE:
        r = g_ai.map == g_ai_party.map && x == g_ai_party.x && y == g_ai_party.y;
        break;
    case COND_CARRIES:
        r = cond_carries(a);
        break;
    case COND_ALERT:
        r = g_ai.alert;
        break;
    case COND_AT_HOME:
        r = at_home;
        break;
    case COND_AT_HOME_CARRIES:
        r = at_home && cond_carries(a);
        break;
    case COND_MOOD_BIT:
        r = (mood & (1 << (a & BIT_INDEX_MASK))) != 0;
        break;
    case COND_ON_PARTY_MAP:
        r = g_ai.map == g_ai_party.map;
        break;
    case COND_PARTY_HOLDS:
        r = cond_party_holds_weapon();
        break;
    case COND_PARTY_LEVEL:
        r = champ_get_party_level() >= a;
        break;
    case COND_PARTY_AT_PIT:
        r = cond_party_at_pit(x, y);
        break;
    case COND_DOOR_BEHIND:
        r = cond_door_behind(x, y);
        break;
    case COND_STATIC_AHEAD:
        r = cond_static_ahead(x, y);
        break;
    case COND_HEALTH_BELOW: {
        /* fix: the original divides by a maximum health of 0 */
        unsigned max = g_ai.info->max_health ? g_ai.info->max_health : 1;
        r = thing_word(g_ai.group_thing, GROUP_HEALTH) * (unsigned)PERCENT / max <= (unsigned)a;
        break;
    }
    case COND_FEW_OF_TYPE:
        r = cond_few_of_type(a);
        break;
    case COND_ON_HOME_MAP:
        r = g_ai.map == location_map(home);
        break;
    case COND_MARKER:
        ai_cache_square((Location){x, y});
        if (g_ai_square.square & SQUARE_HAS_THINGS)
            r = !ai_find_marker_by_id(a, (int16_t)thing_word(g_ai.group_thing, GROUP_ITEM_CLASS));
        break;
    case COND_ON_GAME_MAP:
        r = g_ai.map == g_game.party_map;
        break;
    case COND_MOOD_BIT_CLEAR_AWAY:
        r = !(mood & (1 << (a & BIT_INDEX_MASK))) && g_ai.map != g_game.party_map;
        break;
    case COND_MOOD_BIT_ON_GAME_MAP:
        r = mood & (1 << (a & BIT_INDEX_MASK)) && g_ai.map == g_game.party_map;
        break;
    case COND_AT_COUNTER:
        r = cond_at_counter(x, y, a);
        break;
    }
    if (negate)
        r = !r;
    return r;
}

/* Target scanners: they turn the target records of a plan into candidates for the path search. */

void ai_add_candidate(const TargetScan *scan, uint32_t record, const AiTarget *t, int8_t bonus, uint16_t marker)
{
    if (g_ai.candidate_count >= AI_CANDIDATE_MAX)
        return;
    int8_t cost = t->cost;
    if (g_ai.map != g_ai_party.map && cost > 0 && !(g_ai.info->flags & CREATURE_WANDERER)) {
        cost = (int8_t)(cost >> 2);
        bonus = (int8_t)(bonus >> 2);
    }
    int16_t c = (int16_t)(cost + bonus);
    if (c < -1)
        c = -1;
    if (c > INT8_MAX)
        c = INT8_MAX;
    if ((int8_t)c < 0)
        return;
    AiCandidate *d = candidate_at(g_ai.candidate_count++);
    d->plan = (int8_t)scan->plan;
    d->scan_arg = (int8_t)scan->arg;
    d->marker = marker;
    d->cost = (int8_t)c;
    d->value = t->value;
    d->kind = t->kind;
    d->item = t->item;
    d->mask = t->mask & g_ai.candidate_mask;
    d->scan_kind = (int8_t)scan->kind;
    d->targets = record;
}

/* Starts a new count of wanted items at the count entry p: the entries of the count before get what it still wants. */
static void start_want_count(WantCount *count, int8_t *p, int at, int8_t want)
{
    for (; count->first < p; count->first += AI_COUNT_ENTRY_SIZE)
        count->first[at] = (int8_t)(count->want - count->used);
    count->want = want;
    count->used = 0;
}

/* Counts n carried items against a count of wanted items; returns how many of them the count still wanted. */
static int16_t count_carried(WantCount *count, int16_t n)
{
    int16_t left = (int16_t)(count->want - count->used);
    count->used = (int16_t)(count->used + n < count->want ? count->used + n : count->want);
    return n < left ? n : left;
}

AiItemCounts *ai_get_item_counts(uint32_t targets, int scan)
{
    if (g_ai.counts_dirty) {
        memset(g_ai.item_counts, 0, sizeof g_ai.item_counts);
        g_ai.counts_dirty = false;
    }
    AiItemCounts *slot = &g_ai.item_counts[0];
    for (int i = 0; i < AI_COUNT_LISTS; i++) {
        AiItemCounts *e = &g_ai.item_counts[i];
        if (!e->targets)
            slot = e;
        else if (e->targets == targets && e->scan_kind == scan)
            return e;
    }
    slot->targets = targets;
    slot->scan_kind = (int8_t)scan;
    slot->total = 0;
    slot->wanted = 0;
    slot->extra = 0;
    int8_t *p = slot->counts;
    WantCount wanted = {p, 0, 0};
    WantCount extra = {p, 0, 0};
    for (int16_t i = 0; i < AI_COUNT_TARGETS;) {
        AiTarget t = ai_read_target(targets);
        if (t.scan == scan) {
            if (t.item != TARGET_NO_ITEM && t.kind != TARGET_KIND_UNCOUNTED) {
                if (t.wanted != -1) {
                    start_want_count(&wanted, p, COUNT_AT_WANTED, t.wanted);
                    slot->wanted = (int8_t)(slot->wanted + (int8_t)wanted.want);
                }
                if (t.extra != -1) {
                    start_want_count(&extra, p, COUNT_AT_EXTRA, t.extra);
                    slot->extra = (int8_t)(slot->extra + (int8_t)extra.want);
                }
                int16_t n = (int16_t)ai_count_matching_items(
                    (int16_t)t.item, thing_word(g_ai.group_thing, GROUP_POSSESSIONS), CELL_ANY, false, true);
                p[COUNT_AT_CARRIED] = (int8_t)n;
                slot->total = (int8_t)(slot->total + (int8_t)n);
                slot->wanted = (int8_t)(slot->wanted - (int8_t)count_carried(&wanted, n));
                slot->extra = (int8_t)(slot->extra - (int8_t)count_carried(&extra, n));
            }
            p += AI_COUNT_ENTRY_SIZE;
            i++;
        }
        if (!t.more)
            break;
        targets += AI_TARGET_SIZE;
    }
    /* as in the original: no flush of the last wanted and extra runs after the loop */
    return slot;
}

/* The target records of scan |arg| whose condition holds; a negative arg ignores their cost. */
static void scan_targets(int arg, int plan, uint32_t list, int8_t bonus, uint16_t marker)
{
    if (!list)
        return;
    bool ignore_cost = (int8_t)arg < 0;
    int kind = ignore_cost ? -(int8_t)arg : (int8_t)arg;
    TargetScan scan = {plan, kind, kind};
    for (;; list += AI_TARGET_SIZE) {
        AiTarget t = ai_read_target(list);
        if (t.scan == (int8_t)kind && test_condition(t.condition, t.condition_arg, plan)) {
            AiTarget copy = t;
            if (ignore_cost) {
                copy.cost = 0;
                copy.value = 0;
            }
            ai_add_candidate(&scan, list, &copy, bonus, marker);
        }
        if (!t.more)
            return;
    }
}

/* The items that the creature wants and sees: scans the seen kind of the first or second item set (ignoring costs
   for arg != 0) when it is alert. */
static void scan_items_seen(int arg, int plan, uint32_t list, bool second_set, bool need_attack)
{
    if (!list || !g_ai.alert)
        return;
    int n = second_set ? TARGET_SCAN_SEEN_2 : TARGET_SCAN_SEEN;
    AiItemCounts *c = ai_get_item_counts(list, second_set ? TARGET_SCAN_WANTED_2 : TARGET_SCAN_WANTED);
    if ((!need_attack || g_ai.attack_flags & CANDIDATE_MASK_NO_ATTACK) && c->total <= 0)
        g_ai.candidate_mask &= ~CANDIDATE_MASK_NO_ATTACK;
    if (!g_ai.attack_flags)
        return;
    scan_targets(arg ? -n : n, plan, list, 0, FOUND_NONE);
}

/* The items still wanted by count: candidates with a bonus by the count of each. */
static void scan_item_counts(int arg, int plan, uint32_t list, int n)
{
    if (!list)
        return;
    AiItemCounts *c = ai_get_item_counts(list, n);
    if (!c->extra)
        return;
    int8_t *p = c->counts;
    TargetScan scan = {plan, n, (int8_t)arg};
    for (;; list += AI_TARGET_SIZE) {
        AiTarget t = ai_read_target(list);
        if (t.scan == (int8_t)n) {
            if (t.item != TARGET_NO_ITEM && test_condition(t.condition, t.condition_arg, plan)) {
                int8_t bonus = (int8_t)-(t.cost < p[COUNT_AT_CARRIED] ? t.cost : p[COUNT_AT_CARRIED]);
                AiTarget copy = t;
                if (arg) {
                    copy.cost = 0;
                    copy.value = 0;
                    bonus = 0;
                }
                ai_add_candidate(&scan, list, &copy, bonus, FOUND_NONE);
            }
            p += AI_COUNT_ENTRY_SIZE;
        }
        if (!t.more)
            return;
    }
}

/* The targets of scan n whose items the creature carries (filters 0 and 1 of a target). */
static void scan_carried_kind(int arg, int plan, uint32_t list, int n)
{
    if (!list)
        return;
    TargetScan scan = {plan, n, (int8_t)arg};
    for (;; list += AI_TARGET_SIZE) {
        AiTarget t = ai_read_target(list);
        if (t.scan == (int8_t)n) {
            bool carried = true;
            if (t.item != TARGET_NO_ITEM && (t.mask == 0 || t.mask == 1))
                carried = ai_find_matching_item((int16_t)t.item, thing_word(g_ai.group_thing, GROUP_POSSESSIONS),
                                                CELL_ANY) != THING_ENDOFLIST;
            if (carried && test_condition(t.condition, t.condition_arg, plan)) {
                AiTarget copy = t;
                if (arg) {
                    copy.cost = 0;
                    copy.value = 0;
                }
                ai_add_candidate(&scan, list, &copy, 0, FOUND_NONE);
            }
        }
        if (!t.more)
            return;
    }
}

/* The targets of scan n by their condition; the target filter becomes the item class of the group. */
static void scan_by_condition(int arg, int plan, uint32_t list, int n)
{
    if (!list)
        return;
    TargetScan scan = {plan, n, (int8_t)arg};
    for (;; list += AI_TARGET_SIZE) {
        AiTarget t = ai_read_target(list);
        if (t.scan == (int8_t)n && test_condition(t.condition, t.condition_arg, plan)) {
            AiTarget copy = t;
            copy.mask = thing_word(g_ai.group_thing, GROUP_ITEM_CLASS);
            if (arg) {
                copy.cost = 0;
                copy.value = 0;
            }
            ai_add_candidate(&scan, list, &copy, 0, FOUND_NONE);
        }
        if (!t.more)
            return;
    }
}

/* A creature that waits or wanders: the idle target. */
static void scan_idle(int arg, int plan, uint32_t list)
{
    AiTarget t = ai_read_target(AI_TARGET_IDLE);
    t.kind = TARGET_ANY;
    TargetScan scan = {plan, 0, (int8_t)arg};
    ai_add_candidate(&scan, list, &t, 0, FOUND_NONE);
}

/* A creature that wanders now and then on maps that allow it. */
static void scan_random_wander(int arg, int plan, uint32_t list)
{
    uint16_t mood = group_mood(g_ai.group_thing);
    int set = dungeon_map_wallset(g_ai.map);
    if (gdat_entry_index(gdat_current(), CAT_WALLSET, set, GDAT_TYPE_VALUE, MAP_GRAPHICS_KEY_FLAGS) &
            MAP_KEY_NO_WANDER ||
        (!(int8_t)arg && mood & MOOD_WANDERS)) {
        group_set_mood(g_ai.group_thing, mood & ~MOOD_WANDERS);
        return;
    }
    if (!(int8_t)arg) {
        if (rand_next() & ROLL_WANDER_START)
            return;
        group_set_mood(g_ai.group_thing, mood | MOOD_WANDERS);
    } else if (!(rand_next() & ROLL_WANDER_STOP)) {
        group_set_mood(g_ai.group_thing, mood & ~MOOD_WANDERS);
    }
    AiTarget t = ai_read_target(AI_TARGET_IDLE);
    t.kind = TARGET_ANY;
    TargetScan scan = {plan, 0, (int8_t)arg};
    ai_add_candidate(&scan, list, &t, 0, FOUND_NONE);
}

/* Runs a scanner kind over a target list: each target that fits becomes a candidate. */
static void run_target_scanner(int kind, int arg, int plan, uint32_t targets)
{
    g_ai.candidate_mask = CANDIDATE_MASK_ALL;
    arg = (int8_t)arg;
    plan = (int8_t)plan;
    switch (kind & PLAN_SCAN_KIND) {
    case SCANNER_IDLE:
        scan_idle(arg, plan, targets);
        break;
    case SCANNER_TARGETS:
        scan_targets(arg, plan, targets, 0, FOUND_NONE);
        break;
    case SCANNER_ITEMS_SEEN:
        scan_items_seen(arg, plan, targets, false, false);
        break;
    case SCANNER_ITEMS_SEEN_2:
        scan_items_seen(arg, plan, targets, true, false);
        break;
    case SCANNER_ITEM_COUNTS:
        scan_item_counts(arg, plan, targets, TARGET_SCAN_WANTED);
        break;
    case SCANNER_ITEM_COUNTS_2:
        scan_item_counts(arg, plan, targets, TARGET_SCAN_WANTED_2);
        break;
    case SCANNER_ITEMS_SEEN_ATTACK:
        scan_items_seen(arg, plan, targets, false, true);
        break;
    case SCANNER_ITEMS_SEEN_ATTACK_2:
        scan_items_seen(arg, plan, targets, true, true);
        break;
    case SCANNER_WANDER_PATH:
        scan_targets(TARGET_SCAN_WANDER_PATH, plan, targets, 0,
                     g_ai.cache.kind == TARGET_WANDER ? g_ai.cache.found : FOUND_NONE);
        break;
    case SCANNER_HOME: {
        uint16_t home = thing_word(g_ai.group_thing, GROUP_HOME);
        if (arg && g_ai.event.data[EVENT_X] == location_x(home) && g_ai.event.data[EVENT_Y] == location_y(home) &&
            g_ai.map == location_map(home))
            break;
        scan_targets(TARGET_SCAN_HOME, plan, targets, 0, FOUND_NONE);
        break;
    }
    case SCANNER_MINION: {
        uint16_t p = item_find_link_projectile(g_ai.group_thing, THING_NONE);
        if (p != THING_NONE && thing_type(thing_word(p, PROJECTILE_PAYLOAD)) == THING_CONTAINER)
            scan_targets(TARGET_MINION, plan, targets, 0,
                         thing_word(thing_word(p, PROJECTILE_PAYLOAD), MINION_MAP_LOCATION));
        break;
    }
    case SCANNER_FLEE_PATH:
        scan_targets(TARGET_SCAN_FLEE_PATH, plan, targets, 0,
                     g_ai.cache.kind == TARGET_FLEE ? g_ai.cache.found : FOUND_NONE);
        break;
    case SCANNER_CARRIED:
        scan_carried_kind(arg, plan, targets, TARGET_SCAN_CARRIED);
        break;
    case SCANNER_CARRIED_2:
        scan_carried_kind(arg, plan, targets, TARGET_SCAN_CARRIED_2);
        break;
    case SCANNER_WANDER:
        scan_random_wander(arg, plan, targets);
        break;
    case SCANNER_ITEM_CLASS:
        scan_by_condition(arg, plan, targets, TARGET_SCAN_ITEM_CLASS);
        break;
    case SCANNER_PARTY:
        scan_targets(TARGET_PARTY, plan, targets, 0,
                     (uint16_t)location_pack(g_ai_party.x, g_ai_party.y, g_ai_party.map));
        break;
    }
}

/* Moods and plans. */

/* The mood bits of the creature after their random changes: far, hurry, bored, lazy, restless, quick, stays, fear. */
static uint16_t change_mood(uint16_t mood)
{
    ActiveGroup *a = g_ai.active;
    uint32_t r = rand_next();
    if (g_ai.map == g_ai_party.map) {
        mood &= ~MOOD_FAR;
    } else if (!((uint16_t)r & (a->plan == -1 ? ROLL_FAR_NO_PLAN : ROLL_FAR))) {
        mood = (uint16_t)((mood | MOOD_FAR) & ~MOOD_HURRY);
    }
    if (!(mood & MOOD_FAR) && !((uint16_t)r & (mood & MOOD_FEAR ? ROLL_HURRY_FEAR : ROLL_HURRY)))
        mood ^= MOOD_HURRY;
    if (!((uint16_t)r % (SPEED_MOOD_STEPS - creature_fear_resistance(g_ai.info))))
        mood &= ~MOOD_BORED;
    if (g_ai_class_flags[g_ai.class_index] & AI_CLASS_LAZY_GUARD)
        mood |= MOOD_LAZY;
    else if (!(r & ROLL_LAZY))
        mood &= ~MOOD_LAZY;
    if (!(r & ROLL_RESTLESS))
        mood &= ~MOOD_RESTLESS;
    if (!(r & ROLL_QUICK))
        mood &= ~MOOD_QUICK;
    if (!(r & ROLL_STAYS))
        mood &= ~MOOD_STAYS;
    int n = 2;
    if (!(g_ai_class_flags[g_ai.class_index] & AI_CLASS_ONE_FEAR))
        n = (((g_ai.group_thing & FEAR_PERIOD_BITS) + 1) * 2 - 1) * 2;
    if (!rand_below((uint16_t)n)) {
        int max = (int16_t)g_ai.info->max_health < 1 ? 1 : (int16_t)g_ai.info->max_health;
        if (thing_word(g_ai.group_thing, GROUP_HEALTH) * (unsigned)PERCENT / (unsigned)max < FEAR_HEALTH_PERCENT)
            mood |= MOOD_FEAR;
        else
            mood &= ~MOOD_FEAR;
    }
    return mood;
}

/* The mood entry of the creature: its mood bits change at random, then the first entry that fits them. */
static int update_mood(uint32_t entries)
{
    ActiveGroup *a = g_ai.active;
    uint16_t mood = change_mood(group_mood(g_ai.group_thing));
    group_set_mood(g_ai.group_thing, mood & ~GROUP_SEEN);
    int16_t any = -1;
    int16_t all = -1;
    int16_t i = 0;
    uint16_t mask;
    for (;; i++) {
        mask = read_mood_mask(entries + (uint32_t)(i * AI_MOOD_ENTRY_SIZE));
        if (!mask)
            break;
        if ((mask & MOOD_GLOBAL_TEST) == MOOD_GLOBAL_TEST) {
            if (game_global_var(mask & MOOD_GLOBAL_MASK))
                break;
            continue;
        }
        if (any == -1 && mask & mood)
            any = i;
        if (all == -1 && (mask & mood) == mask)
            all = i;
        if (mask == mood)
            break;
    }
    if (!mask && (all != -1 || any != -1))
        i = all != -1 ? all : any;
    if ((int8_t)i != a->behavior) {
        g_ai.cache.path_left = 0;
        a->plan = -1;
        a->plan_step = 0;
        g_ai.cache.kind = -1;
    }
    a->behavior = (int8_t)i;
    return (int8_t)i;
}

/* The mood entry of the creature class. */
static void select_behavior(void)
{
    load_class();
    g_ai.mood = (int16_t)update_mood(g_ai_behaviors[g_ai.class_index]);
    g_ai.mood_entry = g_ai_behaviors[g_ai.class_index] + (uint32_t)(g_ai.mood * AI_MOOD_ENTRY_SIZE);
}

/* The plan state of a found candidate: its path, kind, and target for the next updates. */
static void load_candidate_path(const AiCandidate *c)
{
    int length = c->path_length < 0 ? 0 : c->path_length;
    g_ai.cache.path_length = (uint8_t)length;
    g_ai.cache.path_left = (uint8_t)length;
    g_ai.cache.found = c->found;
    g_ai.cache.kind = c->kind;
    g_ai.cache.item = c->item;
    g_ai.cache.mask = c->mask;
    g_ai.cache.scan_kind = c->scan_kind;
    g_ai.cache.targets = c->targets;
    uint8_t *path = NULL;
    if (length > 0) {
        path = ai_store_path(length * AI_PATH_STEP_SIZE);
        memcpy(path, g_ai.path, (size_t)length * AI_PATH_STEP_SIZE);
    }
    g_ai.cached_path = path;
}

/* The current plan goes on when its targets are still there: returns the plan, or -1. */
static int continue_plan(void)
{
    ActiveGroup *a = g_ai.active;
    bool found = false;
    int plan = -1;
    if (g_ai.cache.path_length && g_ai.cache.path_left && g_ai.cache.kind != -1) {
        g_ai.candidate_count = 0;
        if (a->plan != -1) {
            plan = a->plan;
            PlanStep step = read_plan_step(plan, a->plan_step);
            run_target_scanner(step.scan, step.scan_arg, plan, g_ai.cache.targets);
        }
        if (g_ai.candidate_count) {
            found = ai_plan_path();
            if (found)
                a->destination = g_ai.candidates[0].destination;
        }
    }
    return found ? plan : -1;
}

/* Halves the costs of the candidates and removes those without a cost. */
static void compact_candidates(void)
{
    int16_t count = (int8_t)g_ai.candidate_count;
    if (!count)
        return;
    for (int i = 0; i < count; i++) {
        AiCandidate *c = candidate_at(i);
        if (c->cost > 0) {
            int16_t half = (int16_t)(c->cost >> 1);
            int16_t limit = (int16_t)(g_ai.cache.path_length - PATH_END_MARGIN);
            c->cost = (int8_t)(half < limit ? half : limit);
        }
    }
    for (int16_t i = 0; i < count; i++) {
        if (candidate_at(i)->cost >= 0)
            continue;
        int16_t j = (int16_t)(i + 1);
        while (j < count && candidate_at(j)->cost < 0)
            j++;
        if (j >= count) {
            count = (int16_t)(i + 1);
        } else {
            memmove(candidate_at(i), candidate_at(j), (size_t)(count - j) * sizeof(AiCandidate));
            count = (int16_t)(count - (j - i));
        }
    }
    if (candidate_at(count - 1)->cost < 0)
        count--;
    g_ai.candidate_count = (uint8_t)count;
}

/* The plans of the mood entry whose chance comes up scan for their targets; the current plan always. */
static void scan_plan_list(uint32_t choices, int plan, int step, bool continuing)
{
    for (;;) {
        AiChoice c = read_choice(choices);
        bool stop = false;
        bool run = false;
        int at = 0;
        if (c.plan == plan) {
            stop = continuing || g_ai.plan_flags;
            run = true;
            at = (int8_t)step;
        } else if (!g_ai.plan_flags) {
            if (!c.chance)
                run = true;
            else if (c.chance > 0)
                run = !rand_below((uint16_t)c.chance);
            else
                run = rand_below((uint16_t)-c.chance) != 0;
        }
        if (run) {
            PlanStep s = read_plan_step(c.plan, at);
            run_target_scanner(s.scan, s.scan_arg, c.plan, c.targets);
        }
        if (stop || !c.more)
            return;
        choices += AI_CHOICE_SIZE;
    }
}

/* The scan flags of the current plan step; 0 when the step applies only on the party map and the creature is away. */
static int step_scan_flags(void)
{
    ActiveGroup *a = g_ai.active;
    if (a->plan == -1)
        return 0;
    uint8_t scan = read_plan_step(a->plan, a->plan_step).scan;
    if ((scan & PLAN_SCAN_PARTY_MAP) == PLAN_SCAN_ONLY_PARTY_MAP && g_ai.map != g_ai_party.map)
        return 0;
    return scan & PLAN_SCAN_FLAGS;
}

/* The plan of the next update: the plan that goes on, or the nearest candidate of a scan; AI_IDLE_PLAN when there
   is none; -1 when the plan stays; AI_RESULT_FAIL when the plan ends. */
static int choose_plan(void)
{
    ActiveGroup *a = g_ai.active;
    bool continuing = continue_plan() != -1;
    int chosen = -1;
    if (g_ai_class_flags[g_ai.class_index] & AI_CLASS_ALWAYS_SCANS || !continuing || !rand_2bits()) {
        g_ai.candidate_count = 0;
        g_ai.plan_flags = (uint8_t)step_scan_flags();
        scan_plan_list(read_mood_choices(g_ai.mood_entry), a->plan, a->plan_step, continuing);
        if (continuing)
            compact_candidates();
        if (g_ai.candidate_count) {
            uint16_t mood = group_mood(g_ai.group_thing);
            int flags = mood & SEARCH_FLAGS_FLEE ? g_ai.search_flags : 0;
            if (mood & MOOD_STAYS)
                flags |= AI_MOVE_ATTACK_GROUPS;
            int i = ai_search_area(ai_group_square(), (uint8_t)flags, (int8_t)g_ai.candidate_count, g_ai.candidates,
                                   g_ai.path);
            g_ai.chosen = (int8_t)i;
            if ((int16_t)i != -1) {
                chosen = candidate_at(i)->plan;
                a->destination = candidate_at(i)->destination;
                load_candidate_path(candidate_at(i));
            } else if (g_ai.plan_flags) {
                return AI_RESULT_FAIL;
            }
        }
    }
    if ((int8_t)chosen == -1 && continuing)
        return -1;
    if ((int8_t)chosen == -1) {
        g_ai.cache.path_left = 0;
        g_ai.cache.kind = -1;
        return AI_IDLE_PLAN;
    }
    if (a->plan != -1) {
        if (a->plan == (int8_t)chosen)
            return -1;
        a->plan = -1;
        a->plan_step = 0;
    }
    return (int8_t)chosen;
}

/* The command of the current plan step; directives before it set the plan counters. */
static int fetch_plan_step(void)
{
    ActiveGroup *a = g_ai.active;
    int8_t step = a->plan_step;
    PlanStep s;
    for (;; step++) {
        s = read_plan_step(a->plan, step);
        if (s.command >= 0)
            break;
        if (s.command == PLAN_STEP_DIRECTIVE_COUNTER && s.on_done >= 0 && s.on_done < PLAN_COUNTER_COUNT)
            a->counters[(int)s.on_done] = s.on_fail;
    }
    a->plan_step = step;
    g_ai.arg1 = s.arg1;
    g_ai.arg2 = s.arg2;
    return s.command;
}

/* The next plan step after a command result; true when the step changed. */
static bool advance_plan_step(int result)
{
    ActiveGroup *a = g_ai.active;
    int8_t step = a->plan_step;
    PlanStep s = read_plan_step(a->plan, step);
    int8_t next = (int8_t)result == (int8_t)AI_RESULT_DONE ? s.on_done : s.on_fail;
    if (next == PLAN_NEXT_END || next == PLAN_NEXT_FAIL) {
        a->plan = -1;
        a->plan_step = 0;
        return true;
    }
    bool changed = false;
    if (next <= PLAN_NEXT_FORWARD && next >= PLAN_NEXT_SKIP) {
        if (next != PLAN_NEXT_STAY) {
            if (next == PLAN_NEXT_SKIP)
                step = (int8_t)(step + 2);
            else
                step = (int8_t)(step + (next == PLAN_NEXT_FORWARD ? 1 : -1));
            changed = true;
        }
    } else {
        changed = step != next;
        step = next;
    }
    a->plan_step = step;
    return changed;
}

/* Is the next step of a plan a step of the same scan as the first one: the plan goes on in this update. */
static bool plan_scan_goes_on(int first_kind, int first_arg, const PlanStep *s)
{
    int k = s->scan & PLAN_SCAN_KIND;
    if (first_kind == k && k != SCANNER_TARGETS)
        return true;
    if (k == SCANNER_IDLE)
        return true;
    return first_kind == SCANNER_TARGETS && k == SCANNER_TARGETS && s->scan_arg == first_arg;
}

/* A creature that wanders: a step forward, a turn, or a wait. */
static void wander_at_random(void)
{
    ActiveGroup *a = g_ai.active;
    int r = rand_next() & WANDER_ROLL_MASK;
    if (r > WANDER_ROLL_STEP) {
        a->action = ACTION_NONE;
        return;
    }
    if (!r) {
        ai_turn_toward(g_game.game_time & DIR_MASK);
        return;
    }
    int16_t dir = group_dir(g_ai.group_thing);
    a->direction = (uint8_t)dir;
    int x = (g_ai.event.data[EVENT_X] + g_dir_dx[dir]) & LOCATION_COORD_MASK;
    int y = (g_ai.event.data[EVENT_Y] + g_dir_dy[dir]) & LOCATION_COORD_MASK;
    a->destination = (uint16_t)location_set_xy(a->destination, x, y);
    int type = square_type(dungeon_square_at((Location){x, y}));
    if (type == SQUARE_WALL || type == SQUARE_UNKNOWN) {
        a->action = ACTION_NONE;
        return;
    }
    if (type == SQUARE_TELEPORTER) {
        PassageEnd end;
        if (dungeon_linked_teleporter_dest(&end, (Location){x, y}) &&
            !creature_is_native_to_map(g_ai.group_thing, end.map)) {
            a->action = ACTION_NONE;
            return;
        }
    }
    a->action = ACTION_MOVE_NEAR;
}

/* Runs the steps of the chosen plan (or of the current plan) while they scan as its first step does. */
static void run_plan(int chosen)
{
    ActiveGroup *a = g_ai.active;
    int plan, step;
    if (chosen >= 0 && chosen < AI_PLAN_COUNT) {
        a->plan = (int8_t)chosen;
        plan = a->plan;
        a->plan_step = 0;
        step = 0;
    } else {
        plan = a->plan;
        step = a->plan_step;
    }
    PlanStep first = read_plan_step(plan, step);
    int first_kind = first.scan & PLAN_SCAN_KIND;
    int first_arg = first.scan_arg;
    bool changed = false;
    int n = 0;
    do {
        if (changed) {
            PlanStep s = read_plan_step(a->plan, a->plan_step);
            if (!plan_scan_goes_on(first_kind, first_arg, &s)) {
                a->action = ACTION_RESCAN;
                break;
            }
        }
        int8_t r = (int8_t)ai_exec_plan_command(fetch_plan_step());
        if (r != (int8_t)AI_RESULT_ISSUED)
            changed |= advance_plan_step(r);
        if (changed)
            g_ai.cache.path_left = 0;
        if (n++ > AI_STEP_LIMIT)
            a->action = ACTION_NONE;
        if (a->action == ACTIVE_ACTION_NONE && r != (int8_t)AI_RESULT_ISSUED && a->plan == -1)
            a->action = ACTION_NONE;
    } while (a->action == ACTIVE_ACTION_NONE);
}

/* The creature chooses what to do next: wander, flee, or follow the best plan. */
static void decide(void)
{
    ActiveGroup *a = g_ai.active;
    select_behavior();
    uint32_t class_flags = g_ai_class_flags[g_ai.class_index];
    g_ai.search_flags = class_flags & AI_CLASS_NO_FLEE       ? MOVE_KIND_PLAIN
                        : class_flags & AI_CLASS_SEARCH_NEAR ? MOVE_KIND_SAFE
                                                             : MOVE_KIND_SAFE_FLEE;
    uint32_t choices = read_mood_choices(g_ai.mood_entry);
    if (choices == AI_CHOICES_NONE) {
        a->action = ACTION_NONE;
        return;
    }
    if (choices == AI_CHOICES_WANDER) {
        wander_at_random();
        return;
    }
    ai_prepare();
    ai_scratch_push();
    if (ai_try_flee(class_flags)) {
        ai_scratch_pop();
        return;
    }
    a->action = ACTIVE_ACTION_NONE;
    int chosen = (int8_t)choose_plan();
    if (chosen != (int8_t)AI_RESULT_FAIL) {
        run_plan(chosen);
    } else if (!(g_ai.plan_flags & PLAN_SCAN_ENDS)) {
        a->action = ACTION_NONE;
    } else {
        if (advance_plan_step(AI_RESULT_FAIL))
            g_ai.cache.path_left = 0;
        a->action = ACTION_RESCAN;
    }
    ai_scratch_pop();
}

/* The update of a creature that acts */

/* The action that the think event starts: the planned next action, or a new decision. */
static void take_next_action(void)
{
    ActiveGroup *a = g_ai.active;
    a->action = (uint8_t)a->next_action;
    if (a->next_action == -1) {
        decide();
        return;
    }
    a->next_action = -1;
    if (step_scan_flags() && (int8_t)a->action != ACTION_VANISH) {
        a->action = ACTIVE_ACTION_NONE;
        return;
    }
    if (ai_action_flag((int8_t)a->action) & AI_ACTION_FLAG_PATH) {
        g_ai.cache.path_left = 0;
        g_ai.cache.kind = -1;
    }
    int16_t dir = group_dir(g_ai.group_thing);
    if (a->action == ACTION_TURN_LEFT)
        a->facing = (uint8_t)dir_turn(dir, TURN_LEFT);
    else if (a->action == ACTION_TURN_RIGHT)
        a->facing = (uint8_t)dir_turn(dir, TURN_RIGHT);
}

/* The creature starts to vanish: it keeps its animation and leaves a death cloud by its size. */
static void start_vanish(void)
{
    ActiveGroup *a = g_ai.active;
    a->counters[0] = (int16_t)g_ai.anim[0];
    a->counters[1] = (int16_t)g_ai.anim[1];
    if (g_ai_class_flags[ai_group_class(g_ai.group_thing)] & AI_CLASS_NO_DEATH_CLOUD)
        return;
    int size = g_ai.info->hit_size; /* the power of the death cloud by size */
    explosion_create(EXPLOSION_DEATH_CLOUD,
                     size == 1                ? DEATH_CLOUD_POWER_MEDIUM
                     : size == HIT_SIZE_LARGE ? POWER_MAX
                                              : DEATH_CLOUD_POWER_SMALL,
                     g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y], EXPLOSION_AT_CENTER);
}

/* Runs the animation steps of the action: the creature acts at its acting steps; returns the last step result. */
static int run_animation(bool think, int r)
{
    ActiveGroup *a = g_ai.active;
    for (;;) {
        if ((think || !a->interrupt || !(g_ai.anim_step[STEP_JUMP] & STEP_INTERRUPTIBLE)) &&
            g_ai.anim_step[STEP_JUMP] & STEP_ACTS) {
            ai_prepare();
            a->interrupt |= (uint8_t)ai_execute_action();
        }
        if (!a->interrupt || !(g_ai.anim_step[STEP_JUMP] & STEP_INTERRUPTIBLE))
            break;
        r = ai_set_animation(creature_anim_next_step(group_creature(g_ai.group_thing), g_ai.anim[0], g_ai.anim[1]));
        if ((uint16_t)r == ANIM_SEQUENCE_END)
            break;
    }
    return r;
}

/* The next action and its animation; false when the group left the dungeon. */
static bool run_next_action(void)
{
    ActiveGroup *a = g_ai.active;
    uint16_t *anim = g_ai.anim;
    bool think = g_ai.event.type == EVENT_CREATURE_THINK;
    int r;
    if (think) {
        take_next_action();
        if ((int8_t)a->action == ACTION_VANISH)
            start_vanish();
        int8_t action = (int8_t)a->action;
        if (action >= ACTION_WAIT_FIRST && action <= ACTION_WAIT_LAST) {
            g_ai.event.time_map =
                timeline_time_map((uint32_t)(action - ACTION_WAIT_FIRST) + g_game.game_time, g_ai.map);
            return true;
        }
        r = ai_set_animation(creature_anim_start(group_creature(g_ai.group_thing), action, 0));
    } else {
        r = ai_set_animation(creature_anim_next_random_step(group_creature(g_ai.group_thing), anim[0], anim[1]));
    }
    r = run_animation(think, r);
    g_ai.event.type = (uint16_t)r != ANIM_MORE_STEPS ? EVENT_CREATURE_THINK : EVENT_CREATURE_ANIMATION;
    if (g_ai.removed)
        return false;
    g_ai.event.time_map = timeline_time_map(creature_animate_step(), g_ai.map);
    return true;
}

/* Keeps the plan state of the group for its next update. */
static void store_plan_cache(void)
{
    int g = thing_index(g_ai.group_thing);
    ai_state.groups[g].present = true;
    ai_state.groups[g].cache = g_ai.cache;
}

void ai_update_group(void)
{
    ActiveGroup *a = g_ai.active;
    if (g_game.magic_light[MAGIC_ACTION_EFFECT] && !(g_ai.info->flags & CREATURE_FAST_VANISH) &&
        a->action != ACTION_VANISH && a->next_action != ACTION_VANISH) {
        timeline_delay_event(&g_ai.event, ACTION_DELAY_EFFECT);
    } else if (!run_next_action()) {
        return;
    }
    if (g_ai.prepared && (g_ai.cache.path_length || g_ai.cache.kind != -1))
        store_plan_cache();
    if (g_ai.active->event != EVENT_NO_SLOT)
        creature_cancel_update(g_ai.group_thing);
    g_ai.active->event = (int16_t)timeline_add_event(&g_ai.event);
}
