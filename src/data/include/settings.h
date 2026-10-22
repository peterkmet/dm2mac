/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_DATA_SETTINGS_H
#define DM2_DATA_SETTINGS_H
/* Prefixes: settings_ */

/* The settings file of the advanced mode (SETTINGS.TXT in the saves folder): the upscale filter as the text line
   filter=none|scale2x|scale3x. A player preference, not game state: the save file does not hold it. It reads and
   writes the file; it does not apply the value. */

#include <stdbool.h>
#include <stddef.h>

/* The settings file. */
#define SETTINGS_FILE     "SETTINGS.TXT"
#define SETTINGS_MAX_SIZE 1024 /* a larger file gives the defaults */

/* The upscale filters, the same numbers as the upscale factors of the window. */
#define SETTINGS_FILTER_NONE    1
#define SETTINGS_FILTER_SCALE2X 2
#define SETTINGS_FILTER_SCALE3X 3

/* The values of the settings file. */
typedef struct {
    int filter; /* SETTINGS_FILTER_* */
} Settings;

/* Queries */
/* The values of the settings text: a missing line, a bad value, or a line of 64 characters or more gives the default
   (no filter), an unknown key is ignored, a text above SETTINGS_MAX_SIZE gives all defaults; exported for the tests. */
Settings settings_parse(const char *file_text, size_t length);
/* The values of the settings file in dir, as settings_parse; a missing file gives the defaults. */
Settings settings_load(const char *dir);

/* Commands */
/* Writes the values to the settings file in dir through SETTINGS.TMP. False when the write fails; the old file then stays. */
bool settings_save(const char *dir, const Settings *settings);

#endif /* DM2_DATA_SETTINGS_H */
