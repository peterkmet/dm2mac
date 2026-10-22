/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_PLATFORM_PLATFORM_H
#define DM2_PLATFORM_PLATFORM_H
/* Prefixes: plat_ */

/* The window, the sound output and the input events: the SDL3 side of the game screen. */

#include "movie.h"
#include <stdint.h>

enum { PLAT_QUIT, PLAT_KEY, PLAT_MOUSE, PLAT_MOUSE_UP };

typedef struct {
    int type;
    int key;           /* SDL keycode for PLAT_KEY */
    int scancode, mod; /* SDL scancode and modifier keys for PLAT_KEY */
    int button, x, y;  /* PLAT_MOUSE and PLAT_MOUSE_UP, x and y in game screen pixels */
} PlatEvent;

/* Opens the window at scale times the game screen; filter is the upscale factor of scale_filter_factor. */
int plat_init(int scale, int filter);
void plat_quit(void);
/* Shows g_video_fb with the palette, scaled into the window. */
void plat_present(void);
/* Shows a movie picture on the screen of its player, in the largest 4:3 rectangle of the window. */
void plat_present_movie(const MoviePicture *p);
/* Returns 1 with the next key, mouse button, or quit event; mouse motion only moves the pointer. */
int plat_poll(PlatEvent *e);
/* Mouse position in game screen pixels. */
void plat_mouse_position(int *x, int *y);
/* Sets the window and Dock icon from w by h ARGB pixels. */
void plat_set_icon(const uint32_t *argb, int w, int h);
/* Sleeps until the next timer tick. */
void plat_wait_tick(void);
/* Mixes into frames of interleaved stereo at SOUND_OUTPUT_RATE; the music mixer fills the frames and runs first, the sound mixer adds. */
typedef void PlatMixer(int16_t *stereo, int frames);
/* Sets the music source that the audio output mixes before the sound effects; NULL for none. */
void plat_audio_music(PlatMixer *mix);
/* Feeds the mixed music and sound effects to the audio device; plat_wait_tick calls it. */
void plat_audio_pump(void);

#endif /* DM2_PLATFORM_PLATFORM_H */
