/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Creature groups: creature info, animation, active groups, and the creature update events. */
#include "creature.h"
#include "gdat.h"
#include "ids.h"
#include "le.h"
#include "os.h"
#include "ai.h"
#include "champion.h"
#include "dungeon.h"
#include "game.h"
#include "hooks.h"
#include "item.h"
#include "minion.h"
#include "move.h"
#include "projectile.h"
#include "random.h"
#include "save.h"
#include "sensor.h"
#include "sound_request.h"
#include "timeline.h"
#include <stdlib.h>
#include <string.h>

/* GDAT value key: the index of the creature info record. */
#define CREATURE_KEY_INFO 0x05

/* GDAT value keys 10 to 20: the items that a dead creature leaves. */
#define CREATURE_ATTR_DROP_FIRST 0x0a
#define CREATURE_ATTR_DROP_LAST  0x14

/* Animation state word. */
#define ANIM_STILL         0x4000 /* the creature shows the first frame of its sequence */
#define ANIM_LOOP          0x8000 /* the sequence repeats with the game time */
#define ANIM_LOOP_ALT      0x2000 /* the sequence repeats until its end */
#define ANIM_RESTART       0x1000 /* a looping sequence starts again from frame 0 */
#define ANIM_START_MASK    0x0fc0 /* first frame of a looping sequence */
#define ANIM_RESTART_CLEAR 0xe03f /* clears ANIM_RESTART and the first frame */
#define ANIM_FRAME_MASK    0x003f /* frame of a still sequence, length of a looping one */
#define ANIM_KIND_MASK     0xe03f
#define ANIM_SINGLE_LOOP   0x8001 /* a looping sequence of one frame */
#define ANIM_STEP_NONE     0xffff /* no step of the sequence shown yet */

/* The shift of the first frame of a looping sequence in the animation state word. */
#define ANIM_START_SHIFT 6

/* Animation step. */
#define ANIM_STEP_SIZE 4

/* Bits of the bytes of an animation step. */
#define STEP_SOUND_MASK       0x7f
#define STEP_NO_SOUND         0x7f
#define STEP_CHANCE_MASK      0x0f /* chance of 16 to stay on the step; 15: always */
#define STEP_JUMP_MASK        0x3f
#define STEP_JITTER           0x01 /* time byte: a random picture shift */
#define STEP_MIRROR           0x02 /* time byte: a random mirror */
#define STEP_RANDOM_TIME_MASK 0x0c
#define STEP_TIME_MASK        0xf0

/* Shifts of the fields of an animation step. */
#define STEP_ACTS_SHIFT        7 /* sound byte bit 7: the creature acts at this step */
#define STEP_NEXT_SHIFT        4 /* bits 4-7: steps to the next step, 0 for the last */
#define STEP_RANDOM_TIME_SHIFT 2 /* bits 2-3: random extra game steps */
#define STEP_TIME_SHIFT        4 /* bits 4-7: game steps */

/* Anim bits of an active group. */
#define ANIM_BIT_MIRROR 0x40
#define ANIM_BIT_KEEP   0xc0

/* Traits bits 0-1: sideways random shift, bits 2-3: forward random shift. */
#define TRAIT_JITTER_MASK 0x3

/* The shifts of the forward picture shift in the anim bits and in the traits. */
#define ANIM_BIT_SHIFT_Y 3 /* anim bits 0-2: sideways picture shift, bits 3-5: forward shift */
#define TRAIT_SHIFT_Y    2

/* A sequence table entry: the action word, then the word of its first step. */
#define SEQUENCE_ENTRY_SIZE 4
#define SEQUENCE_FIRST_STEP 2
#define SEQUENCE_END        (-1)

/* AI class flags of g_ai_class_flags. */
#define AI_CLASS_NO_HOME_EVENT   0x0004 /* class flag: the home square gets no event when the creature is hurt */
#define AI_CLASS_NO_TURN         0x0080 /* an alert does not turn the creature */
#define AI_CLASS_WAKES           0x0410
#define AI_CLASS_DIES_NEAR_PARTY 0x0800 /* the creature dies only in front of the party; the commands wait */
#define AI_CLASS_SCREAMS         0x8000 /* the creature screams when it is hurt */

/* Class flag: a creature on another map than the party keeps its pace. */
#define AI_CLASS_STAYS_FAR 0x10000

/* AI actions. */
#define AI_ACTION_JUMP       0x05
#define AI_ACTION_TURN_LEFT  0x06
#define AI_ACTION_TURN_RIGHT 0x07
#define AI_ACTION_IDLE       0x11
#define AI_ACTION_STAND_1    0x23
#define AI_ACTION_ATTACK_2   0x24
#define AI_ACTION_STAND_2    0x25

/* Flags of an AI action. */
#define AI_ACTION_FLAG_WAKES     0x02
#define ACTION_FLAG_PATH         0x04
#define AI_ACTION_FLAG_INTERRUPT 0x10

/* Active groups and the size of a group. */
#define ACTIVE_GROUP_SPARE   100 /* active groups beyond the acting groups of the dungeon */
#define ACTION_TIME_LONG_AGO 127 /* a new active group acted this many game steps ago */
#define GROUP_SIZE_MAX       31  /* creatures of a group - 1 */
#define GROUP_HEALTH_SHIFT   3   /* each creature of a new group has 1/8 of the maximum health, plus 0 to 1/8 */

/* creature_create_group: the group links a minion projectile. */
#define GROUP_MINION 0x8000

/* The delays of an animation step. */
#define REGEN_TIME_SHIFT    2  /* regeneration counts in game time / 4 */
#define VANISH_DELAY_FACTOR 3  /* a vanish in a party action effect is 3 times slower */
#define FAR_DELAY_FACTOR    4  /* a creature away from the party and unseen is 4 times slower */
#define FEAR_DELAY_PERCENT  75 /* a frightened creature is faster */
#define FEAR_HURRY_DELAY    2  /* a frightened creature in a hurry keeps delays up to this */

/* The items that a dead creature leaves: a value of a GDAT drop key. */
#define DROP_AMOUNT_MASK 0x0f
#define DROP_RANDOM_MASK 0x70

/* The shifts of the random count and the item of a drop value. */
#define DROP_RANDOM_SHIFT 4
#define DROP_ITEM_SHIFT   7

/* The sound of a hurt creature. */
#define SOUND_ENTRY_HURT 0x09 /* and 10 */

/* Random masks of the combat rolls. */
#define ROLL_COMBAT      0x1f /* the random part of hit, dodge, and defense */
#define ATTACK_ROLL_MASK 0x0f
#define SCREAM_ROLL_MASK 0x07 /* a hurt creature screams with chance 1 in 8 */

/* Damage kinds 6 to 0x15 set mood bit kind - 6. */
#define DAMAGE_KIND_MOOD_FIRST 0x06
#define DAMAGE_KIND_MOOD_END   0x16

/* Damage, death, and combat values. */
#define BOSS_DEATH_WAIT           160 /* timer ticks */
#define SCREAM_MAX_HEALTH_PERCENT 3   /* or more often when the damage is above this part of its maximum health */
#define SCREAM_HEALTH_PERCENT     5   /* ... or of its health */
#define NEAR_PARTY_DISTANCE       3
#define CREATURE_DEFENSE_SHIFT    3 /* the defense of a creature counts in eighths */
#define ATTACK_DAMAGE_SHIFT       2 /* the damage of a creature hit is a quarter of the rolls */
#define ATTACK_MIN_DAMAGE         2
#define ANGER_DAMAGE              30 /* the damage since the last update that makes a creature angry */
#define ANGER_DAMAGE_MAYBE        4  /* ... with chance 1 in 4 */
#define ANGER_HEALTH_PERCENT      15 /* ... or this part of its maximum health */
#define ARMOR_FACTOR              8  /* the armor factor: (amount + 0 to 3) * 8 / (armor + 2) */
#define ARMOR_BASE                2

/* Alert flags of the alerts of this module. */
#define ALERT_SET_ANGRY 0x0002 /* alert flags: set mood bit 2, MOOD_ANGRY */
#define ALERT_MOOD_BIT  0x0040
#define ALERT_PUSHED    0x4002

/* Alert chances in percent. */
#define ALERT_HIT_CHANCE    60
#define ALERT_PUSHED_CHANCE 10

/* The retry of a group move. */
#define GROUP_RETRY_DELAY 5
#define EVENT_GROUP_WORD  2 /* group retry event: data bytes 2-3 hold the group */

/* Pushing creatures. */
#define PUSH_FAIL_STAMINA  5
#define PUSH_LIGHT         46 /* creatures below this resistance are easy to push */
#define PUSH_BASE_STRENGTH 40
#define PUSH_LEADER_SHIFT  3  /* the leader pushes with 1/8 more strength */
#define PUSH_LIGHT_SHIFT   2  /* a light creature: 1/4 more strength */
#define PUSH_STAMINA_SHIFT 4  /* a push costs stamina when a champion has more than 1/16 of the maximum */
#define PUSH_COST_SHIFT    2  /* a push costs a quarter of the resistance, at least PUSH_FAIL_STAMINA */
#define PUSH_STOP_SHIFT    2  /* a weak push stops with chance 1 in (resistance - strength) / 4 + 1 */
#define LOAD_PUSH_DIVISOR  10 /* the push strength of the party: its load / 10 per champion */

/* The power of a vanish sensor that makes the creatures vanish. */
#define VANISH_POWER 2

ActiveGroup *g_active_groups;
int g_active_group_max;
int g_active_group_count;

/* The push that waits for the slow move of the party. */
static struct {
    int16_t turn, y, x, map, dir, party_y, party_x;
} push;

/* The creature info records of SKULL.EXE. */
static const CreatureInfo creature_infos[CREATURE_INFO_COUNT] = {
    {0x45,   0xff, 0x0,  0x2bc, 0x0,  0x0,  0xff, 0x0,  0x0,    0x0,  0x0,   0x0,  0x0,
     0x0,    0x100,  0x1f, 0xf,  0x0,  0xf0, 0x0, 0xff, 0x4000, 0x0,    0x0, 0x2},
    {0x85,   0xff, 0x0,  0x1c2, 0x0,  0x0,  0xff, 0x0,  0x0,    0x0,  0x0,   0x0,  0x0,
     0x0,    0x100,  0x1f, 0xf,  0x0,  0xf0, 0x0, 0xff, 0xfd22, 0x1068, 0x0, 0x2},
    {0x85,   0xff, 0x0,  0x64,  0x0,  0x0,  0xff, 0x0,  0x0,    0x0,  0x0,   0x0,  0x0,
     0x0,    0x100,  0xff, 0xf,  0x0,  0xf0, 0x0, 0xff, 0x9e22, 0x1014, 0x0, 0x2},
    {0x85,   0x64, 0x0,  0x78,  0x0,  0x0,  0x0,  0x0,  0x0,    0x0,  0x0,   0x0,  0x0,
     0x0,    0x100,  0x1f, 0xd,  0x0,  0xf0, 0x0, 0xff, 0x2000, 0x0,    0x0, 0x1},
    {0x45,   0xff, 0x0,  0x64,  0x0,  0x0,  0xff, 0x0,  0x0,    0x0,  0x0,   0x0,  0x0,
     0x0,    0x100,  0xff, 0xf,  0x0,  0xf0, 0x0, 0xff, 0x4000, 0x0,    0x0, 0x1},
    {0x45,   0xff, 0x0,  0x64,  0x0,  0x0,  0xff, 0x0,  0x0,    0x0,  0x0,   0x0,  0x0,
     0x0,    0x100,  0xff, 0xf,  0x0,  0xf0, 0x0, 0xff, 0x4000, 0x0,    0x0, 0x1},
    {0x45,   0xff, 0x0,  0x64,  0x0,  0x0,  0xff, 0x0,  0x0,    0x0,  0x0,   0x0,  0x0,
     0x0,    0x100,  0xff, 0xf,  0x0,  0xf0, 0x0, 0x5f, 0x4000, 0x0,    0x2, 0x2},
    {0x85,   0xff, 0x0,  0x64,  0x0,  0x0,  0xff, 0x0,  0x0,    0x0,  0x0,   0x0,  0x0,
     0x0,    0x100,  0xff, 0xf,  0x0,  0xf0, 0x0, 0xff, 0xa01,  0xfd4,  0x0, 0x2},
    {0x41,   0xff, 0x0,  0x64,  0x0,  0x0,  0xff, 0x0,  0x0,    0x0,  0x0,   0x0,  0x0,
     0x0,    0x100,  0xff, 0xf,  0x0,  0xf0, 0x0, 0xff, 0x4000, 0x0,    0x0, 0x2},
    {0x81,   0x50, 0x0,  0xa0,  0x0,  0x0,  0x0,  0x0,  0x0,    0x0,  0x0,   0x0,  0x0,
     0x0,    0x100,  0x1f, 0xf,  0x0,  0xf0, 0x0, 0x2d, 0xac42, 0xfa0,  0x0, 0x2},
    {0x85,   0xff, 0x0,  0x64,  0x0,  0x0,  0xff, 0x0,  0x0,    0x0,  0x0,   0x0,  0x0,
     0x0,    0x100,  0xff, 0xf,  0x0,  0xf0, 0x0, 0xff, 0xb01,  0x1004, 0x0, 0x2},
    {0x45,   0xff, 0x0,  0x64,  0x0,  0x0,  0xff, 0x0,  0x0,    0x0,  0x0,   0x0,  0x0,
     0x0,    0x100,  0xff, 0xf,  0x0,  0xf0, 0x0, 0x28, 0x4000, 0x0,    0x0, 0x0},
    {0x81,   0xff, 0x0,  0x190, 0x0,  0x0,  0xff, 0x0,  0x0,    0x0,  0x0,   0x0,  0x0,
     0x0,    0x100,  0xff, 0xf,  0x0,  0xf0, 0x0, 0xff, 0xed41, 0x109f, 0x0, 0x0},
    {0xcc40, 0x82, 0x8,  0xc8,  0x0,  0x0,  0x64, 0x1c, 0x6d9e, 0x0,  0x40,  0x21, 0x0,
     0x20ff, 0x1f0,  0x5f, 0xc,  0xa0, 0xfc, 0x4, 0xa,  0x0,    0x0,    0x0, 0x1},
    {0xc840, 0x96, -1,   0x64,  0x28, 0x0,  0x8c, 0x1c, 0x6d9e, 0x0,  0x40,  0x1,  0x0,
     0x20f8, 0x1de,  0xcc, 0xc,  0x50, 0xfc, 0x4, 0xa,  0x0,    0x0,    0x0, 0x1},
    {0xc840, 0x82, 0x32, 0x96,  0x0,  0x0,  0x28, 0xc,  0x6d9e, 0x3,  0x0,   0x21, 0x0,
     0x20f7, 0x1c0,  0x55, 0xc,  0x50, 0xfc, 0x4, 0xa,  0x0,    0x0,    0x0, 0x1},
    {0xc840, 0x82, 0x32, 0x96,  0x0,  0x0,  0x28, 0xc,  0x6d9e, 0x3,  0x0,   0x21, 0x0,
     0x20f7, 0x1c0,  0x55, 0xc,  0x50, 0xfc, 0x4, 0xa,  0x0,    0x0,    0x0, 0x1},
    {0xc840, 0xc8, -33,  0xc8,  0x3c, 0x0,  0x14, 0x1c, 0x2c06, 0x0,  0x40,  0x0,  0x0,
     0x20f2, 0x1fe,  0xdd, 0xc,  0x50, 0xfc, 0x4, 0xff, 0x0,    0x0,    0x0, 0x1},
    {0xc840, 0x6e, -16,  0x96,  0x0,  0x0,  0xa,  0xc,  0x6d9e, 0x3,  0x0,   0x21, 0x0,
     0x20f7, 0x1c0,  0x15, 0xc,  0x50, 0xfc, 0x4, 0xa,  0x0,    0x0,    0x0, 0x1},
    {0x240,  0x55, 0x21, 0x190, 0x32, 0x0,  0x78, 0x0,  0x2,    0x0,  0x2,   0x0,  0x0,
     0x1053, 0x78b,  0x76, 0x8,  0x40, 0xfd, 0x9, 0xfd, 0x0,    0x0,    0x3, 0x2},
    {0x8081, 0xff, 0x0,  0x384, 0x0,  0x0,  0xff, 0x0,  0x0,    0x0,  0x0,   0x0,  0x0,
     0x0,    0x100,  0xff, 0xf,  0x0,  0xf0, 0x0, 0xff, 0x6000, 0x0,    0x0, 0x1},
    {0x8ca0, 0x96, 0x8,  0x64,  0x28, 0x0,  0x64, 0x0,  0x6,    0x0,  0x40,  0x0,  0x0,
     0x2012, 0x2ff,  0x81, 0xf,  0x72, 0xfd, 0x4, 0xff, 0x0,    0x0,    0x1, 0x1},
    {0x8860, 0x64, 0x21, 0x50,  0x64, 0x0,  0x96, 0x0,  0xa06,  0x0,  0x10,  0x4,  0x0,
     0x4035, 0x3fd,  0xf6, 0xf,  0x62, 0xfd, 0x4, 0x14, 0x0,    0x0,    0x1, 0x1},
    {0x8840, 0x32, 0x10, 0x1e,  0xa,  0x28, 0xc8, 0xc,  0x806,  0x0,  0x1,   0x0,  0x0,
     0x10f3, 0x295,  0x11, 0x3,  0x0,  0xf1, 0x4, 0xa,  0x0,    0x0,    0x1, 0x1},
    {0xac0,  0x41, 0x21, 0x24,  0x6,  0x0,  0x50, 0x0,  0x2402, 0x1,  0x1,   0x0,  0x0,
     0x1034, 0x924a, 0xd7, 0x6,  0xfc, 0xff, 0x4, 0x41, 0x0,    0x0,    0x1, 0x1},
    {0xa80,  0x50, 0x10, 0x96,  0x28, 0x0,  0x50, 0x0,  0xe402, 0x3,  0xa,   0x2,  0x1,
     0x307a, 0x328a, 0xcc, 0x9,  0x82, 0xfe, 0x4, 0x46, 0x0,    0x0,    0x1, 0x1},
    {0x280,  0x32, 0x8,  0x15e, 0x14, 0x0,  0xf0, 0x0,  0x64a2, 0x4f, 0x0,   0x32, 0x1,
     0x20fd, 0x2df,  0xef, 0x6,  0xd3, 0xfe, 0x4, 0x28, 0x0,    0x0,    0x1, 0x0},
    {0x280,  0x46, 0x10, 0xaf,  0x23, 0x0,  0x6e, 0x0,  0x6402, 0x47, 0xd,   0x2,  0x1,
     0x30a7, 0x47c,  0x55, 0x8,  0xa2, 0xfe, 0x4, 0x2d, 0x0,    0x0,    0x1, 0x1},
    {0xac0,  0x64, 0x21, 0xfa,  0x50, 0x96, 0x46, 0x0,  0x2,    0x0,  0x1,   0x0,  0x0,
     0x10a2, 0x86e4, 0x72, 0x9,  0xec, 0xff, 0x4, 0x5a, 0x0,    0x0,    0x2, 0x2},
    {0xa40,  0x87, 0x32, 0x15e, 0x32, 0x0,  0x28, 0x0,  0x2,    0x0,  0x1,   0x0,  0x0,
     0x10a3, 0xc7ca, 0xc,  0xe,  0x10, 0xfb, 0x4, 0xff, 0x0,    0x0,    0x0, 0x2},
    {0x1c40, 0xaf, 0x8,  0x898, 0x87, 0x0,  0xaa, 0x8,  0x41a2, 0x0,  0x5d3, 0x5f, 0x0,
     0x40fd, 0x1aef, 0xdd, 0xe,  0x62, 0xfd, 0x4, 0xfd, 0x0,    0x0,    0x2, 0x1},
    {0x40,   0xdc, 0x10, 0x4b0, 0xff, 0x0,  0x78, 0x0,  0x4082, 0x1,  0x183, 0xa,  0x1,
     0x4068, 0x7cb,  0x88, 0xb,  0x41, 0xfc, 0x9, 0xfe, 0x0,    0x0,    0x2, 0x2},
    {0x40,   0x78, 0x0,  0x12c, 0x0,  0x0,  0x5,  0x0,  0x0,    0x0,  0x0,   0x0,  0x0,
     0x0,    0x1ff,  0xff, 0xf,  0x0,  0xf8, 0x0, 0xff, 0x0,    0x0,    0x1, 0x2},
    {0x240,  0xb4, 0x8,  0x3e8, 0x82, 0x0,  0xc8, 0x0,  0x4002, 0xf,  0xb,   0x2,  0x1,
     0x3057, 0x8ea,  0xdc, 0xc,  0x30, 0xfc, 0x4, 0x6e, 0x0,    0x0,    0x1, 0x1},
    {0xc840, 0x4b, -1,   0xc8,  0x2d, 0x0,  0xb4, 0x1c, 0x6d86, 0x0,  0x40,  0x21, 0x0,
     0x20fa, 0x4ec,  0xdf, 0xc,  0xa0, 0xfc, 0x4, 0xa,  0x0,    0x0,    0x0, 0x1},
    {0x8840, 0x32, 0x10, 0x78,  0x3c, 0x4b, 0xc8, 0xc,  0x807,  0x0,  0x1,   0x0,  0x0,
     0x10f4, 0x295,  0x11, 0x3,  0x0,  0xf0, 0x4, 0xa,  0x0,    0x0,    0x1, 0x1},
    {0x240,  0xa0, 0x10, 0x1a4, 0x64, 0x0,  0x8c, 0x0,  0x6402, 0xf,  0x9,   0x2,  0x1,
     0x3078, 0x78c,  0x7c, 0x9,  0x40, 0xfc, 0x4, 0x6e, 0x0,    0x0,    0x2, 0x2},
    {0x43,   0xff, 0x0,  0x64,  0x0,  0x0,  0xff, 0x0,  0x0,    0x0,  0x0,   0x0,  0x0,
     0x0,    0x100,  0xff, 0xf,  0x0,  0xf0, 0x0, 0x46, 0x4000, 0x1,    0x0, 0x2},
    {0x45,   0xff, 0x0,  0x64,  0x0,  0x0,  0xff, 0x0,  0x0,    0x0,  0x0,   0x0,  0x0,
     0x0,    0x100,  0xff, 0xf,  0x0,  0xf0, 0x0, 0xff, 0x4100, 0x1,    0x0, 0x2},
    {0x85,   0xff, 0x0,  0x64,  0x0,  0x0,  0xff, 0x0,  0x0,    0x0,  0x0,   0x0,  0x0,
     0x0,    0x100,  0xff, 0xf,  0x0,  0xf0, 0x0, 0xff, 0xa01,  0xfe3,  0x0, 0x2},
    {0x140,  0xdc, 0x0,  0x1f4, 0x3c, 0x0,  0xfa, 0x20, 0x2,    0x0,  0x2,   0x0,  0x0,
     0x0,    0x100,  0x4f, 0xf,  0x61, 0xfc, 0x8, 0xff, 0x0,    0x0,    0x3, 0x2},
    {0x8c78, 0x64, 0x32, 0x96,  0x2,  0x0,  0x96, 0x0,  0x6606, 0x0,  0x1,   0x0,  0x0,
     0x4034, 0x65cf, 0xf6, 0xf,  0x62, 0xfd, 0x4, 0xff, 0x0,    0x0,    0x1, 0x1},
    {0x280,  0x41, 0x32, 0x50,  0xa,  0x0,  0x5a, 0x0,  0x2402, 0x0,  0x1,   0x0,  0x0,
     0x1031, 0x637d, 0x1c, 0x4,  0x10, 0xfb, 0x4, 0xfe, 0x0,    0x0,    0x0, 0x2},
    {0xc840, 0x4b, 0x8,  0x96,  0x8,  0x0,  0xb4, 0x1c, 0x6d9e, 0x0,  0x40,  0x21, 0x0,
     0x20ff, 0x3e9,  0x7f, 0xc,  0xa0, 0xfc, 0x4, 0xa,  0x0,    0x0,    0x0, 0x1},
    {0x240,  0x73, 0x10, 0xaf,  0x5a, 0x0,  0x64, 0x0,  0x6402, 0x4b, 0x9,   0x8,  0x1,
     0x3045, 0x15ea, 0x55, 0xa,  0xa2, 0xfe, 0x4, 0xb4, 0x0,    0x0,    0x1, 0x1},
    {0x81,   0xff, 0x0,  0xbe,  0x0,  0x0,  0xff, 0x0,  0x0,    0x0,  0x0,   0x0,  0x0,
     0x0,    0x100,  0xff, 0xf,  0x0,  0xf0, 0x0, 0xff, 0x8c42, 0xfa0,  0x0, 0x2},
    {0x240,  0x46, 0x32, 0x64,  0x23, 0x0,  0x3c, 0x0,  0x4402, 0x4f, 0x9,   0xa,  0x1,
     0x1024, 0x25e2, 0x3f, 0x1f, 0xa2, 0xfe, 0x4, 0x32, 0x0,    0x0,    0x1, 0x1},
    {0x2044, 0xff, 0x0,  0x320, 0x0,  0x0,  0xff, 0x20, 0x0,    0x0,  0x0,   0x0,  0x0,
     0x0,    0x100,  0xff, 0xf,  0x61, 0xfc, 0x8, 0xff, 0x0,    0x0,    0x0, 0x1},
    {0x290,  0x64, 0x8,  0x96,  0xf,  0x0,  0xa0, 0x0,  0x4002, 0xf,  0x5dd, 0x37, 0x1,
     0x309e, 0x85e6, 0x6f, 0xc,  0x93, 0xfd, 0x4, 0x28, 0x0,    0x0,    0x1, 0x1},
    {0xc840, 0x4b, 0x0,  0x64,  0x32, 0x0,  0xb4, 0xc,  0x2c06, 0x0,  0x40,  0x21, 0x0,
     0x20ff, 0x3fe,  0x7f, 0xc,  0xa0, 0xfc, 0x4, 0xa,  0x0,    0x0,    0x0, 0x1},
    {0x240,  0x73, 0x10, 0xaf,  0x3c, 0x0,  0x64, 0x8,  0x6402, 0x4f, 0x9,   0xa,  0x0,
     0x3014, 0x14e8, 0x85, 0xf,  0xa2, 0xfe, 0x4, 0x37, 0x0,    0x0,    0x1, 0x1},
    {0x41,   0xff, 0x0,  0x64,  0x0,  0x0,  0xff, 0x0,  0x0,    0x0,  0x0,   0x0,  0x0,
     0x0,    0x100,  0xff, 0xf,  0x0,  0xf0, 0x0, 0x32, 0x4000, 0x0,    0x0, 0x1},
    {0x280,  0x5a, 0x21, 0x96,  0x1e, 0x0,  0x82, 0x0,  0xc002, 0x1,  0x1,   0x0,  0x0,
     0x10c7, 0x3da,  0x72, 0x6,  0xc5, 0xfe, 0x4, 0x41, 0x0,    0x0,    0x2, 0x2},
    {0xacb8, 0xff, 0x10, 0xc8,  0x0,  0x0,  0xff, 0x40, 0xc04,  0x0,  0x0,   0x0,  0x0,
     0x40ff, 0x1c7,  0xff, 0xf,  0x62, 0xfd, 0x4, 0xff, 0x0,    0x0,    0x1, 0x1},
    {0xacb8, 0xff, 0x10, 0xc8,  0x0,  0x0,  0xff, 0x0,  0x4802, 0x0,  0x0,   0x60, 0x0,
     0x40ff, 0x1c7,  0xff, 0xf,  0x62, 0xfd, 0x4, 0xff, 0x0,    0x0,    0x1, 0x1},
    {0x290,  0x8c, 0x8,  0x177, 0x50, 0x0,  0xb8, 0x0,  0x4002, 0xf,  0x10,  0x37, 0x1,
     0x30af, 0x55b9, 0xaf, 0xd,  0x93, 0xfd, 0x4, 0x32, 0x0,    0x0,    0x1, 0x1},
    {0x8081, 0xff, 0x0,  0x384, 0x0,  0x0,  0xff, 0x0,  0x0,    0x0,  0x0,   0x0,  0x0,
     0x0,    0x100,  0xff, 0xf,  0x0,  0xf0, 0x0, 0xff, 0x6000, 0x0,    0x0, 0x1},
    {0x240,  0x64, 0x10, 0xa0,  0x3c, 0x0,  0x78, 0x0,  0x6402, 0x4b, 0xd,   0x8,  0x1,
     0x3075, 0x1588, 0x55, 0xa,  0xa2, 0xfe, 0x4, 0xbe, 0x0,    0x0,    0x1, 0x1},
    {0x880,  0x6e, 0x3,  0x15e, 0x0,  0x0,  0xa,  0xc,  0x2,    0x1,  0x0,   0x0,  0x0,
     0x10ff, 0x3ff,  0x9c, 0x1f, 0x50, 0xfc, 0x4, 0x28, 0x0,    0x0,    0x0, 0x1},
    {0x45,   0x8c, 0x0,  0x104, 0x0,  0x0,  0x0,  0x0,  0x0,    0x0,  0x0,   0x0,  0x0,
     0x0,    0x100,  0x3f, 0xf,  0x0,  0xf0, 0x0, 0x37, 0x6000, 0x0,    0x0, 0x2},
    {0x85,   0xff, 0x0,  0x64,  0x0,  0x0,  0xff, 0x0,  0x0,    0x0,  0x0,   0x0,  0x0,
     0x0,    0x100,  0xff, 0xf,  0x0,  0xf0, 0x0, 0xff, 0x4000, 0x0,    0x0, 0x2},
    {0x8c78, 0x64, 0x32, 0x96,  0x28, 0x0,  0x96, 0xd,  0x6606, 0x0,  0x1,   0x0,  0x0,
     0x4034, 0x44c6, 0xf6, 0xf,  0x62, 0xfd, 0x4, 0xff, 0x0,    0x0,    0x1, 0x1},
    {0xc840, 0x4b, 0x8,  0x12c, 0x2d, 0x0,  0xb4, 0x1c, 0x6d9e, 0x0,  0x40,  0x21, 0x0,
     0x20ff, 0x3ec,  0x7f, 0xc,  0xa0, 0xfc, 0x4, 0xa,  0x0,    0x0,    0x0, 0x1},
};

const CreatureInfo *creature_info_at(int index)
{
    return index >= 0 && index < CREATURE_INFO_COUNT ? creature_infos + index : NULL;
}

int creature_gdat_attr(int creature_type, int attr)
{
    return gdat_entry_index(gdat_current(), CAT_CREATURE, creature_type, GDAT_TYPE_VALUE, attr);
}

const CreatureInfo *creature_info(const gdat_t *g, int creature_type)
{
    const CreatureInfo *info =
        creature_info_at(gdat_entry_index(g, CAT_CREATURE, creature_type, GDAT_TYPE_VALUE, CREATURE_KEY_INFO));
    return info ? info : creature_infos; /* fix: the original reads past the table */
}

const CreatureInfo *creature_group_info(uint16_t group)
{
    return creature_info(gdat_current(), group_creature(group));
}

ActiveGroup *creature_active(uint16_t group)
{
    int index = group_active(group);
    /* after a load the records keep the stale indices of the saved game until the session activates the groups */
    return index == GROUP_NOT_ACTIVE || index >= g_active_group_max ? NULL : g_active_groups + index;
}

uint16_t *creature_anim_words(uint16_t group)
{
    if (creature_group_info(group)->flags & CREATURE_STATIC)
        return group_anim_words(group);
    return &creature_active(group)->anim_sequence;
}

/* The raw GDAT data (0x0f, creature type, type, entry). */
static const uint8_t *read_creature_data(int creature_type, int type, int entry, uint32_t *size)
{
    const gdat_t *g = gdat_current();
    uint32_t n = 0;
    const uint8_t *data = gdat_item(g, gdat_find(g, CAT_CREATURE, creature_type, type, entry), &n);
    if (size)
        *size = n;
    return data;
}

/* The step of an animation state relative to the start of its sequence; a restarting loop clears its restart. */
static unsigned anim_frame_offset(uint16_t *state)
{
    if (*state & ANIM_STILL)
        return 0;
    if (!(*state & ANIM_LOOP))
        return *state & ANIM_FRAME_MASK;
    unsigned start = (*state & ANIM_START_MASK) >> ANIM_START_SHIFT;
    if (*state & ANIM_RESTART) {
        *state &= ANIM_RESTART_CLEAR;
        start = 0;
    }
    unsigned length = *state & ANIM_FRAME_MASK;
    return length ? (start + game.game_time) % length : 0; /* fix: the original divides by 0 */
}

void creature_anim_frame(const gdat_t *g, uint16_t group, uint8_t frame[CREATURE_FRAME_SIZE])
{
    int type = group_creature(group);
    uint16_t sequence = 0, state = 0;
    uint16_t *words = NULL;
    if (creature_info(g, type)->flags & CREATURE_STATIC || creature_active(group)) {
        words = creature_anim_words(group);
        sequence = words[0];
        state = words[1];
    }
    unsigned index = (uint16_t)(anim_frame_offset(&state) + sequence);
    if (words)
        words[1] = state;
    creature_anim_frame_of(g, type, (uint16_t)index, ANIM_STILL, frame);
}

void creature_anim_frame_of(const gdat_t *g, int type, uint16_t sequence, uint16_t state,
                            uint8_t frame[CREATURE_FRAME_SIZE])
{
    unsigned index = (uint16_t)(anim_frame_offset(&state) + sequence);
    uint32_t size;
    const uint8_t *data =
        gdat_item(g, gdat_find(g, CAT_CREATURE, type, GDAT_TYPE_CREATURE_DATA, GFX_CREATURE_FRAMES), &size);
    memset(frame, 0, CREATURE_FRAME_SIZE);
    if (data && (index + 1) * CREATURE_FRAME_SIZE <= size) /* fix: the original reads past the frames */
        memcpy(frame, data + index * CREATURE_FRAME_SIZE, CREATURE_FRAME_SIZE);
}

/* The 4-byte step of a creature animation at its current state. */
static const uint8_t *anim_current_step(int creature_type, uint16_t sequence, uint16_t *state)
{
    const uint8_t *steps = read_creature_data(creature_type, GDAT_TYPE_CREATURE_DATA, GFX_CREATURE_STEPS, NULL);
    return steps + (uint16_t)(anim_frame_offset(state) + sequence) * ANIM_STEP_SIZE;
}

/* The number of steps of the animation sequence that starts at sequence. */
static int anim_sequence_length(int creature_type, uint16_t sequence)
{
    const uint8_t *step = read_creature_data(creature_type, GDAT_TYPE_CREATURE_DATA, GFX_CREATURE_STEPS, NULL) +
                          sequence * ANIM_STEP_SIZE;
    int n = 1;
    while (step[STEP_NEXT] >> STEP_NEXT_SHIFT) {
        n++;
        step += ANIM_STEP_SIZE;
    }
    return n;
}

bool creature_anim_next_random_step(int creature_type, uint16_t sequence, uint16_t *step, const uint8_t **frame)
{
    const uint8_t *steps = read_creature_data(creature_type, GDAT_TYPE_CREATURE_DATA, GFX_CREATURE_STEPS, NULL);
    unsigned s = *step;
    bool has_duration = false;
    bool ended = false;
    if (s == ANIM_STEP_NONE) {
        s = 0;
    } else {
        int next = steps[((uint16_t)(s + sequence)) * ANIM_STEP_SIZE + STEP_NEXT] >> STEP_NEXT_SHIFT;
        if (!next)
            ended = true;
        else
            s += next;
    }
    if (!ended) {
        const uint8_t *p = steps + ((uint16_t)(s + sequence)) * ANIM_STEP_SIZE;
        while (p[STEP_NEXT] >> STEP_NEXT_SHIFT) {
            unsigned chance = p[STEP_NEXT] & STEP_CHANCE_MASK;
            if (chance != STEP_CHANCE_MASK && (rand_next() & STEP_CHANCE_MASK) > chance) {
                s++;
                p += ANIM_STEP_SIZE;
                continue;
            }
            if (((p[STEP_TIME] & STEP_RANDOM_TIME_MASK) >> STEP_RANDOM_TIME_SHIFT) +
                ((p[STEP_TIME] & STEP_TIME_MASK) >> STEP_TIME_SHIFT))
                has_duration = true;
            break;
        }
    }
    *step = (uint16_t)s;
    *frame = steps + ((uint16_t)(s + sequence)) * ANIM_STEP_SIZE;
    return has_duration;
}

AnimStepResult creature_anim_next_step(int creature_type, uint16_t sequence, uint16_t *step, const uint8_t **frame)
{
    const uint8_t *steps = read_creature_data(creature_type, GDAT_TYPE_CREATURE_DATA, GFX_CREATURE_STEPS, NULL);
    if (*step == ANIM_STEP_NONE) {
        *step = 0;
    } else {
        int jump = steps[((uint16_t)(*step + sequence)) * ANIM_STEP_SIZE + STEP_JUMP] & STEP_JUMP_MASK;
        if (!jump) {
            *frame = steps + ((uint16_t)(*step + sequence)) * ANIM_STEP_SIZE;
            return ANIM_SEQUENCE_END;
        }
        *step = (uint16_t)(*step + jump);
    }
    *frame = steps + ((uint16_t)(*step + sequence)) * ANIM_STEP_SIZE;
    return steps[((uint16_t)(*step + sequence)) * ANIM_STEP_SIZE + STEP_NEXT] >> STEP_NEXT_SHIFT ? ANIM_MORE_STEPS
                                                                                                 : ANIM_LAST_STEP;
}

bool creature_anim_start(int creature_type, int action, uint16_t *anim, const uint8_t **frame, uint16_t start)
{
    uint16_t *sequence = anim;
    uint16_t *state = anim + 1;
    const uint8_t *table = read_creature_data(creature_type, GDAT_TYPE_TABLE, GFX_CREATURE_SEQUENCES, NULL);
    for (;; table += SEQUENCE_ENTRY_SIZE) {
        int16_t a = (int16_t)le16(table);
        if (a == SEQUENCE_END || a == (int16_t)action)
            break;
    }
    uint16_t first = le16(table + SEQUENCE_FIRST_STEP);
    *sequence = first;
    if (!(creature_info(gdat_current(), creature_type)->flags & CREATURE_STATIC)) {
        *state = ANIM_STEP_NONE;
        return creature_anim_next_random_step(creature_type, first, state, frame);
    }
    uint16_t n = (uint16_t)anim_sequence_length(creature_type, first);
    *state = start ? (uint16_t)(n | (start & ANIM_FRAME_MASK) << ANIM_START_SHIFT | ANIM_LOOP)
                   : (uint16_t)(n | ANIM_LOOP | ANIM_RESTART);
    return true;
}

/* The first animation step of the idle sequence of a static creature. */
static void reset_idle_animation(uint16_t group)
{
    uint16_t *words = creature_anim_words(group);
    const uint8_t *frame;
    creature_anim_start(group_creature(group), AI_ACTION_IDLE, words, &frame, thing_word(group, GROUP_HOME));
}

int creature_current_step_acts(uint16_t group)
{
    uint16_t *words = creature_anim_words(group);
    return anim_current_step(group_creature(group), words[0], words + 1)[STEP_SOUND] >> STEP_ACTS_SHIFT;
}

uint16_t creature_get_group_at(int x, int y)
{
    return creature_group_at(party.cur_map, x, y);
}

uint16_t creature_group_at(int map, int x, int y)
{
    if (x < 0 || x >= dungeon_map_width(map) || y < 0 || y >= dungeon_map_height(map))
        return THING_NONE;
    for (uint16_t t = dungeon_square_first_thing(map, x, y); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        if (thing_type(t) == THING_GROUP)
            return t;
    return THING_NONE;
}

bool creature_is_native_to_map(uint16_t group, int map)
{
    if (creature_group_info(group)->flags & CREATURE_WANDERER)
        return true;
    uint8_t type = group_creature(group);
    const uint8_t *types = dungeon_map_creature_types(map);
    for (int i = 0; i < dungeon_map_creature_type_count(map); i++)
        if (types[i] == type)
            return true;
    return false;
}

/* True while an active group is in the air in a fall or jump. */
static bool is_floating(int active_index)
{
    const ActiveGroup *a = g_active_groups + active_index;
    return a->action == AI_ACTION_JUMP && (a->phase == 1 || a->phase == 2);
}

bool creature_is_flying(uint16_t group)
{
    if (creature_group_info(group)->movement & CREATURE_FLIES)
        return true;
    int active = group_active(group);
    return active != GROUP_NOT_ACTIVE && is_floating(active);
}

int creature_cell_mask(uint16_t possessions)
{
    /* by display kind; the original reads kinds 5 to 15 from the data after its table, which this keeps */
    static const uint8_t cells[POSSESSIONS_KIND_MASK + 1] = {0,    4,    0x0c, 0x0e, 0x0f, 0,    1, 8,
                                                             0x20, 0x40, 2,    0x80, 0,    0x12, 0, 0};
    if (!(possessions & STATIC_POSSESSIONS_SHOWN) || possessions & STATIC_POSSESSIONS_ALL)
        return CELLS_ALL;
    return cells[possessions & POSSESSIONS_KIND_MASK];
}

/* Active groups. */

int creature_active_group_event(int active_index)
{
    return g_active_groups[active_index].event;
}

int creature_group_jumps(uint16_t group)
{
    int active = group_active(group);
    return active != GROUP_NOT_ACTIVE && is_floating(active);
}

void creature_free_active_groups(void)
{
    free(g_active_groups);
    g_active_groups = NULL;
    g_active_group_max = 0;
    g_active_group_count = 0;
    ai_forget_all();
}

void creature_alloc_active_groups(void)
{
    int n = 0, count = dungeon_thing_count(THING_GROUP);
    for (int i = 0; i < count; i++) {
        uint16_t g = thing_make(THING_GROUP, i);
        if (thing_word(g, THING_NEXT) != THING_NONE && !(creature_group_info(g)->flags & CREATURE_STATIC))
            n++;
    }
    g_active_group_max = n + ACTIVE_GROUP_SPARE < count ? n + ACTIVE_GROUP_SPARE : count;
    free(g_active_groups);
    g_active_groups = calloc(g_active_group_max ? g_active_group_max : 1, sizeof *g_active_groups);
    if (!g_active_groups)
        os_fatal("out of memory");
}

void creature_reset_active_groups(void)
{
    creature_alloc_active_groups();
    g_active_group_count = 0;
    for (int i = 0; i < g_active_group_max; i++)
        g_active_groups[i].group = ACTIVE_FREE;
    for (int i = 0; i < dungeon_thing_count(THING_GROUP); i++)
        group_set_active(thing_make(THING_GROUP, i), GROUP_NOT_ACTIVE);
    creature_activate_all_maps();
}

void creature_activate_groups_on_map(void)
{
    int map = party.cur_map;
    for (int x = 0; x < dungeon_map_width(map); x++)
        for (int y = 0; y < dungeon_map_height(map); y++) {
            uint16_t g = creature_group_at(map, x, y);
            if (g == THING_NONE || group_active(g) != GROUP_NOT_ACTIVE)
                continue;
            if (!(creature_group_info(g)->flags & CREATURE_STATIC)) {
                creature_activate_group(g, x, y);
                continue;
            }
            uint16_t state = thing_word(g, GROUP_ANIM_STATE);
            reset_idle_animation(g);
            uint16_t now = thing_word(g, GROUP_ANIM_STATE);
            uint16_t keep = state & (ANIM_STILL | ANIM_LOOP_ALT);
            now |= keep;
            if ((state & (ANIM_LOOP | ANIM_FRAME_MASK)) == ANIM_SINGLE_LOOP)
                now = (uint16_t)((now & (uint16_t)~(ANIM_LOOP | ANIM_FRAME_MASK)) | keep | ANIM_SINGLE_LOOP);
            set_thing_word(g, GROUP_ANIM_STATE, now);
        }
}

void creature_activate_all_maps(void)
{
    int saved = party.cur_map;
    for (int map = 0; map < dungeon_map_count(); map++) {
        dun_set_current_map(map);
        creature_activate_groups_on_map();
    }
    dun_set_current_map(saved);
}

void creature_deactivate_all_groups(void)
{
    for (int i = 0; g_active_group_count; i++)
        if (g_active_groups[i].group != ACTIVE_FREE)
            creature_deactivate_group(i);
}

void creature_cancel_update(uint16_t group)
{
    if (thing_type(group) != THING_GROUP)
        return;
    ActiveGroup *a = creature_active(group);
    if (a && a->event >= 0) {
        timeline_delete_event((uint16_t)a->event);
        a->event = EVENT_NO_SLOT;
    }
}

/* Schedules the next update of the group on (x, y) for the next game step. */
static void schedule_update(int x, int y)
{
    uint16_t group = creature_get_group_at(x, y);
    ActiveGroup *a = creature_active(group);
    if (a->event != EVENT_NO_SLOT)
        creature_cancel_update(group);
    TimelineEvent e = {.time_map = timeline_time_map(game.game_time + 1, party.cur_map)};
    e.type = thing_word(group, GROUP_ITEM_CLASS) != THING_NONE ? EVENT_CREATURE_THINK : EVENT_CREATURE_ANIMATION;
    e.priority = group_creature(group);
    e.data[0] = (uint8_t)x;
    e.data[1] = (uint8_t)y;
    a->event = (int16_t)timeline_add_event(&e);
}

void creature_activate_group(uint16_t group, int x, int y)
{
    if (group_active(group) != GROUP_NOT_ACTIVE)
        return;
    /* the group keeps its record while it looks for a free active group */
    bool kept = group_flag(group, GROUP_DO_NOT_DISCARD);
    group_set_flag(group, GROUP_DO_NOT_DISCARD, true);
    {
        for (int i = 0; i < g_active_group_max; i++) {
            ActiveGroup *a = g_active_groups + i;
            if (a->group != ACTIVE_FREE)
                continue;
            group_set_flag(group, GROUP_DO_NOT_DISCARD, kept);
            g_active_group_count++;
            memset(a, 0, sizeof *a);
            a->group = thing_index(group);
            group_set_active(group, (uint8_t)i);
            a->event = EVENT_NO_SLOT;
            a->action = ACTIVE_ACTION_NONE;
            AiContext saved;
            bool restored = ai_begin(group, x, y, EVENT_CREATURE_THINK, &saved);
            g_ai.active->plan = -1;
            g_ai.active->plan_step = 0;
            a->regeneration_time = (uint8_t)((game.game_time >> REGEN_TIME_SHIFT) - 1);
            a->last_action_time = (uint8_t)(game.game_time - ACTION_TIME_LONG_AGO);
            a->position = (uint16_t)location_pack(g_ai.event.data[0], g_ai.event.data[1], party.cur_map);
            g_ai.cache.path_left = 0;
            a->behavior = -1;
            a->next_action = -1;
            a->anim_bits = 0;
            schedule_update(x, y);
            a->action = thing_word(group, GROUP_ITEM_CLASS) == THING_NONE ? AI_ACTION_IDLE : 0;
            if (!(g_ai.info->flags & CREATURE_STATIC)) {
                group_set_mood_flag(g_ai.group_thing, MOOD_FAR, true);
                group_set_mood_flag(g_ai.group_thing, MOOD_HURRY, false);
                creature_animate_step();
            }
            ai_end(restored, &saved);
            return;
        }
        /* no free entry: the original discards a creature group of the dungeon and tries again; the port
           leaves the group inactive (the table has room for 100 more groups than the dungeon has) */
        group_set_flag(group, GROUP_DO_NOT_DISCARD, kept);
        return;
    }
}

void creature_deactivate_group(int active_index)
{
    if (active_index > g_active_group_max)
        return;
    ActiveGroup *a = g_active_groups + active_index;
    if (a->group == ACTIVE_FREE)
        return;
    uint16_t group = thing_make(THING_GROUP, a->group);
    bool vanish = !(creature_group_info(group)->flags & CREATURE_STATIC) && a->action == CREATURE_ACTION_VANISH;
    int x = 0, y = 0;
    if (vanish) {
        if (a->event == EVENT_NO_SLOT)
            vanish = false;
        else {
            x = g_timeline.events[a->event].data[0];
            y = g_timeline.events[a->event].data[1];
        }
    }
    a->action = 0;
    creature_cancel_update(group);
    g_active_group_count--;
    group_set_active(group, GROUP_NOT_ACTIVE);
    a->group = ACTIVE_FREE;
    if (vanish)
        creature_delete_group_at(x, y, DROP_ALL, 1);
}

void creature_set_next_action(uint16_t group, int x, int y, int action, int now)
{
    if (group == THING_NONE && (group = creature_get_group_at(x, y)) == THING_NONE)
        return;
    ActiveGroup *a = creature_active(group);
    if (!a || a->next_action == CREATURE_ACTION_VANISH || a->action == CREATURE_ACTION_VANISH)
        return;
    a->next_action = (int8_t)action;
    if (!now)
        return;
    if (ai_action_flag(a->action) & AI_ACTION_FLAG_INTERRUPT) {
        a->interrupt = 1;
        return;
    }
    creature_cancel_update(group);
    schedule_update(x, y);
}

/* A looping animation of a static creature: the next update comes when the loop reaches its first frame. */
static void schedule_loop_start(void)
{
    uint16_t *state = g_ai.anim + 1;
    if ((*state & ANIM_KIND_MASK) == ANIM_SINGLE_LOOP)
        return;
    int length = anim_sequence_length(group_creature(g_ai.group_thing), g_ai.anim[0]);
    uint16_t start = *state & ANIM_START_MASK;
    unsigned at = (start + game.game_time) % (unsigned)length;
    if (!(uint16_t)at) {
        *state = start | ANIM_SINGLE_LOOP;
        return;
    }
    *state = (uint16_t)(start | length | ANIM_LOOP | ANIM_STILL);
    creature_cancel_update(g_ai.group_thing);
    g_ai.event.time_map = timeline_time_map((uint32_t)(length - (int16_t)at) + game.game_time, g_ai.map);
    g_ai.active->event = (int16_t)timeline_add_event(&g_ai.event);
}

/* A static creature that plays its sequence to the end: the next update comes at the end of the loop. */
static void schedule_loop_end(void)
{
    uint16_t *state = g_ai.anim + 1;
    if ((*state & (ANIM_LOOP | ANIM_STILL | ANIM_LOOP_ALT)) == ANIM_LOOP && (*state & ANIM_FRAME_MASK) > 1)
        return;
    int length = anim_sequence_length(group_creature(g_ai.group_thing), g_ai.anim[0]);
    uint16_t start = *state & ANIM_START_MASK;
    unsigned at = (start + game.game_time) % (unsigned)length;
    uint16_t word = (uint16_t)(length | start);
    if (!(uint16_t)at) {
        *state = word | ANIM_LOOP;
        return;
    }
    *state = word | ANIM_LOOP | ANIM_LOOP_ALT;
    creature_cancel_update(g_ai.group_thing);
    g_ai.event.time_map = timeline_time_map((uint32_t)(length - (int16_t)at) + game.game_time, g_ai.map);
    g_ai.active->event = (int16_t)timeline_add_event(&g_ai.event);
}

void creature_update_anim_at(int x, int y, int alt)
{
    uint16_t group = creature_get_group_at(x, y);
    if (group == THING_NONE)
        return;
    if (creature_group_info(group)->flags & CREATURE_STATIC && group_active(group) == GROUP_NOT_ACTIVE)
        creature_activate_group(group, x, y);
    AiContext saved;
    bool restored = ai_begin(group, x, y, EVENT_CREATURE_ANIMATION, &saved);
    if (g_ai.active) {
        if (!alt)
            schedule_loop_start();
        else
            schedule_loop_end();
    }
    ai_end(restored, &saved);
}

/* A random picture shift of up to range - 1 to either side, in the 3 bits of an anim bits field. */
static uint8_t anim_random_shift(int range)
{
    unsigned v = rand_below((uint16_t)range);
    if (rand_bit())
        v = -v & ANIM_JITTER;
    return (uint8_t)v;
}

/* The anim bits of the creature in the AI context after a step: a random shift by its traits and a random mirror. */
static uint8_t anim_step_bits(const uint8_t *step)
{
    uint8_t traits = g_ai.info->traits;
    uint8_t bits = g_ai.active->anim_bits;
    if (step[STEP_TIME] & STEP_JITTER && g_ai.active->action != AI_ACTION_ATTACK_2 &&
        g_ai.active->action != AI_ACTION_STAND_1 && g_ai.active->action != AI_ACTION_STAND_2) {
        bits &= ANIM_BIT_KEEP;
        if (traits & TRAIT_JITTER_MASK)
            bits |= anim_random_shift(traits & TRAIT_JITTER_MASK);
        if (traits >> TRAIT_SHIFT_Y & TRAIT_JITTER_MASK)
            bits |= (uint8_t)(anim_random_shift(traits >> TRAIT_SHIFT_Y & TRAIT_JITTER_MASK) << ANIM_BIT_SHIFT_Y);
    }
    if (step[STEP_TIME] & STEP_MIRROR) {
        if (rand_bit())
            bits |= ANIM_BIT_MIRROR;
        else
            bits &= ~ANIM_BIT_MIRROR;
    }
    return bits;
}

/* The game steps of an animation step of the creature in the AI context: its mood, a vanish, and the party change it. */
static int16_t anim_step_delay(const uint8_t *step)
{
    uint16_t mood = group_mood(g_ai.group_thing);
    int base = step[STEP_TIME] >> STEP_TIME_SHIFT;
    if (mood & MOOD_QUICK)
        return (int16_t)(base < 1 ? base : 1);
    int r = (step[STEP_TIME] & STEP_RANDOM_TIME_MASK) >> STEP_RANDOM_TIME_SHIFT;
    int16_t d = (int16_t)((r ? rand_below((uint16_t)r) : 0) + base);
    if (g_ai.active->action == CREATURE_ACTION_VANISH && game.magic_light[MAGIC_ACTION_EFFECT] &&
        !(g_ai.info->flags & CREATURE_FAST_VANISH)) {
        d = (int16_t)(d * VANISH_DELAY_FACTOR);
    } else if (party.cur_map != g_ai_party.map && !(g_ai_class_flags[g_ai.class_index] & AI_CLASS_STAYS_FAR) &&
               mood & MOOD_FAR && !(mood & GROUP_SEEN)) {
        d = (int16_t)(d * FAR_DELAY_FACTOR + rand_bit());
    } else if (g_party_sleeping) {
        d = (int16_t)(d * 2);
        if (party.cur_map != g_ai_party.map)
            d = (int16_t)(d * 2);
    } else if (mood & MOOD_FEAR) {
        if (!(mood & MOOD_HURRY) || d > FEAR_HURRY_DELAY)
            d = (int16_t)(d * FEAR_DELAY_PERCENT / PERCENT);
        if (d < 1)
            d = 1;
    }
    return d;
}

uint32_t creature_animate_step(void)
{
    int type = group_creature(g_ai.group_thing);
    uint16_t anim[] = {g_ai.anim[0], g_ai.anim[1]};
    static const uint8_t no_step[ANIM_STEP_SIZE];
    bool own_step = false;
    if (!g_ai.anim_step) {
        uint16_t start = g_ai.info->flags & CREATURE_STATIC ? thing_word(g_ai.group_thing, GROUP_HOME) : 0;
        creature_anim_start(type, (int8_t)g_ai.active->action, anim, &g_ai.anim_step, start);
        if (!g_ai.anim_step) {
            g_ai.anim_step = no_step;
            own_step = true;
        }
    }
    const uint8_t *step = g_ai.anim_step;
    g_ai.active->anim_bits = anim_step_bits(step);
    g_ai.anim[0] = anim[0];
    g_ai.anim[1] = anim[1];
    if ((step[STEP_SOUND] & STEP_SOUND_MASK) != STEP_NO_SOUND)
        sound_request(CAT_CREATURE, type, step[STEP_SOUND] & STEP_SOUND_MASK, SOUND_PRIORITY_CREATURE,
                      SOUND_VOLUME_NORMAL, g_ai.event.data[0], g_ai.event.data[1], 1);
    int16_t delay = anim_step_delay(step);
    if (own_step)
        g_ai.anim_step = NULL;
    return (uint32_t)(delay + (int32_t)game.game_time);
}

void creature_process_event(int x, int y, int type)
{
    uint16_t group = creature_get_group_at(x, y);
    if (group == THING_NONE)
        return;
    AiContext saved;
    bool restored = ai_begin(group, x, y, type, &saved);
    ActiveGroup *a = g_ai.active;
    const CreatureInfo *info = g_ai.info;
    a->event = EVENT_NO_SLOT;
    if (!thing_word(group, GROUP_HEALTH)) {
        a->damage = 1;
        set_thing_word(group, GROUP_HEALTH, a->damage);
    }
    int16_t damage = (int16_t)a->damage;
    a->damage = 0;
    int16_t regen = info->regeneration;
    if (regen) {
        if (regen < 0)
            regen = (int16_t)-regen;
        unsigned now = (uint8_t)(game.game_time >> REGEN_TIME_SHIFT);
        int16_t elapsed = (int16_t)((a->regeneration_time > now ? now + TIME_BYTE_WRAP : now) - a->regeneration_time);
        int16_t q = (int16_t)(elapsed / regen);
        if (q > 0) {
            if (info->regeneration < 0)
                damage = (int16_t)(damage + q);
            else if (thing_word(group, GROUP_HEALTH) < info->max_health)
                set_thing_word(group, GROUP_HEALTH, (uint16_t)(thing_word(group, GROUP_HEALTH) + q));
            a->regeneration_time = (uint8_t)((game.game_time >> REGEN_TIME_SHIFT) - elapsed % regen);
        }
    }
    if (damage > 0) {
        if (!(info->flags & CREATURE_STATIC))
            group_set_mood_flag(g_ai.group_thing, MOOD_FAR, false);
        if (creature_damage_current_group((uint16_t)damage)) {
            ai_end(restored, &saved);
            return;
        }
    }
    if (!(info->flags & CREATURE_STATIC)) {
        ai_update_group();
    } else {
        if (g_ai.anim[1] & ANIM_STILL)
            schedule_loop_start();
        else if (g_ai.anim[1] & ANIM_LOOP_ALT)
            schedule_loop_end();
        if (a->event == EVENT_NO_SLOT)
            creature_deactivate_group(group_active(g_ai.group_thing));
    }
    ai_end(restored, &saved);
}

/* Group life: creation, damage, death, alerts. */

/* A random cell for an item that a creature drops on (x, y): beside the party on its square. */
static int drop_cell(int x, int y)
{
    if (x == game.party_x && y == game.party_y)
        return dir_turn(game.party_dir, rand_bit());
    return rand_2bits();
}

void creature_drop_possessions(uint16_t group, int x, int y, int mode, int sound_map)
{
    if (mode == DROP_NOTHING)
        return;
    if (mode == DROP_ALL) {
        for (int attr = CREATURE_ATTR_DROP_FIRST; attr <= CREATURE_ATTR_DROP_LAST; attr++) {
            int value = creature_gdat_attr(group_creature(group), attr);
            if (!(uint16_t)value)
                continue;
            int count = (value & DROP_AMOUNT_MASK) + 1;
            int extra = (value & DROP_RANDOM_MASK) >> DROP_RANDOM_SHIFT;
            if (extra)
                count = rand_below((uint16_t)(extra + 1)) + count;
            while ((int16_t)--count != -1) {
                uint16_t item = item_create_by_index((uint16_t)value >> DROP_ITEM_SHIFT);
                if (item == THING_NONE)
                    break;
                uint16_t t = thing_with_cell(item, drop_cell(x, y));
                move_thing(t, MOVE_NEW_ITEM, 0, x, y);
                if ((int16_t)sound_map >= 0)
                    sound_request_with_fallback(item_gdat_category(item), item_gdat_index(item), SOUND_ENTRY_DROP,
                                                SOUND_FALLBACK, x, y, (int16_t)sound_map, SOUND_PRIORITY_DROP,
                                                SOUND_VOLUME_NORMAL);
            }
        }
    }
    uint16_t t = thing_word(group, GROUP_POSSESSIONS);
    while (t != THING_ENDOFLIST) {
        uint16_t next = dungeon_next_thing(t);
        if (!(creature_group_info(group)->flags & CREATURE_STATIC))
            t = thing_with_cell(t, drop_cell(x, y));
        if (thing_type(t) == THING_PROJECTILE) {
            set_thing_word(t, THING_NEXT, THING_NONE);
        } else {
            move_thing(t, MOVE_NEW_ITEM, 0, x, y);
            if ((int16_t)sound_map >= 0)
                sound_request_with_fallback(item_gdat_category(t), item_gdat_index(t), SOUND_ENTRY_DROP, SOUND_FALLBACK,
                                            x, y, (int16_t)sound_map, SOUND_PRIORITY_DROP, SOUND_VOLUME_NORMAL);
        }
        t = next;
    }
}

/* The home square of a creature group gets a square event (effect) at the next game step. */
static void signal_home_square(uint16_t group, int effect)
{
    int saved = party.cur_map;
    uint16_t home = thing_word(group, GROUP_HOME);
    dun_set_current_map(location_map(home));
    sensor_schedule_square_event(location_x(home), location_y(home), 0, effect, game.game_time + 1);
    dun_set_current_map(saved);
}

void creature_delete_group_at(int x, int y, int mode, int sound_map)
{
    uint16_t group = creature_get_group_at(x, y);
    if (group == THING_NONE)
        return;
    if (!(creature_group_info(group)->flags & CREATURE_STATIC)) {
        if (!(g_ai_class_flags[(int16_t)creature_gdat_attr(group_creature(group), CREATURE_ATTR_CLASS)] &
              AI_CLASS_NO_HOME_EVENT))
            signal_home_square(group, 0);
        if (group_active(group) != GROUP_NOT_ACTIVE)
            g_active_groups[group_active(group)].action = 0;
    }
    move_thing(group, x, y, MOVE_CREATED, 0);
    creature_drop_possessions(group, x, y, mode, sound_map);
    ai_free_cached_data(group);
    set_thing_word(group, THING_NEXT, THING_NONE);
}

/* True when (x, y) of map is near the party, in front of it, and the party sees it. */
static bool is_near_party_front(int x, int y, int map)
{
    if (map != g_ai_party.map || g_party_sleeping || ui.inventory)
        return false;
    int dx = abs(g_ai_party.x - x), dy = abs(g_ai_party.y - y);
    if (dx == dy)
        return false;
    if (dun_get_direction_to(g_ai_party.x, g_ai_party.y, x, y) != g_ai_party.dir)
        return false;
    return dx < NEAR_PARTY_DISTANCE && dy < NEAR_PARTY_DISTANCE;
}

bool creature_damage_current_group(uint16_t damage)
{
    const CreatureInfo *info = g_ai.info;
    if (info->defense == CREATURE_NO_HURT)
        return false;
    bool is_static = info->flags & CREATURE_STATIC;
    uint32_t class_flags = 0;
    if (!is_static) {
        class_flags =
            g_ai_class_flags[(uint16_t)creature_gdat_attr(group_creature(g_ai.group_thing), CREATURE_ATTR_CLASS)];
        if (!(class_flags & AI_CLASS_NO_HOME_EVENT))
            signal_home_square(g_ai.group_thing, 1);
    }
    uint16_t health = thing_word(g_ai.group_thing, GROUP_HEALTH);
    if (health <= damage) {
        set_thing_word(g_ai.group_thing, GROUP_HEALTH, 1);
        if (!is_static) {
            if (class_flags & AI_CLASS_DIES_NEAR_PARTY) {
                if (!is_near_party_front(g_ai.event.data[0], g_ai.event.data[1], g_ai.map))
                    return false;
                g_hooks.set_wait(BOSS_DEATH_WAIT);
            }
            creature_set_next_action(g_ai.group_thing, g_ai.event.data[0], g_ai.event.data[1], CREATURE_ACTION_VANISH,
                                     1);
            return false;
        }
        creature_delete_group_at(g_ai.event.data[0], g_ai.event.data[1], DROP_ALL, 1);
        return true;
    }
    if (!is_static && class_flags & AI_CLASS_SCREAMS) {
        bool scream = !(rand_next() & SCREAM_ROLL_MASK);
        if (!scream && ((int)damage > (int)(info->max_health * SCREAM_MAX_HEALTH_PERCENT) / PERCENT ||
                        (int)damage > (int)(health * SCREAM_HEALTH_PERCENT) / PERCENT)) {
            if (group_mood_flag(g_ai.group_thing, MOOD_FEAR) && rand_bit())
                scream = true;
            else
                scream = !rand_2bits();
        }
        if (scream)
            sound_request(CAT_CREATURE, group_creature(g_ai.group_thing), SOUND_ENTRY_HURT + (rand_bit() != 0),
                          SOUND_PRIORITY_CREATURE, SOUND_VOLUME_NORMAL, g_ai.event.data[0], g_ai.event.data[1], 1);
    }
    set_thing_word(g_ai.group_thing, GROUP_HEALTH, (uint16_t)(health - damage));
    return false;
}

int creature_attack_group_at(int x, int y)
{
    uint16_t group = creature_get_group_at(x, y);
    if (group == THING_NONE)
        return -1;
    const CreatureInfo *target = creature_group_info(group), *attacker = g_ai.info;
    if (target->dexterity == CREATURE_NO_HURT)
        return 0;
    int16_t hit = (int16_t)((rand_next() & ROLL_COMBAT) + attacker->dexterity);
    int16_t dodge = (int16_t)(target->dexterity + (rand_next() & ROLL_COMBAT));
    if (dodge > hit && rand_2bits())
        return 0;
    int defense = (target->defense + (uint16_t)(rand_next() & ROLL_COMBAT)) >> CREATURE_DEFENSE_SHIFT;
    int r = rand_next() & ATTACK_ROLL_MASK;
    int16_t damage = (int16_t)(attacker->attack + (attacker->attack < r ? attacker->attack : r) - defense);
    if (damage <= 1) {
        if (rand_bit())
            return 0;
        damage = (int16_t)(rand_2bits() + ATTACK_MIN_DAMAGE);
    }
    int a = rand_below((uint16_t)damage);
    damage = (int16_t)(damage + a + rand_2bits());
    damage = (int16_t)(damage + rand_below((uint16_t)damage));
    damage = (int16_t)(damage >> ATTACK_DAMAGE_SHIFT);
    damage = (int16_t)(damage + rand_2bits() + 1);
    if (rand_bit())
        damage = (int16_t)(damage - rand_below((uint16_t)((damage >> ATTACK_DAMAGE_SHIFT) + 1)));
    creature_alert(group, (Location){x, y}, ALERT_SET_ANGRY, ALERT_HIT_CHANCE, (uint16_t)damage);
    return damage;
}

/* A creature turns toward an alarm: the turn action (left or right) toward the party, or none. */
static int alert_turn(uint16_t group, int x, int y)
{
    uint16_t mood = group_mood(group);
    int facing = group_dir(group);
    int dir = dun_get_direction_to(x, y, game.party_x, game.party_y);
    if ((mood & MOOD_FEAR && rand_2bits()) || (dir != facing && !rand_2bits()))
        dir = dir_opposite(dir);
    if (facing == dir_opposite(dir))
        return rand_bit() ? AI_ACTION_TURN_RIGHT : AI_ACTION_TURN_LEFT;
    if (facing != dir)
        return facing == dir_turn(dir, TURN_LEFT) ? AI_ACTION_TURN_RIGHT : AI_ACTION_TURN_LEFT;
    if (rand_2bits())
        return -1;
    return rand_bit() ? AI_ACTION_TURN_LEFT : AI_ACTION_TURN_RIGHT;
}

/* An alert makes the group angry by its damage total; an angry creature may turn toward the party. */
static void alert_anger(uint16_t group, int x, int y, uint16_t total, bool anger)
{
    if (!group_mood_flag(group, MOOD_ANGRY) &&
        (total > ANGER_DAMAGE || (total > ANGER_DAMAGE_MAYBE && !rand_2bits()) ||
         total * (unsigned)PERCENT / creature_group_info(group)->max_health > ANGER_HEALTH_PERCENT)) {
        group_set_mood_flag(group, MOOD_ANGRY, true);
        anger = true;
    }
    if (anger &&
        !(g_ai_class_flags[(uint16_t)creature_gdat_attr(group_creature(group), CREATURE_ATTR_CLASS)] &
          AI_CLASS_NO_TURN) &&
        rand_bit()) {
        int action = alert_turn(group, x, y);
        if (action != -1)
            creature_set_next_action(group, x, y, action, 0);
    }
}

/* True when an alert that changed the mood wakes the creature from its current action. */
static bool alert_wakes(uint16_t group, const ActiveGroup *a)
{
    uint8_t f = ai_action_flag((int8_t)a->action);
    if (!(f & AI_ACTION_FLAG_INTERRUPT))
        return true;
    return g_ai_class_flags[(uint16_t)creature_gdat_attr(group_creature(group), CREATURE_ATTR_CLASS)] &
               AI_CLASS_WAKES &&
           f & AI_ACTION_FLAG_WAKES;
}

void creature_alert(uint16_t group, Location at, int flags, int chance, int damage)
{
    int x = at.x;
    int y = at.y;
    if (group == THING_NONE && (group = creature_get_group_at(x, y)) == THING_NONE)
        return;
    bool anger = flags & ALERT_ANGER;
    if (anger) {
        flags &= ~ALERT_ANGER;
        if (rand_bit())
            anger = false;
    }
    bool loud = flags & ALERT_LOUD;
    flags &= ~ALERT_LOUD;
    bool is_static = creature_group_info(group)->flags & CREATURE_STATIC;
    if (group_active(group) == GROUP_NOT_ACTIVE) {
        if (!is_static)
            return;
        creature_activate_group(group, x, y);
    }
    ActiveGroup *a = creature_active(group);
    if (!a)
        return; /* fix: a stale index of a save before the session activates the groups */
    a->damage = (uint16_t)(a->damage + damage);
    uint16_t total = a->damage;
    bool changed = false;
    bool clear = false;
    uint16_t mask = 0;
    if (!is_static && (int16_t)chance > 0) {
        alert_anger(group, x, y, total, anger);
        changed = (int16_t)chance > (int)rand_below(PERCENT);
        if (changed) {
            clear = flags & ALERT_CLEAR;
            mask = (uint16_t)(1 << (flags & BIT_INDEX_MASK));
            uint16_t mood = group_mood(group);
            group_set_mood(group, clear ? mood & ~mask : mood | mask);
        }
    }
    bool wake = false;
    if (!is_static && loud && !(int16_t)chance)
        wake = true;
    else if (changed && ((!clear && mask & ALERT_MOOD_BIT) || loud))
        wake = alert_wakes(group, a);
    if ((int8_t)a->action == CREATURE_ACTION_VANISH)
        return;
    if (!wake && total < thing_word(group, GROUP_HEALTH))
        return;
    creature_cancel_update(group);
    schedule_update(x, y);
}

int creature_roll_armor_factor(uint16_t group, int amount)
{
    if (!(uint16_t)amount)
        return 0;
    int armor = creature_armor(creature_group_info(group));
    if (armor == ARMOR_IMMUNE)
        return 0;
    int r = rand_2bits();
    return ((uint16_t)amount + (uint16_t)r) * ARMOR_FACTOR / (armor + ARMOR_BASE);
}

void creature_apply_damage_kind(uint16_t group, int x, int y, int kind)
{
    int k = kind & (uint16_t)~ALERT_CLEAR;
    if (k >= DAMAGE_KIND_MOOD_FIRST && k < DAMAGE_KIND_MOOD_END)
        creature_alert(group, (Location){x, y}, (k - DAMAGE_KIND_MOOD_FIRST) | (kind & ALERT_CLEAR), ALERT_ALWAYS, 0);
}

uint16_t creature_create_group(int creature_type, int count, int dir, int x, int y)
{
    int minion = creature_type & GROUP_MINION;
    uint16_t projectile = THING_NONE;
    if (minion) {
        creature_type &= ~GROUP_MINION;
        projectile = dungeon_unused_thing(THING_PROJECTILE);
        if (projectile == THING_NONE)
            return THING_NONE;
    }
    uint16_t group = dungeon_unused_thing(THING_GROUP);
    if (group == THING_NONE || g_active_group_count >= g_active_group_max) {
        if (minion)
            set_thing_word(projectile, THING_NEXT, THING_NONE);
        return THING_NONE;
    }
    group_set_creature(group, (uint8_t)creature_type);
    const CreatureInfo *info = creature_info(gdat_current(), creature_type);
    set_thing_word(group, GROUP_POSSESSIONS, THING_ENDOFLIST);
    group_set_flag(group, GROUP_DO_NOT_DISCARD, false);
    group_set_dir(group, dir);
    group_set_active(group, GROUP_NOT_ACTIVE);
    if (minion)
        dungeon_append_thing(projectile, group_possessions(group));
    int n = (count < GROUP_SIZE_MAX ? count : GROUP_SIZE_MAX) + 1;
    uint16_t health = (uint16_t)((info->max_health * (uint16_t)n) >> GROUP_HEALTH_SHIFT);
    health = (uint16_t)(health + rand_below((uint16_t)((health >> GROUP_HEALTH_SHIFT) + 1)));
    set_thing_word(group, GROUP_HEALTH, health);
    group_set_mood(group, 0);
    set_thing_word(group, GROUP_HOME, (uint16_t)location_pack(x, y, party.cur_map));
    set_thing_word(group, GROUP_ITEM_CLASS, THING_NONE);
    if (move_thing(group, MOVE_CREATED, 0, x, y) == 1) {
        /* the original frees a stale thing here when the group links no projectile */
        if (minion)
            set_thing_word(projectile, THING_NEXT, THING_NONE);
        return THING_NONE;
    }
    set_thing_word(group, GROUP_ITEM_CLASS, 0);
    return group;
}

uint16_t creature_summon_group(CreatureSummon summon)
{
    int x = summon.x;
    int y = summon.y;
    int link = summon.link;
    int search_dir = summon.search_dir;
    int saved = party.cur_map;
    dun_set_current_map((int16_t)summon.map);
    uint16_t group = THING_NONE;
    int tries = (int8_t)search_dir < 0 ? 0 : DIR_COUNT;
    int turn = rand_bit() ? 1 : -1;
    int16_t sx = (int16_t)x, sy = (int16_t)y;
    for (;;) {
        uint8_t square = dun_get_square(sx, sy);
        int type = square_type(square);
        if (type != SQUARE_WALL && type != SQUARE_STAIRS &&
            (type != SQUARE_FAKE_WALL || square & (SQUARE_FAKE_WALL_OPEN | SQUARE_FLAG_0)) &&
            (type != SQUARE_DOOR || !(square & SQUARE_DOOR_STATE) ||
             (square & SQUARE_DOOR_STATE) == DOOR_STATE_BROKEN) &&
            creature_get_group_at(sx, sy) == THING_NONE) {
            group = creature_create_group(summon.creature_type | ((int16_t)link != -1 ? GROUP_MINION : 0), summon.level,
                                          summon.dir, sx, sy);
            if (group != THING_NONE)
                break;
        }
        if (tries) {
            sx = (int16_t)(x + dir_dx[(int8_t)search_dir]);
            sy = (int16_t)(y + dir_dy[(int8_t)search_dir]);
            search_dir = dir_turn(search_dir, turn);
        }
        if (--tries == -1)
            break;
    }
    if ((int16_t)link != -1 && group != THING_NONE) {
        uint16_t t = thing_word(group, GROUP_POSSESSIONS);
        while (t != THING_ENDOFLIST && thing_type(t) != THING_PROJECTILE)
            t = dungeon_next_thing(t);
        set_thing_word(t, PROJECTILE_PAYLOAD, (uint16_t)link);
    }
    dun_set_current_map(saved);
    return group;
}

/* The minion of an item vanishes. */
void minion_dismiss(uint16_t link)
{
    uint16_t projectile = minion_find_link_projectile(link, THING_NONE);
    int saved = party.cur_map;
    if (!projectile)
        return;
    uint16_t at = thing_word(projectile, PROJECTILE_LOCATION);
    dun_set_current_map(location_map(at));
    creature_set_next_action(link, location_x(at), location_y(at), CREATURE_ACTION_VANISH, 1);
    dun_set_current_map(saved);
}

void creature_turn_group(uint16_t group, int absolute, int dir)
{
    int old = group_dir(group);
    if (!absolute)
        dir = dir_turn(dir, old);
    group_set_dir(group, dir);
    if (!(creature_group_info(group)->flags & CREATURE_STATIC))
        return;
    int turn = dir_relative(dir, old);
    for (uint16_t *link = group_possessions(group); *link != THING_ENDOFLIST; link = thing_list(*link, THING_NEXT))
        *link = thing_with_cell(*link, dir_turn(thing_cell(*link), turn));
}

/* Keeps the position of the projectiles that a group carries. */
void move_update_carried_missiles(uint16_t holder, int x, int y, int map, int flag)
{
    for (uint16_t t = thing_word(holder, GROUP_POSSESSIONS); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_PROJECTILE)
            continue;
        set_thing_word(t, PROJECTILE_LOCATION, (uint16_t)location_pack(x, y, map));
        projectile_set_holder_waits(t, flag & 1);
    }
}

/* Schedules a new try of a group move that a full square stopped. */
void move_schedule_group_retry(uint16_t group, int x, int y, int map, int audible)
{
    TimelineEvent e = {.time_map = timeline_time_map(game.game_time + GROUP_RETRY_DELAY, map)};
    e.type = audible ? EVENT_MOVE_GROUP_2 : EVENT_MOVE_GROUP;
    e.data[0] = (uint8_t)x;
    e.data[1] = (uint8_t)y;
    put_le16(e.data + EVENT_GROUP_WORD, group);
    move_update_carried_missiles(group, x, y, map, 1);
    timeline_add_event(&e);
}

/* A group moves to the square that it could not enter before. */
void timeline_event_move_group(TimelineEvent *e)
{
    int x = e->data[0], y = e->data[1];
    if (!move_thing(le16(e->data + EVENT_GROUP_WORD), MOVE_RETRY, 0, x, y) && e->type == EVENT_MOVE_GROUP_2)
        sound_request(CAT_DUNGEON, 0, SOUND_ENTRY_TELEPORT, SOUND_PRIORITY_MOVE, SOUND_VOLUME_NORMAL, x, y, 1);
}

int creature_get_party_distance(int x, int y)
{
    const uint8_t *map;
    int width;
    if (party.cur_map == g_party_distance.map) {
        map = g_party_distance.cells;
        width = g_party_distance.width;
    } else if (party.cur_map == g_alt_distance.map) {
        map = g_alt_distance.cells;
        width = g_alt_distance.width;
    } else {
        return -1;
    }
    if ((int16_t)x >= width)
        return -1;
    uint8_t d = map[x * DISTANCE_MAP_SIDE + y];
    if (d & DISTANCE_NEXT_TO) {
        d = DISTANCE_FAR;
        for (int dir = 0; dir < DIR_COUNT; dir++) {
            int16_t nx = (int16_t)(dir_dx[dir] + x), ny = (int16_t)(y + dir_dy[dir]);
            if (nx < 0 || nx >= width || ny < 0 || ny >= dungeon_map_height(party.cur_map))
                continue;
            uint8_t n = map[nx * DISTANCE_MAP_SIDE + ny];
            if (n && n < d)
                d = n;
        }
    }
    return d - 1;
}

/* Pushing creatures. */

bool creature_can_be_pushed(int x, int y, int dir, uint16_t *group)
{
    uint16_t g = creature_get_group_at(x, y);
    if (g == THING_NONE || creature_group_info(g)->push_resistance >= PUSH_IMMOVABLE)
        return false;
    PassageEnd end;
    if (dun_get_linked_teleporter_dest(&end, x, y) && dir == dir_opposite(end.side)) {
        if (!creature_is_native_to_map(g, end.map))
            return false;
        int saved = party.cur_map;
        dun_set_current_map(end.map);
        int blocked = dun_square_blocks_movement(dun_get_square(end.x, end.y)) ||
                      (group && creature_get_group_at((uint16_t)(end.x + dir_dx[end.target_side]),
                                                      (uint16_t)(end.y + dir_dy[end.target_side])) != THING_NONE);
        dun_set_current_map(saved);
        if (blocked)
            return false;
    }
    int16_t nx = (int16_t)(x + dir_dx[dir]), ny = (int16_t)(y + dir_dy[dir]);
    uint8_t square = dun_get_square(nx, ny);
    if (dun_square_blocks_movement(square) || square_type(square) == SQUARE_STAIRS)
        return false;
    if (group && creature_get_group_at(nx, ny) != THING_NONE)
        return false;
    if (dun_get_linked_teleporter_dest(&end, nx, ny) && !creature_is_native_to_map(g, end.map))
        return false;
    if (group)
        *group = g;
    return true;
}

/* True when the champions push the group; the push costs stamina. */
bool move_party_can_push_group(uint16_t group)
{
    int16_t resistance = creature_group_info(group)->push_resistance;
    if (resistance >= PUSH_IMMOVABLE)
        return false;
    bool pushed = false;
    for (int i = 0; i < game.champion_count; i++) {
        Champion *c = champion(i);
        if (!c->health)
            continue;
        int16_t s = (int16_t)champ_get_stat(c, STAT_STRENGTH, 0);
        if (i == game.leader)
            s = (int16_t)(s + (s >> PUSH_LEADER_SHIFT));
        if (resistance < PUSH_LIGHT)
            s = (int16_t)(s + (s >> PUSH_LIGHT_SHIFT));
        s = (int16_t)champ_scale_by_stamina(c, s);
        if (resistance <= s || !rand_below((uint16_t)(((resistance - s) >> 1) + 1))) {
            pushed = true;
            break;
        }
    }
    for (int i = 0; i < game.champion_count; i++) {
        Champion *c = champion(i);
        if (c->health && c->max_stamina >> PUSH_STAMINA_SHIFT < c->stamina) {
            int16_t cost = PUSH_FAIL_STAMINA;
            if (pushed && resistance >> PUSH_COST_SHIFT > PUSH_FAIL_STAMINA)
                cost = (int16_t)(resistance >> PUSH_COST_SHIFT);
            champ_decrease_stamina(i, cost);
        }
    }
    return pushed;
}

void creature_push_group(int x, int y, int dir, int strength)
{
    uint16_t g;
    if (creature_can_be_pushed(x, y, dir, &g)) {
        int16_t resistance = creature_group_info(g)->push_resistance;
        int stop = 0;
        if ((int16_t)strength < PUSH_IMMOVABLE && (int16_t)strength < resistance)
            stop = rand_below((uint16_t)(((resistance - (int16_t)strength) >> PUSH_STOP_SHIFT) + 1)) != 0;
        if (!stop) {
            int16_t nx = (int16_t)(x + dir_dx[dir]), ny = (int16_t)(y + dir_dy[dir]);
            uint16_t flags = creature_group_info(g)->flags;
            if (party.cur_map == party.square_map && nx == party.square_x && ny == party.square_y &&
                !(flags & CREATURE_NOT_SOLID)) {
                int16_t first, second;
                move_party_bump_damage(x, y, &first, &second, game.party_dir, dir_opposite(dir));
            } else {
                ai_free_cached_data(g);
                ActiveGroup *a = creature_active(g);
                if (a && (int8_t)a->action != -1 && (int8_t)a->action != CREATURE_ACTION_VANISH &&
                    ai_action_flag((int8_t)a->action) & ACTION_FLAG_PATH) {
                    creature_cancel_update(g);
                    schedule_update(x, y);
                }
                move_thing(g, x, y, nx, ny);
                if (creature_get_group_at(nx, ny) != THING_NONE && !(flags & CREATURE_STATIC))
                    creature_alert(THING_NONE, (Location){nx, ny}, ALERT_PUSHED, ALERT_PUSHED_CHANCE, 0);
            }
        }
    }
    if (party.cur_map != party.square_map || x != party.square_x || y != party.square_y)
        return;
    int16_t load = 0;
    uint16_t alive = 0;
    for (int i = 0; i < CHAMPION_MAX; i++) {
        Champion *c = champion(i);
        if (!c->health)
            continue;
        sound_request_with_fallback(CAT_CHAMPION, c->portrait, SOUND_ENTRY_PUSHED, SOUND_FALLBACK, x, y, 1,
                                    SOUND_PRIORITY_PUSHED, SOUND_VOLUME_LOUD);
        load = (int16_t)(load + (uint16_t)champ_get_load(i) / LOAD_PUSH_DIVISOR);
        alive++;
    }
    if (!alive)
        return;
    load = (int16_t)(load / (int16_t)alive + PUSH_BASE_STRENGTH);
    if ((int16_t)strength >= PUSH_STRENGTH_PARTY || load < (int16_t)strength)
        move_party(dir_relative(dir, game.party_dir) + CMD_MOVE_FORWARD);
}

/* The party pushes the creature in front by a push arrow. */
void move_push_creature(int arrow)
{
    static const int8_t turns[PUSH_ARROW_COUNT] = {0, CMD_TURN_RIGHT, CMD_TURN_LEFT, 0, CMD_TURN_RIGHT, CMD_TURN_LEFT};
    static const int8_t commands[PUSH_ARROW_COUNT] = {CMD_MOVE_FORWARD, CMD_MOVE_FORWARD, CMD_MOVE_RIGHT,
                                                      CMD_MOVE_BACK,    CMD_MOVE_LEFT,    CMD_MOVE_FORWARD};
    static const int8_t dirs[PUSH_ARROW_COUNT] = {0, 1, 2, 2, 2, -1};
    if (arrow < 0 || arrow >= PUSH_ARROW_COUNT)
        return;
    int dir = dir_turn(game.party_dir, dirs[arrow]);
    int16_t x = (int16_t)(game.party_x + dir_dx[game.party_dir]), y = (int16_t)(game.party_y + dir_dy[game.party_dir]);
    uint16_t g;
    if (!creature_can_be_pushed(x, y, dir, &g))
        return;
    if (!move_party_can_push_group(g)) {
        for (int i = 0; i < game.champion_count; i++)
            if (champion(i)->health && (i == game.leader || !rand_2bits()))
                sound_request_with_fallback(CAT_CHAMPION, i, SOUND_HURT, SOUND_FALLBACK, game.party_x, game.party_y, 1,
                                            SOUND_PRIORITY_HURT, SOUND_VOLUME_LOUD);
        return;
    }
    push.x = x;
    push.party_x = (int16_t)game.party_x;
    push.party_y = (int16_t)game.party_y;
    push.map = (int16_t)game.party_map;
    party.push_pending_group = g;
    push.turn = turns[arrow];
    push.y = y;
    push.dir = (int16_t)dir;
    move_party(commands[arrow]);
}

void creature_continue_push(int command)
{
    if (game.party_dir != party.view_dir)
        return;
    if (game.party_map != push.map || game.party_x != push.party_x || game.party_y != push.party_y ||
        creature_get_group_at(push.x, push.y) != party.push_pending_group)
        return;
    party.push_pending_group = THING_NONE;
    int map = game.party_map;
    if (command == CMD_MOVE_FORWARD)
        creature_push_group(push.x, push.y, push.dir, PUSH_STRENGTH_PARTY);
    if (move_party(command)) {
        move_party_turn(push.turn);
        if (command != CMD_MOVE_FORWARD) {
            dun_set_current_map(map);
            creature_push_group(push.x, push.y, push.dir, PUSH_STRENGTH_PARTY);
        }
    }
    dun_set_current_map(game.party_map);
}

/* Creature sensors. */

void sensor_turn_creatures(const TimelineEvent *e, uint16_t sensor)
{
    /* an inverted sensor turns the creatures on effect 1 instead of 0 */
    if (e->data[EVENT_EFFECT] != (sensor_flag(sensor, SENSOR_INVERTED) ? 1 : 0))
        return;
    uint16_t g = creature_get_group_at(sensor_target_x(sensor), sensor_target_y(sensor));
    if (g != THING_NONE)
        creature_turn_group(g, 1, sensor_turn_dir(sensor));
}

void sensor_affect_creatures_in_area(uint16_t sensor, int x, int y, int effect)
{
    int power = sensor_area_power(sensor), creature_type = sensor_area_creature(sensor);
    int tx = sensor_target_x(sensor), ty = sensor_target_y(sensor), kind = sensor_type(sensor);
    int rx = abs((int16_t)(x - tx)), ry = abs((int16_t)(y - ty));
    int left = x - rx, top = y - ry;
    for (int row = ry * 2; row >= 0; row--)
        for (int col = rx * 2; col >= 0; col--) {
            int16_t sx = (int16_t)(left + col), sy = (int16_t)(top + row);
            if (sx < 0 || sx >= dungeon_map_width(party.cur_map) || sy < 0 || sy >= dungeon_map_height(party.cur_map))
                continue;
            uint16_t g = creature_get_group_at(sx, sy);
            if (g == THING_NONE || thing_word(g, GROUP_ITEM_CLASS) != (uint16_t)creature_type)
                continue;
            if (kind == SENSOR_VANISH) {
                if ((uint16_t)power > 1) {
                    if ((uint16_t)power != VANISH_POWER)
                        return;
                    creature_set_next_action(g, sx, sy, CREATURE_ACTION_VANISH, 1);
                }
            } else if (kind == SENSOR_ALERT) {
                creature_alert(g, (Location){sx, sy}, power | (effect ? ALERT_CLEAR : 0), ALERT_ALWAYS, 0);
            }
        }
}
