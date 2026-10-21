/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_AI_MOVE_H
#define DM2_ENGINE_AI_MOVE_H
/* Prefixes: AI_MOVE_ MOVE_KIND_ */

/* Creature perception and movement: squares, lines of sight, item filters, the tests of the AI commands, and the
   path search (ai_move.c). The functions work on the AI context (ai.h) and set ai.command_result. */

#include "ai.h"
#include "money.h"
#include "move.h"
#include <stdint.h>

/* Bits of the command byte of the AI tests (AI_EXECUTE in ai.h). */
#define AI_MODE_MASK           0x7f
#define AI_MOVE_KIND_MASK      0x1f /* ai_try_move: the kind of the move */
#define AI_MOVE_NO_MOVERS      0x40 /* ... groups that move do not block */
#define AI_MOVE_ATTACK_GROUPS  0x20 /* ... a group of filter 9 in the way gets attacked */
#define AI_MOVE_FLAG_BITS      0xe0 /* ... the bits above the kind */
#define FILTER_WEAPON_HANDS    0x0b /* item filter of the champion hands that creatures notice */
#define FILTER_ATTACKED_GROUPS 9    /* item filter of the groups that a creature attacks when they are in its way */
/* The kinds of a move test (bits 0-4 of the command of ai_try_move). */
enum ai_move_kind {
    MOVE_KIND_PLAIN,
    MOVE_KIND_KEEP_DISTANCE, /* the creature keeps a distance to the party */
    MOVE_KIND_NONE,          /* no move: a search tests only its start square */
    MOVE_KIND_OPEN,          /* the quick test of the search: no wall */
    MOVE_KIND_SAFE,          /* the creature avoids poison clouds and missiles */
    MOVE_KIND_SAFE_FLEE,     /* ... and the missiles that fly toward the square */
    MOVE_KIND_STEP_BACK,
    MOVE_KIND_SIGHT, /* the quick test of the light search: no obstacle to the sight */
    MOVE_KIND_COST   /* the quick test of the cost search */
};
/* Search target kinds (AiCandidate.kind). */
enum {
    TARGET_ANY,
    TARGET_HOME,
    TARGET_PARTY_VIEW,
    TARGET_ACTIVATION,
    TARGET_PARTY_RANGE_2,
    TARGET_FLEE,
    TARGET_SHOOT_PARTY,
    TARGET_MELEE_PARTY,
    TARGET_MELEE_LINE,
    TARGET_SHOOT_LINE,
    TARGET_STEAL,
    TARGET_MARKER,
    TARGET_NEAR_MARKER,
    TARGET_WANDER,
    TARGET_RANDOM,
    TARGET_ITEM,
    TARGET_ITEM_2,
    TARGET_ITEM_WALL,
    TARGET_GROUP_IN_FRONT,
    TARGET_GROUP_FACING,
    TARGET_DAMAGE_GROUP,
    TARGET_MOVE_TEST,
    TARGET_ACTUATOR,
    TARGET_LIGHT,
    TARGET_AI_MARKER,
    TARGET_MARKER_SIDE,
    TARGET_DOOR,
    TARGET_PARTY_DISTANCE
};
/* The modes of ai_try_attack. */
enum ai_attack_mode {
    ATTACK_MODE_PARTY,
    ATTACK_MODE_PARTY_RANGED,
    ATTACK_MODE_GROUP,
    ATTACK_MODE_ANY,
    ATTACK_MODE_DOOR
};
/* The modes of ai_try_door_action. */
enum ai_door_mode { DOOR_MODE_OPEN, DOOR_MODE_CLOSE, DOOR_MODE_CLOSE_MARKED };
/* The modes of ai_try_take_item. */
enum ai_item_mode { ITEM_MODE_TAKE, ITEM_MODE_GIVE };
/* The modes of ai_try_use_wall_actuator. */
enum ai_actuator_mode { ACTUATOR_MODE_USE, ACTUATOR_MODE_PUT, ACTUATOR_MODE_TAKE };

/* ai.move_flags: the squares that the creature may enter. */
#define MOVE_WALL               0x0001
#define MOVE_FLOOR              0x0002
#define MOVE_CLOSED_PIT         0x0004
#define MOVE_OPEN_PIT           0x0008
#define MOVE_FALL               0x0010
#define MOVE_INVISIBLE_PIT      0x0020
#define MOVE_HIDDEN_FAKE_WALL   0x0040
#define MOVE_FAKE_WALL          0x0080
#define MOVE_STAIRS             0x0100
#define MOVE_LOCKED_DOOR        0x0200
#define MOVE_TELEPORTER         0x0400
#define MOVE_PARTY              0x0800
#define MOVE_GROUPS             0x1000
#define MOVE_LEVELS             0x2000
#define MOVE_DOOR               0x4000
#define MOVE_FALLS              0x8000
#define MOVE_LEVEL_FLAGS        0x0118
#define MOVE_DIR4_FLAGS         0x0110 /* the moves of a level change up (dir 4) */
#define MOVE_DIR5_FLAGS         0x0108 /* ... down (dir 5) */
#define MOVE_FLAGS_LIGHT        0x0227
#define MOVE_FLAGS_PARTY_MAP    0x36e7
#define MOVE_FLAGS_NO_FALL      0x7db7
/* ai.door_flags. */
#define DOOR_SKILLS_OPEN        0x6f
#define DOOR_SKILLS_CLOSE       0x73
#define DOOR_SKILL_BUTTON       0x0003
#define DOOR_SKILL_SHOOT_BUTTON 0x0001
#define DOOR_SKILL_PRESS_BUTTON 0x0002
#define DOOR_SKILL_SPELL        0x0004 /* breaks a door that magic breaks with a fireball or lightning */
#define DOOR_SKILL_BASH         0x0008 /* breaks a breakable door with hits */
#define DOOR_SKILL_MARK         0x0020
#define DOOR_SKILL_HANDLE       0x0040
#define DOOR_SKILL_ANY_SIDE     0x0020
/* AI actions (ActiveGroup.action). A pair or range of codes runs the same act with a variant. */
enum ai_action {
    ACTION_NONE = 0,
    ACTION_MOVE = 1,
    ACTION_MOVE_NEAR = 2,
    ACTION_STEP_RIGHT = 3,
    ACTION_STEP_LEFT = 4,
    ACTION_FALL = 5,
    ACTION_TURN_LEFT = 6,
    ACTION_TURN_RIGHT = 7,
    ACTION_MELEE = 8,
    ACTION_TURN_BACK = 9,
    ACTION_STEAL = 0x0a,
    ACTION_OPERATE_DOOR = 0x0b,
    ACTION_TAKE_LEFT = 0x0c,
    ACTION_TAKE_RIGHT = 0x0d,
    ACTION_THROW = 0x0e,
    ACTION_THROW_2 = 0x0f,
    ACTION_IDLE = 0x11,
    ACTION_VANISH = 0x13,
    ACTION_TURN_GROUP = 0x15,
    ACTION_TURN_GROUP_2 = 0x16,
    ACTION_GIVE = 0x17,
    ACTION_TAKE_FROM_GROUP = 0x18,
    ACTION_PUT_IN_WALL = 0x19,
    ACTION_TAKE_FROM_WALL = 0x1a,
    ACTION_CHECK_AHEAD = 0x1d,
    ACTION_PLACE_WAIT = 0x1e,
    ACTION_STAND = 0x23,
    ACTION_MELEE_2 = 0x26,
    ACTION_SHOOT = 0x27,
    ACTION_SHOOT_2 = 0x28,
    ACTION_DROP_HERE = 0x29,
    ACTION_DROP_HERE_2 = 0x2a,
    ACTION_TAKE_FRONT = 0x2b,
    ACTION_TAKE_SIDE = 0x2c,
    ACTION_DROP_AHEAD = 0x2d,
    ACTION_DROP_AHEAD_2 = 0x2e,
    ACTION_USE_ACTUATOR = 0x2f,
    ACTION_USE_ACTUATOR_LAST = 0x31,
    ACTION_WAIT_FIRST = 0x32, /* 0x32 to 0x34: wait 0 to 2 game steps without animation */
    ACTION_RESCAN = 0x33,
    ACTION_WAIT_LAST = 0x34,
    ACTION_STAIRS_UP = 0x35,
    ACTION_STAIRS_DOWN = 0x36,
    ACTION_FALL_IN_PIT = 0x37,
    ACTION_PIT = 0x38,
    ACTION_LINK_UP = 0x39,
    ACTION_LINK_DOWN = 0x3a,
    ACTION_TRANSFORM = 0x3b,
    ACTION_MORPH = 0x3c,
    ACTION_SPECIAL = 0x3d,
    ACTION_SPECIAL_LAST = 0x40,
    ACTION_DODGE = 0x55
};

/* The light sums of the light search (Step 17 moves them to its module). */
extern int16_t cached_light_sum, cached_explosion_light_sum, last_party_map;
/* The state of the shift register that picks the first search direction (0x752e8 of SKULL.EXE). */
extern uint16_t search_direction_bits;

/* The bit set of the item indexes of an item class (GDAT text entry of a creature type, money.h). */
#define ITEM_CLASS_SET_SIZE SHOP_CLASS_SET_SIZE
#define FILTER_BITMAP_SIZE  ITEM_CLASS_SET_SIZE /* the item class bitmap of an item filter */

#endif /* DM2_ENGINE_AI_MOVE_H */
