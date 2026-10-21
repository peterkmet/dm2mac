/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "platform.h"
#include "movie.h"
#include "music.h"
#include "sound.h"
#include "video.h"
#include "scale.h"
#include <SDL3/SDL.h>

_Static_assert(MUSIC_SAMPLE_RATE == SOUND_OUTPUT_RATE, "the audio stream plays the music at the sound output rate");

#define PIT_CLOCK_HZ       1193182 /* PIT input clock */
#define PIT_DIVISOR        4971    /* 240 Hz, see docs/skull-formats.md section 6 */
#define TICK_NS            ((Uint64)SDL_NS_PER_SECOND * PIT_DIVISOR / PIT_CLOCK_HZ)
#define ARGB_BYTES         4
#define FILTER_MAX         SCALE3X_FACTOR
#define AUDIO_CHANNELS     2
#define AUDIO_QUEUE_FRAMES 2048 /* about 46 ms at 44100 Hz */
#define SCREEN_ASPECT_W    4    /* a VGA monitor shows every mode on a 4:3 tube */
#define SCREEN_ASPECT_H    3

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static Uint64 next_tick;
static int mouse_x, mouse_y;
static int filter = 1; /* upscale factor: 1 none, 2 Scale2x, 3 Scale3x */
static uint8_t big[SCREEN_H * FILTER_MAX][SCREEN_W * FILTER_MAX];
static SDL_AudioStream *audio;
static PlatMixer *music_mixer;
static SDL_Texture *movie_tex;
static int movie_tex_w, movie_tex_h;

int plat_init(int scale, int filter_factor)
{
    filter = filter_factor;
    SDL_SetHint(SDL_HINT_FORCE_RAISEWINDOW, "1");
    if (!SDL_Init(SDL_INIT_VIDEO) ||
        !SDL_CreateWindowAndRenderer("Dungeon Master II", SCREEN_W * scale, SCREEN_H * scale, SDL_WINDOW_RESIZABLE,
                                     &win, &ren) ||
        !(tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W * filter,
                                  SCREEN_H * filter))) {
        SDL_Log("SDL: %s", SDL_GetError());
        return 0;
    }
    SDL_RaiseWindow(win);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    SDL_HideCursor(); /* the game draws its own cursor into fb */
    SDL_AudioSpec spec = {SDL_AUDIO_S16, AUDIO_CHANNELS, SOUND_OUTPUT_RATE};
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO) ||
        !(audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL)))
        SDL_Log("SDL audio: %s, playing without sound", SDL_GetError());
    else
        SDL_ResumeAudioStreamDevice(audio);
    next_tick = SDL_GetTicksNS() + TICK_NS;
    return 1;
}

void plat_quit(void)
{
    SDL_Quit();
}

void plat_present(void)
{
    uint32_t pal[PALETTE_COLORS], *px;
    int pitch, w, h, s, ox, oy, tw = SCREEN_W * filter;
    const uint8_t *src = filter == SCALE_NONE_FACTOR ? &fb[0][0] : &big[0][0];
    if (filter == SCALE2X_FACTOR)
        scale2x(&fb[0][0], SCREEN_W, SCREEN_H, &big[0][0]);
    if (filter == SCALE3X_FACTOR)
        scale3x(&fb[0][0], SCREEN_W, SCREEN_H, &big[0][0]);
    for (int i = 0; i < PALETTE_COLORS; i++)
        pal[i] = video_argb((uint8_t)i);
    if (!SDL_LockTexture(tex, NULL, (void **)&px, &pitch))
        return;
    int stride = pitch / ARGB_BYTES;
    for (int y = 0; y < SCREEN_H * filter; y++)
        for (int x = 0; x < tw; x++)
            px[y * stride + x] = pal[src[y * tw + x]];
    SDL_UnlockTexture(tex);

    SDL_GetCurrentRenderOutputSize(ren, &w, &h);
    video_layout(w, h, &s, &ox, &oy);
    SDL_FRect dst = {ox, oy, SCREEN_W * s, SCREEN_H * s};
    SDL_SetRenderDrawColor(ren, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(ren);
    SDL_RenderTexture(ren, tex, NULL, &dst);
    SDL_RenderPresent(ren);
}

/* The largest 4:3 rectangle in the middle of the window: the tube of a VGA monitor. */
static SDL_FRect movie_screen(int w, int h)
{
    float sw = (float)w, sh = (float)w * SCREEN_ASPECT_H / SCREEN_ASPECT_W;
    if (sh > h)
        sh = (float)h, sw = (float)h * SCREEN_ASPECT_W / SCREEN_ASPECT_H;
    return (SDL_FRect){(w - sw) / 2, (h - sh) / 2, sw, sh};
}

void plat_present_movie(const MoviePicture *p)
{
    uint32_t pal[PALETTE_COLORS], *px;
    int pitch, w, h;
    if (!movie_tex || movie_tex_w != p->width || movie_tex_h != p->height) {
        if (movie_tex)
            SDL_DestroyTexture(movie_tex);
        movie_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, p->width, p->height);
        if (!movie_tex)
            return;
        movie_tex_w = p->width, movie_tex_h = p->height;
        SDL_SetTextureScaleMode(movie_tex, SDL_SCALEMODE_LINEAR);
    }
    for (int i = 0; i < PALETTE_COLORS; i++)
        pal[i] = video_argb_pack(video_vga_to_8bit(p->palette[i][0]), video_vga_to_8bit(p->palette[i][1]),
                                 video_vga_to_8bit(p->palette[i][2]));
    if (!SDL_LockTexture(movie_tex, NULL, (void **)&px, &pitch))
        return;
    for (int y = 0; y < p->height; y++)
        for (int x = 0; x < p->width; x++)
            px[y * (pitch / ARGB_BYTES) + x] = pal[p->pixels[y * p->width + x]];
    SDL_UnlockTexture(movie_tex);

    SDL_GetCurrentRenderOutputSize(ren, &w, &h);
    SDL_FRect screen = movie_screen(w, h);
    float kx = screen.w / p->screen_width, ky = screen.h / p->screen_height;
    SDL_FRect dst = {screen.x + (p->screen_width - p->shown_width) / 2 * kx,
                     screen.y + (p->screen_height - p->height) / 2 * ky, p->shown_width * kx, p->height * ky};
    SDL_SetRenderDrawColor(ren, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(ren);
    SDL_RenderTexture(ren, movie_tex, NULL, &dst);
    SDL_RenderPresent(ren);
}

int plat_poll(PlatEvent *e)
{
    SDL_Event ev;
    int w, h;
    while (SDL_PollEvent(&ev)) {
        *e = (PlatEvent){0};
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            e->type = PLAT_QUIT;
            return 1;
        case SDL_EVENT_KEY_DOWN:
            e->type = PLAT_KEY;
            e->key = ev.key.key;
            e->scancode = ev.key.scancode;
            e->mod = ev.key.mod;
            return 1;
        case SDL_EVENT_MOUSE_MOTION:
            SDL_ConvertEventToRenderCoordinates(ren, &ev); /* window points -> output pixels */
            SDL_GetCurrentRenderOutputSize(ren, &w, &h);
            video_map_mouse(w, h, (int)ev.motion.x, (int)ev.motion.y, &mouse_x, &mouse_y);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            SDL_ConvertEventToRenderCoordinates(ren, &ev);
            SDL_GetCurrentRenderOutputSize(ren, &w, &h);
            e->type = ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? PLAT_MOUSE : PLAT_MOUSE_UP;
            e->button = ev.button.button;
            video_map_mouse(w, h, (int)ev.button.x, (int)ev.button.y, &e->x, &e->y);
            mouse_x = e->x;
            mouse_y = e->y;
            return 1;
        }
    }
    return 0;
}

void plat_set_icon(const uint32_t *argb, int w, int h)
{
    SDL_Surface *icon = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ARGB8888, (void *)argb, w * ARGB_BYTES);
    if (!icon)
        return;
    SDL_SetWindowIcon(win, icon);
    SDL_DestroySurface(icon);
}

void plat_mouse_position(int *x, int *y)
{
    *x = mouse_x;
    *y = mouse_y;
}

void plat_wait_tick(void)
{
    plat_audio_pump();
    Uint64 now = SDL_GetTicksNS();
    if (now < next_tick)
        SDL_DelayPrecise(next_tick - now);
    else
        next_tick = now; /* fell behind: resync instead of bursting */
    next_tick += TICK_NS;
}

void plat_audio_music(PlatMixer *mix)
{
    music_mixer = mix;
}

/* Tops up the audio queue on the main thread, so that the mixers need no lock. */
void plat_audio_pump(void)
{
    static int16_t buf[AUDIO_QUEUE_FRAMES * AUDIO_CHANNELS];
    const int frame_bytes = sizeof(buf[0]) * AUDIO_CHANNELS;
    if (!audio)
        return;
    int frames = AUDIO_QUEUE_FRAMES - SDL_GetAudioStreamQueued(audio) / frame_bytes;
    if (frames <= 0)
        return;
    SDL_memset(buf, 0, frames * frame_bytes);
    if (music_mixer)
        music_mixer(buf, frames);
    sound_mix(buf, frames);
    SDL_PutAudioStreamData(audio, buf, frames * frame_bytes);
}
