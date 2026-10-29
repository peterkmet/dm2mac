/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The start menu, new games, the load and save dialogs, and the end of the game. */
#include "session.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "action.h"
#include "action_area.h"
#include "champion.h"
#include "creature.h"
#include "dialog.h"
#include "draw.h"
#include "dungeon.h"
#include "game.h"
#include "game_text.h"
#include "ids.h"
#include "input.h"
#include "le.h"
#include "money.h"
#include "move.h"
#include "play.h"
#include "save.h"
#include "seen.h"
#include "sensor.h"
#include "sound.h"
#include "sound_request.h"
#include "text.h"
#include "timeline.h"
#include "ui.h"
#include "view.h"
#include "weather.h"
#include "zone.h"

/* The waits of the start menu picture and of the end of the game, and the time of a quit without a question. */
#define MENU_PICTURE_VBLANKS 1800
#define END_SOUND_VBLANKS    240
#define QUIT_SAFE_TICKS      100 /* a quit within this time after a load or a save needs no question */

/* The characters of a slot name. */
#define SAVE_NAME_MAX 31

/* The buttons of the save menu (DIALOG_SAVE_MENU) and of the question before a quit (DIALOG_NOT_SAVED). */
#define SAVE_MENU_SAVE    0
#define SAVE_MENU_QUIT    1
#define SAVE_MENU_RESTART 2
#define SAVE_MENU_RESUME  3
#define NOT_SAVED_CANCEL  3

/* The choices of the slot dialogs: the list of INPUT_MODE_DIALOG with 5 or 6 buttons. */
#define SLOT_CANCEL       1
#define SLOT_OK           2
#define SLOT_LIST         3
#define SLOT_NAME         4
#define SLOT_BUTTONS_LOAD 5
#define SLOT_BUTTONS_SAVE 6

/* The slot of a click below the slot list and of a save dialog without an empty slot (SAVE_SLOT_NONE is a cancel). */
#define SLOT_OUTSIDE SAVE_SLOT_COUNT

/* The layout of a slot dialog. */
#define SLOT_NAME_TOP   4 /* the text base line below the top of a slot zone */
#define SLOT_TEXT_COUNT 2 /* the text lines above the slot list */

/* The sound of the end of the game: the scream of the first champion (fallback: the generic scream). */
#define END_SOUND_PRIORITY 0xff

/* The name and title fields of the inventory while a name changes. */
#define CHAMP_NAME_MAX  7
#define CHAMP_TITLE_MAX 15

/* The name editor of the inventory: the champion and its name and title while the player types. */
typedef struct {
    int16_t champion_index;
    char name[CHAMPION_NAME_SIZE];
    char title[CHAMPION_TITLE_SIZE];
    bool in_name; /* the cursor is in the name field, else in the title field */
} NameEditor;

/* The characters of a champion name. */
static const char name_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ,.;: -()'?!";

/* The headers of the save slots in the slot dialogs. */
static uint8_t slots[SAVE_SLOT_COUNT][SAVE_HEADER_SIZE + 1];

/* The path of slot in the save folder; bak: the backup file. */
static char *slot_path(char *path, int16_t slot, bool bak)
{
    snprintf(path, OS_PATH_SIZE, "%s/DATA/SKSAVE%d.%s", g_session.data_dir, slot, bak ? "BAK" : "DAT");
    return path;
}

/* Draws a GDAT picture placed in the screen zone, with its colors. */
static void render_screen_picture(int category, int index, int entry)
{
    const Bitmap *image = draw_gdat_image(category, index, entry);
    int16_t r[RECT_SIZE];
    ScreenSize size = {0, 0};
    if (image && zone_compute_rect(image, r, ZONE_SCREEN, &size))
        ui_blit_bitmap(&(BlitSource){.bitmap = image,
                                     .x = size.w,
                                     .y = size.h,
                                     .transparent = GFX_OPAQUE,
                                     .colormap = draw_gdat_image_palette(category, index, entry)},
                       &g_draw_screen, r);
}

/* ---- Start menu ---- */

void menu_draw_title(void)
{
    render_screen_picture(CAT_MENU, 0, GFX_MENU_TITLE);
}

void menu_show_picture_and_wait(void)
{
    render_screen_picture(CAT_MENU, 0, GFX_MENU_PICTURE);
    input_set_mode(INPUT_MODE_PICTURE);
    for (int n = MENU_PICTURE_VBLANKS; n && !g_main_loop.quit; n--) {
        command_process_queue();
        if (g_input_match.value == CMD_PICTURE_CLICK)
            break;
        play_wait_frames(1);
    }
    game_set_new_game(MENU_REDRAW);
    input_set_mode(INPUT_MODE_MENU);
}

/* ---- Slot dialogs ---- */

static const char *slot_name(int16_t slot)
{
    return (const char *)slots[slot] + SAVE_HEADER_NAME;
}

static bool slot_unused(int16_t slot)
{
    return le32(slots[slot] + SAVE_HEADER_EMPTY_MARK) == SAVE_EMPTY_SLOT_MARK;
}

void session_draw_slot_selection(int16_t chosen)
{
    int16_t r[RECT_SIZE];
    if (!zone_get_rect(ZONE_SLOT_LIST, r))
        return;
    int16_t y = r[RECT_Y] + SLOT_NAME_TOP;
    for (int16_t i = 0; i < SAVE_SLOT_COUNT; i++, y += TEXT_LINE_HEIGHT)
        text_draw_in_viewport(r[RECT_X], y, g_ui_colors[i == chosen ? UI_WHITE : UI_YELLOW], slot_name(i));
}

void session_draw_name_entry(const char *name, bool cursor)
{
    int16_t r[RECT_SIZE];
    if (!zone_get_rect(ZONE_SLOT_NAME, r))
        return;
    ui_blit_bitmap(&(BlitSource){.bitmap = draw_gdat_image(CAT_DIALOG, DIALOG_SAVE_SLOTS, 0),
                                 .x = r[RECT_X],
                                 .y = r[RECT_Y],
                                 .transparent = GFX_OPAQUE,
                                 .colormap = draw_gdat_image_palette(CAT_DIALOG, DIALOG_SAVE_SLOTS, 0)},
                   &g_view_viewport, r);
    text_draw_in_viewport(r[RECT_X], r[RECT_Y] + SLOT_NAME_TOP, g_ui_colors[UI_WHITE], name);
    if (cursor) {
        r[RECT_X] = (int16_t)(r[RECT_X] + (int)strlen(name) * FONT_ADVANCE);
        r[RECT_W] = FONT_ADVANCE;
        r[RECT_H] = (int16_t)(r[RECT_H] - SLOT_NAME_TOP);
        gfx_fill_rect(g_view_viewport.pixels, r, g_ui_colors[UI_YELLOW], g_view_viewport.width, IMAGE_8BPP);
    }
    dialog_present();
}

/* Reads the header of each slot; an empty slot gets the name "n UNUSED". */
static void list_slots(void)
{
    char path[OS_PATH_SIZE];
    for (int i = 0; i < SAVE_SLOT_COUNT; i++) {
        memset(slots[i], 0, sizeof slots[i]);
        FILE *f = fopen(slot_path(path, i, false), "rb");
        if (f) {
            size_t n = fread(slots[i], 1, SAVE_HEADER_SIZE, f);
            (void)n;                                             /* a short file keeps its zeros after what it has */
            slots[i][SAVE_HEADER_NAME + SAVE_NAME_SIZE - 1] = 0; /* a damaged name ends at its field */
            fclose(f);
            continue;
        }
        snprintf((char *)slots[i] + SAVE_HEADER_NAME, SAVE_HEADER_SIZE - SAVE_HEADER_NAME, "%d UNUSED", i);
        put_le32(slots[i] + SAVE_HEADER_EMPTY_MARK, SAVE_EMPTY_SLOT_MARK);
    }
}

void session_open_slot_dialog(int id, int buttons)
{
    char text[SLOT_TEXT_COUNT][TEXT_SIZE];
    gtext_gdat(CAT_DIALOG, id, 0, text[0]);
    gtext_gdat(CAT_DIALOG, id, 1, text[1]);
    int16_t r[RECT_SIZE];
    if (zone_get_rect(ZONE_DIALOG_BACK, r))
        gfx_blit_to_bitmap(draw_gdat_image(CAT_DIALOG, id, 0), &g_view_viewport, r, GFX_OPAQUE,
                           draw_gdat_image_palette(CAT_DIALOG, id, 0));
    dialog_screen_mode(true);
    text_draw_in_zone(ZONE_DIALOG_VERSION, g_ui_colors[UI_DARKEST_GRAY], DIALOG_VERSION_TEXT);
    text_draw_in_zone(ZONE_BUTTON_TEXT_BOTTOM_LEFT, g_ui_colors[UI_YELLOW], text[0]);
    text_draw_in_zone(ZONE_BUTTON_TEXT_BOTTOM_RIGHT, g_ui_colors[UI_YELLOW], text[1]);
    list_slots();
    g_dialog.button_count = buttons;
    input_push_mode(INPUT_MODE_DIALOG);
}

void session_close_slot_dialog(void)
{
    g_dialog.right_choice = 0;
    input_pop_mode();
    game_set_screen_refresh(true);
}

/* The slot of a click in the slot list. */
static int16_t clicked_slot(void)
{
    int16_t r[RECT_SIZE] = {0};
    zone_get_rect(g_input_match.zone, r);
    ScreenPoint origin = zone_get_position(g_input_match.origin);
    int16_t row_y = g_input_match.y - r[RECT_Y] - origin.y;
    int16_t slot = row_y / TEXT_LINE_HEIGHT;
    return slot < SAVE_SLOT_COUNT ? slot : SLOT_OUTSIDE;
}

/* The load dialog: the slot that the player loads, SAVE_SLOT_NONE when cancelled. A restore after the end of the
   game loads the last slot without a dialog. */
static int16_t choose_load_slot(void)
{
    if (g_session.loaded)
        return g_session.slot;
    session_open_slot_dialog(DIALOG_LOAD_SLOTS, SLOT_BUTTONS_LOAD);
    session_draw_slot_selection(SAVE_SLOT_NONE);
    dialog_present();
    dialog_screen_mode(false);
    int16_t chosen = SAVE_SLOT_NONE;
    bool done = false;
    g_input_match.choice = DIALOG_NO_CHOICE;
    while (!done && !g_main_loop.quit) {
        command_process_queue();
        if (g_input_match.choice == SLOT_CANCEL) {
            chosen = SAVE_SLOT_NONE;
            done = true;
        } else if (g_input_match.choice == SLOT_OK) {
            done = true;
        } else if (g_input_match.choice == SLOT_LIST) {
            chosen = clicked_slot();
            if (chosen < SAVE_SLOT_COUNT && slot_unused(chosen))
                chosen = SAVE_SLOT_NONE;
            session_draw_slot_selection(chosen);
            dialog_present();
        }
        g_input_match.choice = 0; /* 0: no button; the next pass waits for a new click */
        if (!done)
            play_wait_frames(1);
    }
    session_close_slot_dialog();
    return chosen;
}

/* The default name of slot: "GAME n". */
static void make_slot_name(char *dst, int16_t slot)
{
    snprintf(dst, SAVE_NAME_SIZE, "GAME %d", slot);
}

/* A key of the name field of the save dialog: delete, end, or a character. Returns false when the key ends the
   edit. */
static bool type_slot_name_key(char *name, int *length)
{
    uint16_t key = g_input_match.key;
    bool editing = key != KEY_ENTER;
    if (key == KEY_BACKSPACE) {
        if (*length > 0)
            name[--*length] = '\0';
    } else if (editing && *length < SAVE_NAME_MAX) {
        char c = kbd_key_to_ascii(key);
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        name[(*length)++] = c;
        name[*length] = '\0';
    }
    session_draw_name_entry(name, editing);
    return editing;
}

/* The slot that the save dialog lights first, with its name in name: the slot of the last load or save, else the
   first empty slot (SLOT_OUTSIDE: none). */
static int16_t first_save_slot(char *name)
{
    int16_t chosen = g_session.slot;
    if (chosen < 0) {
        for (chosen = 0; chosen < SAVE_SLOT_COUNT && !slot_unused(chosen); chosen++) {
        }
        if (chosen < SAVE_SLOT_COUNT)
            make_slot_name(name, chosen);
    } else {
        snprintf(name, SAVE_NAME_SIZE, "%s", slot_name(chosen));
    }
    return chosen;
}

/* The save dialog: the slot that the player saves into (its name in g_session.save_name), SAVE_SLOT_NONE when
   cancelled. */
static int16_t choose_save_slot(void)
{
    char *name = g_session.save_name;
    session_open_slot_dialog(DIALOG_SAVE_SLOTS, SLOT_BUTTONS_SAVE);
    g_dialog.right_choice = SLOT_CANCEL;
    int16_t chosen = first_save_slot(name);
    int length = 0;
    bool editing = false; /* the player types the slot name */
    bool done = false;
    session_draw_slot_selection(chosen);
    if (chosen == SLOT_OUTSIDE)
        dialog_present();
    else
        session_draw_name_entry(name, false);
    while (!done && !g_main_loop.quit) {
        command_process_queue();
        switch (g_input_match.choice) {
        case SLOT_CANCEL:
            chosen = SAVE_SLOT_NONE;
            editing = false;
            done = true;
            break;
        case SLOT_OK:
            editing = false;
            if (chosen == SLOT_OUTSIDE)
                break;
            if (!*name)
                make_slot_name(name, chosen);
            session_draw_name_entry(name, false);
            done = true;
            break;
        case SLOT_LIST:
            editing = false;
            chosen = clicked_slot();
            if (chosen < SAVE_SLOT_COUNT && slot_unused(chosen))
                make_slot_name(name, chosen);
            else
                snprintf(name, SAVE_NAME_SIZE, "%s",
                         slot_name(chosen < SAVE_SLOT_COUNT ? chosen : SAVE_SLOT_COUNT - 1));
            session_draw_slot_selection(chosen);
            session_draw_name_entry(name, false);
            break;
        case SLOT_NAME:
            if (chosen != SLOT_OUTSIDE && !editing) {
                editing = true;
                session_draw_name_entry(name, true);
                length = (int)strlen(name);
            }
            break;
        }
        if (editing && g_input_match.value >= CMD_TEXT_FIRST && g_input_match.value <= CMD_SLOT_TEXT_LAST)
            editing = type_slot_name_key(name, &length);
        g_input_match.choice = 0;
        g_input_match.value = 0;
        if (!done)
            play_wait_frames(1);
    }
    session_close_slot_dialog();
    return chosen;
}

/* ---- Load and save ---- */

/* The path of the file name in the data folder. */
static char *data_path(char *path, const char *name)
{
    snprintf(path, OS_PATH_SIZE, "%s/DATA/%s", g_session.data_dir, name);
    return path;
}

/* Whether the file at path can be read. */
static bool file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f)
        fclose(f);
    return f != NULL;
}

/* Reads DUNGEON.DAT (DUNGENB.DAT when chosen and present) for a new game; returns false on a read error. */
static bool load_new_game(void)
{
    char path[OS_PATH_SIZE];
    if (!g_session.use_dungenb || !file_exists(data_path(path, "DUNGENB.DAT")))
        data_path(path, "DUNGEON.DAT");
    save_clear_party();
    champ_reset_step_state(); /* no step state of the last game, as at a load */
    bool first = !g_weather.dungeon_loaded;
    if (!dungeon_load(path))
        return false;
    seen_reset();
    game_set_cheats(0);
    uint16_t start = dungeon_party_start();
    /* the party start keeps the direction in the map bits */
    save_set_party_position((Position){0, location_x(start), location_y(start)}, location_dir(start));
    timeline_init(EVENT_SLACK + dungeon_thing_count(THING_GROUP) + dungeon_thing_count(THING_PROJECTILE) +
                  dungeon_thing_count(THING_EXPLOSION));
    weather_load(gdat_current());
    dungeon_link_things_after_load();
    if (first)
        dungeon_read_level_table();
    return true;
}

/* Reads the save file at path; the first load of a run also reads the level table. Returns false on an error. */
static bool read_save(const char *path)
{
    bool first = !g_weather.dungeon_loaded;
    if (save_load(path, gdat_current()) != SAVE_OK)
        return false;
    if (first)
        dungeon_read_level_table();
    return true;
}

/* Loads the slot file at path into the game; returns false on an error. */
static bool load_slot(const char *path)
{
    save_clear_party();
    if (!read_save(path))
        return false;
    game_note_slot_loaded();
    return true;
}

/* The arrival of the party on its map, with the whole view to draw. */
static void arrive_on_party_map(void)
{
    move_compute_map_arrival((Position){g_game.party_map, g_game.party_x, g_game.party_y});
    g_party.redraw = PARTY_REDRAW_ALL;
}

bool session_load(void)
{
    char path[OS_PATH_SIZE], bak[OS_PATH_SIZE], ftl[OS_PATH_SIZE];
    bool from_bak = false;
    if (!g_session.new_game) {
        int16_t slot = choose_load_slot();
        if (slot < 0) {
            game_set_save_slot(0); /* as in the original: a cancelled load leaves slot 0 */
            return false;
        }
        game_set_save_slot(slot);
        slot_path(path, slot, false);
        slot_path(bak, slot, true);
        if (!file_exists(path) && file_exists(bak)) {
            snprintf(path, sizeof path, "%s", bak);
            from_bak = true;
        } else if (!file_exists(path) && !g_session.loaded && file_exists(data_path(ftl, "DUNGEON.FTL"))) {
            game_set_new_game(1);
        }
    }
    bool ok = g_session.new_game ? load_new_game() : load_slot(path);
    if (!ok) {
        /* fix: the original asks for the disk of the game when the load of the last slot fails */
        dialog_run(dialog_draw(DIALOG_ERROR, MESSAGE_GAME_DAMAGED));
        if (!g_session.loaded)
            session_quit();
        return false;
    }
    if (g_session.new_game) {
        sensor_start_timers();
    } else if (from_bak) {
        slot_path(bak, g_session.slot, false);
        rename(path, bak);
    }
    dialog_draw(DIALOG_LOADING, MESSAGE_NONE);
    g_dialog.progress_enabled = true;
    game_clear_end();
    arrive_on_party_map();
    return true;
}

/* Writes the game into slot; returns false on an error. */
static bool write_slot(int16_t slot)
{
    char path[OS_PATH_SIZE], bak[OS_PATH_SIZE];
    if (save_write(slot_path(path, slot, false), slot_path(bak, slot, true), g_session.save_name, gdat_current()) !=
        SAVE_OK)
        return false;
    game_note_slot_saved();
    return true;
}

/* The save menu until the player saves (returns the slot), quits (*quit true), or resumes (SAVE_SLOT_NONE). */
static int16_t run_save_menu(bool *quit)
{
    for (;;) {
        int choice = dialog_run_cancel(dialog_draw(DIALOG_SAVE_MENU, MESSAGE_NONE), SAVE_MENU_RESUME);
        if (g_main_loop.quit || choice == SAVE_MENU_RESUME)
            return SAVE_SLOT_NONE;
        if (choice == SAVE_MENU_QUIT || choice == SAVE_MENU_RESTART) {
            if (choice == SAVE_MENU_RESTART)
                game_set_restart(true); /* a cancel below keeps it, as in the original */
            if (g_session.load_time + QUIT_SAFE_TICKS < g_game.game_time &&
                g_session.save_time + QUIT_SAFE_TICKS < g_game.game_time &&
                dialog_run_cancel(dialog_draw(DIALOG_NOT_SAVED, MESSAGE_NONE), NOT_SAVED_CANCEL) == NOT_SAVED_CANCEL)
                continue;
            game_set_offer_restore(false);
            *quit = true;
            return SAVE_SLOT_NONE;
        }
        int16_t slot = choose_save_slot();
        if (slot >= 0)
            return slot;
    }
}

void session_save(void)
{
    bool quit = false;
    game_set_restart(false);
    action_area_close();
    area_update(false);
    play_hide_view();
    timeline_rebuild_free_list();
    int16_t slot = run_save_menu(&quit);
    if (slot >= 0) {
        game_set_save_slot(slot);
        dialog_draw(DIALOG_SAVING, MESSAGE_NONE);
        if (!write_slot(slot))
            dialog_run(dialog_draw(DIALOG_ERROR, MESSAGE_SAVE_FAILED));
    }
    creature_activate_all_maps();
    if (quit) {
        session_end(false);
        return;
    }
    play_show_view();
}

/* The start menu: waits until the player chooses a new game or a load. */
static void run_menu(void)
{
    input_set_mode(INPUT_MODE_MENU);
    do {
        menu_draw_title();
        input_flush();
        game_set_new_game(MENU_WAITING);
        while (g_session.new_game == MENU_WAITING && !g_main_loop.quit) {
            command_process_queue();
            play_wait_frames(1);
        }
    } while (g_session.new_game == MENU_REDRAW && !g_main_loop.quit);
}

/* The sound and music volumes, the interface and the coin table of a loaded game, then its play. */
static void start_loaded_game(void)
{
    sound_set_volume_and_test(g_game.sound_volume, DEVICE_SOUND);
    sound_set_volume_and_test(g_game.music_volume, DEVICE_MUSIC);
    ui_reset();
    money_init_coin_table();
    play_start_session();
    input_flush();
}

bool session_init(void)
{
    do {
        run_menu();
        if (g_main_loop.quit)
            return false;
    } while (!session_load() && !g_main_loop.quit);
    if (g_main_loop.quit)
        return false;
    start_loaded_game();
    return true;
}

bool session_start_new_game(void)
{
    game_set_new_game(1);
    game_set_dungenb(false);
    if (!load_new_game())
        return false;
    sensor_start_timers();
    game_clear_end();
    game_set_completed(false);
    arrive_on_party_map();
    start_loaded_game();
    return true;
}

bool session_start_save(const char *path)
{
    if (!read_save(path))
        return false;
    ui_reset();
    money_init_coin_table();
    ui_fill_zone_screen(ZONE_SCREEN, 0); /* play_start_session runs champ_init_party_state */
    arrive_on_party_map();
    play_start_session();
    return true;
}

/* ---- End of the game ---- */

void session_quit(void)
{
    if (g_session.completed)
        ui_fill_zone_screen(ZONE_SCREEN, 0);
    game_set_exit_code(g_session.completed   ? EXIT_COMPLETED
                       : g_session.quit_code ? EXIT_PARTY_DEAD
                       : g_session.restart   ? EXIT_RESTART
                                             : EXIT_QUIT);
    game_request_quit();
}

void session_complete(void)
{
    game_set_completed(true);
    if (g_game_mode != GAME_MODE_TURNBASED)
        session_quit(); /* the turn-based mode stays for the next load or new game */
}

void session_end(bool party_dead)
{
    if (party_dead && !g_session.completed) {
        int portrait = g_game.champion_count ? champion(0)->portrait : SOUND_FALLBACK;
        sndreq_request_with_fallback(
            (SoundCue){CAT_CHAMPION, portrait, SOUND_ENTRY_SCREAM, END_SOUND_PRIORITY, SOUND_VOLUME_FULL},
            SOUND_FALLBACK, g_game.party_x, g_game.party_y, SOUND_NOW);
        play_wait_frames(END_SOUND_VBLANKS);
    }
    g_dialog.screen_mode = 1;
    if (party_dead) {
        ui_fill_zone_screen(ZONE_SCREEN, 0);
        if (g_session.offer_restore) {
            game_set_session_loaded(dialog_run(dialog_draw(DIALOG_THE_END, MESSAGE_NONE)) == 0);
            if (g_session.loaded && session_load()) {
                play_start_session();
                game_set_session_loaded(false);
                input_flush();
                return;
            }
        }
    }
    ui_fill_zone_screen(ZONE_SCREEN, 0);
    if (g_session.restart || party_dead || g_main_loop.quit) {
        if (party_dead)
            game_set_quit_code(1);
        session_quit();
        return;
    }
    render_screen_picture(CAT_END, 0, GFX_END_PICTURE);
    input_set_mode(INPUT_MODE_PICTURE);
    do {
        command_process_queue();
        play_wait_frames(1);
    } while (g_input_match.value != CMD_PICTURE_CLICK && !g_main_loop.quit);
    session_quit();
}

/* ---- Champion names ---- */

/* A key of a champion name field other than Enter and Tab: deletes the last character, or adds a character of the
   name alphabet while the text is shorter than max. Returns true when the field needs a redraw. */
static bool type_name_key(char *text, size_t max)
{
    uint16_t key = g_input_match.key;
    size_t length = strlen(text);
    if (key == KEY_BACKSPACE) {
        if (length > 0)
            text[length - 1] = '\0';
        return true;
    }
    if (length >= max)
        return true;
    char ch = kbd_key_to_ascii(key);
    if (ch >= 'a' && ch <= 'z')
        ch = (char)(ch - 'a' + 'A');
    if (!ch || !strchr(name_alphabet, ch) || (ch == ' ' && !length))
        return false;
    text[length] = ch;
    text[length + 1] = '\0';
    return true;
}

/* The field that the cursor is in. */
static char *edited_text(NameEditor *e)
{
    return e->in_name ? e->name : e->title;
}

/* Writes the fields into the champion: the editor changes the champion with each key, as in the original. */
static void store_name_fields(const NameEditor *e)
{
    champ_set_name(e->champion_index, e->name, e->title);
}

/* Removes the spaces at the end of the edited field, one by one as in the original. */
static void trim_spaces(NameEditor *e)
{
    char *text = edited_text(e);
    /* fix: the original reads the byte before an empty text */
    for (size_t n = strlen(text); n > 0 && text[n - 1] == ' '; n--) {
        text[n - 1] = '\0';
        store_name_fields(e);
    }
}

/* The name and title fields of the champion, with the cursor after the edited field. */
static void render_name_fields(const NameEditor *e)
{
    ScreenPoint at = zone_get_position(e->in_name ? ZONE_NAME : ZONE_TITLE);
    int16_t x = (int16_t)(at.x + (int)strlen(e->in_name ? e->name : e->title) * FONT_ADVANCE);
    int16_t y = at.y;
    ui_draw_viewport_image(CAT_PANEL, 0, GFX_NAME_BUTTON_DOWN, ZONE_NAME_BUTTON, GFX_OPAQUE);
    text_draw_in_zone(ZONE_NAME, g_ui_colors[UI_YELLOW], e->name);
    text_draw_in_zone(ZONE_TITLE, g_ui_colors[UI_YELLOW], e->title);
    text_draw_in_viewport(x, y, g_ui_colors[UI_GOLD], "_");
    play_present_viewport();
}

/* Ends the name edit: the close button, the champion state with the new name, and the view again. */
static void close_name_editor(int16_t champion_index)
{
    ui_draw_viewport_image(CAT_PANEL, 0, GFX_PANEL_BUTTON_CLOSE, ZONE_NAME_BUTTON, GFX_OPAQUE);
    champion(champion_index)->redraw |= REDRAW_NAME;
    ui_draw_champion_state(g_ui.inventory - 1);
    play_show_view();
    input_pop_mode();
}

/* One command of the name editor; returns true when the edit ends. *redraw is set when the fields need a redraw. */
static bool run_name_command(NameEditor *e, const NameEditor *before, bool *redraw)
{
    uint16_t command = g_input_match.value;
    if (command == CMD_EDIT_NAME && !e->in_name) {
        e->in_name = true;
        *redraw = true;
    } else if (command == CMD_EDIT_TITLE && e->in_name) {
        e->in_name = false;
        *redraw = true;
    } else if (command == CMD_EDIT_CANCEL) {
        store_name_fields(before);
        return true;
    } else if (!command) {
        play_wait_frames(1);
    } else if (g_input_match.key == KEY_ENTER || g_input_match.key == KEY_TAB) {
        trim_spaces(e);
        if (!e->in_name && g_input_match.key == KEY_ENTER)
            return true;
        e->in_name = !e->in_name;
        *redraw = true;
    } else if (type_name_key(edited_text(e), e->in_name ? CHAMP_NAME_MAX : CHAMP_TITLE_MAX)) {
        store_name_fields(e);
        *redraw = true;
    }
    return false;
}

void session_edit_champion_name(int16_t champion_index)
{
    const Champion *c = champion(champion_index);
    NameEditor e = {.champion_index = champion_index, .in_name = true};
    memcpy(e.name, c->name, sizeof e.name);
    memcpy(e.title, c->title, sizeof e.title);
    const NameEditor before = e;
    game_set_panel_mode(PANEL_MODE_NAME);
    input_push_mode(INPUT_MODE_NAME);
    play_hide_view();
    bool done = false;
    bool redraw = true;
    while (!done && !g_main_loop.quit) {
        if (redraw)
            render_name_fields(&e);
        redraw = false;
        g_input_match.choice = 0;
        g_input_match.value = 0;
        command_process_queue();
        done = run_name_command(&e, &before, &redraw);
    }
    close_name_editor(champion_index);
}

/* ---- Arguments ---- */

/* Whether arg is one of the option_count value_options. */
static bool is_value_option(const char *arg, const char *const *value_options, int option_count)
{
    for (int n = 0; n < option_count; n++)
        if (!strcmp(arg, value_options[n]))
            return true;
    return false;
}

int args_drop_save(int argc, char **argv, const char *const *value_options, int option_count)
{
    int out = 1;
    for (int i = 1; i < argc; i++) {
        if (is_value_option(argv[i], value_options, option_count) && i + 1 < argc) {
            if (strcmp(argv[i], "--save")) {
                argv[out++] = argv[i];
                argv[out++] = argv[i + 1];
            }
            i++;
        } else
            argv[out++] = argv[i];
    }
    argv[out] = NULL;
    return out;
}
