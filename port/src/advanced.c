/* The advanced mode: the movement letter keys and the quick save and quick load. */
#include <stdio.h>
#include "advanced.h"
#include "draw.h"
#include "save.h"
#include "session.h"
#include "text.h"
#include "timeline.h"

#define QUICK_SAVE_NAME "QUICK SAVE"    /* the name in the header of QUICK.DAT */

/* PC scan codes of the letter keys and the keypad keys that move and turn the party. */
enum { SCAN_Q = 0x10, SCAN_W = 0x11, SCAN_E = 0x12, SCAN_A = 0x1e, SCAN_S = 0x1f, SCAN_D = 0x20 };
enum {
    KEYPAD_TURN_LEFT = 0x4b, KEYPAD_FORWARD = 0x4c, KEYPAD_TURN_RIGHT = 0x4d, KEYPAD_LEFT = 0x4f, KEYPAD_BACK = 0x50,
    KEYPAD_RIGHT = 0x51
};

int game_mode = GAME_MODE_VANILLA;
const char *const game_mode_names[GAME_MODE_COUNT] = {"vanilla", "advanced"};

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

/* Writes the game to path (game_save prepares the timeline the same way); returns 0 on an error. */
static int write_game(const char *path, const char *bak)
{
    timeline_rebuild_free_list();
    return save_write(path, bak, QUICK_SAVE_NAME, draw_gdat()) == SAVE_OK;
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
    int loaded = game_start_save(path);
    if (!loaded) game_start_save(keep);
    remove(keep);
    session = kept;
    if (loaded) session.load_time = session.save_time = 0;  /* a quick load is not a saved game for the QUIT question */
    print_line(loaded ? "GAME LOADED" : "QUICK SAVE DAMAGED");
}
