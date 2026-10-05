#ifndef CREATURE_H
#define CREATURE_H
#include <stdint.h>
#include "gdat.h"

/* The fields of a creature info record of SKULL.EXE that the port reads. */
typedef struct {
    uint16_t flags;             /* CREATURE_STATIC, CREATURE_FACES_PARTY */
    uint16_t possessions;       /* how a static creature shows the items it holds */
    uint8_t size;               /* 1 to 3: how far the creature stands from the center of its cell toward the party */
    uint8_t door_flags;         /* CREATURE_DOOR_HALF_DAMAGE */
    uint8_t push_resistance;    /* against the party strength; 0xfe and 0xff: the creature cannot be pushed */
    uint8_t hit_size;           /* row of the hit radius table */
    uint16_t movement;          /* CREATURE_FLIES */
    uint16_t possession_zone;   /* the view zone of the possessions that the creature hides */
} CreatureInfo;

#define CREATURE_STATIC 0x0001      /* the creature does not act: no active group, animation state in its record */
#define CREATURE_FACES_PARTY 0x0004 /* the creature always shows its front */
#define CREATURE_NOT_DOOR_HIT 0x0020 /* a closing door does not hit the creature */
#define CREATURE_DOOR_SIZE_SHIFT 6  /* bits 6-7: the door height that the creature blocks */
#define CREATURE_WANDERER 0x4000    /* the creature may enter any map */
#define CREATURE_NOT_SOLID 0x8000   /* the party and objects pass the creature */
#define CREATURE_DOOR_HALF_DAMAGE 0x10
#define CREATURE_POSSESSIONS_ON_FLOOR 0x0800 /* possessions bit: a static creature holds what lies on its square */
#define PUSH_IMMOVABLE 0xfe
#define CREATURE_FLIES 0x0004
#define POSSESSIONS_HIDE_CLICKS 0x2000  /* possessions bit: objects under the creature stay clickable */
#define CREATURE_INFO_COUNT 63
#define CREATURE_FRAME_SIZE 8

/* An animation frame (GDAT (0x0f, creature type, 7, 0xfd), 8 bytes per frame). */
enum creature_frame_field {
    FRAME_ENTRY = 0,            /* 4 bytes: picture entry by the side the creature shows (front, right, back, left) */
    FRAME_CELL = 4,             /* cell in the 5 x 5 grid of its square */
    FRAME_SIZE = 5,             /* row of the size table (0x0f, creature type, 7, 0xfe) */
    FRAME_SHIFT = 6,            /* sideways and forward shift of the picture */
    FRAME_MIRROR = 7,           /* 2 bits per side: bit 0 mirror, bit 1 mirror on an animation bit */
};

/* The info record of a creature type, read through GDAT (0x0f, type, 0x0b, 5). */
const CreatureInfo *creature_info(const gdat_t *g, int creature_type);
/* The info record at index; NULL past the table. */
const CreatureInfo *creature_info_at(int index);
/* The info record of the creature of a group thing. */
const CreatureInfo *creature_group_info(uint16_t group);
/* True when the creature of group may stay on map: a wanderer, or a creature type of the map. */
int creature_is_native_to_map(uint16_t group, int map);
/* The cells (bits 0 to 3) in which a static creature shows its possessions, from its possessions word. */
int creature_cell_mask(uint16_t possessions);
/* True for a creature that flies over pits and does not press floor sensors. */
int creature_is_flying(uint16_t group);
/* The first creature group on the square (x, y) of map, THING_NONE for none. */
uint16_t creature_group_at(int map, int x, int y);

/* The current animation frame of a creature group. */
void creature_anim_frame(const gdat_t *g, uint16_t group, uint8_t frame[CREATURE_FRAME_SIZE]);

#endif
