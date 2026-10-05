#include <stdio.h>
#include <string.h>
#include "draw.h"
#include "dungeon.h"
#include "save.h"
#include "text.h"
#include "view.h"
#include "zone.h"

#define FONT_CATEGORY 1
#define FONT_INDEX 0
#define FONT_TYPE 7
#define FONT_ENTRY 0
#define FONT_CHARS 128
#define FONT_SIZE (FONT_GLYPH_HEIGHT * FONT_CHARS)
#define UI_COLORS_TYPE 0x0d
#define UI_COLORS_ENTRY 0xfe
#define GLYPH_BYTES_PER_ROW 3
#define GLYPH_BIT_FIRST 0x10        /* font bit of the first pixel of a pixel pair */
#define GLYPH_BIT_SECOND 0x08
#define FORMAT_CODE_BYTE 1          /* a format code: this byte, then the code + FORMAT_CODE_BASE */
#define FORMAT_CODE_BASE 0x20
#define FORMAT_CODE_DIGITS 3        /* or ".Z" and 3 decimal digits */
#define FIELD_COUNT 18
#define NUMBER_SIZE 12
#define SCREEN_WIDTH_PIXELS 320
#define MESSAGE_WIDTH SCREEN_WIDTH_PIXELS
#define MESSAGE_SCROLL_HEIGHT 7     /* rows the message area moves up for a new row */
#define MESSAGE_LINE_BASE (TEXT_LINE_HEIGHT - FONT_BASELINE - 2)  /* base line of the text in the new row bitmap */

/* Format codes. */
enum {
    FORMAT_TITLE = 2, FORMAT_CHAMPION = 7, FORMAT_NUMBER_FIRST = 10, FORMAT_NUMBER_LAST = 14, FORMAT_TEXT = 17,
    FORMAT_TITLE_2 = 27
};
#define FORMAT_TITLE_INDEX 0xfe
#define FORMAT_TITLE_2_ENTRY 6
#define FORMAT_TEXT_CATEGORY 7

TextArgs text_args = {-1, {0}, 0};
const uint8_t *ui_colors;
uint32_t message_expiry[MESSAGE_ROWS];

static uint8_t font[FONT_SIZE + FONT_CHARS];   /* characters 128 to 255 of the last row read past the font: zeros */
static uint8_t glyph[FONT_GLYPH_HEIGHT * GLYPH_BYTES_PER_ROW];
static const char field_names[FIELD_COUNT][3] = {
    "SK", "LV", "CM", "BZ", "TR", "ST", "PA", "TA", "NC", "EX", "PB", "DM", "MS", "SD", "RP", "HN", "AT", "WH"
};

static struct {
    int16_t column;             /* pixel column of the next text */
    int16_t row;
    int16_t top;                /* y of the first row in the message area */
    int16_t scroll_pending;     /* the new row is in the row bitmap; the area scrolls before the next text */
    uint8_t line[TEXT_LINE_HEIGHT * SCREEN_WIDTH_PIXELS];
} message;
static Bitmap message_line = {message.line, SCREEN_WIDTH_PIXELS, TEXT_LINE_HEIGHT, 8};

int text_init(const gdat_t *gdat)
{
    uint32_t size;
    const uint8_t *p = gdat_item(gdat, gdat_find(gdat, FONT_CATEGORY, FONT_INDEX, FONT_TYPE, FONT_ENTRY), &size);
    const uint8_t *colors = gdat_item(gdat, gdat_find(gdat, FONT_CATEGORY, FONT_INDEX, UI_COLORS_TYPE, UI_COLORS_ENTRY),
                                      &size);
    if (!p || !colors) return 0;
    memcpy(font, p, FONT_SIZE);
    ui_colors = colors;
    text_set_message_cursor(0, MESSAGE_ROWS - 1);
    for (int i = 0; i < MESSAGE_ROWS; i++) message_expiry[i] = MESSAGE_NEVER;
    int16_t r[RECT_SIZE] = {0};
    zone_get_rect(ZONE_MESSAGE_AREA, r);
    message.top = (int16_t)((r[RECT_H] - (TEXT_LINE_HEIGHT * MESSAGE_ROWS - FONT_SPACING)) >> 1);
    return 1;
}

const uint8_t *text_build_glyph(uint8_t c, uint8_t fg, uint8_t bg)
{
    uint8_t *out = glyph;
    for (int row = 0; row < FONT_GLYPH_HEIGHT; row++) {
        uint8_t bits = font[row * FONT_CHARS + c];
        for (int pair = 0; pair < GLYPH_BYTES_PER_ROW; pair++, bits = (uint8_t)(bits << 2)) {
            uint8_t first = bits & GLYPH_BIT_FIRST ? fg : bg, second = bits & GLYPH_BIT_SECOND ? fg : bg;
            *out++ = (uint8_t)(first << 4 | second);
        }
    }
    return glyph;
}

void gfx_draw_text(uint8_t *dst, int dst_width, int x, int y, int color, int bg, const char *text, int bpp)
{
    if (!*text) return;
    int transparent = GFX_OPAQUE;
    if (bg & TEXT_TRANSPARENT) {
        bg &= ~TEXT_TRANSPARENT;
        transparent = (int16_t)bg;
    }
    uint8_t colormap[2] = {(uint8_t)color, (uint8_t)bg};
    if (bpp != 4) {  /* the glyph holds 0 and 1, the colormap gives the colors */
        color = 0;
        bg = 1;
        if (transparent != GFX_OPAQUE) transparent = 1;
    }
    int16_t rect[RECT_SIZE] = {(int16_t)x, (int16_t)(y + FONT_BASELINE - (FONT_GLYPH_HEIGHT - 1)), FONT_ADVANCE,
                               FONT_GLYPH_HEIGHT};
    for (; *text; text++, rect[RECT_X] += FONT_ADVANCE)
        gfx_blit(text_build_glyph((uint8_t)*text, (uint8_t)color, (uint8_t)bg), dst, rect, 0, 0, FONT_GLYPH_WIDTH,
                 dst_width, transparent, MIRROR_NONE, 4, bpp, colormap);
}

int text_measure(const char *text, int16_t *width, int16_t *height)
{
    if (!text) return 0;
    int16_t w = -FONT_SPACING;
    for (; *text; text++) w += FONT_ADVANCE;
    if (w <= 0) return 0;
    *width = w;
    *height = FONT_GLYPH_HEIGHT - FONT_BASELINE;
    return 1;
}

int text_wrap_line(const char *text, char *line, int16_t *pos, int max_width)
{
    int16_t p = *pos, out = 0, width = 0;
    int16_t space = -1, space_out = 0, space_width = 0;
    for (;;) {
        line[out] = '\0';
        char c = text[p];
        if (c == '\n' || c == '\0') break;
        if (c == ' ') {
            space = p;
            space_out = out;
            space_width = width;
        }
        if (width + FONT_ADVANCE > (int16_t)max_width) {
            if (space == -1) break;
            *pos = (int16_t)(space + 1);
            line[space_out] = '\0';
            return space_width;
        }
        if (width == 0) width = -FONT_SPACING;
        width += FONT_ADVANCE;
        line[out++] = text[p++];
    }
    *pos = p;
    return width;
}

void text_draw_with_shadow(uint8_t *dst, int dst_width, int x, int y, int color, int bg, const char *text)
{
    int16_t w, h;
    if (!text_measure(text, &w, &h)) return;
    if (!(bg & TEXT_TRANSPARENT)) {
        int16_t box[RECT_SIZE] = {(int16_t)(x - 1), (int16_t)(y - h), (int16_t)(w + 2), (int16_t)(h + 2)};
        gfx_fill_rect(dst, box, (uint16_t)bg, dst_width, 8);
    }
    int shadow = ui_colors[UI_WHITE] | TEXT_TRANSPARENT;
    gfx_draw_text(dst, dst_width, x, (int16_t)(y + 1), ui_colors[UI_BLACK], shadow, text, 8);
    gfx_draw_text(dst, dst_width, (int16_t)(x + 1), (int16_t)(y + 1), ui_colors[UI_BLACK], shadow, text, 8);
    gfx_draw_text(dst, dst_width, x, y, color, (int16_t)(bg | TEXT_TRANSPARENT), text, 8);
}

void text_draw_on_screen(int x, int y, int color, int bg, const char *text)
{
    gfx_draw_text(draw_screen.pixels, draw_screen.width, x, y, color, bg, text, 8);
}

void text_draw_in_viewport(int x, int y, int color, const char *text)
{
    gfx_draw_text(view_viewport.pixels, view_viewport.width, x, y, color, ui_colors[UI_DARKEST_GRAY] | TEXT_TRANSPARENT,
                  text, 8);
}

/* The base line start of text placed in zone; returns 0 when it is not visible. */
static int text_zone_position(int zone, const char *text, int16_t *x, int16_t *y)
{
    int16_t w, h, r[RECT_SIZE];
    if (!text_measure(text, &w, &h) || !zone_compute_rect(NULL, r, (int16_t)zone, &w, &h, ZONE_NO_ANCHOR)) return 0;
    *x = r[RECT_X];
    *y = (int16_t)(r[RECT_Y] + r[RECT_H] - 1);
    return 1;
}

void text_draw_in_zone(int zone, int color, const char *text)
{
    int16_t x, y;
    if (text_zone_position(zone, text, &x, &y)) text_draw_in_viewport(x, y, (uint16_t)color, text);
}

void text_draw_in_zone_with_shadow(int zone, int color, int bg, const char *text)
{
    int16_t x, y;
    if (text_zone_position(zone, text, &x, &y))
        text_draw_with_shadow(view_viewport.pixels, view_viewport.width, x, y, color, bg, text);
}

const char *text_format_number(unsigned value, int pad, int width)
{
    enum { FIELD = 4, SPARE = 4 };     /* 4 characters; a longer number goes on into the spare bytes */
    static char buf[SPARE + FIELD + 1];
    char *end = buf + SPARE + FIELD, *p = end;
    if (pad) memset(buf + SPARE, ' ', FIELD);
    *end = '\0';
    value = (uint16_t)value;
    if (!value) *--p = '0';
    for (; value; value /= 10) *--p = (char)('0' + value % 10);
    return pad ? end - (uint16_t)width : p;
}

/* Appends src to the formatted text in dst. */
static char *append(char *dst, const char *src)
{
    size_t n = strlen(dst);
    snprintf(dst + n, TEXT_SIZE - n, "%s", src);
    return dst + strlen(dst);
}

char *text_format(const char *fmt, char *dst)
{
    char *out = dst;
    for (;;) {
        unsigned char c = (unsigned char)*fmt;
        if (!c) break;
        if (!(c == '.' && fmt[1] == 'Z') && c != FORMAT_CODE_BYTE) {
            if (out < dst + TEXT_SIZE - 1) *out++ = (char)c;
            fmt++;
            continue;
        }
        int code = 0;
        if (c == FORMAT_CODE_BYTE) {
            code = (unsigned char)fmt[1] - FORMAT_CODE_BASE;
            fmt += 2;
        } else {
            fmt += 2;
            for (int i = 0; i < FORMAT_CODE_DIGITS; i++) code = code * 10 + (*fmt++ - '0');
        }
        *out = '\0';
        char value[TEXT_SIZE] = "", inner[TEXT_SIZE];
        if (code == FORMAT_CHAMPION) {
            if (text_args.champion >= 0 && text_args.champion < CHAMPION_MAX)
                snprintf(value, sizeof value, "%s", (const char *)game.champions[text_args.champion]);
        } else if (code >= FORMAT_NUMBER_FIRST && code <= FORMAT_NUMBER_LAST) {
            snprintf(value, sizeof value, "%d", text_args.number[code - FORMAT_NUMBER_FIRST]);
        } else if (code == FORMAT_TEXT) {
            text_gdat(FORMAT_TEXT_CATEGORY, 0, text_args.entry, value);
        } else if (code == FORMAT_TITLE || code == FORMAT_TITLE_2) {
            text_gdat(FONT_CATEGORY, FORMAT_TITLE_INDEX, code == FORMAT_TITLE ? 0 : FORMAT_TITLE_2_ENTRY, value);
        }  /* ponytail: the drive, path, and system error codes of the dialogs give "" until Step 20 */
        out = append(dst, text_format(value, inner));
    }
    *out = '\0';
    return dst;
}

char *text_gdat(int category, int index, int entry, char *dst)
{
    char raw[TEXT_SIZE];
    dst[0] = '\0';
    if (!draw_gdat()) return dst;
    gdat_text(draw_gdat(), category, index, entry, raw, sizeof raw);
    return text_format(raw, dst);
}

const char *text_gdat_label(int category, int index, int entry)
{
    static char label[TEXT_SIZE];
    char text[TEXT_SIZE];
    if (!*text_gdat(category, index, entry, text)) return NULL;
    size_t n = strcspn(text, ":");
    memcpy(label, text, n);
    label[n] = '\0';
    return label;
}

int16_t text_gdat_field(int category, int index, int entry, int field)
{
    char text[TEXT_SIZE];
    if ((unsigned)field >= FIELD_COUNT || !*text_gdat(category, index, entry, text)) return 0;
    return (int16_t)text_find_int_field(text, field_names[field]);
}

/* Dungeon texts: 3 codes of 5 bits per word. */
#define TEXT_CODE_BITS 5
#define TEXT_CODE_MASK 0x1f
enum { TEXT_SPACE = 26, TEXT_PERIOD = 27, TEXT_NEWLINE = 28, TEXT_ESCAPE_CHAR = 29, TEXT_ESCAPE_WORD = 30, TEXT_END = 31 };
static const char *const text_escape_words[] = {"x", "y", "THE ", "YOU ", "z", "{", "|", "}", "~", "\x7f"};
static const char text_escape_chars[] = "abcdefghijklmnopqrstuvwx01234567";
#define TEXT_VISIBLE 0x0001
#define TEXT_KIND_SHIFT 1
#define TEXT_KIND_MASK 3
#define TEXT_KIND_ORNAMENT 1
#define TEXT_OFFSET_SHIFT 3
#define TEXT_SUBTYPE_SHIFT 11
#define TEXT_SUBTYPE_PLATE 14
#define SCROLL_TEXT_SHIFT 10       /* scroll word 2: an ornament text id in bits 10-15, else a text thing index */
#define TEXT_MODE_NEW_LINE 1
#define TEXT_MODE_HIDDEN 0x8000
#define TEXT_MODE_MASK 0x7fff
#define ORNAMENT_TEXT_CATEGORY 3
#define NO_ORNAMENT_TEXT 0xff

/* The text of a scroll or a text thing as stored, with its format codes; see text_decode_dungeon for mode. */
void text_decode_raw(uint16_t thing, int mode, char *out, size_t size)
{
    char *o = out, *end = out + size - 1;
    unsigned ornament = NO_ORNAMENT_TEXT;
    uint16_t text = thing;
    *o = '\0';
    if (thing_type(thing) == THING_SCROLL) {
        uint16_t w = thing_word(thing, 2);
        if (w >> SCROLL_TEXT_SHIFT) ornament = (uint8_t)(w >> SCROLL_TEXT_SHIFT);
        else text = (uint16_t)(THING_TEXT << THING_TYPE_SHIFT | (w & THING_INDEX_MASK));
    } else if (thing_type(thing) != THING_TEXT) {
        return;
    }
    if (ornament == NO_ORNAMENT_TEXT) {
        uint16_t w = thing_word(text, TEXT_DATA);
        int kind = w >> TEXT_KIND_SHIFT & TEXT_KIND_MASK;
        if (kind && (kind != TEXT_KIND_ORNAMENT || w >> TEXT_SUBTYPE_SHIFT != TEXT_SUBTYPE_PLATE)) return;
        if (!(w & TEXT_VISIBLE) && !(mode & TEXT_MODE_HIDDEN)) return;
        if (kind == TEXT_KIND_ORNAMENT) ornament = (uint8_t)(w >> TEXT_OFFSET_SHIFT);
    }
    if ((mode & TEXT_MODE_MASK) == TEXT_MODE_NEW_LINE && o < end) *o++ = '\n';
    if (ornament != NO_ORNAMENT_TEXT) {
        if (draw_gdat()) gdat_text(draw_gdat(), ORNAMENT_TEXT_CATEGORY, 0, (int)ornament, o, (size_t)(end + 1 - o));
        return;
    }
    const uint16_t *p = dungeon_text_data() + (thing_word(text, TEXT_DATA) >> TEXT_OFFSET_SHIFT);
    const uint16_t *text_end = dungeon_text_data() + dungeon_text_word_count();
    uint16_t word = 0;
    int escape = 0;
    for (int phase = 0;; phase = (phase + 1) % 3) {
        if (phase == 0) {
            if (p >= text_end) break;  /* fix: the original reads past the text of a damaged dungeon */
            word = *p++;
        }
        int code = word >> (TEXT_CODE_BITS * (2 - phase)) & TEXT_CODE_MASK;
        if (escape) {
            const char *s = escape == TEXT_ESCAPE_WORD ? (code < 10 ? text_escape_words[code] : "")
                                                       : (char[]){text_escape_chars[code], '\0'};
            while (*s && o < end) *o++ = *s++;
            escape = 0;
        } else if (code < TEXT_NEWLINE) {
            if (o < end) *o++ = code == TEXT_SPACE ? ' ' : code == TEXT_PERIOD ? '.' : (char)('A' + code);
        } else if (code == TEXT_NEWLINE) {
            if (o < end) *o++ = '\n';
        } else if (code == TEXT_END) {
            break;
        } else {
            escape = code;
        }
    }
    *o = '\0';
}

char *text_decode_dungeon(char *dst, size_t size, uint16_t thing, int mode)
{
    char raw[TEXT_SIZE * 2], formatted[TEXT_SIZE];
    text_decode_raw(thing, mode, raw, sizeof raw);
    text_format(raw, formatted);
    snprintf(dst, size, "%s", formatted);
    return dst;
}

void text_set_message_cursor(int column, int row)
{
    if (column < 0) column = 0;
    int16_t x = (int16_t)(column * FONT_ADVANCE), limit = MESSAGE_WIDTH - FONT_ADVANCE;
    message.column = x < limit ? x : limit;
    message.row = (int16_t)(row < 0 ? 0 : row > MESSAGE_ROWS - 1 ? MESSAGE_ROWS - 1 : row);
    message.scroll_pending = 0;
}

void text_clear_expired_rows(void)
{
    int16_t r[RECT_SIZE];
    zone_get_rect(ZONE_MESSAGE_AREA, r);
    for (int i = 0; i < MESSAGE_ROWS; i++) {
        if (message_expiry[i] == MESSAGE_NEVER || message_expiry[i] > game.game_time) continue;
        int16_t row[RECT_SIZE] = {r[RECT_X], (int16_t)(message.top + TEXT_LINE_HEIGHT * i + r[RECT_Y]), r[RECT_W],
                                  FONT_GLYPH_HEIGHT};
        gfx_fill_rect(draw_screen.pixels, row, ui_colors[UI_BLACK], draw_screen.width, 8);
        message_expiry[i] = MESSAGE_NEVER;
    }
}

/* Moves the message area up by one row and shows the new row from the row bitmap at its bottom. */
static void text_scroll_message_area(void)
{
    int16_t r[RECT_SIZE];
    zone_get_rect(ZONE_MESSAGE_AREA, r);
    /* ponytail: the IBMIOP scroll (service 0x105) is not decompiled; this moves the rows and keeps the bottom rows */
    for (int y = r[RECT_Y]; y < r[RECT_Y] + r[RECT_H] - MESSAGE_SCROLL_HEIGHT; y++)
        memcpy(draw_screen.pixels + y * draw_screen.width + r[RECT_X],
               draw_screen.pixels + (y + MESSAGE_SCROLL_HEIGHT) * draw_screen.width + r[RECT_X], r[RECT_W]);
    r[RECT_Y] = (int16_t)(r[RECT_Y] + r[RECT_H] - FONT_GLYPH_HEIGHT);
    r[RECT_H] = FONT_GLYPH_HEIGHT;
    gfx_blit(message.line, draw_screen.pixels, r, 0, 0, MESSAGE_WIDTH, draw_screen.width, GFX_OPAQUE, MIRROR_NONE, 8, 8,
             NULL);
}

/* Goes to the next message row; at the bottom row the new row waits in the row bitmap. */
static void text_message_new_line(void)
{
    if (message.row != MESSAGE_ROWS - 1) {
        message.scroll_pending = 0;
        message.row++;
        return;
    }
    if (message.scroll_pending) text_scroll_message_area();
    gfx_fill_bitmap(&message_line, ui_colors[UI_BLACK]);
    message.scroll_pending = 1;
    for (int i = 0; i < MESSAGE_ROWS - 1; i++) message_expiry[i] = message_expiry[i + 1];
    message_expiry[MESSAGE_ROWS - 1] = MESSAGE_NEVER;
}

/* Draws a message line at the cursor (or into the pending row) and starts its show time. */
static void text_message_draw_line(int color, const char *line)
{
    int16_t r[RECT_SIZE];
    zone_get_rect(ZONE_MESSAGE_AREA, r);
    if (message.scroll_pending)
        gfx_draw_text(message.line, MESSAGE_WIDTH, message.column, MESSAGE_LINE_BASE, color, ui_colors[UI_BLACK], line, 8);
    else
        text_draw_on_screen((int16_t)(r[RECT_X] + message.column),
                            (int16_t)(message.row * TEXT_LINE_HEIGHT + r[RECT_Y] + FONT_GLYPH_HEIGHT - FONT_BASELINE - 1
                                      + message.top),
                            color, ui_colors[UI_BLACK], line);
    message_expiry[message.row] = game.game_time + MESSAGE_SHOW_TIME;
}

/* Shows the row bitmap of a new row that is still pending. */
static void flush_pending_row(void)
{
    if (!message.scroll_pending) return;
    text_scroll_message_area();
    gfx_fill_bitmap(&message_line, ui_colors[UI_BLACK]);
    message.scroll_pending = 0;
}

void text_print_message(int color, const char *text)
{
    flush_pending_row();
    int c = ui_colors[color];
    char line[TEXT_SIZE];
    int16_t pos = 0;
    while (text[pos]) {
        int width = text_wrap_line(text, line, &pos, MESSAGE_WIDTH - message.column);
        if (text[pos] == '\n') {
            text_message_new_line();
            text_message_draw_line(c, line);
            message.column = 0;
            pos++;
        } else if (text[pos] == '\0') {
            text_message_draw_line(c, line);
            message.column = (int16_t)(message.column + width);
        } else {
            text_message_draw_line(c, line);
            text_message_new_line();
            message.column = MESSAGE_INDENT;
        }
    }
    flush_pending_row();
}

void text_print_char(int color, char c)
{
    char s[2] = {c, '\0'};
    text_print_message(color, s);
}

void text_print_number(int color, int32_t value)
{
    char s[NUMBER_SIZE];
    snprintf(s, sizeof s, "%d", value);
    text_print_message(color, s);
    text_print_message(color, " ");
}

void text_print_newline(void) { text_print_message(UI_BLACK, "\n"); }
