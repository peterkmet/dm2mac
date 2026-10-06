#ifndef MONEY_H
#define MONEY_H
#include <stdint.h>

#define SHOP_CLASS_SET_SIZE 0x40    /* a bit for each item index */

/* The thing type of an item index, -1 for none. */
int item_index_to_thing_type(uint16_t item);
/* A new item of item index item (bit 15: a miscellaneous item may take the kept records); THING_NONE when the
   dungeon has no free record. */
uint16_t item_create_by_index(uint16_t item);
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
/* The item class set of list of a merchant creature (its GDAT text 0x10 + list): a bit per item index. */
int shop_parse_item_class_set(int creature, int list, uint8_t *bitset, int skip_containers);
/* The price of item at a merchant: its price list list (0 to 4), buying or selling, with the margin, charges (-1:
   the full charges). */
int shop_get_item_value(uint16_t item, int creature, int list, int buying, int apply_margin, int16_t charges);

#endif
