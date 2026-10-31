/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "filter.h"
#include <stdio.h>
#include "game.h"
#include "game_text.h"
#include "text.h"

/* The filters, from SETTINGS_FILTER_NONE. */
#define FILTER_COUNT (SETTINGS_FILTER_SCALE3X - SETTINGS_FILTER_NONE + 1)

bool (*g_upscale_filter_hook)(enum SettingsFilter filter);

static struct {
    enum SettingsFilter filter; /* the filter in use */
    enum SettingsFilter saved;  /* the filter of SETTINGS.TXT, or in use at the start when the file holds none */
    bool save_failed;           /* the last write failed and its message shows */
} upscale = {.filter = SETTINGS_FILTER_NONE, .saved = SETTINGS_FILTER_NONE};

/* The folder of SETTINGS.TXT: the --saves folder, else the DATA folder of the game (as QUICK.DAT). */
static char *find_folder(char *folder, size_t size)
{
    if (g_session.save_dir[0])
        snprintf(folder, size, "%s", g_session.save_dir);
    else
        snprintf(folder, size, "%s/DATA", g_session.data_dir);
    return folder;
}

/* Setup */

void upscale_start(enum SettingsFilter filter)
{
    upscale.save_failed = false;
    if (filter != SETTINGS_FILTER_UNSET) {
        upscale.filter = filter;
        upscale.saved = filter;
        return;
    }
    char dir[OS_DIR_SIZE + sizeof "/DATA"];
    upscale.filter = SETTINGS_FILTER_NONE;
    if (g_game_mode == GAME_MODE_ADVANCED) {
        upscale.filter = settings_load(find_folder(dir, sizeof dir)).filter;
        if (g_upscale_filter_hook && upscale.filter != SETTINGS_FILTER_NONE && !g_upscale_filter_hook(upscale.filter))
            upscale.filter = SETTINGS_FILTER_NONE;
    }
    upscale.saved = upscale.filter;
}

/* Queries */

enum SettingsFilter upscale_get_filter(void)
{
    return upscale.filter;
}

/* Commands */

/* Writes SETTINGS.TXT when the filter differs from the file; a failed write says so once per failure run. */
static void write_change(void)
{
    char dir[OS_DIR_SIZE + sizeof "/DATA"];
    if (upscale.filter == upscale.saved)
        return;
    if (settings_save(find_folder(dir, sizeof dir), &(Settings){upscale.filter})) {
        upscale.saved = upscale.filter;
        upscale.save_failed = false;
    } else if (!upscale.save_failed) {
        upscale.save_failed = true;
        text_print_message(UI_WHITE, "\nUNABLE TO SAVE OPTIONS");
    }
}

void upscale_cycle_filter(void)
{
    enum SettingsFilter filter =
        (enum SettingsFilter)((upscale.filter - SETTINGS_FILTER_NONE + 1) % FILTER_COUNT + SETTINGS_FILTER_NONE);
    if (g_upscale_filter_hook && !g_upscale_filter_hook(filter))
        return;
    upscale.filter = filter;
    write_change();
}
