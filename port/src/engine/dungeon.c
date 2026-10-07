#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ai.h"
#include "draw.h"
#include "dungeon.h"
#include "le.h"
#include "move.h"
#include "random.h"
#include "text.h"

#define HEADER_SIZE 0x2c
#define MAP_DESC_SIZE 16
#define COMPRESSED_SIGNATURE 0x8104
#define SQUARE_THING_SLACK 300
#define THING_COUNT_MAX 0x400
#define EXPLOSION_COUNT_MAX 0x300
#define MAP_COUNT_MAX 255
#define MAP_WIDTH_SHIFT 6
#define MAP_WIDTH_MASK 0x1f
#define MAP_HEIGHT_SHIFT 11
#define MISC_RESERVED 3          /* free misc records that only the forced creation of a misc item may use */

enum header_field {
    HEADER_MAP_DATA_SIZE = 2, HEADER_MAP_COUNT = 4, HEADER_TEXT_WORDS = 6, HEADER_PARTY_START = 8,
    HEADER_SQUARE_THINGS = 10, HEADER_THING_COUNTS = 12
};
enum map_desc_field {
    MAP_DATA_OFFSET = 0, MAP_FLAGS = 2, MAP_OFFSET_X = 6, MAP_OFFSET_Y = 7, MAP_SIZE_AND_LEVEL = 8,
    MAP_ORNAMENT_COUNTS = 10, MAP_OBJECT_COUNTS = 12, MAP_GRAPHICS = 14
};
#define WALL_ORNAMENT_MASK 0xf
#define FLOOR_ORNAMENT_SHIFT 8
#define CREATURE_TYPE_SHIFT 4

/* The record size in bytes of each thing type. */
static const uint8_t thing_size[THING_TYPE_COUNT] = {4, 6, 4, 8, 16, 4, 4, 4, 4, 8, 4, 0, 0, 0, 8, 4};
/* Free records that a new game adds to each thing type, for things created during play. */
static const uint8_t thing_slack[THING_TYPE_COUNT] = {0, 0, 0, 0, 75, 100, 60, 0, 12, 5, 200, 0, 0, 0, 60, 50};

static struct {
    uint8_t header[HEADER_SIZE];  /* the file header with the counts of the loaded game */
    uint16_t party_start;
    int map_count;
    uint8_t *map_descs;
    uint16_t *map_first_column;
    uint16_t *column_first_thing;
    int column_count;
    uint16_t *square_first_things;
    int square_thing_count;
    int text_word_count;
    uint16_t *text_data;
    int thing_count[THING_TYPE_COUNT];
    uint8_t *things[THING_TYPE_COUNT];
    uint8_t *map_data;
    uint8_t level_start[LEVEL_COUNT];
    uint8_t level_maps[LEVEL_COUNT + MAP_COUNT_MAX];
} dun;

static int read_bytes(FILE *f, void *dst, size_t size)
{
    return size == 0 || (dst && fread(dst, 1, size, f) == size);
}

static int write_bytes(FILE *f, const void *src, size_t size)
{
    return size == 0 || fwrite(src, 1, size, f) == size;
}

/* Writes count words in little-endian order. */
static int write_words(FILE *f, const uint16_t *src, int count)
{
    for (int i = 0; i < count; i++) {
        uint8_t le[2];
        put_le16(le, src[i]);
        if (!write_bytes(f, le, 2)) return 0;
    }
    return 1;
}

/* Reads count little-endian words into dst. */
static int read_words(FILE *f, uint16_t *dst, int count)
{
    if (!read_bytes(f, dst, 2 * (size_t)count)) return 0;
    for (int i = 0; i < count; i++) dst[i] = le16((const uint8_t *)(dst + i));
    return 1;
}

static uint16_t map_size_and_level(int map)
{
    return le16(dun.map_descs + map * MAP_DESC_SIZE + MAP_SIZE_AND_LEVEL);
}

int dungeon_map_width(int map) { return (map_size_and_level(map) >> MAP_WIDTH_SHIFT & MAP_WIDTH_MASK) + 1; }
int dungeon_map_height(int map) { return (map_size_and_level(map) >> MAP_HEIGHT_SHIFT) + 1; }
int dungeon_map_level(int map) { return map_size_and_level(map) & MAP_LEVEL_MASK; }
int dungeon_map_count(void) { return dun.map_count; }
uint16_t dungeon_party_start(void) { return dun.party_start; }
int dungeon_square_thing_count(void) { return dun.square_thing_count; }
const uint16_t *dungeon_square_things(void) { return dun.square_first_things; }
int dungeon_thing_count(int type) { return dun.thing_count[type]; }
int dungeon_thing_size(int type) { return thing_size[type]; }
int dungeon_text_word_count(void) { return dun.text_word_count; }
const uint16_t *dungeon_text_data(void) { return dun.text_data; }
const uint8_t *dungeon_level_maps(int level) { return dun.level_maps + dun.level_start[level]; }

uint8_t *dungeon_thing_record(uint16_t thing)
{
    return dun.things[thing_type(thing)] + thing_size[thing_type(thing)] * thing_index(thing);
}

static int map_data_offset(int map)
{
    return le16(dun.map_descs + map * MAP_DESC_SIZE + MAP_DATA_OFFSET);
}

/* A map's squares are stored column by column: one column of height bytes for each x. */
static const uint8_t *map_column(int map, int x)
{
    return dun.map_data + map_data_offset(map) + x * dungeon_map_height(map);
}

uint8_t dungeon_square(int map, int x, int y)
{
    return map_column(map, x)[y];
}

uint8_t *dungeon_square_byte(int map, int x, int y)
{
    return dun.map_data + map_data_offset(map) + x * dungeon_map_height(map) + y;
}

/* Squares with things own consecutive entries of the first-thing table, counted per column; -1 for a square without things. */
static int square_thing_index(int map, int x, int y)
{
    if (x < 0 || x >= dungeon_map_width(map) || y < 0 || y >= dungeon_map_height(map)) return -1;
    const uint8_t *column = map_column(map, x);
    if (!(column[y] & SQUARE_HAS_THINGS)) return -1;
    int index = dun.column_first_thing[dun.map_first_column[map] + x];
    for (int i = 0; i < y; i++)
        if (column[i] & SQUARE_HAS_THINGS) index++;
    return index;
}

uint16_t dungeon_square_first_thing(int map, int x, int y)
{
    int index = square_thing_index(map, x, y);
    return index < 0 ? THING_ENDOFLIST : dun.square_first_things[index];
}

uint16_t dungeon_square_first_object(int map, int x, int y)
{
    uint16_t t = dungeon_square_first_thing(map, x, y);
    while (thing_type(t) <= THING_SENSOR) t = dungeon_next_thing(t);
    return t;
}

uint16_t thing_word(uint16_t thing, int field)
{
    return le16(dungeon_thing_record(thing) + field);
}

void set_thing_word(uint16_t thing, int field, uint16_t value)
{
    put_le16(dungeon_thing_record(thing) + field, value);
}

uint16_t dungeon_next_thing(uint16_t thing)
{
    return thing_word(thing, THING_NEXT);
}

void dungeon_unlink_thing_from_list(uint16_t thing, uint16_t *list)
{
    uint8_t *link = (uint8_t *)list;
    while (le16(link) != THING_ENDOFLIST && (le16(link) & THING_ID_MASK) != (thing & THING_ID_MASK))
        link = dungeon_thing_record(le16(link)) + THING_NEXT;
    if (le16(link) != THING_ENDOFLIST) put_le16(link, dungeon_next_thing(le16(link)));
    set_thing_word(thing, THING_NEXT, THING_ENDOFLIST);
}

void dungeon_save_record(uint16_t thing, uint8_t *buffer)
{
    memcpy(buffer, dungeon_thing_record(thing), dungeon_thing_size(thing_type(thing)));
}

void dungeon_restore_record(uint16_t thing, const uint8_t *buffer)
{
    memcpy(dungeon_thing_record(thing), buffer, dungeon_thing_size(thing_type(thing)));
}

void dungeon_append_thing(uint16_t thing, uint16_t *list)
{
    set_thing_word(thing, THING_NEXT, THING_ENDOFLIST);
    uint8_t *link = NULL;
    for (uint16_t t = *list; t != THING_ENDOFLIST; t = le16(link)) link = dungeon_thing_record(t) + THING_NEXT;
    if (link) put_le16(link, thing);
    else *list = thing;
}

/* Adds count to the first-thing index of each column after column x of map, in all later maps too. */
static void shift_later_columns(int map, int x, int count)
{
    for (int c = dun.map_first_column[map] + x + 1; c < dun.column_count; c++) dun.column_first_thing[c] += count;
}

int dungeon_link_thing_to_square(uint16_t thing, int map, int x, int y)
{
    set_thing_word(thing, THING_NEXT, THING_ENDOFLIST);
    uint16_t last = dungeon_square_first_thing(map, x, y);
    if (last != THING_ENDOFLIST) {
        while (dungeon_next_thing(last) != THING_ENDOFLIST) last = dungeon_next_thing(last);
        set_thing_word(last, THING_NEXT, thing);
        return 1;
    }
    uint16_t *table = dun.square_first_things;
    int count = dun.square_thing_count;
    if (table[count - 1] != THING_NONE) return 0;  /* the original stops with a system error */
    *dungeon_square_byte(map, x, y) |= SQUARE_HAS_THINGS;
    shift_later_columns(map, x, 1);
    int index = square_thing_index(map, x, y);
    memmove(table + index + 1, table + index, (count - index - 1) * sizeof *table);
    table[index] = thing;
    return 1;
}

void dungeon_unlink_thing_from_square(uint16_t thing, int map, int x, int y)
{
    int index = square_thing_index(map, x, y);
    if (index < 0) return;  /* fix: the original reads before the first-thing table */
    uint16_t *table = dun.square_first_things;
    int count = dun.square_thing_count;
    uint16_t id = thing & THING_ID_MASK;
    if (dungeon_next_thing(thing) == THING_ENDOFLIST && (table[index] & THING_ID_MASK) == id) {
        *dungeon_square_byte(map, x, y) &= ~SQUARE_HAS_THINGS;
        memmove(table + index, table + index + 1, (count - 1 - index) * sizeof *table);
        table[count - 1] = THING_NONE;
        shift_later_columns(map, x, -1);
    } else if ((table[index] & THING_ID_MASK) == id) {
        table[index] = dungeon_next_thing(thing);
    } else {
        uint16_t prev = table[index];
        while (dungeon_next_thing(prev) != THING_ENDOFLIST && (dungeon_next_thing(prev) & THING_ID_MASK) != id)
            prev = dungeon_next_thing(prev);
        if (dungeon_next_thing(prev) != THING_ENDOFLIST) set_thing_word(prev, THING_NEXT, dungeon_next_thing(thing));
    }
    set_thing_word(thing, THING_NEXT, THING_ENDOFLIST);
}

uint16_t dungeon_unused_thing(int type)
{
    int count = dun.thing_count[type & THING_TYPE_MASK] - (type == THING_MISC ? MISC_RESERVED : 0);
    type &= THING_TYPE_MASK;
    uint16_t thing = THING_NONE;
    for (int i = 0; i < count && thing == THING_NONE; i++)
        if (dungeon_next_thing(type << THING_TYPE_SHIFT | i) == THING_NONE) thing = type << THING_TYPE_SHIFT | i;
    if (thing == THING_NONE) thing = dun_get_discarded_thing(type, DISCARD_ANY_KIND);
    if (thing == THING_NONE) return THING_NONE;
    memset(dungeon_thing_record(thing), 0, thing_size[type]);
    set_thing_word(thing, THING_NEXT, THING_ENDOFLIST);
    if (type == THING_CONTAINER) set_thing_word(thing, CONTAINER_CONTENTS, THING_ENDOFLIST);
    return thing;
}

/* The side (1 to 4) of the link sensor of a teleporter square, 0 when the square is no teleporter with a link. */
static int teleporter_link_side(int map, int x, int y, uint16_t *teleporter)
{
    if (x < 0 || x >= dungeon_map_width(map) || y < 0 || y >= dungeon_map_height(map)) return 0;
    if (square_type(dungeon_square(map, x, y)) != SQUARE_TELEPORTER) return 0;
    *teleporter = dungeon_square_first_thing(map, x, y);
    if (*teleporter == THING_ENDOFLIST) return 0;  /* fix: the original reads a record of the end mark */
    for (uint16_t t = dungeon_next_thing(*teleporter); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        if (thing_type(t) == THING_SENSOR && sensor_type(t) == SENSOR_TELEPORTER_LINK) {
            int rotation = teleporter_rotation(*teleporter);
            return dir_opposite(rotation) + 1;
        }
    return 0;
}

int dungeon_teleporter_link(int map, int x, int y, TeleporterLink *link)
{
    uint16_t teleporter, other;
    int side = teleporter_link_side(map, x, y, &teleporter);
    if (!side) return 0;
    int target_map = teleporter_map(teleporter);
    if (target_map >= dun.map_count) return 0;  /* fix: the original uses a map outside the dungeon */
    int tx = teleporter_x(teleporter), ty = teleporter_y(teleporter);
    int target_side = teleporter_link_side(target_map, tx, ty, &other);
    if (!target_side) return 0;
    *link = (TeleporterLink){side - 1, target_side - 1, tx, ty, target_map};
    return 1;
}

int dungeon_teleporter_link_map(int map, int x, int y)
{
    TeleporterLink link;
    return dungeon_teleporter_link(map, x, y, &link) ? link.map : -1;
}

static const uint8_t *map_desc(int map) { return dun.map_descs + map * MAP_DESC_SIZE; }
int dungeon_map_offset_x(int map) { return map_desc(map)[MAP_OFFSET_X]; }
int dungeon_map_offset_y(int map) { return map_desc(map)[MAP_OFFSET_Y]; }
uint16_t dungeon_map_flags(int map) { return le16(map_desc(map) + MAP_FLAGS); }
uint16_t dungeon_map_ornament_counts(int map) { return le16(map_desc(map) + MAP_ORNAMENT_COUNTS); }
uint16_t dungeon_map_object_counts(int map) { return le16(map_desc(map) + MAP_OBJECT_COUNTS); }
uint16_t dungeon_map_graphics(int map) { return le16(map_desc(map) + MAP_GRAPHICS); }
uint16_t dungeon_ornament_seed(void) { return le16(dun.header); }

/* After the squares of a map: its creature types, then its wall, floor, and door ornaments. */
static const uint8_t *map_extra(int map)
{
    return dun.map_data + map_data_offset(map) + dungeon_map_width(map) * dungeon_map_height(map);
}

const uint8_t *dungeon_map_creature_types(int map)
{
    return map_extra(map);
}

const uint8_t *dungeon_map_wall_ornaments(int map)
{
    return map_extra(map) + (dungeon_map_object_counts(map) >> CREATURE_TYPE_SHIFT & WALL_ORNAMENT_MASK);
}

const uint8_t *dungeon_map_floor_ornaments(int map)
{
    return dungeon_map_wall_ornaments(map) + (dungeon_map_ornament_counts(map) & WALL_ORNAMENT_MASK);
}

const uint8_t *dungeon_map_door_ornaments(int map)
{
    return dungeon_map_floor_ornaments(map) + (dungeon_map_ornament_counts(map) >> FLOOR_ORNAMENT_SHIFT & WALL_ORNAMENT_MASK);
}

/* True for a closed teleporter square. */
static int teleporter_closed(int map, int x, int y)
{
    uint16_t t = dungeon_square_first_thing(map, x, y);
    return t != THING_ENDOFLIST && teleporter_is_closed(t);  /* fix: the original reads the end mark */
}

/* True for a square that the party can see into: not a wall, not unknown, not a closed teleporter. */
static int square_open(int map, int x, int y)
{
    int type = square_type(dungeon_square(map, x, y));
    if (type == SQUARE_TELEPORTER && teleporter_closed(map, x, y)) return 0;
    return type != SQUARE_WALL && type != SQUARE_UNKNOWN;
}

/* The edge wall beside an edge square at (ex, ey): face toward the map when that square is open, a plain wall
   when the square after it (step) is open, else outside. */
static uint8_t edge_wall(int map, int ex, int ey, int step_x, int step_y, int limit, uint8_t face)
{
    if (square_open(map, ex, ey)) return face;
    int along = step_x ? ex : ey;
    if (along > 0 && square_open(map, ex - step_x, ey - step_y)) return 0;
    if (along + 1 >= limit) return SQUARE_OUTSIDE;
    return square_open(map, ex + step_x, ey + step_y) ? 0 : SQUARE_OUTSIDE;
}

uint8_t dungeon_square_edge(int map, int x, int y)
{
    int w = dungeon_map_width(map), h = dungeon_map_height(map);
    int in_x = x >= 0 && x < w, in_y = y >= 0 && y < h;
    if (in_x && in_y) return dungeon_square(map, x, y);
    if (in_y) {
        if (x == -1) return edge_wall(map, 0, y, 0, 1, h, WALL_FACE(DIR_EAST));
        if (x == w) return edge_wall(map, w - 1, y, 0, 1, h, WALL_FACE(DIR_WEST));
        return SQUARE_OUTSIDE;
    }
    if (in_x) {
        if (y == -1) return edge_wall(map, x, 0, 1, 0, w, WALL_FACE(DIR_SOUTH));
        if (y == h) return edge_wall(map, x, h - 1, 1, 0, w, WALL_FACE(DIR_NORTH));
        return SQUARE_OUTSIDE;
    }
    int ex;
    if (x == -1) ex = 0;
    else if (x == w) ex = w - 1;
    else return SQUARE_OUTSIDE;
    if (y == -1 && square_open(map, ex, 0)) return 0;
    if (y != h) return SQUARE_OUTSIDE;
    return square_open(map, ex, h - 1) ? 0 : SQUARE_OUTSIDE;
}

int dungeon_location_on_map(int map, int level_delta, int *x, int *y)
{
    return dungeon_location_on_map_from(map, level_delta, x, y, NULL);
}

int dungeon_location_on_map_from(int map, int level_delta, int *x, int *y, int *next)
{
    int ax = *x + dungeon_map_offset_x(map), ay = *y + dungeon_map_offset_y(map);
    int level = dungeon_map_level(map) + level_delta;
    int start = next ? *next : 0;
    if (next) *next = 0;
    if (level < 0 || level >= LEVEL_COUNT) return -1;
    const uint8_t *maps = dungeon_level_maps(level);
    for (const uint8_t *m = maps + start; *m != LEVEL_LIST_END; m++) {
        int ox = dungeon_map_offset_x(*m), oy = dungeon_map_offset_y(*m);
        if (ax < ox - 1 || (unsigned)ax > (unsigned)(ox + dungeon_map_width(*m)) || ay < oy - 1
            || (unsigned)ay > (unsigned)(oy + dungeon_map_height(*m)))
            continue;
        int lx = ax - ox, ly = ay - oy;
        int type = square_type(dungeon_square_edge(*m, lx, ly));
        if (type == SQUARE_UNKNOWN || (type == SQUARE_TELEPORTER && teleporter_closed(*m, lx, ly))) continue;
        *x = lx;
        *y = ly;
        if (next) *next = (int)(m - maps) + 1;
        return *m;
    }
    return -1;
}

void dungeon_free(void)
{
    free(dun.map_descs);
    free(dun.map_first_column);
    free(dun.column_first_thing);
    free(dun.square_first_things);
    free(dun.text_data);
    free(dun.map_data);
    for (int type = 0; type < THING_TYPE_COUNT; type++) free(dun.things[type]);
    memset(&dun, 0, sizeof dun);
}

/* Reads the records of one thing type; a new game appends free records. */
static int read_things(FILE *f, int type, int count_in_file, int new_game)
{
    int count = count_in_file + (new_game ? thing_slack[type] : 0);
    int max = type == THING_EXPLOSION ? EXPLOSION_COUNT_MAX : THING_COUNT_MAX;
    if (count > max) count = max;
    if (count_in_file > count) return 0;  /* fix: the original reads past the capped buffer */
    if (count && !thing_size[type]) return 0;  /* fix: the original writes links into the records of an unused type */
    dun.thing_count[type] = count;
    put_le16(dun.header + HEADER_THING_COUNTS + 2 * type, count);
    dun.things[type] = calloc(count, thing_size[type]);
    if (!read_bytes(f, dun.things[type], (size_t)thing_size[type] * count_in_file)) return 0;
    for (int i = count_in_file; i < count; i++)  /* fix: the original writes free records past the cap */
        put_le16(dun.things[type] + i * thing_size[type], THING_NONE);  /* a free record has THING_NONE as link */
    return 1;
}

/* Lists the maps of each level in map order, each list ended by LEVEL_LIST_END. */
static void build_level_lists(void)
{
    int n = 0;
    for (int level = 0; level < LEVEL_COUNT; level++) {
        dun.level_start[level] = n;
        for (int map = 0; map < dun.map_count; map++)
            if (dungeon_map_level(map) == level) dun.level_maps[n++] = map;
        dun.level_maps[n++] = LEVEL_LIST_END;
    }
}

int dungeon_thing_valid(uint16_t thing)
{
    return thing_index(thing) < dun.thing_count[thing_type(thing)];
}

/* Checks that a thing list has only existing things and ends. */
static int thing_list_valid(uint16_t thing)
{
    for (int things = 0; thing != THING_ENDOFLIST; thing = dungeon_next_thing(thing))
        if (!dungeon_thing_valid(thing) || ++things > THING_TYPE_COUNT * THING_COUNT_MAX) return 0;  /* bad thing or loop */
    return 1;
}

/* Checks that the squares with things own the first entries of the first-thing table in column order,
   and that each has a valid thing list. */
static int square_things_valid(void)
{
    int index = 0;
    for (int map = 0; map < dun.map_count; map++)
        for (int x = 0; x < dungeon_map_width(map); x++) {
            if (dun.column_first_thing[dun.map_first_column[map] + x] != index) return 0;
            for (int y = 0; y < dungeon_map_height(map); y++) {
                if (!(map_column(map, x)[y] & SQUARE_HAS_THINGS)) continue;
                if (index >= dun.square_thing_count || !thing_list_valid(dun.square_first_things[index++])) return 0;
            }
        }
    return 1;
}

/* Reads the DUNGEON.DAT sections in file order: header, maps, thing lists, text, things, squares. */
static int read_dungeon(FILE *f, int new_game)
{
    uint8_t *header = dun.header;
    if (!read_bytes(f, header, HEADER_SIZE) || le16(header) == COMPRESSED_SIGNATURE) return 0;
    dun.party_start = le16(header + HEADER_PARTY_START);
    dun.map_count = header[HEADER_MAP_COUNT];
    dun.map_descs = malloc(dun.map_count * MAP_DESC_SIZE);
    if (!read_bytes(f, dun.map_descs, dun.map_count * MAP_DESC_SIZE)) return 0;

    dun.map_first_column = malloc(dun.map_count * sizeof(uint16_t));
    int column_count = 0;
    for (int map = 0; map < dun.map_count; map++) {
        dun.map_first_column[map] = column_count;
        column_count += dungeon_map_width(map);
    }
    dun.column_count = column_count;
    dun.column_first_thing = malloc(column_count * sizeof(uint16_t));
    if (!read_words(f, dun.column_first_thing, column_count)) return 0;

    int square_things = le16(header + HEADER_SQUARE_THINGS);
    int slack = new_game ? SQUARE_THING_SLACK : 0;
    if (square_things + slack == 0) return 0;
    dun.square_first_things = malloc((square_things + slack) * sizeof(uint16_t));
    if (!read_words(f, dun.square_first_things, square_things)) return 0;
    for (int i = 0; i < slack; i++) dun.square_first_things[square_things + i] = THING_NONE;
    dun.square_thing_count = square_things + slack;
    put_le16(header + HEADER_SQUARE_THINGS, dun.square_thing_count);

    dun.text_word_count = le16(header + HEADER_TEXT_WORDS);
    dun.text_data = malloc(dun.text_word_count * sizeof(uint16_t));
    if (!read_words(f, dun.text_data, dun.text_word_count)) return 0;

    for (int type = 0; type < THING_TYPE_COUNT; type++)
        if (!read_things(f, type, le16(header + HEADER_THING_COUNTS + 2 * type), new_game)) return 0;

    int map_data_size = le16(header + HEADER_MAP_DATA_SIZE);
    dun.map_data = malloc(map_data_size);
    if (!read_bytes(f, dun.map_data, map_data_size)) return 0;
    for (int map = 0; map < dun.map_count; map++)
        if (map_data_offset(map) + dungeon_map_width(map) * dungeon_map_height(map) > map_data_size) return 0;
    if (!square_things_valid()) return 0;
    build_level_lists();
    return 1;
}

int dungeon_read(FILE *f, int new_game)
{
    dungeon_free();
    int ok = read_dungeon(f, new_game);
    if (!ok) dungeon_free();
    return ok;
}

int dungeon_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        dungeon_free();
        return 0;
    }
    int ok = dungeon_read(f, 1);
    fclose(f);
    return ok;
}

int dungeon_write(FILE *f)
{
    if (!write_bytes(f, dun.header, HEADER_SIZE) || !write_bytes(f, dun.map_descs, dun.map_count * MAP_DESC_SIZE)
        || !write_words(f, dun.column_first_thing, dun.column_count)
        || !write_words(f, dun.square_first_things, dun.square_thing_count)
        || !write_words(f, dun.text_data, dun.text_word_count))
        return 0;
    for (int type = 0; type < THING_TYPE_COUNT; type++)
        if (!write_bytes(f, dun.things[type], (size_t)thing_size[type] * dun.thing_count[type])) return 0;
    return write_bytes(f, dun.map_data, le16(dun.header + HEADER_MAP_DATA_SIZE));
}

/* Finds where the stairs, open pit, or level link of a square lead one level up or down. */
int dun_get_level_transfer_destination(int16_t *x, int16_t *y, int map, int level_delta, unsigned flags)
{
    int fall_into = 0;
    dun_set_current_map(map);
    uint8_t square = dun_get_square(*x, *y);
    int type = square_type(square);
    if (type == SQUARE_STAIRS) {
        if (!(flags & TRANSFER_STAIRS)) return -1;
        if (square & SQUARE_STAIRS_UP ? level_delta != LEVEL_UP : level_delta != LEVEL_DOWN) return -1;
    } else if (!(type == SQUARE_PIT && flags & TRANSFER_PIT && level_delta == LEVEL_DOWN && square & SQUARE_PIT_OPEN
                 && !(square & SQUARE_FLAG_0))) {
        if (!(square & SQUARE_LEVEL_LINK) || type == SQUARE_WALL || type == SQUARE_UNKNOWN || type == SQUARE_DOOR) return -1;
        uint16_t sensor;
        if (!(flags & TRANSFER_STAIRS) || dun_find_adjacent_level_link(*x, *y, level_delta, &sensor) < 0) {
            if (!(flags & TRANSFER_FALL_INTO) || level_delta != LEVEL_UP) return -1;
            fall_into = 1;
        }
    }
    int lx = *x, ly = *y;
    int found = dungeon_location_on_map(map, level_delta, &lx, &ly);
    *x = (int16_t)lx;
    *y = (int16_t)ly;
    if (found >= 0 && fall_into) {
        /* the square above must be an open pit that leads here */
        dun_set_current_map(found);
        square = dun_get_square(*x, *y);
        if (square_type(square) != SQUARE_PIT || !(square & SQUARE_PIT_OPEN) || square & SQUARE_FLAG_0) found = -1;
        dun_set_current_map(map);
    }
    return found;
}

/* ---- The passes after the load of DUNGEON.DAT ---- */

enum text_ornament_subtype { TEXT_LEVEL_LINK_FIRST = 6, TEXT_LEVEL_LINK_LAST = 7, TEXT_SHOWN_AT_START = 8 };
enum link_sensor_type {
    SENSOR_LEVEL_LINK = 0x1c, SENSOR_ANIMATED_ORNAMENT = 0x2c, SENSOR_STARTS_ON = 0x3f, SENSOR_DOOR_FLAG = 0x46
};
#define WALLSET_PIT_TO_MARKER 0x6a      /* wall set value: its pits lead to a marker, not to the square below */
#define LEVEL_TEXT_SIZE 200
#define LEVEL_TEXT_FILL '1'             /* the level digit of the levels that the start text does not name */
#define LEVEL_TEXT_FILLED 0x40
#define LEVEL_TEXT_MODE 0x8002          /* text decode mode: a hidden text too */

/* Marks (x, y) of map as a square with a link to another level. */
static void mark_level_link(int x, int y, int map)
{
    *dungeon_square_byte(map, x, y) |= SQUARE_LEVEL_LINK;
}

/* Numbers the teleporter link sensors (0x27) after thing in its list that have no number yet with map + 1;
   returns 1 when the list has a link sensor. */
static int number_linked_actuators(uint16_t thing, int map, int *numbered)
{
    int found = 0;
    for (thing = dungeon_next_thing(thing); thing != THING_ENDOFLIST; thing = dungeon_next_thing(thing)) {
        if (thing_type(thing) != THING_SENSOR) continue;
        if (sensor_type(thing) != SENSOR_TELEPORTER_LINK) continue;
        found = 1;
        if (sensor_data(thing)) continue;
        sensor_set_data(thing, map + 1);
        ++*numbered;
    }
    return found;
}

/* Puts the fixed things (doors, teleporters, texts, sensors) at the head of list b in front of list a, in reverse
   order, and appends the first movable thing of b; the movable things after it drop out, as in the original. */
static uint16_t merge_object_lists(uint16_t a, uint16_t b)
{
    if (a == THING_ENDOFLIST) return b;
    if (b == THING_ENDOFLIST) return a;
    while (b != THING_ENDOFLIST && thing_type(b) <= THING_SENSOR) {
        uint16_t next = dungeon_next_thing(b);
        set_thing_word(b, THING_NEXT, a);
        a = b;
        b = next;
    }
    uint16_t list = a;
    if (b != THING_ENDOFLIST) dungeon_append_thing(b, &list);
    return a;
}

/* A teleporter whose target is a teleporter back to this square: both get numbered link sensors; with two
   numbered sensors both squares share one thing list. Returns 0 when the rest of the square is skipped. */
static int link_teleporters(uint16_t teleporter, int x, int y, int map)
{
    int target_map = teleporter_map(teleporter);
    dun_set_current_map(target_map);
    uint16_t other = dungeon_square_first_thing(target_map, teleporter_x(teleporter), teleporter_y(teleporter));
    if (other == THING_ENDOFLIST || thing_type(other) != THING_TELEPORTER) return 1;
    if (teleporter_x(other) != x || teleporter_y(other) != y || teleporter_map(other) != map)
        return 1;
    int numbered = 0;
    int found = number_linked_actuators(other, target_map, &numbered);
    found |= number_linked_actuators(teleporter, map, &numbered);
    if (numbered == 2) {
        uint16_t merged = merge_object_lists(dungeon_next_thing(teleporter), dungeon_next_thing(other));
        set_thing_word(other, THING_NEXT, merged);
        set_thing_word(teleporter, THING_NEXT, merged);
        return 1;
    }
    return numbered || !found;
}

/* A creature group gets its full health; a moving one its home square, a static one random cells for its
   possessions. */
static void prepare_group(uint16_t group, int x, int y, int map)
{
    const CreatureInfo *info = creature_info(draw_gdat(), group_creature(group));
    set_thing_word(group, GROUP_HEALTH, info->max_health);
    group_set_mood(group, 0);
    if (!(info->flags & CREATURE_STATIC)) {
        set_thing_word(group, GROUP_HOME, (uint16_t)location_pack(x, y, map));
        return;
    }
    set_thing_word(group, GROUP_ANIM_SEQUENCE, 0);
    if (group_flag(group, GROUP_KEEP_CELLS)) return;
    uint16_t t = thing_word(group, GROUP_POSSESSIONS);
    if (t == THING_ENDOFLIST) return;
    set_thing_word(group, GROUP_POSSESSIONS, (uint16_t)thing_with_cell(t, rand_2bits()));
    for (; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        uint16_t next = dungeon_next_thing(t);
        if (next != THING_ENDOFLIST)
            set_thing_word(t, THING_NEXT, (uint16_t)thing_with_cell(next, rand_2bits()));
    }
}

/* The sensor 0x46 on (x, y) of map copies its state into a flag of the door at its target. */
static void link_door_flag(uint16_t sensor, int map)
{
    dun_set_current_map(map);
    int tx = sensor_target_x(sensor), ty = sensor_target_y(sensor);
    if (square_type(dun_get_square(tx, ty)) != SQUARE_DOOR) return;
    uint16_t door = dungeon_square_first_thing(map, tx, ty);
    door_set_flag(door, DOOR_SWITCH_ON, sensor_flag(sensor, SENSOR_STATE));
}

/* The link pass of a thing on (x, y) of map; returns 0 when the rest of the square is skipped. */
static int link_thing(uint16_t t, int x, int y, int map)
{
    int cell = thing_cell(t);
    switch (thing_type(t)) {
    case THING_TELEPORTER:
        return link_teleporters(t, x, y, map);
    case THING_TEXT: {
        if (text_kind(t) != TEXT_KIND_ORNAMENT) break;
        int subtype = text_subtype(t);
        if (subtype >= TEXT_LEVEL_LINK_FIRST && subtype <= TEXT_LEVEL_LINK_LAST)
            mark_level_link(x + dir_dx[cell], y + dir_dy[cell], map);
        else if (subtype == TEXT_SHOWN_AT_START)
            text_set_visible(t, true);
        break;
    }
    case THING_SENSOR: {
        switch (sensor_type(t)) {
        case SENSOR_LEVEL_LINK:
            mark_level_link(x + dir_dx[cell], y + dir_dy[cell], map);
            break;
        case SENSOR_TELEPORTER_LINK: case SENSOR_ANIMATED_ORNAMENT:
            if (sensor_flag(t, SENSOR_STATE)) sensor_set_flag(t, SENSOR_BUSY, true);
            break;
        case SENSOR_STARTS_ON:
            sensor_set_flag(t, SENSOR_BUSY, false);
            sensor_set_flag(t, SENSOR_STATE, true);
            break;
        case SENSOR_DOOR_FLAG:
            link_door_flag(t, map);
            break;
        }
        break;
    }
    case THING_GROUP:
        prepare_group(t, x, y, map);
        break;
    }
    return 1;
}

void dungeon_link_things_after_load(void)
{
    for (int map = 0; map < dun.map_count; map++)
        for (int x = 0; x < dungeon_map_width(map); x++)
            for (int y = 0; y < dungeon_map_height(map); y++) {
                uint8_t square = dungeon_square(map, x, y);
                if (square & SQUARE_HAS_THINGS)
                    for (uint16_t t = dungeon_square_first_thing(map, x, y); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
                        if (!link_thing(t, x, y, map)) break;
                if (square_type(square) != SQUARE_PIT
                    || gdat_entry_index(draw_gdat(), CAT_WALLSET, dungeon_map_wallset(map), GDAT_TYPE_VALUE,
                                        WALLSET_PIT_TO_MARKER))
                    continue;
                int bx = x, by = y;
                int below = dungeon_location_on_map(map, LEVEL_DOWN, &bx, &by);
                if (below >= 0) mark_level_link(bx, by, below);  /* fix: the original marks a square of map -1 */
            }
}

static uint8_t level_digits[LEVEL_COUNT];

void dungeon_read_level_table(void)
{
    char text[LEVEL_TEXT_SIZE] = {0};
    memset(text, LEVEL_TEXT_FILL, LEVEL_TEXT_FILLED);
    int last = 0;
    for (int map = 0; map < dun.map_count; map++)
        if (dungeon_map_level(map) > last) last = dungeon_map_level(map);
    dun_set_current_map(0);
    uint16_t t = dungeon_square_first_thing(0, location_x(dun.party_start), location_y(dun.party_start));
    while (t != THING_ENDOFLIST && thing_type(t) != THING_TEXT) t = dungeon_next_thing(t);
    if (t != THING_ENDOFLIST && text_kind(t) == TEXT_KIND_TEXT)
        text_decode_dungeon(text, sizeof text, t, LEVEL_TEXT_MODE);
    const char *p = text;
    for (int level = 0; level <= last; level++) {
        while (p < text + sizeof text - 1 && (*p == '\0' || *p == '\n' || *p == ' ')) p++;
        level_digits[level] = (uint8_t)(*p++ - '0');
    }
}

int dungeon_level_digit(int level)
{
    return level >= 0 && level < LEVEL_COUNT ? level_digits[level] : 0;
}
