/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Click feedback: the feedback scripts that draw a pressed button, and the movement arrows. */
#include <stdlib.h>
#include <string.h>
#include "action.h"
#include "action_area.h"
#include "advanced.h"
#include "creature.h"
#include "dialog.h"
#include "game.h"
#include "game_text.h"
#include "ids.h"
#include "input_internal.h"
#include "minion_map.h"
#include "move.h"
#include "recruit.h"
#include "view.h"
#include "zone.h"

/* Mouse event types of the event ring besides the buttons. */
#define EVENT_CLICK_OTHER 0x10 /* a click type of the original that the port never queues */
/* the mouse event types that find a click command: bits 0, 1, and 4 */
#define EVENT_CLICK_TYPES (INPUT_BUTTON_RIGHT | INPUT_BUTTON_LEFT | EVENT_CLICK_OTHER)

/* The size of the click feedback scripts. */
#define CLICK_FEEDBACK_SIZE 236

/* The places of the click feedback scripts. */
#define SCRIPT_QUEUE            0x00
#define SCRIPT_EXECUTE          0x01
#define SCRIPT_MOVE_FIRST       0x02
#define SCRIPT_HAND_FIRST       0x5c
#define SCRIPT_PARTY_ICON_FIRST 0x66
#define SCRIPT_ACTION_CANCEL    0x7a
#define SCRIPT_ROTATE_FIRST     0x82
#define SCRIPT_RUNE             0x92
#define SCRIPT_REMOVE_RUNE      0x96
#define SCRIPT_CAST_SPELL       0x9e
#define SCRIPT_ACTION_FIRST     0xa2
#define SCRIPT_INVENTORY_CLOSE  0xb1
#define SCRIPT_SLEEP            0xb6
#define SCRIPT_SAVE             0xbb
#define SCRIPT_MAP_OPTION_FIRST 0xc0
#define SCRIPT_DIALOG_BUTTON    0xdc
#define SCRIPT_DIALOG_BUTTON_2  0xe3
#define SCRIPT_VIEWPORT         0xea

/* A script of a command group is SIZE long per command. */
#define SCRIPT_MOVE_SIZE       15
#define SCRIPT_HAND_SIZE       5
#define SCRIPT_PARTY_ICON_SIZE 5
#define SCRIPT_ROTATE_SIZE     8
#define SCRIPT_ACTION_SIZE     5
#define SCRIPT_MAP_OPTION_SIZE 7

/* Feedback script opcodes (low 6 bits; bit 6: run on, bit 7: end). */
#define FB_DELAY              0x00
#define FB_RELEASE_QUEUE      0x01
#define FB_WAIT_QUEUE         0x02
#define FB_WAIT_STEP          0x03
#define FB_QUEUE              0x04
#define FB_EXECUTE            0x05
#define FB_QUEUE_AND_RELEASE  0x06
#define FB_PICTURE            0x07
#define FB_SPELL_ICON         0x08
#define FB_HAND_ICON          0x09
#define FB_RUNE_BUTTON        0x0a
#define FB_ENTERED_RUNES      0x0b
#define FB_ACTION_NAME        0x0c
#define FB_INVENTORY_BUTTON   0x0d
#define FB_MAP_OPTION_BUTTONS 0x0e
#define FB_DIALOG_BUTTON      0x0f
#define FB_DIALOG_BUTTON_2    0x10

/* Bits of a feedback script opcode. */
#define FB_OP_MASK 0x3f
#define FB_RUN_ON  0x40
#define FB_END     0x80

/* The arguments of a picture opcode: a GDAT image and its transparent color; a dialog button has the first three. */
#define PICTURE_CATEGORY   0
#define PICTURE_INDEX      1
#define PICTURE_ENTRY      2
#define PICTURE_KEY        3
#define PICTURE_ARGS       4
#define DIALOG_BUTTON_ARGS PICTURE_KEY

/* The middle part of the viewport without a push arrow. */
#define ARROW_CENTER_X       20 /* half size of the middle part of the viewport, without an arrow */
#define ARROW_CENTER_Y       15
#define ARROW_PRESSED_OFFSET 1 /* the graphic entry of a pressed movement arrow after the released one */

/* The feedback bitmap of the clicked button, open or closed. */
typedef struct {
    Offscreen o;
    bool opened;
} Feedback;

/* The click feedback scripts (see run_click_feedback). */
static const uint8_t click_feedback_scripts[CLICK_FEEDBACK_SIZE] = {
    0x86, 0x85, 0x47, 0x01, 0x03, 0x03, 0xff, 0x46, 0x00, 0x04, 0x03, 0x07, 0x01, 0x03, 0x02, 0xff, 0x82, 0x47, 0x01,
    0x03, 0x05, 0xff, 0x46, 0x00, 0x04, 0x03, 0x07, 0x01, 0x03, 0x04, 0xff, 0x82, 0x47, 0x01, 0x03, 0x07, 0xff, 0x46,
    0x00, 0x04, 0x03, 0x07, 0x01, 0x03, 0x06, 0xff, 0x82, 0x47, 0x01, 0x03, 0x09, 0xff, 0x46, 0x00, 0x04, 0x03, 0x07,
    0x01, 0x03, 0x08, 0xff, 0x82, 0x47, 0x01, 0x03, 0x0b, 0xff, 0x46, 0x00, 0x04, 0x03, 0x07, 0x01, 0x03, 0x0a, 0xff,
    0x82, 0x47, 0x01, 0x03, 0x0d, 0xff, 0x46, 0x00, 0x04, 0x03, 0x07, 0x01, 0x03, 0x0c, 0xff, 0x82, 0x49, 0x00, 0x00,
    0x06, 0x85, 0x49, 0x01, 0x00, 0x06, 0x85, 0x48, 0x00, 0x00, 0x06, 0x85, 0x48, 0x01, 0x00, 0x06, 0x85, 0x48, 0x02,
    0x00, 0x06, 0x85, 0x48, 0x03, 0x00, 0x06, 0x85, 0x47, 0x01, 0x04, 0x0f, 0xff, 0x00, 0x06, 0x85, 0x47, 0x01, 0x04,
    0x11, 0xff, 0x00, 0x04, 0x85, 0x47, 0x01, 0x04, 0x13, 0xff, 0x00, 0x04, 0x85, 0x4a, 0x00, 0x06, 0x85, 0x47, 0x01,
    0x05, 0x0a, 0xff, 0x00, 0x06, 0x85, 0x4b, 0x00, 0x06, 0x86, 0x4c, 0x00, 0x00, 0x06, 0x86, 0x4c, 0x01, 0x00, 0x06,
    0x86, 0x4c, 0x02, 0x00, 0x06, 0x86, 0x4d, 0x02, 0x00, 0x06, 0x86, 0x4d, 0x04, 0x00, 0x06, 0x85, 0x4d, 0x08, 0x00,
    0x06, 0x86, 0x4e, 0x01, 0x05, 0x00, 0x04, 0x8e, 0x00, 0x4e, 0x02, 0x05, 0x00, 0x04, 0x8e, 0x00, 0x4e, 0x04, 0x05,
    0x00, 0x04, 0x8e, 0x00, 0x4e, 0x08, 0x05, 0x00, 0x04, 0x8e, 0x00, 0x4f, 0x1a, 0xfe, 0x00, 0x00, 0x04, 0x85, 0x50,
    0x1a, 0xfe, 0x01, 0x00, 0x04, 0x85, 0x06, 0x83,
};

/* ---- Click feedback ---- */

void input_refine_champion_click(Command *c)
{
    int command = c->command;
    if (command >= CMD_HAND_FIRST && command <= CMD_HAND_LAST) {
        int n = command - CMD_HAND_FIRST;
        int16_t hand = n % SLOT_HAND_COUNT;
        int16_t champion_index = input_party_cell_champion(n / SLOT_HAND_COUNT);
        if (champion_index < 0 || champion(champion_index)->action_delay[hand] ||
            !champ_list_actions(champion_index, champion(champion_index)->slots[hand], hand))
            c->command = 0;
        else
            c->command = (uint16_t)(CMD_HAND_FIRST + champion_index * SLOT_HAND_COUNT + hand);
        return;
    }
    if (command < CMD_PARTY_ICON_FIRST || command > CMD_PARTY_ICON_LAST)
        return;
    int icon = command - CMD_PARTY_ICON_FIRST; /* the cell relative to the party direction */
    int16_t champion_index = input_party_cell_champion(icon);
    if (champion_index < 0) {
        c->command = 0;
        return;
    }
    const int16_t *r = c->rect;
    /* the distances to the edges of the icon that face the party center */
    int16_t dy = (int16_t)(icon < HALF_CELLS ? r[RECT_Y] + r[RECT_H] - 1 - c->y : c->y - r[RECT_Y]);
    int16_t dx = (int16_t)(icon == CELL_FRONT_RIGHT || icon == CELL_BACK_RIGHT ? c->x - r[RECT_X]
                                                                               : r[RECT_X] + r[RECT_W] - 1 - c->x);
    if (dx <= dy)
        c->command = (uint16_t)(CMD_LEADER_FIRST + icon);
    else if (champion(champion_index)->action_delay[ACTION_SPELL])
        c->command = 0;
}

/* The script of a pressed dialog button: by the button count of the open dialog and the command, as in the
   original. */
static int pressed_button_script(uint16_t command)
{
    switch (g_dialog.button_count) {
    case 1:
    case 2:
    case 7:
        return SCRIPT_DIALOG_BUTTON_2;
    case 3:
        return command == CMD_DIALOG_BUTTON_FIRST ? SCRIPT_DIALOG_BUTTON_2 : SCRIPT_DIALOG_BUTTON;
    case 4:
        return SCRIPT_DIALOG_BUTTON;
    case 5:
    case 6:
        return command <= CMD_DIALOG_BUTTON_FIRST + 1 ? SCRIPT_DIALOG_BUTTON : SCRIPT_EXECUTE;
    default:
        return SCRIPT_EXECUTE;
    }
}

const uint8_t *input_click_feedback_script(Command *c)
{
    input_refine_champion_click(c);
    uint16_t command = c->command;
    int offset;
    if (!command || command > CMD_OPTIONS)
        return NULL;
    if ((command >= CMD_SLOT_FIRST && command <= CMD_SLOT_LAST) || command == CMD_VOLUME ||
        (command >= CMD_DRAG_FIRST && command <= CMD_DRAG_CANCEL) ||
        (command >= CMD_LEADER_FIRST && command <= CMD_LEADER_LAST) ||
        (command >= CMD_SOUND_VOLUME_FIRST && command <= CMD_MUSIC_VOLUME_LAST) ||
        (command >= CMD_EDIT_CHAMPION_NAME && command <= CMD_EDIT_CANCEL))
        offset = SCRIPT_EXECUTE;
    else if (command <= CMD_MOVE_LEFT)
        offset = (command - CMD_TURN_LEFT) * SCRIPT_MOVE_SIZE + SCRIPT_MOVE_FIRST;
    else if (command >= CMD_HAND_FIRST && command <= CMD_HAND_LAST)
        offset = (command - CMD_HAND_FIRST) % SLOT_HAND_COUNT * SCRIPT_HAND_SIZE + SCRIPT_HAND_FIRST;
    else if (command >= CMD_PARTY_ICON_FIRST && command <= CMD_PARTY_ICON_LAST)
        offset = (command - CMD_PARTY_ICON_FIRST) * SCRIPT_PARTY_ICON_SIZE + SCRIPT_PARTY_ICON_FIRST;
    else if (command == CMD_ACTION_CANCEL)
        offset = SCRIPT_ACTION_CANCEL;
    else if (command == CMD_ROTATE_LEFT || command == CMD_ROTATE_RIGHT)
        offset = (command - CMD_ROTATE_LEFT) * SCRIPT_ROTATE_SIZE + SCRIPT_ROTATE_FIRST;
    else if (command >= CMD_RUNE_FIRST && command <= CMD_RUNE_LAST)
        offset = SCRIPT_RUNE;
    else if (command == CMD_REMOVE_RUNE)
        offset = SCRIPT_REMOVE_RUNE;
    else if (command == CMD_CAST_SPELL)
        offset = SCRIPT_CAST_SPELL;
    else if (command >= CMD_ACTION_FIRST && command <= CMD_ACTION_LAST)
        offset = (command - CMD_ACTION_FIRST) * SCRIPT_ACTION_SIZE + SCRIPT_ACTION_FIRST;
    else if (command == CMD_INVENTORY_LEADER || command == CMD_RECRUIT_CANCEL)
        offset = SCRIPT_INVENTORY_CLOSE;
    else if (command == CMD_SLEEP)
        offset = SCRIPT_SLEEP;
    else if (command == CMD_SAVE)
        offset = SCRIPT_SAVE;
    else if (command >= CMD_MAP_OPTION_FIRST && command <= CMD_MAP_OPTION_LAST)
        offset = (command - CMD_MAP_OPTION_FIRST) * SCRIPT_MAP_OPTION_SIZE + SCRIPT_MAP_OPTION_FIRST;
    else if (command >= CMD_DIALOG_BUTTON_FIRST && command <= CMD_DIALOG_BUTTON_LAST)
        offset = pressed_button_script(command);
    else
        offset = command == CMD_VIEWPORT ? SCRIPT_VIEWPORT : SCRIPT_QUEUE;
    return click_feedback_scripts + offset;
}

void rect_center_in(int16_t *rect, const int16_t *src, int16_t w, int16_t h)
{
    rect[RECT_W] = w;
    rect[RECT_X] = (int16_t)(src[RECT_X] + ((src[RECT_W] - w + 1) >> 1));
    rect[RECT_H] = h;
    rect[RECT_Y] = (int16_t)(src[RECT_Y] + ((src[RECT_H] - h + 1) >> 1));
}

static void queue_current(void)
{
    g_input.queue[g_input.queue_tail] = g_input.current;
    g_input.queue_tail = (g_input.queue_tail + 1) % COMMAND_QUEUE;
}

/* The commands that wait in the queue keep the input disabled until they run. */
static void release_queue(void)
{
    g_input.disabled = (g_input.queue_tail - g_input.queue_head + COMMAND_QUEUE) % COMMAND_QUEUE;
}

/* Opens the feedback bitmap of the clicked button when it is closed; r becomes a w x h rect centered in it. */
static void feedback_init_rect(Feedback *f, int16_t *r, int16_t w, int16_t h)
{
    if (!f->opened) {
        memcpy(f->o.rect, g_input.current.rect, sizeof f->o.rect);
        offscreen_create(&f->o, ZONE_NONE, false);
        f->opened = true;
    }
    rect_center_in(r, f->o.rect, w, h);
}

/* Draws the picture of the arguments at p centered in the clicked button. */
static void feedback_picture(Feedback *f, const uint8_t *p)
{
    int category = p[PICTURE_CATEGORY];
    int index = p[PICTURE_INDEX];
    int entry = p[PICTURE_ENTRY];
    const Bitmap *image = draw_gdat_image(category, index, entry);
    if (!image)
        return;
    int16_t r[RECT_SIZE];
    feedback_init_rect(f, r, image->width, image->height);
    offscreen_blit(&f->o,
                   &(BlitSource){.bitmap = image,
                                 .transparent = (int8_t)p[PICTURE_KEY], /* 0xff in the script is GFX_OPAQUE */
                                 .colormap = draw_gdat_image_palette(category, index, entry)},
                   r);
}

/* Draws the pressed rune button with the rune of the pressed button in the next rune set. */
static void feedback_rune_button(Feedback *f)
{
    const Bitmap *image = draw_gdat_image(CAT_INTERFACE, UI_INDEX_SPELL, GFX_SPELL_RUNE_PRESSED);
    if (!image)
        return;
    int16_t r[RECT_SIZE];
    feedback_init_rect(f, r, image->width, image->height);
    offscreen_blit(
        &f->o,
        &(BlitSource){.bitmap = image,
                      .transparent = GFX_OPAQUE,
                      .colormap = draw_gdat_image_palette(CAT_INTERFACE, UI_INDEX_SPELL, GFX_SPELL_RUNE_PRESSED)},
        r);
    /* the rune of the pressed button in the next rune set */
    int set = champion(g_action.champion - 1)->rune_count;
    int rune = g_input.current.command - CMD_RUNE_FIRST;
    char text[RUNE_TEXT_SIZE] = {(char)(set * SPELL_RUNES_PER_SET + SPELL_RUNE_GLYPH_FIRST + rune), '\0'};
    ScreenSize size = text_measure(text);
    rect_center_in(r, f->o.rect, size.w, size.h);
    offscreen_draw_text_at(&f->o, r[RECT_X], r[RECT_Y] + r[RECT_H] - 1,
                           (TextColors){g_ui_colors[UI_BLACK], g_ui_colors[UI_CYAN]}, text);
}

/* Copies the drawn action area to the screen. */
static void present_action_area(void)
{
    offscreen_flush(area_begin_draw(false), true);
}

/* Draws the pressed hand icon of the action champion, or of the champion of a hand command, in the action area. */
static void feedback_hand_icon(int icon)
{
    int16_t champion_index = g_action.champion - 1;
    if (!g_action.champion) {
        if (g_input.current.command < CMD_HAND_FIRST || g_input.current.command > CMD_HAND_LAST)
            return;
        champion_index = (g_input.current.command - CMD_HAND_FIRST) / SLOT_HAND_COUNT;
    }
    area_draw_hand_icon(champion_index, icon, true);
    present_action_area();
}

/* Draws the pressed spell icon of the champion in the party cell in the action area. */
static void feedback_spell_icon(int16_t cell)
{
    int16_t champion_index = input_party_cell_champion(cell);
    if (champion_index < 0)
        return;
    area_draw_spell_icon(champion_index, true);
    present_action_area();
}

/* Runs a drawing opcode of a feedback script with its arguments at p; returns the next opcode. */
static const uint8_t *feedback_draw_op(Feedback *f, uint8_t op, const uint8_t *p)
{
    switch (op & FB_OP_MASK) {
    case FB_PICTURE:
        feedback_picture(f, p);
        p += PICTURE_ARGS;
        break;
    case FB_RUNE_BUTTON:
        feedback_rune_button(f);
        break;
    case FB_SPELL_ICON:
        feedback_spell_icon(*p++);
        break;
    case FB_HAND_ICON:
        feedback_hand_icon(*p++);
        break;
    case FB_ACTION_NAME:
        area_draw_name(*p++, true);
        present_action_area();
        break;
    case FB_ENTERED_RUNES:
        area_draw_entered_runes(true);
        present_action_area();
        break;
    case FB_MAP_OPTION_BUTTONS:
        mmap_draw_option_buttons(*p++);
        present_action_area();
        break;
    case FB_INVENTORY_BUTTON:
        if (g_ui.inventory) {
            game_set_inventory_buttons(g_ui.buttons | *p);
            ui_draw_champion_state(g_ui.inventory - 1);
        }
        p++;
        break;
    case FB_DIALOG_BUTTON:
    case FB_DIALOG_BUTTON_2:
        dialog_draw_button(true, (GdatKey){p[PICTURE_CATEGORY], p[PICTURE_INDEX], p[PICTURE_ENTRY]});
        p += DIALOG_BUTTON_ARGS;
        f->opened = false; /* the original forgets the open feedback bitmap */
        dialog_present();
        break;
    default:
        break;
    }
    return p;
}

/* Runs the opcode op of a feedback script with its arguments at p; returns the next opcode. */
static const uint8_t *feedback_run_op(Feedback *f, uint8_t op, const uint8_t *p, bool *queue_full)
{
    switch (op & FB_OP_MASK) {
    case FB_DELAY:
        g_input.script_delay = *p++;
        break;
    case FB_RELEASE_QUEUE:
        release_queue();
        break;
    case FB_WAIT_QUEUE:
        g_input.feedback_waiting = true;
        break;
    case FB_WAIT_STEP:
        g_input_feedback_busy = true;
        break;
    case FB_QUEUE:
    case FB_QUEUE_AND_RELEASE:
        if ((g_input.queue_tail + 1) % COMMAND_QUEUE == g_input.queue_head) {
            if (!g_input.disabled)
                g_input.disabled = 1;
            *queue_full = true; /* the script waits */
            break;
        }
        queue_current();
        if ((op & FB_OP_MASK) == FB_QUEUE_AND_RELEASE)
            release_queue();
        break;
    case FB_EXECUTE:
        /* the command may run a nested input_poll (a dialog); the script continues after the opcode, or repeats it
           when the command waits */
        g_input.script = p;
        if (!command_execute(&g_input.current))
            p = g_input.script ? g_input.script - 1 : p - 1;
        else if (g_input_command_stop)
            release_queue();
        break;
    default:
        return feedback_draw_op(f, op, p);
    }
    return p;
}

/* Runs the click feedback script for elapsed timer ticks: pictures of pressed buttons, then the command. */
static void run_click_feedback(int elapsed)
{
    if (g_input.script_delay) {
        if (elapsed < g_input.script_delay) {
            g_input.script_delay -= elapsed;
            return;
        }
        g_input.script_delay = 0;
    }
    if (g_input.disabled || g_input.feedback_waiting || g_input_feedback_busy || !g_input.script)
        return;
    Feedback f = {0};
    bool queue_full = false;
    uint8_t op;
    do {
        op = g_input.script[0];
        const uint8_t *p = feedback_run_op(&f, op, g_input.script + 1, &queue_full);
        if (queue_full)
            break;
        g_input.script = p;
        if (op & FB_END) {
            g_input.script = NULL;
            break;
        }
    } while (op & FB_RUN_ON);
    if (f.opened)
        offscreen_flush(&f.o, true);
}

/* The open viewport screen takes the keys and the mouse events until it closes; the mouse drags the automap. */
static void forward_to_screen(void)
{
    uint16_t key;
    while (g_advanced_screen && (key = input_read_key()))
        advanced_screen_event(INPUT_KEY, key, 0);
    while (g_advanced_screen && g_input.event_count) {
        int type = g_input.events[g_input.event_head].type;
        int16_t x = g_input.events[g_input.event_head].x;
        int16_t y = g_input.events[g_input.event_head].y;
        g_input.event_count--;
        g_input.event_head = (g_input.event_head + 1) % EVENT_RING;
        advanced_screen_event(type, x, y);
    }
    if (g_advanced_screen) {
        advanced_automap_drag(g_input_mouse_x, g_input_mouse_y);
        return;
    }
    input_flush(); /* the input after the close key does not reach the game */
    uint16_t pending = advanced_take_pending_key();
    if (pending)
        input_key_down(pending);
}

/* The command of the next waiting event, 0 when the events run out; g_input.current takes the command, its position and
   the rect of its zone. */
static uint16_t next_event_command(void)
{
    uint16_t command = 0;
    while (!command && g_input.event_count) {
        int type = g_input.events[g_input.event_head].type;
        int16_t x = g_input.events[g_input.event_head].x;
        int16_t y = g_input.events[g_input.event_head].y;
        g_input.event_count--;
        g_input.event_head = (g_input.event_head + 1) % EVENT_RING;
        int16_t rect[RECT_SIZE] = {0};
        if (type < INPUT_KEY) {
            if (type & INPUT_LEFT_RELEASED)
                command = CMD_CANCEL_CAPTURE;
            else if (type & EVENT_CLICK_TYPES) {
                if (type == INPUT_BUTTON_LEFT)
                    area_end_damage_hold(x, y);
                if (!input_right_click_cancels_dialog(type))
                    command = input_click_command(g_input.mode, x, y, type, rect);
            }
        } else if (type == INPUT_KEY) {
            command = input_key_command(g_input.mode, (uint16_t)x, rect);
        } else if (type == EVENT_ENTER_AREA) {
            command = CMD_DRAG_CANCEL;
        } else if (type == EVENT_LEAVE_AREA) {
            command = CMD_CANCEL_PUSH;
        }
        if (command) {
            g_input.current.x = g_input_match.x;
            g_input.current.y = g_input_match.y;
            memcpy(g_input.current.rect, rect, sizeof rect);
            g_input.current.command = command;
        }
    }
    return command;
}

void input_poll(void)
{
    int elapsed = (int)(g_main_loop.timer_ticks - g_input.last_ticks);
    if (!elapsed)
        return;
    g_input.last_ticks = g_main_loop.timer_ticks;
    if (g_advanced_screen) {
        forward_to_screen();
        return;
    }
    if (!g_input.disabled && !g_input.feedback_waiting && !g_input_feedback_busy) {
        while (!g_input.script) {
            for (int i = 0; i < g_input.key_count; i++)
                input_enqueue_key(g_input.keys[i]);
            g_input.key_count = 0;
            g_input.events_locked = true;
            uint16_t command = next_event_command();
            g_input.events_locked = false;
            input_flush_deferred_click();
            if (!command)
                break;
            g_input.script = input_click_feedback_script(&g_input.current);
            elapsed = 0;
        }
    }
    run_click_feedback(elapsed);
}

void input_reset_states(void)
{
    game_release_eye_and_mouth();
    move_set_push_mode(false);
    g_input_feedback_busy = false;
    g_input.feedback_waiting = false;
    g_input.script = NULL;
    game_clear_drag();
}

/* ---- Movement arrows ---- */

void input_cancel_push_mode(void)
{
    if (!g_party.push_mode)
        return;
    move_set_push_mode(false);
    ui_draw_movement_arrows();
}

/* Draws a movement arrow released or pressed. */
static void show_move_arrow(int entry, int zone, bool pressed)
{
    g_input.arrow_pressed = pressed;
    g_input.arrow_zone = zone;
    g_input.arrow_entry = entry;
    Offscreen o;
    offscreen_create(&o, zone, true);
    gfx_fill_bitmap(o.bitmap, g_ui_colors[UI_BLACK]);
    offscreen_draw_gdat_image(
        &o, (GdatKey){CAT_INTERFACE, UI_INDEX_ARROWS, entry + (pressed ? ARROW_PRESSED_OFFSET : 0)}, zone, GFX_OPAQUE);
    offscreen_flush(&o, true);
    draw_free_temp();
}

void input_release_move_arrow(void)
{
    /* the push arrow under the mouse stays pressed; after the push mode the arrows are already drawn released */
    if (!g_input.arrow_pressed || (g_party.push_mode && g_party.push_arrow != PUSH_ARROW_NONE))
        return;
    if (g_party.push_mode)
        show_move_arrow(g_input.arrow_entry, g_input.arrow_zone, false);
    else
        g_input.arrow_pressed = false;
}

void input_update_viewport_arrow(void)
{
    /* by push arrow: the movement arrow entry and zone, the turn from the party direction to the push and to the
       party step (0: ahead) */
    static const struct {
        uint8_t entry;
        uint16_t zone;
        int8_t push_turn, step_turn;
    } push_arrows[PUSH_ARROW_COUNT] = {
        {GFX_PUSH_ARROWS + 4,  ZONE_ARROW_FIRST + 2, 0,          0         },
        {GFX_PUSH_ARROWS + 2,  ZONE_ARROW_FIRST + 1, TURN_RIGHT, 0         },
        {GFX_PUSH_ARROWS + 6,  ZONE_ARROW_FIRST + 3, TURN_BACK,  TURN_RIGHT},
        {GFX_PUSH_ARROWS + 8,  ZONE_ARROW_FIRST + 4, TURN_BACK,  TURN_BACK },
        {GFX_PUSH_ARROWS + 10, ZONE_ARROW_FIRST + 5, TURN_BACK,  TURN_LEFT },
        {GFX_PUSH_ARROWS,      ZONE_ARROW_FIRST,     TURN_LEFT,  0         },
    };
    if (!g_view_arrows_enabled || g_party.push_pending_group != THING_NONE)
        return;
    int16_t r[RECT_SIZE];
    if (!zone_get_rect(ZONE_VIEWPORT, r))
        return;
    int x = g_input_mouse_x - r[RECT_X];
    int y = g_input_mouse_y - r[RECT_Y];
    move_set_push_arrow(PUSH_ARROW_NONE);
    input_release_move_arrow();
    if (x < 0 || y < 0 || x >= VIEWPORT_WIDTH || y >= VIEWPORT_HEIGHT)
        return;
    int dx = x - VIEWPORT_WIDTH / 2;
    int dy = y - VIEWPORT_HEIGHT / 2;
    int arrow;
    if (abs(dx) < ARROW_CENTER_X) {
        if (abs(dy) < ARROW_CENTER_Y)
            return;
        arrow = dy >= 0 ? PUSH_ARROW_DOWN : PUSH_ARROW_UP;
    } else if (dx < 0) {
        arrow = abs(dy) < ARROW_CENTER_Y ? PUSH_ARROW_LEFT : dy >= 0 ? PUSH_ARROW_DOWN_LEFT : PUSH_ARROW_UP;
    } else {
        arrow = abs(dy) < ARROW_CENTER_Y ? PUSH_ARROW_RIGHT : dy >= 0 ? PUSH_ARROW_DOWN_RIGHT : PUSH_ARROW_UP;
    }
    int step = dir_turn(push_arrows[arrow].step_turn, g_game.party_dir);
    if (dungeon_square_blocks_movement(
            dungeon_square_at(dungeon_step((Location){g_game.party_x, g_game.party_y}, step))))
        return;
    Location front = dungeon_step((Location){g_game.party_x, g_game.party_y}, g_game.party_dir);
    if (!creature_can_be_pushed(front, dir_turn(push_arrows[arrow].push_turn, g_game.party_dir), false, NULL))
        return;
    show_move_arrow(push_arrows[arrow].entry, push_arrows[arrow].zone, true);
    move_set_push_arrow(arrow);
}
