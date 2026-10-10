#include <string.h>
#include "le.h"
#include "pcx.h"

#define PCX_HEADER_SIZE 128
#define PCX_MANUFACTURER 0x0a          /* ZSoft */
#define PCX_VERSION_256_COLORS 5
#define PCX_ENCODING_RLE 1
#define PCX_BITS_PER_PIXEL 8
#define PCX_PLANES 1
#define PCX_PALETTE_MARKER 0x0c        /* the byte before the palette at the end of the file */
#define PCX_PALETTE_SIZE (1 + PCX_PALETTE_COLORS * PCX_RGB)
#define PCX_RUN_FLAGS 0xc0             /* a byte with both high bits set is a run length */
#define PCX_RUN_MASK 0x3f
/* Offsets in the header. */
#define PCX_AT_VERSION 1
#define PCX_AT_ENCODING 2
#define PCX_AT_BITS 3
#define PCX_AT_X_MIN 4
#define PCX_AT_Y_MIN 6
#define PCX_AT_X_MAX 8
#define PCX_AT_Y_MAX 10
#define PCX_AT_PLANES 65
#define PCX_AT_BYTES_PER_LINE 66

/* Checks the header for an 8-bit one-plane RLE picture and reads its size and the bytes of a row. */
static int read_header(const uint8_t *data, size_t size, int *width, int *height, int *stride)
{
    if (size < PCX_HEADER_SIZE + PCX_PALETTE_SIZE || data[0] != PCX_MANUFACTURER
        || data[PCX_AT_VERSION] != PCX_VERSION_256_COLORS || data[PCX_AT_ENCODING] != PCX_ENCODING_RLE
        || data[PCX_AT_BITS] != PCX_BITS_PER_PIXEL || data[PCX_AT_PLANES] != PCX_PLANES
        || data[size - PCX_PALETTE_SIZE] != PCX_PALETTE_MARKER)
        return PCX_ERR_DATA;
    *width = le16(data + PCX_AT_X_MAX) - le16(data + PCX_AT_X_MIN) + 1;
    *height = le16(data + PCX_AT_Y_MAX) - le16(data + PCX_AT_Y_MIN) + 1;
    *stride = le16(data + PCX_AT_BYTES_PER_LINE);
    return *width > 0 && *height > 0 && *stride >= *width ? PCX_OK : PCX_ERR_DATA;
}

int pcx_decode(const uint8_t *data, size_t size, uint8_t *pixels, int max_width, int max_height, int *width,
               int *height, uint8_t palette[PCX_PALETTE_COLORS][PCX_RGB])
{
    int stride;
    if (read_header(data, size, width, height, &stride) != PCX_OK || *width > max_width || *height > max_height)
        return PCX_ERR_DATA;
    const uint8_t *p = data + PCX_HEADER_SIZE, *end = data + size - PCX_PALETTE_SIZE;
    int run = 0;
    uint8_t value = 0;
    for (int y = 0; y < *height; y++)
        for (int x = 0; x < stride; x++) {
            while (!run) {  /* a run of 0 bytes gives no pixel */
                if (p >= end) return PCX_ERR_DATA;
                value = *p++;
                run = 1;
                if ((value & PCX_RUN_FLAGS) != PCX_RUN_FLAGS) continue;
                if (p >= end) return PCX_ERR_DATA;
                run = value & PCX_RUN_MASK;
                value = *p++;
            }
            run--;
            if (x < *width) pixels[y * *width + x] = value;
        }
    memcpy(palette, end + 1, PCX_PALETTE_COLORS * PCX_RGB);
    return PCX_OK;
}
