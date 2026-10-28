/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "hmp.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "le.h"

/* HMP header. */
#define HMP_SIGNATURE "HMIMIDIP013195"

/* HMP header offsets. */
#define HMP_BRANCHES    32 /* u32: offset of the loop table */
#define HMP_TRACK_COUNT 48 /* u32 */
#define HMP_TICK_RATE   56 /* u32: song timer ticks per second */
#define HMP_FIRST_TRACK 904

/* Track counts of a song. */
#define HMP_MAX_TRACKS       32
#define TEMPO_AND_LAST_TRACK 2 /* track 0 and the last playing track of tracks 1 and up */

/* Track chunk: number, chunk size, MIDI channel, then delta times and events. */
#define TRACK_SIZE    4
#define TRACK_CHANNEL 8
#define TRACK_DATA    12

/* Loop table: a record count per track, then 24-byte records of all tracks in track order. */
#define BRANCH_SIZE       24
#define BRANCH_OFFSET     0 /* u32: loop point in the track data */
#define BRANCH_ID         4
#define BRANCH_PROGRAM    5
#define BRANCH_LOOPS      6 /* loops left, LOOPS_FOREVER for no end */
#define BRANCH_CTRL_BYTES 7
#define BRANCH_CTRLS      12 /* u32: offset of the controller and value pairs to send at the loop point */
#define LOOPS_FOREVER     255

/* MIDI and HMI event codes. */
#define MIDI_SYSTEM       0xf0
#define MIDI_META         0xff
#define META_END_OF_TRACK 0x2f
#define META_TEMPO        0x51
#define HMI_FIRST         0x67 /* controllers 0x67 to 0x79 are HMI commands, never sent to the driver */
#define HMI_LAST          0x79
#define HMI_SET_LOOPS     0x6d /* the value of the next event (an 0x6e controller) sets the loop count */
#define HMI_LOOP          0x6f /* all tracks jump to their loop point with the id of the value */
#define HMI_LOOP_2        0x70
#define VARLEN_LAST       0x80
#define VARLEN_BIT_MASK   0x1f

/* Event sizes and bit counts. */
#define META_TEMPO_LENGTH   5
#define MIDI_VOLUME_SHIFT   7
#define HMI_SET_LOOPS_VALUE 6
#define VARLEN_GROUP_WIDTH  7
#define STATUS_NIBBLES      16

typedef struct {
    uint32_t start, end; /* track data */
    uint32_t cursor;     /* next event, 0 when the track ended */
    uint32_t delta;      /* ticks from the last event to the next event */
    uint32_t elapsed;    /* ticks since the last event */
    uint32_t branches;   /* first loop record */
    int branch_count;
    uint8_t channel;
} Track;

/* Event length by status high nibble, and for system events by low nibble (tables of SKULL.EXE). */
static const uint8_t channel_event_length[STATUS_NIBBLES] = {0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3, 3, 2, 2, 3, 0};
static const uint8_t system_event_length[STATUS_NIBBLES] = {0, 1, 2, 1, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2};

static struct {
    uint8_t *song; /* owned copy of the playing song */
    size_t song_size;
    HmpSend send;
    Track tracks[HMP_MAX_TRACKS];
    int track_count;
    int active_tracks;
    bool playing;
    bool paused;
    bool muted;
    int song_volume;
    int master_volume;
    uint8_t channel_volume[MIDI_CHANNELS]; /* last volume of each channel; it stays from song to song */
} hmp = {
    .song_volume = MIDI_MAX,
    .master_volume = MIDI_MAX,
    .channel_volume = {MIDI_MAX, MIDI_MAX, MIDI_MAX, MIDI_MAX, MIDI_MAX, MIDI_MAX, MIDI_MAX, MIDI_MAX, MIDI_MAX,
                       MIDI_MAX, MIDI_MAX, MIDI_MAX, MIDI_MAX, MIDI_MAX, MIDI_MAX, MIDI_MAX},
};

/* Reads a delta time: 7-bit groups, low group first; the last byte has bit 7 set. */
static uint32_t read_delta(Track *tr)
{
    uint32_t value = 0;
    for (int shift = 0; tr->cursor < tr->end; shift += VARLEN_GROUP_WIDTH) {
        uint8_t b = hmp.song[tr->cursor++];
        value |= (uint32_t)(b & ~VARLEN_LAST) << (shift & VARLEN_BIT_MASK);
        if (b & VARLEN_LAST)
            break;
    }
    return value;
}

/* Sends an event; a channel volume is stored and scaled by the song and master volume. */
static void send_event(const uint8_t *event)
{
    if ((event[MIDI_STATUS] & MIDI_STATUS_MASK) != MIDI_CONTROL || event[MIDI_DATA1] != MIDI_CTRL_VOLUME) {
        hmp.send(event);
        return;
    }
    hmp.channel_volume[event[MIDI_STATUS] & MIDI_CHANNEL_MASK] = event[MIDI_DATA2];
    int volume = hmp.master_volume * (event[MIDI_DATA2] * hmp.song_volume >> MIDI_VOLUME_SHIFT) >> MIDI_VOLUME_SHIFT;
    uint8_t scaled[MIDI_EVENT_SIZE] = {event[MIDI_STATUS], event[MIDI_DATA1], (uint8_t)(hmp.muted ? 0 : volume)};
    hmp.send(scaled);
}

static void send3(int status, int data1, int data2)
{
    uint8_t event[MIDI_EVENT_SIZE] = {(uint8_t)status, (uint8_t)data1, (uint8_t)data2};
    hmp.send(event);
}

/* Sends the stored channel volume of each playing track again. */
static void resend_volumes(void)
{
    for (int t = 0; t < hmp.track_count; t++) {
        if (!hmp.tracks[t].cursor)
            continue;
        uint8_t event[MIDI_EVENT_SIZE] = {MIDI_CONTROL | hmp.tracks[t].channel, MIDI_CTRL_VOLUME,
                                          hmp.channel_volume[hmp.tracks[t].channel]};
        send_event(event);
    }
}

/* Notes off, controllers reset, pitch bend center, and volume 0 on the channels of tracks 1 and up. */
static void silence(void)
{
    for (int t = 1; t < hmp.track_count; t++) {
        int ch = hmp.tracks[t].channel;
        send3(MIDI_CONTROL | ch, MIDI_CTRL_NOTES_OFF, 0);
        send3(MIDI_CONTROL | ch, MIDI_CTRL_RESET, 0);
        send3(MIDI_PITCH_BEND | ch, BEND_CENTER, BEND_CENTER);
        send3(MIDI_CONTROL | ch, MIDI_CTRL_VOLUME, 0);
    }
}

/* Song playback */

enum HmpResult hmp_start(const uint8_t *data, size_t size, HmpSend send)
{
    hmp_stop();
    if (size < HMP_FIRST_TRACK || memcmp(data, HMP_SIGNATURE, strlen(HMP_SIGNATURE)) || !le32(data + HMP_TICK_RATE))
        return HMP_ERR_FORMAT;
    uint32_t count = le32(data + HMP_TRACK_COUNT);
    uint32_t branches = le32(data + HMP_BRANCHES);
    if (count < 1 || count > HMP_MAX_TRACKS || branches > size || size - branches < count)
        return HMP_ERR_FORMAT;
    uint32_t offset = HMP_FIRST_TRACK;
    uint32_t record = branches + count;
    for (uint32_t t = 0; t < count; t++) {
        if (size - offset < TRACK_DATA)
            return HMP_ERR_FORMAT;
        uint32_t chunk = le32(data + offset + TRACK_SIZE);
        if (chunk < TRACK_DATA || chunk > size - offset)
            return HMP_ERR_FORMAT;
        Track *tr = &hmp.tracks[t];
        *tr = (Track){.start = offset + TRACK_DATA,
                      .end = offset + chunk,
                      .branches = record,
                      .branch_count = data[branches + t],
                      .channel = data[offset + TRACK_CHANNEL] & MIDI_CHANNEL_MASK};
        for (int b = 0; b < tr->branch_count; b++, record += BRANCH_SIZE) {
            if (size - BRANCH_SIZE < record)
                return HMP_ERR_FORMAT;
            uint32_t ctrls = le32(data + record + BRANCH_CTRLS);
            uint32_t point = le32(data + record + BRANCH_OFFSET);
            if (ctrls > size || size - ctrls < data[record + BRANCH_CTRL_BYTES] || point >= chunk - TRACK_DATA)
                return HMP_ERR_FORMAT;
        }
        offset += chunk;
    }
    hmp.song = malloc(size);
    if (!hmp.song)
        return HMP_ERR_MEMORY;
    memcpy(hmp.song, data, size);
    hmp.song_size = size;
    hmp.send = send;
    hmp.track_count = (int)count;
    hmp.active_tracks = (int)count;
    for (int t = 0; t < hmp.track_count; t++) {
        hmp.tracks[t].cursor = hmp.tracks[t].start;
        hmp.tracks[t].delta = read_delta(&hmp.tracks[t]);
    }
    hmp.playing = true;
    hmp.paused = false;
    return HMP_OK;
}

void hmp_stop(void)
{
    if (hmp.playing)
        silence();
    hmp.playing = false;
    free(hmp.song);
    hmp.song = NULL;
    hmp.track_count = 0;
}

/* Finds the loop record of a track with a loop point id. */
static uint8_t *find_branch(int t, int id)
{
    for (int b = 0; b < hmp.tracks[t].branch_count; b++) {
        uint8_t *rec = hmp.song + hmp.tracks[t].branches + b * BRANCH_SIZE;
        if (rec[BRANCH_ID] == id)
            return rec;
    }
    return NULL;
}

/* Moves a track to its loop point id and sends the program and controllers of the loop point. */
static void jump(int t, int id)
{
    Track *tr = &hmp.tracks[t];
    const uint8_t *rec = find_branch(t, id);
    if (!rec)
        return; /* fix: the original searches past the loop records of the track */
    tr->cursor = tr->start + le32(rec + BRANCH_OFFSET);
    tr->delta = read_delta(tr);
    tr->elapsed = 0;
    if (tr->cursor >= tr->end)
        return;
    int ch = hmp.song[tr->cursor] & MIDI_CHANNEL_MASK;
    uint8_t program[MIDI_EVENT_SIZE] = {MIDI_PROGRAM | ch, rec[BRANCH_PROGRAM], 0};
    send_event(program);
    const uint8_t *pairs = hmp.song + le32(rec + BRANCH_CTRLS);
    for (int i = 0; i + 1 < rec[BRANCH_CTRL_BYTES]; i += 2) {
        uint8_t event[MIDI_EVENT_SIZE] = {MIDI_CONTROL | ch, pairs[i], pairs[i + 1]};
        send_event(event);
    }
}

/* Runs an HMI controller of track t; returns true when the tracks jumped to a loop point. */
static bool hmi_control(int t, const uint8_t *event)
{
    uint8_t *rec = find_branch(t, event[MIDI_DATA2]);
    if (!rec)
        return false;
    if (event[MIDI_DATA1] == HMI_SET_LOOPS) {
        if (hmp.tracks[t].cursor + HMI_SET_LOOPS_VALUE < hmp.song_size)
            rec[BRANCH_LOOPS] = hmp.song[hmp.tracks[t].cursor + HMI_SET_LOOPS_VALUE];
        return false;
    }
    if (event[MIDI_DATA1] != HMI_LOOP && event[MIDI_DATA1] != HMI_LOOP_2)
        return false;
    int loops = rec[BRANCH_LOOPS];
    if (loops != LOOPS_FOREVER && loops)
        loops = --rec[BRANCH_LOOPS];
    if (!loops)
        return false;
    for (int k = 1; k < hmp.track_count; k++)
        if (hmp.tracks[k].cursor)
            jump(k, event[MIDI_DATA2]);
    return true;
}

/* Ends a track; returns true when the song ended. */
static bool end_track(int t)
{
    hmp.tracks[t].cursor = 0;
    if (hmp.active_tracks == TEMPO_AND_LAST_TRACK && hmp.tracks[0].cursor) {
        hmp.active_tracks--;
        hmp.tracks[0].cursor = 0;
    }
    if (--hmp.active_tracks)
        return false;
    hmp.playing = false;
    silence();
    return true;
}

void hmp_tick(void)
{
    if (!hmp.playing || hmp.paused)
        return;
    for (int t = 0; t < hmp.track_count; t++) {
        Track *tr = &hmp.tracks[t];
        tr->elapsed++;
        if (!tr->cursor || tr->delta > tr->elapsed)
            continue;
        do {
            if (tr->cursor + MIDI_EVENT_SIZE > tr->end) { /* fix: a track without an end event ends at its chunk end */
                if (end_track(t))
                    return;
                break;
            }
            const uint8_t *event = hmp.song + tr->cursor;
            uint8_t status = event[MIDI_STATUS];
            uint32_t length = status < MIDI_SYSTEM ? channel_event_length[status >> NIBBLE_BIT_COUNT]
                                                   : system_event_length[status & MIDI_CHANNEL_MASK];
            bool next_delta = true;
            if (event[MIDI_STATUS] == MIDI_META) {
                if (event[MIDI_DATA1] == META_END_OF_TRACK) {
                    if (end_track(t))
                        return;
                    break;
                }
                if (event[MIDI_DATA1] == META_TEMPO)
                    length = META_TEMPO_LENGTH;
            } else if ((event[MIDI_STATUS] & MIDI_STATUS_MASK) == MIDI_CONTROL && event[MIDI_DATA1] >= HMI_FIRST &&
                       event[MIDI_DATA1] <= HMI_LAST) {
                if (hmi_control(t, event)) {
                    length = 0;
                    next_delta = false;
                }
            } else if (t) {
                send_event(event);
            }
            tr->elapsed = 0;
            if (!tr->cursor)
                break;
            tr->cursor += length;
            if (next_delta)
                tr->delta = read_delta(tr);
        } while (tr->delta == 0);
    }
}

int hmp_tick_rate(void)
{
    return hmp.song ? (int)le32(hmp.song + HMP_TICK_RATE) : 0;
}

/* Volumes and pause */

void hmp_set_song_volume(int volume)
{
    hmp.song_volume = volume;
    resend_volumes();
}

void hmp_set_master_volume(int volume)
{
    hmp.master_volume = volume;
    if (hmp.playing)
        resend_volumes();
}

void hmp_pause(void)
{
    hmp.paused = true;
    hmp.muted = true;
    resend_volumes();
}

void hmp_resume(void)
{
    hmp.paused = false;
    if (!hmp.muted)
        return;
    hmp.muted = false;
    resend_volumes();
}

void hmp_unmute(void)
{
    hmp.muted = false;
    resend_volumes();
}
