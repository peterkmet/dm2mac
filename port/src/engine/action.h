#ifndef ACTION_H
#define ACTION_H
#include <stdint.h>
#include "save.h"
#include "ui.h"

/* Prefixes: action_ */

/* What the action area shows. */
enum action_mode {
    ACTION_MODE_HANDS, ACTION_MODE_MENU, ACTION_MODE_SPELL, ACTION_MODE_MINION_MAP, ACTION_MODE_MONEY_BOX,
    ACTION_MODE_CONTAINER, ACTION_MODE_RESULT, ACTION_MODE_NONE = -1
};
#define ACTION_HAND_SPELL 2
#define SPELL_RUNES_PER_SET 6
#define SPELL_RUNE_GLYPH_FIRST 0x60   /* the font character of the first rune */
/* Results of an action (action.result < 0). */
enum { RESULT_NOTHING = -1, RESULT_NO_AMMO = -2, RESULT_FIZZLE = -3, RESULT_NEEDS = -4, RESULT_CANT = -6 };

/* Action area state; Step 14 (clicks) and Step 15 (actions) change it. */
typedef struct {
    int16_t champion;           /* champion ordinal whose hand or spell panel is open, 0: the hands of the party */
    int16_t hand;               /* the open hand, ACTION_HAND_SPELL for the spell panel */
    int16_t mode;               /* enum action_mode */
    int16_t dirty;              /* the whole area redraws */
    int16_t redraw_delay;       /* updates to wait before the open panel redraws */
    uint16_t flags;             /* minion map flags */
    int16_t result;             /* the result to show: damage (>= 0) or a failure (< 0), 0 none */
    int16_t result_flag;        /* for result -2: 0 shows "no ammunition" with the champion's hand picture */
    uint16_t result_ammo;       /* for result -2: the ammunition kinds (weapon attribute 5) that the action needs */
    int16_t flash;              /* champion ordinal whose icon flashes (> 0 on, < 0 off), 0 none */
    int16_t flash_hand;
    uint8_t map_item;           /* GDAT index of the open minion map */
    uint16_t open_items[8];     /* the items of the open container */
    uint16_t open_container;    /* THING_NONE none */
    uint16_t hand_items[CHAMPION_MAX][2];   /* the hand items that the hand icons show */
} ActionArea;

extern ActionArea action;

/* Clears the action area state and its caches. */
void action_reset(void);
/* Clears the drawn state of the action area for champion champ. */
void action_reset_champion(int champ);
/* The item in the open hand, as the action area shows it. */
uint16_t action_open_hand_item(void);
/* Closes the open hand or spell panel. */
void action_area_close(void);
/* Redraws what changed in the action area (tick: one game tick passed). */
void action_update_area(int tick);
/* Opens the action area bitmap (clear: it starts black) and returns it. */
Offscreen *action_area_begin_draw(int clear);
/* Draws the runes that the champion entered in the spell panel (clear: their background first). */
void action_draw_entered_runes(int clear);
/* Draws the button and the name of action i of the action menu or of a minion map. */
void action_draw_name(int i, int highlight);
/* Draws the hand icon of champ in the action area (flash: pressed). */
void action_draw_hand_icon(int champ, int hand, int flash);
/* Draws the spell icon of champ in the party icons (flash: pressed). */
void action_draw_spell_icon(int champ, int flash);

/* The action area of a hand or of the spells (champ_action.c). */
/* Opens the action menu of a hand of champ (0 to 3) in the action area. */
void action_area_open_hand(int champ, int hand);
/* Opens the spell panel of champ in the action area. */
void action_area_open_spells(int champ);

#endif
