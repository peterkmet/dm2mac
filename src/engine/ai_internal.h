/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_AI_INTERNAL_H
#define DM2_ENGINE_AI_INTERNAL_H
/* Prefixes: ai_ */

/* The shared private declarations of the creature AI (ai.c, ai_data.c, ai_move.c): the AI tables of SKULL.EXE, the
   square that the AI read last, and the perception, command test and path functions of ai_move.c that ai.c calls. */

#include <stdbool.h>
#include <stdint.h>
#include "ai.h"
#include "dungeon.h"

/* The AI tables of SKULL.EXE (ai_data.c). Records link each other by their SKULL.EXE address. */
#define AI_SCRIPTS_BASE        0x73020u
#define AI_TARGET_IDLE         0x731d2u /* the target of a creature that waits or wanders */
#define AI_CHOICES_WANDER      0x73392u /* ... of a creature that wanders at random */
#define AI_CHOICES_NONE        0x73399u /* the plan choices of a creature that does nothing */
/* The sizes of the AI tables and of their records. */
#define AI_PLAN_COUNT          63
#define AI_TARGET_SIZE         14
#define AI_PLAN_STEP_SIZE      7
#define AI_CHOICE_SIZE         7
#define AI_MOOD_ENTRY_SIZE     6 /* a mood mask word and the address of its plan choices */
#define AI_SCRIPTS_SIZE        8030
#define AI_ACTION_COUNT        86
#define AI_MOVE_TEST_KINDS     20 /* the move test kinds of g_ai_command_enabled */
#define AI_TARGET_KIND_COUNT   28
#define AI_POSSESSION_KINDS    16 /* the display kinds of possession_cells, with the data after the table */
#define AI_AFTER_COMMAND_BYTES 60
extern const uint8_t g_ai_scripts[AI_SCRIPTS_SIZE];
extern const uint32_t g_ai_plans[AI_PLAN_COUNT];
extern const uint8_t g_ai_action_flags[AI_ACTION_COUNT];
extern const uint32_t g_ai_behaviors[AI_CLASS_COUNT];
extern const uint8_t g_ai_command_enabled[AI_MOVE_TEST_KINDS];
extern const uint8_t g_ai_after_command_table[AI_AFTER_COMMAND_BYTES];
extern const uint8_t g_ai_kind_flags[AI_TARGET_KIND_COUNT];
extern const uint8_t g_ai_possession_cells[AI_POSSESSION_KINDS][DIR_COUNT];

/* The longest path of the path search. */
#define AI_PATH_MAX 49
/* A candidate or plan state without a found thing or marker. */
#define FOUND_NONE  0xffff

/* The square that the AI read last (g_ai_sq_* of SKULL.EXE); x -1: none. */
typedef struct {
    int16_t x, y, map;
    uint8_t square;
    uint16_t first_thing, first_object, first_group;
    int16_t cost;
} AiSquare;
extern AiSquare g_ai_square;

/* A square of the current map. */
typedef struct {
    int16_t x, y;
} AiLocation;
/* The target square of an AI command test that means the square in the direction of the test. */
#define AI_SQUARE_IN_DIR ((AiLocation){-1, -1})

/* The square (x, y) of the current map. */
static inline AiLocation ai_location(int16_t x, int16_t y)
{
    return (AiLocation){(int16_t)x, (int16_t)y};
}
/* The square of the group in the AI context. */
static inline AiLocation ai_group_square(void)
{
    return ai_location(g_ai.event.data[0], g_ai.event.data[1]);
}

/* A target that the line search found: found false for none; location: x bits 0-4, y bits 5-9, map bits 10-15. */
typedef struct {
    bool found;
    uint16_t location;
} AiLineTarget;

/* The update context (ai.c) */

/* Makes map the current map of the AI and reads where the party stands as that map sees it. */
void ai_set_map(int16_t map);
/* The stored path of the group in the AI context, NULL when it has none. */
uint8_t *ai_find_path(void);

/* Perception, items, and paths (ai_move.c) */

/* True for squares that the sight of a creature does not cross: walls, closed doors that hide, closed fake walls. */
bool ai_square_is_obstacle(int16_t x, int16_t y);
/* Walks the squares of the line from (x2, y2) toward (x1, y1): the square distance of the ends (1 for adjacent
   squares), 0 when blocked() stops it. */
int ai_trace_line(int x1, int y1, int x2, int y2, bool (*blocked)(int16_t x, int16_t y));
/* Makes (x, y) of the current map the square that the AI reads; keeps the cache when it is the same square. */
void ai_cache_square(int16_t x, int16_t y);
/* Turns the group toward dir (action turn left or right); false when it already faces dir. */
bool ai_turn_toward(int16_t dir);
/* True when a dangerous projectile flies toward (x, y) within 3 squares in a direction that the creature sees. */
bool ai_projectile_incoming(int16_t x, int16_t y);
/* Removes and frees the items of filter from a thing list (cell CELL_ANY: any cell); the matching items inside a
   chest go too, and a matching chest gives its other contents to the list. */
void ai_remove_matching_items(uint16_t *list_head, int filter, int16_t cell);
/* The number of items of filter in a thing list (cell CELL_ANY: any cell), with the possessions of groups and the
   contents of chests on request. */
int ai_count_matching_items(int filter, uint16_t first_thing, int16_t cell, int into_groups, int into_containers);
/* The AI marker text on the square in the AI cache for the level delta with kind id: its id, -1 none. */
int ai_find_marker_by_id(int level_delta, int id);
/* The direction of the next step of the stored path into the action; returns the steps left (0: none). A group
   at the end of its path forgets the found square. */
int ai_next_path_step(void);
/* Checks that the stored path still leads to the target of the plan: the target is still where the search found it,
   and the next steps (at most 5) of the path are free; true when the path holds. */
bool ai_plan_path(void);

/* The tests of the AI commands and the path search (ai_move.c) */

/* Looks along the lines from the square from (dir -1: all four) for a target of filter in attack range: a group, or for a
   ranged attack a wall ornament that is a shooting target; then tests the attack from the farthest square of the
   line that the search reached no later. distance_map: the map of the search (4 bytes per square, 32 squares per
   column). It is exported for the tests. */
AiLineTarget ai_find_target_in_lines(int filter, uint8_t *distance_map, AiLocation from, int16_t dir, bool ranged);
/* Tests (and with AI_EXECUTE starts) an attack (ATTACK_MODE_*) from the square from on the square to
   (AI_SQUARE_IN_DIR: the square in dir). */
bool ai_try_attack(int mode, AiLocation from, AiLocation to, int16_t dir);
/* Tests (and with AI_EXECUTE starts) an action (DOOR_MODE_*) on the door on the square to
   (AI_SQUARE_IN_DIR: the square in dir). */
bool ai_try_door_action(int mode, AiLocation from, AiLocation to, int16_t dir);
/* Tests (and with AI_EXECUTE starts) a move (MOVE_KIND_*) of the group from the square from to the square to
   (AI_SQUARE_IN_DIR: the square in dir); dir DIR_LEVEL_UP and DIR_LEVEL_DOWN change the level, DIR_KEEP_FACING keeps the facing. */
bool ai_try_move(int command, AiLocation from, AiLocation to, int16_t dir);
/* Tests (and with AI_EXECUTE starts) taking or giving (ITEM_MODE_*) an item of filter on the square to
   (AI_SQUARE_IN_DIR: the square in dir): from the floor, an alcove, a chest, or a group. */
bool ai_try_take_item(int mode, AiLocation from, AiLocation to, int16_t dir, int filter);
/* Tests (and with AI_EXECUTE starts) the use of a wall actuator on the square to (AI_SQUARE_IN_DIR: the square in
   dir) from the slot of the creature type on that wall face. */
bool ai_try_use_wall_actuator(int mode, AiLocation from, AiLocation to, int16_t dir, int filter);
/* The breadth-first search from the square from with the move command: the nearest of count candidates; returns its index,
   -1 for none. out_path gets the steps to it (2 bytes each). */
int ai_search_area(AiLocation from, int command, int count, AiCandidate *candidates, uint8_t *out_path);

#endif /* DM2_ENGINE_AI_INTERNAL_H */
