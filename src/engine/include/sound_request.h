/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_SOUND_REQUEST_H
#define DM2_ENGINE_SOUND_REQUEST_H
/* Prefixes: sound_clear_ sound_flush_ sound_load sound_party_ sound_request sound_reset_ sound_volume_ */

/* The sound requests of the game rules: the queues of a game step and the delayed sounds. */

#include "gdat.h"
#include <stdbool.h>
#include <stdint.h>

/* The stereo pan of a sound straight ahead or behind. */
#define SOUND_PAN_CENTER 0x8000

/* The GDAT index of the generic sound of a category: it plays when an index has no sound of its own. */
#define SOUND_FALLBACK           0xfe
/* Volumes of a sound request. */
#define SOUND_VOLUME_NORMAL      0x80
#define SOUND_VOLUME_LOUD        0xc8
#define SOUND_VOLUME_FULL        0xff
/* Sounds of champions and creatures: the entry and the priority. */
#define SOUND_HURT               0x82
#define SOUND_PRIORITY_HURT      0x69
#define SOUND_ENTRY_SWALLOW      0x83 /* a champion eats or drinks */
#define SOUND_PRIORITY_SWALLOW   0x96
#define SOUND_PRIORITY_CREATURE  0x46 /* the steps and cries of a creature, a creature that a door crushes */
/* Sounds of clicks and switches: the entry and the priority. */
#define SOUND_ENTRY_CLICK        0x84 /* a click on a wall or on a creature that cannot move */
#define SOUND_ENTRY_ACTIVATE     0x88 /* an ornament, a door button, or a warp marker works */
#define SOUND_PRIORITY_CLICK     0x8c
/* Sound (CAT_DUNGEON, 0, SOUND_ENTRY_TRANSFORM): a creature transforms; the volume dialog plays it as its test sound. */
#define SOUND_ENTRY_TRANSFORM    0x8b
#define SOUND_PRIORITY_TRANSFORM 0x6c

/* The requests that went to the voices, for the tests. */
#define SOUND_PLAYED_MAX 64

/* Signed 8-bit mono PCM of a GDAT sound item. */
typedef struct {
    const int8_t *data;
    uint32_t length;
} Sound;

/* A sound that a rule asks for: the GDAT sound (category, index, entry), its priority, and its volume. */
typedef struct {
    int category, index, entry, priority, volume;
} SoundCue;

/* The place of a sound seen by the party: lateral (right is positive) and forward. */
typedef struct {
    int lateral, forward;
} SoundOffset;

/* A sound that the game asks for: the GDAT sound (category, index, entry), its priority, its volume, and its place
   seen by the party (lateral: right is positive). */
typedef struct {
    uint8_t category, index, entry, priority, volume;
    int8_t lateral, forward;
} SoundRequest;

/* The sounds that started in a voice, in start order, with their loudness and stereo pan, for the tests. */
typedef struct {
    const int8_t *data;
    int loudness, pan;
} SoundStart;

/* The requests that went to the voices, in order. */
extern SoundRequest g_sound_played[SOUND_PLAYED_MAX]; /* exported for the tests */
extern int g_sound_played_count;                      /* exported for the tests */
extern SoundStart g_sound_started[SOUND_PLAYED_MAX];  /* exported for the tests */
extern int g_sound_started_count;                     /* exported for the tests */

/* Setup */
/* Clears all queues and delayed sounds: the state of the program start. */
void sound_reset_requests(void); /* exported for the tests */
/* Drops the requests of the game step: a map change loads other sounds. */
void sound_clear_requests(void);

/* Queries */
/* Finds sound (category, index, entry) in GRAPHICS.DAT; the data stays in g. */
int sound_load(const gdat_t *g, int category, int index, int entry, Sound *sound); /* exported for the tests */
/* The place of a sound at (dx, dy) from the party seen by the party facing party_dir. A sound that comes around
   walls (path_distance above the straight distance) is pushed further away. path_distance is the party distance
   map value at the square of the sound; false when it is negative (no path). Also exported for the tests. */
bool sound_party_offset(int dx, int dy, int party_dir, int path_distance, SoundOffset *offset);
/* The loudness (0-255) and stereo pan (0x0800 left to 0xf800 right) of a sound at lateral, forward. */
void sound_volume_pan(uint8_t volume, int lateral, int forward, int *loudness, int *pan);

/* Requests */
/* Asks for the sound of cue at (x, y) of the current map: delay 0 plays it now, 1 at the end of the game step, a
   negative delay at the next sound_flush_requests(1), a longer delay through the timeline. */
void sound_request(SoundCue cue, int16_t x, int16_t y, int delay);
/* sound_request with the sound of index fallback when the index of cue has no such sound. */
void sound_request_with_fallback(SoundCue cue, int fallback, int16_t x, int16_t y, int delay);
/* A delayed sound (EVENT_DELAYED_SOUND) is due: it plays when the party is still on its map. */
void sound_request_delayed(int16_t slot);
/* Plays the requests of the game step (queue 0) or the deferred requests (queue 1). */
void sound_flush_requests(int queue);

#endif /* DM2_ENGINE_SOUND_REQUEST_H */
