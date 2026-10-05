/* Weak placeholders of the Step 16 parts that other branches port (ai_move.c); removed at their merge. */
#include "ai_move.h"

#define WEAK __attribute__((weak))
WEAK int creature_square_is_obstacle(int x, int y) { (void)x; (void)y; return 0; }
WEAK int creature_trace_line(int x1, int y1, int x2, int y2, int (*blocked)(int x, int y)) { (void)x1; (void)y1; (void)x2; (void)y2; (void)blocked; return 0; }
WEAK void creature_ai_cache_square(int x, int y) { (void)x; (void)y; }
WEAK int creature_ai_turn_toward(int dir) { (void)dir; return 0; }
WEAK int creature_ai_try_attack(int mode, int x, int y, int tx, int ty, int dir) { (void)mode; (void)x; (void)y; (void)tx; (void)ty; (void)dir; ai.command_result = AI_RESULT_FAIL; return 0; }
WEAK int creature_ai_try_door_action(int mode, int x, int y, int tx, int ty, int dir) { (void)mode; (void)x; (void)y; (void)tx; (void)ty; (void)dir; ai.command_result = AI_RESULT_FAIL; return 0; }
WEAK int creature_ai_projectile_incoming(int x, int y) { (void)x; (void)y; return 0; }
WEAK int creature_ai_try_move(int c, int x, int y, int tx, int ty, int dir) { (void)c; (void)x; (void)y; (void)tx; (void)ty; (void)dir; ai.command_result = AI_RESULT_FAIL; return 0; }
WEAK int creature_ai_try_take_item(int mode, int x, int y, int tx, int ty, int dir, int f) { (void)mode; (void)x; (void)y; (void)tx; (void)ty; (void)dir; (void)f; ai.command_result = AI_RESULT_FAIL; return 0; }
WEAK int creature_ai_try_use_wall_actuator(int mode, int x, int y, int tx, int ty, int dir, int f) { (void)mode; (void)x; (void)y; (void)tx; (void)ty; (void)dir; (void)f; ai.command_result = AI_RESULT_FAIL; return 0; }
WEAK int creature_thing_matches_filter(uint16_t thing, int filter) { (void)thing; (void)filter; return 0; }
WEAK uint16_t creature_find_matching_item(int filter, uint16_t first, int cell) { (void)filter; (void)first; (void)cell; return 0xfffe; }
WEAK void creature_remove_matching_items(uint16_t *list, int filter, int cell) { (void)list; (void)filter; (void)cell; }
WEAK int creature_count_matching_items(int filter, uint16_t first, int cell, int g, int c) { (void)filter; (void)first; (void)cell; (void)g; (void)c; return 0; }
WEAK int creature_find_ai_marker_by_id(int level_delta, int id) { (void)level_delta; (void)id; return 0; }
WEAK int creature_ai_search_area(int x, int y, int m, int count, AiCandidate *c, uint8_t *path) { (void)x; (void)y; (void)m; (void)count; (void)c; (void)path; return -1; }
WEAK int creature_ai_next_path_step(void) { return 0; }
WEAK int creature_ai_plan_path(void) { return 0; }
WEAK void projectile_create(uint16_t thing, int x, int y, int cell, int dir, int kinetic, int attack, int step) { (void)thing; (void)x; (void)y; (void)cell; (void)dir; (void)kinetic; (void)attack; (void)step; }
WEAK int creature_attack_champion(uint16_t group, int champ) { (void)group; (void)champ; return 0; }
