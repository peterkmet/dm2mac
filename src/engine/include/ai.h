/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_AI_H
#define DM2_ENGINE_AI_H
/* Prefixes: ai_ */

/* The creature AI: its update context, the plan commands, and the record fields of the AI markers. */

#include "creature.h"
#include "dungeon.h"
#include "timeline.h"
#include <stdbool.h>
#include <stdint.h>

/* Record fields of the things of this module; dungeon.h holds the record layout. */
static inline uint16_t ai_marker_id(uint16_t text)
{
    return thing_field(text, TEXT_DATA, AI_MARKER_ID_SHIFT, AI_MARKER_ID_MASK);
}
static inline uint16_t ai_marker_kind(uint16_t text)
{
    return thing_field(text, TEXT_DATA, AI_MARKER_KIND_SHIFT, AI_MARKER_KIND_MASK);
}
/* The level delta that the AI marker is for. */
static inline uint16_t ai_marker_level(uint16_t text)
{
    return thing_field(text, TEXT_DATA, AI_MARKER_LEVEL_SHIFT, AI_MARKER_LEVEL_MASK);
}

/* The flags of the AI classes (ai_data.c). */
#define AI_CLASS_COUNT 47
extern const uint32_t g_ai_class_flags[AI_CLASS_COUNT];

/* A candidate destination of a plan: the 22-byte record of SKULL.EXE (g_ai_candidates, 0x7f674), same layout. */
typedef struct __attribute__((packed)) {
    int8_t cost;          /* +0x00 */
    uint8_t value;        /* +0x01 target value_9 */
    uint16_t destination; /* +0x02 filled by the search: x bits 0-4, y bits 5-9, map bits 10-15 */
    uint16_t found;       /* +0x04 filled by the search */
    int8_t path_length;   /* +0x06 filled by the search */
    int8_t kind;          /* +0x07 */
    uint16_t item;        /* +0x08 target item */
    uint16_t mask;        /* +0x0a target mask & candidate mask */
    uint16_t marker;      /* +0x0c */
    int8_t plan;          /* +0x0e */
    int8_t scan_arg;      /* +0x0f */
    uint8_t unknown_10;   /* +0x10 */
    int8_t scan_kind;     /* +0x11 */
    uint32_t targets;     /* +0x12 address of the target list */
} AiCandidate;

/* The size of a candidate record. */
#define AI_CANDIDATE_SIZE 22
_Static_assert(sizeof(AiCandidate) == AI_CANDIDATE_SIZE, "the candidates of SKULL.EXE have 22 bytes");

/* The sizes of the candidates, the path, and the item filters of a creature update. */
#define AI_CANDIDATE_MAX 16
#define AI_PATH_BYTES    100 /* a path buffer: 2 bytes per step */
#define FILTER_SLOTS     42  /* the item filters of a creature update */
/* The bits of a shift count that the x86 shift uses: the low 5 bits. */
#define BIT_INDEX_MASK   0x1f

/* The plan state of a group that lasts from one update to the next: the 14 bytes at 0x7f7d4 of SKULL.EXE. */
typedef struct __attribute__((packed)) {
    uint8_t path_length; /* 0x7f7d4 */
    uint8_t path_left;   /* 0x7f7d5 */
    int8_t scan_kind;    /* 0x7f7d6 */
    int8_t kind;         /* 0x7f7d7, -1 none */
    uint16_t item;       /* 0x7f7d8 */
    uint16_t mask;       /* 0x7f7da */
    uint16_t found;      /* 0x7f7dc */
    uint32_t targets;    /* 0x7f7de */
} AiPlanCache;

/* The sizes of the item counts of a creature update. */
#define AI_COUNT_TARGETS    6 /* the target records that a count list holds */
#define AI_COUNT_ENTRY_SIZE 4 /* per target: carried, still wanted, still wanted extra, unused */
#define AI_COUNT_LISTS      4 /* the count lists of one creature update */

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
    const CreatureInfo *info;
    ActiveGroup *active;
    const uint8_t *anim_step; /* the current animation step (4 bytes) */
    uint16_t *anim;           /* the animation sequence and state words */
    TimelineEvent event;      /* the next update event: x in data[0], y in data[1] */
    uint8_t previous_action;
    uint8_t command_result; /* AI_RESULT_* */
    uint8_t removed;        /* the group left the dungeon */
    uint8_t map;
    int16_t arg1, arg2; /* the arguments of the plan command */
    uint16_t move_flags, attack_flags, door_flags, item_flags, actuator_flags;
    uint16_t candidate_mask;
    int16_t attr7;
    int16_t class_index; /* -1 until read */
    int16_t mood;
    uint32_t mood_entry; /* address of the mood entry */
    uint8_t counts_dirty;
    uint8_t alert;
    AiItemCounts item_counts[AI_COUNT_LISTS];
    uint8_t path[AI_PATH_BYTES]; /* 0x7f60a: the path of the search (2 bytes per step) */
    uint8_t candidate_count;
    int8_t chosen;
    AiCandidate candidates[AI_CANDIDATE_MAX];
    AiPlanCache cache;
    uint8_t *cached_path; /* 0x7f7e2: the stored path of the group, NULL none */
    uint8_t active_flag;  /* a context is in use: a nested update saves it */
    uint8_t prepared;
    uint8_t plan_flags;
    uint8_t search_flags;
    uint8_t *filters[FILTER_SLOTS]; /* item class bitmaps of the filters, NULL until parsed */
    int16_t filter_free;
    uint8_t *scratch;
    int scratch_used;
} AiContext;

extern AiContext g_ai;

/* Command results (g_ai.command_result and the plan commands). */
#define AI_RESULT_ISSUED 0xfc /* an action was set: the update ends */
#define AI_RESULT_FAIL   0xfd
#define AI_RESULT_DONE   0xfe /* nothing to do: the goal is reached */
#define AI_EXECUTE       0x80 /* ai_try_* mode bit: set the action when the test passes */

/* The party position as the current map sees it (g_ai_target_* of SKULL.EXE). */
typedef struct {
    int16_t x, y, map, dir;
} AiPartyView;
extern AiPartyView g_ai_party;

/* The distances from the party of the squares of a map (32 x 32, 0: not reached; bit 7: next to a reached square). */
#define DISTANCE_MAP_SIDE MAP_SIDE_MAX /* the cells of a distance map: x * 32 + y */
typedef struct {
    int16_t map, width;
    uint8_t cells[DISTANCE_MAP_SIDE * DISTANCE_MAP_SIDE];
} DistanceMap;
/* The marks of a distance map cell. */
#define DISTANCE_NEXT_TO 0x80
/* The distance of a cell that the search did not reach. */
#define DISTANCE_FAR     127
extern DistanceMap g_party_distance, g_alt_distance;

/* Setup */

/* Forgets the AI data of all groups (a load). */
void ai_forget_all(void);
/* Forgets the plan state and path of a group. */
void ai_free_cached_data(uint16_t group);

/* The update context */

/* Begins a creature update: saves the context of an update in progress; true when it saved one. */
bool ai_begin(uint16_t group, int16_t x, int16_t y, int type, AiContext *saved);
/* Ends a creature update: frees the context, or restores the update that the nested one interrupted. */
void ai_end(bool restored, const AiContext *saved);
/* Takes a scratch block for the item filters of the update; exported for the tests. */
uint8_t *ai_scratch_push(void);
/* Gives back the scratch block of the update; exported for the tests. */
void ai_scratch_pop(void);
/* Reads the skills of the creature and its plan state once per update, before the first test.
   It is exported for the tests. */
void ai_prepare(void);
/* The stored path buffer of the group, big enough for path_size bytes; exported for the tests. */
uint8_t *ai_store_path(int path_size);
/* The flags of an action: how it moves, waits, or follows the path. */
uint8_t ai_action_flag(int action);

/* Plan commands that the merchant commands use */

/* Plan command: the group in front carries an item of filter g_ai.arg1 in cell g_ai.arg2. */
int ai_cmd_front_group_carries(void);
/* Plan command: the creature puts down a carried item of filter g_ai.arg1. */
int ai_cmd_seek_with_carried(void);

/* The update of a creature that acts */

/* The update of a creature that acts: the next action and its animation, then the next update event. */
void ai_update_group(void);

/* Item filters (ai_move.c) */

/* True (1, or FILTER_INVERT for an inverted filter) when thing belongs to the item filter of the group in the AI
   context: classes of items, creatures, and wall actuators that the creature text lists, or money boxes. */
int ai_thing_matches_filter(uint16_t thing, int filter);
/* The first item of filter in a thing list (cell CELL_ANY: any cell), THING_ENDOFLIST for none. */
uint16_t ai_find_matching_item(int filter, uint16_t first_thing, int16_t cell);

/* Perception and items of the AI (ai_move.c), exported for the tests */

/* True when a square stops a line of sight or of fire: walls, doors, clouds, and creatures that hide.
   It is exported for the tests. */
bool ai_square_blocks_line(int16_t x, int16_t y);
/* A line toward the party: the party square blocks it (the line ends there); exported for the tests. */
bool ai_blocks_line_to_target(int16_t x, int16_t y);
/* A line test that ignores the square of the group in the AI context; exported for the tests. */
bool ai_blocks_line_except_self(int16_t x, int16_t y);
/* The first object of the square in the AI cache, read once; exported for the tests. */
uint16_t ai_cached_first_object(void);
/* The first creature group of the square in the AI cache, read once; exported for the tests. */
uint16_t ai_cached_first_group(void);
/* An item of filter inside a chest or in the possessions of a group that the creature may take from: the chest
   cell (0 to 3), POSSESSION_CELL_BASE plus the cell of the possession, -1 none; exported for the tests. */
int ai_find_matching_item_inside(uint16_t thing, int filter, int side);
/* The wall actuator in cell of a square thing list for mode: the first slot that takes filter, else the last
   actuator of the cell; THING_NONE for none; exported for the tests. */
uint16_t ai_find_wall_actuator(uint16_t first_thing, int16_t cell, int mode, int filter);
/* True when the wall actuator reacts to the creature in mode (ACTUATOR_MODE_*) with an item of filter; state: the
   state bits of the creature slot; exported for the tests. */
int ai_wall_actuator_accepts(uint16_t actuator, int mode, int filter, uint16_t state);
/* The AI marker text on the square in the AI cache for the level delta (1: also the markers of level 2 with bit 0)
   whose kind is in id_mask: its id (0 to 31), -1 none; exported for the tests. */
int ai_find_marker(int level_delta, uint16_t id_mask);
/* Points g_ai.cached_path to the stored path of the group; a lost path clears the path state.
   It is exported for the tests. */
void ai_load_cached_path(void);

#endif /* DM2_ENGINE_AI_H */
