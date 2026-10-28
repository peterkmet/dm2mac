/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_OPTMENU_H
#define DM2_UI_OPTMENU_H
/* Prefixes: optmenu_ */

/* The options menu of the advanced mode (not in the original game), a viewport screen: the FILTER row and CONTINUE.
   A change of the filter shows at once and goes to SETTINGS.TXT in the save folder. */

#include <stdbool.h>
#include <stdint.h>

/* The platform sets it: shows the filter (SETTINGS_FILTER_*) in the window; false when it cannot. NULL: no window. */
extern bool (*g_optmenu_filter_hook)(int filter);

/* The rows of the menu. */
#define OPTMENU_ROW_FILTER   0
#define OPTMENU_ROW_CONTINUE 1
#define OPTMENU_ROW_COUNT    2

/* Setup */
/* Starts the menu state at the beginning of the program, after the game mode is chosen. filter is the SETTINGS_FILTER_*
   value that --filter or DM2_FILTER gave, or 0 for none given: the advanced mode then takes the filter of SETTINGS.TXT. */
void optmenu_start(int filter);

/* Queries */
/* The filter in use (SETTINGS_FILTER_*); exported for the tests. */
int optmenu_get_filter(void);
/* The selected row (OPTMENU_ROW_*); exported for the tests. */
int optmenu_get_row(void);

/* Commands */
/* Opens the menu in the viewport with the FILTER row selected. */
void optmenu_open(void);
/* A key on the open menu. Esc closes; Up, Down, W, and S move; Left, Right, A, and D change the filter (with wrap);
   Return on CONTINUE closes, on FILTER steps the filter. Returns true when the menu closes. */
bool optmenu_process_key(uint16_t key);
/* A left click at (x, y) on the open menu: FILTER steps the filter, CONTINUE closes. Returns true when the menu closes. */
bool optmenu_process_click(int16_t x, int16_t y);

#endif /* DM2_UI_OPTMENU_H */
