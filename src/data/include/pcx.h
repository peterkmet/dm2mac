/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_DATA_PCX_H
#define DM2_DATA_PCX_H
/* Prefixes: pcx_ */

/* PCX pictures, 8 bits per pixel, RLE, palette at the end (the splash INTRPLAY.PCX); decodes from memory only. */

#include <stddef.h>
#include <stdint.h>

/* The palette of a picture. */
#define PCX_PALETTE_COLORS 256
#define PCX_RGB            3

enum PcxResult { PCX_OK, PCX_ERR_DATA };

/* The picture to decode into: the caller sets pixels (max_width × max_height bytes) and the limits. */
typedef struct {
    uint8_t *pixels;
    int max_width, max_height;
    int width, height; /* the size of the decoded picture */
    uint8_t palette[PCX_PALETTE_COLORS][PCX_RGB];
} PcxImage;

/* Decodes the picture (width bytes per row) and its 8-bit palette; PCX_ERR_DATA for another kind, size, or short data. */
enum PcxResult pcx_decode(const uint8_t *data, size_t size, PcxImage *image);

#endif /* DM2_DATA_PCX_H */
