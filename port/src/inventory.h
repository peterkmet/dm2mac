#ifndef INVENTORY_H
#define INVENTORY_H
#include <stdint.h>

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

/* Puts thing into the hand of the leader; defer_cursor: the original changes the mouse cursor later. */
void champ_put_object_in_leader_hand(uint16_t thing, int defer_cursor);
/* Takes the object out of the hand of the leader; THING_NONE when the hand is empty. */
uint16_t champ_remove_object_from_leader_hand(void);
/* Applies (or takes back) the effects of thing in slot of champ (slot -1: the leader hand). */
void champ_apply_item_effects(int champ, uint16_t thing, int slot, int mode);
/* Computes the load of the champion: its slots and the items of its open chest. */
void champ_compute_load(int champ);
/* Changes a statistic of the champion by delta; big changes away from the maximum shrink. */
void champ_adjust_stat(uint8_t *stat, int delta);
/* Prints the name of thing in the message area. */
void ui_show_object_name(uint16_t thing);
/* Updates the cursor when the icon of the animated leader hand object changes. */
void inv_update_hand_cursor(void);
/* The object of the leader hand that the cursor shows, THING_NONE none. */
uint16_t inv_cursor_object(void);

#endif
