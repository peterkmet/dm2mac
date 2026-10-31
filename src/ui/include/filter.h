/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_FILTER_H
#define DM2_UI_FILTER_H
/* Prefixes: upscale_ */

/* The screen filter of the window (not in the original game): the filter at the start, the filter in use, and the
   change from the SCREEN FILTER row of the cheat screen. A change shows at once and goes to SETTINGS.TXT in the
   save folder. The prefix upscale_ is the module prefix: the first word filter_ is in use in engine/. */

#include <stdbool.h>
#include "settings.h"

/* The platform sets it: shows the filter in the window; false when it cannot. NULL: no window. */
extern bool (*g_upscale_filter_hook)(enum SettingsFilter filter);

/* Setup */
/* Starts the filter state at the beginning of the program, after the game mode is chosen. filter is the value
   that --filter or DM2_FILTER gave, or SETTINGS_FILTER_UNSET for none given: the advanced mode then takes the
   filter of SETTINGS.TXT. */
void upscale_start(enum SettingsFilter filter);

/* Queries */
/* The filter in use. */
enum SettingsFilter upscale_get_filter(void);

/* Commands */
/* Changes to the next filter (NONE, SCALE2X, SCALE3X, then NONE again), shows it in the window, and writes it to
   SETTINGS.TXT. When the window cannot show the filter, the filter does not change; a failed write says
   UNABLE TO SAVE OPTIONS once per failure run and keeps the filter. */
void upscale_cycle_filter(void);

#endif /* DM2_UI_FILTER_H */
