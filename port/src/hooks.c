#include "dungeon.h"
#include "hooks.h"
#include "input.h"
#include "save.h"

#define CHAMPION_SLOTS_OFFSET 0xc3   /* the hand slots in the champion record */

/* Empty until their steps; see hooks.h. */
DamageCall damage_calls[DAMAGE_CALL_MAX];
int damage_call_count;

static void record_damage(int champ, int damage, int wound_mask, int attack_type)
{
    if (damage_call_count < DAMAGE_CALL_MAX)
        damage_calls[damage_call_count++] = (DamageCall){(int16_t)champ, (int16_t)damage, (int16_t)wound_mask, (int16_t)attack_type};
}

int champ_take_damage(int champ, int damage, int wound_mask, int attack_type)
{
    record_damage(champ, damage, wound_mask, attack_type);
    return 0;
}

HookCall hook_calls[HOOK_CALL_MAX];
int hook_call_count;

void hook_record(int kind, int a, int b, int c, int d, int e)
{
    if (hook_call_count < HOOK_CALL_MAX) hook_calls[hook_call_count++] = (HookCall){kind, {a, b, c, d, e}};
}

void champ_add_experience(int champ, int skill, uint32_t amount) { hook_record(HOOK_ADD_EXPERIENCE, champ, skill, (int)amount, 0, 0); }
void champ_add_action_delay(int champ, uint16_t ticks, int hand) { hook_record(HOOK_ADD_ACTION_DELAY, champ, ticks, hand, 0, 0); }
void champ_add_object_to_slot(int champ, uint16_t thing, int slot) { hook_record(HOOK_ADD_OBJECT_TO_SLOT, champ, thing, slot, 0, 0); }

int champ_damage_party(int damage, int wound_mask, int attack_type)
{
    record_damage(-1, damage, wound_mask, attack_type);
    return 0;
}
void champ_apply_pending_damage(void) { }
void champ_apply_time_effects(void) { }
void champ_process_repeated_actions(void) { }
void items_burn_held_lights(void) { }
void inv_update_hand_cursor(void) { }
void timeline_event_altar_rebirth(TimelineEvent *e) { (void)e; }
void timeline_event_apply_item_effect(TimelineEvent *e) { (void)e; }
void champ_poison(int champ, int amount) { (void)champ; (void)amount; }
int champ_get_stat_adjusted_attack(int champ, int stat, int attack) { (void)champ; (void)stat; (void)attack; return 0; }
void world_update_day_night(int step) { (void)step; }
void light_update_party_light(void) { }
void sensor_transfer_objects(const TimelineEvent *e, uint16_t sensor, int kind) { (void)e; (void)sensor; (void)kind; }
void sound_play_with_fallback(int category, int index, int entry, int fallback, int x, int y, int delay, int priority, int volume) { (void)category; (void)index; (void)entry; (void)fallback; (void)x; (void)y; (void)delay; (void)priority; (void)volume; }
void sound_request(int category, int index, int entry, int priority, int volume, int x, int y, int delay) { (void)category; (void)index; (void)entry; (void)priority; (void)volume; (void)x; (void)y; (void)delay; }
void sound_flush_requests(int queue) { (void)queue; }
void sound_play_delayed(int index) { (void)index; }
void game_complete(void) { }
int dungeon_level_digit(int level) { (void)level; return 0; }
void command_execute_later(const Command *c) { (void)c; }
uint16_t champ_remove_object_from_leader_hand(void) { return THING_NONE; }
void champ_put_object_in_leader_hand(uint16_t thing, int flag) { hook_record(HOOK_PUT_IN_LEADER_HAND, thing, flag, 0, 0, 0); }
uint16_t item_create_by_index(int item) { (void)item; return THING_NONE; }
void champ_start_recruit(int x, int y, int map) { (void)x; (void)y; (void)map; }
void view_click_take_object(int x, int y) { (void)x; (void)y; }
int view_click_place_object(int x, int y, int square_x, int square_y, int cell_from, int cell, int kind) { (void)x; (void)y; (void)square_x; (void)square_y; (void)cell_from; (void)cell; (void)kind; return 0; }
int view_click_drop_object(int square_x, int square_y, int cell) { (void)square_x; (void)square_y; (void)cell; return 0; }
void view_click_throw_object(int x, int y) { (void)x; (void)y; }
int shop_cmd_wait_for_offer(void) { return 0xfd; }
int shop_cmd_evaluate_offer(void) { return 0xfd; }
int shop_cmd_count_payment(void) { return 0xfd; }
int shop_cmd_pay_change(void) { return 0xfd; }
int champ_test_luck(int champ, int chance) { (void)champ; (void)chance; return 0; }
int champ_get_dexterity(int champ) { (void)champ; return 0; }
uint16_t item_get_value(uint16_t thing) { (void)thing; return 0; }
uint16_t champ_remove_object_from_slot(int champ, int slot)
{
    uint16_t *slots = (uint16_t *)(game.champions[champ] + CHAMPION_SLOTS_OFFSET);
    uint16_t t = slots[slot];
    slots[slot] = THING_NONE;
    return t;
}
