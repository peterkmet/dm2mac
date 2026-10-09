#ifndef HOOKS_H
#define HOOKS_H
#include <stdint.h>
#include "sound_request.h"

/* Calls of the game rules into the user interface, for events that must run the interface inside the rule.
   ui/hook_table.c sets every member. */
typedef struct {
    void (*print_message)(int color, const char *text);  /* a line in the message area */
    void (*print_newline)(void);                         /* the next message starts on a new row */
    int (*play_sound)(const Sound *s, uint8_t volume, int8_t lateral, int8_t forward);  /* starts a sound in a free voice */
} Hooks;

extern Hooks g_hooks;

#endif
