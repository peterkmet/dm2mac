/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_HOOKS_H
#define DM2_ENGINE_HOOKS_H
/* Prefixes: none */

/* Calls of the game rules into the user interface, for events that must run the interface inside the rule.
   ui/ui_hooks.c sets every member. */

#include "sound_request.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    void (*print_message)(int color, const char *text); /* a line in the message area */
    void (*print_newline)(void);                        /* the next message starts on a new row */
    bool (*play_sound)(const Sound *s, uint8_t volume, int8_t lateral,
                       int8_t forward); /* starts a sound in a free voice */
    bool (*zone_offset)(int from, int to, int percent, int16_t *dx,
                        int16_t *dy);               /* the offset percent of the way between two layout zones */
    int16_t *(*zone_rect)(int zone, int16_t *rect); /* the screen rectangle of a layout zone */
    void (*update_action_area)(int tick);           /* redraws what changed in the action area */
    void (*reset_action_area)(void);                /* clears the drawn state of the action area */
    void (*reset_action_champion)(int champ);       /* clears the drawn state of the action area for a champion */
    void (*render_hand_icon)(void);                 /* draws the leader hand object into the cursor */
    void (*draw_interface_image)(int category, int index, int entry,
                                 int zone);        /* an interface picture in a screen zone */
    void (*enter_map_view)(int16_t map);           /* the view loads the graphics of the map that the party enters */
    void (*flush_input)(void);                     /* drops the waiting clicks and keys */
    void (*poll_input)(void);                      /* reads the clicks and keys that came since the last read */
    void (*clear_viewport)(void);                  /* fills the cleared band of the viewport */
    void (*set_input_mode)(int mode);              /* the clicks and keys map to the commands of an INPUT_MODE_* */
    void (*show_view)(void);                       /* shows the dungeon view again (recruit) */
    void (*hide_view)(void);                       /* hides the dungeon view (recruit) */
    void (*present_viewport)(void);                /* copies the view picture to the screen */
    void (*refresh_input_mode)(void);              /* recomputes the active zones of the input mode */
    void (*set_wait)(int steps);                   /* the commands wait for steps timer ticks */
    void (*set_champion_cursor)(int champ);        /* the dragged champion cursor shows the face of champ */
    void (*toggle_inventory)(int champ);           /* opens the inventory of champ, or closes it (champ 4: close) */
    void (*draw_champion_state)(int champ);        /* draws what changed of the status box of champ */
    void (*draw_all_champion_states)(int tick);    /* draws all status boxes, the inventory, the action area */
    void (*draw_movement_arrows)(void);            /* draws the movement arrows */
    void (*fill_screen_zone)(int zone, int color); /* fills a screen zone with a palette color */
    void (*draw_viewport_image)(int category, int index, int entry, int zone,
                                int transparent);            /* a picture in a zone of the view */
    void (*complete_game)(void);                             /* the party finished the game */
    void (*draw_dungeon)(int16_t dir, int16_t x, int16_t y); /* draws the view of the party at (x, y) facing dir */
} Hooks;

extern Hooks g_hooks;

#endif /* DM2_ENGINE_HOOKS_H */
