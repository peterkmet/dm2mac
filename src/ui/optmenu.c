/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "optmenu.h"
#include <stdio.h>
#include "advanced.h"
#include "game.h"
#include "game_text.h"
#include "rows.h"
#include "settings.h"
#include "text.h"

/* The text of a row. */
#define FILTER_ROW_SIZE 40
#define LABEL_COLUMNS   10
#define FILTER_COUNT    (SETTINGS_FILTER_SCALE3X - SETTINGS_FILTER_NONE + 1)

static const char *const filter_names[FILTER_COUNT] = {"NONE", "SCALE2X", "SCALE3X"}; /* from SETTINGS_FILTER_NONE */

bool (*g_optmenu_filter_hook)(int filter);

static struct {
    int row;          /* the selected row */
    int filter;       /* the filter in use */
    int saved;        /* the filter of SETTINGS.TXT, or in use at the start when the file holds none */
    bool save_failed; /* the last write failed and its message shows */
} menu = {.filter = SETTINGS_FILTER_NONE, .saved = SETTINGS_FILTER_NONE};

/* The folder of SETTINGS.TXT: the --saves folder, else the DATA folder of the game (as QUICK.DAT). */
static char *find_folder(char *folder, size_t size)
{
    if (g_session.save_dir[0])
        snprintf(folder, size, "%s", g_session.save_dir);
    else
        snprintf(folder, size, "%s/DATA", g_session.data_dir);
    return folder;
}

/* Draws the menu with the current filter. */
static void show_menu(void)
{
    char filter_row[FILTER_ROW_SIZE];
    snprintf(filter_row, sizeof filter_row, "%-*s%s", LABEL_COLUMNS, "FILTER",
             filter_names[menu.filter - SETTINGS_FILTER_NONE]);
    const char *rows[OPTMENU_ROW_COUNT] = {[OPTMENU_ROW_FILTER] = filter_row, [OPTMENU_ROW_CONTINUE] = "CONTINUE"};
    rows_draw("OPTIONS", rows, OPTMENU_ROW_COUNT, menu.row);
}

/* Setup */

void optmenu_start(int filter)
{
    menu.row = OPTMENU_ROW_FILTER;
    menu.save_failed = false;
    if (filter) {
        menu.filter = filter;
        menu.saved = filter;
        return;
    }
    char dir[OS_DIR_SIZE + sizeof "/DATA"];
    menu.filter = SETTINGS_FILTER_NONE;
    if (g_game_mode == GAME_MODE_ADVANCED) {
        menu.filter = settings_load(find_folder(dir, sizeof dir)).filter;
        if (g_optmenu_filter_hook && menu.filter != SETTINGS_FILTER_NONE && !g_optmenu_filter_hook(menu.filter))
            menu.filter = SETTINGS_FILTER_NONE;
    }
    menu.saved = menu.filter;
}

/* Queries */

int optmenu_get_filter(void)
{
    return menu.filter;
}

int optmenu_get_row(void)
{
    return menu.row;
}

/* Commands */

void optmenu_open(void)
{
    menu.row = OPTMENU_ROW_FILTER;
    show_menu();
}

/* Writes SETTINGS.TXT when the filter differs from the file; a failed write says so once per failure run. */
static void write_change(void)
{
    char dir[OS_DIR_SIZE + sizeof "/DATA"];
    if (menu.filter == menu.saved)
        return;
    if (settings_save(find_folder(dir, sizeof dir), &(Settings){menu.filter})) {
        menu.saved = menu.filter;
        menu.save_failed = false;
    } else if (!menu.save_failed) {
        menu.save_failed = true;
        text_print_message(UI_WHITE, "\nUNABLE TO SAVE OPTIONS");
    }
}

/* Moves the filter one step down (-1) or up (1) with wrap, shows it in the window, and saves it. */
static void change_filter(int step)
{
    int filter = (menu.filter - SETTINGS_FILTER_NONE + step + FILTER_COUNT) % FILTER_COUNT + SETTINGS_FILTER_NONE;
    if (g_optmenu_filter_hook && !g_optmenu_filter_hook(filter))
        return;
    menu.filter = filter;
    write_change();
}

bool optmenu_process_key(uint16_t key)
{
    switch (key) {
    case SCAN_ESCAPE:
        return true;
    case SCAN_RETURN:
        if (menu.row == OPTMENU_ROW_CONTINUE)
            return true;
        change_filter(1);
        break;
    case KEYPAD_FORWARD:
    case SCAN_W:
        if (menu.row > OPTMENU_ROW_FILTER)
            menu.row--;
        break;
    case KEYPAD_BACK:
    case SCAN_S:
        if (menu.row < OPTMENU_ROW_CONTINUE)
            menu.row++;
        break;
    case KEYPAD_TURN_LEFT:
    case KEYPAD_LEFT:
    case SCAN_A:
    case KEYPAD_TURN_RIGHT:
    case KEYPAD_RIGHT:
    case SCAN_D:
        if (menu.row == OPTMENU_ROW_FILTER)
            change_filter(key == KEYPAD_TURN_LEFT || key == KEYPAD_LEFT || key == SCAN_A ? -1 : 1);
        break;
    default:
        return false;
    }
    show_menu();
    return false;
}

bool optmenu_process_click(int16_t x, int16_t y)
{
    int row = rows_find_row(x, y, OPTMENU_ROW_COUNT);
    if (row < 0)
        return false;
    menu.row = row;
    if (row == OPTMENU_ROW_CONTINUE)
        return true;
    change_filter(1);
    show_menu();
    return false;
}
