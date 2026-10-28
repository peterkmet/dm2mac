/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_CREATURE_H
#define DM2_ENGINE_CREATURE_H
/* Prefixes: creature_ */

/* Creature groups: the creature info records and the update of the groups. The AI (ai.c) chooses the actions; this
   module does not plan, and it draws nothing. */

#include <stdbool.h>
#include <stdint.h>
#include "dungeon.h"
#include "gdat.h"

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
    uint16_t sight_and_range; /* bits 0-3 sight, 12-15 attack range */
    uint16_t speed;           /* bits 0-3 think speed, 4-7 fear resistance, 8-11 experience, 12-15 turn chance */
    uint8_t dodge;            /* bits 4-7: how much explosion damage the creature dodges */
    uint8_t door_flags;       /* bits 0-3 armor, CREATURE_RESISTS_NON_FIRE */
    uint8_t wound_feet_legs;  /* chance nibbles of a melee wound: bits 0-3 feet, 4-7 legs */
    uint8_t wound_torso_head; /* ... bits 0-3 torso, 4-7 head */
    uint8_t attack_kind;      /* ATTACK_KIND_*: how the melee attack uses the dexterity */
    uint8_t push_resistance;  /* against the party strength; 0xfe and 0xff: the creature cannot be pushed */
    uint16_t possessions;     /* how a static creature shows the items it holds */
    uint16_t possession_zone; /* the view zone of the possessions that the creature hides */
    uint8_t size;             /* 1 to 3: how far the creature stands from the center of its cell toward the party */
    uint8_t hit_size;         /* row of the hit radius table; also the size of its death explosion */
} CreatureInfo;

/* The creature info table of SKULL.EXE. */
#define CREATURE_INFO_SIZE  36
#define CREATURE_INFO_COUNT 63
_Static_assert(sizeof(CreatureInfo) == CREATURE_INFO_SIZE, "the creature info records of SKULL.EXE have 36 bytes");

/* Bits of CreatureInfo.flags. */
#define CREATURE_STATIC          0x0001 /* the creature does not act: no active group, animation state in its record */
#define CREATURE_REFLECTS_SPELLS 0x0002 /* spells do not hit the creature: they turn away or fly on */
#define CREATURE_FACES_PARTY     0x0004 /* the creature always shows its front */
#define CREATURE_ATTACKS_ALL     0x0008 /* a melee attack hits several champions */
#define CREATURE_ATTACKS_RANDOM  0x0010 /* ... random champions */
/* CREATURE_NON_MATERIAL: a closing door does not hit the creature, a melee hit reaches it only when the hit pierces, a
   MELEE_MODE_REACH attack of the champions reaches only it, a missile hits it only as harm non-material, and the sight
   of creatures passes it. */
#define CREATURE_NON_MATERIAL    0x0020
#define CREATURE_ATTACKS_FIRST   0x0100 /* flags: the creature attacks before it moves */
#define CREATURE_CATCHES_OBJECTS 0x0200 /* the creature takes a thrown object with ITEM_FLAG_CAUGHT that hits it */
#define CREATURE_SEES_INVISIBLE  0x0400 /* flags: the party spell of invisibility does not hide the party */
#define CREATURE_SEES_IN_DARK    0x0800 /* flags: the light does not limit its sight */
#define CREATURE_FAST_VANISH     0x1000 /* flags: the creature vanishes quickly in a party action effect */
#define CREATURE_WANDERER        0x4000 /* the creature may enter any map */
#define CREATURE_NOT_SOLID       0x8000 /* the party and objects pass the creature */

/* Bits 6-7 of CreatureInfo.flags: the door height that the creature blocks. */
#define CREATURE_DOOR_LEVEL_MASK 0x3

/* The shift of the door height in CreatureInfo.flags. */
#define CREATURE_DOOR_LEVEL_SHIFT 6

/* Bits of CreatureInfo.door_flags. */
#define CREATURE_RESISTS_NON_FIRE 0x10 /* door_flags: doors, melee hits, and spells that are not fire hurt it less */

/* Bits of CreatureInfo.traits. */
#define CREATURE_TRAIT_SHOT_HURTS    0x10 /* a missile that it shoots hurts the creature */
#define CREATURE_TRAIT_TWO_CHAMPIONS 0x20 /* a melee attack hits two champions */
#define CREATURE_TRAIT_CLOSES_PITS   0x40 /* the creature closes the pit under it while it walks */

/* Bits of CreatureInfo.movement. */
#define CREATURE_FLIES 0x0004

/* Bits of CreatureInfo.possessions. */
#define CREATURE_POSSESSIONS_ON_FLOOR 0x0800 /* possessions bit: a static creature holds what lies on its square */

/* Possessions bit: the objects under the creature stay clickable, and a drop goes into the front cells. */
#define POSSESSIONS_FLOOR_CLICKABLE 0x2000

/* the display kind (0: none shown; 1 or less: carried low); a static creature: the possession cell count */
#define POSSESSIONS_KIND_MASK    0x000f
#define STATIC_POSSESSIONS_SHOWN 0x0400
#define STATIC_POSSESSIONS_ALL   0x0200

/* Push resistances of CreatureInfo.push_resistance, and the push strength of the party. */
#define PUSH_IMMOVABLE      0xfe
#define PUSH_WALL           0xff /* push resistance: the party bumps into the creature as into a wall */
#define PUSH_STRENGTH_PARTY 0xfe

/* The defense or dexterity of a creature that nothing hurts or hits. */
#define CREATURE_NO_HURT 0xff

/* A cell mask: all four cells. */
#define CELLS_ALL 0x0f

/* The nibbles of CreatureInfo.speed and the armor in door_flags. */
#define INFO_NIBBLE_MASK 0x0f
#define ARMOR_IMMUNE     0x0f /* the armor of a creature that takes no damage: the highest */

/* The shifts of the nibbles of CreatureInfo.speed. */
#define SPEED_FEAR_SHIFT       4
#define SPEED_EXPERIENCE_SHIFT 8
#define SPEED_TURN_SHIFT       12
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
    return info->flags >> CREATURE_DOOR_LEVEL_SHIFT & CREATURE_DOOR_LEVEL_MASK;
}
/* The shift of the attack range in CreatureInfo.sight_and_range. */
#define ATTACK_RANGE_SHIFT 12
/* The squares that the attacks of the creature reach. */
static inline uint16_t creature_attack_range(const CreatureInfo *info)
{
    return info->sight_and_range >> ATTACK_RANGE_SHIFT;
}
/* How well the creature sees an invisible party or a party in the dark. */
static inline uint16_t creature_sight(const CreatureInfo *info)
{
    return info->sight_and_range & INFO_NIBBLE_MASK;
}

/* GDAT value keys (0x0f, creature type, 0x0b, key) of a creature type. */
#define CREATURE_ATTR_CLASS       0x01 /* GDAT value key: the AI class */
#define CREATURE_ATTR_MARKER_MASK 0x07 /* GDAT value key: the kinds of the AI markers that the creature heeds */

/* The sound of an item that a creature drops. */
#define SOUND_ENTRY_DROP    0x85
#define SOUND_PRIORITY_DROP 0x3a

/* The sound of a creature that a teleporter moves or that a generator creates. */
#define SOUND_ENTRY_TELEPORT 0x89
#define SOUND_PRIORITY_MOVE  0x61

/* Animation step (4 bytes): the bytes of a step. */
#define STEP_SOUND 0
#define STEP_NEXT  1
#define STEP_JUMP  2
#define STEP_TIME  3

/* Anim bits: a random picture shift, sideways (bits 0-2) and forward. */
#define ANIM_JITTER 0x07

/* The time bytes of an active group are the low byte of the game time. */
#define TIME_BYTE_WRAP 0x100

/* An animation frame (GDAT (0x0f, creature type, 7, 0xfd), 8 bytes per frame). */
#define FRAME_ENTRY         0 /* 4 bytes: picture entry by the side the creature shows (front, right, back, left) */
#define FRAME_CELL          4 /* cell in the 5 x 5 grid of its square */
#define FRAME_SIZE          5 /* row of the size table (0x0f, creature type, 7, 0xfe) */
#define FRAME_SHIFT         6 /* sideways and forward shift of the picture */
#define FRAME_MIRROR        7 /* 2 bits per side: bit 0 mirror, bit 1 mirror on an animation bit */
#define CREATURE_FRAME_SIZE 8

/* The plan variables of an active group. */
#define PLAN_COUNTER_COUNT 2

/* An active creature group: the state of a creature that acts (34 bytes, the layout of SKULL.EXE). */
typedef struct {
    uint16_t group;           /* record index of the group, ACTIVE_FREE for a free entry */
    int16_t event;            /* the timeline event of its next update, -1 none */
    uint8_t last_action_time; /* low byte of the game time of its last attack or move */
    uint8_t padding;
    uint8_t regeneration_time;            /* low byte of game time / 4 of the last health change */
    uint8_t anim_bits;                    /* ANIM_BIT_*: random shift and mirror of the picture */
    uint16_t anim_sequence;               /* first step of the animation sequence */
    uint16_t anim_state;                  /* step in the sequence */
    uint16_t position;                    /* where it was activated: x bits 0-4, y bits 5-9, map bits 10-15 */
    int16_t counters[PLAN_COUNTER_COUNT]; /* plan variables */
    int8_t plan;                          /* the plan that it follows, -1 none */
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

/* The size of an active group of SKULL.EXE. */
#define ACTIVE_GROUP_SIZE 34
_Static_assert(sizeof(ActiveGroup) == ACTIVE_GROUP_SIZE, "active groups have 34 bytes");

/* The marker of a free active group. */
#define ACTIVE_FREE 0xffff

/* Markers of a group without an active group and of an active group without an action. */
#define GROUP_NOT_ACTIVE   0xff
#define ACTIVE_ACTION_NONE 0xff /* an active group without a chosen action */

/* An active group without a scheduled update. */
#define EVENT_NO_SLOT (-1)

/* The results of creature_anim_next_step: the step is the last, more steps follow, or the sequence ended. */
typedef enum { ANIM_LAST_STEP, ANIM_MORE_STEPS, ANIM_SEQUENCE_END } AnimStepResult;

/* A step of a creature animation: the sequence, the state word, the step record, and how the animation goes on. */
typedef struct {
    uint16_t sequence;     /* the first step of the sequence */
    uint16_t state;        /* the animation state word */
    const uint8_t *frame;  /* the step (4 bytes); NULL: the start of a static creature keeps the step */
    AnimStepResult result; /* ANIM_MORE_STEPS: the step has a duration or more steps follow */
} AnimStep;

/* Creature types of minions, by the command that makes them. */
#define MINION_ATTACK  0x30
#define MINION_SCOUT   0x31
#define MINION_GUARD   0x32
#define MINION_FETCH   0x33
#define MINION_CARRIER 0x34
#define MINION_ARMY    0x35

/* Group life. flags of an alert: bits 0-3 a mood bit that the alert sets (or clears, ALERT_CLEAR) with chance
   percent, 0x2000 loud (wakes the creature), 0x4000 may make it angry; damage adds to its damage. */
#define ALERT_MOOD_BORED 0x0005 /* alert flags: mood bit 5, MOOD_BORED */
#define ALERT_MOOD_QUICK 0x0006 /* alert flags: mood bit 6, MOOD_QUICK */
#define ALERT_LOUD       0x2000 /* the alert wakes the creature */
#define ALERT_ANGER      0x4000 /* alert flags: the creature may become angry */
#define ALERT_CLEAR      0x8000 /* the alert clears the mood bit (bits 0-3) instead of setting it */
#define ALERT_STARTLED   (ALERT_LOUD | ALERT_MOOD_QUICK) /* a loud alert that makes the creature hurry */

/* An alert chance in percent. */
#define ALERT_ALWAYS 100

/* What a creature that leaves the dungeon drops: its drop items and possessions, its possessions, or nothing. */
typedef enum { DROP_ALL, DROP_POSSESSIONS, DROP_NOTHING } DropMode;

/* creature_create_group count: the group starts with full health. */
#define GROUP_COUNT_FULL_HEALTH 7

/* A group that appears on a square of a map, or on a free square beside it. */
typedef struct {
    int creature_type;
    uint16_t count; /* the creature_create_group count */
    int16_t dir;
    int16_t x, y, map;
    uint16_t link;     /* the item that controls a minion, THING_NONE none */
    int8_t search_dir; /* the first direction of the search for a free square, -1 no search */
} CreatureSummon;

/* A new group and the square where its move put it (group THING_NONE: none). */
typedef struct {
    uint16_t group;
    Position at;
} SummonedGroup;

/* The active groups; NULL before the first game. */
extern ActiveGroup *g_active_groups;
/* The size of g_active_groups; exported for the tests */
extern int g_active_group_max;
/* The active groups in use; exported for the tests */
extern int g_active_group_count;

/* Creature info. */
/* The info record at index; NULL past the table. */
const CreatureInfo *creature_info_at(int index);
/* A GDAT value (0x0f, creature type, 0x0b, attr) of a creature type. */
uint16_t creature_gdat_attr(int creature_type, int attr);
/* The info record of a creature type, read through GDAT (0x0f, type, 0x0b, 5). */
const CreatureInfo *creature_info(const Gdat *g, int creature_type);
/* The info record of the creature of a group thing. */
const CreatureInfo *creature_group_info(uint16_t group);
/* The active group of a creature group, NULL when it has none. */
ActiveGroup *creature_active(uint16_t group);

/* Animation. */
/* The animation sequence and state words of a group record: in the record of a static creature, else in its active group. */
uint16_t *creature_anim_words(uint16_t group);
/* The current animation frame of a creature group. */
void creature_anim_frame(const Gdat *g, uint16_t group, uint8_t frame[CREATURE_FRAME_SIZE]);
/* The frame of an animation state of a creature type. */
void creature_anim_frame_of(const Gdat *g, int creature_type, uint16_t sequence, uint16_t state,
                            uint8_t frame[CREATURE_FRAME_SIZE]);
/* The next step of a sequence after state, through the random chances of its steps; ANIM_MORE_STEPS when the step has
   a duration. */
AnimStep creature_anim_next_random_step(int creature_type, uint16_t sequence, uint16_t state);
/* The step after state by the jump of the current step. */
AnimStep creature_anim_next_step(int creature_type, uint16_t sequence, uint16_t state);
/* Starts the animation sequence of action; ANIM_MORE_STEPS when the step has a duration. start: the first frame of a
   looping sequence of a static creature. */
AnimStep creature_anim_start(int creature_type, int action, uint16_t start);
/* True when the current animation step of a group is a step at which the creature acts. */
bool creature_current_step_acts(uint16_t group);

/* Groups on squares. */
/* The creature group on the square at of the current map, THING_NONE for none. */
uint16_t creature_get_group_at(Location at);
/* The first creature group on the square at, THING_NONE for none. */
uint16_t creature_group_at(Position at);
/* True when the creature of group may stay on map: a wanderer, or a creature type of the map. */
bool creature_is_native_to_map(uint16_t group, int16_t map);
/* True for a creature that flies over pits and does not press floor sensors. */
bool creature_is_flying(uint16_t group);
/* The cells (bits 0 to 3) in which a static creature shows its possessions to side (0 to 3), from its possessions
   word. */
int creature_cell_mask(uint16_t possessions, int16_t side);

/* Active groups. */
/* The timeline event of the next update of an active group. */
int creature_active_group_event(int active_index);
/* True while the creature of a group is in the air in a fall or jump. */
bool creature_group_jumps(uint16_t group);
/* Drops the active groups of the last game (a load). */
void creature_free_active_groups(void);
/* Allocates the active groups for the groups of the dungeon; exported for the tests */
void creature_alloc_active_groups(void);
/* Frees all active groups and activates the groups of all maps (a new game or a load). */
void creature_reset_active_groups(void);
/* Activates the creatures of the current map and restarts the loops of its static creatures. */
void creature_activate_groups_on_map(void);
/* Activates the creatures of all maps. */
void creature_activate_all_maps(void);
/* Deactivates all active groups. */
void creature_deactivate_all_groups(void);
/* Deletes the next update event of a group. */
void creature_cancel_update(uint16_t group);
/* Gives a group an active group: its AI state, animation, and first update. */
void creature_activate_group(uint16_t group, Location at);
/* Frees an active group; a creature that vanished leaves the dungeon. */
void creature_deactivate_group(int active_index);
/* Sets the action of the next update of a group; now: the update comes at once, or the current action stops. */
void creature_set_next_action(uint16_t group, Location at, int action, bool now);
/* Schedules the next animation update of the static creature on at; alt: at the end of its loop. */
void creature_update_anim_at(Location at, bool alt);
/* Animates the creature in the AI context by one step (picture shift, mirror, sound); returns the time of its next
   update. */
uint32_t creature_animate_step(void);
/* The update event of a creature group: regeneration, damage, then the AI or the animation. */
void creature_process_event(Location at, int type);

/* Group life: creation, damage, death, alerts. */
/* A dead creature leaves its drop items and its possessions (by mode) on at; audible: each item makes a sound. */
void creature_drop_possessions(uint16_t group, Location at, DropMode mode, bool audible);
/* Removes the group on at from the dungeon; it drops what it carries (by mode). */
void creature_delete_group_at(Location at, DropMode mode, bool audible);
/* Damages the group in the AI context; true when it was removed. */
bool creature_damage_current_group(int damage);
/* The creature in the AI context hits the group on at; false when no group is there. *damage_out: 0 for a miss. */
bool creature_attack_group_at(Location at, int16_t *damage_out);
/* Alerts the group (or the group on at for THING_NONE) with the alert flags, chance and damage. */
void creature_alert(uint16_t group, Location at, unsigned flags, int16_t chance, int damage);
/* An extra damage amount (poison) against the armor of the creature, with a random part; 0 for no amount. */
int creature_roll_armor_factor(uint16_t group, uint16_t amount);
/* A damage kind that sets or clears a mood bit of the group. kind keeps ALERT_CLEAR in its mask word, as the AI
   candidate mask holds it (ai_move.c). */
void creature_apply_damage_kind(uint16_t group, Location at, int kind);
/* Creates a group of count + 1 creatures on at of the current map; THING_NONE when the dungeon is full. */
uint16_t creature_create_group(int creature_type, int count, int16_t dir, Location at);
/* Creates a group on the square (x, y) of map or on a free square beside it, and returns where it ends; link: the
   item that controls a minion. */
SummonedGroup creature_summon_group(CreatureSummon summon);
/* Turns a group, to dir (absolute) or by dir; a static creature turns its possessions too. */
void creature_turn_group(uint16_t group, bool absolute, int16_t dir);
/* The distance from at to the party in the distance map of the current map; -1 when no map has it. */
int creature_get_party_distance(Location at);

/* Pushing creatures: the push arrows of the party and the pushes of creatures. */
/* True when the group on at can be pushed one square in dir; *out_group (if not NULL) gets it. target_must_be_free:
   a group on the target square stops the push (a real push); false only asks whether the push arrow shows. */
bool creature_can_be_pushed(Location at, int16_t dir, bool target_must_be_free, uint16_t *out_group);
/* Pushes the group on at one square in dir; a push into the party moves the party. */
void creature_push_group(Location at, int16_t dir, int16_t strength);
/* The party pushes the creature in front by a push arrow. */
void creature_push_creature(int arrow);
/* The party walks on after a push that waited for its slow move: the push ends now. */
void creature_continue_push(int command);

/* Creature groups near a point. */
/* A creature group and its square. */
typedef struct {
    uint16_t group;
    Location at;
} GroupNear;
/* The creature group whose body covers the point of cell (0xff: any cell) of the square at of the current map or of a
   square next to it, and that square; group THING_NONE (with at) when there is none. */
GroupNear creature_find_group_near_point(Location at, int16_t cell);

#endif /* DM2_ENGINE_CREATURE_H */
