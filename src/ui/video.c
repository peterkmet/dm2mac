/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "video.h"
#include <math.h>
#include "num.h"

/* Bit depth of a VGA DAC value and of an output channel. */
#define VGA_DEPTH     6
#define CHANNEL_DEPTH 8

uint8_t g_video_fb[SCREEN_H][SCREEN_W] = {0}; /* initialized: keeps it out of __common, which ld warns about */
uint8_t g_video_palette[PALETTE_COLORS][RGB_CHANNELS];

/* ---- Palette colors ---- */

uint8_t video_vga_to_8bit(uint8_t v)
{
    return (uint8_t)(v << (CHANNEL_DEPTH - VGA_DEPTH) | v >> (2 * VGA_DEPTH - CHANNEL_DEPTH));
}

uint32_t video_argb(uint8_t palette_color)
{
    const uint8_t *p = g_video_palette[palette_color];
    uint8_t red = video_vga_to_8bit(p[RGB_RED]);
    uint8_t green = video_vga_to_8bit(p[RGB_GREEN]);
    uint8_t blue = video_vga_to_8bit(p[RGB_BLUE]);
    return video_argb_pack(red, green, blue);
}

/* ---- Output layout ---- */

VideoLayout video_layout(int output_w, int output_h)
{
    float sx = (float)output_w / SCREEN_W;
    float sy = (float)output_h / SCREEN_H;
    float s = sx < sy ? sx : sy;
    if (s <= 0)
        s = 1; /* minimized window: avoid a zero scale */
    return (VideoLayout){s, (output_w - SCREEN_W * s) / 2, (output_h - SCREEN_H * s) / 2};
}

VideoPoint video_map_mouse(int output_w, int output_h, int output_x, int output_y)
{
    VideoLayout layout = video_layout(output_w, output_h);
    return (VideoPoint){clamp_int(0, (int)floorf((output_x - layout.x) / layout.scale), SCREEN_W - 1),
                        clamp_int(0, (int)floorf((output_y - layout.y) / layout.scale), SCREEN_H - 1)};
}
