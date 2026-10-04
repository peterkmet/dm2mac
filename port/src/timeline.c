#include <stdlib.h>
#include "timeline.h"

#define EVENT_TIME_MASK 0xffffffu
#define END_OF_FREE_LIST (-1)

Timeline timeline;

static uint32_t event_time(uint16_t index)
{
    return timeline.events[index].time_map & EVENT_TIME_MASK;
}

/* True when event a runs before event b: earlier time, then higher type, then higher priority, then lower index. */
static int runs_before(uint16_t a, uint16_t b)
{
    const TimelineEvent *ea = &timeline.events[a], *eb = &timeline.events[b];
    if (event_time(a) != event_time(b)) return event_time(a) < event_time(b);
    if (ea->type != eb->type) return ea->type > eb->type;
    if (ea->priority != eb->priority) return ea->priority > eb->priority;
    return a <= b;
}

/* Moves the event in a queue slot up, or else down, until the heap order holds again. */
static void fix_queue_slot(int slot)
{
    uint16_t *queue = timeline.queue;
    uint16_t moving = queue[slot];
    int last = timeline.count - 1;
    int moved_up = 0;

    timeline.fix_slot = TIMELINE_NO_SLOT;
    if (last == 0) return;
    while (slot > 0 && runs_before(moving, queue[(slot - 1) / 2])) {
        queue[slot] = queue[(slot - 1) / 2];
        slot = (slot - 1) / 2;
        moved_up = 1;
    }
    while (!moved_up && slot <= (last - 1) / 2) {
        int child = 2 * slot + 1;
        if (child + 1 <= last && runs_before(queue[child + 1], queue[child])) child++;
        if (!runs_before(queue[child], moving)) break;
        queue[slot] = queue[child];
        slot = child;
    }
    queue[slot] = moving;
}

static void fix_pending_slot(void)
{
    if (timeline.fix_slot >= 0) fix_queue_slot(timeline.fix_slot);
}

/* The queue slot of a queued event. */
static int find_queue_slot(uint16_t index)
{
    int slot = 0;
    while (slot < timeline.used_end && timeline.queue[slot] != index) slot++;
    if (slot == timeline.used_end) abort(); /* the original stops with a system error */
    return slot;
}

void timeline_init(uint16_t capacity)
{
    free(timeline.events);
    free(timeline.queue);
    timeline.events = calloc(capacity, sizeof *timeline.events);
    timeline.queue = calloc(capacity, sizeof *timeline.queue);
    for (int i = 0; i < capacity; i++) timeline.events[i].next_free = i + 1;
    timeline.events[capacity - 1].next_free = END_OF_FREE_LIST;
    timeline.capacity = capacity;
    timeline.count = 0;
    timeline.first_free = 0;
    timeline.used_end = 0;
    timeline.fix_slot = TIMELINE_NO_SLOT;
}

int timeline_add_event(const TimelineEvent *event)
{
    if (event->type == EVENT_NONE) return -1;
    if (timeline.count == timeline.capacity) abort(); /* the original stops with a system error */
    int index = timeline.first_free;
    timeline.first_free = timeline.events[index].next_free;
    timeline.events[index] = *event;
    if (index >= timeline.used_end) timeline.used_end = index + 1;
    /* A slot left open by a delete takes the new event, else the queue grows by one. */
    int slot = timeline.fix_slot >= 0 ? timeline.fix_slot : timeline.count;
    timeline.count++;
    timeline.queue[slot] = index;
    fix_queue_slot(slot);
    return index;
}

/* The last queue entry fills the slot of the deleted event; the next operation restores the heap order. */
void timeline_delete_event(uint16_t index)
{
    fix_pending_slot();
    timeline.events[index].type = EVENT_NONE;
    timeline.events[index].next_free = timeline.first_free;
    timeline.first_free = index;
    if (--timeline.count == 0) return;
    int slot = find_queue_slot(index);
    if (slot != timeline.count) {
        timeline.fix_slot = slot;
        timeline.queue[slot] = timeline.queue[timeline.count];
    }
}

void timeline_pop_first_event(TimelineEvent *event)
{
    uint16_t index = timeline.queue[0];
    *event = timeline.events[index];
    timeline_delete_event(index);
}

int timeline_is_event_due(uint32_t game_time)
{
    fix_pending_slot();
    return timeline.count != 0 && event_time(timeline.queue[0]) <= game_time;
}
