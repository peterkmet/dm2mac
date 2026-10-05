#ifndef SENSOR_H
#define SENSOR_H
#include <stdint.h>
#include "timeline.h"

/* Timeline event types of Step 14. */
enum event_type {
    EVENT_DOOR_ANIMATION = 1, EVENT_DOOR_DESTRUCTION = 2, EVENT_SQUARE = 4, EVENT_HIDE_DAMAGE = 0x0c,
    EVENT_ALTAR_REBIRTH = 0x0d, EVENT_SENSOR_ANIMATION_STEP = 0x55, EVENT_SENSOR_TIMER = 0x56,
    EVENT_SENSOR_IDLE = 0x57, EVENT_CLEAR_THING_FLAG8 = 0x58, EVENT_SENSOR_ANIMATION_END = 0x59,
    EVENT_SENSOR_AMBIENT_SOUND = 0x5a, EVENT_SENSOR_READY = 0x5b, EVENT_SET_MARKER = 0x5c,
    EVENT_TELEPORT_PARTY = 0x5d, EVENT_SPAWN_CREATURE = 0x5e
};
/* Effects of a square event: what it does to its target. */
enum sensor_effect { EFFECT_SET, EFFECT_CLEAR, EFFECT_TOGGLE, EFFECT_HOLD };
/* Bytes of a square event. */
enum { EVENT_X, EVENT_Y, EVENT_CELL, EVENT_EFFECT };

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
/* Runs a due square event or door event. */
void timeline_event_square(TimelineEvent *e);
void timeline_event_door_animation(TimelineEvent *e);
void timeline_event_door_destruction(TimelineEvent *e);

/* The party enters (1) or leaves (0) the party map: the map entry sensors and the creature generators. */
void sensor_process_map_entry(int entering);
/* Starts the timers of the timer sensors of a new game. */
void sensor_start_timers(int new_game);
/* Runs a due sensor animation, timer, or busy event. */
void timeline_event_sensor(TimelineEvent *e);
/* Script variables: 64 flags, 64 bytes, 64 words (0x00, 0x40, 0x80); op 0 set, 1 clear, 2 toggle, 3 add, 4 subtract, 6 assign. */
int dun_get_global_var(int var);
void dun_modify_global_var(int var, int value, int op);

#endif
