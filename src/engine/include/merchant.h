/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_MERCHANT_H
#define DM2_ENGINE_MERCHANT_H
/* Prefixes: shop_ */

/* Merchant trading: the plan commands of the creature AI that a shopkeeper runs with the party in front of it. */

#include <stdint.h>

/* The merchant commands of the creature AI plans; they return AI_RESULT_*. */
/* Waits until the customer shows coins or goods. */
int shop_cmd_wait_for_offer(void);
/* Compares the offer with the price of the goods. */
int shop_cmd_evaluate_offer(void);
/* Checks the payment and computes the change. */
int shop_cmd_count_payment(void);
/* Gives the change in coins. */
int shop_cmd_pay_change(void);

#endif /* DM2_ENGINE_MERCHANT_H */
