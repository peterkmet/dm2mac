#ifndef AI_MOVE_H
#define AI_MOVE_H
#include <stdint.h>
#include "ai.h"

/* Creature perception and movement: squares, lines of sight, item filters, the tests of the AI commands, and the
   path search (ai_move.c). The functions work on the AI context (ai.h) and set ai.command_result. */

int creature_square_is_obstacle(int x, int y);
int creature_square_blocks_line(int x, int y);
int creature_square_blocks_line_to_target(int x, int y);
int creature_square_blocks_line_except_self(int x, int y);
int creature_trace_line(int x1, int y1, int x2, int y2, int (*blocked)(int x, int y));
void creature_ai_cache_square(int x, int y);
uint16_t creature_ai_cached_first_object(void);
uint16_t creature_ai_cached_first_group(void);
int creature_ai_turn_toward(int dir);
int creature_ai_find_target_in_lines(int filter, uint8_t *distance_map, uint16_t *out_location, int x, int y, int dir, int ranged);
int creature_ai_try_attack(int mode, int x, int y, int target_x, int target_y, int dir);
int creature_ai_try_door_action(int mode, int x, int y, int door_x, int door_y, int dir);
int creature_ai_projectile_incoming(int x, int y);
int creature_ai_try_move(int command, int x, int y, int dest_x, int dest_y, int dir);
int creature_find_matching_item_inside(uint16_t thing, int filter, int dir);
int creature_ai_try_take_item(int mode, int x, int y, int dest_x, int dest_y, int dir, int filter);
uint16_t creature_find_wall_actuator_in_cell(uint16_t first_thing, int cell, int mode, int filter);
int creature_wall_actuator_accepts(uint16_t actuator, int mode, int filter, uint16_t state);
int creature_ai_try_use_wall_actuator(int mode, int x, int y, int dest_x, int dest_y, int dir, int filter);
int creature_thing_matches_filter(uint16_t thing, int filter);
uint16_t creature_find_matching_item(int filter, uint16_t first_thing, int cell);
void creature_remove_matching_items(uint16_t *list_head, int filter, int cell);
int creature_count_matching_items(int filter, uint16_t first_thing, int cell, int into_groups, int into_containers);
int creature_find_ai_marker(int level_delta, uint16_t id_mask);
int creature_find_ai_marker_by_id(int level_delta, int id);
/* The breadth-first search from (x, y): the nearest of count candidates; returns its index, -1 for none. */
int creature_ai_search_area(int x, int y, int command, int count, AiCandidate *candidates, uint8_t *path);
void creature_ai_load_cached_path(void);
int creature_ai_next_path_step(void);
int creature_ai_plan_path(void);

#endif
