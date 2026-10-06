#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "advanced.h"
#include "automap.h"
#include "champion.h"
#include "creature.h"
#include "draw.h"
#include "dungeon.h"
#include "inventory.h"
#include "random.h"
#include "save.h"
#include "sensor.h"
#include "timeline.h"
#include "weather.h"

/* Save file: a 42-byte header, the dungeon image, then the game state as one bit stream. Each record
   of the bit stream stores only the bits that its mask table selects, most significant bit first.
   Format analysis: reDM2 docs/savegame.md and docs/skull-formats.md section 7. In the advanced mode the AMAP block
   of the automap follows the bit stream (automap.h); the original loader ignores it. */

#define HEADER_SIZE 0x2a
#define HEADER_FORMAT 0
#define HEADER_NAME 2
#define SAVE_FORMAT 1
#define GAME_RECORD_SIZE 60
#define EVENT_SIZE 12
#define CHAMPION_SLOTS 0xc3   /* 30 thing words: hands, body, pouches, quiver, backpack */
#define CHAMPION_DAMAGE_EVENT 0x2e  /* the timeline event that ends the damage display */
#define SLOT_COUNT 30
#define DEFERRED_MAX 100
#define EVENT_SLACK 50        /* timeline events beyond one for each group, projectile, and explosion record */

/* Bit widths of the values between the records, as masks. */
#define MORE_MASK 0x1
#define TYPE_MASK 0xf
#define CELL_MASK 0x3
#define CREATURE_MASK 0x7f
#define CONTAINER_KIND_MASK 0x3
#define SPELL_MASK 0x7f
#define SENSOR_DATA_MASK 0x1ff
#define INDEX_MASK 0x3ff
#define SPELL_THING_BASE 0xff80   /* a projectile payload that is a spell, not a thing record */

/* Square bits that change during play and that the save stores, by square type. */
#define PIT_SAVED_BITS 0x08
#define DOOR_SAVED_BITS 0x07
#define FAKE_WALL_SAVED_BITS 0x04
#define TELEPORTER_SAVED_BITS 0x08   /* only for a teleporter without a linked teleporter */

#define CONTAINER_KIND_SHIFT 1
#define CONTAINER_ITEM_SHIFT 13
enum container_kind { CONTAINER_CHEST = 0, CONTAINER_MINION_MAP = 1 };

enum event_data { EVENT_THING = 0, EVENT_THING2 = 2 };  /* byte offsets in TimelineEvent.data */

#define GDAT_CREATURE_CATEGORY 0x0f
#define GDAT_CREATURE_INFO_ENTRY 5
#define GDAT_CONTAINER_CATEGORY 0x14
#define GDAT_TYPE_TEXT 5
#define GDAT_MONEYBOX_ENTRY 0x40

/* Sensors that store 9 bits of their data word besides their record mask: teleporter link (0x27),
   animated ornament (0x2c), shop display (0x30), activation with sound (0x32), and four of unknown use. */
static const uint8_t sensors_with_data[] = {0x27, 0x1b, 0x1d, 0x41, 0x2c, 0x32, 0x30, 0x2d};

/* Mask tables of SKULL.EXE: one mask byte per record byte. */
static const uint8_t all_bits[2] = {0xff, 0xff};
static const uint8_t game_record_mask[GAME_RECORD_SIZE] = {
    0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0x00, 0x00, 0x07, 0x00, 0x1f, 0x00, 0x1f, 0x00, 0x03, 0x00, 0x3f, 0x00, 0x03, 0x00,
    0xff, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x07, 0x00, 0x07, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00,
    0xff, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x03, 0xff, 0xff, 0xff, 0x03, 0xff, 0x00, 0x1f, 0x03, 0xff, 0xff, 0xff, 0x00,
};
static const uint8_t champion_mask[CHAMPION_SIZE] = {
    0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x00, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
    0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x00, 0x03, 0x03, 0x07, 0x00,
    0x3f, 0x3f, 0x7f, 0x7f, 0x7f, 0x7f, 0x00, 0x00, 0x03, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x12, 0x00, 0x3f, 0x00, 0xff, 0x03, 0xff, 0x03, 0xff, 0x3f, 0xff, 0x3f, 0xff, 0x03,
    0xff, 0x03, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe0,
    0xff, 0xff, 0x1f, 0xe0, 0xff, 0xff, 0x1f, 0xe0, 0xff, 0xff, 0x1f, 0xe0, 0xff, 0xff, 0x1f, 0xe0,
    0xff, 0xff, 0x1f, 0xe0, 0xff, 0xff, 0x1f, 0xe0, 0xff, 0xff, 0x1f, 0xe0, 0xff, 0xff, 0x1f, 0xe0,
    0xff, 0xff, 0x1f, 0xe0, 0xff, 0xff, 0x1f, 0xe0, 0xff, 0xff, 0x1f, 0xe0, 0xff, 0xff, 0x1f, 0xe0,
    0xff, 0xff, 0x1f, 0xe0, 0xff, 0xff, 0x1f, 0xe0, 0xff, 0xff, 0x1f, 0xe0, 0xff, 0xff, 0x1f, 0xe0,
    0xff, 0xff, 0x1f, 0xe0, 0xff, 0xff, 0x1f, 0xe0, 0xff, 0xff, 0x1f, 0xe0, 0xff, 0xff, 0x1f, 0x00,
    [0x101] = 0x3f, 0x0f,
};
static const uint8_t magic_light_mask[MAGIC_LIGHT_SIZE] = {0x00, 0x00, 0x00, 0xff, 0xff, 0x00};
static const uint8_t event_mask[EVENT_SIZE] = {0xff, 0xff, 0xff, 0x3f, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00};

static const uint8_t door_mask[] = {0x00, 0x00, 0x00, 0x3e};
static const uint8_t text_mask[] = {0x00, 0x00, 0x01, 0x00};
static const uint8_t sensor_mask[] = {0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00};
static const uint8_t group_mask[] = {0, 0, 0, 0, 0, 0, 0xff, 0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80, 0x07};
static const uint8_t static_group_mask[] = {0, 0, 0, 0, 0, 0, 0xff, 0x3f, 0xff, 0x07, 0xff, 0xff, 0x3f, 0x00, 0x00, 0x07};
static const uint8_t weapon_mask[] = {0x00, 0x00, 0xff, 0x3d};
static const uint8_t armour_mask[] = {0x00, 0x00, 0xff, 0x1f};
static const uint8_t word_mask[] = {0x00, 0x00, 0xff, 0xff};
static const uint8_t container_mask[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x00, 0x04};
static const uint8_t minion_map_mask[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0xff, 0xff};
static const uint8_t misc_mask[] = {0x00, 0x00, 0xff, 0xc0};
static const uint8_t moneybox_misc_mask[] = {0x00, 0x00, 0x7f, 0xff};
static const uint8_t projectile_mask[] = {0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x03};
static const uint8_t carried_projectile_mask[] = {0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x01, 0x00};

/* The mask of each thing type; teleporters and the unused types are not stored. */
static const uint8_t *const thing_masks[THING_TYPE_COUNT] = {
    door_mask, NULL, text_mask, sensor_mask, group_mask, weapon_mask, armour_mask, word_mask, word_mask,
    container_mask, misc_mask, NULL, NULL, NULL, projectile_mask, word_mask,
};

/* Game record fields, byte offsets. */
enum game_record_field {
    REC_GAME_TIME = 0x00, REC_RANDOM_SEED = 0x04, REC_CHAMPION_COUNT = 0x08, REC_PARTY_X = 0x0a, REC_PARTY_Y = 0x0c,
    REC_PARTY_DIR = 0x0e, REC_PARTY_MAP = 0x10, REC_LEADER = 0x12, REC_EVENT_COUNT = 0x14, REC_UNKNOWN_TIME_A = 0x16,
    REC_UNKNOWN_TIME_B = 0x1a, REC_MOVE_COOLDOWN = 0x1e, REC_UNKNOWN_COUNTDOWN = 0x20, REC_UNKNOWN_DIR = 0x22,
    REC_VOLUMES = 0x28, REC_WEATHER_VISIBLE = 0x2a, REC_HEAVY_RAIN = 0x2e, REC_WIND_DIR = 0x2f,
    REC_RAIN_INTENSITY = 0x30, REC_SKY_DARKNESS = 0x31, REC_CLOUD_COVER = 0x32, REC_STORM_MULTIPLIER = 0x33,
    REC_STORM_INTENSITY = 0x34, REC_STORM_STEP = 0x36, REC_STORM_CURVE = 0x37, REC_NEXT_HOUR_TIME = 0x38
};
#define VOLUME_MASK 0xf
#define MUSIC_VOLUME_SHIFT 4

Game game = {.leader = 0xffff, .sound_volume = VOLUME_SETTING_START, .music_volume = VOLUME_SETTING_START};  /* no leader */

/* State of one load or write. */
static struct {
    FILE *f;
    uint8_t byte;               /* the byte being read or written */
    int bits;                   /* bits of byte already read or written */
    int format;                 /* header format word of the loaded file */
    int in_group;               /* inside the possessions of a creature group */
    int in_projectile;          /* inside the payload of a projectile */
    const uint8_t *misc_mask;   /* moneybox_misc_mask inside a moneybox */
    uint16_t deferred[DEFERRED_MAX];
    int deferred_count;
    uint16_t *group_order;      /* written: position of each group record in the bit stream */
    uint16_t *container_order;  /* written: position of each container record in the bit stream */
    int group_count, container_count;
    const gdat_t *gdat;
} io;

static uint16_t le16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t)le16(p + 2) << 16; }
static void put_le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = v >> 8; }
static void put_le32(uint8_t *p, uint32_t v) { put_le16(p, (uint16_t)v); put_le16(p + 2, v >> 16); }

static void start_io(FILE *f, const gdat_t *gdat)
{
    memset(&io, 0, sizeof io);
    io.f = f;
    io.gdat = gdat;
    io.misc_mask = misc_mask;
}

/* Writes the bits that mask selects of count records of size bytes. */
static int write_packed(const uint8_t *src, const uint8_t *mask, int size, int count)
{
    for (int r = 0; r < count; r++, src += size)
        for (int i = 0; i < size; i++)
            for (int bit = 7; bit >= 0; bit--) {
                if (!(mask[i] >> bit & 1)) continue;
                io.byte = (uint8_t)(io.byte << 1 | (src[i] >> bit & 1));
                if (++io.bits < 8) continue;
                if (fputc(io.byte, io.f) == EOF) return 0;
                io.byte = 0;
                io.bits = 0;
            }
    return 1;
}

/* Reads the bits that mask selects of count records; the other bits keep their value, or become 0 without keep. */
static int read_packed(uint8_t *dst, const uint8_t *mask, int size, int count, int keep)
{
    for (int r = 0; r < count; r++, dst += size)
        for (int i = 0; i < size; i++) {
            uint8_t value = keep ? dst[i] : 0;
            for (int bit = 7; bit >= 0; bit--) {
                if (!(mask[i] >> bit & 1)) continue;
                if (io.bits == 0) {
                    int c = fgetc(io.f);
                    if (c == EOF) return 0;  /* fix: the original reads on with a stale byte */
                    io.byte = (uint8_t)c;
                }
                value = io.byte >> 7 ? value | 1 << bit : value & ~(1 << bit);  /* most significant bit first */
                io.byte <<= 1;
                io.bits = (io.bits + 1) % 8;
            }
            dst[i] = value;
        }
    return 1;
}

/* A value between the records: a little-endian word through a little-endian mask. */
static int write_value(uint16_t value, uint16_t mask)
{
    uint8_t v[2], m[2];
    put_le16(v, value);
    put_le16(m, mask);
    return write_packed(v, m, 2, 1);
}

static int read_value(uint16_t *value, uint16_t mask)
{
    uint8_t v[2], m[2];
    put_le16(m, mask);
    if (!read_packed(v, m, 2, 1, 0)) return 0;
    *value = le16(v);
    return 1;
}

/* Writes the last byte; its unused low bits are 0. */
static int flush_bits(void)
{
    if (io.bits == 0) return 1;
    int ok = fputc((uint8_t)(io.byte << (8 - io.bits)), io.f) != EOF;
    io.byte = 0;
    io.bits = 0;
    return ok;
}

static uint16_t champion_slot(int champion, int slot)
{
    return le16(game.champions[champion] + CHAMPION_SLOTS + 2 * slot);
}

static void set_champion_slot(int champion, int slot, uint16_t thing)
{
    put_le16(game.champions[champion] + CHAMPION_SLOTS + 2 * slot, thing);
}

static int sensor_has_data(uint16_t sensor)
{
    int type = thing_word(sensor, SENSOR_TYPE_DATA) & SENSOR_TYPE_MASK;
    for (size_t i = 0; i < sizeof sensors_with_data; i++)
        if (sensors_with_data[i] == type) return 1;
    return 0;
}

static int creature_is_static(uint16_t group)
{
    int info = gdat_find(io.gdat, GDAT_CREATURE_CATEGORY, dungeon_thing_record(group)[GROUP_CREATURE], GDAT_TYPE_VALUE,
                         GDAT_CREATURE_INFO_ENTRY);
    if (info == -1) info = 0;  /* a missing value key reads as 0 */
    const CreatureInfo *record = creature_info_at(info);
    return record && record->flags & CREATURE_STATIC;  /* fix: no read past the table */
}

static int container_kind(uint16_t container)
{
    return thing_word(container, CONTAINER_KIND) >> CONTAINER_KIND_SHIFT & CONTAINER_KIND_MASK;
}

/* A chest whose item has the moneybox text holds coins. */
static int container_is_moneybox(uint16_t container)
{
    if (container_kind(container) != CONTAINER_CHEST) return 0;
    int item = thing_word(container, CONTAINER_KIND) >> CONTAINER_ITEM_SHIFT;
    return gdat_find(io.gdat, GDAT_CONTAINER_CATEGORY, item, GDAT_TYPE_TEXT, GDAT_MONEYBOX_ENTRY) != -1;
}

static int defer_thing(uint16_t thing)
{
    if (io.deferred_count == DEFERRED_MAX) return 0;  /* fix: the original writes past its buffer */
    io.deferred[io.deferred_count++] = thing;
    return 1;
}

/* Points each projectile at the timeline event that moves it, and each champion at the event that ends its damage
   display (THING_NONE: no damage shows). */
static void timeline_relink_events(void)
{
    for (int c = 0; c < CHAMPION_MAX; c++) put_le16(game.champions[c] + CHAMPION_DAMAGE_EVENT, THING_NONE);
    for (int i = 0; i < timeline.count; i++) {
        const TimelineEvent *e = &timeline.events[i];
        uint16_t thing = le16(e->data + EVENT_THING);
        if (e->type == EVENT_HIDE_DAMAGE && e->priority < CHAMPION_MAX)  /* fix: the original does not check the champion */
            put_le16(game.champions[e->priority] + CHAMPION_DAMAGE_EVENT, (uint16_t)i);
        if ((e->type == EVENT_PROJECTILE_MOVE || e->type == EVENT_PROJECTILE_MOVE_2)
            && thing_type(thing) == THING_PROJECTILE && dungeon_thing_valid(thing))
            set_thing_word(thing, PROJECTILE_EVENT, i);
    }
}

static int write_thing_list(uint16_t thing, int with_cell, int follow);

/* Writes what the record of thing stores beside its mask, the record, and the things that it holds. */
static int write_thing(uint16_t thing)
{
    int type = thing_type(thing);
    uint8_t *record = dungeon_thing_record(thing);
    const uint8_t *mask = type == THING_MISC ? io.misc_mask : thing_masks[type];
    int carried = 0;  /* minion map, or projectile in a creature: the reference follows at the end */
    switch (type) {
    case THING_SENSOR:
        if (sensor_has_data(thing) && !write_value(thing_word(thing, SENSOR_TYPE_DATA) >> SENSOR_DATA_SHIFT, SENSOR_DATA_MASK))
            return 0;
        break;
    case THING_GROUP:
        if (!write_value(record[GROUP_CREATURE], CREATURE_MASK)) return 0;
        if (creature_is_static(thing)) mask = static_group_mask;
        io.group_order[thing_index(thing)] = io.group_count++;
        break;
    case THING_CONTAINER:
        if (!write_value(container_kind(thing), CONTAINER_KIND_MASK)) return 0;
        if (container_kind(thing) == CONTAINER_MINION_MAP) {
            mask = minion_map_mask;
            carried = 1;
        }
        io.container_order[thing_index(thing)] = io.container_count++;
        break;
    case THING_PROJECTILE:
        if (io.in_group) {
            mask = carried_projectile_mask;
            carried = 1;
        }
        break;
    }
    if (!write_packed(record, mask, dungeon_thing_size(type), 1)) return 0;

    switch (type) {
    case THING_GROUP: {
        io.in_group = 1;
        int ok = write_thing_list(thing_word(thing, GROUP_POSSESSIONS), mask != group_mask, 1);
        io.in_group = 0;
        return ok;
    }
    case THING_CONTAINER: {
        uint16_t contents = thing_word(thing, CONTAINER_CONTENTS);
        if (carried) {
            int linked = contents != THING_NONE && contents != THING_ENDOFLIST;
            return write_value(linked, MORE_MASK) && (!linked || defer_thing(thing));
        }
        int moneybox = container_is_moneybox(thing);
        if (moneybox) io.misc_mask = moneybox_misc_mask;
        int ok = write_thing_list(contents, 0, 1);
        if (moneybox) io.misc_mask = misc_mask;
        return ok;
    }
    case THING_PROJECTILE: {
        if (carried) return defer_thing(thing);
        io.in_projectile = 1;
        int ok = write_thing_list(thing_word(thing, PROJECTILE_PAYLOAD), 0, 0);
        io.in_projectile = 0;
        return ok;
    }
    case THING_EXPLOSION:
        for (int i = 0; i < timeline.count; i++)
            if (timeline.events[i].type == EVENT_EXPLOSION && le16(timeline.events[i].data + EVENT_THING2) == thing)
                return write_value(1, MORE_MASK) && write_value(i, INDEX_MASK);
        return write_value(0, MORE_MASK);
    }
    return 1;
}

/* Writes a thing list: one thing without follow, else up to the end of the list. Things above
   the sensors start with a 1 bit and their type; a 0 bit ends a list. */
static int write_thing_list(uint16_t thing, int with_cell, int follow)
{
    while (thing != THING_ENDOFLIST && thing != THING_NONE) {
        int type = thing_type(thing);
        if (type > THING_SENSOR) {
            if (!write_value(1, MORE_MASK) || !write_value(type, TYPE_MASK)) return 0;
            if (with_cell && type != THING_GROUP && !write_value(thing >> THING_CELL_SHIFT, CELL_MASK)) return 0;
        }
        if (type == THING_EXPLOSION && io.in_projectile) {
            if (!write_value(thing, SPELL_MASK)) return 0;
            break;
        }
        if (thing_masks[type] && !write_thing(thing)) return 0;
        if (!follow) break;
        thing = dungeon_next_thing(thing);
    }
    if (!follow && thing != THING_NONE) return 1;  /* like the original, also no end bit for an empty single thing */
    return write_value(0, MORE_MASK);
}

/* The mask of the stored square bits; *skip when a linked teleporter of a lower map stores the things. */
static uint8_t square_mask(int map, int x, int y, int *skip)
{
    *skip = 0;
    switch (dungeon_square(map, x, y) >> SQUARE_TYPE_SHIFT) {
    case SQUARE_PIT: return PIT_SAVED_BITS;
    case SQUARE_DOOR: return DOOR_SAVED_BITS;
    case SQUARE_FAKE_WALL: return FAKE_WALL_SAVED_BITS;
    case SQUARE_TELEPORTER: {
        int target = dungeon_teleporter_link_map(map, x, y);
        if (target < 0) return TELEPORTER_SAVED_BITS;
        *skip = target < map;
        return 0;
    }
    default: return 0;
    }
}

/* Writes the saved bits and the thing list of each square, column by column. */
static int write_squares(void)
{
    for (int map = 0; map < dungeon_map_count(); map++)
        for (int x = 0; x < dungeon_map_width(map); x++)
            for (int y = 0; y < dungeon_map_height(map); y++) {
                int skip;
                uint8_t mask = square_mask(map, x, y, &skip);
                if (mask && !write_packed(dungeon_square_byte(map, x, y), &mask, 1, 1)) return 0;
                if (!skip && !write_thing_list(dungeon_square_first_thing(map, x, y), 1, 1)) return 0;
            }
    return 1;
}

static int is_move_group_event(const TimelineEvent *e)
{
    return e->type == EVENT_MOVE_GROUP || e->type == EVENT_MOVE_GROUP_2;
}

/* Writes the things of the deferred minion maps and carried projectiles as bit stream positions. */
static int write_deferred(void)
{
    for (int i = 0; i < io.deferred_count; i++) {
        uint16_t thing = io.deferred[i];
        uint16_t target = thing_index(thing_word(thing, CONTAINER_CONTENTS));
        int of_group = thing_type(thing) == THING_CONTAINER;
        /* fix: the original reads past its table for a minion whose link is no container (the minion items) */
        uint16_t order = 0;
        if (target < dungeon_thing_count(of_group ? THING_GROUP : THING_CONTAINER))
            order = of_group ? io.group_order[target] : io.container_order[target];
        if (!write_value(order, INDEX_MASK)) return 0;
    }
    return 1;
}

static void pack_game_record(uint8_t *r)
{
    memset(r, 0, GAME_RECORD_SIZE);
    put_le32(r + REC_GAME_TIME, game.game_time);
    put_le32(r + REC_RANDOM_SEED, random_seed);
    put_le16(r + REC_CHAMPION_COUNT, game.champion_count);
    put_le16(r + REC_PARTY_X, game.party_x);
    put_le16(r + REC_PARTY_Y, game.party_y);
    put_le16(r + REC_PARTY_DIR, game.party_dir);
    put_le16(r + REC_PARTY_MAP, game.party_map);
    put_le16(r + REC_LEADER, game.leader);
    put_le16(r + REC_EVENT_COUNT, timeline.count);
    put_le32(r + REC_UNKNOWN_TIME_A, game.last_combat_time);
    put_le32(r + REC_UNKNOWN_TIME_B, game.first_recruit_time);
    put_le16(r + REC_MOVE_COOLDOWN, game.party_move_cooldown);
    put_le16(r + REC_UNKNOWN_COUNTDOWN, game.throw_cooldown);
    put_le16(r + REC_UNKNOWN_DIR, game.throw_dir);
    put_le16(r + REC_VOLUMES, (game.sound_volume & VOLUME_MASK) | (game.music_volume & VOLUME_MASK) << MUSIC_VOLUME_SHIFT);
    put_le32(r + REC_WEATHER_VISIBLE, game.weather_visible);
    r[REC_HEAVY_RAIN] = game.heavy_rain;
    r[REC_WIND_DIR] = game.wind_dir;
    r[REC_RAIN_INTENSITY] = game.rain_intensity;
    r[REC_SKY_DARKNESS] = game.sky_darkness;
    r[REC_CLOUD_COVER] = game.cloud_cover;
    r[REC_STORM_MULTIPLIER] = game.storm_multiplier;
    put_le16(r + REC_STORM_INTENSITY, game.storm_intensity);
    r[REC_STORM_STEP] = game.storm_step;
    r[REC_STORM_CURVE] = game.storm_curve;
    put_le32(r + REC_NEXT_HOUR_TIME, game.next_hour_time);
}

static void unpack_game_record(const uint8_t *r)
{
    game.game_time = le32(r + REC_GAME_TIME);
    random_seed = le32(r + REC_RANDOM_SEED);
    game.champion_count = le16(r + REC_CHAMPION_COUNT);
    game.party_x = le16(r + REC_PARTY_X);
    game.party_y = le16(r + REC_PARTY_Y);
    game.party_dir = le16(r + REC_PARTY_DIR);
    game.party_map = le16(r + REC_PARTY_MAP);
    game.leader = le16(r + REC_LEADER);
    timeline.count = le16(r + REC_EVENT_COUNT);
    game.last_combat_time = le32(r + REC_UNKNOWN_TIME_A);
    game.first_recruit_time = le32(r + REC_UNKNOWN_TIME_B);
    game.party_move_cooldown = le16(r + REC_MOVE_COOLDOWN);
    game.throw_cooldown = le16(r + REC_UNKNOWN_COUNTDOWN);
    game.throw_dir = le16(r + REC_UNKNOWN_DIR);
    game.sound_volume = r[REC_VOLUMES] & VOLUME_MASK;
    game.music_volume = r[REC_VOLUMES] >> MUSIC_VOLUME_SHIFT & VOLUME_MASK;
    game.weather_visible = r[REC_WEATHER_VISIBLE];
    game.heavy_rain = r[REC_HEAVY_RAIN];
    game.wind_dir = r[REC_WIND_DIR];
    game.rain_intensity = r[REC_RAIN_INTENSITY];
    game.sky_darkness = r[REC_SKY_DARKNESS];
    game.cloud_cover = r[REC_CLOUD_COVER];
    game.storm_multiplier = r[REC_STORM_MULTIPLIER];
    game.storm_intensity = le16(r + REC_STORM_INTENSITY);
    game.storm_step = r[REC_STORM_STEP];
    game.storm_curve = r[REC_STORM_CURVE];
    game.next_hour_time = le32(r + REC_NEXT_HOUR_TIME);
}

/* The global words and the timeline events are stored in host order; the host is little-endian like the save file. */
static int write_state(void)
{
    uint8_t record[GAME_RECORD_SIZE];
    timeline_compact();
    timeline_relink_events();
    pack_game_record(record);
    if (!write_packed(record, game_record_mask, GAME_RECORD_SIZE, 1)
        || !write_packed(game.global_flags, all_bits, 1, GLOBAL_FLAG_BYTES)
        || !write_packed(game.global_bytes, all_bits, 1, GLOBAL_BYTE_COUNT)
        || !write_packed((const uint8_t *)game.global_words, all_bits, 2, GLOBAL_WORD_COUNT)
        || !write_packed(game.champions[0], champion_mask, CHAMPION_SIZE, game.champion_count)
        || !write_packed(game.magic_light, magic_light_mask, MAGIC_LIGHT_SIZE, 1)
        || !write_packed((const uint8_t *)timeline.events, event_mask, EVENT_SIZE, timeline.count))
        return 0;
    for (int c = 0; c < game.champion_count; c++)
        for (int s = 0; s < SLOT_COUNT; s++)
            if (!write_thing_list(champion_slot(c, s), 0, 0)) return 0;
    if (!write_thing_list(game.leader_hand, 0, 0)) return 0;
    for (int i = 0; i < timeline.count; i++)
        if (is_move_group_event(&timeline.events[i]) && !write_thing_list(le16(timeline.events[i].data + EVENT_THING2), 0, 0))
            return 0;
    return write_squares() && write_deferred() && flush_bits();
}

static int read_thing_list(int map, int x, int y, uint16_t *list, int with_cell, int follow);

static int set_event_thing(uint16_t event, int offset, uint16_t thing)
{
    if (event >= timeline.capacity) return 0;  /* fix: the original writes outside the timeline */
    put_le16(timeline.events[event].data + offset, thing);
    return 1;
}

/* Reads what the record of a new thing stores beside its mask, the record, and the things that it holds. */
static int read_thing(uint16_t thing)
{
    int type = thing_type(thing);
    uint8_t *record = dungeon_thing_record(thing);
    const uint8_t *mask = type == THING_MISC ? io.misc_mask : thing_masks[type];
    int carried = 0;
    uint16_t value;
    switch (type) {
    case THING_GROUP:
        if (!read_value(&value, CREATURE_MASK)) return 0;
        record[GROUP_CREATURE] = (uint8_t)value;
        if (creature_is_static(thing)) mask = static_group_mask;
        break;
    case THING_CONTAINER:
        if (!read_value(&value, CONTAINER_KIND_MASK)) return 0;
        set_thing_word(thing, CONTAINER_KIND, (thing_word(thing, CONTAINER_KIND) & ~(CONTAINER_KIND_MASK << CONTAINER_KIND_SHIFT))
                                                  | value << CONTAINER_KIND_SHIFT);
        if (value == CONTAINER_MINION_MAP) {
            mask = minion_map_mask;
            carried = 1;
        }
        break;
    case THING_PROJECTILE:
        if (io.in_group) {
            mask = carried_projectile_mask;
            carried = 1;
        }
        break;
    }
    if (!read_packed(record, mask, dungeon_thing_size(type), 1, 1)) return 0;

    switch (type) {
    case THING_GROUP: {
        uint16_t possessions = THING_ENDOFLIST;
        io.in_group = 1;
        int ok = read_thing_list(0, 0, 0, &possessions, mask != group_mask, 1);
        io.in_group = 0;
        set_thing_word(thing, GROUP_POSSESSIONS, possessions);
        return ok;
    }
    case THING_CONTAINER: {
        uint16_t contents = THING_ENDOFLIST;
        if (carried) {
            if (!read_value(&value, MORE_MASK)) return 0;
            set_thing_word(thing, CONTAINER_CONTENTS, contents);
            return !value || defer_thing(thing);
        }
        int moneybox = container_is_moneybox(thing);
        if (moneybox) io.misc_mask = moneybox_misc_mask;
        int ok = read_thing_list(0, 0, 0, &contents, 0, 1);
        if (moneybox) io.misc_mask = misc_mask;
        set_thing_word(thing, CONTAINER_CONTENTS, contents);
        return ok;
    }
    case THING_PROJECTILE: {
        if (carried) return defer_thing(thing);
        uint16_t payload = THING_ENDOFLIST;
        if (!set_event_thing(thing_word(thing, PROJECTILE_EVENT), EVENT_THING, thing)) return 0;
        io.in_projectile = 1;
        int ok = read_thing_list(0, 0, 0, &payload, 0, 0);
        io.in_projectile = 0;
        set_thing_word(thing, PROJECTILE_PAYLOAD, payload);
        return ok;
    }
    case THING_EXPLOSION:
        if (!read_value(&value, MORE_MASK)) return 0;
        if (!value) return 1;
        return read_value(&value, INDEX_MASK) && set_event_thing(value, EVENT_THING2, thing);
    }
    return 1;
}

/* Reads a thing list into new records: onto the square (x, y) of map, or with list onto the end of *list. */
static int read_thing_list(int map, int x, int y, uint16_t *list, int with_cell, int follow)
{
    do {
        uint16_t more, type, cell = 0;
        if (!read_value(&more, MORE_MASK)) return 0;
        if (!more) return 1;
        if (!read_value(&type, TYPE_MASK)) return 0;
        if (with_cell && type != THING_GROUP && !read_value(&cell, CELL_MASK)) return 0;
        if (type == THING_EXPLOSION && io.in_projectile) {
            uint16_t spell;
            if (!read_value(&spell, SPELL_MASK)) return 0;
            if (list) *list = spell | SPELL_THING_BASE;
            return 1;
        }
        uint16_t thing = dungeon_unused_thing(type);
        if (thing == THING_NONE) return 0;
        thing |= cell << THING_CELL_SHIFT;
        if (list) dungeon_append_thing(thing, list);
        else if (!dungeon_link_thing_to_square(thing, map, x, y)) return 0;
        if (thing_masks[type] && !read_thing(thing)) return 0;
    } while (follow);
    return 1;
}

/* Removes the creature groups and items from the squares and frees their records; the bit stream creates them again. */
static void free_movable_things(void)
{
    for (int map = 0; map < dungeon_map_count(); map++)
        for (int x = 0; x < dungeon_map_width(map); x++)
            for (int y = 0; y < dungeon_map_height(map); y++)
                for (uint16_t t = dungeon_square_first_thing(map, x, y); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
                    if (thing_type(t) > THING_SENSOR) {
                        set_thing_word(t, THING_NEXT, THING_ENDOFLIST);
                        dungeon_unlink_thing_from_square(t, map, x, y);
                        break;
                    }
    for (int type = THING_GROUP; type < THING_TYPE_COUNT; type++)
        for (int i = 0; i < dungeon_thing_count(type); i++) set_thing_word(type << THING_TYPE_SHIFT | i, THING_NEXT, THING_NONE);
}

/* Reads the saved bits of each square, the bits of its fixed things, and its movable things. */
static int read_squares(void)
{
    for (int map = 0; map < dungeon_map_count(); map++)
        for (int x = 0; x < dungeon_map_width(map); x++)
            for (int y = 0; y < dungeon_map_height(map); y++) {
                int skip;
                uint8_t mask = square_mask(map, x, y, &skip);
                if (mask && !read_packed(dungeon_square_byte(map, x, y), &mask, 1, 1, 1)) return 0;
                if (skip) continue;
                for (uint16_t t = dungeon_square_first_thing(map, x, y); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
                    int type = thing_type(t);
                    if (!thing_masks[type]) continue;
                    uint16_t data;
                    if (type == THING_SENSOR && sensor_has_data(t)) {
                        if (!read_value(&data, SENSOR_DATA_MASK)) return 0;
                        set_thing_word(t, SENSOR_TYPE_DATA, (thing_word(t, SENSOR_TYPE_DATA) & SENSOR_TYPE_MASK) | data << SENSOR_DATA_SHIFT);
                    }
                    if (!read_packed(dungeon_thing_record(t), thing_masks[type], dungeon_thing_size(type), 1, 1)) return 0;
                }
                if (!read_thing_list(map, x, y, NULL, 1, 1)) return 0;
            }
    return 1;
}

/* Links the deferred minion maps to their creature groups and the carried projectiles to their containers. */
static int read_deferred(void)
{
    for (int i = 0; i < io.deferred_count; i++) {
        uint16_t index, thing = io.deferred[i];
        if (!read_value(&index, INDEX_MASK)) return 0;
        int target_type = thing_type(thing) == THING_CONTAINER ? THING_GROUP : THING_CONTAINER;
        uint16_t target = target_type << THING_TYPE_SHIFT | index;
        if (!dungeon_thing_valid(target)) return 0;
        set_thing_word(thing, CONTAINER_CONTENTS, target);
    }
    return 1;
}

/* Reads the things of the champions, the leader's hand, the timeline events, and the squares; the items that the
   champions hold give their bonuses again. */
static int read_things(void)
{
    free_movable_things();
    for (int c = 0; c < game.champion_count; c++)
        for (int s = 0; s < SLOT_COUNT; s++) {
            uint16_t slot = THING_ENDOFLIST;
            if (!read_thing_list(0, 0, 0, &slot, 0, 0)) return 0;
            set_champion_slot(c, s, slot == THING_ENDOFLIST ? THING_NONE : slot);
        }
    game.leader_hand = THING_ENDOFLIST;
    if (!read_thing_list(0, 0, 0, &game.leader_hand, 0, 0)) return 0;
    if (game.leader_hand == THING_ENDOFLIST) game.leader_hand = THING_NONE;
    for (int c = 0; c < game.champion_count; c++)
        for (int s = 0; s < SLOT_COUNT; s++) champ_apply_item_effects(c, champion(c)->slots[s], s, EFFECT_MODE_LOAD);
    champ_apply_item_effects((int16_t)game.leader, game.leader_hand, -1, EFFECT_MODE_LOAD);
    for (int i = 0; i < timeline.count; i++) {
        TimelineEvent *e = &timeline.events[i];
        if (!is_move_group_event(e)) continue;
        uint16_t group = THING_ENDOFLIST;
        if (io.format == 0 || !read_thing_list(0, 0, 0, &group, 0, 0)) return 0;
        put_le16(e->data + EVENT_THING2, group);
    }
    return read_squares() && read_deferred();
}

/* The light of a magic light event by its power (event data), from SKULL.EXE. */
static const int8_t light_power[16] = {0, 5, 12, 24, 33, 40, 46, 51, 59, 68, 76, 82, 89, 94, 97, 100};
#define LIGHT_POWER_COUNT 16

/* The magic effects that the timeline holds come back: magic light, invisibility, auras, poison, eaten items; then
   the champion loads. */
static void save_restore_timed_effects(void)
{
    memset(game.magic_light, 0, sizeof game.magic_light);
    for (int i = 0; i < timeline.used_end; i++) {
        const TimelineEvent *e = &timeline.events[i];
        int16_t v = (int16_t)(e->data[0] | e->data[1] << 8);
        switch (e->type) {
        case EVENT_ITEM_EFFECT: timeline_event_apply_item_effect(e, EFFECT_MODE_RESTORE); break;
        case EVENT_MAGIC_LIGHT:
            if (v && v > -LIGHT_POWER_COUNT && v < LIGHT_POWER_COUNT) {
                uint16_t light = (uint16_t)(game.magic_light[0] | game.magic_light[1] << 8);
                light = (uint16_t)(light + (v < 0 ? light_power[-v] : light_power[v] * -2));
                game.magic_light[0] = (uint8_t)light;
                game.magic_light[1] = (uint8_t)(light >> 8);
            }
            break;
        case EVENT_INVISIBILITY: game.magic_light[MAGIC_INVISIBILITY]++; break;
        case EVENT_AURA:
            for (int c = 0; c < game.champion_count; c++)
                if (e->priority >> c & 1 && champion(c)->health) champion(c)->aura_value = (int16_t)(champion(c)->aura_value + v);
            break;
        case EVENT_POISON:
            champion(e->priority)->poisoned++;
            champion(e->priority)->poison = (int16_t)(champion(e->priority)->poison + v);
            break;
        }
    }
    for (int c = 0; c < game.champion_count; c++) champ_compute_load(c);
}

static int read_state(void)
{
    uint8_t record[GAME_RECORD_SIZE];
    timeline_init(EVENT_SLACK + dungeon_thing_count(THING_GROUP) + dungeon_thing_count(THING_PROJECTILE)
                  + dungeon_thing_count(THING_EXPLOSION));
    weather_load(io.gdat);  /* the weather part of the original dungeon_load; the record then sets the weather */
    if (!read_packed(record, game_record_mask, GAME_RECORD_SIZE, 1, 0)) return 0;
    unpack_game_record(record);
    if (game.champion_count > CHAMPION_MAX || timeline.count > timeline.capacity) return 0;
    memset(game.champions, 0, sizeof game.champions);
    if (!read_packed(game.global_flags, all_bits, 1, GLOBAL_FLAG_BYTES, 0)
        || !read_packed(game.global_bytes, all_bits, 1, GLOBAL_BYTE_COUNT, 0)
        || !read_packed((uint8_t *)game.global_words, all_bits, 2, GLOBAL_WORD_COUNT, 0)
        || !read_packed(game.champions[0], champion_mask, CHAMPION_SIZE, game.champion_count, 0)
        || !read_packed(game.magic_light, magic_light_mask, MAGIC_LIGHT_SIZE, 1, 0)
        || !read_packed((uint8_t *)timeline.events, event_mask, EVENT_SIZE, timeline.count, 0))
        return 0;
    for (int i = timeline.count; i < timeline.capacity; i++) timeline.events[i].type = EVENT_NONE;
    timeline_build_queue();
    if (!read_things()) return 0;
    save_restore_timed_effects();
    timeline_relink_events();
    return 1;
}

int save_load(const char *path, const gdat_t *gdat)
{
    FILE *f = fopen(path, "rb");
    if (!f) return SAVE_ERR_IO;
    creature_free_active_groups();
    uint8_t header[HEADER_SIZE];
    int result = SAVE_ERR_FORMAT;
    if (fread(header, 1, HEADER_SIZE, f) == HEADER_SIZE && dungeon_read(f, 0)) {
        start_io(f, gdat);
        if (draw_gdat() != gdat) draw_init(gdat);  /* the item effects of the champions read the item attributes */
        champ_reset_step_state();
        io.format = le16(header + HEADER_FORMAT);
        result = read_state() ? SAVE_OK : SAVE_ERR_DATA;
        if (result == SAVE_OK) automap_read(f);  /* the original stops at the last bit of the state: the block follows */
    }
    fclose(f);
    if (result != SAVE_OK) dungeon_free();
    return result;
}

int save_write(const char *path, const char *bak_path, const char *name, const gdat_t *gdat)
{
    uint8_t header[HEADER_SIZE] = {0};  /* fix: without an old file the original writes uninitialized bytes */
    remove(bak_path);
    rename(path, bak_path);
    FILE *old = fopen(bak_path, "rb");
    if (old) {
        if (fread(header, 1, HEADER_SIZE, old) != HEADER_SIZE) memset(header, 0, HEADER_SIZE);
        fclose(old);
    }
    put_le16(header + HEADER_FORMAT, SAVE_FORMAT);
    size_t n = strnlen(name, SAVE_NAME_SIZE - 1);
    memcpy(header + HEADER_NAME, name, n);  /* the name bytes after its end mark keep the old header bytes */
    header[HEADER_NAME + n] = 0;

    FILE *f = fopen(path, "wb");
    if (!f) return SAVE_ERR_IO;
    start_io(f, gdat);
    io.group_order = calloc(dungeon_thing_count(THING_GROUP) + 1, sizeof *io.group_order);
    io.container_order = calloc(dungeon_thing_count(THING_CONTAINER) + 1, sizeof *io.container_order);
    /* the records keep the indices of the active groups; the timeline loses their events (game_save) */
    int ok = fwrite(header, 1, HEADER_SIZE, f) == HEADER_SIZE && dungeon_write(f);
    creature_deactivate_all_groups();
    ok = ok && write_state() && (game_mode == GAME_MODE_VANILLA || automap_write(f));
    free(io.group_order);
    free(io.container_order);
    if (fclose(f) != 0) ok = 0;
    if (!ok) remove(path);
    creature_activate_groups_all_maps();
    return ok ? SAVE_OK : SAVE_ERR_IO;
}
