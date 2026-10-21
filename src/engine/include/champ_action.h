/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_CHAMP_ACTION_H
#define DM2_ENGINE_CHAMP_ACTION_H
/* Prefixes: ActionRepeat */

/* The actions of the champions: the commands, the charges, and the repeats of an action. */

#include "save.h"
#include <stdbool.h>
#include <stdint.h>

/* An action that repeats by itself in the next game steps (a flurry of blows). */
typedef struct {
    uint16_t count; /* the repeats that the action gave: the chance to use a charge at each repeat */
    uint16_t left;  /* repeats to come */
    uint8_t fresh;  /* set in the step of the action: the first repeat waits a step */
    uint8_t action; /* the index in champ_actions */
    uint16_t thing; /* the item in the hand; another item stops the repeats */
} ActionRepeat;

extern ActionRepeat champ_action_repeat[CHAMPION_MAX][2];

/* Action commands (FIELD_COMMAND of an action text). */
enum {
    COMMAND_INVISIBILITY = 2,
    COMMAND_SPELL_PROJECTILE,
    COMMAND_MELEE,
    COMMAND_FRIGHTEN,
    COMMAND_LIGHT,
    COMMAND_EXPLOSION,
    COMMAND_MELEE_2,
    COMMAND_HASTE,
    COMMAND_LEVITATE_DOWN,
    COMMAND_ACTION_EFFECT,
    COMMAND_AURA_FIRST = 0x0c,
    COMMAND_AURA_LAST = 0x0f,
    COMMAND_CONSUME = 0x10,
    COMMAND_LOAD = 0x11,
    COMMAND_SHOOT = 0x20,
    COMMAND_SHIELD_FIRST = 0x21,
    COMMAND_SHIELD_2,
    COMMAND_SHIELD_LAST,
    COMMAND_HEAL,
    COMMAND_LIGHT_2 = 0x26,
    COMMAND_LIGHT_3,
    COMMAND_THROW = 0x2a,
    COMMAND_MINION_MAP_SET = 0x2c,
    COMMAND_MINION_GUARD,
    COMMAND_MINION_FETCH,
    COMMAND_MINION_ATTACK,
    COMMAND_MINION_DISMISS,
    COMMAND_SUMMON_FIRST,
    COMMAND_SUMMON_2,
    COMMAND_SUMMON_LAST,
    COMMAND_WARP = 0x36
};
/* Charges of an action text (FIELD_CHARGES) above a charge count. */
enum {
    CHARGES_USE_ALL = 0x10,  /* the action needs a charge and uses all charges */
    CHARGES_USE_NONE = 0x11, /* the action needs a charge and uses none */
    CHARGES_EMPTY = 0x12     /* the action needs an item without charges */
};
#define ACTION_REPEATED 0x8000 /* champ_perform_action flag: a repeat; no delay, stamina, experience */

#endif /* DM2_ENGINE_CHAMP_ACTION_H */
