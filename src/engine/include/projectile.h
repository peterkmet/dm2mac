/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_PROJECTILE_H
#define DM2_ENGINE_PROJECTILE_H
/* Prefixes: projectile_ explosion_ */

/* Projectiles and explosions: their record fields and their timeline events. */

#include <stdbool.h>
#include <stdint.h>
#include "dungeon.h"
#include "move.h"
#include "timeline.h"

/* Record fields of the things of this module; dungeon.h holds the record layout. */
static inline uint8_t projectile_energy(uint16_t projectile)
{
    return thing_byte(projectile, PROJECTILE_ENERGY);
}
static inline void projectile_set_energy(uint16_t projectile, int energy)
{
    thing_set_byte(projectile, PROJECTILE_ENERGY, energy);
}
static inline uint8_t projectile_hit_chance(uint16_t projectile)
{
    return thing_byte(projectile, PROJECTILE_HIT_CHANCE);
}
static inline void projectile_set_hit_chance(uint16_t projectile, int chance)
{
    thing_set_byte(projectile, PROJECTILE_HIT_CHANCE, chance);
}
static inline void projectile_set_holder_waits(uint16_t projectile, bool waits)
{
    thing_set_bit(projectile, PROJECTILE_CARRIED, PROJECTILE_HOLDER_WAITS, waits);
}
static inline uint16_t explosion_type(uint16_t explosion)
{
    return thing_field(explosion, EXPLOSION_DATA, 0, EXPLOSION_TYPE_MASK);
}
static inline void explosion_set_type(uint16_t explosion, int type)
{
    thing_set_field(explosion, EXPLOSION_DATA, 0, EXPLOSION_TYPE_MASK, type);
}
static inline bool explosion_is_centered(uint16_t explosion)
{
    return thing_bit(explosion, EXPLOSION_DATA, EXPLOSION_CENTERED);
}
static inline void explosion_set_centered(uint16_t explosion, bool centered)
{
    thing_set_bit(explosion, EXPLOSION_DATA, EXPLOSION_CENTERED, centered);
}
static inline uint16_t explosion_power(uint16_t explosion)
{
    return thing_field(explosion, EXPLOSION_DATA, EXPLOSION_POWER_SHIFT, EXPLOSION_POWER_MASK);
}
static inline void explosion_set_power(uint16_t explosion, int power)
{
    thing_set_field(explosion, EXPLOSION_DATA, EXPLOSION_POWER_SHIFT, EXPLOSION_POWER_MASK, power);
}

/* Projectiles (thrown objects, spell missiles, missiles of sensors) and explosions. */

/* The payload of a spell missile and the type of an explosion: SPELL_THING_FIRST + the explosion type. */
#define SPELL_THING_FIRST         0xff80
#define SPELL_FIREBALL            0xff80
#define SPELL_SLIME               0xff81
#define SPELL_LIGHTNING           0xff82
#define SPELL_HARM_NON_MATERIAL   0xff83
#define SPELL_DOOR_TOGGLE         0xff84
#define SPELL_POISON_BOLT         0xff86
#define SPELL_POISON_CLOUD        0xff87
#define SPELL_PUSH                0xff89
#define SPELL_PULL                0xff8a
#define SPELL_DOOR_SET            0xff8d
#define SPELL_MISSILE_WALL        0xff8e
#define EXPLOSION_DEATH_CLOUD     0xffa8
#define EXPLOSION_STORM_LIGHTNING 0xffb0
#define EXPLOSION_STORM_BLAST     0xffb1
#define EXPLOSION_SMOKE           0xffe4
/* The cell of an explosion in the middle of its square. */
#define EXPLOSION_AT_CENTER       0xff
/* The power of a missile or an explosion is a byte. */
#define POWER_MAX                 255
/* The sound of an explosion: (CAT_EXPLOSION, explosion type, SOUND_EXPLODE). */
#define SOUND_EXPLODE             0x81
#define SOUND_PRIORITY_EXPLODE    0x6c

/* Bytes of a projectile move event: the projectile, then its position word (event_square_dir with the energy step
   in bits 12-15). */
#define PROJECTILE_EVENT_THING    0
#define PROJECTILE_EVENT_POSITION 2
/* Bytes of an explosion event: its square, then the explosion. */
#define EXPLOSION_EVENT_X         0
#define EXPLOSION_EVENT_Y         1
#define EXPLOSION_EVENT_THING     2

/* A launch of a projectile: payload from (x, y) of the current map in cell, flying toward dir. */
typedef struct {
    uint16_t payload;
    int16_t x, y, cell, dir;
    uint8_t energy; /* its range and power */
    uint8_t hit_chance;
    uint8_t step;     /* the energy that each square takes */
    bool from_sensor; /* a sensor launches it: it checks for hits on its first square */
} ProjectileLaunch;

/* ---- Projectile events ---- */

/* The flight direction of projectile: the direction of its queued move event. */
int projectile_dir(uint16_t projectile);
/* Launches a projectile and queues its first move event. */
void projectile_create(const ProjectileLaunch *launch);
/* Moves the queued move event of projectile to dest: its square, flight direction, map, and cell. */
void projectile_move_event(uint16_t projectile, MoveDest dest);

/* ---- Explosions ---- */

/* An explosion of type with power on (x, y) of the current map in cell (EXPLOSION_AT_CENTER: the middle). */
void explosion_create(uint16_t type, int power, int16_t x, int16_t y, int16_t cell);
/* Moves the events of explosion to dest: their square, map, and cell. */
void explosion_move_events(uint16_t explosion, MoveDest dest);

/* ---- Projectile hits ---- */

/* Removes the queued move of a projectile. */
void projectile_cancel_event(uint16_t projectile);
/* The projectile ends on (x, y): its object falls there (or into list, the possessions of a creature). */
void projectile_destroy(uint16_t projectile, uint16_t *list, int16_t x, int16_t y);
/* The power of a hit of projectile with payload. */
int projectile_get_attack(uint16_t projectile, uint16_t payload);
/* The move event of a projectile: it hits what is on its square, then flies on to the next cell or square. */
void projectile_process_event(TimelineEvent *e);

/* ---- Explosion events ---- */

/* The damage of an explosion on target: a door, a creature group, or THING_NONE for the party. */
int explosion_get_attack(uint16_t explosion, uint16_t target);
/* The event of an explosion: it hurts doors, the party, and creatures on its square, then a cloud fades or the
   explosion ends. */
void explosion_process_event(TimelineEvent *e);

/* ---- Creature attacks ---- */

/* The 4 cells of the party on party_at that a creature on at attacks first to last; cell 0xff: random order. */
void projectile_order_attack_cells(uint8_t order[CELL_COUNT], Location at, Location party_at, int16_t cell);
/* A melee attack of the creature of group on a champion; returns the damage that the champion takes. */
int projectile_attack_champion(uint16_t group, int champ);

/* ---- Missiles that the party walks into ---- */

/* The party moves from from to to (to.x -1: no step): the projectiles there hit the champions. */
void projectile_check_missiles_hit_party(Location from, Location to);

/* ---- Launches of sensors and champions ---- */

/* A missile launcher sensor shoots the objects of its square or new objects or spells. */
void projectile_launch_missile(uint16_t sensor, const TimelineEvent *e);
/* A champion throws or casts payload from the party square. */
void projectile_launch_from_champion(int champ, uint16_t payload, int energy, int attack, int step);

#endif /* DM2_ENGINE_PROJECTILE_H */
