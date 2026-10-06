#ifndef MERCHANT_H
#define MERCHANT_H

/* The merchant commands of the creature AI plans; they return AI_RESULT_*. */
int shop_cmd_wait_for_offer(void);       /* waits until the customer shows coins or goods */
int shop_cmd_evaluate_offer(void);       /* compares the offer with the price of the goods */
int shop_cmd_count_payment(void);        /* checks the payment and computes the change */
int shop_cmd_pay_change(void);           /* gives the change in coins */

#endif
