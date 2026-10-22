/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "movie.h"
#include "pcx.h"
#include "game.h"
#include "session.h"
#include "sound.h"
#include "video.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The times of the script. */
#define SPLASH_NS 3000000000ull /* splash intrplay.pcx 3: three seconds */
#define NS_PER_US 1000

/* The screens of the player. */
#define WIDE_SCREEN_WIDTH  640 /* the screen of the argument 640x350 */
#define WIDE_SCREEN_HEIGHT 350
#define WIDE_NUMERATOR     4 /* wide: the player shows the video 4/3 wider */
#define WIDE_DENOMINATOR   3
#define SVGA_MODE_WIDTH    640 /* the SVGA movies set 640 × 480; the VGA movies 320 × 200 */
#define SVGA_MODE_HEIGHT   480

/* The palette and audio sizes of the movies, and the length of a script. */
#define PCX_TO_VGA_SHIFT  2      /* 8-bit palette values to the 6 bits of the VGA */
#define AUDIO_RING_FRAMES 131072 /* stereo frames at SOUND_OUTPUT_RATE: about 3 seconds */
#define STEREO_CHANNELS   2
#define U8_TO_S16         256
#define SCRIPT_STEPS_MAX  4 /* the steps of the longest script and its end mark */

/* The sample value of silence. */
#define U8_SILENCE 0x80 /* the movie samples are unsigned 8-bit */

/* One step of the SVGA script of IBMIOP.EXE: its file and the arguments that the launcher gives its player. */
typedef struct {
    const char *file;                /* in the data folder */
    bool noabort;                    /* the vanilla mode does not stop it on a click */
    int screen_width, screen_height; /* the screen of the player; 0 keeps the video mode of the movie */
    bool wide;                       /* the player shows the video 4/3 wider */
} Step;

static const Step steps[] = {
    [MOVIE_STEP_SPLASH] = {"INTRPLAY.PCX", true,  0,                 0,                  false},
    [MOVIE_STEP_FTL] = {"FTL",          true,  0,                 0,                  false},
    [MOVIE_STEP_INTRO] = {"INTRO",        false, WIDE_SCREEN_WIDTH, WIDE_SCREEN_HEIGHT, true },
    [MOVIE_STEP_END] = {"END",          false, WIDE_SCREEN_WIDTH, WIDE_SCREEN_HEIGHT, true },
    [MOVIE_STEP_CREDITS] = {"CREDITS",      false, WIDE_SCREEN_WIDTH, WIDE_SCREEN_HEIGHT, true },
};

/* The steps of each script in order, up to MOVIE_STEP_NONE (the next steps of the script table of IBMIOP.EXE). */
static const int8_t scripts[][SCRIPT_STEPS_MAX] = {
    [MOVIE_SCRIPT_NONE] = {MOVIE_STEP_NONE},
    [MOVIE_SCRIPT_START] = {MOVIE_STEP_SPLASH, MOVIE_STEP_FTL, MOVIE_STEP_INTRO, MOVIE_STEP_NONE},
    [MOVIE_SCRIPT_COMPLETED] = {MOVIE_STEP_END, MOVIE_STEP_CREDITS, MOVIE_STEP_NONE},
    [MOVIE_SCRIPT_PARTY_DEAD] = {MOVIE_STEP_CREDITS, MOVIE_STEP_NONE},
};

static struct {
    bool playing;
    int script, index, step; /* the script, the index of its current step, and that step */
    bool skip;               /* a click waits for movie_update */
    uint64_t start_ns;       /* the start of the step */
    char data_dir[SAVE_PATH_SIZE];
    uint8_t *file; /* the movie file of the step */
    MveStream mve;
    int16_t ring[AUDIO_RING_FRAMES][STEREO_CHANNELS];
    int ring_start, ring_count;
    uint8_t saved_fb[SCREEN_H][SCREEN_W];
    uint8_t saved_palette[PALETTE_COLORS][RGB_CHANNELS];
} mv = {.step = MOVIE_STEP_NONE};

/* Adds an audio frame (NULL: silence) to the ring as 16-bit stereo; another format than unsigned 8-bit plays none. */
static void queue_audio(const uint8_t *samples, int bytes)
{
    const MveStream *m = &mv.mve;
    if (!m->audio_rate || m->audio_16bit || m->audio_compressed || SOUND_OUTPUT_RATE % m->audio_rate)
        return;
    int channels = m->audio_stereo ? STEREO_CHANNELS : 1;
    int repeat = SOUND_OUTPUT_RATE / m->audio_rate;
    for (int i = 0; i + channels <= bytes; i += channels) {
        int16_t left = samples ? (int16_t)((samples[i] - U8_SILENCE) * U8_TO_S16) : 0;
        int16_t right = samples ? (int16_t)((samples[i + channels - 1] - U8_SILENCE) * U8_TO_S16) : 0;
        for (int r = 0; r < repeat && mv.ring_count < AUDIO_RING_FRAMES; r++, mv.ring_count++) {
            int16_t *out = mv.ring[(mv.ring_start + mv.ring_count) % AUDIO_RING_FRAMES];
            out[0] = left;
            out[1] = right;
        }
    }
}

/* Runs the movie to its next picture and queues its audio frames on the way; MVE_SHOW, MVE_END, or MVE_ERROR. */
static int next_picture(void)
{
    int result;
    while ((result = mve_step(&mv.mve)) == MVE_AUDIO)
        queue_audio(mv.mve.audio, mv.mve.audio_size);
    return result;
}

/* Scripts */

int movie_script_after(int exit_code)
{
    if (exit_code == EXIT_COMPLETED)
        return MOVIE_SCRIPT_COMPLETED;
    if (exit_code == EXIT_PARTY_DEAD)
        return MOVIE_SCRIPT_PARTY_DEAD;
    return MOVIE_SCRIPT_NONE;
}

void movie_stop(void)
{
    if (!mv.playing)
        return;
    free(mv.file);
    mv.file = NULL;
    mv.playing = false;
    mv.step = MOVIE_STEP_NONE;
    memcpy(g_video_fb, mv.saved_fb, sizeof g_video_fb);
    memcpy(g_video_palette, mv.saved_palette, sizeof g_video_palette);
}

/* Reads a file of the data folder into memory; NULL when it is missing, empty, or does not read. */
static uint8_t *load_file(const char *name, size_t *size)
{
    char path[SAVE_PATH_SIZE * 2];
    snprintf(path, sizeof path, "%s/%s", mv.data_dir, name);
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    uint8_t *data = NULL;
    long length = fseek(f, 0, SEEK_END) ? -1 : ftell(f);
    if (length > 0 && !fseek(f, 0, SEEK_SET) && (data = malloc((size_t)length)) &&
        fread(data, 1, (size_t)length, f) != (size_t)length) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *size = length > 0 ? (size_t)length : 0;
    return data;
}

/* Shows the splash picture on the game screen; false when it is missing or not a 320 × 200 picture. */
static bool start_splash(void)
{
    size_t size;
    uint8_t *file = load_file(steps[MOVIE_STEP_SPLASH].file, &size);
    PcxImage image = {.pixels = &g_video_fb[0][0], .max_width = SCREEN_W, .max_height = SCREEN_H};
    bool ok = file && pcx_decode(file, size, &image) == PCX_OK && image.width == SCREEN_W && image.height == SCREEN_H;
    free(file);
    if (ok)
        for (int c = 0; c < PALETTE_COLORS; c++)
            for (int ch = 0; ch < RGB_CHANNELS; ch++)
                g_video_palette[c][ch] = image.palette[c][ch] >> PCX_TO_VGA_SHIFT;
    return ok;
}

/* Loads the movie of step and decodes its first picture; false when it is missing, damaged, or not an SVGA movie. */
static bool start_movie(int step)
{
    size_t size;
    mv.file = load_file(steps[step].file, &size);
    if (!mv.file || mve_open(&mv.mve, mv.file, size) != MVE_OK)
        return false;
    if (next_picture() != MVE_SHOW || !mv.mve.frame_us)
        return false;
    return mv.mve.mode_width == SVGA_MODE_WIDTH && mv.mve.mode_height == SVGA_MODE_HEIGHT;
}

/* Ends the current step and starts the next step of the script that can play; after the last step the script stops. */
static void next_step(uint64_t now_ns)
{
    for (;;) {
        free(mv.file);
        mv.file = NULL;
        mv.ring_start = 0;
        mv.ring_count = 0;
        mv.step = scripts[mv.script][++mv.index];
        mv.start_ns = now_ns;
        if (mv.step == MOVIE_STEP_NONE) {
            movie_stop();
            return;
        }
        if (mv.step == MOVIE_STEP_SPLASH ? start_splash() : start_movie(mv.step))
            return;
    }
}

bool movie_start(int script, const char *data_dir, uint64_t now_ns)
{
    movie_stop();
    if (script == MOVIE_SCRIPT_NONE)
        return false;
    snprintf(mv.data_dir, sizeof mv.data_dir, "%s", data_dir);
    memcpy(mv.saved_fb, g_video_fb, sizeof g_video_fb);
    memcpy(mv.saved_palette, g_video_palette, sizeof g_video_palette);
    mv.script = script;
    mv.index = -1;
    mv.skip = false;
    mv.playing = true;
    next_step(now_ns);
    return mv.playing;
}

/* Queries */

bool movie_is_playing(void)
{
    return mv.playing;
}

int movie_current_step(void)
{
    return mv.playing ? mv.step : MOVIE_STEP_NONE;
}

bool movie_shows_splash(void)
{
    return mv.playing && mv.step == MOVIE_STEP_SPLASH;
}

void movie_picture(MoviePicture *p)
{
    if (!mv.playing || mv.step == MOVIE_STEP_SPLASH) {
        *p = (MoviePicture){0};
        return;
    }
    const Step *s = &steps[mv.step];
    p->pixels = mve_pixels(&mv.mve);
    p->width = mv.mve.width;
    p->height = mv.mve.height;
    p->shown_width = s->wide ? p->width * WIDE_NUMERATOR / WIDE_DENOMINATOR : p->width;
    p->screen_width = s->screen_width ? s->screen_width : mv.mve.mode_width;
    p->screen_height = s->screen_height ? s->screen_height : mv.mve.mode_height;
    p->palette = mv.mve.palette;
}

/* Playback */

void movie_update(uint64_t now_ns)
{
    if (!mv.playing)
        return;
    bool skip = mv.skip;
    mv.skip = false; /* a click that the mode does not allow is dropped */
    if (skip && (g_game_mode != GAME_MODE_VANILLA || !steps[mv.step].noabort)) {
        next_step(now_ns);
        return;
    }
    if (mv.step == MOVIE_STEP_SPLASH) {
        if (now_ns - mv.start_ns >= SPLASH_NS)
            next_step(now_ns);
        return;
    }
    /* picture k shows k frame times after the start of the step; a late update decodes the missed pictures */
    while (mv.playing && mv.step != MOVIE_STEP_SPLASH &&
           now_ns - mv.start_ns >= (uint64_t)mv.mve.frames * mv.mve.frame_us * NS_PER_US)
        if (next_picture() != MVE_SHOW)
            next_step(now_ns);
}

void movie_click(void)
{
    if (mv.playing)
        mv.skip = true;
}

void movie_render_audio(int16_t *stereo, int frames)
{
    if (!mv.playing || mv.step == MOVIE_STEP_SPLASH || !mv.mve.audio_started)
        return;
    for (int i = 0; i < frames && mv.ring_count; i++, mv.ring_count--) {
        const int16_t *in = mv.ring[mv.ring_start];
        mv.ring_start = (mv.ring_start + 1) % AUDIO_RING_FRAMES;
        stereo[i * STEREO_CHANNELS] = in[0];
        stereo[i * STEREO_CHANNELS + 1] = in[1];
    }
}
