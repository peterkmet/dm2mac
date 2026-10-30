/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_GAME_TEXT_H
#define DM2_ENGINE_GAME_TEXT_H
/* Prefixes: gtext_ */

/* The game texts and the UI colors: decoding, formatting, and GDAT text items. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The size of a formatted text. */
#define TEXT_SIZE 256

/* The 16 UI colors: GDAT (1, 0, 13, 0xfe) maps them to palette colors. */
#define UI_BLACK        0x00
#define UI_DARK_GRAY    0x01
#define UI_GRAY         0x02
#define UI_DARK_BROWN   0x03
#define UI_CYAN         0x04
#define UI_BROWN        0x05
#define UI_DARK_GREEN   0x06
#define UI_GREEN        0x07
#define UI_RED          0x08
#define UI_GOLD         0x09
#define UI_TAN          0x0a
#define UI_YELLOW       0x0b
#define UI_DARKEST_GRAY 0x0c
#define UI_LIGHT_GRAY   0x0d
#define UI_BLUE         0x0e
#define UI_WHITE        0x0f
/* The number of UI colors. */
#define UI_COLOR_COUNT  16
/* The palette colors of the UI colors (text_init reads them). */
extern const uint8_t *g_ui_colors;

/* The values that the format codes of a text insert (".Z" and 3 digits, or byte 1 and the code + 32). */
#define TEXT_ARG_NUMBERS 5
typedef struct {
    int16_t champion;                 /* code 7: the name of this champion, -1 none */
    int16_t number[TEXT_ARG_NUMBERS]; /* codes 10 to 14 */
    uint8_t entry;                    /* code 17: the text (7, 0, entry) */
} TextArgs;

extern TextArgs g_text_args;

/* Fields of an action text. */
#define FIELD_SKILL      0
#define FIELD_LEVEL      1
#define FIELD_COMMAND    2
#define FIELD_DELAY      3
#define FIELD_STAMINA    4
#define FIELD_POWER      5
#define FIELD_MODE       6
#define FIELD_DEFENSE    7
#define FIELD_CHARGES    8
#define FIELD_EXPERIENCE 9
#define FIELD_ATTACK     10
#define FIELD_WOUNDS     11
#define FIELD_SOUND      13
#define FIELD_REPEATS    14
#define FIELD_PIERCES    15
#define FIELD_MELEE_KIND 16 /* 1: full damage to creatures that take half damage from other hits */
#define FIELD_HAND       17

/* Formatted texts */
/* A number as decimal text; pad right-aligns it in width characters with spaces (width at most NUMBER_WIDTH, 4, of
   game_text.c). The text stays valid until the next call. */
const char *gtext_format_number(int value, bool pad, int width);
/* Copies fmt to dst with the format codes replaced; returns dst (TEXT_SIZE bytes). */
char *gtext_format(const char *fmt, char *dst);
/* The text entry (category, index, 5, entry), decrypted and formatted, in dst (TEXT_SIZE bytes). */
char *gtext_gdat(int category, int index, int entry, char *dst);
/* The part of a text entry before its first ':', NULL for an absent text. The text stays valid until the next
   call. */
const char *gtext_gdat_label(int category, int index, int entry);
/* The number of field (0 SK, 1 LV, 2 CM, ... 17 WH) of a text entry, 0 when it is absent. */
int16_t gtext_gdat_field(int category, int index, int entry, int field);

/* Dungeon texts */
/* The text of a scroll or a text thing as stored, without the format pass; new_line and hidden as in
   gtext_decode_dungeon. */
void gtext_decode_raw(char *dst, size_t size, uint16_t thing, bool new_line, bool hidden);
/* The text of a scroll or a text thing with its format codes replaced; new_line starts it with a new line, hidden
   also gives a hidden text. */
char *gtext_decode_dungeon(char *dst, size_t size, uint16_t thing, bool new_line, bool hidden);

/* The UI colors and the text arguments */
/* The UI colors that the font item of the interface GDAT gives (text_init). */
void gtext_gdat_set_ui_colors(const uint8_t *colors);
/* The number that format code 10 + index inserts. */
void gtext_format_set_number(int index, int value);
/* The text (7, 0, entry) that format code 17 inserts. */
void gtext_format_set_entry(int entry);

#endif /* DM2_ENGINE_GAME_TEXT_H */
