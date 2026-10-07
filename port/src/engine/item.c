#include <stdio.h>
#include <string.h>
#include "action.h"
#include "champion.h"
#include "draw.h"
#include "dungeon.h"
#include "item.h"
#include "move.h"
#include "random.h"
#include "save.h"
#include "text.h"

#define EXPLOSION_THING_FIRST 0xff80   /* thing values of explosion types; the low 7 bits are the GDAT index */
#define EXPLOSION_INDEX_BASE 0x80
#define WEAPON_MAX_CHARGES 15
#define MISC_MAX_CHARGES 3
#define POTION_POWER_MAX 0xff
#define MONEYBOX_TEXT_ENTRY 0x40       /* GDAT text (0x14, index, 5, 0x40): the coin slots of a money box */
#define ITEM_NAME_ENTRY 0x18
#define ITEM_ICON_ENTRY 0x18           /* the first icon of an item */
#define ACTION_ENTRY_FIRST 8           /* GDAT texts 8 to 11 of an item are its actions */
#define ACTION_ENTRY_LAST 11
#define FIELD_COMMAND 2
#define FIELD_CHARGES 8
#define MISC_ITEM_INDEX_BASE 0x100
#define ITEM_KIND_ITEMS 0x80           /* item indices per kind: weapons, clothing, miscellaneous, then the rest */
#define MONEY_SLOT_COUNT 10
#define COIN_SCAN_COUNT 0x80
#define TEXT_CHAMPION_TOKEN 0          /* the miscellaneous item 0 (a champion token) names its champion */
#define COINS_PER_WEIGHT 5             /* the coins of a money box weigh one unit per 5 coins, rounded up */
/* Ornament text subtypes that show their ornament as an item (the view names them SUBTYPE_*). */
enum { ORNAMENT_ITEM_STILL = 0, ORNAMENT_ITEM_ANIMATED = 2, ORNAMENT_ITEM_TOGGLE = 5, ORNAMENT_ITEM_TOGGLE_2 = 13 };
/* Item index kinds: item index / ITEM_KIND_ITEMS. */
enum { ITEM_KIND_WEAPON, ITEM_KIND_CLOTHING, ITEM_KIND_MISC };

/* GDAT category of each thing type, from SKULL.EXE. */
static const uint8_t thing_gdat_category[THING_TYPE_COUNT] = {
    0x0e, 0x18, CAT_NONE, CAT_NONE, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, CAT_NONE,
    CAT_NONE, CAT_NONE, CAT_NONE, 0x0d
};
/* The first item index of each thing type; ITEM_BASE_FIXED marks the types whose GDAT index is not added. */
#define ITEM_BASE_FIXED 0x8000
static const uint16_t item_index_base[THING_TYPE_COUNT] = {
    0x81ff, 0x81ff, 0x81ff, 0x81ff, 0x01b0, 0x0000, 0x0080, 0x81fc, 0x0180, 0x01e0, 0x0100, 0x81ff, 0x81ff, 0x81ff,
    0x81ff, 0x81ff
};
/* The slot masks (ATTR_SLOTS bits) of the champion slots 0 to 12, from SKULL.EXE. */
static const uint16_t slot_item_masks[] = {
    0x0200, 0x0100, 0x0001, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0040, 0x0040, 0x0002, 0x0020, 0x0080
};
#define SLOT_MASK_COUNT ((int)(sizeof slot_item_masks / sizeof *slot_item_masks))

static struct {
    int ready, count;
    uint16_t items[COIN_TYPE_MAX];
    int16_t values[COIN_TYPE_MAX];
} coins;

/* The 16-bit field of a thing record at byte offset offset. */
static uint16_t record_word(uint16_t thing, int offset) { return thing_word(thing, offset); }

uint8_t item_gdat_category(uint16_t thing)
{
    while (thing != THING_NONE && thing_type(thing) == THING_PROJECTILE) thing = record_word(thing, PROJECTILE_PAYLOAD);
    return thing == THING_NONE ? CAT_NONE : thing_gdat_category[thing_type(thing)];
}

uint8_t item_gdat_index(uint16_t thing)
{
    for (;;) {
        if (thing == THING_NONE) return GDAT_NO_INDEX;
        if (thing >= EXPLOSION_THING_FIRST) return (uint8_t)(thing - EXPLOSION_INDEX_BASE);
        switch (thing_type(thing)) {
        case THING_GROUP: return group_creature(thing);
        case THING_WEAPON:
        case THING_ARMOUR:
        case THING_MISC:
            return (uint8_t)item_type(thing);
        case THING_EXPLOSION: return (uint8_t)explosion_type(thing);
        case THING_SCROLL: return 0;
        case THING_POTION: return potion_type(thing);
        case THING_CONTAINER: return (uint8_t)container_gdat_index(thing);
        case THING_PROJECTILE: thing = record_word(thing, PROJECTILE_PAYLOAD); break;
        case THING_TEXT: {
            /* the ornament of a data text: subtypes 0, 2, 5, and 13 */
            int sub = text_subtype(thing);
            if (text_kind(thing) != TEXT_KIND_ORNAMENT
                || (sub != ORNAMENT_ITEM_STILL && sub != ORNAMENT_ITEM_ANIMATED && sub != ORNAMENT_ITEM_TOGGLE
                    && sub != ORNAMENT_ITEM_TOGGLE_2))
                return GDAT_NO_INDEX;
            return text_value(thing);
        }
        case THING_SENSOR: {
            int ordinal = sensor_ornament_ordinal(thing);
            return ordinal && party.cur_map >= 0 ? dungeon_map_wall_ornaments(party.cur_map)[ordinal - 1] : GDAT_NO_INDEX;
        }
        default: return GDAT_NO_INDEX;
        }
    }
}

uint16_t item_attr(uint16_t thing, int entry)
{
    if (thing == THING_NONE) return 0;
    return gdat_entry_index(draw_gdat(), item_gdat_category(thing), item_gdat_index(thing), GDAT_TYPE_VALUE,
                            (uint8_t)entry);
}

int item_charges(uint16_t thing)
{
    if (thing == THING_NONE) return 0;
    switch (thing_type(thing)) {
    case THING_WEAPON: return weapon_charges(thing);
    case THING_ARMOUR: return armour_charges(thing);
    case THING_MISC: return misc_charges(thing);
    default: return 0;
    }
}

int item_max_charges(uint16_t thing)
{
    if (thing == THING_NONE) return 0;
    int type = thing_type(thing);
    return type == THING_WEAPON || type == THING_ARMOUR ? WEAPON_MAX_CHARGES : type == THING_MISC ? MISC_MAX_CHARGES : 0;
}

int item_fits_slot(uint16_t thing, int slot, int strict)
{
    uint16_t slots = item_attr(thing, ATTR_SLOTS);
    slot = (int16_t)slot;
    if (strict) {
        if (slot >= SLOT_MASK_COUNT) return 0;
        return slots & (slot < 0 ? SLOT_MASK_HAND : slot_item_masks[slot]);
    }
    if (slot >= SLOT_HAND_COUNT && slot < SLOT_MASK_COUNT) return slots & slot_item_masks[slot];
    if (slot >= SLOT_CONTAINER_FIRST && slot < SLOT_CONTAINER_FIRST + CONTAINER_SLOTS) {
        if (slots & SLOT_MASK_NO_CONTAINER) return 0;
        if (item_fits_slot(action_open_hand_item(), SLOT_CONTAINER_TEST, 0)) return slots & SLOT_MASK_POUCH;
    }
    return 1;
}

/* Icon animation modes: bits 8-12 of ATTR_ICON_ANIMATION. */
enum {
    ICON_BY_TIME, ICON_RANDOM, ICON_BY_PARTY_DIR, ICON_BY_CHARGES, ICON_BY_CHARGES_AND_TIME, ICON_BY_TIME_PER_ITEM,
    ICON_BY_CHARGES_AND_TIME_PER_ITEM
};
#define ICON_FRAMES_MASK 0x1f
#define ICON_MODE_SHIFT 8
#define ICON_MODE_MASK 0x1f
#define ICON_STEP_MASK 0xe0
#define ICON_STEP_SHIFT 5
#define ICON_IN_SLOT 0x8000        /* the first frame shows the item in a fitting slot */
#define ICON_IN_ACTION_HAND 0x4000 /* the first frame shows the item in the open action hand */

int item_icon_index(uint16_t thing, int slot, int animated)
{
    uint8_t icon = ITEM_ICON_ENTRY;
    if (!animated) return icon;
    uint16_t attr = item_attr(thing, ATTR_ICON_ANIMATION);
    uint16_t frames = attr & ICON_FRAMES_MASK;
    if (!frames) return icon;
    int special = 0;
    if (attr & ICON_IN_SLOT && !(special = item_fits_slot(thing, slot, 1))) return icon;
    if (attr & ICON_IN_ACTION_HAND) {
        if (!action.champion || thing != action_open_hand_item()) return icon;
        special = 1;
    }
    if (special) {
        icon++;
        frames--;
    }
    if (!frames) return icon;
    int32_t time = (int32_t)game.game_time;
    int mode = attr >> ICON_MODE_SHIFT & ICON_MODE_MASK;
    switch (mode) {
    case ICON_BY_TIME_PER_ITEM: time += thing & THING_INDEX_MASK; /* fall through */
    case ICON_BY_TIME: return (uint8_t)(icon + time % frames);
    case ICON_RANDOM: return (uint8_t)(icon + rand_below(frames));
    case ICON_BY_PARTY_DIR: return (uint8_t)(icon + game.party_dir);
    case ICON_BY_CHARGES: {
        int charges = item_charges(thing);
        if (!charges) return icon;
        return (uint8_t)(icon + charges * frames / (item_max_charges(thing) + 1) + 1);
    }
    case ICON_BY_CHARGES_AND_TIME_PER_ITEM: time += thing & THING_INDEX_MASK; /* fall through */
    case ICON_BY_CHARGES_AND_TIME: {
        int step = (attr & ICON_STEP_MASK) >> ICON_STEP_SHIFT;
        int charges = item_charges(thing);
        if (!charges) return icon;
        int frame = frames / step * charges / (item_max_charges(thing) + 1) * step + 1 + (uint16_t)(time % step);
        return (uint8_t)(icon + frame);
    }
    default: return icon;
    }
}

bool item_is_moneybox(uint16_t thing)
{
    if (thing == THING_NONE || thing_type(thing) != THING_CONTAINER || container_kind(thing)) return 0;
    return gdat_find(draw_gdat(), CAT_CONTAINER, item_gdat_index(thing), GDAT_TYPE_TEXT, MONEYBOX_TEXT_ENTRY)
           != -1;
}

bool item_is_chest(uint16_t thing)
{
    if (thing == THING_NONE || thing_type(thing) != THING_CONTAINER || item_is_moneybox(thing)) return 0;
    return !container_kind(thing);
}

bool item_is_minion_map(uint16_t thing)
{
    return thing != THING_NONE && thing_type(thing) == THING_CONTAINER
           && container_kind(thing) == CONTAINER_MINION_MAP;
}

/* The sum of attribute entry over thing and its contents; a money box counts coins by their stack size.
   Weight and value add a part per charge; the value of a potion scales with its power. */
static int item_sum_attr(uint16_t thing, int entry)
{
    if (thing == THING_NONE) return 0;
    int sum = item_attr(thing, entry);
    if (entry == ATTR_WEIGHT || entry == ATTR_COIN_VALUE) {
        uint16_t per_charge = item_attr(thing, entry == ATTR_WEIGHT ? ATTR_WEIGHT_PER_CHARGE : ATTR_VALUE_PER_CHARGE);
        if (per_charge) sum += item_charges(thing) * per_charge;
    }
    if (entry == ATTR_COIN_VALUE && thing_type(thing) == THING_POTION && sum > 1)
        sum = (sum >> 1) + (int)(potion_power(thing) * (unsigned)(sum >> 1) / POTION_POWER_MAX);
    if (thing_type(thing) != THING_CONTAINER || container_kind(thing)) return sum;
    int moneybox = item_is_moneybox(thing), coin_sum = 0;
    for (uint16_t t = record_word(thing, CONTAINER_CONTENTS); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (!moneybox || thing_type(t) != THING_MISC) {
            sum += item_sum_attr(t, entry);
        } else {
            int stack = misc_stack(t) + 1;
            coin_sum += item_attr(t, entry) * stack;
        }
    }
    if (moneybox) sum += entry == ATTR_WEIGHT ? (coin_sum + COINS_PER_WEIGHT - 1) / COINS_PER_WEIGHT : coin_sum;
    return sum;
}

int item_weight(uint16_t thing) { return item_sum_attr(thing, ATTR_WEIGHT); }

uint16_t item_get_value(uint16_t thing)
{
    int v = item_sum_attr(thing, ATTR_COIN_VALUE);
    return (uint16_t)(v > UINT16_MAX ? UINT16_MAX : v);
}

void item_set_do_not_discard(uint16_t thing, int keep)
{
    switch (thing_type(thing)) {
    case THING_WEAPON: case THING_ARMOUR: case THING_MISC: case THING_POTION:
        item_set_kept(thing, keep & 1);
        break;
    case THING_GROUP:
        group_set_flag(thing, GROUP_DO_NOT_DISCARD, keep & 1);
        break;
    default: break;
    }
}

bool item_has_charged_action(uint16_t thing)
{
    if (thing == THING_NONE) return 0;
    int category = item_gdat_category(thing), index = item_gdat_index(thing);
    for (int entry = ACTION_ENTRY_FIRST; entry <= ACTION_ENTRY_LAST; entry++)
        if (gdat_exists(draw_gdat(), category, index, GDAT_TYPE_TEXT, entry)
            && text_gdat_field(category, index, entry, FIELD_COMMAND)
            && text_gdat_field(category, index, entry, FIELD_CHARGES))
            return 1;
    return 0;
}

const char *item_name(uint16_t thing)
{
    static char name[TEXT_SIZE];
    int category = item_gdat_category(thing), index = item_gdat_index(thing);
    text_args.champion = -1;
    if (category == CAT_MISC && index == TEXT_CHAMPION_TOKEN) {
        int champion = misc_charges(thing);  /* the champion of the token */
        if (champion < game.champion_count) text_args.champion = (int16_t)champion;
    }
    char raw[TEXT_SIZE];
    return text_format(text_gdat(category, index, ITEM_NAME_ENTRY, raw), name);
}

uint16_t minion_find_link_projectile(uint16_t group, uint16_t filter)
{
    if (group == THING_NONE || group == THING_ENDOFLIST || record_word(group, THING_NEXT) == THING_NONE) return 0;
    for (uint16_t t = record_word(group, GROUP_POSSESSIONS); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        if (thing_type(t) == THING_PROJECTILE && (filter == THING_NONE || filter == record_word(t, PROJECTILE_PAYLOAD)))
            return t;
    return 0;
}

void money_init_coin_table(void)
{
    if (coins.ready) return;
    coins.ready = 1;
    for (int index = 0; index < COIN_SCAN_COUNT; index++) {
        if (!(gdat_entry_index(draw_gdat(), CAT_MISC, index, GDAT_TYPE_VALUE, ATTR_FLAGS) & ITEM_FLAG_COIN))
            continue;
        int16_t value = (int16_t)gdat_entry_index(draw_gdat(), CAT_MISC, index, GDAT_TYPE_VALUE, ATTR_COIN_VALUE);
        int at = 0;
        while (at < coins.count && coins.values[at] < value) at++;
        if (coins.count == COIN_TYPE_MAX) break;
        memmove(coins.values + at + 1, coins.values + at, (coins.count - at) * sizeof *coins.values);
        memmove(coins.items + at + 1, coins.items + at, (coins.count - at) * sizeof *coins.items);
        coins.values[at] = value;
        coins.items[at] = (uint16_t)(MISC_ITEM_INDEX_BASE + index);
        coins.count++;
    }
}

int money_coin_type_count(void) { return coins.count; }
uint16_t money_coin_item(int coin_type) { return coins.items[coin_type]; }
int16_t money_coin_value(int coin_type) { return coins.values[coin_type]; }

bool money_is_coin(uint16_t thing)
{
    return thing_type(thing) == THING_MISC && item_attr(thing, ATTR_FLAGS) & ITEM_FLAG_COIN;
}

uint16_t item_get_index(uint16_t thing)
{
    if (thing == THING_NONE) return ITEM_INDEX_NONE;
    uint8_t index = item_gdat_index(thing);
    uint16_t base = item_index_base[thing_type(thing)];
    if (base & ITEM_BASE_FIXED) {
        base &= ~ITEM_BASE_FIXED;
        index = 0;
    }
    return (uint16_t)(base + index);
}

void money_count_coins_in_box(uint16_t box, int16_t counts[COIN_TYPE_MAX])
{
    memset(counts, 0, COIN_TYPE_MAX * sizeof *counts);
    for (uint16_t t = record_word(box, CONTAINER_CONTENTS); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (!money_is_coin(t)) continue;
        uint16_t item = item_get_index(t);
        for (int c = 0; c < coins.count; c++)
            if (item == coins.items[c]) counts[c] += misc_stack(t) + 1;
    }
}

int money_coin_type_for_slot(uint16_t box, int slot)
{
    char text[TEXT_SIZE];
    const char *p = text_gdat(CAT_CONTAINER, item_gdat_index(box), MONEYBOX_TEXT_ENTRY, text);
    if (!*p) return -1;
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
                base = MISC_ITEM_INDEX_BASE;
            } else {
                if (c == 'J') p--;
                if (c == '\0') done = 1;
                break;
            }
        }
        if (low < 0) low = number;
        for (; low <= number; low++, counter++)
            if (counter == slot)
                for (int t = 0; t < coins.count; t++)
                    if ((int16_t)coins.items[t] == base + low) return t;
        number = 0;
    }
    return -1;
}

uint8_t item_index_gdat_index(uint16_t item)
{
    item &= ITEM_INDEX_NONE;
    switch (item / ITEM_KIND_ITEMS) {
    case ITEM_KIND_WEAPON: return (uint8_t)item;
    case ITEM_KIND_CLOTHING: return (uint8_t)(item - ITEM_FIRST_CLOTHING);
    case ITEM_KIND_MISC: return (uint8_t)(item - ITEM_FIRST_MISC);
    default:
        if (item >= ITEM_SCROLL) return 0;
        if (item >= ITEM_FIRST_CONTAINER) return (uint8_t)(item - ITEM_FIRST_CONTAINER);
        if (item >= ITEM_FIRST_CREATURE) return (uint8_t)(item - ITEM_FIRST_CREATURE);
        return (uint8_t)(item - ITEM_FIRST_POTION);
    }
}

uint8_t item_index_gdat_category(uint16_t item)
{
    static const uint8_t kind_type[] = {THING_WEAPON, THING_ARMOUR, THING_MISC};
    item &= ITEM_INDEX_NONE;
    int type;
    if (item < ITEM_FIRST_POTION) type = kind_type[item / ITEM_KIND_ITEMS];
    else if (item > ITEM_SCROLL) type = THING_EXPLOSION;  /* the original reads an item index of no type like this */
    else if (item == ITEM_SCROLL) type = THING_SCROLL;
    else if (item >= ITEM_FIRST_CONTAINER) type = THING_CONTAINER;
    else if (item >= ITEM_FIRST_CREATURE) type = THING_GROUP;
    else type = THING_POTION;
    return thing_gdat_category[type];
}

int item_add_charges(uint16_t thing, int delta)
{
    if (thing == THING_NONE) return 0;
    int max = item_max_charges(thing);
    if (!max) return 0;  /* fix: the original returns a register of no meaning */
    int n = (int16_t)(item_charges(thing) + delta);
    n = n < 0 ? 0 : n > max ? max : n;
    switch (thing_type(thing)) {
    case THING_WEAPON: weapon_set_charges(thing, (uint16_t)n); break;
    case THING_ARMOUR: armour_set_charges(thing, (uint16_t)n); break;
    default: misc_set_charges(thing, (uint16_t)n); break;
    }
    return n;
}

void item_set_gdat_index(uint16_t thing, int index)
{
    if (thing == THING_NONE || thing >= EXPLOSION_THING_FIRST) return;
    switch (thing_type(thing)) {
    case THING_GROUP: group_set_creature(thing, (uint8_t)index); break;
    case THING_WEAPON: case THING_ARMOUR: case THING_MISC: item_set_type(thing, (uint16_t)index); break;
    case THING_POTION: potion_set_type(thing, (uint16_t)index); break;
    case THING_CONTAINER: {
        int old_kind = container_kind(thing);
        container_set_gdat_index(thing, (uint16_t)index);
        if (old_kind == CONTAINER_MINION_MAP) set_thing_word(thing, MINION_MAP_LOCATION, THING_NONE);
        break;
    }
    }
}
