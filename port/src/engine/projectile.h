#ifndef PROJECTILE_H
#define PROJECTILE_H
#include <stdint.h>
#include "timeline.h"

/* Projectiles (thrown objects, spell missiles, missiles of sensors) and explosions. */

/* The payload of a spell missile and the type of an explosion: SPELL_THING_FIRST + the explosion type. */
#define SPELL_THING_FIRST 0xff80
enum spell_thing {
    SPELL_FIREBALL = 0xff80, SPELL_SLIME = 0xff81, SPELL_LIGHTNING = 0xff82, SPELL_HARM_NON_MATERIAL = 0xff83,
    SPELL_DOOR_TOGGLE = 0xff84, SPELL_POISON_BOLT = 0xff86, SPELL_POISON_CLOUD = 0xff87, SPELL_PUSH = 0xff89,
    SPELL_PULL = 0xff8a, SPELL_DOOR_SET = 0xff8d, SPELL_MISSILE_WALL = 0xff8e, EXPLOSION_DEATH_CLOUD = 0xffa8,
    EXPLOSION_STORM_LIGHTNING = 0xffb0, EXPLOSION_STORM_BLAST = 0xffb1, EXPLOSION_SMOKE = 0xffe4
};
#define EXPLOSION_AT_CENTER 0xff    /* the cell of an explosion in the middle of its square */

/* Bytes of a projectile move event: the projectile, then its position word (event_square_dir with the energy step
   in bits 12-15). Bytes of an explosion event: its square, then the explosion. */
enum { PROJECTILE_EVENT_THING = 0, PROJECTILE_EVENT_POSITION = 2 };
enum { EXPLOSION_EVENT_X = 0, EXPLOSION_EVENT_Y = 1, EXPLOSION_EVENT_THING = 2 };


/* The extra damage (poison) and the attack type of the last projectile_get_attack. */
extern uint16_t projectile_extra_damage, projectile_attack_type;
/* A sensor launches the next projectiles: they check for hits on their first square. */
extern int missile_from_sensor;

/* Launches payload from (x, y) of the current map in cell, flying toward dir: energy is its range and power,
   attack its hit chance, step the energy that each square takes. */
void projectile_create(uint16_t payload, int x, int y, int cell, int dir, int energy, int attack, int step);
/* An explosion of type with power on (x, y) of the current map in cell (EXPLOSION_AT_CENTER: the middle); an x
   above 0xff packs the square of a hit (low bytes) and the square of the explosion (high bytes, x + 1). */
void explosion_create(int type, int power, int x, int y, int cell);
/* Sets the square and the flight direction of a projectile move event; the energy step stays. */
void projectile_event_set_position(TimelineEvent *e, int x, int y, int dir);
/* The flight direction in the position word of a projectile move event. */
int projectile_event_dir(const TimelineEvent *e);
/* The flight direction of projectile: the direction of its queued move event. */
int projectile_dir(uint16_t projectile);
/* Removes the queued move of a projectile. */
void projectile_cancel_event(uint16_t projectile);
/* The projectile ends on (x, y): its object falls there (or into holder, a list of creature possessions). */
void projectile_destroy(uint16_t projectile, uint16_t *holder, int x, int y);
/* The power of a projectile hit; sets projectile_extra_damage and projectile_attack_type. */
int projectile_get_attack(uint16_t projectile, uint16_t payload);
/* The projectile from (x, y) meets impact (PROJECTILE_HIT_*, or a square type); returns 1 when it stops. */
int projectile_hit(int impact, int x, int y, int cell, uint16_t projectile);
enum { PROJECTILE_HIT_PARTY_SQUARE = -3, PROJECTILE_HIT_CHAMPION = -2, PROJECTILE_HIT_CREATURE = -1 };
void projectile_process_event(TimelineEvent *e);
/* The damage of an explosion on target: a door, a creature group, or THING_NONE for the party. */
int explosion_get_attack(uint16_t explosion, uint16_t target);
void explosion_process_event(TimelineEvent *e);
/* The 4 cells of the party that a creature on (x, y) attacks first to last; cell 0xff: random order. */
void creature_get_ordered_attack_cells(uint8_t order[4], int x, int y, int party_x, int party_y, int cell);
/* A melee attack of the creature of group on a champion; returns the damage that the champion takes. */
int creature_attack_champion(uint16_t group, int champ);
/* The party moves from (from_x, from_y) to (to_x, to_y): the projectiles there hit the champions. */
void move_check_missiles_hit_party(int from_x, int from_y, int to_x, int to_y);
/* A missile launcher sensor shoots the objects of its square or new objects or spells. */
void sensor_launch_missile(uint16_t sensor, const TimelineEvent *e);
/* A champion throws or casts payload from the party square. */
void champ_launch_projectile(int champ, uint16_t payload, int energy, int attack, int step);

#endif
