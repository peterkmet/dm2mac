#ifndef AI_H
#define AI_H
#include <stdint.h>
#include "creature.h"
#include "timeline.h"

/* The AI tables of SKULL.EXE (ai_data.c). Records link each other by their SKULL.EXE address. */
#define AI_SCRIPTS_BASE 0x73020u
#define AI_PLAN_COUNT 63
#define AI_CLASS_COUNT 47
#define AI_TARGET_SIZE 14
#define AI_TARGET_IDLE 0x731d2u      /* the target of a creature that waits or wanders */
#define AI_CHOICES_NONE 0x73399u     /* the plan choices of a creature that does nothing */
#define AI_CHOICES_WANDER 0x73392u   /* ... of a creature that wanders at random */
extern const uint8_t ai_scripts[0x1f5e];
extern const uint32_t ai_plans[AI_PLAN_COUNT];
extern const uint32_t ai_class_flags[AI_CLASS_COUNT];
extern const uint8_t ai_action_flags[0x56];
extern const uint32_t ai_behaviors[AI_CLASS_COUNT];
extern const uint8_t ai_command_enabled[20];

/* A step of a plan (7 bytes). */
typedef struct {
    int8_t command;     /* AI_CMD_* (0x3f and up), a step index, or a directive (negative) */
    int8_t on_done;     /* the next step when the command is done: index or AI_NEXT_* */
    int8_t on_fail;     /* ... when it failed */
    int8_t arg1, arg2;
    uint8_t scan;       /* bits 0-4 scanner kind, bits 5-7 AI_SCAN_* */
    int8_t scan_arg;
} PlanStep;

/* A target record (14 bytes): what a scanner looks for. */
typedef struct {
    int8_t kind;        /* candidate kind */
    uint8_t condition;  /* AI_COND_*, bit 6 plan test, bit 7 negation */
    int16_t condition_arg;
    uint16_t item;      /* item filter or marker */
    uint16_t mask;
    int8_t cost;
    uint8_t value_9;
    int8_t count_10, count_11;
    int8_t scan;        /* the scanner kind that uses the record */
    uint8_t more;       /* 0 for the last record of a list */
} AiTarget;

/* A plan choice (7 bytes). */
typedef struct {
    int8_t plan, chance;
    uint32_t targets;   /* address of the target records */
    uint8_t more;
} AiChoice;

PlanStep ai_plan_step(int plan, int step);
AiTarget ai_target(uint32_t addr);
AiChoice ai_choice(uint32_t addr);
uint16_t ai_mood_mask(uint32_t addr);
uint32_t ai_mood_choices(uint32_t addr);

/* A candidate destination of a plan (22 bytes in SKULL.EXE). */
typedef struct {
    int8_t cost;
    uint8_t value;
    uint16_t destination;   /* filled by the search: x bits 0-4, y bits 5-9, map bits 10-15 */
    uint16_t found;         /* filled by the search */
    int8_t path_length;     /* filled by the search */
    int8_t kind;
    uint16_t item;
    uint16_t mask;
    uint16_t marker;
    int8_t plan, scan_arg;
    uint8_t unknown_10;
    int8_t scan_kind;
    uint32_t targets;
} AiCandidate;

#define AI_CANDIDATE_MAX 16
#define AI_PATH_MAX 0x31

/* The cached plan state of a group (14 bytes in SKULL.EXE): it lasts from one update to the next. */
typedef struct {
    uint8_t path_length;
    uint8_t path_left;
    int8_t scan_kind;
    int8_t kind;
    uint16_t item, mask, found;
    uint32_t targets;
} AiPlanCache;

/* The item counts of a target list (32 bytes in SKULL.EXE). */
typedef struct {
    uint32_t targets;
    int8_t scan_kind;
    int8_t total, wanted, extra;
    int8_t counts[24];
} AiItemCounts;

/* The context of a creature update (the 0x350-byte block of SKULL.EXE). */
typedef struct {
    uint16_t group_thing;
    uint8_t *group;             /* the group record */
    const CreatureInfo *info;
    ActiveGroup *active;
    const uint8_t *anim_step;   /* the current animation step (4 bytes) */
    uint16_t *anim;             /* the animation sequence and state words */
    TimelineEvent event;        /* the next update event: x in data[0], y in data[1] */
    uint8_t previous_action;
    uint8_t command_result;     /* AI_RESULT_* */
    uint8_t removed;            /* the group left the dungeon */
    uint8_t map;
    int16_t arg1, arg2;         /* the arguments of the plan command */
    uint16_t move_flags, attack_flags, door_flags, item_flags, actuator_flags;
    uint16_t candidate_mask;
    int16_t attr7;
    int16_t class_index;        /* -1 until read */
    int16_t mood;
    uint32_t mood_entry;        /* address of the mood entry */
    uint8_t counts_dirty;
    uint8_t alert;
    AiItemCounts item_counts[4];
    uint16_t path[AI_PATH_MAX];
    uint8_t candidate_count;
    int8_t chosen;
    AiCandidate candidates[AI_CANDIDATE_MAX];
    AiPlanCache cache;
    uint16_t *cached_path;
    uint8_t active_flag;        /* a context is in use: a nested update saves it */
    uint8_t prepared;
    uint8_t plan_flags;
    uint8_t search_flags;
    uint8_t *filters[0x2a];     /* item class bitmaps of the filters, NULL until parsed */
    int16_t filter_free;
    uint8_t *scratch;
    int scratch_used;
} AiContext;

extern AiContext ai;

/* The party position as the current map sees it (g_ai_target_* of SKULL.EXE). */
typedef struct { int16_t x, y, map, dir; } AiPartyView;
extern AiPartyView ai_party;

/* The square that the AI read last (g_ai_sq_* of SKULL.EXE); x -1: none. */
typedef struct {
    int16_t x, y, map;
    uint8_t square;
    uint16_t first_thing, first_object, first_group;
    int16_t cost;
} AiSquare;
extern AiSquare ai_square;

uint8_t ai_action_flag(int8_t action);
int creature_ai_class(void);
uint8_t *ai_scratch_push(void);
void ai_scratch_pop(void);
uint16_t *ai_store_path(int size);
uint16_t *ai_stored_path(void);
void creature_free_cached_ai_data(uint16_t group);
void creature_ai_update_group(void);
void creature_ai_decide(void);
int creature_ai_plan_flags(void);
int creature_ai_execute_action(void);
int creature_group_jumps(uint16_t group);
int creature_damage_current_group(uint16_t damage);
void creature_delete_group_at(int x, int y, int mode, int sound_map);

/* Begins a creature update: saves the context of an update in progress; returns 1 when it saved one. */
int creature_ai_begin(uint16_t group, int x, int y, int type, AiContext *saved);
void creature_ai_end(int restored, const AiContext *saved);
void creature_ai_prepare(void);
void creature_ai_set_map(int map);

#endif
