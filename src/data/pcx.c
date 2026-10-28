/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pcx.h"
#include <string.h>
#include "le.h"

/* The sizes of the header and of the palette at the end. */
#define PCX_HEADER_SIZE  128
#define PCX_PALETTE_SIZE (1 + PCX_PALETTE_COLORS * PCX_RGB)

/* The offsets in the header. */
#define PCX_AT_VERSION        1
#define PCX_AT_ENCODING       2
#define PCX_AT_BIT_DEPTH      3
#define PCX_AT_X_MIN          4
#define PCX_AT_Y_MIN          6
#define PCX_AT_X_MAX          8
#define PCX_AT_Y_MAX          10
#define PCX_AT_PLANES         65
#define PCX_AT_BYTES_PER_LINE 66

/* The bytes of the header and of the palette marker that the decoder accepts. */
#define PCX_MANUFACTURER       0x0a /* ZSoft */
#define PCX_VERSION_256_COLORS 0x05
#define PCX_ENCODING_RLE       0x01
#define PCX_BITS_PER_PIXEL     0x08
#define PCX_PLANES             0x01
#define PCX_PALETTE_MARKER     0x0c /* the byte before the palette at the end of the file */

/* The bits of an RLE byte. */
#define PCX_RUN_FLAGS 0xc0 /* a byte with both high bits set is a run length */
#define PCX_RUN_MASK  0x3f

/* The checked header: the picture size and the bytes of a row. */
typedef struct {
    enum PcxResult result;
    int width, height, stride;
} PcxHeader;

/* Checks the header for an 8-bit one-plane RLE picture and reads its size and the bytes of a row. */
static PcxHeader read_header(const uint8_t *data, size_t size)
{
    PcxHeader h = {PCX_ERR_DATA, 0, 0, 0};
    if (size < PCX_HEADER_SIZE + PCX_PALETTE_SIZE || data[0] != PCX_MANUFACTURER ||
        data[PCX_AT_VERSION] != PCX_VERSION_256_COLORS || data[PCX_AT_ENCODING] != PCX_ENCODING_RLE ||
        data[PCX_AT_BIT_DEPTH] != PCX_BITS_PER_PIXEL || data[PCX_AT_PLANES] != PCX_PLANES ||
        data[size - PCX_PALETTE_SIZE] != PCX_PALETTE_MARKER)
        return h;
    h.width = le16(data + PCX_AT_X_MAX) - le16(data + PCX_AT_X_MIN) + 1;
    h.height = le16(data + PCX_AT_Y_MAX) - le16(data + PCX_AT_Y_MIN) + 1;
    h.stride = le16(data + PCX_AT_BYTES_PER_LINE);
    if (h.width > 0 && h.height > 0 && h.stride >= h.width)
        h.result = PCX_OK;
    return h;
}

/* Decoding */

enum PcxResult pcx_decode(const uint8_t *data, size_t size, PcxImage *image)
{
    PcxHeader header = read_header(data, size);
    if (header.result != PCX_OK || header.width > image->max_width || header.height > image->max_height)
        return PCX_ERR_DATA;
    image->width = header.width;
    image->height = header.height;
    const uint8_t *p = data + PCX_HEADER_SIZE;
    const uint8_t *end = data + size - PCX_PALETTE_SIZE;
    int run = 0;
    uint8_t value = 0;
    for (int y = 0; y < image->height; y++)
        for (int x = 0; x < header.stride; x++) {
            while (!run) { /* a run of 0 bytes gives no pixel */
                if (p >= end)
                    return PCX_ERR_DATA;
                value = *p++;
                run = 1;
                if ((value & PCX_RUN_FLAGS) == PCX_RUN_FLAGS) {
                    if (p >= end)
                        return PCX_ERR_DATA;
                    run = value & PCX_RUN_MASK;
                    value = *p++;
                }
            }
            run--;
            if (x < image->width)
                image->pixels[y * image->width + x] = value;
        }
    memcpy(image->palette, end + 1, PCX_PALETTE_COLORS * PCX_RGB);
    return PCX_OK;
}
