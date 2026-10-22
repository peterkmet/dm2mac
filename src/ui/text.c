/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "text.h"
#include "ids.h"
#include "game_text.h"
#include "save.h"
#include "draw.h"
#include "video.h"
#include "view.h"
#include "zone.h"
#include <stdio.h>
#include <string.h>

/* GDAT index and type of the font and the UI colors. */
#define FONT_INDEX 0x00
#define FONT_TYPE  0x07

/* Font bits of a glyph row: the first and the second pixel of a pixel pair. */
#define GLYPH_BIT_FIRST  0x10
#define GLYPH_BIT_SECOND 0x08

/* Sizes of the font, a glyph and the message area. */
#define FONT_CHARS            128
#define FONT_SIZE             (FONT_GLYPH_HEIGHT * FONT_CHARS)
#define GLYPH_BYTES_PER_ROW   3
#define GLYPH_PAIR_SHIFT      2
#define TEXT_COLOR_COUNT      2 /* the text color and the background */
#define NUMBER_SIZE           12
#define MESSAGE_WIDTH         SCREEN_W
#define MESSAGE_SCROLL_HEIGHT 7                                      /* rows the message area moves up for a new row */
#define MESSAGE_LINE_BASE     (TEXT_LINE_HEIGHT - FONT_BASELINE - 2) /* base line of the text in the new row bitmap */

uint32_t g_text_message_expiry[MESSAGE_ROWS];
/* The text of each message row, as its pixels show it. */
static char message_text[MESSAGE_ROWS][MESSAGE_TEXT_SIZE];

static uint8_t font[FONT_SIZE + FONT_CHARS]; /* characters 128 to 255 of the last row read past the font: zeros */
static uint8_t glyph[FONT_GLYPH_HEIGHT * GLYPH_BYTES_PER_ROW];

static struct {
    int16_t column; /* pixel column of the next text */
    int16_t row;
    int16_t top;         /* y of the first row in the message area */
    bool scroll_pending; /* the new row is in the row bitmap; the area scrolls before the next text */
    uint8_t line[TEXT_LINE_HEIGHT * SCREEN_W];
} message;
static Bitmap message_line = {message.line, SCREEN_W, TEXT_LINE_HEIGHT, IMAGE_8BPP};

bool text_init(const gdat_t *gdat)
{
    uint32_t size;
    const uint8_t *p = gdat_item(gdat, gdat_find(gdat, CAT_INTERFACE, FONT_INDEX, FONT_TYPE, GFX_FONT), &size);
    const uint8_t *colors =
        gdat_item(gdat, gdat_find(gdat, CAT_INTERFACE, FONT_INDEX, GDAT_TYPE_DATA, GFX_UI_COLORS), &size);
    if (!p || !colors)
        return false;
    memcpy(font, p, FONT_SIZE);
    g_ui_colors = colors;
    text_set_message_cursor(0, MESSAGE_ROWS - 1);
    for (int i = 0; i < MESSAGE_ROWS; i++) {
        g_text_message_expiry[i] = MESSAGE_NEVER;
        message_text[i][0] = '\0';
    }
    int16_t r[RECT_SIZE] = {0};
    zone_get_rect(ZONE_MESSAGE_AREA, r);
    message.top = (int16_t)((r[RECT_H] - (TEXT_LINE_HEIGHT * MESSAGE_ROWS - FONT_SPACING)) >> 1);
    return true;
}

/* A 4 bpp glyph of 6 x 6 pixels (3 bytes a row): fg on the set bits, bg on the others. */
static const uint8_t *build_glyph(uint8_t c, uint8_t fg, uint8_t bg)
{
    uint8_t *out = glyph;
    for (int row = 0; row < FONT_GLYPH_HEIGHT; row++) {
        uint8_t bits = font[row * FONT_CHARS + c];
        for (int pair = 0; pair < GLYPH_BYTES_PER_ROW; pair++, bits = (uint8_t)(bits << GLYPH_PAIR_SHIFT)) {
            uint8_t first = bits & GLYPH_BIT_FIRST ? fg : bg, second = bits & GLYPH_BIT_SECOND ? fg : bg;
            *out++ = (uint8_t)(first << NIBBLE_BIT_COUNT | second);
        }
    }
    return glyph;
}

void text_draw(Bitmap *dst, int x, int y, TextColors colors, const char *text)
{
    if (!*text)
        return;
    int color = colors.color, bg = colors.bg;
    int transparent = GFX_OPAQUE;
    if (bg & TEXT_TRANSPARENT) {
        bg &= ~TEXT_TRANSPARENT;
        transparent = (int16_t)bg;
    }
    uint8_t colormap[TEXT_COLOR_COUNT] = {(uint8_t)color, (uint8_t)bg};
    if (dst->bpp != IMAGE_4BPP) { /* the glyph holds 0 and 1, the colormap gives the colors */
        color = 0;
        bg = 1;
        if (transparent != GFX_OPAQUE)
            transparent = 1;
    }
    int16_t rect[RECT_SIZE] = {(int16_t)x, (int16_t)(y + FONT_BASELINE - (FONT_GLYPH_HEIGHT - 1)), FONT_ADVANCE,
                               FONT_GLYPH_HEIGHT};
    for (; *text; text++, rect[RECT_X] += FONT_ADVANCE) {
        Blit blit = {.src = build_glyph((uint8_t)*text, (uint8_t)color, (uint8_t)bg),
                     .dst = dst->pixels,
                     .src_width = FONT_GLYPH_WIDTH,
                     .dst_width = dst->width,
                     .src_bpp = IMAGE_4BPP,
                     .dst_bpp = dst->bpp,
                     .transparent = transparent,
                     .mirror = MIRROR_NONE,
                     .colormap = colormap};
        gfx_blit(&blit, rect);
    }
}

bool text_measure(const char *text, int16_t *width, int16_t *height)
{
    if (!text)
        return false;
    int16_t w = -FONT_SPACING;
    for (; *text; text++)
        w += FONT_ADVANCE;
    if (w <= 0)
        return false;
    *width = w;
    *height = FONT_GLYPH_HEIGHT - FONT_BASELINE;
    return true;
}

int text_wrap_line(const char *text, char *line, int16_t *pos, int max_width)
{
    int16_t p = *pos, out = 0, width = 0;
    int16_t space = -1, space_out = 0, space_width = 0;
    for (;;) {
        line[out] = '\0';
        char c = text[p];
        if (c == '\n' || c == '\0')
            break;
        if (c == ' ') {
            space = p;
            space_out = out;
            space_width = width;
        }
        if (width + FONT_ADVANCE > (int16_t)max_width) {
            if (space == -1)
                break;
            *pos = (int16_t)(space + 1);
            line[space_out] = '\0';
            return space_width;
        }
        if (width == 0)
            width = -FONT_SPACING;
        width += FONT_ADVANCE;
        line[out++] = text[p++];
    }
    *pos = p;
    return width;
}

void text_draw_with_shadow(Bitmap *dst, int x, int y, TextColors colors, const char *text)
{
    int16_t w, h;
    if (!text_measure(text, &w, &h))
        return;
    if (!(colors.bg & TEXT_TRANSPARENT)) {
        int16_t box[RECT_SIZE] = {(int16_t)(x - 1), (int16_t)(y - h), (int16_t)(w + 2), (int16_t)(h + 2)};
        gfx_fill_rect(dst->pixels, box, (uint16_t)colors.bg, dst->width, IMAGE_8BPP);
    }
    TextColors shadow = {g_ui_colors[UI_BLACK], g_ui_colors[UI_WHITE] | TEXT_TRANSPARENT};
    text_draw(dst, x, (int16_t)(y + 1), shadow, text);
    text_draw(dst, (int16_t)(x + 1), (int16_t)(y + 1), shadow, text);
    text_draw(dst, x, y, (TextColors){colors.color, (int16_t)(colors.bg | TEXT_TRANSPARENT)}, text);
}

void text_draw_on_screen(int x, int y, int color, int bg, const char *text)
{
    text_draw(&g_draw_screen, x, y, (TextColors){color, bg}, text);
}

void text_draw_in_viewport(int x, int y, int color, const char *text)
{
    text_draw(&g_view_viewport, x, y, (TextColors){color, g_ui_colors[UI_DARKEST_GRAY] | TEXT_TRANSPARENT}, text);
}

/* The base line start of text placed in zone; returns false when it is not visible. */
static bool baseline_in_zone(int zone, const char *text, int16_t *x, int16_t *y)
{
    int16_t w, h, r[RECT_SIZE];
    if (!text_measure(text, &w, &h) || !zone_compute_rect(NULL, r, (int16_t)zone, &w, &h))
        return false;
    *x = r[RECT_X];
    *y = (int16_t)(r[RECT_Y] + r[RECT_H] - 1);
    return true;
}

void text_draw_in_zone(int zone, int color, const char *text)
{
    int16_t x, y;
    if (baseline_in_zone(zone, text, &x, &y))
        text_draw_in_viewport(x, y, (uint16_t)color, text);
}

void text_draw_in_zone_shadowed(int zone, int color, int bg, const char *text)
{
    int16_t x, y;
    if (baseline_in_zone(zone, text, &x, &y))
        text_draw_with_shadow(&g_view_viewport, x, y, (TextColors){color, bg}, text);
}

void text_set_message_cursor(int column, int row)
{
    if (column < 0)
        column = 0;
    int16_t x = (int16_t)(column * FONT_ADVANCE), limit = MESSAGE_WIDTH - FONT_ADVANCE;
    message.column = x < limit ? x : limit;
    message.row = (int16_t)(row < 0 ? 0 : row > MESSAGE_ROWS - 1 ? MESSAGE_ROWS - 1 : row);
    message.scroll_pending = false;
}

const char *text_message_row(int row)
{
    return row >= 0 && row < MESSAGE_ROWS ? message_text[row] : "";
}

void text_clear_expired_rows(void)
{
    int16_t r[RECT_SIZE];
    zone_get_rect(ZONE_MESSAGE_AREA, r);
    for (int i = 0; i < MESSAGE_ROWS; i++) {
        if (g_text_message_expiry[i] == MESSAGE_NEVER || g_text_message_expiry[i] > g_game.game_time)
            continue;
        int16_t row[RECT_SIZE] = {r[RECT_X], (int16_t)(message.top + TEXT_LINE_HEIGHT * i + r[RECT_Y]), r[RECT_W],
                                  FONT_GLYPH_HEIGHT};
        gfx_fill_rect(g_draw_screen.pixels, row, g_ui_colors[UI_BLACK], g_draw_screen.width, IMAGE_8BPP);
        g_text_message_expiry[i] = MESSAGE_NEVER;
        message_text[i][0] = '\0';
    }
}

/* Moves the message area up by one row and shows the new row from the row bitmap at its bottom. */
static void shift_message_area(void)
{
    int16_t r[RECT_SIZE];
    zone_get_rect(ZONE_MESSAGE_AREA, r);
    /* the scroll service of the original is not in SKULL.EXE: move the rows up and keep the bottom rows */
    for (int y = r[RECT_Y]; y < r[RECT_Y] + r[RECT_H] - MESSAGE_SCROLL_HEIGHT; y++)
        memcpy(g_draw_screen.pixels + y * g_draw_screen.width + r[RECT_X],
               g_draw_screen.pixels + (y + MESSAGE_SCROLL_HEIGHT) * g_draw_screen.width + r[RECT_X], r[RECT_W]);
    r[RECT_Y] = (int16_t)(r[RECT_Y] + r[RECT_H] - FONT_GLYPH_HEIGHT);
    r[RECT_H] = FONT_GLYPH_HEIGHT;
    Blit blit = {.src = message.line,
                 .dst = g_draw_screen.pixels,
                 .src_width = MESSAGE_WIDTH,
                 .dst_width = g_draw_screen.width,
                 .src_bpp = IMAGE_8BPP,
                 .dst_bpp = IMAGE_8BPP,
                 .transparent = GFX_OPAQUE,
                 .mirror = MIRROR_NONE};
    gfx_blit(&blit, r);
}

/* Goes to the next message row; at the bottom row the new row waits in the row bitmap. */
static void new_message_line(void)
{
    if (message.row != MESSAGE_ROWS - 1) {
        message.scroll_pending = false;
        message.row++;
        return;
    }
    if (message.scroll_pending)
        shift_message_area();
    gfx_fill_bitmap(&message_line, g_ui_colors[UI_BLACK]);
    message.scroll_pending = true;
    for (int i = 0; i < MESSAGE_ROWS - 1; i++) {
        g_text_message_expiry[i] = g_text_message_expiry[i + 1];
        memcpy(message_text[i], message_text[i + 1], MESSAGE_TEXT_SIZE);
    }
    g_text_message_expiry[MESSAGE_ROWS - 1] = MESSAGE_NEVER;
    message_text[MESSAGE_ROWS - 1][0] = '\0';
}

/* Draws a message line at the cursor (or into the pending row) and starts its show time. */
static void show_message_line(int color, const char *line)
{
    int16_t r[RECT_SIZE];
    zone_get_rect(ZONE_MESSAGE_AREA, r);
    if (message.scroll_pending)
        text_draw(&message_line, message.column, MESSAGE_LINE_BASE, (TextColors){color, g_ui_colors[UI_BLACK]}, line);
    else
        text_draw_on_screen(
            (int16_t)(r[RECT_X] + message.column),
            (int16_t)(message.row * TEXT_LINE_HEIGHT + r[RECT_Y] + FONT_GLYPH_HEIGHT - FONT_BASELINE - 1 + message.top),
            color, g_ui_colors[UI_BLACK], line);
    g_text_message_expiry[message.row] = g_game.game_time + MESSAGE_SHOW_TIME;
    char *text = message_text[message.row];
    size_t length = strlen(text);
    snprintf(text + length, MESSAGE_TEXT_SIZE - length, "%s", line);
}

/* Shows the row bitmap of a new row that is still pending. */
static void flush_pending_row(void)
{
    if (!message.scroll_pending)
        return;
    shift_message_area();
    gfx_fill_bitmap(&message_line, g_ui_colors[UI_BLACK]);
    message.scroll_pending = false;
}

void text_print_message(int color, const char *text)
{
    flush_pending_row();
    int c = g_ui_colors[color];
    char line[TEXT_SIZE];
    int16_t pos = 0;
    while (text[pos]) {
        int width = text_wrap_line(text, line, &pos, MESSAGE_WIDTH - message.column);
        if (text[pos] == '\n') {
            new_message_line();
            show_message_line(c, line);
            message.column = 0;
            pos++;
        } else if (text[pos] == '\0') {
            show_message_line(c, line);
            message.column = (int16_t)(message.column + width);
        } else {
            show_message_line(c, line);
            new_message_line();
            message.column = MESSAGE_INDENT;
        }
    }
    flush_pending_row();
}

void text_print_number(int color, int32_t value)
{
    char s[NUMBER_SIZE];
    snprintf(s, sizeof s, "%d", value);
    text_print_message(color, s);
    text_print_message(color, " ");
}

void text_print_newline(void)
{
    text_print_message(UI_BLACK, "\n");
}
