/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_MONEY_H
#define DM2_ENGINE_MONEY_H
/* Prefixes: money_ */

/* Coins in money boxes, the money box panel, and the item class sets and prices of the merchants. */

#include <stdbool.h>
#include <stdint.h>

/* Money: the coin types are the miscellaneous items with ITEM_FLAG_COIN, by value. */
#define COIN_TYPE_MAX       10
/* The bytes of an item class set (a GDAT text entry of a creature type): a bit for each item index. */
#define ITEM_CLASS_SET_SIZE 64

/* The terms of a merchant price: the merchant creature, its price list (0 to 4), buying or selling, the margin. */
typedef struct {
    int creature;
    int list;
    bool buying;
    bool apply_margin;
} PriceTerms;

/* Setup */
/* Builds the coin table once. */
void money_init_coin_table(void);

/* Coins */
/* The number of coin types. */
int money_coin_type_count(void); /* exported for the tests */
/* The item index of a coin type. */
uint16_t money_coin_item(int coin_type);
/* The value of a coin type. */
int16_t money_coin_value(int coin_type);
/* The coins of each coin type in a money box. */
void money_count_coins_in_box(uint16_t box, int16_t counts[COIN_TYPE_MAX]);
/* The coin type that the money box shows in slot (0 to 9), -1 none. */
int money_coin_type_for_slot(uint16_t box, int16_t slot);
/* The coins of an amount: their total value and their number. */
typedef struct {
    int16_t total;
    int16_t count;
} MoneySplit;

/* Splits amount into at most max_coins coins, the most valuable first; returns their value and number. */
MoneySplit money_split_amount(int16_t amount, int16_t max_coins, uint8_t *coin_types);

/* Money boxes */
/* Puts coin into the money box; false when box is no money box or coin no coin. */
bool money_add_coin_to_box(uint16_t box, uint16_t coin);
/* Takes a coin of coin_type out of the money box; THING_NONE when it has none. */
uint16_t money_take_coin_from_box(uint16_t box, int coin_type); /* exported for the tests */
/* A click on the coin pile of slot in the open money box: a coin goes into or out of the leader hand. */
void money_box_click_coin(int16_t slot);
/* Exchanges the coins of box for the fewest coins of the same value; check_only: only tests. True when the coins
   change. As in the original, it takes the coins out of the box one by one. */
bool money_normalize_coins(bool check_only, uint16_t box);

/* Merchant prices */
/* The item class set of class_list of a merchant creature (its GDAT text 0x10 + class_list): a bit per item index;
   false when the creature has no such text. skip_containers: the letter C chooses the weapons. */
bool money_parse_item_class_set(int creature, int class_list, uint8_t *bitset, bool skip_containers);
/* The price of item at a merchant under terms; charges -1: the full charges. */
uint16_t money_get_item_value(uint16_t item, const PriceTerms *terms, int16_t charges);

#endif /* DM2_ENGINE_MONEY_H */
