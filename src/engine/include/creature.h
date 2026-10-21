/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_CREATURE_H
#define DM2_ENGINE_CREATURE_H
/* Prefixes: creature_ */

/* Creature groups: the creature info records and the update of the groups. */

#include "gdat.h"
#include "dungeon.h"
#include "timeline.h"
#include <stdbool.h>
#include <stdint.h>

/* A creature info record of SKULL.EXE (36 bytes, the same layout). */
typedef struct {
    uint16_t flags;      /* CREATURE_STATIC, CREATURE_FACES_PARTY, ... */
    uint8_t defense;     /* against hits; 0xff: the creature cannot be hurt */
    int8_t regeneration; /* game steps / 4 per health point; negative: the creature loses health */
    uint16_t max_health;
    uint8_t attack; /* the power of its attacks and missiles */
    uint8_t poison;
    uint8_t dexterity;        /* hit chance; 0xff: other creatures cannot hit it */
    uint8_t traits;           /* CREATURE_TRAIT_*; bits 0-1 and 2-3: random picture shift */
    uint16_t movement;        /* what the creature may walk through: CREATURE_FLIES, AI_MOVE_* */
    uint16_t item_skills;     /* how the creature handles items: AI_ITEM_* */
    uint16_t attack_skills;   /* AI_ATTACK_* */
    uint16_t door_skills;     /* AI_DOOR_* */
    uint16_t actuator_skills; /* how the creature uses wall actuators */
    uint16_t unknown_14;
    uint16_t speed; /* bits 0-3 think speed, 4-7 fear resistance, 8-11 experience, 12-15 turn chance */
    uint8_t unknown_18;
    uint8_t door_flags; /* bits 0-3 armor, CREATURE_RESISTS_NON_FIRE */
    uint8_t unknown_1a, unknown_1b, unknown_1c;
    uint8_t push_resistance;  /* against the party strength; 0xfe and 0xff: the creature cannot be pushed */
    uint16_t possessions;     /* how a static creature shows the items it holds */
    uint16_t possession_zone; /* the view zone of the possessions that the creature hides */
    uint8_t size;             /* 1 to 3: how far the creature stands from the center of its cell toward the party */
    uint8_t hit_size;         /* row of the hit radius table; also the size of its death explosion */
} CreatureInfo;

#define CREATURE_INFO_SIZE 36
_Static_assert(sizeof(CreatureInfo) == CREATURE_INFO_SIZE, "the creature info records of SKULL.EXE have 36 bytes");

#define CREATURE_STATIC           0x0001 /* the creature does not act: no active group, animation state in its record */
#define CREATURE_FACES_PARTY      0x0004 /* the creature always shows its front */
#define CREATURE_ATTACKS_ALL      0x0008 /* a melee attack hits several champions */
#define CREATURE_ATTACKS_RANDOM   0x0010 /* ... random champions */
#define CREATURE_NOT_DOOR_HIT     0x0020 /* a closing door does not hit the creature */
#define CREATURE_DOOR_SIZE_SHIFT  6      /* bits 6-7: the door height that the creature blocks */
#define CREATURE_DOOR_SIZE_MASK   3
#define CREATURE_WANDERER         0x4000 /* the creature may enter any map */
#define CREATURE_NOT_SOLID        0x8000 /* the party and objects pass the creature */
#define CREATURE_RESISTS_NON_FIRE 0x10   /* door_flags: doors, melee hits, and spells that are not fire hurt it less */
#define CREATURE_POSSESSIONS_ON_FLOOR 0x0800 /* possessions bit: a static creature holds what lies on its square */
#define CREATURE_TRAIT_SHOT_HURTS     0x10   /* a missile that it shoots hurts the creature */
#define CREATURE_TRAIT_TWO_CHAMPIONS  0x20   /* a melee attack hits two champions */
#define CREATURE_TRAIT_CLOSES_PITS    0x40   /* the creature closes the pit under it while it walks */
#define PUSH_IMMOVABLE                0xfe
#define PUSH_WALL                     0xff /* push resistance: the party bumps into the creature as into a wall */
#define PUSH_STRENGTH_PARTY           0xfe
#define CREATURE_FLIES                0x0004
#define CREATURE_SEES_INVISIBLE       0x0400 /* flags: the party spell of invisibility does not hide the party */
#define CREATURE_SEES_IN_DARK         0x0800 /* flags: the light does not limit its sight */
#define CREATURE_FAST_VANISH          0x1000 /* flags: the creature vanishes quickly in a party action effect */
/* Animation step (4 bytes): the bytes of a step. */
enum anim_step_field { STEP_SOUND = 0, STEP_NEXT = 1, STEP_JUMP = 2, STEP_TIME = 3 };
#define ANIM_JITTER                 7     /* anim bits: a random picture shift, sideways (bits 0-2) and forward */
#define TIME_BYTE_WRAP              0x100 /* the time bytes of an active group are the low byte of the game time */
/* Possessions bit: the objects under the creature stay clickable, and a drop goes into the front cells. */
#define POSSESSIONS_FLOOR_CLICKABLE 0x2000
/* the display kind (0: none shown; 1 or less: carried low); a static creature: the possession cell count */
#define POSSESSIONS_KIND_MASK       0x000f
#define STATIC_POSSESSIONS_SHOWN    0x400
#define STATIC_POSSESSIONS_ALL      0x200
#define CELLS_ALL                   0x0f /* a cell mask: all four cells */
#define CREATURE_INFO_COUNT         63

/* The nibbles of CreatureInfo.speed and the armor in door_flags. */
#define INFO_NIBBLE_MASK       0x0f
#define SPEED_FEAR_SHIFT       4
#define SPEED_EXPERIENCE_SHIFT 8
#define SPEED_TURN_SHIFT       12
#define ARMOR_IMMUNE           INFO_NIBBLE_MASK /* the armor of a creature that takes no damage */
static inline uint16_t creature_think_speed(const CreatureInfo *info)
{
    return info->speed & INFO_NIBBLE_MASK;
}
/* The resistance to fear, 15 immune; it also sets how often boredom ends. */
static inline uint16_t creature_fear_resistance(const CreatureInfo *info)
{
    return info->speed >> SPEED_FEAR_SHIFT & INFO_NIBBLE_MASK;
}
/* The experience factor of a hit on the creature. */
static inline uint16_t creature_experience(const CreatureInfo *info)
{
    return info->speed >> SPEED_EXPERIENCE_SHIFT & INFO_NIBBLE_MASK;
}
static inline uint16_t creature_turn_chance(const CreatureInfo *info)
{
    return info->speed >> SPEED_TURN_SHIFT;
}
static inline uint16_t creature_armor(const CreatureInfo *info)
{
    return info->door_flags & INFO_NIBBLE_MASK;
}
/* The door height that the creature blocks: a closing door above it does not hit the creature. */
static inline uint16_t creature_door_size(const CreatureInfo *info)
{
    return info->flags >> CREATURE_DOOR_SIZE_SHIFT & CREATURE_DOOR_SIZE_MASK;
}
/* The sound of an item that a creature drops. */
#define SOUND_ENTRY_DROP     0x85
#define SOUND_PRIORITY_DROP  0x3a
/* The sound of a creature that a teleporter moves or that a generator creates. */
#define SOUND_ENTRY_TELEPORT 0x89
#define SOUND_PRIORITY_MOVE  0x61

/* An animation frame (GDAT (0x0f, creature type, 7, 0xfd), 8 bytes per frame). */
enum creature_frame_field {
    FRAME_ENTRY = 0,  /* 4 bytes: picture entry by the side the creature shows (front, right, back, left) */
    FRAME_CELL = 4,   /* cell in the 5 x 5 grid of its square */
    FRAME_SIZE = 5,   /* row of the size table (0x0f, creature type, 7, 0xfe) */
    FRAME_SHIFT = 6,  /* sideways and forward shift of the picture */
    FRAME_MIRROR = 7, /* 2 bits per side: bit 0 mirror, bit 1 mirror on an animation bit */
};
#define CREATURE_FRAME_SIZE 8
#define CREATURE_NO_HURT    0xff /* defense, dexterity */
#define CREATURE_ATTR_CLASS 1    /* GDAT value key: the AI class */
#define CREATURE_ATTR_7     7

/* An active creature group: the state of a creature that acts (34 bytes, the layout of SKULL.EXE). */
typedef struct {
    uint16_t group;           /* record index of the group, ACTIVE_FREE for a free entry */
    int16_t event;            /* the timeline event of its next update, -1 none */
    uint8_t last_action_time; /* low byte of the game time of its last attack or move */
    uint8_t unused_5;
    uint8_t regeneration_time; /* low byte of game time / 4 of the last health change */
    uint8_t anim_bits;         /* ANIM_BIT_*: random shift and mirror of the picture */
    uint16_t anim_sequence;    /* first step of the animation sequence */
    uint16_t anim_state;       /* step in the sequence */
    uint16_t position;         /* where it was activated: x bits 0-4, y bits 5-9, map bits 10-15 */
    int16_t counters[2];       /* plan variables */
    int8_t plan;               /* the plan that it follows, -1 none */
    int8_t plan_step;
    uint16_t damage;      /* damage taken since its last update */
    int8_t behavior;      /* index of its mood entry */
    int8_t next_action;   /* the action that its next update starts, -1 none */
    uint16_t destination; /* x bits 0-4, y bits 5-9, map bits 10-15; bits 8-15 also an action direction */
    uint8_t action;       /* AI_ACTION_* */
    uint8_t direction;    /* the direction of the action */
    uint8_t cell;         /* the cell of the action */
    uint8_t facing;       /* the direction the action turns to */
    int8_t sub;           /* the item filter or sub-command of the action */
    uint8_t phase;        /* the phase of a multi-step action */
    uint8_t mode;         /* the mode of the action */
    uint8_t interrupt;    /* the action stops its animation */
} ActiveGroup;

#define ACTIVE_GROUP_SIZE 34
_Static_assert(sizeof(ActiveGroup) == ACTIVE_GROUP_SIZE, "active groups have 34 bytes");

#define ACTIVE_FREE        0xffff
#define GROUP_NOT_ACTIVE   0xff
#define ACTIVE_ACTION_NONE 0xff /* an active group without a chosen action */
#define EVENT_NO_SLOT      (-1)

extern ActiveGroup *active_groups;
extern int active_group_max, active_group_count;

/* The info record of a creature type, read through GDAT (0x0f, type, 0x0b, 5). */
const CreatureInfo *creature_info(const gdat_t *g, int creature_type);
/* The info record at index; NULL past the table. */
const CreatureInfo *creature_info_at(int index);
/* The info record of the creature of a group thing. */
const CreatureInfo *creature_group_info(uint16_t group);
/* A GDAT value (0x0f, creature type, 0x0b, attr) of a creature type. */
int creature_gdat_attr(int creature_type, int attr);
/* True when the creature of group may stay on map: a wanderer, or a creature type of the map. */
bool creature_is_native_to_map(uint16_t group, int map);
/* The cells (bits 0 to 3) in which a static creature shows its possessions, from its possessions word. */
int creature_cell_mask(uint16_t possessions);
/* True for a creature that flies over pits and does not press floor sensors. */
bool creature_is_flying(uint16_t group);
/* The first creature group on the square (x, y) of map, THING_NONE for none. */
uint16_t creature_group_at(int map, int x, int y);
/* The creature group on (x, y) of the current map, THING_NONE for none. */
uint16_t creature_get_group_at(int x, int y);
/* The active group of a creature group, NULL when it has none. */
ActiveGroup *creature_active(uint16_t group);

/* The current animation frame of a creature group. */
void creature_anim_frame(const gdat_t *g, uint16_t group, uint8_t frame[CREATURE_FRAME_SIZE]);
/* The frame of an animation state of a creature type. */
void creature_anim_frame_of(const gdat_t *g, int creature_type, uint16_t sequence, uint16_t state,
                            uint8_t frame[CREATURE_FRAME_SIZE]);
#define CREATURE_ACTION_VANISH 0x13
/* The animation bits (random shift and mirror) of a creature group, 0 for a static creature. */
int creature_anim_bits(uint16_t group);
/* The animation sequence and state words of a group record: in the record of a static creature, else in its active group. */
uint16_t *creature_anim_words(uint16_t group);
/* Starts the animation sequence of action: *sequence and *state, *frame the first step; returns 1 when the step
   has a duration. start: the first frame of a looping sequence of a static creature. */
int creature_anim_start(int creature_type, int action, uint16_t *sequence, uint16_t *state, const uint8_t **frame,
                        uint16_t start);
int creature_anim_next_random_step(int creature_type, uint16_t sequence, uint16_t *step, const uint8_t **frame);
/* The results of creature_anim_next_step: more steps follow, or the sequence ended. */
enum { ANIM_MORE_STEPS = 1, ANIM_SEQUENCE_END = 2 };
int creature_anim_next_step(int creature_type, uint16_t sequence, uint16_t *step, const uint8_t **frame);
/* True when the current animation step of a group is a step at which the creature acts. */
int creature_current_step_acts(uint16_t group);
/* The time of the next update of the group in the AI context, with the animation bits of its step. */
uint32_t creature_ai_animate_and_get_next_time(void);

/* Active groups. */
void creature_alloc_active_groups(void);
/* Drops the active groups of the last game (a load). */
void creature_free_active_groups(void);
void creature_reset_active_groups(void);
void creature_activate_groups_on_map(void);
void creature_activate_groups_all_maps(void);
void creature_deactivate_all_groups(void);
void creature_activate_group(uint16_t group, int x, int y);
void creature_deactivate_group(int active_index);
void creature_schedule_update(int x, int y);
void creature_cancel_update(uint16_t group);
void creature_set_next_action(uint16_t group, int x, int y, int action, int now);
int creature_active_group_event(int active_index);
int creature_active_group_floats(int active_index);
void creature_process_event(int x, int y, int type);
void creature_update_anim_at(int x, int y, int alt);
void creature_refresh_party_maps(void);

/* Group life. flags of an alert: bits 0-3 a mood bit that the alert sets (or clears, ALERT_CLEAR) with chance
   percent, 0x2000 loud (wakes the creature), 0x4000 may make it angry; damage adds to its damage. */
/* Creature types of minions, by the command that makes them. */
#define MINION_ATTACK  0x30
#define MINION_SCOUT   0x31
#define MINION_GUARD   0x32
#define MINION_FETCH   0x33
#define MINION_CARRIER 0x34
#define MINION_ARMY    0x35
#define ALERT_ALWAYS   100 /* an alert chance in percent */
void creature_alert(uint16_t group, int x, int y, int flags, int chance, int damage);
#define ALERT_ANGER      0x4000 /* alert flags: the creature may become angry */
#define ALERT_LOUD       0x2000 /* the alert wakes the creature */
#define ALERT_CLEAR      0x8000 /* the alert clears the mood bit (bits 0-3) instead of setting it */
#define ALERT_MOOD_QUICK 6      /* alert flags: mood bit 6, MOOD_QUICK */
#define ALERT_STARTLED   (ALERT_LOUD | ALERT_MOOD_QUICK) /* a loud alert that makes the creature hurry */
void creature_turn_group(uint16_t group, int absolute, int dir);
/* What a creature that leaves the dungeon drops: its drop items and possessions, its possessions, or nothing. */
enum drop_mode { DROP_ALL, DROP_POSSESSIONS, DROP_NOTHING };
void creature_drop_possessions(uint16_t group, int x, int y, int mode, int sound_map);
void creature_delete_group_at(int x, int y, int mode, int sound_map);
#define GROUP_COUNT_FULL_HEALTH 7 /* creature_create_group count: the group starts with full health */
uint16_t creature_create_group(int creature_type, int count, int dir, int x, int y);

/* Pushing creatures: the push arrows of the party and the pushes of creatures. */
bool creature_can_be_pushed(int x, int y, int dir, uint16_t *group);
void creature_push_group(int x, int y, int dir, int strength);
/* The party walks on after a push that waited for its slow move: the push ends now. */
void creature_continue_push(int command);

/* Creature updates and damage (creature.c). */
int creature_get_party_distance(int x, int y);
bool creature_is_near_party_front(int x, int y, int map);
int creature_attack_group_at(int x, int y);
uint16_t creature_summon_group(int creature_type, int level, int dir, int x, int y, int map, int link, int search_dir);
/* An extra damage amount (poison) against the armor of the creature, with a random part; 0 for no amount. */
int creature_roll_armor_factor(uint16_t group, int amount);
void creature_apply_damage_kind(uint16_t group, int x, int y, int kind);
int creature_group_jumps(uint16_t group);
int creature_damage_current_group(uint16_t damage);

/* Creature groups of move.c. */
/* The creature group whose body covers the point of cell (0xff: any cell) of the square (*x, *y) of the current
   map or of a square next to it; moves *x, *y to that square. THING_NONE when there is none. */
uint16_t creature_find_group_near_point(int16_t *x, int16_t *y, int cell);

/* Creature attacks (projectile.c). */
/* The 4 cells of the party that a creature on (x, y) attacks first to last; cell 0xff: random order. */
void creature_get_ordered_attack_cells(uint8_t order[4], int x, int y, int party_x, int party_y, int cell);
/* A melee attack of the creature of group on a champion; returns the damage that the champion takes. */
int creature_attack_champion(uint16_t group, int champ);

#endif /* DM2_ENGINE_CREATURE_H */
