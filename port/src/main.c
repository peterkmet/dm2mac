#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "draw.h"
#include "gdat.h"
#include "platform.h"
#include "save.h"
#include "scale.h"
#include "video.h"
#include "view.h"
#include "zone.h"

#define WINDOW_SCALE 3
#define PATTERN_CELLS 16 /* 16 x 16 cells show the 256 palette colors */
#define VGA_MAX 63
#define EXIT_USAGE 2
#define ZONE_VIEWPORT 7
#define PATH_SIZE 1024

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

/* Loads a save and draws the dungeon view of the party into the screen; returns 0 with a message on an error. */
static int show_save(const char *data, const char *path)
{
    char graphics[PATH_SIZE];
    snprintf(graphics, sizeof graphics, "%s/DATA/GRAPHICS.DAT", data);
    static gdat_t *gdat;
    if (gdat_load(graphics, &gdat) != GDAT_OK) {
        fprintf(stderr, "cannot read %s\n", graphics);
        return 0;
    }
    if (save_load(path, gdat) != SAVE_OK) {
        fprintf(stderr, "cannot load the save %s\n", path);
        return 0;
    }
    if (!zone_load(gdat) || !pal_load_color_ramps(gdat)) {
        fprintf(stderr, "%s has no screen layout or color ramps\n", graphics);
        return 0;
    }
    for (int c = 0; c < PALETTE_COLORS; c++) gdat_palette_color(gdat, c, palette[c]);
    draw_init(gdat);
    view_enter_map(game.party_map);
    view_draw_dungeon(game.party_dir, game.party_x, game.party_y);
    int16_t r[RECT_SIZE];
    if (zone_get_rect(ZONE_VIEWPORT, r))
        for (int y = 0; y < VIEWPORT_HEIGHT; y++)
            memcpy(&fb[r[RECT_Y] + y][r[RECT_X]], view_viewport.pixels + y * VIEWPORT_WIDTH, VIEWPORT_WIDTH);
    return 1;
}

int main(int argc, char **argv)
{
    const char *data = "game", *filter = SDL_getenv("DM2_FILTER"), *save = NULL;
    for (int i = 1; i < argc; i++) {
        int is_data = !strcmp(argv[i], "--data"), is_filter = !strcmp(argv[i], "--filter");
        int is_save = !strcmp(argv[i], "--save");
        if (!is_data && !is_filter && !is_save) continue;
        if (i + 1 == argc) {
            fprintf(stderr, "%s needs a value\n", argv[i]);
            return EXIT_USAGE;
        }
        if (is_data) data = argv[++i];
        else if (is_filter) filter = argv[++i];
        else save = argv[++i];
    }
    int factor = filter ? scale_filter_factor(filter) : 1;
    if (!factor) {
        fprintf(stderr, "unknown filter %s: use none, scale2x, or scale3x\n", filter);
        return EXIT_USAGE;
    }
    SDL_Log("data folder: %s", data);

    if (save && !show_save(data, save)) return 1;
    if (!plat_init(WINDOW_SCALE, factor)) return 1;
    if (!save) test_pattern();
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
