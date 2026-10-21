/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_DATA_HMP_H
#define DM2_DATA_HMP_H
/* Prefixes: hmp_ */

/* HMP song player of the HMI SOS MIDI library in SKULL.EXE: tracks, delta times, HMI loops, volumes. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    HMP_OK = 0,
    HMP_ERR_FORMAT = -1, /* bad signature, track count, or chunk and loop table bounds */
    HMP_ERR_MEMORY = -2,
};

/* The bytes of a MIDI channel message. */
enum { MIDI_STATUS, MIDI_DATA1, MIDI_DATA2, MIDI_EVENT_SIZE };
/* MIDI messages and controllers that the song player and the music driver share. */
#define MIDI_STATUS_MASK    0xf0
#define MIDI_CHANNEL_MASK   0x0f
#define MIDI_CONTROL        0xb0
#define MIDI_PROGRAM        0xc0
#define MIDI_PITCH_BEND     0xe0
#define MIDI_CTRL_VOLUME    7
#define MIDI_CTRL_RESET     0x79
#define MIDI_CTRL_NOTES_OFF 0x7b
#define MIDI_CHANNELS       16
#define MIDI_MAX            127
#define BEND_CENTER         0x40 /* the pitch bend data value of no bend */

/* Receives a 3-byte MIDI channel message for the music driver. */
typedef void (*HmpSend)(const uint8_t *event);

/* Copies a song and starts it; send gets the events of tracks 1 and up. */
int hmp_start(const uint8_t *data, size_t size, HmpSend send);
/* Stops the song and silences the channels of its tracks. */
void hmp_stop(void);
/* One tick of the song timer: plays the events that are due. */
void hmp_tick(void);
/* Song timer ticks per second from the song header, 0 without a song. */
int hmp_tick_rate(void);
bool hmp_is_playing(void);
/* Song and master volume, 0 to 127; they scale the channel volume messages. */
void hmp_set_song_volume(int volume);
void hmp_set_master_volume(int volume);
/* Stops the ticks and mutes the song. */
void hmp_pause(void);
/* Starts the ticks again and unmutes the song. */
void hmp_resume(void);
void hmp_unmute(void);

#endif /* DM2_DATA_HMP_H */
