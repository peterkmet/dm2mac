/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_ITEM_H
#define DM2_ENGINE_ITEM_H
/* Prefixes: item_ */

/* The items: their record fields, their GDAT types, and what the party possesses. */

#include "dungeon.h"
#include <stdbool.h>
#include <stdint.h>

/* Record fields of the things of this module; dungeon.h holds the record layout. */
/* The type (GDAT index) of a weapon, armour, or miscellaneous item. */
static inline uint16_t item_type(uint16_t item)
{
    return thing_field(item, ITEM_DATA, 0, ITEM_TYPE_MASK);
}
static inline void item_set_type(uint16_t item, uint16_t type)
{
    set_thing_field(item, ITEM_DATA, 0, ITEM_TYPE_MASK, type);
}
/* The dungeon keeps the item: no record reuse, and creatures do not consume it. */
static inline bool item_is_kept(uint16_t item)
{
    return thing_bit(item, ITEM_DATA, thing_type(item) == THING_POTION ? POTION_DO_NOT_DISCARD : ITEM_DO_NOT_DISCARD);
}
static inline void item_set_kept(uint16_t item, bool kept)
{
    set_thing_bit(item, ITEM_DATA, thing_type(item) == THING_POTION ? POTION_DO_NOT_DISCARD : ITEM_DO_NOT_DISCARD,
                  kept);
}

#define GDAT_NO_INDEX        0xff
#define ITEM_INDEX_NONE      0x1ff /* item index of no thing */
/* Item index ranges. */
#define ITEM_FIRST_CLOTHING  0x80
#define ITEM_FIRST_MISC      0x100
#define ITEM_FIRST_POTION    0x180
#define ITEM_FIRST_CREATURE  0x1b0
#define ITEM_FIRST_CONTAINER 0x1e0
#define ITEM_SCROLL          0x1fc
#define ITEM_BONES           0 /* miscellaneous item 0 (CAT_MISC index): the bones of a champion */

/* Item attribute entries: GDAT value keys (category, index, 0x0b, entry). */
enum item_attr {
    ATTR_FLAGS = 0,
    ATTR_WEIGHT = 1,
    ATTR_VALUE = 2,
    ATTR_SLOTS = 4,
    ATTR_ICON_ANIMATION = 6,
    ATTR_ARMOUR_CLASS = 0x0b,
    ATTR_STRENGTH = 8,
    ATTR_THROW_POWER = 9,
    ATTR_WEIGHT_PER_CHARGE = 0x34,
    ATTR_VALUE_PER_CHARGE = 0x35,
    ATTR_AMMO = 5,
    ATTR_EXTRA_DAMAGE = 0x0d, /* extra damage of a hit: armour piercing against creatures, poison against champions */
    ATTR_LIGHT_SOURCE = 0xf8  /* item and creature attribute: the light that the thing gives */
};
/* ATTR_FLAGS bits. */
#define ITEM_FLAG_COIN         0x4000
#define ITEM_FLAG_ICON_IN_HAND 0x8000 /* the icon changes with the slot */
#define ITEM_FLAG_SCROLL_INFO  0x20   /* the inventory shows the object info while it is in the hand */
#define ITEM_FLAG_REFILLS      0x0001 /* the item holds water */
/* ATTR_AMMO bits. */
#define AMMO_LAUNCHER          0x8000 /* a launcher; the low bits are the ammunition kinds */
/* ATTR_SLOTS bits. */
#define SLOT_MASK_NO_CONTAINER 0x8000 /* the item does not go into a container */
#define SLOT_MASK_POUCH        0x40
#define SLOT_MASK_HAND         0x400
#define POTION_POWER_MAX       255

/* GDAT texts (category, index, 5, entry) of an item: its name and its actions. */
#define ITEM_NAME_ENTRY    0x18
#define ACTION_ENTRY_FIRST 8
#define ACTION_ENTRY_LAST  0x0b

/* The GDAT category of a thing; a projectile gives the category of what it carries. */
uint8_t item_gdat_category(uint16_t thing);
/* The GDAT index of an item or creature; a projectile gives the index of what it carries. */
uint8_t item_gdat_index(uint16_t thing);
/* The attribute entry of an item, 0 for no thing. */
uint16_t item_attr(uint16_t thing, int entry);
/* The item index of a thing: the base of its type plus its GDAT index, ITEM_INDEX_NONE for no thing. */
uint16_t item_get_index(uint16_t thing);
/* Charges of a weapon, clothing, or miscellaneous item; 0 for other things. */
int item_charges(uint16_t thing);
/* Adds delta to the charges of a weapon, clothing, or miscellaneous item (clamped); returns the new charges. */
int item_add_charges(uint16_t thing, int delta);
/* Changes the GDAT index (the kind) of an item or creature group. */
void item_set_gdat_index(uint16_t thing, int index);
/* The highest charge count of the thing type. */
int item_max_charges(uint16_t thing);
/* The value of a thing with its contents (attribute 2), at most 0xffff. */
uint16_t item_get_value(uint16_t thing);
/* Sets or clears the bit that keeps a thing from being reused when the dungeon runs out of records. */
void item_set_do_not_discard(uint16_t thing, int keep);
/* True when thing may go into slot. strict: only the champion slots 0 to 12 (or -1, a hand) count. */
int item_fits_slot(uint16_t thing, int slot, int strict);
/* The icon entry of an item in slot (animated: by its icon animation attribute). */
int item_icon_index(uint16_t thing, int slot, int animated);
/* Container kinds: money box (a chest with coin slots), chest, minion map. */
bool item_is_moneybox(uint16_t thing);
bool item_is_chest(uint16_t thing);
bool item_is_minion_map(uint16_t thing);
/* The weight of a thing in 1/10 kg, with its contents. */
int item_weight(uint16_t thing);
/* True when one of the actions of the item uses charges. */
bool item_has_charged_action(uint16_t thing);
/* The name of an item: GDAT text (category, index, 0x18) with its format codes. Uses one static buffer. */
const char *item_name(uint16_t thing);

/* Money: the coin types are the miscellaneous items with ITEM_FLAG_COIN, by value. */
#define COIN_TYPE_MAX 10
/* GDAT index and category of an item index. */
uint8_t item_index_gdat_index(uint16_t item);
uint8_t item_index_gdat_category(uint16_t item);

/* Items of the champion actions (champ_action.c). */
/* True when thing is ammunition for the launcher in hand of champ. */
bool item_is_ammo_for_hand(int champ, int hand, uint16_t thing);
/* True when thing has an action with command for champ. */
bool item_has_action_command(int champ, uint16_t thing, int command);

/* Item charges (inventory.c). */
/* Uses a charge of an item that holds water; returns 1 when it had one. */
int item_use_charge(uint16_t thing);

/* Items by item index (money.c). */
/* The thing type of an item index, -1 for none. */
int item_index_to_thing_type(uint16_t item);
/* A new item of item index item (bit 15: a miscellaneous item may take the kept records); THING_NONE when the
   dungeon has no free record. */
uint16_t item_create_by_index(uint16_t item);

/* Items of the party (sensor.c). */
/* True when a living champion, the open container, or the leader hand holds an item of the item index type. */
int item_party_possesses_type(int type);

#endif /* DM2_ENGINE_ITEM_H */
