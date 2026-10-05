#ifndef HOOKS_H
#define HOOKS_H
#include <stdint.h>
#include "timeline.h"

/* Calls of the movement, sensor, and main loop code into systems of later steps. Each hook is empty or
   returns the "nothing happened" value until its step ports it; the step then moves it to its module. */

/* Step 15: champions. */
int champ_take_damage(int champ, int damage, int wound_mask, int attack_type);   /* returns 0: no damage */
int champ_damage_party(int damage, int wound_mask, int attack_type);             /* returns the hit champions, 0 */
void champ_apply_pending_damage(void);
void champ_apply_time_effects(void);
void champ_process_repeated_actions(void);
void items_burn_held_lights(void);
void inv_update_hand_cursor(void);
void timeline_event_altar_rebirth(TimelineEvent *e);
void timeline_event_apply_item_effect(TimelineEvent *e);
void timeline_event_magic_light(TimelineEvent *e);
void champ_poison(int champ, int amount);

/* Step 16: creatures and projectiles. */
void creature_alert(uint16_t group, int x, int y, int flags, int amount, int chance);
int creature_can_be_pushed(int x, int y, int dir, uint16_t *group);              /* returns 0: no push */
int move_party_can_push_group(uint16_t group);                                   /* returns 0 */
void creature_push_group(int x, int y, int dir, int strength);
void creature_turn_group(uint16_t group, int absolute, int dir);
void creature_activate_groups_on_map(void);
void creature_activate_groups_all_maps(void);
void creature_refresh_party_maps(void);
void creature_activate_group(uint16_t group, int x, int y);
void creature_deactivate_group(int active_index);
void creature_cancel_update(uint16_t group);
void creature_drop_possessions(uint16_t group, int x, int y, int mode, int sound_map);
void creature_delete_group_at(int x, int y, int mode, int sound_map);
void move_schedule_group_retry(uint16_t group, int x, int y, int map, int audible);
void move_update_carried_missiles(uint16_t holder, int x, int y, int map, int flag);
void move_check_missiles_hit_party(int from_x, int from_y, int to_x, int to_y);
int creature_active_group_event(int active_index);
int creature_active_group_floats(int active_index);
uint16_t creature_create_group(int creature_type, int count, int dir, int x, int y);  /* returns THING_NONE */
void creature_process_event(int x, int y, int type);
void creature_update_anim_at(int x, int y, int start);
void timeline_event_move_group(TimelineEvent *e);
void projectile_process_event(TimelineEvent *e);
void explosion_process_event(TimelineEvent *e);
void sensor_launch_missile(uint16_t sensor, const TimelineEvent *e);
void sensor_affect_creatures_in_area(int kind_data, int count, int x, int y, int tx, int ty, int kind, int effect);
void sensor_turn_creatures(const TimelineEvent *e);                              /* returns 0 */                               /* returns -1: no event */

/* Step 17: weather, day and night. */
void world_update_day_night(int step);
void light_update_party_light(void);
/* Skullkeep machines: sensors that move or create objects (kinds 0x3b, 0x3c, 0x40, 0x47, 0x48, 0x49). */
void sensor_transfer_objects(const TimelineEvent *e, uint16_t sensor, int kind);

/* Step 18: sound effects. */
void sound_play_with_fallback(int category, int index, int entry, int fallback, int x, int y, int delay, int priority,
                              int volume);
void sound_request(int category, int index, int entry, int priority, int volume, int x, int y, int delay);
void sound_flush_requests(int queue);
void sound_play_delayed(int index);

/* Step 20: end of game. */
void game_complete(void);

#endif
