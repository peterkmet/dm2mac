#ifndef MAGIC_H
#define MAGIC_H
#include <stdint.h>
#include "champion.h"
#include "timeline.h"

/* A spell of the spell table. */
typedef struct {
    uint32_t runes;             /* power rune in bits 24-31 (0: any power), then up to 3 runes, first rune highest */
    uint8_t difficulty;
    uint8_t skill;              /* the skill that casts it and that gains the experience */
    uint16_t effect;            /* kind in bits 0-3, type in bits 4-9, action delay factor in bits 10-15 */
} Spell;

/* Results of a cast: 0 success, else a failure kind ORed with the base skill (0 to 3). */
enum cast_result { CAST_FAILED = 0x10, CAST_UNKNOWN_SPELL = 0x20, CAST_NEEDS_FLASK = 0x30 };

/* Kinds of magic light (magic_add_light). */
enum { LIGHT_DARKNESS = 6, LIGHT_TORCH = 0x26, LIGHT_LIGHT = 0x27 };

/* The spell of the entered runes (a power rune and 1 to 3 runes), NULL when no spell has them. */
const Spell *magic_find_spell(const uint8_t *runes);
/* The hand (1, then 0) that holds an empty flask, -1 none. */
int magic_find_flask_in_hands(const Champion *c);
/* Champion champ casts spell with power 1 to 6; returns enum cast_result. */
int magic_cast_spell(int champ, const Spell *spell, int power);
/* The champion of the open spell panel casts the entered runes; returns 1 when the cast failed. */
int magic_cast_entered_runes(void);
/* Adds rune 0 to 5 of the next rune set to the runes of the open spell panel when the mana suffices. */
void magic_add_rune(int rune);
void magic_remove_last_rune(void);
/* Shows why a cast failed (enum cast_result) and counts it in the global variables. */
void magic_report_cast_failure(int result);
/* Adds magic light (or darkness) of a kind and power; it fades through timeline events. */
void magic_add_light(int kind, int power);
/* Gives the living champions of mask aura type with value for duration game ticks. */
void magic_add_party_aura(int mask, int type, int16_t value, uint16_t duration);
/* A party aura from champion c; pay: it costs 4 mana (less mana halves it). Returns 0 when it was not paid in full. */
int magic_cast_party_aura(Champion *c, int type, uint16_t value, int pay);
/* Champion champ shoots the missile thing with power for cost mana; returns 0 without the mana. */
int magic_cast_projectile_spell(int champ, uint16_t thing, int power, int cost);
/* The party on a warp marker text moves to the marker of its id; returns 1 when it moved. */
int move_party_warp_to_marker(void);
/* A magic light event: the light fades one level. */
void timeline_event_magic_light(TimelineEvent *e);

/* Projectiles (port/src/projectile.c): champion c launches thing from the party square. */

#endif
