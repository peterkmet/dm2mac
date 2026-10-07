#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>
#include "gdat.h"

#define SOUND_RATE 11025        /* the SOS driver mix rate; the samples play at this rate */
#define SOUND_OUTPUT_RATE 44100 /* the port mixes at 4 times the sample rate */
#define SOUND_VOICES 64
#define SOUND_PAN_CENTER 0x8000
#define VOLUME_SETTING_MAX 7   /* the sound and music volume settings: 0 to 7 */

/* Signed 8-bit mono PCM of a GDAT sound item. */
typedef struct {
    const int8_t *data;
    uint32_t length;
} Sound;

/* Finds sound (category, index, entry) in GRAPHICS.DAT; the data stays in g. */
int sound_load(const gdat_t *g, int category, int index, int entry, Sound *out);
/* The position of a sound at (x, y) seen by the party: lateral (right is positive) and forward.
   A sound that comes around walls (path_distance above the straight distance) is pushed further away.
   path_distance is the party distance map value at the original (x, y); returns 0 when it is negative (no path). */
int sound_party_offset(int x, int y, int party_x, int party_y, int party_dir, int path_distance,
                       int *lateral, int *forward);
/* The loudness (0-255) and stereo pan (0x0800 left to 0xf800 right) of a sound at lateral, forward. */
void sound_volume_pan(uint8_t volume, int8_t lateral, int8_t forward, int *loudness, int *pan);
/* Starts a sound in the first free voice; returns 0 when all voices play. */
int sound_play(const Sound *s, uint8_t volume, int8_t lateral, int8_t forward);
/* The master volume of the voices, 0 (off) to 7 (the sound volume setting). */
void sound_set_volume(int level);
/* Stops all voices. */
void sound_stop_all(void);
/* Adds the playing voices to frames of interleaved stereo at SOUND_OUTPUT_RATE. */
void sound_mix(int16_t *stereo, int frames);

#endif
