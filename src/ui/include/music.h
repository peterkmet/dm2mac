/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_MUSIC_H
#define DM2_UI_MUSIC_H
/* Prefixes: music_ */

/* Game music: the song of each map from SONGLIST.DAT, played by the HMP player on the FM driver. */

#include <stdbool.h>
#include <stdint.h>
#include "gdat.h"

/* The output rate of the music and the size of SONGLIST.DAT. */
#define MUSIC_SAMPLE_RATE 44100
#define SONGLIST_MAX      63 /* the game reads at most 63 bytes of SONGLIST.DAT */

/* The song number of no song. */
#define MUSIC_NO_SONG 0xff

typedef struct {
    int enabled; /* the FM driver has its banks */
    int songlist_loaded;
    uint8_t songlist[SONGLIST_MAX]; /* song number of each map */
    int songlist_size;
    int16_t fade; /* game steps left of the fade-out before the next song; 0: no fade */
    int paused, playing, started;
    int16_t requested_song; /* last song asked for, -1 at start */
    uint8_t current_song, next_song;
    int16_t volume; /* music volume setting, 0 to 7 */
} Music;

/* The state of the music player. */
extern Music g_music; /* exported for the tests */

/* Setup */
/* Loads the FM banks and SONGLIST.DAT from data_dir, takes songs from g, and sets volume 5. Returns false when
   MELODIC.BNK or DRUM.BNK is missing or bad; music stays off. */
bool music_init(const Gdat *g, const char *data_dir);
/* Stops the song and frees the FM banks. */
void music_quit(void);

/* Songs */
/* Starts song (GDAT key (4, song, 3, 0)); song 0 starts nothing. */
void music_play_song(int song); /* exported for the tests */
/* Pauses the song until the next music_update_for_map. */
void music_pause(void);
/* Once per game step: resumes a paused song, steps the fade, and changes to the song of map. */
void music_update_for_map(int16_t map);
/* Music volume 0 to 7; 0 stops the song. */
void music_set_volume(int level);

/* Output */
/* Renders frames of stereo samples at MUSIC_SAMPLE_RATE and runs the song timer. */
void music_render(int16_t *out, int frames);

#endif /* DM2_UI_MUSIC_H */
