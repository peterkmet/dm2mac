/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_MERCHANT_H
#define DM2_ENGINE_MERCHANT_H
/* Prefixes: shop_ */

#include <stdint.h>

/* The merchant commands of the creature AI plans and the prices of the goods. */

/* The merchant commands of the creature AI plans; they return AI_RESULT_*. */
int shop_cmd_wait_for_offer(void);       /* waits until the customer shows coins or goods */
int shop_cmd_evaluate_offer(void);       /* compares the offer with the price of the goods */
int shop_cmd_count_payment(void);        /* checks the payment and computes the change */
int shop_cmd_pay_change(void);           /* gives the change in coins */

/* Item classes and values (money.c). */
/* The item class set of list of a merchant creature (its GDAT text 0x10 + list): a bit per item index. */
int shop_parse_item_class_set(int creature, int list, uint8_t *bitset, int skip_containers);
/* The price of item at a merchant: its price list list (0 to 4), buying or selling, with the margin, charges (-1:
   the full charges). */
int shop_get_item_value(uint16_t item, int creature, int list, int buying, int apply_margin, int16_t charges);

#endif /* DM2_ENGINE_MERCHANT_H */
