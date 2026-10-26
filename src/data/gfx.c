/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gfx.h"
#include <stdlib.h>
#include <string.h>
#include "ids.h"
#include "le.h"

/* The high nibble of a 4 bpp byte. */
#define HIGH_NIBBLE_MASK 0xf0

/* Fraction bits: the scalers step through the source in 1/128 pixel, the 8 bpp scaler in 1/256 pixel; the dimming
   divides brightness * light by 64. */
#define FRACTION_SHIFT      7
#define BYTE_FRACTION_SHIFT 8
#define DIM_SHIFT           6

/* The color ramps: GDAT (1, 0, 7, 2) holds the ramp count, the length of each ramp, the brightness steps
   and the colors of each ramp, then the ramp and step of each of the 256 palette colors. */
#define RAMP_INDEX         0
#define RAMP_TYPE          7
#define RAMP_MAX           256
#define RAMP_OF_COLOR_SIZE 2 /* the ramp and the step of a color */

/* The state of a tile row that wraps at its end. */
typedef struct {
    int left; /* pixels until the tile row ends */
    int pos;  /* pixel position in the tile row */
} TileCursor;

static struct {
    int count;
    uint8_t length[RAMP_MAX];
    const uint8_t *brightness[RAMP_MAX];
    const uint8_t *colors[RAMP_MAX];
    const uint8_t *of_color; /* ramp and step of each color */
} ramps;

/* The row width of a 4 bpp picture: an even pixel count. */
static int even(int width)
{
    return (width + 1) & ~1;
}

/* The color of pixel i of a 4 bpp or 8 bpp picture. */
static int get_pixel(const uint8_t *p, int bpp, int i)
{
    if (bpp == IMAGE_8BPP)
        return p[i];
    return i & 1 ? p[i >> 1] & NIBBLE_MASK : p[i >> 1] >> NIBBLE_BIT_COUNT;
}

/* Sets pixel i of a 4 bpp or 8 bpp picture. */
static void put_pixel(uint8_t *p, int bpp, int i, int color)
{
    if (bpp == IMAGE_8BPP) {
        p[i] = (uint8_t)color;
        return;
    }
    uint8_t *b = p + (i >> 1);
    *b = i & 1 ? (*b & HIGH_NIBBLE_MASK) | (color & NIBBLE_MASK)
               : (*b & NIBBLE_MASK) | (uint8_t)(color << NIBBLE_BIT_COUNT);
}

void gfx_blit(const Blit *blit, const int16_t *rect)
{
    int src_width = blit->src_width;
    int dst_width = blit->dst_width;
    int src_bpp = blit->src_bpp;
    int dst_bpp = blit->dst_bpp;
    int mirror = blit->mirror;
    const uint8_t *colormap = blit->colormap;
    if (!rect || rect[RECT_W] <= 0 || rect[RECT_H] <= 0)
        return;
    if (src_bpp == IMAGE_4BPP) {
        src_width = even(src_width);
        if (dst_bpp == IMAGE_4BPP) {
            dst_width = even(dst_width);
            colormap = NULL;
        }
    } else {
        dst_bpp = IMAGE_8BPP; /* the original treats the destination of an 8 bpp source as 8 bpp */
    }
    if (mirror > MIRROR_HV)
        mirror = MIRROR_NONE;
    int w = rect[RECT_W];
    int h = rect[RECT_H];
    for (int row = 0; row < h; row++) {
        int sy = mirror & MIRROR_V ? blit->src_y + h - 1 - row : blit->src_y + row;
        int s = sy * src_width + blit->src_x;
        int d = (rect[RECT_Y] + row) * dst_width + rect[RECT_X];
        for (int col = 0; col < w; col++) {
            int color = get_pixel(blit->src, src_bpp, mirror & MIRROR_H ? s + w - 1 - col : s + col);
            if (blit->transparent >= 0 && color == (uint8_t)blit->transparent)
                continue;
            put_pixel(blit->dst, dst_bpp, d + col, colormap ? colormap[color] : color);
        }
    }
}

void gfx_blit_scaled_4bpp(const Bitmap *source, Bitmap *target, const uint8_t *colormap)
{
    const uint8_t *src = source->pixels;
    uint8_t *dst = target->pixels;
    int src_width = source->width;
    int src_height = source->height;
    int dst_width = target->width;
    int dst_height = target->height;
    int src_stride = even(src_width);
    int dst_stride = even(dst_width);
    uint16_t step_x = (uint16_t)(((unsigned)src_width << FRACTION_SHIFT) / (unsigned)dst_width);
    uint16_t step_y = (uint16_t)(((unsigned)src_height << FRACTION_SHIFT) / (unsigned)dst_height);
    uint16_t acc_y = step_y >> 1;
    int prev = -1;
    for (int row = 0; row < dst_height; row++, acc_y += step_y) {
        int sy = acc_y >> FRACTION_SHIFT;
        uint8_t *out = dst + row * dst_stride / 2;
        if (sy == prev && row > 0) {
            memcpy(out, out - dst_stride / 2, dst_stride / 2); /* same source row: copy the row above */
            continue;
        }
        prev = sy;
        int base = sy * src_stride;
        uint16_t acc_x = step_x >> 1;
        /* two pixels per destination byte; an odd width also fills the pad nibble */
        for (int i = 0; i < (dst_width + 1) / 2; i++) {
            int hi = get_pixel(src, IMAGE_4BPP, (uint16_t)((acc_x >> FRACTION_SHIFT) + base));
            acc_x += step_x;
            int lo = get_pixel(src, IMAGE_4BPP, (uint16_t)((acc_x >> FRACTION_SHIFT) + base));
            acc_x += step_x;
            if (colormap) {
                hi = colormap[hi];
                lo = colormap[lo];
            }
            out[i] = (uint8_t)(hi << NIBBLE_BIT_COUNT | lo);
        }
    }
}

void gfx_blit_scaled_8bpp(const Bitmap *source, Bitmap *target)
{
    const uint8_t *src = source->pixels;
    uint8_t *dst = target->pixels;
    int src_width = source->width;
    int src_height = source->height;
    int dst_width = target->width;
    int dst_height = target->height;
    uint16_t step_x = (uint16_t)(((unsigned)src_width << FRACTION_SHIFT) / (unsigned)dst_width);
    uint16_t step_y = (uint16_t)(((unsigned)src_height << FRACTION_SHIFT) / (unsigned)dst_height);
    uint16_t acc_y = step_y >> 1;
    uint16_t step2 = (uint16_t)(step_x << 1);
    int prev = -1;
    for (int row = 0; row < dst_height; row++, acc_y += step_y) {
        int sy = acc_y >> FRACTION_SHIFT;
        uint8_t *out = dst + row * dst_width;
        if (sy == prev && row > 0) {
            memcpy(out, out - dst_width, dst_width);
            continue;
        }
        prev = sy;
        const uint8_t *in = src + sy * src_width;
        uint32_t pos = step_x; /* half a step in 1/256 pixel */
        for (int i = 0; i < dst_width; i++, pos += step2)
            out[i] = in[pos >> BYTE_FRACTION_SHIFT];
    }
}

/* Draws count tile pixels from the cursor into an 8 bpp row and advances the cursor, wrapping at the tile end. */
static void tile_run(const TileFill *fill, uint8_t *dst, int at, int count, TileCursor *cursor)
{
    while (count > 0) {
        int n = cursor->left <= count ? cursor->left : count;
        for (int i = 0; i < n; i++) {
            int color = get_pixel(fill->tile, IMAGE_4BPP, cursor->pos + i);
            if (fill->transparent < 0 || color != (uint8_t)fill->transparent)
                dst[at + i] = fill->colormap[color];
        }
        at += n;
        count -= n;
        if (n == cursor->left) {
            cursor->left = fill->tile_width;
            cursor->pos = 0;
        } else {
            cursor->left -= n;
            cursor->pos += n;
        }
    }
}

static int mask_pixel(const uint8_t *mask, int i)
{
    return get_pixel(mask, IMAGE_4BPP, i);
}

void gfx_blit_tiled_4to8_masked(const TileFill *fill, uint8_t *dst, int dst_width, const int16_t *rect)
{
    int w = rect[RECT_W];
    int h = rect[RECT_H];
    TileCursor cursor = {fill->tile_width - fill->tile_offset, fill->tile_offset};
    int row_start = rect[RECT_Y] * dst_width + rect[RECT_X];
    int mask_width = even(fill->mask_width);
    int mask_row = fill->mask_x + fill->mask_y * mask_width;
    for (int row = 0; row < h; row++, row_start += dst_width, mask_row += mask_width) {
        int skip = 0;
        int count = w;
        if (fill->mask) {
            while (count > 0 && !mask_pixel(fill->mask, mask_row + skip)) {
                count--;
                skip++;
                cursor.pos++;
                if (--cursor.left == 0) {
                    cursor.pos = 0;
                    cursor.left = fill->tile_width;
                }
            }
            for (int last = mask_row + w - 1; count > 0 && !mask_pixel(fill->mask, last); last--)
                count--;
        }
        tile_run(fill, dst, row_start + skip, count, &cursor);
        if (w & 1) { /* the tile row continues like a 4 bpp row of even length */
            if (--cursor.left == 0) {
                cursor.left = fill->tile_width;
                cursor.pos = 0;
            } else {
                cursor.pos++;
            }
        }
    }
}

void gfx_blit_tiled_4to8(const TileFill *fill, uint8_t *dst, int dst_width, const int16_t *rect)
{
    TileFill whole = *fill;
    whole.mask_x = 0;
    whole.mask_y = 0;
    whole.mask_width = rect[RECT_W];
    gfx_blit_tiled_4to8_masked(&whole, dst, dst_width, rect);
}

void gfx_blit_image(const Bitmap *source, Bitmap *target, int mirror)
{
    int16_t rect[RECT_SIZE] = {0, 0, source->width, source->height};
    target->width = source->width;
    target->height = source->height;
    target->bpp = source->bpp;
    Blit blit = {.src = source->pixels,
                 .dst = target->pixels,
                 .src_width = source->width,
                 .dst_width = source->width,
                 .src_bpp = source->bpp,
                 .dst_bpp = source->bpp,
                 .transparent = GFX_OPAQUE,
                 .mirror = mirror};
    gfx_blit(&blit, rect);
}

void gfx_blit_to_bitmap(const Bitmap *source, Bitmap *target, const int16_t *rect, int transparent,
                        const uint8_t *colormap)
{
    Blit blit = {.src = source->pixels,
                 .dst = target->pixels,
                 .src_width = source->width,
                 .dst_width = target->width,
                 .src_bpp = source->bpp,
                 .dst_bpp = IMAGE_8BPP,
                 .transparent = transparent,
                 .mirror = MIRROR_NONE,
                 .colormap = colormap};
    gfx_blit(&blit, rect);
}

void gfx_fill_rect(uint8_t *dst, const int16_t *rect, int color, int dst_width, int bpp)
{
    for (int row = 0; row < rect[RECT_H]; row++)
        for (int col = 0; col < rect[RECT_W]; col++)
            put_pixel(dst, bpp, (rect[RECT_Y] + row) * dst_width + rect[RECT_X] + col, color);
}

void gfx_fill_bitmap(Bitmap *target, int color)
{
    int width = target->bpp == IMAGE_4BPP ? even(target->width) : target->width;
    int16_t rect[RECT_SIZE] = {0, 0, (int16_t)width, target->height};
    gfx_fill_rect(target->pixels, rect, color, width, target->bpp);
}

int16_t gfx_scale_64(int16_t value, int16_t scale)
{
    return (int16_t)((value * scale + (scale >> 1)) >> DIM_SHIFT);
}

bool pal_load_color_ramps(const gdat_t *gdat)
{
    uint32_t size;
    const uint8_t *p = gdat_item(gdat, gdat_find(gdat, CAT_INTERFACE, RAMP_INDEX, RAMP_TYPE, GFX_RAMP), &size);
    if (!p || size < 1)
        return false;
    const uint8_t *end = p + size;
    ramps.count = *p++;
    if (end - p < ramps.count)
        return false;
    for (int i = 0; i < ramps.count; i++)
        ramps.length[i] = *p++;
    for (int i = 0; i < ramps.count; i++) {
        ramps.brightness[i] = p;
        p += ramps.length[i];
    }
    for (int i = 0; i < ramps.count; i++) {
        ramps.colors[i] = p;
        p += ramps.length[i];
    }
    ramps.of_color = p;
    return end - p >= RAMP_OF_COLOR_SIZE * REMAP_COLORS;
}

/* The step of a ramp whose brightness is nearest to target, the lower step on a tie. */
static int nearest_step(const uint8_t *brightness, int last, int target)
{
    int k = 0;
    for (; k < last; k++) {
        int below = target - brightness[k];
        int above = brightness[k + 1] - target;
        if (below < 0 || above < 0)
            continue;
        return below > above ? k + 1 : k;
    }
    return k;
}

uint8_t *pal_apply_dim_remap(uint8_t *table, int dim, int key1, int key2, int16_t *count)
{
    if (!table || !(uint8_t)dim)
        return table;
    int light = (uint8_t)(SCALE_FULL - (uint8_t)dim > 0 ? SCALE_FULL - (uint8_t)dim : 0);
    bool identity = *count <= 0;
    if (identity)
        *count = REMAP_COLORS;
    for (int i = 0; i < *count; i++) {
        const uint8_t *rs = ramps.of_color + RAMP_OF_COLOR_SIZE * (identity ? i : table[i]);
        int r = rs[0];
        int last = ramps.length[r] - 1;
        const uint8_t *brightness = ramps.brightness[r];
        const uint8_t *colors = ramps.colors[r];
        int target = brightness[rs[1]] * light >> DIM_SHIFT;
        int k = nearest_step(brightness, last, target);
        int color = colors[k];
        /* the keys keep their meaning: take the nearest other step, alternating below and above */
        for (int lo = k - 1, hi = k + 1; color == key1 || color == key2;) {
            if (lo < 0 || (hi <= last && brightness[hi] - target < target - brightness[lo]))
                k = hi++;
            else
                k = lo--;
            color = colors[k];
        }
        table[i] = (uint8_t)color;
    }
    if (key1 >= 0)
        table[key1] = (uint8_t)key1;
    if (key2 >= 0)
        table[key2] = (uint8_t)key2;
    return table;
}

uint8_t *pal_get_dimmed_remap(const gdat_t *gdat, uint8_t *table, const DimmedRemap *remap, int16_t *count)
{
    uint32_t size;
    const uint8_t *colors =
        gdat_item(gdat, gdat_find(gdat, remap->category, remap->index, GDAT_TYPE_REMAP, remap->entry), &size);
    if (colors && size >= REMAP_COLORS) {
        if (*count < 1) {
            *count = REMAP_COLORS;
            memcpy(table, colors, REMAP_COLORS);
        } else {
            for (int i = *count - 1; i >= 0; i--)
                table[i] = colors[table[i]];
        }
    }
    return pal_apply_dim_remap(table, remap->dim, remap->key1, remap->key2, count);
}
