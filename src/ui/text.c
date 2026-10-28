/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "text.h"
#include <stdio.h>
#include <string.h>
#include "draw.h"
#include "game_text.h"
#include "ids.h"
#include "save.h"
#include "video.h"
#include "view.h"
#include "zone.h"

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

/* The base line start of a text in a zone, and whether the zone shows it. */
typedef struct {
    bool visible;
    int16_t x, y;
} Baseline;

uint32_t g_text_message_expiry[MESSAGE_ROWS];

static struct {
    uint8_t font[FONT_SIZE + FONT_CHARS]; /* characters 128 to 255 of the last row read past the font: zeros */
    uint8_t glyph[FONT_GLYPH_HEIGHT * GLYPH_BYTES_PER_ROW]; /* the glyph that build_glyph made last */
    char message_text[MESSAGE_ROWS][MESSAGE_TEXT_SIZE];     /* the text of each message row, as its pixels show it */
    int16_t column;                                         /* pixel column of the next text */
    int16_t row;
    int16_t top;         /* y of the first row in the message area */
    bool scroll_pending; /* the new row is in the row bitmap; the area scrolls before the next text */
    uint8_t line[TEXT_LINE_HEIGHT * SCREEN_W];
    Bitmap line_bitmap; /* the row bitmap over line */
} text_state = {
    .line_bitmap = {text_state.line, SCREEN_W, TEXT_LINE_HEIGHT, IMAGE_8BPP}
};

bool text_init(const Gdat *gdat)
{
    uint32_t size;
    const uint8_t *p = gdat_item(gdat, gdat_find(gdat, CAT_INTERFACE, FONT_INDEX, FONT_TYPE, GFX_FONT), &size);
    const uint8_t *colors =
        gdat_item(gdat, gdat_find(gdat, CAT_INTERFACE, FONT_INDEX, GDAT_TYPE_DATA, GFX_UI_COLORS), &size);
    if (!p || !colors)
        return false;
    memcpy(text_state.font, p, FONT_SIZE);
    gtext_gdat_set_ui_colors(colors);
    text_set_message_cursor(0, MESSAGE_ROWS - 1);
    for (int i = 0; i < MESSAGE_ROWS; i++) {
        g_text_message_expiry[i] = MESSAGE_NEVER;
        text_state.message_text[i][0] = '\0';
    }
    int16_t r[RECT_SIZE] = {0};
    zone_get_rect(ZONE_MESSAGE_AREA, r);
    text_state.top = (int16_t)((r[RECT_H] - (TEXT_LINE_HEIGHT * MESSAGE_ROWS - FONT_SPACING)) >> 1);
    return true;
}

/* A 4 bpp glyph of 6 x 6 pixels (3 bytes a row): fg on the set bits, bg on the others. */
static const uint8_t *build_glyph(uint8_t c, uint8_t fg, uint8_t bg)
{
    uint8_t *out = text_state.glyph;
    for (int row = 0; row < FONT_GLYPH_HEIGHT; row++) {
        uint8_t bits = text_state.font[row * FONT_CHARS + c];
        for (int pair = 0; pair < GLYPH_BYTES_PER_ROW; pair++, bits = (uint8_t)(bits << GLYPH_PAIR_SHIFT)) {
            uint8_t first = bits & GLYPH_BIT_FIRST ? fg : bg;
            uint8_t second = bits & GLYPH_BIT_SECOND ? fg : bg;
            *out++ = (uint8_t)(first << NIBBLE_BIT_COUNT | second);
        }
    }
    return text_state.glyph;
}

void text_draw(Bitmap *dst, int16_t x, int16_t y, TextColors colors, const char *text)
{
    if (!*text)
        return;
    int color = colors.color;
    int bg = colors.bg;
    int transparent = GFX_OPAQUE;
    if (bg & TEXT_TRANSPARENT) {
        bg &= ~TEXT_TRANSPARENT;
        transparent = bg;
    }
    uint8_t colormap[TEXT_COLOR_COUNT] = {(uint8_t)color, (uint8_t)bg};
    if (dst->bpp != IMAGE_4BPP) { /* the glyph holds 0 and 1, the colormap gives the colors */
        color = 0;
        bg = 1;
        if (transparent != GFX_OPAQUE)
            transparent = 1;
    }
    int16_t rect[RECT_SIZE] = {x, (int16_t)(y + FONT_BASELINE - (FONT_GLYPH_HEIGHT - 1)), FONT_ADVANCE,
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

TextSize text_measure(const char *text)
{
    if (!text)
        return (TextSize){0, 0};
    int16_t w = -FONT_SPACING;
    for (; *text; text++)
        w += FONT_ADVANCE;
    if (w <= 0)
        return (TextSize){0, 0};
    return (TextSize){w, FONT_GLYPH_HEIGHT - FONT_BASELINE};
}

int16_t text_wrap_line(const char *text, char *line, int16_t *out_pos, int16_t max_width)
{
    int16_t p = *out_pos;
    int16_t out = 0;
    int16_t width = 0;
    int16_t space = -1;
    int16_t space_out = 0;
    int16_t space_width = 0;
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
        if (width + FONT_ADVANCE > max_width) {
            if (space == -1)
                break;
            *out_pos = (int16_t)(space + 1);
            line[space_out] = '\0';
            return space_width;
        }
        if (width == 0)
            width = -FONT_SPACING;
        width += FONT_ADVANCE;
        line[out++] = text[p++];
    }
    *out_pos = p;
    return width;
}

void text_draw_with_shadow(Bitmap *dst, int16_t x, int16_t y, TextColors colors, const char *text)
{
    TextSize size = text_measure(text);
    if (!size.w)
        return;
    if (!(colors.bg & TEXT_TRANSPARENT)) {
        int16_t box[RECT_SIZE] = {(int16_t)(x - 1), (int16_t)(y - size.h), (int16_t)(size.w + 2),
                                  (int16_t)(size.h + 2)};
        gfx_fill_rect(dst->pixels, box, (uint16_t)colors.bg, dst->width, IMAGE_8BPP);
    }
    TextColors shadow = {g_ui_colors[UI_BLACK], g_ui_colors[UI_WHITE] | TEXT_TRANSPARENT};
    text_draw(dst, x, (int16_t)(y + 1), shadow, text);
    text_draw(dst, (int16_t)(x + 1), (int16_t)(y + 1), shadow, text);
    text_draw(dst, x, y, (TextColors){colors.color, colors.bg | TEXT_TRANSPARENT}, text);
}

void text_draw_on_screen(int16_t x, int16_t y, int color, int bg, const char *text)
{
    text_draw(&g_draw_screen, x, y, (TextColors){color, bg}, text);
}

void text_draw_in_viewport(int16_t x, int16_t y, int color, const char *text)
{
    text_draw(&g_view_viewport, x, y, (TextColors){color, g_ui_colors[UI_DARKEST_GRAY] | TEXT_TRANSPARENT}, text);
}

/* The base line start of text placed in zone; not visible for an empty text or a zone off the view. */
static Baseline baseline_in_zone(int zone, const char *text)
{
    TextSize size = text_measure(text);
    int16_t r[RECT_SIZE];
    if (!size.w || !zone_compute_rect(NULL, r, (int16_t)zone, &size.w, &size.h))
        return (Baseline){false, 0, 0};
    return (Baseline){true, r[RECT_X], (int16_t)(r[RECT_Y] + r[RECT_H] - 1)};
}

void text_draw_in_zone(int zone, int color, const char *text)
{
    Baseline at = baseline_in_zone(zone, text);
    if (at.visible)
        text_draw_in_viewport(at.x, at.y, color, text);
}

void text_draw_in_zone_shadowed(int zone, int color, int bg, const char *text)
{
    Baseline at = baseline_in_zone(zone, text);
    if (at.visible)
        text_draw_with_shadow(&g_view_viewport, at.x, at.y, (TextColors){color, bg}, text);
}

void text_set_message_cursor(int column, int row)
{
    if (column < 0)
        column = 0;
    int16_t x = (int16_t)(column * FONT_ADVANCE);
    int16_t limit = MESSAGE_WIDTH - FONT_ADVANCE;
    text_state.column = x < limit ? x : limit;
    text_state.row = (int16_t)(row < 0 ? 0 : row > MESSAGE_ROWS - 1 ? MESSAGE_ROWS - 1 : row);
    text_state.scroll_pending = false;
}

const char *text_message_row(int row)
{
    return row >= 0 && row < MESSAGE_ROWS ? text_state.message_text[row] : "";
}

void text_clear_expired_rows(void)
{
    int16_t r[RECT_SIZE];
    zone_get_rect(ZONE_MESSAGE_AREA, r);
    for (int i = 0; i < MESSAGE_ROWS; i++) {
        if (g_text_message_expiry[i] == MESSAGE_NEVER || g_text_message_expiry[i] > g_game.game_time)
            continue;
        int16_t row[RECT_SIZE] = {r[RECT_X], (int16_t)(text_state.top + TEXT_LINE_HEIGHT * i + r[RECT_Y]), r[RECT_W],
                                  FONT_GLYPH_HEIGHT};
        gfx_fill_rect(g_draw_screen.pixels, row, g_ui_colors[UI_BLACK], g_draw_screen.width, IMAGE_8BPP);
        g_text_message_expiry[i] = MESSAGE_NEVER;
        text_state.message_text[i][0] = '\0';
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
    Blit blit = {.src = text_state.line,
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
    if (text_state.row != MESSAGE_ROWS - 1) {
        text_state.scroll_pending = false;
        text_state.row++;
        return;
    }
    if (text_state.scroll_pending)
        shift_message_area();
    gfx_fill_bitmap(&text_state.line_bitmap, g_ui_colors[UI_BLACK]);
    text_state.scroll_pending = true;
    for (int i = 0; i < MESSAGE_ROWS - 1; i++) {
        g_text_message_expiry[i] = g_text_message_expiry[i + 1];
        memcpy(text_state.message_text[i], text_state.message_text[i + 1], MESSAGE_TEXT_SIZE);
    }
    g_text_message_expiry[MESSAGE_ROWS - 1] = MESSAGE_NEVER;
    text_state.message_text[MESSAGE_ROWS - 1][0] = '\0';
}

/* Draws a message line at the cursor (or into the pending row) and starts its show time. */
static void show_message_line(int color, const char *line)
{
    int16_t r[RECT_SIZE];
    zone_get_rect(ZONE_MESSAGE_AREA, r);
    if (text_state.scroll_pending)
        text_draw(&text_state.line_bitmap, text_state.column, MESSAGE_LINE_BASE,
                  (TextColors){color, g_ui_colors[UI_BLACK]}, line);
    else
        text_draw_on_screen((int16_t)(r[RECT_X] + text_state.column),
                            (int16_t)(text_state.row * TEXT_LINE_HEIGHT + r[RECT_Y] + FONT_GLYPH_HEIGHT -
                                      FONT_BASELINE - 1 + text_state.top),
                            color, g_ui_colors[UI_BLACK], line);
    g_text_message_expiry[text_state.row] = g_game.game_time + MESSAGE_SHOW_TIME;
    char *text = text_state.message_text[text_state.row];
    size_t length = strlen(text);
    snprintf(text + length, MESSAGE_TEXT_SIZE - length, "%s", line);
}

/* Shows the row bitmap of a new row that is still pending. */
static void flush_pending_row(void)
{
    if (!text_state.scroll_pending)
        return;
    shift_message_area();
    gfx_fill_bitmap(&text_state.line_bitmap, g_ui_colors[UI_BLACK]);
    text_state.scroll_pending = false;
}

void text_print_message(int color, const char *text)
{
    flush_pending_row();
    int c = g_ui_colors[color];
    char line[TEXT_SIZE];
    int16_t pos = 0;
    while (text[pos]) {
        int16_t width = text_wrap_line(text, line, &pos, (int16_t)(MESSAGE_WIDTH - text_state.column));
        if (text[pos] == '\n') {
            new_message_line();
            show_message_line(c, line);
            text_state.column = 0;
            pos++;
        } else if (text[pos] == '\0') {
            show_message_line(c, line);
            text_state.column = (int16_t)(text_state.column + width);
        } else {
            show_message_line(c, line);
            new_message_line();
            text_state.column = MESSAGE_INDENT;
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
