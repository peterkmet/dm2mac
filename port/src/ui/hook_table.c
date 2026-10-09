/* The user interface functions that the game rules call (engine/hooks.h). */
#include "hooks.h"
#include "sound.h"
#include "text.h"

Hooks g_hooks = {
    .print_message = text_print_message,
    .print_newline = text_print_newline,
    .play_sound = sound_play,
};
