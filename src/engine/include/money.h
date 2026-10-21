/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_MONEY_H
#define DM2_ENGINE_MONEY_H
/* Prefixes: money_ */

/* Coins in money boxes, the money box panel, and new items. */

#include <stdint.h>
#include "item.h"

#define SHOP_CLASS_SET_SIZE 64      /* a bit for each item index */

/* Puts coin into the money box; returns 0 when box is no money box or coin no coin. */
int money_add_coin_to_box(uint16_t box, uint16_t coin);
/* Takes a coin of coin_type out of the money box; THING_NONE when it has none. */
uint16_t money_take_coin_from_box(uint16_t box, int coin_type);
/* A click on the coin pile of slot in the open money box: a coin goes into or out of the leader hand. */
void money_box_click_coin(int slot);
/* Splits amount into at most max_coins coins, the most valuable first; returns the value of the coins. */
int money_split_amount(int amount, int max_coins, uint8_t *coin_types, int16_t *coin_count);
/* Exchanges the coins of box for the fewest coins of the same value; check_only 1: only tests. Returns 1 when the
   coins change. */
int money_normalize_coins(int check_only, uint16_t box);

/* Coins (item.c). */
/* Builds the coin table once. */
void money_init_coin_table(void);
int money_coin_type_count(void);
uint16_t money_coin_item(int coin_type);   /* the item index of a coin type */
int16_t money_coin_value(int coin_type);
bool money_is_coin(uint16_t thing);
/* The coins of each coin type in a money box. */
void money_count_coins_in_box(uint16_t box, int16_t counts[COIN_TYPE_MAX]);
/* The coin type that the money box shows in slot (0 to 9), -1 none. */
int money_coin_type_for_slot(uint16_t box, int slot);

#endif /* DM2_ENGINE_MONEY_H */
