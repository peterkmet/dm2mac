/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fm.h"
#include "hmp.h"
#include "os.h"
#include "sound.h"

/* The GDAT type and entry of a song in CAT_MUSIC. */
#define SONG_TYPE  0x03
#define SONG_ENTRY 0x00

/* The HMP song volume of a song at full loudness. */
#define SONG_VOLUME_FULL 0x7f

/* The game steps of the fade-out, the volume setting at start, and the layout of the master volume. */
#define FADE_STEPS          127
#define VOLUME_DEFAULT      5
#define MASTER_VOLUME_SHIFT 4 /* the master volume repeats the level in its high bits */

/* The sizes of the file reads and the channels of an output frame. */
#define BANK_MAX 65536
#define STEREO   2

Music g_music = {.requested_song = -1, .current_song = MUSIC_NO_SONG};
static const Gdat *gdat;
static uint32_t tick_phase;

/* Stops the song and its fade; a paused song does not resume. */
static void stop_song(void)
{
    if (!g_music.enabled || !g_music.playing)
        return;
    hmp_stop();
    g_music.playing = false;
    g_music.paused = false;
    g_music.fade = 0;
}

/* Setup */

/* Reads up to max bytes of data_dir/name into buf and sets size to the byte count; false when the file is missing. */
static bool read_file(const char *data_dir, const char *name, uint8_t *buf, size_t max, size_t *size)
{
    char path[OS_PATH_SIZE];
    snprintf(path, sizeof path, "%s/%s", data_dir, name);
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    *size = fread(buf, 1, max, f);
    fclose(f);
    return true;
}

/* Loads the instrument bank file name into the FM driver bank; false when it is missing or bad. */
static bool load_bank(const char *data_dir, const char *name, int bank)
{
    uint8_t *buf = malloc(BANK_MAX);
    size_t size;
    bool ok = buf && read_file(data_dir, name, buf, BANK_MAX, &size) && fm_set_bank(bank, buf, size);
    free(buf);
    return ok;
}

bool music_init(const Gdat *g, const char *data_dir)
{
    g_music = (Music){.requested_song = -1, .current_song = MUSIC_NO_SONG};
    tick_phase = 0;
    gdat = g;
    fm_reset(MUSIC_SAMPLE_RATE);
    if (!load_bank(data_dir, "MELODIC.BNK", FM_BANK_MELODIC) || !load_bank(data_dir, "DRUM.BNK", FM_BANK_DRUM)) {
        fm_free_banks();
        return false;
    }
    g_music.enabled = true;
    size_t size;
    if (read_file(data_dir, "DATA/SONGLIST.DAT", g_music.songlist, SONGLIST_MAX, &size)) {
        g_music.songlist_size = (int)size;
        g_music.songlist_loaded = true;
    }
    music_set_volume(VOLUME_DEFAULT);
    return true;
}

void music_quit(void)
{
    stop_song();
    fm_free_banks();
    g_music.enabled = false;
}

/* Songs */

void music_play_song(int16_t song)
{
    if (g_music.enabled && song && g_music.volume > 0) {
        stop_song();
        int item = gdat_find(gdat, CAT_MUSIC, song, SONG_TYPE, SONG_ENTRY);
        uint32_t size;
        const uint8_t *data = item < 0 ? NULL : gdat_item(gdat, item, &size);
        if (!data || hmp_start(data, size, fm_send) != HMP_OK)
            return;
        hmp_set_song_volume(SONG_VOLUME_FULL);
        hmp_unmute();
        g_music.started = true;
        g_music.playing = true;
    }
    g_music.requested_song = song;
}

void music_pause(void)
{
    if (!g_music.enabled || !g_music.playing)
        return;
    hmp_pause();
    g_music.paused = true;
}

void music_update_for_map(int16_t map)
{
    if (!g_music.enabled || !g_music.songlist_loaded)
        return;
    if (g_music.paused) {
        hmp_resume();
        g_music.paused = false;
    }
    if (g_music.fade > 1) {
        hmp_set_song_volume(g_music.fade);
        g_music.fade--;
    } else if (g_music.fade == 1) {
        g_music.current_song = g_music.next_song;
        music_play_song(g_music.current_song);
        g_music.fade = 0;
        return;
    }
    if (map < 0 || map >= g_music.songlist_size)
        return; /* fix: the original reads past SONGLIST.DAT */
    g_music.next_song = g_music.songlist[map];
    if (g_music.current_song == g_music.next_song)
        return;
    if (g_music.playing && !g_music.fade)
        g_music.fade = FADE_STEPS;
    else
        music_play_song(g_music.next_song);
    g_music.current_song = g_music.next_song;
}

void music_set_volume(int level)
{
    level = level < 0 ? 0 : level > VOLUME_SETTING_MAX ? VOLUME_SETTING_MAX : level;
    if (g_music.enabled) {
        hmp_set_master_volume(level * 2 | level << MASTER_VOLUME_SHIFT); /* 0 to 126 */
        if (g_music.started) {
            if (!level) {
                stop_song();
            } else if (!g_music.volume && g_music.requested_song >= 0) {
                g_music.volume = (int16_t)level;
                music_play_song(g_music.requested_song);
            }
        }
    }
    g_music.volume = (int16_t)level;
}

/* Output */

void music_render(int16_t *out, int frames)
{
    if (!g_music.enabled) {
        memset(out, 0, (size_t)frames * STEREO * sizeof *out);
        return;
    }
    for (int i = 0; i < frames; i++) {
        tick_phase += (uint32_t)hmp_tick_rate();
        if (tick_phase >= MUSIC_SAMPLE_RATE) {
            tick_phase -= MUSIC_SAMPLE_RATE;
            hmp_tick();
        }
        fm_render(out + STEREO * i, 1);
    }
}
