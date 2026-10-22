/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "settings.h"
#include <stdio.h>
#include <string.h>

/* The text of the settings file. */
#define LINE_SIZE          64 /* longer lines are unreadable */
#define SETTINGS_PATH_SIZE 1100
#define TEMPORARY_FILE     "SETTINGS.TMP"

static const char *const filter_names[] = {"none", "scale2x", "scale3x"}; /* from SETTINGS_FILTER_NONE */

/* The filter of a value text, or 0 for an unknown name. */
static int parse_filter(const char *value)
{
    for (int filter = SETTINGS_FILTER_NONE; filter <= SETTINGS_FILTER_SCALE3X; filter++)
        if (!strcmp(value, filter_names[filter - SETTINGS_FILTER_NONE]))
            return filter;
    return 0;
}

/* Sets the value of one line key=value; an unknown key or a bad value changes nothing. */
static void parse_line(char *line, Settings *settings)
{
    char *value = strchr(line, '=');
    if (!value)
        return;
    *value++ = '\0';
    if (!strcmp(line, "filter") && parse_filter(value))
        settings->filter = parse_filter(value);
}

/* Queries */

Settings settings_parse(const char *file_text, size_t length)
{
    Settings settings = {SETTINGS_FILTER_NONE};
    if (length > SETTINGS_MAX_SIZE)
        return settings;
    char line[LINE_SIZE];
    size_t used = 0;
    bool too_long = false;
    for (size_t i = 0; i <= length; i++) {
        char c = i < length ? file_text[i] : '\n'; /* the last line needs no final newline */
        if (c != '\n') {
            too_long |= used + 1 >= sizeof line;
            if (!too_long && c != '\r')
                line[used++] = c;
            continue;
        }
        line[used] = '\0';
        if (!too_long)
            parse_line(line, &settings);
        used = 0;
        too_long = false;
    }
    return settings;
}

Settings settings_load(const char *dir)
{
    char path[SETTINGS_PATH_SIZE], text[SETTINGS_MAX_SIZE + 1];
    size_t length = 0;
    if (snprintf(path, sizeof path, "%s/%s", dir, SETTINGS_FILE) < (int)sizeof path) {
        FILE *file = fopen(path, "rb");
        if (file) {
            length = fread(text, 1, sizeof text, file); /* one byte above the limit tells a file that is too large */
            fclose(file);
        }
    }
    return settings_parse(text, length);
}

/* Commands */

bool settings_save(const char *dir, const Settings *settings)
{
    char path[SETTINGS_PATH_SIZE], temporary[SETTINGS_PATH_SIZE], text[LINE_SIZE];
    if (snprintf(path, sizeof path, "%s/%s", dir, SETTINGS_FILE) >= (int)sizeof path ||
        snprintf(temporary, sizeof temporary, "%s/%s", dir, TEMPORARY_FILE) >= (int)sizeof temporary ||
        settings->filter < SETTINGS_FILTER_NONE || settings->filter > SETTINGS_FILTER_SCALE3X)
        return false;
    int length = snprintf(text, sizeof text, "filter=%s\n", filter_names[settings->filter - SETTINGS_FILTER_NONE]);
    FILE *file = fopen(temporary, "wb");
    if (!file)
        return false;
    bool ok = fwrite(text, 1, (size_t)length, file) == (size_t)length;
    ok = fclose(file) == 0 && ok;
    if (ok && rename(temporary, path)) {
        remove(path); /* Windows does not replace a file by a rename */
        ok = !rename(temporary, path);
    }
    if (!ok)
        remove(temporary);
    return ok;
}
