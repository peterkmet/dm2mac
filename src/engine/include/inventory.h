/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_INVENTORY_H
#define DM2_ENGINE_INVENTORY_H
/* Prefixes: inv_ */

/* The leader hand and the champion slots, and the effects of the items that a champion holds. */

#include <stdint.h>

/* Modes of champ_apply_item_effects: how an item changes the champion that holds it. */
#define EFFECT_MODE_LOAD        0    /* a loaded game: the bonuses of the items that the champion holds */
#define EFFECT_MODE_ADD         1    /* the item goes into a slot or the hand */
#define EFFECT_MODE_REMOVE      (-1) /* the item leaves a slot or the hand */
#define EFFECT_MODE_CONSUME     2    /* the champion eats or drinks the item */
#define EFFECT_MODE_CONSUME_END (-2) /* the timed effect of a consumed item ends */
#define EFFECT_MODE_RESTORE     3    /* a loaded game: a timed effect of a consumed item */
/* The leader hand icon: its size in pixels, its color numbers, and the color number that stays clear. */
#define HAND_ICON_SIZE          16
#define HAND_ICON_COLORS        16
#define HAND_ICON_KEY           12
/* The icon entry of no leader hand icon. */
#define HAND_ICON_NONE          0xff
/* The mouth picture of the inventory: GDAT (9, 0x5b, 1, entry). */
#define MOUTH_CATEGORY          0x09
#define MOUTH_INDEX             0x5b

/* The leader hand object as the original keeps it beside g_game.leader_hand. */
typedef struct {
    uint16_t attr;                                   /* item attribute 0 (flags) of the object */
    uint16_t weight;                                 /* its weight when it went into the hand */
    uint8_t icon;                                    /* the icon entry that the cursor shows, HAND_ICON_NONE none */
    uint8_t cursor[HAND_ICON_SIZE * HAND_ICON_SIZE]; /* the icon: color numbers 0 to 15, HAND_ICON_KEY clear */
    uint8_t palette[HAND_ICON_COLORS];               /* the palette colors of the color numbers */
} LeaderHand;

extern LeaderHand g_leader_hand;

/* The object of the leader hand that the cursor shows, THING_NONE none. */
uint16_t inv_cursor_object(void);
/* Prints the name of thing in the message area. */
void inv_show_object_name(uint16_t thing);
/* Updates the cursor when the icon of the animated leader hand object changes. */
void inv_update_hand_cursor(void);
/* A click on a slot: 0 to 7 the hands in the status boxes, then the inventory slots, then the open chest. The leader
   hand object and the item in the slot change places. */
void inv_click_slot(int16_t slot);
/* Puts the leader hand object into the first free pouch or pack slot of champ. */
void inv_store_leader_hand_object(int champ);
/* Puts thing into the first free slot of champ that takes it. */
void inv_put_in_free_slot(int champ, uint16_t thing);

#endif /* DM2_ENGINE_INVENTORY_H */
