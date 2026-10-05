#ifndef CREATURE_H
#define CREATURE_H
#include <stdint.h>
#include "gdat.h"

/* The fields of a creature info record of SKULL.EXE that the port reads. */
typedef struct {
    uint16_t flags;             /* CREATURE_STATIC, CREATURE_FACES_PARTY */
    uint16_t possessions;       /* how a static creature shows the items it holds */
    uint8_t size;               /* 1 to 3: how far the creature stands from the center of its cell toward the party */
} CreatureInfo;

#define CREATURE_STATIC 0x0001      /* the creature does not act: no active group, animation state in its record */
#define CREATURE_FACES_PARTY 0x0004 /* the creature always shows its front */
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
/* The current animation frame of a creature group. */
void creature_anim_frame(const gdat_t *g, uint16_t group, uint8_t frame[CREATURE_FRAME_SIZE]);

#endif
