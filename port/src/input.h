#ifndef INPUT_H
#define INPUT_H
#include <stdint.h>

/* Input modes: the root of the input tree that maps clicks and keys to commands. */
enum input_mode { INPUT_MODE_SLEEP = 2, INPUT_MODE_PAUSE = 3, INPUT_MODE_MOVE = 5, INPUT_MODE_PUSH = 6, INPUT_MODE_INVENTORY = 8 };
/* Mouse event types: buttons (bit 1 left, bit 0 right), a released right button, a key. */
enum { INPUT_BUTTON_RIGHT = 1, INPUT_BUTTON_LEFT = 2, INPUT_RIGHT_RELEASED = 4, INPUT_KEY = 0x20 };
/* Key code bits of the original keyboard layer: a scan code, extended keys, shift, control, alt. */
#define KEY_EXTENDED 0x1000
#define KEY_SHIFT 0x0200
#define KEY_CONTROL 0x0400
#define KEY_ALT 0x0800

/* A command and the click that made it: position and the rectangle of its zone. */
typedef struct {
    int16_t x, y;
    uint16_t command;
    int16_t rect[4];
} Command;

/* The last command stops the command processing of this tick (the original g_command_stop). */
extern int input_command_stop;
/* A click feedback script waits for the next game step. */
extern int input_feedback_busy;
/* A champion portrait or an object is dragged (Step 15), 0 none. */
extern int input_drag_state;

/* Builds the input tables; call once. */
void input_init(void);
/* Selects the input tree of mode (move, inventory, sleep, pause, ...); a new mode clears the pending input. */
void input_set_mode(int mode);
/* The command of a click (key 0) or of a key in mode, and the rectangle of its zone; 0 for none. */
uint16_t input_find_command(int mode, int x, int y, int buttons, int key, int16_t *rect);
/* The mode of before the last input_push_mode returns. */
void input_push_mode(int mode);
void input_pop_mode(void);
/* Recomputes the active zones of the current mode. */
void input_refresh_mode(void);
/* A mouse button event at (x, y) of the screen (type: INPUT_BUTTON_* bits or INPUT_RIGHT_RELEASED). */
void input_mouse_click(int x, int y, int type);
/* A key press of the keyboard (key code with KEY_* bits); input_poll reads it. */
void input_key_down(uint16_t key);
/* Clears the pending keys, clicks, and commands. */
void input_flush(void);
/* A click on a champion hand (status box) or a champion icon (party icons) becomes the command of the champion
   in that cell: the hand action when the hand is ready and has actions; near the icon edges toward the party
   center the leader choice, else the icon command when the spell is ready; else no command (0). */
void input_refine_champion_click(Command *c);
/* Turns the waiting events into commands and runs the click feedback. */
void input_poll(void);
/* Ends the push mode, the click feedback, and a drag. */
void input_reset_states(void);
/* Ends the push mode and redraws the movement arrows. */
void input_cancel_push_mode(void);
/* Redraws the released movement arrow. */
void ui_release_move_arrow(void);
/* Executes the queued commands until one stops the processing. */
void command_process_queue(void);
/* Executes one command; returns 0 when it must wait. */
int command_execute(Command *c);
/* The commands of later steps: hands, slots, spells, recruiting, dialogs (empty until then). */
void command_execute_later(const Command *c);
/* A click in the dungeon view: walls, doors, objects, creatures. */
void view_click_in_viewport(int x, int y);

#endif
