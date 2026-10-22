/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Merchant trading: the plan commands of the creature AI that a shopkeeper runs with the party in front of it. */
#include "merchant.h"
#include "ai.h"
#include "creature.h"
#include "dungeon.h"
#include "item.h"
#include "money.h"
#include "move.h"
#include "random.h"
#include "sensor.h"

/* The actions of a merchant (ActiveGroup.action). */
#define MERCHANT_REFUSE 0x1b
#define MERCHANT_ACCEPT 0x1c
#define MERCHANT_WAIT   0x1d
#define MERCHANT_IDLE   0x1e
#define MERCHANT_OFFER  0x1f
#define MERCHANT_HAGGLE 0x20

/* Item filters of a merchant: coins and the goods it takes as payment. */
#define FILTER_COINS     0x10
#define FILTER_GOODS     0x07
#define FILTER_MONEY_BOX 0x3e
#define FILTER_NEW_COINS 0x47 /* g_ai.arg1 of the carried item search after the change */

/* Any cell of a square. */
#define ANY_CELL 0xff

/* Prices, waits and chances of a merchant. */
#define COIN_MAX         18 /* the most coins of a price */
#define DISCOUNT_FROM    16 /* a price above it gets a random discount of up to 15 % */
#define HAGGLE_LIMIT     76 /* an offer above 76 % of the price makes the merchant think it over */
#define HAGGLE_RANDOM    7  /* the random part added to HAGGLE_LIMIT */
#define LOW_OFFER_CHANCE 5  /* a roll below it over the missing percent marks the offer as low */
#define WAIT_START       5
#define OFFER_WAIT       9
#define GOODS_WAIT_MIN   6
#define OFFER_WAIT_STEP  4 /* the wait shortens by it while the offer stays the same */
#define OFFER_CELL       2 /* the cell, relative to the merchant, where the customer shows coins and goods */

/* -1 as a word: no offer, or no change due. */
#define VALUE_NONE 0xffff

/* g_ai.arg1 of shop_cmd_pay_change: the counter that holds the change. */
#define PAY_COUNTED 1
#define PAY_PRICE   2

static int max16(int a, int b)
{
    return (int16_t)a > (int16_t)b ? (int16_t)a : (int16_t)b;
}
static int min16(int a, int b)
{
    return (int16_t)a < (int16_t)b ? (int16_t)a : (int16_t)b;
}

/* The values that a sum of items needs (the stack record of sum_items_value). */
typedef struct {
    int16_t sum; /* -1: no item matched */
    int filter, buying, margin, charges;
} ItemSum;

/* The direction the merchant faces. */
static int facing(void)
{
    return group_dir(g_ai.group_thing);
}

/* The group of the customer in front of the merchant. */
static uint16_t front_group(void)
{
    int d = facing();
    return creature_get_group_at((uint16_t)(g_ai.event.data[EVENT_X] + g_dir_dx[d]),
                                 (uint16_t)(g_ai.event.data[EVENT_Y] + g_dir_dy[d]));
}

/* True when the group in front holds an item in cell (relative to the merchant, ANY_CELL: any) that is neither coins
   nor goods: the customer shows what it wants to sell. */
static bool front_customer_has_goods(int cell)
{
    if (cell != ANY_CELL)
        cell = dir_opposite(cell + facing());
    uint16_t g = front_group();
    if (g == THING_NONE)
        return false;
    for (uint16_t t = thing_word(g, GROUP_POSSESSIONS); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        int type = thing_type(t);
        if (!(type > THING_GROUP && type < THING_PROJECTILE))
            continue;
        if (cell != ANY_CELL && cell != thing_cell(t))
            continue;
        if (!ai_thing_matches_filter(t, FILTER_COINS) && !ai_thing_matches_filter(t, FILTER_GOODS))
            return true;
    }
    return false;
}

/* Adds the value of thing t to the sum when it matches the filter. */
static void sum_thing(uint16_t t, ItemSum *s)
{
    if (!ai_thing_matches_filter(t, s->filter))
        return;
    if (s->sum == -1)
        s->sum = 0;
    int charges = s->charges ? (int16_t)item_add_charges(t, 0) : -1;
    s->sum = (int16_t)(s->sum + shop_get_item_value(item_get_index(t), group_creature(g_ai.group_thing),
                                                    (int16_t)thing_word(g_ai.group_thing, GROUP_ANIM_SEQUENCE),
                                                    s->buying, s->margin, (int16_t)charges));
}

/* Adds the values of the items of a thing list in cell (ANY_CELL: any), also inside chests (dun_find_thing_in_list). */
static void sum_list(uint16_t first, int cell, ItemSum *s)
{
    for (uint16_t t = first; t != THING_ENDOFLIST && t != THING_NONE; t = dungeon_next_thing(t)) {
        if (cell != ANY_CELL && thing_cell(t) != cell)
            continue;
        if (item_is_chest(t))
            sum_list(thing_word(t, CONTAINER_CONTENTS), ANY_CELL, s);
        sum_thing(t, s);
    }
}

/* The merchant value of the items that match the filter of s in cell of the possessions of group; -1 when none
   matches. */
static int16_t sum_items_value(uint16_t group, int cell, ItemSum s)
{
    sum_list(thing_word(group, GROUP_POSSESSIONS), cell, &s);
    return s.sum;
}

/* The value of the coins and goods of the customer in cell, -1 when it shows none. */
static int16_t offered_value(uint16_t group, int cell, int16_t *goods)
{
    int16_t coins = sum_items_value(group, cell, (ItemSum){-1, FILTER_COINS, 0, 0, 0});
    *goods = sum_items_value(group, cell, (ItemSum){-1, FILTER_GOODS, 0, 0, 0});
    if (*goods == -1)
        return coins;
    return coins == -1 ? *goods : (int16_t)(coins + *goods);
}

/* The price in whole coins of the goods of the merchant in cell. */
static int16_t price_in_coins(uint16_t group, int cell)
{
    uint8_t coins[COIN_MAX];
    int16_t count;
    return (int16_t)money_split_amount(sum_items_value(group, cell, (ItemSum){-1, FILTER_COINS, 1, 1, 0}), COIN_MAX,
                                       coins, &count);
}

/* An offer below the price: the merchant haggles (MERCHANT_HAGGLE) or refuses, by chance. */
static void haggle_or_refuse(int percent)
{
    bool low = rand_below((uint16_t)max16(1, (int16_t)(PERCENT - percent))) < LOW_OFFER_CHANCE;
    bool odd = rand_2bits() == 0;
    g_ai.active->action = low ^ odd ? MERCHANT_HAGGLE : MERCHANT_REFUSE;
}

int shop_cmd_wait_for_offer(void)
{
    ActiveGroup *a = g_ai.active;
    if (front_customer_has_goods(OFFER_CELL)) {
        if (a->counters[0]-- <= WAIT_START) {
            a->counters[0] = OFFER_WAIT;
            a->action = MERCHANT_OFFER;
            return AI_RESULT_ISSUED;
        }
        a->action = MERCHANT_WAIT;
        return AI_RESULT_ISSUED;
    }
    int16_t goods_too = g_ai.arg1;
    g_ai.arg1 = FILTER_COINS;
    g_ai.arg2 = OFFER_CELL;
    if (ai_cmd_front_group_carries() == AI_RESULT_DONE)
        return AI_RESULT_DONE;
    if (goods_too) {
        g_ai.arg1 = FILTER_GOODS;
        if (ai_cmd_front_group_carries() == AI_RESULT_DONE)
            return AI_RESULT_DONE;
    }
    if (a->counters[0]-- <= 0) {
        a->counters[0] = WAIT_START;
        a->action = MERCHANT_IDLE;
        return AI_RESULT_FAIL;
    }
    if (a->counters[0] > WAIT_START)
        a->counters[0] = (int16_t)(a->counters[0] - WAIT_START);
    a->action = MERCHANT_WAIT;
    return AI_RESULT_FAIL;
}

int shop_cmd_evaluate_offer(void)
{
    ActiveGroup *a = g_ai.active;
    int dir = facing();
    if (front_customer_has_goods(OFFER_CELL)) {
        if (a->counters[0]-- <= GOODS_WAIT_MIN) {
            a->counters[0] = (int16_t)(rand_2bits() + OFFER_WAIT);
            a->action = MERCHANT_OFFER;
        } else {
            a->action = MERCHANT_WAIT;
        }
        return AI_RESULT_ISSUED;
    }
    uint16_t g = front_group();
    if (g == THING_NONE)
        return AI_RESULT_FAIL;
    a->action = MERCHANT_WAIT;
    int16_t goods;
    uint16_t offer = (uint16_t)offered_value(g, dir, &goods);
    if (offer == VALUE_NONE) {
        a->counters[1] = 0;
        return AI_RESULT_FAIL;
    }
    uint16_t price = (uint16_t)price_in_coins(g, dir_opposite(dir)), full = price;
    uint16_t last = (uint16_t)a->counters[1];
    a->counters[1] = (int16_t)offer;
    if (price > DISCOUNT_FROM)
        price = (uint16_t)(price - (int)rand_below(DISCOUNT_FROM) * price / PERCENT);
    /* fix: the original divides by 0 for a price of 0 */
    int16_t percent = (int16_t)(price ? (int)offer * PERCENT / (int16_t)price : PERCENT);
    if (offer >= price) {
        a->counters[0] = (int16_t)min16((int16_t)offer, (int16_t)full);
        a->action = MERCHANT_ACCEPT;
        return AI_RESULT_DONE;
    }
    if (offer != last) {
        if (last == VALUE_NONE || (int16_t)offer <= (int16_t)last)
            return AI_RESULT_ISSUED;
        if (rand_2bits() && percent <= (int)(rand_next() & HAGGLE_RANDOM) + HAGGLE_LIMIT)
            return AI_RESULT_ISSUED;
        a->position = 0;
        haggle_or_refuse(percent);
        return AI_RESULT_ISSUED;
    }
    if (a->counters[0]-- > 0) {
        if (a->counters[0] > GOODS_WAIT_MIN)
            a->counters[0] = (int16_t)(a->counters[0] - OFFER_WAIT_STEP);
        a->action = MERCHANT_WAIT;
        return AI_RESULT_ISSUED;
    }
    if (percent > HAGGLE_LIMIT)
        haggle_or_refuse(percent);
    else
        a->action = MERCHANT_REFUSE;
    return AI_RESULT_FAIL;
}

int shop_cmd_count_payment(void)
{
    ActiveGroup *a = g_ai.active;
    int dir = facing();
    if (front_customer_has_goods(dir_opposite(g_ai.arg1))) {
        a->action = MERCHANT_OFFER;
        return AI_RESULT_FAIL;
    }
    uint16_t g = front_group();
    if (g == THING_NONE)
        return AI_RESULT_FAIL;
    int cell = dir_turn(dir, g_ai.arg1), goods_cell = dir_opposite(cell);
    int16_t price = price_in_coins(g, goods_cell), due = price;
    int16_t goods = sum_items_value(g, goods_cell, (ItemSum){-1, FILTER_GOODS, 0, 0, 0});
    if (goods != -1)
        due = (int16_t)(due + goods);
    else
        goods = 0;
    int16_t paid_goods;
    uint16_t paid = (uint16_t)offered_value(g, cell, &paid_goods);
    if (paid == VALUE_NONE)
        paid = 0;
    if ((int)paid < (uint16_t)goods + a->counters[0]) {
        a->action = MERCHANT_REFUSE;
        return AI_RESULT_FAIL;
    }
    if ((int)paid != a->counters[1]) {
        a->action = MERCHANT_ACCEPT;
        a->counters[1] = (int16_t)paid;
    }
    a->position = (uint16_t)max16(0, (int16_t)(paid - due));
    return AI_RESULT_DONE;
}

int shop_cmd_pay_change(void)
{
    ActiveGroup *a = g_ai.active;
    int dir = facing();
    uint16_t g = front_group();
    if (g == THING_NONE)
        return AI_RESULT_FAIL;
    a->action = MERCHANT_WAIT;
    if (!g_ai.arg1) {
        uint16_t box = ai_find_matching_item(FILTER_MONEY_BOX, thing_word(g, GROUP_POSSESSIONS), dir_opposite(dir));
        if (box == THING_ENDOFLIST)
            box = THING_NONE;
        int16_t value = sum_items_value(g, dir_opposite(dir), (ItemSum){-1, FILTER_COINS, 0, 1, 1});
        if ((value == 0 || value == -1) && box == THING_NONE)
            return AI_RESULT_FAIL;
        a->counters[1] = (int16_t)max16(0, value);
        return AI_RESULT_DONE;
    }
    uint16_t *change = g_ai.arg1 == PAY_COUNTED ? (uint16_t *)&a->counters[1]
                       : g_ai.arg1 == PAY_PRICE ? (uint16_t *)&a->counters[0]
                                                : &a->position;
    uint16_t box = THING_NONE;
    for (uint16_t t = thing_word(g_ai.group_thing, GROUP_POSSESSIONS); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        if (item_is_moneybox(t)) {
            box = t;
            money_normalize_coins(0, t);
        }
    g_ai.arg1 = FILTER_NEW_COINS;
    int16_t amount = (int16_t)*change;
    if (amount <= 0) {
        int r = ai_cmd_seek_with_carried();
        return r == AI_RESULT_ISSUED ? AI_RESULT_ISSUED : AI_RESULT_DONE;
    }
    uint8_t coins[COIN_MAX];
    int16_t count;
    money_split_amount(amount, COIN_MAX, coins, &count);
    *change = VALUE_NONE;
    if (count <= 0)
        return AI_RESULT_DONE;
    for (int i = count - 1; i >= 0; i--) {
        uint16_t coin = item_create_by_index(money_coin_item(coins[i]));
        if (coin == THING_NONE)
            break;
        if (box != THING_NONE)
            money_add_coin_to_box(box, coin);
        else
            dungeon_append_thing(coin, group_possessions(g_ai.group_thing));
    }
    ai_cmd_seek_with_carried();
    return AI_RESULT_ISSUED;
}
