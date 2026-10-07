#include <stdlib.h>
#include "sound.h"

#define SAMPLE_HEADER 6 /* u16 rate, then 08 01 00 00 */
#define UPSAMPLE (SOUND_OUTPUT_RATE / SOUND_RATE)

enum { DIR_NORTH, DIR_EAST, DIR_SOUTH, DIR_WEST };

/* Distance scaling and volume in 10-bit and 8-bit fixed point. */
#define SCALE_SHIFT 10
#define SCALE_ROUND (1 << (SCALE_SHIFT - 1))
#define VOLUME_SHIFT 8
#define LOUDNESS_SHIFT 5
#define DISTANCE_BIAS 8
#define PAN_RATIO_SHIFT 11

/* SOS volume (0-0x7fff) from loudness, and the mixer gain shift for 8-bit samples. */
#define SOS_VOLUME_SHIFT 7
#define GAIN_SHIFT 15
#define SAMPLE_TO_S16 8

#define PAN_STEPS 16
#define SOUND_VOLUME_MAX 7
#define PAN_FULL 0x10000

/* Lateral to forward ratios (<< 11) that start each pan step, from the side to the front (table of SKULL.EXE). */
static const uint16_t pan_thresholds[PAN_STEPS / 2] = {
    0x25a0, 0x11f0, 0x0b00, 0x0730, 0x0490, 0x0290, 0x00d0, 0x0000,
};

/* SOS pan values from full left to full right (table of SKULL.EXE). */
static const uint16_t pan_values[PAN_STEPS] = {
    0x0800, 0x1700, 0x2600, 0x3500, 0x4400, 0x5300, 0x6200, 0x7100,
    0x8f00, 0x9e00, 0xad00, 0xbc00, 0xcb00, 0xda00, 0xe900, 0xf800,
};

typedef struct {
    const int8_t *data;
    uint32_t end;  /* length in output frames */
    uint32_t pos;  /* output frame */
    int left, right;
} Voice;

static Voice voices[SOUND_VOICES];

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

static int master_volume = SOUND_VOLUME_MAX;

void sound_set_volume(int level)
{
    master_volume = level < 0 ? 0 : level > SOUND_VOLUME_MAX ? SOUND_VOLUME_MAX : level;
}

void sound_mix(int16_t *stereo, int frames)
{
    for (int f = 0; f < frames; f++) {
        int left = stereo[2 * f], right = stereo[2 * f + 1];
        for (int i = 0; i < SOUND_VOICES; i++) {
            Voice *v = &voices[i];
            if (!v->data) continue;
            int s = (v->data[v->pos / UPSAMPLE] << SAMPLE_TO_S16) * master_volume / SOUND_VOLUME_MAX;
            left += s * v->left >> GAIN_SHIFT;
            right += s * v->right >> GAIN_SHIFT;
            if (++v->pos == v->end) v->data = NULL;
        }
        stereo[2 * f] = clamp16(left);
        stereo[2 * f + 1] = clamp16(right);
    }
}
