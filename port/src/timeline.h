#ifndef TIMELINE_H
#define TIMELINE_H
#include <stdint.h>

#define EVENT_NONE 0
#define TIMELINE_NO_SLOT (-1)

/* A scheduled game event. The event handlers read the data bytes. */
typedef struct {
    union {
        uint32_t time_map;  /* time in bits 0-23, map in bits 24-31 */
        int16_t next_free;  /* a free event links the next free event, -1 ends the list */
    };
    uint8_t type;           /* EVENT_NONE marks a free event */
    uint8_t priority;
    uint8_t data[6];        /* x, y, cell, effect, or thing, by event type */
} TimelineEvent;

_Static_assert(sizeof(TimelineEvent) == 12, "the save game stores 12-byte events");

typedef struct {
    TimelineEvent *events;
    uint16_t *queue;        /* binary heap of event indices, the next event in queue[0] */
    int16_t fix_slot;       /* queue slot whose heap order the next operation restores, or TIMELINE_NO_SLOT */
    int16_t first_free;     /* first free event, -1 when all are in use */
    uint16_t capacity;
    uint16_t count;
    int16_t used_end;       /* one past the highest event index in use */
} Timeline;

extern Timeline timeline;

/* Starts an empty timeline with room for capacity events. */
void timeline_init(uint16_t capacity);
/* Queues a copy of event; returns its event index, or -1 for an event without a type. */
int timeline_add_event(const TimelineEvent *event);
/* Removes a queued event and frees it. */
void timeline_delete_event(uint16_t index);
/* Removes the next event and copies it to event; call timeline_is_event_due first. */
void timeline_pop_first_event(TimelineEvent *event);
/* Orders the queue of the first count events and links the other events into the free list. */
void timeline_build_queue(void);
/* Moves the events in use to the first count slots and rebuilds the queue. */
void timeline_compact(void);
/* True when the next event is due at game_time. */
int timeline_is_event_due(uint32_t game_time);

#endif
