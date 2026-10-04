#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "platform.h"
#include "scale.h"
#include "video.h"

#define WINDOW_SCALE 3
#define PATTERN_CELLS 16 /* 16 x 16 cells show the 256 palette colors */
#define VGA_MAX 63
#define EXIT_USAGE 2

/* Fills the palette with a color cube and the screen with one cell per palette color. */
static void test_pattern(void)
{
    enum { RED_STEPS = 8, GREEN_STEPS = 8, BLUE_STEPS = 4 };
    for (int i = 0; i < PALETTE_COLORS; i++) {
        palette[i][0] = (uint8_t)(i / (GREEN_STEPS * BLUE_STEPS) * VGA_MAX / (RED_STEPS - 1));
        palette[i][1] = (uint8_t)(i / BLUE_STEPS % GREEN_STEPS * VGA_MAX / (GREEN_STEPS - 1));
        palette[i][2] = (uint8_t)(i % BLUE_STEPS * VGA_MAX / (BLUE_STEPS - 1));
    }
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++)
            fb[y][x] = (uint8_t)(y * PATTERN_CELLS / SCREEN_H * PATTERN_CELLS + x * PATTERN_CELLS / SCREEN_W);
}

int main(int argc, char **argv)
{
    const char *data = "game", *filter = SDL_getenv("DM2_FILTER");
    for (int i = 1; i < argc; i++) {
        int is_data = !strcmp(argv[i], "--data"), is_filter = !strcmp(argv[i], "--filter");
        if (!is_data && !is_filter) continue;
        if (i + 1 == argc) {
            fprintf(stderr, "%s needs a value\n", argv[i]);
            return EXIT_USAGE;
        }
        if (is_data) data = argv[++i];
        else filter = argv[++i];
    }
    int factor = filter ? scale_filter_factor(filter) : 1;
    if (!factor) {
        fprintf(stderr, "unknown filter %s: use none, scale2x, or scale3x\n", filter);
        return EXIT_USAGE;
    }
    SDL_Log("data folder: %s", data);

    if (!plat_init(WINDOW_SCALE, factor)) return 1;
    test_pattern();
    for (int running = 1; running;) {
        PlatEvent e;
        while (plat_poll(&e))
            if (e.type == PLAT_QUIT || (e.type == PLAT_KEY && e.key == SDLK_ESCAPE)) running = 0;
        plat_present();
        plat_wait_tick();
    }
    plat_quit();
    return 0;
}
