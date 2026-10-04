#include <SDL3/SDL.h>
#include "platform.h"
#include "scale.h"
#include "video.h"

#define PIT_CLOCK_HZ 1193182 /* PIT input clock */
#define PIT_DIVISOR 65536    /* DOS default divisor, about 18.2065 Hz; Step 5 confirms the rate of SKULL.EXE */
#define TICK_NS ((Uint64)SDL_NS_PER_SECOND * PIT_DIVISOR / PIT_CLOCK_HZ)
#define ARGB_BYTES 4
#define FILTER_MAX 3

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static Uint64 next_tick;
static int mouse_x, mouse_y;
static int filter = 1; /* upscale factor: 1 none, 2 Scale2x, 3 Scale3x */
static uint8_t big[SCREEN_H * FILTER_MAX][SCREEN_W * FILTER_MAX];

int plat_init(int scale, int filter_factor)
{
    filter = filter_factor;
    if (!SDL_Init(SDL_INIT_VIDEO) ||
        !SDL_CreateWindowAndRenderer("Dungeon Master II", SCREEN_W * scale, SCREEN_H * scale,
                                     SDL_WINDOW_RESIZABLE, &win, &ren) ||
        !(tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                SCREEN_W * filter, SCREEN_H * filter))) {
        SDL_Log("SDL: %s", SDL_GetError());
        return 0;
    }
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
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
    const uint8_t *src = filter == 1 ? &fb[0][0] : &big[0][0];
    if (filter == 2) scale2x(&fb[0][0], SCREEN_W, SCREEN_H, &big[0][0]);
    if (filter == 3) scale3x(&fb[0][0], SCREEN_W, SCREEN_H, &big[0][0]);
    for (int i = 0; i < PALETTE_COLORS; i++) pal[i] = video_argb((uint8_t)i);
    if (!SDL_LockTexture(tex, NULL, (void **)&px, &pitch)) return;
    int stride = pitch / ARGB_BYTES;
    for (int y = 0; y < SCREEN_H * filter; y++)
        for (int x = 0; x < tw; x++) px[y * stride + x] = pal[src[y * tw + x]];
    SDL_UnlockTexture(tex);

    SDL_GetCurrentRenderOutputSize(ren, &w, &h);
    video_layout(w, h, &s, &ox, &oy);
    SDL_FRect dst = { ox, oy, SCREEN_W * s, SCREEN_H * s };
    SDL_SetRenderDrawColor(ren, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(ren);
    SDL_RenderTexture(ren, tex, NULL, &dst);
    SDL_RenderPresent(ren);
}

int plat_poll(PlatEvent *e)
{
    SDL_Event ev;
    int w, h;
    while (SDL_PollEvent(&ev)) {
        *e = (PlatEvent){ 0 };
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            e->type = PLAT_QUIT;
            return 1;
        case SDL_EVENT_KEY_DOWN:
            if (ev.key.repeat) break;
            e->type = PLAT_KEY;
            e->key = ev.key.key;
            e->modifiers = ev.key.mod;
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

void plat_mouse_position(int *x, int *y)
{
    *x = mouse_x;
    *y = mouse_y;
}

void plat_wait_tick(void)
{
    plat_audio_pump();
    Uint64 now = SDL_GetTicksNS();
    if (now < next_tick) SDL_DelayPrecise(next_tick - now);
    else next_tick = now; /* fell behind: resync instead of bursting */
    next_tick += TICK_NS;
}

/* Audio stub: the port has no sound output yet. */
void plat_audio_pump(void)
{
}
