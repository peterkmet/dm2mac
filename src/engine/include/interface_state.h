/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_INTERFACE_STATE_H
#define DM2_ENGINE_INTERFACE_STATE_H
/* Prefixes: none */

/* The interface state that the rules and the drawing share: the panels, the input mode, the mouse. */

#include <stdbool.h>
#include <stdint.h>

/* Interface state that the drawing reads. */
typedef struct {
    /* ordinal as in the original: the input tree compares it with node arguments */
    int16_t inventory; /* champion ordinal (champion + 1) of the open inventory, 0 none */
    /* ordinal as in the original: the input tree compares it with node arguments */
    uint16_t candidate;        /* champion ordinal of a champion that is being recruited, 0 none */
    bool screen_needs_refresh; /* the next drawing of the champion states redraws the whole open inventory */
    uint16_t buttons;          /* inventory buttons that show pressed */
    bool eye;                  /* the eye of the inventory is held */
    bool mouth;                /* the mouth of the inventory is held */
    bool push_mode;            /* the movement arrows show the push arrows */
    int16_t panel_mode;        /* what the inventory panel shows: a PanelMode */
} UiState;

/* What the inventory panel shows (g_ui.panel_mode). */
enum PanelMode {
    PANEL_MODE_NONE = 0,
    PANEL_MODE_FOOD = 1,
    PANEL_MODE_SKILLS = 2,
    PANEL_MODE_OBJECT = 3,
    PANEL_MODE_SCROLL = 5,
    PANEL_MODE_MOUTH = 7,
    PANEL_MODE_NAME = 8 /* the name and title fields while a name changes */
};

extern UiState g_ui;

/* Input modes: the root of the input tree that maps clicks and keys to commands. */
enum InputMode {
    INPUT_MODE_MENU,
    INPUT_MODE_PICTURE,
    INPUT_MODE_SLEEP,
    INPUT_MODE_PAUSE,
    INPUT_MODE_DIALOG,
    INPUT_MODE_MOVE,
    INPUT_MODE_PUSH,
    INPUT_MODE_RECRUIT,
    INPUT_MODE_INVENTORY,
    INPUT_MODE_NAME,
    INPUT_MODE_COUNT /* the number of input modes */
};
/* The last command stops the command processing of this tick. */
extern bool g_input_command_stop;
/* A champion portrait or an object is dragged, 0 none. */
/* ordinal as in the original: the input tree compares it with node arguments */
extern int g_input_drag_state;
/* Mouse event types: buttons (bit 1 left, bit 0 right), a released left button (only while the eye or the mouth is
   held or the automap is open), a key. */
#define INPUT_BUTTON_RIGHT  0x01
#define INPUT_BUTTON_LEFT   0x02
#define INPUT_LEFT_RELEASED 0x04
#define INPUT_KEY           0x20
/* The mouse buttons that are down: INPUT_BUTTON_* bits. */
extern int g_input_mouse_buttons;

#endif
