#ifndef MUSIC_H
#define MUSIC_H
#include <stdint.h>
#include "gdat.h"

/* Prefixes: music_ */

/* Game music: the song of each map from SONGLIST.DAT, played by the HMP player on the FM driver. */

#define MUSIC_SAMPLE_RATE 44100
#define MUSIC_NO_SONG 0xff
#define SONGLIST_MAX 0x3f   /* the game reads at most 63 bytes of SONGLIST.DAT */

enum {
    MUSIC_OK = 0,
    MUSIC_ERR_FILES = -1,   /* MELODIC.BNK or DRUM.BNK is missing or bad; music stays off */
};

typedef struct {
    int enabled;              /* the FM driver has its banks */
    int songlist_loaded;
    uint8_t songlist[SONGLIST_MAX];   /* song number of each map */
    int songlist_size;
    int16_t fade;             /* game steps left of the fade-out before the next song; 0: no fade */
    int paused, playing, started;
    int16_t requested_song;   /* last song asked for, -1 at start */
    uint8_t current_song, next_song;
    int16_t volume;           /* music volume setting, 0 to 7 */
} Music;

extern Music music;

/* Loads the FM banks and SONGLIST.DAT from data_dir, takes songs from g, and sets volume 5. */
int music_init(const gdat_t *g, const char *data_dir);
void music_quit(void);
/* Starts song (GDAT key (4, song, 3, 0)); song 0 starts nothing. */
void music_play_song(int song);
void music_stop(void);
void music_pause(void);
/* Once per game step: resumes a paused song, steps the fade, and changes to the song of map. */
void music_update_for_map(int map);
/* Music volume 0 to 7; 0 stops the song. */
void music_set_volume(int level);
/* Renders frames of stereo samples at MUSIC_SAMPLE_RATE and runs the song timer. */
void music_render(int16_t *out, int frames);

#endif
