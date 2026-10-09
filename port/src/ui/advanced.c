/* The advanced mode: the movement letter keys, the quick save and quick load, the automap, and the cheats. */
#include <stdio.h>
#include "advanced.h"
#include "automap.h"
#include "draw.h"
#include "dungeon.h"
#include "input.h"
#include "minion.h"
#include "music.h"
#include "save.h"
#include "session.h"
#include "sound.h"
#include "text.h"
#include "timeline.h"
#include "ui.h"
#include "zone.h"

#define QUICK_SAVE_NAME "QUICK SAVE"    /* the name in the header of QUICK.DAT */

/* PC scan codes of the letter keys that move and turn the party. */
enum { SCAN_Q = 0x10, SCAN_W = 0x11, SCAN_E = 0x12, SCAN_A = 0x1e, SCAN_S = 0x1f, SCAN_D = 0x20 };

/* PC scan codes of the keys on the open automap. */
enum { SCAN_ESCAPE = 0x01, SCAN_I = 0x17, SCAN_F = 0x21, SCAN_L = 0x26 };
#define READOUT_MARGIN 2                /* pixels between the viewport edge and the readout */
/* The rows of the readout. */
enum { READOUT_POSITION, READOUT_DIRECTION, READOUT_IMMORTAL, READOUT_FOOD, READOUT_LIGHT };

int game_mode = GAME_MODE_VANILLA;
const char *const game_mode_names[GAME_MODE_COUNT] = {"vanilla", "advanced"};
int advanced_automap_open, cheat_light, cheat_immortal, cheat_food;

static struct {
    int active;
    int x, y;                           /* the mouse position that matches the current scroll */
} drag;

static const char *const direction_names[DIR_COUNT] = {"NORTH", "EAST", "SOUTH", "WEST"};

uint16_t advanced_movement_key(uint16_t key)
{
    if (game_mode == GAME_MODE_VANILLA) return 0;
    switch (key) {
    case SCAN_W: return KEYPAD_FORWARD;
    case SCAN_S: return KEYPAD_BACK;
    case SCAN_A: return KEYPAD_LEFT;
    case SCAN_D: return KEYPAD_RIGHT;
    case SCAN_Q: return KEYPAD_TURN_LEFT;
    case SCAN_E: return KEYPAD_TURN_RIGHT;
    }
    return 0;
}

static char *quick_path(char *path, const char *extension)
{
    snprintf(path, SAVE_PATH_SIZE, "%s/DATA/QUICK.%s", session.data_dir, extension);
    return path;
}

/* A white line in the message area. */
static void print_line(const char *text)
{
    char line[TEXT_SIZE];
    snprintf(line, sizeof line, "\n%s", text);
    text_print_message(UI_WHITE, line);
}

/* Writes the game to path (session_save prepares the timeline the same way); returns 0 on an error. */
static int write_game(const char *path, const char *bak)
{
    timeline_rebuild_free_list();
    return save_write(path, bak, QUICK_SAVE_NAME, gdat_current()) == SAVE_OK;
}

void advanced_quick_save(void)
{
    char path[SAVE_PATH_SIZE], bak[SAVE_PATH_SIZE];
    int ok = write_game(quick_path(path, "DAT"), quick_path(bak, "BAK"));
    if (ok) remove(bak);
    else rename(bak, path);  /* the old quick save stays */
    print_line(ok ? "GAME SAVED" : "UNABLE TO SAVE");
}

void advanced_quick_load(void)
{
    char path[SAVE_PATH_SIZE], keep[SAVE_PATH_SIZE];
    FILE *f = fopen(quick_path(path, "DAT"), "rb");
    if (!f) {
        print_line("NO QUICK SAVE");
        return;
    }
    fclose(f);
    /* the running game goes to QUICK.TMP first: a damaged quick save loads it back (the same path: no backup) */
    if (!write_game(quick_path(keep, "TMP"), keep)) {
        print_line("UNABLE TO SAVE");
        return;
    }
    Session kept = session;
    session.loaded = session.new_game = 0;  /* game_start_session shows no LOADED dialog and starts no new game */
    int loaded = session_start_save(path), restored = loaded || session_start_save(keep);
    if (restored) remove(keep);  /* else QUICK.TMP keeps the running game */
    session = kept;
    if (!restored) {
        /* the dungeon is empty: the game cannot go on */
        fprintf(stderr, "cannot load %s or the running game in %s\n", path, keep);
        session_quit();
        session.exit_code = EXIT_GAME_LOST;
        return;
    }
    if (loaded) {
        session.load_time = session.save_time = 0;  /* a quick load is not a saved game for the QUIT question */
        sound_set_volume(game.sound_volume);
        music_set_volume(game.music_volume);
    }
    print_line(loaded ? "GAME LOADED" : "QUICK SAVE DAMAGED");
}

/* Line row of the readout at the top left of the viewport, in white on black. */
static void draw_readout_line(Offscreen *o, int row, const char *text)
{
    int baseline = o->rect[RECT_Y] + READOUT_MARGIN + row * TEXT_LINE_HEIGHT + FONT_GLYPH_HEIGHT - FONT_BASELINE - 1;
    offscreen_draw_text_at(o, o->rect[RECT_X] + READOUT_MARGIN, baseline, ui_colors[UI_WHITE], ui_colors[UI_BLACK], text);
}

static const char *on_off(int on) { return on ? "ON" : "OFF"; }

void advanced_draw_automap(void)
{
    Offscreen o;
    char line[TEXT_SIZE];
    offscreen_create(&o, ZONE_VIEWPORT, 1);
    if (!o.bitmap) return;
    automap_draw(&o, o.rect);
    snprintf(line, sizeof line, "LEVEL %d  X %d  Y %d", dungeon_map_level(game.party_map), game.party_x, game.party_y);
    draw_readout_line(&o, READOUT_POSITION, line);
    draw_readout_line(&o, READOUT_DIRECTION, direction_names[game.party_dir & DIR_MASK]);
    snprintf(line, sizeof line, "I IMMORTAL %s", on_off(cheat_immortal));
    draw_readout_line(&o, READOUT_IMMORTAL, line);
    snprintf(line, sizeof line, "F FOOD AND WATER %s", on_off(cheat_food));
    draw_readout_line(&o, READOUT_FOOD, line);
    snprintf(line, sizeof line, "L FULL LIGHT %s", on_off(cheat_light));
    draw_readout_line(&o, READOUT_LIGHT, line);
    offscreen_flush(&o, 1);
    draw_free_temp();
}

static void close_automap(void)
{
    advanced_automap_open = drag.active = 0;
    automap_scroll_reset();
}

void advanced_reset(void)
{
    close_automap();
    cheat_light = cheat_immortal = cheat_food = 0;
}

void advanced_open_automap(void)
{
    advanced_automap_open = 1;
    advanced_draw_automap();
}

void advanced_automap_drag(int x, int y)
{
    if (!drag.active) return;
    int dx = (x - drag.x) / MAP_CELL_SIZE, dy = (y - drag.y) / MAP_CELL_SIZE;
    if (!dx && !dy) return;
    drag.x += dx * MAP_CELL_SIZE;
    drag.y += dy * MAP_CELL_SIZE;
    automap_scroll(-dx, -dy);
    advanced_draw_automap();
}

void advanced_automap_event(int type, int x, int y)
{
    int16_t r[RECT_SIZE];
    if (type == INPUT_KEY) {
        if (x == KEY_AUTOMAP || x == KEY_AUTOMAP_M || x == SCAN_ESCAPE) {
            close_automap();
            return;
        }
        int *cheat = x == SCAN_I ? &cheat_immortal : x == SCAN_F ? &cheat_food : x == SCAN_L ? &cheat_light : NULL;
        if (!cheat) return;
        *cheat = !*cheat;
        advanced_draw_automap();
    } else if (type == INPUT_LEFT_RELEASED) {
        advanced_automap_drag(x, y);
        drag.active = 0;
    } else if (type & INPUT_BUTTON_LEFT && zone_get_rect(ZONE_VIEWPORT, r) && x >= r[RECT_X] && x < r[RECT_X] + r[RECT_W]
               && y >= r[RECT_Y] && y < r[RECT_Y] + r[RECT_H]) {
        drag.active = 1;
        drag.x = x;
        drag.y = y;
    }
}
