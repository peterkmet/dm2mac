/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_CHAMPION_INTERNAL_H
#define DM2_ENGINE_CHAMPION_INTERNAL_H
/* Prefixes: champ_ champion inv_ */

/* The private declarations of the champion module: the commands, charges, and repeats of the actions, the name
   message of an object, and the 16-bit limits of the rules. */

#include <stdbool.h>
#include <stdint.h>
#include "champion.h"
#include "save.h"

/* An action that repeats by itself in the next game steps (a flurry of blows). */
typedef struct {
    uint16_t count; /* the repeats that the action gave: the chance to use a charge at each repeat */
    uint16_t left;  /* repeats to come */
    bool fresh;     /* set in the step of the action: the first repeat waits a step */
    uint8_t action; /* the index in g_champ_actions */
    uint16_t thing; /* the item in the hand; another item stops the repeats */
} ActionRepeat;

/* The slot that a hand loads from (12 or 7-9 for the action hand, 11 or 6 for the ready hand), -1 none. */
int champ_find_hand_source_slot(int16_t champion_index, int16_t hand);
/* Prints the name of thing in the message area. */
void inv_show_object_name(uint16_t thing);

/* The repeats of the actions of each champion and hand; exported for the tests. */
extern ActionRepeat g_action_repeats[CHAMPION_MAX][SLOT_HAND_COUNT];

/* Action commands (FIELD_COMMAND of an action text). */
#define COMMAND_LOAD           0x11
#define COMMAND_MINION_MAP_SET 0x2c
#define COMMAND_MINION_GUARD   0x2d
#define COMMAND_MINION_FETCH   0x2e
#define COMMAND_MINION_ATTACK  0x2f
#define COMMAND_MINION_DISMISS 0x30
/* Charges of an action text (FIELD_CHARGES) above a charge count. */
#define CHARGES_USE_ALL        0x10 /* the action needs a charge and uses all charges */
#define CHARGES_USE_NONE       0x11 /* the action needs a charge and uses none */
#define CHARGES_EMPTY          0x12 /* the action needs an item without charges */
/* Each doubling of the experience above this is one more level of a skill or of the party. */
#define SKILL_LEVEL_EXPERIENCE 512

#endif /* DM2_ENGINE_CHAMPION_INTERNAL_H */
