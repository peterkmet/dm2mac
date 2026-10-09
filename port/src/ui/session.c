/* The start menu, new games, the load and save dialogs, and the end of the game. */
#include <stdio.h>
#include <string.h>
#include "action.h"
#include "automap.h"
#include "creature.h"
#include "dialog.h"
#include "draw.h"
#include "dungeon.h"
#include "game.h"
#include "input.h"
#include "item.h"
#include "le.h"
#include "move.h"
#include "save.h"
#include "sensor.h"
#include "session.h"
#include "sound_request.h"
#include "text.h"
#include "timeline.h"
#include "ui.h"
#include "view.h"
#include "weather.h"
#include "zone.h"
#include "money.h"
#include "sound.h"

#define MENU_PICTURE 1                  /* GDAT (5, 0, 1, 1): the picture of the menu command 0xda */
#define MENU_TITLE 4                    /* GDAT (5, 0, 1, 4): the title picture */
#define END_PICTURE 1                   /* GDAT (6, 0, 1, 1): the picture after the player quits */
#define MENU_PICTURE_VBLANKS 1800
#define END_SOUND_VBLANKS 240
#define SAVE_HEADER_UNUSED 0x26         /* the slot list marks an empty slot with UNUSED_SLOT here */
#define UNUSED_SLOT 0xdeadbeefu
#define SAVE_NAME_MAX 31                /* characters of a slot name */
#define QUIT_SAFE_TICKS 100             /* a quit within this time after a load or a save needs no question */
#define CMD_TEXT_KEY_LAST 0xd6
/* The buttons of the save menu (dialog 0x1b) and of the question before a quit (dialog 0x0c). */
enum { SAVE_MENU_SAVE, SAVE_MENU_QUIT, SAVE_MENU_RESTART, SAVE_MENU_RESUME };
enum { NOT_SAVED_CANCEL = 3 };
/* The choices of the slot dialogs: the list of input mode 4 with 5 or 6 buttons. */
enum { SLOT_CANCEL = 1, SLOT_OK, SLOT_LIST, SLOT_NAME };
enum { SLOT_BUTTONS_LOAD = 5, SLOT_BUTTONS_SAVE = 6 };
enum {
    ZONE_SLOT_LIST = 0x1c3, ZONE_SLOT_NAME = 0x1c5, ZONE_SLOT_TEXT_1 = 0x1d2,
    ZONE_SLOT_TEXT_2 = 0x1d3
};
#define SLOT_NAME_TOP 4                 /* the text base line below the top of a slot zone */
/* The sound of the end of the game: the scream of the first champion (fallback: the generic scream). */
enum { END_SOUND_PRIORITY = 0xff };
/* The name and title fields of the inventory while a name changes. */
enum {
    NAME_PANEL_MODE = 8, ZONE_NAME = 0x229, ZONE_TITLE = 0x273, ZONE_NAME_BUTTON = 0x267,
    NAME_BUTTON_UP = 0x13, NAME_BUTTON_DOWN = 0x14,
    CHAMP_NAME_MAX = 7, CHAMP_TITLE_MAX = 15
};
/* The characters of a champion name. */
static const char name_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ,.;: -()'?!";

Session session = {.slot = SAVE_SLOT_NONE};
/* The headers of the save slots in the slot dialogs. */
static uint8_t slots[SAVE_SLOT_COUNT][SAVE_HEADER_SIZE + 1];
static int editing;                     /* the player types the slot name or a champion name */

char *session_save_path(char *path, int slot, int bak)
{
    snprintf(path, SAVE_PATH_SIZE, "%s/DATA/SKSAVE%d.%s", session.data_dir, slot, bak ? "BAK" : "DAT");
    return path;
}

static char *data_path(char *path, const char *name)
{
    snprintf(path, SAVE_PATH_SIZE, "%s/DATA/%s", session.data_dir, name);
    return path;
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

/* Draws a GDAT picture placed in the screen zone, with its colors. */
static void draw_screen_picture(int category, int index, int entry)
{
    const Bitmap *image = draw_gdat_image(category, index, entry);
    int16_t r[RECT_SIZE], x = 0, y = 0;
    if (image && zone_compute_rect(image, r, ZONE_SCREEN, &x, &y, ZONE_NO_ANCHOR))
        ui_blit_bitmap(image, &draw_screen, r, x, y, UI_OPAQUE, draw_gdat_image_palette(category, index, entry));
}

/* ---- Start menu ---- */

void menu_draw_title(void)
{
    draw_screen_picture(CAT_MENU, 0, MENU_TITLE);
}

void menu_show_picture_and_wait(void)
{
    draw_screen_picture(CAT_MENU, 0, MENU_PICTURE);
    input_set_mode(INPUT_MODE_PICTURE);
    for (int n = MENU_PICTURE_VBLANKS; n && !main_loop.quit; n--) {
        command_process_queue();
        if (input_match.value == CMD_PICTURE_CLICK) break;
        game_wait_frames(1);
    }
    session.new_game = MENU_REDRAW;
    input_set_mode(INPUT_MODE_MENU);
}

void menu_run(void)
{
    input_set_mode(INPUT_MODE_MENU);
    do {
        menu_draw_title();
        input_flush();
        session.new_game = MENU_WAITING;
        while (session.new_game == MENU_WAITING && !main_loop.quit) {
            command_process_queue();
            game_wait_frames(1);
        }
    } while (session.new_game == MENU_REDRAW && !main_loop.quit);
}

/* ---- Slot dialogs ---- */

static const char *slot_name(int slot) { return (const char *)slots[slot] + SAVE_HEADER_NAME; }

static int slot_unused(int slot)
{
    return le32(slots[slot] + SAVE_HEADER_UNUSED) == UNUSED_SLOT;
}

void session_list_slots(void)
{
    char path[SAVE_PATH_SIZE];
    for (int i = 0; i < SAVE_SLOT_COUNT; i++) {
        memset(slots[i], 0, sizeof slots[i]);
        FILE *f = fopen(session_save_path(path, i, 0), "rb");
        if (f) {
            if (fread(slots[i], 1, SAVE_HEADER_SIZE, f) != SAVE_HEADER_SIZE) { /* a short file keeps what it has */ }
            fclose(f);
            continue;
        }
        snprintf((char *)slots[i] + SAVE_HEADER_NAME, SAVE_HEADER_SIZE - SAVE_HEADER_NAME, "%d UNUSED", i);
        put_le32(slots[i] + SAVE_HEADER_UNUSED, UNUSED_SLOT);
    }
}

/* The default name of slot: "GAME n". */
static void save_make_slot_name(char *dst, int slot)
{
    snprintf(dst, SAVE_NAME_SIZE, "GAME %d", slot);
}

void session_draw_slot_selection(int chosen)
{
    int16_t r[RECT_SIZE];
    if (!zone_get_rect(ZONE_SLOT_LIST, r)) return;
    int y = r[RECT_Y] + SLOT_NAME_TOP;
    for (int i = 0; i < SAVE_SLOT_COUNT; i++, y += TEXT_LINE_HEIGHT)
        text_draw_in_viewport(r[RECT_X], y, ui_colors[i == chosen ? UI_WHITE : UI_YELLOW], slot_name(i));
}

void session_draw_name_entry(const char *name, int cursor)
{
    int16_t r[RECT_SIZE];
    if (!zone_get_rect(ZONE_SLOT_NAME, r)) return;
    ui_blit_bitmap(draw_gdat_image(CAT_DIALOG, DIALOG_SAVE_SLOTS, 0), &view_viewport, r, r[RECT_X], r[RECT_Y],
                    UI_OPAQUE, draw_gdat_image_palette(CAT_DIALOG, DIALOG_SAVE_SLOTS, 0));
    text_draw_in_viewport(r[RECT_X], r[RECT_Y] + SLOT_NAME_TOP, ui_colors[UI_WHITE], name);
    if (cursor) {
        r[RECT_X] = (int16_t)(r[RECT_X] + (int)strlen(name) * FONT_ADVANCE);
        r[RECT_W] = FONT_ADVANCE;
        r[RECT_H] = (int16_t)(r[RECT_H] - SLOT_NAME_TOP);
        gfx_fill_rect(view_viewport.pixels, r, ui_colors[UI_YELLOW], view_viewport.width, IMAGE_8BPP);
    }
    dialog_present();
}

void session_open_slot_dialog(int id, int buttons)
{
    char text[2][TEXT_SIZE];
    text_gdat(CAT_DIALOG, id, 0, text[0]);
    text_gdat(CAT_DIALOG, id, 1, text[1]);
    int16_t r[RECT_SIZE];
    if (zone_get_rect(ZONE_DIALOG_BACK, r))
        gfx_blit_to_bitmap(draw_gdat_image(CAT_DIALOG, id, 0), &view_viewport, r, UI_OPAQUE,
                           draw_gdat_image_palette(CAT_DIALOG, id, 0));
    dialog_screen_mode(1);
    text_draw_in_zone(ZONE_DIALOG_VERSION, ui_colors[UI_DARKEST_GRAY], DIALOG_VERSION_TEXT);
    text_draw_in_zone(ZONE_SLOT_TEXT_1, ui_colors[UI_YELLOW], text[0]);
    text_draw_in_zone(ZONE_SLOT_TEXT_2, ui_colors[UI_YELLOW], text[1]);
    session_list_slots();
    dialog.button_count = buttons;
    input_push_mode(INPUT_MODE_DIALOG);
}

void session_close_slot_dialog(void)
{
    input_pop_mode();
    ui.screen_needs_refresh = 1;
}

/* The slot of a click in the slot list. */
static int clicked_slot(void)
{
    int16_t r[RECT_SIZE] = {0}, x = 0, y = 0;
    zone_get_rect(input_match.zone, r);
    zone_get_position(input_match.origin, &x, &y);
    int slot = (int16_t)(input_match.y - (r[RECT_Y] + y)) / TEXT_LINE_HEIGHT;
    return slot < SAVE_SLOT_COUNT ? slot : SAVE_SLOT_COUNT;
}

/* The load dialog: the slot that the player loads, SAVE_SLOT_NONE when cancelled. A restore after the end of the
   game loads the last slot without a dialog. */
static int save_choose_load_slot(void)
{
    if (session.loaded) return session.slot;
    session_open_slot_dialog(DIALOG_LOAD_SLOTS, SLOT_BUTTONS_LOAD);
    session_draw_slot_selection(SAVE_SLOT_NONE);
    dialog_present();
    dialog_screen_mode(0);
    int chosen = SAVE_SLOT_NONE, done = 0;
    input_match.choice = DIALOG_NO_CHOICE;
    while (!done && !main_loop.quit) {
        command_process_queue();
        if (input_match.choice == SLOT_CANCEL) {
            chosen = SAVE_SLOT_NONE;
            done = 1;
        } else if (input_match.choice == SLOT_OK) {
            done = 1;
        } else if (input_match.choice == SLOT_LIST) {
            chosen = clicked_slot();
            if (chosen < SAVE_SLOT_COUNT && slot_unused(chosen)) chosen = SAVE_SLOT_NONE;
            session_draw_slot_selection(chosen);
            dialog_present();
        }
        input_match.choice = -1;
        if (!done) game_wait_frames(1);
    }
    session_close_slot_dialog();
    return chosen;
}

/* A key of the name field of the save dialog: delete, end, or a character. */
static void type_slot_name_key(char *name, int *length)
{
    uint16_t key = input_match.key;
    if (key == KEY_BACKSPACE) {
        if (*length > 0) name[--*length] = '\0';
    } else if (key == KEY_ENTER) {
        editing = 0;
    } else if (*length < SAVE_NAME_MAX) {
        char c = kbd_key_to_ascii(key);
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        name[(*length)++] = c;
        name[*length] = '\0';
    }
    session_draw_name_entry(name, editing);
}

/* The save dialog: the slot that the player saves into (its name in session.save_name), SAVE_SLOT_NONE when
   cancelled. */
static int save_choose_save_slot(void)
{
    char *name = session.save_name;
    session_open_slot_dialog(DIALOG_SAVE_SLOTS, SLOT_BUTTONS_SAVE);
    int chosen = session.slot, length = 0, done = 0;
    if (chosen < 0) {
        for (chosen = 0; chosen < SAVE_SLOT_COUNT && !slot_unused(chosen); chosen++) {}
        if (chosen < SAVE_SLOT_COUNT) save_make_slot_name(name, chosen);
    } else {
        snprintf(name, SAVE_NAME_SIZE, "%s", slot_name(chosen));
    }
    session_draw_slot_selection(chosen);
    if (chosen == SAVE_SLOT_COUNT) dialog_present();
    else session_draw_name_entry(name, 0);
    while (!done && !main_loop.quit) {
        command_process_queue();
        switch (input_match.choice) {
        case SLOT_CANCEL:
            chosen = SAVE_SLOT_NONE;
            editing = 0;
            done = 1;
            break;
        case SLOT_OK:
            editing = 0;
            if (chosen == SAVE_SLOT_COUNT) break;
            if (!*name) save_make_slot_name(name, chosen);
            session_draw_name_entry(name, 0);
            done = 1;
            break;
        case SLOT_LIST:
            editing = 0;
            chosen = clicked_slot();
            if (chosen < SAVE_SLOT_COUNT && slot_unused(chosen)) save_make_slot_name(name, chosen);
            else snprintf(name, SAVE_NAME_SIZE, "%s", slot_name(chosen < SAVE_SLOT_COUNT ? chosen : SAVE_SLOT_COUNT - 1));
            session_draw_slot_selection(chosen);
            session_draw_name_entry(name, 0);
            break;
        case SLOT_NAME:
            if (chosen != SAVE_SLOT_COUNT && !editing) {
                editing = 1;
                session_draw_name_entry(name, 1);
                length = (int)strlen(name);
            }
            break;
        }
        if (editing && input_match.value >= CMD_TEXT_FIRST && input_match.value <= CMD_TEXT_KEY_LAST)
            type_slot_name_key(name, &length);
        input_match.choice = 0;
        input_match.value = 0;
        if (!done) game_wait_frames(1);
    }
    session_close_slot_dialog();
    return chosen;
}

/* ---- Load and save ---- */

/* Reads DUNGEON.DAT (DUNGENB.DAT when chosen and present) for a new game; returns 0 on a read error. */
static int load_new_game(void)
{
    char path[SAVE_PATH_SIZE];
    if (!session.use_dungenb || !file_exists(data_path(path, "DUNGENB.DAT"))) data_path(path, "DUNGEON.DAT");
    game.champion_count = 0;
    game.leader_hand = THING_NONE;
    int first = !weather.dungeon_loaded;
    if (!dungeon_load(path)) return 0;
    automap_reset();
    uint16_t start = dungeon_party_start();
    game.party_x = location_x(start);
    game.party_y = location_y(start);
    game.party_dir = location_dir(start);  /* the party start keeps the direction in the map bits */
    game.party_map = 0;
    timeline_init(EVENT_SLACK + dungeon_thing_count(THING_GROUP) + dungeon_thing_count(THING_PROJECTILE)
                  + dungeon_thing_count(THING_EXPLOSION));
    weather_load(gdat_current());
    dungeon_link_things_after_load();
    if (first) dungeon_read_level_table();
    return 1;
}

/* Loads the slot file at path into the game; returns 0 on an error. */
static int load_slot(const char *path)
{
    game.champion_count = 0;
    game.leader_hand = THING_NONE;
    int first = !weather.dungeon_loaded;
    if (save_load(path, gdat_current()) != SAVE_OK) return 0;
    if (first) dungeon_read_level_table();
    session.new_game = 0;
    session.load_time = game.game_time;
    session.loaded = 1;
    return 1;
}

int session_load(void)
{
    char path[SAVE_PATH_SIZE], bak[SAVE_PATH_SIZE];
    int from_bak = 0, ok;
    if (!session.new_game) {
        int slot = save_choose_load_slot();
        if (slot < 0) {
            session.slot = 0;  /* as in the original: a cancelled load leaves slot 0 */
            return -1;
        }
        session.slot = slot;
        session_save_path(path, slot, 0);
        session_save_path(bak, slot, 1);
        if (!file_exists(path) && file_exists(bak)) {
            snprintf(path, sizeof path, "%s", bak);
            from_bak = 1;
        } else if (!file_exists(path) && !session.loaded && file_exists(data_path(bak, "DUNGEON.FTL"))) {
            session.new_game = 1;
        }
    }
    ok = session.new_game ? load_new_game() : load_slot(path);
    if (!ok) {
        /* fix: the original asks for the disk of the game (dialog 7) when the load of the last slot fails */
        dialog_run(dialog_draw(DIALOG_ERROR, MESSAGE_GAME_DAMAGED));
        if (!session.loaded) session_quit();
        return -1;
    }
    if (session.new_game) {
        sensor_start_timers();
    } else if (from_bak) {
        session_save_path(bak, session.slot, 0);
        rename(path, bak);
    }
    dialog_draw(DIALOG_LOADING, MESSAGE_NONE);
    dialog.progress_enabled = 1;
    main_loop.end = 0;
    move_compute_map_arrival(game.party_x, game.party_y, game.party_map);
    party.redraw = REDRAW_ALL_VIEW;
    return 1;
}

/* Writes the game into slot; returns 0 on an error. */
static int save_into_slot(int slot)
{
    char path[SAVE_PATH_SIZE], bak[SAVE_PATH_SIZE];
    if (save_write(session_save_path(path, slot, 0), session_save_path(bak, slot, 1), session.save_name, gdat_current())
        != SAVE_OK)
        return 0;
    session.new_game = 0;
    session.save_time = game.game_time;
    return 1;
}

/* The save menu until the player saves (returns the slot), quits (*quit 1), or resumes (SAVE_SLOT_NONE). */
static int save_menu(int *quit)
{
    for (;;) {
        int choice = dialog_run(dialog_draw(DIALOG_SAVE_MENU, MESSAGE_NONE));
        if (main_loop.quit || choice == SAVE_MENU_RESUME) return SAVE_SLOT_NONE;
        if (choice == SAVE_MENU_QUIT || choice == SAVE_MENU_RESTART) {
            if (choice == SAVE_MENU_RESTART) session.restart = 1;  /* a cancel below keeps it, as in the original */
            if (session.load_time + QUIT_SAFE_TICKS < game.game_time && session.save_time + QUIT_SAFE_TICKS < game.game_time
                && dialog_run(dialog_draw(DIALOG_NOT_SAVED, MESSAGE_NONE)) == NOT_SAVED_CANCEL)
                continue;
            session.offer_restore = 0;
            *quit = 1;
            return SAVE_SLOT_NONE;
        }
        int slot = save_choose_save_slot();
        if (slot >= 0) return slot;
    }
}

void session_save(void)
{
    int quit = 0;
    session.restart = 0;
    action_area_close();
    action_update_area(0);
    game_hide_view();
    timeline_rebuild_free_list();
    int slot = save_menu(&quit);
    if (slot >= 0) {
        session.slot = slot;
        dialog_draw(DIALOG_SAVING, MESSAGE_NONE);
        if (!save_into_slot(slot)) dialog_run(dialog_draw(DIALOG_ERROR, MESSAGE_SAVE_FAILED));
    }
    creature_activate_groups_all_maps();
    if (quit) {
        session_end(0);
        return;
    }
    game_show_view();
}

int session_init(void)
{
    do {
        menu_run();
        if (main_loop.quit) return 0;
    } while (session_load() != 1 && !main_loop.quit);
    if (main_loop.quit) return 0;
    sound_set_volume_and_test(game.sound_volume, DEVICE_SOUND);
    sound_set_volume_and_test(game.music_volume, DEVICE_MUSIC);
    ui_reset();
    money_init_coin_table();
    game_start_session();
    input_flush();
    return 1;
}

int session_start_save(const char *path)
{
    int first = !weather.dungeon_loaded;
    if (save_load(path, gdat_current()) != SAVE_OK) return 0;
    if (first) dungeon_read_level_table();
    ui_reset();
    money_init_coin_table();
    ui_fill_zone_screen(ZONE_SCREEN, 0);  /* game_start_session runs champ_init_party_state */
    move_compute_map_arrival(game.party_x, game.party_y, game.party_map);
    party.redraw = REDRAW_ALL_VIEW;
    game_start_session();
    return 1;
}

/* ---- End of the game ---- */

void session_quit(void)
{
    if (session.completed) ui_fill_zone_screen(ZONE_SCREEN, 0);
    session.exit_code = session.completed ? EXIT_COMPLETED : session.quit_code ? EXIT_PARTY_DEAD
                        : session.restart ? EXIT_RESTART : EXIT_QUIT;
    main_loop.quit = 1;
}

void session_complete(void)
{
    session.completed = 1;
    session_quit();
}

void session_end(int party_dead)
{
    if (party_dead && !session.completed) {
        int portrait = game.champion_count ? champion(0)->portrait : SOUND_FALLBACK;
        sound_request_with_fallback(CAT_CHAMPION, portrait, SOUND_ENTRY_SCREAM, SOUND_FALLBACK, game.party_x,
                                 game.party_y, 0, END_SOUND_PRIORITY, SOUND_VOLUME_MAX);
        game_wait_frames(END_SOUND_VBLANKS);
    }
    dialog.screen_mode = 1;
    if (party_dead) {
        ui_fill_zone_screen(ZONE_SCREEN, 0);
        if (session.offer_restore) {
            session.loaded = dialog_run(dialog_draw(DIALOG_THE_END, MESSAGE_NONE)) == 0;
            if (session.loaded && session_load() != -1) {
                game_start_session();
                session.loaded = 0;
                input_flush();
                return;
            }
        }
    }
    ui_fill_zone_screen(ZONE_SCREEN, 0);
    if (session.restart || party_dead || main_loop.quit) {
        if (party_dead) session.quit_code = 1;
        session_quit();
        return;
    }
    draw_screen_picture(CAT_END, 0, END_PICTURE);
    input_set_mode(INPUT_MODE_PICTURE);
    do {
        command_process_queue();
        game_wait_frames(1);
    } while (input_match.value != CMD_PICTURE_CLICK && !main_loop.quit);
    session_quit();
}

/* ---- Champion names ---- */

/* A key of a champion name field other than Enter and Tab: deletes the last character, or adds a character of the
   name alphabet while the text is shorter than max; *x is the cursor. Returns 1 when the field needs a redraw. */
static int type_name_key(char *text, int *length, int max, int *x)
{
    uint16_t key = input_match.key;
    if (key == KEY_BACKSPACE) {
        if (*length > 0) {
            text[--*length] = '\0';
            *x -= FONT_ADVANCE;
        }
        return 1;
    }
    if (*length >= max) return 1;
    char ch = kbd_key_to_ascii(key);
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    if (!ch || !strchr(name_alphabet, ch) || (ch == ' ' && !*length)) return 0;
    text[(*length)++] = ch;
    text[*length] = '\0';
    *x += FONT_ADVANCE;
    return 1;
}

/* Removes the spaces at the end of text. */
static void trim_spaces(char *text)
{
    /* fix: the original reads the byte before an empty text */
    for (size_t n = strlen(text); n > 0 && text[n - 1] == ' '; n--) text[n - 1] = '\0';
}

void session_edit_champion_name(Champion *c)
{
    char name[CHAMPION_NAME_SIZE], title[CHAMPION_TITLE_SIZE];
    memcpy(name, c->name, sizeof name);
    memcpy(title, c->title, sizeof title);
    int16_t name_x, name_y, title_x, title_y;
    ui.panel_mode = NAME_PANEL_MODE;
    zone_get_position(ZONE_NAME, &name_x, &name_y);
    zone_get_position(ZONE_TITLE, &title_x, &title_y);
    input_push_mode(INPUT_MODE_NAME);
    editing = 1;
    game_hide_view();
    int in_name = 1, done = 0;
    while (!done && !main_loop.quit) {
        char *text = in_name ? c->name : c->title;
        int max = in_name ? CHAMP_NAME_MAX : CHAMP_TITLE_MAX, length = (int)strlen(text);
        int x = (in_name ? name_x : title_x) + length * FONT_ADVANCE, y = in_name ? name_y : title_y;
        for (int field_done = 0, redraw = 1; !field_done && !main_loop.quit;) {
            if (redraw) {
                ui_draw_gdat_in_viewport_zone(CAT_PANEL, 0, NAME_BUTTON_DOWN, ZONE_NAME_BUTTON, UI_OPAQUE);
                text_draw_in_zone(ZONE_NAME, ui_colors[UI_YELLOW], c->name);
                text_draw_in_zone(ZONE_TITLE, ui_colors[UI_YELLOW], c->title);
                text_draw_in_viewport(x, y, ui_colors[UI_GOLD], "_");
                game_present_viewport();
            }
            redraw = 0;
            input_match.choice = 0;
            input_match.value = 0;
            command_process_queue();
            int command = input_match.value;
            if (command == CMD_EDIT_NAME && !in_name) {
                in_name = 1;
                field_done = 1;
            } else if (command == CMD_EDIT_TITLE && in_name) {
                in_name = 0;
                field_done = 1;
            } else if (command == CMD_EDIT_CANCEL) {
                memcpy(c->name, name, sizeof name);
                memcpy(c->title, title, sizeof title);
                field_done = done = 1;
            } else if (!command) {
                game_wait_frames(1);
            } else if (input_match.key == KEY_ENTER || input_match.key == KEY_TAB) {
                trim_spaces(text);
                field_done = 1;
                if (in_name) in_name = 0;
                else if (input_match.key == KEY_TAB) in_name = 1;
                else done = 1;
            } else {
                redraw = type_name_key(text, &length, max, &x);
            }
        }
    }
    editing = 0;
    ui_draw_gdat_in_viewport_zone(CAT_PANEL, 0, NAME_BUTTON_UP, ZONE_NAME_BUTTON, UI_OPAQUE);
    c->redraw |= REDRAW_NAME;
    ui_draw_champion_state(ui.inventory - 1);
    game_show_view();
    input_pop_mode();
}

int args_drop_save(int argc, char **argv)
{
    int out = 1;
    for (int i = 1; i < argc; i++) {
        int is_value_option = !strcmp(argv[i], "--data") || !strcmp(argv[i], "--filter") || !strcmp(argv[i], "--save")
                              || !strcmp(argv[i], "--control") || !strcmp(argv[i], "--mode");
        if (is_value_option && i + 1 < argc) {
            if (strcmp(argv[i], "--save")) {
                argv[out++] = argv[i];
                argv[out++] = argv[i + 1];
            }
            i++;
        } else argv[out++] = argv[i];
    }
    argv[out] = NULL;
    return out;
}
