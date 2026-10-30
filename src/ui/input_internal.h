/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_INPUT_INTERNAL_H
#define DM2_UI_INPUT_INTERNAL_H
/* Prefixes: input_ rect_ */

/* The shared private declarations of the input module (input.c, input_tree.c, input_feedback.c): the sizes, the
   record types and the tables of the input tree, the input state, and the functions that the files call. */

#include <stdbool.h>
#include <stdint.h>
#include "champion.h"
#include "input.h"
#include "ui.h"
/* Sizes of the input tables of SKULL.EXE. */
#define INPUT_CHILD_COUNT   83
#define INPUT_ZONE_COUNT    62
#define INPUT_COMMAND_COUNT 264
#define INPUT_KEY_COUNT     121
#define CURSOR_ZONE_COUNT   18

/* Sizes of the event ring, the command queue, and the key buffer. */
#define EVENT_RING    11
#define COMMAND_QUEUE 3
#define KEY_BUFFER    16

/* Mouse event types of the event ring besides the buttons. */
#define EVENT_ENTER_AREA 0x40
#define EVENT_LEAVE_AREA 0x60

/* Sizes of the cursor images that the state holds. */
#define OBJECT_SHADOW      2 /* the offset of the black shadow of the held object cursor */
#define OBJECT_CURSOR_SIZE (HAND_ICON_SIZE + OBJECT_SHADOW)

/* Relative cells of the squares of a view click. */
#define HALF_CELLS 2 /* relative cells 0 and 1: the front half of a square, 2 and 3: its back half */

typedef struct {
    uint8_t cond, arg;
    uint16_t link;
} InputNode;
typedef struct {
    uint16_t zone;
    uint8_t kind, priority;
} CursorZone;
typedef struct {
    uint16_t command, zone;
    uint8_t buttons, flags;
} InputCommand;

typedef struct {
    uint8_t child_list_start[INPUT_CHILD_COUNT]; /* position of each child list */
    int16_t command_list_start[INPUT_COMMAND_COUNT];
    int16_t key_list_start[INPUT_KEY_COUNT];
    enum InputMode mode, saved_mode;
    struct {
        int16_t type, x, y;
    } events[EVENT_RING];
    int event_head, event_tail, event_count;
    bool events_locked;
    bool deferred;
    int16_t deferred_x, deferred_y;
    int deferred_type;
    bool dropped_left; /* a left click did not fit: a right release still may */
    uint16_t keys[KEY_BUFFER];
    int key_count;
    Command current; /* the command of the last event */
    Command queue[COMMAND_QUEUE];
    int queue_head, queue_tail;
    int disabled;          /* commands queued by feedback scripts that wait */
    int wait;              /* ticks without new commands */
    const uint8_t *script; /* the running click feedback script */
    int script_delay;
    uint32_t last_ticks;
    bool feedback_waiting; /* the script waits for the next queue run */
    bool arrow_pressed;
    int arrow_zone, arrow_entry;
    bool zone_active[INPUT_ZONE_COUNT];     /* the input zones of the input tree of the mode */
    uint8_t cursor_list[CURSOR_ZONE_COUNT]; /* the shown cursor zones (from 1), by priority */
    int cursor_count;
    uint8_t champion_cursor[FACE_SIZE * FACE_SIZE];                 /* the dragged champion cursor */
    uint8_t champion_cursor_key;                                    /* its clear color */
    uint8_t object_cursor[OBJECT_CURSOR_SIZE * OBJECT_CURSOR_SIZE]; /* the held object cursor */
} InputState;

extern InputState g_input;
extern const InputNode g_input_mode_roots[INPUT_MODE_COUNT];
extern const CursorZone g_input_cursor_zones[CURSOR_ZONE_COUNT];

/* The champion in the cell relative to the party direction, -1 none. */
int input_party_cell_champion(int16_t cell);
/* The screen rectangle of an input zone. */
bool input_get_zone_rect(uint16_t zone, int16_t *rect);
/* True when the point is inside the rectangle. */
bool rect_contains_point(const int16_t *r, int16_t x, int16_t y);
/* The command entry of command in the input tree under node, NULL for none. As in the original, the walk of an entry
   list ends at the first entry without a command; a list needs that end entry. */
const InputCommand *input_find_command_entry(const InputNode *node, uint16_t command);
/* In the advanced mode a right click in a dialog with a cancel button chooses that button; true when it did. */
bool input_right_click_cancels_dialog(int type);

/* Queues the click that came while the events were locked. */
void input_flush_deferred_click(void);
/* Queues a key event when the event ring has space, and unlocks the events. */
void input_enqueue_key(uint16_t key);

#endif
