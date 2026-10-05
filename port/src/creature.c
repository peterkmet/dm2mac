/* Creature groups: creature info, animation, active groups, and the creature update events. */
#include <stdlib.h>
#include <string.h>
#include "ai.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "draw.h"
#include "hooks.h"
#include "move.h"
#include "random.h"
#include "save.h"
#include "sensor.h"

#define CAT_CREATURE 0x0f
#define TYPE_DATA 7                 /* GDAT key type of raw data */
#define TYPE_TABLE 8                /* GDAT key type of a word table */
#define CREATURE_KEY_INFO 5         /* value key: index of the creature info record */
#define CREATURE_KEY_CLASS 1        /* value key: the AI class */
#define CREATURE_FRAMES_ENTRY 0xfd  /* data: the animation frames */
#define CREATURE_STEPS_ENTRY 0xfc   /* data: the animation steps (4 bytes each) */
#define CREATURE_SEQUENCES_ENTRY 0xfb /* table: (action, first step) pairs, ended by -1 */

/* Animation state word. */
#define ANIM_STILL 0x4000           /* the creature shows the first frame of its sequence */
#define ANIM_LOOP 0x8000            /* the sequence repeats with the game time */
#define ANIM_LOOP_ALT 0x2000        /* the sequence repeats until its end */
#define ANIM_RESTART 0x1000         /* a looping sequence starts again from frame 0 */
#define ANIM_START_MASK 0x0fc0      /* first frame of a looping sequence */
#define ANIM_START_SHIFT 6
#define ANIM_RESTART_CLEAR 0xe03f   /* clears ANIM_RESTART and the first frame */
#define ANIM_FRAME_MASK 0x3f        /* frame of a still sequence, length of a looping one */
#define ANIM_KIND_MASK 0xe03f
#define ANIM_SINGLE_LOOP 0x8001     /* a looping sequence of one frame */
#define ANIM_STEP_NONE 0xffff       /* no step of the sequence shown yet */

/* Animation step (4 bytes). */
enum anim_step_field { STEP_SOUND = 0, STEP_NEXT = 1, STEP_JUMP = 2, STEP_TIME = 3 };
#define STEP_SOUND_MASK 0x7f
#define STEP_NO_SOUND 0x7f
#define STEP_NEXT_SHIFT 4           /* bits 4-7: steps to the next step, 0 for the last */
#define STEP_CHANCE_MASK 0x0f       /* chance of 16 to stay on the step; 15: always */
#define STEP_JUMP_MASK 0x3f
#define STEP_JITTER 0x01            /* time byte: a random picture shift */
#define STEP_MIRROR 0x02            /* time byte: a random mirror */
#define STEP_RANDOM_TIME_SHIFT 2    /* bits 2-3: random extra game steps */
#define STEP_TIME_SHIFT 4           /* bits 4-7: game steps */

#define ANIM_BIT_MIRROR 0x40
#define ANIM_BIT_KEEP 0xc0
#define ANIM_JITTER 7

#define GROUP_MOOD_ACTIVE 0x80      /* group record byte +0x0b */
#define GROUP_MOOD_QUICK 0x0040     /* mood flags: the creature hurries */
#define GROUP_MOOD_SLOW 0x0002
#define GROUP_MOOD_FEAR 0x0008
#define GROUP_MOOD_HURRY 0x4000
#define GROUP_MOOD_FAR 0x8000
#define GROUP_DONT_DISCARD 0x0400   /* GROUP_DIR word: the record stays when the dungeon needs records */
#define GROUP_ACTIVATING 0x0400

#define AI_CLASS_STAYS_FAR 0x10000  /* class flag: a creature on another map than the party keeps its pace */
#define AI_ACTION_VANISH 0x13
#define AI_ACTION_IDLE 0x11
#define AI_ACTION_ATTACK_2 0x24
#define AI_ACTION_STAND_1 0x23
#define AI_ACTION_STAND_2 0x25
#define AI_ACTION_FLAG_INTERRUPT 0x10
#define CREATURE_DEATH_EXPLOSION 0xffa8
#define EXPLOSION_CENTER 0xff
#define SOUND_PRIORITY_STEP 0x46
#define SOUND_VOLUME 0x80
#define ACTIVE_ACTION_NONE 0xff
#define AI_ACTION_JUMP 5
#define GROUP_HEALTH 6              /* group record word: health */
#define GROUP_ITEM_CLASS 8          /* group record word: the item class of a creature that acts, -1 while it is created */
#define CREATURE_FAST_VANISH 0x1000 /* flags: the creature vanishes quickly in a party action effect */

ActiveGroup *active_groups;
int active_group_max, active_group_count;

/* The creature info records of SKULL.EXE. */
static const CreatureInfo creature_infos[CREATURE_INFO_COUNT] = {
    {0x45, 0xff, 0x0, 0x2bc, 0x0, 0x0, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x100, 0x1f, 0xf, 0x0, 0xf0, 0x0, 0xff, 0x4000, 0x0, 0x0, 0x2},
    {0x85, 0xff, 0x0, 0x1c2, 0x0, 0x0, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x100, 0x1f, 0xf, 0x0, 0xf0, 0x0, 0xff, 0xfd22, 0x1068, 0x0, 0x2},
    {0x85, 0xff, 0x0, 0x64, 0x0, 0x0, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x100, 0xff, 0xf, 0x0, 0xf0, 0x0, 0xff, 0x9e22, 0x1014, 0x0, 0x2},
    {0x85, 0x64, 0x0, 0x78, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x100, 0x1f, 0xd, 0x0, 0xf0, 0x0, 0xff, 0x2000, 0x0, 0x0, 0x1},
    {0x45, 0xff, 0x0, 0x64, 0x0, 0x0, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x100, 0xff, 0xf, 0x0, 0xf0, 0x0, 0xff, 0x4000, 0x0, 0x0, 0x1},
    {0x45, 0xff, 0x0, 0x64, 0x0, 0x0, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x100, 0xff, 0xf, 0x0, 0xf0, 0x0, 0xff, 0x4000, 0x0, 0x0, 0x1},
    {0x45, 0xff, 0x0, 0x64, 0x0, 0x0, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x100, 0xff, 0xf, 0x0, 0xf0, 0x0, 0x5f, 0x4000, 0x0, 0x2, 0x2},
    {0x85, 0xff, 0x0, 0x64, 0x0, 0x0, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x100, 0xff, 0xf, 0x0, 0xf0, 0x0, 0xff, 0xa01, 0xfd4, 0x0, 0x2},
    {0x41, 0xff, 0x0, 0x64, 0x0, 0x0, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x100, 0xff, 0xf, 0x0, 0xf0, 0x0, 0xff, 0x4000, 0x0, 0x0, 0x2},
    {0x81, 0x50, 0x0, 0xa0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x100, 0x1f, 0xf, 0x0, 0xf0, 0x0, 0x2d, 0xac42, 0xfa0, 0x0, 0x2},
    {0x85, 0xff, 0x0, 0x64, 0x0, 0x0, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x100, 0xff, 0xf, 0x0, 0xf0, 0x0, 0xff, 0xb01, 0x1004, 0x0, 0x2},
    {0x45, 0xff, 0x0, 0x64, 0x0, 0x0, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x100, 0xff, 0xf, 0x0, 0xf0, 0x0, 0x28, 0x4000, 0x0, 0x0, 0x0},
    {0x81, 0xff, 0x0, 0x190, 0x0, 0x0, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x100, 0xff, 0xf, 0x0, 0xf0, 0x0, 0xff, 0xed41, 0x109f, 0x0, 0x0},
    {0xcc40, 0x82, 0x8, 0xc8, 0x0, 0x0, 0x64, 0x1c, 0x6d9e, 0x0, 0x40, 0x21, 0x0, 0x20ff, 0x1f0, 0x5f, 0xc, 0xa0, 0xfc, 0x4, 0xa, 0x0, 0x0, 0x0, 0x1},
    {0xc840, 0x96, -1, 0x64, 0x28, 0x0, 0x8c, 0x1c, 0x6d9e, 0x0, 0x40, 0x1, 0x0, 0x20f8, 0x1de, 0xcc, 0xc, 0x50, 0xfc, 0x4, 0xa, 0x0, 0x0, 0x0, 0x1},
    {0xc840, 0x82, 0x32, 0x96, 0x0, 0x0, 0x28, 0xc, 0x6d9e, 0x3, 0x0, 0x21, 0x0, 0x20f7, 0x1c0, 0x55, 0xc, 0x50, 0xfc, 0x4, 0xa, 0x0, 0x0, 0x0, 0x1},
    {0xc840, 0x82, 0x32, 0x96, 0x0, 0x0, 0x28, 0xc, 0x6d9e, 0x3, 0x0, 0x21, 0x0, 0x20f7, 0x1c0, 0x55, 0xc, 0x50, 0xfc, 0x4, 0xa, 0x0, 0x0, 0x0, 0x1},
    {0xc840, 0xc8, -33, 0xc8, 0x3c, 0x0, 0x14, 0x1c, 0x2c06, 0x0, 0x40, 0x0, 0x0, 0x20f2, 0x1fe, 0xdd, 0xc, 0x50, 0xfc, 0x4, 0xff, 0x0, 0x0, 0x0, 0x1},
    {0xc840, 0x6e, -16, 0x96, 0x0, 0x0, 0xa, 0xc, 0x6d9e, 0x3, 0x0, 0x21, 0x0, 0x20f7, 0x1c0, 0x15, 0xc, 0x50, 0xfc, 0x4, 0xa, 0x0, 0x0, 0x0, 0x1},
    {0x240, 0x55, 0x21, 0x190, 0x32, 0x0, 0x78, 0x0, 0x2, 0x0, 0x2, 0x0, 0x0, 0x1053, 0x78b, 0x76, 0x8, 0x40, 0xfd, 0x9, 0xfd, 0x0, 0x0, 0x3, 0x2},
    {0x8081, 0xff, 0x0, 0x384, 0x0, 0x0, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x100, 0xff, 0xf, 0x0, 0xf0, 0x0, 0xff, 0x6000, 0x0, 0x0, 0x1},
    {0x8ca0, 0x96, 0x8, 0x64, 0x28, 0x0, 0x64, 0x0, 0x6, 0x0, 0x40, 0x0, 0x0, 0x2012, 0x2ff, 0x81, 0xf, 0x72, 0xfd, 0x4, 0xff, 0x0, 0x0, 0x1, 0x1},
    {0x8860, 0x64, 0x21, 0x50, 0x64, 0x0, 0x96, 0x0, 0xa06, 0x0, 0x10, 0x4, 0x0, 0x4035, 0x3fd, 0xf6, 0xf, 0x62, 0xfd, 0x4, 0x14, 0x0, 0x0, 0x1, 0x1},
    {0x8840, 0x32, 0x10, 0x1e, 0xa, 0x28, 0xc8, 0xc, 0x806, 0x0, 0x1, 0x0, 0x0, 0x10f3, 0x295, 0x11, 0x3, 0x0, 0xf1, 0x4, 0xa, 0x0, 0x0, 0x1, 0x1},
    {0xac0, 0x41, 0x21, 0x24, 0x6, 0x0, 0x50, 0x0, 0x2402, 0x1, 0x1, 0x0, 0x0, 0x1034, 0x924a, 0xd7, 0x6, 0xfc, 0xff, 0x4, 0x41, 0x0, 0x0, 0x1, 0x1},
    {0xa80, 0x50, 0x10, 0x96, 0x28, 0x0, 0x50, 0x0, 0xe402, 0x3, 0xa, 0x2, 0x1, 0x307a, 0x328a, 0xcc, 0x9, 0x82, 0xfe, 0x4, 0x46, 0x0, 0x0, 0x1, 0x1},
    {0x280, 0x32, 0x8, 0x15e, 0x14, 0x0, 0xf0, 0x0, 0x64a2, 0x4f, 0x0, 0x32, 0x1, 0x20fd, 0x2df, 0xef, 0x6, 0xd3, 0xfe, 0x4, 0x28, 0x0, 0x0, 0x1, 0x0},
    {0x280, 0x46, 0x10, 0xaf, 0x23, 0x0, 0x6e, 0x0, 0x6402, 0x47, 0xd, 0x2, 0x1, 0x30a7, 0x47c, 0x55, 0x8, 0xa2, 0xfe, 0x4, 0x2d, 0x0, 0x0, 0x1, 0x1},
    {0xac0, 0x64, 0x21, 0xfa, 0x50, 0x96, 0x46, 0x0, 0x2, 0x0, 0x1, 0x0, 0x0, 0x10a2, 0x86e4, 0x72, 0x9, 0xec, 0xff, 0x4, 0x5a, 0x0, 0x0, 0x2, 0x2},
    {0xa40, 0x87, 0x32, 0x15e, 0x32, 0x0, 0x28, 0x0, 0x2, 0x0, 0x1, 0x0, 0x0, 0x10a3, 0xc7ca, 0xc, 0xe, 0x10, 0xfb, 0x4, 0xff, 0x0, 0x0, 0x0, 0x2},
    {0x1c40, 0xaf, 0x8, 0x898, 0x87, 0x0, 0xaa, 0x8, 0x41a2, 0x0, 0x5d3, 0x5f, 0x0, 0x40fd, 0x1aef, 0xdd, 0xe, 0x62, 0xfd, 0x4, 0xfd, 0x0, 0x0, 0x2, 0x1},
    {0x40, 0xdc, 0x10, 0x4b0, 0xff, 0x0, 0x78, 0x0, 0x4082, 0x1, 0x183, 0xa, 0x1, 0x4068, 0x7cb, 0x88, 0xb, 0x41, 0xfc, 0x9, 0xfe, 0x0, 0x0, 0x2, 0x2},
    {0x40, 0x78, 0x0, 0x12c, 0x0, 0x0, 0x5, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1ff, 0xff, 0xf, 0x0, 0xf8, 0x0, 0xff, 0x0, 0x0, 0x1, 0x2},
    {0x240, 0xb4, 0x8, 0x3e8, 0x82, 0x0, 0xc8, 0x0, 0x4002, 0xf, 0xb, 0x2, 0x1, 0x3057, 0x8ea, 0xdc, 0xc, 0x30, 0xfc, 0x4, 0x6e, 0x0, 0x0, 0x1, 0x1},
    {0xc840, 0x4b, -1, 0xc8, 0x2d, 0x0, 0xb4, 0x1c, 0x6d86, 0x0, 0x40, 0x21, 0x0, 0x20fa, 0x4ec, 0xdf, 0xc, 0xa0, 0xfc, 0x4, 0xa, 0x0, 0x0, 0x0, 0x1},
    {0x8840, 0x32, 0x10, 0x78, 0x3c, 0x4b, 0xc8, 0xc, 0x807, 0x0, 0x1, 0x0, 0x0, 0x10f4, 0x295, 0x11, 0x3, 0x0, 0xf0, 0x4, 0xa, 0x0, 0x0, 0x1, 0x1},
    {0x240, 0xa0, 0x10, 0x1a4, 0x64, 0x0, 0x8c, 0x0, 0x6402, 0xf, 0x9, 0x2, 0x1, 0x3078, 0x78c, 0x7c, 0x9, 0x40, 0xfc, 0x4, 0x6e, 0x0, 0x0, 0x2, 0x2},
    {0x43, 0xff, 0x0, 0x64, 0x0, 0x0, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x100, 0xff, 0xf, 0x0, 0xf0, 0x0, 0x46, 0x4000, 0x1, 0x0, 0x2},
    {0x45, 0xff, 0x0, 0x64, 0x0, 0x0, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x100, 0xff, 0xf, 0x0, 0xf0, 0x0, 0xff, 0x4100, 0x1, 0x0, 0x2},
    {0x85, 0xff, 0x0, 0x64, 0x0, 0x0, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x100, 0xff, 0xf, 0x0, 0xf0, 0x0, 0xff, 0xa01, 0xfe3, 0x0, 0x2},
    {0x140, 0xdc, 0x0, 0x1f4, 0x3c, 0x0, 0xfa, 0x20, 0x2, 0x0, 0x2, 0x0, 0x0, 0x0, 0x100, 0x4f, 0xf, 0x61, 0xfc, 0x8, 0xff, 0x0, 0x0, 0x3, 0x2},
    {0x8c78, 0x64, 0x32, 0x96, 0x2, 0x0, 0x96, 0x0, 0x6606, 0x0, 0x1, 0x0, 0x0, 0x4034, 0x65cf, 0xf6, 0xf, 0x62, 0xfd, 0x4, 0xff, 0x0, 0x0, 0x1, 0x1},
    {0x280, 0x41, 0x32, 0x50, 0xa, 0x0, 0x5a, 0x0, 0x2402, 0x0, 0x1, 0x0, 0x0, 0x1031, 0x637d, 0x1c, 0x4, 0x10, 0xfb, 0x4, 0xfe, 0x0, 0x0, 0x0, 0x2},
    {0xc840, 0x4b, 0x8, 0x96, 0x8, 0x0, 0xb4, 0x1c, 0x6d9e, 0x0, 0x40, 0x21, 0x0, 0x20ff, 0x3e9, 0x7f, 0xc, 0xa0, 0xfc, 0x4, 0xa, 0x0, 0x0, 0x0, 0x1},
    {0x240, 0x73, 0x10, 0xaf, 0x5a, 0x0, 0x64, 0x0, 0x6402, 0x4b, 0x9, 0x8, 0x1, 0x3045, 0x15ea, 0x55, 0xa, 0xa2, 0xfe, 0x4, 0xb4, 0x0, 0x0, 0x1, 0x1},
    {0x81, 0xff, 0x0, 0xbe, 0x0, 0x0, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x100, 0xff, 0xf, 0x0, 0xf0, 0x0, 0xff, 0x8c42, 0xfa0, 0x0, 0x2},
    {0x240, 0x46, 0x32, 0x64, 0x23, 0x0, 0x3c, 0x0, 0x4402, 0x4f, 0x9, 0xa, 0x1, 0x1024, 0x25e2, 0x3f, 0x1f, 0xa2, 0xfe, 0x4, 0x32, 0x0, 0x0, 0x1, 0x1},
    {0x2044, 0xff, 0x0, 0x320, 0x0, 0x0, 0xff, 0x20, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x100, 0xff, 0xf, 0x61, 0xfc, 0x8, 0xff, 0x0, 0x0, 0x0, 0x1},
    {0x290, 0x64, 0x8, 0x96, 0xf, 0x0, 0xa0, 0x0, 0x4002, 0xf, 0x5dd, 0x37, 0x1, 0x309e, 0x85e6, 0x6f, 0xc, 0x93, 0xfd, 0x4, 0x28, 0x0, 0x0, 0x1, 0x1},
    {0xc840, 0x4b, 0x0, 0x64, 0x32, 0x0, 0xb4, 0xc, 0x2c06, 0x0, 0x40, 0x21, 0x0, 0x20ff, 0x3fe, 0x7f, 0xc, 0xa0, 0xfc, 0x4, 0xa, 0x0, 0x0, 0x0, 0x1},
    {0x240, 0x73, 0x10, 0xaf, 0x3c, 0x0, 0x64, 0x8, 0x6402, 0x4f, 0x9, 0xa, 0x0, 0x3014, 0x14e8, 0x85, 0xf, 0xa2, 0xfe, 0x4, 0x37, 0x0, 0x0, 0x1, 0x1},
    {0x41, 0xff, 0x0, 0x64, 0x0, 0x0, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x100, 0xff, 0xf, 0x0, 0xf0, 0x0, 0x32, 0x4000, 0x0, 0x0, 0x1},
    {0x280, 0x5a, 0x21, 0x96, 0x1e, 0x0, 0x82, 0x0, 0xc002, 0x1, 0x1, 0x0, 0x0, 0x10c7, 0x3da, 0x72, 0x6, 0xc5, 0xfe, 0x4, 0x41, 0x0, 0x0, 0x2, 0x2},
    {0xacb8, 0xff, 0x10, 0xc8, 0x0, 0x0, 0xff, 0x40, 0xc04, 0x0, 0x0, 0x0, 0x0, 0x40ff, 0x1c7, 0xff, 0xf, 0x62, 0xfd, 0x4, 0xff, 0x0, 0x0, 0x1, 0x1},
    {0xacb8, 0xff, 0x10, 0xc8, 0x0, 0x0, 0xff, 0x0, 0x4802, 0x0, 0x0, 0x60, 0x0, 0x40ff, 0x1c7, 0xff, 0xf, 0x62, 0xfd, 0x4, 0xff, 0x0, 0x0, 0x1, 0x1},
    {0x290, 0x8c, 0x8, 0x177, 0x50, 0x0, 0xb8, 0x0, 0x4002, 0xf, 0x10, 0x37, 0x1, 0x30af, 0x55b9, 0xaf, 0xd, 0x93, 0xfd, 0x4, 0x32, 0x0, 0x0, 0x1, 0x1},
    {0x8081, 0xff, 0x0, 0x384, 0x0, 0x0, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x100, 0xff, 0xf, 0x0, 0xf0, 0x0, 0xff, 0x6000, 0x0, 0x0, 0x1},
    {0x240, 0x64, 0x10, 0xa0, 0x3c, 0x0, 0x78, 0x0, 0x6402, 0x4b, 0xd, 0x8, 0x1, 0x3075, 0x1588, 0x55, 0xa, 0xa2, 0xfe, 0x4, 0xbe, 0x0, 0x0, 0x1, 0x1},
    {0x880, 0x6e, 0x3, 0x15e, 0x0, 0x0, 0xa, 0xc, 0x2, 0x1, 0x0, 0x0, 0x0, 0x10ff, 0x3ff, 0x9c, 0x1f, 0x50, 0xfc, 0x4, 0x28, 0x0, 0x0, 0x0, 0x1},
    {0x45, 0x8c, 0x0, 0x104, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x100, 0x3f, 0xf, 0x0, 0xf0, 0x0, 0x37, 0x6000, 0x0, 0x0, 0x2},
    {0x85, 0xff, 0x0, 0x64, 0x0, 0x0, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x100, 0xff, 0xf, 0x0, 0xf0, 0x0, 0xff, 0x4000, 0x0, 0x0, 0x2},
    {0x8c78, 0x64, 0x32, 0x96, 0x28, 0x0, 0x96, 0xd, 0x6606, 0x0, 0x1, 0x0, 0x0, 0x4034, 0x44c6, 0xf6, 0xf, 0x62, 0xfd, 0x4, 0xff, 0x0, 0x0, 0x1, 0x1},
    {0xc840, 0x4b, 0x8, 0x12c, 0x2d, 0x0, 0xb4, 0x1c, 0x6d9e, 0x0, 0x40, 0x21, 0x0, 0x20ff, 0x3ec, 0x7f, 0xc, 0xa0, 0xfc, 0x4, 0xa, 0x0, 0x0, 0x0, 0x1},
};

const CreatureInfo *creature_info_at(int index)
{
    return index >= 0 && index < CREATURE_INFO_COUNT ? creature_infos + index : NULL;
}

int creature_gdat_attr(int creature_type, int attr)
{
    return gdat_entry_index(draw_gdat(), CAT_CREATURE, creature_type, GDAT_TYPE_VALUE, attr);
}

const CreatureInfo *creature_info(const gdat_t *g, int creature_type)
{
    const CreatureInfo *info = creature_info_at(gdat_entry_index(g, CAT_CREATURE, creature_type, GDAT_TYPE_VALUE,
                                                                 CREATURE_KEY_INFO));
    return info ? info : creature_infos;  /* fix: the original reads past the table */
}

const CreatureInfo *creature_group_info(uint16_t group)
{
    return creature_info(draw_gdat(), dungeon_thing_record(group)[GROUP_CREATURE]);
}

ActiveGroup *creature_active(uint16_t group)
{
    int index = dungeon_thing_record(group)[GROUP_ACTIVE];
    return index == GROUP_NOT_ACTIVE ? NULL : active_groups + index;
}

uint16_t *creature_anim_words(uint16_t group)
{
    if (creature_group_info(group)->flags & CREATURE_STATIC)
        return (uint16_t *)(dungeon_thing_record(group) + GROUP_ANIM_SEQUENCE);
    return &creature_active(group)->anim_sequence;
}

/* The raw GDAT data (0x0f, creature type, type, entry). */
static const uint8_t *creature_data(int creature_type, int type, int entry, uint32_t *size)
{
    const gdat_t *g = draw_gdat();
    uint32_t n = 0;
    const uint8_t *data = gdat_item(g, gdat_find(g, CAT_CREATURE, creature_type, type, entry), &n);
    if (size) *size = n;
    return data;
}

/* The step of an animation state relative to the start of its sequence; a restarting loop clears its restart. */
static unsigned anim_frame_offset(uint16_t *state)
{
    if (*state & ANIM_STILL) return 0;
    if (!(*state & ANIM_LOOP)) return *state & ANIM_FRAME_MASK;
    unsigned start = (*state & ANIM_START_MASK) >> ANIM_START_SHIFT;
    if (*state & ANIM_RESTART) {
        *state &= ANIM_RESTART_CLEAR;
        start = 0;
    }
    unsigned length = *state & ANIM_FRAME_MASK;
    return length ? (start + game.game_time) % length : 0;  /* fix: the original divides by 0 */
}

void creature_anim_frame(const gdat_t *g, uint16_t group, uint8_t frame[CREATURE_FRAME_SIZE])
{
    uint8_t *record = dungeon_thing_record(group);
    int type = record[GROUP_CREATURE];
    uint16_t sequence = 0, state = 0;
    uint16_t *words = NULL;
    if (creature_info(g, type)->flags & CREATURE_STATIC || record[GROUP_ACTIVE] != GROUP_NOT_ACTIVE) {
        words = creature_anim_words(group);
        sequence = words[0];
        state = words[1];
    }
    unsigned index = (uint16_t)(anim_frame_offset(&state) + sequence);
    if (words) words[1] = state;
    uint32_t size;
    const uint8_t *data = gdat_item(g, gdat_find(g, CAT_CREATURE, type, TYPE_DATA, CREATURE_FRAMES_ENTRY), &size);
    memset(frame, 0, CREATURE_FRAME_SIZE);
    if (data && (index + 1) * CREATURE_FRAME_SIZE <= size)  /* fix: the original reads past the frames */
        memcpy(frame, data + index * CREATURE_FRAME_SIZE, CREATURE_FRAME_SIZE);
}

int creature_anim_bits(uint16_t group)
{
    ActiveGroup *a = creature_active(group);
    return a && !(creature_group_info(group)->flags & CREATURE_STATIC) ? a->anim_bits : 0;
}

/* The 4-byte step of a creature animation at its current state (creature_get_anim_step). */
static const uint8_t *anim_current_step(int creature_type, uint16_t sequence, uint16_t *state)
{
    const uint8_t *steps = creature_data(creature_type, TYPE_DATA, CREATURE_STEPS_ENTRY, NULL);
    return steps + (uint16_t)(anim_frame_offset(state) + sequence) * 4;
}

/* The number of steps of the animation sequence that starts at sequence. */
static int anim_sequence_length(int creature_type, uint16_t sequence)
{
    const uint8_t *step = creature_data(creature_type, TYPE_DATA, CREATURE_STEPS_ENTRY, NULL) + sequence * 4;
    int n = 1;
    while (step[STEP_NEXT] >> STEP_NEXT_SHIFT) {
        n++;
        step += 4;
    }
    return n;
}

/* Moves *step to the next step of a sequence, through the random chances of its steps; *frame gets the step.
   Returns 1 when the step has a duration. */
int creature_anim_next_random_step(int creature_type, uint16_t sequence, uint16_t *step, const uint8_t **frame)
{
    const uint8_t *steps = creature_data(creature_type, TYPE_DATA, CREATURE_STEPS_ENTRY, NULL);
    unsigned s = *step, result = 0;
    int ended = 0;
    if (s == ANIM_STEP_NONE) {
        s = 0;
    } else {
        int next = steps[((uint16_t)(s + sequence)) * 4 + STEP_NEXT] >> STEP_NEXT_SHIFT;
        if (!next) ended = 1;
        else s += next;
    }
    if (!ended) {
        const uint8_t *p = steps + ((uint16_t)(s + sequence)) * 4;
        while (p[STEP_NEXT] >> STEP_NEXT_SHIFT) {
            unsigned chance = p[STEP_NEXT] & STEP_CHANCE_MASK;
            if (chance != STEP_CHANCE_MASK && (rand_next() & 0xf) > chance) {
                s++;
                p += 4;
                continue;
            }
            if (((p[STEP_TIME] & 0xc) >> 2) + ((p[STEP_TIME] & 0xf0) >> 4)) result = 1;
            break;
        }
    }
    *step = (uint16_t)s;
    *frame = steps + ((uint16_t)(s + sequence)) * 4;
    return result;
}

/* Moves *step by the jump of the current step; returns 2 at the end of the sequence, 1 when more steps follow. */
int creature_anim_next_step(int creature_type, uint16_t sequence, uint16_t *step, const uint8_t **frame)
{
    const uint8_t *steps = creature_data(creature_type, TYPE_DATA, CREATURE_STEPS_ENTRY, NULL);
    int result;
    if (*step == ANIM_STEP_NONE) {
        *step = 0;
        result = -1;
    } else {
        int jump = steps[((uint16_t)(*step + sequence)) * 4 + STEP_JUMP] & STEP_JUMP_MASK;
        if (!jump) result = 2;
        else {
            *step = (uint16_t)(*step + jump);
            result = -1;
        }
    }
    if (result != 2) result = steps[((uint16_t)(*step + sequence)) * 4 + STEP_NEXT] >> STEP_NEXT_SHIFT ? 1 : 0;
    *frame = steps + ((uint16_t)(*step + sequence)) * 4;
    return result;
}

int creature_anim_start(int creature_type, int action, uint16_t *sequence, uint16_t *state, const uint8_t **frame,
                        uint16_t start)
{
    const uint8_t *table = creature_data(creature_type, TYPE_TABLE, CREATURE_SEQUENCES_ENTRY, NULL);
    for (;; table += 4) {
        int16_t a = (int16_t)(table[0] | table[1] << 8);
        if (a == -1 || a == (int16_t)action) break;
    }
    uint16_t first = (uint16_t)(table[2] | table[3] << 8);
    *sequence = first;
    if (!(creature_info(draw_gdat(), creature_type)->flags & CREATURE_STATIC)) {
        *state = ANIM_STEP_NONE;
        return creature_anim_next_random_step(creature_type, first, state, frame);
    }
    uint16_t n = (uint16_t)anim_sequence_length(creature_type, first);
    *state = start ? (uint16_t)(n | (start & ANIM_FRAME_MASK) << ANIM_START_SHIFT | ANIM_LOOP)
                   : (uint16_t)(n | ANIM_LOOP | ANIM_RESTART);
    return 1;
}

/* The first animation step of the idle sequence of a static creature (creature_reset_animation). */
static void creature_reset_animation(uint16_t group)
{
    uint8_t *record = dungeon_thing_record(group);
    uint16_t *words = creature_anim_words(group);
    const uint8_t *frame;
    creature_anim_start(record[GROUP_CREATURE], AI_ACTION_IDLE, words, words + 1, &frame, thing_word(group, GROUP_HOME));
}

int creature_current_step_acts(uint16_t group)
{
    uint8_t *record = dungeon_thing_record(group);
    uint16_t *words = creature_anim_words(group);
    return anim_current_step(record[GROUP_CREATURE], words[0], words + 1)[STEP_SOUND] >> 7;
}

uint16_t creature_get_group_at(int x, int y)
{
    return creature_group_at(party.cur_map, x, y);
}

uint16_t creature_group_at(int map, int x, int y)
{
    if (x < 0 || x >= dungeon_map_width(map) || y < 0 || y >= dungeon_map_height(map)) return THING_NONE;
    for (uint16_t t = dungeon_square_first_thing(map, x, y); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        if (thing_type(t) == THING_GROUP) return t;
    return THING_NONE;
}

int creature_is_native_to_map(uint16_t group, int map)
{
    if (creature_group_info(group)->flags & CREATURE_WANDERER) return 1;
    uint8_t type = dungeon_thing_record(group)[GROUP_CREATURE];
    const uint8_t *types = dungeon_map_creature_types(map);
    for (int i = 0; i < (dungeon_map_object_counts(map) >> 4 & 0xf); i++)
        if (types[i] == type) return 1;
    return 0;
}

int creature_is_flying(uint16_t group)
{
    if (creature_group_info(group)->movement & CREATURE_FLIES) return 1;
    int active = dungeon_thing_record(group)[GROUP_ACTIVE];
    return active != GROUP_NOT_ACTIVE && creature_active_group_floats(active);
}

int creature_cell_mask(uint16_t possessions)
{
    /* by display kind; the original reads kinds 5 to 15 from the data after its table, which this keeps */
    static const uint8_t cells[16] = {0, 4, 0x0c, 0x0e, 0x0f, 0, 1, 8, 0x20, 0x40, 2, 0x80, 0, 0x12, 0, 0};
    if (!(possessions & 0x400) || possessions & 0x200) return 0x0f;
    return cells[possessions & 0x0f];
}

/* Active groups. */

int creature_active_group_event(int active_index)
{
    return active_groups[active_index].event;
}

int creature_active_group_floats(int active_index)
{
    const ActiveGroup *a = active_groups + active_index;
    return a->action == AI_ACTION_JUMP && (a->phase == 1 || a->phase == 2);
}

void creature_alloc_active_groups(void)
{
    int n = 0, count = dungeon_thing_count(THING_GROUP);
    for (int i = 0; i < count; i++) {
        uint16_t g = (uint16_t)(THING_GROUP << THING_TYPE_SHIFT | i);
        if (thing_word(g, THING_NEXT) != THING_NONE && !(creature_group_info(g)->flags & CREATURE_STATIC)) n++;
    }
    active_group_max = n + 100 < count ? n + 100 : count;
    free(active_groups);
    active_groups = calloc(active_group_max ? active_group_max : 1, sizeof *active_groups);
}

void creature_reset_active_groups(void)
{
    creature_alloc_active_groups();
    active_group_count = 0;
    for (int i = 0; i < active_group_max; i++) active_groups[i].group = ACTIVE_FREE;
    for (int i = 0; i < dungeon_thing_count(THING_GROUP); i++)
        dungeon_thing_record((uint16_t)(THING_GROUP << THING_TYPE_SHIFT | i))[GROUP_ACTIVE] = GROUP_NOT_ACTIVE;
    creature_activate_groups_all_maps();
}

void creature_activate_groups_on_map(void)
{
    int map = party.cur_map;
    for (int x = 0; x < dungeon_map_width(map); x++)
        for (int y = 0; y < dungeon_map_height(map); y++) {
            uint16_t g = creature_group_at(map, x, y);
            if (g == THING_NONE || dungeon_thing_record(g)[GROUP_ACTIVE] != GROUP_NOT_ACTIVE) continue;
            if (!(creature_group_info(g)->flags & CREATURE_STATIC)) {
                creature_activate_group(g, x, y);
                continue;
            }
            uint16_t state = thing_word(g, GROUP_ANIM_STATE);
            creature_reset_animation(g);
            uint16_t now = thing_word(g, GROUP_ANIM_STATE);
            uint16_t keep = state & 0x6000;
            now |= keep;
            if ((state & 0x803f) == ANIM_SINGLE_LOOP) now = (uint16_t)((now & 0x7fc0) | keep | ANIM_SINGLE_LOOP);
            set_thing_word(g, GROUP_ANIM_STATE, now);
        }
}

void creature_activate_groups_all_maps(void)
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
    for (int i = 0; active_group_count; i++)
        if (active_groups[i].group != ACTIVE_FREE) creature_deactivate_group(i);
}

void creature_cancel_update(uint16_t group)
{
    if (thing_type(group) != THING_GROUP) return;
    ActiveGroup *a = creature_active(group);
    if (a && a->event >= 0) {
        timeline_delete_event((uint16_t)a->event);
        a->event = EVENT_NO_SLOT;
    }
}

void creature_schedule_update(int x, int y)
{
    uint16_t group = creature_get_group_at(x, y);
    ActiveGroup *a = creature_active(group);
    if (a->event != EVENT_NO_SLOT) creature_cancel_update(group);
    TimelineEvent e = {.time_map = (game.game_time + 1) | (uint32_t)party.cur_map << 24};
    e.type = thing_word(group, GROUP_ITEM_CLASS) != THING_NONE ? EVENT_CREATURE_THINK : EVENT_CREATURE_ANIMATION;
    e.priority = dungeon_thing_record(group)[GROUP_CREATURE];
    e.data[0] = (uint8_t)x;
    e.data[1] = (uint8_t)y;
    a->event = (int16_t)timeline_add_event(&e);
}

void creature_activate_group(uint16_t group, int x, int y)
{
    uint8_t *record = dungeon_thing_record(group);
    if (record[GROUP_ACTIVE] != GROUP_NOT_ACTIVE) return;
    uint16_t dir_word = thing_word(group, GROUP_DIR);
    set_thing_word(group, GROUP_DIR, dir_word | GROUP_ACTIVATING);
    {
        for (int i = 0; i < active_group_max; i++) {
            ActiveGroup *a = active_groups + i;
            if (a->group != ACTIVE_FREE) continue;
            set_thing_word(group, GROUP_DIR, (thing_word(group, GROUP_DIR) & ~GROUP_ACTIVATING) | (dir_word & GROUP_ACTIVATING));
            active_group_count++;
            memset(a, 0, sizeof *a);
            a->group = thing_index(group);
            record[GROUP_ACTIVE] = (uint8_t)i;
            a->event = EVENT_NO_SLOT;
            a->action = ACTIVE_ACTION_NONE;
            AiContext saved;
            int restored = creature_ai_begin(group, x, y, EVENT_CREATURE_THINK, &saved);
            ai.active->plan = -1;
            ai.active->plan_step = 0;
            a->regeneration_time = (uint8_t)((game.game_time >> 2) - 1);
            a->last_action_time = (uint8_t)(game.game_time - 0x7f);
            a->position = (uint16_t)((ai.event.data[0] & 0x1f) | (ai.event.data[1] & 0x1f) << 5 | party.cur_map << 10);
            ai.cache.path_left = 0;
            a->behavior = -1;
            a->next_action = -1;
            a->anim_bits = 0;
            creature_schedule_update(x, y);
            a->action = thing_word(group, GROUP_ITEM_CLASS) == THING_NONE ? AI_ACTION_IDLE : 0;
            if (!(ai.info->flags & CREATURE_STATIC)) {
                ai.group[GROUP_FLAGS + 1] = (uint8_t)((ai.group[GROUP_FLAGS + 1] | GROUP_MOOD_ACTIVE) & ~0x40);
                creature_ai_animate_and_get_next_time();
            }
            creature_ai_end(restored, &saved);
            return;
        }
        /* no free entry: the original discards a creature group of the dungeon and tries again; the port
           leaves the group inactive (the table has room for 100 more groups than the dungeon has) */
        set_thing_word(group, GROUP_DIR, dir_word);
        return;
    }
}

void creature_deactivate_group(int active_index)
{
    if (active_index > active_group_max) return;
    ActiveGroup *a = active_groups + active_index;
    if (a->group == ACTIVE_FREE) return;
    uint16_t group = (uint16_t)(a->group | THING_GROUP << THING_TYPE_SHIFT);
    int vanish = !(creature_group_info(group)->flags & CREATURE_STATIC) && a->action == AI_ACTION_VANISH;
    int x = 0, y = 0;
    if (vanish) {
        if (a->event == EVENT_NO_SLOT) vanish = 0;
        else {
            x = timeline.events[a->event].data[0];
            y = timeline.events[a->event].data[1];
        }
    }
    a->action = 0;
    creature_cancel_update(group);
    active_group_count--;
    dungeon_thing_record(group)[GROUP_ACTIVE] = GROUP_NOT_ACTIVE;
    a->group = ACTIVE_FREE;
    if (vanish) creature_delete_group_at(x, y, 0, 1);
}

void creature_set_next_action(uint16_t group, int x, int y, int action, int now)
{
    if (group == THING_NONE && (group = creature_get_group_at(x, y)) == THING_NONE) return;
    ActiveGroup *a = creature_active(group);
    if (!a || a->next_action == AI_ACTION_VANISH || a->action == AI_ACTION_VANISH) return;
    a->next_action = (int8_t)action;
    if (!now) return;
    if (ai_action_flag(a->action) & AI_ACTION_FLAG_INTERRUPT) {
        a->interrupt = 1;
        return;
    }
    creature_cancel_update(group);
    creature_schedule_update(x, y);
}

/* A looping animation of a static creature: the next update comes when the loop reaches its first frame. */
static void creature_anim_schedule(void)
{
    uint16_t *state = ai.anim + 1;
    if ((*state & ANIM_KIND_MASK) == ANIM_SINGLE_LOOP) return;
    int length = anim_sequence_length(ai.group[GROUP_CREATURE], ai.anim[0]);
    uint16_t start = *state & ANIM_START_MASK;
    unsigned at = (start + game.game_time) % (unsigned)length;
    if (!(uint16_t)at) {
        *state = start | ANIM_SINGLE_LOOP;
        return;
    }
    *state = (uint16_t)(start | length | ANIM_LOOP | ANIM_STILL);
    creature_cancel_update(ai.group_thing);
    ai.event.time_map = (uint32_t)(length - (int16_t)at) + game.game_time | (uint32_t)ai.map << 24;
    ai.active->event = (int16_t)timeline_add_event(&ai.event);
}

/* A static creature that plays its sequence to the end: the next update comes at the end of the loop. */
static void creature_anim_schedule_alt(void)
{
    uint16_t *state = ai.anim + 1;
    if ((*state & 0xe000) == ANIM_LOOP && (*state & ANIM_FRAME_MASK) > 1) return;
    int length = anim_sequence_length(ai.group[GROUP_CREATURE], ai.anim[0]);
    uint16_t start = *state & ANIM_START_MASK;
    unsigned at = (start + game.game_time) % (unsigned)length;
    uint16_t word = (uint16_t)(length | start);
    if (!(uint16_t)at) {
        *state = word | ANIM_LOOP;
        return;
    }
    *state = word | ANIM_LOOP | ANIM_LOOP_ALT;
    creature_cancel_update(ai.group_thing);
    ai.event.time_map = (uint32_t)(length - (int16_t)at) + game.game_time | (uint32_t)ai.map << 24;
    ai.active->event = (int16_t)timeline_add_event(&ai.event);
}

void creature_update_anim_at(int x, int y, int alt)
{
    uint16_t group = creature_get_group_at(x, y);
    if (group == THING_NONE) return;
    if (creature_group_info(group)->flags & CREATURE_STATIC && dungeon_thing_record(group)[GROUP_ACTIVE] == GROUP_NOT_ACTIVE)
        creature_activate_group(group, x, y);
    AiContext saved;
    int restored = creature_ai_begin(group, x, y, EVENT_CREATURE_ANIMATION, &saved);
    if (ai.active) {
        if (!alt) creature_anim_schedule();
        else creature_anim_schedule_alt();
    }
    creature_ai_end(restored, &saved);
}

uint32_t creature_ai_animate_and_get_next_time(void)
{
    uint8_t traits = ai.info->traits;
    int type = ai.group[GROUP_CREATURE];
    uint16_t sequence = ai.anim[0], state = ai.anim[1];
    static const uint8_t no_step[4];
    int own_step = 0;
    if (!ai.anim_step) {
        uint16_t start = ai.info->flags & CREATURE_STATIC ? thing_word(ai.group_thing, GROUP_HOME) : 0;
        creature_anim_start(type, (int8_t)ai.active->action, &sequence, &state, &ai.anim_step, start);
        if (!ai.anim_step) {
            ai.anim_step = no_step;
            own_step = 1;
        }
    }
    uint8_t bits = ai.active->anim_bits;
    const uint8_t *step = ai.anim_step;
    if (step[STEP_TIME] & STEP_JITTER && ai.active->action != AI_ACTION_ATTACK_2
        && ai.active->action != AI_ACTION_STAND_1 && ai.active->action != AI_ACTION_STAND_2) {
        bits &= ANIM_BIT_KEEP;
        if (traits & 3) {
            unsigned v = rand_below(traits & 3);
            if (rand_bit()) v = -v & ANIM_JITTER;
            bits |= (uint8_t)v;
        }
        if (traits >> 2 & 3) {
            unsigned v = rand_below(traits >> 2 & 3);
            if (rand_bit()) v = -v & ANIM_JITTER;
            bits |= (uint8_t)(v << 3);
        }
    }
    if (step[STEP_TIME] & STEP_MIRROR) {
        if (rand_bit()) bits |= ANIM_BIT_MIRROR;
        else bits &= ~ANIM_BIT_MIRROR;
    }
    ai.active->anim_bits = bits;
    ai.anim[0] = sequence;
    ai.anim[1] = state;
    if ((step[STEP_SOUND] & STEP_SOUND_MASK) != STEP_NO_SOUND)
        sound_request(CAT_CREATURE, type, step[STEP_SOUND] & STEP_SOUND_MASK, SOUND_PRIORITY_STEP, SOUND_VOLUME,
                      ai.event.data[0], ai.event.data[1], 1);
    uint16_t mood = thing_word(ai.group_thing, GROUP_FLAGS);
    int16_t delay;
    int base = step[STEP_TIME] >> STEP_TIME_SHIFT;
    if (mood & GROUP_MOOD_QUICK) {
        delay = (int16_t)(base < 1 ? base : 1);
    } else {
        int r = step[STEP_TIME] >> STEP_RANDOM_TIME_SHIFT & 3;
        int16_t d = (int16_t)((r ? rand_below((uint16_t)r) : 0) + base);
        if (ai.active->action == AI_ACTION_VANISH && game.magic_light[MAGIC_ACTION_EFFECT] && !(ai.info->flags & CREATURE_FAST_VANISH)) {
            d = (int16_t)(d * 3);
        } else if (party.cur_map != ai_party.map && !(ai_class_flags[ai.class_index] & AI_CLASS_STAYS_FAR)
                   && mood & GROUP_MOOD_FAR && !(mood & GROUP_MOOD_SLOW)) {
            d = (int16_t)(d * 4 + rand_bit());
        } else if (party_sleeping) {
            d = (int16_t)(d * 2);
            if (party.cur_map != ai_party.map) d = (int16_t)(d * 2);
        } else if (mood & GROUP_MOOD_FEAR) {
            if (!(mood & GROUP_MOOD_HURRY) || d > 2) d = (int16_t)(d * 75 / 100);
            if (d < 1) d = 1;
        }
        delay = d;
    }
    if (own_step) ai.anim_step = NULL;
    return (uint32_t)(delay + (int32_t)game.game_time);
}

void creature_process_event(int x, int y, int type)
{
    uint16_t group = creature_get_group_at(x, y);
    if (group == THING_NONE) return;
    AiContext saved;
    int restored = creature_ai_begin(group, x, y, type, &saved);
    ActiveGroup *a = ai.active;
    const CreatureInfo *info = ai.info;
    a->event = EVENT_NO_SLOT;
    if (!thing_word(group, GROUP_HEALTH)) {
        a->damage = 1;
        set_thing_word(group, GROUP_HEALTH, a->damage);
    }
    int16_t damage = (int16_t)a->damage;
    a->damage = 0;
    int16_t regen = info->regeneration;
    if (regen) {
        if (regen < 0) regen = (int16_t)-regen;
        unsigned now = game.game_time >> 2 & 0xff;
        int16_t elapsed = (int16_t)((a->regeneration_time > now ? now + 0x100 : now) - a->regeneration_time);
        int16_t q = (int16_t)(elapsed / regen);
        if (q > 0) {
            if (info->regeneration < 0) damage = (int16_t)(damage + q);
            else if (thing_word(group, GROUP_HEALTH) < info->max_health)
                set_thing_word(group, GROUP_HEALTH, (uint16_t)(thing_word(group, GROUP_HEALTH) + q));
            a->regeneration_time = (uint8_t)((game.game_time >> 2) - elapsed % regen);
        }
    }
    if (damage > 0) {
        if (!(info->flags & CREATURE_STATIC)) ai.group[GROUP_FLAGS + 1] &= ~GROUP_MOOD_ACTIVE;
        if (creature_damage_current_group((uint16_t)damage)) {
            creature_ai_end(restored, &saved);
            return;
        }
    }
    if (!(info->flags & CREATURE_STATIC)) {
        creature_ai_update_group();
    } else {
        if (ai.anim[1] & ANIM_STILL) creature_anim_schedule();
        else if (ai.anim[1] & ANIM_LOOP_ALT) creature_anim_schedule_alt();
        if (a->event == EVENT_NO_SLOT) creature_deactivate_group(ai.group[GROUP_ACTIVE]);
    }
    creature_ai_end(restored, &saved);
}
