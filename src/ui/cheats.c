/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cheats.h"
#include "action.h"
#include "champion.h"
#include "game.h"
#include "game_text.h"
#include "save.h"
#include "rows.h"
#include "text.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* The rows of the cheat page: the six cheats, then the items. */
#define ROW_LIGHT         0
#define ROW_IMMORTAL      1
#define ROW_FOOD          2
#define ROW_KILL          3
#define ROW_THROUGH_WALLS 4
#define ROW_MAP           5
#define ROW_QUICK_SAVE    6
#define ROW_QUICK_LOAD    7
#define ROW_SPELLS        8
#define ROW_CLOSE         9
#define PAGE_ROW_COUNT    10

/* The items after the spells of a spell page. */
#define SPELL_ITEM_NEXT  0
#define SPELL_ITEM_BACK  1
#define SPELL_ITEM_CLOSE 2
#define SPELL_ITEM_COUNT 3
#define SPELLS_PER_PAGE  (ROWS_MAX_COUNT - SPELL_ITEM_COUNT)

/* The text of a row and of a spell line. */
#define ROW_TEXT_SIZE       40
#define TITLE_SIZE          24
#define RUNE_NUMBER_SIZE    4
#define CHEAT_LABEL_COLUMNS 20
#define ANY_POWER           "X"

static const char *const cheat_labels[ROW_QUICK_SAVE] = {"FULL LIGHT",   "IMMORTAL",           "FOOD AND WATER",
                                                         "ONE-HIT KILL", "WALK THROUGH WALLS", "REVEAL MAP"};
static const uint8_t cheat_bits[ROW_QUICK_SAVE] = {CHEAT_LIGHT, CHEAT_IMMORTAL, CHEAT_FOOD,
                                                   CHEAT_KILL,  CHEAT_WALLS,    CHEAT_MAP};
static const char *const spell_items[SPELL_ITEM_COUNT] = {"NEXT", "BACK", "CLOSE"};

static CheatsCursor screen;

/* The spells of the spell table. */
static int16_t count_spells(void)
{
    int16_t count = 0;
    while (magic_get_spell(count))
        count++;
    return count;
}

static int16_t count_spell_pages(void)
{
    return (int16_t)((count_spells() + SPELLS_PER_PAGE - 1) / SPELLS_PER_PAGE);
}

/* The spells on the shown spell page. */
static int16_t count_page_spells(void)
{
    int16_t left = (int16_t)(count_spells() - (screen.page - 1) * SPELLS_PER_PAGE);
    return left < SPELLS_PER_PAGE ? left : SPELLS_PER_PAGE;
}

/* The rows of the shown page. */
static int16_t count_rows(void)
{
    return screen.page == CHEATS_PAGE ? PAGE_ROW_COUNT : (int16_t)(count_page_spells() + SPELL_ITEM_COUNT);
}

/* Appends text to the line of size bytes in upper case; a full line keeps its start. */
static void append_text(char *line, size_t size, const char *text)
{
    size_t length = strlen(line);
    for (; *text && length + 1 < size; text++)
        line[length++] = (char)toupper((unsigned char)*text);
    line[length] = '\0';
}

/* Queries */

void cheats_get_spell_line(const Spell *spell, char *line, size_t size)
{
    char runes[ROW_TEXT_SIZE] = "";
    char numbers[ROW_TEXT_SIZE] = "(";
    for (int step = 0; step < CHAMPION_RUNES_SIZE; step++) {
        uint8_t glyph = magic_get_spell_rune(spell, step);
        if (!glyph && step)
            break;
        char number[RUNE_NUMBER_SIZE] = ANY_POWER; /* a spell without a power rune takes any power */
        if (glyph) {
            int rune = glyph - SPELL_RUNE_GLYPH_FIRST;
            const char *name = magic_get_rune_name(rune);
            if (name && runes[0])
                append_text(runes, sizeof runes, " ");
            if (name) /* a glyph outside the runes gives no name */
                append_text(runes, sizeof runes, name);
            snprintf(number, sizeof number, "%d", rune % SPELL_RUNES_PER_SET + 1);
        }
        if (step)
            append_text(numbers, sizeof numbers, ",");
        append_text(numbers, sizeof numbers, number);
    }
    append_text(numbers, sizeof numbers, ") ");
    append_text(numbers, sizeof numbers, magic_get_spell_name(spell));
    /* a line wider than a row drops the rune names and keeps the numbers */
    if (strlen(runes) + 1 + strlen(numbers) > ROWS_COLUMNS)
        snprintf(line, size, "%s", numbers);
    else
        snprintf(line, size, "%s %s", runes, numbers);
}

CheatsCursor cheats_get_cursor(void)
{
    return screen;
}

/* Commands */

/* Draws the cheat page with the state of each cheat. */
static void show_cheat_page(void)
{
    char texts[ROW_QUICK_SAVE][ROW_TEXT_SIZE];
    const char *rows[PAGE_ROW_COUNT] = {[ROW_QUICK_SAVE] = "QUICK SAVE F5",
                                        [ROW_QUICK_LOAD] = "QUICK LOAD F9",
                                        [ROW_SPELLS] = "SPELLS",
                                        [ROW_CLOSE] = "CLOSE"};
    for (int16_t i = ROW_LIGHT; i < ROW_QUICK_SAVE; i++) {
        snprintf(texts[i], ROW_TEXT_SIZE, "%-*s%s", CHEAT_LABEL_COLUMNS, cheat_labels[i],
                 game_has_cheat(cheat_bits[i]) ? "ON" : "OFF");
        rows[i] = texts[i];
    }
    rows_draw("CHEATS", rows, PAGE_ROW_COUNT, screen.row);
}

/* Draws the shown spell page: its spells, then NEXT, BACK, and CLOSE. */
static void show_spell_page(void)
{
    char title[TITLE_SIZE];
    char texts[SPELLS_PER_PAGE][ROW_TEXT_SIZE];
    const char *rows[ROWS_MAX_COUNT];
    int16_t count = count_page_spells();
    int first = (screen.page - 1) * SPELLS_PER_PAGE;
    for (int16_t i = 0; i < count; i++) {
        cheats_get_spell_line(magic_get_spell(first + i), texts[i], ROW_TEXT_SIZE);
        rows[i] = texts[i];
    }
    for (int16_t i = 0; i < SPELL_ITEM_COUNT; i++)
        rows[count + i] = spell_items[i];
    snprintf(title, sizeof title, "SPELLS %d/%d", screen.page, count_spell_pages());
    rows_draw(title, rows, (int16_t)(count + SPELL_ITEM_COUNT), screen.row);
}

/* Draws the shown page. */
static void show_page(void)
{
    if (screen.page == CHEATS_PAGE)
        show_cheat_page();
    else
        show_spell_page();
}

/* Shows a page with row selected; a spell page selects its NEXT row for -1. */
static void open_page(int16_t page, int16_t row)
{
    screen.page = page;
    screen.row = row >= 0 ? row : (int16_t)(count_page_spells() + SPELL_ITEM_NEXT);
    show_page();
}

void cheats_open(void)
{
    open_page(CHEATS_PAGE, ROW_LIGHT);
}

void cheats_move_selection(int16_t step)
{
    int16_t row = (int16_t)(screen.row + step);
    if (row >= 0 && row < count_rows())
        screen.row = row;
    show_page();
}

/* The caster of the spell area: the champion of the open spell panel, else the leader; -1 for none. */
static int16_t find_caster(void)
{
    if (g_action.champion > 0 && g_action.hand == ACTION_HAND_SPELL)
        return (int16_t)(g_action.champion - 1);
    return g_game.leader == LEADER_NONE ? -1 : (int16_t)g_game.leader;
}

/* The caster of the spell area casts spell without its cost; a potion without an empty flask says so. */
static void cast_spell(const Spell *spell)
{
    int16_t caster = find_caster();
    if (caster < 0 || caster >= g_game.champion_count)
        return;
    if ((magic_cast_cheat_spell(caster, spell) & CAST_KIND_MASK) == CAST_NEEDS_FLASK)
        text_print_message(UI_WHITE, "\nNO EMPTY FLASK IN HAND");
}

/* Runs a row of a spell page: a spell line casts the spell and closes the screen, so the player sees the effect;
   NEXT goes to the next spell page (after the last, the first), BACK to the page before (the cheat page before the
   first spell page). */
static enum CheatsResult run_spell_row(int16_t row)
{
    int16_t item = (int16_t)(row - count_page_spells());
    if (item < 0) {
        cast_spell(magic_get_spell((screen.page - 1) * SPELLS_PER_PAGE + row));
        return CHEATS_CLOSE;
    }
    if (item == SPELL_ITEM_NEXT)
        open_page((int16_t)(screen.page % count_spell_pages() + 1), -1);
    else if (item == SPELL_ITEM_BACK)
        open_page((int16_t)(screen.page - 1), screen.page == 1 ? ROW_SPELLS : -1);
    else
        return CHEATS_CLOSE;
    return CHEATS_STAY;
}

enum CheatsResult cheats_run_selected_row(void)
{
    if (screen.page != CHEATS_PAGE)
        return run_spell_row(screen.row);
    switch (screen.row) {
    case ROW_QUICK_SAVE:
        return CHEATS_QUICK_SAVE;
    case ROW_QUICK_LOAD:
        return CHEATS_QUICK_LOAD;
    case ROW_SPELLS:
        open_page(1, -1);
        return CHEATS_STAY;
    case ROW_CLOSE:
        return CHEATS_CLOSE;
    }
    game_toggle_cheat(cheat_bits[screen.row]);
    show_page();
    return CHEATS_STAY;
}

enum CheatsResult cheats_process_click(int16_t x, int16_t y)
{
    int16_t row = rows_find_row(x, y, count_rows());
    if (row < 0)
        return CHEATS_STAY;
    screen.row = row;
    show_page();
    return cheats_run_selected_row();
}
