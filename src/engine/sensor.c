/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Floor and wall sensors, square events, doors, pits, teleporters, and fake walls. */
#include "sensor.h"
#include <stdlib.h>
#include <string.h>
#include "action.h"
#include "ai.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "game.h"
#include "game_text.h"
#include "gdat.h"
#include "hooks.h"
#include "item.h"
#include "le.h"
#include "money.h"
#include "move.h"
#include "projectile.h"
#include "random.h"
#include "recruit.h"
#include "save.h"
#include "sound_request.h"

/* The sound category of a creature that a generator creates: sound (0, 0, 0x89). */
#define CAT_CREATURE_APPEARS     0x00
/* Values of the door and ornament GDAT entries. */
#define DOOR_KEY_STRENGTH        0x0e /* door value: the strength that breaks the door */
#define DOOR_KEY_DAMAGE          0x0f /* door value: the damage of a closing door */
#define ORNAMENT_KEY_ITEM        0x0e /* ornament value: the item that the ornament takes */
#define ORNAMENT_KEY_ALTAR       0x0c /* ornament value: an altar that brings back a champion from its bones */
#define ORNAMENT_KEY_FRAMES      0x0d /* ornament value (or text): the frame count of its animation */
#define ORNAMENT_KEY_SOUND_DELAY 0x88 /* ornament value: the delay of its sound in its animation cycle */
/* The frame count in the ORNAMENT_KEY_FRAMES value. */
#define ORNAMENT_FRAMES_MASK     0x7fff

/* Sound arguments (entry, priority) of sndreq_request_with_fallback and sndreq_request. */
#define SOUND_PRIORITY_MAP_SOUND 0x73
#define SOUND_ENTRY_DOOR_MOVE    0x8e
#define SOUND_ENTRY_DOOR_SHUT    0x8f
#define SOUND_PRIORITY_DOOR      0x5f

/* Sensor types of square events. A wall and a floor give some types other meanings. */
#define SENSOR_GAME_END           0x12
#define SENSOR_MAP_RELAY          0x16
#define SENSOR_COUNTER            0x1d
#define SENSOR_TIMER              0x1e /* a timer sensor; 0x33 to 0x37 count in steps of 8 to 128 ticks */
#define SENSOR_RELAY              0x20
#define SENSOR_MAP_ENTRY          0x21
#define SENSOR_WALL_STATE         0x26
#define SENSOR_ANIMATION          0x2c
#define SENSOR_COUNTDOWN          0x2d
#define SENSOR_GENERATOR          0x2e
#define SENSOR_PARTY_TELEPORT     0x2e
#define SENSOR_BUSY_RELAY         0x31
#define SENSOR_ACTIVATION         0x32
#define SENSOR_TIMER_8            0x33
#define SENSOR_TIMER_16           0x34
#define SENSOR_TIMER_32           0x35
#define SENSOR_TIMER_64           0x36
#define SENSOR_TIMER_128          0x37
#define SENSOR_CREATURE_ANIMATION 0x3a
#define SENSOR_TRANSFER           0x3b
#define SENSOR_CREATE_ITEM        0x3c
#define SENSOR_FORWARD            0x3d
#define SENSOR_TRANSFER_CLASS     0x40
#define SENSOR_FRAME_STEP         0x41
#define SENSOR_TURN_CREATURES     0x42
#define SENSOR_SET_GLOBAL         0x43
#define SENSOR_TEST_GLOBAL        0x44
#define SENSOR_RELAY_SCALED       0x45
#define SENSOR_DOOR_SWITCH        0x46
#define SENSOR_TRANSFER_BACK      0x47
#define SENSOR_TRANSFER_ONE       0x48
#define SENSOR_TRANSFER_BACK_ONE  0x49
/* The effect bits of SENSOR_CREATE_ITEM: the new item is kept from reuse. */
#define SENSOR_KEEP_ITEM          EFFECT_HOLD
/* The period factor of the slow timers SENSOR_TIMER_8 to SENSOR_TIMER_128. */
#define TIMER_SLOW_FACTOR         8

/* Square event priorities by effect. */
#define SQUARE_PRIORITY_SET    1
#define SQUARE_PRIORITY_TOGGLE 2
#define SQUARE_PRIORITY_CLEAR  3
/* The ticks and steps of the square and click events. */
#define PIT_OPEN_TICKS         5 /* a random pit closes again after 5 ticks */
#define ALTAR_REBIRTH_STEPS    2
#define CLICK_EVENT_DELAY      2  /* the ticks before a clicked button turns idle or an item holder shows its marker */
#define BUTTON_INVERTED_DELAY  16 /* an inverted button triggers 16 ticks later */
/* A party that leaves a fall risk square: its chance in percent to fall to the next level. */
#define FALL_LOAD_FACTOR       10 /* per champion load / half of the maximum load */
#define FALL_CHANCE_SHOWN      50 /* the text is visible */
#define FALL_CHANCE_HIDDEN     25
#define FALL_CHANCE_MAX        90

/* The power of a vanish sensor that makes the creatures vanish. */
#define VANISH_POWER 2

/* The delay of the next creature of a generator text: a base and a random part as large. */
#define GENERATOR_SLOW_DELAY        512
#define GENERATOR_FAST_DELAY        8192
#define GENERATOR_SLOW_SHOWN_FACTOR 4 /* a visible generator text waits longer */

static uint8_t *current_square(int x, int y)
{
    return dungeon_square_byte(g_party.cur_map, x, y);
}

static uint16_t first_thing(int x, int y)
{
    return dungeon_square_first_thing(g_party.cur_map, x, y);
}

static uint32_t current_map_time(uint32_t time)
{
    return timeline_time_map(time & EVENT_TIME_MASK, g_party.cur_map);
}

/* ---- Doors and square events ---- */

uint16_t sensor_door_value(uint16_t door, int key)
{
    return gdat_entry_index(gdat_current(), CAT_DOOR, dun_get_door_type(door), GDAT_TYPE_VALUE, key);
}

static int holder_item(int ornament)
{
    return gdat_entry_index(gdat_current(), CAT_WALL_ORNAMENT, ornament, GDAT_TYPE_VALUE, ORNAMENT_KEY_ITEM);
}

/* The wall ornament (GDAT index) of a sensor, 0xff none. */
static int wall_ornament(uint16_t sensor)
{
    int index = sensor_ornament_index(sensor);
    return index >= 0 ? dungeon_map_wall_ornaments(g_party.cur_map)[index] : GDAT_NO_INDEX;
}

static int floor_ornament(uint16_t sensor)
{
    int index = sensor_ornament_index(sensor);
    return index >= 0 ? dungeon_map_floor_ornaments(g_party.cur_map)[index] : GDAT_NO_INDEX;
}

static int ornament_category(int wall)
{
    return wall ? CAT_WALL_ORNAMENT : CAT_FLOOR_ORNAMENT;
}

static int wall_or_floor_ornament(uint16_t sensor, int wall)
{
    return wall ? wall_ornament(sensor) : floor_ornament(sensor);
}

/* The sound of the ornament of a sensor at (x, y), with delay. */
static void ornament_sound(uint16_t sensor, int wall, int x, int y, int delay)
{
    sndreq_request_with_fallback((SoundCue){ornament_category(wall), wall_or_floor_ornament(sensor, wall),
                                            SOUND_ENTRY_ACTIVATE, SOUND_PRIORITY_CLICK, SOUND_VOLUME_NORMAL},
                                 SOUND_FALLBACK, x, y, delay);
}

/* A wall sound comes from the square in front of the wall side, unless the party stands in the wall square. */
static void sound_square_in_front(int *x, int *y, int side)
{
    int dx = *x + g_dir_dx[side];
    int dy = *y + g_dir_dy[side];
    if (g_party.cur_map != g_party.square_map || dx != g_party.square_x || dy != g_party.square_y) {
        *x = dx;
        *y = dy;
    }
}

/* True when the party carries an item of the item index type. */
static bool contains_item_type(uint16_t thing, int type)
{
    if (thing_type(thing) != THING_CONTAINER || container_kind(thing) != CONTAINER_CHEST)
        return false;
    for (uint16_t t = thing_word(thing, CONTAINER_CONTENTS); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        if (item_get_index(t) == type || contains_item_type(t, type))
            return true;
    return false;
}

int sensor_party_possesses_type(int type)
{
    for (int i = 0; i < g_game.champion_count; i++) {
        Champion *c = champion(i);
        if (!c->health)
            continue;
        for (int s = 0; s < SLOT_COUNT; s++)
            if (item_get_index(c->slots[s]) == type || contains_item_type(c->slots[s], type))
                return 1;
    }
    if (g_action.mode == ACTION_MODE_CONTAINER)
        for (int s = 0; s < CONTAINER_SLOTS; s++)
            if (g_action.open_items[s] != THING_NONE && item_get_index(g_action.open_items[s]) == type)
                return 1;
    return item_get_index(g_game.leader_hand) == type || contains_item_type(g_game.leader_hand, type);
}

/* True when a static creature group holds an item of the item index type. */
static bool possessions_hold_type(uint16_t group, int type)
{
    for (uint16_t t = thing_word(group, GROUP_POSSESSIONS); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        if (item_get_index(t) == type)
            return true;
    return false;
}

void sensor_schedule_square_event(int16_t x, int16_t y, int16_t cell, int effect, uint32_t time)
{
    TimelineEvent e = {.time_map = current_map_time(time), .type = EVENT_SQUARE};
    /* fix: the original leaves the priority of a hold effect uninitialized; it gets the priority of a clear */
    e.priority = effect == EFFECT_SET                              ? SQUARE_PRIORITY_SET
                 : effect == EFFECT_CLEAR || effect == EFFECT_HOLD ? SQUARE_PRIORITY_CLEAR
                                                                   : SQUARE_PRIORITY_TOGGLE;
    e.data[EVENT_X] = (uint8_t)x;
    e.data[EVENT_Y] = (uint8_t)y;
    e.data[EVENT_CELL] = (uint8_t)cell;
    e.data[EVENT_EFFECT] = (uint8_t)effect;
    timeline_add_event(&e);
}

void sensor_press_door_button(Location at, uint16_t door)
{
    sensor_schedule_square_event(at.x, at.y, 0, EFFECT_TOGGLE, g_game.game_time + 1);
    TimelineEvent e = {.time_map = current_map_time(g_game.game_time + 1), .type = EVENT_DOOR_BUTTON_RELEASE};
    put_le16(e.data + SENSOR_EVENT_THING, first_thing(at.x, at.y));
    timeline_add_event(&e);
    door_set_flag(door, DOOR_BUTTON_PRESSED, true);
}

void sensor_trigger_target(uint16_t sensor, int effect, int delay)
{
    sensor_schedule_square_event(sensor_target_x(sensor), sensor_target_y(sensor), sensor_target_cell(sensor), effect,
                                 (uint32_t)((int16_t)delay + sensor_delay(sensor) + g_game.game_time));
}

bool sensor_door_try_destroy(int16_t x, int16_t y, int strength, bool magic, int delay)
{
    uint16_t door = first_thing(x, y);
    if (!door_flag(door, magic ? DOOR_BREAKABLE_BY_MAGIC : DOOR_BREAKABLE))
        return false;
    if ((int16_t)strength < (int16_t)sensor_door_value(door, DOOR_KEY_STRENGTH))
        return false;
    uint8_t *square = current_square(x, y);
    if ((*square & SQUARE_DOOR_STATE) != DOOR_STATE_CLOSED)
        return false;
    if (delay) {
        /* fix: the original leaves 4 data bytes of the event uninitialized */
        TimelineEvent e = {.time_map = current_map_time((uint16_t)delay + g_game.game_time),
                           .type = EVENT_DOOR_DESTRUCTION};
        e.data[EVENT_X] = (uint8_t)x;
        e.data[EVENT_Y] = (uint8_t)y;
        timeline_add_event(&e);
    } else {
        *square = (uint8_t)((*square & ~SQUARE_DOOR_STATE) | DOOR_STATE_BROKEN);
    }
    return true;
}

/* ---- Floor sensors ---- */

/* A sensor fires: the inverted flag, then the effect (hold follows the trigger), then its target. */
static void fire_sensor(uint16_t sensor, int trigger, int floor, int x, int y)
{
    trigger ^= sensor_flag(sensor, SENSOR_INVERTED);
    int effect = sensor_effect(sensor);
    if (effect == EFFECT_HOLD)
        effect = !trigger;
    else if (!trigger)
        return;
    if (sensor_flag(sensor, SENSOR_AUDIBLE))
        ornament_sound(sensor, !floor, x, y, 1);
    sensor_trigger_target(sensor, effect, 0);
}

/* The party leaves a square with a fall risk text: by the champion loads, it may fall to the next level. */
static void fall_risk(uint16_t text, int x, int y, int on_square)
{
    int load = 0;
    for (int i = 0; i < g_game.champion_count; i++) {
        Champion *c = champion(i);
        if (c->health)
            load = (int16_t)(load + (uint16_t)champ_get_load(i) / (int)((uint16_t)champ_get_max_load(c) >> 1));
    }
    int chance =
        (int16_t)(load * FALL_LOAD_FACTOR + (textrec_is_visible(text) ? FALL_CHANCE_SHOWN : FALL_CHANCE_HIDDEN));
    if (chance > FALL_CHANCE_MAX)
        chance = FALL_CHANCE_MAX;
    if (!on_square && (int)rand_below(PERCENT) < chance) {
        /* fix: the original leaves the high bits of the event data uninitialized */
        TimelineEvent e = {.time_map = current_map_time(g_game.game_time), .type = EVENT_TELEPORT_PARTY};
        put_le16(e.data + TELEPORT_EVENT_TARGET, event_square_dir(x, y, g_game.party_dir));
        e.data[TELEPORT_EVENT_MAP] = (uint8_t)g_game.party_map;
        timeline_add_event(&e);
        int c = rand_2bits();
        if (!champion(c)->health)
            c = g_game.leader;
        sndreq_request_with_fallback(
            (SoundCue){CAT_CHAMPION, champion(c)->portrait, SOUND_HURT, SOUND_PRIORITY_HURT, SOUND_VOLUME_LOUD},
            SOUND_FALLBACK, g_game.party_x, g_game.party_y, 1);
    } else {
        sndreq_request((SoundCue){CAT_FLOOR_ORNAMENT, textrec_value(text), SOUND_ENTRY_ACTIVATE, SOUND_PRIORITY_CLICK,
                                  SOUND_VOLUME_NORMAL},
                       g_game.party_x, g_game.party_y, 1);
    }
}

/* A thing that moves on or off a square, and what else lies there (on a wall: on its cell). */
typedef struct {
    int type, item; /* the thing type and item index of the thing, -1 for the party */
    int cell;       /* the wall cell, -1 on a floor */
    bool enter, on_square;
    bool other_item, same_item, creature, items;
} FloorMove;

/* The static creature on (x, y) whose possessions take or give thing, THING_NONE for the square. */
static uint16_t floor_holder(int x, int y, bool enter, bool possessions)
{
    uint16_t g = creature_group_at(g_party.cur_map, x, y);
    if (g == THING_NONE || !possessions)
        return THING_NONE;
    const CreatureInfo *info = creature_group_info(g);
    if (!enter || (info->flags & CREATURE_STATIC && info->possessions & CREATURE_POSSESSIONS_ON_FLOOR))
        return g;
    return THING_NONE;
}

/* Looks at what else lies on the square; the party that steps on a floor reads its visible texts. */
static void floor_scan(FloorMove *m, int x, int y)
{
    for (uint16_t t = first_thing(x, y); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        int tt = thing_type(t);
        if (m->cell == -1) {
            if (tt == THING_GROUP && !creature_is_flying(t)) {
                if (creature_group_info(t)->flags & CREATURE_STATIC && possessions_hold_type(t, m->item))
                    m->same_item = true;
                m->creature = true;
            } else if (tt == THING_TEXT && m->type == -1 && m->enter && !m->on_square) {
                if (textrec_kind(t) == TEXT_KIND_TEXT && textrec_is_visible(t)) {
                    char text[TEXT_SIZE];
                    g_hooks.print_message(UI_WHITE, gtext_decode_dungeon(text, sizeof text, t, true, false));
                }
            } else if (tt > THING_GROUP && tt < THING_PROJECTILE) {
                m->items = true;
                m->same_item |= item_get_index(t) == m->item;
                m->other_item |= item_get_index(t) != m->item;
            }
        } else if (m->cell == thing_cell(t) && tt > THING_GROUP) {
            m->items = true;
            m->same_item |= item_get_index(t) == m->item;
            m->other_item |= item_get_index(t) != m->item;
        }
    }
}

/* True when thing is the bones of a champion that the altar ornament t on its wall cell takes. */
static int altar_takes_bones(uint16_t t, uint16_t thing)
{
    return item_gdat_index(t) != GDAT_NO_INDEX &&
           gdat_entry_index(gdat_current(), CAT_WALL_ORNAMENT, item_gdat_index(t), GDAT_TYPE_VALUE,
                            ORNAMENT_KEY_ALTAR) &&
           item_gdat_category(thing) == CAT_MISC && item_gdat_index(thing) == ITEM_BONES;
}

/* The altar brings back the champion of the bones. */
static void altar_schedule_rebirth(uint16_t bones, int x, int y, int cell)
{
    int champ = item_charges(bones);
    if (champ < 0 || champ >= g_game.champion_count)
        return;
    TimelineEvent e = {
        .time_map = current_map_time(g_game.game_time + 1), .type = EVENT_ALTAR_REBIRTH, .priority = (uint8_t)champ};
    e.data[EVENT_X] = (uint8_t)x;
    e.data[EVENT_Y] = (uint8_t)y;
    e.data[EVENT_CELL] = (uint8_t)cell;
    e.data[ALTAR_EVENT_STEPS] = ALTAR_REBIRTH_STEPS;
    timeline_add_event(&e);
}

/* The trigger of a floor sensor for the move, -1 when the sensor does not react. */
static int floor_sensor_trigger(const FloorMove *m, int kind, int data)
{
    switch (kind) {
    case FLOOR_ANY:
        return m->on_square || m->items || m->creature ? -1 : m->enter;
    case FLOOR_PARTY_OR_CREATURE:
        return m->type > THING_GROUP || m->on_square || m->creature ? -1 : m->enter;
    case FLOOR_PARTY:
        if (m->type != -1 || !g_game.champion_count)
            return -1;
        if (data)
            return m->enter ? data == g_game.party_dir + 1 : 0;
        return m->on_square ? -1 : m->enter;
    case FLOOR_ITEM:
        return data != m->item || m->same_item ? -1 : m->enter;
    case FLOOR_CREATURE:
        return m->type > THING_GROUP || m->type == -1 || m->creature ? -1 : m->enter;
    case FLOOR_PARTY_POSSESSES:
        return m->type != -1 ? -1 : sensor_party_possesses_type(data);
    }
    return -1;
}

/* The trigger of a wall sensor t on the cell of the move, -1 when the sensor does not react. */
static int wall_sensor_trigger(const FloorMove *m, uint16_t t, int kind, int data)
{
    switch (kind) {
    case WALL_ANY_ITEM:
        return m->items ? -1 : m->enter;
    case WALL_ITEM:
        return m->same_item || data != m->item ? -1 : m->enter;
    case WALL_NOT_ITEM:
        return m->other_item || data == m->item ? -1 : m->enter;
    case WALL_ITEM_HOLDER:
        if (m->same_item || sensor_flag(t, SENSOR_STATE) == m->enter)
            return -1;
        if (m->item != holder_item(wall_ornament(t)))
            return -1;
        sensor_set_flag(t, SENSOR_STATE, m->enter & 1);
        return m->enter;
    }
    return -1;
}

/* An ornament text of the square reacts to the move: a random pit, a fall risk, an item holder on a wall. */
static void floor_text(const FloorMove *m, uint16_t t, int x, int y)
{
    if (textrec_kind(t) != TEXT_KIND_ORNAMENT)
        return;
    int sub = textrec_subtype(t);
    if (m->cell == -1) {
        if (sub == SUBTYPE_RANDOM_PIT) {
            if (m->type == -1 && !m->on_square && g_game.champion_count && rand_below(PERCENT) < textrec_value(t) &&
                textrec_is_visible(t) != m->enter) {
                sensor_schedule_square_event(x, y, 0, EFFECT_SET, g_game.game_time + 1);
                sensor_schedule_square_event(x, y, 0, EFFECT_CLEAR, g_game.game_time + PIT_OPEN_TICKS);
            }
        } else if (sub == SUBTYPE_TRAP_TELEPORT && m->type == -1 && !m->enter && g_game.champion_count) {
            fall_risk(t, x, y, m->on_square);
        }
    } else if (m->cell == thing_cell(t) && (sub == SUBTYPE_ITEM_HOLDER || sub == SUBTYPE_ITEM_HOLDER_MARKER) &&
               !m->same_item && textrec_is_visible(t) != m->enter && m->item == holder_item(textrec_value(t))) {
        textrec_set_visible(t, m->enter & 1);
    }
}

/* A thing (THING_NONE: the party) leaves or enters (m.enter) the square (x, y) of the current map: the floor sensors
   and texts of the square react. m.on_square: the party stands there; possessions: an item goes into the
   possessions of a static creature. */
static void process_floor(int x, int y, uint16_t thing, FloorMove m, bool possessions)
{
    if (thing == THING_NONE && game_has_cheat(CHEAT_WALLS) &&
        square_type(dungeon_square(g_party.cur_map, x, y)) == SQUARE_WALL)
        return; /* the party in a wall (walk-through-walls cheat) does not touch the wall sensors */
    m.type = -1;
    m.item = -1;
    uint16_t holder = THING_NONE; /* a static creature whose possessions take the thing */
    if (thing != THING_NONE) {
        m.type = thing_type(thing);
        m.item = item_get_index(thing);
        holder = floor_holder(x, y, m.enter, possessions);
    }
    if (!m.enter && m.type != -1) {
        if (holder != THING_NONE)
            dungeon_unlink_thing_from_list(thing, group_possessions(holder));
        else
            dungeon_unlink_thing_from_square(thing, g_party.cur_map, x, y);
    }
    m.cell = square_type(dungeon_square(g_party.cur_map, x, y)) == SQUARE_WALL ? thing_cell(thing) : -1;
    floor_scan(&m, x, y);
    if (m.enter && m.type != -1) {
        if (holder != THING_NONE)
            dungeon_append_thing(thing, group_possessions(holder));
        else
            dungeon_link_thing_to_square(thing, g_party.cur_map, x, y);
    }
    for (uint16_t t = first_thing(x, y); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (m.cell != -1 && altar_takes_bones(t, thing)) {
            altar_schedule_rebirth(thing, x, y, m.cell);
            return;
        }
        int tt = thing_type(t);
        if (tt >= THING_GROUP)
            return;
        if (tt == THING_SENSOR) {
            int kind = sensor_type(t);
            int data = sensor_data(t);
            if (!kind)
                continue;
            if (m.cell != -1 && m.cell != thing_cell(t))
                continue;
            int trigger = m.cell == -1 ? floor_sensor_trigger(&m, kind, data) : wall_sensor_trigger(&m, t, kind, data);
            if (trigger == -1)
                continue;
            fire_sensor(t, trigger, m.cell == -1, x, y);
        } else if (tt == THING_TEXT) {
            floor_text(&m, t, x, y);
        }
    }
}

void sensor_enter_square(int16_t x, int16_t y, uint16_t thing, bool on_square, bool into_possessions)
{
    process_floor(x, y, thing, (FloorMove){.enter = true, .on_square = on_square}, into_possessions);
}

void sensor_leave_square(int16_t x, int16_t y, uint16_t thing, bool on_square, bool into_possessions)
{
    process_floor(x, y, thing, (FloorMove){.enter = false, .on_square = on_square}, into_possessions);
}

/* ---- Sensor effects ---- */

/* The new state of a two-state target: set 1, clear 0, toggle the state. */
static int apply_effect(int effect, int state)
{
    if (effect == EFFECT_SET)
        return 1;
    if (effect == EFFECT_TOGGLE)
        return state ^ 1;
    return 0;
}

/* True when an event effect activates a sensor: set, or clear for an inverted sensor. */
static bool effect_activates(uint16_t sensor, int effect)
{
    return sensor_flag(sensor, SENSOR_INVERTED) ? effect == EFFECT_CLEAR : effect == EFFECT_SET;
}

/* The effect that a sensor passes on: its own effect when its state option is on, else the event effect. */
static int passed_effect(uint16_t sensor, int effect)
{
    return sensor_flag(sensor, SENSOR_STATE) ? sensor_effect(sensor) : effect;
}

/* A relay: passes the event on to its target after the delay in its data (scaled: data << delay bits). */
static void relay_delayed(const TimelineEvent *e, uint16_t sensor, int scaled)
{
    int effect = e->data[EVENT_EFFECT];
    if (sensor_flag(sensor, SENSOR_STATE) && !effect_activates(sensor, effect))
        return;
    unsigned data = sensor_data(sensor);
    unsigned bits = sensor_delay(sensor);
    uint32_t time = scaled ? (data << bits) + g_game.game_time : g_game.game_time + data + bits;
    sensor_schedule_square_event(sensor_target_x(sensor), sensor_target_y(sensor), sensor_target_cell(sensor),
                                 passed_effect(sensor, effect), time);
}

/* Passes the event effect (hold sensors) or the own effect of the sensor to its target. */
static void forward_effect(const TimelineEvent *e, uint16_t sensor, int delay)
{
    int effect = e->data[EVENT_EFFECT];
    if (sensor_effect(sensor) == EFFECT_HOLD) {
        if (!sensor_flag(sensor, SENSOR_INVERTED)) {
            sensor_trigger_target(sensor, effect, delay);
            return;
        }
        sensor_trigger_target(sensor, effect, 0);
        if (!delay)
            return;
        /* fix: the original leaves the reverse of a hold effect uninitialized; it stays a hold here */
        static const uint8_t reverse[] = {EFFECT_CLEAR, EFFECT_SET, EFFECT_TOGGLE, EFFECT_HOLD};
        sensor_trigger_target(sensor, reverse[effect & SENSOR_EFFECT_MASK], delay);
        return;
    }
    if (!effect_activates(sensor, effect))
        return;
    sensor_trigger_target(sensor, sensor_effect(sensor), delay);
}

/* Sets, clears, or toggles the script variable of the sensor data; an inverted sensor adds or subtracts 1. */
static void set_global_flag(const TimelineEvent *e, uint16_t sensor)
{
    int op = e->data[EVENT_EFFECT] + (sensor_flag(sensor, SENSOR_INVERTED) ? GLOBAL_VAR_ADD : GLOBAL_VAR_SET);
    dun_modify_global_var(sensor_data(sensor), 1, op);
    sensor_trigger_target(sensor, passed_effect(sensor, e->data[EVENT_EFFECT]), 0);
}

/* Triggers the target when the script variable of the sensor data matches the event effect (inverted: the reverse). */
static void test_global_flag(const TimelineEvent *e, uint16_t sensor)
{
    bool set = dun_get_global_var(sensor_data(sensor)) != 0;
    bool inverted = sensor_flag(sensor, SENSOR_INVERTED);
    int effect = e->data[EVENT_EFFECT];
    if ((set != inverted && (effect == EFFECT_SET || effect == EFFECT_TOGGLE)) ||
        (set == inverted && effect == EFFECT_CLEAR))
        sensor_trigger_target(sensor, passed_effect(sensor, effect), 0);
}

/* The frame count of the animation of the ornament of a sensor (wall or floor). */
static unsigned ornament_cycle(uint16_t sensor, int wall)
{
    int category = ornament_category(wall);
    int ornament = wall_or_floor_ornament(sensor, wall);
    if (ornament == GDAT_NO_INDEX)
        return 1;
    uint16_t frames = gdat_entry_index(gdat_current(), category, ornament, GDAT_TYPE_VALUE, ORNAMENT_KEY_FRAMES);
    if (frames)
        return frames & ORNAMENT_FRAMES_MASK;
    char text[TEXT_SIZE];
    gdat_text(gdat_current(), (GdatKey){category, ornament, ORNAMENT_KEY_FRAMES}, text, sizeof text);
    size_t length = strlen(text);
    return length ? (uint16_t)length : 1;
}

/* An audible animated ornament starts its repeating sound at the next start of its animation cycle. */
static void start_ambient_sound(uint16_t sensor, int x, int y, unsigned cycle, bool wall)
{
    if (!sensor_flag(sensor, SENSOR_AUDIBLE) || sensor_sound_on(sensor))
        return;
    int frame = sensor_frame(sensor);
    uint32_t start = g_game.game_time + frame;
    uint16_t offset = gdat_entry_index(gdat_current(), ornament_category(wall), wall_or_floor_ornament(sensor, wall),
                                       GDAT_TYPE_VALUE, ORNAMENT_KEY_SOUND_DELAY);
    unsigned phase = (offset + start) % cycle;
    TimelineEvent e = {.time_map = current_map_time(cycle - phase + g_game.game_time),
                       .type = EVENT_SENSOR_AMBIENT_SOUND};
    e.data[EVENT_X] = (uint8_t)x;
    e.data[EVENT_Y] = (uint8_t)y;
    put_le16(e.data + SENSOR_EVENT_SQUARE_THING, sensor);
    timeline_add_event(&e);
    sensor_set_data(sensor, SENSOR_DATA_SOUND | frame);
    if (phase)
        return; /* the sound plays now only when the animation cycle starts now */
    if (wall)
        sound_square_in_front(&x, &y, thing_cell(sensor));
    ornament_sound(sensor, wall, x, y, 1);
}

/* An animated ornament turns on (it runs) or off (it stops at the end of its cycle). */
static void toggle_animation(const TimelineEvent *e, uint16_t sensor, bool wall)
{
    bool old = sensor_flag(sensor, SENSOR_STATE);
    int now = apply_effect(e->data[EVENT_EFFECT], old) & 1;
    sensor_set_flag(sensor, SENSOR_STATE, now);
    if (old != now) {
        unsigned cycle = (uint16_t)ornament_cycle(sensor, wall);
        if (now) {
            if (!sensor_flag(sensor, SENSOR_BUSY)) {
                sensor_set_flag(sensor, SENSOR_BUSY, true);
                if (g_party.cur_map == g_game.party_map)
                    g_party.redraw |= REDRAW_VIEW;
                unsigned frame = (cycle - g_game.game_time % cycle) % cycle & SENSOR_DATA_FRAME_MASK;
                sensor_set_data(sensor, (sensor_data(sensor) & SENSOR_DATA_SOUND) | frame);
                start_ambient_sound(sensor, e->data[EVENT_X], e->data[EVENT_Y], cycle, wall);
            }
        } else {
            unsigned r = (sensor_frame(sensor) + g_game.game_time) % cycle;
            if (!(uint16_t)r) {
                if (g_party.cur_map == g_game.party_map)
                    g_party.redraw |= REDRAW_VIEW;
                sensor_set_flag(sensor, SENSOR_BUSY, false);
            } else {
                /* fix: the original leaves 2 of the data bytes uninitialized */
                TimelineEvent ev = {.time_map = current_map_time(g_game.game_time + (int16_t)(cycle - r)),
                                    .type = EVENT_SENSOR_ANIMATION_END};
                put_le16(ev.data + SENSOR_EVENT_SQUARE_THING, sensor);
                timeline_add_event(&ev);
            }
        }
    }
    if (sensor_flag(sensor, SENSOR_INVERTED) && sensor_effect(sensor) == EFFECT_HOLD)
        sensor_trigger_target(sensor, e->data[EVENT_EFFECT], 0);
}

/* A sensor with a one-shot animation: it runs once (busy until its end); with its state set it also forwards. */
static void run_activation(const TimelineEvent *e, uint16_t sensor, bool wall)
{
    if (!sensor_flag(sensor, SENSOR_BUSY)) {
        sensor_set_flag(sensor, SENSOR_BUSY, true);
        sensor_set_data(sensor, 0);
        TimelineEvent ev = {.time_map = current_map_time(g_game.game_time + 1), .type = EVENT_SENSOR_ANIMATION_STEP};
        put_le16(ev.data + SENSOR_EVENT_THING, sensor);
        ev.data[SENSOR_EVENT_WALL] = (uint8_t)wall;
        timeline_add_event(&ev);
        if (sensor_flag(sensor, SENSOR_AUDIBLE)) {
            int x = e->data[EVENT_X];
            int y = e->data[EVENT_Y];
            if (wall)
                sound_square_in_front(&x, &y, e->data[EVENT_EFFECT] & DIR_MASK);
            int delay = (int16_t)(gdat_entry_index(gdat_current(), ornament_category(wall),
                                                   wall_or_floor_ornament(sensor, wall), GDAT_TYPE_VALUE,
                                                   ORNAMENT_KEY_SOUND_DELAY) +
                                  1);
            ornament_sound(sensor, wall, x, y, delay);
        }
    }
    if (sensor_flag(sensor, SENSOR_STATE))
        forward_effect(e, sensor, 0);
}

/* The tick factor of the period of a timer sensor: 1, or 8 to 128 for the slow timers. */
static unsigned timer_factor(int kind)
{
    return kind >= SENSOR_TIMER_8 && kind <= SENSOR_TIMER_128 ? (unsigned)TIMER_SLOW_FACTOR << (kind - SENSOR_TIMER_8)
                                                              : 1;
}

static bool is_timer(int kind)
{
    return kind == SENSOR_TIMER || (kind >= SENSOR_TIMER_8 && kind <= SENSOR_TIMER_128);
}

/* Starts the repeating timer of a timer sensor; the type sets the period factor. */
static void schedule_timer(uint16_t sensor, uint16_t thing)
{
    uint16_t period = sensor_data(sensor);
    unsigned factor = timer_factor(sensor_type(sensor));
    if (!period)
        return;
    /* fix: the original leaves bits 9-15 of the period word uninitialized; bit 8 is the timer phase */
    TimelineEvent e = {.time_map = current_map_time(g_game.game_time % (period * factor) + g_game.game_time),
                       .type = EVENT_SENSOR_TIMER};
    put_le16(e.data + SENSOR_EVENT_THING, thing);
    e.data[SENSOR_EVENT_TIMER_FACTOR] = (uint8_t)factor;
    timeline_add_event(&e);
    sensor_set_flag(sensor, SENSOR_BUSY, true);
}

void sensor_start_timers(void)
{
    for (int map = 0; map < dungeon_map_count(); map++)
        for (int x = 0; x < dungeon_map_width(map); x++)
            for (int y = 0; y < dungeon_map_height(map); y++)
                for (uint16_t t = dungeon_square_first_thing(map, x, y); thing_type(t) <= THING_SENSOR;
                     t = dungeon_next_thing(t)) {
                    if (thing_type(t) != THING_SENSOR)
                        continue;
                    if (!is_timer(sensor_type(t)))
                        continue;
                    if (sensor_flag(t, SENSOR_STATE)) {
                        dun_set_current_map(map);
                        schedule_timer(t, t);
                    } else {
                        sensor_set_flag(t, SENSOR_BUSY, false);
                    }
                }
}

/* ---- Square events ---- */

static void text_set_shown(uint16_t text, int effect)
{
    textrec_set_visible(text, effect == EFFECT_TOGGLE ? !textrec_is_visible(text) : effect == EFFECT_SET);
}

/* Text subtypes of the square events: generators and sounds. */
#define TEXT_GENERATOR_SLOW     0x13
#define TEXT_GENERATOR_ON_ENTRY 0x15
#define TEXT_GENERATOR_FAST     0x16
#define TEXT_SOUND              0x17
/* The ornament value of a ladder that goes up: the linked twin is on the level above. */
#define ORNAMENT_KEY_UP         0x11

/* A wall text of subtype 7 shows the same state as its twin on the level above or below. */
static void text_update_linked(const TimelineEvent *e, uint16_t text)
{
    bool visible = textrec_is_visible(text);
    int delta =
        gdat_entry_index(gdat_current(), CAT_WALL_ORNAMENT, textrec_value(text), GDAT_TYPE_VALUE, ORNAMENT_KEY_UP) ? -1
                                                                                                                   : 1;
    int16_t x = e->data[EVENT_X];
    int16_t y = e->data[EVENT_Y];
    int map = dungeon_location_on_map(g_game.party_map, delta, &x, &y);
    if (map < 0)
        return;
    int saved = g_party.cur_map;
    dun_set_current_map(map);
    for (uint16_t t = first_thing(x, y); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_TEXT || thing_cell(t) != e->data[EVENT_CELL])
            continue;
        if (textrec_kind(t) == TEXT_KIND_ORNAMENT && textrec_subtype(t) == SUBTYPE_LADDER_HIDDEN)
            textrec_set_visible(t, visible);
    }
    dun_set_current_map(saved);
}

/* A text with a sound plays it on a set event. */
static void text_play_sound(uint16_t text, int x, int y)
{
    /* sound (CAT_DUNGEON, map + 1, entry): a sound of a map */
    sndreq_request(
        (SoundCue){CAT_DUNGEON, g_party.cur_map + 1, textrec_value(text), SOUND_PRIORITY_MAP_SOUND, SOUND_VOLUME_LOUD},
        x, y, 1);
}

/* A counter sensor reaches zero (or leaves it): true when its count is zero or below. */
static bool counter_empty(uint16_t sensor)
{
    return !sensor_data(sensor) || (sensor_data(sensor) & SENSOR_DATA_SIGN);
}

/* Object transfer sensors: the charge change of a moved object (sensor flags bits 3-4). */
#define TRANSFER_CHARGES_FILL   0
#define TRANSFER_CHARGES_EMPTY  1
#define TRANSFER_CHARGES_RANDOM 2
/* The charge changes of item_add_charges that empty and fill the charges of an object. */
#define CHARGES_EMPTY_AMOUNT    (-15)
#define CHARGES_FULL_AMOUNT     CHARGES_MASK
/* Sensor flags: bones move like other objects; the moved object goes into the possessions of a static creature there. */
#define SENSOR_KEEP_BONES       SENSOR_STATE
/* Sensor data: item index 0x1ff, any object; the item class set of SENSOR_TRANSFER_CLASS. */
#define SENSOR_ANY_ITEM         0x1ff
#define ITEM_CLASS_SET_MASK     0x0ff
/* The item class text of SENSOR_TRANSFER_CLASS: the first GDAT entry; the sensor delay bits step by 3 entries. */
#define ITEM_CLASS_ENTRY_FIRST  0x10
#define ITEM_CLASS_ENTRY_STEP   0x03
static bool is_object(uint16_t thing)
{
    return thing_type(thing) >= THING_WEAPON && thing_type(thing) <= THING_MISC;
}

/* A square of the current map and a cell on it. */
typedef struct {
    int x, y, cell;
} SquareCell;

/* Moves thing (from the square from, NULL: a new thing; from_creature: from the possessions of a creature there) to
   the cell of the square to for a transfer sensor, with the charge change of the sensor; on a floor the cell is
   random. */
static void transfer_object(uint16_t thing, uint16_t sensor, const SquareCell *from, SquareCell to, bool from_creature)
{
    if (item_gdat_category(thing) != CAT_MISC || item_gdat_index(thing) != ITEM_BONES) {
        int charges = 0; /* fix: mode 3 adds an uninitialized amount in the original */
        switch (sensor_effect(sensor)) {
        case TRANSFER_CHARGES_FILL:
            charges = item_max_charges(thing);
            break;
        case TRANSFER_CHARGES_EMPTY:
            charges = CHARGES_EMPTY_AMOUNT;
            break;
        case TRANSFER_CHARGES_RANDOM:
            charges = rand_below((uint16_t)(item_max_charges(thing) + 1));
            break;
        }
        item_add_charges(thing, charges);
    }
    if (square_type(*current_square(to.x, to.y)) != SQUARE_WALL)
        to.cell = rand_below(CELL_COUNT);
    if (from)
        move_thing(thing, from->x, from->y, -1, from_creature ? -1 : 0);
    move_thing(thing_with_cell(thing, to.cell), -1, sensor_flag(sensor, SENSOR_KEEP_BONES) ? -1 : 0, to.x, to.y);
}

/* An object transfer of a Skullkeep machine: the square event and the sensor, and how the sensor picks the objects. */
typedef struct {
    const TimelineEvent *e;
    uint16_t sensor;
    bool any_cell;   /* all cells, not only the event cell */
    bool use_filter; /* the item class of the sensor data */
    bool reverse;    /* from the sensor target to the event square */
    bool first_only; /* one object only */
} ObjectTransfer;

/* A Skullkeep machine: the objects of the event square (reverse: of the sensor target) that the sensor wants move
   to the sensor target (reverse: to the event square), also from the creatures there. first: the first thing to
   look at, THING_NONE for the square; possessions: the list of the creature that holds the things. */
static void transfer_objects(ObjectTransfer tr, uint16_t first, uint16_t *possessions)
{
    const TimelineEvent *e = tr.e;
    uint16_t sensor = tr.sensor;
    uint16_t data = sensor_data(sensor);
    bool keep_bones = sensor_flag(sensor, SENSOR_KEEP_BONES);
    SquareCell event_square = {e->data[EVENT_X], e->data[EVENT_Y], e->data[EVENT_CELL]};
    SquareCell target = {sensor_target_x(sensor), sensor_target_y(sensor), sensor_target_cell(sensor)};
    SquareCell from = tr.reverse ? target : event_square;
    SquareCell to = tr.reverse ? event_square : target;
    int effect = e->data[EVENT_EFFECT];
    if (!effect_activates(sensor, effect))
        return;
    if (first == THING_NONE) {
        first = first_thing(from.x, from.y);
        if (square_type(*current_square(from.x, from.y)) != SQUARE_WALL)
            tr.any_cell = true;
    }
    int count = 0;
    for (uint16_t t = first; t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        if (is_object(t) && (tr.any_cell || thing_cell(t) == from.cell))
            count++;
    uint8_t bits[ITEM_CLASS_SET_SIZE];
    if (tr.use_filter)
        money_parse_item_class_set(data & ITEM_CLASS_SET_MASK,
                                   sensor_delay(sensor) * ITEM_CLASS_ENTRY_STEP + ITEM_CLASS_ENTRY_FIRST, bits, 0);
    for (uint16_t t = first, next; t != THING_ENDOFLIST; t = next) {
        next = dungeon_next_thing(t);
        if (count > 0 && is_object(t) && (tr.any_cell || thing_cell(t) == from.cell)) {
            count--;
            uint16_t item = item_get_index(t);
            bool match;
            if (tr.use_filter) {
                match = bits[item / BYTE_BIT_COUNT] >> (item % BYTE_BIT_COUNT) & 1;
            } else if (item == data) {
                if (!keep_bones && item_gdat_category(t) == CAT_MISC && item_gdat_index(t) == ITEM_BONES) {
                    /* bones: the champion comes back */
                    if (possessions)
                        dungeon_unlink_thing_from_list(t, possessions);
                    else
                        dungeon_unlink_thing_from_square(t, g_party.cur_map, from.x, from.y);
                    thing_set_word(t, THING_NEXT, THING_NONE);
                    champ_revive(item_add_charges(t, 0));
                    continue;
                }
                match = true;
            } else {
                match = data == SENSOR_ANY_ITEM;
            }
            if (!match)
                continue;
            transfer_object(t, sensor, &from, to, possessions != NULL);
            if (tr.first_only)
                return;
        } else if (!tr.use_filter && thing_type(t) == THING_GROUP) {
            uint16_t *list = group_possessions(t);
            transfer_objects(tr, thing_word(t, GROUP_POSSESSIONS), list);
        }
    }
}

/* A sensor of kind SENSOR_CREATE_ITEM makes a new item of its data item index at its target. */
static void create_item(uint16_t sensor)
{
    uint16_t item = sensor_data(sensor);
    int type = item_index_to_thing_type(item);
    uint16_t thing = type < 0 ? THING_NONE : dungeon_unused_thing(type); /* fix: no record of type 0xffff */
    if (thing == THING_NONE)
        return;
    item_set_gdat_index(thing, item_index_gdat_index(item));
    if (sensor_effect(sensor) == SENSOR_KEEP_ITEM)
        item_set_do_not_discard(thing, 1);
    SquareCell target = {sensor_target_x(sensor), sensor_target_y(sensor), sensor_target_cell(sensor)};
    transfer_object(thing, sensor, NULL, target, false);
}

/* SENSOR_COUNTDOWN: a data value up to 400 counts down, a larger one is a chance of data - 400 in 100. */
#define COUNTDOWN_MAX 400
#define CHANCE_END    500

/* A sensor turns the group on its target square (event effect EFFECT_SET, or EFFECT_CLEAR with SENSOR_INVERTED). */
static void set_creatures_facing(const TimelineEvent *e, uint16_t sensor)
{
    /* an inverted sensor turns the creatures on EFFECT_CLEAR instead of EFFECT_SET */
    if (e->data[EVENT_EFFECT] != (sensor_flag(sensor, SENSOR_INVERTED) ? EFFECT_CLEAR : EFFECT_SET))
        return;
    uint16_t g = creature_get_group_at(sensor_target_x(sensor), sensor_target_y(sensor));
    if (g != THING_NONE)
        creature_turn_group(g, 1, sensor_turn_dir(sensor));
}

/* The sensor types that react the same way to a square event on a wall and on a floor; returns 0 for another type. */
static int shared_event(TimelineEvent *e, uint16_t t, int wall)
{
    int kind = sensor_type(t);
    switch (kind) {
    case SENSOR_RELAY:
    case SENSOR_RELAY_SCALED:
        relay_delayed(e, t, kind == SENSOR_RELAY_SCALED);
        return 1;
    case SENSOR_FORWARD:
        forward_effect(e, t, sensor_data(t));
        return 1;
    case SENSOR_ANIMATION:
        toggle_animation(e, t, wall);
        return 1;
    case SENSOR_ACTIVATION:
        run_activation(e, t, wall);
        return 1;
    case SENSOR_TRANSFER:
        transfer_objects((ObjectTransfer){.e = e, .sensor = t, .any_cell = !wall}, t, NULL);
        return 1;
    case SENSOR_TRANSFER_CLASS:
        transfer_objects((ObjectTransfer){.e = e, .sensor = t, .any_cell = !wall, .use_filter = true}, t, NULL);
        return 1;
    case SENSOR_TRANSFER_BACK:
        transfer_objects((ObjectTransfer){.e = e, .sensor = t, .reverse = true}, THING_NONE, NULL);
        return 1;
    case SENSOR_TRANSFER_ONE:
        transfer_objects((ObjectTransfer){.e = e, .sensor = t, .first_only = true}, t, NULL);
        return 1;
    case SENSOR_TRANSFER_BACK_ONE:
        transfer_objects((ObjectTransfer){.e = e, .sensor = t, .reverse = true, .first_only = true}, THING_NONE, NULL);
        return 1;
    case SENSOR_TURN_CREATURES:
        set_creatures_facing(e, t);
        return 1;
    case SENSOR_SET_GLOBAL:
        set_global_flag(e, t);
        return 1;
    case SENSOR_TEST_GLOBAL:
        test_global_flag(e, t);
        return 1;
    }
    return 0;
}

/* A relay that stays busy for data ticks. */
static void busy_relay(uint16_t t, int effect)
{
    if (sensor_flag(t, SENSOR_BUSY))
        return;
    TimelineEvent ev = {.time_map = current_map_time(sensor_data(t) + g_game.game_time), .type = EVENT_SENSOR_READY};
    put_le16(ev.data + SENSOR_EVENT_THING, t);
    timeline_add_event(&ev);
    sensor_set_flag(t, SENSOR_BUSY, true);
    if (effect_activates(t, effect))
        sensor_trigger_target(t, passed_effect(t, effect), 0);
}

/* A creature generator creates a group of its data creature type at its target. */
static void generate_creature(uint16_t t)
{
    int tx = sensor_target_x(t);
    int ty = sensor_target_y(t);
    int dir = sensor_flag(t, SENSOR_STATE) ? rand_2bits() : sensor_effect(t);
    uint16_t g = creature_create_group(sensor_data(t), GROUP_COUNT_FULL_HEALTH, dir, tx, ty);
    if (sensor_flag(t, SENSOR_INVERTED) && g != THING_NONE)
        thing_set_word(g, GROUP_ITEM_CLASS, sensor_delay(t));
    if (sensor_flag(t, SENSOR_AUDIBLE))
        sndreq_request(
            (SoundCue){CAT_CREATURE_APPEARS, 0, SOUND_ENTRY_TELEPORT, SOUND_PRIORITY_MOVE, SOUND_VOLUME_NORMAL}, tx, ty,
            1);
}

/* A counter: clear counts up, set counts down. */
static void counter_event(uint16_t t, int effect)
{
    unsigned data = sensor_data(t);
    bool before = counter_empty(t);
    if (effect == EFFECT_CLEAR)
        sensor_set_data(t, data + 1);
    else if (effect == EFFECT_SET && (!sensor_flag(t, SENSOR_STATE) || data))
        sensor_set_data(t, data - 1);
    bool after = counter_empty(t);
    if (after == before)
        return;
    if (sensor_effect(t) == EFFECT_HOLD)
        sensor_trigger_target(t, after == sensor_flag(t, SENSOR_INVERTED), 0);
    else if (after)
        sensor_trigger_target(t, sensor_effect(t), 0);
}

/* A countdown, or a random chance for the values above 400. */
static void countdown_event(uint16_t t, int effect)
{
    unsigned data = sensor_data(t);
    if (data && data <= COUNTDOWN_MAX) {
        sensor_set_data(t, data - 1);
        sensor_trigger_target(t, effect, 0);
    } else if (data > COUNTDOWN_MAX && data < CHANCE_END) {
        int hit = rand_below(PERCENT) < (unsigned)(data - COUNTDOWN_MAX);
        if (sensor_effect(t) == EFFECT_HOLD)
            sensor_trigger_target(t, !hit, 0);
        else if (hit)
            sensor_trigger_target(t, effect, 0);
    }
}

/* Passes the event to a square of another map. */
static void relay_to_map(const TimelineEvent *e, uint16_t t)
{
    int map = sensor_data_map(t);
    int tx = sensor_target_x(t);
    int ty = sensor_target_y(t);
    if (map >= dungeon_map_count())
        return; /* fix: the original reads a map outside the dungeon */
    TimelineEvent ev = {.time_map = timeline_time_map(g_game.game_time & EVENT_TIME_MASK, map),
                        .type = EVENT_SQUARE,
                        .priority = e->priority};
    ev.data[EVENT_X] = (uint8_t)tx;
    ev.data[EVENT_Y] = (uint8_t)ty;
    ev.data[EVENT_CELL] = square_type(dungeon_square(map, tx, ty)) == SQUARE_WALL ? sensor_data_cell(t) : 0;
    ev.data[EVENT_EFFECT] = e->data[EVENT_EFFECT];
    timeline_add_event(&ev);
}

/* Sets bit 13 of the door at the target. */
static void switch_door(uint16_t t, int effect)
{
    uint16_t door = first_thing(sensor_target_x(t), sensor_target_y(t));
    if (effect == EFFECT_SET)
        door_set_flag(door, DOOR_SWITCH_ON, true);
    else if (effect == EFFECT_CLEAR)
        door_set_flag(door, DOOR_SWITCH_ON, false);
    else if (effect == EFFECT_TOGGLE)
        door_set_flag(door, DOOR_SWITCH_ON, !door_flag(door, DOOR_SWITCH_ON));
}

/* A wall sensor reacts to a square event on its cell. */
static void wall_sensor_event(TimelineEvent *e, uint16_t t, int effect)
{
    if (shared_event(e, t, 1))
        return;
    unsigned data = sensor_data(t);
    switch (sensor_type(t)) {
    case SENSOR_BUSY_RELAY:
        busy_relay(t, effect);
        break;
    case SENSOR_GENERATOR:
        if (effect == EFFECT_SET)
            generate_creature(t);
        break;
    case SENSOR_COUNTER:
        counter_event(t, effect);
        break;
    case SENSOR_COUNTDOWN:
        countdown_event(t, effect);
        break;
    case SENSOR_TIMER:
    case SENSOR_TIMER_8:
    case SENSOR_TIMER_16:
    case SENSOR_TIMER_32:
    case SENSOR_TIMER_64:
    case SENSOR_TIMER_128:
        sensor_set_flag(t, SENSOR_STATE, apply_effect(effect, sensor_flag(t, SENSOR_STATE)) & 1);
        if (!sensor_flag(t, SENSOR_BUSY) && sensor_flag(t, SENSOR_STATE))
            schedule_timer(t, t);
        break;
    case LAUNCH_ITEM:
    case LAUNCH_SPELL:
    case LAUNCH_ITEMS:
    case LAUNCH_SPELLS:
    case LAUNCH_OBJECT:
    case LAUNCH_OBJECTS:
        projectile_launch_missile(t, e);
        break;
    case SENSOR_MAP_RELAY:
        relay_to_map(e, t);
        break;
    case SENSOR_GAME_END:
        g_hooks.complete_game();
        break;
    case SENSOR_FRAME_STEP: { /* an ornament that steps its animation frame */
        unsigned frame = (uint16_t)(effect == EFFECT_SET ? data + 1 : data - 1);
        sensor_set_data(t, frame % (uint16_t)ornament_cycle(t, 1));
        break;
    }
    case SENSOR_WALL_STATE:
        sensor_set_flag(t, SENSOR_STATE, apply_effect(effect, sensor_flag(t, SENSOR_STATE)) & 1);
        break;
    case SENSOR_CREATE_ITEM:
        if (effect_activates(t, effect))
            create_item(t);
        break;
    case SENSOR_DOOR_SWITCH:
        switch_door(t, effect);
        break;
    }
}

/* A square event on a wall: the texts and the sensors on the event cell react. */
static void wall_square_event(TimelineEvent *e)
{
    int x = e->data[EVENT_X];
    int y = e->data[EVENT_Y];
    int effect = e->data[EVENT_EFFECT];
    for (uint16_t t = first_thing(x, y); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR;
         t = dungeon_next_thing(t)) {
        if (thing_cell(t) != e->data[EVENT_CELL])
            continue;
        if (thing_type(t) == THING_TEXT) {
            if (textrec_kind(t) > TEXT_KIND_ORNAMENT)
                continue;
            int sub = textrec_subtype(t);
            if (sub == SUBTYPE_TOGGLE || sub == SUBTYPE_LADDER_HIDDEN) {
                text_set_shown(t, effect);
                if (sub == SUBTYPE_LADDER_HIDDEN)
                    text_update_linked(e, t);
            } else if (sub == TEXT_SOUND && effect == EFFECT_SET) {
                text_play_sound(t, x, y);
            }
            continue;
        }
        if (thing_type(t) == THING_SENSOR)
            wall_sensor_event(e, t, effect);
    }
}

/* A generator text on a set event queues its next creature after a random delay. */
static void text_schedule_spawn(uint16_t t, int sub, bool was_visible, int x, int y)
{
    unsigned base = sub == TEXT_GENERATOR_SLOW ? GENERATOR_SLOW_DELAY : GENERATOR_FAST_DELAY;
    if (was_visible)
        base *= sub == TEXT_GENERATOR_SLOW ? GENERATOR_SLOW_SHOWN_FACTOR : 2;
    unsigned r = (uint16_t)rand_next();
    TimelineEvent ev = {.time_map = current_map_time(base + ((base * 2 - 1) & r) + g_game.game_time),
                        .type = EVENT_SPAWN_CREATURE};
    ev.data[EVENT_X] = (uint8_t)x;
    ev.data[EVENT_Y] = (uint8_t)y;
    ev.data[SPAWN_EVENT_CREATURE] = textrec_value(t);
    timeline_add_event(&ev);
}

/* A floor text reacts to a square event: it shows or hides (a visible message prints), spawns, or plays its sound. */
static void floor_text_event(uint16_t t, int effect, int x, int y)
{
    if (textrec_kind(t) > TEXT_KIND_ORNAMENT)
        return;
    int sub = textrec_subtype(t);
    bool was_visible = textrec_is_visible(t);
    if (sub == SUBTYPE_TOGGLE) {
        text_set_shown(t, effect);
        if (textrec_kind(t) == TEXT_KIND_TEXT && !was_visible && textrec_is_visible(t) &&
            dun_on_party_square((Location){x, y})) {
            char text[TEXT_SIZE];
            g_hooks.print_message(UI_WHITE, gtext_decode_dungeon(text, sizeof text, t, true, false));
        }
    } else if ((sub == TEXT_GENERATOR_SLOW || sub == TEXT_GENERATOR_FAST) && effect == EFFECT_SET) {
        text_schedule_spawn(t, sub, was_visible, x, y);
    } else if (sub == TEXT_SOUND && effect == EFFECT_SET) {
        text_play_sound(t, x, y);
    }
}

/* A sensor reaches the groups of the creature type of its data in the rectangle around (x, y) that its target spans:
   SENSOR_VANISH makes them vanish (power VANISH_POWER), SENSOR_ALERT sets mood bit power (cleared when effect is set). */
static void affect_creatures_in_area(uint16_t sensor, int16_t x, int16_t y, int effect)
{
    int power = sensor_area_power(sensor);
    int creature_type = sensor_area_creature(sensor);
    int tx = sensor_target_x(sensor);
    int ty = sensor_target_y(sensor);
    int kind = sensor_type(sensor);
    int rx = abs((int16_t)(x - tx));
    int ry = abs((int16_t)(y - ty));
    int left = x - rx;
    int top = y - ry;
    for (int row = ry * 2; row >= 0; row--)
        for (int col = rx * 2; col >= 0; col--) {
            int16_t sx = (int16_t)(left + col);
            int16_t sy = (int16_t)(top + row);
            if (sx < 0 || sx >= dungeon_map_width(g_party.cur_map) || sy < 0 ||
                sy >= dungeon_map_height(g_party.cur_map))
                continue;
            uint16_t g = creature_get_group_at(sx, sy);
            if (g == THING_NONE || thing_word(g, GROUP_ITEM_CLASS) != (uint16_t)creature_type)
                continue;
            if (kind == SENSOR_VANISH) {
                if ((uint16_t)power > 1) {
                    if ((uint16_t)power != VANISH_POWER)
                        return;
                    creature_set_next_action(g, sx, sy, CREATURE_ACTION_VANISH, 1);
                }
            } else if (kind == SENSOR_ALERT) {
                creature_alert(g, (Location){sx, sy}, power | (effect ? ALERT_CLEAR : 0), ALERT_ALWAYS, 0);
            }
        }
}

/* A square event on a floor: the texts and the sensors of the square react. */
static void floor_square_event(TimelineEvent *e)
{
    int x = e->data[EVENT_X];
    int y = e->data[EVENT_Y];
    int effect = e->data[EVENT_EFFECT];
    for (uint16_t t = first_thing(x, y); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR;
         t = dungeon_next_thing(t)) {
        if (thing_type(t) == THING_TEXT) {
            floor_text_event(t, effect, x, y);
            continue;
        }
        if (thing_type(t) != THING_SENSOR || shared_event(e, t, 0))
            continue;
        int kind = sensor_type(t);
        switch (kind) {
        case SENSOR_TELEPORTER_LINK:
            sensor_set_flag(t, SENSOR_BUSY, apply_effect(effect, sensor_flag(t, SENSOR_BUSY)) & 1);
            break;
        case SENSOR_VANISH:
        case SENSOR_ALERT:
            affect_creatures_in_area(t, x, y, effect);
            break;
        case SENSOR_PARTY_TELEPORT: { /* moves the party to its target (or to this square) */
            int tx = x;
            int ty = y;
            if (sensor_flag(t, SENSOR_STATE)) {
                tx = sensor_target_x(t);
                ty = sensor_target_y(t);
            }
            int dir = sensor_flag(t, SENSOR_INVERTED) ? sensor_effect(t) : dir_turn(sensor_effect(t), g_game.party_dir);
            move_party_to((Position){g_party.cur_map, tx, ty}, dir);
            break;
        }
        case SENSOR_CREATURE_ANIMATION:
            creature_update_anim_at(x, y, effect == EFFECT_SET);
            break;
        }
    }
}

/* A square event opens, closes, or toggles a pit or a teleporter (on_bit); an opened one moves the square contents. */
static void pit_or_teleporter_event(TimelineEvent *e, uint8_t on_bit)
{
    int x = e->data[EVENT_X];
    int y = e->data[EVENT_Y];
    uint8_t *square = current_square(x, y);
    int effect = e->data[EVENT_EFFECT];
    if (effect == EFFECT_TOGGLE)
        effect = (*square & on_bit) != 0;
    if (effect == EFFECT_SET) {
        *square |= on_bit;
        move_square_contents_through((Location){x, y});
    } else {
        *square &= (uint8_t)~on_bit;
    }
}

/* A square event opens, closes, or toggles a door that is not broken: it starts or turns the door animation. */
static void doorway_event(TimelineEvent *e)
{
    uint8_t state = *current_square(e->data[EVENT_X], e->data[EVENT_Y]) & SQUARE_DOOR_STATE;
    if (state == DOOR_STATE_BROKEN)
        return;
    uint16_t door = first_thing(e->data[EVENT_X], e->data[EVENT_Y]);
    int effect = e->data[EVENT_EFFECT];
    if (door_flag(door, DOOR_MOVING)) {
        int opening = apply_effect(effect, door_flag(door, DOOR_OPENING)) & 1;
        door_set_flag(door, DOOR_OPENING, opening);
        if (!opening)
            door_set_flag(door, DOOR_MARKED, false);
        return;
    }
    int move = 0;
    if (state == DOOR_STATE_OPEN) {
        if (effect == EFFECT_CLEAR || effect == EFFECT_TOGGLE) {
            door_set_flag(door, DOOR_OPENING, false);
            move = 1;
        }
    } else if (state == DOOR_STATE_CLOSED) {
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
        if (!door_flag(door, DOOR_OPENING))
            door_set_flag(door, DOOR_MARKED, false);
        e->type = EVENT_DOOR_ANIMATION;
        timeline_add_event(e);
    }
}

/* A square event opens or closes a fake wall; a party or a creature on it delays the close by one tick. */
static void fake_wall_event(TimelineEvent *e)
{
    int x = e->data[EVENT_X];
    int y = e->data[EVENT_Y];
    uint8_t *square = current_square(x, y);
    int effect = e->data[EVENT_EFFECT];
    if (effect == EFFECT_TOGGLE)
        effect = (*square & SQUARE_FAKE_WALL_OPEN) != 0;
    if (effect == EFFECT_CLEAR) {
        int blocked = dun_on_party_square((Location){x, y});
        uint16_t g = creature_group_at(g_party.cur_map, x, y);
        if (g != THING_NONE && !(creature_group_info(g)->flags & CREATURE_NOT_DOOR_HIT))
            blocked = 1;
        if (blocked) {
            e->time_map++;
            timeline_add_event(e);
        } else {
            *square &= (uint8_t)~SQUARE_FAKE_WALL_OPEN;
        }
    } else {
        *square |= SQUARE_FAKE_WALL_OPEN;
    }
    if (g_party.cur_map == g_game.party_map)
        g_party.redraw = REDRAW_ALL_VIEW;
}

void sensor_event_square(TimelineEvent *e)
{
    switch (square_type(dungeon_square(g_party.cur_map, e->data[EVENT_X], e->data[EVENT_Y]))) {
    case SQUARE_WALL:
        wall_square_event(e);
        break;
    case SQUARE_FLOOR:
        floor_square_event(e);
        break;
    case SQUARE_PIT:
        pit_or_teleporter_event(e, SQUARE_PIT_OPEN);
        floor_square_event(e);
        break;
    case SQUARE_DOOR:
        doorway_event(e);
        break;
    case SQUARE_TELEPORTER:
        if (!teleporter_is_fixed(first_thing(e->data[EVENT_X], e->data[EVENT_Y])))
            pit_or_teleporter_event(e, SQUARE_TELEPORTER_ON);
        floor_square_event(e);
        break;
    case SQUARE_FAKE_WALL:
        fake_wall_event(e);
        break;
    }
}

void sensor_event_door_destruction(TimelineEvent *e)
{
    uint8_t *square = current_square(e->data[EVENT_X], e->data[EVENT_Y]);
    *square = (uint8_t)((*square & ~SQUARE_DOOR_STATE) | DOOR_STATE_BROKEN);
    if (g_party.cur_map == g_game.party_map)
        g_party.redraw = REDRAW_ALL_VIEW;
}

/* The closing door at (x, y) hits the party and the creature under it; *state follows a creature that stops it.
   True when it hits. */
static bool closing_door_hits(uint16_t door, int x, int y, uint8_t *square, int *state)
{
    bool vertical = door_flag(door, DOOR_VERTICAL);
    bool hit = false;
    int16_t damage = (int16_t)sensor_door_value(door, DOOR_KEY_DAMAGE);
    if (dun_on_party_square((Location){x, y}) && *state) {
        hit = true;
        if (g_game.champion_count) {
            *square &= (uint8_t)~SQUARE_DOOR_STATE;
            /* the walk-through-walls cheat: the door opens and does not hurt the party */
            int hurt =
                game_has_cheat(CHEAT_WALLS) ? 0 : champ_damage_party((uint16_t)damage, WOUND_HEAD, ATTACK_TYPE_SELF);
            for (int i = 0; i < CHAMPION_MAX; i++)
                if (hurt >> i & 1)
                    sndreq_request_with_fallback((SoundCue){CAT_CHAMPION, champion(i)->portrait, SOUND_ENTRY_PUSHED,
                                                            SOUND_PRIORITY_PUSHED, SOUND_VOLUME_LOUD},
                                                 SOUND_FALLBACK, x, y, 1);
        }
    }
    uint16_t g = creature_group_at(g_party.cur_map, x, y);
    if (g == THING_NONE)
        return hit;
    const CreatureInfo *info = creature_group_info(g);
    int height = vertical ? creature_door_size(info) : 1;
    if (info->flags & CREATURE_NOT_DOOR_HIT || *state < height)
        return hit;
    if (info->door_flags & CREATURE_RESISTS_NON_FIRE) {
        damage >>= 1;
        if (damage < 1)
            damage = 1;
    }
    creature_alert(g, (Location){x, y}, ALERT_STARTLED, ALERT_ALWAYS, (uint16_t)damage);
    *state = *state ? *state - 1 : 0;
    *square = (uint8_t)((*square & ~SQUARE_DOOR_STATE) | *state);
    sndreq_request_with_fallback(
        (SoundCue){CAT_CREATURE, item_gdat_index(g), SOUND_HURT, SOUND_PRIORITY_CREATURE, SOUND_VOLUME_NORMAL},
        SOUND_FALLBACK, x, y, 1);
    sndreq_request_with_fallback(
        (SoundCue){CAT_MISC, SOUND_FALLBACK, SOUND_ENTRY_DROP, SOUND_PRIORITY_CREATURE, SOUND_VOLUME_NORMAL},
        SOUND_FALLBACK, x, y, 1);
    return true;
}

void sensor_event_door_animation(TimelineEvent *e)
{
    int x = e->data[EVENT_X];
    int y = e->data[EVENT_Y];
    uint8_t *square = current_square(x, y);
    int state = *square & SQUARE_DOOR_STATE;
    if (state == DOOR_STATE_BROKEN)
        return;
    if (g_party.cur_map == g_game.party_map)
        g_party.redraw = REDRAW_ALL_VIEW;
    uint16_t door = first_thing(x, y);
    if (!door_flag(door, DOOR_MOVING))
        return;
    bool opening = door_flag(door, DOOR_OPENING);
    e->time_map++;
    int again = 0;
    if (!opening) {
        if (state == DOOR_STATE_CLOSED) {
            door_set_flag(door, DOOR_MOVING, false);
            return;
        }
        if (closing_door_hits(door, x, y, square, &state)) {
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
        if (opening ? state != DOOR_STATE_OPEN : state != DOOR_STATE_CLOSED)
            again = 1;
        sndreq_request_with_fallback(
            (SoundCue){CAT_DOOR, dun_get_door_type(door),
                       !again && state == DOOR_STATE_CLOSED ? SOUND_ENTRY_DOOR_SHUT : SOUND_ENTRY_DOOR_MOVE,
                       SOUND_PRIORITY_DOOR, SOUND_VOLUME_NORMAL},
            SOUND_FALLBACK, x, y, 1);
    }
    if (again) {
        door_set_flag(door, DOOR_MOVING, true);
        timeline_add_event(e);
        return;
    }
    door_set_flag(door, DOOR_MOVING, false);
}

/* ---- Sensor timeline events ---- */

/* The values of the index bits 8-9 of a record. */
#define RECORD_INDEX_HIGH_COUNT 4

/* The step to the square in front of a wall of the ambient sound event: the original indexes g_dir_dx and g_dir_dy
   with the high byte of the sensor thing (cell in bits 6-7, index bits 8-9 of the record in bits 0-1), so it reads
   the data after those tables in SKULL.EXE; these are the values it reads. */
static const int16_t ambient_wall_dx[CELL_COUNT][RECORD_INDEX_HIGH_COUNT] = {
    {2052, 4, 0,   1032},
    {0,    0, 0,   0   },
    {0,    0, 0,   0   },
    {450,  0, 255, 0   }
};
static const int16_t ambient_wall_dy[CELL_COUNT][RECORD_INDEX_HIGH_COUNT] = {
    {0, 0, 25675, 60},
    {0, 0, 0,     0 },
    {0, 0, 0,     0 },
    {0, 0, 0,     0 }
};

/* A value of an ambient wall table for the sensor thing: by its cell and the index bits 8-9 of its record. */
static int16_t ambient_wall_step(const int16_t table[CELL_COUNT][RECORD_INDEX_HIGH_COUNT], uint16_t thing)
{
    return table[thing_cell(thing)][thing_index(thing) >> BYTE_BIT_COUNT];
}

/* The repeating sound of an animated ornament plays while it runs on the party map. */
static void ambient_sound_event(TimelineEvent *e, uint16_t thing)
{
    if (!sensor_flag(thing, SENSOR_BUSY) || (int8_t)timeline_event_map(e) != g_game.party_map) {
        sensor_set_data(thing, sensor_frame(thing));
        return;
    }
    bool wall = square_type(dun_get_square((Location){e->data[EVENT_X], e->data[EVENT_Y]})) == SQUARE_WALL;
    e->time_map += (uint16_t)ornament_cycle(thing, wall);
    timeline_add_event(e);
    int16_t x = e->data[EVENT_X];
    int16_t y = e->data[EVENT_Y];
    if (wall) { /* the sound comes from the square in front of the wall, unless the party stands there */
        int16_t fx = (int16_t)(x + ambient_wall_step(ambient_wall_dx, thing));
        int16_t fy = (int16_t)(y + ambient_wall_step(ambient_wall_dy, thing));
        if (g_party.cur_map != g_party.square_map || fx != g_party.square_x || fy != g_party.square_y) {
            x = fx;
            y = fy;
        }
    }
    ornament_sound(thing, wall, x, y, 1);
}

/* A timer sensor ticks: it triggers its target and runs again while it is on (a hold timer alternates its effect). */
static void timer_event(TimelineEvent *e, uint16_t thing)
{
    int running, effect;
    if (sensor_effect(thing) == EFFECT_HOLD) {
        bool phase = !(e->data[SENSOR_EVENT_TIMER_PHASE] & 1);
        e->data[SENSOR_EVENT_TIMER_PHASE] = (uint8_t)((e->data[SENSOR_EVENT_TIMER_PHASE] & ~1) | phase);
        running = phase | sensor_flag(thing, SENSOR_STATE);
        effect = !(e->data[SENSOR_EVENT_TIMER_PHASE] & 1);
        sensor_trigger_target(thing, effect, 0);
    } else {
        running = sensor_flag(thing, SENSOR_STATE);
        if (running)
            sensor_trigger_target(thing, sensor_effect(thing), 0);
    }
    if (!running) {
        sensor_set_flag(thing, SENSOR_BUSY, false);
    } else {
        e->time_map += e->data[SENSOR_EVENT_TIMER_FACTOR] * sensor_data(thing);
        timeline_add_event(e);
    }
}

void sensor_process_event(TimelineEvent *e)
{
    uint16_t thing = le16(e->data + SENSOR_EVENT_THING);
    switch (e->type) {
    case EVENT_SENSOR_ANIMATION_STEP: {
        e->time_map++;
        unsigned cycle = (uint16_t)ornament_cycle(thing, e->data[SENSOR_EVENT_WALL]);
        unsigned frame = (sensor_data(thing) + 1) & SENSOR_DATA_MASK;
        sensor_set_data(thing, frame);
        if (frame % cycle == 0)
            sensor_set_flag(thing, SENSOR_BUSY, false);
        else
            timeline_add_event(e);
        break;
    }
    case EVENT_SENSOR_TIMER:
        timer_event(e, thing);
        break;
    case EVENT_SENSOR_ANIMATION_END:
        thing = le16(e->data + SENSOR_EVENT_SQUARE_THING);
        if (!sensor_flag(thing, SENSOR_STATE)) {
            if (g_party.cur_map == g_game.party_map)
                g_party.redraw |= REDRAW_VIEW;
            sensor_set_flag(thing, SENSOR_BUSY, false);
        }
        break;
    case EVENT_SENSOR_AMBIENT_SOUND:
        ambient_sound_event(e, le16(e->data + SENSOR_EVENT_SQUARE_THING));
        break;
    case EVENT_SENSOR_IDLE:
    case EVENT_SENSOR_READY:
        sensor_set_flag(thing, SENSOR_BUSY, false);
        break;
    case EVENT_DOOR_BUTTON_RELEASE:
        door_set_flag(thing, DOOR_BUTTON_PRESSED, false);
        break;
    case EVENT_SET_MARKER:
        textrec_set_visible(thing, true);
        break;
    }
}

void sensor_process_map_entry(bool entering)
{
    int saved = g_party.cur_map;
    dun_set_current_map(g_game.party_map);
    int map = g_party.cur_map;
    for (int x = 0; x < dungeon_map_width(map); x++)
        for (int y = 0; y < dungeon_map_height(map); y++) {
            bool wall = square_type(dungeon_square(map, x, y)) == SQUARE_WALL;
            for (uint16_t t = first_thing(x, y); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR;
                 t = dungeon_next_thing(t)) {
                if (thing_type(t) == THING_SENSOR) {
                    int kind = sensor_type(t);
                    if (kind == SENSOR_MAP_ENTRY) {
                        if (sensor_effect(t) == EFFECT_HOLD) {
                            sensor_trigger_target(t, sensor_flag(t, SENSOR_INVERTED) == entering, 0);
                        } else if ((!sensor_flag(t, SENSOR_INVERTED)) == entering) {
                            sensor_trigger_target(t, sensor_effect(t), 0);
                        }
                    } else if (kind == SENSOR_ANIMATION && entering && sensor_flag(t, SENSOR_BUSY)) {
                        start_ambient_sound(t, x, y, (uint16_t)ornament_cycle(t, wall), wall);
                    }
                } else if (thing_type(t) == THING_TEXT && entering) {
                    if (textrec_kind(t) == TEXT_KIND_ORNAMENT && textrec_subtype(t) == TEXT_GENERATOR_ON_ENTRY &&
                        !textrec_is_visible(t)) {
                        textrec_set_visible(t, true);
                        creature_create_group(textrec_value(t), GROUP_COUNT_FULL_HEALTH, rand_2bits(), x, y);
                    }
                }
            }
        }
    dun_set_current_map(saved);
}

/* ---- Wall clicks ---- */

/* Values of the wall ornament GDAT entries of the wall clicks. */
#define ORNAMENT_KEY_MARKER_DELAY 0x12
/* The water of a champion after a drink from a fountain. */
#define WATER_FULL                2048
/* The item index of an empty flask, and the GDAT index of a flask of water. */
#define ITEM_EMPTY_FLASK          0x194
#define FLASK_FULL_INDEX          0x00f
/* A wall sensor that does not react to the click. */
#define CLICK_IGNORED             (-1)

/* A GDAT value of the wall ornament of thing, 0 for none. */
static int ornament_value(uint16_t thing, int key)
{
    int ornament = item_gdat_index(thing);
    return ornament == GDAT_NO_INDEX
               ? 0
               : gdat_entry_index(gdat_current(), CAT_WALL_ORNAMENT, ornament, GDAT_TYPE_VALUE, key);
}

int sensor_wall_ornament_value(uint16_t first, uint16_t thing, int key)
{
    return dun_is_alcove_shown(first, thing) ? ornament_value(thing, key) : 0;
}

/* The object of the item index type on the cell of the square (x, y), or a new one; THING_NONE when none can be made. */
static uint16_t wall_item(int x, int y, int cell, int item)
{
    for (uint16_t t = dungeon_square_first_object(g_party.cur_map, x, y); t != THING_ENDOFLIST;
         t = dungeon_next_thing(t))
        if (item_get_index(t) == item && (cell == -1 || thing_cell(t) == cell))
            return t;
    uint16_t t = item_create_by_index(item);
    if (t == THING_NONE)
        return t;
    t = thing_with_cell(t, cell);
    dungeon_link_thing_to_square(t, g_party.cur_map, x, y);
    return t;
}

/* A click on a wall cell of (x, y) by the party or a creature. */
typedef struct {
    int x, y, cell;
    uint16_t hand;         /* the object in the hand, THING_NONE after it went to the wall */
    uint16_t give;         /* the object that goes into the hand */
    bool take, stop, done; /* the hand object goes to the wall, the click stops, the click ends */
    bool has_leader;
} WallClick;

/* The hand takes the item of an item holder on the clicked cell, with full charges; false when there is none. */
static bool wall_click_give(WallClick *c, int item)
{
    uint16_t object = wall_item(c->x, c->y, c->cell, item);
    if (object == THING_NONE)
        return false;
    item_add_charges(object, CHARGES_FULL_AMOUNT);
    move_thing(object, c->x, c->y, -1, 0);
    c->give = object;
    return true;
}

/* The hand object goes to the clicked cell. */
static void wall_click_put(WallClick *c)
{
    c->take = true;
    move_thing(thing_with_cell(c->hand, c->cell), -1, 0, c->x, c->y);
}

/* An empty hand takes the last object of an alcove cell; an object in the hand goes into the alcove. */
static void wall_click_alcove(WallClick *c)
{
    if (c->hand != THING_NONE) {
        wall_click_put(c);
        return;
    }
    uint16_t object = THING_NONE;
    for (uint16_t o = dungeon_square_first_object(g_party.cur_map, c->x, c->y); o != THING_ENDOFLIST;
         o = dungeon_next_thing(o))
        if (thing_cell(o) == c->cell)
            object = o;
    if (object != THING_NONE && thing_type(object) >= THING_WEAPON && thing_type(object) <= THING_MISC) {
        move_thing(object, c->x, c->y, -1, 0);
        c->give = object;
    }
}

/* A fountain: an empty hand gives water to the living champions, a refillable object or an empty flask fills. */
static void wall_click_fountain(WallClick *c)
{
    uint16_t hand = c->hand;
    if (hand == THING_NONE) {
        if (!c->has_leader)
            return;
        for (int i = 0; i < CHAMPION_MAX; i++)
            if (champion(i)->health)
                champion(i)->water = WATER_FULL;
        sndreq_request_with_fallback((SoundCue){CAT_CHAMPION, champion(g_game.leader)->portrait, SOUND_ENTRY_SWALLOW,
                                                SOUND_PRIORITY_SWALLOW, SOUND_VOLUME_NORMAL},
                                     SOUND_FALLBACK, g_game.party_x, g_game.party_y, 0);
    } else if (item_attr(hand, ATTR_FLAGS) & ITEM_FLAG_REFILLS) {
        item_add_charges(hand, CHARGES_FULL_AMOUNT);
        c->take = true;
        c->give = hand;
    } else if (item_get_index(hand) == ITEM_EMPTY_FLASK) {
        item_set_gdat_index(hand, FLASK_FULL_INDEX);
        c->take = true;
        c->give = hand;
    }
}

/* A button: it turns idle after its data ticks; a hold button sets and clears its target itself. Returns the skip of
   the trigger or CLICK_IGNORED; *extra gets the trigger delay of an inverted button. */
static int wall_click_button(WallClick *c, uint16_t t, int ornament, int *extra)
{
    if (c->hand != THING_NONE || sensor_flag(t, SENSOR_BUSY))
        return CLICK_IGNORED;
    unsigned data = sensor_data(t);
    /* fix: the original leaves 4 data bytes of the event uninitialized */
    TimelineEvent e = {.time_map = current_map_time(data + g_game.game_time + CLICK_EVENT_DELAY),
                       .type = EVENT_SENSOR_IDLE};
    put_le16(e.data + SENSOR_EVENT_THING, t);
    timeline_add_event(&e);
    sensor_set_flag(t, SENSOR_BUSY, true);
    if (sensor_flag(t, SENSOR_INVERTED))
        *extra = BUTTON_INVERTED_DELAY;
    if (sensor_effect(t) != EFFECT_HOLD)
        return 0;
    c->stop = true;
    if (sensor_flag(t, SENSOR_AUDIBLE))
        sndreq_request_with_fallback(
            (SoundCue){CAT_WALL_ORNAMENT, ornament, SOUND_ENTRY_ACTIVATE, SOUND_PRIORITY_CLICK, SOUND_VOLUME_NORMAL},
            SOUND_FALLBACK, g_game.party_x, g_game.party_y, 1);
    sensor_trigger_target(t, EFFECT_SET, *extra);
    sensor_trigger_target(t, EFFECT_CLEAR, (int16_t)(data + 1 + *extra));
    return CLICK_IGNORED;
}

/* An item holder sensor: when on, an empty hand takes its item; when off, the hand puts its item there. */
static int wall_click_item_holder(WallClick *c, uint16_t t, int ornament)
{
    int item = holder_item(ornament);
    if (sensor_flag(t, SENSOR_STATE)) {
        if (c->hand != THING_NONE) {
            c->done = true;
            return CLICK_IGNORED;
        }
        if (!wall_click_give(c, item))
            return CLICK_IGNORED;
    } else {
        if (c->hand == THING_NONE || item_get_index(c->hand) != item)
            return CLICK_IGNORED;
        wall_click_put(c);
    }
    c->hand = THING_NONE;
    g_party.redraw |= REDRAW_VIEW;
    c->done = true;
    return CLICK_IGNORED;
}

/* A counted item sensor takes the item of its ornament from the hand until its count is 0; then it turns busy. Returns
   the skip of the trigger or CLICK_IGNORED. */
static int wall_click_counted_item(WallClick *c, uint16_t t, int ornament)
{
    unsigned data = sensor_data(t);
    if (!data || item_get_index(c->hand) != holder_item(ornament))
        return CLICK_IGNORED;
    c->take = true;
    c->hand = THING_NONE;
    sensor_set_data(t, --data);
    if ((uint16_t)data)
        return 1;
    sensor_set_flag(t, SENSOR_BUSY, true);
    return 0;
}

/* A teleport button moves the party to the target of the sensor on the map of its data; returns the skip of the
   trigger or CLICK_IGNORED. */
static int wall_click_teleport(WallClick *c, uint16_t t)
{
    if (c->hand != THING_NONE || sensor_flag(t, SENSOR_STATE))
        return CLICK_IGNORED;
    int effect = sensor_effect(t);
    int dir = sensor_flag(t, SENSOR_INVERTED) ? effect : dir_turn(effect, g_game.party_dir);
    move_party_to((Position){sensor_data_map(t), sensor_target_x(t), sensor_target_y(t)}, dir);
    g_hooks.flush_input();
    return 1;
}

/* The skip of the trigger of a clicked wall sensor t on the clicked cell, or CLICK_IGNORED. */
static int wall_click_sensor_skip(WallClick *c, uint16_t t, int ornament, int *extra)
{
    int effect = sensor_effect(t);
    unsigned data = sensor_data(t);
    switch (sensor_type(t)) {
    case WALL_CLICK_ANY:
        return effect == EFFECT_HOLD ? CLICK_IGNORED : 0;
    case WALL_CLICK_EMPTY_HAND:
        return (c->hand == THING_NONE) != sensor_flag(t, SENSOR_INVERTED);
    case WALL_CLICK_CHARGED_ITEM:
        if (!item_add_charges(c->hand, 0))
            return 1;
        __attribute__((fallthrough));
    case WALL_CLICK_ITEM: {
        bool match = item_get_index(c->hand) == data;
        if (match && sensor_flag(t, SENSOR_STATE))
            thing_set_word(champ_take_from_leader_hand(), THING_NEXT, THING_NONE); /* frees it */
        return match == sensor_flag(t, SENSOR_INVERTED);
    }
    case WALL_CLICK_TOGGLE:
        if (c->hand != THING_NONE)
            return CLICK_IGNORED;
        sensor_set_flag(t, SENSOR_STATE, !sensor_flag(t, SENSOR_STATE));
        return sensor_flag(t, SENSOR_INVERTED) == sensor_flag(t, SENSOR_STATE);
    case WALL_CLICK_DOOR_BUTTON:
        if (!door_flag(first_thing(sensor_target_x(t), sensor_target_y(t)), DOOR_SWITCH_ON))
            return CLICK_IGNORED;
        return wall_click_button(c, t, ornament, extra);
    case WALL_CLICK_BUTTON:
    case WALL_CLICK_BUTTON_2:
        return wall_click_button(c, t, ornament, extra);
    case WALL_CLICK_PORTRAIT:
        if (!sensor_flag(t, SENSOR_STATE) || c->cell != dir_opposite(g_game.party_dir))
            return 1;
        recruit_start(g_game.party_x, g_game.party_y, g_game.party_dir, g_game.party_map);
        c->done = true;
        return 0;
    case WALL_ITEM_HOLDER:
        return wall_click_item_holder(c, t, ornament);
    case WALL_CLICK_COUNTED_ITEM:
        return wall_click_counted_item(c, t, ornament);
    case WALL_CLICK_TELEPORT:
        return wall_click_teleport(c, t);
    case WALL_CLICK_RESET:
        if (c->hand == THING_NONE)
            sensor_set_flag(t, SENSOR_BUSY, false);
        return 1;
    }
    return CLICK_IGNORED;
}

/* A clicked wall sensor triggers its target (a hold sensor: set when it does not skip, else clear). */
static void wall_click_sensor(WallClick *c, uint16_t t)
{
    int kind = sensor_type(t);
    int effect = sensor_effect(t);
    if (!c->has_leader && kind != WALL_CLICK_PORTRAIT)
        return;
    int ornament = wall_ornament(t);
    if (!kind)
        return;
    int extra = 0;
    int skip = wall_click_sensor_skip(c, t, ornament, &extra);
    if (skip == CLICK_IGNORED)
        return;
    if (effect == EFFECT_HOLD) {
        effect = skip != 0;
        skip = 0;
    }
    if (skip)
        return;
    c->stop = true;
    if (sensor_flag(t, SENSOR_AUDIBLE))
        sndreq_request_with_fallback(
            (SoundCue){CAT_WALL_ORNAMENT, ornament, SOUND_ENTRY_ACTIVATE, SOUND_PRIORITY_CLICK, SOUND_VOLUME_NORMAL},
            SOUND_FALLBACK, g_game.party_x, g_game.party_y, 1);
    sensor_trigger_target(t, effect, extra);
}

/* An item holder text: when visible, an empty hand takes its item (a second holder type also shows its marker
   later); when hidden, the hand puts its item there. */
static void wall_click_holder_text(WallClick *c, uint16_t t, int sub, int value, bool visible)
{
    int item = holder_item(value);
    if (visible) {
        if (c->hand != THING_NONE) {
            c->done = true;
            return;
        }
        if (!wall_click_give(c, item))
            return;
        c->hand = THING_NONE;
        if (sub == SUBTYPE_ITEM_HOLDER_MARKER) {
            /* fix: the original leaves 4 data bytes of the event uninitialized */
            uint16_t delay =
                gdat_entry_index(gdat_current(), CAT_WALL_ORNAMENT, value, GDAT_TYPE_VALUE, ORNAMENT_KEY_MARKER_DELAY);
            TimelineEvent e = {.time_map = current_map_time(g_game.game_time + delay + CLICK_EVENT_DELAY),
                               .type = EVENT_SET_MARKER};
            put_le16(e.data + SENSOR_EVENT_THING, t);
            timeline_add_event(&e);
        }
    } else if (sub != SUBTYPE_ITEM_HOLDER_MARKER) {
        if (c->hand == THING_NONE || item_get_index(c->hand) != item)
            return;
        wall_click_put(c);
        c->hand = THING_NONE;
    }
    g_party.redraw |= REDRAW_VIEW;
    c->done = true;
}

/* A clicked ornament text: an item holder, a ladder, or a switch. */
static void wall_click_text(WallClick *c, uint16_t t)
{
    if (textrec_kind(t) != TEXT_KIND_ORNAMENT || !c->has_leader)
        return;
    int value = textrec_value(t);
    int sub = textrec_subtype(t);
    bool visible = textrec_is_visible(t);
    if (sub == SUBTYPE_ITEM_HOLDER || sub == SUBTYPE_ITEM_HOLDER_MARKER) {
        wall_click_holder_text(c, t, sub, value, visible);
    } else if ((sub == SUBTYPE_LADDER_HIDDEN && !visible) || sub == SUBTYPE_LADDER) {
        if (c->hand != THING_NONE)
            return;
        int delta =
            gdat_entry_index(gdat_current(), CAT_WALL_ORNAMENT, value, GDAT_TYPE_VALUE, ORNAMENT_KEY_UP) ? -1 : 1;
        int16_t lx = g_game.party_x;
        int16_t ly = g_game.party_y;
        int map = dungeon_location_on_map(g_game.party_map, delta, &lx, &ly);
        if (map >= 0) {
            move_party_to((Position){map, lx, ly}, g_game.party_dir);
            g_hooks.flush_input();
        }
    } else if (sub == SUBTYPE_HAND_SWITCH && c->hand == THING_NONE) {
        textrec_set_visible(t, !visible);
    }
}

void sensor_process_wall_click(int16_t x, int16_t y, int side, uint16_t actor, uint16_t held)
{
    WallClick c = {.x = x, .y = y, .hand = held, .give = THING_NONE, .has_leader = g_game.leader != LEADER_NONE};
    uint16_t first = first_thing(x, y);
    for (uint16_t t = first; t != THING_ENDOFLIST && !c.done; t = dungeon_next_thing(t)) {
        c.cell = thing_cell(t);
        if (c.cell != side)
            continue;
        if (dun_is_alcove_shown(first, t)) {
            wall_click_alcove(&c);
            break;
        }
        if (ornament_value(t, ORNAMENT_KEY_TOUCH)) {
            wall_click_fountain(&c);
            break;
        }
        int type = thing_type(t);
        if (type == THING_SENSOR)
            wall_click_sensor(&c, t);
        else if (actor == THING_NONE && type == THING_TEXT)
            wall_click_text(&c, t);
        else if (type >= THING_SENSOR)
            break;
    }
    if (actor == THING_NONE) {
        if (c.take)
            champ_take_from_leader_hand();
        if (c.give != THING_NONE)
            champ_put_in_leader_hand(c.give, 1);
        g_input_command_stop = c.stop;
    } else {
        uint16_t *possessions = group_possessions(actor);
        if (!c.take && held != THING_NONE)
            dungeon_append_thing(held, possessions);
        if (c.give != THING_NONE)
            dungeon_append_thing(c.give, possessions);
    }
}
