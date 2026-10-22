/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_TEXT_H
#define DM2_UI_TEXT_H
/* Prefixes: text_clear_ text_draw text_init text_measure text_message_ text_print_ text_set_message_ text_wrap_ */

/* Text: the font, message wrapping and printing into the screen. */

#include "gdat.h"
#include "gfx.h"
#include <stdbool.h>
#include <stdint.h>

/* The font: 6 rows of 128 bytes in GDAT (1, 0, 7, 0), one byte per character and row, 5 pixels in bits 4 to 0. */
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

/* Loads the font and the UI colors and clears the message area state; returns false when they are missing. */
bool text_init(const gdat_t *gdat);

/* Draws text into dst with its base line at y. */
void text_draw(Bitmap *dst, int x, int y, TextColors colors, const char *text);
/* The width and height in pixels of a text line; returns false for an empty text. */
bool text_measure(const char *text, int16_t *width, int16_t *height);
/* Copies the part of text from *pos that fits in max_width pixels into line, broken at a space when it does not
   fit; *pos goes to the rest. Returns the width of line. */
int text_wrap_line(const char *text, char *line, int16_t *pos, int max_width);
/* Draws text with a shadow down and right into an 8 bpp dst; bg without TEXT_TRANSPARENT fills a box behind it
   first. */
void text_draw_with_shadow(Bitmap *dst, int x, int y, TextColors colors, const char *text);
/* Draws text into the screen; exported for the tests. */
void text_draw_on_screen(int x, int y, int color, int bg, const char *text);
/* Draws text without background into the view picture. */
void text_draw_in_viewport(int x, int y, int color, const char *text);
/* Draws text placed in a layout zone into the view picture. */
void text_draw_in_zone(int zone, int color, const char *text);
/* Draws text placed in a layout zone into the view picture, with a shadow. */
void text_draw_in_zone_shadowed(int zone, int color, int bg, const char *text);
/* Message area. color is a UI color. */
/* Moves the message cursor to a character column and a row; exported for the tests. */
void text_set_message_cursor(int column, int row);
/* The text of message row (0 at the top to MESSAGE_ROWS - 1); "" when the row is clear. */
const char *text_message_row(int row);
/* Clears the message rows whose show time is over. */
void text_clear_expired_rows(void);
/* Prints a message at the message cursor. */
void text_print_message(int color, const char *text);
/* Prints a number and a space at the message cursor; exported for the tests. */
void text_print_number(int color, int32_t value);
/* Prints a new line at the message cursor. */
void text_print_newline(void);

#endif /* DM2_UI_TEXT_H */
