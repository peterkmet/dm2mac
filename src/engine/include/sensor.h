/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_SENSOR_H
#define DM2_ENGINE_SENSOR_H
/* Prefixes: sensor_ */

/* Floor and wall sensors: their record fields, their square events and timers, and wall clicks. */

#include <stdbool.h>
#include <stdint.h>
#include "dungeon.h"
#include "timeline.h"

/* Record fields of the things of this module; dungeon.h holds the record layout. */
static inline uint16_t sensor_type(uint16_t sensor)
{
    return thing_field(sensor, SENSOR_TYPE_DATA, 0, SENSOR_TYPE_MASK);
}
/* The value of the sensor type: an item index, a counter, a map, an animation frame. */
static inline uint16_t sensor_data(uint16_t sensor)
{
    return thing_field(sensor, SENSOR_TYPE_DATA, SENSOR_DATA_SHIFT, SENSOR_DATA_MASK);
}
static inline void sensor_set_data(uint16_t sensor, uint16_t data)
{
    thing_set_field(sensor, SENSOR_TYPE_DATA, SENSOR_DATA_SHIFT, SENSOR_DATA_MASK, data);
}
static inline uint16_t sensor_frame(uint16_t sensor)
{
    return sensor_data(sensor) & SENSOR_DATA_FRAME_MASK;
}
static inline bool sensor_sound_on(uint16_t sensor)
{
    return (sensor_data(sensor) & SENSOR_DATA_SOUND) != 0;
}
static inline bool sensor_flag(uint16_t sensor, uint16_t bit)
{
    return thing_bit(sensor, SENSOR_FLAGS, bit);
}
static inline void sensor_set_flag(uint16_t sensor, uint16_t bit, bool on)
{
    thing_set_bit(sensor, SENSOR_FLAGS, bit, on);
}
/* The effect on the target: EFFECT_SET, EFFECT_CLEAR, EFFECT_TOGGLE, or EFFECT_HOLD. */
static inline uint16_t sensor_effect(uint16_t sensor)
{
    return thing_field(sensor, SENSOR_FLAGS, SENSOR_EFFECT_SHIFT, SENSOR_EFFECT_MASK);
}
/* The ticks between the trigger and the event on the target. */
static inline uint16_t sensor_delay(uint16_t sensor)
{
    return thing_field(sensor, SENSOR_FLAGS, SENSOR_DELAY_SHIFT, SENSOR_DELAY_MASK);
}
/* The index of the ornament in the wall or floor ornament list of the map, -1 for none (the record stores index + 1,
   0 for none). */
static inline int16_t sensor_ornament_index(uint16_t sensor)
{
    return (int16_t)(thing_field(sensor, SENSOR_FLAGS, SENSOR_ORNAMENT_SHIFT, SENSOR_ORNAMENT_MASK) - 1);
}
static inline uint16_t sensor_target_x(uint16_t sensor)
{
    return thing_field(sensor, SENSOR_TARGET, SENSOR_TARGET_X_SHIFT, LOCATION_COORD_MASK);
}
static inline uint16_t sensor_target_y(uint16_t sensor)
{
    return thing_field(sensor, SENSOR_TARGET, SENSOR_TARGET_Y_SHIFT, LOCATION_COORD_MASK);
}
static inline uint16_t sensor_target_cell(uint16_t sensor)
{
    return thing_field(sensor, SENSOR_TARGET, SENSOR_TARGET_CELL_SHIFT, SENSOR_TARGET_CELL_MASK);
}
static inline uint16_t sensor_launch_energy(uint16_t sensor)
{
    return thing_field(sensor, SENSOR_TARGET, LAUNCH_ENERGY_SHIFT, LAUNCH_ENERGY_MASK);
}
static inline uint16_t sensor_launch_step(uint16_t sensor)
{
    return thing_field(sensor, SENSOR_TARGET, LAUNCH_STEP_SHIFT, LAUNCH_STEP_MASK);
}

/* Effects of a square event: what it does to its target. */
#define EFFECT_SET                0
#define EFFECT_CLEAR              1
#define EFFECT_TOGGLE             2
#define EFFECT_HOLD               3
/* Bytes of a square event. */
#define EVENT_X                   0x00
#define EVENT_Y                   0x01
#define EVENT_CELL                0x02
#define EVENT_EFFECT              0x03
/* Bytes of the other events of this module: a sensor event holds its sensor thing (an ambient sound or animation end
   event after its square), then the wall flag or the timer factor and phase; a party teleport holds the target word
   and the map; a creature spawn holds the square and the creature type; an altar rebirth holds a square event with
   the steps left in place of the effect. */
#define SENSOR_EVENT_THING        0x00
#define SENSOR_EVENT_WALL         0x02
#define SENSOR_EVENT_TIMER_FACTOR 0x02
#define SENSOR_EVENT_TIMER_PHASE  0x03
#define SENSOR_EVENT_SQUARE_THING 0x02
#define TELEPORT_EVENT_TARGET     0x00
#define TELEPORT_EVENT_MAP        0x02
#define SPAWN_EVENT_CREATURE      0x03
#define ALTAR_EVENT_STEPS         0x03

/* Sensor types that launch missiles. */
#define LAUNCH_ITEM             0x07
#define LAUNCH_SPELL            0x08
#define LAUNCH_ITEMS            0x09
#define LAUNCH_SPELLS           0x0a
#define LAUNCH_OBJECT           0x0e
#define LAUNCH_OBJECTS          0x0f
/* Floor sensor types. */
#define FLOOR_ANY               0x01
#define FLOOR_PARTY_OR_CREATURE 0x02
#define FLOOR_PARTY             0x03
#define FLOOR_ITEM              0x04
#define FLOOR_CREATURE          0x07
#define FLOOR_PARTY_POSSESSES   0x08
/* Sensor type 0: the sensor does nothing. */
#define SENSOR_NONE             0x00
/* Floor sensor types that act on the creatures of an area. */
#define SENSOR_VANISH           0x0b
#define SENSOR_ALERT            0x28

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

/* Sensor data sub-fields: the target map (bits 0-5) and wall cell (bits 6-7) of a map relay and a teleport button,
   the direction of a creature turn sensor, and the power (bits 0-3) and creature type of an area sensor. */
#define SENSOR_MAP_MASK     0x3f
#define AREA_POWER_MASK     0x0f
/* The positions of the wall cell and the creature type in the sensor data. */
#define SENSOR_CELL_SHIFT   6
#define AREA_CREATURE_SHIFT 4
static inline uint16_t sensor_data_map(uint16_t sensor)
{
    return sensor_data(sensor) & SENSOR_MAP_MASK;
}
static inline uint16_t sensor_data_cell(uint16_t sensor)
{
    return sensor_data(sensor) >> SENSOR_CELL_SHIFT & DIR_MASK;
}
static inline uint16_t sensor_turn_dir(uint16_t sensor)
{
    return sensor_data(sensor) & DIR_MASK;
}
static inline uint16_t sensor_area_power(uint16_t sensor)
{
    return sensor_data(sensor) & AREA_POWER_MASK;
}
static inline uint16_t sensor_area_creature(uint16_t sensor)
{
    return sensor_data(sensor) >> AREA_CREATURE_SHIFT;
}

/* Wall sensor types of objects put on a wall cell. */
#define WALL_ITEM_HOLDER        0x1a
#define WALL_ANY_ITEM           0x29
#define WALL_ITEM               0x2a
#define WALL_NOT_ITEM           0x2b
/* Wall sensor types of a click on a wall cell. */
#define WALL_CLICK_ANY          0x01
#define WALL_CLICK_EMPTY_HAND   0x02
#define WALL_CLICK_ITEM         0x03
#define WALL_CLICK_CHARGED_ITEM 0x15
#define WALL_CLICK_TOGGLE       0x17
#define WALL_CLICK_BUTTON       0x18
#define WALL_CLICK_COUNTED_ITEM 0x1b
#define WALL_CLICK_TELEPORT     0x1c
#define WALL_CLICK_RESET        0x3f
#define WALL_CLICK_DOOR_BUTTON  0x46
#define WALL_CLICK_BUTTON_2     0x4a
#define WALL_CLICK_PORTRAIT     0x7e
/* The ornament value of a fountain: touching the ornament fills the water. */
#define ORNAMENT_KEY_TOUCH      0x0b

/* Doors and square events. */
/* A GDAT value of the door type of a door thing. */
uint16_t sensor_door_value(uint16_t door, int key);
/* True when a living champion, the open container, or the leader hand holds an item of the item index type. */
bool sensor_party_possesses_type(int type);
/* Queues a square event for (x, y) of the current map at time. */
void sensor_schedule_square_event(int16_t x, int16_t y, int16_t cell, int effect, uint32_t time);
/* Presses the button of door at the square: toggles the door next tick and queues the button release. */
void sensor_press_door_button(Location at, uint16_t door);
/* Queues the square event of the target of sensor with effect, delay ticks after its own delay. */
void sensor_trigger_target(uint16_t sensor, int effect, int delay);
/* A strong hit on the door at (x, y): it breaks when strength reaches its door type value (magic: by spell).
   True when the door breaks. */
bool sensor_door_try_destroy(int16_t x, int16_t y, int strength, bool magic, int delay);

/* Floor sensors. */
/* A thing (THING_NONE: the party) enters the square (x, y) of the current map: the floor sensors and texts of the
   square react. on_square: the party stands there (for the party: the move ends on the same square); into_possessions:
   an item goes into the possessions of a static creature. */
void sensor_enter_square(int16_t x, int16_t y, uint16_t thing, bool on_square, bool into_possessions);
/* A thing (THING_NONE: the party) leaves the square (x, y) of the current map, as sensor_enter_square. */
void sensor_leave_square(int16_t x, int16_t y, uint16_t thing, bool on_square, bool into_possessions);

/* Sensor effects. */
/* A new game starts the timer sensors that are on and stops the others. */
void sensor_start_timers(void);

/* Square events. */
/* Runs a due square event or door event. */
void sensor_event_square(TimelineEvent *e);
/* Runs a due door destruction event. */
void sensor_event_door_destruction(TimelineEvent *e);
/* Runs a due door animation step. */
void sensor_event_door_animation(TimelineEvent *e);

/* Sensor timeline events. */
/* Runs a due sensor animation, timer, or busy event. */
void sensor_process_event(TimelineEvent *e);
/* The party enters (entering) or leaves the party map: the map entry sensors and the creature generators. */
void sensor_process_map_entry(bool entering);

/* Wall clicks. */
/* A value of the alcove ornament of thing on a wall square (first: its first thing) that the wall face shows, else 0. */
int sensor_wall_ornament_value(uint16_t first, uint16_t thing, int key);
/* A click on the wall face side of (x, y) of the current map, by the party (actor THING_NONE) or a creature group,
   with held (the object in the hand, THING_NONE: empty): buttons, alcoves, item holders, ladders. True when a sensor
   reacts, so that the command stops. */
bool sensor_process_wall_click(int16_t x, int16_t y, int side, uint16_t actor, uint16_t held);

/* Script variables: 64 flags, 64 bytes, 64 words (0x00, 0x40, 0x80); op 0 set, 1 clear, 2 toggle, 3 add, 4 subtract, 6 assign. */
#define GLOBAL_VAR_SET      0
#define GLOBAL_VAR_CLEAR    1
#define GLOBAL_VAR_TOGGLE   2
#define GLOBAL_VAR_ADD      3
#define GLOBAL_VAR_SUBTRACT 4
#define GLOBAL_VAR_ASSIGN   6
/* The first byte variable, and the largest value of a byte variable. */
#define GLOBAL_BYTE_FIRST   (GLOBAL_FLAG_BYTES * BYTE_BIT_COUNT)
#define GLOBAL_BYTE_MAX     255

#endif /* DM2_ENGINE_SENSOR_H */
