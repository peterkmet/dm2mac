#ifndef DUNGEON_H
#define DUNGEON_H
#include <stdint.h>

enum thing_type {
    THING_DOOR, THING_TELEPORTER, THING_TEXT, THING_SENSOR, THING_GROUP, THING_WEAPON, THING_ARMOUR,
    THING_SCROLL, THING_POTION, THING_CONTAINER, THING_MISC, THING_PROJECTILE = 14, THING_EXPLOSION,
    THING_TYPE_COUNT
};

#define THING_NONE 0xffff
#define THING_ENDOFLIST 0xfffe
#define LEVEL_COUNT 63
#define LEVEL_LIST_END 0xff

/* A thing id: record index in bits 0-9, thing type in bits 10-13, cell in bits 14-15. */
#define thing_type(t) (((t) >> 10) & 0xf)
#define THING_INDEX_MASK 0x3ff
#define thing_index(t) ((t) & THING_INDEX_MASK)

/* Loads a new game from DUNGEON.DAT; returns 1 on success, 0 on a read error or a compressed file. */
int dungeon_load(const char *path);
void dungeon_free(void);

uint16_t dungeon_party_start(void);
int dungeon_map_count(void);
int dungeon_map_width(int map);
int dungeon_map_height(int map);
int dungeon_map_level(int map);
/* The square byte at (x, y); x and y must be inside the map. */
uint8_t dungeon_square(int map, int x, int y);
/* The first thing on the square (x, y), THING_ENDOFLIST when the square has no things. */
uint16_t dungeon_square_first_thing(int map, int x, int y);
int dungeon_thing_count(int type);
int dungeon_thing_size(int type);
uint8_t *dungeon_thing_record(uint16_t thing);
int dungeon_text_word_count(void);
const uint16_t *dungeon_text_data(void);
/* The maps of a level, ended by LEVEL_LIST_END. */
const uint8_t *dungeon_level_maps(int level);

#endif
