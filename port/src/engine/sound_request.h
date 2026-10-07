#ifndef SOUND_REQUEST_H
#define SOUND_REQUEST_H
#include <stdint.h>

/* A sound that the game asks for: the GDAT sound (category, index, entry), its priority, its volume, and its place
   seen by the party (lateral: right is positive). */
typedef struct {
    uint8_t category, index, entry, priority, volume;
    int8_t lateral, forward;
} SoundRequest;

/* The requests that went to the voices, in order, for the tests. */
#define SOUND_PLAYED_MAX 64
extern SoundRequest sound_played[SOUND_PLAYED_MAX];
extern int sound_played_count;
/* The sounds that started in a voice, in start order, with their loudness and stereo pan, for the tests. */
typedef struct {
    const int8_t *data;
    int loudness, pan;
} SoundStart;
extern SoundStart sound_started[SOUND_PLAYED_MAX];
extern int sound_started_count;

/* The GDAT index of the generic sound of a category: it plays when an index has no sound of its own. */
#define SOUND_FALLBACK 0xfe
/* Volumes of a sound request. */
#define SOUND_VOLUME_NORMAL 0x80
#define SOUND_VOLUME_LOUD 200
#define SOUND_VOLUME_MAX 0xff
/* Sounds of champions and creatures: the entry and the priority. */
#define SOUND_HURT 0x82
#define SOUND_PRIORITY_HURT 0x69
#define SOUND_ENTRY_SWALLOW 0x83        /* a champion eats or drinks */
#define SOUND_PRIORITY_SWALLOW 0x96
#define SOUND_PRIORITY_CREATURE 0x46    /* the steps and cries of a creature, a creature that a door crushes */

/* Asks for sound (category, index, entry) at (x, y) of the current map: delay 0 plays it now, 1 at the end of the
   game step, a negative delay at the next sound_flush_requests(1), a longer delay through the timeline. */
void sound_request(int category, int index, int entry, int priority, int volume, int x, int y, int delay);
/* sound_request with the sound of index fallback when index has no such sound. */
void sound_play_with_fallback(int category, int index, int entry, int fallback, int x, int y, int delay, int priority,
                              int volume);
/* Plays the requests of the game step (queue 0) or the deferred requests (queue 1). */
void sound_flush_requests(int queue);
/* Clears all queues and delayed sounds: the state of the program start. */
void sound_reset_requests(void);
/* Drops the requests of the game step: a map change loads other sounds. */
void sound_clear_requests(void);
/* A delayed sound (timeline event 0x15) is due: it plays when the party is still on its map. */
void sound_play_delayed(int slot);

#endif
