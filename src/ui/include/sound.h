/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_SOUND_H
#define DM2_UI_SOUND_H
/* Prefixes: sound_ */

/* Sound output: the sample mixer, the voices and the volume settings. */

#include <stdbool.h>
#include <stdint.h>
#include "sound_request.h"

/* The sample rates, the voices and the volume settings of the sound output. */
#define SOUND_RATE         11025 /* the SOS driver mix rate; the samples play at this rate */
#define SOUND_OUTPUT_RATE  44100 /* the port mixes at 4 times the sample rate */
#define SOUND_VOICES       64
#define VOLUME_SETTING_MAX 7 /* the sound and music volume settings: 0 to 7 */

/* The devices of sound_set_volume_and_test. */
#define DEVICE_SOUND 0
#define DEVICE_MUSIC 10

/* Settings */
/* The master volume of the voices, 0 (off) to 7 (the sound volume setting). */
void sound_set_volume(int level);
/* Sets the sound (DEVICE_SOUND, plays a test sound) or music (DEVICE_MUSIC) volume setting, 0 to 7. */
void sound_set_volume_and_test(int level, int device);

/* Voices */
/* Starts a sound in the first free voice; false when all voices play. */
bool sound_play(const Sound *s, uint8_t volume, int8_t lateral, int8_t forward);
/* Stops all voices. */
void sound_stop_all(void);
/* Adds the playing voices to frames of interleaved stereo at SOUND_OUTPUT_RATE. */
void sound_mix(int16_t *stereo, int frames);

#endif /* DM2_UI_SOUND_H */
