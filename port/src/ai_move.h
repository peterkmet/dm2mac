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

/* ai.move_flags: the squares that the creature may enter. */
#define MOVE_WALL 0x0001
#define MOVE_FLOOR 0x0002
#define MOVE_CLOSED_PIT 0x0004
#define MOVE_OPEN_PIT 0x0008
#define MOVE_FALL 0x0010
#define MOVE_INVISIBLE_PIT 0x0020
#define MOVE_HIDDEN_FAKE_WALL 0x0040
#define MOVE_FAKE_WALL 0x0080
#define MOVE_STAIRS 0x0100
#define MOVE_LOCKED_DOOR 0x0200
#define MOVE_TELEPORTER 0x0400
#define MOVE_PARTY 0x0800
#define MOVE_GROUPS 0x1000
#define MOVE_LEVELS 0x2000
#define MOVE_DOOR 0x4000
#define MOVE_FALLS 0x8000
#define MOVE_LEVEL_FLAGS 0x0118
#define MOVE_DIR4_FLAGS 0x0110         /* the moves of a level change up (dir 4) */
#define MOVE_DIR5_FLAGS 0x0108         /* ... down (dir 5) */
#define MOVE_FLAGS_LIGHT 0x0227
#define MOVE_FLAGS_PARTY_MAP 0x36e7
#define MOVE_FLAGS_NO_FALL 0x7db7
/* ai.door_flags. */
#define DOOR_SKILLS_OPEN 0x6f
#define DOOR_SKILLS_CLOSE 0x73
#define DOOR_SKILL_BUTTON 0x0003
#define DOOR_SKILL_SHOOT_BUTTON 0x0001
#define DOOR_SKILL_BASH 0x0004
#define DOOR_SKILL_BURN 0x0008
#define DOOR_SKILL_MARK 0x0020
#define DOOR_SKILL_HANDLE 0x0040
#define DOOR_SKILL_ANY_SIDE 0x0020
/* AI actions (ActiveGroup.action). A pair or range of codes runs the same act with a variant. */
enum ai_action {
    ACTION_NONE = 0, ACTION_MOVE = 1, ACTION_MOVE_NEAR = 2, ACTION_STEP_RIGHT = 3, ACTION_STEP_LEFT = 4,
    ACTION_FALL = 5, ACTION_TURN_LEFT = 6, ACTION_TURN_RIGHT = 7, ACTION_MELEE = 8, ACTION_TURN_BACK = 9,
    ACTION_STEAL = 0x0a, ACTION_OPERATE_DOOR = 0x0b, ACTION_TAKE_LEFT = 0x0c, ACTION_TAKE_RIGHT = 0x0d,
    ACTION_THROW = 0x0e, ACTION_THROW_2 = 0x0f, ACTION_IDLE = 0x11, ACTION_VANISH = 0x13, ACTION_TURN_GROUP = 0x15,
    ACTION_TURN_GROUP_2 = 0x16, ACTION_GIVE = 0x17, ACTION_TAKE_FROM_GROUP = 0x18, ACTION_PUT_IN_WALL = 0x19,
    ACTION_TAKE_FROM_WALL = 0x1a, ACTION_CHECK_AHEAD = 0x1d, ACTION_PLACE_WAIT = 0x1e, ACTION_STAND = 0x23,
    ACTION_MELEE_2 = 0x26, ACTION_SHOOT = 0x27, ACTION_SHOOT_2 = 0x28, ACTION_DROP_HERE = 0x29,
    ACTION_DROP_HERE_2 = 0x2a, ACTION_TAKE_FRONT = 0x2b, ACTION_TAKE_SIDE = 0x2c, ACTION_DROP_AHEAD = 0x2d,
    ACTION_DROP_AHEAD_2 = 0x2e, ACTION_USE_ACTUATOR = 0x2f, ACTION_USE_ACTUATOR_LAST = 0x31,
    ACTION_WAIT_FIRST = 0x32,   /* 0x32 to 0x34: wait 0 to 2 game steps without animation */
    ACTION_RESCAN = 0x33, ACTION_WAIT_LAST = 0x34, ACTION_STAIRS_UP = 0x35, ACTION_STAIRS_DOWN = 0x36,
    ACTION_FALL_IN_PIT = 0x37, ACTION_PIT = 0x38, ACTION_LINK_UP = 0x39, ACTION_LINK_DOWN = 0x3a,
    ACTION_TRANSFORM = 0x3b, ACTION_MORPH = 0x3c, ACTION_SPECIAL = 0x3d, ACTION_SPECIAL_LAST = 0x40,
    ACTION_DODGE = 0x55
};

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

#endif
