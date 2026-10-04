#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dungeon.h"

#define HEADER_SIZE 0x2c
#define MAP_DESC_SIZE 16
#define COMPRESSED_SIGNATURE 0x8104
#define SQUARE_THING_SLACK 300
#define THING_COUNT_MAX 0x400
#define EXPLOSION_COUNT_MAX 0x300
#define SQUARE_HAS_THINGS 0x10
#define MAP_COUNT_MAX 255
#define MAP_LEVEL_MASK 0x3f
#define MAP_WIDTH_SHIFT 6
#define MAP_WIDTH_MASK 0x1f
#define MAP_HEIGHT_SHIFT 11

enum header_field {
    HEADER_MAP_DATA_SIZE = 2, HEADER_MAP_COUNT = 4, HEADER_TEXT_WORDS = 6, HEADER_PARTY_START = 8,
    HEADER_SQUARE_THINGS = 10, HEADER_THING_COUNTS = 12
};
enum map_desc_field { MAP_DATA_OFFSET = 0, MAP_SIZE_AND_LEVEL = 8 };

static const uint8_t thing_size[THING_TYPE_COUNT] = {4, 6, 4, 8, 16, 4, 4, 4, 4, 8, 4, 0, 0, 0, 8, 4};
/* Free records that a new game adds to each thing type, for things created during play. */
static const uint8_t thing_slack[THING_TYPE_COUNT] = {0, 0, 0, 0, 75, 100, 60, 0, 12, 5, 200, 0, 0, 0, 60, 50};

static struct {
    uint16_t party_start;
    int map_count;
    uint8_t *map_descs;
    uint16_t *map_first_column;
    uint16_t *column_first_thing;
    uint16_t *square_first_things;
    int text_word_count;
    uint16_t *text_data;
    int thing_count[THING_TYPE_COUNT];
    uint8_t *things[THING_TYPE_COUNT];
    uint8_t *map_data;
    uint8_t level_start[LEVEL_COUNT];
    uint8_t level_maps[LEVEL_COUNT + MAP_COUNT_MAX];
} dun;

static uint16_t le16(const uint8_t *p)
{
    return p[0] | p[1] << 8;
}

static void put_le16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = value >> 8;
}

static int read_bytes(FILE *f, void *dst, size_t size)
{
    return size == 0 || (dst && fread(dst, 1, size, f) == size);
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

/* Squares with things own consecutive entries of the first-thing table, counted per column. */
uint16_t dungeon_square_first_thing(int map, int x, int y)
{
    const uint8_t *column = map_column(map, x);
    if (!(column[y] & SQUARE_HAS_THINGS)) return THING_ENDOFLIST;
    int index = dun.column_first_thing[dun.map_first_column[map] + x];
    for (int i = 0; i < y; i++)
        if (column[i] & SQUARE_HAS_THINGS) index++;
    return dun.square_first_things[index];
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

/* Reads the records of one thing type and appends the free records of a new game. */
static int read_things(FILE *f, int type, int count_in_file)
{
    int count = count_in_file + thing_slack[type];
    int max = type == THING_EXPLOSION ? EXPLOSION_COUNT_MAX : THING_COUNT_MAX;
    if (count > max) count = max;
    if (count_in_file > count) return 0;
    dun.thing_count[type] = count;
    dun.things[type] = calloc(count, thing_size[type]);
    if (!read_bytes(f, dun.things[type], (size_t)thing_size[type] * count_in_file)) return 0;
    for (int i = count_in_file; i < count; i++)
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

/* Reads the DUNGEON.DAT sections in file order: header, maps, thing lists, text, things, squares. */
static int read_dungeon(FILE *f)
{
    uint8_t header[HEADER_SIZE];
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
    dun.column_first_thing = malloc(column_count * sizeof(uint16_t));
    if (!read_words(f, dun.column_first_thing, column_count)) return 0;

    int square_things = le16(header + HEADER_SQUARE_THINGS);
    dun.square_first_things = malloc((square_things + SQUARE_THING_SLACK) * sizeof(uint16_t));
    if (!read_words(f, dun.square_first_things, square_things)) return 0;
    for (int i = 0; i < SQUARE_THING_SLACK; i++) dun.square_first_things[square_things + i] = THING_NONE;

    dun.text_word_count = le16(header + HEADER_TEXT_WORDS);
    dun.text_data = malloc(dun.text_word_count * sizeof(uint16_t));
    if (!read_words(f, dun.text_data, dun.text_word_count)) return 0;

    for (int type = 0; type < THING_TYPE_COUNT; type++)
        if (!read_things(f, type, le16(header + HEADER_THING_COUNTS + 2 * type))) return 0;

    int map_data_size = le16(header + HEADER_MAP_DATA_SIZE);
    dun.map_data = malloc(map_data_size);
    if (!read_bytes(f, dun.map_data, map_data_size)) return 0;
    for (int map = 0; map < dun.map_count; map++)
        if (map_data_offset(map) + dungeon_map_width(map) * dungeon_map_height(map) > map_data_size) return 0;
    build_level_lists();
    return 1;
}

int dungeon_load(const char *path)
{
    dungeon_free();
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int ok = read_dungeon(f);
    fclose(f);
    if (!ok) dungeon_free();
    return ok;
}
