#ifndef TEXT_H
#define TEXT_H
#include <stddef.h>
#include <stdint.h>
#include "gdat.h"
#include "game_text.h"
#include "gfx.h"

/* Prefixes: text_build_ text_clear_ text_draw text_init text_measure text_print_ text_set_message_ text_wrap_ */

/* The font: 6 rows of 128 bytes in GDAT (1, 0, 7, 0), one byte per character and row, 5 pixels in bits 4 to 0. */
#define FONT_GLYPH_WIDTH 5
#define FONT_GLYPH_HEIGHT 6
#define FONT_SPACING 1          /* the gap after a glyph */
#define FONT_BASELINE 1         /* rows of a glyph below the base line */
#define FONT_ADVANCE 6          /* glyph width plus spacing */
#define TEXT_LINE_HEIGHT 7
#define TEXT_TRANSPARENT 0x4000 /* flag of a background color: draw the text without its background */

/* Message area: 3 rows at the bottom of the screen. More rows scroll the area up. */
#define MESSAGE_ROWS 3
#define MESSAGE_SHOW_TIME 90    /* game ticks until a message row clears */
#define MESSAGE_INDENT 12       /* pixels: the rest of a wrapped message goes on indented */
#define ZONE_MESSAGE_AREA 0x0f

/* Game time at which each message row clears, MESSAGE_NEVER when it is clear. */
#define MESSAGE_NEVER 0xffffffffu
extern uint32_t message_expiry[MESSAGE_ROWS];

/* Loads the font and the UI colors and clears the message area state; returns 0 when they are missing. */
int text_init(const gdat_t *gdat);

/* A 4 bpp glyph of 6 x 6 pixels (3 bytes a row): fg on the set bits, bg on the others. */
const uint8_t *text_build_glyph(uint8_t c, uint8_t fg, uint8_t bg);
/* Draws text with its base line at y. bg with TEXT_TRANSPARENT leaves the background. In an 8 bpp bitmap,
   color and bg are palette colors; in a 4 bpp bitmap, they are pixel values. */
void text_draw(uint8_t *dst, int dst_width, int x, int y, int color, int bg, const char *text, int bpp);
/* The width and height in pixels of a text line; returns 0 for an empty text. */
int text_measure(const char *text, int16_t *width, int16_t *height);
/* Copies the part of text from *pos that fits in max_width pixels into line, broken at a space when it does not
   fit; *pos goes to the rest. Returns the width of line. */
int text_wrap_line(const char *text, char *line, int16_t *pos, int max_width);
/* Draws text with a shadow down and right; bg without TEXT_TRANSPARENT fills a box behind it first. */
void text_draw_with_shadow(uint8_t *dst, int dst_width, int x, int y, int color, int bg, const char *text);
void text_draw_on_screen(int x, int y, int color, int bg, const char *text);
/* Draws text without background into the view picture. */
void text_draw_in_viewport(int x, int y, int color, const char *text);
/* Draws text placed in a layout zone into the view picture. */
void text_draw_in_zone(int zone, int color, const char *text);
void text_draw_in_zone_with_shadow(int zone, int color, int bg, const char *text);
/* Message area. color is a UI color. */
void text_set_message_cursor(int column, int row);
/* Clears the message rows whose show time is over. */
void text_clear_expired_rows(void);
/* Prints a message, a character, a number and a space, or a new line at the message cursor. */
void text_print_message(int color, const char *text);
void text_print_char(int color, char c);
void text_print_number(int color, int32_t value);
void text_print_newline(void);

#endif
