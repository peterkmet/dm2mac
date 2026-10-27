/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_VIDEO_H
#define DM2_UI_VIDEO_H
/* Prefixes: video_ */

/* The frame buffer and the palette of the 320x200 screen. */

#include <stdint.h>

/* Size of the screen in pixels. */
#define SCREEN_W 320
#define SCREEN_H 200

/* Size of the palette. */
#define PALETTE_COLORS 256
#define RGB_CHANNELS   3

/* Channel order of a palette entry. */
#define RGB_RED   0
#define RGB_GREEN 1
#define RGB_BLUE  2

/* Alpha bits of an ARGB8888 pixel. */
#define ARGB_OPAQUE 0xff000000u

/* Bit positions of the red and green channels of an ARGB8888 pixel. */
#define ARGB_RED_SHIFT   16
#define ARGB_GREEN_SHIFT 8

/* A point in game screen pixels. */
typedef struct {
    int x, y;
} VideoPoint;

/* The game screen in the output: its scale and the output position of its top left corner. */
typedef struct {
    float scale, x, y;
} VideoLayout;

/* The game screen: one palette index per pixel. */
extern uint8_t g_video_fb[SCREEN_H][SCREEN_W];
/* The VGA palette: red, green, and blue of each color, 6 bits each (0 to 63). */
extern uint8_t g_video_palette[PALETTE_COLORS][RGB_CHANNELS];

/* Converts a 6-bit VGA DAC value to 8 bits: repeats the high bits in the low bits, so 0 gives 0 and 63 gives 255 (as DOSBox). */
uint8_t video_vga_to_8bit(uint8_t v);
/* ARGB8888 of a palette color. */
uint32_t video_argb(uint8_t palette_color);
/* Largest scale (fractional) that fits the output, centered (letterbox). */
VideoLayout video_layout(int output_w, int output_h);
/* Maps output pixel coordinates to game coordinates, clamped to the screen. */
VideoPoint video_map_mouse(int output_w, int output_h, int output_x, int output_y);

/* An opaque ARGB8888 pixel of 8-bit red, green, and blue. */
static inline uint32_t video_argb_pack(uint32_t red, uint32_t green, uint32_t blue)
{
    return ARGB_OPAQUE | red << ARGB_RED_SHIFT | green << ARGB_GREEN_SHIFT | blue;
}
/* The red channel of an ARGB8888 pixel. */
static inline uint8_t video_argb_red(uint32_t argb)
{
    return (uint8_t)(argb >> ARGB_RED_SHIFT);
}
/* The green channel of an ARGB8888 pixel. */
static inline uint8_t video_argb_green(uint32_t argb)
{
    return (uint8_t)(argb >> ARGB_GREEN_SHIFT);
}
/* The blue channel of an ARGB8888 pixel. */
static inline uint8_t video_argb_blue(uint32_t argb)
{
    return (uint8_t)argb;
}

#endif /* DM2_UI_VIDEO_H */
