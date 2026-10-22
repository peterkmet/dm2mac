/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_ROWS_H
#define DM2_UI_ROWS_H
/* Prefixes: rows_ */

/* The rows of text of the viewport screens of the advanced mode (not in the original game), such as the cheat
   screen. It draws a title and the rows in the viewport with the game font and the interface colors, and maps a
   mouse position to a row. It holds no screen state. */

#include <stdint.h>

/* The rows that fit below the title, and the characters of the font that fit in a row. */
#define ROWS_MAX_COUNT 11
#define ROWS_COLUMNS   36

/* Draws the title in cyan and the rows in white on black in the viewport, the selected row in yellow. */
void rows_draw(const char *title, const char *const *rows, int16_t row_count, int16_t selected);
/* The row of row_count rows at the screen position (x, y), or -1 outside the rows. */
int16_t rows_find_row(int16_t x, int16_t y, int16_t row_count);

#endif /* DM2_UI_ROWS_H */
