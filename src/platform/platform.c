/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "platform.h"
#include <SDL3/SDL.h>
#include "movie.h"
#include "music.h"
#include "os.h"
#include "scale.h"
#include "sound.h"
#include "video.h"

_Static_assert(MUSIC_SAMPLE_RATE == SOUND_OUTPUT_RATE, "the audio stream plays the music at the sound output rate");

/* The timer: the PIT ticks of the original. */
#define PIT_CLOCK_HZ 1193182 /* PIT input clock */
#define PIT_DIVISOR  4971    /* 240 Hz, see docs/formats/runtime.md */
#define TICK_NS      ((Uint64)SDL_NS_PER_SECOND * PIT_DIVISOR / PIT_CLOCK_HZ)

/* The sizes of the window textures. */
#define ARGB_BYTES      4
#define FILTER_MAX      SCALE3X_FACTOR
#define SCREEN_ASPECT_W 4 /* a VGA monitor shows every mode on a 4:3 tube */
#define SCREEN_ASPECT_H 3

/* The audio stream. */
#define AUDIO_CHANNELS     2
#define AUDIO_QUEUE_FRAMES 2048 /* about 46 ms at 44100 Hz */

/* The channels of a movie palette color. */
#define CHANNEL_RED   0
#define CHANNEL_GREEN 1
#define CHANNEL_BLUE  2

/* The window, its textures, the pointer, the timer, and the audio stream. */
static struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    Uint64 next_tick;
    int mouse_x, mouse_y;
    int filter; /* upscale factor: 1 none, 2 Scale2x, 3 Scale3x */
    uint8_t big[SCREEN_H * FILTER_MAX][SCREEN_W * FILTER_MAX];
    SDL_AudioStream *audio;
    int16_t audio_frames[AUDIO_QUEUE_FRAMES * AUDIO_CHANNELS]; /* the mixed frames that top up the audio queue */
    PlatMixer *music_mixer;
    SDL_Texture *movie_tex;
    int movie_tex_w, movie_tex_h;
    int movie_shown_w, movie_screen_w, movie_screen_h;
    bool movie_last; /* the last present was a movie picture */
} platform = {.filter = SCALE_NONE_FACTOR};

/* Setup */

/* The largest 4:3 rectangle in the middle of the window: the tube of a VGA monitor. */
static SDL_FRect tube_rect(int w, int h)
{
    float sw = (float)w;
    float sh = (float)w * SCREEN_ASPECT_H / SCREEN_ASPECT_W;
    if (sh > h) {
        sh = (float)h;
        sw = (float)h * SCREEN_ASPECT_W / SCREEN_ASPECT_H;
    }
    return (SDL_FRect){(w - sw) / 2, (h - sh) / 2, sw, sh};
}

/* The window rectangle of the last movie picture, centered on the tube. */
static SDL_FRect last_picture_rect(int w, int h)
{
    SDL_FRect screen = tube_rect(w, h);
    float kx = screen.w / platform.movie_screen_w;
    float ky = screen.h / platform.movie_screen_h;
    return (SDL_FRect){screen.x + (platform.movie_screen_w - platform.movie_shown_w) / 2 * kx,
                       screen.y + (platform.movie_screen_h - platform.movie_tex_h) / 2 * ky,
                       platform.movie_shown_w * kx, platform.movie_tex_h * ky};
}

/* Draws the frame texture letterboxed into the window. */
static void render_frame(void)
{
    int w, h;
    float s, ox, oy;
    SDL_GetCurrentRenderOutputSize(platform.ren, &w, &h);
    video_layout(w, h, &s, &ox, &oy);
    SDL_FRect dst = {ox, oy, SCREEN_W * s, SCREEN_H * s};
    SDL_Texture *tex = platform.tex;
    if (platform.movie_last) {
        dst = last_picture_rect(w, h);
        tex = platform.movie_tex;
    }
    SDL_SetRenderDrawColor(platform.ren, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(platform.ren);
    SDL_RenderTexture(platform.ren, tex, NULL, &dst);
    SDL_RenderPresent(platform.ren);
}

/* Redraws the last frame while macOS blocks the main loop during a live window resize. */
static bool SDLCALL on_expose(void *userdata, SDL_Event *ev)
{
    (void)userdata;
    if (ev->type == SDL_EVENT_WINDOW_EXPOSED && platform.tex)
        render_frame();
    return true;
}

bool plat_init(int scale, int filter_factor)
{
    platform.filter = filter_factor;
    SDL_SetHint(SDL_HINT_FORCE_RAISEWINDOW, "1");
    if (!SDL_Init(SDL_INIT_VIDEO) ||
        !SDL_CreateWindowAndRenderer("DM2Mac", SCREEN_W * scale, SCREEN_H * scale, SDL_WINDOW_RESIZABLE, &platform.win,
                                     &platform.ren) ||
        !(platform.tex = SDL_CreateTexture(platform.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                           SCREEN_W * platform.filter, SCREEN_H * platform.filter))) {
        os_log("SDL: %s", SDL_GetError());
        return false;
    }
    SDL_RaiseWindow(platform.win);
    SDL_SetTextureScaleMode(platform.tex, SDL_SCALEMODE_PIXELART); /* even pixels at a fractional scale */
    SDL_AddEventWatch(on_expose, NULL);
    SDL_HideCursor(); /* the game draws its own cursor into g_video_fb */
    SDL_AudioSpec spec = {SDL_AUDIO_S16, AUDIO_CHANNELS, SOUND_OUTPUT_RATE};
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO) ||
        !(platform.audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL)))
        os_log("SDL audio: %s, playing without sound", SDL_GetError());
    else
        SDL_ResumeAudioStreamDevice(platform.audio);
    platform.next_tick = SDL_GetTicksNS() + TICK_NS;
    return true;
}

bool plat_set_filter(int filter_factor)
{
    SDL_Texture *tex = SDL_CreateTexture(platform.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                         SCREEN_W * filter_factor, SCREEN_H * filter_factor);
    if (!tex) {
        os_log("SDL: %s", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_PIXELART);
    SDL_DestroyTexture(platform.tex);
    platform.tex = tex;
    platform.filter = filter_factor;
    plat_present();
    return true;
}

void plat_quit(void)
{
    SDL_Quit();
}

void plat_set_icon(const uint32_t *argb, int w, int h)
{
    SDL_Surface *icon = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ARGB8888, (void *)argb, w * ARGB_BYTES);
    if (!icon)
        return;
    SDL_SetWindowIcon(platform.win, icon);
    SDL_DestroySurface(icon);
}

/* Screen */

void plat_present(void)
{
    uint32_t pal[PALETTE_COLORS], *px;
    int pitch, tw = SCREEN_W * platform.filter;
    const uint8_t *src = platform.filter == SCALE_NONE_FACTOR ? &g_video_fb[0][0] : &platform.big[0][0];
    if (platform.filter == SCALE2X_FACTOR)
        scale2x(&g_video_fb[0][0], SCREEN_W, SCREEN_H, &platform.big[0][0]);
    if (platform.filter == SCALE3X_FACTOR)
        scale3x(&g_video_fb[0][0], SCREEN_W, SCREEN_H, &platform.big[0][0]);
    for (int i = 0; i < PALETTE_COLORS; i++)
        pal[i] = video_argb((uint8_t)i);
    if (!SDL_LockTexture(platform.tex, NULL, (void **)&px, &pitch))
        return;
    int stride = pitch / ARGB_BYTES;
    for (int y = 0; y < SCREEN_H * platform.filter; y++)
        for (int x = 0; x < tw; x++)
            px[y * stride + x] = pal[src[y * tw + x]];
    SDL_UnlockTexture(platform.tex);
    platform.movie_last = false;
    render_frame();
}

void plat_present_movie(const MoviePicture *p)
{
    uint32_t pal[PALETTE_COLORS], *px;
    int pitch;
    if (!platform.movie_tex || platform.movie_tex_w != p->width || platform.movie_tex_h != p->height) {
        if (platform.movie_tex)
            SDL_DestroyTexture(platform.movie_tex);
        platform.movie_tex =
            SDL_CreateTexture(platform.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, p->width, p->height);
        if (!platform.movie_tex)
            return;
        platform.movie_tex_w = p->width;
        platform.movie_tex_h = p->height;
        SDL_SetTextureScaleMode(platform.movie_tex, SDL_SCALEMODE_LINEAR);
    }
    for (int i = 0; i < PALETTE_COLORS; i++)
        pal[i] = video_argb_pack(video_vga_to_8bit(p->palette[i][CHANNEL_RED]),
                                 video_vga_to_8bit(p->palette[i][CHANNEL_GREEN]),
                                 video_vga_to_8bit(p->palette[i][CHANNEL_BLUE]));
    if (!SDL_LockTexture(platform.movie_tex, NULL, (void **)&px, &pitch))
        return;
    for (int y = 0; y < p->height; y++)
        for (int x = 0; x < p->width; x++)
            px[y * (pitch / ARGB_BYTES) + x] = pal[p->pixels[y * p->width + x]];
    SDL_UnlockTexture(platform.movie_tex);

    platform.movie_shown_w = p->shown_width;
    platform.movie_screen_w = p->screen_width;
    platform.movie_screen_h = p->screen_height;
    platform.movie_last = true;
    render_frame();
}

/* Input */

bool plat_poll(PlatEvent *e)
{
    SDL_Event ev;
    int w, h;
    VideoPoint at;
    while (SDL_PollEvent(&ev)) {
        *e = (PlatEvent){0};
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            e->type = PLAT_QUIT;
            return true;
        case SDL_EVENT_KEY_DOWN:
            e->type = PLAT_KEY;
            e->key = ev.key.key;
            e->scancode = ev.key.scancode;
            e->mod = ev.key.mod;
            return true;
        case SDL_EVENT_MOUSE_MOTION:
            SDL_ConvertEventToRenderCoordinates(platform.ren, &ev); /* window points -> output pixels */
            SDL_GetCurrentRenderOutputSize(platform.ren, &w, &h);
            at = video_map_mouse(w, h, (int)ev.motion.x, (int)ev.motion.y);
            platform.mouse_x = at.x;
            platform.mouse_y = at.y;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            SDL_ConvertEventToRenderCoordinates(platform.ren, &ev);
            SDL_GetCurrentRenderOutputSize(platform.ren, &w, &h);
            e->type = ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? PLAT_MOUSE : PLAT_MOUSE_UP;
            e->button = ev.button.button;
            at = video_map_mouse(w, h, (int)ev.button.x, (int)ev.button.y);
            e->x = at.x;
            e->y = at.y;
            platform.mouse_x = e->x;
            platform.mouse_y = e->y;
            return true;
        }
    }
    return false;
}

void plat_mouse_position(int *out_x, int *out_y)
{
    *out_x = platform.mouse_x;
    *out_y = platform.mouse_y;
}

/* Timer and sound */

/* Tops up the audio queue on the main thread, so that the mixers need no lock. */
static void pump_audio(void)
{
    int16_t *buf = platform.audio_frames;
    const int frame_bytes = sizeof platform.audio_frames[0] * AUDIO_CHANNELS;
    if (!platform.audio)
        return;
    int frames = AUDIO_QUEUE_FRAMES - SDL_GetAudioStreamQueued(platform.audio) / frame_bytes;
    if (frames <= 0)
        return;
    SDL_memset(buf, 0, frames * frame_bytes);
    if (platform.music_mixer)
        platform.music_mixer(buf, frames);
    sound_mix(buf, frames);
    SDL_PutAudioStreamData(platform.audio, buf, frames * frame_bytes);
}

void plat_wait_tick(void)
{
    pump_audio();
    Uint64 now = SDL_GetTicksNS();
    if (now < platform.next_tick)
        SDL_DelayPrecise(platform.next_tick - now);
    else
        platform.next_tick = now; /* fell behind: resync instead of bursting */
    platform.next_tick += TICK_NS;
}

void plat_audio_music(PlatMixer *mix)
{
    platform.music_mixer = mix;
}
