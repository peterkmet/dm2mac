/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The sound requests of the game: the queues of a game step, delayed sounds, and the place of a sound seen by the
   party. sound.c plays them. */
#include <stdlib.h>
#include <string.h>
#include "champion.h"
#include "ai.h"
#include "gdat.h"
#include "dungeon.h"
#include "hooks.h"
#include "move.h"
#include "save.h"
#include "sensor.h"
#include "sound_request.h"

#define REQUESTS_MAX 20         /* requests of a game step */
#define DEFERRED_MAX 6
#define DELAYED_MAX 8
#define DELAY_NOW 0
#define DELAY_STEP 1            /* at the end of the game step */

#define SAMPLE_HEADER 6 /* u16 rate, then 08 01 00 00 */

/* Distance scaling and volume in 10-bit and 8-bit fixed point. */
#define SCALE_SHIFT 10
#define SCALE_ROUND (1 << (SCALE_SHIFT - 1))
#define VOLUME_SHIFT 8
#define LOUDNESS_SHIFT 5
#define DISTANCE_BIAS 8
#define PAN_RATIO_SHIFT 11

#define PAN_STEPS 16

/* Lateral to forward ratios (<< 11) that start each pan step, from the side to the front (table of SKULL.EXE). */
static const uint16_t pan_thresholds[PAN_STEPS / 2] = {
    0x25a0, 0x11f0, 0x0b00, 0x0730, 0x0490, 0x0290, 0x00d0, 0x0000,
};

/* SOS pan values from full left to full right (table of SKULL.EXE). */
static const uint16_t pan_values[PAN_STEPS] = {
    0x0800, 0x1700, 0x2600, 0x3500, 0x4400, 0x5300, 0x6200, 0x7100,
    0x8f00, 0x9e00, 0xad00, 0xbc00, 0xcb00, 0xda00, 0xe900, 0xf800,
};

int sound_load(const gdat_t *g, int category, int index, int entry, Sound *out)
{
    uint32_t size;
    int item = gdat_find(g, category, index, GDAT_TYPE_SOUND, entry);
    const uint8_t *p = item < 0 ? NULL : gdat_item(g, item, &size);
    if (!p) return GDAT_ERR_ITEM;
    if (size <= SAMPLE_HEADER) return GDAT_ERR_FORMAT;
    out->data = (const int8_t *)p + SAMPLE_HEADER;
    out->length = size - SAMPLE_HEADER;
    return GDAT_OK;
}

/* Moves one coordinate away from the party by scale/1024, rounded, keeping its sign. */
static int scale_away(int c, int scale)
{
    if (c < 0) return -((-c * scale + SCALE_ROUND) >> SCALE_SHIFT);
    return (c * scale + SCALE_ROUND) >> SCALE_SHIFT;
}

int sound_party_offset(int x, int y, int party_x, int party_y, int party_dir, int path_distance,
                       int *lateral, int *forward)
{
    int lat, fwd;
    switch (party_dir) {
    case DIR_EAST:  lat = y - party_y; fwd = x - party_x; break;
    case DIR_SOUTH: lat = party_x - x; fwd = y - party_y; break;
    case DIR_WEST:  lat = party_y - y; fwd = party_x - x; break;
    default:        lat = x - party_x; fwd = party_y - y; break;
    }
    int dist = abs(lat) + abs(fwd);
    if (dist > 1) {
        if (path_distance < 0) return 0;
        if (dist < path_distance) {
            int scale = (path_distance << SCALE_SHIFT) / dist;
            lat = scale_away(lat, scale);
            fwd = scale_away(fwd, scale);
        }
    }
    *lateral = lat;
    *forward = fwd;
    return 1;
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
        while (pan_thresholds[i] > ratio) i++;
        *pan = pan_values[lateral < 0 ? i : PAN_STEPS - 1 - i];
    }
}

typedef struct {
    SoundRequest r;
    Sound sound;
} Pending;

typedef struct {
    int used;
    uint8_t category, index, entry, map, x, y;
    int16_t priority, volume;
} Delayed;

SoundRequest sound_played[SOUND_PLAYED_MAX];
int sound_played_count;
SoundStart sound_started[SOUND_PLAYED_MAX];
int sound_started_count;

static Pending requests[REQUESTS_MAX], deferred[DEFERRED_MAX];
static int request_count, deferred_count;
static Delayed delayed[DELAYED_MAX];

/* True when request a plays before request b: a higher priority, then a louder sound. */
static int has_priority(const Pending *a, int loud_a, const Pending *b, int loud_b)
{
    if (a->r.priority != b->r.priority) return a->r.priority > b->r.priority;
    return loud_a >= loud_b;
}

/* Starts the sounds of a request list, the most important first, while voices are free. */
static void sound_play_requests(const Pending *list, int count)
{
    int order[REQUESTS_MAX], loudness[REQUESTS_MAX], pan[REQUESTS_MAX];
    for (int i = 0; i < count; i++) {
        if (sound_played_count < SOUND_PLAYED_MAX) sound_played[sound_played_count++] = list[i].r;
        sound_volume_pan(list[i].r.volume, list[i].r.lateral, list[i].r.forward, &loudness[i], &pan[i]);
        order[i] = i;
    }
    for (int swapped = 1; swapped;) {
        swapped = 0;
        for (int i = 1; i < count; i++)
            if (!has_priority(&list[order[i - 1]], loudness[order[i - 1]], &list[order[i]], loudness[order[i]])) {
                int t = order[i - 1];
                order[i - 1] = order[i];
                order[i] = t;
                swapped = 1;
            }
    }
    for (int i = 0; i < count; i++) {
        const Pending *p = &list[order[i]];
        if (!g_hooks.play_sound(&p->sound, p->r.volume, p->r.lateral, p->r.forward)) return;
        if (sound_started_count < SOUND_PLAYED_MAX)
            sound_started[sound_started_count++] = (SoundStart){p->sound.data, loudness[order[i]], pan[order[i]]};
    }
}

/* Keeps a sound for later: a timeline event plays it after delay ticks. */
static void sound_request_later(int category, int index, int entry, int priority, int volume, int x, int y, int delay)
{
    int slot = 0;
    while (slot < DELAYED_MAX && delayed[slot].used) slot++;
    if (slot == DELAYED_MAX) return;
    delayed[slot] = (Delayed){1, (uint8_t)category, (uint8_t)index, (uint8_t)entry, (uint8_t)party.cur_map, (uint8_t)x,
                              (uint8_t)y, (int16_t)priority, (int16_t)volume};
    TimelineEvent e = {.time_map = timeline_time_map((game.game_time + delay - 1) & EVENT_TIME_MASK, party.cur_map),
                       .type = EVENT_DELAYED_SOUND, .priority = (uint8_t)priority};
    e.data[0] = (uint8_t)slot;  /* fix: the original leaves data bytes 2 to 5 uninitialized */
    timeline_add_event(&e);
}

void sound_request(int category, int index, int entry, int priority, int volume, int x, int y, int delay)
{
    delay = (int16_t)delay;
    if (delay > 0 && party.cur_map != party.square_map && party.cur_map != party.alt_view_map) return;
    if (request_count == REQUESTS_MAX) return;
    Sound sound;
    if (sound_load(gdat_current(), category, index, entry, &sound) != GDAT_OK) return;
    volume = (int16_t)volume;
    if (party_sleeping) volume >>= 1;
    if (delay > DELAY_STEP) {
        sound_request_later(category, index, entry, priority, volume, x, y, delay);
        return;
    }
    int map_x = x, map_y = y;
    if (delay > 0) {  /* a sound on the other map of a seamless passage */
        x += dungeon_map_offset_x(party.cur_map) - dungeon_map_offset_x(game.party_map);
        y += dungeon_map_offset_y(party.cur_map) - dungeon_map_offset_y(game.party_map);
    }
    int lateral, forward;
    sound_party_offset((int16_t)x, (int16_t)y, game.party_x, game.party_y, game.party_dir, 0, &lateral, &forward);
    Pending *queue = requests;
    int *count = &request_count;
    if (delay < 0) {
        if (deferred_count == DEFERRED_MAX) return;
        queue = deferred;
        count = &deferred_count;
    }
    for (int i = 0; i < *count; i++)
        if (queue[i].sound.data == sound.data && queue[i].r.lateral == lateral && queue[i].r.forward == forward) return;
    if (abs(lateral) + abs(forward) > 1) {
        int path = creature_get_party_distance(map_x, map_y);
        if (!sound_party_offset((int16_t)x, (int16_t)y, game.party_x, game.party_y, game.party_dir, path, &lateral, &forward))
            return;
    }
    Pending *p = &queue[*count];  /* a sound of delay 0 uses the next free entry for itself */
    *p = (Pending){{(uint8_t)category, (uint8_t)index, (uint8_t)entry, (uint8_t)priority, (uint8_t)volume,
                    (int8_t)lateral, (int8_t)forward}, sound};
    if (delay == DELAY_NOW) sound_play_requests(p, 1);
    else (*count)++;
}

void sound_request_with_fallback(int category, int index, int entry, int fallback, int x, int y, int delay, int priority,
                              int volume)
{
    Sound sound;
    if (sound_load(gdat_current(), category, index, entry, &sound) != GDAT_OK) index = fallback;
    sound_request(category, index, entry, priority, volume, x, y, delay);
}

void sound_flush_requests(int queue)
{
    if (queue) {
        if (deferred_count > 0) sound_play_requests(deferred, deferred_count);
        deferred_count = 0;
        return;
    }
    if (request_count > 0) sound_play_requests(requests, request_count);
    request_count = 0;
}

void sound_reset_requests(void)
{
    request_count = deferred_count = 0;
    memset(delayed, 0, sizeof delayed);
}

void sound_clear_requests(void)
{
    request_count = 0;
}

void sound_request_delayed(int slot)
{
    Delayed *d = &delayed[slot & (DELAYED_MAX - 1)];  /* fix: a slot from the save stays in the table */
    int map = (int8_t)d->map;
    if (map == party.square_map || map == party.alt_view_map)
        sound_request(d->category, d->index, d->entry, d->priority, d->volume, (int8_t)d->x, (int8_t)d->y, DELAY_STEP);
    d->used = 0;
}
