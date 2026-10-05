#include <string.h>
#include "creature.h"
#include "dungeon.h"
#include "save.h"

#define CAT_CREATURE 0x0f
#define TYPE_DATA 7                 /* GDAT key type of raw data */
#define CREATURE_KEY_INFO 5         /* value key: index of the creature info record */
#define CREATURE_FRAMES_ENTRY 0xfd  /* data: the animation frames */

/* Animation state word. */
#define ANIM_STILL 0x4000           /* the creature shows the first frame of its sequence */
#define ANIM_LOOP 0x8000            /* the sequence repeats with the game time */
#define ANIM_RESTART 0x1000         /* a looping sequence starts again from frame 0 */
#define ANIM_START_MASK 0x0fc0      /* first frame of a looping sequence */
#define ANIM_START_SHIFT 6
#define ANIM_RESTART_CLEAR 0xe03f   /* clears ANIM_RESTART and the first frame */
#define ANIM_FRAME_MASK 0x3f        /* frame of a still sequence, length of a looping one */

/* The creature info records of SKULL.EXE: flags, possessions, size. */
static const CreatureInfo creature_infos[CREATURE_INFO_COUNT] = {
    {0x0045, 0x4000, 0}, {0x0085, 0xfd22, 0}, {0x0085, 0x9e22, 0}, {0x0085, 0x2000, 0}, {0x0045, 0x4000, 0},
    {0x0045, 0x4000, 0}, {0x0045, 0x4000, 2}, {0x0085, 0x0a01, 0}, {0x0041, 0x4000, 0}, {0x0081, 0xac42, 0},
    {0x0085, 0x0b01, 0}, {0x0045, 0x4000, 0}, {0x0081, 0xed41, 0}, {0xcc40, 0x0000, 0}, {0xc840, 0x0000, 0},
    {0xc840, 0x0000, 0}, {0xc840, 0x0000, 0}, {0xc840, 0x0000, 0}, {0xc840, 0x0000, 0}, {0x0240, 0x0000, 3},
    {0x8081, 0x6000, 0}, {0x8ca0, 0x0000, 1}, {0x8860, 0x0000, 1}, {0x8840, 0x0000, 1}, {0x0ac0, 0x0000, 1},
    {0x0a80, 0x0000, 1}, {0x0280, 0x0000, 1}, {0x0280, 0x0000, 1}, {0x0ac0, 0x0000, 2}, {0x0a40, 0x0000, 0},
    {0x1c40, 0x0000, 2}, {0x0040, 0x0000, 2}, {0x0040, 0x0000, 1}, {0x0240, 0x0000, 1}, {0xc840, 0x0000, 0},
    {0x8840, 0x0000, 1}, {0x0240, 0x0000, 2}, {0x0043, 0x4000, 0}, {0x0045, 0x4100, 0}, {0x0085, 0x0a01, 0},
    {0x0140, 0x0000, 3}, {0x8c78, 0x0000, 1}, {0x0280, 0x0000, 0}, {0xc840, 0x0000, 0}, {0x0240, 0x0000, 1},
    {0x0081, 0x8c42, 0}, {0x0240, 0x0000, 1}, {0x2044, 0x0000, 0}, {0x0290, 0x0000, 1}, {0xc840, 0x0000, 0},
    {0x0240, 0x0000, 1}, {0x0041, 0x4000, 0}, {0x0280, 0x0000, 2}, {0xacb8, 0x0000, 1}, {0xacb8, 0x0000, 1},
    {0x0290, 0x0000, 1}, {0x8081, 0x6000, 0}, {0x0240, 0x0000, 1}, {0x0880, 0x0000, 0}, {0x0045, 0x6000, 0},
    {0x0085, 0x4000, 0}, {0x8c78, 0x0000, 1}, {0xc840, 0x0000, 0},
};

const CreatureInfo *creature_info_at(int index)
{
    return index >= 0 && index < CREATURE_INFO_COUNT ? creature_infos + index : NULL;
}

const CreatureInfo *creature_info(const gdat_t *g, int creature_type)
{
    const CreatureInfo *info = creature_info_at(gdat_entry_index(g, CAT_CREATURE, creature_type, GDAT_TYPE_VALUE,
                                                                 CREATURE_KEY_INFO));
    return info ? info : creature_infos;  /* fix: the original reads past the table */
}

/* The frame of an animation state relative to the start of its sequence; a restarting loop clears its restart. */
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
    /* Static creatures keep their animation in the record. The others keep it in their active group, which
       Step 16 adds; until then they show the first frame of their first sequence. */
    int is_static = creature_info(g, type)->flags & CREATURE_STATIC;
    uint16_t sequence = 0, state = 0;
    if (is_static) {
        sequence = thing_word(group, GROUP_ANIM_SEQUENCE);
        state = thing_word(group, GROUP_ANIM_STATE);
    }
    unsigned index = (uint16_t)(anim_frame_offset(&state) + sequence);
    if (is_static) set_thing_word(group, GROUP_ANIM_STATE, state);
    uint32_t size;
    const uint8_t *data = gdat_item(g, gdat_find(g, CAT_CREATURE, type, TYPE_DATA, CREATURE_FRAMES_ENTRY), &size);
    memset(frame, 0, CREATURE_FRAME_SIZE);
    if (data && (index + 1) * CREATURE_FRAME_SIZE <= size)  /* fix: the original reads past the frames */
        memcpy(frame, data + index * CREATURE_FRAME_SIZE, CREATURE_FRAME_SIZE);
}
