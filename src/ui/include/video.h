/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_VIDEO_H
#define DM2_UI_VIDEO_H
/* Prefixes: video_ */

/* The frame buffer and the palette of the 320x200 screen. */

#include <stdint.h>

#define SCREEN_W         320
#define SCREEN_H         200
#define PALETTE_COLORS   256
#define RGB_CHANNELS     3
#define ARGB_OPAQUE      0xff000000u
#define ARGB_RED_SHIFT   16
#define ARGB_GREEN_SHIFT 8

/* The game screen: one palette index per pixel. */
extern uint8_t fb[SCREEN_H][SCREEN_W];
/* The VGA palette: red, green, and blue of each color, 6 bits each (0 to 63). */
extern uint8_t palette[PALETTE_COLORS][RGB_CHANNELS];

/* Converts a 6-bit VGA DAC value to 8 bits. */
uint8_t video_vga_to_8bit(uint8_t v);
/* ARGB8888 of palette color c. */
uint32_t video_argb(uint8_t c);
/* Largest integer scale that fits the output, centered (letterbox). */
void video_layout(int out_w, int out_h, int *scale, int *ox, int *oy);
/* Maps output pixel coordinates to game coordinates, clamped to the screen. */
void video_map_mouse(int out_w, int out_h, int x, int y, int *gx, int *gy);

/* An opaque ARGB8888 pixel of 8-bit red, green, and blue, and its channels. */
static inline uint32_t video_argb_pack(uint32_t red, uint32_t green, uint32_t blue)
{
    return ARGB_OPAQUE | red << ARGB_RED_SHIFT | green << ARGB_GREEN_SHIFT | blue;
}
static inline uint8_t video_argb_red(uint32_t argb)
{
    return (uint8_t)(argb >> ARGB_RED_SHIFT);
}
static inline uint8_t video_argb_green(uint32_t argb)
{
    return (uint8_t)(argb >> ARGB_GREEN_SHIFT);
}
static inline uint8_t video_argb_blue(uint32_t argb)
{
    return (uint8_t)argb;
}

#endif /* DM2_UI_VIDEO_H */
