/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "timeline.h"
#include <stdlib.h>
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "gdat.h"
#include "item.h"
#include "le.h"
#include "move.h"
#include "os.h"
#include "projectile.h"
#include "sensor.h"
#include "sound_request.h"

/* The end of the free list, and the system errors of the original: no queue slot, a full timeline. */
#define END_OF_FREE_LIST           (-1)
#define SYSTEM_ERROR_NO_QUEUE_SLOT 70
#define SYSTEM_ERROR_TIMELINE_FULL 45
/* The steps of an altar rebirth: smoke, then the bones of the champion vanish, then the champion lives again. */
#define REBIRTH_REVIVE             0
#define REBIRTH_BONES              1
#define REBIRTH_SMOKE              2
/* Game steps of the rebirth smoke. */
#define SMOKE_TIME                 5

Timeline g_timeline;

static uint32_t time_of_event(uint16_t index)
{
    return g_timeline.events[index].time_map & EVENT_TIME_MASK;
}

/* True when event a runs before event b: earlier time, then higher type, then higher priority, then lower index. */
static bool runs_before(uint16_t a, uint16_t b)
{
    const TimelineEvent *ea = &g_timeline.events[a];
    const TimelineEvent *eb = &g_timeline.events[b];
    if (time_of_event(a) != time_of_event(b))
        return time_of_event(a) < time_of_event(b);
    if (ea->type != eb->type)
        return ea->type > eb->type;
    if (ea->priority != eb->priority)
        return ea->priority > eb->priority;
    return a <= b;
}

/* Moves the event in a queue slot up, or else down, until the heap order holds again. */
static void fix_queue_slot(int slot)
{
    uint16_t *queue = g_timeline.queue;
    uint16_t moving = queue[slot];
    int last = g_timeline.count - 1;
    bool moved_up = false;

    g_timeline.fix_slot = TIMELINE_NO_SLOT;
    if (last == 0)
        return;
    while (slot > 0 && runs_before(moving, queue[(slot - 1) / 2])) {
        queue[slot] = queue[(slot - 1) / 2];
        slot = (slot - 1) / 2;
        moved_up = true;
    }
    while (!moved_up && slot <= (last - 1) / 2) {
        int child = 2 * slot + 1;
        if (child + 1 <= last && runs_before(queue[child + 1], queue[child]))
            child++;
        if (!runs_before(queue[child], moving))
            break;
        queue[slot] = queue[child];
        slot = child;
    }
    queue[slot] = moving;
}

static void fix_pending_slot(void)
{
    if (g_timeline.fix_slot >= 0)
        fix_queue_slot(g_timeline.fix_slot);
}

/* The queue slot of a queued event. */
static int find_queue_slot(uint16_t index)
{
    int slot = 0;
    while (slot < g_timeline.used_end && g_timeline.queue[slot] != index)
        slot++;
    if (slot == g_timeline.used_end)
        os_fatal("SYSTEM ERROR %d", SYSTEM_ERROR_NO_QUEUE_SLOT); /* the original stops with a system error */
    return slot;
}

void timeline_init(uint16_t capacity)
{
    free(g_timeline.events);
    free(g_timeline.queue);
    g_timeline.events = calloc(capacity, sizeof *g_timeline.events);
    g_timeline.queue = calloc(capacity, sizeof *g_timeline.queue);
    if (!g_timeline.events || !g_timeline.queue)
        os_fatal("out of memory");
    for (int i = 0; i < capacity; i++)
        g_timeline.events[i].next_free = i + 1;
    g_timeline.events[capacity - 1].next_free = END_OF_FREE_LIST;
    g_timeline.capacity = capacity;
    g_timeline.count = 0;
    g_timeline.first_free = 0;
    g_timeline.used_end = 0;
    g_timeline.fix_slot = TIMELINE_NO_SLOT;
}

void timeline_free(void)
{
    free(g_timeline.events);
    free(g_timeline.queue);
    g_timeline.events = NULL;
    g_timeline.queue = NULL;
    g_timeline.capacity = 0;
    g_timeline.count = 0;
    g_timeline.used_end = 0;
}

int timeline_add_event(const TimelineEvent *event)
{
    if (event->type == EVENT_NONE)
        return -1;
    if (g_timeline.count == g_timeline.capacity)
        os_fatal("SYSTEM ERROR %d", SYSTEM_ERROR_TIMELINE_FULL); /* the original stops with a system error */
    int index = g_timeline.first_free;
    g_timeline.first_free = g_timeline.events[index].next_free;
    g_timeline.events[index] = *event;
    if (index >= g_timeline.used_end)
        g_timeline.used_end = index + 1;
    /* A slot left open by a delete takes the new event, else the queue grows by one. */
    int slot = g_timeline.fix_slot >= 0 ? g_timeline.fix_slot : g_timeline.count;
    g_timeline.count++;
    g_timeline.queue[slot] = index;
    fix_queue_slot(slot);
    return index;
}

void timeline_delete_event(int index)
{
    fix_pending_slot();
    g_timeline.events[index].type = EVENT_NONE;
    g_timeline.events[index].next_free = g_timeline.first_free;
    g_timeline.first_free = index;
    if (--g_timeline.count == 0)
        return;
    int slot = find_queue_slot(index);
    if (slot != g_timeline.count) {
        g_timeline.fix_slot = slot;
        g_timeline.queue[slot] = g_timeline.queue[g_timeline.count];
    }
}

void timeline_reposition_event(int index)
{
    fix_pending_slot();
    fix_queue_slot(find_queue_slot(index));
}

void timeline_pop_first_event(TimelineEvent *event)
{
    uint16_t index = g_timeline.queue[0];
    *event = g_timeline.events[index];
    timeline_delete_event(index);
}

void timeline_build_queue(void)
{
    uint16_t *queue = g_timeline.queue;
    int count = g_timeline.count;
    for (int i = 0; i < count; i++)
        queue[i] = i;
    for (int start = (count - 2) / 2; count > 1 && start >= 0; start--) {
        uint16_t moving = queue[start];
        for (int slot = start; 2 * slot + 1 < count;) {
            int child = 2 * slot + 1;
            if (child + 1 < count) {
                if (runs_before(moving, queue[child]) ? !runs_before(moving, queue[child + 1])
                                                      : runs_before(queue[child + 1], queue[child]))
                    child++;
                else if (runs_before(moving, queue[child]))
                    break;
            } else if (runs_before(moving, queue[child])) {
                break;
            }
            queue[slot] = queue[child];
            queue[child] = moving;
            slot = child;
        }
    }
    g_timeline.fix_slot = TIMELINE_NO_SLOT;
    timeline_rebuild_free_list();
}

void timeline_rebuild_free_list(void)
{
    int prev = END_OF_FREE_LIST;
    g_timeline.used_end = 0;
    g_timeline.first_free = END_OF_FREE_LIST;
    for (int i = 0; i < g_timeline.capacity; i++) {
        if (g_timeline.events[i].type != EVENT_NONE) {
            g_timeline.used_end = i + 1;
            continue;
        }
        if (prev == END_OF_FREE_LIST)
            g_timeline.first_free = i;
        else
            g_timeline.events[prev].next_free = i;
        g_timeline.events[i].next_free = END_OF_FREE_LIST;
        prev = i;
    }
}

void timeline_compact(void)
{
    if (g_timeline.count == 0)
        return;
    for (int i = 0; i < g_timeline.count; i++) {
        if (g_timeline.events[i].type != EVENT_NONE)
            continue;
        int j = i + 1;
        while (j < g_timeline.capacity && g_timeline.events[j].type == EVENT_NONE)
            j++;
        if (j == g_timeline.capacity)
            break; /* fix: the original reads past the events when count is too high */
        g_timeline.events[i] = g_timeline.events[j];
        g_timeline.events[j].type = EVENT_NONE;
    }
    timeline_build_queue();
}

bool timeline_is_event_due(uint32_t game_time)
{
    fix_pending_slot();
    return g_timeline.count != 0 && time_of_event(g_timeline.queue[0]) <= game_time;
}

/* The event handlers. */

void timeline_event_altar_rebirth(TimelineEvent *e)
{
    int x = e->data[EVENT_X];
    int y = e->data[EVENT_Y];
    int cell = e->data[EVENT_CELL];
    int champ = e->priority;
    switch (e->data[ALTAR_EVENT_STEPS]) {
    case REBIRTH_REVIVE:
        champ_revive(champ);
        return;
    case REBIRTH_SMOKE:
        explosion_create(EXPLOSION_SMOKE, 0, x, y, cell);
        e->time_map += SMOKE_TIME;
        break;
    case REBIRTH_BONES: {
        uint16_t t = dungeon_square_first_thing(g_party.cur_map, x, y);
        for (; t != THING_ENDOFLIST; t = dungeon_next_thing(t))
            if (thing_cell(t) == cell && item_gdat_category(t) == CAT_MISC && item_gdat_index(t) == ITEM_BONES &&
                (int16_t)item_add_charges(t, 0) == champ)
                break;
        if (t == THING_ENDOFLIST)
            return;
        dungeon_unlink_thing_from_square(t, g_party.cur_map, x, y);
        thing_set_word(t, THING_NEXT, THING_NONE);
        e->time_map++;
        break;
    }
    default:
        return;
    }
    e->data[ALTAR_EVENT_STEPS]--;
    timeline_add_event(e);
}

void timeline_event_move_group(TimelineEvent *e)
{
    int x = e->data[0];
    int y = e->data[1];
    if (!move_thing(le16(e->data + EVENT_GROUP_WORD), MOVE_RETRY, 0, x, y) && e->type == EVENT_MOVE_GROUP_2)
        sndreq_request((SoundCue){CAT_DUNGEON, 0, SOUND_ENTRY_TELEPORT, SOUND_PRIORITY_MOVE, SOUND_VOLUME_NORMAL}, x, y,
                       SOUND_STEP_END);
}

void timeline_event_apply_item_effect(const TimelineEvent *e, enum EffectMode mode)
{
    /* the effect ends: a record of the item type stands in for the eaten item, with its kind */
    uint16_t thing = thing_make(le16(e->data + EFFECT_DATA_TYPE), 0);
    uint8_t saved[DUNGEON_RECORD_MAX];
    dungeon_save_record(thing, saved);
    item_set_gdat_index(thing, e->data[EFFECT_DATA_INDEX]);
    champ_apply_item_effects(e->priority, thing, -1, mode);
    dungeon_restore_record(thing, saved);
}
