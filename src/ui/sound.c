/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "sound.h"
#include <stdlib.h>
#include "gdat.h"
#include "music.h"
#include "save.h"
#include "sound_request.h"

/* The output frames of one sample. */
#define UPSAMPLE (SOUND_OUTPUT_RATE / SOUND_RATE)

/* The scale of the stereo pan: 0 is the left side, PAN_FULL the right side. */
#define PAN_FULL 0x10000

/* SOS volume (0-0x7fff) from loudness, and the mixer gain shift; 8-bit samples scale to 16 bits. */
#define SOS_VOLUME_SHIFT 7
#define GAIN_SHIFT       15
#define SAMPLE_SCALE     256

/* The test sound of a new sound volume. */
#define TEST_SOUND_INDEX 0

typedef struct {
    const int8_t *data;
    uint32_t end; /* length in output frames */
    uint32_t pos; /* output frame */
    int left, right;
} Voice;

static Voice voices[SOUND_VOICES];
static int master_volume = VOLUME_SETTING_MAX;

/* Settings */

void sound_set_volume(int level)
{
    master_volume = level < 0 ? 0 : level > VOLUME_SETTING_MAX ? VOLUME_SETTING_MAX : level;
}

void sound_set_volume_and_test(int level, int device)
{
    if (device == DEVICE_SOUND) {
        save_set_sound_volume((uint8_t)level);
        sndreq_request((SoundCue){CAT_DUNGEON, TEST_SOUND_INDEX, SOUND_ENTRY_TRANSFORM, SOUND_PRIORITY_TRANSFORM,
                                  SOUND_VOLUME_LOUD},
                       g_game.party_x, g_game.party_y, SOUND_NOW);
        sound_set_volume(level);
    } else if (device == DEVICE_MUSIC) {
        save_set_music_volume((uint8_t)level);
        music_set_volume(level);
    }
}

/* Voices */

bool sound_play(const Sound *s, uint8_t volume, int8_t lateral, int8_t forward)
{
    SoundLevel level = sndreq_volume_pan(volume, lateral, forward);
    for (int i = 0; i < SOUND_VOICES; i++) {
        Voice *v = &voices[i];
        if (v->data)
            continue;
        int sos_volume = level.loudness << SOS_VOLUME_SHIFT | level.loudness >> 1;
        /* Linear balance: the center plays at full volume on both sides. */
        int half = PAN_FULL / 2;
        v->left = sos_volume * (PAN_FULL - level.pan < half ? PAN_FULL - level.pan : half) / half;
        v->right = sos_volume * (level.pan < half ? level.pan : half) / half;
        v->data = s->data;
        v->end = s->length * UPSAMPLE;
        v->pos = 0;
        return true;
    }
    return false;
}

void sound_stop_all(void)
{
    for (int i = 0; i < SOUND_VOICES; i++)
        voices[i].data = NULL;
}

static int16_t clamp16(int s)
{
    return s < INT16_MIN ? INT16_MIN : s > INT16_MAX ? INT16_MAX : s;
}

void sound_mix(int16_t *stereo, int frames)
{
    for (int f = 0; f < frames; f++) {
        int left = stereo[2 * f];
        int right = stereo[2 * f + 1];
        for (int i = 0; i < SOUND_VOICES; i++) {
            Voice *v = &voices[i];
            if (!v->data)
                continue;
            int s = v->data[v->pos / UPSAMPLE] * SAMPLE_SCALE * master_volume / VOLUME_SETTING_MAX;
            left += s * v->left >> GAIN_SHIFT;
            right += s * v->right >> GAIN_SHIFT;
            if (++v->pos == v->end)
                v->data = NULL;
        }
        stereo[2 * f] = clamp16(left);
        stereo[2 * f + 1] = clamp16(right);
    }
}
