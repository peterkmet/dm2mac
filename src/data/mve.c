/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mve.h"
#include <string.h>
#include "le.h"
#include "mve_internal.h"

/* The file header: the signature, then three magic words at their offsets. */
#define MVE_SIGNATURE     "Interplay MVE File\x1a"
#define SIGNATURE_SIZE    (sizeof MVE_SIGNATURE) /* with its 0 byte */
#define FILE_HEADER_SIZE  26                     /* the signature and three magic words */
#define HEADER_LENGTH_AT  SIGNATURE_SIZE
#define HEADER_VERSION_AT (HEADER_LENGTH_AT + sizeof(uint16_t))
#define HEADER_CHECK_AT   (HEADER_VERSION_AT + sizeof(uint16_t))

/* The values of the three magic words. */
#define HEADER_WORD_LENGTH  0x001a
#define HEADER_WORD_VERSION 0x0100
#define HEADER_WORD_CHECK   0x1133

/* The sizes of the headers and bodies of chunks and opcodes. */
#define CHUNK_HEADER_SIZE       4 /* length and type */
#define OPCODE_HEADER_SIZE      4 /* length, type, and version */
#define TIMER_SIZE              6 /* microseconds per subdivision, subdivisions per picture */
#define AUDIO_INIT_SIZE         8
#define AUDIO_FRAME_HEADER_SIZE 6 /* sequence, stream mask, and length */
#define VIDEO_INIT_SIZE         4 /* width and height in blocks */
#define MODE_SIZE               4
#define PALETTE_HEADER_SIZE     4 /* first color and color count */

/* The offsets of the fields in the headers and bodies. */
#define OPCODE_TYPE_AT        2
#define OPCODE_VERSION_AT     3
#define TIMER_SUBDIVISION_AT  4
#define AUDIO_INIT_FLAGS_AT   2
#define AUDIO_INIT_RATE_AT    4
#define AUDIO_FRAME_MASK_AT   2
#define AUDIO_FRAME_LENGTH_AT 4
#define VIDEO_INIT_HEIGHT_AT  2
#define MODE_HEIGHT_AT        2
#define PALETTE_COUNT_AT      2

/* The flags of audio init and the track of the audio frames. */
#define AUDIO_FLAG_STEREO     0x0001
#define AUDIO_FLAG_16BIT      0x0002
#define AUDIO_FLAG_COMPRESSED 0x0004 /* only from audio init version 1 on */
#define AUDIO_TRACK           0x0001 /* the stream mask bit of the track that the player plays */

/* The mask of the 6-bit VGA color values. */
#define VGA_VALUE_MASK 0x3f

/* The opcodes. */
#define OP_END_OF_STREAM 0x00
#define OP_CREATE_TIMER  0x02
#define OP_INIT_AUDIO    0x03
#define OP_START_AUDIO   0x04
#define OP_INIT_VIDEO    0x05
#define OP_SHOW_FRAME    0x07
#define OP_AUDIO_FRAME   0x08
#define OP_AUDIO_SILENCE 0x09
#define OP_VIDEO_MODE    0x0a
#define OP_PALETTE       0x0c
#define OP_DECODING_MAP  0x0f
#define OP_VIDEO_DATA    0x11

/* Setup */

enum MveResult mve_open(MveStream *m, const uint8_t *file, size_t size)
{
    memset(m, 0, offsetof(MveStream, buffers));
    for (size_t at = size >= FILE_HEADER_SIZE ? size - FILE_HEADER_SIZE + 1 : 0; at-- > 0;) {
        const uint8_t *header = file + at;
        if (memcmp(header, MVE_SIGNATURE, SIGNATURE_SIZE))
            continue;
        if (le16(header + HEADER_LENGTH_AT) != HEADER_WORD_LENGTH ||
            le16(header + HEADER_VERSION_AT) != HEADER_WORD_VERSION ||
            le16(header + HEADER_CHECK_AT) != HEADER_WORD_CHECK)
            return MVE_ERROR;
        m->data = header + FILE_HEADER_SIZE;
        m->size = size - at - FILE_HEADER_SIZE;
        return MVE_OK;
    }
    return MVE_ERROR;
}

/* Playback */

/* Enters the next chunk; false when the stream ends inside its header or its data. */
static bool enter_next_chunk(MveStream *m)
{
    if (m->size - m->pos < CHUNK_HEADER_SIZE)
        return false;
    size_t length = le16(m->data + m->pos);
    m->pos += CHUNK_HEADER_SIZE;
    if (m->size - m->pos < length)
        return false;
    m->chunk_end = m->pos + length;
    return true;
}

/* Reads the audio format: rate, channels, sample size, compression. */
static enum MveResult init_audio(MveStream *m, int version, const uint8_t *body, int size)
{
    if (size < AUDIO_INIT_SIZE)
        return MVE_ERROR;
    uint16_t flags = le16(body + AUDIO_INIT_FLAGS_AT);
    m->audio_rate = le16(body + AUDIO_INIT_RATE_AT);
    m->audio_stereo = (flags & AUDIO_FLAG_STEREO) != 0;
    m->audio_16bit = (flags & AUDIO_FLAG_16BIT) != 0;
    m->audio_compressed = version > 0 && (flags & AUDIO_FLAG_COMPRESSED) != 0;
    return MVE_OK;
}

/* Sets the video size in blocks and clears both frames. */
static enum MveResult init_video(MveStream *m, const uint8_t *body, int size)
{
    if (size < VIDEO_INIT_SIZE)
        return MVE_ERROR;
    int width = le16(body) * MVE_BLOCK_SIZE;
    int height = le16(body + VIDEO_INIT_HEIGHT_AT) * MVE_BLOCK_SIZE;
    if (!width || !height || width > MVE_WIDTH_MAX || height > MVE_HEIGHT_MAX)
        return MVE_ERROR;
    m->width = width;
    m->height = height;
    memset(m->buffers, 0, sizeof m->buffers);
    return MVE_OK;
}

/* An audio frame or a silent frame: MVE_AUDIO for track 0, else nothing. */
static enum MveResult read_audio_frame(MveStream *m, int type, const uint8_t *body, int size)
{
    if (size < AUDIO_FRAME_HEADER_SIZE)
        return MVE_ERROR;
    if (!(le16(body + AUDIO_FRAME_MASK_AT) & AUDIO_TRACK))
        return MVE_OK;
    int length = le16(body + AUDIO_FRAME_LENGTH_AT);
    if (type == OP_AUDIO_FRAME && size - AUDIO_FRAME_HEADER_SIZE < length)
        return MVE_ERROR;
    m->audio = type == OP_AUDIO_FRAME ? body + AUDIO_FRAME_HEADER_SIZE : NULL;
    m->audio_size = length;
    return MVE_AUDIO;
}

/* Copies colors of the palette opcode into the palette of the movie. */
static enum MveResult set_palette(MveStream *m, const uint8_t *body, int size)
{
    if (size < PALETTE_HEADER_SIZE)
        return MVE_ERROR;
    int first = le16(body);
    int count = le16(body + PALETTE_COUNT_AT);
    if (first + count > MVE_PALETTE_COLORS || size - PALETTE_HEADER_SIZE < count * MVE_RGB)
        return MVE_ERROR;
    for (int c = 0; c < count; c++)
        for (int ch = 0; ch < MVE_RGB; ch++)
            m->palette[first + c][ch] = body[PALETTE_HEADER_SIZE + c * MVE_RGB + ch] & VGA_VALUE_MASK;
    return MVE_OK;
}

/* Runs one opcode; MVE_OK lets mve_step go on. */
static enum MveResult run_opcode(MveStream *m, int type, int version, const uint8_t *body, int size)
{
    switch (type) {
    case OP_END_OF_STREAM:
        return MVE_END;
    case OP_CREATE_TIMER:
        if (size < TIMER_SIZE)
            return MVE_ERROR;
        m->frame_us = le32(body) * le16(body + TIMER_SUBDIVISION_AT);
        return MVE_OK;
    case OP_INIT_AUDIO:
        return init_audio(m, version, body, size);
    case OP_START_AUDIO:
        m->audio_started = true;
        return MVE_OK;
    case OP_INIT_VIDEO:
        return init_video(m, body, size);
    case OP_SHOW_FRAME:
        m->frames++;
        return MVE_SHOW;
    case OP_AUDIO_FRAME:
    case OP_AUDIO_SILENCE:
        return read_audio_frame(m, type, body, size);
    case OP_VIDEO_MODE:
        if (size < MODE_SIZE)
            return MVE_ERROR;
        m->mode_width = le16(body);
        m->mode_height = le16(body + MODE_HEIGHT_AT);
        return MVE_OK;
    case OP_PALETTE:
        return set_palette(m, body, size);
    case OP_DECODING_MAP:
        m->map = body;
        m->map_size = size;
        return MVE_OK;
    case OP_VIDEO_DATA:
        return mve_decode_video(m, body, size) ? MVE_OK : MVE_ERROR;
    default:
        return MVE_OK; /* end of chunk, block statistics (0x13), and opcodes that the movies of DM2 do not use */
    }
}

enum MveResult mve_step(MveStream *m)
{
    for (;;) {
        if (m->pos == m->chunk_end && !enter_next_chunk(m))
            return MVE_ERROR;
        if (m->chunk_end - m->pos < OPCODE_HEADER_SIZE)
            return MVE_ERROR;
        const uint8_t *header = m->data + m->pos;
        int size = le16(header);
        if (m->chunk_end - m->pos - OPCODE_HEADER_SIZE < (size_t)size)
            return MVE_ERROR;
        m->pos += OPCODE_HEADER_SIZE + size;
        enum MveResult result =
            run_opcode(m, header[OPCODE_TYPE_AT], header[OPCODE_VERSION_AT], header + OPCODE_HEADER_SIZE, size);
        if (result != MVE_OK)
            return result;
    }
}

const uint8_t *mve_pixels(const MveStream *m)
{
    return m->buffers[m->current];
}
