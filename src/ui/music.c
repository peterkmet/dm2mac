/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "music.h"
#include "hmp.h"
#include "fm.h"
#include "sound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SONG_TYPE           3
#define SONG_ENTRY          0
#define SONG_VOLUME_FULL    0x7f
#define FADE_STEPS          127
#define VOLUME_DEFAULT      5
#define PATH_MAX_LENGTH     1024
#define STEREO              2
#define MASTER_VOLUME_SHIFT 4 /* the master volume repeats the level in its high bits */

Music music = {.requested_song = -1, .current_song = MUSIC_NO_SONG};
static const gdat_t *gdat;
static uint32_t tick_phase;

/* Reads up to max bytes of data_dir/name into buf; returns the byte count or -1. */
static long read_file(const char *data_dir, const char *name, uint8_t *buf, long max)
{
    char path[PATH_MAX_LENGTH];
    snprintf(path, sizeof path, "%s/%s", data_dir, name);
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    long n = (long)fread(buf, 1, (size_t)max, f);
    fclose(f);
    return n;
}

static int load_bank(const char *data_dir, const char *name, int bank)
{
    enum { BANK_MAX = 65536 };
    uint8_t *buf = malloc(BANK_MAX);
    long n = buf ? read_file(data_dir, name, buf, BANK_MAX) : -1;
    int result = n < 0 ? FM_ERR_BANK : fm_set_bank(bank, buf, (size_t)n);
    free(buf);
    return result;
}

int music_init(const gdat_t *g, const char *data_dir)
{
    gdat = g;
    fm_reset(MUSIC_SAMPLE_RATE);
    if (load_bank(data_dir, "MELODIC.BNK", FM_BANK_MELODIC) || load_bank(data_dir, "DRUM.BNK", FM_BANK_DRUM)) {
        fm_free_banks();
        return MUSIC_ERR_FILES;
    }
    music.enabled = 1;
    long n = read_file(data_dir, "DATA/SONGLIST.DAT", music.songlist, SONGLIST_MAX);
    if (n >= 0) {
        music.songlist_size = (int)n;
        music.songlist_loaded = 1;
    }
    music_set_volume(VOLUME_DEFAULT);
    return MUSIC_OK;
}

void music_quit(void)
{
    music_stop();
    fm_free_banks();
    music.enabled = 0;
}

void music_play_song(int song)
{
    if (music.enabled && song && music.volume > 0) {
        music_stop();
        int item = gdat_find(gdat, CAT_MUSIC, song, SONG_TYPE, SONG_ENTRY);
        uint32_t size;
        const uint8_t *data = item < 0 ? NULL : gdat_item(gdat, item, &size);
        if (!data || hmp_start(data, size, fm_send) != HMP_OK)
            return;
        hmp_set_song_volume(SONG_VOLUME_FULL);
        hmp_unmute();
        music.started = 1;
        music.playing = 1;
    }
    music.requested_song = (int16_t)song;
}

void music_stop(void)
{
    if (!music.enabled || !music.playing)
        return;
    hmp_stop();
    music.playing = 0;
    music.paused = 0;
    music.fade = 0;
}

void music_pause(void)
{
    if (!music.enabled || !music.playing)
        return;
    hmp_pause();
    music.paused = 1;
}

void music_update_for_map(int map)
{
    if (!music.enabled || !music.songlist_loaded)
        return;
    if (music.paused) {
        hmp_resume();
        music.paused = 0;
    }
    if (music.fade > 1) {
        hmp_set_song_volume(music.fade);
        music.fade--;
    } else if (music.fade == 1) {
        music.current_song = music.next_song;
        music_play_song(music.current_song);
        music.fade = 0;
        return;
    }
    if (map < 0 || map >= music.songlist_size)
        return; /* fix: the original reads past SONGLIST.DAT */
    music.next_song = music.songlist[map];
    if (music.current_song == music.next_song)
        return;
    if (music.playing && !music.fade)
        music.fade = FADE_STEPS;
    else
        music_play_song(music.next_song);
    music.current_song = music.next_song;
}

void music_set_volume(int level)
{
    level = level < 0 ? 0 : level > VOLUME_SETTING_MAX ? VOLUME_SETTING_MAX : level;
    if (music.enabled) {
        hmp_set_master_volume(level * 2 | level << MASTER_VOLUME_SHIFT); /* 0 to 126 */
        if (music.started) {
            if (!level) {
                music_stop();
            } else if (!music.volume && music.requested_song >= 0) {
                music.volume = (int16_t)level;
                music_play_song(music.requested_song);
            }
        }
    }
    music.volume = (int16_t)level;
}

void music_render(int16_t *out, int frames)
{
    if (!music.enabled) {
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
