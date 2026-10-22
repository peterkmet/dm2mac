/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The sound requests of the game: the queues of a game step, delayed sounds, and the place of a sound seen by the
   party. sound.c plays them. */
#include "sound_request.h"
#include "gdat.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "hooks.h"
#include "move.h"
#include "save.h"
#include "sensor.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Sizes of the request queues and the delays of a request. */
#define REQUESTS_MAX 20 /* requests of a game step */
#define DEFERRED_MAX 6
#define DELAYED_MAX  8
#define DELAY_NOW    0
#define DELAY_STEP   1 /* at the end of the game step */

/* The header of a GDAT sound item before its samples. */
#define SAMPLE_HEADER 6 /* u16 rate, then 08 01 00 00 */

/* Distance scaling and volume in 10-bit and 8-bit fixed point. */
#define SCALE_SHIFT     10
#define SCALE_ROUND     (1 << (SCALE_SHIFT - 1))
#define VOLUME_SHIFT    8
#define LOUDNESS_SHIFT  5
#define DISTANCE_BIAS   8
#define PAN_RATIO_SHIFT 11

/* The stereo pan steps from full left to full right. */
#define PAN_STEPS 16

/* A request in a queue with the sound that it plays. */
typedef struct {
    SoundRequest r;
    Sound sound;
} Pending;

/* A sound that waits for its timeline event. */
typedef struct {
    bool used;
    uint8_t category, index, entry, map, x, y;
    int16_t priority, volume;
} Delayed;

/* Lateral to forward ratios (<< 11) that start each pan step, from the side to the front (table of SKULL.EXE). */
static const uint16_t pan_thresholds[PAN_STEPS / 2] = {
    0x25a0, 0x11f0, 0x0b00, 0x0730, 0x0490, 0x0290, 0x00d0, 0x0000,
};

/* SOS pan values from full left to full right (table of SKULL.EXE). */
static const uint16_t pan_values[PAN_STEPS] = {
    0x0800, 0x1700, 0x2600, 0x3500, 0x4400, 0x5300, 0x6200, 0x7100,
    0x8f00, 0x9e00, 0xad00, 0xbc00, 0xcb00, 0xda00, 0xe900, 0xf800,
};

SoundRequest g_sound_played[SOUND_PLAYED_MAX];
int g_sound_played_count;
SoundStart g_sound_started[SOUND_PLAYED_MAX];
int g_sound_started_count;

/* The queues of the game step and the deferred requests, and the delayed sounds. */
static struct {
    Pending requests[REQUESTS_MAX];
    int request_count;
    Pending deferred[DEFERRED_MAX];
    int deferred_count;
    Delayed delayed[DELAYED_MAX];
} queues;

/* Setup */

void sound_reset_requests(void)
{
    queues.request_count = 0;
    queues.deferred_count = 0;
    memset(queues.delayed, 0, sizeof queues.delayed);
}

void sound_clear_requests(void)
{
    queues.request_count = 0;
}

/* Queries */

int sound_load(const gdat_t *g, int category, int index, int entry, Sound *out)
{
    uint32_t size;
    int item = gdat_find(g, category, index, GDAT_TYPE_SOUND, entry);
    const uint8_t *p = item < 0 ? NULL : gdat_item(g, item, &size);
    if (!p)
        return GDAT_ERR_ITEM;
    if (size <= SAMPLE_HEADER)
        return GDAT_ERR_FORMAT;
    out->data = (const int8_t *)p + SAMPLE_HEADER;
    out->length = size - SAMPLE_HEADER;
    return GDAT_OK;
}

/* Moves one coordinate away from the party by scale/1024, rounded, keeping its sign. */
static int push_away(int c, int scale)
{
    if (c < 0)
        return -((-c * scale + SCALE_ROUND) >> SCALE_SHIFT);
    return (c * scale + SCALE_ROUND) >> SCALE_SHIFT;
}

bool sound_party_offset(int dx, int dy, int party_dir, int path_distance, SoundOffset *out)
{
    int lat;
    int fwd;
    switch (party_dir) {
    case DIR_EAST:
        lat = dy;
        fwd = dx;
        break;
    case DIR_SOUTH:
        lat = -dx;
        fwd = dy;
        break;
    case DIR_WEST:
        lat = -dy;
        fwd = -dx;
        break;
    default:
        lat = dx;
        fwd = -dy;
        break;
    }
    int dist = abs(lat) + abs(fwd);
    if (dist > 1) {
        if (path_distance < 0)
            return false;
        if (dist < path_distance) {
            int scale = (path_distance << SCALE_SHIFT) / dist;
            lat = push_away(lat, scale);
            fwd = push_away(fwd, scale);
        }
    }
    out->lateral = lat;
    out->forward = fwd;
    return true;
}

void sound_volume_pan(uint8_t volume, int8_t lateral, int8_t forward, int *loudness, int *pan)
{
    *loudness = ((volume << VOLUME_SHIFT) / (forward * forward + lateral * lateral + DISTANCE_BIAS)) >> LOUDNESS_SHIFT;
    if (lateral == 0) {
        *pan = SOUND_PAN_CENTER;
    } else if (forward == 0) {
        *pan = pan_values[lateral < 0 ? 0 : PAN_STEPS - 1];
    } else {
        /* Fix: the original keeps only 16 bits of the ratio and pans |lateral| >= 32 near the center. */
        int ratio = (abs(lateral) << PAN_RATIO_SHIFT) / abs(forward);
        int i = 0;
        while (pan_thresholds[i] > ratio)
            i++;
        *pan = pan_values[lateral < 0 ? i : PAN_STEPS - 1 - i];
    }
}

/* Requests */

/* True when request a plays before request b: a higher priority, then a louder sound. */
static bool has_priority(const Pending *a, int loud_a, const Pending *b, int loud_b)
{
    if (a->r.priority != b->r.priority)
        return a->r.priority > b->r.priority;
    return loud_a >= loud_b;
}

/* Starts the sounds of a request list, the most important first, while voices are free. */
static void start_requests(const Pending *list, int count)
{
    int order[REQUESTS_MAX], loudness[REQUESTS_MAX], pan[REQUESTS_MAX];
    for (int i = 0; i < count; i++) {
        if (g_sound_played_count < SOUND_PLAYED_MAX)
            g_sound_played[g_sound_played_count++] = list[i].r;
        sound_volume_pan(list[i].r.volume, list[i].r.lateral, list[i].r.forward, &loudness[i], &pan[i]);
        order[i] = i;
    }
    for (bool swapped = true; swapped;) {
        swapped = false;
        for (int i = 1; i < count; i++)
            if (!has_priority(&list[order[i - 1]], loudness[order[i - 1]], &list[order[i]], loudness[order[i]])) {
                int t = order[i - 1];
                order[i - 1] = order[i];
                order[i] = t;
                swapped = true;
            }
    }
    for (int i = 0; i < count; i++) {
        const Pending *p = &list[order[i]];
        if (!g_hooks.play_sound(&p->sound, p->r.volume, p->r.lateral, p->r.forward))
            return;
        if (g_sound_started_count < SOUND_PLAYED_MAX)
            g_sound_started[g_sound_started_count++] = (SoundStart){p->sound.data, loudness[order[i]], pan[order[i]]};
    }
}

/* Keeps a sound for later: a timeline event plays it after delay ticks. */
static void request_later(const SoundCue *cue, int x, int y, int delay)
{
    int slot = 0;
    while (slot < DELAYED_MAX && queues.delayed[slot].used)
        slot++;
    if (slot == DELAYED_MAX)
        return;
    queues.delayed[slot] = (Delayed){true,
                                     (uint8_t)cue->category,
                                     (uint8_t)cue->index,
                                     (uint8_t)cue->entry,
                                     (uint8_t)g_party.cur_map,
                                     (uint8_t)x,
                                     (uint8_t)y,
                                     (int16_t)cue->priority,
                                     (int16_t)cue->volume};
    TimelineEvent e = {.time_map = timeline_time_map((g_game.game_time + delay - 1) & EVENT_TIME_MASK, g_party.cur_map),
                       .type = EVENT_DELAYED_SOUND,
                       .priority = (uint8_t)cue->priority};
    e.data[0] = (uint8_t)slot; /* fix: the original leaves data bytes 2 to 5 uninitialized */
    timeline_add_event(&e);
}

void sound_request(SoundCue cue, int16_t x, int16_t y, int delay)
{
    delay = (int16_t)delay;
    if (delay > 0 && g_party.cur_map != g_party.square_map && g_party.cur_map != g_party.alt_view_map)
        return;
    if (queues.request_count == REQUESTS_MAX)
        return;
    Sound sound;
    if (sound_load(gdat_current(), cue.category, cue.index, cue.entry, &sound) != GDAT_OK)
        return;
    cue.volume = (int16_t)cue.volume;
    if (g_party_sleeping)
        cue.volume >>= 1;
    if (delay > DELAY_STEP) {
        request_later(&cue, x, y, delay);
        return;
    }
    int map_x = x;
    int map_y = y;
    if (delay > 0) { /* a sound on the other map of a seamless passage */
        x += dungeon_map_offset_x(g_party.cur_map) - dungeon_map_offset_x(g_game.party_map);
        y += dungeon_map_offset_y(g_party.cur_map) - dungeon_map_offset_y(g_game.party_map);
    }
    int dx = (int16_t)x - g_game.party_x;
    int dy = (int16_t)y - g_game.party_y;
    SoundOffset offset;
    sound_party_offset(dx, dy, g_game.party_dir, 0, &offset);
    Pending *queue = queues.requests;
    int *count = &queues.request_count;
    if (delay < 0) {
        if (queues.deferred_count == DEFERRED_MAX)
            return;
        queue = queues.deferred;
        count = &queues.deferred_count;
    }
    for (int i = 0; i < *count; i++)
        if (queue[i].sound.data == sound.data && queue[i].r.lateral == offset.lateral &&
            queue[i].r.forward == offset.forward)
            return;
    if (abs(offset.lateral) + abs(offset.forward) > 1) {
        int path = creature_get_party_distance(map_x, map_y);
        if (!sound_party_offset(dx, dy, g_game.party_dir, path, &offset))
            return;
    }
    Pending *p = &queue[*count]; /* a sound of delay 0 uses the next free entry for itself */
    *p = (Pending){
        {(uint8_t)cue.category, (uint8_t)cue.index, (uint8_t)cue.entry, (uint8_t)cue.priority, (uint8_t)cue.volume,
         (int8_t)offset.lateral, (int8_t)offset.forward},
        sound
    };
    if (delay == DELAY_NOW)
        start_requests(p, 1);
    else
        (*count)++;
}

void sound_request_with_fallback(SoundCue cue, int fallback, int16_t x, int16_t y, int delay)
{
    Sound sound;
    if (sound_load(gdat_current(), cue.category, cue.index, cue.entry, &sound) != GDAT_OK)
        cue.index = fallback;
    sound_request(cue, x, y, delay);
}

void sound_request_delayed(int16_t slot)
{
    Delayed *d = &queues.delayed[slot & (DELAYED_MAX - 1)]; /* fix: a slot from the save stays in the table */
    int map = (int8_t)d->map;
    if (map == g_party.square_map || map == g_party.alt_view_map)
        sound_request((SoundCue){d->category, d->index, d->entry, d->priority, d->volume}, (int8_t)d->x, (int8_t)d->y,
                      DELAY_STEP);
    d->used = false;
}

void sound_flush_requests(int queue)
{
    if (queue) {
        if (queues.deferred_count > 0)
            start_requests(queues.deferred, queues.deferred_count);
        queues.deferred_count = 0;
        return;
    }
    if (queues.request_count > 0)
        start_requests(queues.requests, queues.request_count);
    queues.request_count = 0;
}
