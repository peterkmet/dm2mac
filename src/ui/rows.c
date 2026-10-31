/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "rows.h"
#include <string.h>
#include "game_text.h"
#include "ids.h"
#include "text.h"
#include "ui.h"
#include "zone.h"

/* The row layout in the viewport, in pixels. */
#define TITLE_LEFT 2                           /* the first text column of the title line and the subtitles */
#define LEFT       (TITLE_LEFT + FONT_ADVANCE) /* the first text column of a row */
#define TITLE_TOP  6                           /* the top of the title */
#define HEIGHT     10                          /* a row: the text line and the gap to the next row */
#define TOP        (TITLE_TOP + HEIGHT)        /* the top of the first row: one row below the title */

/* The left edge of the title item: right-aligned with the margin of the title. */
static int16_t find_item_left(const int16_t *rect, const char *item)
{
    return (int16_t)(rect[RECT_X] + rect[RECT_W] - TITLE_LEFT - (int)strlen(item) * FONT_ADVANCE + FONT_SPACING);
}

/* Draws a text line from left with its top at top pixels below the top of the viewport. */
static void show_line(Offscreen *o, int16_t left, int16_t top, int color, const char *line)
{
    int16_t baseline = (int16_t)(o->rect[RECT_Y] + top + FONT_GLYPH_HEIGHT - FONT_BASELINE - 1);
    offscreen_draw_text_at(o, left, baseline, (TextColors){g_ui_colors[color], g_ui_colors[UI_BLACK]}, line);
}

void rows_draw(const RowsScreen *screen, int16_t selected)
{
    Offscreen o;
    if (!zone_get_viewport_rect(o.rect))
        return;
    offscreen_create(&o, ZONE_NONE, true);
    if (!o.bitmap)
        return;
    offscreen_fill_rect(&o, o.rect, g_ui_colors[UI_BLACK]);
    show_line(&o, (int16_t)(o.rect[RECT_X] + TITLE_LEFT), TITLE_TOP, UI_CYAN, screen->title);
    if (screen->title_item)
        show_line(&o, find_item_left(o.rect, screen->title_item), TITLE_TOP,
                  selected == screen->row_count ? UI_YELLOW : UI_WHITE, screen->title_item);
    for (int16_t i = 0; i < screen->row_count && i < ROWS_MAX_COUNT; i++) {
        bool subtitle = screen->subtitles >> i & 1;
        int color = subtitle ? UI_CYAN : i == selected ? UI_YELLOW : UI_WHITE;
        show_line(&o, (int16_t)(o.rect[RECT_X] + (subtitle ? TITLE_LEFT : LEFT)),
                  (int16_t)(TOP + i * HEIGHT + (HEIGHT - TEXT_LINE_HEIGHT) / 2), color, screen->rows[i]);
    }
    offscreen_flush(&o, true);
}

int16_t rows_find_row(const RowsScreen *screen, int16_t x, int16_t y)
{
    int16_t r[RECT_SIZE];
    if (!zone_get_viewport_rect(r) || x < r[RECT_X] || x >= r[RECT_X] + r[RECT_W] || y < r[RECT_Y] ||
        y >= r[RECT_Y] + r[RECT_H])
        return -1;
    if (y < r[RECT_Y] + TOP)
        return screen->title_item && x >= find_item_left(r, screen->title_item) ? screen->row_count : -1;
    int16_t row = (int16_t)((y - r[RECT_Y] - TOP) / HEIGHT);
    return row < screen->row_count && row < ROWS_MAX_COUNT && !(screen->subtitles >> row & 1) ? row : -1;
}
