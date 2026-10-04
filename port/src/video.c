#include "video.h"

#define VGA_BITS 6
#define CHANNEL_BITS 8
#define ARGB_OPAQUE 0xff000000u

uint8_t fb[SCREEN_H][SCREEN_W] = { 0 }; /* initialized: keeps it out of __common, which ld warns about */
uint8_t palette[PALETTE_COLORS][RGB_CHANNELS];

/* Repeats the high bits in the low bits, so 0 gives 0 and 63 gives 255. */
uint8_t video_vga_to_8bit(uint8_t v)
{
    return (uint8_t)(v << (CHANNEL_BITS - VGA_BITS) | v >> (2 * VGA_BITS - CHANNEL_BITS));
}

uint32_t video_argb(uint8_t c)
{
    const uint8_t *p = palette[c];
    return ARGB_OPAQUE | (uint32_t)video_vga_to_8bit(p[0]) << 16 | (uint32_t)video_vga_to_8bit(p[1]) << 8 |
           video_vga_to_8bit(p[2]);
}

void video_layout(int out_w, int out_h, int *scale, int *ox, int *oy)
{
    int sx = out_w / SCREEN_W, sy = out_h / SCREEN_H;
    int s = sx < sy ? sx : sy;
    if (s < 1) s = 1;
    *scale = s;
    *ox = (out_w - SCREEN_W * s) / 2;
    *oy = (out_h - SCREEN_H * s) / 2;
}

static int clamp(int v, int max) { return v < 0 ? 0 : v > max ? max : v; }

void video_map_mouse(int out_w, int out_h, int x, int y, int *gx, int *gy)
{
    int s, ox, oy;
    video_layout(out_w, out_h, &s, &ox, &oy);
    *gx = clamp((x - ox) / s, SCREEN_W - 1);
    *gy = clamp((y - oy) / s, SCREEN_H - 1);
}
