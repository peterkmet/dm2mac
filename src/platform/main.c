/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
#include <SDL3/SDL.h>
#include "advanced.h"
#include "control.h"
#include "dialog.h"
#include "draw.h"
#include "dungeon.h"
#include "game.h"
#include "gdat.h"
#include "icon.h"
#include "input.h"
#include "item.h"
#include "movie.h"
#include "music.h"
#include "os.h"
#include "platform.h"
#include "play.h"
#include "save.h"
#include "session.h"
#include "scale.h"
#include "text.h"
#include "transport.h"
#include "ui.h"
#include "video.h"
#include "view.h"
#include "zone.h"
#include "sound.h"

#define WINDOW_SCALE 3
#define ICON_MAX_PIXELS (128 * 128)
#define EXIT_USAGE 2
#define PATH_SIZE 1024
#define PIT_TICKS_PER_GAME_TICK 4 /* the timer interrupt runs at 240 Hz, the game timer at 60 Hz */
#define RESTART_ENV "DM2_RESTARTED"   /* set for the program that a RESTART starts: the launcher plays no start movies */
#define LOG_FILE "dm2.log"            /* the messages of the Windows build, in the game folder */

/* Shows fb with the cursor image of input_cursor_at at the mouse pointer (the original mouse cursor). */
static void present_with_cursor(void)
{
    static uint8_t saved[SCREEN_H][SCREEN_W];
    memcpy(saved, fb, sizeof saved);
    input_draw_cursor(&fb[0][0], input_mouse_x, input_mouse_y);
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
        os_log("cannot read %s", graphics);
        return 0;
    }
    if (!zone_load(gdat) || !pal_load_color_ramps(gdat) || !text_init(gdat)) {
        os_log("%s has no screen layout, color ramps, or font", graphics);
        return 0;
    }
    for (int c = 0; c < PALETTE_COLORS; c++) gdat_palette_color(gdat, c, palette[c]);
    draw_init(gdat);
    input_init();
    return 1;
}

/* Gives the window the mummy icon from GRAPHICS.DAT; without the graphic, SDL keeps its icon. */
static void set_icon(void)
{
    static uint32_t argb[ICON_MAX_PIXELS];
    int w, h;
    if (icon_build(gdat_current(), argb, ICON_MAX_PIXELS, &w, &h)) plat_set_icon(argb, w, h);
}

/* Loads a save and starts its game at once; returns 0 with a message on an error. */
static int show_save(const char *path)
{
    if (!session_start_save(path)) {
        os_log("cannot load the save %s", path);
        return 0;
    }
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
    case SDL_SCANCODE_UP: return KEYPAD_FORWARD;
    case SDL_SCANCODE_DOWN: return KEYPAD_BACK;
    case SDL_SCANCODE_LEFT: return shift ? KEYPAD_LEFT : KEYPAD_TURN_LEFT;
    case SDL_SCANCODE_RIGHT: return shift ? KEYPAD_RIGHT : KEYPAD_TURN_RIGHT;
    }
    for (size_t i = 0; i < sizeof scan / sizeof scan[0]; i++)
        if (scan[i].sdl == scancode)
            return (uint16_t)(scan[i].pc | (shift ? KEY_SHIFT : 0) | (mod & SDL_KMOD_CTRL ? KEY_CONTROL : 0)
                              | (mod & SDL_KMOD_ALT ? KEY_ALT : 0));
    return 0;
}

/* Passes the window events and the control requests to the input of the game; a closed window ends the program.
   A mouse move of the window and a move request of the control both set the pointer. */
static void pump_events(void)
{
    static int window_x = -1, window_y = -1;
    int x, y;
    PlatEvent e;
    while (plat_poll(&e)) {
        if (e.type == PLAT_QUIT) {
            main_loop.quit = main_loop.closed = 1;
        } else if (e.type == PLAT_KEY) {
            uint16_t key = pc_key_code(e.scancode, e.mod);
            if (key) input_key_down(key);
        } else if (e.type == PLAT_MOUSE) {
            input_mouse_down(e.x, e.y, e.button == SDL_BUTTON_RIGHT ? INPUT_BUTTON_RIGHT : INPUT_BUTTON_LEFT);
        } else if (e.type == PLAT_MOUSE_UP) {
            input_mouse_up(e.x, e.y, e.button == SDL_BUTTON_RIGHT ? INPUT_BUTTON_RIGHT : INPUT_BUTTON_LEFT);
        }
    }
    plat_mouse_position(&x, &y);
    if (x != window_x || y != window_y) input_mouse_x = window_x = x, input_mouse_y = window_y = y;
    transport_poll();
}

/* One timer tick of a modal loop (menu, dialog): the input, the screen, and the wait. */
static void modal_frame(void)
{
    pump_events();
    present_with_cursor();
    for (int i = 0; i < PIT_TICKS_PER_GAME_TICK; i++) plat_wait_tick();
}

/* The window in the turn-based mode: only the quit event; keys, clicks, and the pointer do not reach the game. */
static void pump_quit_events(void)
{
    PlatEvent e;
    while (plat_poll(&e))
        if (e.type == PLAT_QUIT) main_loop.quit = main_loop.closed = 1;
}

/* One frame of a modal loop (menu, dialog) in the turn-based mode: the requests and the screen; no wait while a turn
   runs. */
static void turnbased_modal_frame(void)
{
    pump_quit_events();
    transport_poll();
    present_with_cursor();
    if (!control_is_turn_pending())
        for (int i = 0; i < PIT_TICKS_PER_GAME_TICK; i++) plat_wait_tick();
}

/* The main loop of the turn-based mode: the game runs only during a turn, one game step per frame. */
static void run_turnbased(void)
{
    while (!main_loop.quit) {
        pump_quit_events();
        transport_poll();
        control_frame();
        present_with_cursor();
        if (!control_is_turn_pending())
            for (int i = 0; i < PIT_TICKS_PER_GAME_TICK; i++) plat_wait_tick();
    }
}

/* The main loop of the vanilla and advanced modes: the input on each PIT tick, a game tick on every fourth, the end
   of the game, and the screen. */
static void run_realtime(void)
{
    for (int pit = 0; !main_loop.quit;) {
        pump_events();
        if (++pit == PIT_TICKS_PER_GAME_TICK) {
            pit = 0;
            play_tick();
            if (main_loop.end && !main_loop.quit) session_end(main_loop.end);
            present_with_cursor();
        }
        plat_wait_tick();
    }
}

/* The events of a movie: a click skips by the rules of the mode, a closed window ends the program. */
static void pump_movie_events(void)
{
    PlatEvent e;
    while (plat_poll(&e)) {
        if (e.type == PLAT_QUIT) main_loop.quit = main_loop.closed = 1;
        else if (e.type == PLAT_MOUSE) movie_click();
    }
    transport_poll();
}

/* Shows the splash on the game screen or the movie picture on the screen of its player. */
static void present_movie(void)
{
    MoviePicture picture;
    if (movie_shows_splash()) {
        plat_present();
        return;
    }
    movie_picture(&picture);
    plat_present_movie(&picture);
}

/* Plays a script of the launcher with the movie sound instead of the music, until its end or a quit. */
static void play_movies(int script)
{
    if (game_mode == GAME_MODE_TURNBASED) return;  /* the turn-based mode plays no movie */
    if (!movie_start(script, session.data_dir, SDL_GetTicksNS())) return;
    plat_audio_music(movie_render_audio);
    for (int pit = 0; movie_is_playing() && !main_loop.quit;) {
        pump_movie_events();
        if (++pit == PIT_TICKS_PER_GAME_TICK) {
            pit = 0;
            movie_update(SDL_GetTicksNS());
            if (movie_is_playing()) present_movie();
        }
        plat_wait_tick();
    }
    movie_stop();
    plat_audio_music(music_render);
}

/* Reads the values of the command line options; returns 0 with a message when an option has no value. */
static int read_options(int argc, char **argv, const char **data, const char **filter, const char **save,
                        const char **control, const char **mode, const char **saves)
{
    for (int i = 1; i < argc; i++) {
        int is_data = !strcmp(argv[i], "--data"), is_filter = !strcmp(argv[i], "--filter");
        int is_save = !strcmp(argv[i], "--save"), is_control = !strcmp(argv[i], "--control");
        int is_mode = !strcmp(argv[i], "--mode"), is_saves = !strcmp(argv[i], "--saves");
        if (!is_data && !is_filter && !is_save && !is_control && !is_mode && !is_saves) continue;
        if (i + 1 == argc) {
            os_log("%s needs a value", argv[i]);
            return 0;
        }
        if (is_data) *data = argv[++i];
        else if (is_filter) *filter = argv[++i];
        else if (is_save) *save = argv[++i];
        else if (is_mode) *mode = argv[++i];
        else if (is_saves) *saves = argv[++i];
        else *control = argv[++i];
    }
    return 1;
}

/* Sets game_mode from its name; returns 0 with a message for an unknown name. */
static int select_mode(const char *mode)
{
    for (game_mode = 0; game_mode < GAME_MODE_COUNT && strcmp(mode, game_mode_names[game_mode]); game_mode++) {}
    if (game_mode < GAME_MODE_COUNT) return 1;
    os_log("unknown mode %s: use vanilla, advanced, or turnbased", mode);
    return 0;
}

/* Builds the default control socket path $HOME/.dm2/control.sock of the turn-based mode; returns 0 without HOME. */
static int default_control_path(const char **control)
{
    static char path[PATH_SIZE];
    const char *home = getenv("HOME");
    if (!home) {
        os_log("HOME is not set: use --control PATH");
        return 0;
    }
    snprintf(path, sizeof path, "%s/.dm2/control.sock", home);
    *control = path;
    return 1;
}

/* Starts the new game of the turn-based mode or the start menu of the other modes; returns 0 to end the program. */
static int start_game(const char *data)
{
    if (game_mode != GAME_MODE_TURNBASED) return session_init();
    if (session_start_new_game()) return 1;
    os_log("cannot start a new game from %s/DATA/DUNGEON.DAT", data);
    return 0;
}

/* The absolute path of an existing file, for the screenshot reply of the control socket. */
static bool full_path(const char *path, char *full, size_t size)
{
    char resolved[PATH_MAX];
#ifdef _WIN32
    if (!_fullpath(resolved, path, sizeof resolved) || strlen(resolved) >= size) return false;
#else
    if (!realpath(path, resolved) || strlen(resolved) >= size) return false;
#endif
    memcpy(full, resolved, strlen(resolved) + 1);
    return true;
}

#ifdef _WIN32
static FILE *log_file;  /* dm2.log; NULL when it cannot open */

/* The log and fatal error hook of the Windows build: a line of dm2.log, as the build has no console. */
static void log_to_file(const char *message)
{
    fprintf(log_file, "%s\n", message);
    fflush(log_file);
}

/* Opens dm2.log in the game folder and sends the messages to it. */
static void open_log(const char *data)
{
    char path[PATH_SIZE];
    snprintf(path, sizeof path, "%s/%s", data, LOG_FILE);
    if (!(log_file = fopen(path, "w"))) return;
    g_os_log_hook = log_to_file;
    g_os_fatal_hook = log_to_file;
}
#endif

/* Closes the control socket, the music, and SDL. */
static void close_platform(void)
{
    transport_close();
    music_quit();
    plat_quit();
}

/* Starts the program again for a RESTART of the game; returns only with a message when it cannot start. */
static void restart(int argc, char **argv)
{
    args_drop_save(argc, argv);  /* the original always restarts into the start menu */
#ifdef _WIN32
    _putenv_s(RESTART_ENV, "1");
    _execvp(argv[0], (const char *const *)argv);  /* the DOS launcher starts the game again */
#else
    setenv(RESTART_ENV, "1", 1);
    execvp(argv[0], argv);  /* the DOS launcher starts the game again */
#endif
    os_log("cannot restart %s", argv[0]);
}

int main(int argc, char **argv)
{
    const char *data = "game", *filter = SDL_getenv("DM2_FILTER"), *save = NULL, *control = NULL, *mode = "advanced", *saves = NULL;
    if (!read_options(argc, argv, &data, &filter, &save, &control, &mode, &saves)) return EXIT_USAGE;
#ifdef _WIN32
    open_log(data);
#endif
    if (!select_mode(mode)) return EXIT_USAGE;
    if (!transport_is_available() && (game_mode == GAME_MODE_TURNBASED || control)) {
        os_log("the turn-based mode and --control need macOS");
        return 1;
    }
    if (game_mode == GAME_MODE_TURNBASED && !control && !default_control_path(&control)) return EXIT_USAGE;
    int factor = filter ? scale_filter_factor(filter) : 1;
    if (!factor) {
        os_log("unknown filter %s: use none, scale2x, or scale3x", filter);
        return EXIT_USAGE;
    }
    SDL_Log("data folder: %s, mode %s", data, mode);
    snprintf(session.data_dir, sizeof session.data_dir, "%s", data);
    if (saves) snprintf(session.save_dir, sizeof session.save_dir, "%s", saves);
    snprintf(text_drive_path, sizeof text_drive_path, "%s", data);

    if (!load_graphics(data)) return 1;
    if (save && !show_save(save)) return 1;
    if (!plat_init(WINDOW_SCALE, factor)) return 1;
    set_icon();
    g_os_full_path_hook = full_path;
    if (control && !transport_open(control)) {
        os_log("cannot open the control socket %s", control);
        return 1;
    }
    if (music_init(gdat_current(), data) != MUSIC_OK) SDL_Log("cannot load MELODIC.BNK or DRUM.BNK in %s: playing without music", data);
    plat_audio_music(music_render);
    if (save) {
        sound_set_volume_and_test(game.sound_volume, DEVICE_SOUND);
        sound_set_volume_and_test(game.music_volume, DEVICE_MUSIC);
    }
    play_frame_hook = game_mode == GAME_MODE_TURNBASED ? turnbased_modal_frame : modal_frame;
    if (!save && !getenv(RESTART_ENV)) play_movies(MOVIE_SCRIPT_START);
    if (!save && !main_loop.quit && !start_game(data)) {
        if (game_mode == GAME_MODE_TURNBASED) {
            close_platform();
            return 1;
        }
        main_loop.quit = 1;
    }
    if (game_mode == GAME_MODE_TURNBASED) run_turnbased();  /* the turn-based mode never calls session_end */
    else run_realtime();
    main_loop.quit = 0;  /* the end movies run until their end, a closed window, or a quit request */
    sound_stop_all();
    if (!main_loop.closed) play_movies(movie_script_after(session.exit_code));
    close_platform();
    if (session.exit_code == EXIT_RESTART) restart(argc, argv);
    return session.exit_code;
}
