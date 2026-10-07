#ifndef VIDEO_H
#define VIDEO_H
#include <stdint.h>

#define SCREEN_W 320
#define SCREEN_H 200
#define PALETTE_COLORS 256
#define RGB_CHANNELS 3

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

#endif
