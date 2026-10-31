/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_TIMELINE_H
#define DM2_ENGINE_TIMELINE_H
/* Prefixes: timeline_ */

/* The timeline of the game: the scheduled events and their handlers. */

#include <stdbool.h>
#include <stdint.h>
#include "champion.h"

/* Timeline event types; EVENT_NONE marks a free event. */
#define EVENT_NONE                  0x00
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

/* The mask of the time in time_map. */
#define EVENT_TIME_MASK 0x00ffffffu

/* The shift of the map in time_map. */
#define EVENT_MAP_SHIFT 24

/* No queue slot. */
#define TIMELINE_NO_SLOT (-1)

/* The sizes of an event and of its data bytes. */
#define TIMELINE_EVENT_SIZE      12
#define TIMELINE_EVENT_DATA_SIZE 6

/* A scheduled game event. The event handlers read the data bytes. */
typedef struct {
    union {
        uint32_t time_map; /* time in bits 0-23, map in bits 24-31 */
        int16_t next_free; /* a free event links the next free event, -1 ends the list */
    };
    uint8_t type; /* EVENT_NONE marks a free event */
    uint8_t priority;
    uint8_t data[TIMELINE_EVENT_DATA_SIZE]; /* x, y, cell, effect, or thing, by event type */
} TimelineEvent;

_Static_assert(sizeof(TimelineEvent) == TIMELINE_EVENT_SIZE, "the save game stores 12-byte events");

/* The time_map value of an event at time on map; the caller masks time when it can pass 24 bits. */
static inline uint32_t timeline_time_map(uint32_t time, int16_t map)
{
    return time | (uint32_t)(uint8_t)map << EVENT_MAP_SHIFT;
}

/* An event of type at time on map with priority; its data bytes are 0. */
static inline TimelineEvent timeline_event_at(uint32_t time, int16_t map, uint8_t type, uint8_t priority)
{
    return (TimelineEvent){.time_map = timeline_time_map(time, map), .type = type, .priority = priority};
}

/* The map of an event; time_map stores map -1 as 0xff. */
static inline int16_t timeline_event_map(const TimelineEvent *e)
{
    return (int8_t)(e->time_map >> EVENT_MAP_SHIFT);
}

/* Moves an event delay ticks later on its map; a game time stays below EVENT_TIME_MASK, so the map stays. */
static inline void timeline_delay_event(TimelineEvent *e, uint32_t delay)
{
    e->time_map += delay;
}

/* Moves an event to map and keeps its time. */
static inline void timeline_set_event_map(TimelineEvent *e, int16_t map)
{
    e->time_map = timeline_time_map(e->time_map & EVENT_TIME_MASK, map);
}

typedef struct {
    TimelineEvent *events; /* owned: capacity events */
    uint16_t *queue;       /* owned: binary heap of event indices, the next event in queue[0] */
    int16_t fix_slot;      /* queue slot whose heap order the next operation restores, or TIMELINE_NO_SLOT */
    int16_t first_free;    /* first free event, -1 when all are in use */
    uint16_t capacity;
    uint16_t count;
    int16_t used_end; /* one past the highest event index in use */
} Timeline;

extern Timeline g_timeline;

/* Setup */
/* Starts an empty timeline with room for capacity events. */
void timeline_init(uint16_t capacity);
/* Frees the event and queue storage; safe to call twice. */
void timeline_free(void);

/* Queries */
/* True when the next event is due at game_time. */
bool timeline_is_event_due(uint32_t game_time);
/* The index of the first event from index from on with type and priority, -1 none. all_slots reads all event slots;
   otherwise only the first count slots, as in the original. */
int timeline_find_event(int from, uint8_t type, uint8_t priority, bool all_slots);

/* Commands */
/* Queues a copy of event; returns its event index, or -1 for an event without a type. */
int timeline_add_event(const TimelineEvent *event);
/* Removes a queued event and frees it. The last queue entry fills its slot; the next operation restores the heap
   order. */
void timeline_delete_event(int index);
/* Moves a queued event to time on map and restores the queue order. */
void timeline_set_event_time(int index, uint32_t time, int16_t map);
/* Removes the next event and copies it to event; call timeline_is_event_due first. */
void timeline_pop_first_event(TimelineEvent *event);
/* Orders the queue of the first count events and links the other events into the free list. */
void timeline_build_queue(void);
/* Links the events without a type into the free list and finds the end of the used events. */
void timeline_rebuild_free_list(void);
/* Moves the events in use to the first count slots and rebuilds the queue. */
void timeline_compact(void);

/* The event handlers. */
/* A step of an altar rebirth (EVENT_ALTAR_REBIRTH). */
void timeline_event_altar_rebirth(TimelineEvent *e);
/* Runs a due group move event: the group moves to the square that it could not enter before. */
void timeline_event_move_group(TimelineEvent *e);
/* The timed effect of an eaten item (EVENT_ITEM_EFFECT) changes a champion: mode EFFECT_MODE_CONSUME_END, or EFFECT_MODE_RESTORE
   for a loaded game. */
void timeline_event_apply_item_effect(const TimelineEvent *e, enum EffectMode mode);

#endif /* DM2_ENGINE_TIMELINE_H */
