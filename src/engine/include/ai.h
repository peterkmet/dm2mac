/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_AI_H
#define DM2_ENGINE_AI_H
/* Prefixes: ai_ */

/* The creature AI: its update context, the plan commands, the record fields of the AI markers, and the perception,
   tests, and path search of creature movement. */

#include <stdbool.h>
#include <stdint.h>
#include "creature.h"
#include "dungeon.h"
#include "timeline.h"

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

/* A candidate destination of a plan: a 22-byte record with the layout of the original. */
typedef struct __attribute__((packed)) {
    int8_t cost;          /* +0x00 */
    uint8_t value;        /* +0x01 the value of the target */
    uint16_t destination; /* +0x02 filled by the search: x bits 0-4, y bits 5-9, map bits 10-15 */
    uint16_t found;       /* +0x04 filled by the search */
    int8_t path_length;   /* +0x06 filled by the search */
    int8_t kind;          /* +0x07 */
    uint16_t item;        /* +0x08 target item */
    uint16_t mask;        /* +0x0a target mask & candidate mask */
    uint16_t marker;      /* +0x0c */
    int8_t plan;          /* +0x0e */
    int8_t scan_arg;      /* +0x0f */
    uint8_t kind_flags;   /* +0x10 the flags of the target kind (KIND_*) */
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

/* The plan state of a group that lasts from one update to the next: 14 bytes with the layout of the original. */
typedef struct __attribute__((packed)) {
    uint8_t path_length;
    uint8_t path_left;
    int8_t scan_kind;
    int8_t kind; /* -1 none */
    uint16_t item;
    uint16_t mask;
    uint16_t found;
    uint32_t targets;
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
    TimelineEvent event;      /* the next update event: x in data[EVENT_X], y in data[EVENT_Y] */
    uint8_t previous_action;
    uint8_t command_result; /* AI_RESULT_* */
    bool removed;           /* the group left the dungeon */
    uint8_t map;
    int16_t arg1, arg2; /* the arguments of the plan command */
    uint16_t move_flags, attack_flags, door_flags, item_flags, actuator_flags;
    uint16_t candidate_mask;
    int16_t marker_mask; /* the kinds of the AI markers that the creature heeds */
    int16_t class_index; /* -1 until read */
    int16_t mood;
    uint32_t mood_entry; /* address of the mood entry */
    bool counts_dirty;
    bool alert;
    AiItemCounts item_counts[AI_COUNT_LISTS];
    uint8_t path[AI_PATH_BYTES]; /* the path of the search (2 bytes per step) */
    uint8_t candidate_count;
    int8_t chosen;
    AiCandidate candidates[AI_CANDIDATE_MAX];
    AiPlanCache cache;
    uint8_t *cached_path; /* the stored path of the group, NULL none */
    bool active_flag;     /* a context is in use: a nested update saves it */
    bool prepared;
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

/* The party position as the current map sees it. */
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

/* Where an item of a filter is inside a chest or a group: the cell (-1 none), and whether the cell is a cell of the
   possessions of a group (else the cell of the chest). */
typedef struct {
    int16_t cell;
    bool in_possessions;
} AiInsideItem;

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

/* True when a square stops a line of sight or of fire: walls, doors, clouds, and creatures that hide.
   It is exported for the tests. */
bool ai_square_blocks_line(Location at);
/* A line toward the party: the party square blocks it (the line ends there); exported for the tests. */
bool ai_blocks_line_to_target(Location at);
/* A line test that ignores the square of the group in the AI context; exported for the tests. */
bool ai_blocks_line_except_self(Location at);
/* The first object of the square in the AI cache, read once; exported for the tests. */
uint16_t ai_cached_first_object(void);
/* The first creature group of the square in the AI cache, read once; exported for the tests. */
uint16_t ai_cached_first_group(void);
/* An item of filter inside a chest or in the possessions of a group that the creature may take from: the cell of the
   chest or of the possession, cell -1 for none; exported for the tests. */
AiInsideItem ai_find_matching_item_inside(uint16_t thing, int16_t filter, int side);
/* The wall actuator in cell of a square thing list for mode: the first slot that takes filter, else the last
   actuator of the cell; THING_NONE for none; exported for the tests. */
uint16_t ai_find_wall_actuator(uint16_t first_thing, int16_t cell, int16_t mode, int16_t filter);
/* True when the wall actuator reacts to the creature in mode (ACTUATOR_MODE_*) with an item of filter; state: the
   state bits of the creature slot; exported for the tests. */
bool ai_wall_actuator_accepts(uint16_t actuator, int16_t mode, int16_t filter, uint16_t state);

/* True when thing belongs to the item filter of the group in the AI context: classes of items, creatures, and wall
   actuators that the creature text lists, or money boxes. */
bool ai_thing_matches_filter(uint16_t thing, int16_t filter);
/* The first item of filter in a thing list (cell CELL_ANY: any cell), THING_ENDOFLIST for none. */
uint16_t ai_find_matching_item(int16_t filter, uint16_t first_thing, int16_t cell);

/* The AI marker text on the square in the AI cache for the level delta (1: also the markers of level 2 with bit 0)
   whose kind is in id_mask: its id (0 to 31), -1 none; exported for the tests. */
int16_t ai_find_marker(int16_t level_delta, uint16_t id_mask);
/* Points g_ai.cached_path to the stored path of the group; a lost path clears the path state.
   It is exported for the tests. */
void ai_load_cached_path(void);
/* After the party moved or the squares changed: the distance maps of the squares around the party on the party map
   and on the map of the alternate view; after a view change also the light sums of the light sources near the party. */
void ai_refresh_party_maps(void);

/* The flags, modes, kinds and targets of the AI command tests. */
/* Bits of a packed move command byte (ai_move_test) and the mode mask of the other command tests (AiTest). */
#define AI_EXECUTE               0x80 /* set the action when the test passes */
#define AI_MODE_MASK             0x7f
#define AI_MOVE_KIND_MASK        0x1f /* ai_try_move: the kind of the move */
#define AI_MOVE_NO_MOVERS        0x40 /* ... groups that move do not block */
#define AI_MOVE_ATTACK_GROUPS    0x20 /* ... a group of FILTER_ATTACKED_GROUPS in the way gets attacked */
#define AI_MOVE_FLAG_BITS        0xe0 /* ... the bits above the kind */
/* The item filters that the AI tests use. */
#define FILTER_ATTACKED_GROUPS   0x09 /* the groups that a creature attacks when they are in its way */
#define FILTER_WEAPON_HANDS      0x0b /* the items in the champion hands that creatures notice */
/* The kinds of a move test (bits 0-4 of the command of ai_try_move). */
#define MOVE_KIND_PLAIN          0x00
#define MOVE_KIND_KEEP_DISTANCE  0x01 /* the creature keeps a distance to the party */
#define MOVE_KIND_NONE           0x02 /* no move: a search tests only its start square */
#define MOVE_KIND_OPEN           0x03 /* the quick test of the search: no wall */
#define MOVE_KIND_SAFE           0x04 /* the creature avoids poison clouds and missiles */
#define MOVE_KIND_SAFE_FLEE      0x05 /* ... and the missiles that fly toward the square */
#define MOVE_KIND_STEP_BACK      0x06
#define MOVE_KIND_SIGHT          0x07 /* the quick test of the light search: no obstacle to the sight */
#define MOVE_KIND_COST           0x08 /* the quick test of the cost search */
/* Search target kinds (AiCandidate.kind). */
#define TARGET_ANY               0x00
#define TARGET_HOME              0x01
#define TARGET_PARTY_VIEW        0x02
#define TARGET_ACTIVATION        0x03
#define TARGET_PARTY_RANGE_2     0x04
#define TARGET_FLEE              0x05
#define TARGET_SHOOT_PARTY       0x06
#define TARGET_MELEE_PARTY       0x07
#define TARGET_MELEE_LINE        0x08
#define TARGET_SHOOT_LINE        0x09
#define TARGET_STEAL             0x0a
#define TARGET_MARKER            0x0b
#define TARGET_NEAR_MARKER       0x0c
#define TARGET_WANDER            0x0d
#define TARGET_RANDOM            0x0e
#define TARGET_ITEM              0x0f
#define TARGET_ITEM_2            0x10
#define TARGET_ITEM_WALL         0x11
#define TARGET_GROUP_IN_FRONT    0x12
#define TARGET_GROUP_FACING      0x13
#define TARGET_DAMAGE_GROUP      0x14
#define TARGET_MOVE_TEST         0x15
#define TARGET_ACTUATOR          0x16
#define TARGET_LIGHT             0x17
#define TARGET_AI_MARKER         0x18
#define TARGET_MARKER_SIDE       0x19
#define TARGET_DOOR              0x1a
#define TARGET_PARTY_DISTANCE    0x1b
/* The modes of ai_try_attack. */
#define ATTACK_MODE_PARTY        0x00
#define ATTACK_MODE_PARTY_RANGED 0x01
#define ATTACK_MODE_GROUP        0x02
#define ATTACK_MODE_ANY          0x03
#define ATTACK_MODE_DOOR         0x04
/* The modes of ai_try_door_action. */
#define DOOR_MODE_OPEN           0x00
#define DOOR_MODE_CLOSE          0x01
#define DOOR_MODE_CLOSE_MARKED   0x02
/* The modes of ai_try_take_item. */
#define ITEM_MODE_TAKE           0x00
#define ITEM_MODE_GIVE           0x01
/* The modes of ai_try_use_wall_actuator. */
#define ACTUATOR_MODE_USE        0x00
#define ACTUATOR_MODE_PUT        0x01
#define ACTUATOR_MODE_TAKE       0x02

/* g_ai.move_flags: the squares that the creature may enter. */
#define MOVE_WALL                0x0001
#define MOVE_FLOOR               0x0002
#define MOVE_CLOSED_PIT          0x0004
#define MOVE_OPEN_PIT            0x0008
#define MOVE_FALL                0x0010
#define MOVE_INVISIBLE_PIT       0x0020
#define MOVE_HIDDEN_FAKE_WALL    0x0040
#define MOVE_FAKE_WALL           0x0080
#define MOVE_STAIRS              0x0100
#define MOVE_LOCKED_DOOR         0x0200
#define MOVE_TELEPORTER          0x0400
#define MOVE_PARTY               0x0800
#define MOVE_GROUPS              0x1000
#define MOVE_LEVELS              0x2000
#define MOVE_DOOR                0x4000
#define MOVE_FALLS               0x8000
#define MOVE_LEVEL_FLAGS         0x0118
#define MOVE_LEVEL_UP_FLAGS      0x0110 /* the moves of a level change up (DIR_LEVEL_UP) */
#define MOVE_LEVEL_DOWN_FLAGS    0x0108 /* ... down (DIR_LEVEL_DOWN) */
#define MOVE_FLAGS_LIGHT         0x0227
#define MOVE_FLAGS_PARTY_MAP     0x36e7
#define MOVE_FLAGS_NO_FALL       0x7db7
/* g_ai.door_flags. */
#define DOOR_SKILLS_OPEN         0x006f
#define DOOR_SKILLS_CLOSE        0x0073
#define DOOR_SKILL_BUTTON        0x0003
#define DOOR_SKILL_SHOOT_BUTTON  0x0001
#define DOOR_SKILL_PRESS_BUTTON  0x0002
#define DOOR_SKILL_SPELL         0x0004 /* breaks a door that magic breaks with a fireball or lightning */
#define DOOR_SKILL_BASH          0x0008 /* breaks a breakable door with hits */
#define DOOR_SKILL_CLOSE_BEHIND  0x0020 /* marks a door that it opens; closes a door beside or behind it */
#define DOOR_SKILL_HANDLE        0x0040
/* AI actions (ActiveGroup.action). A pair or range of codes runs the same act with a variant. */
#define ACTION_NONE              0x00
#define ACTION_MOVE              0x01
#define ACTION_MOVE_NEAR         0x02
#define ACTION_STEP_RIGHT        0x03
#define ACTION_STEP_LEFT         0x04
#define ACTION_FALL              0x05
#define ACTION_TURN_LEFT         0x06
#define ACTION_TURN_RIGHT        0x07
#define ACTION_MELEE             0x08
#define ACTION_TURN_BACK         0x09
#define ACTION_STEAL             0x0a
#define ACTION_OPERATE_DOOR      0x0b
#define ACTION_TAKE_LEFT         0x0c
#define ACTION_TAKE_RIGHT        0x0d
#define ACTION_THROW             0x0e
#define ACTION_THROW_2           0x0f
#define ACTION_IDLE              0x11
#define ACTION_VANISH            0x13
#define ACTION_TURN_GROUP        0x15
#define ACTION_TURN_GROUP_2      0x16
#define ACTION_GIVE              0x17
#define ACTION_TAKE_FROM_GROUP   0x18
#define ACTION_PUT_IN_WALL       0x19
#define ACTION_TAKE_FROM_WALL    0x1a
#define ACTION_CHECK_AHEAD       0x1d
#define ACTION_PLACE_WAIT        0x1e
#define ACTION_STAND             0x23
#define ACTION_MELEE_2           0x26
#define ACTION_SHOOT             0x27
#define ACTION_SHOOT_2           0x28
#define ACTION_DROP_HERE         0x29
#define ACTION_DROP_HERE_2       0x2a
#define ACTION_TAKE_FRONT        0x2b
#define ACTION_TAKE_SIDE         0x2c
#define ACTION_DROP_AHEAD        0x2d
#define ACTION_DROP_AHEAD_2      0x2e
#define ACTION_USE_ACTUATOR      0x2f
#define ACTION_USE_ACTUATOR_LAST 0x31
#define ACTION_WAIT_FIRST        0x32 /* 0x32 to 0x34: wait 0 to 2 game steps without animation */
#define ACTION_RESCAN            0x33
#define ACTION_WAIT_LAST         0x34
#define ACTION_STAIRS_UP         0x35
#define ACTION_STAIRS_DOWN       0x36
#define ACTION_FALL_IN_PIT       0x37
#define ACTION_PIT               0x38
#define ACTION_LINK_UP           0x39
#define ACTION_LINK_DOWN         0x3a
#define ACTION_TRANSFORM         0x3b
#define ACTION_MORPH             0x3c
#define ACTION_SPECIAL           0x3d
#define ACTION_SPECIAL_LAST      0x40
#define ACTION_DODGE             0x55

/* The light sums of the last light search and the party map of the last refresh; exported for the tests. */
extern int16_t g_cached_light_sum, g_cached_explosion_light_sum, g_last_party_map;
/* The shift register that picks the first search direction; exported for the tests. */
extern uint16_t g_search_direction_bits;

#endif /* DM2_ENGINE_AI_H */
