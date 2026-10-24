/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "save.h"
#include "gdat.h"
#include "ids.h"
#include "le.h"
#include "os.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "game.h"
#include "inventory.h"
#include "random.h"
#include "seen.h"
#include "sensor.h"
#include "timeline.h"
#include "weather.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Save file: a 42-byte header, the dungeon image, then the game state as one bit stream. Each record
   of the bit stream stores only the bits that its mask table selects, most significant bit first.
   Format analysis: reDM2 docs/savegame.md and docs/formats/save.md. Outside the vanilla mode the AMAP
   block of the automap follows the bit stream (seen.h), in the advanced mode then the CHTS block of the cheats; the
   original loader ignores them. */

/* The save file header and the sizes of the game state. */
#define HEADER_FORMAT       0
#define SAVE_FORMAT         1
#define GAME_RECORD_SIZE    60
#define DEFERRED_THINGS_MAX 100
#define PACKED_WORD_BYTES   2 /* a little-endian word: a global word, or a value between the records */

/* The CHTS block of the cheats after the AMAP block: the magic, the version, one byte of cheat bits (CHEAT_*). */
#define CHEAT_MAGIC      "CHTS"
#define CHEAT_MAGIC_SIZE 4
#define CHEAT_VERSION    1
#define CHEAT_BITS_AT    (CHEAT_MAGIC_SIZE + 1)
#define CHEAT_BLOCK_SIZE (CHEAT_MAGIC_SIZE + 2)

/* Byte offsets in a champion record. */
#define CHAMPION_SLOTS        0xc3 /* 30 thing words: hands, body, pouches, quiver, backpack */
#define CHAMPION_DAMAGE_EVENT 0x2e /* the timeline event that ends the damage display */

/* Bit widths of the values between the records, as masks. */
#define MORE_MASK        0x0001
#define TYPE_MASK        0x000f
#define CELL_MASK        0x0003
#define CREATURE_MASK    0x007f
#define SPELL_MASK       0x007f
#define INDEX_MASK       0x03ff
#define SPELL_THING_BASE 0xff80 /* a projectile payload that is a spell, not a thing record */

/* Square bits that change during play and that the save stores, by square type. */
#define PIT_SAVED_BITS        0x08
#define DOOR_SAVED_BITS       0x07
#define FAKE_WALL_SAVED_BITS  0x04
#define TELEPORTER_SAVED_BITS 0x08 /* only for a teleporter without a linked teleporter */

/* Byte offsets in TimelineEvent.data. */
#define EVENT_THING  0x00
#define EVENT_VALUE  0x00
#define EVENT_THING2 0x02

/* The GDAT entry of the creature info record. */
#define GDAT_CREATURE_INFO_ENTRY 0x05

/* Game record fields, byte offsets. */
#define REC_GAME_TIME         0x00
#define REC_RANDOM_SEED       0x04
#define REC_CHAMPIONS         0x08
#define REC_PARTY_X           0x0a
#define REC_PARTY_Y           0x0c
#define REC_PARTY_DIR         0x0e
#define REC_PARTY_MAP         0x10
#define REC_LEADER            0x12
#define REC_TIMELINE_EVENTS   0x14
#define REC_UNKNOWN_TIME_A    0x16
#define REC_UNKNOWN_TIME_B    0x1a
#define REC_MOVE_COOLDOWN     0x1e
#define REC_UNKNOWN_COUNTDOWN 0x20
#define REC_UNKNOWN_DIR       0x22
#define REC_VOLUMES           0x28
#define REC_WEATHER_VISIBLE   0x2a
#define REC_HEAVY_RAIN        0x2e
#define REC_WIND_DIR          0x2f
#define REC_RAIN_INTENSITY    0x30
#define REC_SKY_DARKNESS      0x31
#define REC_CLOUD_COVER       0x32
#define REC_STORM_MULTIPLIER  0x33
#define REC_STORM_INTENSITY   0x34
#define REC_STORM_STEP        0x36
#define REC_STORM_CURVE       0x37
#define REC_NEXT_HOUR_TIME    0x38

/* The sound volume in the low nibble of the volume byte, the music volume in the high nibble. */
#define VOLUME_MASK 0xf

/* The bit position of the music volume in the volume byte. */
#define MUSIC_VOLUME_SHIFT 4

/* The powers of a magic light event (its event data is the power, negative for darkness). */
#define LIGHT_POWER_COUNT 16

/* Sensors that store 9 bits of their data word besides their record mask: teleporter link (0x27),
   animated ornament (0x2c), shop display (0x30), activation with sound (0x32), and four of unknown use. */
static const uint8_t sensors_with_data[] = {0x27, 0x1b, 0x1d, 0x41, 0x2c, 0x32, 0x30, 0x2d};

/* Mask tables of SKULL.EXE: one mask byte per record byte. */
static const uint8_t all_bits[PACKED_WORD_BYTES] = {0xff, 0xff};
static const uint8_t game_record_mask[GAME_RECORD_SIZE] = {
    0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0x00, 0x00, 0x07, 0x00, 0x1f, 0x00, 0x1f, 0x00, 0x03,
    0x00, 0x3f, 0x00, 0x03, 0x00, 0xff, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
    0x07, 0x00, 0x07, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0xff, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x01, 0x03, 0xff, 0xff, 0xff, 0x03, 0xff, 0x00, 0x1f, 0x03, 0xff, 0xff, 0xff, 0x00,
};
static const uint8_t champion_mask[CHAMPION_SIZE] = {
    0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,           0x00, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
    0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,           0x7f, 0x7f, 0x7f, 0x00, 0x03, 0x03, 0x07, 0x00, 0x3f, 0x3f,
    0x7f, 0x7f, 0x7f, 0x7f, 0x00, 0x00, 0x03,           0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12,
    0x00, 0x3f, 0x00, 0xff, 0x03, 0xff, 0x03,           0xff, 0x3f, 0xff, 0x3f, 0xff, 0x03, 0xff, 0x03, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0xff,           0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,           0xff, 0xff, 0xff, 0xe0, 0xff, 0xff, 0x1f, 0xe0, 0xff, 0xff,
    0x1f, 0xe0, 0xff, 0xff, 0x1f, 0xe0, 0xff,           0xff, 0x1f, 0xe0, 0xff, 0xff, 0x1f, 0xe0, 0xff, 0xff, 0x1f,
    0xe0, 0xff, 0xff, 0x1f, 0xe0, 0xff, 0xff,           0x1f, 0xe0, 0xff, 0xff, 0x1f, 0xe0, 0xff, 0xff, 0x1f, 0xe0,
    0xff, 0xff, 0x1f, 0xe0, 0xff, 0xff, 0x1f,           0xe0, 0xff, 0xff, 0x1f, 0xe0, 0xff, 0xff, 0x1f, 0xe0, 0xff,
    0xff, 0x1f, 0xe0, 0xff, 0xff, 0x1f, 0xe0,           0xff, 0xff, 0x1f, 0xe0, 0xff, 0xff, 0x1f, 0xe0, 0xff, 0xff,
    0x1f, 0xe0, 0xff, 0xff, 0x1f, 0x00, [0x101] = 0x3f, 0x0f,
};
static const uint8_t magic_light_mask[MAGIC_LIGHT_SIZE] = {0x00, 0x00, 0x00, 0xff, 0xff, 0x00};
static const uint8_t event_mask[TIMELINE_EVENT_SIZE] = {0xff, 0xff, 0xff, 0x3f, 0x7f, 0xff,
                                                        0xff, 0xff, 0xff, 0xff, 0x00, 0x00};

static const uint8_t door_mask[] = {0x00, 0x00, 0x00, 0x3e};
static const uint8_t text_mask[] = {0x00, 0x00, 0x01, 0x00};
static const uint8_t sensor_mask[] = {0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00};
static const uint8_t group_mask[] = {0, 0, 0, 0, 0, 0, 0xff, 0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80, 0x07};
static const uint8_t static_group_mask[] = {0,    0,    0,    0,    0,    0,    0xff, 0x3f,
                                            0xff, 0x07, 0xff, 0xff, 0x3f, 0x00, 0x00, 0x07};
static const uint8_t weapon_mask[] = {0x00, 0x00, 0xff, 0x3d};
static const uint8_t armour_mask[] = {0x00, 0x00, 0xff, 0x1f};
static const uint8_t word_mask[] = {0x00, 0x00, 0xff, 0xff};
static const uint8_t container_mask[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x00, 0x04};
static const uint8_t minion_map_mask[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0xff, 0xff};
static const uint8_t misc_mask[] = {0x00, 0x00, 0xff, 0xc0};
static const uint8_t moneybox_misc_mask[] = {0x00, 0x00, 0x7f, 0xff};
static const uint8_t projectile_mask[] = {0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x03};
static const uint8_t carried_projectile_mask[] = {0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x01, 0x00};

/* The light of a magic light event by its power (event data), from SKULL.EXE. */
static const int8_t light_power[LIGHT_POWER_COUNT] = {0, 5, 12, 24, 33, 40, 46, 51, 59, 68, 76, 82, 89, 94, 97, 100};

/* The mask of each thing type; teleporters and the unused types are not stored. */
static const uint8_t *const thing_masks[THING_TYPE_COUNT] = {
    door_mask, NULL,           text_mask, sensor_mask, group_mask, weapon_mask, armour_mask,     word_mask,
    word_mask, container_mask, misc_mask, NULL,        NULL,       NULL,        projectile_mask, word_mask,
};

Game g_game = {
    .leader = LEADER_NONE, .sound_volume = VOLUME_SETTING_START, .music_volume = VOLUME_SETTING_START}; /* no leader */

/* State of one load or write. */
static struct {
    FILE *f;
    uint8_t byte;             /* the byte being read or written */
    int bits;                 /* bits of byte already read or written */
    int format;               /* header format word of the loaded file */
    bool in_group;            /* inside the possessions of a creature group */
    bool in_projectile;       /* inside the payload of a projectile */
    const uint8_t *misc_mask; /* moneybox_misc_mask inside a moneybox */
    uint16_t deferred[DEFERRED_THINGS_MAX];
    int deferred_count;
    uint16_t *group_order;     /* written: position of each group record in the bit stream */
    uint16_t *container_order; /* written: position of each container record in the bit stream */
    int group_count, container_count;
    const gdat_t *gdat;
} io;

/* Starts a load or a write of the bit stream of f. */
static void start_io(FILE *f, const gdat_t *gdat)
{
    memset(&io, 0, sizeof io);
    io.f = f;
    io.gdat = gdat;
    io.misc_mask = misc_mask;
}

/* Writes the bits that mask selects of count records of size bytes. */
static bool write_packed(const uint8_t *src, const uint8_t *mask, int size, int count)
{
    for (int r = 0; r < count; r++, src += size)
        for (int i = 0; i < size; i++)
            for (int bit = 7; bit >= 0; bit--) {
                if (!(mask[i] >> bit & 1))
                    continue;
                io.byte = (uint8_t)(io.byte << 1 | (src[i] >> bit & 1));
                if (++io.bits < 8)
                    continue;
                if (fputc(io.byte, io.f) == EOF)
                    return false;
                io.byte = 0;
                io.bits = 0;
            }
    return true;
}

/* Reads the bits that mask selects of count records; the other bits keep their value, or become 0 without keep. */
static bool read_packed(uint8_t *dst, const uint8_t *mask, int size, int count, bool keep)
{
    for (int r = 0; r < count; r++, dst += size)
        for (int i = 0; i < size; i++) {
            uint8_t value = keep ? dst[i] : 0;
            for (int bit = 7; bit >= 0; bit--) {
                if (!(mask[i] >> bit & 1))
                    continue;
                if (io.bits == 0) {
                    int c = fgetc(io.f);
                    if (c == EOF)
                        return false; /* fix: the original reads on with a stale byte */
                    io.byte = (uint8_t)c;
                }
                value = io.byte >> 7 ? value | 1 << bit : value & ~(1 << bit); /* most significant bit first */
                io.byte <<= 1;
                io.bits = (io.bits + 1) % 8;
            }
            dst[i] = value;
        }
    return true;
}

/* A value between the records: a little-endian word through a little-endian mask. */
static bool write_value(uint16_t value, uint16_t mask)
{
    uint8_t v[PACKED_WORD_BYTES];
    uint8_t m[PACKED_WORD_BYTES];
    put_le16(v, value);
    put_le16(m, mask);
    return write_packed(v, m, PACKED_WORD_BYTES, 1);
}

/* Reads a value between the records: the bits of mask of a little-endian word. */
static bool read_value(uint16_t *value, uint16_t mask)
{
    uint8_t v[PACKED_WORD_BYTES];
    uint8_t m[PACKED_WORD_BYTES];
    put_le16(m, mask);
    if (!read_packed(v, m, PACKED_WORD_BYTES, 1, false))
        return false;
    *value = le16(v);
    return true;
}

/* Writes the last byte; its unused low bits are 0. */
static bool flush_bits(void)
{
    if (io.bits == 0)
        return true;
    bool ok = fputc((uint8_t)(io.byte << (8 - io.bits)), io.f) != EOF;
    io.byte = 0;
    io.bits = 0;
    return ok;
}

/* The thing in a slot of a champion record. */
static uint16_t held_thing(int champion, int slot)
{
    return le16(g_game.champions[champion] + CHAMPION_SLOTS + 2 * slot);
}

static void set_held_thing(int champion, int slot, uint16_t thing)
{
    put_le16(g_game.champions[champion] + CHAMPION_SLOTS + 2 * slot, thing);
}

/* A sensor of a type in sensors_with_data stores 9 bits of its data word. */
static bool has_sensor_data(uint16_t sensor)
{
    int type = sensor_type(sensor);
    for (size_t i = 0; i < sizeof sensors_with_data; i++)
        if (sensors_with_data[i] == type)
            return true;
    return false;
}

/* A group of a creature with the static flag stores fewer bits of its record. */
static bool is_static_creature(uint16_t group)
{
    int info = gdat_find(io.gdat, CAT_CREATURE, group_creature(group), GDAT_TYPE_VALUE, GDAT_CREATURE_INFO_ENTRY);
    if (info == -1)
        info = 0; /* a missing value key reads as 0 */
    const CreatureInfo *record = creature_info_at(info);
    return record && record->flags & CREATURE_STATIC; /* fix: no read past the table */
}

/* A chest whose item has the moneybox text holds coins. */
static bool is_moneybox(uint16_t container)
{
    if (container_kind(container) != CONTAINER_CHEST)
        return false;
    int item = container_type(container);
    return gdat_find(io.gdat, CAT_CONTAINER, item, GDAT_TYPE_TEXT, GFX_MONEYBOX_TEXT) != -1;
}

/* Keeps a minion map or a carried projectile for the links at the end of the bit stream. */
static bool defer_thing(uint16_t thing)
{
    if (io.deferred_count == DEFERRED_THINGS_MAX)
        return false; /* fix: the original writes past its buffer */
    io.deferred[io.deferred_count++] = thing;
    return true;
}

/* Points each projectile at the timeline event that moves it, and each champion at the event that ends its damage
   display (THING_NONE: no damage shows). */
static void relink_events(void)
{
    for (int c = 0; c < CHAMPION_MAX; c++)
        put_le16(g_game.champions[c] + CHAMPION_DAMAGE_EVENT, THING_NONE);
    for (int i = 0; i < g_timeline.count; i++) {
        const TimelineEvent *e = &g_timeline.events[i];
        uint16_t thing = le16(e->data + EVENT_THING);
        if (e->type == EVENT_HIDE_DAMAGE &&
            e->priority < CHAMPION_MAX) /* fix: the original does not check the champion */
            put_le16(g_game.champions[e->priority] + CHAMPION_DAMAGE_EVENT, (uint16_t)i);
        if ((e->type == EVENT_PROJECTILE_MOVE || e->type == EVENT_PROJECTILE_MOVE_2) &&
            thing_type(thing) == THING_PROJECTILE && dungeon_thing_valid(thing))
            set_thing_word(thing, PROJECTILE_EVENT, i);
    }
}

/* Recursion: write_thing_list writes the things that a thing holds through write_held_things. */
static bool write_thing_list(uint16_t thing, bool with_cell, bool follow);

/* Writes what the record of thing stores beside its mask; returns the mask of the record, NULL on a write error. */
static const uint8_t *write_thing_head(uint16_t thing, bool *carried)
{
    int type = thing_type(thing);
    const uint8_t *mask = type == THING_MISC ? io.misc_mask : thing_masks[type];
    *carried = false; /* minion map, or projectile in a creature: the reference follows at the end */
    switch (type) {
    case THING_SENSOR:
        if (has_sensor_data(thing) && !write_value(sensor_data(thing), SENSOR_DATA_MASK))
            return NULL;
        break;
    case THING_GROUP:
        if (!write_value(group_creature(thing), CREATURE_MASK))
            return NULL;
        if (is_static_creature(thing))
            mask = static_group_mask;
        io.group_order[thing_index(thing)] = io.group_count++;
        break;
    case THING_CONTAINER:
        if (!write_value(container_kind(thing), CONTAINER_KIND_MASK))
            return NULL;
        if (container_kind(thing) == CONTAINER_MINION_MAP) {
            mask = minion_map_mask;
            *carried = true;
        }
        io.container_order[thing_index(thing)] = io.container_count++;
        break;
    case THING_PROJECTILE:
        if (io.in_group) {
            mask = carried_projectile_mask;
            *carried = true;
        }
        break;
    }
    return mask;
}

/* Writes the things that thing holds after its record: possessions, contents, payload, or the explosion event. */
static bool write_held_things(uint16_t thing, const uint8_t *mask, bool carried)
{
    switch (thing_type(thing)) {
    case THING_GROUP: {
        io.in_group = true;
        bool ok = write_thing_list(thing_word(thing, GROUP_POSSESSIONS), mask != group_mask, true);
        io.in_group = false;
        return ok;
    }
    case THING_CONTAINER: {
        uint16_t contents = thing_word(thing, CONTAINER_CONTENTS);
        if (carried) {
            bool linked = contents != THING_NONE && contents != THING_ENDOFLIST;
            return write_value(linked, MORE_MASK) && (!linked || defer_thing(thing));
        }
        bool moneybox = is_moneybox(thing);
        if (moneybox)
            io.misc_mask = moneybox_misc_mask;
        bool ok = write_thing_list(contents, false, true);
        if (moneybox)
            io.misc_mask = misc_mask;
        return ok;
    }
    case THING_PROJECTILE: {
        if (carried)
            return defer_thing(thing);
        io.in_projectile = true;
        bool ok = write_thing_list(thing_word(thing, PROJECTILE_PAYLOAD), false, false);
        io.in_projectile = false;
        return ok;
    }
    case THING_EXPLOSION:
        for (int i = 0; i < g_timeline.count; i++)
            if (g_timeline.events[i].type == EVENT_EXPLOSION && le16(g_timeline.events[i].data + EVENT_THING2) == thing)
                return write_value(1, MORE_MASK) && write_value(i, INDEX_MASK);
        return write_value(0, MORE_MASK);
    }
    return true;
}

/* Writes what the record of thing stores beside its mask, the record, and the things that it holds. */
static bool write_thing(uint16_t thing)
{
    bool carried;
    const uint8_t *mask = write_thing_head(thing, &carried);
    return mask && write_packed(dungeon_thing_record(thing), mask, dungeon_thing_size(thing_type(thing)), 1) &&
           write_held_things(thing, mask, carried);
}

/* Writes a thing list: one thing without follow, else up to the end of the list. Things above
   the sensors start with a 1 bit and their type; a 0 bit ends a list. */
static bool write_thing_list(uint16_t thing, bool with_cell, bool follow)
{
    while (thing != THING_ENDOFLIST && thing != THING_NONE) {
        int type = thing_type(thing);
        if (type > THING_SENSOR) {
            if (!write_value(1, MORE_MASK) || !write_value(type, TYPE_MASK))
                return false;
            if (with_cell && type != THING_GROUP && !write_value(thing_cell(thing), CELL_MASK))
                return false;
        }
        if (type == THING_EXPLOSION && io.in_projectile) {
            if (!write_value(thing, SPELL_MASK))
                return false;
            break;
        }
        if (thing_masks[type] && !write_thing(thing))
            return false;
        if (!follow)
            break;
        thing = dungeon_next_thing(thing);
    }
    if (!follow && thing != THING_NONE)
        return true; /* like the original, also no end bit for an empty single thing */
    return write_value(0, MORE_MASK);
}

/* The mask of the stored square bits; *skip when a linked teleporter of a lower map stores the things. */
static uint8_t saved_bits_mask(int map, int x, int y, bool *skip)
{
    *skip = false;
    switch (square_type(dungeon_square(map, x, y))) {
    case SQUARE_PIT:
        return PIT_SAVED_BITS;
    case SQUARE_DOOR:
        return DOOR_SAVED_BITS;
    case SQUARE_FAKE_WALL:
        return FAKE_WALL_SAVED_BITS;
    case SQUARE_TELEPORTER: {
        int target = dungeon_teleporter_link_map(map, x, y);
        if (target < 0)
            return TELEPORTER_SAVED_BITS;
        *skip = target < map;
        return 0;
    }
    default:
        return 0;
    }
}

/* Writes the saved bits and the thing list of each square, column by column. */
static bool write_squares(void)
{
    for (int map = 0; map < dungeon_map_count(); map++)
        for (int x = 0; x < dungeon_map_width(map); x++)
            for (int y = 0; y < dungeon_map_height(map); y++) {
                bool skip;
                uint8_t mask = saved_bits_mask(map, x, y, &skip);
                if (mask && !write_packed(dungeon_square_byte(map, x, y), &mask, 1, 1))
                    return false;
                if (!skip && !write_thing_list(dungeon_square_first_thing(map, x, y), true, true))
                    return false;
            }
    return true;
}

/* The timeline events that move a creature group hold the group. */
static bool is_move_group_event(const TimelineEvent *e)
{
    return e->type == EVENT_MOVE_GROUP || e->type == EVENT_MOVE_GROUP_2;
}

/* Writes the things of the deferred minion maps and carried projectiles as bit stream positions. */
static bool write_deferred(void)
{
    for (int i = 0; i < io.deferred_count; i++) {
        uint16_t thing = io.deferred[i];
        uint16_t target = thing_index(thing_word(thing, CONTAINER_CONTENTS));
        bool of_group = thing_type(thing) == THING_CONTAINER;
        /* fix: the original reads past its table for a minion whose link is no container (the minion items) */
        uint16_t order = 0;
        if (target < dungeon_thing_count(of_group ? THING_GROUP : THING_CONTAINER))
            order = of_group ? io.group_order[target] : io.container_order[target];
        if (!write_value(order, INDEX_MASK))
            return false;
    }
    return true;
}

/* Fills the game record of the save from the game state. */
static void pack_game_record(uint8_t *r)
{
    memset(r, 0, GAME_RECORD_SIZE);
    put_le32(r + REC_GAME_TIME, g_game.game_time);
    put_le32(r + REC_RANDOM_SEED, g_random_seed);
    put_le16(r + REC_CHAMPIONS, g_game.champion_count);
    put_le16(r + REC_PARTY_X, g_game.party_x);
    put_le16(r + REC_PARTY_Y, g_game.party_y);
    put_le16(r + REC_PARTY_DIR, g_game.party_dir);
    put_le16(r + REC_PARTY_MAP, g_game.party_map);
    put_le16(r + REC_LEADER, g_game.leader);
    put_le16(r + REC_TIMELINE_EVENTS, g_timeline.count);
    put_le32(r + REC_UNKNOWN_TIME_A, g_game.last_combat_time);
    put_le32(r + REC_UNKNOWN_TIME_B, g_game.first_recruit_time);
    put_le16(r + REC_MOVE_COOLDOWN, g_game.party_move_cooldown);
    put_le16(r + REC_UNKNOWN_COUNTDOWN, g_game.throw_cooldown);
    put_le16(r + REC_UNKNOWN_DIR, g_game.throw_dir);
    put_le16(r + REC_VOLUMES, (g_game.sound_volume & VOLUME_MASK) | (g_game.music_volume & VOLUME_MASK)
                                                                        << MUSIC_VOLUME_SHIFT);
    put_le32(r + REC_WEATHER_VISIBLE, g_game.weather_visible);
    r[REC_HEAVY_RAIN] = g_game.heavy_rain;
    r[REC_WIND_DIR] = g_game.wind_dir;
    r[REC_RAIN_INTENSITY] = g_game.rain_intensity;
    r[REC_SKY_DARKNESS] = g_game.sky_darkness;
    r[REC_CLOUD_COVER] = g_game.cloud_cover;
    r[REC_STORM_MULTIPLIER] = g_game.storm_multiplier;
    put_le16(r + REC_STORM_INTENSITY, g_game.storm_intensity);
    r[REC_STORM_STEP] = g_game.storm_step;
    r[REC_STORM_CURVE] = g_game.storm_curve;
    put_le32(r + REC_NEXT_HOUR_TIME, g_game.next_hour_time);
}

/* Sets the game state from the game record of the save. */
static void unpack_game_record(const uint8_t *r)
{
    g_game.game_time = le32(r + REC_GAME_TIME);
    g_random_seed = le32(r + REC_RANDOM_SEED);
    g_game.champion_count = le16(r + REC_CHAMPIONS);
    g_game.party_x = le16(r + REC_PARTY_X);
    g_game.party_y = le16(r + REC_PARTY_Y);
    g_game.party_dir = le16(r + REC_PARTY_DIR);
    g_game.party_map = le16(r + REC_PARTY_MAP);
    g_game.leader = le16(r + REC_LEADER);
    g_timeline.count = le16(r + REC_TIMELINE_EVENTS);
    g_game.last_combat_time = le32(r + REC_UNKNOWN_TIME_A);
    g_game.first_recruit_time = le32(r + REC_UNKNOWN_TIME_B);
    g_game.party_move_cooldown = le16(r + REC_MOVE_COOLDOWN);
    g_game.throw_cooldown = le16(r + REC_UNKNOWN_COUNTDOWN);
    g_game.throw_dir = le16(r + REC_UNKNOWN_DIR);
    g_game.sound_volume = r[REC_VOLUMES] & VOLUME_MASK;
    g_game.music_volume = r[REC_VOLUMES] >> MUSIC_VOLUME_SHIFT & VOLUME_MASK;
    g_game.weather_visible = r[REC_WEATHER_VISIBLE];
    g_game.heavy_rain = r[REC_HEAVY_RAIN];
    g_game.wind_dir = r[REC_WIND_DIR];
    g_game.rain_intensity = r[REC_RAIN_INTENSITY];
    g_game.sky_darkness = r[REC_SKY_DARKNESS];
    g_game.cloud_cover = r[REC_CLOUD_COVER];
    g_game.storm_multiplier = r[REC_STORM_MULTIPLIER];
    g_game.storm_intensity = le16(r + REC_STORM_INTENSITY);
    g_game.storm_step = r[REC_STORM_STEP];
    g_game.storm_curve = r[REC_STORM_CURVE];
    g_game.next_hour_time = le32(r + REC_NEXT_HOUR_TIME);
}

/* The global words and the timeline events are stored in host order; the host is little-endian like the save file. */
static bool write_state(void)
{
    uint8_t record[GAME_RECORD_SIZE];
    timeline_compact();
    relink_events();
    pack_game_record(record);
    if (!write_packed(record, game_record_mask, GAME_RECORD_SIZE, 1) ||
        !write_packed(g_game.global_flags, all_bits, 1, GLOBAL_FLAG_BYTES) ||
        !write_packed(g_game.global_bytes, all_bits, 1, GLOBAL_BYTE_COUNT) ||
        !write_packed((const uint8_t *)g_game.global_words, all_bits, PACKED_WORD_BYTES, GLOBAL_WORD_COUNT) ||
        !write_packed(g_game.champions[0], champion_mask, CHAMPION_SIZE, g_game.champion_count) ||
        !write_packed(g_game.magic_light, magic_light_mask, MAGIC_LIGHT_SIZE, 1) ||
        !write_packed((const uint8_t *)g_timeline.events, event_mask, TIMELINE_EVENT_SIZE, g_timeline.count))
        return false;
    for (int c = 0; c < g_game.champion_count; c++)
        for (int s = 0; s < SLOT_COUNT; s++)
            if (!write_thing_list(held_thing(c, s), false, false))
                return false;
    if (!write_thing_list(g_game.leader_hand, false, false))
        return false;
    for (int i = 0; i < g_timeline.count; i++)
        if (is_move_group_event(&g_timeline.events[i]) &&
            !write_thing_list(le16(g_timeline.events[i].data + EVENT_THING2), false, false))
            return false;
    return write_squares() && write_deferred() && flush_bits();
}

/* Recursion: read_thing_list reads the things that a thing holds through read_held_things. */
static bool read_thing_list(const Position *square, uint16_t *list, bool with_cell, bool follow);

/* Puts thing into the data of a timeline event at offset. */
static bool set_event_thing(uint16_t event, int offset, uint16_t thing)
{
    if (event >= g_timeline.capacity)
        return false; /* fix: the original writes outside the timeline */
    put_le16(g_timeline.events[event].data + offset, thing);
    return true;
}

/* Reads what the record of a new thing stores beside its mask; returns the mask of the record, NULL on a read error. */
static const uint8_t *read_thing_head(uint16_t thing, bool *carried)
{
    int type = thing_type(thing);
    const uint8_t *mask = type == THING_MISC ? io.misc_mask : thing_masks[type];
    uint16_t value;
    *carried = false;
    switch (type) {
    case THING_GROUP:
        if (!read_value(&value, CREATURE_MASK))
            return NULL;
        group_set_creature(thing, (uint8_t)value);
        if (is_static_creature(thing))
            mask = static_group_mask;
        break;
    case THING_CONTAINER:
        if (!read_value(&value, CONTAINER_KIND_MASK))
            return NULL;
        container_set_kind(thing, value);
        if (value == CONTAINER_MINION_MAP) {
            mask = minion_map_mask;
            *carried = true;
        }
        break;
    case THING_PROJECTILE:
        if (io.in_group) {
            mask = carried_projectile_mask;
            *carried = true;
        }
        break;
    }
    return mask;
}

/* Reads the things that a new thing holds after its record: possessions, contents, payload, or the explosion event. */
static bool read_held_things(uint16_t thing, const uint8_t *mask, bool carried)
{
    uint16_t value;
    switch (thing_type(thing)) {
    case THING_GROUP: {
        uint16_t possessions = THING_ENDOFLIST;
        io.in_group = true;
        bool ok = read_thing_list(NULL, &possessions, mask != group_mask, true);
        io.in_group = false;
        set_thing_word(thing, GROUP_POSSESSIONS, possessions);
        return ok;
    }
    case THING_CONTAINER: {
        uint16_t contents = THING_ENDOFLIST;
        if (carried) {
            if (!read_value(&value, MORE_MASK))
                return false;
            set_thing_word(thing, CONTAINER_CONTENTS, contents);
            return !value || defer_thing(thing);
        }
        bool moneybox = is_moneybox(thing);
        if (moneybox)
            io.misc_mask = moneybox_misc_mask;
        bool ok = read_thing_list(NULL, &contents, false, true);
        if (moneybox)
            io.misc_mask = misc_mask;
        set_thing_word(thing, CONTAINER_CONTENTS, contents);
        return ok;
    }
    case THING_PROJECTILE: {
        if (carried)
            return defer_thing(thing);
        uint16_t payload = THING_ENDOFLIST;
        if (!set_event_thing(thing_word(thing, PROJECTILE_EVENT), EVENT_THING, thing))
            return false;
        io.in_projectile = true;
        bool ok = read_thing_list(NULL, &payload, false, false);
        io.in_projectile = false;
        set_thing_word(thing, PROJECTILE_PAYLOAD, payload);
        return ok;
    }
    case THING_EXPLOSION:
        if (!read_value(&value, MORE_MASK))
            return false;
        if (!value)
            return true;
        return read_value(&value, INDEX_MASK) && set_event_thing(value, EVENT_THING2, thing);
    }
    return true;
}

/* Reads what the record of a new thing stores beside its mask, the record, and the things that it holds. */
static bool read_thing(uint16_t thing)
{
    bool carried;
    const uint8_t *mask = read_thing_head(thing, &carried);
    return mask && read_packed(dungeon_thing_record(thing), mask, dungeon_thing_size(thing_type(thing)), 1, true) &&
           read_held_things(thing, mask, carried);
}

/* Reads a thing list into new records: with list onto the end of *list, else onto square. */
static bool read_thing_list(const Position *square, uint16_t *list, bool with_cell, bool follow)
{
    do {
        uint16_t more;
        uint16_t type;
        uint16_t cell = 0;
        if (!read_value(&more, MORE_MASK))
            return false;
        if (!more)
            return true;
        if (!read_value(&type, TYPE_MASK))
            return false;
        if (with_cell && type != THING_GROUP && !read_value(&cell, CELL_MASK))
            return false;
        if (type == THING_EXPLOSION && io.in_projectile) {
            uint16_t spell;
            if (!read_value(&spell, SPELL_MASK))
                return false;
            if (list)
                *list = spell | SPELL_THING_BASE;
            return true;
        }
        uint16_t thing = dungeon_unused_thing(type);
        if (thing == THING_NONE)
            return false;
        thing = thing_with_cell(thing, cell);
        if (list)
            dungeon_append_thing(thing, list);
        else if (!dungeon_link_thing_to_square(thing, square->map, square->x, square->y))
            return false;
        if (thing_masks[type] && !read_thing(thing))
            return false;
    } while (follow);
    return true;
}

/* Removes the creature groups and items from the squares and frees their records; the bit stream creates them again. */
static void free_movable_things(void)
{
    for (int map = 0; map < dungeon_map_count(); map++)
        for (int x = 0; x < dungeon_map_width(map); x++)
            for (int y = 0; y < dungeon_map_height(map); y++)
                for (uint16_t t = dungeon_square_first_thing(map, x, y); t != THING_ENDOFLIST;
                     t = dungeon_next_thing(t))
                    if (thing_type(t) > THING_SENSOR) {
                        set_thing_word(t, THING_NEXT, THING_ENDOFLIST);
                        dungeon_unlink_thing_from_square(t, map, x, y);
                        break;
                    }
    for (int type = THING_GROUP; type < THING_TYPE_COUNT; type++)
        for (int i = 0; i < dungeon_thing_count(type); i++)
            set_thing_word(thing_make(type, i), THING_NEXT, THING_NONE);
}

/* Reads the saved bits of each square, the bits of its fixed things, and its movable things. */
static bool read_squares(void)
{
    for (int16_t map = 0; map < dungeon_map_count(); map++)
        for (int16_t x = 0; x < dungeon_map_width(map); x++)
            for (int16_t y = 0; y < dungeon_map_height(map); y++) {
                bool skip;
                uint8_t mask = saved_bits_mask(map, x, y, &skip);
                if (mask && !read_packed(dungeon_square_byte(map, x, y), &mask, 1, 1, true))
                    return false;
                if (skip)
                    continue;
                for (uint16_t t = dungeon_square_first_thing(map, x, y); t != THING_ENDOFLIST;
                     t = dungeon_next_thing(t)) {
                    int type = thing_type(t);
                    if (!thing_masks[type])
                        continue;
                    uint16_t data;
                    if (type == THING_SENSOR && has_sensor_data(t)) {
                        if (!read_value(&data, SENSOR_DATA_MASK))
                            return false;
                        sensor_set_data(t, data);
                    }
                    if (!read_packed(dungeon_thing_record(t), thing_masks[type], dungeon_thing_size(type), 1, true))
                        return false;
                }
                Position square = {map, x, y};
                if (!read_thing_list(&square, NULL, true, true))
                    return false;
            }
    return true;
}

/* Links the deferred minion maps to their creature groups and the carried projectiles to their containers. */
static bool read_deferred(void)
{
    for (int i = 0; i < io.deferred_count; i++) {
        uint16_t index;
        uint16_t thing = io.deferred[i];
        if (!read_value(&index, INDEX_MASK))
            return false;
        int target_type = thing_type(thing) == THING_CONTAINER ? THING_GROUP : THING_CONTAINER;
        uint16_t target = thing_make(target_type, index);
        if (!dungeon_thing_valid(target))
            return false;
        set_thing_word(thing, CONTAINER_CONTENTS, target);
    }
    return true;
}

/* Reads the things of the champions, the leader's hand, the timeline events, and the squares; the items that the
   champions hold give their bonuses again. */
static bool read_things(void)
{
    free_movable_things();
    for (int c = 0; c < g_game.champion_count; c++)
        for (int s = 0; s < SLOT_COUNT; s++) {
            uint16_t slot = THING_ENDOFLIST;
            if (!read_thing_list(NULL, &slot, false, false))
                return false;
            set_held_thing(c, s, slot == THING_ENDOFLIST ? THING_NONE : slot);
        }
    g_game.leader_hand = THING_ENDOFLIST;
    if (!read_thing_list(NULL, &g_game.leader_hand, false, false))
        return false;
    if (g_game.leader_hand == THING_ENDOFLIST)
        g_game.leader_hand = THING_NONE;
    for (int c = 0; c < g_game.champion_count; c++)
        for (int s = 0; s < SLOT_COUNT; s++)
            champ_apply_item_effects(c, champion(c)->slots[s], s, EFFECT_MODE_LOAD);
    champ_apply_item_effects((int16_t)g_game.leader, g_game.leader_hand, -1, EFFECT_MODE_LOAD);
    for (int i = 0; i < g_timeline.count; i++) {
        TimelineEvent *e = &g_timeline.events[i];
        if (!is_move_group_event(e))
            continue;
        uint16_t group = THING_ENDOFLIST;
        if (io.format == 0 || !read_thing_list(NULL, &group, false, false))
            return false;
        put_le16(e->data + EVENT_THING2, group);
    }
    return read_squares() && read_deferred();
}

/* The magic effects that the timeline holds come back: magic light, invisibility, auras, poison, eaten items; then
   the champion loads. */
static void restore_timed_effects(void)
{
    memset(g_game.magic_light, 0, sizeof g_game.magic_light);
    for (int i = 0; i < g_timeline.used_end; i++) {
        const TimelineEvent *e = &g_timeline.events[i];
        int16_t v = (int16_t)le16(e->data + EVENT_VALUE);
        switch (e->type) {
        case EVENT_ITEM_EFFECT:
            timeline_event_apply_item_effect(e, EFFECT_MODE_RESTORE);
            break;
        case EVENT_MAGIC_LIGHT:
            if (v && v > -LIGHT_POWER_COUNT && v < LIGHT_POWER_COUNT) {
                save_set_magic_light((int16_t)(save_magic_light() + (v < 0 ? light_power[-v] : light_power[v] * -2)));
            }
            break;
        case EVENT_INVISIBILITY:
            g_game.magic_light[MAGIC_INVISIBILITY]++;
            break;
        case EVENT_AURA:
            for (int c = 0; c < g_game.champion_count; c++)
                if (e->priority >> c & 1 && champion(c)->health)
                    champion(c)->aura_value = (int16_t)(champion(c)->aura_value + v);
            break;
        case EVENT_POISON:
            champion(e->priority)->poisoned++;
            champion(e->priority)->poison = (int16_t)(champion(e->priority)->poison + v);
            break;
        }
    }
    for (int c = 0; c < g_game.champion_count; c++)
        champ_compute_load(c);
}

/* Reads the game record, the global variables, the champions, the timeline, and the things; then the timed effects
   come back. */
static bool read_state(void)
{
    uint8_t record[GAME_RECORD_SIZE];
    timeline_init(EVENT_SLACK + dungeon_thing_count(THING_GROUP) + dungeon_thing_count(THING_PROJECTILE) +
                  dungeon_thing_count(THING_EXPLOSION));
    weather_load(io.gdat); /* the weather part of the original dungeon_load; the record then sets the weather */
    if (!read_packed(record, game_record_mask, GAME_RECORD_SIZE, 1, false))
        return false;
    unpack_game_record(record);
    if (g_game.champion_count > CHAMPION_MAX || g_timeline.count > g_timeline.capacity)
        return false;
    memset(g_game.champions, 0, sizeof g_game.champions);
    if (!read_packed(g_game.global_flags, all_bits, 1, GLOBAL_FLAG_BYTES, false) ||
        !read_packed(g_game.global_bytes, all_bits, 1, GLOBAL_BYTE_COUNT, false) ||
        !read_packed((uint8_t *)g_game.global_words, all_bits, PACKED_WORD_BYTES, GLOBAL_WORD_COUNT, false) ||
        !read_packed(g_game.champions[0], champion_mask, CHAMPION_SIZE, g_game.champion_count, false) ||
        !read_packed(g_game.magic_light, magic_light_mask, MAGIC_LIGHT_SIZE, 1, false) ||
        !read_packed((uint8_t *)g_timeline.events, event_mask, TIMELINE_EVENT_SIZE, g_timeline.count, false))
        return false;
    for (int i = g_timeline.count; i < g_timeline.capacity; i++)
        g_timeline.events[i].type = EVENT_NONE;
    timeline_build_queue();
    if (!read_things())
        return false;
    restore_timed_effects();
    relink_events();
    return true;
}

/* What follows the AMAP block. */
enum SaveTail { TAIL_END, TAIL_CHEATS, TAIL_OTHER };

/* Writes the CHTS block with the cheats that are on; returns false on an error. */
static bool write_cheats(FILE *f)
{
    uint8_t block[CHEAT_BLOCK_SIZE];
    memcpy(block, CHEAT_MAGIC, CHEAT_MAGIC_SIZE);
    block[CHEAT_MAGIC_SIZE] = CHEAT_VERSION;
    block[CHEAT_BITS_AT] = game_get_cheats();
    return fwrite(block, 1, sizeof block, f) == sizeof block;
}

/* Reads the rest of f: nothing, a valid CHTS block (its cheat bits to *cheats), or other bytes. */
static enum SaveTail read_tail(FILE *f, uint8_t *cheats)
{
    uint8_t block[CHEAT_BLOCK_SIZE + 1]; /* one byte more: the block must end the file */
    size_t size = fread(block, 1, sizeof block, f);
    if (!size)
        return TAIL_END;
    if (size != CHEAT_BLOCK_SIZE || memcmp(block, CHEAT_MAGIC, CHEAT_MAGIC_SIZE) ||
        block[CHEAT_MAGIC_SIZE] != CHEAT_VERSION || block[CHEAT_BITS_AT] & ~CHEAT_ALL)
        return TAIL_OTHER;
    *cheats = block[CHEAT_BITS_AT];
    return TAIL_CHEATS;
}

/* Reads the blocks after the bit stream. The AMAP block counts only at the end of the file or before a valid CHTS
   block; the cheats come only from a valid CHTS block after it, and only in the advanced mode. */
static void read_extension(FILE *f)
{
    uint8_t cheats = 0;
    bool amap = seen_read(f);
    enum SaveTail tail = read_tail(f, &cheats);
    if (tail == TAIL_OTHER)
        seen_reset();
    game_set_cheats(amap && tail == TAIL_CHEATS && g_game_mode == GAME_MODE_ADVANCED ? cheats : 0);
}

SaveResult save_load(const char *path, const gdat_t *gdat)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return SAVE_ERR_IO;
    creature_free_active_groups();
    uint8_t header[SAVE_HEADER_SIZE];
    SaveResult result = SAVE_ERR_FORMAT;
    if (fread(header, 1, SAVE_HEADER_SIZE, f) == SAVE_HEADER_SIZE && dungeon_read(f, 0)) {
        start_io(f, gdat);
        if (gdat_current() != gdat)
            gdat_select(gdat); /* the item effects of the champions read the item attributes */
        champ_reset_step_state();
        io.format = le16(header + HEADER_FORMAT);
        result = read_state() ? SAVE_OK : SAVE_ERR_DATA;
        if (result == SAVE_OK)
            read_extension(f); /* the original stops at the last bit of the state: the blocks follow */
    }
    fclose(f);
    if (result != SAVE_OK)
        dungeon_free();
    return result;
}

SaveResult save_write(const char *path, const char *bak_path, const char *name, const gdat_t *gdat)
{
    uint8_t header[SAVE_HEADER_SIZE] = {0}; /* fix: without an old file the original writes uninitialized bytes */
    uint16_t *group_order = calloc(dungeon_thing_count(THING_GROUP) + 1, sizeof *group_order);
    uint16_t *container_order = calloc(dungeon_thing_count(THING_CONTAINER) + 1, sizeof *container_order);
    if (!group_order || !container_order)
        os_fatal("out of memory"); /* before the files change: the old save stays at path */
    remove(bak_path);
    rename(path, bak_path);
    FILE *old = fopen(bak_path, "rb");
    if (old) {
        if (fread(header, 1, SAVE_HEADER_SIZE, old) != SAVE_HEADER_SIZE)
            memset(header, 0, SAVE_HEADER_SIZE);
        fclose(old);
    }
    put_le16(header + HEADER_FORMAT, SAVE_FORMAT);
    size_t n = strnlen(name, SAVE_NAME_SIZE - 1);
    memcpy(header + SAVE_HEADER_NAME, name, n); /* the name bytes after its end mark keep the old header bytes */
    header[SAVE_HEADER_NAME + n] = 0;

    FILE *f = fopen(path, "wb");
    if (!f) {
        free(group_order);
        free(container_order);
        return SAVE_ERR_IO;
    }
    start_io(f, gdat);
    io.group_order = group_order;
    io.container_order = container_order;
    /* the records keep the indices of the active groups; the timeline loses their events (session_save) */
    bool ok = fwrite(header, 1, SAVE_HEADER_SIZE, f) == SAVE_HEADER_SIZE && dungeon_write(f);
    creature_deactivate_all_groups();
    ok =
        ok && write_state() &&
        (g_game_mode == GAME_MODE_VANILLA || (seen_write(f) && (g_game_mode != GAME_MODE_ADVANCED || write_cheats(f))));
    free(io.group_order);
    free(io.container_order);
    if (fclose(f) != 0)
        ok = false;
    if (!ok)
        remove(path);
    creature_activate_all_maps();
    return ok ? SAVE_OK : SAVE_ERR_IO;
}

void save_clear_party(void)
{
    g_game.champion_count = 0;
    g_game.leader_hand = THING_NONE;
}

void save_set_party_position(int16_t x, int16_t y, int16_t dir, int16_t map)
{
    g_game.party_x = (uint16_t)x;
    g_game.party_y = (uint16_t)y;
    g_game.party_dir = (uint16_t)dir;
    g_game.party_map = (uint16_t)map;
}

void save_set_sound_volume(uint8_t volume)
{
    g_game.sound_volume = volume;
}

void save_set_music_volume(uint8_t volume)
{
    g_game.music_volume = volume;
}
