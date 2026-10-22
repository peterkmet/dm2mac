/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_DATA_MVE_H
#define DM2_DATA_MVE_H
/* Prefixes: mve_ */

/* Interplay MVE movies: chunks, opcodes, 8-bit video, audio (docs/skull-formats.md section 15); from memory only. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The largest picture of a movie and its palette. */
#define MVE_WIDTH_MAX      640
#define MVE_HEIGHT_MAX     480
#define MVE_PIXELS_MAX     (MVE_WIDTH_MAX * MVE_HEIGHT_MAX)
#define MVE_PALETTE_COLORS 256
#define MVE_RGB            3
#define MVE_FRAME_BUFFERS  2 /* the current frame and the frame before it */

enum MveResult {
    MVE_ERROR = -1, /* damaged data: the movie stops */
    MVE_OK,
    MVE_END,   /* the end of the stream */
    MVE_AUDIO, /* an audio frame of track 0: audio and audio_size; audio NULL for silence */
    MVE_SHOW,  /* a new picture: mve_pixels and palette */
};

/* The state of one movie; the caller owns it (no global state). */
typedef struct {
    const uint8_t *data;         /* the MVE stream after the file header */
    size_t size, pos;            /* the stream size and the position of the next opcode */
    size_t chunk_end;            /* the end of the current chunk */
    uint32_t frame_us;           /* the time of one picture: timer rate times subdivisions (opcode 0x02) */
    int mode_width, mode_height; /* the video mode of the player (opcode 0x0a) */
    int audio_rate;              /* samples per second and channel (opcode 0x03); 0: no audio */
    bool audio_stereo, audio_16bit, audio_compressed;
    bool audio_started;   /* opcode 0x04 came: the sound plays from the next picture */
    const uint8_t *audio; /* the samples of the last MVE_AUDIO, NULL for silence */
    int audio_size;       /* the bytes of the last MVE_AUDIO */
    int width, height;    /* the video size in pixels (opcode 0x05) */
    const uint8_t *map;   /* the decoding map of the next video data (opcode 0x0f): 4 bits per block */
    int map_size;
    int frames;     /* show frame opcodes so far */
    int bad_blocks; /* blocks with encoding 6 or a source outside the frame; they stay unchanged */
    int current;    /* the buffer of the frame that is drawn and shown */
    uint8_t palette[MVE_PALETTE_COLORS][MVE_RGB];       /* 6-bit VGA values */
    uint8_t buffers[MVE_FRAME_BUFFERS][MVE_PIXELS_MAX]; /* the current frame and the frame before it */
} MveStream;

/* Finds the MVE data after the player program of a movie file; MVE_ERROR when the file has none. */
enum MveResult mve_open(MveStream *m, const uint8_t *file, size_t size);
/* Runs the opcodes up to the next audio frame, picture, or the end. */
enum MveResult mve_step(MveStream *m);
/* The picture of the last MVE_SHOW: width × height palette indexes. */
const uint8_t *mve_pixels(const MveStream *m);

#endif /* DM2_DATA_MVE_H */
