#ifndef DUNGEON_H
#define DUNGEON_H
#include <stdint.h>
#include <stdio.h>

enum thing_type {
    THING_DOOR, THING_TELEPORTER, THING_TEXT, THING_SENSOR, THING_GROUP, THING_WEAPON, THING_ARMOUR,
    THING_SCROLL, THING_POTION, THING_CONTAINER, THING_MISC, THING_PROJECTILE = 14, THING_EXPLOSION,
    THING_TYPE_COUNT
};

/* Square byte: square type in bits 5-7; the other bits depend on the type. */
#define SQUARE_TYPE_SHIFT 5
#define square_type(s) ((s) >> SQUARE_TYPE_SHIFT)
enum square_type {
    SQUARE_WALL, SQUARE_FLOOR, SQUARE_PIT, SQUARE_STAIRS, SQUARE_DOOR, SQUARE_TELEPORTER, SQUARE_FAKE_WALL,
    SQUARE_UNKNOWN
};
#define SQUARE_OUTSIDE 0xe0     /* dungeon_square_edge: a square away from the map */
/* Square bits 0-3 by square type. */
#define SQUARE_PIT_OPEN 0x08
#define SQUARE_PIT_VARIANT 0x04         /* selects the second pit picture */
#define SQUARE_STAIRS_ALONG_X 0x08      /* the stairs run east to west */
#define SQUARE_STAIRS_UP 0x04
#define SQUARE_DOOR_ALONG_X 0x08        /* the door faces east and west */
#define SQUARE_DOOR_STATE 0x07          /* 0 open, 1-3 half open, 4 closed, 5 broken */
#define SQUARE_TELEPORTER_ON 0x08
#define SQUARE_TELEPORTER_VISIBLE 0x04
#define SQUARE_FAKE_WALL_OPEN 0x04
#define SQUARE_FLAG_3 0x08              /* bit 3 of any square: one floor text ornament shows it */
#define SQUARE_LEVEL_LINK 0x02          /* a square with a link to another level */
#define SQUARE_FLAG_0 0x01              /* a fake wall that blinks with MAP_OPTION_DETAILS; a pit that is not a way down */
#define SQUARE_HAS_THINGS 0x10          /* bit 4 of any square: things lie on it */
#define square_flag(s, bit) (((s) & (bit)) != 0)
/* Bits 0-3 of a wall: the faces that show a random ornament, by the direction the face looks to. */
#define WALL_FACE(dir) (8 >> (dir))

enum direction { DIR_NORTH, DIR_EAST, DIR_SOUTH, DIR_WEST, DIR_COUNT };

#define THING_NONE 0xffff
#define THING_ENDOFLIST 0xfffe
#define LEVEL_COUNT 63
#define LEVEL_LIST_END 0xff
#define MAP_LEVEL_MASK 0x3f             /* the dungeon level in the map size word */

/* A thing id: record index in bits 0-9, thing type in bits 10-13, cell in bits 14-15. */
#define THING_TYPE_SHIFT 10
#define THING_TYPE_MASK 0xf
#define thing_type(t) (((t) >> THING_TYPE_SHIFT) & THING_TYPE_MASK)
#define THING_INDEX_MASK 0x3ff
#define thing_index(t) ((t) & THING_INDEX_MASK)
#define THING_CELL_SHIFT 14
#define THING_ID_MASK 0x3fff            /* a thing id without its cell */
#define CELL_ANY 0xff                   /* a cell argument: any cell */

/* A location word: x bits 0-4, y bits 5-9, map bits 10-15. */
#define LOCATION_COORD_MASK 0x1f
#define LOCATION_Y_SHIFT 5
#define LOCATION_MAP_SHIFT 10
#define LOCATION_MAP_MASK 0x3f
#define LOCATION_MAP_BITS 0xfc00
#define location_x(w) ((w) & LOCATION_COORD_MASK)
#define location_y(w) ((w) >> LOCATION_Y_SHIFT & LOCATION_COORD_MASK)
#define location_map(w) ((w) >> LOCATION_MAP_SHIFT)
#define location_pack(x, y, map) \
    (((x) & LOCATION_COORD_MASK) | ((y) & LOCATION_COORD_MASK) << LOCATION_Y_SHIFT | ((map) & LOCATION_MAP_MASK) << LOCATION_MAP_SHIFT)

/* Byte offsets of thing record fields. Every record starts with the link to the next thing. */
enum thing_field {
    THING_NEXT = 0,
    TELEPORTER_TARGET = 2,      /* x bits 0-4, y bits 5-9, rotation bits 10-11 */
    TELEPORTER_MAP = 4,         /* target map in bits 8-15, bit 0 closed */
    TEXT_DATA = 2,              /* bit 0 visible, bits 1-2 kind, bits 3-10 ornament, bits 11-15 subtype */
    DOOR_DATA = 2,              /* bit 0 door type, bits 1-4 ornament ordinal, bit 5 no animation, bit 6 button */
    SENSOR_TYPE_DATA = 2,       /* sensor type bits 0-6, data bits 7-15 */
    SENSOR_FLAGS = 4,           /* bit 0 active, bit 2 state, bits 12-15 ornament ordinal */
    GROUP_POSSESSIONS = 2,
    GROUP_CREATURE = 4,         /* byte: creature type */
    GROUP_ACTIVE = 5,           /* byte: active group index, 0xff none */
    GROUP_ANIM_SEQUENCE = 8,    /* static creatures: first frame of the animation sequence */
    GROUP_FLAGS = 0x0a,         /* byte: bit 1 seen by the party */
    GROUP_ANIM_STATE = 0x0a,    /* static creatures: animation state word */
    GROUP_HOME = 0x0c,          /* home square: x bits 0-4, y bits 5-9, map bits 10-15 */
    GROUP_DIR = 0x0e,           /* bits 8-9: direction the creature faces */
    CONTAINER_CONTENTS = 2,
    CONTAINER_KIND = 4,         /* bit 0 open, bits 1-2 container kind, bits 13-15 subtype */
    PROJECTILE_PAYLOAD = 2,
    PROJECTILE_POWER = 4,       /* byte: power of a spell, 0xff for none */
    PROJECTILE_EVENT = 6,       /* timeline event that moves the projectile */
    EXPLOSION_DATA = 2,         /* explosion type bits 0-6, bit 7 centered on the square, power bits 8-15 */
};
#define GROUP_SEEN 0x02
#define GROUP_DIR_SHIFT 8
#define CONTAINER_OPEN 0x0001
#define EXPLOSION_TYPE_MASK 0x7f
#define EXPLOSION_CENTERED 0x0080
#define EXPLOSION_POWER_SHIFT 8

#define TELEPORTER_CLOSED 0x0001
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
/* The first thing on the square after its doors, teleporters, texts, and sensors. */
uint16_t dungeon_square_first_object(int map, int x, int y);
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
/* Removes thing from the thing list whose first-thing word is at field (a record field). */
void dungeon_unlink_thing_from_list(uint16_t thing, uint8_t *field);
/* Appends thing at the end of a thing list. */
void dungeon_append_thing(uint16_t thing, uint16_t *list);
/* Appends thing at the end of the square's thing list; returns 0 when the first-thing table is full. */
int dungeon_link_thing_to_square(uint16_t thing, int map, int x, int y);
/* Removes thing from the square's thing list. */
void dungeon_unlink_thing_from_square(uint16_t thing, int map, int x, int y);
/* Takes a free record of type, cleared, without cell bits; THING_NONE when all records are in use. */
uint16_t dungeon_unused_thing(int type);
/* Frees a record of type for reuse when the table is full: removes a thing of kind subtype (DISCARD_ANY_KIND:
   any) that may go, away from the party; THING_NONE when there is none. */
#define DISCARD_ANY_KIND 0xff
/* The map where the last search for each thing type ended; the next search starts there. */
extern uint8_t last_discarded_map[THING_TYPE_COUNT];
uint16_t dun_get_discarded_thing(int type, int subtype);
/* The target map of a teleporter square whose target square is a teleporter too, both with a link sensor; else -1. */
int dungeon_teleporter_link_map(int map, int x, int y);
/* The square byte of map at (x, y). Away from the map it gives the edge walls: a wall (with the bit of the
   side that faces the map) next to an open square at the map edge, else SQUARE_OUTSIDE. */
uint8_t dungeon_square_edge(int map, int x, int y);
/* The map of the level at level_delta from the level of map that holds the dungeon position of (x, y) of map, and
   its square there in *x, *y; -1 when no such map has an open square there. */
int dungeon_location_on_map(int map, int level_delta, int *x, int *y);
/* The same search from list position *next (0: the first map of the level); sets *next to the position after the
   map that it found, 0 when it found none. */
int dungeon_location_on_map_from(int map, int level_delta, int *x, int *y, int *next);
#define LEVEL_UP (-1)
#define LEVEL_DOWN 1
/* Level transfer flags. */
#define TRANSFER_PIT 0x008
#define TRANSFER_FALL_INTO 0x010
#define TRANSFER_STAIRS 0x100
/* The map one level up (level_delta -1) or down (1) that the square (*x, *y) of map leads to by its stairs, its open
   pit (TRANSFER_PIT), or a level link; *x, *y become the square there. -1 none. Leaves map current. */
int dun_get_level_transfer_destination(int16_t *x, int16_t *y, int map, int level_delta, unsigned flags);

/* A pair of teleporters with link sensors: a seamless passage to another map. */
typedef struct {
    int side, target_side;  /* side of the passage on the source and on the target square, 0 to 3 */
    int x, y, map;          /* the target square */
} TeleporterLink;
/* Fills link for a teleporter square whose target is a teleporter, both with link sensors; returns 0 when not. */
int dungeon_teleporter_link(int map, int x, int y, TeleporterLink *link);

/* Map descriptor values. */
int dungeon_map_offset_x(int map);              /* position of the map on its level */
int dungeon_map_offset_y(int map);
uint16_t dungeon_map_flags(int map);            /* bit 7 door type 0 present, bit 8 door type 1 present */
uint16_t dungeon_map_ornament_counts(int map);  /* wall bits 0-3, random wall 4-7, floor 8-11, random floor 12-15 */
uint16_t dungeon_map_object_counts(int map);    /* door ornaments bits 0-3, creature types bits 4-7 */
uint16_t dungeon_map_graphics(int map);         /* graphics set bits 4-7, door types bits 8-11 and 12-15 */
#define MAP_HAS_DOOR_TYPE_0 0x0080
#define MAP_HAS_DOOR_TYPE_1 0x0100
#define MAP_WALLSET_SHIFT 4
#define MAP_DOOR_TYPE_SHIFT 8       /* door type 0; door type 1 follows 4 bits higher */
#define MAP_DOOR_TYPE_1_SHIFT 12
#define MAP_NIBBLE_MASK 0xf
#define DOOR_TYPE_NONE 0xff
/* The wall set (graphics set) of map. */
#define dungeon_map_wallset(map) (dungeon_map_graphics(map) >> MAP_WALLSET_SHIFT & MAP_NIBBLE_MASK)
/* The creature types of map (count in dungeon_map_object_counts bits 4-7). */
const uint8_t *dungeon_map_creature_types(int map);
/* The ornament lists of map: GDAT indices of its wall, floor, and door ornaments. */
const uint8_t *dungeon_map_wall_ornaments(int map);
const uint8_t *dungeon_map_floor_ornaments(int map);
const uint8_t *dungeon_map_door_ornaments(int map);
/* The first word of the dungeon header; it seeds the random wall ornaments. */
uint16_t dungeon_ornament_seed(void);

/* The pass of a new game after the load of DUNGEON.DAT: numbered teleporter links, shared lists of linked
   teleporters, level link marks, the start states of sensors and texts, creature health and homes. */
void dungeon_link_things_after_load(void);
/* Reads the level digits from the text on the party start square (the first load of a run). */
void dungeon_read_level_table(void);
/* The level digit of level: the map icon set of the squares outside the maps of the level. */
int dungeon_level_digit(int level);

int dungeon_text_word_count(void);
const uint16_t *dungeon_text_data(void);
/* The maps of a level, ended by LEVEL_LIST_END. */
const uint8_t *dungeon_level_maps(int level);

#endif
