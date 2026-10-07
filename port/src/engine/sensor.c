/* Floor and wall sensors, square events, doors, pits, teleporters, and fake walls. */
#include <string.h>
#include "action.h"
#include "ai_move.h"
#include "champion.h"
#include "creature.h"
#include "draw.h"
#include "dungeon.h"
#include "gdat.h"
#include "hooks.h"
#include "inventory.h"
#include "money.h"
#include "item.h"
#include "move.h"
#include "random.h"
#include "save.h"
#include "session.h"
#include "sensor.h"
#include "input.h"
#include "text.h"
#include "view.h"

#define CAT_DOOR 0x0e
#define CAT_ORNAMENT 9
#define DOOR_KEY_STRENGTH 0x0e      /* door value: the strength that breaks the door */
#define DOOR_KEY_DAMAGE 0x0f        /* door value: the damage of a closing door */
#define ORNAMENT_KEY_ITEM 0x0e      /* ornament value: the item that the ornament takes */
#define ORNAMENT_KEY_ALTAR 0x0c     /* ornament value: an altar that brings back a champion from its bones */
#define BONES_INDEX 0               /* miscellaneous item 0: the bones of a champion */

#define SENSOR_CREATE_ITEM 0x3c
#define SENSOR_KEEP_ITEM EFFECT_HOLD  /* the effect bits of SENSOR_CREATE_ITEM: the new item is kept from reuse */
#define GLOBAL_BYTE_FIRST (GLOBAL_FLAG_BYTES * 8)  /* script variables: 64 flags, 64 bytes, 64 words */
#define GLOBAL_WORD_FIRST 0x80
#define GLOBAL_VAR_END 0xc0
#define GLOBAL_BYTE_MAX 0xff
#define SENSOR_TIMER 0x1e               /* a timer sensor; 0x33 to 0x37 count in steps of 8 to 128 ticks */
#define SENSOR_TIMER_SLOW_FIRST 0x33
#define SENSOR_TIMER_SLOW_LAST 0x37
/* Floor sensor types. */
enum {
    FLOOR_ANY = 1, FLOOR_PARTY_OR_CREATURE, FLOOR_PARTY, FLOOR_ITEM, FLOOR_CREATURE = 7, FLOOR_PARTY_POSSESSES
};
/* Wall sensor types of objects put on a wall cell. */
enum { WALL_ITEM_HOLDER = 0x1a, WALL_ANY_ITEM = 0x29, WALL_ITEM = 0x2a, WALL_NOT_ITEM = 0x2b };
/* Ornament text subtypes. */
enum { TEXT_ITEM_HOLDER = 4, TEXT_ITEM_HOLDER_2 = 8, TEXT_RANDOM_PIT = 9, TEXT_FALL_RISK = 10 };

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

int dun_get_door_type(uint16_t door)
{
    uint16_t flags = dungeon_map_flags(party.cur_map), graphics = dungeon_map_graphics(party.cur_map);
    if (!door_map_type(door)) return flags & 0x80 ? graphics >> 8 & 0xf : 0xff;
    return flags & 0x100 ? graphics >> 12 : 0xff;
}

uint16_t door_value(uint16_t door, int key)
{
    return gdat_entry_index(draw_gdat(), CAT_DOOR, dun_get_door_type(door), GDAT_TYPE_VALUE, key);
}

static int item_holder_item(int ornament)
{
    return gdat_entry_index(draw_gdat(), CAT_ORNAMENT, ornament, GDAT_TYPE_VALUE, ORNAMENT_KEY_ITEM);
}

/* The wall ornament (GDAT index) of a sensor, 0xff none. */
static int sensor_wall_ornament(uint16_t sensor)
{
    int ordinal = sensor_ornament_ordinal(sensor);
    return ordinal ? dungeon_map_wall_ornaments(party.cur_map)[ordinal - 1] : 0xff;
}

static int sensor_floor_ornament(uint16_t sensor)
{
    int ordinal = sensor_ornament_ordinal(sensor);
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

int item_party_possesses_type(int type)
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
    /* fix: the original leaves the priority of a hold effect uninitialized; it gets the priority of a clear */
    e.priority = effect == EFFECT_SET ? 1 : effect == EFFECT_CLEAR || effect == EFFECT_HOLD ? 3 : 2;
    e.data[EVENT_X] = (uint8_t)x;
    e.data[EVENT_Y] = (uint8_t)y;
    e.data[EVENT_CELL] = (uint8_t)cell;
    e.data[EVENT_EFFECT] = (uint8_t)effect;
    timeline_add_event(&e);
}

void sensor_trigger_target(uint16_t sensor, int effect, int delay)
{
    sensor_schedule_square_event(sensor_target_x(sensor), sensor_target_y(sensor), sensor_target_cell(sensor), effect,
                                 (uint32_t)((int16_t)delay + sensor_delay(sensor) + game.game_time));
}

int sensor_door_try_destroy(int x, int y, int strength, int magic, int delay)
{
    uint16_t door = first_thing(x, y);
    if (!door_flag(door, magic ? DOOR_BREAKABLE_BY_MAGIC : DOOR_BREAKABLE)) return 0;
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
    trigger ^= sensor_flag(sensor, SENSOR_INVERTED);
    int effect = sensor_effect(sensor);
    if (effect == EFFECT_HOLD) effect = !trigger;
    else if (!trigger) return;
    if (sensor_flag(sensor, SENSOR_AUDIBLE))
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
    int chance = (int16_t)(load * 10 + (text_is_visible(text) ? 0x32 : 0x19));
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
        sound_request(10, text_value(text), 0x88, 0x8c, 0x80, game.party_x, game.party_y, 1);
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
                if (text_kind(t) == TEXT_KIND_TEXT && text_is_visible(t)) {
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
            int kind = sensor_type(t), data = sensor_data(t);
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
                    if (same_item || sensor_flag(t, SENSOR_STATE) == enter) continue;
                    if (item != item_holder_item(sensor_wall_ornament(t))) continue;
                    sensor_set_flag(t, SENSOR_STATE, enter & 1);
                    break;
                default:
                    continue;
                }
            }
            sensor_fire(t, trigger, cell == -1, x, y);
        } else if (tt == THING_TEXT) {
            if (text_kind(t) != TEXT_KIND_ORNAMENT) continue;
            int sub = text_subtype(t);
            if (cell == -1) {
                if (sub == TEXT_RANDOM_PIT) {
                    if (type == -1 && !on_square && game.champion_count && rand_below(100) < text_value(t)
                        && text_is_visible(t) != enter) {
                        sensor_schedule_square_event(x, y, 0, EFFECT_SET, game.game_time + 1);
                        sensor_schedule_square_event(x, y, 0, EFFECT_CLEAR, game.game_time + 5);
                    }
                } else if (sub == TEXT_FALL_RISK && type == -1 && !enter && game.champion_count) {
                    sensor_fall_risk(t, x, y, on_square);
                }
            } else if (cell == t >> THING_CELL_SHIFT && (sub == TEXT_ITEM_HOLDER || sub == TEXT_ITEM_HOLDER_2) && !same_item
                       && text_is_visible(t) != enter && item == item_holder_item(text_value(t))) {
                text_set_visible(t, enter & 1);
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
static int sensor_activated(uint16_t sensor, int effect)
{
    return sensor_flag(sensor, SENSOR_INVERTED) ? effect == EFFECT_CLEAR : effect == EFFECT_SET;
}

/* The effect that a sensor passes on: its own effect when its state option is on, else the event effect. */
static int sensor_passed_effect(uint16_t sensor, int effect)
{
    return sensor_flag(sensor, SENSOR_STATE) ? sensor_effect(sensor) : effect;
}

/* A relay: passes the event on to its target after the delay in its data (scaled: data << delay bits). */
static void sensor_relay_delayed(const TimelineEvent *e, uint16_t sensor, int scaled)
{
    int effect = e->data[EVENT_EFFECT];
    if (sensor_flag(sensor, SENSOR_STATE) && !sensor_activated(sensor, effect)) return;
    unsigned data = sensor_data(sensor), bits = sensor_delay(sensor);
    uint32_t time = scaled ? (data << bits) + game.game_time : game.game_time + data + bits;
    sensor_schedule_square_event(sensor_target_x(sensor), sensor_target_y(sensor), sensor_target_cell(sensor),
                                 sensor_passed_effect(sensor, effect), time);
}

/* Passes the event effect (hold sensors) or the own effect of the sensor to its target. */
static void sensor_forward_effect(const TimelineEvent *e, uint16_t sensor, int delay)
{
    int effect = e->data[EVENT_EFFECT];
    if (sensor_effect(sensor) == EFFECT_HOLD) {
        if (!sensor_flag(sensor, SENSOR_INVERTED)) {
            sensor_trigger_target(sensor, effect, delay);
            return;
        }
        sensor_trigger_target(sensor, effect, 0);
        if (!delay) return;
        /* fix: the original leaves the reverse of a hold effect uninitialized; it stays a hold here */
        static const uint8_t reverse[4] = {EFFECT_CLEAR, EFFECT_SET, EFFECT_TOGGLE, EFFECT_HOLD};
        sensor_trigger_target(sensor, reverse[effect & 3], delay);
        return;
    }
    if (!sensor_activated(sensor, effect)) return;
    sensor_trigger_target(sensor, sensor_effect(sensor), delay);
}

int dun_modify_global_var(int var, int value, int op)
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
        return (int16_t)v != 0;
    }
    v = (int16_t)v;
    if (var < GLOBAL_WORD_FIRST) {
        v = v < 0 ? 0 : v > GLOBAL_BYTE_MAX ? GLOBAL_BYTE_MAX : v;
        game.global_bytes[var - GLOBAL_BYTE_FIRST] = (uint8_t)v;
    } else if (var < GLOBAL_VAR_END) {
        game.global_words[var - GLOBAL_WORD_FIRST] = (uint16_t)v;
    }
    return v;
}

int dun_get_global_var(int var)
{
    if (var < GLOBAL_FLAG_BYTES * 8) return game.global_flags[var >> 3] >> (var & 7) & 1;
    if (var < GLOBAL_WORD_FIRST) return game.global_bytes[var - GLOBAL_BYTE_FIRST];
    if (var < GLOBAL_VAR_END) return game.global_words[var - GLOBAL_WORD_FIRST];
    return 0;
}

static void sensor_set_global_flag(const TimelineEvent *e, uint16_t sensor)
{
    dun_modify_global_var(sensor_data(sensor), 1, e->data[EVENT_EFFECT] + (sensor_flag(sensor, SENSOR_INVERTED) ? 3 : 0));
    sensor_trigger_target(sensor, sensor_passed_effect(sensor, e->data[EVENT_EFFECT]), 0);
}

static void sensor_test_global_flag(const TimelineEvent *e, uint16_t sensor)
{
    int set = dun_get_global_var(sensor_data(sensor)) != 0;
    int inverted = sensor_flag(sensor, SENSOR_INVERTED), effect = e->data[EVENT_EFFECT];
    if ((set != inverted && (effect == EFFECT_SET || effect == EFFECT_TOGGLE)) || (set == inverted && effect == EFFECT_CLEAR))
        sensor_trigger_target(sensor, sensor_passed_effect(sensor, effect), 0);
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
    if (!sensor_flag(sensor, SENSOR_AUDIBLE) || sensor_sound_on(sensor)) return;
    int category = wall ? CAT_ORNAMENT : CAT_ORNAMENT + 1;
    int ornament = wall ? sensor_wall_ornament(sensor) : sensor_floor_ornament(sensor);
    int frame = sensor_frame(sensor);
    uint32_t start = game.game_time + frame;
    uint16_t offset = gdat_entry_index(draw_gdat(), category, ornament, GDAT_TYPE_VALUE, 0x88);
    unsigned phase = (offset + start) % cycle;
    TimelineEvent e = {.time_map = event_time(cycle - phase + game.game_time),
                       .type = EVENT_SENSOR_AMBIENT_SOUND};
    e.data[0] = (uint8_t)x;
    e.data[1] = (uint8_t)y;
    e.data[2] = (uint8_t)thing;
    e.data[3] = (uint8_t)(thing >> 8);
    timeline_add_event(&e);
    sensor_set_data(sensor, SENSOR_DATA_SOUND | frame);
    if (phase) return;  /* the sound plays now only when the animation cycle starts now */
    if (wall) {  /* a wall sound comes from the square in front of the wall, unless the party stands in the wall */
        int side = thing >> THING_CELL_SHIFT;
        int dx = x + (side == DIR_EAST) - (side == DIR_WEST), dy = y + (side == DIR_SOUTH) - (side == DIR_NORTH);
        if (party.cur_map != party.square_map || dx != party.square_x || dy != party.square_y) {
            x = dx;
            y = dy;
        }
    }
    sound_play_with_fallback(category, ornament, 0x88, 0xfe, x, y, 1, 0x8c, 0x80);
}

/* An animated ornament turns on (it runs) or off (it stops at the end of its cycle). */
static void sensor_toggle_animation(uint16_t thing, const TimelineEvent *e, uint16_t sensor, int wall)
{
    int old = sensor_flag(sensor, SENSOR_STATE), now = sensor_apply_effect(e->data[EVENT_EFFECT], old) & 1;
    sensor_set_flag(sensor, SENSOR_STATE, now);
    if (old != now) {
        unsigned cycle = (uint16_t)sensor_get_ornament_cycle(sensor, wall);
        if (now) {
            if (!sensor_flag(sensor, SENSOR_BUSY)) {
                sensor_set_flag(sensor, SENSOR_BUSY, true);
                if (party.cur_map == game.party_map) party.redraw |= REDRAW_VIEW;
                unsigned frame = (cycle - game.game_time % cycle) % cycle & SENSOR_DATA_FRAME_MASK;
                sensor_set_data(sensor, (sensor_data(sensor) & SENSOR_DATA_SOUND) | frame);
                sensor_start_ambient_sound(sensor, thing, e->data[EVENT_X], e->data[EVENT_Y], cycle, wall);
            }
        } else {
            unsigned r = (sensor_frame(sensor) + game.game_time) % cycle;
            if (!(uint16_t)r) {
                if (party.cur_map == game.party_map) party.redraw |= REDRAW_VIEW;
                sensor_set_flag(sensor, SENSOR_BUSY, false);
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
    if (sensor_flag(sensor, SENSOR_INVERTED) && sensor_effect(sensor) == EFFECT_HOLD) sensor_trigger_target(sensor, e->data[EVENT_EFFECT], 0);
}

/* A sensor with a one-shot animation: it runs once (busy until its end); with its state set it also forwards. */
static void sensor_play_activation(const TimelineEvent *e, uint16_t sensor, uint16_t thing, int wall)
{
    if (!sensor_flag(sensor, SENSOR_BUSY)) {
        sensor_set_flag(sensor, SENSOR_BUSY, true);
        sensor_set_data(sensor, 0);
        TimelineEvent ev = {.time_map = event_time(game.game_time + 1), .type = EVENT_SENSOR_ANIMATION_STEP};
        ev.data[0] = (uint8_t)thing;
        ev.data[1] = (uint8_t)(thing >> 8);
        ev.data[2] = (uint8_t)wall;
        timeline_add_event(&ev);
        if (sensor_flag(sensor, SENSOR_AUDIBLE)) {
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
    if (sensor_flag(sensor, SENSOR_STATE)) sensor_forward_effect(e, sensor, 0);
}

/* Starts the repeating timer of a timer sensor; the type sets the period factor. */
static void sensor_schedule_timer(uint16_t sensor, uint16_t thing)
{
    uint16_t period = sensor_data(sensor);
    unsigned factor = 1;
    switch (sensor_type(sensor)) {
    case 0x1e: factor = 1; break;
    case 0x33: factor = 8; break;
    case 0x34: factor = 0x10; break;
    case 0x35: factor = 0x20; break;
    case 0x36: factor = 0x40; break;
    case 0x37: factor = 0x80; break;
    }
    if (!period) return;
    /* fix: the original leaves bits 9-15 of the period word uninitialized; bit 8 is the timer phase */
    TimelineEvent e = {.time_map = event_time(game.game_time % (period * (factor & 0xffff)) + game.game_time),
                       .type = EVENT_SENSOR_TIMER};
    e.data[0] = (uint8_t)thing;
    e.data[1] = (uint8_t)(thing >> 8);
    e.data[2] = (uint8_t)factor;
    timeline_add_event(&e);
    sensor_set_flag(sensor, SENSOR_BUSY, true);
}

void sensor_start_timers(void)
{
    for (int map = 0; map < dungeon_map_count(); map++)
        for (int x = 0; x < dungeon_map_width(map); x++)
            for (int y = 0; y < dungeon_map_height(map); y++)
                for (uint16_t t = dungeon_square_first_thing(map, x, y); thing_type(t) <= THING_SENSOR; t = dungeon_next_thing(t)) {
                    if (thing_type(t) != THING_SENSOR) continue;
                    int kind = sensor_type(t);
                    if (kind != SENSOR_TIMER && (kind < SENSOR_TIMER_SLOW_FIRST || kind > SENSOR_TIMER_SLOW_LAST)) continue;
                    if (sensor_flag(t, SENSOR_STATE)) {
                        dun_set_current_map(map);
                        sensor_schedule_timer(t, t);
                    } else {
                        sensor_set_flag(t, SENSOR_BUSY, false);
                    }
                }
}

/* ---- Square events ---- */

static void text_set_shown(uint16_t text, int effect)
{
    text_set_visible(text, effect == EFFECT_TOGGLE ? !text_is_visible(text) : effect == EFFECT_SET);
}

enum { TEXT_TOGGLED = 5, TEXT_LINKED = 7, TEXT_GENERATOR_SLOW = 0x13, TEXT_GENERATOR_FAST = 0x16, TEXT_SOUND = 0x17 };

/* A wall text of subtype 7 shows the same state as its twin on the level above or below. */
static void text_update_linked(const TimelineEvent *e, uint16_t text)
{
    bool visible = text_is_visible(text);
    int delta = gdat_entry_index(draw_gdat(), CAT_ORNAMENT, text_value(text), GDAT_TYPE_VALUE, 0x11) ? -1 : 1;
    int x = e->data[EVENT_X], y = e->data[EVENT_Y];
    int map = dungeon_location_on_map(game.party_map, delta, &x, &y);
    if (map < 0) return;
    int saved = party.cur_map;
    dun_set_current_map(map);
    for (uint16_t t = first_thing(x, y); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_TEXT || t >> THING_CELL_SHIFT != e->data[EVENT_CELL]) continue;
        if (text_kind(t) == TEXT_KIND_ORNAMENT && text_subtype(t) == TEXT_LINKED) text_set_visible(t, visible);
    }
    dun_set_current_map(saved);
}

/* A counter sensor reaches zero (or leaves it): returns 1 when its count is zero or below. */
static int counter_empty(uint16_t sensor)
{
    return !sensor_data(sensor) || (sensor_data(sensor) & SENSOR_DATA_SIGN);
}

/* Object transfer sensors: the charge change of a moved object (sensor flags bits 3-4). */
enum { CHARGES_FILL, CHARGES_EMPTY, CHARGES_RANDOM };
#define CHARGES_EMPTY_AMOUNT (-15)
#define SENSOR_KEEP_BONES SENSOR_STATE  /* sensor flags: bones move like other objects; the moved object goes into
                                           the possessions of a static creature there */
#define SENSOR_ANY_ITEM 0x1ff       /* sensor data: item index 0x1ff, any object */
#define ITEM_CLASS_SET_MASK 0xff    /* sensor data: the item class set; sensor delay bits: the item class text */
#define ITEM_CLASS_ENTRY_STEP 3
#define ITEM_CLASS_ENTRY_FIRST 0x10
#define CELL_COUNT 4

static int is_object(uint16_t thing) { return thing_type(thing) >= THING_WEAPON && thing_type(thing) <= THING_MISC; }

void sensor_transfer_object(uint16_t thing, uint16_t sensor, int from_x, int from_y, int to_x, int to_y, int to_cell,
                            int from_creature, int to_possessions)
{
    if (item_gdat_category(thing) != GDAT_MISC_CATEGORY || item_gdat_index(thing) != BONES_INDEX) {
        int charges = 0;  /* fix: mode 3 adds an uninitialized amount in the original */
        switch (sensor_effect(sensor)) {
        case CHARGES_FILL: charges = item_max_charges(thing); break;
        case CHARGES_EMPTY: charges = CHARGES_EMPTY_AMOUNT; break;
        case CHARGES_RANDOM: charges = rand_below((uint16_t)(item_max_charges(thing) + 1)); break;
        }
        item_add_charges(thing, charges);
    }
    if (square_type(*square_byte(to_x, to_y)) != SQUARE_WALL) to_cell = rand_below(CELL_COUNT);
    if (from_x >= 0 && from_y >= 0) move_thing(thing, from_x, from_y, -1, from_creature ? -1 : 0);
    move_thing((uint16_t)((thing & ~(3 << THING_CELL_SHIFT)) | to_cell << THING_CELL_SHIFT), -1, to_possessions ? -1 : 0,
               to_x, to_y);
}

/* A Skullkeep machine: the objects of the event square (reverse: of the sensor target) that the sensor wants move
   to the sensor target (reverse: to the event square), also from the creatures there. first: the first thing to
   look at, THING_NONE for the square; any_cell: all cells, not only the event cell; use_filter: the item class of
   the sensor data; possessions: the list of the creature that holds the things; first_only: one object only. */
static void sensor_transfer_objects(const TimelineEvent *e, uint16_t sensor, uint16_t first, int any_cell, int use_filter,
                                    uint8_t *possessions, int reverse, int first_only)
{
    uint16_t data = sensor_data(sensor);
    bool keep_bones = sensor_flag(sensor, SENSOR_KEEP_BONES);
    int ex = e->data[EVENT_X], ey = e->data[EVENT_Y], ecell = e->data[EVENT_CELL];
    int tx = sensor_target_x(sensor), ty = sensor_target_y(sensor), tcell = sensor_target_cell(sensor);
    int fx = reverse ? tx : ex, fy = reverse ? ty : ey, fcell = reverse ? tcell : ecell;
    int dx = reverse ? ex : tx, dy = reverse ? ey : ty, dcell = reverse ? ecell : tcell;
    int effect = e->data[EVENT_EFFECT];
    if (!sensor_activated(sensor, effect)) return;
    if (first == THING_NONE) {
        first = first_thing(fx, fy);
        if (square_type(*square_byte(fx, fy)) != SQUARE_WALL) any_cell = 1;
    }
    int count = 0;
    for (uint16_t t = first; t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        if (is_object(t) && (any_cell || t >> THING_CELL_SHIFT == fcell)) count++;
    uint8_t bits[ITEM_CLASS_SET_SIZE];
    if (use_filter)
        shop_parse_item_class_set(data & ITEM_CLASS_SET_MASK, sensor_delay(sensor) * ITEM_CLASS_ENTRY_STEP + ITEM_CLASS_ENTRY_FIRST,
                                  bits, 0);
    for (uint16_t t = first, next; t != THING_ENDOFLIST; t = next) {
        next = dungeon_next_thing(t);
        if (count > 0 && is_object(t) && (any_cell || t >> THING_CELL_SHIFT == fcell)) {
            count--;
            uint16_t item = item_get_index(t);
            int match;
            if (use_filter) {
                match = bits[item >> 3] >> (item & 7) & 1;
            } else if (item == data) {
                if (!keep_bones && item_gdat_category(t) == GDAT_MISC_CATEGORY
                    && item_gdat_index(t) == BONES_INDEX) {
                    /* bones: the champion comes back */
                    if (possessions) dungeon_unlink_thing_from_list(t, possessions);
                    else dungeon_unlink_thing_from_square(t, party.cur_map, fx, fy);
                    set_thing_word(t, THING_NEXT, THING_NONE);
                    champ_revive(item_add_charges(t, 0));
                    continue;
                }
                match = 1;
            } else {
                match = data == SENSOR_ANY_ITEM;
            }
            if (!match) continue;
            sensor_transfer_object(t, sensor, fx, fy, dx, dy, dcell, possessions != NULL, keep_bones);
            if (first_only) return;
        } else if (!use_filter && thing_type(t) == THING_GROUP) {
            uint8_t *list = dungeon_thing_record(t) + GROUP_POSSESSIONS;
            sensor_transfer_objects(e, sensor, thing_word(t, GROUP_POSSESSIONS), any_cell, 0, list, reverse, first_only);
        }
    }
}

/* A sensor of kind SENSOR_CREATE_ITEM makes a new item of its data item index at its target. */
static void sensor_create_item(uint16_t sensor)
{
    uint16_t item = sensor_data(sensor);
    int type = item_index_to_thing_type(item);
    uint16_t thing = type < 0 ? THING_NONE : dungeon_unused_thing(type);  /* fix: no record of type 0xffff */
    if (thing == THING_NONE) return;
    item_set_gdat_index(thing, item_index_gdat_index(item));
    if (sensor_effect(sensor) == SENSOR_KEEP_ITEM) item_set_do_not_discard(thing, 1);
    sensor_transfer_object(thing, sensor, -1, -1, sensor_target_x(sensor), sensor_target_y(sensor), sensor_target_cell(sensor), 0,
                           sensor_flag(sensor, SENSOR_KEEP_BONES));
}

#define COUNTDOWN_MAX 400           /* sensor 0x2d: a larger data value is a chance of data - 400 in 100 */
#define CHANCE_END 500

static void timeline_event_wall_square(TimelineEvent *e)
{
    int x = e->data[EVENT_X], y = e->data[EVENT_Y], effect = e->data[EVENT_EFFECT];
    for (uint16_t t = first_thing(x, y); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR; t = dungeon_next_thing(t)) {
        if (t >> THING_CELL_SHIFT != e->data[EVENT_CELL]) continue;
        if (thing_type(t) == THING_TEXT) {
            if (text_kind(t) > TEXT_KIND_ORNAMENT) continue;
            int sub = text_subtype(t);
            if (sub == TEXT_TOGGLED || sub == TEXT_LINKED) {
                text_set_shown(t, effect);
                if (sub == TEXT_LINKED) text_update_linked(e, t);
            } else if (sub == TEXT_SOUND && effect == EFFECT_SET) {
                sound_request(3, party.cur_map + 1, text_value(t), 0x73, 200, x, y, 1);
            }
            continue;
        }
        if (thing_type(t) != THING_SENSOR) continue;
        unsigned data = sensor_data(t);
        int tx = sensor_target_x(t), ty = sensor_target_y(t);
        int kind = sensor_type(t);
        switch (kind) {
        case 0x31:  /* a relay that stays busy for data ticks */
            if (sensor_flag(t, SENSOR_BUSY)) break;
            {
                TimelineEvent ev = {.time_map = event_time(data + game.game_time), .type = EVENT_SENSOR_READY};
                ev.data[0] = (uint8_t)t;
                ev.data[1] = (uint8_t)(t >> 8);
                timeline_add_event(&ev);
            }
            sensor_set_flag(t, SENSOR_BUSY, true);
            if (sensor_activated(t, effect)) sensor_trigger_target(t, sensor_passed_effect(t, effect), 0);
            break;
        case 0x2e:  /* a creature generator */
            if (effect != EFFECT_SET) break;
            {
                int dir = sensor_flag(t, SENSOR_STATE) ? rand_2bits() : sensor_effect(t);
                uint16_t g = creature_create_group(data, 7, dir, tx, ty);
                if (sensor_flag(t, SENSOR_INVERTED) && g != THING_NONE) set_thing_word(g, GROUP_ITEM_CLASS, sensor_delay(t));
                if (sensor_flag(t, SENSOR_AUDIBLE)) sound_request(0, 0, 0x89, 0x61, 0x80, tx, ty, 1);
            }
            break;
        case 0x1d: {  /* a counter: clear counts up, set counts down */
            int before = counter_empty(t);
            if (effect == EFFECT_CLEAR) sensor_set_data(t, data + 1);
            else if (effect == EFFECT_SET && (!sensor_flag(t, SENSOR_STATE) || data)) sensor_set_data(t, data - 1);
            int after = counter_empty(t);
            if (after == before) break;
            if (sensor_effect(t) == EFFECT_HOLD) sensor_trigger_target(t, after == sensor_flag(t, SENSOR_INVERTED), 0);
            else if (after) sensor_trigger_target(t, sensor_effect(t), 0);
            break;
        }
        case 0x2d: {  /* a countdown, or a random chance for the values above 400 */
            if (data && data <= COUNTDOWN_MAX) {
                sensor_set_data(t, data - 1);
                sensor_trigger_target(t, effect, 0);
            } else if (data > COUNTDOWN_MAX && data < CHANCE_END) {
                int hit = rand_below(100) < (unsigned)(data - COUNTDOWN_MAX);
                if (sensor_effect(t) == EFFECT_HOLD) sensor_trigger_target(t, !hit, 0);
                else if (hit) sensor_trigger_target(t, effect, 0);
            }
            break;
        }
        case 0x1e: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37:  /* timers */
            sensor_set_flag(t, SENSOR_STATE, sensor_apply_effect(effect, sensor_flag(t, SENSOR_STATE)) & 1);
            if (!sensor_flag(t, SENSOR_BUSY) && sensor_flag(t, SENSOR_STATE)) sensor_schedule_timer(t, t);
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
            int map = data & 0x3f;
            if (map >= dungeon_map_count()) break;  /* fix: the original reads a map outside the dungeon */
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
            unsigned frame = (uint16_t)(effect == EFFECT_SET ? data + 1 : data - 1);
            sensor_set_data(t, frame % (uint16_t)sensor_get_ornament_cycle(t, 1));
            break;
        }
        case 0x2c:
            sensor_toggle_animation(t, e, t, 1);
            break;
        case 0x26:
            sensor_set_flag(t, SENSOR_STATE, sensor_apply_effect(effect, sensor_flag(t, SENSOR_STATE)) & 1);
            break;
        case 0x3b: sensor_transfer_objects(e, t, t, 0, 0, NULL, 0, 0); break;
        case 0x40: sensor_transfer_objects(e, t, t, 0, 1, NULL, 0, 0); break;
        case 0x47: sensor_transfer_objects(e, t, THING_NONE, 0, 0, NULL, 1, 0); break;
        case 0x48: sensor_transfer_objects(e, t, t, 0, 0, NULL, 0, 1); break;
        case 0x49: sensor_transfer_objects(e, t, THING_NONE, 0, 0, NULL, 1, 1); break;
        case SENSOR_CREATE_ITEM:
            if (sensor_activated(t, effect)) sensor_create_item(t);
            break;
        case 0x42:
            sensor_turn_creatures(e, t);
            break;
        case 0x43:
            sensor_set_global_flag(e, t);
            break;
        case 0x44:
            sensor_test_global_flag(e, t);
            break;
        case 0x46: {  /* sets bit 13 of the door at the target */
            uint16_t door = first_thing(tx, ty);
            if (effect == EFFECT_SET) door_set_flag(door, DOOR_SWITCH_ON, true);
            else if (effect == EFFECT_CLEAR) door_set_flag(door, DOOR_SWITCH_ON, false);
            else if (effect == EFFECT_TOGGLE) door_set_flag(door, DOOR_SWITCH_ON, !door_flag(door, DOOR_SWITCH_ON));
            break;
        }
        }
    }
}

#define AREA_POWER_MASK 0xf         /* sensors 0x0b and 0x28: power in data bits 0-3, creature type in bits 4-8 */
#define AREA_CREATURE_SHIFT 4

static void timeline_event_floor_square(TimelineEvent *e)
{
    int x = e->data[EVENT_X], y = e->data[EVENT_Y], effect = e->data[EVENT_EFFECT];
    for (uint16_t t = first_thing(x, y); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR; t = dungeon_next_thing(t)) {
        if (thing_type(t) == THING_TEXT) {
            if (text_kind(t) > TEXT_KIND_ORNAMENT) continue;
            int sub = text_subtype(t);
            bool was_visible = text_is_visible(t);
            if (sub == TEXT_TOGGLED) {
                text_set_shown(t, effect);
                if (text_kind(t) == TEXT_KIND_TEXT && !was_visible && text_is_visible(t) && dun_on_party_square(x, y)) {
                    char text[TEXT_SIZE];
                    text_print_message(UI_WHITE, text_decode_dungeon(text, sizeof text, t, 1));
                }
            } else if ((sub == TEXT_GENERATOR_SLOW || sub == TEXT_GENERATOR_FAST) && effect == EFFECT_SET) {
                unsigned base = sub == TEXT_GENERATOR_SLOW ? 0x200 : 0x2000;
                if (was_visible) base *= sub == TEXT_GENERATOR_SLOW ? 4 : 2;
                unsigned r = rand_next() & 0xffff;
                TimelineEvent ev = {.time_map = event_time(base + ((base * 2 - 1) & r) + game.game_time),
                                    .type = EVENT_SPAWN_CREATURE};
                ev.data[0] = (uint8_t)x;
                ev.data[1] = (uint8_t)y;
                ev.data[3] = text_value(t);
                timeline_add_event(&ev);
            } else if (sub == TEXT_SOUND && effect == EFFECT_SET) {
                sound_request(3, party.cur_map + 1, text_value(t), 0x73, 200, x, y, 1);
            }
            continue;
        }
        if (thing_type(t) != THING_SENSOR) continue;
        int kind = sensor_type(t);
        switch (kind) {
        case 0x20: case 0x45:
            sensor_relay_delayed(e, t, kind == 0x45);
            break;
        case SENSOR_TELEPORTER_LINK:
            sensor_set_flag(t, SENSOR_BUSY, sensor_apply_effect(effect, sensor_flag(t, SENSOR_BUSY)) & 1);
            break;
        case 0x0b: case 0x28:
            sensor_affect_creatures_in_area(sensor_data(t) & AREA_POWER_MASK, sensor_data(t) >> AREA_CREATURE_SHIFT, x, y,
                                            sensor_target_x(t), sensor_target_y(t), kind, effect);
            break;
        case 0x2c:
            sensor_toggle_animation(t, e, t, 0);
            break;
        case 0x2e: {  /* moves the party to its target (or to this square) */
            int tx = x, ty = y;
            if (sensor_flag(t, SENSOR_STATE)) {
                tx = sensor_target_x(t);
                ty = sensor_target_y(t);
            }
            int dir = sensor_flag(t, SENSOR_INVERTED) ? sensor_effect(t) : (sensor_effect(t) + game.party_dir) & 3;
            move_party_to(tx, ty, party.cur_map, dir);
            break;
        }
        case 0x32:
            sensor_play_activation(e, t, t, 0);
            break;
        case 0x3a:
            creature_update_anim_at(x, y, effect == EFFECT_SET);
            break;
        case 0x3b: sensor_transfer_objects(e, t, t, 1, 0, NULL, 0, 0); break;
        case 0x40: sensor_transfer_objects(e, t, t, 1, 1, NULL, 0, 0); break;
        case 0x47: sensor_transfer_objects(e, t, THING_NONE, 0, 0, NULL, 1, 0); break;
        case 0x48: sensor_transfer_objects(e, t, t, 0, 0, NULL, 0, 1); break;
        case 0x49: sensor_transfer_objects(e, t, THING_NONE, 0, 0, NULL, 1, 1); break;
        case 0x3d:
            sensor_forward_effect(e, t, sensor_data(t));
            break;
        case 0x42:
            sensor_turn_creatures(e, t);
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
void move_square_contents_through(int x, int y)
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
    int effect = e->data[EVENT_EFFECT];
    if (door_flag(door, DOOR_MOVING)) {
        int opening = sensor_apply_effect(effect, door_flag(door, DOOR_OPENING)) & 1;
        door_set_flag(door, DOOR_OPENING, opening);
        if (!opening) door_set_flag(door, DOOR_MARKED, false);
        return;
    }
    int move = 0;
    if (state == DOOR_OPEN) {
        if (effect == EFFECT_CLEAR || effect == EFFECT_TOGGLE) {
            door_set_flag(door, DOOR_OPENING, false);
            move = 1;
        }
    } else if (state == DOOR_CLOSED) {
        if (effect == EFFECT_SET || effect == EFFECT_TOGGLE) {
            door_set_flag(door, DOOR_OPENING, true);
            move = 1;
        }
    } else {
        door_set_flag(door, DOOR_OPENING, effect == EFFECT_SET);
        move = 1;
    }
    door_set_flag(door, DOOR_MOVING, move);
    if (move) {
        if (!door_flag(door, DOOR_OPENING)) door_set_flag(door, DOOR_MARKED, false);
        e->type = EVENT_DOOR_ANIMATION;
        timeline_add_event(e);
    }
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
        if (!teleporter_is_fixed(first_thing(e->data[EVENT_X], e->data[EVENT_Y])))
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
    if (!door_flag(door, DOOR_MOVING)) return;
    bool opening = door_flag(door, DOOR_OPENING);
    e->time_map++;
    int again = 0;
    if (!opening) {
        if (state == DOOR_CLOSED) {
            door_set_flag(door, DOOR_MOVING, false);
            return;
        }
        int vertical = door_flag(door, DOOR_VERTICAL), hit = 0;
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
            int height = vertical ? info->flags >> CREATURE_DOOR_SIZE_SHIFT & 3 : 1;
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
        door_set_flag(door, DOOR_MOVING, false);
        return;
    }
    if (!again) {
        state += opening ? -1 : 1;
        *square = (uint8_t)((*square & ~SQUARE_DOOR_STATE) | (state & SQUARE_DOOR_STATE));
        if (opening ? state != DOOR_OPEN : state != DOOR_CLOSED) again = 1;
        sound_play_with_fallback(CAT_DOOR, dun_get_door_type(door), !again && state == DOOR_CLOSED ? 0x8f : 0x8e, 0xfe, x, y, 1, 0x5f,
                                 0x80);
    }
    if (again) {
        door_set_flag(door, DOOR_MOVING, true);
        timeline_add_event(e);
        return;
    }
    door_set_flag(door, DOOR_MOVING, false);
}

/* ---- Sensor timeline events ---- */

/* The step to the square in front of a wall of the ambient sound event: the original indexes g_dir_dx and g_dir_dy
   with the high byte of the sensor thing (cell in bits 6-7, index bits 8-9 of the record in bits 0-1), so it reads
   the data after those tables in SKULL.EXE (0x717f8 and 0x71800); these are the values it reads. */
static const int16_t ambient_wall_dx[4][4] = {{2052, 4, 0, 1032}, {0, 0, 0, 0}, {0, 0, 0, 0}, {450, 0, 255, 0}};
static const int16_t ambient_wall_dy[4][4] = {{0, 0, 25675, 60}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};

void timeline_event_sensor(TimelineEvent *e)
{
    uint16_t thing = (uint16_t)(e->data[0] | e->data[1] << 8);
    switch (e->type) {
    case EVENT_SENSOR_ANIMATION_STEP: {
        e->time_map++;
        unsigned cycle = (uint16_t)sensor_get_ornament_cycle(thing, e->data[2]);
        unsigned frame = (sensor_data(thing) + 1) & SENSOR_DATA_MASK;
        sensor_set_data(thing, frame);
        if (frame % cycle == 0) sensor_set_flag(thing, SENSOR_BUSY, false);
        else timeline_add_event(e);
        break;
    }
    case EVENT_SENSOR_TIMER: {
        int running, effect;
        if (sensor_effect(thing) == EFFECT_HOLD) {
            int phase = !(e->data[3] & 1);
            e->data[3] = (uint8_t)((e->data[3] & ~1) | phase);
            running = phase | sensor_flag(thing, SENSOR_STATE);
            effect = !(e->data[3] & 1);
            sensor_trigger_target(thing, effect, 0);
        } else {
            running = sensor_flag(thing, SENSOR_STATE);
            if (running) sensor_trigger_target(thing, sensor_effect(thing), 0);
        }
        if (!running) {
            sensor_set_flag(thing, SENSOR_BUSY, false);
        } else {
            e->time_map += e->data[2] * sensor_data(thing);
            timeline_add_event(e);
        }
        break;
    }
    case EVENT_SENSOR_ANIMATION_END:
        thing = (uint16_t)(e->data[2] | e->data[3] << 8);
        if (!sensor_flag(thing, SENSOR_STATE)) {
            if (party.cur_map == game.party_map) party.redraw |= REDRAW_VIEW;
            sensor_set_flag(thing, SENSOR_BUSY, false);
        }
        break;
    case EVENT_SENSOR_AMBIENT_SOUND: {
        thing = (uint16_t)(e->data[2] | e->data[3] << 8);
        if (sensor_flag(thing, SENSOR_BUSY) && (int8_t)(e->time_map >> 24) == game.party_map) {
            int wall = square_type(dun_get_square(e->data[0], e->data[1])) == SQUARE_WALL;
            e->time_map += (uint16_t)sensor_get_ornament_cycle(thing, wall);
            timeline_add_event(e);
            int16_t x = e->data[0], y = e->data[1];
            if (wall) {  /* the sound comes from the square in front of the wall, unless the party stands there */
                int16_t fx = (int16_t)(x + ambient_wall_dx[e->data[3] >> 6][e->data[3] & 3]);
                int16_t fy = (int16_t)(y + ambient_wall_dy[e->data[3] >> 6][e->data[3] & 3]);
                if (party.cur_map != party.square_map || fx != party.square_x || fy != party.square_y) {
                    x = fx;
                    y = fy;
                }
            }
            sound_play_with_fallback(wall ? CAT_ORNAMENT : CAT_ORNAMENT + 1,
                                     wall ? sensor_wall_ornament(thing) : sensor_floor_ornament(thing), 0x88, 0xfe,
                                     x, y, 1, 0x8c, 0x80);
        } else {
            sensor_set_data(thing, sensor_frame(thing));
        }
        break;
    }
    case EVENT_SENSOR_IDLE: case EVENT_SENSOR_READY:
        sensor_set_flag(thing, SENSOR_BUSY, false);
        break;
    case EVENT_DOOR_BUTTON_RELEASE:
        door_set_flag(thing, DOOR_BUTTON_PRESSED, false);
        break;
    case EVENT_SET_MARKER:
        text_set_visible(thing, true);
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
                    int kind = sensor_type(t);
                    if (kind == 0x21) {
                        if (sensor_effect(t) == EFFECT_HOLD) {
                            sensor_trigger_target(t, sensor_flag(t, SENSOR_INVERTED) == entering, 0);
                        } else if ((!sensor_flag(t, SENSOR_INVERTED)) == entering) {
                            sensor_trigger_target(t, sensor_effect(t), 0);
                        }
                    } else if (kind == 0x2c && entering && sensor_flag(t, SENSOR_BUSY)) {
                        sensor_start_ambient_sound(t, t, x, y, (uint16_t)sensor_get_ornament_cycle(t, wall), wall);
                    }
                } else if (thing_type(t) == THING_TEXT && entering) {
                    if (text_kind(t) == TEXT_KIND_ORNAMENT && text_subtype(t) == 0x15 && !text_is_visible(t)) {
                        text_set_visible(t, true);
                        creature_create_group(text_value(t), 7, rand_2bits(), x, y);
                    }
                }
            }
        }
    dun_set_current_map(saved);
}

/* ---- Wall clicks ---- */

#define ORNAMENT_KEY_TOUCH 0x0b      /* ornament value: touching the ornament fills the water (a fountain) */
#define ORNAMENT_KEY_ALCOVE 0x0a     /* ornament value 1: an alcove */
#define ORNAMENT_KEY_LADDER_UP 0x11  /* ornament value: the ladder goes up */
#define ORNAMENT_KEY_MARKER_DELAY 0x12
#define WATER_FULL 0x800
#define ITEM_EMPTY_FLASK 0x194
#define FLASK_FULL_INDEX 0x0f
#define ATTR_REFILLS 0x0001          /* item attribute 0 bit: the item takes water */
enum {
    WALL_CLICK_ANY = 1, WALL_CLICK_EMPTY_HAND, WALL_CLICK_ITEM, WALL_CLICK_CHARGED_ITEM = 0x15, WALL_CLICK_TOGGLE = 0x17,
    WALL_CLICK_BUTTON = 0x18, WALL_CLICK_COUNTED_ITEM = 0x1b, WALL_CLICK_TELEPORT = 0x1c, WALL_CLICK_RESET = 0x3f,
    WALL_CLICK_DOOR_BUTTON = 0x46, WALL_CLICK_BUTTON_2 = 0x4a, WALL_CLICK_PORTRAIT = 0x7e
};
enum { TEXT_LADDER = 6, TEXT_LADDER_HIDDEN = 7, TEXT_SWITCH = 0x0d };

/* True when the thing on a wall cell is the alcove ornament that the wall face shows. */
int dun_is_wall_thing_ornament_shown(uint16_t first, uint16_t thing)
{
    int ornament = item_gdat_index(thing);
    if (ornament == 0xff
        || gdat_entry_index(draw_gdat(), CAT_ORNAMENT, ornament, GDAT_TYPE_VALUE, ORNAMENT_KEY_ALCOVE) != 1)
        return 0;
    return view_wall_face_ornament(party.cur_map, first, ((thing >> THING_CELL_SHIFT) + 2) & 3) == ornament;
}

static int ornament_value(uint16_t thing, int key)
{
    int ornament = item_gdat_index(thing);
    return ornament == 0xff ? 0 : gdat_entry_index(draw_gdat(), CAT_ORNAMENT, ornament, GDAT_TYPE_VALUE, key);
}

int sensor_wall_ornament_value(uint16_t first, uint16_t thing, int key)
{
    return dun_is_wall_thing_ornament_shown(first, thing) ? ornament_value(thing, key) : 0;
}

/* The object of the item index type on the cell of the square (x, y), or a new one; THING_NONE when none can be made. */
static uint16_t wall_item(int x, int y, int cell, int item)
{
    for (uint16_t t = dungeon_square_first_object(party.cur_map, x, y); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        if (item_get_index(t) == item && (cell == -1 || t >> THING_CELL_SHIFT == cell)) return t;
    uint16_t t = item_create_by_index(item);
    if (t == THING_NONE) return t;
    t = (uint16_t)((t & 0x3fff) | cell << THING_CELL_SHIFT);
    dungeon_link_thing_to_square(t, party.cur_map, x, y);
    return t;
}

void sensor_process_wall_click(int x, int y, int side, uint16_t actor, uint16_t hand)
{
    const uint16_t hand_arg = hand;
    int stop = 0, take = 0, done = 0;
    uint16_t give = THING_NONE;
    int has_leader = (int16_t)game.leader != -1;
    uint16_t first = first_thing(x, y);
    for (uint16_t t = first; t != THING_ENDOFLIST && !done; t = dungeon_next_thing(t)) {
        int extra = 0, cell = t >> THING_CELL_SHIFT;
        if (cell != side) continue;
        if (dun_is_wall_thing_ornament_shown(first, t)) {
            if (hand == THING_NONE) {
                uint16_t object = THING_NONE;
                for (uint16_t o = dungeon_square_first_object(party.cur_map, x, y); o != THING_ENDOFLIST; o = dungeon_next_thing(o))
                    if (o >> THING_CELL_SHIFT == cell) object = o;
                if (object != THING_NONE && thing_type(object) >= THING_WEAPON && thing_type(object) <= THING_MISC) {
                    move_thing(object, x, y, -1, 0);
                    give = object;
                }
            } else {
                take = 1;
                move_thing((uint16_t)((hand & 0x3fff) | cell << THING_CELL_SHIFT), -1, 0, x, y);
            }
            break;
        }
        if (ornament_value(t, ORNAMENT_KEY_TOUCH)) {
            if (hand == THING_NONE) {
                if (!has_leader) break;
                for (int i = 0; i < CHAMPION_MAX; i++)
                    if (champion(i)->health) champion(i)->water = WATER_FULL;
                sound_play_with_fallback(GDAT_CHAMPION_CATEGORY, champion(game.leader)->portrait, 0x83, 0xfe, game.party_x,
                                         game.party_y, 0, 0x96, 0x80);
            } else if (item_attr(hand, 0) & ATTR_REFILLS) {
                item_add_charges(hand, 0xf);
                take = 1;
                give = hand;
            } else if (item_get_index(hand) == ITEM_EMPTY_FLASK) {
                item_set_gdat_index(hand, FLASK_FULL_INDEX);
                take = 1;
                give = hand;
            }
            break;
        }
        int type = thing_type(t);
        if (type == THING_SENSOR) {
            int kind = sensor_type(t), effect = sensor_effect(t);
            unsigned data = sensor_data(t);
            if (!has_leader && kind != WALL_CLICK_PORTRAIT) continue;
            int ornament = sensor_wall_ornament(t);
            if (!kind) continue;
            int skip = 1;
            switch (kind) {
            case WALL_CLICK_ANY:
                skip = 0;
                if (effect == EFFECT_HOLD) continue;
                break;
            case WALL_CLICK_EMPTY_HAND:
                skip = (hand == THING_NONE) != sensor_flag(t, SENSOR_INVERTED);
                break;
            case WALL_CLICK_CHARGED_ITEM:
                if (!item_add_charges(hand, 0)) break;
                /* fall through */
            case WALL_CLICK_ITEM: {
                int match = item_get_index(hand) == data;
                skip = match == sensor_flag(t, SENSOR_INVERTED);
                if (match && sensor_flag(t, SENSOR_STATE)) set_thing_word(champ_remove_object_from_leader_hand(), THING_NEXT, THING_NONE);  /* frees it */
                break;
            }
            case WALL_CLICK_TOGGLE: {
                if (hand != THING_NONE) continue;
                sensor_set_flag(t, SENSOR_STATE, !sensor_flag(t, SENSOR_STATE));
                skip = sensor_flag(t, SENSOR_INVERTED) == sensor_flag(t, SENSOR_STATE);
                break;
            }
            case WALL_CLICK_DOOR_BUTTON: {
                if (!door_flag(first_thing(sensor_target_x(t), sensor_target_y(t)), DOOR_SWITCH_ON)) continue;
            }
                /* fall through */
            case WALL_CLICK_BUTTON: case WALL_CLICK_BUTTON_2: {
                if (hand != THING_NONE || sensor_flag(t, SENSOR_BUSY)) continue;
                /* fix: the original leaves 4 data bytes of the event uninitialized */
                TimelineEvent e = {.time_map = event_time(data + game.game_time + 2), .type = EVENT_SENSOR_IDLE};
                e.data[0] = (uint8_t)t;
                e.data[1] = (uint8_t)(t >> 8);
                timeline_add_event(&e);
                sensor_set_flag(t, SENSOR_BUSY, true);
                skip = 0;
                if (sensor_flag(t, SENSOR_INVERTED)) extra = 0x10;
                if (effect != EFFECT_HOLD) break;
                stop = 1;
                if (sensor_flag(t, SENSOR_AUDIBLE))
                    sound_play_with_fallback(CAT_ORNAMENT, ornament, 0x88, 0xfe, game.party_x, game.party_y, 1, 0x8c, 0x80);
                sensor_trigger_target(t, EFFECT_SET, extra);
                sensor_trigger_target(t, EFFECT_CLEAR, (int16_t)(data + 1 + extra));
                continue;
            }
            case WALL_CLICK_PORTRAIT:
                if (sensor_flag(t, SENSOR_STATE) && side == ((game.party_dir + 2) & 3)) {
                    champ_start_recruit(game.party_x, game.party_y, game.party_dir, game.party_map);
                    done = 1;
                    skip = 0;
                }
                break;
            case WALL_ITEM_HOLDER: {
                int item = item_holder_item(ornament);
                if (sensor_flag(t, SENSOR_STATE)) {
                    if (hand != THING_NONE) {
                        done = 1;
                        continue;
                    }
                    uint16_t object = wall_item(x, y, cell, item);
                    if (object == THING_NONE) continue;
                    item_add_charges(object, 0xf);
                    move_thing(object, x, y, -1, 0);
                    give = object;
                } else {
                    if (hand == THING_NONE || item_get_index(hand) != item) continue;
                    take = 1;
                    move_thing((uint16_t)((hand & 0x3fff) | cell << THING_CELL_SHIFT), -1, 0, x, y);
                }
                hand = THING_NONE;
                party.redraw |= REDRAW_VIEW;
                done = 1;
                continue;
            }
            case WALL_CLICK_COUNTED_ITEM:
                if (!data || item_get_index(hand) != item_holder_item(ornament)) continue;
                take = 1;
                hand = THING_NONE;
                if (!(uint16_t)--data) {
                    skip = 0;
                    sensor_set_flag(t, SENSOR_BUSY, true);
                }
                sensor_set_data(t, data);
                break;
            case WALL_CLICK_TELEPORT: {
                if (hand != THING_NONE || sensor_flag(t, SENSOR_STATE)) continue;
                int dir = sensor_flag(t, SENSOR_INVERTED) ? sensor_effect(t) : (sensor_effect(t) + game.party_dir) & 3;
                move_party_to(sensor_target_x(t), sensor_target_y(t), data & 0x3f, dir);
                input_flush();
                break;
            }
            case WALL_CLICK_RESET:
                if (hand == THING_NONE) sensor_set_flag(t, SENSOR_BUSY, false);
                break;
            default:
                continue;
            }
            if (effect == EFFECT_HOLD) {
                effect = skip != 0;
                skip = 0;
            }
            if (skip) continue;
            stop = 1;
            if (sensor_flag(t, SENSOR_AUDIBLE))
                sound_play_with_fallback(CAT_ORNAMENT, ornament, 0x88, 0xfe, game.party_x, game.party_y, 1, 0x8c, 0x80);
            sensor_trigger_target(t, effect, extra);
        } else if (actor == THING_NONE && type == THING_TEXT) {
            if (text_kind(t) != TEXT_KIND_ORNAMENT || !has_leader) continue;
            int value = text_value(t), sub = text_subtype(t);
            bool visible = text_is_visible(t);
            if (sub == TEXT_ITEM_HOLDER || sub == TEXT_ITEM_HOLDER_2) {
                int item = item_holder_item(value);
                if (visible) {
                    if (hand != THING_NONE) {
                        done = 1;
                        continue;
                    }
                    uint16_t object = wall_item(x, y, cell, item);
                    if (object == THING_NONE) continue;
                    item_add_charges(object, 0xf);
                    move_thing(object, x, y, -1, 0);
                    give = object;
                    hand = THING_NONE;
                    if (sub == TEXT_ITEM_HOLDER_2) {
                        /* fix: the original leaves 4 data bytes of the event uninitialized */
                        uint16_t delay = gdat_entry_index(draw_gdat(), CAT_ORNAMENT, value, GDAT_TYPE_VALUE,
                                                          ORNAMENT_KEY_MARKER_DELAY);
                        TimelineEvent e = {.time_map = event_time(game.game_time + delay + 2), .type = EVENT_SET_MARKER};
                        e.data[0] = (uint8_t)t;
                        e.data[1] = (uint8_t)(t >> 8);
                        timeline_add_event(&e);
                    }
                } else if (sub != TEXT_ITEM_HOLDER_2) {
                    if (hand == THING_NONE || item_get_index(hand) != item) continue;
                    take = 1;
                    move_thing((uint16_t)((hand & 0x3fff) | cell << THING_CELL_SHIFT), -1, 0, x, y);
                    hand = THING_NONE;
                }
                party.redraw |= REDRAW_VIEW;
                done = 1;
            } else if ((sub == TEXT_LADDER_HIDDEN && !visible) || sub == TEXT_LADDER) {
                if (hand != THING_NONE) continue;
                int delta = gdat_entry_index(draw_gdat(), CAT_ORNAMENT, value, GDAT_TYPE_VALUE, ORNAMENT_KEY_LADDER_UP) ? -1 : 1;
                int lx = game.party_x, ly = game.party_y;
                int map = dungeon_location_on_map(game.party_map, delta, &lx, &ly);
                if (map >= 0) {
                    move_party_to(lx, ly, map, game.party_dir);
                    input_flush();
                }
            } else if (sub == TEXT_SWITCH && hand == THING_NONE) {
                text_set_visible(t, !visible);
            }
        } else if (type >= THING_SENSOR) {
            break;
        }
    }
    if (actor == THING_NONE) {
        if (take) champ_remove_object_from_leader_hand();
        if (give != THING_NONE) champ_put_object_in_leader_hand(give, 1);
        input_command_stop = stop;
    } else {
        uint16_t *possessions = (uint16_t *)(void *)(dungeon_thing_record(actor) + GROUP_POSSESSIONS);
        if (!take && hand_arg != THING_NONE) dungeon_append_thing(hand_arg, possessions);
        if (give != THING_NONE) dungeon_append_thing(give, possessions);
    }
}
