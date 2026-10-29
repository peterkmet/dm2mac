/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_TEXT_H
#define DM2_UI_TEXT_H
/* Prefixes: text_ */

/* Text: the font, message wrapping and printing into the screen. */

#include <stdbool.h>
#include <stdint.h>
#include "gdat.h"
#include "gfx.h"

/* The font: 6 rows of 128 bytes in GDAT (CAT_INTERFACE, 0, GDAT_TYPE_FONT, GFX_FONT), one byte per character and row,
   5 pixels in bits 4 to 0. */
#define FONT_GLYPH_WIDTH  5
#define FONT_GLYPH_HEIGHT 6
#define FONT_SPACING      1 /* the gap after a glyph */
#define FONT_BASELINE     1 /* rows of a glyph below the base line */
#define FONT_ADVANCE      6 /* glyph width plus spacing */
#define TEXT_LINE_HEIGHT  7

/* Flag of a background color: draw the text without its background. */
#define TEXT_TRANSPARENT 0x4000

/* Message area: 3 rows at the bottom of the screen. More rows scroll the area up. */
#define MESSAGE_ROWS      3
#define MESSAGE_SHOW_TIME 90 /* game ticks until a message row clears */
#define MESSAGE_INDENT    12 /* pixels: the rest of a wrapped message goes on indented */

/* Game time at which each message row clears, MESSAGE_NEVER when it is clear. */
#define MESSAGE_NEVER 0xffffffffu
extern uint32_t g_text_message_expiry[MESSAGE_ROWS];
/* The characters of a message row (53 fit in the screen width) and the end. */
#define MESSAGE_TEXT_SIZE 64

/* The colors of a text: color on the glyph pixels, bg behind them; bg with TEXT_TRANSPARENT leaves the background.
   In an 8 bpp bitmap they are palette colors; in a 4 bpp bitmap, they are pixel values. */
typedef struct {
    int color, bg;
} TextColors;

/* ---- Setup ---- */

/* Loads the font and the UI colors and clears the message area state; returns false when they are missing. */
bool text_init(const Gdat *gdat);

/* ---- Queries ---- */

/* The size of a text line; {0, 0} for an empty text. */
ScreenSize text_measure(const char *text);
/* Copies the part of text from *out_pos that fits in max_width pixels into line, broken at a space when it does
   not fit; *out_pos goes to the rest. Returns the width of line. */
int16_t text_wrap_line(const char *text, char *line, int16_t *out_pos, int16_t max_width);
/* The text of message row (0 at the top to MESSAGE_ROWS - 1); "" when the row is clear. */
const char *text_message_row(int row);

/* ---- Drawing ---- */

/* Draws text into dst with its base line at y. */
void text_draw(Bitmap *dst, int16_t x, int16_t y, TextColors colors, const char *text);
/* Draws text with a shadow down and right into an 8 bpp dst; bg without TEXT_TRANSPARENT fills a box behind it
   first. */
void text_draw_with_shadow(Bitmap *dst, int16_t x, int16_t y, TextColors colors, const char *text);
/* Draws text into the screen; exported for the tests. */
void text_draw_on_screen(int16_t x, int16_t y, int color, int bg, const char *text);
/* Draws text without background into the view picture. */
void text_draw_in_viewport(int16_t x, int16_t y, int color, const char *text);
/* Draws text placed in a layout zone into the view picture. */
void text_draw_in_zone(int zone, int color, const char *text);
/* Draws text placed in a layout zone into the view picture, with a shadow. */
void text_draw_in_zone_shadowed(int zone, int color, int bg, const char *text);

/* ---- Message area ---- */

/* Moves the message cursor to a character column and a row; exported for the tests. */
void text_set_message_cursor(int column, int row);
/* Clears the message rows whose show time is over. */
void text_clear_expired_rows(void);
/* Prints a message at the message cursor in UI color ui_color. */
void text_print_message(int ui_color, const char *text);
/* Prints a number and a space at the message cursor in UI color ui_color; exported for the tests. */
void text_print_number(int ui_color, int32_t value);
/* Prints a new line at the message cursor. */
void text_print_newline(void);

#endif /* DM2_UI_TEXT_H */
