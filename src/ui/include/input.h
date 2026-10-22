/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_INPUT_H
#define DM2_UI_INPUT_H
/* Prefixes: input_ command_ kbd_ rect_ */

/* Mouse and keyboard input: key codes, rectangles and the command queue. */

#include "game.h"
#include <stdbool.h>
#include <stdint.h>

/* Key code bits of the original keyboard layer: a scan code, extended keys, shift, control, alt. */
#define KEY_EXTENDED  0x1000
#define KEY_SHIFT     0x0200
#define KEY_CONTROL   0x0400
#define KEY_ALT       0x0800
/* Scan codes of the keys that edit a text. */
#define KEY_BACKSPACE 0x0e
#define KEY_TAB       0x0f
#define KEY_ENTER     0x1c

/* Commands (command_execute). */
enum {
    CMD_INVENTORY_FIRST = 7,
    CMD_INVENTORY_LEADER = 0x0b,
    CMD_LEADER_FIRST = 0x10,
    CMD_LEADER_LAST = 0x13,
    CMD_SLOT_FIRST = 0x14,
    CMD_SLOT_LAST = 0x41,
    CMD_SLOT_INVENTORY_FIRST = 0x1c, /* + slot: a slot of the open inventory, after the hands of the 4 status boxes */
    CMD_SLOT_CHEST_FIRST = 0x3a,
    /* + slot - SLOT_CONTAINER_FIRST: a slot of the open chest */ CMD_VIEWPORT = 0x50,
    CMD_LEADER_INVENTORY = 0x52,
    CMD_ROTATE_LEFT = 0x5d,
    CMD_ROTATE_RIGHT = 0x5e,
    CMD_SLEEP = 0x8e,
    CMD_WAKE = 0x8f,
    CMD_PAUSE = 0x90,
    CMD_RESUME = 0x91,
    CMD_QUIT = 0xe0,
    CMD_SAVE = 0x8c,
    CMD_VOLUME = 0x8d,
    CMD_EDIT_CHAMPION_NAME = 0x48,
    CMD_NEW_GAME = 0xd7,
    CMD_NEW_GAME_B = 0xd8,
    CMD_LOAD_GAME = 0xd9,
    CMD_MENU_PICTURE = 0xda,
    CMD_CANCEL_PUSH = 0xe1,
    CMD_CANCEL_CAPTURE = 0xe3,
    CMD_TEXT_FIRST = 0xa5,
    CMD_TEXT_LAST = 0xc6,
    CMD_LAST = 0xf2,
    CMD_PARTY_ICON_FIRST = 0x5f,
    CMD_PARTY_ICON_LAST = 0x62,
    CMD_HAND_FIRST = 0x74,
    CMD_HAND_LAST = 0x7b,
    CMD_STORE_FIRST = 0xea,
    CMD_STORE_LAST = 0xed,
    CMD_DRAG_FIRST = 0x7d,
    CMD_DRAG_CANCEL = 0x81,
    CMD_ACTION_CANCEL = 0x70,
    CMD_ACTION_LAST = 0x73,
    CMD_MOUTH = 0x46,
    CMD_EYE = 0x47,
    CMD_COIN_FIRST = 0xe4,
    CMD_COIN_LAST = 0xe9,
    CMD_RUNE_FIRST = 0x65,
    CMD_RUNE_LAST = 0x6a,
    CMD_REMOVE_RUNE = 0x6b,
    CMD_CAST_SPELL = 0x6c,
    CMD_MAP_OPTION_FIRST = 0x56,
    CMD_MAP_OPTION_LAST = 0x59,
    CMD_EDIT_NAME = 0x49,
    CMD_EDIT_TITLE = 0x4a,
    CMD_EDIT_CANCEL = 0x4b, /* the buttons of the name editor */
    CMD_SOUND_VOLUME_FIRST = 0x95,
    CMD_MUSIC_VOLUME_FIRST = 0x9d,
    CMD_MUSIC_VOLUME_LAST = 0xa4, /* settings 0 to 7 */
    CMD_PICTURE_CLICK = 0xef,     /* a click on a picture of INPUT_MODE_PICTURE */
    CMD_QUICK_SAVE = CMD_LAST + 1,
    CMD_QUICK_LOAD,
    CMD_AUTOMAP /* the advanced mode: no original command has these numbers */
};

/* A command and the click that made it: position and the rectangle of its zone. */
typedef struct {
    int16_t x, y;
    uint16_t command;
    int16_t rect[4];
} Command;

/* The last matched command entry: the dialogs read it. */
#define DIALOG_NO_CHOICE 0xff /* a dialog waits while its choice has this value */
typedef struct {
    uint16_t zone;   /* the zone of the entry, ZONE_NONE none */
    int16_t origin;  /* the zone that the entry zone is relative to, -1 none */
    uint16_t key;    /* the key code of a key command, 0 for a click */
    int16_t choice;  /* the place of the command in its list, from 1; 0 none */
    int16_t x, y;    /* the click position */
    uint16_t value;  /* the command */
    int16_t rect[4]; /* the screen rect of the zone of the entry */
} InputMatch;
extern InputMatch input_match;

/* A click feedback script waits for the next game step. */
extern int input_feedback_busy;
/* A command waits (in.wait) after the last one: the input queue takes no new command. */
bool input_is_waiting(void);
/* No pending key, mouse event, deferred click, queued command, or running click feedback script. */
bool input_is_idle(void);

/* Builds the input tables; call once. */
void input_init(void);
/* Selects the input tree of mode (move, inventory, sleep, pause, ...); a new mode clears the pending input. */
void input_set_mode(int mode);
/* The cursor image at (x, y) of the screen (the original mouse_move_event): the held object or the hand over a
   cursor zone of kind 1, the dragged champion over a zone of kind 2, else the arrow. */
enum { CURSOR_ARROW, CURSOR_HAND, CURSOR_OBJECT, CURSOR_CHAMPION };
int input_cursor_at(int x, int y);
/* A cursor image (mouse_set_cursor_image): bpp 4 (high nibble first) or 8, the hot spot at the pointer, the clear
   color number, and the palette colors of the color numbers (NULL: the numbers are palette colors). */
typedef struct {
    const uint8_t *bits, *palette;
    int bpp, width, height, hot_x, hot_y, key;
} CursorImage;
/* The image of a cursor of input_cursor_at. */
CursorImage input_cursor_image(int cursor);
/* Draws the cursor image of input_cursor_at(x, y) with its hot spot at (x, y) into the screen dst. */
void input_draw_cursor(uint8_t *dst, int x, int y);
/* Draws the face of champ into the dragged champion cursor (champ_drag_click). */
void input_set_champion_cursor(int champ);
/* The current input mode. */
int input_get_mode(void);
/* The command of a click (key 0) or of a key in mode, and the rectangle of its zone; 0 for none. */
uint16_t input_find_command(int mode, int x, int y, int buttons, int key, int16_t *rect);
/* The mode of before the last input_push_mode returns. */
void input_push_mode(int mode);
void input_pop_mode(void);
/* Recomputes the active zones of the current mode. */
void input_refresh_mode(void);
/* A mouse button event at (x, y) of the screen (type: INPUT_BUTTON_* bits or INPUT_LEFT_RELEASED). */
void input_mouse_click(int x, int y, int type);
/* A mouse button (INPUT_BUTTON_*) goes down or up at (x, y) of the screen. The release of the left button ends the
   mouse capture of the eye or the mouth (the original event 4). */
void input_mouse_down(int x, int y, int button);
void input_mouse_up(int x, int y, int button);
/* A key press of the keyboard (key code with KEY_* bits); input_poll reads it. */
void input_key_down(uint16_t key);
/* Clears the pending keys, clicks, and commands. */
void input_flush(void);
/* A click on a champion hand (status box) or a champion icon (party icons) becomes the command of the champion
   in that cell: the hand action when the hand is ready and has actions; near the icon edges toward the party
   center the leader choice, else the icon command when the spell is ready; else no command (0). */
void input_refine_champion_click(Command *c);
/* A left or right click on the zone of command in the current mode, as if the player clicked it. */
void input_simulate_command_click(uint16_t command);
/* The screen rect of the zone that input_simulate_command_click clicks for command; false when the current mode has
   none. */
bool input_command_rect(uint16_t command, int16_t *rect);
/* Takes a key from the keys that the input has not read yet; 0 for none. */
uint16_t input_read_key(void);
/* The character of a key code (US layout), 0 for none. */
char kbd_key_to_ascii(uint16_t key);
/* Centers a w x h rect in src. */
void rect_center_in(int16_t *dst, const int16_t *src, int w, int h);
/* Turns the waiting events into commands and runs the click feedback. */
void input_poll(void);
/* Ends the push mode, the click feedback, and a drag. */
void input_reset_states(void);
/* Ends the push mode and redraws the movement arrows. */
void input_cancel_push_mode(void);
/* The commands wait for steps timer ticks (the death of a boss near the party). */
void input_set_wait(int steps);
/* The mouse position on the screen (main.c sets it before each tick). */
extern int input_mouse_x, input_mouse_y;
/* The push arrow under the mouse in the viewport while the party can push a creature (g_party.push_arrow). */
void input_update_viewport_arrow(void);
/* Redraws the released movement arrow. */
void input_release_move_arrow(void);
/* Executes the queued commands until one stops the processing. */
void command_process_queue(void);
/* Executes one command; returns 0 when it must wait. */
int command_execute(Command *c);

#endif /* DM2_UI_INPUT_H */
