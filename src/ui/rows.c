/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "rows.h"
#include "game_text.h"
#include "ids.h"
#include "text.h"
#include "ui.h"
#include "zone.h"

/* The row layout in the viewport, in pixels. */
#define LEFT      8  /* the first text column */
#define TITLE_TOP 6  /* the top of the title */
#define TOP       22 /* the top of the first row */
#define HEIGHT    10 /* a row: the text line and the gap to the next row */

/* Draws a text line with its top at top pixels below the top of the viewport. */
static void show_line(Offscreen *o, int16_t top, int color, const char *line)
{
    int16_t baseline = (int16_t)(o->rect[RECT_Y] + top + FONT_GLYPH_HEIGHT - FONT_BASELINE - 1);
    offscreen_draw_text_at(o, (int16_t)(o->rect[RECT_X] + LEFT), baseline,
                           (TextColors){g_ui_colors[color], g_ui_colors[UI_BLACK]}, line);
}

void rows_draw(const char *title, const char *const *rows, int16_t row_count, int16_t selected)
{
    Offscreen o;
    if (!zone_get_viewport_rect(o.rect))
        return;
    offscreen_create(&o, ZONE_NONE, true);
    if (!o.bitmap)
        return;
    offscreen_fill_rect(&o, o.rect, g_ui_colors[UI_BLACK]);
    show_line(&o, TITLE_TOP, UI_CYAN, title);
    for (int16_t i = 0; i < row_count && i < ROWS_MAX_COUNT; i++)
        show_line(&o, (int16_t)(TOP + i * HEIGHT + (HEIGHT - TEXT_LINE_HEIGHT) / 2),
                  i == selected ? UI_YELLOW : UI_WHITE, rows[i]);
    offscreen_flush(&o, true);
}

int16_t rows_find_row(int16_t x, int16_t y, int16_t row_count)
{
    int16_t r[RECT_SIZE];
    if (!zone_get_viewport_rect(r) || x < r[RECT_X] || x >= r[RECT_X] + r[RECT_W] || y < r[RECT_Y] + TOP ||
        y >= r[RECT_Y] + r[RECT_H])
        return -1;
    int16_t row = (int16_t)((y - r[RECT_Y] - TOP) / HEIGHT);
    return row < row_count && row < ROWS_MAX_COUNT ? row : -1;
}
