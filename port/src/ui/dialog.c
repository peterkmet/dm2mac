/* Dialogs: messages, questions, progress, and the volume settings. */
#include <string.h>
#include "dialog.h"
#include "draw.h"
#include "game.h"
#include "gdat.h"
#include "input.h"
#include "music.h"
#include "save.h"
#include "sound.h"
#include "sound_request.h"
#include "text.h"
#include "ui.h"
#include "view.h"
#include "zone.h"

#define DIALOG_TEXT_COUNT 20            /* text entries 0 to 19: the buttons */
#define DIALOG_MESSAGE_ENTRY 0x14
#define DIALOG_PICTURE_ENTRY 0
#define DIALOG_VOLUME_BUTTONS 7
#define BUTTON_KEY 7                    /* transparent color of a pressed button picture */
#define BUTTON_PRESS_VBLANKS 20
#define ZONE_DIALOG_SCREEN 5            /* where the dialog viewport goes on a black screen */
#define ZONE_DIALOG_MESSAGE_WIDE 0x1d5  /* the message zone of a dialog with fewer than 2 buttons */
#define ZONE_DIALOG_MESSAGE 0x1d7
#define ZONE_PROGRESS_BAR 0x1da
#define DIALOG_LINE_GAP 1               /* pixels between two lines */
#define DIALOG_MESSAGE_TOP 6            /* the message area starts this far into its zone */
#define PERMILLE 1000
#define MESSAGE_TEXTS 2                 /* the dialog message and the error message under it */
#define WRAP_MARGIN_SHIFT 3             /* a text wider than 7/8 of the zone wraps */
#define WRAP_SHRINK_SHIFT 2             /* to 3/4 of its width, when it is not wider than 5/4 of the zone */
/* The zones of the button texts (table of SKULL.EXE); the first one by button count: 2 at 1, 3 at 3, 4 at 6, else 0. */
static const uint16_t button_text_zones[] = {0x1ce, 0x1cf, 0x1ce, 0x1cf, 0x1d2, 0x1d3, 0x1d0, 0x1d1, 0x1d2, 0x1d3};

/* The volume dialog: the labels, the buttons of the settings 0 to 7, and their texts. */
enum {
    VOLUME_SOUND_LABEL = 0x28, VOLUME_SOUND_OFF = 0x29, VOLUME_MUSIC_LABEL = 0x3c, VOLUME_MUSIC_OFF = 0x3d,
    VOLUME_BUTTON = 2, VOLUME_BUTTON_ON = 3, VOLUME_LEVELS = 8,
    ZONE_SOUND_LABEL = 0x15f, ZONE_SOUND_BUTTON = 0x160, ZONE_SOUND_TEXT = 0x168,
    ZONE_MUSIC_LABEL = 0x170, ZONE_MUSIC_BUTTON = 0x171, ZONE_MUSIC_TEXT = 0x179
};
/* The test sound of a new sound volume. */
enum { TEST_SOUND_INDEX = 0 };

DialogState dialog = {.screen_mode = 1};

void dialog_screen_mode(int begin)
{
    if (dialog.screen_mode && begin) ui_fill_zone_screen(ZONE_SCREEN, 0);
}

void dialog_present(void)
{
    if (!dialog.screen_mode) {
        game_present_viewport();
        return;
    }
    int16_t r[RECT_SIZE], x = 0, y = 0;
    if (zone_compute_rect(&view_viewport, r, ZONE_DIALOG_SCREEN, &x, &y, ZONE_NO_ANCHOR))
        ui_blit_bitmap(&view_viewport, &draw_screen, r, x, y, UI_OPAQUE, NULL);
}

void dialog_draw_button(int zone, int highlighted)
{
    (void)zone;
    if (!highlighted) return;
    const Bitmap *image = draw_gdat_image(dialog.button_category, dialog.button_index, dialog.button_entry);
    if (!image) return;
    int16_t r[RECT_SIZE];
    rect_center_in(r, input_match.rect, image->width, image->height);
    if (input_match.origin != -1) {
        int16_t x, y;
        zone_get_position(input_match.origin, &x, &y);
        r[RECT_X] = (int16_t)(r[RECT_X] - x);
        r[RECT_Y] = (int16_t)(r[RECT_Y] - y);
    }
    gfx_blit_to_bitmap(image, &view_viewport, r, BUTTON_KEY,
                       draw_gdat_image_palette(dialog.button_category, dialog.button_index, dialog.button_entry));
    game_wait_frames(BUTTON_PRESS_VBLANKS);
}

/* The message texts of a dialog, centered in rect: each text wraps to a width near the zone width. */
static void draw_message(const int16_t *rect, char *texts[MESSAGE_TEXTS], const int colors[MESSAGE_TEXTS])
{
    int16_t widths[MESSAGE_TEXTS] = {0}, heights[MESSAGE_TEXTS] = {0};
    int total = 0;
    char line[TEXT_SIZE];
    for (int i = 0; i < MESSAGE_TEXTS; i++) {
        if (!texts[i]) continue;
        text_measure(texts[i], &widths[i], &heights[i]);
        int w = rect[RECT_W];
        if (widths[i] > w - (w >> WRAP_MARGIN_SHIFT))
            widths[i] = widths[i] < w + (w >> WRAP_SHRINK_SHIFT) ? (int16_t)(widths[i] - (widths[i] >> WRAP_SHRINK_SHIFT))
                                                                 : (int16_t)w;
        for (int16_t pos = 0; texts[i][pos];) {
            text_wrap_line(texts[i], line, &pos, widths[i]);
            total += 2 * DIALOG_LINE_GAP + heights[i] - FONT_BASELINE + 1;
            if (texts[i][pos] == '\n') pos++;
        }
    }
    if (!(int16_t)total) return;
    int offset = DIALOG_MESSAGE_TOP + ((rect[RECT_H] - ((int16_t)total - 2 * DIALOG_LINE_GAP)) >> 1) - 1;
    int y = rect[RECT_Y] + (offset > 0 ? offset : 0);
    for (int i = 0; i < MESSAGE_TEXTS; i++) {
        if (!texts[i]) continue;
        for (int16_t pos = 0; texts[i][pos];) {
            int w = text_wrap_line(texts[i], line, &pos, widths[i]);
            text_draw_in_viewport((int16_t)(VIEWPORT_WIDTH - w) >> 1, (int16_t)y, colors[i], line);
            y += TEXT_LINE_HEIGHT + DIALOG_LINE_GAP;
            if (texts[i][pos] == '\n') pos++;
        }
    }
}

int dialog_draw(int id, int message)
{
    const gdat_t *g = gdat_current();
    int present = !((id == DIALOG_LOADING || id == DIALOG_VOLUME) && message);
    if (message && id != DIALOG_ERROR && id != DIALOG_LOADING && gdat_exists(g, CAT_DIALOG, DIALOG_ERROR, GDAT_TYPE_IMAGE, DIALOG_PICTURE_ENTRY)) {
        dialog_run(dialog_draw(DIALOG_ERROR, message));
        message = MESSAGE_NONE;
    }
    dialog.id = id;
    if (present) dialog_screen_mode(1);
    static char texts[DIALOG_TEXT_COUNT][TEXT_SIZE];
    int count = 0;
    for (int e = 0; e < DIALOG_TEXT_COUNT; e++)
        if (*text_gdat(CAT_DIALOG, id, e, texts[count])) count++;
    int16_t r[RECT_SIZE];
    if (zone_get_rect(ZONE_DIALOG_BACK, r))
        gfx_blit_to_bitmap(draw_gdat_image(CAT_DIALOG, id, DIALOG_PICTURE_ENTRY), &view_viewport, r, UI_OPAQUE,
                           draw_gdat_image_palette(CAT_DIALOG, id, DIALOG_PICTURE_ENTRY));
    text_draw_in_zone(ZONE_DIALOG_VERSION, ui_colors[UI_DARKEST_GRAY], DIALOG_VERSION_TEXT);
    int first = count == 2 ? 1 : count == 3 ? 3 : count == 4 ? 6 : 0;
    for (int i = 0; i < count; i++) text_draw_in_zone(button_text_zones[first + i], ui_colors[UI_YELLOW], texts[i]);
    if (id != DIALOG_VOLUME && zone_get_rect(count < 2 ? ZONE_DIALOG_MESSAGE_WIDE : ZONE_DIALOG_MESSAGE, r)) {
        static char message_texts[MESSAGE_TEXTS][TEXT_SIZE];
        char *shown[MESSAGE_TEXTS] = {NULL, NULL};
        int colors[MESSAGE_TEXTS];
        if (message && id == DIALOG_ERROR) {
            colors[0] = ui_colors[UI_WHITE];
            shown[0] = text_gdat(CAT_DIALOG, DIALOG_ERROR, message, message_texts[0]);
        } else {
            colors[0] = ui_colors[UI_YELLOW];
            shown[0] = text_gdat(CAT_DIALOG, id, DIALOG_MESSAGE_ENTRY, message_texts[0]);
            if (message) {
                colors[1] = ui_colors[UI_WHITE];
                shown[1] = text_gdat(CAT_DIALOG, DIALOG_ERROR, message, message_texts[1]);
            }
        }
        draw_message(r, shown, colors);
    }
    if (present) {
        dialog_present();
        dialog_screen_mode(0);
    }
    ui.screen_needs_refresh = 1;
    return id;
}

int dialog_run(int id)
{
    const gdat_t *g = gdat_current();
    uint8_t values[DIALOG_TEXT_COUNT];
    int count = 0;
    char text[TEXT_SIZE];
    for (int e = 0; e < DIALOG_TEXT_COUNT; e++) {
        if (!*gdat_text(g, CAT_DIALOG, id, e, text, sizeof text)) continue;
        /* ponytail: the high byte (a default button of the original) only feeds a check that is always 0 */
        uint8_t value = (uint8_t)gdat_entry_index(g, CAT_DIALOG, id, GDAT_TYPE_VALUE, e);
        values[count++] = value ? value : (uint8_t)e;
    }
    dialog.button_count = id == DIALOG_VOLUME ? DIALOG_VOLUME_BUTTONS : count;
    input_push_mode(INPUT_MODE_DIALOG);
    input_match.choice = DIALOG_NO_CHOICE;
    do {
        command_process_queue();
        game_wait_frames(1);
        if (input_match.choice == DIALOG_NO_CHOICE && input_read_key() == KEY_ENTER)
            input_simulate_command_click(CMD_DIALOG_BUTTON_FIRST);
    } while (input_match.choice == DIALOG_NO_CHOICE && !main_loop.quit);
    int choice = input_match.choice;
    int result = id == DIALOG_VOLUME ? input_match.value : choice >= 1 && choice <= count ? values[choice - 1] : 0;
    ui.screen_needs_refresh = 1;
    input_pop_mode();
    return result;
}

void dialog_draw_progress_bar(int permille)
{
    int16_t r[RECT_SIZE];
    if (!dialog.progress_enabled || !zone_get_rect(ZONE_PROGRESS_BAR, r)) return;
    r[RECT_W] = (int16_t)(r[RECT_W] * permille / PERMILLE);
    if (r[RECT_W] <= 0 || r[RECT_W] == dialog.progress_width) return;
    dialog.progress_width = r[RECT_W];
    gfx_fill_rect(view_viewport.pixels, r, ui_colors[UI_GOLD], view_viewport.width, IMAGE_8BPP);
    dialog_present();
}

void sound_set_volume_and_test(int volume, int device)
{
    if (device == DEVICE_SOUND) {
        game.sound_volume = (uint8_t)volume;
        sound_request(CAT_DUNGEON, TEST_SOUND_INDEX, SOUND_ENTRY_TRANSFORM, SOUND_PRIORITY_TRANSFORM, SOUND_VOLUME_LOUD,
                      game.party_x, game.party_y, 0);
        sound_set_volume(volume);
    } else if (device == DEVICE_MUSIC) {
        game.music_volume = (uint8_t)volume;
        music_set_volume(volume);
    }
}

/* The buttons of the settings of one volume, the chosen one lit, and their texts (0: "off"). */
static void draw_volume_buttons(int setting, int button_zone, int text_zone, int off_entry)
{
    char text[TEXT_SIZE];
    for (int level = VOLUME_LEVELS - 1; level >= 0; level--) {
        ui_draw_gdat_in_viewport_zone(CAT_DIALOG, DIALOG_VOLUME, level == setting ? VOLUME_BUTTON_ON : VOLUME_BUTTON,
                                       button_zone + level, UI_OPAQUE);
        if (level) {
            text[0] = (char)('0' + level);
            text[1] = '\0';
        } else {
            text_gdat(CAT_DIALOG, DIALOG_VOLUME, off_entry, text);
        }
        text_draw_in_zone(text_zone + level, ui_colors[UI_YELLOW], text);
    }
}

void dialog_volume(void)
{
    char text[TEXT_SIZE];
    if (!gdat_exists(gdat_current(), CAT_DIALOG, DIALOG_VOLUME, GDAT_TYPE_IMAGE, DIALOG_PICTURE_ENTRY)) return;
    dialog_draw(DIALOG_VOLUME, MESSAGE_NONE);
    text_draw_in_zone(ZONE_SOUND_LABEL, ui_colors[UI_YELLOW], text_gdat(CAT_DIALOG, DIALOG_VOLUME, VOLUME_SOUND_LABEL, text));
    text_draw_in_zone(ZONE_MUSIC_LABEL, ui_colors[UI_YELLOW], text_gdat(CAT_DIALOG, DIALOG_VOLUME, VOLUME_MUSIC_LABEL, text));
    for (;;) {
        draw_volume_buttons(game.sound_volume, ZONE_SOUND_BUTTON, ZONE_SOUND_TEXT, VOLUME_SOUND_OFF);
        draw_volume_buttons(game.music_volume, ZONE_MUSIC_BUTTON, ZONE_MUSIC_TEXT, VOLUME_MUSIC_OFF);
        dialog_present();
        int command = dialog_run(DIALOG_VOLUME);
        if (command == CMD_DIALOG_BUTTON_FIRST || main_loop.quit) return;
        if (command >= CMD_MUSIC_VOLUME_FIRST && command <= CMD_MUSIC_VOLUME_LAST)
            sound_set_volume_and_test(command - CMD_MUSIC_VOLUME_FIRST, DEVICE_MUSIC);
        else
            sound_set_volume_and_test(command - CMD_SOUND_VOLUME_FIRST, DEVICE_SOUND);
    }
}
