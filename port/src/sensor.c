/* Floor and wall sensors, square events, doors, pits, teleporters, and fake walls. */
#include <string.h>
#include "action.h"
#include "champion.h"
#include "creature.h"
#include "draw.h"
#include "dungeon.h"
#include "gdat.h"
#include "hooks.h"
#include "item.h"
#include "move.h"
#include "random.h"
#include "save.h"
#include "sensor.h"
#include "text.h"

#define CAT_DOOR 0x0e
#define CAT_ORNAMENT 9
#define DOOR_KEY_STRENGTH 0x0e      /* door value: the strength that breaks the door */
#define DOOR_KEY_DAMAGE 0x0f        /* door value: the damage of a closing door */
#define ORNAMENT_KEY_ITEM 0x0e      /* ornament value: the item that the ornament takes */
#define ORNAMENT_KEY_ALTAR 0x0c     /* ornament value: an altar that brings back a champion from its bones */
#define BONES_INDEX 0               /* miscellaneous item 0: the bones of a champion */

/* Sensor record: type and data word, flags word, target word. */
#define SENSOR_TARGET 6
#define SENSOR_BUSY 0x0001
#define SENSOR_STATE_SHIFT 2
#define SENSOR_EFFECT_SHIFT 3
#define SENSOR_INVERTED 0x0020
#define SENSOR_AUDIBLE 0x0040
#define SENSOR_DELAY_SHIFT 7
#define SENSOR_DELAY_MASK 0x0f
#define SENSOR_ORNAMENT_SHIFT 12
/* Floor sensor types. */
enum {
    FLOOR_ANY = 1, FLOOR_PARTY_OR_CREATURE, FLOOR_PARTY, FLOOR_ITEM, FLOOR_CREATURE = 7, FLOOR_PARTY_POSSESSES
};
/* Wall sensor types of objects put on a wall cell. */
enum { WALL_ITEM_HOLDER = 0x1a, WALL_ANY_ITEM = 0x29, WALL_ITEM = 0x2a, WALL_NOT_ITEM = 0x2b };
/* Text record: bits 1-2 kind, bits 3-10 value, bits 11-15 subtype. */
#define TEXT_SHOWN 0x0001
#define TEXT_KIND_MASK 6
#define TEXT_KIND_DATA 2
#define TEXT_VALUE_SHIFT 3
#define TEXT_SUBTYPE_SHIFT 11
enum { TEXT_ITEM_HOLDER = 4, TEXT_ITEM_HOLDER_2 = 8, TEXT_RANDOM_PIT = 9, TEXT_FALL_RISK = 10 };

/* Door record bits. */
#define DOOR_BREAKABLE_BY_MAGIC 0x0080
#define DOOR_BREAKABLE 0x0100
#define DOOR_OPENING 0x0200
#define DOOR_MOVING 0x0400
#define DOOR_ACTIVE 0x1000
#define DOOR_TALL 0x0020                /* a closing door hits creatures of any height */
enum { DOOR_OPEN = 0, DOOR_CLOSED = 4, DOOR_BROKEN = 5 };

static uint8_t *square_byte(int x, int y)
{
    return dungeon_square_byte(party.cur_map, x, y);
}

static uint16_t first_thing(int x, int y)
{
    return dungeon_square_first_thing(party.cur_map, x, y);
}

static uint32_t event_time(uint32_t time)
{
    return (time & 0xffffff) | (uint32_t)(uint8_t)party.cur_map << 24;
}

/* The door type (0 or 1 of the map) of a door thing, 0xff for none. */
static int door_type(uint16_t door)
{
    uint16_t flags = dungeon_map_flags(party.cur_map), graphics = dungeon_map_graphics(party.cur_map);
    if (!(thing_word(door, DOOR_DATA) & 1)) return flags & 0x80 ? graphics >> 8 & 0xf : 0xff;
    return flags & 0x100 ? graphics >> 12 : 0xff;
}

static uint16_t door_value(uint16_t door, int key)
{
    return gdat_entry_index(draw_gdat(), CAT_DOOR, door_type(door), GDAT_TYPE_VALUE, key);
}

static int item_holder_item(int ornament)
{
    return gdat_entry_index(draw_gdat(), CAT_ORNAMENT, ornament, GDAT_TYPE_VALUE, ORNAMENT_KEY_ITEM);
}

/* The wall ornament (GDAT index) of a sensor, 0xff none. */
static int sensor_wall_ornament(uint16_t sensor)
{
    int ordinal = thing_word(sensor, SENSOR_FLAGS) >> SENSOR_ORNAMENT_SHIFT;
    return ordinal ? dungeon_map_wall_ornaments(party.cur_map)[ordinal - 1] : 0xff;
}

static int sensor_floor_ornament(uint16_t sensor)
{
    int ordinal = thing_word(sensor, SENSOR_FLAGS) >> SENSOR_ORNAMENT_SHIFT;
    return ordinal ? dungeon_map_floor_ornaments(party.cur_map)[ordinal - 1] : 0xff;
}

/* True when the party carries an item of the item index type. */
static int item_container_holds_type(uint16_t thing, int type)
{
    if (thing_type(thing) != THING_CONTAINER || item_gdat_index(thing) >= 8) return 0;
    for (uint16_t t = thing_word(thing, CONTAINER_CONTENTS); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        if (item_get_index(t) == type || item_container_holds_type(t, type)) return 1;
    return 0;
}

static int item_party_possesses_type(int type)
{
    for (int i = 0; i < game.champion_count; i++) {
        Champion *c = champion(i);
        if (!c->health) continue;
        for (int s = 0; s < SLOT_COUNT; s++)
            if (item_get_index(c->slots[s]) == type || item_container_holds_type(c->slots[s], type)) return 1;
    }
    if (action.mode == ACTION_MODE_CONTAINER)
        for (int s = 0; s < CONTAINER_SLOTS; s++)
            if (action.open_items[s] != THING_NONE && item_get_index(action.open_items[s]) == type) return 1;
    return item_get_index(game.leader_hand) == type || item_container_holds_type(game.leader_hand, type);
}

/* True when a static creature group holds an item of the item index type. */
static int item_find_in_possessions(uint16_t group, int type)
{
    for (uint16_t t = thing_word(group, GROUP_POSSESSIONS); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        if (item_get_index(t) == type) return 1;
    return 0;
}

void sensor_schedule_square_event(int x, int y, int cell, int effect, uint32_t time)
{
    TimelineEvent e = {.time_map = event_time(time), .type = EVENT_SQUARE};
    e.priority = effect == EFFECT_SET ? 1 : effect == EFFECT_CLEAR || effect == EFFECT_HOLD ? 3 : 2;
    e.data[EVENT_X] = (uint8_t)x;
    e.data[EVENT_Y] = (uint8_t)y;
    e.data[EVENT_CELL] = (uint8_t)cell;
    e.data[EVENT_EFFECT] = (uint8_t)effect;
    timeline_add_event(&e);
}

void sensor_trigger_target(uint16_t sensor, int effect, int delay)
{
    uint16_t target = thing_word(sensor, SENSOR_TARGET), flags = thing_word(sensor, SENSOR_FLAGS);
    sensor_schedule_square_event(target >> 6 & 0x1f, target >> 11, target >> 4 & 3, effect,
                                 (uint32_t)((int16_t)delay + (flags >> SENSOR_DELAY_SHIFT & SENSOR_DELAY_MASK) + game.game_time));
}

int sensor_door_try_destroy(int x, int y, int strength, int magic, int delay)
{
    uint16_t door = first_thing(x, y);
    uint16_t w = thing_word(door, DOOR_DATA);
    if (!(magic ? w & DOOR_BREAKABLE_BY_MAGIC : w & DOOR_BREAKABLE)) return 0;
    if ((int16_t)strength < (int16_t)door_value(door, DOOR_KEY_STRENGTH)) return 0;
    uint8_t *square = square_byte(x, y);
    if ((*square & SQUARE_DOOR_STATE) != DOOR_CLOSED) return 0;
    if (delay) {
        /* fix: the original leaves 4 data bytes of the event uninitialized */
        TimelineEvent e = {.time_map = event_time((uint16_t)delay + game.game_time), .type = EVENT_DOOR_DESTRUCTION};
        e.data[EVENT_X] = (uint8_t)x;
        e.data[EVENT_Y] = (uint8_t)y;
        timeline_add_event(&e);
    } else {
        *square = (uint8_t)((*square & ~SQUARE_DOOR_STATE) | DOOR_BROKEN);
    }
    return 1;
}

/* A sensor fires: the inverted flag, then the effect (hold follows the trigger), then its target. */
static void sensor_fire(uint16_t sensor, int trigger, int floor, int x, int y)
{
    uint16_t flags = thing_word(sensor, SENSOR_FLAGS);
    trigger ^= flags >> 5 & 1;
    int effect = flags >> SENSOR_EFFECT_SHIFT & 3;
    if (effect == EFFECT_HOLD) effect = !trigger;
    else if (!trigger) return;
    if (flags & SENSOR_AUDIBLE)
        sound_play_with_fallback(floor ? 10 : 9, floor ? sensor_floor_ornament(sensor) : sensor_wall_ornament(sensor),
                                 0x88, 0xfe, x, y, 1, 0x8c, 0x80);
    sensor_trigger_target(sensor, effect, 0);
}

/* The party leaves a square with a fall risk text: by the champion loads, it may fall to the next level. */
static void sensor_fall_risk(uint16_t text, int x, int y, int on_square)
{
    int load = 0;
    for (int i = 0; i < game.champion_count; i++) {
        Champion *c = champion(i);
        if (c->health) load = (int16_t)(load + (uint16_t)champ_get_load(i) / (int)((uint16_t)champ_get_max_load(c) >> 1));
    }
    uint16_t w = thing_word(text, TEXT_DATA);
    int chance = (int16_t)(load * 10 + (w & TEXT_SHOWN ? 0x32 : 0x19));
    if (chance > 0x5a) chance = 0x5a;
    if (!on_square && (int)rand_below(100) < chance) {
        /* fix: the original leaves the high bits of the event data uninitialized */
        TimelineEvent e = {.time_map = event_time(game.game_time), .type = EVENT_TELEPORT_PARTY};
        uint16_t target = (uint16_t)((x & 0x1f) | (y & 0x1f) << 5 | (game.party_dir & 3) << 10);
        e.data[0] = (uint8_t)target;
        e.data[1] = (uint8_t)(target >> 8);
        e.data[2] = (uint8_t)game.party_map;
        timeline_add_event(&e);
        int c = rand_2bits();
        if (!champion(c)->health) c = game.leader;
        sound_play_with_fallback(GDAT_CHAMPION_CATEGORY, champion(c)->portrait, 0x82, 0xfe, game.party_x, game.party_y, 1,
                                 0x69, 200);
    } else {
        sound_request(10, (uint8_t)(w >> TEXT_VALUE_SHIFT), 0x88, 0x8c, 0x80, game.party_x, game.party_y, 1);
    }
}

void sensor_process_floor(int x, int y, uint16_t thing, int on_square, int enter, int possessions)
{
    int type = -1, item = -1;
    uint16_t holder = THING_NONE;  /* a static creature whose possessions take the thing */
    if (thing != THING_NONE) {
        type = thing_type(thing);
        item = item_get_index(thing);
        uint16_t g = creature_group_at(party.cur_map, x, y);
        if (g != THING_NONE && possessions) {
            const CreatureInfo *info = creature_group_info(g);
            if (!enter || (info->flags & CREATURE_STATIC && info->possessions & CREATURE_POSSESSIONS_ON_FLOOR)) holder = g;
        }
    }
    if (!enter && type != -1) {
        if (holder != THING_NONE) dungeon_unlink_thing_from_list(thing, dungeon_thing_record(holder) + GROUP_POSSESSIONS);
        else dungeon_unlink_thing_from_square(thing, party.cur_map, x, y);
    }
    int cell = square_type(dungeon_square(party.cur_map, x, y)) == SQUARE_WALL ? thing >> THING_CELL_SHIFT : -1;
    int other_item = 0, same_item = 0, creature = 0, items = 0;
    for (uint16_t t = first_thing(x, y); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        int tt = thing_type(t);
        if (cell == -1) {
            if (tt == THING_GROUP && !creature_is_flying(t)) {
                if (creature_group_info(t)->flags & CREATURE_STATIC && item_find_in_possessions(t, item)) same_item = 1;
                creature = 1;
            } else if (tt == THING_TEXT && type == -1 && enter && !on_square) {
                uint16_t w = thing_word(t, TEXT_DATA);
                if (!(w & TEXT_KIND_MASK) && w & TEXT_SHOWN) {
                    char text[TEXT_SIZE];
                    text_print_message(UI_WHITE, text_decode_dungeon(text, sizeof text, t, 1));
                }
            } else if (tt > THING_GROUP && tt < THING_PROJECTILE) {
                items = 1;
                same_item |= item_get_index(t) == item;
                other_item |= item_get_index(t) != item;
            }
        } else if (cell == t >> THING_CELL_SHIFT && tt > THING_GROUP) {
            items = 1;
            same_item |= item_get_index(t) == item;
            other_item |= item_get_index(t) != item;
        }
    }
    if (enter && type != -1) {
        if (holder != THING_NONE) dungeon_append_thing(thing, (uint16_t *)(void *)(dungeon_thing_record(holder) + GROUP_POSSESSIONS));
        else dungeon_link_thing_to_square(thing, party.cur_map, x, y);
    }
    for (uint16_t t = first_thing(x, y); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (cell != -1 && item_gdat_index(t) != 0xff
            && gdat_entry_index(draw_gdat(), CAT_ORNAMENT, item_gdat_index(t), GDAT_TYPE_VALUE, ORNAMENT_KEY_ALTAR)
            && item_gdat_category(thing) == GDAT_MISC_CATEGORY && item_gdat_index(thing) == BONES_INDEX) {
            int champ = item_charges(thing);
            if (champ < 0 || champ >= game.champion_count) return;
            TimelineEvent e = {.time_map = event_time(game.game_time + 1), .type = EVENT_ALTAR_REBIRTH,
                               .priority = (uint8_t)champ};
            e.data[0] = (uint8_t)x;
            e.data[1] = (uint8_t)y;
            e.data[2] = (uint8_t)cell;
            e.data[3] = 2;
            timeline_add_event(&e);
            return;
        }
        int tt = thing_type(t);
        if (tt >= THING_GROUP) return;
        if (tt == THING_SENSOR) {
            uint16_t w = thing_word(t, SENSOR_TYPE_DATA), flags = thing_word(t, SENSOR_FLAGS);
            int kind = w & SENSOR_TYPE_MASK, data = w >> SENSOR_DATA_SHIFT;
            if (!kind) continue;
            int trigger = enter;
            if (cell == -1) {
                switch (kind) {
                case FLOOR_ANY:
                    if (on_square || items || creature) continue;
                    break;
                case FLOOR_PARTY_OR_CREATURE:
                    if (type > THING_GROUP || on_square || creature) continue;
                    break;
                case FLOOR_PARTY:
                    if (type != -1 || !game.champion_count) continue;
                    if (!data) {
                        if (on_square) continue;
                    } else {
                        trigger = enter ? data == game.party_dir + 1 : 0;
                    }
                    break;
                case FLOOR_ITEM:
                    if (data != item || same_item) continue;
                    break;
                case FLOOR_CREATURE:
                    if (type > THING_GROUP || type == -1 || creature) continue;
                    break;
                case FLOOR_PARTY_POSSESSES:
                    if (type != -1) continue;
                    trigger = item_party_possesses_type(data);
                    break;
                default:
                    continue;
                }
            } else {
                if (cell != t >> THING_CELL_SHIFT) continue;
                switch (kind) {
                case WALL_ANY_ITEM:
                    if (items) continue;
                    break;
                case WALL_ITEM:
                    if (same_item || data != item) continue;
                    break;
                case WALL_NOT_ITEM:
                    if (other_item || data == item) continue;
                    break;
                case WALL_ITEM_HOLDER:
                    if (same_item || (flags >> SENSOR_STATE_SHIFT & 1) == enter) continue;
                    if (item != item_holder_item(sensor_wall_ornament(t))) continue;
                    set_thing_word(t, SENSOR_FLAGS, (uint16_t)((flags & ~(1 << SENSOR_STATE_SHIFT)) | (enter & 1) << SENSOR_STATE_SHIFT));
                    break;
                default:
                    continue;
                }
            }
            sensor_fire(t, trigger, cell == -1, x, y);
        } else if (tt == THING_TEXT) {
            uint16_t w = thing_word(t, TEXT_DATA);
            if ((w & TEXT_KIND_MASK) != TEXT_KIND_DATA) continue;
            int sub = w >> TEXT_SUBTYPE_SHIFT;
            if (cell == -1) {
                if (sub == TEXT_RANDOM_PIT) {
                    if (type == -1 && !on_square && game.champion_count && rand_below(100) < (w >> TEXT_VALUE_SHIFT & 0xff)
                        && (w & 1) != enter) {
                        sensor_schedule_square_event(x, y, 0, EFFECT_SET, game.game_time + 1);
                        sensor_schedule_square_event(x, y, 0, EFFECT_CLEAR, game.game_time + 5);
                    }
                } else if (sub == TEXT_FALL_RISK && type == -1 && !enter && game.champion_count) {
                    sensor_fall_risk(t, x, y, on_square);
                }
            } else if (cell == t >> THING_CELL_SHIFT && (sub == TEXT_ITEM_HOLDER || sub == TEXT_ITEM_HOLDER_2) && !same_item
                       && (w & 1) != enter && item == item_holder_item(w >> TEXT_VALUE_SHIFT & 0xff)) {
                set_thing_word(t, TEXT_DATA, (uint16_t)((w & ~1) | (enter & 1)));
            }
        }
    }
}

/* ---- Sensor effects ---- */

/* The new state of a two-state target: set 1, clear 0, toggle the state. */
static int sensor_apply_effect(int effect, int state)
{
    if (effect == EFFECT_SET) return 1;
    if (effect == EFFECT_TOGGLE) return state ^ 1;
    return 0;
}

/* True when an event effect activates a sensor: set, or clear for an inverted sensor. */
static int sensor_activated(uint16_t flags, int effect)
{
    return flags & SENSOR_INVERTED ? effect == EFFECT_CLEAR : effect == EFFECT_SET;
}

static int sensor_own_effect(uint16_t flags)
{
    return flags >> SENSOR_EFFECT_SHIFT & 3;
}

static void set_sensor_flags(uint16_t sensor, uint16_t flags)
{
    set_thing_word(sensor, SENSOR_FLAGS, flags);
}

/* A relay: passes the event on to its target after the delay in its data (scaled: data << delay bits). */
static void sensor_relay_delayed(const TimelineEvent *e, uint16_t sensor, int scaled)
{
    uint16_t w = thing_word(sensor, SENSOR_TYPE_DATA), flags = thing_word(sensor, SENSOR_FLAGS);
    int effect = e->data[EVENT_EFFECT];
    if (flags & 4 && !sensor_activated(flags, effect)) return;
    unsigned data = w >> SENSOR_DATA_SHIFT, bits = flags >> SENSOR_DELAY_SHIFT & SENSOR_DELAY_MASK;
    uint32_t time = scaled ? (data << bits) + game.game_time : game.game_time + data + bits;
    uint16_t target = thing_word(sensor, SENSOR_TARGET);
    sensor_schedule_square_event(target >> 6 & 0x1f, target >> 11, target >> 4 & 3,
                                 flags & 4 ? sensor_own_effect(flags) : effect, time);
}

/* Passes the event effect (hold sensors) or the own effect of the sensor to its target. */
static void sensor_forward_effect(const TimelineEvent *e, uint16_t sensor, int delay)
{
    uint16_t flags = thing_word(sensor, SENSOR_FLAGS);
    int effect = e->data[EVENT_EFFECT];
    if (sensor_own_effect(flags) == EFFECT_HOLD) {
        if (!(flags & SENSOR_INVERTED)) {
            sensor_trigger_target(sensor, effect, delay);
            return;
        }
        sensor_trigger_target(sensor, effect, 0);
        if (!delay) return;
        static const uint8_t reverse[3] = {EFFECT_CLEAR, EFFECT_SET, EFFECT_TOGGLE};
        sensor_trigger_target(sensor, effect < 3 ? reverse[effect] : EFFECT_SET, delay);
        return;
    }
    if (!sensor_activated(flags, effect)) return;
    sensor_trigger_target(sensor, sensor_own_effect(flags), delay);
}

void dun_modify_global_var(int var, int value, int op)
{
    int old = dun_get_global_var(var), v = old;
    switch (op) {
    case 0: v = 1; break;
    case 1: v = 0; break;
    case 2: v = !old; break;
    case 3: v = old + value; break;
    case 4: v = old - value; break;
    case 6: v = value; break;
    }
    if (var < GLOBAL_FLAG_BYTES * 8) {
        uint8_t bit = (uint8_t)(1 << (var & 7));
        if ((int16_t)v) game.global_flags[var >> 3] |= bit;
        else game.global_flags[var >> 3] &= (uint8_t)~bit;
    } else if (var < 0x80) {
        v = (int16_t)v;
        game.global_bytes[var - 0x40] = (uint8_t)(v < 0 ? 0 : v > 0xff ? 0xff : v);
    } else if (var < 0xc0) {
        game.global_words[var - 0x80] = (uint16_t)v;
    }
}

int dun_get_global_var(int var)
{
    if (var < GLOBAL_FLAG_BYTES * 8) return game.global_flags[var >> 3] >> (var & 7) & 1;
    if (var < 0x80) return game.global_bytes[var - 0x40];
    if (var < 0xc0) return game.global_words[var - 0x80];
    return 0;
}

static void sensor_set_global_flag(const TimelineEvent *e, uint16_t sensor)
{
    uint16_t flags = thing_word(sensor, SENSOR_FLAGS);
    dun_modify_global_var(thing_word(sensor, SENSOR_TYPE_DATA) >> SENSOR_DATA_SHIFT, 1,
                          e->data[EVENT_EFFECT] + (flags & SENSOR_INVERTED ? 3 : 0));
    sensor_trigger_target(sensor, flags & 4 ? sensor_own_effect(flags) : e->data[EVENT_EFFECT], 0);
}

static void sensor_test_global_flag(const TimelineEvent *e, uint16_t sensor)
{
    uint16_t flags = thing_word(sensor, SENSOR_FLAGS);
    int set = dun_get_global_var(thing_word(sensor, SENSOR_TYPE_DATA) >> SENSOR_DATA_SHIFT) != 0;
    int inverted = (flags & SENSOR_INVERTED) != 0, effect = e->data[EVENT_EFFECT];
    if ((set != inverted && (effect == EFFECT_SET || effect == EFFECT_TOGGLE)) || (set == inverted && effect == EFFECT_CLEAR))
        sensor_trigger_target(sensor, flags & 4 ? sensor_own_effect(flags) : effect, 0);
}

/* The frame count of the animation of the ornament of a sensor (wall or floor). */
static unsigned sensor_get_ornament_cycle(uint16_t sensor, int wall)
{
    int category = wall ? CAT_ORNAMENT : CAT_ORNAMENT + 1;
    int ornament = wall ? sensor_wall_ornament(sensor) : sensor_floor_ornament(sensor);
    if (ornament == 0xff) return 1;
    uint16_t frames = gdat_entry_index(draw_gdat(), category, ornament, GDAT_TYPE_VALUE, 0x0d);
    if (frames) return frames & 0x7fff;
    char text[TEXT_SIZE];
    gdat_text(draw_gdat(), category, ornament, 0x0d, text, sizeof text);
    return text[0] && strlen(text) ? (uint16_t)strlen(text) : 1;
}

/* An audible animated ornament starts its repeating sound at the next start of its animation cycle. */
static void sensor_start_ambient_sound(uint16_t sensor, uint16_t thing, int x, int y, unsigned cycle, int wall)
{
    uint16_t w = thing_word(sensor, SENSOR_TYPE_DATA);
    if (!(thing_word(sensor, SENSOR_FLAGS) & SENSOR_AUDIBLE) || (int16_t)w < 0) return;
    int category = wall ? CAT_ORNAMENT : CAT_ORNAMENT + 1;
    int ornament = wall ? sensor_wall_ornament(sensor) : sensor_floor_ornament(sensor);
    uint32_t start = game.game_time + (w >> SENSOR_DATA_SHIFT & 0xff);
    uint16_t offset = gdat_entry_index(draw_gdat(), category, ornament, GDAT_TYPE_VALUE, 0x88);
    TimelineEvent e = {.time_map = event_time(cycle - (offset + start) % cycle + game.game_time),
                       .type = EVENT_SENSOR_AMBIENT_SOUND};
    e.data[0] = (uint8_t)x;
    e.data[1] = (uint8_t)y;
    e.data[2] = (uint8_t)thing;
    e.data[3] = (uint8_t)(thing >> 8);
    timeline_add_event(&e);
    set_thing_word(sensor, SENSOR_TYPE_DATA, (uint16_t)((w & 0x7f) | (0x100 | (w >> SENSOR_DATA_SHIFT & 0xff)) << SENSOR_DATA_SHIFT));
    sound_play_with_fallback(category, ornament, 0x88, 0xfe, x, y, 1, 0x8c, 0x80);
}

/* An animated ornament turns on (it runs) or off (it stops at the end of its cycle). */
static void sensor_toggle_animation(uint16_t thing, const TimelineEvent *e, uint16_t sensor, int wall)
{
    uint16_t flags = thing_word(sensor, SENSOR_FLAGS);
    int old = flags >> SENSOR_STATE_SHIFT & 1, now = sensor_apply_effect(e->data[EVENT_EFFECT], old) & 1;
    flags = (uint16_t)((flags & ~(1 << SENSOR_STATE_SHIFT)) | now << SENSOR_STATE_SHIFT);
    set_sensor_flags(sensor, flags);
    if (old != now) {
        unsigned cycle = (uint16_t)sensor_get_ornament_cycle(sensor, wall);
        if (flags & (1 << SENSOR_STATE_SHIFT)) {
            if (!(flags & SENSOR_BUSY)) {
                set_sensor_flags(sensor, flags | SENSOR_BUSY);
                if (party.cur_map == game.party_map) party.redraw |= REDRAW_VIEW;
                uint16_t w = thing_word(sensor, SENSOR_TYPE_DATA);
                unsigned frame = (cycle - game.game_time % cycle) % cycle & 0xff;
                set_thing_word(sensor, SENSOR_TYPE_DATA,
                               (uint16_t)((w & 0x7f) | (((w >> SENSOR_DATA_SHIFT) & 0x100) | frame) << SENSOR_DATA_SHIFT));
                sensor_start_ambient_sound(sensor, thing, e->data[EVENT_X], e->data[EVENT_Y], cycle, wall);
            }
        } else {
            unsigned r = ((thing_word(sensor, SENSOR_TYPE_DATA) >> SENSOR_DATA_SHIFT & 0xff) + game.game_time) % cycle;
            if (!(uint16_t)r) {
                if (party.cur_map == game.party_map) party.redraw |= REDRAW_VIEW;
                set_sensor_flags(sensor, flags & ~SENSOR_BUSY);
            } else {
                /* fix: the original leaves 2 of the data bytes uninitialized */
                TimelineEvent ev = {.time_map = event_time(game.game_time + (int16_t)(cycle - r)),
                                    .type = EVENT_SENSOR_ANIMATION_END};
                ev.data[2] = (uint8_t)thing;
                ev.data[3] = (uint8_t)(thing >> 8);
                timeline_add_event(&ev);
            }
        }
    }
    flags = thing_word(sensor, SENSOR_FLAGS);
    if (flags & SENSOR_INVERTED && sensor_own_effect(flags) == EFFECT_HOLD) sensor_trigger_target(sensor, e->data[EVENT_EFFECT], 0);
}

/* A sensor with a one-shot animation: it runs once (busy until its end); with its state set it also forwards. */
static void sensor_play_activation(const TimelineEvent *e, uint16_t sensor, uint16_t thing, int wall)
{
    uint16_t flags = thing_word(sensor, SENSOR_FLAGS);
    if (!(flags & SENSOR_BUSY)) {
        set_sensor_flags(sensor, flags | SENSOR_BUSY);
        set_thing_word(sensor, SENSOR_TYPE_DATA, thing_word(sensor, SENSOR_TYPE_DATA) & SENSOR_TYPE_MASK);
        TimelineEvent ev = {.time_map = event_time(game.game_time + 1), .type = EVENT_SENSOR_ANIMATION_STEP};
        ev.data[0] = (uint8_t)thing;
        ev.data[1] = (uint8_t)(thing >> 8);
        ev.data[2] = (uint8_t)wall;
        timeline_add_event(&ev);
        if (flags & SENSOR_AUDIBLE) {
            int x = e->data[EVENT_X], y = e->data[EVENT_Y];
            int category = wall ? CAT_ORNAMENT : CAT_ORNAMENT + 1;
            int ornament = wall ? sensor_wall_ornament(sensor) : sensor_floor_ornament(sensor);
            if (wall) {
                int side = e->data[EVENT_EFFECT] & 3;
                int dx = x + (side == DIR_EAST) - (side == DIR_WEST), dy = y + (side == DIR_SOUTH) - (side == DIR_NORTH);
                if (party.cur_map != party.square_map || dx != party.square_x || dy != party.square_y) {
                    x = dx;
                    y = dy;
                }
            }
            sound_play_with_fallback(category, ornament, 0x88, 0xfe, x, y,
                                     (int16_t)(gdat_entry_index(draw_gdat(), category, ornament, GDAT_TYPE_VALUE, 0x88) + 1),
                                     0x8c, 0x80);
        }
    }
    if (thing_word(sensor, SENSOR_FLAGS) & (1 << SENSOR_STATE_SHIFT)) sensor_forward_effect(e, sensor, 0);
}

/* Starts the repeating timer of a timer sensor; the type sets the period factor. */
static void sensor_schedule_timer(uint16_t sensor, uint16_t thing)
{
    uint16_t w = thing_word(sensor, SENSOR_TYPE_DATA);
    unsigned factor = 1;
    switch (w & SENSOR_TYPE_MASK) {
    case 0x1e: factor = 1; break;
    case 0x33: factor = 8; break;
    case 0x34: factor = 0x10; break;
    case 0x35: factor = 0x20; break;
    case 0x36: factor = 0x40; break;
    case 0x37: factor = 0x80; break;
    }
    if (!(w >> SENSOR_DATA_SHIFT)) return;
    /* fix: the original leaves bits 9-15 of the period word uninitialized; bit 8 is the timer phase */
    TimelineEvent e = {.time_map = event_time(game.game_time % ((w >> SENSOR_DATA_SHIFT) * (factor & 0xffff)) + game.game_time),
                       .type = EVENT_SENSOR_TIMER};
    e.data[0] = (uint8_t)thing;
    e.data[1] = (uint8_t)(thing >> 8);
    e.data[2] = (uint8_t)factor;
    timeline_add_event(&e);
    set_sensor_flags(sensor, thing_word(sensor, SENSOR_FLAGS) | SENSOR_BUSY);
}

/* ---- Square events ---- */

static void text_set_shown(uint16_t text, int effect)
{
    uint16_t w = thing_word(text, TEXT_DATA);
    int shown = effect == EFFECT_TOGGLE ? !(w & TEXT_SHOWN) : effect == EFFECT_SET;
    set_thing_word(text, TEXT_DATA, (uint16_t)((w & ~TEXT_SHOWN) | shown));
}

enum { TEXT_TOGGLED = 5, TEXT_LINKED = 7, TEXT_GENERATOR_SLOW = 0x13, TEXT_GENERATOR_FAST = 0x16, TEXT_SOUND = 0x17 };

/* A wall text of subtype 7 shows the same state as its twin on the level above or below. */
static void text_update_linked(const TimelineEvent *e, uint16_t text)
{
    uint16_t w = thing_word(text, TEXT_DATA);
    int delta = gdat_entry_index(draw_gdat(), CAT_ORNAMENT, w >> TEXT_VALUE_SHIFT & 0xff, GDAT_TYPE_VALUE, 0x11) ? -1 : 1;
    int x = e->data[EVENT_X], y = e->data[EVENT_Y];
    int map = dungeon_location_on_map(game.party_map, delta, &x, &y);
    if (map < 0) return;
    int saved = party.cur_map;
    dun_set_current_map(map);
    for (uint16_t t = first_thing(x, y); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_TEXT || t >> THING_CELL_SHIFT != e->data[EVENT_CELL]) continue;
        uint16_t tw = thing_word(t, TEXT_DATA);
        if ((tw & TEXT_KIND_MASK) == TEXT_KIND_DATA && (tw >> TEXT_SUBTYPE_SHIFT & 0x1f) == TEXT_LINKED)
            set_thing_word(t, TEXT_DATA, (uint16_t)((tw & ~1) | (w & 1)));
    }
    dun_set_current_map(saved);
}

/* A counter sensor reaches zero (or leaves it): returns 1 when its count is zero or below. */
static int counter_empty(uint16_t w)
{
    return !(w & 0xff80) || (w >> SENSOR_DATA_SHIFT & 0x100);
}

static void set_sensor_data(uint16_t sensor, unsigned data)
{
    uint16_t w = thing_word(sensor, SENSOR_TYPE_DATA);
    set_thing_word(sensor, SENSOR_TYPE_DATA, (uint16_t)((w & 0x7f) | (data & 0x1ff) << SENSOR_DATA_SHIFT));
}

static void timeline_event_wall_square(TimelineEvent *e)
{
    int x = e->data[EVENT_X], y = e->data[EVENT_Y], effect = e->data[EVENT_EFFECT];
    for (uint16_t t = first_thing(x, y); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR; t = dungeon_next_thing(t)) {
        if (t >> THING_CELL_SHIFT != e->data[EVENT_CELL]) continue;
        if (thing_type(t) == THING_TEXT) {
            uint16_t w = thing_word(t, TEXT_DATA);
            if (w & TEXT_KIND_MASK && (w & TEXT_KIND_MASK) != TEXT_KIND_DATA) continue;
            int sub = w >> TEXT_SUBTYPE_SHIFT & 0x1f;
            if (sub == TEXT_TOGGLED || sub == TEXT_LINKED) {
                text_set_shown(t, effect);
                if (sub == TEXT_LINKED) text_update_linked(e, t);
            } else if (sub == TEXT_SOUND && effect == EFFECT_SET) {
                sound_request(3, party.cur_map + 1, (uint8_t)(w >> TEXT_VALUE_SHIFT), 0x73, 200, x, y, 1);
            }
            continue;
        }
        if (thing_type(t) != THING_SENSOR) continue;
        uint16_t w = thing_word(t, SENSOR_TYPE_DATA), flags = thing_word(t, SENSOR_FLAGS);
        unsigned data = w >> SENSOR_DATA_SHIFT;
        uint16_t target = thing_word(t, SENSOR_TARGET);
        int kind = w & SENSOR_TYPE_MASK;
        switch (kind) {
        case 0x31:  /* a relay that stays busy for data ticks */
            if (flags & SENSOR_BUSY) break;
            {
                TimelineEvent ev = {.time_map = event_time(data + game.game_time), .type = EVENT_SENSOR_READY};
                ev.data[0] = (uint8_t)t;
                ev.data[1] = (uint8_t)(t >> 8);
                timeline_add_event(&ev);
            }
            set_sensor_flags(t, flags | SENSOR_BUSY);
            if (sensor_activated(flags, effect)) sensor_trigger_target(t, flags & 4 ? sensor_own_effect(flags) : effect, 0);
            break;
        case 0x2e:  /* a creature generator */
            if (effect != EFFECT_SET) break;
            {
                int dir = flags & 4 ? rand_2bits() : flags >> 3 & 3;
                uint16_t g = creature_create_group(data, 7, dir, target >> 6 & 0x1f, target >> 11);
                if (flags & SENSOR_INVERTED && g != THING_NONE) set_thing_word(g, 8, flags >> 7 & 0xf);
                if (flags & SENSOR_AUDIBLE) sound_request(0, 0, 0x89, 0x61, 0x80, target >> 6 & 0x1f, target >> 11, 1);
            }
            break;
        case 0x1d: {  /* a counter: clear counts up, set counts down */
            int before = counter_empty(w);
            if (effect == EFFECT_CLEAR) set_sensor_data(t, data + 1);
            else if (effect == EFFECT_SET && (!(flags & 4) || w & 0xff80)) set_sensor_data(t, data - 1);
            int after = counter_empty(thing_word(t, SENSOR_TYPE_DATA));
            if (after == before) break;
            if (sensor_own_effect(flags) == EFFECT_HOLD) sensor_trigger_target(t, after == (flags >> 5 & 1), 0);
            else if (after) sensor_trigger_target(t, sensor_own_effect(flags), 0);
            break;
        }
        case 0x2d: {  /* a countdown, or a random chance for the values above 400 */
            unsigned v = w & 0xff80;
            if (v && v <= 0xc800) {
                set_sensor_data(t, data - 1);
                sensor_trigger_target(t, effect, 0);
            } else if (v > 0xc800 && v < 0xfa00) {
                int hit = rand_below(100) < (unsigned)(data - 400);
                if (sensor_own_effect(flags) == EFFECT_HOLD) sensor_trigger_target(t, !hit, 0);
                else if (hit) sensor_trigger_target(t, effect, 0);
            }
            break;
        }
        case 0x1e: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37:  /* timers */
            flags = (uint16_t)((flags & ~(1 << SENSOR_STATE_SHIFT))
                               | (sensor_apply_effect(effect, flags >> SENSOR_STATE_SHIFT & 1) & 1) << SENSOR_STATE_SHIFT);
            set_sensor_flags(t, flags);
            if (!(flags & SENSOR_BUSY) && flags & (1 << SENSOR_STATE_SHIFT)) sensor_schedule_timer(t, t);
            break;
        case 0x20: case 0x45:
            sensor_relay_delayed(e, t, kind == 0x45);
            break;
        case 0x3d:
            sensor_forward_effect(e, t, data);
            break;
        case 0x07: case 0x08: case 0x09: case 0x0a: case 0x0e: case 0x0f:
            sensor_launch_missile(t, e);
            break;
        case 0x16: {  /* passes the event to a square of another map */
            int map = data & 0x3f, tx = target >> 6 & 0x1f, ty = target >> 11;
            TimelineEvent ev = {.time_map = (game.game_time & 0xffffff) | (uint32_t)map << 24, .type = EVENT_SQUARE,
                                .priority = e->priority};
            ev.data[EVENT_X] = (uint8_t)tx;
            ev.data[EVENT_Y] = (uint8_t)ty;
            ev.data[EVENT_CELL] = square_type(dungeon_square(map, tx, ty)) == SQUARE_WALL ? data >> 6 & 3 : 0;
            ev.data[EVENT_EFFECT] = (uint8_t)effect;
            timeline_add_event(&ev);
            break;
        }
        case 0x12:
            game_complete();
            break;
        case 0x32:
            sensor_play_activation(e, t, t, 1);
            break;
        case 0x41: {  /* an ornament that steps its animation frame */
            int frame = (int16_t)(effect == EFFECT_SET ? data + 1 : data - 1);
            set_sensor_data(t, (unsigned)(frame % (int)(uint16_t)sensor_get_ornament_cycle(t, 1)));
            break;
        }
        case 0x2c:
            sensor_toggle_animation(t, e, t, 1);
            break;
        case 0x26:
            set_sensor_flags(t, (uint16_t)((flags & ~(1 << SENSOR_STATE_SHIFT))
                                           | (sensor_apply_effect(effect, flags >> SENSOR_STATE_SHIFT & 1) & 1) << SENSOR_STATE_SHIFT));
            break;
        case 0x3b: case 0x47: case 0x40: case 0x48: case 0x49: case 0x3c:
            sensor_transfer_objects(e, t, kind);
            break;
        case 0x42:
            sensor_turn_creatures(e);
            break;
        case 0x43:
            sensor_set_global_flag(e, t);
            break;
        case 0x44:
            sensor_test_global_flag(e, t);
            break;
        case 0x46: {  /* sets bit 13 of the door at the target */
            uint16_t door = first_thing(target >> 6 & 0x1f, target >> 11);
            uint16_t d = thing_word(door, DOOR_DATA);
            if (effect == EFFECT_SET) d |= 0x2000;
            else if (effect == EFFECT_CLEAR) d &= ~0x2000;
            else if (effect == EFFECT_TOGGLE) d = (uint16_t)((d & ~0x2000) | (~d & 0x2000));
            set_thing_word(door, DOOR_DATA, d);
            break;
        }
        }
    }
}

static void timeline_event_floor_square(TimelineEvent *e)
{
    int x = e->data[EVENT_X], y = e->data[EVENT_Y], effect = e->data[EVENT_EFFECT];
    for (uint16_t t = first_thing(x, y); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR; t = dungeon_next_thing(t)) {
        if (thing_type(t) == THING_TEXT) {
            uint16_t w = thing_word(t, TEXT_DATA);
            if (w & TEXT_KIND_MASK && (w & TEXT_KIND_MASK) != TEXT_KIND_DATA) continue;
            int sub = w >> TEXT_SUBTYPE_SHIFT & 0x1f;
            if (sub == TEXT_TOGGLED) {
                text_set_shown(t, effect);
                uint16_t now = thing_word(t, TEXT_DATA);
                if (!(now & TEXT_KIND_MASK) && !(w & TEXT_SHOWN) && now & TEXT_SHOWN && dun_on_party_square(x, y)) {
                    char text[TEXT_SIZE];
                    text_print_message(UI_WHITE, text_decode_dungeon(text, sizeof text, t, 1));
                }
            } else if ((sub == TEXT_GENERATOR_SLOW || sub == TEXT_GENERATOR_FAST) && effect == EFFECT_SET) {
                unsigned base = sub == TEXT_GENERATOR_SLOW ? 0x200 : 0x2000;
                if (w & TEXT_SHOWN) base *= sub == TEXT_GENERATOR_SLOW ? 4 : 2;
                unsigned r = rand_next() & 0xffff;
                TimelineEvent ev = {.time_map = event_time(base + ((base * 2 - 1) & r) + game.game_time),
                                    .type = EVENT_SPAWN_CREATURE};
                ev.data[0] = (uint8_t)x;
                ev.data[1] = (uint8_t)y;
                ev.data[3] = (uint8_t)(w >> TEXT_VALUE_SHIFT);
                timeline_add_event(&ev);
            } else if (sub == TEXT_SOUND && effect == EFFECT_SET) {
                sound_request(3, party.cur_map + 1, (uint8_t)(w >> TEXT_VALUE_SHIFT), 0x73, 200, x, y, 1);
            }
            continue;
        }
        if (thing_type(t) != THING_SENSOR) continue;
        uint16_t w = thing_word(t, SENSOR_TYPE_DATA), flags = thing_word(t, SENSOR_FLAGS);
        uint16_t target = thing_word(t, SENSOR_TARGET);
        int kind = w & SENSOR_TYPE_MASK;
        switch (kind) {
        case 0x20: case 0x45:
            sensor_relay_delayed(e, t, kind == 0x45);
            break;
        case SENSOR_TELEPORTER_LINK:
            set_sensor_flags(t, (uint16_t)((flags & ~1) | (sensor_apply_effect(effect, flags & 1) & 1)));
            break;
        case 0x0b: case 0x28:
            sensor_affect_creatures_in_area(w >> SENSOR_DATA_SHIFT & 0xf, w >> 11, x, y, target >> 6 & 0x1f, target >> 11,
                                            kind, effect);
            break;
        case 0x2c:
            sensor_toggle_animation(t, e, t, 0);
            break;
        case 0x2e: {  /* moves the party to its target (or to this square) */
            int tx = x, ty = y;
            if (flags & 4) {
                tx = target >> 6 & 0x1f;
                ty = target >> 11;
            }
            int dir = flags & SENSOR_INVERTED ? flags >> 3 & 3 : ((flags >> 3 & 3) + game.party_dir) & 3;
            move_party_to(tx, ty, party.cur_map, dir);
            break;
        }
        case 0x32:
            sensor_play_activation(e, t, t, 0);
            break;
        case 0x3a:
            creature_update_anim_at(x, y, effect == EFFECT_SET);
            break;
        case 0x3b: case 0x40: case 0x47: case 0x48: case 0x49:
            sensor_transfer_objects(e, t, kind);
            break;
        case 0x3d:
            sensor_forward_effect(e, t, w >> SENSOR_DATA_SHIFT);
            break;
        case 0x42:
            sensor_turn_creatures(e);
            break;
        case 0x43:
            sensor_set_global_flag(e, t);
            break;
        case 0x44:
            sensor_test_global_flag(e, t);
            break;
        }
    }
}

/* A pit or teleporter turns on: the party, a creature, and the objects on it go through. */
static void move_square_contents_through(int x, int y)
{
    if (dun_on_party_square(x, y)) move_thing(THING_NONE, x, y, x, y);
    uint16_t g = creature_group_at(party.cur_map, x, y);
    if (g != THING_NONE) move_thing(g, x, y, x, y);
    uint16_t t = dungeon_square_first_object(party.cur_map, x, y);
    int count = 0;
    for (uint16_t u = t; u != THING_ENDOFLIST; u = dungeon_next_thing(u))
        if (thing_type(u) > THING_GROUP) count++;
    while (t != THING_ENDOFLIST && count) {
        uint16_t thing = t;
        t = dungeon_next_thing(thing);
        int type = thing_type(thing);
        if (type <= THING_GROUP) continue;
        count--;
        move_thing(thing, x, y, x, y);
        if (type == THING_PROJECTILE) {
            TimelineEvent *ev = &timeline.events[thing_word(thing, PROJECTILE_EVENT)];
            uint16_t pos = (uint16_t)(ev->data[2] | ev->data[3] << 8);
            pos = (uint16_t)((pos & 0xf000) | (party.dest_x & 0x1f) | (party.dest_y & 0x1f) << 5 | (party.dest_dir & 3) << 10);
            ev->data[2] = (uint8_t)pos;
            ev->data[3] = (uint8_t)(pos >> 8);
            uint16_t moved = (uint16_t)((thing & 0x3fff) | party.dest_cell << THING_CELL_SHIFT);
            ev->data[0] = (uint8_t)moved;
            ev->data[1] = (uint8_t)(moved >> 8);
            ev->time_map = (ev->time_map & 0xffffff) | (uint32_t)(uint16_t)party.dest_map << 24;
        } else if (type == THING_EXPLOSION) {
            for (int i = 0; i < timeline.used_end; i++) {
                TimelineEvent *ev = &timeline.events[i];
                if (ev->type != 0x19 || (uint16_t)(ev->data[2] | ev->data[3] << 8) != thing) continue;
                ev->data[0] = (uint8_t)party.dest_x;
                ev->data[1] = (uint8_t)party.dest_y;
                uint16_t moved = (uint16_t)((thing & 0x3fff) | party.dest_cell << THING_CELL_SHIFT);
                ev->data[2] = (uint8_t)moved;
                ev->data[3] = (uint8_t)(moved >> 8);
                ev->time_map = (ev->time_map & 0xffffff) | (uint32_t)(uint16_t)party.dest_map << 24;
            }
        }
    }
}

static void timeline_event_pit_or_teleporter(TimelineEvent *e, uint8_t on_bit)
{
    int x = e->data[EVENT_X], y = e->data[EVENT_Y];
    uint8_t *square = square_byte(x, y);
    int effect = e->data[EVENT_EFFECT];
    if (effect == EFFECT_TOGGLE) effect = (*square & on_bit) != 0;
    if (effect == EFFECT_SET) {
        *square |= on_bit;
        move_square_contents_through(x, y);
    } else {
        *square &= (uint8_t)~on_bit;
    }
}

static void timeline_event_door_square(TimelineEvent *e)
{
    uint8_t state = *square_byte(e->data[EVENT_X], e->data[EVENT_Y]) & SQUARE_DOOR_STATE;
    if (state == DOOR_BROKEN) return;
    uint16_t door = first_thing(e->data[EVENT_X], e->data[EVENT_Y]);
    uint16_t w = thing_word(door, DOOR_DATA);
    int effect = e->data[EVENT_EFFECT];
    if (w & DOOR_MOVING) {
        int opening = sensor_apply_effect(effect, w >> 9 & 1) & 1;
        w = (uint16_t)((w & ~DOOR_OPENING) | opening << 9);
        if (!opening) w &= ~DOOR_ACTIVE;
        set_thing_word(door, DOOR_DATA, w);
        return;
    }
    int move = 0;
    if (state == DOOR_OPEN) {
        if (effect == EFFECT_CLEAR || effect == EFFECT_TOGGLE) {
            w &= ~DOOR_OPENING;
            move = 1;
        }
    } else if (state == DOOR_CLOSED) {
        if (effect == EFFECT_SET || effect == EFFECT_TOGGLE) {
            w |= DOOR_OPENING;
            move = 1;
        }
    } else {
        w = (uint16_t)((w & ~DOOR_OPENING) | (effect == EFFECT_SET) << 9);
        move = 1;
    }
    w = (uint16_t)((w & ~DOOR_MOVING) | move << 10);
    if (move) {
        if (!(w & DOOR_OPENING)) w &= ~DOOR_ACTIVE;
        set_thing_word(door, DOOR_DATA, w);
        e->type = EVENT_DOOR_ANIMATION;
        timeline_add_event(e);
        return;
    }
    set_thing_word(door, DOOR_DATA, w);
}

static void timeline_event_fake_wall_square(TimelineEvent *e)
{
    int x = e->data[EVENT_X], y = e->data[EVENT_Y];
    uint8_t *square = square_byte(x, y);
    int effect = e->data[EVENT_EFFECT];
    if (effect == EFFECT_TOGGLE) effect = (*square & SQUARE_FAKE_WALL_OPEN) != 0;
    if (effect == EFFECT_CLEAR) {
        int blocked = dun_on_party_square(x, y);
        uint16_t g = creature_group_at(party.cur_map, x, y);
        if (g != THING_NONE && !(creature_group_info(g)->flags & CREATURE_NOT_DOOR_HIT)) blocked = 1;
        if (blocked) {
            e->time_map++;
            timeline_add_event(e);
        } else {
            *square &= (uint8_t)~SQUARE_FAKE_WALL_OPEN;
        }
    } else {
        *square |= SQUARE_FAKE_WALL_OPEN;
    }
    if (party.cur_map == game.party_map) party.redraw = REDRAW_ALL_VIEW;
}

void timeline_event_square(TimelineEvent *e)
{
    switch (square_type(dungeon_square(party.cur_map, e->data[EVENT_X], e->data[EVENT_Y]))) {
    case SQUARE_WALL: timeline_event_wall_square(e); break;
    case SQUARE_FLOOR: timeline_event_floor_square(e); break;
    case SQUARE_PIT:
        timeline_event_pit_or_teleporter(e, SQUARE_PIT_OPEN);
        timeline_event_floor_square(e);
        break;
    case SQUARE_DOOR: timeline_event_door_square(e); break;
    case SQUARE_TELEPORTER:
        if ((thing_word(first_thing(e->data[EVENT_X], e->data[EVENT_Y]), TELEPORTER_MAP) & 6) != 6)
            timeline_event_pit_or_teleporter(e, SQUARE_TELEPORTER_ON);
        timeline_event_floor_square(e);
        break;
    case SQUARE_FAKE_WALL: timeline_event_fake_wall_square(e); break;
    }
}

void timeline_event_door_destruction(TimelineEvent *e)
{
    uint8_t *square = square_byte(e->data[EVENT_X], e->data[EVENT_Y]);
    *square = (uint8_t)((*square & ~SQUARE_DOOR_STATE) | DOOR_BROKEN);
    if (party.cur_map == game.party_map) party.redraw = REDRAW_ALL_VIEW;
}

void timeline_event_door_animation(TimelineEvent *e)
{
    int x = e->data[EVENT_X], y = e->data[EVENT_Y];
    uint8_t *square = square_byte(x, y);
    int state = *square & SQUARE_DOOR_STATE;
    if (state == DOOR_BROKEN) return;
    if (party.cur_map == game.party_map) party.redraw = REDRAW_ALL_VIEW;
    uint16_t door = first_thing(x, y);
    uint16_t w = thing_word(door, DOOR_DATA);
    if (!(w & DOOR_MOVING)) return;
    e->time_map++;
    int again = 0;
    if (!(w & DOOR_OPENING)) {
        if (state == DOOR_CLOSED) goto stop;
        int tall = (w & DOOR_TALL) != 0, hit = 0;
        int16_t damage = (int16_t)door_value(door, DOOR_KEY_DAMAGE);
        if (dun_on_party_square(x, y) && state) {
            hit = 1;
            if (game.champion_count) {
                *square &= (uint8_t)~SQUARE_DOOR_STATE;
                int hurt = champ_damage_party((uint16_t)damage, 4, 2);
                for (int i = 0; i < 4; i++)
                    if (hurt >> i & 1)
                        sound_play_with_fallback(GDAT_CHAMPION_CATEGORY, champion(i)->portrait, 0x8a, 0xfe, x, y, 1, 100, 200);
            }
        }
        uint16_t g = creature_group_at(party.cur_map, x, y);
        if (g != THING_NONE) {
            const CreatureInfo *info = creature_group_info(g);
            int height = tall ? info->flags >> CREATURE_DOOR_SIZE_SHIFT & 3 : 1;
            if (!(info->flags & CREATURE_NOT_DOOR_HIT) && state >= height) {
                if (info->door_flags & CREATURE_DOOR_HALF_DAMAGE) {
                    damage >>= 1;
                    if (damage < 1) damage = 1;
                }
                creature_alert(g, x, y, 0x2006, 100, (uint16_t)damage);
                state = state ? state - 1 : 0;
                *square = (uint8_t)((*square & ~SQUARE_DOOR_STATE) | state);
                sound_play_with_fallback(0x0f, item_gdat_index(g), 0x82, 0xfe, x, y, 1, 0x46, 0x80);
                sound_play_with_fallback(0x15, 0xfe, 0x85, 0xfe, x, y, 1, 0x46, 0x80);
                hit = 1;
            }
        }
        if (hit) {
            again = 1;
            e->time_map++;
        }
    } else if (!state) {
        set_thing_word(door, DOOR_DATA, w & ~DOOR_MOVING);
        return;
    }
    if (!again) {
        state += w & DOOR_OPENING ? -1 : 1;
        *square = (uint8_t)((*square & ~SQUARE_DOOR_STATE) | (state & SQUARE_DOOR_STATE));
        if (w & DOOR_OPENING ? state != DOOR_OPEN : state != DOOR_CLOSED) again = 1;
        sound_play_with_fallback(CAT_DOOR, door_type(door), !again && state == DOOR_CLOSED ? 0x8f : 0x8e, 0xfe, x, y, 1, 0x5f,
                                 0x80);
    }
    if (again) {
        set_thing_word(door, DOOR_DATA, thing_word(door, DOOR_DATA) | DOOR_MOVING);
        timeline_add_event(e);
        return;
    }
stop:
    set_thing_word(door, DOOR_DATA, thing_word(door, DOOR_DATA) & ~DOOR_MOVING);
}

/* ---- Sensor timeline events ---- */

void timeline_event_sensor(TimelineEvent *e)
{
    uint16_t thing = (uint16_t)(e->data[0] | e->data[1] << 8);
    switch (e->type) {
    case EVENT_SENSOR_ANIMATION_STEP: {
        e->time_map++;
        unsigned cycle = (uint16_t)sensor_get_ornament_cycle(thing, e->data[2]);
        unsigned frame = ((thing_word(thing, SENSOR_TYPE_DATA) >> SENSOR_DATA_SHIFT) + 1) & 0x1ff;
        set_sensor_data(thing, frame);
        if (frame % cycle == 0) set_sensor_flags(thing, thing_word(thing, SENSOR_FLAGS) & ~SENSOR_BUSY);
        else timeline_add_event(e);
        break;
    }
    case EVENT_SENSOR_TIMER: {
        uint16_t flags = thing_word(thing, SENSOR_FLAGS);
        int running, effect;
        if (sensor_own_effect(flags) == EFFECT_HOLD) {
            int phase = !(e->data[3] & 1);
            e->data[3] = (uint8_t)((e->data[3] & ~1) | phase);
            running = phase | (flags >> SENSOR_STATE_SHIFT & 1);
            effect = !(e->data[3] & 1);
            sensor_trigger_target(thing, effect, 0);
        } else {
            running = flags >> SENSOR_STATE_SHIFT & 1;
            if (running) sensor_trigger_target(thing, sensor_own_effect(flags), 0);
        }
        if (!running) {
            set_sensor_flags(thing, thing_word(thing, SENSOR_FLAGS) & ~SENSOR_BUSY);
        } else {
            e->time_map += e->data[2] * (thing_word(thing, SENSOR_TYPE_DATA) >> SENSOR_DATA_SHIFT);
            timeline_add_event(e);
        }
        break;
    }
    case EVENT_SENSOR_ANIMATION_END:
        thing = (uint16_t)(e->data[2] | e->data[3] << 8);
        if (!(thing_word(thing, SENSOR_FLAGS) & (1 << SENSOR_STATE_SHIFT))) {
            if (party.cur_map == game.party_map) party.redraw |= REDRAW_VIEW;
            set_sensor_flags(thing, thing_word(thing, SENSOR_FLAGS) & ~SENSOR_BUSY);
        }
        break;
    case EVENT_SENSOR_AMBIENT_SOUND: {
        thing = (uint16_t)(e->data[2] | e->data[3] << 8);
        uint16_t w = thing_word(thing, SENSOR_TYPE_DATA);
        if (thing_word(thing, SENSOR_FLAGS) & SENSOR_BUSY && (int8_t)(e->time_map >> 24) == game.party_map) {
            int wall = square_type(dun_get_square(e->data[0], e->data[1])) == SQUARE_WALL;
            e->time_map += (uint16_t)sensor_get_ornament_cycle(thing, wall);
            timeline_add_event(e);
            sound_play_with_fallback(wall ? CAT_ORNAMENT : CAT_ORNAMENT + 1,
                                     wall ? sensor_wall_ornament(thing) : sensor_floor_ornament(thing), 0x88, 0xfe,
                                     e->data[0], e->data[1], 1, 0x8c, 0x80);
        } else {
            set_thing_word(thing, SENSOR_TYPE_DATA, (uint16_t)((w & 0x7f) | (w >> SENSOR_DATA_SHIFT & 0xff) << SENSOR_DATA_SHIFT));
        }
        break;
    }
    case EVENT_SENSOR_IDLE: case EVENT_SENSOR_READY:
        set_sensor_flags(thing, thing_word(thing, SENSOR_FLAGS) & ~SENSOR_BUSY);
        break;
    case EVENT_CLEAR_THING_FLAG8:
        set_thing_word(thing, 2, thing_word(thing, 2) & ~0x0800);
        break;
    case EVENT_SET_MARKER:
        set_thing_word(thing, 2, thing_word(thing, 2) | 1);
        break;
    }
}

void sensor_process_map_entry(int entering)
{
    int saved = party.cur_map;
    dun_set_current_map(game.party_map);
    int map = party.cur_map;
    for (int x = 0; x < dungeon_map_width(map); x++)
        for (int y = 0; y < dungeon_map_height(map); y++) {
            int wall = square_type(dungeon_square(map, x, y)) == SQUARE_WALL;
            for (uint16_t t = first_thing(x, y); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR; t = dungeon_next_thing(t)) {
                if (thing_type(t) == THING_SENSOR) {
                    uint16_t w = thing_word(t, SENSOR_TYPE_DATA), flags = thing_word(t, SENSOR_FLAGS);
                    int kind = w & SENSOR_TYPE_MASK;
                    if (kind == 0x21) {
                        if (sensor_own_effect(flags) == EFFECT_HOLD) {
                            sensor_trigger_target(t, (flags >> 5 & 1) == entering, 0);
                        } else if ((!(flags & SENSOR_INVERTED)) == entering) {
                            sensor_trigger_target(t, sensor_own_effect(flags), 0);
                        }
                    } else if (kind == 0x2c && entering && (flags & 1)) {
                        sensor_start_ambient_sound(t, t, x, y, (uint16_t)sensor_get_ornament_cycle(t, wall), wall);
                    }
                } else if (thing_type(t) == THING_TEXT && entering) {
                    uint16_t w = thing_word(t, TEXT_DATA);
                    if ((w & TEXT_KIND_MASK) == TEXT_KIND_DATA && w >> TEXT_SUBTYPE_SHIFT == 0x15 && !(w & 1)) {
                        set_thing_word(t, TEXT_DATA, w | 1);
                        creature_create_group(w >> TEXT_VALUE_SHIFT & 0xff, 7, rand_2bits(), x, y);
                    }
                }
            }
        }
    dun_set_current_map(saved);
}

void sensor_start_timers(int new_game)
{
    for (int map = 0; map < dungeon_map_count(); map++)
        for (int x = 0; x < dungeon_map_width(map); x++)
            for (int y = 0; y < dungeon_map_height(map); y++)
                for (uint16_t t = dungeon_square_first_thing(map, x, y); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR;
                     t = dungeon_next_thing(t)) {
                    if (thing_type(t) != THING_SENSOR) continue;
                    int kind = thing_word(t, SENSOR_TYPE_DATA) & SENSOR_TYPE_MASK;
                    if (!new_game || !(kind == 0x1e || (kind > 0x32 && kind < 0x38))) continue;
                    uint16_t flags = thing_word(t, SENSOR_FLAGS);
                    if (!(flags & (1 << SENSOR_STATE_SHIFT))) {
                        set_sensor_flags(t, flags & ~SENSOR_BUSY);
                    } else {
                        dun_set_current_map(map);
                        sensor_schedule_timer(t, t);
                    }
                }
}
