#ifndef PROJECTILE_H
#define PROJECTILE_H
#include <stdint.h>
#include "timeline.h"
#include "dungeon.h"

/* Prefixes: projectile_ explosion_ */

/* Record fields of the things of this module; dungeon.h holds the record layout. */
static inline uint8_t projectile_energy(uint16_t projectile) { return thing_byte(projectile, PROJECTILE_ENERGY); }
static inline void projectile_set_energy(uint16_t projectile, uint8_t energy)
{
    set_thing_byte(projectile, PROJECTILE_ENERGY, energy);
}
static inline uint8_t projectile_hit_chance(uint16_t projectile) { return thing_byte(projectile, PROJECTILE_HIT_CHANCE); }
static inline void projectile_set_hit_chance(uint16_t projectile, uint8_t chance)
{
    set_thing_byte(projectile, PROJECTILE_HIT_CHANCE, chance);
}
static inline void projectile_set_holder_waits(uint16_t projectile, bool waits)
{
    set_thing_bit(projectile, PROJECTILE_CARRIED, PROJECTILE_HOLDER_WAITS, waits);
}
static inline uint16_t explosion_type(uint16_t explosion) { return thing_field(explosion, EXPLOSION_DATA, 0, EXPLOSION_TYPE_MASK); }
static inline void explosion_set_type(uint16_t explosion, uint16_t type)
{
    set_thing_field(explosion, EXPLOSION_DATA, 0, EXPLOSION_TYPE_MASK, type);
}
static inline bool explosion_is_centered(uint16_t explosion) { return thing_bit(explosion, EXPLOSION_DATA, EXPLOSION_CENTERED); }
static inline void explosion_set_centered(uint16_t explosion, bool centered)
{
    set_thing_bit(explosion, EXPLOSION_DATA, EXPLOSION_CENTERED, centered);
}
static inline uint16_t explosion_power(uint16_t explosion)
{
    return thing_field(explosion, EXPLOSION_DATA, EXPLOSION_POWER_SHIFT, EXPLOSION_POWER_MASK);
}
static inline void explosion_set_power(uint16_t explosion, uint16_t power)
{
    set_thing_field(explosion, EXPLOSION_DATA, EXPLOSION_POWER_SHIFT, EXPLOSION_POWER_MASK, power);
}

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
#define POWER_MAX 255               /* the power of a missile or an explosion is a byte */
/* The sound of an explosion: (CAT_EXPLOSION, explosion type, SOUND_EXPLODE). */
#define SOUND_EXPLODE 0x81
#define SOUND_PRIORITY_EXPLODE 0x6c

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

#endif
