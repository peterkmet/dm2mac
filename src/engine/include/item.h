/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_ITEM_H
#define DM2_ENGINE_ITEM_H
/* Prefixes: item_ */

/* The items: their record fields, their GDAT types, and what the party possesses. */

#include <stdbool.h>
#include <stdint.h>
#include "dungeon.h"

/* Record fields of the things of this module; dungeon.h holds the record layout. */
/* The type (GDAT index) of a weapon, armour, or miscellaneous item. */
static inline uint16_t item_type(uint16_t item)
{
    return thing_field(item, ITEM_DATA, 0, ITEM_TYPE_MASK);
}
static inline void item_set_type(uint16_t item, int type)
{
    thing_set_field(item, ITEM_DATA, 0, ITEM_TYPE_MASK, type);
}
/* The dungeon keeps the item: no record reuse, and creatures do not consume it. */
static inline bool item_is_kept(uint16_t item)
{
    return thing_bit(item, ITEM_DATA, thing_type(item) == THING_POTION ? POTION_DO_NOT_DISCARD : ITEM_DO_NOT_DISCARD);
}
static inline void item_set_kept(uint16_t item, bool kept)
{
    thing_set_bit(item, ITEM_DATA, thing_type(item) == THING_POTION ? POTION_DO_NOT_DISCARD : ITEM_DO_NOT_DISCARD,
                  kept);
}

/* No GDAT index and no item index. */
#define GDAT_NO_INDEX        0x00ff
#define ITEM_INDEX_NONE      0x01ff /* item index of no thing */
/* Item index ranges. */
#define ITEM_FIRST_CLOTHING  0x0080
#define ITEM_FIRST_MISC      0x0100
#define ITEM_FIRST_POTION    0x0180
#define ITEM_FIRST_CREATURE  0x01b0
#define ITEM_FIRST_CONTAINER 0x01e0
#define ITEM_SCROLL          0x01fc
/* Miscellaneous item 0 (CAT_MISC index): the bones of a champion. */
#define ITEM_BONES           0

/* Item attribute entries: GDAT value keys (category, index, 0x0b, entry). */
#define ATTR_FLAGS          0x00
#define ATTR_WEIGHT         0x01
#define ATTR_VALUE          0x02
#define ATTR_SLOTS          0x04
#define ATTR_AMMO           0x05
#define ATTR_ICON_ANIMATION 0x06
#define ATTR_STRENGTH       0x08
#define ATTR_THROW_POWER    0x09
#define ATTR_ARMOUR_CLASS   0x0b
#define ATTR_EXTRA_DAMAGE 0x0d /* extra damage of a hit: armour piercing against creatures, poison against champions */
#define ATTR_WEIGHT_PER_CHARGE 0x34
#define ATTR_VALUE_PER_CHARGE  0x35
#define ATTR_LIGHT_SOURCE      0xf8 /* item and creature attribute: the light that the thing gives */
/* ATTR_FLAGS bits. */
#define ITEM_FLAG_COIN         0x4000
#define ITEM_FLAG_ICON_IN_HAND 0x8000 /* the icon changes with the slot */
#define ITEM_FLAG_SCROLL_INFO  0x0020 /* the inventory shows the object info while it is in the hand */
#define ITEM_FLAG_REFILLS      0x0001 /* the item holds water */
#define ITEM_FLAG_LIGHT        0x0010 /* the item gives light */
/* ATTR_AMMO bits. */
#define AMMO_LAUNCHER          0x8000 /* a launcher; the low bits are the ammunition kinds */
/* ATTR_SLOTS bits. */
#define SLOT_MASK_NO_CONTAINER 0x8000 /* the item does not go into a container */
#define SLOT_MASK_POUCH        0x0040
#define SLOT_MASK_HAND         0x0400
/* The highest power of a potion. */
#define POTION_POWER_MAX       255

/* GDAT keys of things and item indices */
/* The GDAT category of a thing; a projectile gives the category of what it carries. */
uint8_t item_gdat_category(uint16_t thing);
/* The GDAT index of an item or creature; a projectile gives the index of what it carries. */
uint8_t item_gdat_index(uint16_t thing);
/* The attribute entry of an item, 0 for no thing. */
uint16_t item_attr(uint16_t thing, int entry);
/* The item index of a thing: the base of its type plus its GDAT index, ITEM_INDEX_NONE for no thing. */
uint16_t item_get_index(uint16_t thing);
/* The GDAT index of an item index. */
uint8_t item_index_gdat_index(uint16_t item);
/* The GDAT category of an item index. */
uint8_t item_index_gdat_category(uint16_t item);
/* The thing type of an item index, -1 for none. */
int item_index_to_thing_type(uint16_t item);

/* Item queries */
/* Charges of a weapon, clothing, or miscellaneous item; 0 for other things. */
int item_charges(uint16_t thing);
/* The highest charge count of the thing type. */
int item_max_charges(uint16_t thing);
/* True when thing may go into slot. strict: only the champion slots 0 to 12 (or -1, a hand) count. */
int item_fits_slot(uint16_t thing, int16_t slot, int strict);
/* The icon entry of an item in slot (animated: by its icon animation attribute). */
int item_icon_index(uint16_t thing, int16_t slot, int animated);
/* True when the thing is a money box: a chest with coin slots. */
bool item_is_moneybox(uint16_t thing);
/* True when the thing is a chest that is no money box. */
bool item_is_chest(uint16_t thing);
/* True when the thing is a minion map. */
bool item_is_minion_map(uint16_t thing);
/* The weight of a thing in 1/10 kg, with its contents. */
int item_weight(uint16_t thing);
/* The value of a thing with its contents (attribute 2), at most 0xffff. */
uint16_t item_get_value(uint16_t thing);
/* True when one of the actions of the item uses charges. */
bool item_has_charged_action(uint16_t thing);
/* The name of an item: GDAT text (category, index, 0x18) with its format codes. Uses one static buffer. */
const char *item_name(uint16_t thing);

/* Item commands */
/* Adds delta to the charges of a weapon, clothing, or miscellaneous item (clamped); returns the new charges. */
int item_add_charges(uint16_t thing, int delta);
/* Changes the GDAT index (the kind) of an item or creature group. */
void item_set_gdat_index(uint16_t thing, int index);
/* Sets or clears the bit that keeps a thing from being reused when the dungeon runs out of records. */
void item_set_do_not_discard(uint16_t thing, int keep);
/* A new item of item index item (bit 15: a miscellaneous item may take the kept records); THING_NONE when the
   dungeon has no free record. */
uint16_t item_create_by_index(uint16_t item);

/* Minion links. */
/* The projectile among the possessions of group that carries payload (THING_NONE: any); 0 when none. */
uint16_t item_find_link_projectile(uint16_t group, uint16_t payload);

/* Items of the champion actions. */
/* True when thing is ammunition for the launcher in hand of champ. */
bool item_is_ammo_for_hand(int champ, int hand, uint16_t thing);
/* True when thing has an action with command for champ. */
bool item_has_action_command(int champ, uint16_t thing, int command);

/* Item charges. */
/* Uses a charge of an item that holds water; true when it had one. */
bool item_use_charge(uint16_t thing);

#endif /* DM2_ENGINE_ITEM_H */
