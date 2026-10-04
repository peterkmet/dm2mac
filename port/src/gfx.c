#include <stdlib.h>
#include <string.h>
#include "gfx.h"

#define NIBBLE_BITS 4
#define NIBBLE_MASK 0xf
#define HIGH_NIBBLE_MASK 0xf0
#define FRACTION_BITS 7      /* the scalers step through the source in 1/128 pixel */
#define BYTE_FRACTION_BITS 8 /* the 8 bpp scaler steps in 1/256 pixel */
#define DIM_SHIFT 6          /* brightness * light / 64 */

/* The color ramps: GDAT (1, 0, 7, 2) holds the ramp count, the length of each ramp, the brightness steps
   and the colors of each ramp, then the ramp and step of each of the 256 palette colors. */
#define RAMP_CATEGORY 1
#define RAMP_INDEX 0
#define RAMP_TYPE 7
#define RAMP_ENTRY 2
#define RAMP_MAX 256
#define REMAP_TYPE 7         /* GDAT key type of a color remap table */

static struct {
    int count;
    uint8_t length[RAMP_MAX];
    const uint8_t *brightness[RAMP_MAX];
    const uint8_t *colors[RAMP_MAX];
    const uint8_t *of_color;  /* ramp and step of each color */
} ramps;

static int even(int width) { return (width + 1) & ~1; }

static int get_pixel(const uint8_t *p, int bpp, int i)
{
    if (bpp == 8) return p[i];
    return i & 1 ? p[i >> 1] & NIBBLE_MASK : p[i >> 1] >> NIBBLE_BITS;
}

static void put_pixel(uint8_t *p, int bpp, int i, int color)
{
    if (bpp == 8) {
        p[i] = (uint8_t)color;
        return;
    }
    uint8_t *b = p + (i >> 1);
    *b = i & 1 ? (*b & HIGH_NIBBLE_MASK) | (color & NIBBLE_MASK) : (*b & NIBBLE_MASK) | (uint8_t)(color << NIBBLE_BITS);
}

void gfx_blit(const uint8_t *src, uint8_t *dst, const int16_t *rect, int src_x, int src_y, int src_width,
              int dst_width, int transparent, int mirror, int src_bpp, int dst_bpp, const uint8_t *colormap)
{
    if (!rect || rect[RECT_W] <= 0 || rect[RECT_H] <= 0) return;
    if (src_bpp == 4) {
        src_width = even(src_width);
        if (dst_bpp == 4) {
            dst_width = even(dst_width);
            colormap = NULL;
        }
    } else {
        dst_bpp = 8;  /* the original treats the destination of an 8 bpp source as 8 bpp */
    }
    if (mirror > MIRROR_HV) mirror = MIRROR_NONE;
    int w = rect[RECT_W], h = rect[RECT_H];
    for (int row = 0; row < h; row++) {
        int sy = mirror & MIRROR_V ? src_y + h - 1 - row : src_y + row;
        int s = sy * src_width + src_x, d = (rect[RECT_Y] + row) * dst_width + rect[RECT_X];
        for (int col = 0; col < w; col++) {
            int color = get_pixel(src, src_bpp, mirror & MIRROR_H ? s + w - 1 - col : s + col);
            if (transparent >= 0 && color == (uint8_t)transparent) continue;
            put_pixel(dst, dst_bpp, d + col, colormap ? colormap[color] : color);
        }
    }
}

void gfx_blit_scaled_4bpp(const uint8_t *src, uint8_t *dst, int src_width, int src_height, int dst_width,
                          int dst_height, const uint8_t *colormap)
{
    int src_stride = even(src_width), dst_stride = even(dst_width);
    uint16_t step_x = (uint16_t)(((unsigned)src_width << FRACTION_BITS) / (unsigned)dst_width);
    uint16_t step_y = (uint16_t)(((unsigned)src_height << FRACTION_BITS) / (unsigned)dst_height);
    uint16_t acc_y = step_y >> 1;
    int prev = -1;
    for (int row = 0; row < dst_height; row++, acc_y += step_y) {
        int sy = acc_y >> FRACTION_BITS;
        uint8_t *out = dst + row * dst_stride / 2;
        if (sy == prev && row > 0) {
            memcpy(out, out - dst_stride / 2, dst_stride / 2);  /* same source row: copy the row above */
            continue;
        }
        prev = sy;
        int base = sy * src_stride;
        uint16_t acc_x = step_x >> 1;
        /* two pixels per destination byte; an odd width also fills the pad nibble */
        for (int i = 0; i < (dst_width + 1) / 2; i++) {
            int hi = get_pixel(src, 4, (uint16_t)((acc_x >> FRACTION_BITS) + base));
            acc_x += step_x;
            int lo = get_pixel(src, 4, (uint16_t)((acc_x >> FRACTION_BITS) + base));
            acc_x += step_x;
            if (colormap) {
                hi = colormap[hi];
                lo = colormap[lo];
            }
            out[i] = (uint8_t)(hi << NIBBLE_BITS | lo);
        }
    }
}

void gfx_blit_scaled_8bpp(const uint8_t *src, uint8_t *dst, int src_width, int src_height, int dst_width,
                          int dst_height)
{
    uint16_t step_x = (uint16_t)(((unsigned)src_width << FRACTION_BITS) / (unsigned)dst_width);
    uint16_t step_y = (uint16_t)(((unsigned)src_height << FRACTION_BITS) / (unsigned)dst_height);
    uint16_t acc_y = step_y >> 1;
    uint16_t step2 = (uint16_t)(step_x << 1);
    int prev = -1;
    for (int row = 0; row < dst_height; row++, acc_y += step_y) {
        int sy = acc_y >> FRACTION_BITS;
        uint8_t *out = dst + row * dst_width;
        if (sy == prev && row > 0) {
            memcpy(out, out - dst_width, dst_width);
            continue;
        }
        prev = sy;
        const uint8_t *in = src + sy * src_width;
        uint32_t pos = step_x;  /* half a step in 1/256 pixel */
        for (int i = 0; i < dst_width; i++, pos += step2) out[i] = in[pos >> BYTE_FRACTION_BITS];
    }
}

/* Draws count tile pixels from *tile_pos into an 8 bpp row and advances the tile position, wrapping at its end. */
static void tile_run(const uint8_t *src, uint8_t *dst, int at, int count, int *tile_left, int *tile_pos,
                     int tile_width, int transparent, const uint8_t *colormap)
{
    while (count > 0) {
        int n = *tile_left <= count ? *tile_left : count;
        for (int i = 0; i < n; i++) {
            int color = get_pixel(src, 4, *tile_pos + i);
            if (transparent < 0 || color != (uint8_t)transparent) dst[at + i] = colormap[color];
        }
        at += n;
        count -= n;
        if (n == *tile_left) {
            *tile_left = tile_width;
            *tile_pos = 0;
        } else {
            *tile_left -= n;
            *tile_pos += n;
        }
    }
}

static int mask_pixel(const uint8_t *mask, int i) { return get_pixel(mask, 4, i); }

void gfx_blit_tiled_4to8_masked(const uint8_t *src, uint8_t *dst, const uint8_t *mask, const int16_t *rect,
                                int tile_width, int tile_offset, int mask_x, int mask_y, int mask_width,
                                int dst_width, int transparent, const uint8_t *colormap)
{
    int w = rect[RECT_W], h = rect[RECT_H];
    int tile_left = tile_width - tile_offset, tile_pos = tile_offset;
    int row_start = rect[RECT_Y] * dst_width + rect[RECT_X];
    mask_width = even(mask_width);
    int mask_row = mask_x + mask_y * mask_width;
    for (int row = 0; row < h; row++, row_start += dst_width, mask_row += mask_width) {
        int skip = 0, count = w;
        if (mask) {
            while (count > 0 && !mask_pixel(mask, mask_row + skip)) {
                count--;
                skip++;
                tile_pos++;
                if (--tile_left == 0) {
                    tile_pos = 0;
                    tile_left = tile_width;
                }
            }
            for (int last = mask_row + w - 1; count > 0 && !mask_pixel(mask, last); last--) count--;
        }
        tile_run(src, dst, row_start + skip, count, &tile_left, &tile_pos, tile_width, transparent, colormap);
        if (w & 1) {  /* the tile row continues like a 4 bpp row of even length */
            if (--tile_left == 0) {
                tile_left = tile_width;
                tile_pos = 0;
            } else {
                tile_pos++;
            }
        }
    }
}

void gfx_blit_tiled_4to8(const uint8_t *src, uint8_t *dst, const uint8_t *mask, const int16_t *rect,
                         int tile_width, int tile_offset, int dst_width, int transparent, const uint8_t *colormap)
{
    gfx_blit_tiled_4to8_masked(src, dst, mask, rect, tile_width, tile_offset, 0, 0, rect[RECT_W], dst_width,
                               transparent, colormap);
}

void gfx_blit_image(const Bitmap *src, Bitmap *dst, int mirror)
{
    int16_t rect[RECT_SIZE] = {0, 0, src->width, src->height};
    dst->width = src->width;
    dst->height = src->height;
    dst->bpp = src->bpp;
    gfx_blit(src->pixels, dst->pixels, rect, 0, 0, src->width, src->width, GFX_OPAQUE, mirror, src->bpp, src->bpp, NULL);
}

void gfx_blit_to_bitmap(const Bitmap *src, Bitmap *dst, const int16_t *rect, int transparent, const uint8_t *colormap)
{
    gfx_blit(src->pixels, dst->pixels, rect, 0, 0, src->width, dst->width, transparent, MIRROR_NONE, src->bpp, 8, colormap);
}

void gfx_fill_rect(uint8_t *dst, const int16_t *rect, int color, int dst_width, int bpp)
{
    for (int row = 0; row < rect[RECT_H]; row++)
        for (int col = 0; col < rect[RECT_W]; col++)
            put_pixel(dst, bpp, (rect[RECT_Y] + row) * dst_width + rect[RECT_X] + col, color);
}

void gfx_fill_bitmap(Bitmap *dst, int color)
{
    int width = dst->bpp == 4 ? even(dst->width) : dst->width;
    int16_t rect[RECT_SIZE] = {0, 0, (int16_t)width, dst->height};
    gfx_fill_rect(dst->pixels, rect, color, width, dst->bpp);
}

int16_t gfx_scale_64(int16_t value, int16_t scale)
{
    return (int16_t)((value * scale + (scale >> 1)) >> DIM_SHIFT);
}

int pal_load_color_ramps(const gdat_t *gdat)
{
    uint32_t size;
    const uint8_t *p = gdat_item(gdat, gdat_find(gdat, RAMP_CATEGORY, RAMP_INDEX, RAMP_TYPE, RAMP_ENTRY), &size);
    if (!p || size < 1) return 0;
    const uint8_t *end = p + size;
    ramps.count = *p++;
    if (end - p < ramps.count) return 0;
    for (int i = 0; i < ramps.count; i++) ramps.length[i] = *p++;
    for (int i = 0; i < ramps.count; i++) {
        ramps.brightness[i] = p;
        p += ramps.length[i];
    }
    for (int i = 0; i < ramps.count; i++) {
        ramps.colors[i] = p;
        p += ramps.length[i];
    }
    ramps.of_color = p;
    return end - p >= 2 * REMAP_COLORS;
}

/* The step of a ramp whose brightness is nearest to target, the lower step on a tie. */
static int nearest_step(const uint8_t *brightness, int last, int target)
{
    int k = 0;
    for (; k < last; k++) {
        int below = target - brightness[k], above = brightness[k + 1] - target;
        if (below < 0 || above < 0) continue;
        return below > above ? k + 1 : k;
    }
    return k;
}

uint8_t *pal_apply_dim_remap(uint8_t *table, int dim, int key1, int key2, int16_t *count)
{
    if (!table || !(uint8_t)dim) return table;
    int light = (uint8_t)(SCALE_FULL - (uint8_t)dim > 0 ? SCALE_FULL - (uint8_t)dim : 0);
    int identity = *count <= 0;
    if (identity) *count = REMAP_COLORS;
    for (int i = 0; i < *count; i++) {
        const uint8_t *rs = ramps.of_color + 2 * (identity ? i : table[i]);
        int r = rs[0], last = ramps.length[r] - 1;
        const uint8_t *brightness = ramps.brightness[r], *colors = ramps.colors[r];
        int target = brightness[rs[1]] * light >> DIM_SHIFT;
        int k = nearest_step(brightness, last, target);
        int color = colors[k];
        /* the keys keep their meaning: take the nearest other step, alternating below and above */
        for (int lo = k - 1, hi = k + 1; color == key1 || color == key2;) {
            if (lo < 0 || (hi <= last && brightness[hi] - target < target - brightness[lo])) k = hi++;
            else k = lo--;
            color = colors[k];
        }
        table[i] = (uint8_t)color;
    }
    if (key1 >= 0) table[key1] = (uint8_t)key1;
    if (key2 >= 0) table[key2] = (uint8_t)key2;
    return table;
}

uint8_t *pal_get_dimmed_remap(const gdat_t *gdat, uint8_t *table, int category, int index, int entry, int dim,
                              int key1, int key2, int16_t *count)
{
    uint32_t size;
    const uint8_t *remap = gdat_item(gdat, gdat_find(gdat, category, index, REMAP_TYPE, entry), &size);
    if (remap && size >= REMAP_COLORS) {
        if (*count < 1) {
            *count = REMAP_COLORS;
            memcpy(table, remap, REMAP_COLORS);
        } else {
            for (int i = *count - 1; i >= 0; i--) table[i] = remap[table[i]];
        }
    }
    return pal_apply_dim_remap(table, dim, key1, key2, count);
}
