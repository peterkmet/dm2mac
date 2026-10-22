/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The advanced mode: the movement letter keys, the quick save and quick load, the automap, and the cheats. */
#include "advanced.h"
#include "ids.h"
#include "os.h"
#include "dungeon.h"
#include "game.h"
#include "game_text.h"
#include "minion.h"
#include "save.h"
#include "timeline.h"
#include "automap.h"
#include "draw.h"
#include "music.h"
#include "session.h"
#include "sound.h"
#include "text.h"
#include "ui.h"
#include "zone.h"
#include <stdbool.h>
#include <stdio.h>

/* The name in the header of QUICK.DAT. */
#define QUICK_SAVE_NAME "QUICK SAVE"

/* PC scan codes of the letter keys that move and turn the party. */
#define SCAN_Q 0x10
#define SCAN_W 0x11
#define SCAN_E 0x12
#define SCAN_A 0x1e
#define SCAN_S 0x1f
#define SCAN_D 0x20

/* PC scan codes of the keys on the open automap. */
#define SCAN_ESCAPE 0x01
#define SCAN_I      0x17
#define SCAN_F      0x21
#define SCAN_L      0x26

/* The layout of the readout: its margin, then its rows. */
#define READOUT_MARGIN    2 /* pixels between the viewport edge and the readout */
#define READOUT_POSITION  0
#define READOUT_DIRECTION 1
#define READOUT_IMMORTAL  2
#define READOUT_FOOD      3
#define READOUT_LIGHT     4

int g_advanced_automap_open;

static struct {
    bool active;
    int x, y; /* the mouse position that matches the current scroll */
} drag;

/* true after a quick load lost the game: there is no running game to keep. */
static bool lost;

static const char *const direction_names[DIR_COUNT] = {"NORTH", "EAST", "SOUTH", "WEST"};

/* Closes the automap and ends a drag of the map. */
static void close_automap(void)
{
    g_advanced_automap_open = 0;
    drag.active = false;
    automap_scroll_reset();
}

/* Setup */

void advanced_reset(void)
{
    close_automap();
    lost = false;
    g_cheat_light = 0;
    g_cheat_immortal = 0;
    g_cheat_food = 0;
}

/* Movement keys */

uint16_t advanced_movement_key(uint16_t key)
{
    if (g_game_mode != GAME_MODE_ADVANCED)
        return 0;
    switch (key) {
    case SCAN_W:
        return KEYPAD_FORWARD;
    case SCAN_S:
        return KEYPAD_BACK;
    case SCAN_A:
        return KEYPAD_LEFT;
    case SCAN_D:
        return KEYPAD_RIGHT;
    case SCAN_Q:
        return KEYPAD_TURN_LEFT;
    case SCAN_E:
        return KEYPAD_TURN_RIGHT;
    }
    return 0;
}

/* Quick save and load */

/* <save folder>/QUICK.<extension>: the --saves folder, else the DATA folder of the game. */
static char *quick_path(char *path, const char *extension)
{
    if (g_session.save_dir[0])
        snprintf(path, SAVE_PATH_SIZE, "%s/QUICK.%s", g_session.save_dir, extension);
    else
        snprintf(path, SAVE_PATH_SIZE, "%s/DATA/QUICK.%s", g_session.data_dir, extension);
    return path;
}

/* A white line in the message area. */
static void print_line(const char *text)
{
    char line[TEXT_SIZE];
    snprintf(line, sizeof line, "\n%s", text);
    text_print_message(UI_WHITE, line);
}

/* Writes the game to path (session_save prepares the timeline the same way); returns false on an error. */
static bool write_game(const char *path, const char *bak)
{
    timeline_rebuild_free_list();
    return save_write(path, bak, QUICK_SAVE_NAME, gdat_current()) == SAVE_OK;
}

bool advanced_quick_save(void)
{
    char path[SAVE_PATH_SIZE], bak[SAVE_PATH_SIZE];
    bool ok = write_game(quick_path(path, "DAT"), quick_path(bak, "BAK"));
    if (ok)
        remove(bak);
    else
        rename(bak, path); /* the old quick save stays */
    print_line(ok ? "GAME SAVED" : "UNABLE TO SAVE");
    return ok;
}

enum QuickLoadResult advanced_quick_load(void)
{
    char path[SAVE_PATH_SIZE], keep[SAVE_PATH_SIZE];
    FILE *f = fopen(quick_path(path, "DAT"), "rb");
    if (!f) {
        print_line("NO QUICK SAVE");
        return QUICK_MISSING;
    }
    fclose(f);
    /* the running game goes to QUICK.TMP first: a damaged quick save loads it back (the same path: no backup) */
    quick_path(keep, "TMP");
    if (!lost && !write_game(keep, keep)) {
        print_line("UNABLE TO SAVE");
        return QUICK_BLOCKED;
    }
    Session kept = g_session;
    /* play_start_session shows no LOADED dialog and starts no new game */
    g_session.loaded = 0;
    g_session.new_game = 0;
    bool loaded = session_start_save(path);
    bool restored = loaded || (!lost && session_start_save(keep));
    if (restored)
        remove(keep); /* else QUICK.TMP keeps the running game */
    g_session = kept;
    if (!restored) {
        /* the dungeon is empty: the game cannot go on */
        os_log("cannot load %s or the running game in %s", path, keep);
        if (g_game_mode != GAME_MODE_TURNBASED) {
            session_quit();
            g_session.exit_code = EXIT_GAME_LOST;
        }
        lost = true;
        return QUICK_LOST;
    }
    lost = false;
    if (loaded) {
        /* a quick load is not a saved game for the QUIT question */
        g_session.load_time = 0;
        g_session.save_time = 0;
        sound_set_volume(g_game.sound_volume);
        music_set_volume(g_game.music_volume);
    }
    print_line(loaded ? "GAME LOADED" : "QUICK SAVE DAMAGED");
    return loaded ? QUICK_LOADED : QUICK_DAMAGED;
}

/* The automap */

void advanced_open_automap(void)
{
    g_advanced_automap_open = 1;
    advanced_draw_automap();
}

/* Line row of the readout at the top left of the viewport, in white on black. */
static void render_readout_line(Offscreen *o, int row, const char *text)
{
    int baseline = o->rect[RECT_Y] + READOUT_MARGIN + row * TEXT_LINE_HEIGHT + FONT_GLYPH_HEIGHT - FONT_BASELINE - 1;
    offscreen_draw_text_at(o, o->rect[RECT_X] + READOUT_MARGIN, baseline,
                           (TextColors){g_ui_colors[UI_WHITE], g_ui_colors[UI_BLACK]}, text);
}

static const char *on_off(int on)
{
    return on ? "ON" : "OFF";
}

void advanced_draw_automap(void)
{
    Offscreen o;
    char line[TEXT_SIZE];
    offscreen_create(&o, ZONE_VIEWPORT, true);
    if (!o.bitmap)
        return;
    automap_draw(&o, o.rect);
    snprintf(line, sizeof line, "LEVEL %d  X %d  Y %d", dungeon_map_level(g_game.party_map), g_game.party_x,
             g_game.party_y);
    render_readout_line(&o, READOUT_POSITION, line);
    render_readout_line(&o, READOUT_DIRECTION, direction_names[g_game.party_dir & DIR_MASK]);
    snprintf(line, sizeof line, "I IMMORTAL %s", on_off(g_cheat_immortal));
    render_readout_line(&o, READOUT_IMMORTAL, line);
    snprintf(line, sizeof line, "F FOOD AND WATER %s", on_off(g_cheat_food));
    render_readout_line(&o, READOUT_FOOD, line);
    snprintf(line, sizeof line, "L FULL LIGHT %s", on_off(g_cheat_light));
    render_readout_line(&o, READOUT_LIGHT, line);
    offscreen_flush(&o, true);
    draw_free_temp();
}

void advanced_automap_event(int type, int x, int y)
{
    int16_t r[RECT_SIZE];
    if (type == INPUT_KEY) {
        if (x == KEY_AUTOMAP || x == KEY_AUTOMAP_M || x == SCAN_ESCAPE) {
            close_automap();
            return;
        }
        int *cheat = x == SCAN_I   ? &g_cheat_immortal
                     : x == SCAN_F ? &g_cheat_food
                     : x == SCAN_L ? &g_cheat_light
                                   : NULL;
        if (!cheat)
            return;
        *cheat = !*cheat;
        advanced_draw_automap();
    } else if (type == INPUT_LEFT_RELEASED) {
        advanced_automap_drag(x, y);
        drag.active = false;
    } else if (type & INPUT_BUTTON_LEFT && zone_get_rect(ZONE_VIEWPORT, r) && x >= r[RECT_X] &&
               x < r[RECT_X] + r[RECT_W] && y >= r[RECT_Y] && y < r[RECT_Y] + r[RECT_H]) {
        drag.active = true;
        drag.x = x;
        drag.y = y;
    }
}

void advanced_automap_drag(int x, int y)
{
    if (!drag.active)
        return;
    int dx = (x - drag.x) / MAP_CELL_SIZE, dy = (y - drag.y) / MAP_CELL_SIZE;
    if (!dx && !dy)
        return;
    drag.x += dx * MAP_CELL_SIZE;
    drag.y += dy * MAP_CELL_SIZE;
    automap_scroll(-dx, -dy);
    advanced_draw_automap();
}
