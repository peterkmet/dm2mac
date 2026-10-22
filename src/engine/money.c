/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Coins in money boxes, the money box panel, and the prices of the shops. */
#include "money.h"
#include "gdat.h"
#include "ids.h"
#include "action.h"
#include "champion.h"
#include "dungeon.h"
#include "game_text.h"
#include "hooks.h"
#include "item.h"
#include <string.h>

/* Creature attributes of a price list: MERCHANT_ATTR_* + list * MERCHANT_LIST_ATTRS: the margin, then two price
   factors; the class texts MERCHANT_CLASS_* + list * MERCHANT_LIST_ATTRS: the item class sets of the factors. */
#define MERCHANT_ATTR_MARGIN   0xa0
#define MERCHANT_ATTR_FACTOR_A 0xa1
#define MERCHANT_ATTR_FACTOR_B 0xa2
#define MERCHANT_CLASS_A       0x11
#define MERCHANT_CLASS_B       0x12
/* The attributes and class texts of each price list, and the margin in percent without an attribute. */
#define MERCHANT_LIST_ATTRS    3
#define DEFAULT_MARGIN         10
/* The miscellaneous items that the coin table scans. */
#define COIN_SCAN_COUNT        128

static struct {
    int ready, count;
    uint16_t items[COIN_TYPE_MAX];
    int16_t values[COIN_TYPE_MAX];
} coins;

/* True when the thing is a coin. */
static bool is_coin(uint16_t thing)
{
    return thing_type(thing) == THING_MISC && item_attr(thing, ATTR_FLAGS) & ITEM_FLAG_COIN;
}

/* Setup */

void money_init_coin_table(void)
{
    if (coins.ready)
        return;
    coins.ready = 1;
    for (int index = 0; index < COIN_SCAN_COUNT; index++) {
        if (!(gdat_entry_index(gdat_current(), CAT_MISC, index, GDAT_TYPE_VALUE, ATTR_FLAGS) & ITEM_FLAG_COIN))
            continue;
        int16_t value = (int16_t)gdat_entry_index(gdat_current(), CAT_MISC, index, GDAT_TYPE_VALUE, ATTR_VALUE);
        int at = 0;
        while (at < coins.count && coins.values[at] < value)
            at++;
        if (coins.count == COIN_TYPE_MAX)
            break;
        memmove(coins.values + at + 1, coins.values + at, (coins.count - at) * sizeof *coins.values);
        memmove(coins.items + at + 1, coins.items + at, (coins.count - at) * sizeof *coins.items);
        coins.values[at] = value;
        coins.items[at] = (uint16_t)(ITEM_FIRST_MISC + index);
        coins.count++;
    }
}

/* Coins */

int money_coin_type_count(void)
{
    return coins.count;
}

uint16_t money_coin_item(int coin_type)
{
    return coins.items[coin_type];
}

int16_t money_coin_value(int coin_type)
{
    return coins.values[coin_type];
}

void money_count_coins_in_box(uint16_t box, int16_t counts[COIN_TYPE_MAX])
{
    memset(counts, 0, COIN_TYPE_MAX * sizeof *counts);
    for (uint16_t t = thing_word(box, CONTAINER_CONTENTS); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (!is_coin(t))
            continue;
        uint16_t item = item_get_index(t);
        for (int c = 0; c < coins.count; c++)
            if (item == coins.items[c])
                counts[c] += misc_stack(t) + 1;
    }
}

int money_coin_type_for_slot(uint16_t box, int slot)
{
    char text[TEXT_SIZE];
    const char *p = text_gdat(CAT_CONTAINER, item_gdat_index(box), GFX_MONEYBOX_TEXT, text);
    if (!*p)
        return -1;
    /* the text lists item index ranges ("3-7", "J" = miscellaneous items); each index in order is the next slot */
    int16_t number = 0, counter = 0;
    for (int done = 0; !done;) {
        int16_t low = -1, base = -1;
        for (;;) {
            char c = *p++;
            if (c >= '0' && c <= '9') {
                number = (int16_t)(number * DECIMAL_BASE + (c - '0'));
            } else if (c == '-') {
                low = number;
                number = 0;
            } else if (c == 'J' && number == 0) {
                base = ITEM_FIRST_MISC;
            } else {
                if (c == 'J')
                    p--;
                if (c == '\0')
                    done = 1;
                break;
            }
        }
        if (low < 0)
            low = number;
        for (; low <= number; low++, counter++)
            if (counter == slot)
                for (int t = 0; t < coins.count; t++)
                    if ((int16_t)coins.items[t] == base + low)
                        return t;
        number = 0;
    }
    return -1;
}

int money_split_amount(int amount, int max_coins, uint8_t *coin_types, int16_t *coin_count)
{
    int16_t left = (int16_t)amount, n = 0;
    int type = money_coin_type_count() - 1, total = 0;
    *coin_count = 0;
    while (n < (int16_t)max_coins && left > 0 && type >= 0) {
        int16_t value = money_coin_value(type);
        if (value < 1)
            break;
        if (left < value) {
            type--;
            continue;
        }
        coin_types[n++] = (uint8_t)type;
        left = (int16_t)(left - value);
        total += value;
        (*coin_count)++;
    }
    return total;
}

/* Money boxes */

bool money_add_coin_to_box(uint16_t box, uint16_t coin)
{
    if (!item_is_moneybox(box) || !is_coin(coin))
        return false;
    uint16_t *contents = container_contents(box);
    for (uint16_t t = *contents; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_MISC)
            return false;
        if (item_type(t) == item_type(coin) && misc_stack(t) < MISC_STACK_MASK) {
            misc_set_stack(t, misc_stack(t) + 1);
            set_thing_word(coin, THING_NEXT, THING_NONE);
            return true;
        }
    }
    misc_set_stack(coin, 0);
    dungeon_append_thing(coin, contents);
    return true;
}

uint16_t money_take_coin_from_box(uint16_t box, int coin_type)
{
    if (coin_type < 0)
        return THING_NONE; /* fix: the original reads before the coin table */
    uint16_t item = money_coin_item(coin_type), found = THING_NONE;
    uint16_t *contents = container_contents(box);
    for (uint16_t t = *contents; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_MISC)
            return THING_NONE;
        if (item_get_index(t) == item)
            found = t;
    }
    if (found == THING_NONE)
        return found;
    if (!misc_stack(found)) {
        dungeon_unlink_thing_from_list(found, contents);
        return found;
    }
    misc_set_stack(found, misc_stack(found) - 1);
    return item_create_by_index(item);
}

void money_box_click_coin(int slot)
{
    if (action.mode != ACTION_MODE_MONEY_BOX)
        return;
    int champ = action.champion - 1;
    uint16_t box = champion(champ)->slots[action.hand];
    if (g_game.leader_hand == THING_NONE) {
        uint16_t coin = money_take_coin_from_box(box, money_coin_type_for_slot(box, slot));
        if (coin == THING_NONE) {
            champ_compute_load(champ);
            return;
        }
        champ_put_in_leader_hand(coin, 0);
    } else {
        if (!money_add_coin_to_box(box, g_game.leader_hand)) {
            champ_compute_load(champ);
            return;
        }
        champ_take_from_leader_hand();
    }
    action.redraw_delay = 1;
    g_hooks.update_action_area(0);
    champ_compute_load(champ);
}

bool money_normalize_coins(bool check_only, uint16_t box)
{
    int16_t counts[COIN_TYPE_MAX], wanted[COIN_TYPE_MAX] = {0};
    money_count_coins_in_box(box, counts);
    int16_t total = 0;
    for (int t = 0; t < money_coin_type_count(); t++)
        total = (int16_t)(total + money_coin_value(t) * counts[t]);
    bool differs = false;
    for (int t = money_coin_type_count() - 1; t >= 0 && total > 0; t--) {
        while (total >= money_coin_value(t)) {
            wanted[t]++;
            total = (int16_t)(total - money_coin_value(t));
        }
        differs = differs || counts[t] != wanted[t];
        if (differs && check_only)
            return true;
    }
    if (!differs || check_only)
        return differs;
    for (int t = 0; t < money_coin_type_count(); t++)
        for (uint16_t coin; (coin = money_take_coin_from_box(box, t)) != THING_NONE;)
            set_thing_word(coin, THING_NEXT, THING_NONE);
    for (int t = 0; t < money_coin_type_count(); t++)
        while (wanted[t]--) {
            uint16_t coin = item_create_by_index(money_coin_item(t));
            if (coin != THING_NONE)
                money_add_coin_to_box(box, coin);
        }
    return true;
}

/* Merchant prices */

int money_parse_item_class_set(int creature, int list, uint8_t *bitset, int skip_containers)
{
    char text[TEXT_SIZE];
    memset(bitset, 0, SHOP_CLASS_SET_SIZE);
    const char *p = text_gdat(CAT_CREATURE, creature, (uint8_t)(list + GFX_CREATURE_CLASS_TEXT), text);
    if (!*p)
        return 0;
    int16_t number = 0, low = -1, base = -1;
    int digits = 0;
    char c;
    do {
        c = *p++;
        if (c >= '0' && c <= '9') {
            number = (int16_t)(number * DECIMAL_BASE + (c - '0'));
            digits = 1;
            continue;
        }
        if (c == '-') {
            low = number;
            number = 0;
            continue;
        }
        if (digits) {
            if (low < 0)
                low = number;
            for (; low <= number && low < SHOP_CLASS_SET_SIZE * BYTE_BIT_COUNT; low++) {
                int16_t bit = (int16_t)(low + base);
                /* fix: the original writes outside the set for bad numbers */
                if (bit >= 0 && bit < SHOP_CLASS_SET_SIZE * BYTE_BIT_COUNT)
                    bitset[bit / BYTE_BIT_COUNT] |= (uint8_t)(1 << bit % BYTE_BIT_COUNT);
            }
            number = 0;
            low = base = -1;
            digits = 0;
        }
        /* the letters choose the item kind of the next numbers */
        switch (c) {
        case 'W':
            base = 0;
            break;
        case 'A':
            base = ITEM_FIRST_CLOTHING;
            break;
        case 'J':
            base = ITEM_FIRST_MISC;
            break;
        case 'P':
            base = ITEM_FIRST_POTION;
            break;
        case 'C':
            base = skip_containers ? 0 : ITEM_FIRST_CONTAINER;
            break;
        case 'S':
            base = ITEM_SCROLL;
            break;
        }
    } while (c);
    return 1;
}

/* True when the item class set holds item index item. */
static bool class_set_has(const uint8_t *set, int item)
{
    return set[item / BYTE_BIT_COUNT] & 1 << item % BYTE_BIT_COUNT;
}

/* A price factor of a merchant: when the item is in the class set, the price changes by factor percent. */
static uint16_t apply_factor(uint16_t price, uint16_t item, int creature, int attr, int list)
{
    uint8_t set[SHOP_CLASS_SET_SIZE];
    uint16_t factor = gdat_entry_index(gdat_current(), CAT_CREATURE, creature, GDAT_TYPE_VALUE, (uint8_t)attr);
    if (!factor || !money_parse_item_class_set(creature, (int16_t)list, set, 0))
        return price;
    if (!class_set_has(set, item & ITEM_INDEX_NONE))
        return price;
    return (uint16_t)((uint32_t)factor * price / PERCENT);
}

int money_get_item_value(uint16_t item, const PriceTerms *terms, int16_t charges)
{
    int creature = terms->creature, list = terms->list;
    int category = item_index_gdat_category(item), index = item_index_gdat_index(item);
    uint16_t price = gdat_entry_index(gdat_current(), category, index, GDAT_TYPE_VALUE, ATTR_VALUE);
    price = apply_factor(price, item, creature, (uint8_t)(list * MERCHANT_LIST_ATTRS + MERCHANT_ATTR_FACTOR_B),
                         list * MERCHANT_LIST_ATTRS + MERCHANT_CLASS_B);
    price = apply_factor(price, item, creature, (uint8_t)(list * MERCHANT_LIST_ATTRS + MERCHANT_ATTR_FACTOR_A),
                         list * MERCHANT_LIST_ATTRS + MERCHANT_CLASS_A);
    if (charges < 0)
        charges = (int16_t)item_max_charges(thing_make(item_index_to_thing_type(item), 0));
    int value =
        price + gdat_entry_index(gdat_current(), category, index, GDAT_TYPE_VALUE, ATTR_VALUE_PER_CHARGE) * charges;
    if (terms->apply_margin) {
        uint16_t margin = gdat_entry_index(gdat_current(), CAT_CREATURE, creature, GDAT_TYPE_VALUE,
                                           (uint8_t)(list * MERCHANT_LIST_ATTRS + MERCHANT_ATTR_MARGIN));
        if (!margin)
            margin = DEFAULT_MARGIN;
        int16_t part = (int16_t)((uint32_t)margin * (uint16_t)value / PERCENT);
        if (terms->buying)
            value += part;
        else if ((uint16_t)value > 1)
            value -= part > 1 ? part : 1;
    }
    return value;
}
