/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "advanced.h"
#include "control.h"
#include "draw.h"
#include "game.h"
#include "game_text.h"
#include "gdat.h"
#include "icon.h"
#include "input.h"
#include "mode_screen.h"
#include "movie.h"
#include "music.h"
#include "optmenu.h"
#include "os.h"
#include "platform.h"
#include "play.h"
#include "save.h"
#include "scale.h"
#include "seen.h"
#include "session.h"
#include "settings.h"
#include "sound.h"
#include "text.h"
#include "timeline.h"
#include "transport.h"
#include "video.h"
#include "zone.h"
#ifdef _WIN32
#include <process.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif

/* Sizes of the window, the icon, and the paths. */
#define WINDOW_SCALE    3
#define ICON_MAX_PIXELS 16384 /* 128 x 128 */

/* The exit status of a bad command line. */
#define EXIT_USAGE 2

/* The arguments that a RESTART adds: --mode and its value. */
#define MODE_OPTION_ARGS 2

/* The timer ticks of the main loops. */
#define PIT_TICKS_PER_GAME_TICK 4 /* the timer interrupt runs at 240 Hz, the game timer at 60 Hz */

/* The environment variable and the files of the program. */
#define RESTART_ENV "DM2_RESTARTED" /* set for the program that a RESTART starts: the launcher plays no start movies */
#define LOG_FILE    "dm2.log"       /* the messages of the Windows build, in the game folder */

/* The values of the command line options; NULL for an option that is not given. */
typedef struct {
    const char *data;
    const char *filter;
    const char *save;
    const char *control;
    const char *mode;
    const char *saves;
} Options;

/* An SDL key and the PC scan code of the original keyboard layer. */
typedef struct {
    uint8_t sdl, pc;
} ScanCode;

/* The PC scan codes of the SDL keys. */
static const ScanCode scan_codes[] = {
    {SDL_SCANCODE_ESCAPE,       0x01},
    {SDL_SCANCODE_1,            0x02},
    {SDL_SCANCODE_2,            0x03},
    {SDL_SCANCODE_3,            0x04},
    {SDL_SCANCODE_4,            0x05},
    {SDL_SCANCODE_5,            0x06},
    {SDL_SCANCODE_6,            0x07},
    {SDL_SCANCODE_7,            0x08},
    {SDL_SCANCODE_8,            0x09},
    {SDL_SCANCODE_9,            0x0a},
    {SDL_SCANCODE_0,            0x0b},
    {SDL_SCANCODE_MINUS,        0x0c},
    {SDL_SCANCODE_EQUALS,       0x0d},
    {SDL_SCANCODE_BACKSPACE,    0x0e},
    {SDL_SCANCODE_TAB,          0x0f},
    {SDL_SCANCODE_Q,            0x10},
    {SDL_SCANCODE_W,            0x11},
    {SDL_SCANCODE_E,            0x12},
    {SDL_SCANCODE_R,            0x13},
    {SDL_SCANCODE_T,            0x14},
    {SDL_SCANCODE_Y,            0x15},
    {SDL_SCANCODE_U,            0x16},
    {SDL_SCANCODE_I,            0x17},
    {SDL_SCANCODE_O,            0x18},
    {SDL_SCANCODE_P,            0x19},
    {SDL_SCANCODE_LEFTBRACKET,  0x1a},
    {SDL_SCANCODE_RIGHTBRACKET, 0x1b},
    {SDL_SCANCODE_RETURN,       0x1c},
    {SDL_SCANCODE_A,            0x1e},
    {SDL_SCANCODE_S,            0x1f},
    {SDL_SCANCODE_D,            0x20},
    {SDL_SCANCODE_F,            0x21},
    {SDL_SCANCODE_G,            0x22},
    {SDL_SCANCODE_H,            0x23},
    {SDL_SCANCODE_J,            0x24},
    {SDL_SCANCODE_K,            0x25},
    {SDL_SCANCODE_L,            0x26},
    {SDL_SCANCODE_SEMICOLON,    0x27},
    {SDL_SCANCODE_APOSTROPHE,   0x28},
    {SDL_SCANCODE_GRAVE,        0x29},
    {SDL_SCANCODE_BACKSLASH,    0x2b},
    {SDL_SCANCODE_Z,            0x2c},
    {SDL_SCANCODE_X,            0x2d},
    {SDL_SCANCODE_C,            0x2e},
    {SDL_SCANCODE_V,            0x2f},
    {SDL_SCANCODE_B,            0x30},
    {SDL_SCANCODE_N,            0x31},
    {SDL_SCANCODE_M,            0x32},
    {SDL_SCANCODE_COMMA,        0x33},
    {SDL_SCANCODE_PERIOD,       0x34},
    {SDL_SCANCODE_SLASH,        0x35},
    {SDL_SCANCODE_SPACE,        0x39},
    {SDL_SCANCODE_F1,           0x3b},
    {SDL_SCANCODE_F2,           0x3c},
    {SDL_SCANCODE_F3,           0x3d},
    {SDL_SCANCODE_F4,           0x3e},
    {SDL_SCANCODE_F5,           0x3f},
    {SDL_SCANCODE_F6,           0x40},
    {SDL_SCANCODE_F7,           0x41},
    {SDL_SCANCODE_F8,           0x42},
    {SDL_SCANCODE_F9,           0x43},
    {SDL_SCANCODE_F10,          0x44},
    {SDL_SCANCODE_KP_7,         0x47},
    {SDL_SCANCODE_KP_8,         0x48},
    {SDL_SCANCODE_KP_9,         0x49},
    {SDL_SCANCODE_KP_MINUS,     0x4a},
    {SDL_SCANCODE_KP_4,         0x4b},
    {SDL_SCANCODE_KP_5,         0x4c},
    {SDL_SCANCODE_KP_6,         0x4d},
    {SDL_SCANCODE_KP_PLUS,      0x4e},
    {SDL_SCANCODE_KP_1,         0x4f},
    {SDL_SCANCODE_KP_2,         0x50},
    {SDL_SCANCODE_KP_3,         0x51},
    {SDL_SCANCODE_KP_0,         0x52},
    {SDL_SCANCODE_KP_PERIOD,    0x53},
};

/* The log file, the graphics, the window icon, the pointer, and the buffers of the program. */
static struct {
#ifdef _WIN32
    FILE *log_file; /* dm2.log; NULL when it cannot open */
#endif
    uint8_t saved_screen[SCREEN_H][SCREEN_W]; /* the screen without the cursor while the cursor is shown */
    Gdat *gdat;                               /* GRAPHICS.DAT, loaded for the whole run */
    uint32_t icon[ICON_MAX_PIXELS];           /* the ARGB pixels of the window icon */
    int window_x, window_y;                   /* the last pointer position of the window; -1 before the first */
    char control_path[OS_DIR_SIZE];           /* the default control socket path; options.control points to it */
} program = {.window_x = -1, .window_y = -1};

/* Shows g_video_fb with the cursor image of input_cursor_at at the mouse pointer (the original mouse cursor). */
static void present_with_cursor(void)
{
    memcpy(program.saved_screen, g_video_fb, sizeof program.saved_screen);
    input_draw_cursor(&g_video_fb[0][0], g_input_mouse_x, g_input_mouse_y);
    plat_present();
    memcpy(g_video_fb, program.saved_screen, sizeof program.saved_screen);
}

/* Sets the palette of GRAPHICS.DAT. */
static void load_palette(void)
{
    for (int c = 0; c < PALETTE_COLORS; c++)
        gdat_palette_color(gdat_current(), c, g_video_palette[c]);
}

/* Loads GRAPHICS.DAT, the screen layout, the colors, and the font; false with a message on an error. */
static bool load_graphics(const char *data)
{
    char graphics[OS_PATH_SIZE];
    snprintf(graphics, sizeof graphics, "%s/DATA/GRAPHICS.DAT", data);
    if (gdat_load(graphics, &program.gdat) != GDAT_OK) {
        os_log("cannot read %s", graphics);
        return false;
    }
    if (!zone_load(program.gdat) || !pal_load_color_ramps(program.gdat) || !text_init(program.gdat)) {
        os_log("%s has no screen layout, color ramps, or font", graphics);
        return false;
    }
    draw_init(program.gdat);
    load_palette();
    input_init();
    return true;
}

/* Gives the window the mummy icon from GRAPHICS.DAT; without the graphic, SDL keeps its icon. */
static void set_icon(void)
{
    IconSize size = icon_build(gdat_current(), program.icon, ICON_MAX_PIXELS);
    if (size.width)
        plat_set_icon(program.icon, size.width, size.height);
}

/* Loads a save and starts its game at once; false with a message on an error. */
static bool show_save(const char *path)
{
    if (!session_start_save(path)) {
        os_log("cannot load the save %s", path);
        return false;
    }
    return true;
}

/* The key code of the original keyboard layer for an SDL key: the PC scan code with shift, control, and alt bits;
   the arrows give the codes of the numeric keypad keys that the game reads for them. */
static uint16_t pc_key_code(int scancode, int mod)
{
    bool shift = mod & SDL_KMOD_SHIFT;
    switch (scancode) {
    case SDL_SCANCODE_UP:
        return KEYPAD_FORWARD;
    case SDL_SCANCODE_DOWN:
        return KEYPAD_BACK;
    case SDL_SCANCODE_LEFT:
        return shift ? KEYPAD_LEFT : KEYPAD_TURN_LEFT;
    case SDL_SCANCODE_RIGHT:
        return shift ? KEYPAD_RIGHT : KEYPAD_TURN_RIGHT;
    }
    for (size_t i = 0; i < sizeof scan_codes / sizeof scan_codes[0]; i++)
        if (scan_codes[i].sdl == scancode)
            return (uint16_t)(scan_codes[i].pc | (shift ? KEY_SHIFTED : 0) | (mod & SDL_KMOD_CTRL ? KEY_CONTROL : 0) |
                              (mod & SDL_KMOD_ALT ? KEY_ALT : 0));
    return 0;
}

/* The platform ended the program (a closed window): the main loop stops, no end movie plays. */
static void end_by_platform(void)
{
    g_main_loop.quit = 1;
    g_main_loop.closed = 1;
}

/* Passes the window events and the control requests to the input of the game; a closed window ends the program.
   A mouse move of the window and a move request of the control both set the pointer. */
static void pump_events(void)
{
    int x, y;
    PlatEvent e;
    while (plat_poll(&e)) {
        if (e.type == PLAT_QUIT) {
            end_by_platform();
        } else if (e.type == PLAT_KEY) {
            uint16_t key = pc_key_code(e.scancode, e.mod);
            if (key && e.repeat && g_game_mode == GAME_MODE_ADVANCED)
                input_key_repeat(key);
            else if (key)
                input_key_down(key);
        } else if (e.type == PLAT_MOUSE) {
            input_mouse_down(e.x, e.y, e.button == SDL_BUTTON_RIGHT ? INPUT_BUTTON_RIGHT : INPUT_BUTTON_LEFT);
        } else if (e.type == PLAT_MOUSE_UP) {
            input_mouse_up(e.x, e.y, e.button == SDL_BUTTON_RIGHT ? INPUT_BUTTON_RIGHT : INPUT_BUTTON_LEFT);
        }
    }
    plat_mouse_position(&x, &y);
    if (x != program.window_x || y != program.window_y) {
        program.window_x = x;
        program.window_y = y;
        g_input_mouse_x = x;
        g_input_mouse_y = y;
    }
    transport_poll();
}

/* One timer tick of a modal loop (menu, dialog): the input, the screen, and the wait. */
static void modal_frame(void)
{
    pump_events();
    present_with_cursor();
    for (int i = 0; i < PIT_TICKS_PER_GAME_TICK; i++)
        plat_wait_tick();
}

/* The window in the turn-based mode: only the quit event; keys, clicks, and the pointer do not reach the game. */
static void pump_quit_events(void)
{
    PlatEvent e;
    while (plat_poll(&e))
        if (e.type == PLAT_QUIT)
            end_by_platform();
}

/* One frame of a modal loop (menu, dialog) in the turn-based mode: the requests and the screen; no wait while a turn
   runs. */
static void turnbased_modal_frame(void)
{
    pump_quit_events();
    transport_poll();
    present_with_cursor();
    if (!control_is_turn_pending())
        for (int i = 0; i < PIT_TICKS_PER_GAME_TICK; i++)
            plat_wait_tick();
}

/* The main loop of the turn-based mode: the game runs only during a turn, one game step per frame. */
static void run_turnbased(void)
{
    while (!g_main_loop.quit) {
        pump_quit_events();
        transport_poll();
        control_frame();
        present_with_cursor();
        if (!control_is_turn_pending())
            for (int i = 0; i < PIT_TICKS_PER_GAME_TICK; i++)
                plat_wait_tick();
    }
}

/* The main loop of the vanilla and advanced modes: the input on each PIT tick, a game tick on every fourth, the end
   of the game, and the screen. */
static void run_realtime(void)
{
    for (int pit = 0; !g_main_loop.quit;) {
        pump_events();
        if (++pit == PIT_TICKS_PER_GAME_TICK) {
            pit = 0;
            play_tick();
            if (g_main_loop.end && !g_main_loop.quit)
                session_end(g_main_loop.end);
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
        if (e.type == PLAT_QUIT)
            end_by_platform();
        else if (e.type == PLAT_MOUSE)
            movie_click();
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
static void run_movies(int script)
{
    if (g_game_mode == GAME_MODE_TURNBASED)
        return; /* the turn-based mode plays no movie */
    if (!movie_start(script, g_session.data_dir, SDL_GetTicksNS()))
        return;
    plat_audio_music(movie_render_audio);
    for (int pit = 0; movie_is_playing() && !g_main_loop.quit;) {
        pump_movie_events();
        if (++pit == PIT_TICKS_PER_GAME_TICK) {
            pit = 0;
            movie_update(SDL_GetTicksNS());
            if (movie_is_playing())
                present_movie();
        }
        plat_wait_tick();
    }
    movie_stop();
    plat_audio_music(music_render);
}

/* The keys and clicks of the mode screen; a closed window ends the program. */
static void pump_mode_screen_events(void)
{
    PlatEvent e;
    while (plat_poll(&e)) {
        if (e.type == PLAT_QUIT)
            end_by_platform();
        else if (e.type == PLAT_MOUSE && e.button == SDL_BUTTON_LEFT)
            mode_screen_click(e.x, e.y);
        else if (e.type != PLAT_KEY)
            continue;
        else if (e.scancode == SDL_SCANCODE_UP || e.scancode == SDL_SCANCODE_KP_8 || e.scancode == SDL_SCANCODE_W)
            mode_screen_key(MODE_SCREEN_UP);
        else if (e.scancode == SDL_SCANCODE_DOWN || e.scancode == SDL_SCANCODE_KP_2 || e.scancode == SDL_SCANCODE_S)
            mode_screen_key(MODE_SCREEN_DOWN);
        else if (e.scancode == SDL_SCANCODE_RETURN || e.scancode == SDL_SCANCODE_KP_ENTER)
            mode_screen_key(MODE_SCREEN_ENTER);
    }
    plat_mouse_position(&g_input_mouse_x, &g_input_mouse_y);
}

/* The filters of SETTINGS.TXT and the options menu are the upscale factors of the window. */
_Static_assert(SETTINGS_FILTER_NONE == SCALE_NONE_FACTOR && SETTINGS_FILTER_SCALE2X == SCALE2X_FACTOR &&
                   SETTINGS_FILTER_SCALE3X == SCALE3X_FACTOR,
               "filter numbers");

/* Reads the values of the command line options into options; the option without a value, NULL when all have one.
   *unknown is the first argument that starts with -- and matches no option, or NULL. */
static const char *read_options(int argc, char **argv, Options *options, const char **unknown)
{
    const struct {
        const char *name;
        const char **value;
    } names[] = {
        {"--data",    &options->data   },
        {"--filter",  &options->filter },
        {"--save",    &options->save   },
        {"--control", &options->control},
        {"--mode",    &options->mode   },
        {"--saves",   &options->saves  },
    };
    *unknown = NULL;
    for (int i = 1; i < argc; i++) {
        bool known = false;
        for (size_t n = 0; n < sizeof names / sizeof names[0] && !known; n++) {
            if (strcmp(argv[i], names[n].name))
                continue;
            if (i + 1 == argc)
                return argv[i];
            *names[n].value = argv[++i];
            known = true;
        }
        if (!known && !*unknown && !strncmp(argv[i], "--", 2))
            *unknown = argv[i];
    }
    return NULL;
}

/* False with a message for an option without a value or an unknown option. */
static bool options_are_valid(const char *missing, const char *unknown)
{
    if (missing)
        os_log("%s needs a value", missing);
    else if (unknown) {
        os_log("unknown option %s", unknown);
        os_log("usage: dm2 [--data DIR] [--save FILE] [--saves DIR] [--filter NAME] [--mode NAME] [--control PATH]");
    }
    return !missing && !unknown;
}

/* Sets g_game_mode from its name; false with a message for an unknown name. */
static bool select_mode(const char *mode)
{
    for (g_game_mode = 0; g_game_mode < GAME_MODE_COUNT && strcmp(mode, g_game_mode_names[g_game_mode]);
         g_game_mode++) {
    }
    if (g_game_mode < GAME_MODE_COUNT)
        return true;
    os_log("unknown mode %s: use vanilla, advanced, or turnbased", mode);
    return false;
}

/* Shows the mode screen until the player selects a mode, which then plays as with --mode, or closes the window. */
static void choose_mode(void)
{
    mode_screen_start();
    while (!mode_screen_is_done() && !g_main_loop.quit) {
        pump_mode_screen_events();
        mode_screen_draw();
        present_with_cursor();
        for (int i = 0; i < PIT_TICKS_PER_GAME_TICK; i++)
            plat_wait_tick();
    }
    memset(g_video_fb, 0, sizeof g_video_fb); /* the mode screen does not show under the game palette */
    load_palette();
    if (mode_screen_is_done())
        select_mode(g_game_mode_names[mode_screen_cursor()]);
}

/* Builds the default control socket path $HOME/.dm2/control.sock of the turn-based mode; false without HOME. */
static bool default_control_path(const char **control)
{
    const char *home = getenv("HOME");
    if (!home) {
        os_log("HOME is not set: use --control PATH");
        return false;
    }
    snprintf(program.control_path, sizeof program.control_path, "%s/.dm2/control.sock", home);
    *control = program.control_path;
    return true;
}

/* Starts the new game of the turn-based mode or the start menu of the other modes; false to end the program. */
static bool start_game(const char *data)
{
    if (g_game_mode != GAME_MODE_TURNBASED)
        return session_init();
    if (session_start_new_game())
        return true;
    os_log("cannot start a new game from %s/DATA/DUNGEON.DAT", data);
    return false;
}

/* The absolute path of an existing file, for the screenshot reply of the control socket. */
static bool full_path(const char *path, char *full, size_t size)
{
    char resolved[PATH_MAX];
#ifdef _WIN32
    struct stat file;
    if (stat(path, &file) || !_fullpath(resolved, path, sizeof resolved) || strlen(resolved) >= size)
        return false; /* _fullpath does not check that the file exists */
#else
    if (!realpath(path, resolved) || strlen(resolved) >= size)
        return false;
#endif
    memcpy(full, resolved, strlen(resolved) + 1);
    return true;
}

#ifdef _WIN32
/* The log and fatal error hook of the Windows build: a line of dm2.log, as the build has no console. */
static void log_to_file(const char *message)
{
    fprintf(program.log_file, "%s\n", message);
    fflush(program.log_file);
}

/* Opens dm2.log in the game folder and sends the messages to it. */
static void open_log(const char *data)
{
    char path[OS_PATH_SIZE];
    snprintf(path, sizeof path, "%s/%s", data, LOG_FILE);
    program.log_file = fopen(path, "w");
    if (!program.log_file)
        return;
    g_os_log_hook = log_to_file;
    g_os_fatal_hook = log_to_file;
}
#endif

/* Closes the control socket, the music, and SDL. */
static void close_platform(void)
{
    transport_close();
    music_quit();
    draw_free();
    timeline_free();
    seen_free();
    gdat_select(NULL);
    gdat_free(program.gdat);
    program.gdat = NULL;
    plat_quit();
}

/* Starts the program again for a RESTART of the game, in the game mode of this run; returns only with a message when
   it cannot start. */
static void restart(int argc, char **argv, bool mode_given)
{
    int count = args_drop_save(argc, argv); /* the original always restarts into the start menu */
    char **args = calloc((size_t)count + MODE_OPTION_ARGS + 1, sizeof *args);
    if (!args)
        os_fatal("out of memory");
    memcpy(args, argv, (size_t)count * sizeof *args);
    if (!mode_given) { /* the mode of the mode screen */
        args[count++] = "--mode";
        args[count++] = (char *)g_game_mode_names[g_game_mode];
    }
#ifdef _WIN32
    _putenv_s(RESTART_ENV, "1");
    _execvp(args[0], (const char *const *)args); /* the DOS launcher starts the game again */
#else
    setenv(RESTART_ENV, "1", 1);
    execvp(args[0], args); /* the DOS launcher starts the game again */
#endif
    os_log("cannot restart %s", args[0]);
    free(args);
}

/* Loads the game files and the save, opens the window, the control socket, and the music; false with a message on
   an error. */
static bool open_game(const Options *options, int factor)
{
    os_log("data folder: %s, mode %s", options->data, options->mode);
    snprintf(g_session.data_dir, sizeof g_session.data_dir, "%s", options->data);
    if (options->saves)
        snprintf(g_session.save_dir, sizeof g_session.save_dir, "%s", options->saves);
    snprintf(g_text_drive_path, sizeof g_text_drive_path, "%s", options->data);

    if (!load_graphics(options->data))
        return false;
    if (options->save && !show_save(options->save))
        return false;
    if (!plat_init(WINDOW_SCALE, factor))
        return false;
    set_icon();
    g_os_full_path_hook = full_path;
    if (options->control && !transport_open(options->control)) {
        os_log("cannot open the control socket %s", options->control);
        return false;
    }
    if (!music_init(gdat_current(), options->data))
        os_log("cannot load MELODIC.BNK or DRUM.BNK in %s: playing without music", options->data);
    plat_audio_music(music_render);
    if (options->save) {
        sound_set_volume_and_test(g_game.sound_volume, DEVICE_SOUND);
        sound_set_volume_and_test(g_game.music_volume, DEVICE_MUSIC);
    }
    return true;
}

int main(int argc, char **argv)
{
    Options options = {.data = "game", .filter = SDL_getenv("DM2_FILTER")};
    const char *unknown;
    const char *missing = read_options(argc, argv, &options, &unknown);
    bool mode_given = options.mode != NULL;
    if (!mode_given)
        options.mode = g_game_mode_names[GAME_MODE_ADVANCED];
#ifdef _WIN32
    open_log(options.data); /* before the first message: the Windows build has no console */
#endif
    if (!options_are_valid(missing, unknown))
        return EXIT_USAGE;
    if (!select_mode(options.mode))
        return EXIT_USAGE;
    if (!transport_is_available() && (g_game_mode == GAME_MODE_TURNBASED || options.control)) {
        os_log("the turn-based mode and --control need macOS");
        return EXIT_FAILURE;
    }
    if (g_game_mode == GAME_MODE_TURNBASED && !options.control && !default_control_path(&options.control))
        return EXIT_USAGE;
    int factor = options.filter ? scale_filter_factor(options.filter) : SCALE_NONE_FACTOR;
    if (!factor) {
        os_log("unknown filter %s: use none, scale2x, or scale3x", options.filter);
        return EXIT_USAGE;
    }
    if (!open_game(&options, factor))
        return EXIT_FAILURE;
    g_optmenu_filter_hook = plat_set_filter;
    g_play_frame_hook = g_game_mode == GAME_MODE_TURNBASED ? turnbased_modal_frame : modal_frame;
    if (!mode_given && !options.save && !getenv(RESTART_ENV))
        choose_mode();
    optmenu_start(options.filter ? factor : 0); /* --filter and DM2_FILTER go before SETTINGS.TXT */
    if (!options.save && !getenv(RESTART_ENV))
        run_movies(MOVIE_SCRIPT_START);
    if (!options.save && !g_main_loop.quit && !start_game(options.data)) {
        if (g_game_mode == GAME_MODE_TURNBASED) {
            close_platform();
            return EXIT_FAILURE;
        }
        g_main_loop.quit = 1;
    }
    if (g_game_mode == GAME_MODE_TURNBASED)
        run_turnbased(); /* the turn-based mode never calls session_end */
    else
        run_realtime();
    g_main_loop.quit = 0; /* the end movies run until their end, a closed window, or a quit request */
    sound_stop_all();
    if (!g_main_loop.closed)
        run_movies(movie_script_after(g_session.exit_code));
    close_platform();
    if (g_session.exit_code == EXIT_RESTART)
        restart(argc, argv, mode_given);
    return g_session.exit_code;
}
