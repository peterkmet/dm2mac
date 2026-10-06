#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <SDL3/SDL.h>
#include "draw.h"
#include "dungeon.h"
#include "game.h"
#include "gdat.h"
#include "input.h"
#include "item.h"
#include "music.h"
#include "platform.h"
#include "save.h"
#include "session.h"
#include "scale.h"
#include "text.h"
#include "ui.h"
#include "video.h"
#include "view.h"
#include "zone.h"

#define WINDOW_SCALE 3
#define EXIT_USAGE 2
#define PATH_SIZE 1024
#define PIT_TICKS_PER_GAME_TICK 4 /* the timer interrupt runs at 240 Hz, the game timer at 60 Hz */

/* Shows fb with the cursor image of input_cursor_at at the mouse pointer (the original mouse cursor). */
static void present_with_cursor(void)
{
    static uint8_t saved[SCREEN_H][SCREEN_W];
    int mx, my;
    plat_mouse_position(&mx, &my);
    CursorImage c = input_cursor_image(input_cursor_at(mx, my));
    memcpy(saved, fb, sizeof saved);
    for (int y = 0; y < c.height; y++)
        for (int x = 0; x < c.width; x++) {
            int sx = mx - c.hot_x + x, sy = my - c.hot_y + y, i = y * c.width + x;
            if (sx < 0 || sy < 0 || sx >= SCREEN_W || sy >= SCREEN_H) continue;
            int n = c.bpp == 4 ? (c.bits[i / 2] >> (i & 1 ? 0 : 4)) & 0xf : c.bits[i];
            if (n != c.key) fb[sy][sx] = c.palette ? c.palette[n] : (uint8_t)n;
        }
    plat_present();
    memcpy(fb, saved, sizeof saved);
}

/* Loads GRAPHICS.DAT, the screen layout, the colors, and the font; returns 0 with a message on an error. */
static int load_graphics(const char *data)
{
    char graphics[PATH_SIZE];
    snprintf(graphics, sizeof graphics, "%s/DATA/GRAPHICS.DAT", data);
    static gdat_t *gdat;
    if (gdat_load(graphics, &gdat) != GDAT_OK) {
        fprintf(stderr, "cannot read %s\n", graphics);
        return 0;
    }
    if (!zone_load(gdat) || !pal_load_color_ramps(gdat) || !text_init(gdat)) {
        fprintf(stderr, "%s has no screen layout, color ramps, or font\n", graphics);
        return 0;
    }
    for (int c = 0; c < PALETTE_COLORS; c++) gdat_palette_color(gdat, c, palette[c]);
    draw_init(gdat);
    input_init();
    return 1;
}

/* Loads a save and starts its game at once; returns 0 with a message on an error. */
static int show_save(const char *path)
{
    if (save_load(path, draw_gdat()) != SAVE_OK) {
        fprintf(stderr, "cannot load the save %s\n", path);
        return 0;
    }
    ui_reset();
    money_init_coin_table();
    ui_draw_game_screen();
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

/* Passes the window events to the input of the game; a closed window ends the program. */
static void pump_events(void)
{
    PlatEvent e;
    while (plat_poll(&e)) {
        if (e.type == PLAT_QUIT) {
            main_loop.quit = 1;
        } else if (e.type == PLAT_KEY) {
            uint16_t key = pc_key_code(e.scancode, e.mod);
            if (key) input_key_down(key);
        } else if (e.type == PLAT_MOUSE) {
            input_mouse_click(e.x, e.y, e.button == SDL_BUTTON_RIGHT ? INPUT_BUTTON_RIGHT : INPUT_BUTTON_LEFT);
        } else if (e.type == PLAT_MOUSE_UP && e.button == SDL_BUTTON_RIGHT) {
            input_mouse_click(e.x, e.y, INPUT_RIGHT_RELEASED);
        }
    }
    plat_mouse_position(&input_mouse_x, &input_mouse_y);
}

/* One timer tick of a modal loop (menu, dialog): the input, the screen, and the wait. */
static void modal_frame(void)
{
    pump_events();
    present_with_cursor();
    for (int i = 0; i < PIT_TICKS_PER_GAME_TICK; i++) plat_wait_tick();
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
    snprintf(session.data_dir, sizeof session.data_dir, "%s", data);
    snprintf(text_drive_path, sizeof text_drive_path, "%s", data);

    if (!load_graphics(data)) return 1;
    if (save && !show_save(save)) return 1;
    if (!plat_init(WINDOW_SCALE, factor)) return 1;
    if (music_init(draw_gdat(), data) != MUSIC_OK) SDL_Log("cannot load MELODIC.BNK or DRUM.BNK in %s: playing without music", data);
    plat_audio_music(music_render);
    game_frame_hook = modal_frame;
    if (!save && !game_init()) main_loop.quit = 1;
    for (int pit = 0; !main_loop.quit;) {
        pump_events();
        if (++pit == PIT_TICKS_PER_GAME_TICK) {
            pit = 0;
            game_tick();
            if (main_loop.end && !main_loop.quit) game_end(main_loop.end);
            present_with_cursor();
        }
        plat_wait_tick();
    }
    music_quit();
    plat_quit();
    if (session.exit_code == EXIT_RESTART) {
        execvp(argv[0], argv);  /* the DOS launcher starts the game again */
        fprintf(stderr, "cannot restart %s\n", argv[0]);
    }
    return session.exit_code;
}
