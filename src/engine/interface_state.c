/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The interface state that the rules and the drawing share. */
#include "interface_state.h"

UiState g_ui;
bool g_input_command_stop;
int g_input_drag_state;
int g_input_mouse_buttons;
