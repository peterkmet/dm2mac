/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_GAME_TEXT_H
#define DM2_ENGINE_GAME_TEXT_H
/* Prefixes: text_decode_ text_format text_gdat */

/* The game texts and the UI colors: decoding, formatting, and GDAT text items. */

#include <stddef.h>
#include <stdint.h>

#define TEXT_SIZE 256

/* The 16 UI colors: GDAT (1, 0, 13, 0xfe) maps them to palette colors. */
enum ui_color {
    UI_BLACK,
    UI_DARK_GRAY,
    UI_GRAY,
    UI_DARK_BROWN,
    UI_CYAN,
    UI_BROWN,
    UI_DARK_GREEN,
    UI_GREEN,
    UI_RED,
    UI_GOLD,
    UI_TAN,
    UI_YELLOW,
    UI_DARKEST_GRAY,
    UI_LIGHT_GRAY,
    UI_BLUE,
    UI_WHITE,
    UI_COLOR_COUNT
};
/* The palette colors of the UI colors (text_init reads them). */
extern const uint8_t *ui_colors;

/* The values that the format codes of a text insert (".Z" and 3 digits, or byte 1 and the code + 32). */
#define TEXT_ARG_NUMBERS 5
typedef struct {
    int16_t champion;                 /* code 7: the name of this champion, -1 none */
    int16_t number[TEXT_ARG_NUMBERS]; /* codes 10 to 14 */
    uint8_t entry;                    /* code 17: the text (7, 0, entry) */
} TextArgs;

extern TextArgs text_args;
/* The folder of the game files and the saves: the drive path codes of the dialog texts. */
extern char text_drive_path[TEXT_SIZE];

/* Fields of an action text. */
enum {
    FIELD_SKILL = 0,
    FIELD_LEVEL = 1,
    FIELD_COMMAND = 2,
    FIELD_DELAY = 3,
    FIELD_STAMINA = 4,
    FIELD_POWER = 5,
    FIELD_MODE = 6,
    FIELD_DEFENSE = 7,
    FIELD_CHARGES = 8,
    FIELD_EXPERIENCE = 9,
    FIELD_ATTACK = 10,
    FIELD_WOUNDS = 11,
    FIELD_SOUND = 13,
    FIELD_REPEATS = 14,
    FIELD_PIERCES = 15,
    FIELD_MELEE_KIND = 0x10, /* 1: full damage to creatures that take half damage from other hits */
    FIELD_HAND = 17
};

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
