/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_ROWS_H
#define DM2_UI_ROWS_H
/* Prefixes: rows_ */

/* The rows of text of the viewport screens of the advanced mode (not in the original game), such as the cheat
   screen. It draws a title line and the rows in the viewport with the game font and the interface colors, and maps a
   mouse position to a row. It holds no screen state. */

#include <stdint.h>

/* The rows that fit below the title, and the characters of the font that fit in a row. */
#define ROWS_MAX_COUNT 11
#define ROWS_COLUMNS   36

/* A screen of rows. The title item is selectable and comes after the last row: its index is row_count. */
typedef struct {
    const char *title;       /* cyan, at the left of the title line */
    const char *title_item;  /* right-aligned on the title line; NULL for none */
    const char *const *rows; /* row_count rows below the title line */
    int16_t row_count;
    uint16_t subtitles; /* bit i: row i is a cyan subtitle that cannot be selected */
} RowsScreen;

/* Draws the screen in the viewport: the title and the subtitles in cyan at the left margin, the rows in white one
   character further right, all on black; the selected row or title item in yellow. */
void rows_draw(const RowsScreen *screen, int16_t selected);
/* The row at the screen position (x, y), row_count for the title item, or -1 outside them and on a subtitle. */
int16_t rows_find_row(const RowsScreen *screen, int16_t x, int16_t y);

#endif /* DM2_UI_ROWS_H */
