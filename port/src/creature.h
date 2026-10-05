#ifndef CREATURE_H
#define CREATURE_H
#include <stdint.h>
#include "gdat.h"
#include "timeline.h"

/* A creature info record of SKULL.EXE (36 bytes, the same layout). */
typedef struct {
    uint16_t flags;             /* CREATURE_STATIC, CREATURE_FACES_PARTY, ... */
    uint8_t defense;            /* against hits; 0xff: the creature cannot be hurt */
    int8_t regeneration;        /* game steps / 4 per health point; negative: the creature loses health */
    uint16_t max_health;
    uint8_t attack;             /* the power of its attacks and missiles */
    uint8_t poison;
    uint8_t dexterity;          /* hit chance; 0xff: other creatures cannot hit it */
    uint8_t traits;             /* CREATURE_TRAIT_*; bits 0-1 and 2-3: random picture shift */
    uint16_t movement;          /* what the creature may walk through: CREATURE_FLIES, AI_MOVE_* */
    uint16_t item_skills;       /* how the creature handles items: AI_ITEM_* */
    uint16_t attack_skills;     /* AI_ATTACK_* */
    uint16_t door_skills;       /* AI_DOOR_* */
    uint16_t actuator_skills;   /* how the creature uses wall actuators */
    uint16_t unknown_14;
    uint16_t speed;             /* bits 0-3 think speed, 4-7 mood change, 12-15 turn chance */
    uint8_t unknown_18;
    uint8_t door_flags;         /* bits 0-3 armor, CREATURE_DOOR_HALF_DAMAGE */
    uint8_t unknown_1a, unknown_1b, unknown_1c;
    uint8_t push_resistance;    /* against the party strength; 0xfe and 0xff: the creature cannot be pushed */
    uint16_t possessions;       /* how a static creature shows the items it holds */
    uint16_t possession_zone;   /* the view zone of the possessions that the creature hides */
    uint8_t size;               /* 1 to 3: how far the creature stands from the center of its cell toward the party */
    uint8_t hit_size;           /* row of the hit radius table; also the size of its death explosion */
} CreatureInfo;

_Static_assert(sizeof(CreatureInfo) == 36, "the creature info records of SKULL.EXE have 36 bytes");

#define CREATURE_STATIC 0x0001      /* the creature does not act: no active group, animation state in its record */
#define CREATURE_FACES_PARTY 0x0004 /* the creature always shows its front */
#define CREATURE_ATTACKS_ALL 0x0008 /* a melee attack hits several champions */
#define CREATURE_ATTACKS_RANDOM 0x0010 /* ... random champions */
#define CREATURE_NOT_DOOR_HIT 0x0020 /* a closing door does not hit the creature */
#define CREATURE_DOOR_SIZE_SHIFT 6  /* bits 6-7: the door height that the creature blocks */
#define CREATURE_WANDERER 0x4000    /* the creature may enter any map */
#define CREATURE_NOT_SOLID 0x8000   /* the party and objects pass the creature */
#define CREATURE_DOOR_HALF_DAMAGE 0x10
#define CREATURE_POSSESSIONS_ON_FLOOR 0x0800 /* possessions bit: a static creature holds what lies on its square */
#define CREATURE_TRAIT_SHOT_HURTS 0x10      /* a missile that it shoots hurts the creature */
#define CREATURE_TRAIT_TWO_CHAMPIONS 0x20   /* a melee attack hits two champions */
#define CREATURE_TRAIT_CLOSES_PITS 0x40     /* the creature closes the pit under it while it walks */
#define PUSH_IMMOVABLE 0xfe
#define CREATURE_FLIES 0x0004
#define POSSESSIONS_HIDE_CLICKS 0x2000  /* possessions bit: objects under the creature stay clickable */
#define CREATURE_INFO_COUNT 63

/* An animation frame (GDAT (0x0f, creature type, 7, 0xfd), 8 bytes per frame). */
enum creature_frame_field {
    FRAME_ENTRY = 0,            /* 4 bytes: picture entry by the side the creature shows (front, right, back, left) */
    FRAME_CELL = 4,             /* cell in the 5 x 5 grid of its square */
    FRAME_SIZE = 5,             /* row of the size table (0x0f, creature type, 7, 0xfe) */
    FRAME_SHIFT = 6,            /* sideways and forward shift of the picture */
    FRAME_MIRROR = 7,           /* 2 bits per side: bit 0 mirror, bit 1 mirror on an animation bit */
};
#define CREATURE_FRAME_SIZE 8
#define CREATURE_NO_HURT 0xff           /* defense, dexterity */
#define CREATURE_ATTR_CLASS 1           /* GDAT value key: the AI class */
#define CREATURE_ATTR_7 7

/* An active creature group: the state of a creature that acts (34 bytes, the layout of SKULL.EXE). */
typedef struct {
    uint16_t group;             /* record index of the group, ACTIVE_FREE for a free entry */
    int16_t event;              /* the timeline event of its next update, -1 none */
    uint8_t last_action_time;   /* low byte of the game time of its last attack or move */
    uint8_t unused_5;
    uint8_t regeneration_time;  /* low byte of game time / 4 of the last health change */
    uint8_t anim_bits;          /* ANIM_BIT_*: random shift and mirror of the picture */
    uint16_t anim_sequence;     /* first step of the animation sequence */
    uint16_t anim_state;        /* step in the sequence */
    uint16_t position;          /* where it was activated: x bits 0-4, y bits 5-9, map bits 10-15 */
    int16_t counters[2];        /* plan variables */
    int8_t plan;                /* the plan that it follows, -1 none */
    int8_t plan_step;
    uint16_t damage;            /* damage taken since its last update */
    int8_t behavior;            /* index of its mood entry */
    int8_t next_action;         /* the action that its next update starts, -1 none */
    uint16_t destination;       /* x bits 0-4, y bits 5-9, map bits 10-15; bits 8-15 also an action direction */
    uint8_t action;             /* AI_ACTION_* */
    uint8_t direction;          /* the direction of the action */
    uint8_t cell;               /* the cell of the action */
    uint8_t facing;             /* the direction the action turns to */
    int8_t sub;                 /* the item filter or sub-command of the action */
    uint8_t phase;              /* the phase of a multi-step action */
    uint8_t mode;               /* the mode of the action */
    uint8_t interrupt;          /* the action stops its animation */
} ActiveGroup;

_Static_assert(sizeof(ActiveGroup) == 34, "active groups have 34 bytes");

#define ACTIVE_FREE 0xffff
#define GROUP_NOT_ACTIVE 0xff
#define EVENT_NO_SLOT (-1)

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
int creature_is_native_to_map(uint16_t group, int map);
/* The cells (bits 0 to 3) in which a static creature shows its possessions, from its possessions word. */
int creature_cell_mask(uint16_t possessions);
/* True for a creature that flies over pits and does not press floor sensors. */
int creature_is_flying(uint16_t group);
/* The first creature group on the square (x, y) of map, THING_NONE for none. */
uint16_t creature_group_at(int map, int x, int y);
/* The creature group on (x, y) of the current map, THING_NONE for none. */
uint16_t creature_get_group_at(int x, int y);
/* The active group of a creature group, NULL when it has none. */
ActiveGroup *creature_active(uint16_t group);

/* The current animation frame of a creature group. */
void creature_anim_frame(const gdat_t *g, uint16_t group, uint8_t frame[CREATURE_FRAME_SIZE]);
/* The frame of an animation state of a creature type. */
void creature_anim_frame_of(const gdat_t *g, int creature_type, uint16_t sequence, uint16_t state, uint8_t frame[CREATURE_FRAME_SIZE]);
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
int creature_anim_next_step(int creature_type, uint16_t sequence, uint16_t *step, const uint8_t **frame);
/* True when the current animation step of a group is a step at which the creature acts. */
int creature_current_step_acts(uint16_t group);
/* The time of the next update of the group in the AI context, with the animation bits of its step. */
uint32_t creature_ai_animate_and_get_next_time(void);

/* Active groups. */
void creature_alloc_active_groups(void);
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
void creature_alert(uint16_t group, int x, int y, int flags, int chance, int damage);
void creature_turn_group(uint16_t group, int absolute, int dir);
void creature_drop_possessions(uint16_t group, int x, int y, int mode, int sound_map);
void creature_delete_group_at(int x, int y, int mode, int sound_map);
uint16_t creature_create_group(int creature_type, int count, int dir, int x, int y);
void move_schedule_group_retry(uint16_t group, int x, int y, int map, int audible);
void move_update_carried_missiles(uint16_t holder, int x, int y, int map, int flag);
void timeline_event_move_group(TimelineEvent *e);

/* Pushing creatures: the push arrows of the party and the pushes of creatures. */
int creature_can_be_pushed(int x, int y, int dir, uint16_t *group);
int move_party_can_push_group(uint16_t group);
void creature_push_group(int x, int y, int dir, int strength);
void move_push_creature(int arrow);
/* The party walks on after a push that waited for its slow move: the push ends now. */
void creature_continue_push(int command);

#endif
