#ifndef HOOKS_H
#define HOOKS_H
#include <stdint.h>
#include "timeline.h"

/* Calls of the movement, sensor, and main loop code into systems of later steps. Each hook is empty or
   returns the "nothing happened" value until its step ports it; the step then moves it to its module. */

/* Step 15: champions. */
void timeline_event_magic_light(TimelineEvent *e);

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
/* The champion actions on creatures, magic, missiles, and minions (Step 16). They return 0: nothing happened. */
int champ_action_frighten(int power, int x, int y);
int champ_melee_attack_creature(int champ, uint16_t group, int x, int y, int attack, int wounds, int skill, int power);
int magic_cast_projectile_spell(int champ, int spell, int power, int mana_cost);
void magic_add_light(int command, int power);
int magic_cast_party_aura(int champ, int aura, int power, int use_mana);
int move_party_warp_to_marker(void);
void champ_launch_projectile(int champ, uint16_t thing, int kinetic, int attack, int step);
void projectile_create(uint16_t thing, int x, int y, int cell, int dir, int kinetic, int attack, int step);
int minion_map_set_location(uint16_t map_item, int x, int y, int map);
uint16_t creature_summon_group(int type, int level, int dir, int x, int y, int map, uint16_t link, int search_dir);
void creature_free_cached_ai_data(uint16_t group);
void minion_dismiss(uint16_t group);
/* Adds amount to the aura of the champions in mask for duration game steps (magic). */
void magic_add_party_aura(int mask, int aura, int amount, int duration);
/* Creates an explosion of type on cell (0xff: the center) of the square (x, y) of the current map. */
void explosion_create(int type, int attack, int x, int y, int cell);
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
