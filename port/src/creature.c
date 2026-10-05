#include <string.h>
#include "creature.h"
#include "dungeon.h"
#include "draw.h"
#include "hooks.h"
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

/* The creature info records of SKULL.EXE: flags, possessions, size, door flags, push resistance, hit size, movement flags. */
static const CreatureInfo creature_infos[CREATURE_INFO_COUNT] = {
    {0x0045, 0x4000, 0, 0x0f, 0xff, 2, 0x0000}, {0x0085, 0xfd22, 0, 0x0f, 0xff, 2, 0x0000}, {0x0085, 0x9e22, 0, 0x0f, 0xff, 2, 0x0000},
    {0x0085, 0x2000, 0, 0x0d, 0xff, 1, 0x0000}, {0x0045, 0x4000, 0, 0x0f, 0xff, 1, 0x0000}, {0x0045, 0x4000, 0, 0x0f, 0xff, 1, 0x0000},
    {0x0045, 0x4000, 2, 0x0f, 0x5f, 2, 0x0000}, {0x0085, 0x0a01, 0, 0x0f, 0xff, 2, 0x0000}, {0x0041, 0x4000, 0, 0x0f, 0xff, 2, 0x0000},
    {0x0081, 0xac42, 0, 0x0f, 0x2d, 2, 0x0000}, {0x0085, 0x0b01, 0, 0x0f, 0xff, 2, 0x0000}, {0x0045, 0x4000, 0, 0x0f, 0x28, 0, 0x0000},
    {0x0081, 0xed41, 0, 0x0f, 0xff, 0, 0x0000}, {0xcc40, 0x0000, 0, 0x0c, 0x0a, 1, 0x6d9e}, {0xc840, 0x0000, 0, 0x0c, 0x0a, 1, 0x6d9e},
    {0xc840, 0x0000, 0, 0x0c, 0x0a, 1, 0x6d9e}, {0xc840, 0x0000, 0, 0x0c, 0x0a, 1, 0x6d9e}, {0xc840, 0x0000, 0, 0x0c, 0xff, 1, 0x2c06},
    {0xc840, 0x0000, 0, 0x0c, 0x0a, 1, 0x6d9e}, {0x0240, 0x0000, 3, 0x08, 0xfd, 2, 0x0002}, {0x8081, 0x6000, 0, 0x0f, 0xff, 1, 0x0000},
    {0x8ca0, 0x0000, 1, 0x0f, 0xff, 1, 0x0006}, {0x8860, 0x0000, 1, 0x0f, 0x14, 1, 0x0a06}, {0x8840, 0x0000, 1, 0x03, 0x0a, 1, 0x0806},
    {0x0ac0, 0x0000, 1, 0x06, 0x41, 1, 0x2402}, {0x0a80, 0x0000, 1, 0x09, 0x46, 1, 0xe402}, {0x0280, 0x0000, 1, 0x06, 0x28, 0, 0x64a2},
    {0x0280, 0x0000, 1, 0x08, 0x2d, 1, 0x6402}, {0x0ac0, 0x0000, 2, 0x09, 0x5a, 2, 0x0002}, {0x0a40, 0x0000, 0, 0x0e, 0xff, 2, 0x0002},
    {0x1c40, 0x0000, 2, 0x0e, 0xfd, 1, 0x41a2}, {0x0040, 0x0000, 2, 0x0b, 0xfe, 2, 0x4082}, {0x0040, 0x0000, 1, 0x0f, 0xff, 2, 0x0000},
    {0x0240, 0x0000, 1, 0x0c, 0x6e, 1, 0x4002}, {0xc840, 0x0000, 0, 0x0c, 0x0a, 1, 0x6d86}, {0x8840, 0x0000, 1, 0x03, 0x0a, 1, 0x0807},
    {0x0240, 0x0000, 2, 0x09, 0x6e, 2, 0x6402}, {0x0043, 0x4000, 0, 0x0f, 0x46, 2, 0x0000}, {0x0045, 0x4100, 0, 0x0f, 0xff, 2, 0x0000},
    {0x0085, 0x0a01, 0, 0x0f, 0xff, 2, 0x0000}, {0x0140, 0x0000, 3, 0x0f, 0xff, 2, 0x0002}, {0x8c78, 0x0000, 1, 0x0f, 0xff, 1, 0x6606},
    {0x0280, 0x0000, 0, 0x04, 0xfe, 2, 0x2402}, {0xc840, 0x0000, 0, 0x0c, 0x0a, 1, 0x6d9e}, {0x0240, 0x0000, 1, 0x0a, 0xb4, 1, 0x6402},
    {0x0081, 0x8c42, 0, 0x0f, 0xff, 2, 0x0000}, {0x0240, 0x0000, 1, 0x1f, 0x32, 1, 0x4402}, {0x2044, 0x0000, 0, 0x0f, 0xff, 1, 0x0000},
    {0x0290, 0x0000, 1, 0x0c, 0x28, 1, 0x4002}, {0xc840, 0x0000, 0, 0x0c, 0x0a, 1, 0x2c06}, {0x0240, 0x0000, 1, 0x0f, 0x37, 1, 0x6402},
    {0x0041, 0x4000, 0, 0x0f, 0x32, 1, 0x0000}, {0x0280, 0x0000, 2, 0x06, 0x41, 2, 0xc002}, {0xacb8, 0x0000, 1, 0x0f, 0xff, 1, 0x0c04},
    {0xacb8, 0x0000, 1, 0x0f, 0xff, 1, 0x4802}, {0x0290, 0x0000, 1, 0x0d, 0x32, 1, 0x4002}, {0x8081, 0x6000, 0, 0x0f, 0xff, 1, 0x0000},
    {0x0240, 0x0000, 1, 0x0a, 0xbe, 1, 0x6402}, {0x0880, 0x0000, 0, 0x1f, 0x28, 1, 0x0002}, {0x0045, 0x6000, 0, 0x0f, 0x37, 2, 0x0000},
    {0x0085, 0x4000, 0, 0x0f, 0xff, 2, 0x0000}, {0x8c78, 0x0000, 1, 0x0f, 0xff, 1, 0x6606}, {0xc840, 0x0000, 0, 0x0c, 0x0a, 1, 0x6d9e},
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

const CreatureInfo *creature_group_info(uint16_t group)
{
    return creature_info(draw_gdat(), dungeon_thing_record(group)[GROUP_CREATURE]);
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
    return active != 0xff && creature_active_group_floats(active);
}
