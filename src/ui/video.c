/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "video.h"

/* Bit depth of a VGA DAC value and of an output channel. */
#define VGA_DEPTH     6
#define CHANNEL_DEPTH 8

uint8_t g_video_fb[SCREEN_H][SCREEN_W] = {0}; /* initialized: keeps it out of __common, which ld warns about */
uint8_t g_video_palette[PALETTE_COLORS][RGB_CHANNELS];

uint8_t video_vga_to_8bit(uint8_t v)
{
    return (uint8_t)(v << (CHANNEL_DEPTH - VGA_DEPTH) | v >> (2 * VGA_DEPTH - CHANNEL_DEPTH));
}

uint32_t video_argb(uint8_t c)
{
    const uint8_t *p = g_video_palette[c];
    uint32_t red = video_vga_to_8bit(p[RGB_RED]);
    uint32_t green = video_vga_to_8bit(p[RGB_GREEN]);
    uint32_t blue = video_vga_to_8bit(p[RGB_BLUE]);
    return video_argb_pack(red, green, blue);
}

void video_layout(int out_w, int out_h, int *scale, int *ox, int *oy)
{
    int sx = out_w / SCREEN_W, sy = out_h / SCREEN_H;
    int s = sx < sy ? sx : sy;
    if (s < 1)
        s = 1;
    *scale = s;
    *ox = (out_w - SCREEN_W * s) / 2;
    *oy = (out_h - SCREEN_H * s) / 2;
}

static int clamp(int v, int max)
{
    return v < 0 ? 0 : v > max ? max : v;
}

VideoPoint video_map_mouse(int out_w, int out_h, int x, int y)
{
    int s, ox, oy;
    video_layout(out_w, out_h, &s, &ox, &oy);
    return (VideoPoint){clamp((x - ox) / s, SCREEN_W - 1), clamp((y - oy) / s, SCREEN_H - 1)};
}
