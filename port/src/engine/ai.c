/* Creature AI: the update context, moods, plans, and the update of a creature that acts. */
#include <stddef.h>
#include <string.h>
#include "ai.h"
#include "champ_action.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "hooks.h"
#include "move.h"
#include "random.h"
#include "save.h"
#include "sensor.h"
#include "ai_move.h"
#include "inventory.h"
#include "item.h"
#include "merchant.h"
#include "game.h"
#include "ui.h"
#include "le.h"

#define AI_GROUP_KEY_COUNT 1024     /* group record indices */
#define AI_ACTION_FLAG_PATH 0x04    /* the action follows the cached path */
#define AI_ACTION_FLAG_TIMED 0x03   /* the action sets the time of the last action */
#define AI_CLASS_NO_DEATH_CLOUD 0x08
#define CREATURE_FAST_VANISH 0x1000
#define CREATURE_DEATH_EXPLOSION 0xffa8
#define EXPLOSION_CENTER 0xff
#define STEP_JUMP 2
#define STEP_INTERRUPTIBLE 0x40     /* jump byte: the step may end the animation at an interrupt */
#define STEP_ACTS 0x80              /* jump byte: the creature acts at this step */
#define ACTION_DELAY_EFFECT 4       /* game steps that a party action effect delays a creature */
#define SCRATCH_BLOCKS 4            /* nested creature updates that hold a scratch block */
#define FILTER_BITMAP_SIZE 0x40
#define TIME_BYTE_WRAP 0x100        /* the last action time is the low byte of the game time */
#define TARGET_NO_ITEM 0xffff       /* a target record without an item filter */
#define CANDIDATE_MASK_ALL 0xffff
#define POWER_MAX 0xff              /* the power of a missile or an explosion is a byte */
#define DEATH_CLOUD_POWER_SMALL 110 /* the death cloud of a creature of hit size 0 */
#define DEATH_CLOUD_POWER_MEDIUM 190

AiContext ai;
AiPartyView ai_party;

/* The plan state and path of each group between its updates (image cache keys 0x20000000 and 0x30000000 | group
   in SKULL.EXE); the port never evicts them. */
static struct {
    int present;
    AiPlanCache cache;
    int path_size;
    uint8_t path[AI_PATH_BYTES];
} ai_store[AI_GROUP_KEY_COUNT];

/* The scratch blocks of the updates in progress: item filter bitmaps (the 0x100-byte blocks of SKULL.EXE). */
static uint8_t ai_scratch_blocks[SCRATCH_BLOCKS][FILTER_SLOTS * FILTER_BITMAP_SIZE];
static int ai_scratch_depth;

/* The byte offsets of the fields of the AI table records. */
enum { STEP_AT_COMMAND, STEP_AT_ON_DONE, STEP_AT_ON_FAIL, STEP_AT_ARG1, STEP_AT_ARG2, STEP_AT_SCAN, STEP_AT_SCAN_ARG };
enum {
    TARGET_AT_KIND, TARGET_AT_CONDITION, TARGET_AT_CONDITION_ARG, TARGET_AT_ITEM = 4, TARGET_AT_MASK = 6, TARGET_AT_COST = 8,
    TARGET_AT_VALUE, TARGET_AT_COUNT_10, TARGET_AT_COUNT_11, TARGET_AT_SCAN, TARGET_AT_MORE
};
enum { CHOICE_AT_PLAN, CHOICE_AT_CHANCE, CHOICE_AT_TARGETS, CHOICE_AT_MORE = 6 };
enum { MOOD_AT_MASK, MOOD_AT_CHOICES = 2 };

static const uint8_t *script(uint32_t addr) { return ai_scripts + (addr - AI_SCRIPTS_BASE); }

/* A step of a plan from the AI tables. */
PlanStep ai_plan_step(int plan, int step)
{
    const uint8_t *p = script(ai_plans[plan] + (uint32_t)(step * AI_PLAN_STEP_SIZE));
    return (PlanStep){(int8_t)p[STEP_AT_COMMAND], (int8_t)p[STEP_AT_ON_DONE], (int8_t)p[STEP_AT_ON_FAIL], (int8_t)p[STEP_AT_ARG1],
                      (int8_t)p[STEP_AT_ARG2], p[STEP_AT_SCAN], (int8_t)p[STEP_AT_SCAN_ARG]};
}

/* A target record from the AI tables. */
AiTarget ai_target(uint32_t addr)
{
    const uint8_t *p = script(addr);
    return (AiTarget){(int8_t)p[TARGET_AT_KIND], p[TARGET_AT_CONDITION], (int16_t)le16(p + TARGET_AT_CONDITION_ARG),
                      le16(p + TARGET_AT_ITEM), le16(p + TARGET_AT_MASK), (int8_t)p[TARGET_AT_COST], p[TARGET_AT_VALUE],
                      (int8_t)p[TARGET_AT_COUNT_10], (int8_t)p[TARGET_AT_COUNT_11], (int8_t)p[TARGET_AT_SCAN], p[TARGET_AT_MORE]};
}

/* A plan choice from the AI tables. */
AiChoice ai_choice(uint32_t addr)
{
    const uint8_t *p = script(addr);
    return (AiChoice){(int8_t)p[CHOICE_AT_PLAN], (int8_t)p[CHOICE_AT_CHANCE], le32(p + CHOICE_AT_TARGETS), p[CHOICE_AT_MORE]};
}

uint16_t ai_mood_mask(uint32_t addr) { return le16(script(addr) + MOOD_AT_MASK); }

/* The plan choices of a mood entry. */
uint32_t ai_mood_choices(uint32_t addr) { return le32(script(addr) + MOOD_AT_CHOICES); }

/* The flags of an action: how it moves, waits, or follows the path. */
uint8_t ai_action_flag(int8_t action)
{
    return action < 0 ? 0 : ai_action_flags[action];  /* action -1 reads the byte before the table: 0 */
}

/* Makes map the current map of the AI and reads where the party stands as that map sees it. */
void creature_ai_set_map(int map)
{
    if (map == party.cur_map) return;
    dun_set_current_map(map);
    if (map == party.alt_view_map) ai_party = (AiPartyView){party.alt_view_x, party.alt_view_y, party.alt_view_map, party.alt_view_dir};
    else ai_party = (AiPartyView){(int16_t)game.party_x, (int16_t)game.party_y, (int16_t)game.party_map, (int16_t)game.party_dir};
}

int creature_ai_begin(uint16_t group, int x, int y, int type, AiContext *saved)
{
    int map = party.cur_map;
    party.cur_map = -1;
    creature_ai_set_map(map);
    ai_square.x = -1;
    int restored = 0;
    if (ai.active_flag) {
        *saved = ai;
        restored = 1;
    }
    ai.active_flag = 1;
    ai.prepared = 0;
    ai.group_thing = group;
    ai.active = creature_active(group);
    ai.info = creature_group_info(group);
    ai.anim = creature_anim_words(group);
    ai.map = (uint8_t)map;
    ai.event = (TimelineEvent){.time_map = timeline_time_map(game.game_time, map)};
    ai.event.priority = group_creature(ai.group_thing);
    ai.event.type = (uint8_t)type;
    ai.event.data[0] = (uint8_t)x;
    ai.event.data[1] = (uint8_t)y;
    ai.anim_step = NULL;
    ai.removed = 0;
    ai.class_index = -1;
    if (type == EVENT_CREATURE_THINK) {
        ai.arg1 = ai.arg2 = 0;
        ai.previous_action = ai.active->action == ACTIVE_ACTION_NONE ? 0 : ai.active->action;
        ai.active->destination = 0;
        ai.active->action = 0;
        ai.active->direction = 0;
        ai.active->cell = ai.active->facing = ai.active->sub = ai.active->phase = 0;
        ai.active->mode = ai.active->interrupt = 0;
        ai.active->action = ACTIVE_ACTION_NONE;
    }
    return restored;
}

/* Ends a creature update: frees the context, or restores the update that the nested one interrupted. */
void creature_ai_end(int restored, const AiContext *saved)
{
    if (!restored) {
        ai.active_flag = 0;
        return;
    }
    ai = *saved;
}

/* The AI class of the creature, read once per update. */
int creature_ai_class(void)
{
    if (ai.class_index == -1) ai.class_index = (int16_t)creature_gdat_attr(group_creature(ai.group_thing), CREATURE_ATTR_CLASS);
    return ai.class_index;
}

/* Takes a scratch block for the item filters of the update. */
uint8_t *ai_scratch_push(void)
{
    ai.scratch = ai_scratch_blocks[ai_scratch_depth < SCRATCH_BLOCKS - 1 ? ai_scratch_depth++ : ai_scratch_depth];
    ai.scratch_used = 0;
    return ai.scratch;
}

/* Gives back the scratch block of the update. */
void ai_scratch_pop(void)
{
    ai.scratch = NULL;
    if (ai_scratch_depth) ai_scratch_depth--;
}

/* Reads the skills of the creature and its plan state once per update, before the first test. */
void creature_ai_prepare(void)
{
    if (ai.prepared) return;
    ai.prepared = 1;
    creature_ai_class();
    ai.move_flags = ai.info->movement;
    ai.attack_flags = ai.info->attack_skills;
    ai.door_flags = ai.info->door_skills;
    ai.item_flags = ai.info->item_skills;
    ai.actuator_flags = ai.info->actuator_skills;
    if (creature_group_jumps(ai.group_thing)) ai.move_flags &= (uint16_t)~MOVE_FALLS;
    ai.counts_dirty = 1;
    memset(ai.filters, 0, sizeof ai.filters);
    ai.scratch = NULL;
    ai.filter_free = 4;
    ai.plan_flags = 0;
    ai.attr7 = (int16_t)creature_gdat_attr(group_creature(ai.group_thing), CREATURE_ATTR_7);
    int16_t elapsed = (int8_t)((uint8_t)game.game_time - ai.active->last_action_time);
    if (elapsed < 0) elapsed = (int16_t)(elapsed + TIME_BYTE_WRAP);
    int16_t n = (int16_t)((NIBBLE_MASK - creature_think_speed(ai.info)) * 2);
    /* the original tests the address of rand_2bits instead of calling it: the extra delay always applies */
    ai.alert = (int16_t)(rand_below((uint16_t)(n + 1)) + (n >> 2)) <= elapsed;
    int g = thing_index(ai.group_thing);
    if (!ai_store[g].present) {
        memset(&ai.cache, 0, sizeof ai.cache);
        ai.cache.kind = -1;
        ai.cache.found = FOUND_NONE;
    } else {
        ai.cache = ai_store[g].cache;
    }
}

/* Keeps the plan state of the group for its next update. */
static void ai_store_cache(void)
{
    int g = thing_index(ai.group_thing);
    ai_store[g].present = 1;
    ai_store[g].cache = ai.cache;
}

/* The stored path buffer of the group, big enough for size bytes. */
uint8_t *ai_store_path(int size)
{
    int g = thing_index(ai.group_thing);
    if (ai_store[g].path_size < size) ai_store[g].path_size = size;
    return ai_store[g].path;
}

void creature_ai_forget_all(void)
{
    memset(ai_store, 0, sizeof ai_store);
    memset(&ai, 0, sizeof ai);
    ai_scratch_depth = 0;
}

uint8_t *ai_find_path(void)
{
    int g = thing_index(ai.group_thing);
    return ai_store[g].path_size ? ai_store[g].path : NULL;
}

/* Forgets the plan state and path of a group. */
void creature_free_cached_ai_data(uint16_t group)
{
    int g = thing_index(group) | thing_index(ai.group_thing);  /* the original ORs both indices into the key */
    ai_store[g].present = 0;
    ai_store[g].path_size = 0;
}


/* The action that the think event starts: the planned next action, or a new decision. */
static void creature_ai_take_next_action(void)
{
    ActiveGroup *a = ai.active;
    a->action = (uint8_t)a->next_action;
    if (a->next_action == -1) {
        creature_ai_decide();
        return;
    }
    a->next_action = -1;
    if (creature_ai_plan_flags() && (int8_t)a->action != ACTION_VANISH) {
        a->action = ACTIVE_ACTION_NONE;
        return;
    }
    if (ai_action_flag((int8_t)a->action) & AI_ACTION_FLAG_PATH) {
        ai.cache.path_left = 0;
        ai.cache.kind = -1;
    }
    int dir = group_dir(ai.group_thing);
    if (a->action == ACTION_TURN_LEFT) a->facing = (uint8_t)((dir - 1) & DIR_MASK);
    else if (a->action == ACTION_TURN_RIGHT) a->facing = (uint8_t)((dir + 1) & DIR_MASK);
}

/* The creature starts to vanish: it keeps its animation and leaves a death cloud by its size. */
static void creature_ai_start_vanish(void)
{
    ActiveGroup *a = ai.active;
    a->counters[0] = (int16_t)ai.anim[0];
    a->counters[1] = (int16_t)ai.anim[1];
    if (ai_class_flags[creature_gdat_attr(group_creature(ai.group_thing), CREATURE_ATTR_CLASS)] & AI_CLASS_NO_DEATH_CLOUD) return;
    int size = ai.info->hit_size;  /* the power of the death cloud by size */
    explosion_create(CREATURE_DEATH_EXPLOSION, size == 1 ? DEATH_CLOUD_POWER_MEDIUM : size == 2 ? POWER_MAX : DEATH_CLOUD_POWER_SMALL,
                     ai.event.data[0], ai.event.data[1], EXPLOSION_CENTER);
}

/* Runs the animation steps of the action: the creature acts at its acting steps; returns the last step result. */
static int creature_ai_run_animation(int think, int r)
{
    ActiveGroup *a = ai.active;
    for (;;) {
        if ((think || !a->interrupt || !(ai.anim_step[STEP_JUMP] & STEP_INTERRUPTIBLE)) && ai.anim_step[STEP_JUMP] & STEP_ACTS) {
            creature_ai_prepare();
            a->interrupt |= (uint8_t)creature_ai_execute_action();
        }
        if (!a->interrupt || !(ai.anim_step[STEP_JUMP] & STEP_INTERRUPTIBLE)) break;
        r = creature_anim_next_step(group_creature(ai.group_thing), ai.anim[0], ai.anim + 1, &ai.anim_step);
        if ((uint16_t)r == ANIM_SEQUENCE_END) break;
    }
    return r;
}

/* The next action and its animation; returns 0 when the group left the dungeon. */
static int creature_ai_act(void)
{
    ActiveGroup *a = ai.active;
    uint16_t *anim = ai.anim;
    int think = ai.event.type == EVENT_CREATURE_THINK, r;
    if (think) {
        creature_ai_take_next_action();
        if ((int8_t)a->action == ACTION_VANISH) creature_ai_start_vanish();
        int8_t action = (int8_t)a->action;
        if (action >= ACTION_WAIT_FIRST && action <= ACTION_WAIT_LAST) {
            ai.event.time_map = timeline_time_map((uint32_t)(action - ACTION_WAIT_FIRST) + game.game_time, ai.map);
            return 1;
        }
        r = creature_anim_start(group_creature(ai.group_thing), action, anim, anim + 1, &ai.anim_step, 0);
    } else {
        r = creature_anim_next_random_step(group_creature(ai.group_thing), anim[0], anim + 1, &ai.anim_step);
    }
    r = creature_ai_run_animation(think, r);
    ai.event.type = (uint16_t)r != ANIM_MORE_STEPS ? EVENT_CREATURE_THINK : EVENT_CREATURE_ANIMATION;
    if (ai.removed) return 0;
    ai.event.time_map = timeline_time_map(creature_ai_animate_and_get_next_time(), ai.map);
    return 1;
}

/* The update of a creature that acts: the next action and its animation, then the next update event. */
void creature_ai_update_group(void)
{
    ActiveGroup *a = ai.active;
    if (game.magic_light[MAGIC_ACTION_EFFECT] && !(ai.info->flags & CREATURE_FAST_VANISH)
        && a->action != ACTION_VANISH && a->next_action != ACTION_VANISH) {
        ai.event.time_map += ACTION_DELAY_EFFECT;
    } else if (!creature_ai_act()) {
        return;
    }
    if (ai.prepared && (ai.cache.path_length || ai.cache.kind != -1)) ai_store_cache();
    if (ai.active->event != EVENT_NO_SLOT) creature_cancel_update(ai.group_thing);
    ai.active->event = (int16_t)timeline_add_event(&ai.event);
}

/* Parts of later commits of Step 16. */
AiSquare ai_square;
DistanceMap party_distance = {.map = -1}, alt_distance = {.map = -1};

/* Moods and plans. */

#define MOOD_GLOBAL_TEST 0xc000         /* mood entry mask: a global variable selects the entry */
#define MOOD_GLOBAL_MASK 0x3fff
#define AI_CLASS_ALWAYS_SCANS 0x01      /* class flag: the creature scans for plans on each update */
#define AI_CLASS_ONE_FEAR 0x02
#define AI_CLASS_SEARCH_NEAR 0x20
#define AI_CLASS_NO_FLEE 0x40
#define AI_CLASS_DODGES 0x10
#define AI_CLASS_BORED (0x04 << 8)      /* class byte 1 bit 2: the creature is always bored */
#define PLAN_STEP_DIRECTIVE_COUNTER (-10)  /* plan step: sets a counter of the active group */
#define PLAN_NEXT_END (-2)
#define PLAN_NEXT_FAIL (-3)
#define PLAN_NEXT_FORWARD (-5)
#define PLAN_NEXT_BACK (-6)
#define PLAN_NEXT_STAY (-7)
#define PLAN_NEXT_SKIP (-8)
#define PLAN_SCAN_KIND 0x1f
#define PLAN_SCAN_FLAGS 0xe0
#define PLAN_SCAN_PARTY_MAP 0x60        /* scan flags 0x40 without 0x20: only on the party map */
#define PLAN_SCAN_ONLY_PARTY_MAP 0x40
#define PLAN_SCAN_ENDS 0x80             /* plan flags: a plan that ends when nothing is found */
#define AI_IDLE_PLAN 0x11
#define AI_STEP_LIMIT 0x20
#define SEARCH_FLAGS_FLEE 0x2048        /* mood bits that let the search flee */
#define SEARCH_FAR 0x20
#define CANDIDATE_MASK_NO_ATTACK 0x08
/* Random bits that must all be clear for a mood change: the more bits, the rarer the change. */
#define ROLL_FAR_NO_PLAN 0x30
#define ROLL_FAR 0x70
#define ROLL_HURRY_FEAR 0x380
#define ROLL_HURRY 0xf80
#define ROLL_LAZY 0x38
#define ROLL_RESTLESS 0x3000
#define ROLL_STAYS 0x8008
#define SPEED_MOOD_STEPS 0x10           /* speed bits 4-7: 15 changes the bored mood on each update */
#define FEAR_HEALTH_PERCENT 25

/* The scanner kinds of a plan step (PlanStep.scan bits 0-4). */
enum ai_scanner {
    SCANNER_IDLE, SCANNER_TARGETS, SCANNER_ITEMS_SEEN, SCANNER_ITEMS_SEEN_2, SCANNER_ITEM_COUNTS, SCANNER_ITEM_COUNTS_2,
    SCANNER_ITEMS_SEEN_ATTACK, SCANNER_ITEMS_SEEN_ATTACK_2, SCANNER_PATH_13, SCANNER_HOME, SCANNER_MINION, SCANNER_PATH_5,
    SCANNER_CARRIED, SCANNER_CARRIED_2, SCANNER_WANDER, SCANNER_ITEM_CLASS, SCANNER_PARTY
};

static AiCandidate *candidate_at(int i) { return &ai.candidates[i]; }

int creature_ai_class(void);

/* The mood entry of the creature: its mood bits change at random, then the first entry that fits them. */
static int creature_ai_update_mood(uint32_t entries)
{
    ActiveGroup *a = ai.active;
    uint16_t mood = group_mood(ai.group_thing);
    uint32_t r = rand_next();
    if (ai.map == ai_party.map) {
        mood &= ~MOOD_FAR;
    } else if (!((uint16_t)r & (a->plan == -1 ? ROLL_FAR_NO_PLAN : ROLL_FAR))) {
        mood = (uint16_t)((mood | MOOD_FAR) & ~MOOD_HURRY);
    }
    if (!(mood & MOOD_FAR) && !((uint16_t)r & (mood & MOOD_FEAR ? ROLL_HURRY_FEAR : ROLL_HURRY))) mood ^= MOOD_HURRY;
    if (!((uint16_t)r % (SPEED_MOOD_STEPS - creature_fear_resistance(ai.info)))) mood &= ~MOOD_BORED;
    if (ai_class_flags[ai.class_index] & AI_CLASS_BORED) mood |= MOOD_LAZY;
    else if (!(r & ROLL_LAZY)) mood &= ~MOOD_LAZY;
    if (!(r & ROLL_RESTLESS)) mood &= ~MOOD_RESTLESS;
    if (!(r & 3)) mood &= ~MOOD_QUICK;
    if (!(r & ROLL_STAYS)) mood &= ~MOOD_STAYS;
    int n = 2;
    if (!(ai_class_flags[ai.class_index] & AI_CLASS_ONE_FEAR)) n = (((ai.group_thing & 3) + 1) * 2 - 1) * 2;
    if (!rand_below((uint16_t)n)) {
        int max = (int16_t)ai.info->max_health < 1 ? 1 : (int16_t)ai.info->max_health;
        if (thing_word(ai.group_thing, GROUP_HEALTH) * (unsigned)PERCENT / (unsigned)max < FEAR_HEALTH_PERCENT) mood |= MOOD_FEAR;
        else mood &= ~MOOD_FEAR;
    }
    group_set_mood(ai.group_thing, mood & ~GROUP_SEEN);
    int16_t any = -1, all = -1, i = 0;
    uint16_t mask;
    for (;; i++) {
        mask = ai_mood_mask(entries + (uint32_t)(i * AI_MOOD_ENTRY_SIZE));
        if (!mask) break;
        if ((mask & MOOD_GLOBAL_TEST) == MOOD_GLOBAL_TEST) {
            if (dun_get_global_var(mask & MOOD_GLOBAL_MASK)) break;
            continue;
        }
        if (any == -1 && mask & mood) any = i;
        if (all == -1 && (mask & mood) == mask) all = i;
        if (mask == mood) break;
    }
    if (!mask && (all != -1 || any != -1)) i = all != -1 ? all : any;
    if ((int8_t)i != a->behavior) {
        ai.cache.path_left = 0;
        a->plan = -1;
        a->plan_step = 0;
        ai.cache.kind = -1;
    }
    a->behavior = (int8_t)i;
    return (int8_t)i;
}

/* The mood entry of the creature class. */
static void creature_ai_select_behavior(void)
{
    creature_ai_class();
    ai.mood = (int16_t)creature_ai_update_mood(ai_behaviors[ai.class_index]);
    ai.mood_entry = ai_behaviors[ai.class_index] + (uint32_t)(ai.mood * AI_MOOD_ENTRY_SIZE);
}

/* The plan state of a found candidate: its path, kind, and target for the next updates. */
static void creature_ai_load_candidate_path(const AiCandidate *c)
{
    int length = c->path_length < 0 ? 0 : c->path_length;
    ai.cache.path_left = ai.cache.path_length = (uint8_t)length;
    ai.cache.found = c->found;
    ai.cache.kind = c->kind;
    ai.cache.item = c->item;
    ai.cache.mask = c->mask;
    ai.cache.scan_kind = c->scan_kind;
    ai.cache.targets = c->targets;
    uint8_t *path = NULL;
    if (length > 0) {
        path = ai_store_path(length * 2);
        memcpy(path, ai.path, (size_t)length * 2);
    }
    ai.cached_path = path;
}

static void creature_ai_run_target_scanner(int kind, int arg, int plan, uint32_t targets);

/* The current plan goes on when its targets are still there: returns the plan, or -1. */
static int creature_ai_continue_plan(void)
{
    ActiveGroup *a = ai.active;
    int found = 0, plan = -1;
    if (ai.cache.path_length && ai.cache.path_left && ai.cache.kind != -1) {
        ai.candidate_count = 0;
        if (a->plan != -1) {
            plan = a->plan;
            PlanStep step = ai_plan_step(plan, a->plan_step);
            creature_ai_run_target_scanner(step.scan, step.scan_arg, plan, ai.cache.targets);
        }
        if (ai.candidate_count) {
            found = creature_ai_plan_path();
            if (found) a->destination = ai.candidates[0].destination;
        }
    }
    return found ? plan : -1;
}

/* Halves the costs of the candidates and removes those without a cost. */
static void creature_ai_compact_candidates(void)
{
    int16_t count = (int8_t)ai.candidate_count;
    if (!count) return;
    for (int i = 0; i < count; i++) {
        AiCandidate *c = candidate_at(i);
        if (c->cost > 0) {
            int16_t half = (int16_t)(c->cost >> 1), limit = (int16_t)(ai.cache.path_length - 2);
            c->cost = (int8_t)(half < limit ? half : limit);
        }
    }
    for (int16_t i = 0; i < count; i++) {
        if (candidate_at(i)->cost >= 0) continue;
        int16_t j = (int16_t)(i + 1);
        while (j < count && candidate_at(j)->cost < 0) j++;
        if (j >= count) {
            count = (int16_t)(i + 1);
        } else {
            memmove(candidate_at(i), candidate_at(j), (size_t)(count - j) * sizeof(AiCandidate));
            count = (int16_t)(count - (j - i));
        }
    }
    if (candidate_at(count - 1)->cost < 0) count--;
    ai.candidate_count = (uint8_t)count;
}

/* The plans of the mood entry whose chance comes up scan for their targets; the current plan always. */
static void creature_ai_scan_plan_list(uint32_t choices, int plan, int step, int continuing)
{
    for (;;) {
        AiChoice c = ai_choice(choices);
        int stop = 0, run = 0, at = 0;
        if (c.plan == plan) {
            stop = continuing || ai.plan_flags;
            run = 1;
            at = (int8_t)step;
        } else if (!ai.plan_flags) {
            if (!c.chance) run = 1;
            else if (c.chance > 0) run = !rand_below((uint16_t)c.chance);
            else run = rand_below((uint16_t)-c.chance) != 0;
        }
        if (run) {
            PlanStep s = ai_plan_step(c.plan, at);
            creature_ai_run_target_scanner(s.scan, s.scan_arg, c.plan, c.targets);
        }
        if (stop || !c.more) return;
        choices += AI_CHOICE_SIZE;
    }
}

/* The scan flags of the current plan step; 0 when the step applies only on the party map and the creature is away. */
int creature_ai_plan_flags(void)
{
    ActiveGroup *a = ai.active;
    if (a->plan == -1) return 0;
    uint8_t scan = ai_plan_step(a->plan, a->plan_step).scan;
    if ((scan & PLAN_SCAN_PARTY_MAP) == PLAN_SCAN_ONLY_PARTY_MAP && ai.map != ai_party.map) return 0;
    return scan & PLAN_SCAN_FLAGS;
}

/* The plan of the next update: the plan that goes on, or the nearest candidate of a scan; AI_IDLE_PLAN when there
   is none; -1 when the plan stays; AI_RESULT_FAIL when the plan ends. */
static int creature_ai_choose_plan(void)
{
    ActiveGroup *a = ai.active;
    int continuing = creature_ai_continue_plan() != -1;
    int chosen = -1;
    if (ai_class_flags[ai.class_index] & AI_CLASS_ALWAYS_SCANS || !continuing || !rand_2bits()) {
        ai.candidate_count = 0;
        ai.plan_flags = (uint8_t)creature_ai_plan_flags();
        creature_ai_scan_plan_list(ai_mood_choices(ai.mood_entry), a->plan, a->plan_step, continuing);
        if (continuing) creature_ai_compact_candidates();
        if (ai.candidate_count) {
            uint16_t mood = group_mood(ai.group_thing);
            int flags = mood & SEARCH_FLAGS_FLEE ? ai.search_flags : 0;
            if (mood & MOOD_STAYS) flags |= SEARCH_FAR;
            int i = creature_ai_search_area(ai.event.data[0], ai.event.data[1], (uint8_t)flags, (int8_t)ai.candidate_count,
                                            ai.candidates, ai.path);
            ai.chosen = (int8_t)i;
            if ((int16_t)i != -1) {
                chosen = candidate_at(i)->plan;
                a->destination = candidate_at(i)->destination;
                creature_ai_load_candidate_path(candidate_at(i));
            } else if (ai.plan_flags) {
                return AI_RESULT_FAIL;
            }
        }
    }
    if ((int8_t)chosen == -1 && continuing) return -1;
    if ((int8_t)chosen == -1) {
        ai.cache.path_left = 0;
        ai.cache.kind = -1;
        return AI_IDLE_PLAN;
    }
    if (a->plan != -1) {
        if (a->plan == (int8_t)chosen) return -1;
        a->plan = -1;
        a->plan_step = 0;
    }
    return (int8_t)chosen;
}

/* The command of the current plan step; directives before it set the plan counters. */
static int creature_ai_fetch_plan_step(void)
{
    ActiveGroup *a = ai.active;
    int8_t step = a->plan_step;
    PlanStep s;
    for (;; step++) {
        s = ai_plan_step(a->plan, step);
        if (s.command >= 0) break;
        if (s.command == PLAN_STEP_DIRECTIVE_COUNTER && s.on_done >= 0 && s.on_done < 2) a->counters[(int)s.on_done] = s.on_fail;
    }
    a->plan_step = step;
    ai.arg1 = s.arg1;
    ai.arg2 = s.arg2;
    return s.command;
}

/* The next plan step after a command result; returns 1 when the step changed. */
static int creature_ai_advance_plan_step(int result)
{
    ActiveGroup *a = ai.active;
    int8_t step = a->plan_step;
    PlanStep s = ai_plan_step(a->plan, step);
    int8_t next = (int8_t)result == (int8_t)AI_RESULT_DONE ? s.on_done : s.on_fail;
    if (next == PLAN_NEXT_END || next == PLAN_NEXT_FAIL) {
        a->plan = -1;
        a->plan_step = 0;
        return 1;
    }
    int changed = 0;
    if (next <= PLAN_NEXT_FORWARD && next >= PLAN_NEXT_SKIP) {
        if (next != PLAN_NEXT_STAY) {
            if (next == PLAN_NEXT_SKIP) step = (int8_t)(step + 2);
            else step = (int8_t)(step + (next == PLAN_NEXT_FORWARD ? 1 : -1));
            changed = 1;
        }
    } else {
        changed = step != next;
        step = next;
    }
    a->plan_step = step;
    return changed;
}

/* Is the next step of a plan a step of the same scan as the first one: the plan goes on in this update. */
static int plan_scan_goes_on(int first_kind, int first_arg, const PlanStep *s)
{
    int k = s->scan & PLAN_SCAN_KIND;
    if (first_kind == k && k != SCANNER_TARGETS) return 1;
    if (k == SCANNER_IDLE) return 1;
    return first_kind == SCANNER_TARGETS && k == SCANNER_TARGETS && s->scan_arg == first_arg;
}

/* A creature that wanders: a step forward, a turn, or a wait. */
static void creature_ai_wander(void)
{
    ActiveGroup *a = ai.active;
    int r = rand_next() & 7;
    if (r > 3) {
        a->action = 0;
        return;
    }
    if (!r) {
        creature_ai_turn_toward(game.game_time & DIR_MASK);
        return;
    }
    int dir = group_dir(ai.group_thing);
    a->direction = (uint8_t)dir;
    int x = (ai.event.data[0] + dir_dx[dir]) & LOCATION_COORD_MASK, y = (ai.event.data[1] + dir_dy[dir]) & LOCATION_COORD_MASK;
    a->destination = (uint16_t)location_set_xy(a->destination, x, y);
    int type = square_type(dun_get_square(x, y));
    if (type == SQUARE_WALL || type == SQUARE_UNKNOWN) {
        a->action = 0;
        return;
    }
    if (type == SQUARE_TELEPORTER) {
        PassageEnd end;
        if (dun_get_linked_teleporter_dest(&end, x, y) && !creature_is_native_to_map(ai.group_thing, end.map)) {
            a->action = 0;
            return;
        }
    }
    a->action = ACTION_MOVE_NEAR;
}

/* The creature flees: dodges a missile, or steps away in up to two rounds of four directions. Returns 1 when it moved. */
static int creature_ai_flee(int second)
{
    ActiveGroup *a = ai.active;
    uint32_t class_flags = ai_class_flags[ai.class_index];
    group_set_mood_flag(ai.group_thing, MOOD_LAZY, true);
    for (;;) {
        if (class_flags & AI_CLASS_DODGES) {
            uint16_t mood = group_mood(ai.group_thing);
            int look = (mood & MOOD_FEAR && rand_2bits()) || (mood & MOOD_QUICK && rand_bit()) || !rand_2bits();
            if (look && creature_ai_projectile_incoming(ai.event.data[0], ai.event.data[1])) {
                a->plan = -1;
                a->plan_step = 0;
                a->action = ACTION_DODGE;
                return 1;
            }
        }
        int dir;
        if (creature_ai_next_path_step() > 0) dir = a->direction;
        else if (rand_bit()) dir = ((group_dir(ai.group_thing)) + 2) & DIR_MASK;
        else dir = rand_2bits();
        int turn = rand_bit() ? 1 : -1;
        for (int tries = DIR_COUNT; tries; tries--) {
            int command = (second ? MOVE_KIND_PLAIN : ai.search_flags) | AI_MOVE_TEST;
            if (creature_ai_try_move(command, ai.event.data[0], ai.event.data[1], -1, -1, dir)) {
                a->plan = -1;
                a->plan_step = 0;
                return 1;
            }
            dir = (dir + turn) & DIR_MASK;
        }
        second++;
        if (second != 1 || !rand_bit()) return 0;
    }
}

int creature_ai_exec_plan_command(int command);

/* A creature that cannot stay flees: it sees a missile, is not alert, cannot attack, or by chance by its armor.
   Returns 1 when it moved. */
static int creature_ai_try_flee(uint32_t class_flags)
{
    int second = !rand_2bits();
    int x = ai.event.data[0], y = ai.event.data[1];
    if (class_flags & AI_CLASS_NO_FLEE || creature_ai_try_move(ai.search_flags, x, y, x, y, -1)) return 0;
    int flee = (ai.search_flags == MOVE_KIND_SAFE_FLEE && creature_ai_projectile_incoming(x, y)) || !ai.alert
               || !creature_ai_try_attack(ATTACK_MODE_PARTY, x, y, -1, -1, group_dir(ai.group_thing))
               || rand_below((uint16_t)((creature_armor(ai.info) >> 2) + 1)) <= 1;
    return flee && creature_ai_flee(second);
}

/* Runs the steps of the chosen plan (or of the current plan) while they scan as its first step does. */
static void creature_ai_run_plan(int chosen)
{
    ActiveGroup *a = ai.active;
    int plan, step;
    if (chosen >= 0 && chosen < AI_PLAN_COUNT) {
        plan = a->plan = (int8_t)chosen;
        step = a->plan_step = 0;
    } else {
        plan = a->plan;
        step = a->plan_step;
    }
    PlanStep first = ai_plan_step(plan, step);
    int first_kind = first.scan & PLAN_SCAN_KIND, first_arg = first.scan_arg;
    int changed = 0, n = 0;
    do {
        if (changed) {
            PlanStep s = ai_plan_step(a->plan, a->plan_step);
            if (!plan_scan_goes_on(first_kind, first_arg, &s)) {
                a->action = ACTION_RESCAN;
                break;
            }
        }
        int8_t r = (int8_t)creature_ai_exec_plan_command(creature_ai_fetch_plan_step());
        if (r != (int8_t)AI_RESULT_ISSUED) changed |= creature_ai_advance_plan_step(r);
        if (changed) ai.cache.path_left = 0;
        if (n++ > AI_STEP_LIMIT) a->action = 0;
        if ((int8_t)a->action == -1 && r != (int8_t)AI_RESULT_ISSUED && a->plan == -1) a->action = 0;
    } while ((int8_t)a->action == -1);
}

/* The creature chooses what to do next: wander, flee, or follow the best plan. */
void creature_ai_decide(void)
{
    ActiveGroup *a = ai.active;
    creature_ai_select_behavior();
    uint32_t class_flags = ai_class_flags[ai.class_index];
    ai.search_flags = class_flags & AI_CLASS_NO_FLEE      ? MOVE_KIND_PLAIN
                      : class_flags & AI_CLASS_SEARCH_NEAR ? MOVE_KIND_SAFE
                                                           : MOVE_KIND_SAFE_FLEE;
    uint32_t choices = ai_mood_choices(ai.mood_entry);
    if (choices == AI_CHOICES_NONE) {
        a->action = 0;
        return;
    }
    if (choices == AI_CHOICES_WANDER) {
        creature_ai_wander();
        return;
    }
    creature_ai_prepare();
    ai_scratch_push();
    if (creature_ai_try_flee(class_flags)) {
        ai_scratch_pop();
        return;
    }
    a->action = ACTIVE_ACTION_NONE;
    int chosen = (int8_t)creature_ai_choose_plan();
    if (chosen != (int8_t)AI_RESULT_FAIL) {
        creature_ai_run_plan(chosen);
    } else if (!(ai.plan_flags & PLAN_SCAN_ENDS)) {
        a->action = 0;
    } else {
        if (creature_ai_advance_plan_step(AI_RESULT_FAIL)) ai.cache.path_left = 0;
        a->action = ACTION_RESCAN;
    }
    ai_scratch_pop();
}

/* Target scanners: they turn the target records of a plan into candidates for the path search. */

#define TARGET_KIND_NO_COUNT 8          /* target kind that the item counts skip */
#define TARGET_SCAN_ITEMS 0x14          /* candidate kind of the item targets of seek_kind11 */
#define TARGET_SCAN_KIND11 0x11
#define TARGET_SCAN_HOME 6
#define TARGET_MINION 7
#define TARGET_PATH_13 5
#define TARGET_PATH_5 0x12
#define TARGET_PARTY 0x16
#define CACHE_KIND_13 0x0d
#define CACHE_KIND_5 5
#define MAP_KEY_NO_WANDER 0x20          /* GDAT (8, map graphics set, 0x0b, 0x65) bit: creatures do not wander */
#define CAT_MAP_GRAPHICS 8
#define MAP_GRAPHICS_KEY_FLAGS 0x65
#define ROLL_WANDER_START 0x3f          /* random bits: a creature starts to wander in 1 of 64 updates */
#define ROLL_WANDER_STOP 0x1f
#define TARGET_SCAN_CARRIED 0x0f
#define TARGET_SCAN_CARRIED_2 0x10
#define TARGET_SCAN_ITEM_CLASS 0x15
/* The target records of the item scanners: the wanted items and the items seen, counted against them. */
#define TARGET_SCAN_WANTED 1
#define TARGET_SCAN_SEEN 2
#define TARGET_SCAN_WANTED_2 3
#define TARGET_SCAN_SEEN_2 4

/* Adds a candidate destination for the search, with its cost lowered away from the party map. */
static void add_candidate(int kind, uint32_t record, const AiTarget *t, int scan_kind, int8_t bonus, uint16_t marker,
                          int plan, int scan_arg)
{
    if (ai.candidate_count >= AI_CANDIDATE_MAX) return;
    int8_t cost = t->cost;
    if (ai.map != ai_party.map && cost > 0 && !(ai.info->flags & CREATURE_WANDERER)) {
        cost = (int8_t)(cost >> 2);
        bonus = (int8_t)(bonus >> 2);
    }
    int16_t c = (int16_t)(cost + bonus);
    if (c < -1) c = -1;
    if (c > INT8_MAX) c = INT8_MAX;
    if ((int8_t)c < 0) return;
    AiCandidate *d = candidate_at(ai.candidate_count++);
    d->plan = (int8_t)plan;
    d->scan_arg = (int8_t)scan_arg;
    d->marker = marker;
    d->cost = (int8_t)c;
    d->value = t->value_9;
    d->kind = (int8_t)kind;
    d->item = t->item;
    d->mask = t->mask & ai.candidate_mask;
    d->scan_kind = (int8_t)scan_kind;
    d->targets = record;
}

/* The bytes of a count entry: the carried items and the items still wanted of each count. */
enum { COUNT_AT_CARRIED, COUNT_AT_WANTED, COUNT_AT_EXTRA };

/* The item counts of a target list for a scan: how many of each wanted item the creature carries. */
static AiItemCounts *creature_ai_get_item_counts(uint32_t targets, int scan)
{
    if (ai.counts_dirty) {
        memset(ai.item_counts, 0, sizeof ai.item_counts);
        ai.counts_dirty = 0;
    }
    AiItemCounts *slot = &ai.item_counts[0];
    for (int i = 0; i < AI_COUNT_LISTS; i++) {
        AiItemCounts *e = &ai.item_counts[i];
        if (!e->targets) slot = e;
        else if (e->targets == targets && e->scan_kind == (int16_t)scan) return e;
    }
    slot->targets = targets;
    slot->scan_kind = (int8_t)scan;
    slot->total = slot->wanted = slot->extra = 0;
    int8_t *p = slot->counts, *pa = p, *pb = p;
    int16_t want_a = 0, want_b = 0, used_a = 0, used_b = 0;
    for (int16_t i = 0; i < AI_COUNT_TARGETS;) {
        AiTarget t = ai_target(targets);
        if (t.scan == (int16_t)scan) {
            if (t.item != TARGET_NO_ITEM && t.kind != TARGET_KIND_NO_COUNT) {
                if (t.count_10 != -1) {
                    for (; pa < p; pa += AI_COUNT_ENTRY_SIZE) pa[COUNT_AT_WANTED] = (int8_t)(want_a - used_a);
                    want_a = t.count_10;
                    slot->wanted = (int8_t)(slot->wanted + (int8_t)want_a);
                    used_a = 0;
                }
                if (t.count_11 != -1) {
                    for (; pb < p; pb += AI_COUNT_ENTRY_SIZE) pb[COUNT_AT_EXTRA] = (int8_t)(want_b - used_b);
                    want_b = t.count_11;
                    slot->extra = (int8_t)(slot->extra + (int8_t)want_b);
                    used_b = 0;
                }
                int16_t n = (int16_t)creature_count_matching_items((int16_t)t.item, thing_word(ai.group_thing, GROUP_POSSESSIONS),
                                                                   CELL_ANY, 0, 1);
                p[COUNT_AT_CARRIED] = (int8_t)n;
                slot->total = (int8_t)(slot->total + (int8_t)n);
                int16_t ra = (int16_t)(want_a - used_a), rb = (int16_t)(want_b - used_b);
                slot->wanted = (int8_t)(slot->wanted - (int8_t)(n < ra ? n : ra));
                slot->extra = (int8_t)(slot->extra - (int8_t)(n < rb ? n : rb));
                used_a = (int16_t)(used_a + n < want_a ? used_a + n : want_a);
                used_b = (int16_t)(n + used_b < want_b ? n + used_b : want_b);
            }
            p += AI_COUNT_ENTRY_SIZE;
            i++;
        }
        if (!t.more) break;
        targets += AI_TARGET_SIZE;
    }
    return slot;
}

int creature_ai_test_condition(int condition, int arg, int plan);

/* The target records of scan |arg| whose condition holds; a negative arg ignores their cost. */
static void scan_targets(int arg, int plan, uint32_t list, int8_t bonus, uint16_t marker)
{
    if (!list) return;
    int ignore_cost = (int8_t)arg < 0;
    int kind = ignore_cost ? -(int8_t)arg : (int8_t)arg;
    for (;; list += AI_TARGET_SIZE) {
        AiTarget t = ai_target(list);
        if (t.scan == (int8_t)kind && creature_ai_test_condition(t.condition, t.condition_arg, plan)) {
            AiTarget copy = t;
            if (ignore_cost) copy.cost = 0, copy.value_9 = 0;
            add_candidate(t.kind, list, &copy, kind, bonus, marker, plan, kind);
        }
        if (!t.more) return;
    }
}

/* The items that the creature wants and sees: scans kind n (ignoring costs for arg != 0) when it is alert. */
static void scan_items_seen(int arg, int plan, uint32_t list, int n, int counts_scan, int need_attack_8)
{
    if (!list || !ai.alert) return;
    AiItemCounts *c = creature_ai_get_item_counts(list, counts_scan);
    if ((!need_attack_8 || ai.attack_flags & CANDIDATE_MASK_NO_ATTACK) && c->total <= 0)
        ai.candidate_mask &= ~CANDIDATE_MASK_NO_ATTACK;
    if (!ai.attack_flags) return;
    scan_targets(arg ? -n : n, plan, list, 0, FOUND_NONE);
}

/* The items still wanted by count: candidates with a bonus by the count of each. */
static void scan_item_counts(int arg, int plan, uint32_t list, int n)
{
    if (!list) return;
    AiItemCounts *c = creature_ai_get_item_counts(list, n);
    if (!c->extra) return;
    int8_t *p = c->counts;
    for (;; list += AI_TARGET_SIZE) {
        AiTarget t = ai_target(list);
        if (t.scan == (int8_t)n) {
            if (t.item != TARGET_NO_ITEM && creature_ai_test_condition(t.condition, t.condition_arg, plan)) {
                int8_t bonus = (int8_t)-(t.cost < p[COUNT_AT_CARRIED] ? t.cost : p[COUNT_AT_CARRIED]);
                AiTarget copy = t;
                if (arg) {
                    copy.cost = 0;
                    copy.value_9 = 0;
                    bonus = 0;
                }
                add_candidate(t.kind, list, &copy, n, bonus, FOUND_NONE, plan, (int8_t)arg);
            }
            p += AI_COUNT_ENTRY_SIZE;
        }
        if (!t.more) return;
    }
}

/* The targets of scan n whose items the creature carries (filters 0 and 1 of a target). */
static void scan_carried_kind(int arg, int plan, uint32_t list, int n)
{
    if (!list) return;
    for (;; list += AI_TARGET_SIZE) {
        AiTarget t = ai_target(list);
        if (t.scan == (int8_t)n) {
            int carried = 1;
            if (t.item != TARGET_NO_ITEM && (t.mask == 0 || t.mask == 1))
                carried = creature_find_matching_item((int16_t)t.item, thing_word(ai.group_thing, GROUP_POSSESSIONS), CELL_ANY)
                          != THING_ENDOFLIST;
            if (carried && creature_ai_test_condition(t.condition, t.condition_arg, plan)) {
                AiTarget copy = t;
                if (arg) copy.cost = 0, copy.value_9 = 0;
                add_candidate(t.kind, list, &copy, n, 0, FOUND_NONE, plan, (int8_t)arg);
            }
        }
        if (!t.more) return;
    }
}

/* The targets of scan n by their condition; the target filter becomes the item class of the group. */
static void scan_by_condition(int arg, int plan, uint32_t list, int n)
{
    if (!list) return;
    for (;; list += AI_TARGET_SIZE) {
        AiTarget t = ai_target(list);
        if (t.scan == (int8_t)n && creature_ai_test_condition(t.condition, t.condition_arg, plan)) {
            AiTarget copy = t;
            copy.mask = thing_word(ai.group_thing, GROUP_ITEM_CLASS);
            if (arg) copy.cost = 0, copy.value_9 = 0;
            add_candidate(t.kind, list, &copy, n, 0, FOUND_NONE, plan, (int8_t)arg);
        }
        if (!t.more) return;
    }
}

/* A creature that waits or wanders: the idle target. */
static void scan_idle(int arg, int plan, uint32_t list)
{
    AiTarget t = ai_target(AI_TARGET_IDLE);
    add_candidate(0, list, &t, 0, 0, FOUND_NONE, plan, (int8_t)arg);
}

/* A creature that wanders now and then on maps that allow it. */
static void scan_random_wander(int arg, int plan, uint32_t list)
{
    uint16_t mood = group_mood(ai.group_thing);
    int set = dungeon_map_wallset(ai.map);
    if (gdat_entry_index(draw_gdat(), CAT_MAP_GRAPHICS, set, GDAT_TYPE_VALUE, MAP_GRAPHICS_KEY_FLAGS) & MAP_KEY_NO_WANDER
        || (!(int8_t)arg && mood & MOOD_WANDERS)) {
        group_set_mood(ai.group_thing, mood & ~MOOD_WANDERS);
        return;
    }
    if (!(int8_t)arg) {
        if (rand_next() & ROLL_WANDER_START) return;
        group_set_mood(ai.group_thing, mood | MOOD_WANDERS);
    } else if (!(rand_next() & ROLL_WANDER_STOP)) {
        group_set_mood(ai.group_thing, mood & ~MOOD_WANDERS);
    }
    AiTarget t = ai_target(AI_TARGET_IDLE);
    add_candidate(0, list, &t, 0, 0, FOUND_NONE, plan, (int8_t)arg);
}

/* Runs a scanner kind over a target list: each target that fits becomes a candidate. */
static void creature_ai_run_target_scanner(int kind, int arg, int plan, uint32_t targets)
{
    ai.candidate_mask = CANDIDATE_MASK_ALL;
    arg = (int8_t)arg;
    plan = (int8_t)plan;
    switch (kind & PLAN_SCAN_KIND) {
    case SCANNER_IDLE: scan_idle(arg, plan, targets); break;
    case SCANNER_TARGETS: scan_targets(arg, plan, targets, 0, FOUND_NONE); break;
    case SCANNER_ITEMS_SEEN: scan_items_seen(arg, plan, targets, TARGET_SCAN_SEEN, TARGET_SCAN_WANTED, 0); break;
    case SCANNER_ITEMS_SEEN_2: scan_items_seen(arg, plan, targets, TARGET_SCAN_SEEN_2, TARGET_SCAN_WANTED_2, 0); break;
    case SCANNER_ITEM_COUNTS: scan_item_counts(arg, plan, targets, TARGET_SCAN_WANTED); break;
    case SCANNER_ITEM_COUNTS_2: scan_item_counts(arg, plan, targets, TARGET_SCAN_WANTED_2); break;
    case SCANNER_ITEMS_SEEN_ATTACK: scan_items_seen(arg, plan, targets, TARGET_SCAN_SEEN, TARGET_SCAN_WANTED, 1); break;
    case SCANNER_ITEMS_SEEN_ATTACK_2: scan_items_seen(arg, plan, targets, TARGET_SCAN_SEEN_2, TARGET_SCAN_WANTED_2, 1); break;
    case SCANNER_PATH_13:
        scan_targets(TARGET_PATH_13, plan, targets, 0, ai.cache.kind == CACHE_KIND_13 ? ai.cache.found : FOUND_NONE);
        break;
    case SCANNER_HOME: {
        uint16_t home = thing_word(ai.group_thing, GROUP_HOME);
        if (arg && ai.event.data[0] == location_x(home) && ai.event.data[1] == location_y(home) && ai.map == location_map(home)) break;
        scan_targets(TARGET_SCAN_HOME, plan, targets, 0, FOUND_NONE);
        break;
    }
    case SCANNER_MINION: {
        uint16_t p = minion_find_link_projectile(ai.group_thing, THING_NONE);
        if (p && thing_type(thing_word(p, PROJECTILE_PAYLOAD)) == THING_CONTAINER)
            scan_targets(TARGET_MINION, plan, targets, 0, thing_word(thing_word(p, PROJECTILE_PAYLOAD), MINION_MAP_LOCATION));
        break;
    }
    case SCANNER_PATH_5:
        scan_targets(TARGET_PATH_5, plan, targets, 0, ai.cache.kind == CACHE_KIND_5 ? ai.cache.found : FOUND_NONE);
        break;
    case SCANNER_CARRIED: scan_carried_kind(arg, plan, targets, TARGET_SCAN_CARRIED); break;
    case SCANNER_CARRIED_2: scan_carried_kind(arg, plan, targets, TARGET_SCAN_CARRIED_2); break;
    case SCANNER_WANDER: scan_random_wander(arg, plan, targets); break;
    case SCANNER_ITEM_CLASS: scan_by_condition(arg, plan, targets, TARGET_SCAN_ITEM_CLASS); break;
    case SCANNER_PARTY:
        scan_targets(TARGET_PARTY, plan, targets, 0,
                     (uint16_t)location_pack(ai_party.x, ai_party.y, ai_party.map));
        break;
    }
}

/* Conditions of the target records. */

enum ai_condition {
    COND_ALWAYS, COND_SEES_PARTY_FRONT, COND_ON_PARTY_SQUARE, COND_CARRIES, COND_ALERT, COND_AT_HOME, COND_MOOD_BIT,
    COND_ON_PARTY_MAP, COND_PARTY_HOLDS, COND_PARTY_LEVEL, COND_PARTY_AT_PIT, COND_DOOR_BEHIND, COND_STATIC_AHEAD,
    COND_AT_HOME_CARRIES, COND_HEALTH_BELOW, COND_FEW_OF_TYPE, COND_ON_HOME_MAP, COND_MARKER, COND_ON_GAME_MAP,
    COND_MOOD_BIT_CLEAR_AWAY, COND_MOOD_BIT_ON_GAME_MAP, COND_AT_COUNTER, COND_SEES_PARTY
};
#define COND_PLAN_TEST 0x40     /* true at once when the plan is the current plan */
#define COND_NOT 0x80
#define COND_MASK 0x7f
#define SENSOR_SPECIAL_WALL 0x3f
#define CREATURE_TYPE_KIND_A 0x34
#define CREATURE_TYPE_KIND_B 0x31
#define FEW_OF_TYPE_MAX 4

/* The distance between two squares along the axes. */
static int manhattan(int x1, int y1, int x2, int y2)
{
    int16_t dx = (int16_t)(x1 - x2), dy = (int16_t)(y1 - y2);
    return (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
}

/* True when the wall squares around the party have a sensor of the special type. */
static int party_beside_special_wall(void)
{
    for (int d = 0; d < DIR_COUNT; d++) {
        int x = ai_party.x + dir_dx[d], y = ai_party.y + dir_dy[d];
        uint8_t square = dun_get_square(x, y);
        if (square_type(square) || !(square & SQUARE_HAS_THINGS)) continue;
        for (uint16_t t = dungeon_square_first_thing(party.cur_map, x, y); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
            int type = thing_type(t);
            if (type == THING_SENSOR && sensor_type(t) == SENSOR_SPECIAL_WALL) return 1;
            if (type > THING_SENSOR) break;
        }
    }
    return 0;
}

/* True when the party looks at the creature on (x, y): in front of it, or (not front_only) in range and in sight. */
static int cond_sees_party(int x, int y, int front_only, int16_t range)
{
    if (ai.map != ai_party.map || dun_get_direction_to(ai_party.x, ai_party.y, x, y) != ai_party.dir) return 0;
    if (front_only) return 1;
    int16_t d = (int16_t)manhattan(ai_party.x, ai_party.y, x, y);
    if (d <= 1) return 1;
    return d <= range && !ui.inventory && creature_trace_line(ai_party.x, ai_party.y, x, y, creature_square_is_obstacle) != 0;
}

/* True when a champion holds an item of FILTER_WEAPON_HANDS in a hand. */
static int cond_party_holds_weapon(void)
{
    for (int cell = 0; cell < CELL_COUNT; cell++) {
        int ch = champ_get_index_in_cell(cell);
        if (ch == -1) continue;
        uint16_t hand = champion(ch)->slots[SLOT_ACTION_HAND];
        if (hand != THING_NONE && creature_thing_matches_filter(hand, FILTER_WEAPON_HANDS)) return 1;
        hand = champion(ch)->slots[SLOT_READY_HAND];
        if (hand != THING_NONE && creature_thing_matches_filter(hand, FILTER_WEAPON_HANDS)) return 1;
    }
    return 0;
}

/* True when the party stands next to the creature on (x, y) with an open pit behind it. */
static int cond_party_at_pit(int x, int y)
{
    if (ai.map != ai_party.map || (x == ai_party.x && y == ai_party.y)) return 0;
    if (manhattan(x, y, ai_party.x, ai_party.y) > 1) return 0;
    int d = dun_get_direction_to(x, y, ai_party.x, ai_party.y);
    uint8_t square = dun_get_square((int16_t)(ai_party.x + dir_dx[d]), (int16_t)(ai_party.y + dir_dy[d]));
    return square_type(square) == SQUARE_PIT && square & SQUARE_PIT_OPEN && !(square & SQUARE_FLAG_0);
}

/* True when the creature on (x, y) can close the door behind it. */
static int cond_door_behind(int x, int y)
{
    int d = ((group_dir(ai.group_thing)) + 2) & DIR_MASK;
    if (!(ai.door_flags & DOOR_SKILL_ANY_SIDE)) return 0;
    if (square_type(dun_get_square((int16_t)(x + dir_dx[d]), (int16_t)(y + dir_dy[d]))) != SQUARE_DOOR) return 0;
    return creature_ai_try_door_action(DOOR_MODE_CLOSE, x, y, -1, -1, d) != 0;
}

/* True when a static creature stands in front of the creature on (x, y) and the party is beside a special wall. */
static int cond_static_ahead(int x, int y)
{
    if (ai.map != ai_party.map) return 0;
    int d = group_dir(ai.group_thing);
    uint16_t g = creature_get_group_at((uint16_t)(x + dir_dx[d]), (uint16_t)(y + dir_dy[d]));
    return g != THING_NONE && creature_group_info(g)->flags & CREATURE_STATIC && party_beside_special_wall();
}

/* True when the current map has few groups of the creature type: fewer than the other kinds plus one, at most 4. */
static int cond_few_of_type(int16_t creature_type)
{
    int16_t same = 0, others = 0;
    int map = party.cur_map;
    for (int sx = 0; sx < dungeon_map_width(map); sx++)
        for (int sy = 0; sy < dungeon_map_height(map); sy++) {
            uint16_t g = creature_group_at(map, sx, sy);
            if (g == THING_NONE) continue;
            int type = group_creature(g);
            if (type == (uint16_t)creature_type) same++;
            else if (type == CREATURE_TYPE_KIND_A || type == CREATURE_TYPE_KIND_B) others++;
        }
    return same < (others + 1 < FEW_OF_TYPE_MAX ? others + 1 : FEW_OF_TYPE_MAX);
}

/* True when the location in plan counter index of the active group is the square (x, y) of the creature. */
static int cond_at_counter(int x, int y, int16_t index)
{
    uint16_t at = 0;  /* fix: the original reads any word of the active group for an argument out of range */
    int offset = (int)offsetof(ActiveGroup, counters) + (int)sizeof at * index;
    if (offset >= 0 && offset + (int)sizeof at <= (int)sizeof(ActiveGroup)) memcpy(&at, (uint8_t *)ai.active + offset, sizeof at);
    return location_x(at) == x && location_y(at) == y && location_map(at) == ai.map;
}

/* True when the creature carries an item of filter. */
static int cond_carries(int16_t filter)
{
    return creature_find_matching_item(filter, thing_word(ai.group_thing, GROUP_POSSESSIONS), CELL_ANY) != THING_ENDOFLIST;
}

/* True when the condition of a target record holds for the creature. */
int creature_ai_test_condition(int condition, int arg, int plan)
{
    uint8_t c = (uint8_t)condition;
    int16_t a = (int16_t)arg;
    if (c & COND_PLAN_TEST) {
        if ((int8_t)plan == ai.active->plan) return 1;
        c &= ~COND_PLAN_TEST;
    }
    int negate = c & COND_NOT, r = 0;
    c &= COND_MASK;
    int x = ai.event.data[0], y = ai.event.data[1];
    uint16_t home = thing_word(ai.group_thing, GROUP_HOME), mood = group_mood(ai.group_thing);
    int at_home = ai.map == location_map(home) && x == location_x(home) && y == location_y(home);
    switch (c) {
    case COND_ALWAYS: r = 1; break;
    case COND_SEES_PARTY_FRONT:
    case COND_SEES_PARTY: r = cond_sees_party(x, y, c == COND_SEES_PARTY_FRONT, a); break;
    case COND_ON_PARTY_SQUARE: r = ai.map == ai_party.map && x == ai_party.x && y == ai_party.y; break;
    case COND_CARRIES: r = cond_carries(a); break;
    case COND_ALERT: r = ai.alert; break;
    case COND_AT_HOME: r = at_home; break;
    case COND_AT_HOME_CARRIES: r = at_home && cond_carries(a); break;
    case COND_MOOD_BIT: r = (mood & (1 << (a & SHIFT_COUNT_MASK))) != 0; break;
    case COND_ON_PARTY_MAP: r = ai.map == ai_party.map; break;
    case COND_PARTY_HOLDS: r = cond_party_holds_weapon(); break;
    case COND_PARTY_LEVEL: r = champ_get_party_level() >= a; break;
    case COND_PARTY_AT_PIT: r = cond_party_at_pit(x, y); break;
    case COND_DOOR_BEHIND: r = cond_door_behind(x, y); break;
    case COND_STATIC_AHEAD: r = cond_static_ahead(x, y); break;
    case COND_HEALTH_BELOW: r = thing_word(ai.group_thing, GROUP_HEALTH) * (unsigned)PERCENT / ai.info->max_health <= (unsigned)a; break;
    case COND_FEW_OF_TYPE: r = cond_few_of_type(a); break;
    case COND_ON_HOME_MAP: r = ai.map == location_map(home); break;
    case COND_MARKER:
        creature_ai_cache_square(x, y);
        if (ai_square.square & SQUARE_HAS_THINGS)
            r = !creature_find_ai_marker_by_id(a, (int16_t)thing_word(ai.group_thing, GROUP_ITEM_CLASS));
        break;
    case COND_ON_GAME_MAP: r = ai.map == game.party_map; break;
    case COND_MOOD_BIT_CLEAR_AWAY: r = !(mood & (1 << (a & SHIFT_COUNT_MASK))) && ai.map != game.party_map; break;
    case COND_MOOD_BIT_ON_GAME_MAP: r = mood & (1 << (a & SHIFT_COUNT_MASK)) && ai.map == game.party_map; break;
    case COND_AT_COUNTER: r = cond_at_counter(x, y, a); break;
    }
    if (negate) r = !r;
    return r;
}

/* Plan commands: each returns AI_RESULT_DONE, AI_RESULT_FAIL, or AI_RESULT_ISSUED when it set the action. */

enum ai_command {
    PLAN_CMD_MOVE_FORWARD = 0x3f, PLAN_CMD_TURN_SIDEWAYS, PLAN_CMD_VANISH, PLAN_CMD_SEEK_ITEM, PLAN_CMD_STOP, PLAN_CMD_NOP, PLAN_CMD_PLACE_ON_SENSOR,
    PLAN_CMD_FRONT_GROUP_CARRIES, PLAN_CMD_SEEK_WITH_CARRIED, PLAN_CMD_CHECK_PARTY_AHEAD, PLAN_CMD_SHOP_WAIT, PLAN_CMD_SHOP_EVALUATE,
    PLAN_CMD_SHOP_COUNT, PLAN_CMD_TURN_FRONT_GROUP, PLAN_CMD_TAKE_FROM_FRONT_GROUP, PLAN_CMD_DROP_THEN_SEEK, PLAN_CMD_SET_ACTION_A,
    PLAN_CMD_SET_MOOD_FLAG, PLAN_CMD_GO_TO_TARGET, PLAN_CMD_FOLLOW_PATH, PLAN_CMD_SEEK_CACHED_ITEM, PLAN_CMD_SEEK_KIND11, PLAN_CMD_TURN_TO_PARTY,
    PLAN_CMD_SHOOT_RANDOM, PLAN_CMD_MOVE_RELATIVE, PLAN_CMD_USE_ACTUATOR, PLAN_CMD_SHOP_PAY, PLAN_CMD_SET_ACTION_BY_FLAG, PLAN_CMD_CONSUME,
    PLAN_CMD_CHECK_TRANSFORM, PLAN_CMD_SET_SPECIAL, PLAN_CMD_SET_ACTION_B, PLAN_CMD_SET_ACTION_C, PLAN_CMD_DOOR_ACTION, PLAN_CMD_RANDOM_CHANCE,
    PLAN_CMD_CARRIES_ITEM
};
#define SHOT_KIND_RANDOM 0x82
#define FILTER_ANY_ITEM 0x3f
#define FILTER_FOOD 0x06
#define FILTER_PLACEABLE 0x10
#define ITEM_SKILL_TAKE_CARRIED 0x08
#define ITEM_SKILL_PLACE 0x77
#define SENSOR_ITEM_SPOT 0x30           /* floor sensor where a creature places an item */
#define TEXT_TRANSFORM 1                /* ornament subtype: a transform marker */
#define LOCATION_NONE 0xffff
/* The operations of the set mood flag command; MOOD_OP_QUIET: no rescan. */
enum { MOOD_OP_CLEAR, MOOD_OP_SET, MOOD_OP_TEST, MOOD_OP_SET_BY_TARGETS, MOOD_OP_CLEAR_BY_TARGETS };
#define MOOD_OP_MASK 0x0f
#define MOOD_OP_QUIET 0x10
#define MOOD_OP_QUIET_LAST 0x12
#define TARGET_SCAN_MOOD_SET 0x13       /* the target records that set mood bits; 0x14 clears them */
#define ATTACK_SKILL_ITEMS 0x08
#define PLACE_WAIT_CHECKS 3             /* the checks ahead before a creature waits to place its item again */
#define CAT_ORNAMENT 9

static int facing(void) { return group_dir(ai.group_thing); }
static int dest_x(void) { return location_x(ai.active->destination); }
static int dest_y(void) { return location_y(ai.active->destination); }

/* Sets the destination square of the action on the current map. */
static void set_destination(int x, int y)
{
    ai.active->destination = (uint16_t)location_set_xy(ai.active->destination, x, y);
}

/* A floor sensor of a type on (x, y), at cell (CELL_ANY: any); THING_NONE none. */
static uint16_t dun_find_sensor_on_square(int x, int y, int cell, int type)
{
    for (uint16_t t = dungeon_square_first_thing(party.cur_map, x, y); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR;
         t = dungeon_next_thing(t)) {
        if ((thing_type(t) == THING_SENSOR && cell == CELL_ANY) || thing_cell(t) == cell) {
            if (sensor_type(t) == type) return t;
        }
    }
    return THING_NONE;
}

/* True when a group carries an item of index item at cell (CELL_ANY: any; item -2: any item). */
static int item_find_in_possessions(uint16_t group, int item, int cell)
{
    if (group == THING_NONE) return 0;
    for (uint16_t t = thing_word(group, GROUP_POSSESSIONS); t != THING_ENDOFLIST && t != THING_NONE; t = dungeon_next_thing(t))
        if ((cell == CELL_ANY || thing_cell(t) == cell) && ((int16_t)item == -2 || item_get_index(t) == (uint16_t)item)) return 1;
    return 0;
}

/* Plan command: the creature steps forward; done when it cannot. */
static int cmd_move_forward(void)
{
    return creature_ai_try_move(AI_MOVE_TEST, ai.event.data[0], ai.event.data[1], -1, -1, facing()) ? AI_RESULT_ISSUED
                                                                                                  : AI_RESULT_DONE;
}

/* Plan command: the creature steps to a random side, or turns to it when both sides are blocked. */
static void cmd_turn_sideways(void)
{
    int turn = rand_bit() ? 1 : -1, f = facing();
    int first = (turn + f) & DIR_MASK;
    if (creature_ai_try_move(AI_MOVE_TEST, ai.event.data[0], ai.event.data[1], -1, -1, first)) return;
    if (creature_ai_try_move(AI_MOVE_TEST, ai.event.data[0], ai.event.data[1], -1, -1, (f - turn) & DIR_MASK)) return;
    creature_ai_turn_toward(first);
}

/* Plan command: the creature goes to take an item of the filter, unless it already carries one. */
static int cmd_seek_item(void)
{
    int16_t filter = ai.arg1 == -1 ? (int16_t)ai.cache.item : ai.arg1;
    if (ai.arg2 && creature_find_matching_item(filter, thing_word(ai.group_thing, GROUP_POSSESSIONS), CELL_ANY) != THING_ENDOFLIST)
        return AI_RESULT_DONE;
    creature_ai_try_take_item(AI_MOVE_TEST, ai.event.data[0], ai.event.data[1], dest_x(), dest_y(), -1, filter);
    return ai.command_result;
}

/* Plan command: the creature places the item that a floor sensor wants on the sensor square. */
static int cmd_place_on_sensor(void)
{
    uint16_t saved = ai.item_flags;
    if (!(saved & ITEM_SKILL_PLACE)) return AI_RESULT_FAIL;
    if (ai.arg2 == 1
        && creature_find_matching_item(FILTER_PLACEABLE, thing_word(ai.group_thing, GROUP_POSSESSIONS), CELL_ANY) != THING_ENDOFLIST)
        return AI_RESULT_DONE;
    int sx = ai.arg1 ? ai.event.data[0] : dest_x(), sy = ai.arg1 ? ai.event.data[1] : dest_y();
    uint16_t sensor = dun_find_sensor_on_square(sx, sy, CELL_ANY, SENSOR_ITEM_SPOT);
    if (sensor == THING_NONE) return AI_RESULT_FAIL;
    uint16_t item = sensor_data(sensor);
    if (ai.arg2 == 2) {
        int f = facing();
        uint16_t g = creature_get_group_at((uint16_t)(ai.event.data[0] + dir_dx[f]), (uint16_t)(ai.event.data[1] + dir_dy[f]));
        if (g == THING_NONE) return AI_RESULT_FAIL;
        return item_find_in_possessions(g, item, (f + 2) & DIR_MASK) ? AI_RESULT_DONE : AI_RESULT_FAIL;
    }
    int found = 0;
    uint16_t t = dungeon_square_first_object(party.cur_map, sx, sy);
    while (t != THING_ENDOFLIST && thing_type(t) < THING_WEAPON) t = dungeon_next_thing(t);
    if (t != THING_ENDOFLIST) {
        if (item_get_index(t) == item) {
            found = 1;
        } else {
            uint16_t first = t;
            while (t != THING_ENDOFLIST && thing_type(t) <= THING_MISC && item_get_index(t) != item) t = dungeon_next_thing(t);
            if (t != THING_ENDOFLIST) {
                found = 1;
                while (first != THING_ENDOFLIST && thing_type(first) <= THING_MISC && item_get_index(first) != item) {
                    uint16_t next = dungeon_next_thing(first);
                    dungeon_unlink_thing_from_square(first, party.cur_map, sx, sy);
                    dungeon_link_thing_to_square(first, party.cur_map, sx, sy);
                    first = next;
                }
            }
        }
    }
    if (!found) {
        int16_t c = ai.active->counters[0];
        ai.active->counters[0] = (int16_t)(c - 1);
        if (c <= 0) {
            ai.active->counters[0] = PLACE_WAIT_CHECKS;
            ai.active->action = ACTION_PLACE_WAIT;
        } else {
            ai.active->action = ACTION_CHECK_AHEAD;
        }
        return AI_RESULT_FAIL;
    }
    ai.item_flags &= ITEM_SKILL_PLACE;
    creature_ai_try_take_item(AI_MOVE_TEST, ai.event.data[0], ai.event.data[1], sx, sy, -1, FILTER_PLACEABLE);
    ai.item_flags = saved;
    return ai.command_result;
}

/* Plan command: true when the group in front carries an item of the filter in a cell. */
int creature_ai_cmd_front_group_carries(void)
{
    if (ai.arg1 == -1) return AI_RESULT_FAIL;
    int cell = (uint8_t)ai.arg2;
    if (cell != CELL_ANY) cell = (cell + facing() + 2) & DIR_MASK;
    int f = facing();
    uint16_t g = creature_get_group_at((uint16_t)(ai.event.data[0] + dir_dx[f]), (uint16_t)(ai.event.data[1] + dir_dy[f]));
    if (g == THING_NONE) return AI_RESULT_FAIL;
    return creature_find_matching_item(ai.arg1, thing_word(g, GROUP_POSSESSIONS), cell) != THING_ENDOFLIST ? AI_RESULT_DONE
                                                                                                            : AI_RESULT_FAIL;
}

/* Plan command: the creature puts down a carried item of the filter. */
int creature_ai_cmd_seek_with_carried_item(void)
{
    uint16_t saved = ai.item_flags;
    if (thing_word(ai.group_thing, GROUP_POSSESSIONS) == THING_ENDOFLIST || !(saved & ITEM_SKILL_TAKE_CARRIED))
        return AI_RESULT_FAIL;
    int16_t filter = ai.arg1 == -1 ? FILTER_ANY_ITEM : ai.arg1;
    if (creature_find_matching_item(filter, thing_word(ai.group_thing, GROUP_POSSESSIONS), CELL_ANY) == THING_ENDOFLIST)
        return AI_RESULT_FAIL;
    ai.item_flags &= ITEM_SKILL_TAKE_CARRIED;
    creature_ai_try_take_item(AI_MOVE_TEST | ITEM_MODE_GIVE, ai.event.data[0], ai.event.data[1], -1, -1, facing(), filter);
    ai.item_flags = saved;
    return ai.command_result;
}

/* Plan command: done when a creature or the party stands two squares ahead; else the creature looks. */
static int cmd_check_party_ahead(void)
{
    ai.active->position = LOCATION_NONE;
    int f = facing();
    uint16_t x = (uint16_t)(dir_dx[f] * 2 + ai.event.data[0]), y = (uint16_t)(dir_dy[f] * 2 + ai.event.data[1]);
    uint16_t g = creature_get_group_at(x, y);
    if (g != THING_NONE && !(creature_group_info(g)->flags & CREATURE_STATIC)) return AI_RESULT_DONE;
    if (party.cur_map == ai_party.map && (int16_t)x == ai_party.x && (int16_t)y == ai_party.y) return AI_RESULT_DONE;
    ai.active->action = ACTION_CHECK_AHEAD;
    return AI_RESULT_ISSUED;
}

/* Plan command: the creature turns the group in front. */
static void cmd_turn_front_group(void)
{
    int f = facing();
    set_destination(ai.event.data[0] + dir_dx[f], ai.event.data[1] + dir_dy[f]);
    ai.active->facing = (uint8_t)ai.arg1;
    ai.active->action = (uint8_t)((ai.arg1 == 1) + ACTION_TURN_GROUP);
}

/* Plan command: the creature takes items of the filter from the group in front. */
static int cmd_take_from_front_group(void)
{
    int16_t filter = ai.arg1 == -1 ? FILTER_ANY_ITEM : ai.arg1;
    int f = facing();
    set_destination(ai.event.data[0], ai.event.data[1]);
    ai.active->cell = (uint8_t)((f + 2) & DIR_MASK);
    ai.active->sub = (int8_t)filter;
    set_destination(dest_x() + dir_dx[f], dest_y() + dir_dy[f]);
    uint16_t g = creature_get_group_at(dest_x(), dest_y());
    if (g == THING_NONE) return AI_RESULT_FAIL;
    if (creature_find_matching_item(filter, thing_word(g, GROUP_POSSESSIONS), ai.active->cell) == THING_ENDOFLIST)
        return AI_RESULT_DONE;
    ai.active->action = ACTION_TAKE_FROM_GROUP;
    return AI_RESULT_ISSUED;
}

/* Plan command: the creature drops what it does not want, then looks for an item of the filter. */
static int cmd_drop_then_seek(void)
{
    uint16_t *list = thing_list(ai.group_thing, GROUP_POSSESSIONS);
    if (ai.arg2 != -2) {
        int16_t filter = ai.arg2 == -1 ? (int16_t)ai.cache.mask : ai.arg2;
        if ((ai.arg2 != -1 || ai.cache.mask != CANDIDATE_MASK_ALL) && *list != THING_ENDOFLIST)
            creature_remove_matching_items(list, filter, CELL_ANY);
    }
    if (*list == THING_ENDOFLIST) return AI_RESULT_FAIL;
    int16_t filter = ai.arg1;
    if (filter == -1) {
        if (ai.cache.item == TARGET_NO_ITEM) return AI_RESULT_DONE;
        filter = (int16_t)ai.cache.item;
    }
    if (creature_find_matching_item(filter, *list, CELL_ANY) == THING_ENDOFLIST) return AI_RESULT_FAIL;
    creature_ai_try_take_item(AI_MOVE_TEST | ITEM_MODE_GIVE, ai.event.data[0], ai.event.data[1], ai.event.data[0], ai.event.data[1], -1, filter);
    return ai.command_result;
}

/* Plan command: sets, clears, or tests a mood bit; a change makes the creature scan again. */
static int cmd_set_mood_flag(void)
{
    uint16_t op = (uint16_t)ai.arg2;
    uint16_t old = group_mood(ai.group_thing);
    if (op <= MOOD_OP_TEST || (op >= MOOD_OP_QUIET && op <= MOOD_OP_QUIET_LAST)) {
        uint16_t bit = (uint16_t)(1 << (ai.arg1 & SHIFT_COUNT_MASK));
        int was_set = (old & bit) == bit;
        if ((op & MOOD_OP_MASK) == MOOD_OP_CLEAR) group_set_mood(ai.group_thing, old & ~bit);
        else if ((op & MOOD_OP_MASK) == MOOD_OP_SET) group_set_mood(ai.group_thing, old | bit);
        if (!(op & MOOD_OP_QUIET) && old != group_mood(ai.group_thing)) ai.active->action = ACTION_RESCAN;
        return was_set ? AI_RESULT_DONE : AI_RESULT_FAIL;
    }
    if (op > MOOD_OP_CLEAR_BY_TARGETS || !ai.cache.targets) return AI_RESULT_FAIL;
    int kind = (op != MOOD_OP_SET_BY_TARGETS) + TARGET_SCAN_MOOD_SET;
    for (uint32_t list = ai.cache.targets;; list += AI_TARGET_SIZE) {
        AiTarget t = ai_target(list);
        if (t.scan == kind) {
            uint16_t mood = group_mood(ai.group_thing), bit = (uint16_t)(1 << (t.mask & SHIFT_COUNT_MASK));
            if (!t.item) group_set_mood(ai.group_thing, mood | bit);
            else if (t.item == 1) group_set_mood(ai.group_thing, mood & ~bit);
        }
        if (!t.more) break;
    }
    int changed = old != group_mood(ai.group_thing);
    if (changed) ai.active->action = ACTION_RESCAN;
    return changed ? AI_RESULT_DONE : AI_RESULT_FAIL;
}

/* Plan command: the creature walks the next step of its path, or turns toward its destination. */
static int cmd_go_to_target(void)
{
    ActiveGroup *a = ai.active;
    int16_t n = (int16_t)creature_turn_chance(ai.info);
    int hurry = 0;
    if (n) {
        if (group_mood_flag(ai.group_thing, MOOD_LAZY)) n = (int16_t)(n >> 2);
        hurry = (int)(rand_next() & 0xf) < n;
    }
    if (!creature_ai_next_path_step()) {
        int x = ai.event.data[0], y = ai.event.data[1];
        if (x == dest_x() && y == dest_y() && location_map(a->destination) == party.cur_map) return AI_RESULT_DONE;
        int dir = dun_get_direction_to(x, y, dest_x(), dest_y());
        if (facing() == dir) return AI_RESULT_DONE;
        if (hurry && rand_bit()) {
            a->action = 0;
            return AI_RESULT_ISSUED;
        }
        creature_ai_turn_toward(dir);
        return ai.command_result;
    }
    if (hurry) {
        a->action = 0;
        return AI_RESULT_ISSUED;
    }
    int mode = group_mood_flag(ai.group_thing, MOOD_STAYS) ? AI_MOVE_TEST | AI_MOVE_ATTACK_GROUPS : AI_MOVE_TEST;
    creature_ai_try_move(mode, ai.event.data[0], ai.event.data[1], -1, -1, a->direction);
    return ai.command_result;
}

/* Plan command: the creature attacks its target with the attacks that its plan allows. */
static int cmd_follow_path(void)
{
    AiItemCounts *c = creature_ai_get_item_counts(ai.cache.targets, ai.cache.scan_kind);
    int mode = ai.cache.kind == TARGET_MELEE_LINE ? ATTACK_MODE_GROUP
               : ai.cache.kind == TARGET_SHOOT_LINE ? ATTACK_MODE_ANY
                                                       : ATTACK_MODE_PARTY;
    ai.active->sub = (int8_t)ai.cache.item;
    uint16_t saved = ai.attack_flags;
    if (saved & ATTACK_SKILL_ITEMS && c->total <= 0) ai.attack_flags &= ~ATTACK_SKILL_ITEMS;
    ai.attack_flags &= ai.cache.mask;
    creature_ai_try_attack(mode | AI_MOVE_TEST, ai.event.data[0], ai.event.data[1], dest_x(), dest_y(), -1);
    ai.attack_flags = saved;
    return ai.command_result;
}

/* Plan command: the creature searches the area for the item targets of its plan. */
static int cmd_seek_kind11(void)
{
    ai.candidate_count = 0;
    if (!ai.cache.targets) return AI_RESULT_FAIL;
    for (uint32_t list = ai.cache.targets;; list += AI_TARGET_SIZE) {
        AiTarget t = ai_target(list);
        if (t.scan == TARGET_SCAN_KIND11 && t.item != TARGET_NO_ITEM)
            add_candidate(TARGET_SCAN_ITEMS, list, &t, TARGET_SCAN_KIND11, 0, FOUND_NONE, -1, 0);
        if (!t.more) break;
    }
    if (!ai.candidate_count) return AI_RESULT_FAIL;
    ai.chosen = (int8_t)creature_ai_search_area(ai.event.data[0], ai.event.data[1], MOVE_KIND_OPEN, (int8_t)ai.candidate_count,
                                                ai.candidates, ai.path);
    return ai.chosen != -1 ? AI_RESULT_DONE : AI_RESULT_FAIL;
}

/* Plan command: the creature turns toward the party. */
static int cmd_turn_to_party(void)
{
    ai.command_result = AI_RESULT_FAIL;
    if (party.cur_map != ai_party.map) return ai.command_result;
    int x = ai.event.data[0], y = ai.event.data[1];
    int dir = dun_get_direction_to(x, y, ai_party.x, ai_party.y);
    if (square_type(dun_get_square((int16_t)(x + dir_dx[dir]), (int16_t)(y + dir_dy[dir])))) creature_ai_turn_toward(dir);
    return ai.command_result;
}

/* Plan command: the creature shoots a random missile in a random direction. */
static void cmd_shoot_random(void)
{
    ActiveGroup *a = ai.active;
    int b = rand_bit(), r = rand_2bits();
    a->sub = (int8_t)SHOT_KIND_RANDOM;
    a->action = (uint8_t)((b != 0) + ACTION_SHOOT);
    a->direction = (uint8_t)r;
    a->cell = (uint8_t)((r + b) & DIR_MASK);
    a->mode = ATTACK_MODE_PARTY;
}

/* Plan command: the creature moves forward or to the side, through the party and other groups. */
static int cmd_move_relative(void)
{
    uint16_t saved = ai.move_flags;
    ai.move_flags |= MOVE_PARTY | MOVE_GROUPS;
    creature_ai_try_move((ai.arg1 ? MOVE_KIND_STEP_BACK : MOVE_KIND_PLAIN) | AI_MOVE_TEST, ai.event.data[0], ai.event.data[1], -1, -1, (facing() + ai.arg1) & DIR_MASK);
    ai.move_flags = saved;
    return ai.command_result;
}

/* Plan command: the creature stands in one of three ways, by its wander mood. */
static int cmd_set_action_by_flag(void)
{
    bool roams = group_mood_flag(ai.group_thing, MOOD_WANDERS);
    if (!roams && !ai.arg1) return AI_RESULT_FAIL;
    int16_t v = ai.arg1 < 0 ? 0 : ai.arg1 > 2 ? 2 : ai.arg1;
    ai.active->action = (uint8_t)(v + ACTION_STAND);
    return roams && ai.arg1 == 1 ? AI_RESULT_ISSUED : AI_RESULT_DONE;
}

/* The creature eats or uses the item it carries first, or looks for food. */
static int cmd_consume(void)
{
    uint16_t t = thing_word(ai.group_thing, GROUP_POSSESSIONS);
    if (t == THING_ENDOFLIST) return AI_RESULT_FAIL;
    int used;  /* a creature does not consume an item that the dungeon keeps */
    switch (thing_type(t)) {
    case THING_WEAPON: case THING_ARMOUR: case THING_POTION: used = !item_is_kept(t); break;
    case THING_MISC: used = !item_is_kept(t) || item_attr(t, 3) != 0; break;
    default: used = 0;
    }
    if (used) {
        dungeon_unlink_thing_from_list(t, thing_list(ai.group_thing, GROUP_POSSESSIONS));
        set_thing_word(t, THING_NEXT, THING_NONE);
        ai.active->action = 0;
        return AI_RESULT_DONE;
    }
    if (!(rand_next() & 7)) {
        creature_ai_try_take_item(AI_MOVE_TEST | ITEM_MODE_GIVE, ai.event.data[0], ai.event.data[1], ai.event.data[0], ai.event.data[1], -1,
                                  FILTER_FOOD);
        ai.active->counters[0] = (int16_t)location_pack(ai.event.data[0], ai.event.data[1], ai.map);
    } else if (!(rand_next() & 0xf)) {
        ai.active->counters[0] = -1;
    }
    return AI_RESULT_FAIL;
}

/* Plan command: the creature transforms at a transform marker on its square, else it vanishes. */
static int cmd_check_transform(void)
{
    for (uint16_t t = dungeon_square_first_thing(party.cur_map, ai.event.data[0], ai.event.data[1]);
         t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_TEXT) continue;
        if (text_kind(t) != TEXT_KIND_ORNAMENT || text_subtype(t) != TEXT_TRANSFORM) continue;
        ai.active->sub = (int8_t)text_value(t);
        ai.active->action = ACTION_TRANSFORM;
        return AI_RESULT_DONE;
    }
    creature_set_next_action(ai.group_thing, ai.event.data[0], ai.event.data[1], ACTION_VANISH, 1);
    ai.active->action = ACTION_RESCAN;
    return AI_RESULT_FAIL;
}

/* Runs a plan command for the creature; returns AI_RESULT_* or the result of the command. */
int creature_ai_exec_plan_command(int command)
{
    ActiveGroup *a = ai.active;
    switch ((uint8_t)command) {
    case PLAN_CMD_MOVE_FORWARD: return cmd_move_forward();
    case PLAN_CMD_TURN_SIDEWAYS: cmd_turn_sideways(); return AI_RESULT_DONE;
    case PLAN_CMD_VANISH: a->action = ACTION_VANISH; return AI_RESULT_DONE;
    case PLAN_CMD_SEEK_CACHED_ITEM:
        ai.arg1 = -1;
        ai.arg2 = 0;
        return cmd_seek_item();
    case PLAN_CMD_SEEK_ITEM: return cmd_seek_item();
    case PLAN_CMD_STOP: a->action = 0; return AI_RESULT_DONE;
    case PLAN_CMD_PLACE_ON_SENSOR: return cmd_place_on_sensor();
    case PLAN_CMD_FRONT_GROUP_CARRIES: return creature_ai_cmd_front_group_carries();
    case PLAN_CMD_SEEK_WITH_CARRIED: return creature_ai_cmd_seek_with_carried_item();
    case PLAN_CMD_CHECK_PARTY_AHEAD: return cmd_check_party_ahead();
    case PLAN_CMD_SHOP_WAIT: return (uint8_t)shop_cmd_wait_for_offer();
    case PLAN_CMD_SHOP_EVALUATE: return (uint8_t)shop_cmd_evaluate_offer();
    case PLAN_CMD_SHOP_COUNT: return (uint8_t)shop_cmd_count_payment();
    case PLAN_CMD_TURN_FRONT_GROUP: cmd_turn_front_group(); return AI_RESULT_DONE;
    case PLAN_CMD_TAKE_FROM_FRONT_GROUP: return cmd_take_from_front_group();
    case PLAN_CMD_DROP_THEN_SEEK: return cmd_drop_then_seek();
    case PLAN_CMD_SET_MOOD_FLAG: return cmd_set_mood_flag();
    case PLAN_CMD_GO_TO_TARGET: return cmd_go_to_target();
    case PLAN_CMD_FOLLOW_PATH: return cmd_follow_path();
    case PLAN_CMD_SEEK_KIND11: return cmd_seek_kind11();
    case PLAN_CMD_TURN_TO_PARTY: return cmd_turn_to_party();
    case PLAN_CMD_SHOOT_RANDOM: cmd_shoot_random(); return AI_RESULT_DONE;
    case PLAN_CMD_MOVE_RELATIVE: return cmd_move_relative();
    case PLAN_CMD_USE_ACTUATOR:
        creature_ai_try_use_wall_actuator((uint8_t)ai.cache.mask | AI_MOVE_TEST, ai.event.data[0], ai.event.data[1], dest_x(),
                                          dest_y(), -1, (int16_t)ai.cache.item);
        return ai.command_result;
    case PLAN_CMD_SHOP_PAY: return (uint8_t)shop_cmd_pay_change();
    case PLAN_CMD_SET_ACTION_BY_FLAG: return cmd_set_action_by_flag();
    case PLAN_CMD_CONSUME: return cmd_consume();
    case PLAN_CMD_CHECK_TRANSFORM: return cmd_check_transform();
    case PLAN_CMD_SET_SPECIAL:
        a->mode = (uint8_t)ai.cache.item;
        a->sub = (int8_t)ai.cache.mask;
        a->action = (uint8_t)(ai.arg1 + ACTION_SPECIAL);
        return AI_RESULT_DONE;
    case PLAN_CMD_SET_ACTION_A: case PLAN_CMD_SET_ACTION_B: case PLAN_CMD_SET_ACTION_C:
        a->action = (uint8_t)((int8_t)ai.arg1 == -1 ? (uint8_t)ai.cache.item : (uint8_t)ai.arg1);
        return AI_RESULT_DONE;
    case PLAN_CMD_DOOR_ACTION:
        creature_ai_try_door_action((uint8_t)ai.cache.mask | AI_MOVE_TEST, ai.event.data[0], ai.event.data[1], dest_x(),
                                    dest_y(), -1);
        return ai.command_result;
    case PLAN_CMD_RANDOM_CHANCE: return ai.arg1 > (int)rand_below(PERCENT) ? AI_RESULT_DONE : AI_RESULT_FAIL;
    case PLAN_CMD_CARRIES_ITEM:
        if (creature_find_matching_item(ai.arg1, thing_word(ai.group_thing, GROUP_POSSESSIONS), CELL_ANY) != THING_ENDOFLIST
            || creature_find_matching_item(ai.arg2, thing_word(ai.group_thing, GROUP_POSSESSIONS), CELL_ANY) != THING_ENDOFLIST)
            return AI_RESULT_DONE;
        return AI_RESULT_FAIL;
    default: return AI_RESULT_DONE;
    }
}

/* Level links and pits for the creature moves. */

#define SENSOR_LEVEL_LINK 0x1c
#define SENSOR_LINK_FIXED SENSOR_INVERTED  /* a link sensor: the exit direction does not turn with the link */
#define TEXT_LINK_FIRST 6               /* text kinds 6 and 7: stairs markers */
#define TEXT_LINK_LAST 7
#define WALL_ORNAMENT_GOES_UP 0x11      /* GDAT value (9, ornament, 0x0b, 0x11) */
#define PIT_STATE_OPEN 0
#define PIT_STATE_CLOSED 1
#define PIT_STATE_SAME 2

/* The direction of a level link (sensor or stairs marker) beside (x, y) toward level_delta, -1 none. */
int dun_find_adjacent_level_link(int x, int y, int level_delta, uint16_t *sensor)
{
    for (int d = 0; d < 4; d++) {
        int16_t sx = (int16_t)(x + dir_dx[d]), sy = (int16_t)(y + dir_dy[d]);
        for (uint16_t t = dungeon_square_first_thing(party.cur_map, sx, sy); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
            int type = thing_type(t);
            if (type > THING_SENSOR) break;
            if (type == THING_SENSOR) {
                if (sensor_type(t) == SENSOR_LEVEL_LINK
                    && level_delta
                           == (dungeon_map_level(sensor_data(t)) & MAP_LEVEL_MASK) - (dungeon_map_level(party.cur_map) & MAP_LEVEL_MASK)) {
                    *sensor = t;
                    return d;
                }
            } else if (type == THING_TEXT && text_kind(t) == TEXT_KIND_ORNAMENT) {
                int kind = text_subtype(t);
                if (kind < TEXT_LINK_FIRST || kind > TEXT_LINK_LAST) continue;
                int16_t up = (int16_t)gdat_entry_index(draw_gdat(), CAT_ORNAMENT, text_value(t), GDAT_TYPE_VALUE, WALL_ORNAMENT_GOES_UP);
                if ((level_delta == -1 && up) || (level_delta == 1 && !up)) {
                    *sensor = THING_NONE;
                    return d;
                }
            }
        }
    }
    *sensor = THING_NONE;
    return -1;
}

/* Opens or closes the pit or teleporter on (x, y); returns 1 when the state changed. */
int dun_set_pit_or_teleporter_state(int x, int y, int mode)
{
    uint8_t *square = dungeon_square_byte(party.cur_map, x, y);
    int open = 0;
    if (square_type(*square) == SQUARE_PIT) {
        open = (*square & SQUARE_PIT_OPEN) != 0;
    } else if (square_type(*square) == SQUARE_TELEPORTER) {
        if (teleporter_is_fixed(dungeon_square_first_thing(party.cur_map, x, y))) return 0;
        open = (*square & SQUARE_TELEPORTER_ON) != 0;
    } else {
        return 0;
    }
    if (mode == PIT_STATE_SAME) mode = open != 0;
    if (mode == PIT_STATE_OPEN) {
        *square |= 8;
        move_square_contents_through(x, y);
        return !open;
    }
    *square &= ~8;
    return open;
}

/* Actions: what a creature does at the acting step of its animation. Each returns 0 when it was done. */

int16_t ai_dest_x, ai_dest_y;

#define ACTION_FLAG_MOVES 0x04
#define CELL_NEAREST 4
#define MODE_EXPLODE 0                  /* the modes of the explode or spawn action */
#define MODE_SPAWN 1
#define MODE_BECOME 2
#define SPARK_CELLS 5                   /* the four cells and the center */
#define TRANSFORM_PHASES 3              /* the phases of sparks before the creature becomes the new type */
#define TRAIT_CLOSES_PITS 0x40
#define CREATURE_ATTACKS_FIRST 0x0100   /* flags: the creature attacks before it moves */
#define CREATURE_ATTACKS_ANY_DIR 0x0004
#define EXPLOSION_FIREBALL 0xff80
#define EXPLOSION_SPARKS 0xff83
#define EXPLOSION_CLOUD 0xff8e
#define SOUND_ENTRY_TRANSFORM 0x8b
#define SOUND_ENTRY_TRANSFORMED 0x81
#define SOUND_PRIORITY_TRANSFORM 0x6c
#define SOUND_VOLUME_TRANSFORM 200
#define SOUND_CATEGORY_MISC_AI 3
#define SUB_REPEAT 0x40                 /* item filter bit: the action repeats for all matching items */
#define PUSH_BY_CREATURE 0xfe
#define CHAMP_HIT_DIR 2                 /* Champion.unknown_26: the direction of the hardest hit of the turn */
#define CHAMP_HIT_DAMAGE 3              /* ... its damage */
#define PUSH_RESISTANCE_HEAVY 100     /* above it a melee hit pushes the creature only now and then */
#define ROLL_PUSH_HEAVY 0xf             /* random bits: a heavy creature stays only when all are clear and 2 more bits too */
#define STALE_REGISTER 0x100
#define SHOT_POWER_MEDIUM 0x10          /* a missile above power 8 flies at speed 2, above this 3 or 4 */
#define SHOT_POWER_STRONG 0x20          /* ... above this 7 to 10 */
#define THROW_POWER_MIN 0x14
#define ROLL_STEAL 0x70                 /* random bits: a steal fails the luck test only when all are clear */
#define SOUND_VOLUME_DROP 0x80
#define SPARK_POWER_MIN 100
#define SPARK_POWER_RANGE 0x9c
#define TRANSFORM_POWER_STEP 0x55       /* the death cloud of a transform grows by this per phase */
#define DODGE_CLOUD_POWER_MIN 0x14
#define DODGE_CLOUD_POWER_RANGE 0x28

static int act_melee_attack(void);

/* Action: the creature turns to its new facing. */
static void act_turn(void)
{
    int f = ai.active->facing;
    if (((facing() - f) & DIR_MASK) == 2) f = (f + (rand_next() & 2) + 1) & DIR_MASK;
    group_set_dir(ai.group_thing, f);
}

/* The creature arrives on its new square: the AI position follows the move. */
static void act_arrive(int closes_pits)
{
    ai.event.data[0] = (uint8_t)party.dest_x;
    ai.event.data[1] = (uint8_t)party.dest_y;
    ai.map = (uint8_t)party.dest_map;
    creature_ai_set_map(ai.map);
    if (closes_pits) dun_set_pit_or_teleporter_state(ai.event.data[0], ai.event.data[1], PIT_STATE_OPEN);
    if (ai.cache.path_left) ai.cache.path_left--;
}

/* Action: the creature moves to its destination square, after an attack if it attacks first. */
static int act_move(void)
{
    ActiveGroup *a = ai.active;
    if (ai.info->flags & CREATURE_ATTACKS_FIRST) {
        a->action = ACTION_MELEE_2;
        a->cell = CELL_NEAREST;
        act_melee_attack();
    }
    if (!creature_ai_try_move(a->mode | AI_MOVE_TEST, ai.event.data[0], ai.event.data[1], ai_dest_x, ai_dest_y, a->direction)
        || !(ai_action_flag((int8_t)a->action) & ACTION_FLAG_MOVES))
        return 1;
    int closes = ai.info->traits & TRAIT_CLOSES_PITS;
    if (closes) dun_set_pit_or_teleporter_state(ai.event.data[0], ai.event.data[1], PIT_STATE_CLOSED);
    if (!move_thing(ai.group_thing, ai.event.data[0], ai.event.data[1], dest_x(), dest_y())) act_arrive(closes);
    else ai.removed = 1;
    return 0;
}

/* Action: the creature falls or jumps two squares, one step per phase. */
static int act_move_two_steps(void)
{
    ActiveGroup *a = ai.active;
    int r = 0;
    if (a->phase == 0) {
        a->phase = 1;
        uint8_t left = ai.cache.path_left;
        r = act_move();
        ai.cache.path_left = left;
        set_destination(dest_x() + dir_dx[a->direction], dest_y() + dir_dy[a->direction]);
    } else if (a->phase == 1) {
        r = act_move();
        a->phase = r ? 2 : 3;
    } else if (a->phase == 2) {
        a->phase = 3;
        if (move_thing(ai.group_thing, ai.event.data[0], ai.event.data[1], ai.event.data[0], ai.event.data[1])) ai.removed = 1;
        ai.event.data[0] = (uint8_t)party.dest_x;
        ai.event.data[1] = (uint8_t)party.dest_y;
        ai.map = (uint8_t)party.dest_map;
        creature_ai_set_map(ai.map);
    } else {
        r = STALE_REGISTER;  /* the original returns a stale register (the low half of a pointer): nonzero, low byte 0 here */
    }
    return r;
}

/* Action: the creature takes stairs, a pit, or a level link to the next map. */
static int act_move_stairs(void)
{
    ActiveGroup *a = ai.active;
    if (!creature_ai_try_move(a->mode | AI_MOVE_TEST, ai.event.data[0], ai.event.data[1], ai_dest_x, ai_dest_y, a->direction)
        || !(ai_action_flag((int8_t)a->action) & ACTION_FLAG_MOVES))
        return 1;
    int dir = facing();
    ai_dest_x = (int16_t)dest_x();
    ai_dest_y = (int16_t)dest_y();
    if (a->action == ACTION_LINK_UP || a->action == ACTION_LINK_DOWN) {
        int delta = a->action == ACTION_LINK_UP ? -1 : 1;
        uint16_t sensor;
        int d = dun_find_adjacent_level_link(ai.event.data[0], ai.event.data[1], delta, &sensor);
        if (sensor != THING_NONE) {
            int exit = sensor_effect(sensor);
            dir = sensor_flag(sensor, SENSOR_LINK_FIXED) ? exit : (exit + (int16_t)d) & DIR_MASK;
        } else {
            int x = ai.event.data[0], y = ai.event.data[1], saved = party.cur_map;
            dun_set_current_map(dungeon_location_on_map(saved, delta, &x, &y));
            int d2 = dun_find_adjacent_level_link(x, y, -delta, &sensor);
            dun_set_current_map(saved);
            dir = (int16_t)d2 == -1 ? rand_2bits() : d2;
        }
    }
    int closes = ai.info->traits & TRAIT_CLOSES_PITS;
    if (closes) dun_set_pit_or_teleporter_state(ai.event.data[0], ai.event.data[1], PIT_STATE_CLOSED);
    move_thing(ai.group_thing, ai.event.data[0], ai.event.data[1], -1, 0);
    creature_ai_set_map(location_map(a->destination));
    if (a->action == ACTION_STAIRS_UP || a->action == ACTION_STAIRS_DOWN) dir = dun_get_stairs_exit_direction(ai_dest_x, ai_dest_y);
    group_set_dir(ai.group_thing, dir);
    if (move_thing(ai.group_thing, -1, 0, ai_dest_x, ai_dest_y)) ai.removed = 1;
    else act_arrive(closes);
    return 0;
}

/* Action: the creature turns, then moves. */
static int act_turn_then_move(void)
{
    ActiveGroup *a = ai.active;
    int r;
    if (a->phase == 0) {
        act_turn();
        r = 0;
    } else if (a->phase == 1) {
        r = act_move();
    } else {
        r = ai_dest_y;  /* the original returns the register that still holds the destination row */
    }
    a->phase++;
    return (int16_t)r;
}

/* The champions that the creature on (x, y) hits on the party square (dx, dy); returns 1 when it hit one. */
static int melee_attack_party(int x, int y, int dx, int dy)
{
    ActiveGroup *a = ai.active;
    const CreatureInfo *info = ai.info;
    game.last_combat_time = game.game_time;
    int hit = 0, random = info->flags & CREATURE_ATTACKS_RANDOM;
    int8_t list[CHAMPION_MAX];
    int16_t n = 0;
    for (int c = 0; c < game.champion_count; c++)
        if (champion(c)->health) list[n++] = (int8_t)c;
    if (!n) return 0;
    int16_t count;
    if (info->flags & CREATURE_ATTACKS_ALL) count = random ? (int16_t)(rand_below((uint16_t)n) + 1) : (int16_t)game.champion_count;
    else count = info->traits & CREATURE_TRAIT_TWO_CHAMPIONS ? 2 : 1;
    if (a->cell == CELL_NEAREST) {
        int c = champ_get_nearest_at(x, y, CELL_ANY);
        if (c != -1) a->cell = champion(c)->cell;
    }
    int cell = a->cell;
    if (count > (int16_t)game.champion_count) count = (int16_t)game.champion_count;
    while (--count != -1) {
        int c;
        if (random) {
            int16_t i = (int16_t)rand_below((uint16_t)n);
            if (list[i] < 0)
                for (int16_t k = 0; k < n; k++) {
                    if (++i >= n) i = 0;
                    if (list[i] >= 0) break;
                }
            c = list[i];
            list[i] = -1;
        } else {
            c = champ_get_index_in_cell(cell);
            int d = dun_get_direction_to(dx, dy, x, y);
            cell = ((d + cell) & 1 ? cell - 1 : cell + 1) & DIR_MASK;
            if (c == -1) c = champ_get_nearest_at(x, y, CELL_ANY);
            if (c == -1) continue;
        }
        hit = 1;
        int16_t damage = (int16_t)(creature_attack_champion(ai.group_thing, c) + 1);
        Champion *ch = champion(c);
        if (damage > ch->unknown_26[CHAMP_HIT_DAMAGE]) {
            ch->unknown_26[CHAMP_HIT_DAMAGE] = (uint8_t)damage;
            ch->unknown_26[CHAMP_HIT_DIR] = (uint8_t)((dun_get_direction_to(x, y, ai_party.x, ai_party.y) + 2) & DIR_MASK);
        }
    }
    return hit;
}

/* The creature hits the party, a door, or another creature in front of it. */
static int act_melee_attack(void)
{
    ActiveGroup *a = ai.active;
    const CreatureInfo *info = ai.info;
    int dx = ai_dest_x, dy = ai_dest_y, x = ai.event.data[0], y = ai.event.data[1];
    int dir = facing();
    if (a->mode == MOVE_KIND_STEP_BACK) dir = (dir + 2) & DIR_MASK;
    int at_self = x == dx && y == dy;
    if (!at_self) {
        if (manhattan(x, y, dx, dy) > 1) return 1;
        if (dun_get_direction_to(x, y, dx, dy) != dir && !(info->flags & CREATURE_ATTACKS_ANY_DIR)) return 1;
    }
    if (party.cur_map == ai_party.map && dx == ai_party.x && dy == ai_party.y) {
        if (!melee_attack_party(x, y, dx, dy)) return 1;
    } else {
        if (square_type(dun_get_square(dx, dy)) == SQUARE_DOOR) {
            int strength = rand_below((uint16_t)(info->attack + (info->attack >> 1)));
            if (sensor_door_try_destroy(dx, dy, (int16_t)strength, 0, 0)) return 1;
        }
        if (at_self) return 1;
        if ((int16_t)creature_attack_group_at(dx, dy) < 0) return 1;
    }
    if (a->action == ACTION_MELEE_2) {
        int push;
        if (creature_group_info(ai.group_thing)->push_resistance > PUSH_RESISTANCE_HEAVY) push = (rand_next() & ROLL_PUSH_HEAVY) || rand_2bits();
        else push = rand_2bits() != 0;
        if (push) creature_push_group(dx, dy, dir, PUSH_BY_CREATURE);
    }
    return 0;
}

/* Action: the creature shoots a missile, with a power from its attack. */
static int act_shoot(void)
{
    ActiveGroup *a = ai.active;
    const CreatureInfo *info = ai.info;
    if (a->mode <= ATTACK_MODE_PARTY_RANGED) game.last_combat_time = game.game_time;
    if (facing() != (int8_t)a->direction && !(info->flags & CREATURE_ATTACKS_ANY_DIR)) return 1;
    uint16_t p = (uint16_t)((info->attack >> 2) + 1);
    p = (uint16_t)(p + rand_below(p));
    p = (uint16_t)(p + rand_below(p));
    uint8_t sub = (uint8_t)a->sub;
    if (sub == (uint8_t)SPELL_POISON_BOLT) p = (uint16_t)(p * 4);
    else if (sub == (uint8_t)SPELL_POISON_CLOUD || sub == (uint8_t)SPELL_PUSH || sub == (uint8_t)SPELL_PULL) p = (uint16_t)(p << 3);
    int16_t power = (int16_t)p < 4 ? 4 : (int16_t)p > POWER_MAX ? POWER_MAX : (int16_t)p;
    int speed;
    if (power <= 8) speed = 1;
    else if (power <= SHOT_POWER_MEDIUM) speed = 2;
    else if (power <= SHOT_POWER_STRONG) speed = rand_bit() + 3;
    else speed = rand_2bits() + 7;
    projectile_create((uint16_t)a->sub, ai.event.data[0], ai.event.data[1], a->cell, (int8_t)a->direction, (uint8_t)power,
                      info->dexterity, (uint8_t)speed);
    if (info->traits & CREATURE_TRAIT_SHOT_HURTS) {
        int hurt = (power >> 1) + rand_bit();
        if (creature_damage_current_group((uint16_t)hurt)) {
            ai.removed = 1;
            return 1;
        }
    }
    return 0;
}

/* Action: the creature steals an item from the hands of a champion. */
static int act_steal(void)
{
    if (party.cur_map != ai_party.map || ai_dest_x != ai_party.x || ai_dest_y != ai_party.y) return 1;
    int c = champ_get_index_in_cell(ai.active->cell);
    if (c == -1) return 1;
    Champion *ch = champion(c);
    uint32_t r = rand_next();
    int stolen = 0;
    if (!champ_test_luck(champion(c), (uint16_t)(PERCENT - champ_get_dexterity(c))) || r & ROLL_STEAL) {
        uint16_t ready = ch->slots[SLOT_READY_HAND], held = ch->slots[SLOT_ACTION_HAND];
        int16_t v0 = ready != THING_NONE && creature_thing_matches_filter(ready, ai.active->sub) ? (int16_t)item_get_value(ready) : -1;
        int16_t v1 = held != THING_NONE && creature_thing_matches_filter(held, ai.active->sub) ? (int16_t)item_get_value(held) : -1;
        int take_held = r & 3 ? v1 > v0 : (r & 4) != 0;
        uint16_t t = take_held ? held : ready;
        if (t != THING_NONE && creature_thing_matches_filter(t, ai.active->sub)) {
            uint16_t item = champ_remove_object_from_slot(c, take_held);
            dungeon_append_thing(item, thing_list(ai.group_thing, GROUP_POSSESSIONS));
            stolen = 1;
            if (party_sleeping) {
                party_sleeping = 0;
                int roll = rand_next() & 0xf;
                int level = champ_get_skill_level(c, 1, 1);
                party_sleeping = 1;
                if (level + roll > 7) champ_wake_party();
            }
        }
    }
    return !stolen;
}

/* Action: the creature operates the door on its destination square. */
static int act_trigger_square(void)
{
    if (creature_ai_try_door_action(ai.active->mode | AI_MOVE_TEST, ai.event.data[0], ai.event.data[1], ai_dest_x, ai_dest_y, facing())
        && ai.active->action == ACTION_OPERATE_DOOR) {
        sensor_schedule_square_event(ai_dest_x, ai_dest_y, 0, 2, game.game_time);
        return 0;
    }
    return 1;
}

/* Action: the creature picks up the items of the filter on its destination square. */
static int act_pick_up_items(void)
{
    int n = 0;
    for (uint16_t t = dungeon_square_first_object(party.cur_map, ai_dest_x, ai_dest_y); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (thing_type(t) == THING_GROUP) continue;
        if (ai.active->cell != CELL_ANY && thing_cell(t) != ai.active->cell) continue;
        if (!creature_thing_matches_filter(t, ai.active->sub)) continue;
        move_thing(t, ai_dest_x, ai_dest_y, -1, 0);
        dungeon_append_thing(t, thing_list(ai.group_thing, GROUP_POSSESSIONS));
        n++;
        if (!(ai.active->sub & SUB_REPEAT)) break;
    }
    return !n;
}

/* Action: the creature turns to the items, picks them up, and turns back. */
static int act_turn_then_pick_up(void)
{
    ActiveGroup *a = ai.active;
    int r = 0;
    if (a->phase == 0) {
        int f = facing();
        act_turn();
        a->facing = (uint8_t)f;
    } else if (a->phase == 1) {
        act_turn();
        r = act_pick_up_items();
    }
    a->phase++;
    return (int16_t)r;
}

/* The sound of the first item that a creature drops. */
static void drop_sound(uint16_t t, int n)
{
    if (n == 1)
        sound_play_with_fallback(item_gdat_category(t), item_gdat_index(t), SOUND_ENTRY_DROP, SOUND_FALLBACK, ai_dest_x, ai_dest_y, 1,
                                 SOUND_PRIORITY_DROP, SOUND_VOLUME_DROP);
}

/* Action: the creature drops its items of the filter on its destination square. */
static int act_drop_items(void)
{
    int n = 0;
    do {
        uint16_t t = creature_find_matching_item(ai.active->sub, thing_word(ai.group_thing, GROUP_POSSESSIONS), CELL_ANY);
        if (t == THING_ENDOFLIST) break;
        n++;
        dungeon_unlink_thing_from_list(t, thing_list(ai.group_thing, GROUP_POSSESSIONS));
        t = thing_with_cell(t, ai.active->cell);
        move_thing(t, -1, 0, ai_dest_x, ai_dest_y);
        drop_sound(t, n);
    } while (ai.active->sub & SUB_REPEAT);
    return !n;
}

/* Action: the creature leaves the dungeon. */
static void act_vanish(void)
{
    creature_delete_group_at(ai.event.data[0], ai.event.data[1], 0, 1);
    ai.removed = 1;
}

/* Action: the creature throws a carried item of the filter. */
static int act_throw_item(void)
{
    uint16_t t = creature_find_matching_item(ai.active->sub, thing_word(ai.group_thing, GROUP_POSSESSIONS), CELL_ANY);
    if (t == THING_ENDOFLIST) return 1;
    dungeon_unlink_thing_from_list(t, thing_list(ai.group_thing, GROUP_POSSESSIONS));
    uint16_t p = (uint16_t)((ai.info->attack >> 2) + 1);
    p = (uint16_t)(p + rand_below(p));
    p = (uint16_t)(p + rand_below(p));
    int16_t power = (int16_t)p < THROW_POWER_MIN ? THROW_POWER_MIN : (int16_t)p > POWER_MAX ? POWER_MAX : (int16_t)p;
    projectile_create(t, ai.event.data[0], ai.event.data[1], ai.active->cell, (int8_t)ai.active->direction, (uint8_t)power,
                      ai.info->dexterity, 8);
    return 0;
}

/* Action: the creature turns the group on its destination square. */
static int act_turn_target_group(void)
{
    uint16_t g = creature_get_group_at(ai_dest_x, ai_dest_y);
    if (g == THING_NONE) return 1;
    creature_turn_group(g, 0, ai.active->facing);
    return 0;
}

/* Action: the creature gives its items of the filter to the static creature on its destination square. */
static int act_give_items(void)
{
    int n = 0;
    uint16_t g = creature_get_group_at(ai_dest_x, ai_dest_y);
    if (g == THING_NONE || !(creature_group_info(g)->flags & CREATURE_STATIC)) return 1;
    do {
        uint16_t t = creature_find_matching_item(ai.active->sub, thing_word(ai.group_thing, GROUP_POSSESSIONS), CELL_ANY);
        if (t == THING_ENDOFLIST) break;
        n++;
        dungeon_unlink_thing_from_list(t, thing_list(ai.group_thing, GROUP_POSSESSIONS));
        int cell = ai.active->cell;
        if (cell == CELL_ANY) cell = rand_2bits();
        t = thing_with_cell(t, cell);
        if (item_is_moneybox(t)) moneybox_set_placed(t, true);
        move_thing(t, -1, -1, ai_dest_x, ai_dest_y);
        drop_sound(t, n);
    } while (ai.active->sub & SUB_REPEAT);
    return !n;
}

/* Action: the creature takes items of the filter from the static creature on its destination square. */
static int act_take_items(void)
{
    int n = 0;
    uint16_t g = creature_get_group_at(ai_dest_x, ai_dest_y);
    if (g == THING_NONE || !(creature_group_info(g)->flags & CREATURE_STATIC)) return 1;
    do {
        uint16_t t = creature_find_matching_item(ai.active->sub, thing_word(g, GROUP_POSSESSIONS), ai.active->cell);
        if (t == THING_ENDOFLIST) break;
        n++;
        move_thing(t, ai_dest_x, ai_dest_y, -1, -1);
        dungeon_append_thing(t & THING_ID_MASK, thing_list(ai.group_thing, GROUP_POSSESSIONS));
    } while (ai.active->sub & SUB_REPEAT);
    return !n;
}

/* Action: the creature uses a wall actuator, with a carried item of the filter. */
static int act_use_wall_sensor(void)
{
    ActiveGroup *a = ai.active;
    int8_t sub = a->sub;
    if (!creature_ai_try_use_wall_actuator(a->mode | AI_MOVE_TEST, ai.event.data[0], ai.event.data[1], ai_dest_x, ai_dest_y, facing(), sub))
        return 1;
    uint16_t hand = THING_NONE;
    if (a->mode != ACTUATOR_MODE_TAKE && sub != -1) {
        hand = creature_find_matching_item(sub, thing_word(ai.group_thing, GROUP_POSSESSIONS), CELL_ANY);
        if (hand == THING_ENDOFLIST) return 1;
        dungeon_unlink_thing_from_list(hand, thing_list(ai.group_thing, GROUP_POSSESSIONS));
    }
    sensor_process_wall_click(dest_x(), dest_y(), (a->facing + 2) & DIR_MASK, ai.group_thing, hand);
    return 0;
}

/* The creature becomes another creature type: its health, animation, and map. */
static void creature_become(int type)
{
    group_set_creature(ai.group_thing, (uint8_t)type);
    ai.info = creature_info(draw_gdat(), type);
    int16_t max = (int16_t)ai.info->max_health;
    set_thing_word(ai.group_thing, GROUP_HEALTH, (uint16_t)(max + rand_below((uint16_t)((max >> 3) + 1))));
    creature_anim_start(type, (int8_t)ai.active->action, ai.anim, ai.anim + 1, &ai.anim_step, 0);
    if (!creature_is_native_to_map(ai.group_thing, ai.map)) {
        creature_delete_group_at(ai.event.data[0], ai.event.data[1], 0, 1);
        ai.removed = 1;
    }
}

/* Action: the creature transforms into another creature type, with sparks and clouds. */
static void act_transform(void)
{
    ActiveGroup *a = ai.active;
    int x = ai.event.data[0], y = ai.event.data[1];
    int8_t phase = (int8_t)a->phase;
    a->phase++;
    int sound = SOUND_ENTRY_TRANSFORMED;
    if (a->action == ACTION_TRANSFORM) {
        for (int n = rand_2bits() + 1; n; n--) {
            int cell = rand_below(SPARK_CELLS);
            if (cell == SPARK_CELLS - 1) cell = EXPLOSION_CENTER;
            explosion_create(EXPLOSION_SPARKS, rand_below(SPARK_POWER_RANGE) + SPARK_POWER_MIN, x, y, cell);
        }
        if (phase >= 1) explosion_create(CREATURE_DEATH_EXPLOSION, (uint16_t)(phase * TRANSFORM_POWER_STEP), x, y, EXPLOSION_CENTER);
        if (phase < TRANSFORM_PHASES) {
            sound = SOUND_ENTRY_TRANSFORM;
        } else {
            a->action = ACTION_MORPH;
            a->phase = 0;
            group_set_mood(ai.group_thing, 0);
            creature_become((uint8_t)a->sub);
        }
    } else {
        explosion_create(CREATURE_DEATH_EXPLOSION, POWER_MAX, x, y, EXPLOSION_CENTER);
    }
    sound_request(SOUND_CATEGORY_MISC_AI, 0, sound, SOUND_PRIORITY_TRANSFORM, SOUND_VOLUME_TRANSFORM, x, y, 1);
}

/* Action: the creature explodes, spawns a creature, or becomes another creature type. */
static int act_explode_or_spawn(void)
{
    ActiveGroup *a = ai.active;
    if (a->mode == MODE_EXPLODE) {
        uint16_t p = (uint16_t)((ai.info->attack >> 2) + 1);
        p = (uint16_t)(p + rand_below(p));
        p = (uint16_t)(p + rand_below(p));
        int16_t power = (int16_t)p < THROW_POWER_MIN ? THROW_POWER_MIN : (int16_t)p > POWER_MAX ? POWER_MAX : (int16_t)p;
        explosion_create(EXPLOSION_FIREBALL, (uint16_t)power, ai.event.data[0], ai.event.data[1], EXPLOSION_CENTER);
        if (a->sub) act_vanish();
    } else if (a->mode == MODE_SPAWN) {
        int dir = rand_2bits();
        if (creature_summon_group(a->sub, GROUP_COUNT_FULL_HEALTH, dir, ai.event.data[0], ai.event.data[1], ai.map, -1, (int8_t)facing()) == THING_NONE) return 1;
    } else if (a->mode == MODE_BECOME) {
        a->action = ACTION_IDLE;
        a->phase = 0;
        creature_become((uint8_t)a->sub);
    }
    return 0;
}

/* Runs the action of the creature at the acting step of its animation; returns 0 when it was done. */
int creature_ai_execute_action(void)
{
    ActiveGroup *a = ai.active;
    ai_scratch_push();
    ai_dest_x = (int16_t)dest_x();
    ai_dest_y = (int16_t)dest_y();
    uint8_t action = a->action;
    int r = 0;
    if (action == ACTION_MOVE || action == ACTION_MOVE_NEAR || action == ACTION_TURN_BACK) r = act_move();
    else if (action == ACTION_STEP_RIGHT || action == ACTION_STEP_LEFT) r = act_turn_then_move();
    else if (action == ACTION_FALL) r = act_move_two_steps();
    else if (action == ACTION_TURN_LEFT || action == ACTION_TURN_RIGHT) act_turn();
    else if (action == ACTION_MELEE || action == ACTION_MELEE_2) r = act_melee_attack();
    else if (action == ACTION_STEAL) r = act_steal();
    else if (action == ACTION_OPERATE_DOOR) r = act_trigger_square();
    else if (action == ACTION_TAKE_LEFT || action == ACTION_TAKE_RIGHT) r = act_turn_then_pick_up();
    else if (action == ACTION_THROW || action == ACTION_THROW_2) r = act_throw_item();
    else if (action == ACTION_VANISH) act_vanish();
    else if (action == ACTION_TURN_GROUP || action == ACTION_TURN_GROUP_2) r = act_turn_target_group();
    else if (action == ACTION_GIVE) r = act_give_items();
    else if (action == ACTION_TAKE_FROM_GROUP) r = act_take_items();
    else if (action == ACTION_PUT_IN_WALL || action == ACTION_DROP_HERE || action == ACTION_DROP_HERE_2 || action == ACTION_DROP_AHEAD
             || action == ACTION_DROP_AHEAD_2)
        r = act_drop_items();
    else if (action == ACTION_TAKE_FROM_WALL || action == ACTION_TAKE_FRONT || action == ACTION_TAKE_SIDE) r = act_pick_up_items();
    else if (action == ACTION_SHOOT || action == ACTION_SHOOT_2) r = act_shoot();
    else if (action >= ACTION_USE_ACTUATOR && action <= ACTION_USE_ACTUATOR_LAST) r = act_use_wall_sensor();
    else if (action >= ACTION_STAIRS_UP && action <= ACTION_LINK_DOWN) r = act_move_stairs();
    else if (action == ACTION_TRANSFORM || action == ACTION_MORPH) act_transform();
    else if (action >= ACTION_SPECIAL && action <= ACTION_SPECIAL_LAST) r = act_explode_or_spawn();
    else if (action == ACTION_DODGE) explosion_create(EXPLOSION_CLOUD, rand_below(DODGE_CLOUD_POWER_RANGE) + DODGE_CLOUD_POWER_MIN, ai.event.data[0], ai.event.data[1],
                         EXPLOSION_CENTER);
    if (!(int16_t)r) {
        if (ai_action_flag((int8_t)a->action) & AI_ACTION_FLAG_TIMED) a->last_action_time = (uint8_t)game.game_time;
    } else {
        ai.cache.path_left = 0;
    }
    ai_scratch_pop();
    return r;
}
