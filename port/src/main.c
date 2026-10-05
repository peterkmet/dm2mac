#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "draw.h"
#include "game.h"
#include "gdat.h"
#include "input.h"
#include "item.h"
#include "platform.h"
#include "save.h"
#include "scale.h"
#include "text.h"
#include "ui.h"
#include "video.h"
#include "view.h"
#include "zone.h"

#define WINDOW_SCALE 3
#define PATTERN_CELLS 16 /* 16 x 16 cells show the 256 palette colors */
#define VGA_MAX 63
#define EXIT_USAGE 2
#define PATH_SIZE 1024
#define PIT_TICKS_PER_GAME_TICK 4 /* the timer interrupt runs at 240 Hz, the game timer at 60 Hz */

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
    if (!zone_load(gdat) || !pal_load_color_ramps(gdat) || !text_init(gdat)) {
        fprintf(stderr, "%s has no screen layout, color ramps, or font\n", graphics);
        return 0;
    }
    for (int c = 0; c < PALETTE_COLORS; c++) gdat_palette_color(gdat, c, palette[c]);
    draw_init(gdat);
    view_enter_map(game.party_map);
    ui_reset();
    money_init_coin_table();
    ui_draw_game_screen();
    input_init();
    game_start_session();
    return 1;
}

/* The key code of the original keyboard layer for an SDL key: the PC scan code with shift, control, and alt bits;
   the arrows give the codes of the numeric keypad keys that the game reads for them. */
static uint16_t pc_key_code(int scancode, int mod)
{
    static const struct { uint8_t sdl, pc; } scan[] = {
        {SDL_SCANCODE_ESCAPE, 0x01}, {SDL_SCANCODE_1, 0x02}, {SDL_SCANCODE_2, 0x03}, {SDL_SCANCODE_3, 0x04},
        {SDL_SCANCODE_4, 0x05}, {SDL_SCANCODE_5, 0x06}, {SDL_SCANCODE_6, 0x07}, {SDL_SCANCODE_7, 0x08},
        {SDL_SCANCODE_8, 0x09}, {SDL_SCANCODE_9, 0x0a}, {SDL_SCANCODE_0, 0x0b}, {SDL_SCANCODE_MINUS, 0x0c},
        {SDL_SCANCODE_EQUALS, 0x0d}, {SDL_SCANCODE_BACKSPACE, 0x0e}, {SDL_SCANCODE_TAB, 0x0f}, {SDL_SCANCODE_Q, 0x10},
        {SDL_SCANCODE_W, 0x11}, {SDL_SCANCODE_E, 0x12}, {SDL_SCANCODE_R, 0x13}, {SDL_SCANCODE_T, 0x14},
        {SDL_SCANCODE_Y, 0x15}, {SDL_SCANCODE_U, 0x16}, {SDL_SCANCODE_I, 0x17}, {SDL_SCANCODE_O, 0x18},
        {SDL_SCANCODE_P, 0x19}, {SDL_SCANCODE_LEFTBRACKET, 0x1a}, {SDL_SCANCODE_RIGHTBRACKET, 0x1b},
        {SDL_SCANCODE_RETURN, 0x1c}, {SDL_SCANCODE_A, 0x1e}, {SDL_SCANCODE_S, 0x1f}, {SDL_SCANCODE_D, 0x20},
        {SDL_SCANCODE_F, 0x21}, {SDL_SCANCODE_G, 0x22}, {SDL_SCANCODE_H, 0x23}, {SDL_SCANCODE_J, 0x24},
        {SDL_SCANCODE_K, 0x25}, {SDL_SCANCODE_L, 0x26}, {SDL_SCANCODE_SEMICOLON, 0x27}, {SDL_SCANCODE_APOSTROPHE, 0x28},
        {SDL_SCANCODE_GRAVE, 0x29}, {SDL_SCANCODE_BACKSLASH, 0x2b}, {SDL_SCANCODE_Z, 0x2c}, {SDL_SCANCODE_X, 0x2d},
        {SDL_SCANCODE_C, 0x2e}, {SDL_SCANCODE_V, 0x2f}, {SDL_SCANCODE_B, 0x30}, {SDL_SCANCODE_N, 0x31},
        {SDL_SCANCODE_M, 0x32}, {SDL_SCANCODE_COMMA, 0x33}, {SDL_SCANCODE_PERIOD, 0x34}, {SDL_SCANCODE_SLASH, 0x35},
        {SDL_SCANCODE_SPACE, 0x39}, {SDL_SCANCODE_F1, 0x3b}, {SDL_SCANCODE_F2, 0x3c}, {SDL_SCANCODE_F3, 0x3d},
        {SDL_SCANCODE_F4, 0x3e}, {SDL_SCANCODE_F5, 0x3f}, {SDL_SCANCODE_F6, 0x40}, {SDL_SCANCODE_F7, 0x41},
        {SDL_SCANCODE_F8, 0x42}, {SDL_SCANCODE_F9, 0x43}, {SDL_SCANCODE_F10, 0x44}, {SDL_SCANCODE_KP_7, 0x47},
        {SDL_SCANCODE_KP_8, 0x48}, {SDL_SCANCODE_KP_9, 0x49}, {SDL_SCANCODE_KP_MINUS, 0x4a}, {SDL_SCANCODE_KP_4, 0x4b},
        {SDL_SCANCODE_KP_5, 0x4c}, {SDL_SCANCODE_KP_6, 0x4d}, {SDL_SCANCODE_KP_PLUS, 0x4e}, {SDL_SCANCODE_KP_1, 0x4f},
        {SDL_SCANCODE_KP_2, 0x50}, {SDL_SCANCODE_KP_3, 0x51}, {SDL_SCANCODE_KP_0, 0x52}, {SDL_SCANCODE_KP_PERIOD, 0x53},
    };
    int shift = mod & SDL_KMOD_SHIFT;
    switch (scancode) {
    case SDL_SCANCODE_UP: return 0x4c;
    case SDL_SCANCODE_DOWN: return 0x50;
    case SDL_SCANCODE_LEFT: return shift ? 0x4f : 0x4b;
    case SDL_SCANCODE_RIGHT: return shift ? 0x51 : 0x4d;
    }
    for (size_t i = 0; i < sizeof scan / sizeof scan[0]; i++)
        if (scan[i].sdl == scancode)
            return (uint16_t)(scan[i].pc | (shift ? KEY_SHIFT : 0) | (mod & SDL_KMOD_CTRL ? KEY_CONTROL : 0)
                              | (mod & SDL_KMOD_ALT ? KEY_ALT : 0));
    return 0;
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
    for (int running = 1, pit = 0; running && !main_loop.end;) {
        PlatEvent e;
        while (plat_poll(&e)) {
            if (e.type == PLAT_QUIT || (!save && e.type == PLAT_KEY && e.key == SDLK_ESCAPE)) running = 0;
            if (!save) continue;
            if (e.type == PLAT_KEY) {
                uint16_t key = pc_key_code(e.scancode, e.mod);
                if (key) input_key_down(key);
            } else if (e.type == PLAT_MOUSE) {
                input_mouse_click(e.x, e.y, e.button == SDL_BUTTON_RIGHT ? INPUT_BUTTON_RIGHT : INPUT_BUTTON_LEFT);
            } else if (e.type == PLAT_MOUSE_UP && e.button == SDL_BUTTON_RIGHT) {
                input_mouse_click(e.x, e.y, INPUT_RIGHT_RELEASED);
            }
        }
        if (++pit == PIT_TICKS_PER_GAME_TICK) {
            pit = 0;
            if (save) {
                plat_mouse_position(&input_mouse_x, &input_mouse_y);
                game_tick();
            }
            plat_present();
        }
        plat_wait_tick();
    }
    plat_quit();
    return 0;
}
