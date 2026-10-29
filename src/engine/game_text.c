/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "game_text.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "dungeon.h"
#include "gdat.h"
#include "ids.h"
#include "save.h"

/* The format code bytes: this byte, then the code + FORMAT_CODE_BASE. */
#define FORMAT_CODE_BYTE     0x01
#define FORMAT_CODE_BASE     0x20
/* The sizes of a format code, and the number of action text fields. */
#define FORMAT_CODE_DIGITS   3 /* or ".Z" and 3 decimal digits */
#define FORMAT_CODE_PREFIX   2 /* the format code byte and the code, or ".Z" */
#define FIELD_COUNT          18
/* Format codes, and the GDAT entries that they read. */
#define FORMAT_TITLE         0x02
#define FORMAT_DATA_PATH     0x03
#define FORMAT_SAVE_PATH     0x04
#define FORMAT_CHAMPION      0x07
#define FORMAT_NUMBER_FIRST  0x0a
#define FORMAT_NUMBER_LAST   0x0e
#define FORMAT_DISK_PATH     0x0f
#define FORMAT_TEXT          0x11
#define FORMAT_TITLE_2       0x1b
#define FORMAT_TITLE_INDEX   0xfe
#define FORMAT_TEXT_CATEGORY 0x07

/* The characters of a padded number; a longer number goes on into the spare bytes. */
#define NUMBER_WIDTH 4
#define NUMBER_SPARE 4

/* Dungeon texts: 3 codes of 5 bits per word. */
#define TEXT_CODE_WIDTH     5
#define TEXT_CODES_PER_WORD 3
#define TEXT_ESCAPE_WORDS   10
/* The mask of a code, and the codes with a special meaning. */
#define TEXT_CODE_MASK      0x1f
#define TEXT_SPACE          0x1a
#define TEXT_PERIOD         0x1b
#define TEXT_NEWLINE        0x1c
#define TEXT_ESCAPE_CHAR    0x1d
#define TEXT_ESCAPE_WORD    0x1e
#define TEXT_END            0x1f
/* The ornament value of a text without an ornament. */
#define NO_ORNAMENT_TEXT    0xff

TextArgs g_text_args = {-1, {0}, 0};
char g_text_drive_path[TEXT_SIZE];
const uint8_t *g_ui_colors;

/* The two-letter name of each field of an action text, by FIELD_* number. */
static const char field_names[FIELD_COUNT][3] = {"SK", "LV", "CM", "BZ", "TR", "ST", "PA", "TA", "NC",
                                                 "EX", "PB", "DM", "MS", "SD", "RP", "HN", "AT", "WH"};
/* The words and the characters that the escape codes of a dungeon text insert. */
static const char *const text_escape_words[TEXT_ESCAPE_WORDS] = {"x", "y", "THE ", "YOU ", "z",
                                                                 "{", "|", "}",    "~",    "\x7f"};
static const char text_escape_chars[] = "abcdefghijklmnopqrstuvwx01234567";

/* The texts that gtext_format_number and gtext_gdat_label return. */
static struct {
    char number[NUMBER_SPARE + NUMBER_WIDTH + 1];
    char label[TEXT_SIZE];
} gtext;

/* Formatted texts */

const char *gtext_format_number(int value, bool pad, int width)
{
    char *end = gtext.number + NUMBER_SPARE + NUMBER_WIDTH;
    char *p = end;
    if (pad)
        memset(gtext.number + NUMBER_SPARE, ' ', NUMBER_WIDTH);
    *end = '\0';
    value = (uint16_t)value;
    if (!value)
        *--p = '0';
    for (; value; value /= DECIMAL_BASE)
        *--p = (char)('0' + value % DECIMAL_BASE);
    return pad ? end - width : p;
}

/* Appends src to the formatted text in dst. */
static char *append(char *dst, const char *src)
{
    size_t n = strlen(dst);
    snprintf(dst + n, TEXT_SIZE - n, "%s", src);
    return dst + strlen(dst);
}

char *gtext_format(const char *fmt, char *dst)
{
    char *out = dst;
    for (;;) {
        unsigned char c = (unsigned char)*fmt;
        if (!c)
            break;
        if (!(c == '.' && fmt[1] == 'Z') && c != FORMAT_CODE_BYTE) {
            if (out < dst + TEXT_SIZE - 1)
                *out++ = (char)c;
            fmt++;
            continue;
        }
        int code = 0;
        if (c == FORMAT_CODE_BYTE) {
            if (!fmt[1])
                break; /* fix: the original reads its code past a cut text */
            code = (unsigned char)fmt[1] - FORMAT_CODE_BASE;
            fmt += FORMAT_CODE_PREFIX;
        } else {
            fmt += FORMAT_CODE_PREFIX;
            for (int i = 0; i < FORMAT_CODE_DIGITS && *fmt; i++) /* fix: the original reads past a cut code */
                code = code * DECIMAL_BASE + (*fmt++ - '0');
        }
        *out = '\0';
        char value[TEXT_SIZE] = "", inner[TEXT_SIZE];
        if (code == FORMAT_CHAMPION) {
            if (g_text_args.champion >= 0 && g_text_args.champion < CHAMPION_MAX)
                snprintf(value, sizeof value, "%s", (const char *)g_game.champions[g_text_args.champion]);
        } else if (code >= FORMAT_NUMBER_FIRST && code <= FORMAT_NUMBER_LAST) {
            snprintf(value, sizeof value, "%d", g_text_args.number[code - FORMAT_NUMBER_FIRST]);
        } else if (code == FORMAT_TEXT) {
            gtext_gdat(FORMAT_TEXT_CATEGORY, 0, g_text_args.entry, value);
        } else if (code == FORMAT_TITLE || code == FORMAT_TITLE_2) {
            gtext_gdat(CAT_INTERFACE, FORMAT_TITLE_INDEX, code == FORMAT_TITLE ? 0 : GFX_FORMAT_TITLE_2, value);
        } else if (code == FORMAT_DATA_PATH || code == FORMAT_SAVE_PATH || code == FORMAT_DISK_PATH) {
            size_t len = strlen(g_text_drive_path);
            while (len > 1 && g_text_drive_path[len - 1] == '/')
                len--;
            const char *name = g_text_drive_path + len;
            while (name > g_text_drive_path && name[-1] != '/')
                name--;
            snprintf(value, sizeof value, "%.*s", (int)(g_text_drive_path + len - name),
                     name); /* the last folder only */
            for (char *p = value; *p; p++)
                *p = (char)toupper((unsigned char)*p); /* the font has no lowercase */
        } /* the error, memory, and file name codes of the DOS layer give "" */
        out = append(dst, gtext_format(value, inner));
    }
    *out = '\0';
    return dst;
}

char *gtext_gdat(int category, int index, int entry, char *dst)
{
    char raw[TEXT_SIZE];
    dst[0] = '\0';
    if (!gdat_current())
        return dst;
    gdat_text(gdat_current(), (GdatKey){category, index, entry}, raw, sizeof raw);
    return gtext_format(raw, dst);
}

const char *gtext_gdat_label(int category, int index, int entry)
{
    char text[TEXT_SIZE];
    if (!*gtext_gdat(category, index, entry, text))
        return NULL;
    size_t n = strcspn(text, ":");
    memcpy(gtext.label, text, n);
    gtext.label[n] = '\0';
    return gtext.label;
}

int16_t gtext_gdat_field(int category, int index, int entry, int field)
{
    char text[TEXT_SIZE];
    if ((unsigned)field >= FIELD_COUNT || !*gtext_gdat(category, index, entry, text))
        return 0;
    return (int16_t)gdat_text_int_field(text, field_names[field]);
}

/* Dungeon texts */

/* The characters of the packed text at text, into out up to end; returns the end of the output. */
static char *decode_words(uint16_t text, char *o, char *end)
{
    const uint16_t *p = dungeon_text_data() + textrec_offset(text);
    const uint16_t *text_end = dungeon_text_data() + dungeon_text_word_count();
    uint16_t word = 0;
    int escape = 0;
    for (int phase = 0;; phase = (phase + 1) % TEXT_CODES_PER_WORD) {
        if (phase == 0) {
            if (p >= text_end)
                break; /* fix: the original reads past the text of a damaged dungeon */
            word = *p++;
        }
        int code = word >> (TEXT_CODE_WIDTH * (TEXT_CODES_PER_WORD - 1 - phase)) & TEXT_CODE_MASK;
        if (escape) {
            const char *s = escape == TEXT_ESCAPE_WORD ? (code < TEXT_ESCAPE_WORDS ? text_escape_words[code] : "")
                                                       : (char[]){text_escape_chars[code], '\0'};
            while (*s && o < end)
                *o++ = *s++;
            escape = 0;
        } else if (code < TEXT_NEWLINE) {
            if (o < end)
                *o++ = code == TEXT_SPACE ? ' ' : code == TEXT_PERIOD ? '.' : (char)('A' + code);
        } else if (code == TEXT_NEWLINE) {
            if (o < end)
                *o++ = '\n';
        } else if (code == TEXT_END) {
            break;
        } else {
            escape = code;
        }
    }
    return o;
}

void gtext_decode_raw(char *dst, size_t size, uint16_t thing, bool new_line, bool hidden)
{
    char *o = dst;
    char *end = dst + size - 1;
    unsigned ornament = NO_ORNAMENT_TEXT;
    uint16_t text = thing;
    *o = '\0';
    if (thing_type(thing) == THING_SCROLL) {
        if (scroll_ornament_text(thing))
            ornament = (uint8_t)scroll_ornament_text(thing);
        else
            text = scroll_text(thing);
    } else if (thing_type(thing) != THING_TEXT) {
        return;
    }
    if (ornament == NO_ORNAMENT_TEXT) {
        int kind = textrec_kind(text);
        if (kind && (kind != TEXT_KIND_ORNAMENT || textrec_subtype(text) != SUBTYPE_PLATE))
            return;
        if (!textrec_is_visible(text) && !hidden)
            return;
        if (kind == TEXT_KIND_ORNAMENT)
            ornament = textrec_value(text);
    }
    if (new_line && o < end)
        *o++ = '\n';
    if (ornament != NO_ORNAMENT_TEXT) {
        if (gdat_current())
            gdat_text(gdat_current(), (GdatKey){CAT_DUNGEON, 0, (int)ornament}, o, (size_t)(end + 1 - o));
        return;
    }
    o = decode_words(text, o, end);
    *o = '\0';
}

char *gtext_decode_dungeon(char *dst, size_t size, uint16_t thing, bool new_line, bool hidden)
{
    char raw[TEXT_SIZE * 2], formatted[TEXT_SIZE];
    gtext_decode_raw(raw, sizeof raw, thing, new_line, hidden);
    gtext_format(raw, formatted);
    snprintf(dst, size, "%s", formatted);
    return dst;
}

/* The UI colors and the text arguments */

void gtext_gdat_set_ui_colors(const uint8_t *colors)
{
    g_ui_colors = colors;
}

void gtext_format_set_number(int index, int value)
{
    g_text_args.number[index] = (int16_t)value;
}

void gtext_format_set_entry(int entry)
{
    g_text_args.entry = (uint8_t)entry;
}
