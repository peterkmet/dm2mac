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
#include "item.h"
#include "le.h"
#include "merchant.h"
#include "money.h"
#include "move.h"
#include "num.h"
#include "projectile.h"
#include "random.h"
#include "save.h"
#include "sensor.h"
#include "sound_request.h"

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

/* The filter words of a target record that filter nothing. */
#define TARGET_NO_ITEM     0xffff /* a target record without an item filter */
#define CANDIDATE_MASK_ALL 0xffff

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
#define ATTACK_SKILL_ITEMS       0x08

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
#define TARGET_SCAN_SEEK_ITEMS  0x11
#define TARGET_SCAN_FLEE_PATH   0x12 /* ... of a flight */
#define TARGET_SCAN_MOOD_SET    0x13 /* the target records that set mood bits */
#define TARGET_SCAN_MOOD_CLEAR  0x14 /* ... that clear them */
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

/* Plan command arguments: any item (a possession test, a drop), and the front group (a test of its item). */
#define PLAN_ARG_ANY_ITEM    (-2)
#define PLAN_ARG_FRONT_GROUP 2
#define STAND_VARIANT_LAST   2 /* the last of the three stands of cmd_stand_by_mood */
#define HIT_SIZE_LARGE       2 /* creature hit size that makes the strongest death cloud */
#define PATH_END_MARGIN      2 /* path steps that stay out of a candidate cost */

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

/* A step of a plan (7 bytes). */
typedef struct {
    int8_t command; /* PLAN_CMD_* (0x3f and up), a step index, or a directive (negative) */
    int8_t on_done; /* the next step when the command is done: index or PLAN_NEXT_* */
    int8_t on_fail; /* ... when it failed */
    int8_t arg1, arg2;
    uint8_t scan; /* bits 0-4 scanner kind, bits 5-7 PLAN_SCAN_* */
    int8_t scan_arg;
} PlanStep;

/* A target record (14 bytes): what a scanner looks for. */
typedef struct {
    int8_t kind;       /* candidate kind */
    uint8_t condition; /* COND_*, bit 6 plan test, bit 7 negation */
    int16_t condition_arg;
    uint16_t item; /* item filter or marker */
    uint16_t mask;
    int8_t cost;
    uint8_t value;
    int8_t wanted; /* the items of the record that the creature wants; -1: the count before goes on */
    int8_t extra;  /* ... that it wants on top of them */
    int8_t scan;   /* the scanner kind that uses the record */
    uint8_t more;  /* 0 for the last record of a list */
} AiTarget;

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

/* The scan that adds candidates: its plan, the scan kind and the scan argument that the candidates keep. */
typedef struct {
    int plan;
    int kind;
    int arg;
} TargetScan;

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
    int16_t dest_x, dest_y; /* the target square of the action that runs */
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

/* A target record from the AI tables. */
static AiTarget read_target(uint32_t addr)
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

/* The distance between two squares along the axes. */
static int manhattan(int x1, int y1, int x2, int y2)
{
    int16_t dx = (int16_t)(x1 - x2);
    int16_t dy = (int16_t)(y1 - y2);
    return (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
}

static AiCandidate *candidate_at(int i)
{
    return &g_ai.candidates[i];
}

static int facing(void)
{
    return group_dir(g_ai.group_thing);
}

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

/* Keeps an animation step as the animation of the group; returns how the animation goes on. */
static AnimStepResult set_animation(AnimStep step)
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
    if (map == g_party.cur_map)
        return;
    dun_set_current_map(map);
    if (map == g_party.alt_view_map)
        g_ai_party = (AiPartyView){g_party.alt_view_x, g_party.alt_view_y, g_party.alt_view_map, g_party.alt_view_dir};
    else
        g_ai_party = (AiPartyView){(int16_t)g_game.party_x, (int16_t)g_game.party_y, (int16_t)g_game.party_map,
                                   (int16_t)g_game.party_dir};
}

bool ai_begin(uint16_t group, int16_t x, int16_t y, int type, AiContext *saved)
{
    int map = g_party.cur_map;
    g_party.cur_map = CUR_MAP_NONE; /* ai_set_map reads the party position again */
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
    g_ai.event.data[EVENT_X] = (uint8_t)x;
    g_ai.event.data[EVENT_Y] = (uint8_t)y;
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
        int x = g_ai_party.x + g_dir_dx[d];
        int y = g_ai_party.y + g_dir_dy[d];
        uint8_t square = dun_get_square((Location){x, y});
        if (square_type(square) || !(square & SQUARE_HAS_THINGS))
            continue;
        for (uint16_t t = dungeon_square_first_thing(g_party.cur_map, x, y); t != THING_ENDOFLIST;
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
    if (g_ai.map != g_ai_party.map || dun_get_direction_to(g_ai_party.x, g_ai_party.y, x, y) != g_ai_party.dir)
        return false;
    if (front_only)
        return true;
    int16_t d = (int16_t)manhattan(g_ai_party.x, g_ai_party.y, x, y);
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
    if (manhattan(x, y, g_ai_party.x, g_ai_party.y) > 1)
        return false;
    int d = dun_get_direction_to(x, y, g_ai_party.x, g_ai_party.y);
    uint8_t square =
        dun_get_square((Location){(int16_t)(g_ai_party.x + g_dir_dx[d]), (int16_t)(g_ai_party.y + g_dir_dy[d])});
    return square_type(square) == SQUARE_PIT && square & SQUARE_PIT_OPEN && !(square & SQUARE_FLAG_0);
}

/* True when the creature on (x, y) can close the door behind it. */
static bool cond_door_behind(int16_t x, int16_t y)
{
    int d = dir_opposite(group_dir(g_ai.group_thing));
    if (!(g_ai.door_flags & DOOR_SKILL_CLOSE_BEHIND))
        return false;
    if (square_type(dun_get_square((Location){(int16_t)(x + g_dir_dx[d]), (int16_t)(y + g_dir_dy[d])})) != SQUARE_DOOR)
        return false;
    return ai_try_door_action((AiTest){DOOR_MODE_CLOSE, false}, ai_location(x, y), AI_SQUARE_IN_DIR, d).passed;
}

/* True when a static creature stands in front of the creature on (x, y) and the party is beside a special wall. */
static bool cond_static_ahead(int16_t x, int16_t y)
{
    if (g_ai.map != g_ai_party.map)
        return false;
    int d = group_dir(g_ai.group_thing);
    uint16_t g = creature_get_group_at((Location){(x + g_dir_dx[d]), (y + g_dir_dy[d])});
    return g != THING_NONE && creature_group_info(g)->flags & CREATURE_STATIC && party_beside_special_wall();
}

/* True when the current map has few groups of the creature type: fewer than the scout and carrier minions plus one,
   at most 4. */
static bool cond_few_of_type(int16_t creature_type)
{
    int16_t same = 0;
    int16_t others = 0;
    int map = g_party.cur_map;
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
    int x = g_ai.event.data[EVENT_X];
    int y = g_ai.event.data[EVENT_Y];
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
    case COND_HEALTH_BELOW:
        r = thing_word(g_ai.group_thing, GROUP_HEALTH) * (unsigned)PERCENT / g_ai.info->max_health <= (unsigned)a;
        break;
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

/* Adds a candidate destination for the search, with its cost lowered away from the party map. */
static void add_candidate(const TargetScan *scan, uint32_t record, const AiTarget *t, int8_t bonus, uint16_t marker)
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

/* The item counts of a target list for a scan: how many of each wanted item the creature carries. */
static AiItemCounts *get_item_counts(uint32_t targets, int scan)
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
        AiTarget t = read_target(targets);
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
        AiTarget t = read_target(list);
        if (t.scan == (int8_t)kind && test_condition(t.condition, t.condition_arg, plan)) {
            AiTarget copy = t;
            if (ignore_cost) {
                copy.cost = 0;
                copy.value = 0;
            }
            add_candidate(&scan, list, &copy, bonus, marker);
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
    AiItemCounts *c = get_item_counts(list, second_set ? TARGET_SCAN_WANTED_2 : TARGET_SCAN_WANTED);
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
    AiItemCounts *c = get_item_counts(list, n);
    if (!c->extra)
        return;
    int8_t *p = c->counts;
    TargetScan scan = {plan, n, (int8_t)arg};
    for (;; list += AI_TARGET_SIZE) {
        AiTarget t = read_target(list);
        if (t.scan == (int8_t)n) {
            if (t.item != TARGET_NO_ITEM && test_condition(t.condition, t.condition_arg, plan)) {
                int8_t bonus = (int8_t)-(t.cost < p[COUNT_AT_CARRIED] ? t.cost : p[COUNT_AT_CARRIED]);
                AiTarget copy = t;
                if (arg) {
                    copy.cost = 0;
                    copy.value = 0;
                    bonus = 0;
                }
                add_candidate(&scan, list, &copy, bonus, FOUND_NONE);
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
        AiTarget t = read_target(list);
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
                add_candidate(&scan, list, &copy, 0, FOUND_NONE);
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
        AiTarget t = read_target(list);
        if (t.scan == (int8_t)n && test_condition(t.condition, t.condition_arg, plan)) {
            AiTarget copy = t;
            copy.mask = thing_word(g_ai.group_thing, GROUP_ITEM_CLASS);
            if (arg) {
                copy.cost = 0;
                copy.value = 0;
            }
            add_candidate(&scan, list, &copy, 0, FOUND_NONE);
        }
        if (!t.more)
            return;
    }
}

/* A creature that waits or wanders: the idle target. */
static void scan_idle(int arg, int plan, uint32_t list)
{
    AiTarget t = read_target(AI_TARGET_IDLE);
    t.kind = TARGET_ANY;
    TargetScan scan = {plan, 0, (int8_t)arg};
    add_candidate(&scan, list, &t, 0, FOUND_NONE);
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
    AiTarget t = read_target(AI_TARGET_IDLE);
    t.kind = TARGET_ANY;
    TargetScan scan = {plan, 0, (int8_t)arg};
    add_candidate(&scan, list, &t, 0, FOUND_NONE);
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
    int dir = group_dir(g_ai.group_thing);
    a->direction = (uint8_t)dir;
    int x = (g_ai.event.data[EVENT_X] + g_dir_dx[dir]) & LOCATION_COORD_MASK;
    int y = (g_ai.event.data[EVENT_Y] + g_dir_dy[dir]) & LOCATION_COORD_MASK;
    a->destination = (uint16_t)location_set_xy(a->destination, x, y);
    int type = square_type(dun_get_square((Location){x, y}));
    if (type == SQUARE_WALL || type == SQUARE_UNKNOWN) {
        a->action = ACTION_NONE;
        return;
    }
    if (type == SQUARE_TELEPORTER) {
        PassageEnd end;
        if (dun_get_linked_teleporter_dest(&end, (Location){x, y}) &&
            !creature_is_native_to_map(g_ai.group_thing, end.map)) {
            a->action = ACTION_NONE;
            return;
        }
    }
    a->action = ACTION_MOVE_NEAR;
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

/* A creature that cannot stay flees: it sees a missile, is not alert, cannot attack, or by chance by its armor;
   true when it moved. */
static bool try_flee(uint32_t class_flags)
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
    for (uint16_t t = dungeon_square_first_thing(g_party.cur_map, x, y);
         t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR; t = dungeon_next_thing(t)) {
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
    return ai_try_move(executed_move(MOVE_KIND_PLAIN), ai_group_square(), AI_SQUARE_IN_DIR, facing()).passed
               ? AI_RESULT_ISSUED
               : AI_RESULT_DONE;
}

/* Plan command: the creature steps to a random side, or turns to it when both sides are blocked. */
static void cmd_turn_sideways(void)
{
    int turn = rand_bit() ? 1 : -1;
    int f = facing();
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
        int f = facing();
        uint16_t g = creature_get_group_at(
            (Location){g_ai.event.data[EVENT_X] + g_dir_dx[f], g_ai.event.data[EVENT_Y] + g_dir_dy[f]});
        if (g == THING_NONE)
            return AI_RESULT_FAIL;
        return has_possession(g, item, dir_opposite(f)) ? AI_RESULT_DONE : AI_RESULT_FAIL;
    }
    bool found = false;
    uint16_t t = dungeon_square_first_object(g_party.cur_map, sx, sy);
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
                    dungeon_unlink_thing_from_square(first, g_party.cur_map, sx, sy);
                    dungeon_link_thing_to_square(first, g_party.cur_map, sx, sy);
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
    int cell = (uint8_t)g_ai.arg2;
    if (cell != CELL_ANY)
        cell = dir_turn(cell, facing() + TURN_BACK);
    int f = facing();
    uint16_t g = creature_get_group_at(
        (Location){g_ai.event.data[EVENT_X] + g_dir_dx[f], g_ai.event.data[EVENT_Y] + g_dir_dy[f]});
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
    AiCommand given = take_or_give_item(ITEM_MODE_GIVE, AI_SQUARE_IN_DIR, facing(), filter);
    g_ai.item_flags = saved;
    return given.result;
}

/* Plan command: done when a creature or the party stands two squares ahead; else the creature looks. */
static int cmd_check_party_ahead(void)
{
    g_ai.active->position = LOCATION_NONE;
    int f = facing();
    uint16_t x = (uint16_t)(g_dir_dx[f] * 2 + g_ai.event.data[EVENT_X]);
    uint16_t y = (uint16_t)(g_dir_dy[f] * 2 + g_ai.event.data[EVENT_Y]);
    uint16_t g = creature_get_group_at((Location){x, y});
    if (g != THING_NONE && !(creature_group_info(g)->flags & CREATURE_STATIC))
        return AI_RESULT_DONE;
    if (g_party.cur_map == g_ai_party.map && (int16_t)x == g_ai_party.x && (int16_t)y == g_ai_party.y)
        return AI_RESULT_DONE;
    g_ai.active->action = ACTION_CHECK_AHEAD;
    return AI_RESULT_ISSUED;
}

/* Plan command: the creature turns the group in front. */
static void cmd_turn_front_group(void)
{
    int f = facing();
    set_destination(g_ai.event.data[EVENT_X] + g_dir_dx[f], g_ai.event.data[EVENT_Y] + g_dir_dy[f]);
    g_ai.active->facing = (uint8_t)g_ai.arg1;
    g_ai.active->action = (uint8_t)((g_ai.arg1 == 1) + ACTION_TURN_GROUP);
}

/* Plan command: the creature takes items of the filter from the group in front. */
static int cmd_take_from_front(void)
{
    int16_t filter = g_ai.arg1 == -1 ? FILTER_ANY_ITEM : g_ai.arg1;
    int f = facing();
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
        AiTarget t = read_target(list);
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
        int x = g_ai.event.data[EVENT_X];
        int y = g_ai.event.data[EVENT_Y];
        if (x == dest_x() && y == dest_y() && location_map(a->destination) == g_party.cur_map)
            return AI_RESULT_DONE;
        int dir = dun_get_direction_to(x, y, dest_x(), dest_y());
        if (facing() == dir)
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
    AiItemCounts *c = get_item_counts(g_ai.cache.targets, g_ai.cache.scan_kind);
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
        AiTarget t = read_target(list);
        if (t.scan == TARGET_SCAN_SEEK_ITEMS && t.item != TARGET_NO_ITEM) {
            AiTarget copy = t;
            copy.kind = TARGET_DAMAGE_GROUP;
            add_candidate(&scan, list, &copy, 0, FOUND_NONE);
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
    if (g_party.cur_map != g_ai_party.map)
        return AI_RESULT_FAIL;
    int x = g_ai.event.data[EVENT_X];
    int y = g_ai.event.data[EVENT_Y];
    int dir = dun_get_direction_to(x, y, g_ai_party.x, g_ai_party.y);
    if (square_type(dun_get_square((Location){(int16_t)(x + g_dir_dx[dir]), (int16_t)(y + g_dir_dy[dir])})))
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
                                 AI_SQUARE_IN_DIR, dir_turn(facing(), g_ai.arg1));
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
    for (uint16_t t = dungeon_square_first_thing(g_party.cur_map, g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y]);
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

/* Runs a plan command for the creature; returns AI_RESULT_* or the result of the command. */
static int exec_plan_command(int command)
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
        int8_t r = (int8_t)exec_plan_command(fetch_plan_step());
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
    if (try_flee(class_flags)) {
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

/* Actions: what a creature does at the acting step of its animation. Each returns true when the action failed. */

/* Action: the creature turns to its new facing. */
static void act_turn(void)
{
    int f = g_ai.active->facing;
    if (dir_relative(facing(), f) == TURN_BACK)
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
        dun_set_pit_teleporter_state((Location){g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y]}, PIT_STATE_OPEN);
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
    int cell = a->cell;
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
            int d = dun_get_direction_to(dx, dy, x, y);
            cell = dir_turn(cell, (d + cell) & 1 ? -1 : 1);
            if (c == -1)
                c = move_nearest_champion_at((Location){x, y}, CELL_ANY);
            if (c == -1)
                continue;
        }
        hit = true;
        int16_t damage = (int16_t)(projectile_attack_champion(g_ai.group_thing, c) + 1);
        Champion *ch = champion(c);
        if (damage > ch->hit_damage) {
            ch->hit_damage = (uint8_t)damage;
            ch->hit_dir = (uint8_t)dir_opposite(dun_get_direction_to(x, y, g_ai_party.x, g_ai_party.y));
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
    int dx = ai_state.dest_x;
    int dy = ai_state.dest_y;
    int x = g_ai.event.data[EVENT_X];
    int y = g_ai.event.data[EVENT_Y];
    int dir = facing();
    if (a->mode == MOVE_KIND_STEP_BACK)
        dir = dir_opposite(dir);
    bool at_self = x == dx && y == dy;
    if (!at_self) {
        if (manhattan(x, y, dx, dy) > 1)
            return true;
        /* a creature that always shows its front attacks in any direction */
        if (dun_get_direction_to(x, y, dx, dy) != dir && !(info->flags & CREATURE_FACES_PARTY))
            return true;
    }
    if (g_party.cur_map == g_ai_party.map && dx == g_ai_party.x && dy == g_ai_party.y) {
        if (!melee_attack_party(x, y, dx, dy))
            return true;
    } else {
        if (square_type(dun_get_square((Location){dx, dy})) == SQUARE_DOOR) {
            int strength = rand_below((uint16_t)(info->attack + (info->attack >> 1)));
            if (sensor_door_try_destroy(dx, dy, (int16_t)strength, false, 0))
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
        dun_set_pit_teleporter_state((Location){g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y]}, PIT_STATE_CLOSED);
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
    int dir = facing();
    ai_state.dest_x = (int16_t)dest_x();
    ai_state.dest_y = (int16_t)dest_y();
    if (a->action == ACTION_LINK_UP || a->action == ACTION_LINK_DOWN) {
        int delta = a->action == ACTION_LINK_UP ? -1 : 1;
        LevelLink link =
            dun_find_adjacent_level_link((Location){g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y]}, delta);
        if (link.sensor != THING_NONE) {
            int exit = sensor_effect(link.sensor);
            /* inverted: a fixed exit */
            dir = sensor_flag(link.sensor, SENSOR_INVERTED) ? exit : dir_turn(exit, link.dir);
        } else {
            int saved = g_party.cur_map;
            Position to = dungeon_location_on_map(
                (Position){(int16_t)saved, g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y]}, delta);
            dun_set_current_map(to.map);
            int16_t d2 = dun_find_adjacent_level_link((Location){to.x, to.y}, -delta).dir;
            dun_set_current_map(saved);
            dir = d2 == -1 ? rand_2bits() : d2;
        }
    }
    bool closes = g_ai.info->traits & CREATURE_TRAIT_CLOSES_PITS;
    if (closes)
        dun_set_pit_teleporter_state((Location){g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y]}, PIT_STATE_CLOSED);
    move_thing(g_ai.group_thing, g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y], MOVE_NO_SQUARE, 0);
    ai_set_map(location_map(a->destination));
    if (a->action == ACTION_STAIRS_UP || a->action == ACTION_STAIRS_DOWN)
        dir = dun_get_stairs_exit_direction((Location){ai_state.dest_x, ai_state.dest_y});
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
    if (facing() != (int8_t)a->direction && !(info->flags & CREATURE_FACES_PARTY))
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
    if (g_party.cur_map != g_ai_party.map || ai_state.dest_x != g_ai_party.x || ai_state.dest_y != g_ai_party.y)
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
                           ai_location(ai_state.dest_x, ai_state.dest_y), facing())
            .passed &&
        g_ai.active->action == ACTION_OPERATE_DOOR) {
        sensor_schedule_square_event(ai_state.dest_x, ai_state.dest_y, 0, EFFECT_TOGGLE, g_game.game_time);
        return false;
    }
    return true;
}

/* Action: the creature picks up the items of the filter on its destination square. */
static bool act_pick_up_items(void)
{
    int n = 0;
    for (uint16_t t = dungeon_square_first_object(g_party.cur_map, ai_state.dest_x, ai_state.dest_y);
         t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (thing_type(t) == THING_GROUP)
            continue;
        if (g_ai.active->cell != CELL_ANY && thing_cell(t) != g_ai.active->cell)
            continue;
        if (!ai_thing_matches_filter(t, g_ai.active->sub))
            continue;
        move_thing(t, ai_state.dest_x, ai_state.dest_y, MOVE_NO_SQUARE, 0);
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
        int f = facing();
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
        int cell = g_ai.active->cell;
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
                                  ai_location(ai_state.dest_x, ai_state.dest_y), facing(), sub)
             .passed)
        return true;
    uint16_t hand = THING_NONE;
    if (a->mode != ACTUATOR_MODE_TAKE && sub != -1) {
        hand = ai_find_matching_item(sub, thing_word(g_ai.group_thing, GROUP_POSSESSIONS), CELL_ANY);
        if (hand == THING_ENDOFLIST)
            return true;
        dungeon_unlink_thing_from_list(hand, group_possessions(g_ai.group_thing));
    }
    sensor_process_wall_click(dest_x(), dest_y(), dir_opposite(a->facing), g_ai.group_thing, hand);
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
    set_animation(creature_anim_start(type, (int8_t)g_ai.active->action, 0));
    if (!creature_is_native_to_map(g_ai.group_thing, g_ai.map)) {
        creature_delete_group_at((Location){g_ai.event.data[EVENT_X], g_ai.event.data[EVENT_Y]}, DROP_ALL, true);
        g_ai.removed = true;
    }
}

/* Action: the creature transforms into another creature type, with sparks and clouds. */
static void act_transform(void)
{
    ActiveGroup *a = g_ai.active;
    int x = g_ai.event.data[EVENT_X];
    int y = g_ai.event.data[EVENT_Y];
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
        CreatureSummon summon = {.creature_type = a->sub,
                                 .count = GROUP_COUNT_FULL_HEALTH,
                                 .dir = dir,
                                 .x = g_ai.event.data[EVENT_X],
                                 .y = g_ai.event.data[EVENT_Y],
                                 .map = g_ai.map,
                                 .link = THING_NONE,
                                 .search_dir = (int8_t)facing()};
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

/* Runs the action of the creature at the acting step of its animation. The result is 0 when it was done; else the
   low byte of the result interrupts the animation. */
static int execute_action(void)
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
    int dir = group_dir(g_ai.group_thing);
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
            a->interrupt |= (uint8_t)execute_action();
        }
        if (!a->interrupt || !(g_ai.anim_step[STEP_JUMP] & STEP_INTERRUPTIBLE))
            break;
        r = set_animation(creature_anim_next_step(group_creature(g_ai.group_thing), g_ai.anim[0], g_ai.anim[1]));
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
        r = set_animation(creature_anim_start(group_creature(g_ai.group_thing), action, 0));
    } else {
        r = set_animation(creature_anim_next_random_step(group_creature(g_ai.group_thing), anim[0], anim[1]));
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
