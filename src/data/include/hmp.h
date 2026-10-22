/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_DATA_HMP_H
#define DM2_DATA_HMP_H
/* Prefixes: hmp_ */

/* HMP song player of the HMI SOS MIDI library in SKULL.EXE: tracks, delta times, HMI loops, volumes. */

#include <stddef.h>
#include <stdint.h>

enum HmpResult {
    HMP_OK = 0,
    HMP_ERR_FORMAT = -1, /* bad signature, track count, or chunk and loop table bounds */
    HMP_ERR_MEMORY = -2,
};

/* The bytes of a MIDI channel message. */
#define MIDI_STATUS     0
#define MIDI_DATA1      1
#define MIDI_DATA2      2
#define MIDI_EVENT_SIZE 3

/* MIDI status masks and messages that the song player and the music driver share. */
#define MIDI_STATUS_MASK  0xf0
#define MIDI_CHANNEL_MASK 0x0f
#define MIDI_CONTROL      0xb0
#define MIDI_PROGRAM      0xc0
#define MIDI_PITCH_BEND   0xe0

/* MIDI controllers. */
#define MIDI_CTRL_VOLUME    0x07
#define MIDI_CTRL_RESET     0x79
#define MIDI_CTRL_NOTES_OFF 0x7b

/* MIDI channel count and data limits. */
#define MIDI_CHANNELS 16
#define MIDI_MAX      127
#define BEND_CENTER   64 /* the pitch bend data value of no bend */

/* Receives a 3-byte MIDI channel message for the music driver. */
typedef void (*HmpSend)(const uint8_t *event);

/* Copies a song and starts it; send gets the events of tracks 1 and up. */
enum HmpResult hmp_start(const uint8_t *data, size_t size, HmpSend send);
/* Stops the song and silences the channels of its tracks. */
void hmp_stop(void);
/* One tick of the song timer: plays the events that are due. */
void hmp_tick(void);
/* Song timer ticks per second from the song header, 0 without a song. */
int hmp_tick_rate(void);
/* Song volume, 0 to 127; it scales the channel volume messages. */
void hmp_set_song_volume(int volume);
/* Master volume, 0 to 127; it scales the channel volume messages. */
void hmp_set_master_volume(int volume);
/* Stops the ticks and mutes the song. */
void hmp_pause(void);
/* Starts the ticks again and unmutes the song. */
void hmp_resume(void);
/* Unmutes the song. */
void hmp_unmute(void);

#endif /* DM2_DATA_HMP_H */
