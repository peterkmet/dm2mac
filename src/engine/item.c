/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "item.h"
#include <stdio.h>
#include <string.h>
#include "action.h"
#include "champion.h"
#include "dungeon.h"
#include "game_text.h"
#include "gdat.h"
#include "ids.h"
#include "move.h"
#include "projectile.h"
#include "random.h"
#include "save.h"
#include "sensor.h"

/* Explosion things: thing values of explosion types; the low 7 bits are the GDAT index. */
#define EXPLOSION_THING_FIRST    0xff80
#define EXPLOSION_INDEX_BASE     0x0080
/* Charges and item index kinds. */
#define WEAPON_MAX_CHARGES       15
#define MISC_MAX_CHARGES         3
#define ITEM_KIND_ITEMS          128 /* item indices per kind: weapons, clothing, miscellaneous, then the rest */
#define COINS_PER_WEIGHT         5   /* the coins of a money box weigh one unit per 5 coins, rounded up */
/* The miscellaneous item 0 (a champion token) names its champion. */
#define TEXT_CHAMPION_TOKEN      0
/* The ammunition kinds of ATTR_AMMO. */
#define AMMO_KINDS               0x7fff
/* Item index kinds: item index / ITEM_KIND_ITEMS. */
#define ITEM_KIND_WEAPON         0
#define ITEM_KIND_CLOTHING       1
#define ITEM_KIND_MISC           2
/* Icon animation modes: bits 8-12 of ATTR_ICON_ANIMATION. */
#define ICON_BY_TIME             0
#define ICON_RANDOM              1
#define ICON_BY_PARTY_DIR        2
#define ICON_BY_CHARGES          3
#define ICON_BY_CHARGES_AND_TIME 4
#define ICON_BY_TIME_PER_ITEM    5
#define ICON_BY_CHARGES_AND_TIME_PER_ITEM 6
/* Fields of ATTR_ICON_ANIMATION. */
#define ICON_FRAMES_MASK                  0x001f
#define ICON_MODE_MASK                    0x001f
#define ICON_STEP_MASK                    0x00e0
#define ICON_IN_SLOT                      0x8000 /* the first frame shows the item in a fitting slot */
#define ICON_IN_ACTION_HAND               0x4000 /* the first frame shows the item in the open action hand */
/* Shifts of the fields of ATTR_ICON_ANIMATION. */
#define ICON_MODE_SHIFT                   8
#define ICON_STEP_SHIFT                   5

/* GDAT category of each thing type, from SKULL.EXE. */
static const uint8_t thing_gdat_category[THING_TYPE_COUNT] = {0x0e,     0x18,     CAT_NONE, CAT_NONE, 0x0f, 0x10,
                                                              0x11,     0x12,     0x13,     0x14,     0x15, CAT_NONE,
                                                              CAT_NONE, CAT_NONE, CAT_NONE, 0x0d};
/* The first item index of each thing type; ITEM_BASE_FIXED marks the types whose GDAT index is not added. */
#define ITEM_BASE_FIXED 0x8000
static const uint16_t item_index_base[THING_TYPE_COUNT] = {0x81ff, 0x81ff, 0x81ff, 0x81ff, 0x01b0, 0x0000,
                                                           0x0080, 0x81fc, 0x0180, 0x01e0, 0x0100, 0x81ff,
                                                           0x81ff, 0x81ff, 0x81ff, 0x81ff};
/* The slot masks (ATTR_SLOTS bits) of the champion slots 0 to 12, from SKULL.EXE. */
static const uint16_t slot_item_masks[] = {0x0200, 0x0100, 0x0001, 0x0004, 0x0008, 0x0010, 0x0020,
                                           0x0040, 0x0040, 0x0040, 0x0002, 0x0020, 0x0080};
/* The champion slots that have a slot mask. */
#define SLOT_MASK_COUNT ((int)(sizeof slot_item_masks / sizeof *slot_item_masks))

/* The text that item_name returns. */
static struct {
    char name[TEXT_SIZE];
} items;

/* GDAT keys of things and item indices */

uint8_t item_gdat_category(uint16_t thing)
{
    while (thing != THING_NONE && thing_type(thing) == THING_PROJECTILE)
        thing = thing_word(thing, PROJECTILE_PAYLOAD);
    return thing == THING_NONE ? CAT_NONE : thing_gdat_category[thing_type(thing)];
}

uint8_t item_gdat_index(uint16_t thing)
{
    for (;;) {
        if (thing == THING_NONE)
            return GDAT_NO_INDEX;
        if (thing >= EXPLOSION_THING_FIRST)
            return (uint8_t)(thing - EXPLOSION_INDEX_BASE);
        switch (thing_type(thing)) {
        case THING_GROUP:
            return group_creature(thing);
        case THING_WEAPON:
        case THING_ARMOUR:
        case THING_MISC:
            return (uint8_t)item_type(thing);
        case THING_EXPLOSION:
            return (uint8_t)explosion_type(thing);
        case THING_SCROLL:
            return 0;
        case THING_POTION:
            return potion_type(thing);
        case THING_CONTAINER:
            return (uint8_t)container_gdat_index(thing);
        case THING_PROJECTILE:
            thing = thing_word(thing, PROJECTILE_PAYLOAD);
            break;
        case THING_TEXT: {
            /* the ornament of a data text: subtypes 0, 2, 5, and 13 */
            int sub = textrec_subtype(thing);
            if (textrec_kind(thing) != TEXT_KIND_ORNAMENT || (sub != SUBTYPE_STILL && sub != SUBTYPE_ANIMATED &&
                                                              sub != SUBTYPE_TOGGLE && sub != SUBTYPE_HAND_SWITCH))
                return GDAT_NO_INDEX;
            return textrec_value(thing);
        }
        case THING_SENSOR: {
            int index = sensor_ornament_index(thing);
            return index >= 0 && g_party.cur_map >= 0 ? dungeon_map_wall_ornaments(g_party.cur_map)[index]
                                                      : GDAT_NO_INDEX;
        }
        default:
            return GDAT_NO_INDEX;
        }
    }
}

uint16_t item_attr(uint16_t thing, int entry)
{
    if (thing == THING_NONE)
        return 0;
    return gdat_entry_index(gdat_current(), item_gdat_category(thing), item_gdat_index(thing), GDAT_TYPE_VALUE,
                            (uint8_t)entry);
}

uint16_t item_get_index(uint16_t thing)
{
    if (thing == THING_NONE)
        return ITEM_INDEX_NONE;
    uint8_t index = item_gdat_index(thing);
    uint16_t base = item_index_base[thing_type(thing)];
    if (base & ITEM_BASE_FIXED) {
        base &= ~ITEM_BASE_FIXED;
        index = 0;
    }
    return (uint16_t)(base + index);
}

uint8_t item_index_gdat_index(uint16_t item)
{
    item &= ITEM_INDEX_NONE;
    switch (item / ITEM_KIND_ITEMS) {
    case ITEM_KIND_WEAPON:
        return (uint8_t)item;
    case ITEM_KIND_CLOTHING:
        return (uint8_t)(item - ITEM_FIRST_CLOTHING);
    case ITEM_KIND_MISC:
        return (uint8_t)(item - ITEM_FIRST_MISC);
    default:
        if (item >= ITEM_SCROLL)
            return 0;
        if (item >= ITEM_FIRST_CONTAINER)
            return (uint8_t)(item - ITEM_FIRST_CONTAINER);
        if (item >= ITEM_FIRST_CREATURE)
            return (uint8_t)(item - ITEM_FIRST_CREATURE);
        return (uint8_t)(item - ITEM_FIRST_POTION);
    }
}

uint8_t item_index_gdat_category(uint16_t item)
{
    static const uint8_t kind_type[] = {THING_WEAPON, THING_ARMOUR, THING_MISC};
    item &= ITEM_INDEX_NONE;
    int type;
    if (item < ITEM_FIRST_POTION)
        type = kind_type[item / ITEM_KIND_ITEMS];
    else if (item > ITEM_SCROLL)
        type = THING_EXPLOSION; /* the original reads an item index of no type like this */
    else if (item == ITEM_SCROLL)
        type = THING_SCROLL;
    else if (item >= ITEM_FIRST_CONTAINER)
        type = THING_CONTAINER;
    else if (item >= ITEM_FIRST_CREATURE)
        type = THING_GROUP;
    else
        type = THING_POTION;
    return thing_gdat_category[type];
}

int item_index_to_thing_type(uint16_t item)
{
    item &= ITEM_INDEX_NONE;
    if (item < ITEM_FIRST_CLOTHING)
        return THING_WEAPON;
    if (item < ITEM_FIRST_MISC)
        return THING_ARMOUR;
    if (item < ITEM_FIRST_POTION)
        return THING_MISC;
    if (item > ITEM_SCROLL)
        return -1;
    if (item == ITEM_SCROLL)
        return THING_SCROLL;
    if (item >= ITEM_FIRST_CONTAINER)
        return THING_CONTAINER;
    if (item >= ITEM_FIRST_CREATURE)
        return THING_GROUP;
    return THING_POTION;
}

/* Item queries */

int item_charges(uint16_t thing)
{
    if (thing == THING_NONE)
        return 0;
    switch (thing_type(thing)) {
    case THING_WEAPON:
        return weapon_charges(thing);
    case THING_ARMOUR:
        return armour_charges(thing);
    case THING_MISC:
        return misc_charges(thing);
    default:
        return 0;
    }
}

int item_max_charges(uint16_t thing)
{
    if (thing == THING_NONE)
        return 0;
    int type = thing_type(thing);
    return type == THING_WEAPON || type == THING_ARMOUR ? WEAPON_MAX_CHARGES
           : type == THING_MISC                         ? MISC_MAX_CHARGES
                                                        : 0;
}

bool item_fits_slot(uint16_t thing, int16_t slot, bool strict)
{
    uint16_t slots = item_attr(thing, ATTR_SLOTS);
    if (strict) {
        if (slot >= SLOT_MASK_COUNT)
            return false;
        return slots & (slot < 0 ? SLOT_MASK_HAND : slot_item_masks[slot]);
    }
    if (slot >= SLOT_HAND_COUNT && slot < SLOT_MASK_COUNT)
        return slots & slot_item_masks[slot];
    if (slot >= SLOT_CONTAINER_FIRST && slot < SLOT_CONTAINER_FIRST + CONTAINER_SLOTS) {
        if (slots & SLOT_MASK_NO_CONTAINER)
            return false;
        if (item_fits_slot(action_open_hand_item(), SLOT_CONTAINER_TEST, false))
            return slots & SLOT_MASK_POUCH;
    }
    return true;
}

int item_icon_index(uint16_t thing, int16_t slot, bool animated)
{
    uint8_t icon = GFX_ITEM_ICON;
    if (!animated)
        return icon;
    uint16_t attr = item_attr(thing, ATTR_ICON_ANIMATION);
    uint16_t frames = attr & ICON_FRAMES_MASK;
    if (!frames)
        return icon;
    bool special = false;
    if (attr & ICON_IN_SLOT && !(special = item_fits_slot(thing, slot, true)))
        return icon;
    if (attr & ICON_IN_ACTION_HAND) {
        if (!g_action.champion || thing != action_open_hand_item())
            return icon;
        special = true;
    }
    if (special) {
        icon++;
        frames--;
    }
    if (!frames)
        return icon;
    int32_t time = (int32_t)g_game.game_time;
    int mode = attr >> ICON_MODE_SHIFT & ICON_MODE_MASK;
    switch (mode) {
    case ICON_BY_TIME_PER_ITEM:
        time += thing & THING_INDEX_MASK;
        __attribute__((fallthrough));
    case ICON_BY_TIME:
        return (uint8_t)(icon + time % frames);
    case ICON_RANDOM:
        return (uint8_t)(icon + rand_below(frames));
    case ICON_BY_PARTY_DIR:
        return (uint8_t)(icon + g_game.party_dir);
    case ICON_BY_CHARGES: {
        int charges = item_charges(thing);
        if (!charges)
            return icon;
        return (uint8_t)(icon + charges * frames / (item_max_charges(thing) + 1) + 1);
    }
    case ICON_BY_CHARGES_AND_TIME_PER_ITEM:
        time += thing & THING_INDEX_MASK;
        __attribute__((fallthrough));
    case ICON_BY_CHARGES_AND_TIME: {
        int step = (attr & ICON_STEP_MASK) >> ICON_STEP_SHIFT;
        int charges = item_charges(thing);
        if (!charges || !step)
            return icon; /* fix: the original divides by a step of 0 */
        int frame = frames / step * charges / (item_max_charges(thing) + 1) * step + 1 + (uint16_t)(time % step);
        return (uint8_t)(icon + frame);
    }
    default:
        return icon;
    }
}

bool item_is_moneybox(uint16_t thing)
{
    if (thing == THING_NONE || thing_type(thing) != THING_CONTAINER || container_kind(thing))
        return false;
    return gdat_find(gdat_current(), CAT_CONTAINER, item_gdat_index(thing), GDAT_TYPE_TEXT, GFX_MONEYBOX_TEXT) != -1;
}

bool item_is_chest(uint16_t thing)
{
    if (thing == THING_NONE || thing_type(thing) != THING_CONTAINER || item_is_moneybox(thing))
        return false;
    return !container_kind(thing);
}

bool item_is_minion_map(uint16_t thing)
{
    return thing != THING_NONE && thing_type(thing) == THING_CONTAINER && container_kind(thing) == CONTAINER_MINION_MAP;
}

/* The sum of attribute entry over thing and its contents; a money box counts coins by their stack size.
   Weight and value add a part per charge; the value of a potion scales with its power. */
static int sum_attr(uint16_t thing, int entry)
{
    if (thing == THING_NONE)
        return 0;
    int sum = item_attr(thing, entry);
    if (entry == ATTR_WEIGHT || entry == ATTR_VALUE) {
        uint16_t per_charge = item_attr(thing, entry == ATTR_WEIGHT ? ATTR_WEIGHT_PER_CHARGE : ATTR_VALUE_PER_CHARGE);
        if (per_charge)
            sum += item_charges(thing) * per_charge;
    }
    if (entry == ATTR_VALUE && thing_type(thing) == THING_POTION && sum > 1)
        sum = (sum >> 1) + (int)(potion_power(thing) * (unsigned)(sum >> 1) / POTION_POWER_MAX);
    if (thing_type(thing) != THING_CONTAINER || container_kind(thing))
        return sum;
    bool moneybox = item_is_moneybox(thing);
    int coin_sum = 0;
    for (uint16_t t = thing_word(thing, CONTAINER_CONTENTS); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (!moneybox || thing_type(t) != THING_MISC) {
            sum += sum_attr(t, entry);
        } else {
            int stack = misc_stack(t) + 1;
            coin_sum += item_attr(t, entry) * stack;
        }
    }
    if (moneybox)
        sum += entry == ATTR_WEIGHT ? (coin_sum + COINS_PER_WEIGHT - 1) / COINS_PER_WEIGHT : coin_sum;
    return sum;
}

int item_weight(uint16_t thing)
{
    return sum_attr(thing, ATTR_WEIGHT);
}

uint16_t item_get_value(uint16_t thing)
{
    int v = sum_attr(thing, ATTR_VALUE);
    return (uint16_t)(v > UINT16_MAX ? UINT16_MAX : v);
}

bool item_has_charged_action(uint16_t thing)
{
    if (thing == THING_NONE)
        return false;
    int category = item_gdat_category(thing);
    int index = item_gdat_index(thing);
    for (int entry = GFX_ACTION_FIRST; entry <= GFX_ACTION_LAST; entry++)
        if (gdat_exists(gdat_current(), category, index, GDAT_TYPE_TEXT, entry) &&
            gtext_gdat_field(category, index, entry, FIELD_COMMAND) &&
            gtext_gdat_field(category, index, entry, FIELD_CHARGES))
            return true;
    return false;
}

const char *item_name(uint16_t thing)
{
    int category = item_gdat_category(thing);
    int index = item_gdat_index(thing);
    g_text_args.champion = -1;
    if (category == CAT_MISC && index == TEXT_CHAMPION_TOKEN) {
        int champion = misc_charges(thing); /* the champion of the token */
        if (champion < g_game.champion_count)
            g_text_args.champion = (int16_t)champion;
    }
    char raw[TEXT_SIZE];
    /* gtext_gdat formats the name and gtext_format formats it a second time, as in the original */
    return gtext_format(gtext_gdat(category, index, GFX_ITEM_NAME, raw), items.name);
}

/* Item commands */

int item_add_charges(uint16_t thing, int delta)
{
    if (thing == THING_NONE)
        return 0;
    int max = item_max_charges(thing);
    if (!max)
        return 0; /* fix: the original returns a register of no meaning */
    int n = (int16_t)(item_charges(thing) + delta);
    n = n < 0 ? 0 : n > max ? max : n;
    switch (thing_type(thing)) {
    case THING_WEAPON:
        weapon_set_charges(thing, (uint16_t)n);
        break;
    case THING_ARMOUR:
        armour_set_charges(thing, (uint16_t)n);
        break;
    default:
        misc_set_charges(thing, (uint16_t)n);
        break;
    }
    return n;
}

void item_set_gdat_index(uint16_t thing, int index)
{
    if (thing == THING_NONE || thing >= EXPLOSION_THING_FIRST)
        return;
    switch (thing_type(thing)) {
    case THING_GROUP:
        group_set_creature(thing, (uint8_t)index);
        break;
    case THING_WEAPON:
    case THING_ARMOUR:
    case THING_MISC:
        item_set_type(thing, (uint16_t)index);
        break;
    case THING_POTION:
        potion_set_type(thing, (uint16_t)index);
        break;
    case THING_CONTAINER: {
        int old_kind = container_kind(thing);
        container_set_gdat_index(thing, (uint16_t)index);
        if (old_kind == CONTAINER_MINION_MAP)
            thing_set_word(thing, MINION_MAP_LOCATION, THING_NONE);
        break;
    }
    }
}

void item_set_do_not_discard(uint16_t thing, bool keep)
{
    switch (thing_type(thing)) {
    case THING_WEAPON:
    case THING_ARMOUR:
    case THING_MISC:
    case THING_POTION:
        item_set_kept(thing, keep);
        break;
    case THING_GROUP:
        group_set_flag(thing, GROUP_DO_NOT_DISCARD, keep);
        break;
    default:
        break;
    }
}

uint16_t item_create_by_index(uint16_t item)
{
    int type = item_index_to_thing_type(item);
    if (type < 0)
        return THING_NONE; /* fix: the original asks for a record of the type 0xffff */
    /* when the records run out, the dungeon reuses a discarded item */
    uint16_t thing = item & ITEM_ANY_RECORD ? dungeon_unused_thing_any_record(type) : dungeon_unused_thing(type);
    if (thing != THING_NONE)
        item_set_gdat_index(thing, item_index_gdat_index(item));
    return thing;
}

/* Minion links. */

uint16_t item_find_link_projectile(uint16_t group, uint16_t payload)
{
    if (group == THING_NONE || group == THING_ENDOFLIST || thing_word(group, THING_NEXT) == THING_NONE)
        return THING_NONE;
    for (uint16_t t = thing_word(group, GROUP_POSSESSIONS); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        if (thing_type(t) == THING_PROJECTILE &&
            (payload == THING_NONE || payload == thing_word(t, PROJECTILE_PAYLOAD)))
            return t;
    return THING_NONE;
}

/* Items of the champion actions. */

bool item_is_ammo_for_hand(int champ, int hand, uint16_t thing)
{
    uint16_t launcher = champion(champ)->slots[hand];
    if (thing == THING_NONE || launcher == THING_NONE)
        return false;
    uint16_t kinds = item_attr(launcher, ATTR_AMMO);
    if (!(kinds & AMMO_LAUNCHER))
        return false;
    uint16_t ammo = item_attr(thing, ATTR_AMMO);
    return !(ammo & AMMO_LAUNCHER) && ammo & AMMO_KINDS & kinds;
}

bool item_has_action_command(int champ, uint16_t thing, int command)
{
    bool found = false;
    if (thing == THING_NONE)
        return false;
    if (champ_list_actions(champ, thing, -1)) {
        for (int i = 0; i < g_champ_action_count; i++)
            if ((uint16_t)gtext_gdat_field(g_champ_actions[i].category, g_champ_actions[i].index,
                                           g_champ_actions[i].entry, FIELD_COMMAND) == (uint16_t)command) {
                found = true;
                break;
            }
    }
    if (g_action.champion && (g_action.hand == SLOT_READY_HAND || g_action.hand == SLOT_ACTION_HAND))
        champ_list_actions(g_action.champion - 1, champion(g_action.champion - 1)->slots[g_action.hand], g_action.hand);
    return found;
}

/* Item charges. */

bool item_use_charge(uint16_t thing)
{
    if (!(item_attr(thing, ATTR_FLAGS) & ITEM_FLAG_REFILLS) || !item_charges(thing))
        return false;
    item_add_charges(thing, -1);
    if (thing == g_game.leader_hand)
        champ_put_in_leader_hand(thing);
    return true;
}
