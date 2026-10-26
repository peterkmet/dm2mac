/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_CHAMPION_INTERNAL_H
#define DM2_ENGINE_CHAMPION_INTERNAL_H
/* Prefixes: champ_ champion inv_ */

/* The private declarations of the champion module: the commands, charges, and repeats of the actions, and the name
   message of an object. */

#include <stdbool.h>
#include <stdint.h>
#include "champion.h"
#include "save.h"

/* An action that repeats by itself in the next game steps (a flurry of blows). */
typedef struct {
    uint16_t count; /* the repeats that the action gave: the chance to use a charge at each repeat */
    uint16_t left;  /* repeats to come */
    uint8_t fresh;  /* set in the step of the action: the first repeat waits a step */
    uint8_t action; /* the index in g_champ_actions */
    uint16_t thing; /* the item in the hand; another item stops the repeats */
} ActionRepeat;

/* The slot that a hand loads from (12 or 7-9 for the action hand, 11 or 6 for the ready hand), -1 none. */
int champ_find_hand_source_slot(int champ, int hand);
/* Champ throws the item in hand (-1: the leader hand object) to the left or right side; true when it flies. */
bool champ_throw_object(int champ, int hand, int side);
/* Prints the name of thing in the message area. */
void inv_show_object_name(uint16_t thing);

/* The repeats of the actions of each champion and hand; exported for the tests. */
extern ActionRepeat g_action_repeats[CHAMPION_MAX][SLOT_HAND_COUNT];

/* Action commands (FIELD_COMMAND of an action text). */
#define COMMAND_INVISIBILITY     0x02
#define COMMAND_SPELL_PROJECTILE 0x03
#define COMMAND_MELEE            0x04
#define COMMAND_FRIGHTEN         0x05
#define COMMAND_LIGHT            0x06
#define COMMAND_EXPLOSION        0x07
#define COMMAND_MELEE_2          0x08
#define COMMAND_HASTE            0x09
#define COMMAND_LEVITATE_DOWN    0x0a
#define COMMAND_ACTION_EFFECT    0x0b
#define COMMAND_AURA_FIRST       0x0c
#define COMMAND_AURA_2           0x0d
#define COMMAND_AURA_3           0x0e
#define COMMAND_AURA_LAST        0x0f
#define COMMAND_CONSUME          0x10
#define COMMAND_LOAD             0x11
#define COMMAND_SHOOT            0x20
#define COMMAND_SHIELD_FIRST     0x21
#define COMMAND_SHIELD_2         0x22
#define COMMAND_SHIELD_LAST      0x23
#define COMMAND_HEAL             0x24
#define COMMAND_LIGHT_2          0x26
#define COMMAND_LIGHT_3          0x27
#define COMMAND_THROW            0x2a
#define COMMAND_MINION_MAP_SET   0x2c
#define COMMAND_MINION_GUARD     0x2d
#define COMMAND_MINION_FETCH     0x2e
#define COMMAND_MINION_ATTACK    0x2f
#define COMMAND_MINION_DISMISS   0x30
#define COMMAND_SUMMON_FIRST     0x31
#define COMMAND_SUMMON_2         0x32
#define COMMAND_SUMMON_LAST      0x33
#define COMMAND_WARP             0x36
/* Charges of an action text (FIELD_CHARGES) above a charge count. */
#define CHARGES_USE_ALL          0x10 /* the action needs a charge and uses all charges */
#define CHARGES_USE_NONE         0x11 /* the action needs a charge and uses none */
#define CHARGES_EMPTY            0x12 /* the action needs an item without charges */
/* The champ_perform_action flag of a repeat: no delay, stamina, experience. */
#define ACTION_REPEATED          0x8000

#endif /* DM2_ENGINE_CHAMPION_INTERNAL_H */
