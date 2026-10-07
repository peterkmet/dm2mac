#ifndef AI_H
#define AI_H
#include <stdint.h>
#include "creature.h"
#include "dungeon.h"
#include "timeline.h"

/* Prefixes: ai_ */

/* Record fields of the things of this module; dungeon.h holds the record layout. */
static inline uint16_t ai_marker_id(uint16_t text) { return thing_field(text, TEXT_DATA, AI_MARKER_ID_SHIFT, AI_MARKER_ID_MASK); }
static inline uint16_t ai_marker_kind(uint16_t text)
{
    return thing_field(text, TEXT_DATA, AI_MARKER_KIND_SHIFT, AI_MARKER_KIND_MASK);
}
/* The level delta that the AI marker is for. */
static inline uint16_t ai_marker_level(uint16_t text)
{
    return thing_field(text, TEXT_DATA, AI_MARKER_LEVEL_SHIFT, AI_MARKER_LEVEL_MASK);
}

/* The AI tables of SKULL.EXE (ai_data.c). Records link each other by their SKULL.EXE address. */
#define AI_SCRIPTS_BASE 0x73020u
#define AI_PLAN_COUNT 63
#define AI_CLASS_COUNT 47
#define AI_TARGET_SIZE 14
#define AI_PLAN_STEP_SIZE 7
#define AI_CHOICE_SIZE 7
#define AI_MOOD_ENTRY_SIZE 6         /* a mood mask word and the address of its plan choices */
#define AI_SCRIPTS_SIZE 8030
#define AI_ACTION_COUNT 86
#define AI_MOVE_TEST_KINDS 20        /* the move test kinds of ai_command_enabled */
#define AI_TARGET_KIND_COUNT 28
#define AI_POSSESSION_KINDS 16       /* the display kinds of possession_cells, with the data after the table */
#define AI_AFTER_COMMAND_BYTES 60
#define AI_TARGET_IDLE 0x731d2u      /* the target of a creature that waits or wanders */
#define AI_CHOICES_NONE 0x73399u     /* the plan choices of a creature that does nothing */
#define AI_CHOICES_WANDER 0x73392u   /* ... of a creature that wanders at random */
extern const uint8_t ai_scripts[AI_SCRIPTS_SIZE];
extern const uint32_t ai_plans[AI_PLAN_COUNT];
extern const uint32_t ai_class_flags[AI_CLASS_COUNT];
extern const uint8_t ai_action_flags[AI_ACTION_COUNT];
extern const uint32_t ai_behaviors[AI_CLASS_COUNT];
extern const uint8_t ai_command_enabled[AI_MOVE_TEST_KINDS];
extern const uint8_t ai_after_command_table[AI_AFTER_COMMAND_BYTES];
extern const uint8_t ai_kind_flags[AI_TARGET_KIND_COUNT];
extern const uint8_t ai_possession_cells[AI_POSSESSION_KINDS][DIR_COUNT];

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

/* A candidate destination of a plan: the 22-byte record of SKULL.EXE (g_ai_candidates, 0x7f674), same layout. */
typedef struct __attribute__((packed)) {
    int8_t cost;            /* +0x00 */
    uint8_t value;          /* +0x01 target value_9 */
    uint16_t destination;   /* +0x02 filled by the search: x bits 0-4, y bits 5-9, map bits 10-15 */
    uint16_t found;         /* +0x04 filled by the search */
    int8_t path_length;     /* +0x06 filled by the search */
    int8_t kind;            /* +0x07 */
    uint16_t item;          /* +0x08 target item */
    uint16_t mask;          /* +0x0a target mask & candidate mask */
    uint16_t marker;        /* +0x0c */
    int8_t plan;            /* +0x0e */
    int8_t scan_arg;        /* +0x0f */
    uint8_t unknown_10;     /* +0x10 */
    int8_t scan_kind;       /* +0x11 */
    uint32_t targets;       /* +0x12 address of the target list */
} AiCandidate;

#define AI_CANDIDATE_SIZE 22
_Static_assert(sizeof(AiCandidate) == AI_CANDIDATE_SIZE, "the candidates of SKULL.EXE have 22 bytes");

#define AI_CANDIDATE_MAX 16
#define SHIFT_COUNT_MASK 0x1f       /* the x86 shift uses the low 5 bits of the count */
#define FOUND_NONE 0xffff           /* a candidate or plan state without a found thing or marker */
#define AI_PATH_MAX 49
#define AI_PATH_BYTES 100           /* a path buffer: 2 bytes per step */
#define FILTER_SLOTS 42             /* the item filters of a creature update */

/* The plan state of a group that lasts from one update to the next: the 14 bytes at 0x7f7d4 of SKULL.EXE. */
typedef struct __attribute__((packed)) {
    uint8_t path_length;    /* 0x7f7d4 */
    uint8_t path_left;      /* 0x7f7d5 */
    int8_t scan_kind;       /* 0x7f7d6 */
    int8_t kind;            /* 0x7f7d7, -1 none */
    uint16_t item;          /* 0x7f7d8 */
    uint16_t mask;          /* 0x7f7da */
    uint16_t found;         /* 0x7f7dc */
    uint32_t targets;       /* 0x7f7de */
} AiPlanCache;

#define AI_COUNT_TARGETS 6          /* the target records that a count list holds */
#define AI_COUNT_ENTRY_SIZE 4       /* per target: carried, still wanted, still wanted extra, unused */
#define AI_COUNT_LISTS 4            /* the count lists of one creature update */

/* The item counts of a target list (32 bytes in SKULL.EXE). */
typedef struct {
    uint32_t targets;
    int8_t scan_kind;
    int8_t total, wanted, extra;
    int8_t counts[AI_COUNT_TARGETS * AI_COUNT_ENTRY_SIZE];
} AiItemCounts;

/* The context of a creature update (the 0x350-byte block of SKULL.EXE). */
typedef struct {
    uint16_t group_thing;
    void *layout_padding;       /* the place of the old group record pointer: tests/test_ai_move.py reads fixed offsets */
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
    AiItemCounts item_counts[AI_COUNT_LISTS];
    uint8_t path[AI_PATH_BYTES]; /* 0x7f60a: the path of the search (2 bytes per step) */
    uint8_t candidate_count;
    int8_t chosen;
    AiCandidate candidates[AI_CANDIDATE_MAX];
    AiPlanCache cache;
    uint8_t *cached_path;       /* 0x7f7e2: the stored path of the group, NULL none */
    uint8_t active_flag;        /* a context is in use: a nested update saves it */
    uint8_t prepared;
    uint8_t plan_flags;
    uint8_t search_flags;
    uint8_t *filters[FILTER_SLOTS]; /* item class bitmaps of the filters, NULL until parsed */
    int16_t filter_free;
    uint8_t *scratch;
    int scratch_used;
} AiContext;

extern AiContext ai;

/* Command results (ai.command_result and the plan commands). */
#define AI_RESULT_ISSUED 0xfc       /* an action was set: the update ends */
#define AI_RESULT_FAIL 0xfd
#define AI_RESULT_DONE 0xfe         /* nothing to do: the goal is reached */
#define AI_EXECUTE 0x80             /* creature_ai_try_* mode bit: set the action when the test passes */

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

/* The distances from the party of the squares of a map (32 x 32, 0: not reached; bit 7: next to a reached square). */
#define DISTANCE_MAP_SIDE MAP_SIDE_MAX     /* the cells of a distance map: x * 32 + y */
typedef struct { int16_t map, width; uint8_t cells[DISTANCE_MAP_SIDE * DISTANCE_MAP_SIDE]; } DistanceMap;
#define DISTANCE_NEXT_TO 0x80
#define DISTANCE_FAR 127
extern DistanceMap party_distance, alt_distance;

uint8_t ai_action_flag(int8_t action);
/* The target square of the action that runs (g_ai_dest_x/y of SKULL.EXE). */
extern int16_t ai_dest_x, ai_dest_y;
int ai_class(void);
uint8_t *ai_scratch_push(void);
void ai_scratch_pop(void);
uint8_t *ai_store_path(int size);
void ai_free_cached_data(uint16_t group);
/* Plan commands that the merchant commands use: the group in front carries an item of filter ai.arg1 in cell
   ai.arg2; the creature puts down a carried item of filter ai.arg1. */
int ai_cmd_front_group_carries(void);
int ai_cmd_seek_with_carried_item(void);
/* Forgets the AI data of all groups (a load). */
void ai_forget_all(void);
void ai_update_group(void);
void ai_decide(void);
int ai_plan_flags(void);
int ai_execute_action(void);

/* Begins a creature update: saves the context of an update in progress; returns 1 when it saved one. */
int ai_begin(uint16_t group, int x, int y, int type, AiContext *saved);
void ai_end(int restored, const AiContext *saved);
void ai_prepare(void);
void ai_set_map(int map);

/* ai_move.c */
/* The stored path of the group in the AI context, NULL when it has none. */
uint8_t *ai_find_path(void);

/* Perception, movement tests, and path search (ai_move.c). */
bool ai_square_is_obstacle(int x, int y);
bool ai_square_blocks_line(int x, int y);
bool ai_square_blocks_line_to_target(int x, int y);
bool ai_square_blocks_line_except_self(int x, int y);
int ai_trace_line(int x1, int y1, int x2, int y2, bool (*blocked)(int x, int y));
void ai_cache_square(int x, int y);
uint16_t ai_cached_first_object(void);
uint16_t ai_cached_first_group(void);
int ai_turn_toward(int dir);
/* distance_map: the map of the search (4 bytes per square, 32 squares per column); out_location gets the target. */
int ai_find_target_in_lines(int filter, uint8_t *distance_map, uint16_t *out_location, int x, int y, int dir, int ranged);
int ai_try_attack(int mode, int x, int y, int target_x, int target_y, int dir);
int ai_try_door_action(int mode, int x, int y, int door_x, int door_y, int dir);
int ai_projectile_incoming(int x, int y);
int ai_try_move(int command, int x, int y, int dest_x, int dest_y, int dir);
int ai_find_matching_item_inside(uint16_t thing, int filter, int side);
int ai_try_take_item(int mode, int x, int y, int dest_x, int dest_y, int dir, int filter);
uint16_t ai_find_wall_actuator_in_cell(uint16_t first_thing, int cell, int mode, int filter);
int ai_wall_actuator_accepts(uint16_t actuator, int mode, int filter, int state);
int ai_try_use_wall_actuator(int mode, int x, int y, int dest_x, int dest_y, int dir, int filter);
int ai_thing_matches_filter(uint16_t thing, int filter);
uint16_t ai_find_matching_item(int filter, uint16_t first_thing, int cell);
void ai_remove_matching_items(uint16_t *list_head, int filter, int cell);
int ai_count_matching_items(int filter, uint16_t first_thing, int cell, int into_groups, int into_containers);
int ai_find_marker(int level_delta, uint16_t id_mask);
int ai_find_marker_by_id(int level_delta, int id);
/* The breadth-first search from (x, y) with the move command: the nearest of count candidates; returns its index,
   -1 for none. path gets the steps to it (2 bytes each). */
int ai_search_area(int x, int y, int command, int count, AiCandidate *candidates, uint8_t *path);
void ai_load_cached_path(void);
int ai_next_path_step(void);
int ai_plan_path(void);

#endif
