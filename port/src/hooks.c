#include "dungeon.h"
#include "hooks.h"
#include "input.h"

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
void timeline_event_magic_light(TimelineEvent *e) { (void)e; }
void champ_poison(int champ, int amount) { (void)champ; (void)amount; }
void creature_alert(uint16_t group, int x, int y, int flags, int amount, int chance) { (void)group; (void)x; (void)y; (void)flags; (void)amount; (void)chance; }
int creature_can_be_pushed(int x, int y, int dir, uint16_t *group) { (void)x; (void)y; (void)dir; (void)group; return 0; }
int move_party_can_push_group(uint16_t group) { (void)group; return 0; }
void creature_push_group(int x, int y, int dir, int strength) { (void)x; (void)y; (void)dir; (void)strength; }
void creature_turn_group(uint16_t group, int absolute, int dir) { (void)group; (void)absolute; (void)dir; }
void creature_activate_groups_on_map(void) { }
void creature_activate_groups_all_maps(void) { }
void creature_refresh_party_maps(void) { }
void creature_activate_group(uint16_t group, int x, int y) { (void)group; (void)x; (void)y; }
void creature_deactivate_group(int active_index) { (void)active_index; }
void creature_cancel_update(uint16_t group) { (void)group; }
void creature_drop_possessions(uint16_t group, int x, int y, int mode, int sound_map) { (void)group; (void)x; (void)y; (void)mode; (void)sound_map; }
void creature_delete_group_at(int x, int y, int mode, int sound_map) { (void)x; (void)y; (void)mode; (void)sound_map; }
void move_schedule_group_retry(uint16_t group, int x, int y, int map, int audible) { (void)group; (void)x; (void)y; (void)map; (void)audible; }
void move_update_carried_missiles(uint16_t holder, int x, int y, int map, int flag) { (void)holder; (void)x; (void)y; (void)map; (void)flag; }
void move_check_missiles_hit_party(int from_x, int from_y, int to_x, int to_y) { (void)from_x; (void)from_y; (void)to_x; (void)to_y; }
int creature_active_group_event(int active_index) { (void)active_index; return -1; }
int creature_active_group_floats(int active_index) { (void)active_index; return 0; }
uint16_t creature_create_group(int creature_type, int count, int dir, int x, int y) { (void)creature_type; (void)count; (void)dir; (void)x; (void)y; return THING_NONE; }
void creature_process_event(int x, int y, int type) { (void)x; (void)y; (void)type; }
void creature_update_anim_at(int x, int y, int start) { (void)x; (void)y; (void)start; }
void timeline_event_move_group(TimelineEvent *e) { (void)e; }
void projectile_process_event(TimelineEvent *e) { (void)e; }
void explosion_process_event(TimelineEvent *e) { (void)e; }
void sensor_launch_missile(uint16_t sensor, const TimelineEvent *e) { (void)sensor; (void)e; }
void sensor_affect_creatures_in_area(int kind_data, int count, int x, int y, int tx, int ty, int kind, int effect) { (void)kind_data; (void)count; (void)x; (void)y; (void)tx; (void)ty; (void)kind; (void)effect; }
void sensor_turn_creatures(const TimelineEvent *e) { (void)e; }
void world_update_day_night(int step) { (void)step; }
void light_update_party_light(void) { }
void sensor_transfer_objects(const TimelineEvent *e, uint16_t sensor, int kind) { (void)e; (void)sensor; (void)kind; }
void sound_play_with_fallback(int category, int index, int entry, int fallback, int x, int y, int delay, int priority, int volume) { (void)category; (void)index; (void)entry; (void)fallback; (void)x; (void)y; (void)delay; (void)priority; (void)volume; }
void sound_request(int category, int index, int entry, int priority, int volume, int x, int y, int delay) { (void)category; (void)index; (void)entry; (void)priority; (void)volume; (void)x; (void)y; (void)delay; }
void sound_flush_requests(int queue) { (void)queue; }
void sound_play_delayed(int index) { (void)index; }
void game_complete(void) { }
void command_execute_later(const Command *c) { (void)c; }
uint16_t champ_remove_object_from_leader_hand(void) { return THING_NONE; }
void champ_put_object_in_leader_hand(uint16_t thing, int flag) { (void)thing; (void)flag; }
uint16_t item_create_by_index(int item) { (void)item; return THING_NONE; }
void champ_start_recruit(int x, int y, int map) { (void)x; (void)y; (void)map; }
void view_click_take_object(int x, int y) { (void)x; (void)y; }
int view_click_place_object(int x, int y, int square_x, int square_y, int cell_from, int cell, int kind) { (void)x; (void)y; (void)square_x; (void)square_y; (void)cell_from; (void)cell; (void)kind; return 0; }
int view_click_drop_object(int square_x, int square_y, int cell) { (void)square_x; (void)square_y; (void)cell; return 0; }
void view_click_throw_object(int x, int y) { (void)x; (void)y; }
void move_push_creature(int arrow) { (void)arrow; }
void view_click_creature(uint16_t group, int x, int y, int square_x, int square_y) { (void)group; (void)x; (void)y; (void)square_x; (void)square_y; }
