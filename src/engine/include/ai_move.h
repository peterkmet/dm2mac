/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_AI_MOVE_H
#define DM2_ENGINE_AI_MOVE_H
/* Prefixes: AI_MOVE_ MOVE_KIND_ */

/* Creature perception and movement: squares, lines of sight, item filters, the tests of the AI commands, and the
   path search (ai_move.c). The functions work on the AI context (ai.h) and set g_ai.command_result. */

#include "money.h"
#include <stdint.h>

/* Bits of the command byte of the AI tests (AI_EXECUTE in ai.h). */
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
#define MOVE_DIR4_FLAGS          0x0110 /* the moves of a level change up (dir 4) */
#define MOVE_DIR5_FLAGS          0x0108 /* ... down (dir 5) */
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
#define DOOR_SKILL_MARK          0x0020
#define DOOR_SKILL_HANDLE        0x0040
#define DOOR_SKILL_ANY_SIDE      0x0020
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
/* The shift register that picks the first search direction (0x752e8 of SKULL.EXE); exported for the tests. */
extern uint16_t g_search_direction_bits;

/* The bit set of the item indexes of an item class (GDAT text entry of a creature type, money.h). */
#define ITEM_CLASS_SET_SIZE SHOP_CLASS_SET_SIZE
#define FILTER_BITMAP_SIZE  ITEM_CLASS_SET_SIZE /* the item class bitmap of an item filter */

#endif /* DM2_ENGINE_AI_MOVE_H */
