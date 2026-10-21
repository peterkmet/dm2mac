/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_DATA_GFX_H
#define DM2_DATA_GFX_H
/* Prefixes: gfx_ pal_ */

/* Blitting, scaling, and palette handling of 8 bpp pictures and the screen. */

#include "gdat.h"
#include <stdint.h>

/* Rectangle fields: int16_t rect[RECT_SIZE] = x, y, width, height. */
enum { RECT_X, RECT_Y, RECT_W, RECT_H, RECT_SIZE };

/* Blit mirror modes. */
enum { MIRROR_NONE, MIRROR_H, MIRROR_V, MIRROR_HV };

#define GFX_OPAQUE   (-1) /* transparent color value that draws every pixel */
#define SCALE_FULL   64   /* scale factor of 100 % in 1/64 */
#define REMAP_COLORS 256

/* A bitmap of palette indices: 8 bpp, or 4 bpp with two pixels per byte, high nibble first, and rows of an even pixel count. */
typedef struct {
    uint8_t *pixels;
    int16_t width, height;
    uint8_t bpp;
} Bitmap;

/* Copies rect of dst from src at (src_x, src_y). Widths are in pixels. transparent < 0 draws every pixel, else
   source pixels of that value are skipped. A 4 bpp source maps its pixels through colormap into an 8 bpp destination,
   or copies them into a 4 bpp destination; an 8 bpp source maps through colormap when it is not NULL. */
void gfx_blit(const uint8_t *src, uint8_t *dst, const int16_t *rect, int src_x, int src_y, int src_width, int dst_width,
              int transparent, int mirror, int src_bpp, int dst_bpp, const uint8_t *colormap);
/* Scales a whole 4 bpp image to dst_width x dst_height 4 bpp pixels; colormap (or NULL) maps each pixel. */
void gfx_blit_scaled_4bpp(const uint8_t *src, uint8_t *dst, int src_width, int src_height, int dst_width,
                          int dst_height, const uint8_t *colormap);
/* Scales a whole 8 bpp image to dst_width x dst_height. */
void gfx_blit_scaled_8bpp(const uint8_t *src, uint8_t *dst, int src_width, int src_height, int dst_width,
                          int dst_height);
/* Fills rect of an 8 bpp dst with a row of tile_width 4 bpp pixels repeated from tile_offset on, mapped by colormap.
   With a 4 bpp mask, pixels whose mask nibble at (mask_x, mask_y) is 0 at the row start or end are left out. */
void gfx_blit_tiled_4to8_masked(const uint8_t *src, uint8_t *dst, const uint8_t *mask, const int16_t *rect,
                                int tile_width, int tile_offset, int mask_x, int mask_y, int mask_width, int dst_width,
                                int transparent, const uint8_t *colormap);
void gfx_blit_tiled_4to8(const uint8_t *src, uint8_t *dst, const uint8_t *mask, const int16_t *rect, int tile_width,
                         int tile_offset, int dst_width, int transparent, const uint8_t *colormap);
/* Copies a whole bitmap into dst with the size and depth of src, mirrored by mirror. */
void gfx_blit_image(const Bitmap *src, Bitmap *dst, int mirror);
/* Copies a whole bitmap into rect of the 8 bpp dst. */
void gfx_blit_to_bitmap(const Bitmap *src, Bitmap *dst, const int16_t *rect, int transparent, const uint8_t *colormap);
void gfx_fill_rect(uint8_t *dst, const int16_t *rect, int color, int dst_width, int bpp);
void gfx_fill_bitmap(Bitmap *dst, int color);
/* value * scale / 64, rounded the way of the original. */
int16_t gfx_scale_64(int16_t value, int16_t scale);

/* Loads the color ramps of GRAPHICS.DAT, which the dimming uses; returns 0 when they are missing. */
int pal_load_color_ramps(const gdat_t *gdat);
/* Dims a color remap table: each color becomes the color of its ramp nearest to its brightness * (64 - dim) / 64,
   never key1 or key2 (they stay mapped to themselves). *count <= 0 starts from the identity table of 256 colors. */
uint8_t *pal_apply_dim_remap(uint8_t *table, int dim, int key1, int key2, int16_t *count);
/* Remaps table through the GDAT color table (category, index, 7, entry), then dims it. */
uint8_t *pal_get_dimmed_remap(const gdat_t *gdat, uint8_t *table, int category, int index, int entry, int dim, int key1,
                              int key2, int16_t *count);

#endif /* DM2_DATA_GFX_H */
