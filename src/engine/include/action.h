/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_ACTION_H
#define DM2_ENGINE_ACTION_H
/* Prefixes: action_ */

/* The action area of the panel: its modes, the action hands, and the spell and money box views. */

#include "save.h"
#include <stdint.h>

/* What the action area shows. */
enum action_mode {
    ACTION_MODE_HANDS,
    ACTION_MODE_MENU,
    ACTION_MODE_SPELL,
    ACTION_MODE_MINION_MAP,
    ACTION_MODE_MONEY_BOX,
    ACTION_MODE_CONTAINER,
    ACTION_MODE_RESULT,
    ACTION_MODE_NONE = -1
};
#define ACTION_HAND_SPELL      2
#define UI_INDEX_SPELL         5 /* the spell panel pictures: GDAT (1, 5, 1, entry) */
#define SPELL_RUNES_PER_SET    6
#define SPELL_RUNE_GLYPH_FIRST 0x60 /* the font character of the first rune */
/* Results of an action (action.result < 0). */
enum {
    RESULT_NOTHING = -1,
    RESULT_NO_AMMO = -2,
    RESULT_CANT = -6,
    RESULT_UNKNOWN_SPELL = -3,
    RESULT_WIZARD_FAILED = -4,
    RESULT_PRIEST_FAILED = -5 /* a failed cast */
};

/* Action area state; Step 14 (clicks) and Step 15 (actions) change it. */
typedef struct {
    int16_t champion;     /* champion ordinal whose hand or spell panel is open, 0: the hands of the party */
    int16_t hand;         /* the open hand, ACTION_HAND_SPELL for the spell panel */
    int16_t mode;         /* enum action_mode */
    int16_t dirty;        /* the whole area redraws */
    int16_t redraw_delay; /* updates to wait before the open panel redraws */
    uint16_t flags;       /* minion map flags */
    int16_t result;       /* the result to show: damage (>= 0) or a failure (< 0), 0 none */
    int16_t result_flag;  /* for result -2: 0 shows "no ammunition" with the champion's hand picture */
    uint16_t result_ammo; /* for result -2: the ammunition kinds (weapon attribute 5) that the action needs */
    int16_t flash;        /* champion ordinal whose icon flashes (> 0 on, < 0 off), 0 none */
    int16_t flash_hand;
    uint8_t map_item;                     /* GDAT index of the open minion map */
    uint16_t open_items[8];               /* the items of the open container */
    uint16_t open_container;              /* THING_NONE none */
    uint16_t hand_items[CHAMPION_MAX][2]; /* the hand items that the hand icons show */
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
/* Takes the items out of the chest container into the open container slots. */
void action_open_container(uint16_t container);
/* One game tick: the busy hands count down; a free hand reloads. */
void action_tick_hands(void);

/* The action area of a hand or of the spells (champ_action.c). */
/* Opens the action menu of a hand of champ (0 to 3) in the action area. */
void action_area_open_hand(int champ, int hand);
/* Opens the spell panel of champ in the action area. */
void action_area_open_spells(int champ);

#endif /* DM2_ENGINE_ACTION_H */
