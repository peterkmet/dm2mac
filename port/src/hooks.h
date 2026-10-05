#ifndef HOOKS_H
#define HOOKS_H
#include <stdint.h>
#include "timeline.h"

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
void timeline_event_magic_light(TimelineEvent *e);
void champ_poison(int champ, int amount);
uint16_t champ_remove_object_from_leader_hand(void);                            /* returns THING_NONE */
void champ_put_object_in_leader_hand(uint16_t thing, int flag);
uint16_t item_create_by_index(int item);                                        /* returns THING_NONE */
void champ_start_recruit(int x, int y, int map);
/* The clicks in the view with an object in the hand or on objects (Step 15): returns 1 when done. */
void view_click_take_object(int x, int y);
int view_click_place_object(int x, int y, int square_x, int square_y, int cell_from, int cell, int kind);
int view_click_drop_object(int square_x, int square_y, int cell);
void view_click_throw_object(int x, int y);

/* Step 16: creatures and projectiles. */
void creature_alert(uint16_t group, int x, int y, int flags, int amount, int chance);
int creature_can_be_pushed(int x, int y, int dir, uint16_t *group);              /* returns 0: no push */
int move_party_can_push_group(uint16_t group);                                   /* returns 0 */
void creature_push_group(int x, int y, int dir, int strength);
void creature_turn_group(uint16_t group, int absolute, int dir);
void creature_refresh_party_maps(void);
void creature_drop_possessions(uint16_t group, int x, int y, int mode, int sound_map);
void creature_delete_group_at(int x, int y, int mode, int sound_map);
void move_schedule_group_retry(uint16_t group, int x, int y, int map, int audible);
void move_update_carried_missiles(uint16_t holder, int x, int y, int map, int flag);
void move_check_missiles_hit_party(int from_x, int from_y, int to_x, int to_y);
uint16_t creature_create_group(int creature_type, int count, int dir, int x, int y);  /* returns THING_NONE */
void timeline_event_move_group(TimelineEvent *e);
void projectile_process_event(TimelineEvent *e);
void explosion_process_event(TimelineEvent *e);
void explosion_create(int type, int power, int x, int y, int cell);
void sensor_launch_missile(uint16_t sensor, const TimelineEvent *e);
void sensor_affect_creatures_in_area(int kind_data, int count, int x, int y, int tx, int ty, int kind, int effect);
void sensor_turn_creatures(const TimelineEvent *e);
/* The push arrows and the clicks on creatures in front of the party. */
void move_push_creature(int arrow);
void view_click_creature(uint16_t group, int x, int y, int square_x, int square_y);                              /* returns 0 */                               /* returns -1: no event */

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
