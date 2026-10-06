/* Merchant trading: the plan commands of the creature AI that a shopkeeper runs with the party in front of it. */
#include "ai.h"
#include "ai_move.h"
#include "creature.h"
#include "champion.h"
#include "dungeon.h"
#include "item.h"
#include "merchant.h"
#include "money.h"
#include "move.h"
#include "random.h"

/* The actions of a merchant (ActiveGroup.action). */
enum {
    MERCHANT_REFUSE = 0x1b, MERCHANT_ACCEPT = 0x1c, MERCHANT_WAIT = 0x1d, MERCHANT_IDLE = 0x1e, MERCHANT_OFFER = 0x1f,
    MERCHANT_HAGGLE = 0x20
};
#define FILTER_COINS 0x10           /* item filters of a merchant: coins and the goods it takes as payment */
#define FILTER_GOODS 7
#define FILTER_MONEY_BOX 0x3e
#define FILTER_NEW_COINS 0x47       /* ai.arg1 of the carried item search after the change */
#define ANY_CELL 0xff
#define GROUP_DIR_SHIFT 8           /* GROUP_DIR bits 8-9: the direction the group faces */
#define COIN_MAX 0x12               /* the most coins of a price */
#define DISCOUNT_FROM 0x10          /* a price above it gets a random discount of up to 15 % */
#define PERCENT 100
#define HAGGLE_LIMIT 0x4c           /* an offer above 76 % of the price makes the merchant think it over */
#define HAGGLE_RANDOM 7             /* the random part added to HAGGLE_LIMIT */
#define LOW_OFFER_CHANCE 5          /* a roll below it over the missing percent marks the offer as low */
#define WAIT_START 5
#define OFFER_WAIT 9
#define GOODS_WAIT_MIN 6

static int max16(int a, int b) { return (int16_t)a > (int16_t)b ? (int16_t)a : (int16_t)b; }
static int min16(int a, int b) { return (int16_t)a < (int16_t)b ? (int16_t)a : (int16_t)b; }

/* The values that a sum of items needs (the stack record of shop_sum_items_value). */
typedef struct {
    int16_t sum;                    /* -1: no item matched */
    int filter, buying, margin, charges;
} ItemSum;

static int facing(void) { return thing_word(ai.group_thing, GROUP_DIR) >> GROUP_DIR_SHIFT & 3; }

static uint16_t front_group(void)
{
    int d = facing();
    return creature_get_group_at((uint16_t)(ai.event.data[0] + dir_dx[d]), (uint16_t)(ai.event.data[1] + dir_dy[d]));
}

/* True when the group in front holds an item in cell (relative to the merchant, ANY_CELL: any) that is neither coins
   nor goods: the customer shows what it wants to sell. */
static int shop_front_customer_has_goods(int cell)
{
    if (cell != ANY_CELL) cell = (cell + facing() + 2) & 3;
    uint16_t g = front_group();
    if (g == THING_NONE) return 0;
    for (uint16_t t = thing_word(g, GROUP_POSSESSIONS); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        int type = thing_type(t);
        if (!(type > THING_GROUP && type < THING_PROJECTILE)) continue;
        if (cell != ANY_CELL && cell != t >> THING_CELL_SHIFT) continue;
        if (!creature_thing_matches_filter(t, FILTER_COINS) && !creature_thing_matches_filter(t, FILTER_GOODS)) return 1;
    }
    return 0;
}

static void sum_thing(uint16_t t, ItemSum *s)
{
    if (!creature_thing_matches_filter(t, s->filter)) return;
    if (s->sum == -1) s->sum = 0;
    int charges = s->charges ? (int16_t)item_add_charges(t, 0) : -1;
    s->sum = (int16_t)(s->sum + shop_get_item_value(item_get_index(t), ai.group[GROUP_CREATURE],
                                                     (int16_t)thing_word(ai.group_thing, GROUP_ANIM_SEQUENCE), s->buying,
                                                     s->margin, (int16_t)charges));
}

/* Adds the values of the items of a thing list in cell (ANY_CELL: any), also inside chests (dun_find_thing_in_list). */
static void sum_list(uint16_t first, int cell, ItemSum *s)
{
    for (uint16_t t = first; t != THING_ENDOFLIST && t != THING_NONE; t = dungeon_next_thing(t)) {
        if (cell != ANY_CELL && t >> THING_CELL_SHIFT != cell) continue;
        if (item_is_chest(t)) sum_list(thing_word(t, CONTAINER_CONTENTS), ANY_CELL, s);
        sum_thing(t, s);
    }
}

/* The merchant value of the items of filter in cell of the possessions of group; -1 when none matches. */
static int16_t shop_sum_items_value(uint16_t group, int filter, int cell, int buying, int margin, int charges)
{
    ItemSum s = {-1, filter, buying, margin, charges};
    sum_list(thing_word(group, GROUP_POSSESSIONS), cell, &s);
    return s.sum;
}

/* The value of the coins and goods of the customer in cell, -1 when it shows none. */
static int16_t offered_value(uint16_t group, int cell, int16_t *goods)
{
    int16_t coins = shop_sum_items_value(group, FILTER_COINS, cell, 0, 0, 0);
    *goods = shop_sum_items_value(group, FILTER_GOODS, cell, 0, 0, 0);
    if (*goods == -1) return coins;
    return coins == -1 ? *goods : (int16_t)(coins + *goods);
}

/* The price in whole coins of the goods of the merchant in cell. */
static int16_t price_in_coins(uint16_t group, int cell)
{
    uint8_t coins[COIN_MAX];
    int16_t count;
    return (int16_t)money_split_amount(shop_sum_items_value(group, FILTER_COINS, cell, 1, 1, 0), COIN_MAX, coins, &count);
}

/* An offer below the price: the merchant haggles (MERCHANT_HAGGLE) or refuses, by chance. */
static void haggle_or_refuse(int percent)
{
    int low = rand_below((uint16_t)max16(1, (int16_t)(PERCENT - percent))) < LOW_OFFER_CHANCE;
    int odd = rand_2bits() == 0;
    ai.active->action = low ^ odd ? MERCHANT_HAGGLE : MERCHANT_REFUSE;
}

int shop_cmd_wait_for_offer(void)
{
    ActiveGroup *a = ai.active;
    if (shop_front_customer_has_goods(2)) {
        if (a->counters[0]-- <= WAIT_START) {
            a->counters[0] = OFFER_WAIT;
            a->action = MERCHANT_OFFER;
            return AI_RESULT_ISSUED;
        }
        a->action = MERCHANT_WAIT;
        return AI_RESULT_ISSUED;
    }
    int16_t goods_too = ai.arg1;
    ai.arg1 = FILTER_COINS;
    ai.arg2 = 2;
    if (creature_ai_cmd_front_group_carries() == AI_RESULT_DONE) return AI_RESULT_DONE;
    if (goods_too) {
        ai.arg1 = FILTER_GOODS;
        if (creature_ai_cmd_front_group_carries() == AI_RESULT_DONE) return AI_RESULT_DONE;
    }
    if (a->counters[0]-- <= 0) {
        a->counters[0] = WAIT_START;
        a->action = MERCHANT_IDLE;
        return AI_RESULT_FAIL;
    }
    if (a->counters[0] > WAIT_START) a->counters[0] = (int16_t)(a->counters[0] - WAIT_START);
    a->action = MERCHANT_WAIT;
    return AI_RESULT_FAIL;
}

int shop_cmd_evaluate_offer(void)
{
    ActiveGroup *a = ai.active;
    int dir = facing();
    if (shop_front_customer_has_goods(2)) {
        if (a->counters[0]-- <= GOODS_WAIT_MIN) {
            a->counters[0] = (int16_t)(rand_2bits() + OFFER_WAIT);
            a->action = MERCHANT_OFFER;
        } else {
            a->action = MERCHANT_WAIT;
        }
        return AI_RESULT_ISSUED;
    }
    uint16_t g = front_group();
    if (g == THING_NONE) return AI_RESULT_FAIL;
    a->action = MERCHANT_WAIT;
    int16_t goods;
    uint16_t offer = (uint16_t)offered_value(g, dir, &goods);
    if (offer == 0xffff) {
        a->counters[1] = 0;
        return AI_RESULT_FAIL;
    }
    uint16_t price = (uint16_t)price_in_coins(g, (dir + 2) & 3), full = price;
    uint16_t last = (uint16_t)a->counters[1];
    a->counters[1] = (int16_t)offer;
    if (price > DISCOUNT_FROM) price = (uint16_t)(price - (int)rand_below(DISCOUNT_FROM) * price / PERCENT);
    /* fix: the original divides by 0 for a price of 0 */
    int16_t percent = (int16_t)(price ? (int)offer * PERCENT / (int16_t)price : PERCENT);
    if (offer >= price) {
        a->counters[0] = (int16_t)min16((int16_t)offer, (int16_t)full);
        a->action = MERCHANT_ACCEPT;
        return AI_RESULT_DONE;
    }
    if (offer != last) {
        if (last == 0xffff || (int16_t)offer <= (int16_t)last) return AI_RESULT_ISSUED;
        if (rand_2bits() && percent <= (int)(rand_next() & HAGGLE_RANDOM) + HAGGLE_LIMIT) return AI_RESULT_ISSUED;
        a->position = 0;
        haggle_or_refuse(percent);
        return AI_RESULT_ISSUED;
    }
    if (a->counters[0]-- > 0) {
        if (a->counters[0] > GOODS_WAIT_MIN) a->counters[0] = (int16_t)(a->counters[0] - 4);
        a->action = MERCHANT_WAIT;
        return AI_RESULT_ISSUED;
    }
    if (percent > HAGGLE_LIMIT) haggle_or_refuse(percent);
    else a->action = MERCHANT_REFUSE;
    return AI_RESULT_FAIL;
}

int shop_cmd_count_payment(void)
{
    ActiveGroup *a = ai.active;
    int dir = facing();
    if (shop_front_customer_has_goods((uint8_t)(ai.arg1 + 2) & 3)) {
        a->action = MERCHANT_OFFER;
        return AI_RESULT_FAIL;
    }
    uint16_t g = front_group();
    if (g == THING_NONE) return AI_RESULT_FAIL;
    int cell = (dir + ai.arg1) & 3, goods_cell = (cell + 2) & 3;
    int16_t price = price_in_coins(g, goods_cell), due = price;
    int16_t goods = shop_sum_items_value(g, FILTER_GOODS, goods_cell, 0, 0, 0);
    if (goods != -1) due = (int16_t)(due + goods);
    else goods = 0;
    int16_t paid_goods;
    uint16_t paid = (uint16_t)offered_value(g, cell, &paid_goods);
    if (paid == 0xffff) paid = 0;
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
    ActiveGroup *a = ai.active;
    int dir = facing();
    uint16_t g = front_group();
    if (g == THING_NONE) return AI_RESULT_FAIL;
    a->action = MERCHANT_WAIT;
    if (!ai.arg1) {
        uint16_t box = creature_find_matching_item(FILTER_MONEY_BOX, thing_word(g, GROUP_POSSESSIONS), (dir + 2) & 3);
        if (box == THING_ENDOFLIST) box = THING_NONE;
        int16_t value = shop_sum_items_value(g, FILTER_COINS, (dir + 2) & 3, 0, 1, 1);
        if ((value == 0 || value == -1) && box == THING_NONE) return AI_RESULT_FAIL;
        a->counters[1] = (int16_t)max16(0, value);
        return AI_RESULT_DONE;
    }
    uint16_t *change = ai.arg1 == 1 ? (uint16_t *)&a->counters[1]
                       : ai.arg1 == 2 ? (uint16_t *)&a->counters[0] : &a->position;
    uint16_t box = THING_NONE;
    for (uint16_t t = thing_word(ai.group_thing, GROUP_POSSESSIONS); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        if (item_is_moneybox(t)) {
            box = t;
            money_normalize_coins(0, t);
        }
    ai.arg1 = FILTER_NEW_COINS;
    int16_t amount = (int16_t)*change;
    if (amount <= 0) {
        int r = creature_ai_cmd_seek_with_carried_item();
        return r == AI_RESULT_ISSUED ? AI_RESULT_ISSUED : AI_RESULT_DONE;
    }
    uint8_t coins[COIN_MAX];
    int16_t count;
    money_split_amount(amount, COIN_MAX, coins, &count);
    *change = 0xffff;
    if (count <= 0) return AI_RESULT_DONE;
    for (int i = count - 1; i >= 0; i--) {
        uint16_t coin = item_create_by_index(money_coin_item(coins[i]));
        if (coin == THING_NONE) break;
        if (box != THING_NONE) money_add_coin_to_box(box, coin);
        else dungeon_append_thing(coin, (uint16_t *)(void *)(ai.group + GROUP_POSSESSIONS));
    }
    creature_ai_cmd_seek_with_carried_item();
    return AI_RESULT_ISSUED;
}
