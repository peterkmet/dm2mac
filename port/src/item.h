#ifndef ITEM_H
#define ITEM_H
#include <stdint.h>

/* GDAT categories of the things. */
enum gdat_category {
    GDAT_CHAMPION_CATEGORY = 0x16, GDAT_CONTAINER_CATEGORY = 0x14, GDAT_MISC_CATEGORY = 0x15,
    GDAT_SCROLL_CATEGORY = 0x12, GDAT_NO_CATEGORY = 0xff
};
#define GDAT_NO_INDEX 0xff
#define ITEM_INDEX_NONE 0x1ff   /* item index of no thing */
/* Item index ranges. */
#define ITEM_FIRST_CLOTHING 0x80
#define ITEM_FIRST_MISC 0x100
#define ITEM_FIRST_POTION 0x180
#define ITEM_FIRST_CREATURE 0x1b0
#define ITEM_FIRST_CONTAINER 0x1e0
#define ITEM_SCROLL 0x1fc

/* Item attribute entries: GDAT value keys (category, index, 0x0b, entry). */
enum item_attr {
    ATTR_FLAGS = 0, ATTR_WEIGHT = 1, ATTR_COIN_VALUE = 2, ATTR_SLOTS = 4, ATTR_ICON_ANIMATION = 6, ATTR_ARMOUR_CLASS = 0x0b,
    ATTR_STRENGTH = 8, ATTR_THROW_POWER = 9, ATTR_WEIGHT_PER_CHARGE = 0x34,
    ATTR_VALUE_PER_CHARGE = 0x35
};
/* ATTR_FLAGS bits. */
#define ITEM_FLAG_COIN 0x4000
#define ITEM_FLAG_ICON_IN_HAND 0x8000   /* the icon changes with the slot */
#define ITEM_FLAG_SCROLL_INFO 0x20       /* the inventory shows the object info while it is in the hand */
/* ATTR_SLOTS bits. */
#define SLOT_MASK_NO_CONTAINER 0x8000   /* the item does not go into a container */
#define SLOT_MASK_POUCH 0x40
#define SLOT_MASK_HAND 0x400

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
int item_is_moneybox(uint16_t thing);
int item_is_chest(uint16_t thing);
int item_is_minion_map(uint16_t thing);
/* The weight of a thing in 1/10 kg, with its contents. */
int item_weight(uint16_t thing);
/* True when one of the actions of the item uses charges. */
int item_has_charged_action(uint16_t thing);
/* The name of an item: GDAT text (category, index, 0x18) with its format codes. Uses one static buffer. */
const char *item_name(uint16_t thing);
/* The projectile among the possessions of group that carries filter (THING_NONE: any); 0 when none. */
uint16_t minion_find_link_projectile(uint16_t group, uint16_t filter);

/* Money: the coin types are the miscellaneous items with ITEM_FLAG_COIN, by value. */
#define COIN_TYPE_MAX 10
/* Builds the coin table once. */
void money_init_coin_table(void);
int money_coin_type_count(void);
uint16_t money_coin_item(int coin_type);   /* the item index of a coin type */
int16_t money_coin_value(int coin_type);
int money_is_coin(uint16_t thing);
/* The coins of each coin type in a money box. */
void money_count_coins_in_box(uint16_t box, int16_t counts[COIN_TYPE_MAX]);
/* The coin type that the money box shows in slot (0 to 9), -1 none. */
int money_coin_type_for_slot(uint16_t box, int slot);
/* GDAT index and category of an item index. */
uint8_t item_index_gdat_index(uint16_t item);
uint8_t item_index_gdat_category(uint16_t item);

#endif
