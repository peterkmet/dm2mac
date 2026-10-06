#ifndef HOOKS_H
#define HOOKS_H
#include <stdint.h>
#include "sound_request.h"
#include "timeline.h"
#include "projectile.h"

/* Calls of the movement, sensor, and main loop code into systems of later steps. Each hook is empty or
   returns the "nothing happened" value until its step ports it; the step then moves it to its module. */

/* Step 15: champions. */
/* The arguments of the damage hook calls (champ -1: the whole party), for the tests until Step 15. */
typedef struct { int16_t champ, damage, wound_mask, attack_type; } DamageCall;
#define DAMAGE_CALL_MAX 64
extern DamageCall damage_calls[DAMAGE_CALL_MAX];
extern int damage_call_count;
int champ_take_damage(int champ, int damage, int wound_mask, int attack_type);   /* returns 0: no damage */
int champ_damage_party(int damage, int wound_mask, int attack_type);             /* returns the hit champions, 0 */
void champ_apply_pending_damage(void);
void champ_apply_time_effects(void);
void champ_process_repeated_actions(void);
void items_burn_held_lights(void);
void inv_update_hand_cursor(void);
void timeline_event_altar_rebirth(TimelineEvent *e);
void timeline_event_apply_item_effect(TimelineEvent *e);
void champ_poison(int champ, int amount);
int champ_get_dexterity(int champ);                                              /* returns 0 */
int champ_test_luck(int champ, int chance);                                      /* returns 0: no luck */
int champ_get_stat_adjusted_attack(int champ, int stat, int attack);             /* returns 0 */
/* A champion comes back from the bones that a sensor took (Step 15). */
void champ_revive(int champ);
uint16_t champ_remove_object_from_leader_hand(void);                            /* returns THING_NONE */
void champ_put_object_in_leader_hand(uint16_t thing, int flag);
uint16_t item_create_by_index(int item);
/* Rules of Step 15 that the spells use: they change nothing; the tests compare the arguments of their calls. */
enum hook_call_kind {
    HOOK_ADD_EXPERIENCE, HOOK_ADD_ACTION_DELAY, HOOK_ADD_OBJECT_TO_SLOT, HOOK_PUT_IN_LEADER_HAND
};
typedef struct { int32_t kind, args[5]; } HookCall;
#define HOOK_CALL_MAX 64
extern HookCall hook_calls[HOOK_CALL_MAX];
extern int hook_call_count;
void hook_record(int kind, int a, int b, int c, int d, int e);
void champ_add_experience(int champ, int skill, uint32_t amount);
void champ_add_action_delay(int champ, uint16_t ticks, int hand);
void champ_add_object_to_slot(int champ, uint16_t thing, int slot);
/* Rules of Step 15 that the creatures use: a luck test (returns 0: fails), the dexterity (0), the value of an item (0),
   and the removal of a hand object (clears the slot and returns the object). */
uint16_t item_get_value(uint16_t thing);
uint16_t champ_remove_object_from_slot(int champ, int slot);                                        /* returns THING_NONE */
void champ_start_recruit(int x, int y, int map);
/* A wall sensor (kind 0x3c) creates the item of its data and sends it to its target. */
void sensor_create_item(const TimelineEvent *e, uint16_t sensor);
/* The clicks in the view with an object in the hand or on objects (Step 15): returns 1 when done. */
void view_click_take_object(int x, int y);
int view_click_place_object(int x, int y, int square_x, int square_y, int cell_from, int cell, int kind);
int view_click_drop_object(int square_x, int square_y, int cell);
void view_click_throw_object(int x, int y);

/* Merchant commands of the creature AI (shops, Step 15): return AI_RESULT_FAIL (0xfd). */
int shop_cmd_wait_for_offer(void);
int shop_cmd_evaluate_offer(void);
int shop_cmd_count_payment(void);
int shop_cmd_pay_change(void);

/* Step 20: end of game. */
void game_complete(void);
/* The level digit table (dungeon_read_level_table): the map icon set of the squares outside the maps of a level;
   returns 0 until Step 20 reads the table. */
int dungeon_level_digit(int level);

#endif
