/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mode_screen.h"
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "text.h"
#include "video.h"

/* The layout of the mode screen. */
#define HEADING_TOP 56 /* top of the heading line */
#define ITEM_TOP    88 /* top of the first item box */
#define ITEM_STEP   16 /* pixels from one item box to the next */
#define ITEM_MARGIN 8  /* box pixels left and right of the item text */
#define LINE_HEIGHT 12 /* height of an item box and of the heading line */
#define ITEM_COUNT  2  /* VANILLA and ADVANCED */

/* The palette colors of the screen; the arrow cursor keeps its colors black (0) and white. */
#define COLOR_BLACK      0x00
#define COLOR_LIGHT_GRAY 0x01
#define COLOR_GOLD       0x02

/* The 6-bit VGA values of the screen colors. */
#define VGA_FULL       63
#define VGA_TWO_THIRDS 42
#define SCREEN_COLORS  3

/* The heading and the items; the item index is the game mode (GAME_MODE_VANILLA, GAME_MODE_ADVANCED). */
static const char *const heading = "MODE";
static const char *const items[ITEM_COUNT] = {"VANILLA", "ADVANCED"};

/* The red, green, and blue of the screen colors, in the order COLOR_BLACK, COLOR_LIGHT_GRAY, COLOR_GOLD. */
static const uint8_t screen_palette[SCREEN_COLORS][RGB_CHANNELS] = {
    {0,              0,              0             },
    {VGA_TWO_THIRDS, VGA_TWO_THIRDS, VGA_TWO_THIRDS},
    {VGA_FULL,       VGA_TWO_THIRDS, 0             },
};

static int16_t cursor = GAME_MODE_ADVANCED;
static bool done;

/* ---- Setup ---- */

void mode_screen_start(void)
{
    cursor = GAME_MODE_ADVANCED;
    done = false;
}

/* ---- Queries ---- */

int mode_screen_cursor(void)
{
    return cursor;
}

bool mode_screen_is_done(void)
{
    return done;
}

bool mode_screen_item_box(int16_t item, int16_t *rect)
{
    int16_t w, h;
    if (item < 0 || item >= ITEM_COUNT || !text_measure(items[item], &w, &h))
        return false;
    rect[RECT_W] = (int16_t)(w + 2 * ITEM_MARGIN);
    rect[RECT_H] = LINE_HEIGHT;
    rect[RECT_X] = (int16_t)((SCREEN_W - rect[RECT_W]) / 2);
    rect[RECT_Y] = (int16_t)(ITEM_TOP + item * ITEM_STEP);
    return true;
}

/* ---- Commands ---- */

void mode_screen_key(int16_t action)
{
    if (done)
        return;
    if (action == MODE_SCREEN_UP && cursor > 0)
        cursor--;
    else if (action == MODE_SCREEN_DOWN && cursor < ITEM_COUNT - 1)
        cursor++;
    else if (action == MODE_SCREEN_ENTER)
        done = true;
}

void mode_screen_click(int16_t x, int16_t y)
{
    int16_t r[RECT_SIZE];
    if (done)
        return;
    for (int16_t i = 0; mode_screen_item_box(i, r); i++)
        if (x >= r[RECT_X] && x < r[RECT_X] + r[RECT_W] && y >= r[RECT_Y] && y < r[RECT_Y] + r[RECT_H]) {
            cursor = i;
            done = true;
            return;
        }
}

/* Draws text centered in the screen rectangle r. */
static void print_centered(const int16_t r[RECT_SIZE], int color, const char *text)
{
    int16_t w, h;
    if (!text_measure(text, &w, &h))
        return;
    int16_t x = (int16_t)(r[RECT_X] + (r[RECT_W] - w) / 2);
    int16_t top = (int16_t)(r[RECT_Y] + (r[RECT_H] - h) / 2);
    text_draw_on_screen(x, (int16_t)(top + h - 1), color, COLOR_BLACK, text);
}

void mode_screen_draw(void)
{
    int16_t r[RECT_SIZE];
    memset(g_video_fb, COLOR_BLACK, sizeof g_video_fb);
    memcpy(g_video_palette, screen_palette, sizeof screen_palette);
    print_centered((const int16_t[RECT_SIZE]){0, HEADING_TOP, SCREEN_W, LINE_HEIGHT}, COLOR_LIGHT_GRAY, heading);
    for (int16_t i = 0; mode_screen_item_box(i, r); i++)
        print_centered(r, i == cursor ? COLOR_GOLD : COLOR_LIGHT_GRAY, items[i]);
}
