/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The start menu, new games, the load and save dialogs, and the end of the game. */
#include "session.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "action.h"
#include "action_area.h"
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

/* The header byte where the slot list marks an empty slot with UNUSED_SLOT. */
#define SAVE_HEADER_UNUSED 0x26

/* The mark of an empty slot in the slot list. */
#define UNUSED_SLOT 0xdeadbeefu

/* The characters of a slot name. */
#define SAVE_NAME_MAX 31

/* The last command of a text key: the commands CMD_TEXT_FIRST to it type a character. */
#define CMD_TEXT_KEY_LAST 0xd6

/* The buttons of the save menu (dialog 0x1b) and of the question before a quit (dialog 0x0c). */
#define SAVE_MENU_SAVE    0
#define SAVE_MENU_QUIT    1
#define SAVE_MENU_RESTART 2
#define SAVE_MENU_RESUME  3
#define NOT_SAVED_CANCEL  3

/* The choices of the slot dialogs: the list of input mode 4 with 5 or 6 buttons. */
#define SLOT_CANCEL       1
#define SLOT_OK           2
#define SLOT_LIST         3
#define SLOT_NAME         4
#define SLOT_BUTTONS_LOAD 5
#define SLOT_BUTTONS_SAVE 6

/* The layout of a slot dialog. */
#define SLOT_NAME_TOP   4 /* the text base line below the top of a slot zone */
#define SLOT_TEXT_COUNT 2 /* the text lines above the slot list */

/* The sound of the end of the game: the scream of the first champion (fallback: the generic scream). */
#define END_SOUND_PRIORITY 0xff

/* The name and title fields of the inventory while a name changes. */
#define CHAMP_NAME_MAX  7
#define CHAMP_TITLE_MAX 15

/* The characters of a champion name. */
static const char name_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ,.;: -()'?!";

/* The headers of the save slots in the slot dialogs. */
static uint8_t slots[SAVE_SLOT_COUNT][SAVE_HEADER_SIZE + 1];
static bool editing; /* the player types the slot name or a champion name */

/* The path of slot in the save folder; bak: the backup file. */
static char *slot_path(char *path, int slot, bool bak)
{
    snprintf(path, OS_PATH_SIZE, "%s/DATA/SKSAVE%d.%s", g_session.data_dir, slot, bak ? "BAK" : "DAT");
    return path;
}

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

/* Draws a GDAT picture placed in the screen zone, with its colors. */
static void render_screen_picture(int category, int index, int entry)
{
    const Bitmap *image = draw_gdat_image(category, index, entry);
    int16_t r[RECT_SIZE];
    int16_t x = 0;
    int16_t y = 0;
    if (image && zone_compute_rect(image, r, ZONE_SCREEN, &x, &y))
        ui_blit_bitmap(&(BlitSource){.bitmap = image,
                                     .x = x,
                                     .y = y,
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

static const char *slot_name(int slot)
{
    return (const char *)slots[slot] + SAVE_HEADER_NAME;
}

static bool slot_unused(int slot)
{
    return le32(slots[slot] + SAVE_HEADER_UNUSED) == UNUSED_SLOT;
}

void session_draw_slot_selection(int chosen)
{
    int16_t r[RECT_SIZE];
    if (!zone_get_rect(ZONE_SLOT_LIST, r))
        return;
    int y = r[RECT_Y] + SLOT_NAME_TOP;
    for (int i = 0; i < SAVE_SLOT_COUNT; i++, y += TEXT_LINE_HEIGHT)
        text_draw_in_viewport(r[RECT_X], y, g_ui_colors[i == chosen ? UI_WHITE : UI_YELLOW], slot_name(i));
}

void session_draw_name_entry(const char *name, int cursor)
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
            if (fread(slots[i], 1, SAVE_HEADER_SIZE, f) != SAVE_HEADER_SIZE) { /* a short file keeps what it has */
            }
            fclose(f);
            continue;
        }
        snprintf((char *)slots[i] + SAVE_HEADER_NAME, SAVE_HEADER_SIZE - SAVE_HEADER_NAME, "%d UNUSED", i);
        put_le32(slots[i] + SAVE_HEADER_UNUSED, UNUSED_SLOT);
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
    text_draw_in_zone(ZONE_SLOT_TEXT_1, g_ui_colors[UI_YELLOW], text[0]);
    text_draw_in_zone(ZONE_SLOT_TEXT_2, g_ui_colors[UI_YELLOW], text[1]);
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
static int clicked_slot(void)
{
    int16_t r[RECT_SIZE] = {0};
    int16_t x = 0;
    int16_t y = 0;
    zone_get_rect(g_input_match.zone, r);
    zone_get_position(g_input_match.origin, &x, &y);
    int slot = (int16_t)(g_input_match.y - (r[RECT_Y] + y)) / TEXT_LINE_HEIGHT;
    return slot < SAVE_SLOT_COUNT ? slot : SAVE_SLOT_COUNT;
}

/* The load dialog: the slot that the player loads, SAVE_SLOT_NONE when cancelled. A restore after the end of the
   game loads the last slot without a dialog. */
static int choose_load_slot(void)
{
    if (g_session.loaded)
        return g_session.slot;
    session_open_slot_dialog(DIALOG_LOAD_SLOTS, SLOT_BUTTONS_LOAD);
    session_draw_slot_selection(SAVE_SLOT_NONE);
    dialog_present();
    dialog_screen_mode(false);
    int chosen = SAVE_SLOT_NONE;
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
        g_input_match.choice = -1;
        if (!done)
            play_wait_frames(1);
    }
    session_close_slot_dialog();
    return chosen;
}

/* The default name of slot: "GAME n". */
static void make_slot_name(char *dst, int slot)
{
    snprintf(dst, SAVE_NAME_SIZE, "GAME %d", slot);
}

/* A key of the name field of the save dialog: delete, end, or a character. */
static void type_slot_name_key(char *name, int *length)
{
    uint16_t key = g_input_match.key;
    if (key == KEY_BACKSPACE) {
        if (*length > 0)
            name[--*length] = '\0';
    } else if (key == KEY_ENTER) {
        editing = false;
    } else if (*length < SAVE_NAME_MAX) {
        char c = kbd_key_to_ascii(key);
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        name[(*length)++] = c;
        name[*length] = '\0';
    }
    session_draw_name_entry(name, editing);
}

/* The slot that the save dialog lights first, with its name in name: the slot of the last load or save, else the
   first empty slot (SAVE_SLOT_COUNT: none). */
static int first_save_slot(char *name)
{
    int chosen = g_session.slot;
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
static int choose_save_slot(void)
{
    char *name = g_session.save_name;
    session_open_slot_dialog(DIALOG_SAVE_SLOTS, SLOT_BUTTONS_SAVE);
    g_dialog.right_choice = SLOT_CANCEL;
    int chosen = first_save_slot(name);
    int length = 0;
    bool done = false;
    session_draw_slot_selection(chosen);
    if (chosen == SAVE_SLOT_COUNT)
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
            if (chosen == SAVE_SLOT_COUNT)
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
            if (chosen != SAVE_SLOT_COUNT && !editing) {
                editing = true;
                session_draw_name_entry(name, true);
                length = (int)strlen(name);
            }
            break;
        }
        if (editing && g_input_match.value >= CMD_TEXT_FIRST && g_input_match.value <= CMD_TEXT_KEY_LAST)
            type_slot_name_key(name, &length);
        g_input_match.choice = 0;
        g_input_match.value = 0;
        if (!done)
            play_wait_frames(1);
    }
    session_close_slot_dialog();
    return chosen;
}

/* ---- Load and save ---- */

/* Reads DUNGEON.DAT (DUNGENB.DAT when chosen and present) for a new game; returns false on a read error. */
static bool load_new_game(void)
{
    char path[OS_PATH_SIZE];
    if (!g_session.use_dungenb || !file_exists(data_path(path, "DUNGENB.DAT")))
        data_path(path, "DUNGEON.DAT");
    save_clear_party();
    bool first = !g_weather.dungeon_loaded;
    if (!dungeon_load(path))
        return false;
    seen_reset();
    game_set_cheats(0);
    uint16_t start = dungeon_party_start();
    /* the party start keeps the direction in the map bits */
    save_set_party_position(location_x(start), location_y(start), location_dir(start), 0);
    timeline_init(EVENT_SLACK + dungeon_thing_count(THING_GROUP) + dungeon_thing_count(THING_PROJECTILE) +
                  dungeon_thing_count(THING_EXPLOSION));
    weather_load(gdat_current());
    dungeon_link_things_after_load();
    if (first)
        dungeon_read_level_table();
    return true;
}

/* Loads the slot file at path into the game; returns false on an error. */
static bool load_slot(const char *path)
{
    save_clear_party();
    bool first = !g_weather.dungeon_loaded;
    if (save_load(path, gdat_current()) != SAVE_OK)
        return false;
    if (first)
        dungeon_read_level_table();
    game_note_slot_loaded();
    return true;
}

bool session_load(void)
{
    char path[OS_PATH_SIZE], bak[OS_PATH_SIZE];
    bool from_bak = false;
    if (!g_session.new_game) {
        int slot = choose_load_slot();
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
        } else if (!file_exists(path) && !g_session.loaded && file_exists(data_path(bak, "DUNGEON.FTL"))) {
            game_set_new_game(1);
        }
    }
    bool ok = g_session.new_game ? load_new_game() : load_slot(path);
    if (!ok) {
        /* fix: the original asks for the disk of the game (dialog 7) when the load of the last slot fails */
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
    g_dialog.progress_enabled = 1;
    game_clear_end();
    move_compute_map_arrival((Position){g_game.party_map, g_game.party_x, g_game.party_y});
    g_party.redraw = REDRAW_ALL_VIEW;
    return true;
}

/* Writes the game into slot; returns false on an error. */
static bool write_slot(int slot)
{
    char path[OS_PATH_SIZE], bak[OS_PATH_SIZE];
    if (save_write(slot_path(path, slot, false), slot_path(bak, slot, true), g_session.save_name, gdat_current()) !=
        SAVE_OK)
        return false;
    game_note_slot_saved();
    return true;
}

/* The save menu until the player saves (returns the slot), quits (*quit true), or resumes (SAVE_SLOT_NONE). */
static int run_save_menu(bool *quit)
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
        int slot = choose_save_slot();
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
    int slot = run_save_menu(&quit);
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

bool session_init(void)
{
    do {
        run_menu();
        if (g_main_loop.quit)
            return false;
    } while (!session_load() && !g_main_loop.quit);
    if (g_main_loop.quit)
        return false;
    sound_set_volume_and_test(g_game.sound_volume, DEVICE_SOUND);
    sound_set_volume_and_test(g_game.music_volume, DEVICE_MUSIC);
    ui_reset();
    money_init_coin_table();
    play_start_session();
    input_flush();
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
    move_compute_map_arrival((Position){g_game.party_map, g_game.party_x, g_game.party_y});
    g_party.redraw = REDRAW_ALL_VIEW;
    sound_set_volume_and_test(g_game.sound_volume, DEVICE_SOUND);
    sound_set_volume_and_test(g_game.music_volume, DEVICE_MUSIC);
    ui_reset();
    money_init_coin_table();
    play_start_session();
    input_flush();
    return true;
}

bool session_start_save(const char *path)
{
    bool first = !g_weather.dungeon_loaded;
    if (save_load(path, gdat_current()) != SAVE_OK)
        return false;
    if (first)
        dungeon_read_level_table();
    ui_reset();
    money_init_coin_table();
    ui_fill_zone_screen(ZONE_SCREEN, 0); /* play_start_session runs champ_init_party_state */
    move_compute_map_arrival((Position){g_game.party_map, g_game.party_x, g_game.party_y});
    g_party.redraw = REDRAW_ALL_VIEW;
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
   name alphabet while the text is shorter than max; *x is the cursor. Returns true when the field needs a redraw. */
static bool type_name_key(char *text, int *length, int max, int *x)
{
    uint16_t key = g_input_match.key;
    if (key == KEY_BACKSPACE) {
        if (*length > 0) {
            text[--*length] = '\0';
            *x -= FONT_ADVANCE;
        }
        return true;
    }
    if (*length >= max)
        return true;
    char ch = kbd_key_to_ascii(key);
    if (ch >= 'a' && ch <= 'z')
        ch = (char)(ch - 'a' + 'A');
    if (!ch || !strchr(name_alphabet, ch) || (ch == ' ' && !*length))
        return false;
    text[(*length)++] = ch;
    text[*length] = '\0';
    *x += FONT_ADVANCE;
    return true;
}

/* Removes the spaces at the end of text. */
static void trim_spaces(char *text)
{
    /* fix: the original reads the byte before an empty text */
    for (size_t n = strlen(text); n > 0 && text[n - 1] == ' '; n--)
        text[n - 1] = '\0';
}

/* The name and title fields of the champion, with the cursor at (x, y). */
static void render_name_fields(const Champion *c, int x, int y)
{
    ui_draw_viewport_image(CAT_PANEL, 0, GFX_NAME_BUTTON_DOWN, ZONE_NAME_BUTTON, GFX_OPAQUE);
    text_draw_in_zone(ZONE_NAME, g_ui_colors[UI_YELLOW], c->name);
    text_draw_in_zone(ZONE_TITLE, g_ui_colors[UI_YELLOW], c->title);
    text_draw_in_viewport(x, y, g_ui_colors[UI_GOLD], "_");
    play_present_viewport();
}

/* Ends the name edit: the close button, the champion state with the new name, and the view again. */
static void close_name_editor(Champion *c)
{
    editing = false;
    ui_draw_viewport_image(CAT_PANEL, 0, GFX_PANEL_BUTTON_CLOSE, ZONE_NAME_BUTTON, GFX_OPAQUE);
    c->redraw |= REDRAW_NAME;
    ui_draw_champion_state(g_ui.inventory - 1);
    play_show_view();
    input_pop_mode();
}

void session_edit_champion_name(Champion *champion)
{
    char name[CHAMPION_NAME_SIZE], title[CHAMPION_TITLE_SIZE];
    memcpy(name, champion->name, sizeof name);
    memcpy(title, champion->title, sizeof title);
    int16_t name_x, name_y, title_x, title_y;
    game_set_panel_mode(PANEL_MODE_NAME);
    zone_get_position(ZONE_NAME, &name_x, &name_y);
    zone_get_position(ZONE_TITLE, &title_x, &title_y);
    input_push_mode(INPUT_MODE_NAME);
    editing = true;
    play_hide_view();
    bool in_name = true;
    bool done = false;
    while (!done && !g_main_loop.quit) {
        char *text = in_name ? champion->name : champion->title;
        int max = in_name ? CHAMP_NAME_MAX : CHAMP_TITLE_MAX;
        int length = (int)strlen(text);
        int x = (in_name ? name_x : title_x) + length * FONT_ADVANCE;
        int y = in_name ? name_y : title_y;
        bool field_done = false;
        bool redraw = true;
        while (!field_done && !g_main_loop.quit) {
            if (redraw)
                render_name_fields(champion, x, y);
            redraw = false;
            g_input_match.choice = 0;
            g_input_match.value = 0;
            command_process_queue();
            int command = g_input_match.value;
            if (command == CMD_EDIT_NAME && !in_name) {
                in_name = true;
                field_done = true;
            } else if (command == CMD_EDIT_TITLE && in_name) {
                in_name = false;
                field_done = true;
            } else if (command == CMD_EDIT_CANCEL) {
                memcpy(champion->name, name, sizeof name);
                memcpy(champion->title, title, sizeof title);
                field_done = true;
                done = true;
            } else if (!command) {
                play_wait_frames(1);
            } else if (g_input_match.key == KEY_ENTER || g_input_match.key == KEY_TAB) {
                trim_spaces(text);
                field_done = true;
                if (in_name)
                    in_name = false;
                else if (g_input_match.key == KEY_TAB)
                    in_name = true;
                else
                    done = true;
            } else {
                redraw = type_name_key(text, &length, max, &x);
            }
        }
    }
    close_name_editor(champion);
}

/* ---- Arguments ---- */

int args_drop_save(int argc, char **argv)
{
    int out = 1;
    for (int i = 1; i < argc; i++) {
        bool is_value_option = !strcmp(argv[i], "--data") || !strcmp(argv[i], "--filter") ||
                               !strcmp(argv[i], "--save") || !strcmp(argv[i], "--control") ||
                               !strcmp(argv[i], "--mode") || !strcmp(argv[i], "--saves");
        if (is_value_option && i + 1 < argc) {
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
