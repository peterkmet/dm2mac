/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdlib.h>
#include "sound.h"

#define UPSAMPLE (SOUND_OUTPUT_RATE / SOUND_RATE)

#define PAN_FULL 0x10000

/* SOS volume (0-0x7fff) from loudness, and the mixer gain shift for 8-bit samples. */
#define SOS_VOLUME_SHIFT 7
#define GAIN_SHIFT 15
#define SAMPLE_TO_S16 8

typedef struct {
    const int8_t *data;
    uint32_t end;  /* length in output frames */
    uint32_t pos;  /* output frame */
    int left, right;
} Voice;

static Voice voices[SOUND_VOICES];

int sound_play(const Sound *s, uint8_t volume, int8_t lateral, int8_t forward)
{
    int loudness, pan;
    sound_volume_pan(volume, lateral, forward, &loudness, &pan);
    for (int i = 0; i < SOUND_VOICES; i++) {
        Voice *v = &voices[i];
        if (v->data) continue;
        int sos_volume = loudness << SOS_VOLUME_SHIFT | loudness >> 1;
        /* Linear balance: the center plays at full volume on both sides. */
        int half = PAN_FULL / 2;
        v->left = sos_volume * (PAN_FULL - pan < half ? PAN_FULL - pan : half) / half;
        v->right = sos_volume * (pan < half ? pan : half) / half;
        v->data = s->data;
        v->end = s->length * UPSAMPLE;
        v->pos = 0;
        return 1;
    }
    return 0;
}

void sound_stop_all(void)
{
    for (int i = 0; i < SOUND_VOICES; i++) voices[i].data = NULL;
}

static int16_t clamp16(int s)
{
    return s < INT16_MIN ? INT16_MIN : s > INT16_MAX ? INT16_MAX : s;
}

static int master_volume = VOLUME_SETTING_MAX;

void sound_set_volume(int level)
{
    master_volume = level < 0 ? 0 : level > VOLUME_SETTING_MAX ? VOLUME_SETTING_MAX : level;
}

void sound_mix(int16_t *stereo, int frames)
{
    for (int f = 0; f < frames; f++) {
        int left = stereo[2 * f], right = stereo[2 * f + 1];
        for (int i = 0; i < SOUND_VOICES; i++) {
            Voice *v = &voices[i];
            if (!v->data) continue;
            int s = (v->data[v->pos / UPSAMPLE] << SAMPLE_TO_S16) * master_volume / VOLUME_SETTING_MAX;
            left += s * v->left >> GAIN_SHIFT;
            right += s * v->right >> GAIN_SHIFT;
            if (++v->pos == v->end) v->data = NULL;
        }
        stereo[2 * f] = clamp16(left);
        stereo[2 * f + 1] = clamp16(right);
    }
}
