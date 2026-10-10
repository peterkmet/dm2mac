#ifndef PCX_H
#define PCX_H
#include <stddef.h>
#include <stdint.h>

/* Prefixes: pcx_ */

/* PCX pictures, 8 bits per pixel, RLE, palette at the end (the splash INTRPLAY.PCX); decodes from memory only. */

#define PCX_PALETTE_COLORS 256
#define PCX_RGB 3

enum { PCX_OK, PCX_ERR_DATA };

/* Decodes the picture (width bytes per row) and its 8-bit palette; PCX_ERR_DATA for another kind, size, or short data. */
int pcx_decode(const uint8_t *data, size_t size, uint8_t *pixels, int max_width, int max_height, int *width,
               int *height, uint8_t palette[PCX_PALETTE_COLORS][PCX_RGB]);

#endif
