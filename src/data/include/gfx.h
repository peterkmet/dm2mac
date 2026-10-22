/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_DATA_GFX_H
#define DM2_DATA_GFX_H
/* Prefixes: gfx_ pal_ */

/* Blitting, scaling, and palette handling of 8 bpp pictures and the screen. */

#include "gdat.h"
#include <stdbool.h>
#include <stdint.h>

/* Rectangle fields: int16_t rect[RECT_SIZE] = x, y, width, height. */
#define RECT_X    0
#define RECT_Y    1
#define RECT_W    2
#define RECT_H    3
#define RECT_SIZE 4

/* Blit mirror modes, as flags. */
#define MIRROR_NONE 0x0
#define MIRROR_H    0x1
#define MIRROR_V    0x2
#define MIRROR_HV   0x3

/* Transparent color value that draws every pixel. */
#define GFX_OPAQUE (-1)

/* Scale factor of 100 % in 1/64, and the colors of a remap table. */
#define SCALE_FULL   64
#define REMAP_COLORS 256

/* A bitmap of palette indices: 8 bpp, or 4 bpp with two pixels per byte, high nibble first, and rows of an even pixel count. */
typedef struct {
    uint8_t *pixels;
    int16_t width, height;
    uint8_t bpp;
} Bitmap;

/* One blit: widths are in pixels. transparent < 0 draws every pixel, else source pixels of that value are skipped.
   A 4 bpp source maps its pixels through colormap into an 8 bpp destination, or copies them into a 4 bpp
   destination; an 8 bpp source maps through colormap when it is not NULL. */
typedef struct {
    const uint8_t *src;
    uint8_t *dst;
    int src_x, src_y;
    int src_width, dst_width;
    int src_bpp, dst_bpp;
    int transparent;
    int mirror;
    const uint8_t *colormap;
} Blit;

/* A row of 4 bpp tile pixels that repeats over a rectangle, mapped by colormap. With a 4 bpp mask, pixels whose
   mask nibble at (mask_x, mask_y) is 0 at the row start or end are left out. */
typedef struct {
    const uint8_t *tile;
    int tile_width, tile_offset;
    const uint8_t *mask;
    int mask_x, mask_y, mask_width;
    int transparent;
    const uint8_t *colormap;
} TileFill;

/* A dimmed color remap: the GDAT color table (category, index, GDAT_TYPE_REMAP, entry), the dimming, and the two
   key colors that stay mapped to themselves. */
typedef struct {
    int category, index, entry;
    int dim;
    int key1, key2;
} DimmedRemap;

/* Copies rect of blit->dst from blit->src at (src_x, src_y). */
void gfx_blit(const Blit *blit, const int16_t *rect);
/* Scales a whole 4 bpp image to the width x height of dst; colormap (or NULL) maps each pixel. */
void gfx_blit_scaled_4bpp(const Bitmap *src, Bitmap *dst, const uint8_t *colormap);
/* Scales a whole 8 bpp image to the width x height of dst. */
void gfx_blit_scaled_8bpp(const Bitmap *src, Bitmap *dst);
/* Fills rect of an 8 bpp dst with the tile row repeated from tile_offset on; the mask rows start at the mask
   position; exported for the tests. */
void gfx_blit_tiled_4to8_masked(const TileFill *fill, uint8_t *dst, int dst_width, const int16_t *rect);
/* Fills rect of an 8 bpp dst with the tile row repeated from tile_offset on; a mask has the width of rect. */
void gfx_blit_tiled_4to8(const TileFill *fill, uint8_t *dst, int dst_width, const int16_t *rect);
/* Copies a whole bitmap into dst with the size and depth of src, mirrored by mirror. */
void gfx_blit_image(const Bitmap *src, Bitmap *dst, int mirror);
/* Copies a whole bitmap into rect of the 8 bpp dst. */
void gfx_blit_to_bitmap(const Bitmap *src, Bitmap *dst, const int16_t *rect, int transparent, const uint8_t *colormap);
/* Fills rect of dst with one color. */
void gfx_fill_rect(uint8_t *dst, const int16_t *rect, int color, int dst_width, int bpp);
/* Fills a whole bitmap with one color. */
void gfx_fill_bitmap(Bitmap *dst, int color);
/* value * scale / 64, rounded the way of the original. */
int16_t gfx_scale_64(int16_t value, int16_t scale);

/* Loads the color ramps of GRAPHICS.DAT, which the dimming uses; returns false when they are missing. */
bool pal_load_color_ramps(const gdat_t *gdat);
/* Dims a color remap table: each color becomes the color of its ramp nearest to its brightness * (64 - dim) / 64,
   never key1 or key2 (they stay mapped to themselves). *count <= 0 starts from the identity table of 256 colors. */
uint8_t *pal_apply_dim_remap(uint8_t *table, int dim, int key1, int key2, int16_t *count);
/* Remaps table through the GDAT color table of remap, then dims it. */
uint8_t *pal_get_dimmed_remap(const gdat_t *gdat, uint8_t *table, const DimmedRemap *remap, int16_t *count);

#endif /* DM2_DATA_GFX_H */
