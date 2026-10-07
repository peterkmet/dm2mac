#include <stdlib.h>
#include <string.h>
#include "hmp.h"
#include "le.h"

/* HMP header. */
#define HMP_SIGNATURE "HMIMIDIP013195"
#define HMP_BRANCHES 0x20     /* u32: offset of the loop table */
#define HMP_TRACK_COUNT 0x30  /* u32 */
#define HMP_TICK_RATE 0x38    /* u32: song timer ticks per second */
#define HMP_FIRST_TRACK 0x388
#define HMP_MAX_TRACKS 32

/* Track chunk: number, chunk size, MIDI channel, then delta times and events. */
#define TRACK_SIZE 4
#define TRACK_CHANNEL 8
#define TRACK_DATA 12

/* Loop table: a record count per track, then 24-byte records of all tracks in track order. */
#define BRANCH_SIZE 0x18
#define BRANCH_OFFSET 0       /* u32: loop point in the track data */
#define BRANCH_ID 4
#define BRANCH_PROGRAM 5
#define BRANCH_LOOPS 6        /* loops left, LOOPS_FOREVER for no end */
#define BRANCH_CTRL_BYTES 7
#define BRANCH_CTRLS 0x0c     /* u32: offset of the controller and value pairs to send at the loop point */
#define LOOPS_FOREVER 0xff

/* MIDI and HMI events. */
#define MIDI_SYSTEM 0xf0
#define MIDI_META 0xff
#define META_END_OF_TRACK 0x2f
#define META_TEMPO 0x51
#define META_TEMPO_LENGTH 5
#define MIDI_VOLUME_SHIFT 7
#define HMI_FIRST 0x67        /* controllers 0x67 to 0x79 are HMI commands, never sent to the driver */
#define HMI_LAST 0x79
#define HMI_SET_LOOPS 0x6d    /* the value of the next event (an 0x6e controller) sets the loop count */
#define HMI_SET_LOOPS_VALUE 6
#define HMI_LOOP 0x6f         /* all tracks jump to their loop point with the id of the value */
#define HMI_LOOP_2 0x70
#define VARLEN_BITS 7
#define VARLEN_LAST 0x80
#define SHIFT_MASK 31
#define STATUS_NIBBLES 16

/* Event length by status high nibble, and for system events by low nibble (tables of SKULL.EXE). */
static const uint8_t channel_event_length[STATUS_NIBBLES] = {0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3, 3, 2, 2, 3, 0};
static const uint8_t system_event_length[STATUS_NIBBLES] = {0, 1, 2, 1, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2};

typedef struct {
    uint32_t start, end;   /* track data */
    uint32_t cursor;       /* next event, 0 when the track ended */
    uint32_t delta;        /* ticks from the last event to the next event */
    uint32_t elapsed;      /* ticks since the last event */
    uint32_t branches;     /* first loop record */
    int branch_count;
    uint8_t channel;
} Track;

static uint8_t *song;
static size_t song_size;
static HmpSend send;
static Track tracks[HMP_MAX_TRACKS];
static int track_count, active_tracks, playing, paused, muted;
static int song_volume = MIDI_MAX, master_volume = MIDI_MAX;
/* Last volume of each channel; it stays from song to song. */
static uint8_t channel_volume[MIDI_CHANNELS] = {
    MIDI_MAX, MIDI_MAX, MIDI_MAX, MIDI_MAX, MIDI_MAX, MIDI_MAX, MIDI_MAX, MIDI_MAX,
    MIDI_MAX, MIDI_MAX, MIDI_MAX, MIDI_MAX, MIDI_MAX, MIDI_MAX, MIDI_MAX, MIDI_MAX,
};

/* Reads a delta time: 7-bit groups, low group first; the last byte has bit 7 set. */
static uint32_t read_delta(Track *tr)
{
    uint32_t value = 0;
    for (int shift = 0; tr->cursor < tr->end; shift += VARLEN_BITS) {
        uint8_t b = song[tr->cursor++];
        value |= (uint32_t)(b & ~VARLEN_LAST) << (shift & SHIFT_MASK);
        if (b & VARLEN_LAST) break;
    }
    return value;
}

/* Sends an event; a channel volume is stored and scaled by the song and master volume. */
static void send_event(const uint8_t *event)
{
    if ((event[MIDI_STATUS] & MIDI_STATUS_MASK) != MIDI_CONTROL || event[MIDI_DATA1] != MIDI_CTRL_VOLUME) {
        send(event);
        return;
    }
    channel_volume[event[MIDI_STATUS] & MIDI_CHANNEL_MASK] = event[MIDI_DATA2];
    int volume = master_volume * (event[MIDI_DATA2] * song_volume >> MIDI_VOLUME_SHIFT) >> MIDI_VOLUME_SHIFT;
    uint8_t scaled[MIDI_EVENT_SIZE] = {event[MIDI_STATUS], event[MIDI_DATA1], (uint8_t)(muted ? 0 : volume)};
    send(scaled);
}

static void send3(int status, int data1, int data2)
{
    uint8_t event[MIDI_EVENT_SIZE] = {(uint8_t)status, (uint8_t)data1, (uint8_t)data2};
    send(event);
}

/* Sends the stored channel volume of each playing track again. */
static void resend_volumes(void)
{
    for (int t = 0; t < track_count; t++) {
        if (!tracks[t].cursor) continue;
        uint8_t event[MIDI_EVENT_SIZE] = {MIDI_CONTROL | tracks[t].channel, MIDI_CTRL_VOLUME, channel_volume[tracks[t].channel]};
        send_event(event);
    }
}

/* Notes off, controllers reset, pitch bend center, and volume 0 on the channels of tracks 1 and up. */
static void silence(void)
{
    for (int t = 1; t < track_count; t++) {
        int ch = tracks[t].channel;
        send3(MIDI_CONTROL | ch, MIDI_CTRL_NOTES_OFF, 0);
        send3(MIDI_CONTROL | ch, MIDI_CTRL_RESET, 0);
        send3(MIDI_PITCH_BEND | ch, BEND_CENTER, BEND_CENTER);
        send3(MIDI_CONTROL | ch, MIDI_CTRL_VOLUME, 0);
    }
}

int hmp_start(const uint8_t *data, size_t size, HmpSend to_driver)
{
    hmp_stop();
    if (size < HMP_FIRST_TRACK || memcmp(data, HMP_SIGNATURE, strlen(HMP_SIGNATURE)) ||
        !le32(data + HMP_TICK_RATE))
        return HMP_ERR_FORMAT;
    uint32_t count = le32(data + HMP_TRACK_COUNT), branches = le32(data + HMP_BRANCHES);
    if (count < 1 || count > HMP_MAX_TRACKS || branches > size || size - branches < count) return HMP_ERR_FORMAT;
    uint32_t offset = HMP_FIRST_TRACK, record = branches + count;
    for (uint32_t t = 0; t < count; t++) {
        if (size - offset < TRACK_DATA) return HMP_ERR_FORMAT;
        uint32_t chunk = le32(data + offset + TRACK_SIZE);
        if (chunk < TRACK_DATA || chunk > size - offset) return HMP_ERR_FORMAT;
        Track *tr = &tracks[t];
        *tr = (Track){.start = offset + TRACK_DATA, .end = offset + chunk, .branches = record,
                      .branch_count = data[branches + t], .channel = data[offset + TRACK_CHANNEL] & MIDI_CHANNEL_MASK};
        for (int b = 0; b < tr->branch_count; b++, record += BRANCH_SIZE) {
            if (size - BRANCH_SIZE < record) return HMP_ERR_FORMAT;
            uint32_t ctrls = le32(data + record + BRANCH_CTRLS), point = le32(data + record + BRANCH_OFFSET);
            if (ctrls > size || size - ctrls < data[record + BRANCH_CTRL_BYTES] || point >= chunk - TRACK_DATA)
                return HMP_ERR_FORMAT;
        }
        offset += chunk;
    }
    song = malloc(size);
    if (!song) return HMP_ERR_MEMORY;
    memcpy(song, data, size);
    song_size = size;
    send = to_driver;
    track_count = active_tracks = (int)count;
    for (int t = 0; t < track_count; t++) {
        tracks[t].cursor = tracks[t].start;
        tracks[t].delta = read_delta(&tracks[t]);
    }
    playing = 1;
    paused = 0;
    return HMP_OK;
}

void hmp_stop(void)
{
    if (playing) silence();
    playing = 0;
    free(song);
    song = NULL;
    track_count = 0;
}

int hmp_tick_rate(void)
{
    return song ? (int)le32(song + HMP_TICK_RATE) : 0;
}

bool hmp_is_playing(void)
{
    return playing;
}

static uint8_t *find_branch(int t, int id)
{
    for (int b = 0; b < tracks[t].branch_count; b++) {
        uint8_t *rec = song + tracks[t].branches + b * BRANCH_SIZE;
        if (rec[BRANCH_ID] == id) return rec;
    }
    return NULL;
}

/* Moves a track to its loop point id and sends the program and controllers of the loop point. */
static void jump(int t, int id)
{
    Track *tr = &tracks[t];
    const uint8_t *rec = find_branch(t, id);
    if (!rec) return;  /* fix: the original searches past the loop records of the track */
    tr->cursor = tr->start + le32(rec + BRANCH_OFFSET);
    tr->delta = read_delta(tr);
    tr->elapsed = 0;
    if (tr->cursor >= tr->end) return;
    int ch = song[tr->cursor] & MIDI_CHANNEL_MASK;
    uint8_t program[MIDI_EVENT_SIZE] = {MIDI_PROGRAM | ch, rec[BRANCH_PROGRAM], 0};
    send_event(program);
    const uint8_t *pairs = song + le32(rec + BRANCH_CTRLS);
    for (int i = 0; i + 1 < rec[BRANCH_CTRL_BYTES]; i += 2) {
        uint8_t event[MIDI_EVENT_SIZE] = {MIDI_CONTROL | ch, pairs[i], pairs[i + 1]};
        send_event(event);
    }
}

/* Runs an HMI controller of track t; returns 1 when the tracks jumped to a loop point. */
static int hmi_control(int t, const uint8_t *event)
{
    uint8_t *rec = find_branch(t, event[MIDI_DATA2]);
    if (!rec) return 0;
    if (event[MIDI_DATA1] == HMI_SET_LOOPS) {
        if (tracks[t].cursor + HMI_SET_LOOPS_VALUE < song_size)
            rec[BRANCH_LOOPS] = song[tracks[t].cursor + HMI_SET_LOOPS_VALUE];
        return 0;
    }
    if (event[MIDI_DATA1] != HMI_LOOP && event[MIDI_DATA1] != HMI_LOOP_2) return 0;
    int loops = rec[BRANCH_LOOPS];
    if (loops != LOOPS_FOREVER && loops) loops = --rec[BRANCH_LOOPS];
    if (!loops) return 0;
    for (int k = 1; k < track_count; k++)
        if (tracks[k].cursor) jump(k, event[MIDI_DATA2]);
    return 1;
}

/* Ends a track; returns 1 when the song ended. */
static int end_track(int t)
{
    tracks[t].cursor = 0;
    if (active_tracks == 2 && tracks[0].cursor) {
        active_tracks--;
        tracks[0].cursor = 0;
    }
    if (--active_tracks) return 0;
    playing = 0;
    silence();
    return 1;
}

void hmp_tick(void)
{
    if (!playing || paused) return;
    for (int t = 0; t < track_count; t++) {
        Track *tr = &tracks[t];
        tr->elapsed++;
        if (!tr->cursor || tr->delta > tr->elapsed) continue;
        do {
            if (tr->cursor + MIDI_EVENT_SIZE > tr->end) {  /* fix: a track without an end event ends at its chunk end */
                if (end_track(t)) return;
                break;
            }
            const uint8_t *event = song + tr->cursor;
            uint8_t status = event[MIDI_STATUS];
            uint32_t length = status < MIDI_SYSTEM ? channel_event_length[status >> NIBBLE_BITS]
                                                   : system_event_length[status & MIDI_CHANNEL_MASK];
            int next_delta = 1;
            if (event[MIDI_STATUS] == MIDI_META) {
                if (event[MIDI_DATA1] == META_END_OF_TRACK) {
                    if (end_track(t)) return;
                    break;
                }
                if (event[MIDI_DATA1] == META_TEMPO) length = META_TEMPO_LENGTH;
            } else if ((event[MIDI_STATUS] & MIDI_STATUS_MASK) == MIDI_CONTROL && event[MIDI_DATA1] >= HMI_FIRST
                       && event[MIDI_DATA1] <= HMI_LAST) {
                if (hmi_control(t, event)) length = next_delta = 0;
            } else if (t) {
                send_event(event);
            }
            tr->elapsed = 0;
            if (!tr->cursor) break;
            tr->cursor += length;
            if (next_delta) tr->delta = read_delta(tr);
        } while (tr->delta == 0);
    }
}

void hmp_set_song_volume(int volume)
{
    song_volume = volume;
    resend_volumes();
}

void hmp_set_master_volume(int volume)
{
    master_volume = volume;
    if (playing) resend_volumes();
}

void hmp_pause(void)
{
    paused = 1;
    muted = 1;
    resend_volumes();
}

void hmp_resume(void)
{
    paused = 0;
    if (!muted) return;
    muted = 0;
    resend_volumes();
}

void hmp_unmute(void)
{
    muted = 0;
    resend_volumes();
}
