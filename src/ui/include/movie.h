/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_MOVIE_H
#define DM2_UI_MOVIE_H
/* Prefixes: movie_ */

/* The movie script of the DOS launcher IBMIOP.EXE; platform/main.c drives it (docs/formats/movies.md). */

#include <stdbool.h>
#include <stdint.h>
#include "mve.h"

/* The movie scripts of the launcher. */
enum {
    MOVIE_SCRIPT_NONE = 0,
    MOVIE_SCRIPT_START = 1,
    MOVIE_SCRIPT_COMPLETED = 2,
    MOVIE_SCRIPT_PARTY_DEAD = 3,
};

/* The steps of the movie scripts. */
enum {
    MOVIE_STEP_NONE = -1,
    MOVIE_STEP_SPLASH = 0,
    MOVIE_STEP_FTL = 1,
    MOVIE_STEP_INTRO = 2,
    MOVIE_STEP_END = 3,
    MOVIE_STEP_CREDITS = 4,
};

/* The picture of a movie and the screen of the original player that shows it. */
typedef struct {
    const uint8_t *pixels; /* width × height palette indexes */
    int width, height;
    int shown_width;                   /* the width on the screen: 4/3 of width for a wide movie */
    int screen_width, screen_height;   /* the screen of the player */
    const uint8_t (*palette)[MVE_RGB]; /* 6-bit VGA values */
} MoviePicture;

/* Scripts */
/* The script that the launcher runs after the game ends with exit_code. */
int movie_script_after(int exit_code);
/* Stops the script: the game screen and its palette come back. */
void movie_stop(void);
/* Starts script with the files of data_dir at now_ns; false when no step of it can play. */
bool movie_start(int script, const char *data_dir, uint64_t now_ns);

/* Queries */
/* True while a script plays. */
bool movie_is_playing(void);
/* The current step; MOVIE_STEP_NONE when no script plays. */
int movie_current_step(void); /* exported for the tests */
/* True while the splash picture shows on the game screen; during a movie, movie_picture gives the picture. */
bool movie_shows_splash(void);
/* The picture of the current movie and its screen; all zero when no movie shows. */
void movie_picture(MoviePicture *p);

/* Playback */
/* Shows the next picture when its time came, and starts the next step after the end or a skip of a step. */
void movie_update(uint64_t now_ns);
/* A mouse click: the next movie_update stops the step when the mode allows it. */
void movie_click(void);
/* The audio mixer during a movie: the movie sound at full volume, 16-bit stereo at SOUND_OUTPUT_RATE. */
void movie_render_audio(int16_t *stereo, int frames);

#endif /* DM2_UI_MOVIE_H */
