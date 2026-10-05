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
/* A champion comes back from the bones that a sensor took (Step 15). */
void champ_revive(int champ);
/* Sets the bits of the item indexes of an item class (GDAT text of a creature type); the hook sets none. */
#define ITEM_CLASS_SET_SIZE 0x40
int shop_parse_item_class_set(int creature_type, int entry, uint8_t *bits, int skip_containers);
uint16_t champ_remove_object_from_leader_hand(void);                            /* returns THING_NONE */
void champ_put_object_in_leader_hand(uint16_t thing, int flag);
uint16_t item_create_by_index(int item);                                        /* returns THING_NONE */
void champ_start_recruit(int x, int y, int map);
/* A wall sensor (kind 0x3c) creates the item of its data and sends it to its target. */
void sensor_create_item(const TimelineEvent *e, uint16_t sensor);
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
/* The arguments of the explosion hook calls (lightning), for the tests until Step 16. */
typedef struct { uint16_t type, attack, x, y; int16_t cell; } ExplosionCall;
#define EXPLOSION_CALL_MAX 64
extern ExplosionCall explosion_calls[EXPLOSION_CALL_MAX];
extern int explosion_call_count;
void explosion_create(int type, int attack, int x, int y, int cell);
void explosion_process_event(TimelineEvent *e);
void sensor_launch_missile(uint16_t sensor, const TimelineEvent *e);
void sensor_affect_creatures_in_area(int kind_data, int count, int x, int y, int tx, int ty, int kind, int effect);
void sensor_turn_creatures(const TimelineEvent *e);
/* The push arrows and the clicks on creatures in front of the party. */
void move_push_creature(int arrow);
void view_click_creature(uint16_t group, int x, int y, int square_x, int square_y);                              /* returns 0 */                               /* returns -1: no event */

/* Step 18: sound effects. */
void sound_play_with_fallback(int category, int index, int entry, int fallback, int x, int y, int delay, int priority,
                              int volume);
void sound_request(int category, int index, int entry, int priority, int volume, int x, int y, int delay);
void sound_flush_requests(int queue);
void sound_play_delayed(int index);

/* Step 20: end of game. */
void game_complete(void);

#endif
