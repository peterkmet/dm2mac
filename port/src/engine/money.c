/* Coins in money boxes, the money box panel, new items, and the prices of the shops. */
#include <string.h>
#include "action.h"
#include "champion.h"
#include "draw.h"
#include "dungeon.h"
#include "inventory.h"
#include "item.h"
#include "money.h"
#include "text.h"

#define CREATURE_CLASS_TEXT 0x10    /* GDAT texts (0x0f, creature, 5, 0x10 + list): the item classes of a merchant */
#define MERCHANT_ATTR_MARGIN 0xa0   /* creature attributes 0xa0 + list * 3: margin, then two price factors */
#define MERCHANT_ATTR_FACTOR_A 0xa1
#define MERCHANT_ATTR_FACTOR_B 0xa2
#define MERCHANT_CLASS_A 0x11       /* the item class sets of the two price factors: 0x11 + list * 3, 0x12 + list * 3 */
#define MERCHANT_CLASS_B 0x12
#define MERCHANT_LIST_ATTRS 3       /* creature attributes and class texts of each price list */
#define DEFAULT_MARGIN 10
#define PERCENT 100

/* The contents list of a container. */
static uint16_t *contents_of(uint16_t container)
{
    return thing_list(container, CONTAINER_CONTENTS);
}

/* True when the item class set holds item index item. */
static bool class_set_has(const uint8_t *set, int item) { return set[item / BYTE_BITS] & 1 << item % BYTE_BITS; }

int item_index_to_thing_type(uint16_t item)
{
    item &= ITEM_INDEX_NONE;
    if (item < ITEM_FIRST_CLOTHING) return THING_WEAPON;
    if (item < ITEM_FIRST_MISC) return THING_ARMOUR;
    if (item < ITEM_FIRST_POTION) return THING_MISC;
    if (item > ITEM_SCROLL) return -1;
    if (item == ITEM_SCROLL) return THING_SCROLL;
    if (item >= ITEM_FIRST_CONTAINER) return THING_CONTAINER;
    if (item >= ITEM_FIRST_CREATURE) return THING_GROUP;
    return THING_POTION;
}

uint16_t item_create_by_index(uint16_t item)
{
    int type = item_index_to_thing_type(item);
    if (type < 0) return THING_NONE;  /* fix: the original asks for a record of the type 0xffff */
    /* ponytail: when the records run out, the original reuses discarded items (dun_get_discarded_thing) */
    uint16_t thing = dungeon_unused_thing(type | (item & THING_ANY_RECORD));
    if (thing != THING_NONE) item_set_gdat_index(thing, item_index_gdat_index(item));
    return thing;
}

int money_add_coin_to_box(uint16_t box, uint16_t coin)
{
    if (!item_is_moneybox(box) || !money_is_coin(coin)) return 0;
    uint16_t *contents = contents_of(box);
    for (uint16_t t = *contents; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_MISC) return 0;
        if (item_type(t) == item_type(coin) && misc_stack(t) < MISC_STACK_MASK) {
            misc_set_stack(t, misc_stack(t) + 1);
            set_thing_word(coin, THING_NEXT, THING_NONE);
            return 1;
        }
    }
    misc_set_stack(coin, 0);
    dungeon_append_thing(coin, contents);
    return 1;
}

uint16_t money_take_coin_from_box(uint16_t box, int coin_type)
{
    if (coin_type < 0) return THING_NONE;  /* fix: the original reads before the coin table */
    uint16_t item = money_coin_item(coin_type), found = THING_NONE;
    uint16_t *contents = contents_of(box);
    for (uint16_t t = *contents; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_MISC) return THING_NONE;
        if (item_get_index(t) == item) found = t;
    }
    if (found == THING_NONE) return found;
    if (!misc_stack(found)) {
        dungeon_unlink_thing_from_list(found, contents);
        return found;
    }
    misc_set_stack(found, misc_stack(found) - 1);
    return item_create_by_index(item);
}

void money_box_click_coin(int slot)
{
    if (action.mode != ACTION_MODE_MONEY_BOX) return;
    int champ = action.champion - 1;
    uint16_t box = champion(champ)->slots[action.hand];
    if (game.leader_hand == THING_NONE) {
        uint16_t coin = money_take_coin_from_box(box, money_coin_type_for_slot(box, slot));
        if (coin == THING_NONE) {
            champ_compute_load(champ);
            return;
        }
        champ_put_object_in_leader_hand(coin, 0);
    } else {
        if (!money_add_coin_to_box(box, game.leader_hand)) {
            champ_compute_load(champ);
            return;
        }
        champ_remove_object_from_leader_hand();
    }
    action.redraw_delay = 1;
    ui_update_action_area(0);
    champ_compute_load(champ);
}

int money_split_amount(int amount, int max_coins, uint8_t *coin_types, int16_t *coin_count)
{
    int16_t left = (int16_t)amount, n = 0;
    int type = money_coin_type_count() - 1, total = 0;
    *coin_count = 0;
    while (n < (int16_t)max_coins && left > 0 && type >= 0) {
        int16_t value = money_coin_value(type);
        if (value < 1) break;
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

int money_normalize_coins(int check_only, uint16_t box)
{
    int16_t counts[COIN_TYPE_MAX], wanted[COIN_TYPE_MAX] = {0};
    money_count_coins_in_box(box, counts);
    int16_t total = 0;
    for (int t = 0; t < money_coin_type_count(); t++) total = (int16_t)(total + money_coin_value(t) * counts[t]);
    int differs = 0;
    for (int t = money_coin_type_count() - 1; t >= 0 && total > 0; t--) {
        while (total >= money_coin_value(t)) {
            wanted[t]++;
            total = (int16_t)(total - money_coin_value(t));
        }
        differs |= counts[t] != wanted[t];
        if (differs && (int16_t)check_only == 1) return 1;
    }
    if (!differs || (int16_t)check_only == 1) return differs;
    for (int t = 0; t < money_coin_type_count(); t++)
        for (uint16_t coin; (coin = money_take_coin_from_box(box, t)) != THING_NONE;)
            set_thing_word(coin, THING_NEXT, THING_NONE);
    for (int t = 0; t < money_coin_type_count(); t++)
        while (wanted[t]--) {
            uint16_t coin = item_create_by_index(money_coin_item(t));
            if (coin != THING_NONE) money_add_coin_to_box(box, coin);
        }
    return 1;
}

int shop_parse_item_class_set(int creature, int list, uint8_t *bitset, int skip_containers)
{
    char text[TEXT_SIZE];
    memset(bitset, 0, SHOP_CLASS_SET_SIZE);
    const char *p = text_gdat(CAT_CREATURE, creature, (uint8_t)(list + CREATURE_CLASS_TEXT), text);
    if (!*p) return 0;
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
            if (low < 0) low = number;
            for (; low <= number && low < SHOP_CLASS_SET_SIZE * BYTE_BITS; low++) {
                int16_t bit = (int16_t)(low + base);
                /* fix: the original writes outside the set for bad numbers */
                if (bit >= 0 && bit < SHOP_CLASS_SET_SIZE * BYTE_BITS) bitset[bit / BYTE_BITS] |= (uint8_t)(1 << bit % BYTE_BITS);
            }
            number = 0;
            low = base = -1;
            digits = 0;
        }
        /* the letters choose the item kind of the next numbers */
        switch (c) {
        case 'W': base = 0; break;
        case 'A': base = ITEM_FIRST_CLOTHING; break;
        case 'J': base = ITEM_FIRST_MISC; break;
        case 'P': base = ITEM_FIRST_POTION; break;
        case 'C': base = skip_containers ? 0 : ITEM_FIRST_CONTAINER; break;
        case 'S': base = ITEM_SCROLL; break;
        }
    } while (c);
    return 1;
}

/* A price factor of a merchant: when the item is in the class set, the price changes by factor percent. */
static uint16_t apply_factor(uint16_t price, uint16_t item, int creature, int attr, int list)
{
    uint8_t set[SHOP_CLASS_SET_SIZE];
    uint16_t factor = gdat_entry_index(draw_gdat(), CAT_CREATURE, creature, GDAT_TYPE_VALUE, (uint8_t)attr);
    if (!factor || !shop_parse_item_class_set(creature, (int16_t)list, set, 0)) return price;
    if (!class_set_has(set, item & ITEM_INDEX_NONE)) return price;
    return (uint16_t)((uint32_t)factor * price / PERCENT);
}

int shop_get_item_value(uint16_t item, int creature, int list, int buying, int apply_margin, int16_t charges)
{
    int category = item_index_gdat_category(item), index = item_index_gdat_index(item);
    uint16_t price = gdat_entry_index(draw_gdat(), category, index, GDAT_TYPE_VALUE, ATTR_VALUE);
    price = apply_factor(price, item, creature, (uint8_t)(list * MERCHANT_LIST_ATTRS + MERCHANT_ATTR_FACTOR_B), list * MERCHANT_LIST_ATTRS + MERCHANT_CLASS_B);
    price = apply_factor(price, item, creature, (uint8_t)(list * MERCHANT_LIST_ATTRS + MERCHANT_ATTR_FACTOR_A), list * MERCHANT_LIST_ATTRS + MERCHANT_CLASS_A);
    if (charges < 0) charges = (int16_t)item_max_charges((uint16_t)(item_index_to_thing_type(item) << THING_TYPE_SHIFT));
    int value = price + gdat_entry_index(draw_gdat(), category, index, GDAT_TYPE_VALUE, ATTR_VALUE_PER_CHARGE) * charges;
    if (apply_margin) {
        uint16_t margin = gdat_entry_index(draw_gdat(), CAT_CREATURE, creature, GDAT_TYPE_VALUE,
                                           (uint8_t)(list * MERCHANT_LIST_ATTRS + MERCHANT_ATTR_MARGIN));
        if (!margin) margin = DEFAULT_MARGIN;
        int16_t part = (int16_t)((uint32_t)margin * (uint16_t)value / PERCENT);
        if (buying) value += part;
        else if ((uint16_t)value > 1) value -= part > 1 ? part : 1;
    }
    return value;
}
