/* Creature AI: the update context, moods, plans, and the update of a creature that acts. */
#include <string.h>
#include "ai.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "hooks.h"
#include "move.h"
#include "random.h"
#include "save.h"
#include "sensor.h"

#define AI_GROUP_KEY_COUNT 1024     /* group record indices */
#define AI_ACTION_FLAG_PATH 0x04    /* the action follows the cached path */
#define AI_ACTION_FLAG_TIMED 0x03   /* the action sets the time of the last action */
#define AI_ACTION_TURN_LEFT 6
#define AI_ACTION_TURN_RIGHT 7
#define AI_ACTION_VANISH 0x13
#define AI_ACTION_WAIT_FIRST 0x32   /* actions 0x32 to 0x34: wait 0 to 2 game steps without animation */
#define AI_ACTION_WAIT_LAST 0x34
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

AiContext ai;
AiPartyView ai_party;

/* The plan state and path of each group between its updates (image cache keys 0x20000000 and 0x30000000 | group
   in SKULL.EXE); the port never evicts them. */
static struct {
    int present;
    AiPlanCache cache;
    int path_size;
    uint16_t path[AI_PATH_MAX];
} ai_store[AI_GROUP_KEY_COUNT];

/* The scratch blocks of the updates in progress: item filter bitmaps (the 0x100-byte blocks of SKULL.EXE). */
static uint8_t ai_scratch_blocks[SCRATCH_BLOCKS][0x2a * FILTER_BITMAP_SIZE];
static int ai_scratch_depth;

static const uint8_t *script(uint32_t addr) { return ai_scripts + (addr - AI_SCRIPTS_BASE); }

PlanStep ai_plan_step(int plan, int step)
{
    const uint8_t *p = script(ai_plans[plan] + (uint32_t)(step * 7));
    return (PlanStep){(int8_t)p[0], (int8_t)p[1], (int8_t)p[2], (int8_t)p[3], (int8_t)p[4], p[5], (int8_t)p[6]};
}

AiTarget ai_target(uint32_t addr)
{
    const uint8_t *p = script(addr);
    return (AiTarget){(int8_t)p[0], p[1], (int16_t)(p[2] | p[3] << 8), (uint16_t)(p[4] | p[5] << 8),
                      (uint16_t)(p[6] | p[7] << 8), (int8_t)p[8], p[9], (int8_t)p[10], (int8_t)p[11], (int8_t)p[12], p[13]};
}

AiChoice ai_choice(uint32_t addr)
{
    const uint8_t *p = script(addr);
    return (AiChoice){(int8_t)p[0], (int8_t)p[1], (uint32_t)(p[2] | p[3] << 8 | p[4] << 16 | (uint32_t)p[5] << 24), p[6]};
}

uint16_t ai_mood_mask(uint32_t addr) { return (uint16_t)(script(addr)[0] | script(addr)[1] << 8); }

uint32_t ai_mood_choices(uint32_t addr)
{
    const uint8_t *p = script(addr);
    return (uint32_t)(p[2] | p[3] << 8 | p[4] << 16 | (uint32_t)p[5] << 24);
}

uint8_t ai_action_flag(int8_t action)
{
    return action < 0 ? 0 : ai_action_flags[action];  /* action -1 reads the byte before the table: 0 */
}

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
    ai.group = dungeon_thing_record(group);
    ai.active = creature_active(group);
    ai.info = creature_group_info(group);
    ai.anim = creature_anim_words(group);
    ai.map = (uint8_t)map;
    ai.event = (TimelineEvent){.time_map = (uint32_t)map << 24 | game.game_time};
    ai.event.priority = ai.group[GROUP_CREATURE];
    ai.event.type = (uint8_t)type;
    ai.event.data[0] = (uint8_t)x;
    ai.event.data[1] = (uint8_t)y;
    ai.anim_step = NULL;
    ai.removed = 0;
    ai.class_index = -1;
    if (type == EVENT_CREATURE_THINK) {
        ai.arg1 = ai.arg2 = 0;
        ai.previous_action = ai.active->action == 0xff ? 0 : ai.active->action;
        ai.active->destination = 0;
        ai.active->action = 0;
        ai.active->direction = 0;
        ai.active->cell = ai.active->facing = ai.active->sub = ai.active->phase = 0;
        ai.active->mode = ai.active->interrupt = 0;
        ai.active->action = 0xff;
    }
    return restored;
}

void creature_ai_end(int restored, const AiContext *saved)
{
    if (!restored) {
        ai.active_flag = 0;
        return;
    }
    ai = *saved;
}

int creature_ai_class(void)
{
    if (ai.class_index == -1) ai.class_index = (int16_t)creature_gdat_attr(ai.group[GROUP_CREATURE], CREATURE_ATTR_CLASS);
    return ai.class_index;
}

uint8_t *ai_scratch_push(void)
{
    ai.scratch = ai_scratch_blocks[ai_scratch_depth < SCRATCH_BLOCKS - 1 ? ai_scratch_depth++ : ai_scratch_depth];
    ai.scratch_used = 0;
    return ai.scratch;
}

void ai_scratch_pop(void)
{
    ai.scratch = NULL;
    if (ai_scratch_depth) ai_scratch_depth--;
}

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
    if (creature_group_jumps(ai.group_thing)) ai.move_flags &= 0x7fff;
    ai.counts_dirty = 1;
    memset(ai.filters, 0, sizeof ai.filters);
    ai.scratch = NULL;
    ai.filter_free = 4;
    ai.plan_flags = 0;
    ai.attr7 = (int16_t)creature_gdat_attr(ai.group[GROUP_CREATURE], CREATURE_ATTR_7);
    int16_t elapsed = (int8_t)(uint8_t)((uint8_t)game.game_time - ai.active->last_action_time);
    if (elapsed < 0) elapsed = (int16_t)(elapsed + 0x100);
    int16_t n = (int16_t)((0xf - (ai.info->speed & 0xf)) * 2);
    /* the original tests the address of rand_2bits instead of calling it: the extra delay always applies */
    ai.alert = (int16_t)(rand_below((uint16_t)(n + 1)) + (n >> 2)) <= elapsed;
    int g = thing_index(ai.group_thing);
    if (!ai_store[g].present) {
        memset(&ai.cache, 0, sizeof ai.cache);
        ai.cache.kind = -1;
        ai.cache.found = 0xffff;
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

uint16_t *ai_store_path(int size)
{
    int g = thing_index(ai.group_thing);
    if (ai_store[g].path_size < size) ai_store[g].path_size = size;
    return ai_store[g].path;
}

uint16_t *ai_stored_path(void)
{
    int g = thing_index(ai.group_thing);
    return ai_store[g].path_size ? ai_store[g].path : NULL;
}

void creature_free_cached_ai_data(uint16_t group)
{
    int g = thing_index(group) | thing_index(ai.group_thing);  /* the original ORs both indices into the key */
    ai_store[g].present = 0;
    ai_store[g].path_size = 0;
}

/* The update of a creature that acts: the next action and its animation, then the next update event. */
void creature_ai_update_group(void)
{
    ActiveGroup *a = ai.active;
    if (game.magic_light[MAGIC_ACTION_EFFECT] && !(ai.info->flags & CREATURE_FAST_VANISH)
        && a->action != AI_ACTION_VANISH && a->next_action != AI_ACTION_VANISH) {
        ai.event.time_map += ACTION_DELAY_EFFECT;
    } else {
        uint16_t *anim = ai.anim;
        int think = ai.event.type == EVENT_CREATURE_THINK, r;
        int skip_anim = 0;
        if (think) {
            a->action = (uint8_t)a->next_action;
            if (a->next_action == -1) {
                creature_ai_decide();
            } else {
                a->next_action = -1;
                if (creature_ai_plan_flags() && (int8_t)a->action != AI_ACTION_VANISH) {
                    a->action = 0xff;
                } else {
                    if (ai_action_flag((int8_t)a->action) & AI_ACTION_FLAG_PATH) {
                        ai.cache.path_left = 0;
                        ai.cache.kind = -1;
                    }
                    int dir = thing_word(ai.group_thing, GROUP_DIR) >> GROUP_DIR_SHIFT & 3;
                    if (a->action == AI_ACTION_TURN_LEFT) a->facing = (uint8_t)((dir - 1) & 3);
                    else if (a->action == AI_ACTION_TURN_RIGHT) a->facing = (uint8_t)((dir + 1) & 3);
                }
            }
            if ((int8_t)a->action == AI_ACTION_VANISH) {
                a->counters[0] = (int16_t)anim[0];
                a->counters[1] = (int16_t)anim[1];
                if (!(ai_class_flags[creature_gdat_attr(ai.group[GROUP_CREATURE], CREATURE_ATTR_CLASS)] & AI_CLASS_NO_DEATH_CLOUD)) {
                    static const int power[3] = {0x6e, 0xbe, 0xff};
                    int size = ai.info->hit_size;
                    explosion_create(CREATURE_DEATH_EXPLOSION, size == 1 ? power[1] : size == 2 ? power[2] : power[0],
                                     ai.event.data[0], ai.event.data[1], EXPLOSION_CENTER);
                }
            }
            int8_t action = (int8_t)a->action;
            if (action >= AI_ACTION_WAIT_FIRST && action <= AI_ACTION_WAIT_LAST) {
                ai.event.time_map = (uint32_t)(action - AI_ACTION_WAIT_FIRST) + game.game_time | (uint32_t)ai.map << 24;
                skip_anim = 1;
                r = 0;
            } else {
                r = creature_anim_start(ai.group[GROUP_CREATURE], action, anim, anim + 1, &ai.anim_step, 0);
            }
        } else {
            r = creature_anim_next_random_step(ai.group[GROUP_CREATURE], anim[0], anim + 1, &ai.anim_step);
        }
        if (!skip_anim) {
            for (;;) {
                if ((think || !a->interrupt || !(ai.anim_step[STEP_JUMP] & STEP_INTERRUPTIBLE))
                    && ai.anim_step[STEP_JUMP] & STEP_ACTS) {
                    creature_ai_prepare();
                    a->interrupt |= (uint8_t)creature_ai_execute_action();
                }
                if (!a->interrupt || !(ai.anim_step[STEP_JUMP] & STEP_INTERRUPTIBLE)) break;
                r = creature_anim_next_step(ai.group[GROUP_CREATURE], anim[0], anim + 1, &ai.anim_step);
                if ((uint16_t)r == 2) break;
            }
            ai.event.type = (uint16_t)r != 1 ? EVENT_CREATURE_THINK : EVENT_CREATURE_ANIMATION;
            if (ai.removed) return;
            ai.event.time_map = creature_ai_animate_and_get_next_time() | (uint32_t)ai.map << 24;
        }
    }
    if (ai.prepared && (ai.cache.path_length || (uint8_t)ai.cache.kind != 0xff)) ai_store_cache();
    if (ai.active->event != EVENT_NO_SLOT) creature_cancel_update(ai.group_thing);
    ai.active->event = (int16_t)timeline_add_event(&ai.event);
}

/* Parts of later commits of Step 16. */
AiSquare ai_square;
void creature_ai_decide(void) { ai.active->action = 0; }
int creature_ai_plan_flags(void) { return 0; }
int creature_ai_execute_action(void) { return 0; }
int creature_group_jumps(uint16_t group) { (void)group; return 0; }
int creature_damage_current_group(uint16_t damage) { (void)damage; return 0; }
