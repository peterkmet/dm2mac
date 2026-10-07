#ifndef INVENTORY_H
#define INVENTORY_H
#include <stdint.h>
#include "timeline.h"

/* Prefixes: inv_ */

/* Modes of champ_apply_item_effects: how an item changes the champion that holds it. */
enum item_effect_mode {
    EFFECT_MODE_LOAD = 0,       /* a loaded game: the bonuses of the items that the champion holds */
    EFFECT_MODE_ADD = 1,        /* the item goes into a slot or the hand */
    EFFECT_MODE_REMOVE = -1,    /* the item leaves a slot or the hand */
    EFFECT_MODE_CONSUME = 2,    /* the champion eats or drinks the item */
    EFFECT_MODE_CONSUME_END = -2, /* the timed effect of a consumed item ends */
    EFFECT_MODE_RESTORE = 3     /* a loaded game: a timed effect of a consumed item */
};

#define HAND_ICON_SIZE 16
#define HAND_ICON_NONE 0xff
#define HAND_ICON_COLORS 16
#define MOUTH_CATEGORY 9            /* the mouth picture of the inventory: GDAT (9, 0x5b, 1, entry) */
#define MOUTH_INDEX 0x5b

/* The leader hand object as the original keeps it beside game.leader_hand. */
typedef struct {
    uint16_t attr;              /* item attribute 0 (flags) of the object */
    uint16_t weight;            /* its weight when it went into the hand */
    uint8_t icon;               /* the icon entry that the cursor shows, HAND_ICON_NONE none */
    uint8_t cursor[HAND_ICON_SIZE * HAND_ICON_SIZE];  /* the icon: color numbers 0 to 15, HAND_ICON_KEY clear */
    uint8_t palette[HAND_ICON_COLORS];  /* the palette colors of the color numbers */
} LeaderHand;

extern LeaderHand leader_hand;
#define HAND_ICON_KEY 12

/* Prints the name of thing in the message area. */
void inv_show_object_name(uint16_t thing);
/* Updates the cursor when the icon of the animated leader hand object changes. */
void inv_update_hand_cursor(void);
/* A click on a slot: 0 to 7 the hands in the status boxes, then the inventory slots, then the open chest. The leader
   hand object and the item in the slot change places. */
void inv_click_slot(int slot);
/* Puts the leader hand object into the first free pouch or pack slot of champ. */
void inv_store_leader_hand_object(int champ);
/* Puts thing into the first free slot of champ that takes it. */
void inv_put_object_in_free_slot(int champ, uint16_t thing);
/* The object of the leader hand that the cursor shows, THING_NONE none. */
uint16_t inv_cursor_object(void);

#endif
