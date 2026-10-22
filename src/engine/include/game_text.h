/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_GAME_TEXT_H
#define DM2_ENGINE_GAME_TEXT_H
/* Prefixes: text_decode_ text_format text_gdat */

/* The game texts and the UI colors: decoding, formatting, and GDAT text items. */

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
/* The folder of the game files and the saves: the drive path codes of the dialog texts. */
extern char g_text_drive_path[TEXT_SIZE];

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

/* A number as decimal text; pad right-aligns it in width characters with spaces. Uses one static buffer. */
const char *text_format_number(unsigned value, int pad, int width);
/* Copies fmt to dst with the format codes replaced; returns dst (TEXT_SIZE bytes). */
char *text_format(const char *fmt, char *dst);
/* The text entry (category, index, 5, entry), decrypted and formatted, in dst (TEXT_SIZE bytes). */
char *text_gdat(int category, int index, int entry, char *dst);
/* The part of a text entry before its first ':', NULL for an absent text. Uses one static buffer. */
const char *text_gdat_label(int category, int index, int entry);
/* The number of field (0 SK, 1 LV, 2 CM, ... 17 WH) of a text entry, 0 when it is absent. */
int16_t text_gdat_field(int category, int index, int entry, int field);

/* The text of a scroll or a text thing as stored, without the format pass; mode as in text_decode_dungeon. */
void text_decode_raw(uint16_t thing, int mode, char *out, size_t size);
/* The text of a scroll or a text thing with its format codes replaced; mode 1 starts with a new line, mode bit 15
   also gives a hidden text. */
char *text_decode_dungeon(char *dst, size_t size, uint16_t thing, int mode);

#endif /* DM2_ENGINE_GAME_TEXT_H */
