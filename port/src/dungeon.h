#ifndef DUNGEON_H
#define DUNGEON_H
#include <stdint.h>
#include <stdio.h>

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
#define THING_TYPE_SHIFT 10
#define THING_TYPE_MASK 0xf
#define thing_type(t) (((t) >> THING_TYPE_SHIFT) & THING_TYPE_MASK)
#define THING_INDEX_MASK 0x3ff
#define thing_index(t) ((t) & THING_INDEX_MASK)
#define THING_CELL_SHIFT 14

/* Byte offsets of thing record fields. Every record starts with the link to the next thing. */
enum thing_field {
    THING_NEXT = 0,
    TELEPORTER_TARGET = 2,      /* x bits 0-4, y bits 5-9, rotation bits 10-11 */
    TELEPORTER_MAP = 4,         /* target map in bits 8-15 */
    SENSOR_TYPE_DATA = 2,       /* sensor type bits 0-6, data bits 7-15 */
    GROUP_POSSESSIONS = 2,
    GROUP_CREATURE = 4,         /* byte: creature type */
    CONTAINER_CONTENTS = 2,
    CONTAINER_KIND = 4,         /* bits 1-2: container kind */
    PROJECTILE_PAYLOAD = 2,
    PROJECTILE_EVENT = 6,       /* timeline event that moves the projectile */
};

#define SENSOR_TYPE_MASK 0x7f
#define SENSOR_DATA_SHIFT 7
#define SENSOR_TELEPORTER_LINK 0x27

/* Loads a new game from DUNGEON.DAT; returns 1 on success, 0 on a read error or a compressed file. */
int dungeon_load(const char *path);
/* Reads a dungeon at the file position: DUNGEON.DAT (new_game 1, adds free records) or the image in a save file (new_game 0). */
int dungeon_read(FILE *f, int new_game);
/* Writes the dungeon image of a save file: the header with the counts of the loaded game, then the sections. */
int dungeon_write(FILE *f);
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
/* Entries of the square first-thing table, including the free entries of a new game. */
int dungeon_square_thing_count(void);
const uint16_t *dungeon_square_things(void);
int dungeon_thing_count(int type);
int dungeon_thing_size(int type);
uint8_t *dungeon_thing_record(uint16_t thing);
/* True when thing has a record in the loaded dungeon. */
int dungeon_thing_valid(uint16_t thing);
/* The 16-bit field of a thing record at byte offset field. */
uint16_t thing_word(uint16_t thing, int field);
void set_thing_word(uint16_t thing, int field, uint16_t value);
uint16_t dungeon_next_thing(uint16_t thing);
uint8_t *dungeon_square_byte(int map, int x, int y);
/* Appends thing at the end of a thing list. */
void dungeon_append_thing(uint16_t thing, uint16_t *list);
/* Appends thing at the end of the square's thing list; returns 0 when the first-thing table is full. */
int dungeon_link_thing_to_square(uint16_t thing, int map, int x, int y);
/* Removes thing from the square's thing list. */
void dungeon_unlink_thing_from_square(uint16_t thing, int map, int x, int y);
/* Takes a free record of type, cleared, without cell bits; THING_NONE when all records are in use. */
uint16_t dungeon_unused_thing(int type);
/* The target map of a teleporter square whose target square is a teleporter too, both with a link sensor; else -1. */
int dungeon_teleporter_link_map(int map, int x, int y);
int dungeon_text_word_count(void);
const uint16_t *dungeon_text_data(void);
/* The maps of a level, ended by LEVEL_LIST_END. */
const uint8_t *dungeon_level_maps(int level);

#endif
