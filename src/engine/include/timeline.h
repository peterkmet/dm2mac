/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_TIMELINE_H
#define DM2_ENGINE_TIMELINE_H
/* Prefixes: timeline_ */

/* The timeline of the game: the scheduled events and their handlers. */

#include <stdbool.h>
#include <stdint.h>

/* Event type of a free event. */
#define EVENT_NONE 0x00

/* The mask of the time in time_map. */
#define EVENT_TIME_MASK 0x00ffffffu

/* The shift of the map in time_map. */
#define EVENT_MAP_SHIFT 24

/* Sizes of an event and no slot. */
#define TIMELINE_NO_SLOT         (-1)
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

/* The map of an event. */
static inline uint8_t timeline_event_map(const TimelineEvent *e)
{
    return (uint8_t)(e->time_map >> EVENT_MAP_SHIFT);
}

/* Moves an event to map and keeps its time. */
static inline void timeline_set_event_map(TimelineEvent *e, int16_t map)
{
    e->time_map = timeline_time_map(e->time_map & EVENT_TIME_MASK, map);
}

typedef struct {
    TimelineEvent *events;
    uint16_t *queue;    /* binary heap of event indices, the next event in queue[0] */
    int16_t fix_slot;   /* queue slot whose heap order the next operation restores, or TIMELINE_NO_SLOT */
    int16_t first_free; /* first free event, -1 when all are in use */
    uint16_t capacity;
    uint16_t count;
    int16_t used_end; /* one past the highest event index in use */
} Timeline;

extern Timeline g_timeline;

/* Starts an empty timeline with room for capacity events. */
void timeline_init(uint16_t capacity);
/* Frees the event and queue storage; safe to call twice. */
void timeline_free(void);
/* Queues a copy of event; returns its event index, or -1 for an event without a type. */
int timeline_add_event(const TimelineEvent *event);
/* Removes a queued event and frees it. The last queue entry fills its slot; the next operation restores the heap
   order. */
void timeline_delete_event(int index);
/* Restores the queue order after the time of a queued event changed. */
void timeline_reposition_event(int index);
/* Removes the next event and copies it to event; call timeline_is_event_due first. */
void timeline_pop_first_event(TimelineEvent *event);
/* Orders the queue of the first count events and links the other events into the free list. */
void timeline_build_queue(void);
/* Links the events without a type into the free list and finds the end of the used events. */
void timeline_rebuild_free_list(void);
/* Moves the events in use to the first count slots and rebuilds the queue. */
void timeline_compact(void);
/* True when the next event is due at game_time. */
bool timeline_is_event_due(uint32_t game_time);

/* The event loop (game.c) and the event handlers of other modules. */
/* A step of an altar rebirth (EVENT_ALTAR_REBIRTH). */
void timeline_event_altar_rebirth(TimelineEvent *e);
/* Runs a due group move event. */
void timeline_event_move_group(TimelineEvent *e);
/* Runs the due timeline events. */
void timeline_process(void);
/* The timed effect of an eaten item (EVENT_ITEM_EFFECT) changes champ: mode EFFECT_MODE_CONSUME_END, or EFFECT_MODE_RESTORE
   for a loaded game. */
void timeline_event_apply_item_effect(const TimelineEvent *e, int mode);
/* A magic light event: the light fades one level. */
void timeline_event_magic_light(TimelineEvent *e);
/* Runs a due square event or door event. */
void timeline_event_square(TimelineEvent *e);
/* Runs a due door animation step. */
void timeline_event_door_animation(TimelineEvent *e);
/* Runs a due door destruction event. */
void timeline_event_door_destruction(TimelineEvent *e);
/* Runs a due sensor animation, timer, or busy event. */
void timeline_event_sensor(TimelineEvent *e);

#endif /* DM2_ENGINE_TIMELINE_H */
