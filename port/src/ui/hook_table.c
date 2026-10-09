/* The user interface functions that the game rules call (engine/hooks.h). */
#include "hooks.h"
#include "text.h"

Hooks g_hooks = {
    .print_message = text_print_message,
    .print_newline = text_print_newline,
};
