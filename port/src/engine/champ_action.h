#ifndef CHAMP_ACTION_H
#define CHAMP_ACTION_H
#include <stdbool.h>
#include <stdint.h>
#include "save.h"

/* An action that repeats by itself in the next game steps (a flurry of blows). */
typedef struct {
    uint16_t count;     /* the repeats that the action gave: the chance to use a charge at each repeat */
    uint16_t left;      /* repeats to come */
    uint8_t fresh;      /* set in the step of the action: the first repeat waits a step */
    uint8_t action;     /* the index in champ_actions */
    uint16_t thing;     /* the item in the hand; another item stops the repeats */
} ActionRepeat;

extern ActionRepeat champ_action_repeat[CHAMPION_MAX][2];

/* Action commands (FIELD_COMMAND of an action text). */
enum {
    COMMAND_INVISIBILITY = 2, COMMAND_SPELL_PROJECTILE, COMMAND_MELEE, COMMAND_FRIGHTEN, COMMAND_LIGHT,
    COMMAND_EXPLOSION, COMMAND_MELEE_2, COMMAND_HASTE, COMMAND_LEVITATE_DOWN, COMMAND_ACTION_EFFECT,
    COMMAND_AURA_FIRST = 0x0c, COMMAND_AURA_LAST = 0x0f, COMMAND_CONSUME = 0x10, COMMAND_LOAD = 0x11,
    COMMAND_SHOOT = 0x20, COMMAND_SHIELD_FIRST = 0x21, COMMAND_SHIELD_2, COMMAND_SHIELD_LAST, COMMAND_HEAL,
    COMMAND_LIGHT_2 = 0x26, COMMAND_LIGHT_3, COMMAND_THROW = 0x2a, COMMAND_MINION_MAP_SET = 0x2c,
    COMMAND_MINION_GUARD, COMMAND_MINION_FETCH, COMMAND_MINION_ATTACK, COMMAND_MINION_DISMISS,
    COMMAND_SUMMON_FIRST, COMMAND_SUMMON_2, COMMAND_SUMMON_LAST, COMMAND_WARP = 0x36
};
/* Charges of an action text (FIELD_CHARGES) above a charge count. */
enum {
    CHARGES_USE_ALL = 0x10,     /* the action needs a charge and uses all charges */
    CHARGES_USE_NONE = 0x11,    /* the action needs a charge and uses none */
    CHARGES_EMPTY = 0x12        /* the action needs an item without charges */
};
#define ACTION_REPEATED 0x8000  /* champ_perform_action flag: a repeat; no delay, stamina, experience */

/* Opens the action menu of a hand of champ (0 to 3) in the action area. */
void ui_action_area_open_hand(int champ, int hand);
/* Opens the spell panel of champ in the action area. */
void ui_action_area_open_spells(int champ);
/* Runs action (0 to 2, -1: none) of the open action menu; returns 1 when the action was done. */
int champ_execute_selected_action(int action);
/* Champ performs action (an index in champ_actions, bit 15: a repeat) with the item in the open hand. */
int champ_perform_action(int champ, int action);
/* Makes the hand (-1: both hands and the spell) of champ busy for about ticks game steps. */
void champ_add_action_delay(int champ, int ticks, int hand);
/* Takes the charges of the current action from its item; returns 1 when the item has no charges left. */
int champ_use_action_charge(int champ);
/* The hand of champ is free again: a shooter or a thrower takes the next ammunition or throwing item. */
void champ_reload_hand(int champ, int hand);
/* The repeats of the actions of all champions. */
void champ_process_repeated_actions(void);
/* True when thing is ammunition for the launcher in hand of champ. */
bool item_is_ammo_for_hand(int champ, int hand, uint16_t thing);
/* True when thing has an action with command for champ. */
bool item_has_action_command(int champ, uint16_t thing, int command);
/* Adds experience in skill; each new level raises the statistics and prints a message. */
void champ_add_experience(int champ, int skill, uint32_t amount);
/* The level of the whole party: from the sum of the base skill experience of all champions. */
int champ_get_party_level(void);
/* Champ throws the item in hand (-1: the leader hand object) to the left or right side; returns 1 when it flies. */
int champ_throw_object(int champ, int hand, int side);
/* The leader throws the object in the leader hand. */
int champ_throw_leader_hand_object(int side);

#endif
