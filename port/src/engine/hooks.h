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
    int (*zone_offset)(int from, int to, int percent, int16_t *dx, int16_t *dy);  /* the offset percent of the way between two layout zones */
    int16_t *(*zone_rect)(int zone, int16_t *rect);      /* the screen rectangle of a layout zone */
    void (*update_action_area)(int tick);                /* redraws what changed in the action area */
    void (*reset_action_area)(void);                     /* clears the drawn state of the action area */
    void (*reset_action_champion)(int champ);            /* clears the drawn state of the action area for a champion */
    void (*render_hand_icon)(void);                      /* draws the leader hand object into the cursor */
    void (*draw_interface_image)(int category, int index, int entry, int zone);  /* an interface picture in a screen zone */
} Hooks;

extern Hooks g_hooks;

#endif
