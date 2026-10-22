/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_MAGIC_H
#define DM2_ENGINE_MAGIC_H
/* Prefixes: magic_ */

/* Magic: the spell table, runes, spell casting, and the magic light. */

#include "champion.h"
#include <stdbool.h>
#include <stdint.h>

/* A spell of the spell table. */
typedef struct {
    uint32_t runes; /* power rune in bits 24-31 (0: any power), then up to 3 runes, first rune highest */
    uint8_t difficulty;
    uint8_t skill;   /* the skill that casts it and that gains the experience */
    uint16_t effect; /* kind in bits 0-3, type in bits 4-9, action delay factor in bits 10-15 */
} Spell;

/* Results of a cast: 0 success, else a failure kind ORed with the base skill (0 to 3). */
#define CAST_FAILED        0x10
#define CAST_UNKNOWN_SPELL 0x20
#define CAST_NEEDS_FLASK   0x30

/* Kinds of magic light (magic_add_light). */
#define LIGHT_DARKNESS 0x06
#define LIGHT_TORCH    0x26
#define LIGHT_LIGHT    0x27

/* Spell lookup */
/* The spell of the entered runes (a power rune and 1 to 3 runes), NULL when no spell has them. */
const Spell *magic_find_spell(const uint8_t *runes); /* exported for the tests */

/* Light and auras */
/* Adds magic light (or darkness) of a kind and power; it fades through timeline events. */
void magic_add_light(int kind, int power);
/* Gives the living champions of mask aura type with value for duration game ticks. */
void magic_add_party_aura(int mask, int type, int16_t value, uint16_t duration);
/* A party aura from champion c; pay: it costs 4 mana (less mana halves it). Returns false when it was not paid in full. */
bool magic_cast_party_aura(Champion *c, int type, uint16_t value, bool pay);

/* Casting */
/* Champion champ shoots the missile thing with power for cost mana; returns false without the mana. */
bool magic_cast_projectile_spell(int champ, uint16_t thing, int power, int cost);
/* Champion champ casts spell with power 1 to 6; returns the cast result (CAST_*), 0 for success. */
int magic_cast_spell(int champ, const Spell *spell, int power); /* exported for the tests */
/* The champion of the open spell panel casts the entered runes; returns true when the cast failed. */
bool magic_cast_entered_runes(void);

/* Runes */
/* The mana that rune (0 to 5) of the next rune set costs champion champ now. */
uint16_t magic_rune_mana_cost(int champ, int rune);
/* Adds rune 0 to 5 of the next rune set to the runes of the open spell panel when the mana suffices. */
void magic_add_rune(int rune);
/* The champion who casts takes back the last rune. */
void magic_remove_last_rune(void);

#endif /* DM2_ENGINE_MAGIC_H */
