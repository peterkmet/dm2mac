/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_SENSOR_H
#define DM2_ENGINE_SENSOR_H
/* Prefixes: sensor_ */

/* Floor and wall sensors: their record fields, their timeline events, and the missiles. */

#include "dungeon.h"
#include "timeline.h"
#include <stdbool.h>
#include <stdint.h>

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
    set_thing_field(sensor, SENSOR_TYPE_DATA, SENSOR_DATA_SHIFT, SENSOR_DATA_MASK, data);
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
    set_thing_bit(sensor, SENSOR_FLAGS, bit, on);
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
/* The ordinal of the ornament in the wall or floor ornament list of the map, 0 for none. */
static inline uint16_t sensor_ornament_ordinal(uint16_t sensor)
{
    return thing_field(sensor, SENSOR_FLAGS, SENSOR_ORNAMENT_SHIFT, SENSOR_ORNAMENT_MASK);
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

/* Timeline event types of Step 14. */
#define EVENT_DOOR_ANIMATION        0x01
#define EVENT_DOOR_DESTRUCTION      0x02
#define EVENT_SQUARE                0x04
#define EVENT_HIDE_DAMAGE           0x0c
#define EVENT_ALTAR_REBIRTH         0x0d
#define EVENT_ITEM_EFFECT           0x0e
#define EVENT_DELAYED_SOUND         0x15
#define EVENT_EXPLOSION             0x19
#define EVENT_PROJECTILE_MOVE       0x1d
#define EVENT_PROJECTILE_MOVE_2     0x1e
#define EVENT_CREATURE_ANIMATION    0x21
#define EVENT_CREATURE_THINK        0x22
#define EVENT_MOVE_GROUP            0x3c
#define EVENT_MOVE_GROUP_2          0x3d
#define EVENT_MAGIC_LIGHT           0x46
#define EVENT_INVISIBILITY          0x47
#define EVENT_AURA                  0x48
#define EVENT_POISON                0x4b
#define EVENT_WEATHER               0x54
#define EVENT_SENSOR_ANIMATION_STEP 0x55
#define EVENT_SENSOR_TIMER          0x56
#define EVENT_SENSOR_IDLE           0x57
#define EVENT_DOOR_BUTTON_RELEASE   0x58
#define EVENT_SENSOR_ANIMATION_END  0x59
#define EVENT_SENSOR_AMBIENT_SOUND  0x5a
#define EVENT_SENSOR_READY          0x5b
#define EVENT_SET_MARKER            0x5c
#define EVENT_TELEPORT_PARTY        0x5d
#define EVENT_SPAWN_CREATURE        0x5e
/* Effects of a square event: what it does to its target. */
#define EFFECT_SET                  0
#define EFFECT_CLEAR                1
#define EFFECT_TOGGLE               2
#define EFFECT_HOLD                 3
/* Bytes of a square event. */
#define EVENT_X                     0x00
#define EVENT_Y                     0x01
#define EVENT_CELL                  0x02
#define EVENT_EFFECT                0x03
/* Bytes of the other events of this module: a sensor event holds its sensor thing (an ambient sound or animation end
   event after its square), then the wall flag or the timer factor and phase; a party teleport holds the target word
   and the map; a creature spawn holds the square and the creature type; an altar rebirth holds a square event with
   the steps left in place of the effect. */
#define SENSOR_EVENT_THING          0x00
#define SENSOR_EVENT_WALL           0x02
#define SENSOR_EVENT_TIMER_FACTOR   0x02
#define SENSOR_EVENT_TIMER_PHASE    0x03
#define SENSOR_EVENT_SQUARE_THING   0x02
#define TELEPORT_EVENT_TARGET       0x00
#define TELEPORT_EVENT_MAP          0x02
#define SPAWN_EVENT_CREATURE        0x03
#define ALTAR_EVENT_STEPS           0x03

/* Sensor types that launch missiles. */
#define LAUNCH_ITEM    0x07
#define LAUNCH_SPELL   0x08
#define LAUNCH_ITEMS   0x09
#define LAUNCH_SPELLS  0x0a
#define LAUNCH_OBJECT  0x0e
#define LAUNCH_OBJECTS 0x0f
/* Sensor type 0: the sensor does nothing. */
#define SENSOR_NONE    0x00
/* Floor sensor types that act on the creatures of an area. */
#define SENSOR_VANISH  0x0b
#define SENSOR_ALERT   0x28

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

/* A thing (THING_NONE: the party) leaves (enter 0) or enters (enter 1) the square (x, y) of the current map:
   the floor sensors and texts of the square react. on_square: the party stands there (for the party: the
   move ends on the same square); possessions: an item goes into the possessions of a static creature. */
void sensor_process_floor(int x, int y, uint16_t thing, int on_square, int enter, int possessions);
/* Queues a square event for (x, y) of the current map at time. */
void sensor_schedule_square_event(int x, int y, int cell, int effect, uint32_t time);
/* Queues the square event of the target of sensor with effect, delay ticks after its own delay. */
void sensor_trigger_target(uint16_t sensor, int effect, int delay);
/* A strong hit on the door at (x, y): it breaks when strength reaches its door type value (magic: by spell).
   Returns 1 when the door breaks. */
int sensor_door_try_destroy(int x, int y, int strength, int magic, int delay);
/* A GDAT value of the door type of a door thing. */
uint16_t sensor_door_value(uint16_t door, int key);
/* A value of the alcove ornament of thing on a wall square (first: its first thing) that the wall face shows, else 0. */
int sensor_wall_ornament_value(uint16_t first, uint16_t thing, int key);

/* A click on the wall face side of (x, y) of the current map, by the party (actor THING_NONE) or a creature group,
   with hand (the object in the hand, THING_NONE: empty): buttons, alcoves, item holders, ladders. */
void sensor_process_wall_click(int x, int y, int side, uint16_t actor, uint16_t hand);
/* The party enters (1) or leaves (0) the party map: the map entry sensors and the creature generators. */
void sensor_process_map_entry(int entering);
/* A new game starts the timer sensors that are on and stops the others. */
void sensor_start_timers(void);
/* Moves thing (from (from_x, from_y), -1: a new thing) to the cell to_cell of (to_x, to_y) for a transfer sensor,
   with the charge change of the sensor; on a floor the cell is random. */
void sensor_transfer_object(uint16_t thing, uint16_t sensor, int from_x, int from_y, int to_x, int to_y, int to_cell,
                            int from_creature, int to_possessions);
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

/* Sensors that act on creatures (creature.c). */
/* Creature sensors: a sensor turns the group on its target square (event effect 0, or 1 with sensor flag 0x20). */
void sensor_turn_creatures(const TimelineEvent *e, uint16_t sensor);
/* A sensor reaches the groups of a creature type in the rectangle around (x, y) that (tx, ty) spans: kind 0x0b makes
   them vanish (power 2), kind 0x28 sets mood bit power (cleared when effect is set). */
void sensor_affect_creatures_in_area(int power, int creature_type, int x, int y, int tx, int ty, int kind, int effect);

/* Missile launchers (projectile.c). */
/* A missile launcher sensor shoots the objects of its square or new objects or spells. */
void sensor_launch_missile(uint16_t sensor, const TimelineEvent *e);

#endif /* DM2_ENGINE_SENSOR_H */
