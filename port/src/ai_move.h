#ifndef AI_MOVE_H
#define AI_MOVE_H
#include <stdint.h>
#include "ai.h"
#include "move.h"

/* Creature perception and movement: squares, lines of sight, item filters, the tests of the AI commands, and the
   path search (ai_move.c). The functions work on the AI context (ai.h) and set ai.command_result. */

/* The command results of the AI commands (ai.command_result). */
#define AI_RESULT_ACTING 0xfc   /* the command set the action of the group */
#define AI_RESULT_FAILED 0xfd
#define AI_RESULT_DONE 0xfe     /* nothing to do: the goal is reached */
/* Bits of the command byte of the AI tests. */
#define AI_EXECUTE 0x80         /* set the action when the test passes */
#define AI_MODE_MASK 0x7f
#define AI_MOVE_KIND_MASK 0x1f  /* creature_ai_try_move: the kind of the move */
#define AI_MOVE_NO_MOVERS 0x40  /* ... groups that move do not block */
#define AI_MOVE_ATTACK_GROUPS 0x20 /* ... a group of filter 9 in the way gets attacked */

/* More of the square that the AI read last (0x7f8b2 to 0x7f8bf of SKULL.EXE); creature_ai_cache_square resets it. */
typedef struct {
    uint8_t blocking;           /* the square stops the search: a costly wall, a failed move */
    uint8_t passage;            /* the square is a seamless passage; end is its other side */
    PassageEnd end;
    uint8_t unused;
    int8_t items_elsewhere;     /* -1 not read; 1: items or ornaments that the creature cannot reach */
    uint8_t facing_ornament;    /* a wall: the face toward the creature shows an alcove */
    int8_t cells[4];            /* a floor: the cells that the creature reaches (cells[0] 0xff: not read);
                                   a wall: cells[0] is the face toward the creature */
} AiSquareScan;
extern AiSquareScan ai_square_scan;

/* The light sums of the light search (Step 17 moves them to its module). */
extern int16_t light_source_sum, explosion_light_sum;
extern int16_t cached_light_sum, cached_explosion_light_sum, last_party_map;
/* The state of the shift register that picks the first search direction (0x752e8 of SKULL.EXE). */
extern uint16_t search_direction_bits;

int creature_square_is_obstacle(int x, int y);
int creature_square_blocks_line(int x, int y);
int creature_square_blocks_line_to_target(int x, int y);
int creature_square_blocks_line_except_self(int x, int y);
int creature_trace_line(int x1, int y1, int x2, int y2, int (*blocked)(int x, int y));
void creature_ai_cache_square(int x, int y);
uint16_t creature_ai_cached_first_object(void);
uint16_t creature_ai_cached_first_group(void);
int creature_ai_turn_toward(int dir);
/* distance_map: the map of the search (4 bytes per square, 32 squares per column); out_location gets the target. */
int creature_ai_find_target_in_lines(int filter, uint8_t *distance_map, uint16_t *out_location, int x, int y, int dir, int ranged);
int creature_ai_try_attack(int mode, int x, int y, int target_x, int target_y, int dir);
int creature_ai_try_door_action(int mode, int x, int y, int door_x, int door_y, int dir);
int creature_ai_projectile_incoming(int x, int y);
int creature_ai_try_move(int command, int x, int y, int dest_x, int dest_y, int dir);
int creature_find_matching_item_inside(uint16_t thing, int filter, int side);
int creature_ai_try_take_item(int mode, int x, int y, int dest_x, int dest_y, int dir, int filter);
uint16_t creature_find_wall_actuator_in_cell(uint16_t first_thing, int cell, int mode, int filter);
int creature_wall_actuator_accepts(uint16_t actuator, int mode, int filter, int state);
int creature_ai_try_use_wall_actuator(int mode, int x, int y, int dest_x, int dest_y, int dir, int filter);
int creature_thing_matches_filter(uint16_t thing, int filter);
int shop_parse_item_class_set(int creature_type, int filter, uint8_t *bitset, int skip_containers);
uint16_t creature_find_matching_item(int filter, uint16_t first_thing, int cell);
void creature_remove_matching_items(uint16_t *list_head, int filter, int cell);
int creature_count_matching_items(int filter, uint16_t first_thing, int cell, int into_groups, int into_containers);
int creature_find_ai_marker(int level_delta, uint16_t id_mask);
int creature_find_ai_marker_by_id(int level_delta, int id);
/* The breadth-first search from (x, y) with the move command: the nearest of count candidates; returns its index,
   -1 for none. path gets the steps to it (2 bytes each). */
int creature_ai_search_area(int x, int y, int command, int count, AiCandidate *candidates, uint8_t *path);
void creature_ai_load_cached_path(void);
int creature_ai_next_path_step(void);
int creature_ai_plan_path(void);

/* Functions of other parts of Step 16 and of Step 17 (weak stubs in ai_move_stubs.c until they exist). */
int dun_find_adjacent_level_link(int x, int y, int level_delta, uint16_t *sensor);
void light_add_square_light(int distance, int dir, int x, int y, int mode);

#endif
