/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "projectile.h"
#include "ai.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "gdat.h"
#include "item.h"
#include "le.h"
#include "move.h"
#include "num.h"
#include "random.h"
#include "save.h"
#include "sensor.h"
#include "sound_request.h"
#include "weather.h"

/* Projectile event position word (event data bytes 2 and 3). */
#define POSITION_STEP_MASK  0x000f
#define POSITION_SQUARE_DIR 0x0fff /* position word: x, y, and direction */
/* The energy step in bits 12-15 of the position word. */
#define POSITION_STEP_SHIFT 12

/* The impacts of hit_impact besides the square types. */
#define PROJECTILE_HIT_PARTY_SQUARE (-3)
#define PROJECTILE_HIT_CHAMPION     (-2)
#define PROJECTILE_HIT_CREATURE     (-1)

/* The explosion types with an attack. */
#define BLAST_TYPE_COUNT         8
/* What an explosion type attacks: bit 0 random power, 1 doors, 2 the party, 3 creatures. */
#define BLAST_RANDOM             0x01
#define BLAST_DOORS              0x02
#define BLAST_PARTY              0x04
#define BLAST_CREATURES          0x08
/* Lasting clouds: the power that they lose per step and the power that they last at. */
#define POISON_CLOUD_DECAY       3 /* power that a poison cloud loses per step */
#define POISON_CLOUD_LAST        6 /* a poison cloud lasts while its power is at least this */
#define POISON_CLOUD_POWER_SHIFT 5 /* the attack of a poison cloud: its power / 32, at most 4, plus a random bit */
#define POISON_CLOUD_ATTACK_MAX  4
#define DEATH_CLOUD_DECAY        40
#define DEATH_CLOUD_LAST         55
#define SMOKE_DURATION           5

/* Sound entries of a thing: launched, explodes. */
#define SOUND_LAUNCH          0x86
#define SOUND_PRIORITY_LAUNCH 0x6e

/* Sensor data: any object or explosion type. */
#define SENSOR_ANY_THING   0x1ff
/* Sensor flags and effects that projectiles read. */
#define SENSOR_STRONG_ONLY SENSOR_STATE /* explosions of power 0xff (0x7f for lightning) only; a hit stops the object */
#define SENSOR_EFFECT_ALWAYS   EFFECT_HOLD /* effect: trigger with the inverted match */
/* The explosion power that a strong-only sensor needs. */
#define STRONG_POWER           255
#define STRONG_LIGHTNING_POWER 127
/* Ornament and door values of the keys that projectiles test. */
#define ORNAMENT_KEY_CATCH     0x13 /* ornament value: the alcove catches thrown objects */
#define DOOR_KEY_GRATE         0x10 /* door value: objects and weak spells fly through the door */

/* Potions that explode on a hit. */
#define POTION_VEN_BOMB  0x03
#define POTION_FIRE_BOMB 0x13

/* Creature info bits that projectiles test. */
#define STATIC_SPELLS_ONLY        0x0100 /* possessions of a static creature: objects fly through it */
#define POSSESSIONS_DIRECTION     0x0001 /* possessions bits 0-3: a dropped object lies in the flight direction cell */
/* Creature info values that projectiles and melee attacks test. */
#define DODGE_NONE                0x0f /* CreatureInfo.dodge bits 4-7: explosion dodge; 0xf: explosions do not hurt */
#define ATTACK_KIND_ANY_DEXTERITY 0x09 /* CreatureInfo.attack_kind: melee attack kinds */
#define ATTACK_KIND_ALWAYS_HITS   0x08
#define PUSH_BOUNCES              0xff /* push_resistance: objects bounce off the creature */
/* The strength of a push or pull spell. */
#define SPELL_PUSH_MIN            40
#define SPELL_PUSH_MAX            254
/* Random parts of the projectile attacks. */
#define ROLL_POISON               0x007f /* poison weakens when this roll is above the energy */
#define ROLL_GRATE                0x007f /* an object flies through a grate when its hit chance beats this roll */
#define ROLL_DOUBLE_POWER         0x01ff /* a thrown object hits twice as hard when its hit chance beats this roll */
#define ROLL_SLIME                0x001f
#define ROLL_SLIME_POISON         0x000f
#define ROLL_FIRE                 0x000f /* a fire spell: two rolls plus a base */
#define ROLL_POISON_CHAMPION      0x0007 /* a hit with poison poisons the champion unless this roll is 0 */
#define ROLL_MELEE                0x001f
#define ROLL_MELEE_POWER          0x000f
#define ROLL_WOUND_LOCATION       0x0070 /* all clear: the hit wounds the hands */
/* The power and the attack of a projectile hit. */
#define SLIME_POISON_BASE         10
#define FIRE_POWER_BASE           10
#define LIGHTNING_POWER_FACTOR    16
#define THROW_CHANCE_SHIFT        4 /* a thrown object: factor hit chance / 16 + 3, power * factor^2 / 128 */
#define THROW_FACTOR_BASE         3
#define THROW_POWER_SHIFT         7
#define ATTACK_POWER_SHIFT        4  /* the attack of a hit: (power + energy) / 16 + 1 */
#define POISON_BOLT_ATTACK_SHIFT  3  /* a poison bolt: energy / 8 + 1 */
#define DEFENSE_SCALE_SHIFT       6  /* the damage to a creature: attack * 64 / defense */
#define HIT_CHANCE_BONUS_BASE     32 /* a high hit chance doubles the attack above this - hit chance / 8 */
#define HIT_CHANCE_BONUS_SHIFT    3
/* The melee attack of a creature. */
#define WOUND_LOCATION_COUNT      4
#define WOUND_LOCATION_LAST       3
#define MELEE_WEAK_POWER_BASE     2  /* a melee attack that armor stops still hits with 2 to 5 */
#define VISIBILITY_INVISIBLE      16 /* the hit bonus against an invisible party */
#define DEXTERITY_ALWAYS_HITS     255
#define DEXTERITY_DODGE_MARGIN    16
#define LUCK_DODGE                60

/* The wounds of a projectile hit: head and torso. */
#define WOUND_PROJECTILE (WOUND_HEAD | WOUND_TORSO)

/* Alert flags (creature_alert). */
#define ALERT_HIT           0x200d
/* Alert damage and chances of a projectile hit. */
#define ALERT_WEAK_DAMAGE   5 /* a hit below this damage (not a poison cloud) alerts with the weak chance */
#define ALERT_CHANCE_WEAK   30
#define ALERT_CHANCE_STRONG 95

/* Sizes of the tables of this file. */
#define FACING_PARITIES          2 /* a creature faces north or south (even), or east or west (odd) */
#define ATTACK_ORDER_ROWS        8 /* a row for each direction and side */
#define ATTACK_ORDER_RANDOM_SIZE 7 /* the cells from each direction on */
#define ORDER_SECOND_PAIR        2 /* the first cell of the second pair of an attack order */
/* The squares ahead of a projectile that flew on where a creature notices it. */
#define ALERT_AHEAD_SQUARES      2
/* The hit chance of a missile that a sensor launches. */
#define LAUNCH_HIT_CHANCE        100

/* The attack of a projectile hit, its extra damage (poison), and its attack type (ATTACK_TYPE_*). */
typedef struct {
    int attack;
    uint16_t extra_damage, type;
} HitAttack;

/* The state of a projectile hit. */
typedef struct {
    uint16_t projectile, payload;
    int payload_type;
    int16_t stop_x, stop_y; /* where the object falls */
    int16_t x, y;           /* the square of the hit */
    uint16_t *holder;       /* the possessions of a creature that catches the object */
    bool explosive, potion_bomb, hit;
    uint16_t damage;
    uint16_t potion;     /* the potion of a bomb, 0 for none */
    uint16_t blast_type; /* the explosion of a bomb potion, and its power */
    int blast_power;
    int push_dir; /* the flight direction of a push or pull spell */
} Hit;

/* What a projectile meets when it flies into the next square. */
typedef enum { ENTRY_ENTERED, ENTRY_TURNED_BACK, ENTRY_STOPPED } SquareEntry;

static const uint8_t explosion_attack_flags[BLAST_TYPE_COUNT] = {BLAST_RANDOM | BLAST_DOORS | BLAST_CREATURES,
                                                                 0,
                                                                 BLAST_RANDOM | BLAST_DOORS | BLAST_CREATURES,
                                                                 BLAST_RANDOM | BLAST_CREATURES,
                                                                 0,
                                                                 0,
                                                                 0,
                                                                 BLAST_PARTY | BLAST_CREATURES};
/* Which way a spell turns on a creature that reflects spells: by the creature facing (even or odd), the cell of the
   spell, and its flight direction; 4: the spell flies on. */
static const int8_t spell_reflection[FACING_PARITIES][CELL_COUNT][DIR_COUNT] = {
    {{3, 2, 1, 0}, {4, 4, 4, 4}, {3, 2, 1, 0}, {4, 4, 4, 4}},
    {{4, 4, 4, 4}, {1, 0, 3, 2}, {4, 4, 4, 4}, {1, 0, 3, 2}},
};
/* The order in which a creature attacks the 4 cells of the party, by its direction and side (8 rows), and the
   cells from the direction on, before a random swap of the pairs. */
static const uint8_t attack_cell_orders[ATTACK_ORDER_ROWS][CELL_COUNT] = {
    {0, 1, 3, 2},
    {1, 0, 2, 3},
    {1, 2, 0, 3},
    {2, 1, 3, 0},
    {3, 2, 0, 1},
    {2, 3, 1, 0},
    {0, 3, 1, 2},
    {3, 0, 2, 1}
};
static const uint8_t attack_cell_orders_random[ATTACK_ORDER_RANDOM_SIZE] = {0, 1, 2, 3, 0, 1, 2};
/* The wounds of a melee hit, by the hit location chosen from the creature's wound nibbles. */
static const uint8_t melee_wounds[WOUND_LOCATION_COUNT] = {WOUND_FEET, WOUND_LEGS, WOUND_TORSO, WOUND_HEAD};

/* The flight direction in the position word of a projectile move event. */
static int flight_dir(const TimelineEvent *e)
{
    return location_dir(le16(e->data + PROJECTILE_EVENT_POSITION));
}

/* The cell beside cell across the flight direction dir (or its opposite): the cell where a projectile goes on. */
static int cell_across(int16_t cell, int16_t dir)
{
    return dir_turn(cell, (dir & 1) == (cell & 1) ? TURN_LEFT : TURN_RIGHT);
}

/* ---- Projectile events ---- */

/* The energy step packed into bits 12-15 of a position word. */
static uint16_t position_pack_step(int step)
{
    return (uint16_t)((step & POSITION_STEP_MASK) << POSITION_STEP_SHIFT);
}

/* The energy step in bits 12-15 of a position word. */
static int position_step(uint16_t word)
{
    return word >> POSITION_STEP_SHIFT;
}

int projectile_dir(uint16_t projectile)
{
    return flight_dir(&g_timeline.events[thing_word(projectile, PROJECTILE_EVENT)]);
}

void projectile_create(const ProjectileLaunch *launch)
{
    uint16_t payload = launch->payload;
    uint16_t t = dungeon_unused_thing(THING_PROJECTILE);
    if (t == THING_NONE) {
        if (thing_type(payload) != THING_EXPLOSION)
            move_thing(thing_with_cell(payload, launch->cell), MOVE_NO_SQUARE, 0, launch->x, launch->y);
        return;
    }
    sndreq_request_with_fallback((SoundCue){item_gdat_category(payload), item_gdat_index(payload), SOUND_LAUNCH,
                                            SOUND_PRIORITY_LAUNCH, SOUND_VOLUME_NORMAL},
                                 SOUND_FALLBACK, launch->x, launch->y, SOUND_STEP_END);
    t = thing_with_cell(t, launch->cell);
    thing_set_word(t, PROJECTILE_PAYLOAD, payload);
    projectile_set_energy(t, launch->energy);
    projectile_set_hit_chance(t, launch->hit_chance);
    dungeon_link_thing_to_square(t, g_party.cur_map, launch->x, launch->y);
    if (payload == SPELL_FIREBALL && g_party.cur_map == g_game.party_map)
        g_party.redraw |= PARTY_REDRAW_VIEW;
    TimelineEvent e = {.time_map = timeline_time_map(g_game.game_time + 1, g_party.cur_map)};
    e.type = launch->from_sensor ? EVENT_PROJECTILE_MOVE_2 : EVENT_PROJECTILE_MOVE;
    put_le16(e.data + PROJECTILE_EVENT_THING, t);
    put_le16(e.data + PROJECTILE_EVENT_POSITION,
             (uint16_t)(event_square_dir(launch->x, launch->y, launch->dir) | position_pack_step(launch->step)));
    thing_set_word(t, PROJECTILE_EVENT, (uint16_t)timeline_add_event(&e));
}

/* Moves a projectile move event to the square, flight direction, and map of dest; the energy step stays. */
static void set_event_dest(TimelineEvent *e, MoveDest dest)
{
    uint16_t w = le16(e->data + PROJECTILE_EVENT_POSITION);
    put_le16(e->data + PROJECTILE_EVENT_POSITION,
             (uint16_t)((w & ~POSITION_SQUARE_DIR) | event_square_dir(dest.x, dest.y, dest.dir)));
    timeline_set_event_map(e, dest.map);
}

void projectile_move_event(uint16_t projectile, MoveDest dest)
{
    TimelineEvent *e = &g_timeline.events[thing_word(projectile, PROJECTILE_EVENT)];
    set_event_dest(e, dest);
    put_le16(e->data + PROJECTILE_EVENT_THING, thing_with_cell(projectile, dest.cell));
}

/* ---- Explosions ---- */

/* An explosion of type with power on (x, y): the explosion sensors of the square react. */
static void trigger_explosion_sensors(uint16_t type, int16_t x, int16_t y, int power)
{
    for (uint16_t t = dungeon_square_first_thing(g_party.cur_map, x, y); t != THING_ENDOFLIST;
         t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_SENSOR)
            continue;
        if (sensor_type(t) != SENSOR_EXPLOSION)
            continue;
        bool match = sensor_data(t) == SENSOR_ANY_THING || sensor_data(t) == (uint16_t)(type - SPELL_THING_FIRST);
        match = match != sensor_flag(t, SENSOR_INVERTED);
        if (sensor_flag(t, SENSOR_STRONG_ONLY) &&
            (int16_t)power < (type == SPELL_LIGHTNING ? STRONG_LIGHTNING_POWER : STRONG_POWER))
            match = false;
        if (sensor_effect(t) == SENSOR_EFFECT_ALWAYS)
            sensor_trigger_target(t, !match, 0);
        else if (match)
            sensor_trigger_target(t, sensor_effect(t), 0);
    }
}

/* The blast of a fireball or lightning hurts the party and the creature on (x, y), then a creature near it. */
static void blast_square(uint16_t type, int power, int16_t x, int16_t y, int16_t cell)
{
    uint16_t full = (uint16_t)(((uint16_t)power >> 1) + 1);
    full = (uint16_t)(full + rand_below(full) + 1);
    uint16_t damage = full;
    if (type != SPELL_FIREBALL && type != EXPLOSION_STORM_LIGHTNING) {
        damage >>= 1;
        if (!damage)
            return;
    }
    if (dun_on_party_square((Location){x, y}))
        champ_damage_party(damage, WOUND_ALL, ATTACK_TYPE_FIRE);
    int16_t nx = x;
    int16_t ny = y;
    int16_t hx = x;
    int16_t hy = y;
    uint16_t g = creature_get_group_at((Location){x, y});
    for (bool first = true;; first = false) {
        if (g != THING_NONE && !creature_current_step_acts(g)) {
            const CreatureInfo *info = creature_group_info(g);
            if (info->door_flags & CREATURE_RESISTS_NON_FIRE && type != SPELL_FIREBALL)
                damage = (uint16_t)max_int(1, (int16_t)(damage >> 2));
            int dodge = info->dodge >> NIBBLE_BIT_COUNT;
            if (dodge != DODGE_NONE) {
                if (info->flags & CREATURE_NON_MATERIAL)
                    damage >>= 2;
                damage = (uint16_t)(damage - rand_below((uint16_t)(dodge * 2 + 1)));
                if ((int16_t)damage > 0)
                    creature_alert(g, (Location){hx, hy}, ALERT_HIT, ALERT_ALWAYS, damage);
            }
        }
        if (!first)
            return;
        GroupNear near = creature_find_group_near_point((Location){nx, ny}, cell);
        g = near.group;
        nx = near.at.x;
        ny = near.at.y;
        if (g == THING_NONE || (nx == x && ny == y))
            return;
        hx = nx;
        hy = ny;
        damage = (uint16_t)((int16_t)full >> 1);
    }
}

void explosion_create(uint16_t type, int power, int16_t x, int16_t y, int16_t cell)
{
    uint16_t t = dungeon_unused_thing(THING_EXPLOSION);
    if (t == THING_NONE)
        return;
    int16_t ex = x;
    int16_t ey = y;
    bool centered = (uint16_t)cell == EXPLOSION_AT_CENTER;
    if (!centered)
        t = thing_with_cell(t, cell);
    explosion_set_type(t, type - SPELL_THING_FIRST);
    explosion_set_centered(t, centered);
    explosion_set_power(t, (unsigned)power);
    int volume = dun_on_party_square((Location){ex, ey})
                     ? SOUND_VOLUME_FULL
                     : min_int(SOUND_VOLUME_FULL, (int16_t)(((uint16_t)power >> 1) + SOUND_VOLUME_NORMAL));
    SoundCue cue = {CAT_EXPLOSION, explosion_type(t), SOUND_EXPLODE, SOUND_PRIORITY_EXPLODE, volume};
    sndreq_request_with_fallback(cue, SOUND_FALLBACK, ex, ey, SOUND_STEP_END);
    dungeon_link_thing_to_square(t, g_party.cur_map, ex, ey);
    int16_t delay = type == EXPLOSION_SMOKE      ? SMOKE_DURATION
                    : type == SPELL_MISSILE_WALL ? (int16_t)((uint16_t)power >> 1)
                                                 : 1;
    TimelineEvent e = {.time_map = timeline_time_map(g_game.game_time + delay, g_party.cur_map),
                       .type = EVENT_EXPLOSION};
    e.data[EXPLOSION_EVENT_X] = (uint8_t)ex;
    e.data[EXPLOSION_EVENT_Y] = (uint8_t)ey;
    put_le16(e.data + EXPLOSION_EVENT_THING, t);
    timeline_add_event(&e);
    uint16_t power_thing = t;
    switch (type) {
    case SPELL_LIGHTNING:
    case SPELL_FIREBALL:
    case EXPLOSION_STORM_LIGHTNING:
    case EXPLOSION_STORM_BLAST:
        blast_square(type, power, x, y, cell);
        if (g_party.cur_map == g_game.party_map)
            g_party.redraw |= PARTY_REDRAW_VIEW;
        break;
    case SPELL_DOOR_TOGGLE:
    case SPELL_DOOR_SET: {
        uint8_t square = dun_get_square((Location){ex, ey});
        if (square_type(square) != SQUARE_DOOR || (square & SQUARE_DOOR_STATE) == DOOR_STATE_BROKEN)
            break;
        uint16_t door = dungeon_square_first_thing(g_party.cur_map, ex, ey);
        power_thing = door;
        if (door_has_button(door))
            sensor_schedule_square_event(ex, ey, 0, type == SPELL_DOOR_TOGGLE ? EFFECT_TOGGLE : EFFECT_SET,
                                         g_game.game_time + 1);
        break;
    }
    }
    /* kept quirk: after a door spell the original reads the power from the high byte of the door record word */
    trigger_explosion_sensors(type, x, y, explosion_power(power_thing));
}

void explosion_move_events(uint16_t explosion, MoveDest dest)
{
    for (int i = 0; i < g_timeline.used_end; i++) {
        TimelineEvent *e = &g_timeline.events[i];
        if (e->type != EVENT_EXPLOSION || le16(e->data + EXPLOSION_EVENT_THING) != explosion)
            continue;
        e->data[EXPLOSION_EVENT_X] = (uint8_t)dest.x;
        e->data[EXPLOSION_EVENT_Y] = (uint8_t)dest.y;
        put_le16(e->data + EXPLOSION_EVENT_THING, thing_with_cell(explosion, dest.cell));
        timeline_set_event_map(e, dest.map);
    }
}

/* ---- Projectile hits ---- */

void projectile_cancel_event(uint16_t projectile)
{
    if (thing_type(projectile) == THING_PROJECTILE)
        timeline_delete_event(thing_word(projectile, PROJECTILE_EVENT));
}

void projectile_destroy(uint16_t projectile, uint16_t *list, int16_t x, int16_t y)
{
    uint16_t payload = thing_word(projectile, PROJECTILE_PAYLOAD);
    if (thing_type(payload) != THING_EXPLOSION) {
        if (list) {
            dungeon_append_thing(payload, list);
        } else {
            uint16_t g = creature_get_group_at((Location){x, y});
            if (g != THING_NONE && creature_group_info(g)->flags & CREATURE_STATIC) {
                int cell = thing_cell(projectile);
                if ((creature_group_info(g)->possessions & POSSESSIONS_KIND_MASK) == POSSESSIONS_DIRECTION)
                    cell = projectile_dir(projectile);
                move_thing(thing_with_cell(payload, cell), MOVE_DROPPED_PROJECTILE, MOVE_IN_POSSESSIONS, x, y);
            } else {
                move_thing(thing_with_cell(payload, thing_cell(projectile)), MOVE_DROPPED_PROJECTILE, 0, x, y);
            }
        }
        sndreq_request_with_fallback((SoundCue){item_gdat_category(projectile), item_gdat_index(projectile),
                                                SOUND_ENTRY_DROP, SOUND_PRIORITY_DROP, SOUND_VOLUME_NORMAL},
                                     SOUND_FALLBACK, x, y, SOUND_STEP_END);
    }
    thing_set_word(projectile, THING_NEXT, THING_NONE);
}

/* The attack of a hit of projectile with payload, with its extra damage and its attack type. */
static HitAttack roll_attack(uint16_t projectile, uint16_t payload)
{
    uint16_t energy = projectile_energy(projectile);
    uint16_t power;
    HitAttack a = {.attack = 0, .extra_damage = 0, .type = ATTACK_TYPE_BLUNT};
    if (thing_type(payload) != THING_EXPLOSION) {
        uint16_t throw_power = item_attr(payload, ATTR_THROW_POWER);
        power = 0;
        if (throw_power) {
            int factor = (uint16_t)((projectile_hit_chance(projectile) >> THROW_CHANCE_SHIFT) + THROW_FACTOR_BASE);
            power = (uint16_t)((uint16_t)(throw_power + (energy >> 1)) * (factor * factor) >> THROW_POWER_SHIFT);
            a.type = ATTACK_TYPE_SHARP;
            a.extra_damage = item_attr(payload, ATTR_EXTRA_DAMAGE);
            if (a.extra_damage && (rand_next() & ROLL_POISON) > energy)
                a.extra_damage = (uint16_t)(a.extra_damage - rand_below((uint16_t)((a.extra_damage >> 1) + 1)));
        }
        power = (uint16_t)(power + rand_2bits());
        power = (uint16_t)(power + item_weight(payload));
        if (projectile_hit_chance(projectile) > (rand_next() & ROLL_DOUBLE_POWER))
            power = (uint16_t)(power * 2);
    } else if (payload == SPELL_SLIME) {
        uint16_t r = rand_next() & ROLL_SLIME_POISON;
        a.extra_damage = (uint16_t)(r + SLIME_POISON_BASE);
        power = (uint16_t)(r + (rand_next() & ROLL_SLIME));
    } else if (payload >= SPELL_HARM_NON_MATERIAL) {
        a.type = ATTACK_TYPE_MAGIC;
        if (payload != SPELL_POISON_BOLT)
            return a;
        a.extra_damage = energy;
        a.attack = (energy >> POISON_BOLT_ATTACK_SHIFT) + 1;
        return a;
    } else {
        a.type = ATTACK_TYPE_FIRE;
        power = rand_next() & ROLL_FIRE;
        power = (uint16_t)(power + (rand_next() & ROLL_FIRE) + FIRE_POWER_BASE);
        if (payload == SPELL_LIGHTNING) {
            a.type = ATTACK_TYPE_LIGHTNING;
            power = (uint16_t)(power * LIGHTNING_POWER_FACTOR + energy);
        }
    }
    uint16_t attack = (uint16_t)(((power + energy) >> ATTACK_POWER_SHIFT) + 1);
    uint16_t r = rand_below((uint16_t)((attack >> 1) + 1));
    attack = (uint16_t)(attack + rand_2bits() + r);
    int bonus = HIT_CHANCE_BONUS_BASE - (projectile_hit_chance(projectile) >> HIT_CHANCE_BONUS_SHIFT);
    int16_t raised = max_int((int16_t)attack, (int16_t)((attack - bonus) * 2));
    a.attack = min_int((int16_t)raised, (int16_t)(energy * 2));
    return a;
}

int projectile_get_attack(uint16_t projectile, uint16_t payload)
{
    return roll_attack(projectile, payload).attack;
}

/* The projectile on (x, y) hits the wall ahead of it while it flies toward dir: the hit sensors of the wall cell
   react, an alcove can catch the object, a portal sensor sends it on. True when the object stops. */
static bool hit_wall_sensors(int16_t x, int16_t y, int16_t dir, uint16_t projectile)
{
    int wall_x = x + g_dir_dx[dir];
    int wall_y = y + g_dir_dy[dir];
    bool done = false;
    int side = dir_opposite(dir);
    uint16_t first = dungeon_square_first_thing(g_party.cur_map, wall_x, wall_y);
    uint16_t payload = thing_word(projectile, PROJECTILE_PAYLOAD);
    bool object = thing_type(payload) != THING_EXPLOSION;
    for (uint16_t t = first; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        int cell = thing_cell(t);
        if (cell != side)
            continue;
        int to_x = wall_x;
        int to_y = wall_y;
        if (object && !done && sensor_wall_ornament_value(first, t, ORNAMENT_KEY_CATCH) && !rand_2bits()) {
            dungeon_unlink_thing_from_square(projectile, g_party.cur_map, x, y);
            projectile = thing_with_cell(projectile, cell);
        } else if (thing_type(t) == THING_SENSOR && object) {
            uint16_t data = sensor_data(t);
            int kind = sensor_type(t);
            if (kind == SENSOR_PROJECTILE_HIT) {
                bool match = data == SENSOR_ANY_THING || data == item_get_index(payload);
                match = match != sensor_flag(t, SENSOR_INVERTED);
                if (!match)
                    continue;
                sensor_trigger_target(t, sensor_effect(t) == SENSOR_EFFECT_ALWAYS ? 0 : sensor_effect(t), 0);
                if (done || !sensor_flag(t, SENSOR_STRONG_ONLY))
                    continue;
                dungeon_unlink_thing_from_square(projectile, g_party.cur_map, x, y);
                projectile = thing_with_cell(projectile, side);
            } else if (kind == SENSOR_PROJECTILE_PORTAL && !done &&
                       (data == SENSOR_ANY_THING || data == item_get_index(payload))) {
                dungeon_unlink_thing_from_square(projectile, g_party.cur_map, x, y);
                to_x = sensor_target_x(t);
                to_y = sensor_target_y(t);
                int to_cell = square_type(dun_get_square((Location){to_x, to_y})) == SQUARE_WALL ? sensor_target_cell(t)
                                                                                                 : sensor_effect(t);
                projectile = thing_with_cell(projectile, to_cell);
            } else {
                continue;
            }
        } else {
            continue;
        }
        projectile_destroy(projectile, NULL, to_x, to_y);
        done = true;
    }
    return done;
}

/* The projectile meets the creature near the cell of its hit square; true when it hits. */
static bool hit_creature(Hit *h, int16_t cell)
{
    GroupNear near = creature_find_group_near_point((Location){h->x, h->y}, cell);
    uint16_t g = near.group;
    if (g == THING_NONE || creature_current_step_acts(g))
        return false;
    const CreatureInfo *info = creature_group_info(g);
    uint16_t payload = h->payload;
    if (info->flags & CREATURE_NON_MATERIAL && payload != SPELL_HARM_NON_MATERIAL)
        return false;
    bool bounce;
    if (!(info->flags & CREATURE_STATIC)) {
        bounce = info->push_resistance == PUSH_BOUNCES;
    } else {
        if (info->possessions & STATIC_SPELLS_ONLY && payload < SPELL_THING_FIRST)
            return false;
        if (creature_door_size(info) > 1)
            return false;
        bounce = !(info->possessions & CREATURE_POSSESSIONS_ON_FLOOR);
    }
    if (bounce) { /* the object falls back on the square that it came from */
        int back = dir_opposite(projectile_dir(h->projectile));
        h->stop_x = (int16_t)(h->stop_x + g_dir_dx[back]);
        h->stop_y = (int16_t)(h->stop_y + g_dir_dy[back]);
        h->projectile = thing_with_cell(h->projectile, cell_across(thing_cell(h->projectile), back));
    }
    HitAttack a = roll_attack(h->projectile, payload);
    h->damage = (uint16_t)((uint16_t)(a.attack << DEFENSE_SCALE_SHIFT) / info->defense);
    if (h->damage) {
        uint16_t damage = (uint16_t)(creature_roll_armor_factor(g, a.extra_damage) + h->damage);
        if (info->door_flags & CREATURE_RESISTS_NON_FIRE && a.type != ATTACK_TYPE_FIRE)
            damage = (uint16_t)((int16_t)damage >> 2);
        creature_alert(g, near.at, ALERT_HIT, ALERT_ALWAYS, damage);
        if (!h->explosive && info->flags & CREATURE_CATCHES_OBJECTS &&
            item_attr(payload, ATTR_FLAGS) & ITEM_FLAG_CAUGHT)
            h->holder = group_possessions(g);
    }
    if (h->potion_bomb || h->payload_type == THING_WEAPON ||
        (h->payload_type == THING_EXPLOSION && payload != SPELL_DOOR_TOGGLE &&
         (payload < SPELL_PUSH || payload > SPELL_MISSILE_WALL)))
        creature_alert(g, near.at, ALERT_MOOD_ANGRY,
                       h->damage < ALERT_WEAK_DAMAGE && payload != SPELL_POISON_CLOUD ? ALERT_CHANCE_WEAK
                                                                                      : ALERT_CHANCE_STRONG,
                       0);
    return true;
}

/* The projectile hits the champion in cell; false when no champion stands there. */
static bool hit_champion(Hit *h, int16_t cell)
{
    int16_t champion_index = champ_get_index_in_cell(cell);
    if (champion_index < 0)
        return false;
    h->hit = true;
    HitAttack a = roll_attack(h->projectile, h->payload);
    uint16_t attack = (uint16_t)a.attack;
    if (!attack)
        return true;
    if (h->payload == SPELL_POISON_BOLT)
        a.extra_damage = (uint16_t)(a.extra_damage * 2);
    /* the original compares without a wrap: only flights to the north and east can face a champion */
    if (champion(champion_index)->dir == projectile_dir(h->projectile) + TURN_BACK)
        a.type |= ATTACK_BLOCKABLE; /* the champion faces it */
    if ((int16_t)champ_take_damage(champion_index, (int16_t)attack, WOUND_PROJECTILE, a.type) && a.extra_damage &&
        rand_next() & ROLL_POISON_CHAMPION)
        champ_poison(champion_index, a.extra_damage);
    return true;
}

/* The projectile hits the door at (x, y). True when it stops, false when it flies through. */
static bool hit_door(Hit *h)
{
    uint8_t square = dungeon_square(g_party.cur_map, h->x, h->y);
    int state = square & SQUARE_DOOR_STATE;
    uint16_t door = dungeon_square_first_thing(g_party.cur_map, h->x, h->y);
    if (state != DOOR_STATE_BROKEN && (h->payload == SPELL_DOOR_TOGGLE || h->payload == SPELL_DOOR_SET))
        return true;
    if (state == DOOR_STATE_BROKEN || state <= DOOR_STATE_ONE_FOURTH)
        return false;
    if (sensor_door_value(door, DOOR_KEY_GRATE)) {
        bool passes;
        if (h->payload_type == THING_EXPLOSION) {
            passes = h->payload >= SPELL_HARM_NON_MATERIAL;
        } else {
            passes = projectile_hit_chance(h->projectile) > (rand_next() & ROLL_GRATE) &&
                     item_attr(h->payload, ATTR_FLAGS) & ITEM_FLAG_GRATE_PASSES;
        }
        if (passes)
            return false;
    }
    uint16_t attack = (uint16_t)(projectile_get_attack(h->projectile, h->payload) + 1);
    sensor_door_try_destroy(h->x, h->y, (int16_t)(rand_below(attack) + attack), false, 0);
    return true;
}

/* Starts a hit of projectile at a square: its payload (a bomb potion becomes its blast) and the square stop where its
   object stops. */
static void hit_start(Hit *h, uint16_t projectile, Location at, Location stop)
{
    h->projectile = projectile;
    h->payload = thing_word(projectile, PROJECTILE_PAYLOAD);
    h->payload_type = thing_type(h->payload);
    if (h->payload_type == THING_POTION) {
        int kind = potion_type(h->payload);
        if (kind == POTION_VEN_BOMB || kind == POTION_FIRE_BOMB) {
            h->blast_type = kind == POTION_VEN_BOMB ? SPELL_POISON_CLOUD : SPELL_FIREBALL;
            h->potion_bomb = true;
            h->blast_power = potion_power(h->payload);
            h->potion = h->payload;
        }
    }
    h->explosive = h->payload_type == THING_EXPLOSION && h->payload != SPELL_SLIME && h->payload != SPELL_POISON_BOLT;
    h->stop_x = stop.x;
    h->stop_y = stop.y;
    h->x = at.x;
    h->y = at.y;
}

/* A projectile with an explosive payload explodes at (x, y) in cell; returns the strength of a push or pull spell. */
static uint16_t hit_explode(Hit *h, int16_t x, int16_t y, int16_t cell)
{
    uint16_t power;
    uint16_t push_strength = 0;
    if (h->potion_bomb) {
        thing_set_word(h->potion, THING_NEXT, THING_NONE);
        thing_set_word(h->projectile, PROJECTILE_PAYLOAD, h->blast_type);
        h->payload = h->blast_type;
        power = (uint16_t)h->blast_power;
    } else {
        power = projectile_energy(h->projectile);
        if (h->payload == SPELL_POISON_BOLT && !(power >>= 2))
            return 0;
        if (h->payload == SPELL_LIGHTNING && !(power >>= 1))
            return 0;
    }
    if (h->payload == SPELL_PUSH || h->payload == SPELL_PULL) {
        h->push_dir = projectile_dir(h->projectile);
        push_strength = projectile_energy(h->projectile);
    }
    int16_t at = h->payload == SPELL_POISON_CLOUD || h->payload == SPELL_DOOR_TOGGLE || h->payload == SPELL_DOOR_SET
                     ? EXPLOSION_AT_CENTER
                     : cell;
    explosion_create(h->payload, power, x, y, at);
    return push_strength;
}

/* The projectile at a square meets impact (PROJECTILE_HIT_*, or a square type); its object stops at stop. True when
   it stops. */
static bool hit_impact(int impact, Location at, Location stop, int16_t cell, uint16_t projectile)
{
    Hit h = {0};
    hit_start(&h, projectile, at, stop);
    int16_t fall_x = h.stop_x; /* where the explosion and the object removal happen */
    int16_t fall_y = h.stop_y;
    int16_t kind = (int16_t)impact;
    if (kind == SQUARE_WALL) {
        if (hit_wall_sensors(fall_x, fall_y, projectile_dir(projectile), projectile))
            return true;
    } else if (kind == SQUARE_DOOR) {
        if (!hit_door(&h))
            return false;
    } else if (kind == PROJECTILE_HIT_CREATURE || kind == PROJECTILE_HIT_CHAMPION ||
               kind == PROJECTILE_HIT_PARTY_SQUARE) {
        bool champion_hit = kind != PROJECTILE_HIT_CREATURE && hit_champion(&h, cell);
        if (kind != PROJECTILE_HIT_CHAMPION) {
            if (hit_creature(&h, cell))
                h.hit = true;
            else if (kind != PROJECTILE_HIT_PARTY_SQUARE)
                return false;
        } else if (!champion_hit) {
            return false;
        }
    }
    if (kind == PROJECTILE_HIT_PARTY_SQUARE && !h.hit)
        return false;
    uint16_t push_strength = 0;
    if (h.explosive || h.potion_bomb || h.payload == SPELL_POISON_BOLT)
        push_strength = hit_explode(&h, fall_x, fall_y, cell);
    dungeon_unlink_thing_from_square(h.projectile, g_party.cur_map, fall_x, fall_y);
    projectile_destroy(h.projectile, h.holder, h.stop_x, h.stop_y);
    if (push_strength) {
        int16_t strength = max_int((int16_t)min_int((int16_t)push_strength, SPELL_PUSH_MAX), SPELL_PUSH_MIN);
        int dir = h.payload == SPELL_PUSH ? h.push_dir : dir_opposite(h.push_dir);
        creature_push_group(at, (int16_t)dir, strength);
    }
    return true;
}

/* The projectile on (x, y) meets impact there, and its object stops there; true when it stops. */
static bool hit_square(int impact, int16_t x, int16_t y, int16_t cell, uint16_t projectile)
{
    return hit_impact(impact, (Location){x, y}, (Location){x, y}, cell, projectile);
}

/* Sets the flight direction in the position word of event. */
static void set_event_dir(TimelineEvent *e, int16_t dir)
{
    uint16_t w = le16(e->data + PROJECTILE_EVENT_POSITION);
    put_le16(e->data + PROJECTILE_EVENT_POSITION,
             (uint16_t)((w & ~POSITION_SQUARE_DIR) | event_square_dir(location_x(w), location_y(w), dir)));
}

/* The creature on (x, y) may reflect a spell of the projectile; true when a creature stands there and reflects
   spells (then the projectile hits nothing on this square). */
static bool reflect_spell(TimelineEvent *e, uint16_t projectile, int16_t x, int16_t y)
{
    uint16_t g = creature_get_group_at((Location){x, y});
    if (g == THING_NONE || !(creature_group_info(g)->flags & CREATURE_REFLECTS_SPELLS) ||
        thing_word(projectile, PROJECTILE_PAYLOAD) < SPELL_THING_FIRST)
        return false;
    int facing = group_dir(g) & 1;
    int dir = spell_reflection[facing][(int16_t)thing_cell(projectile) & DIR_MASK][flight_dir(e)];
    if (dir < DIR_COUNT) {
        set_event_dir(e, dir);
        /* the original also writes the direction into the freed event of this move */
        set_event_dir(&g_timeline.events[thing_word(projectile, PROJECTILE_EVENT)], dir);
    }
    return true;
}

/* A creature on the squares ahead of a projectile that flew on to dest notices it. */
static void alert_creatures_ahead(MoveDest dest)
{
    int saved = g_party.cur_map;
    if (dest.map != g_party.cur_map)
        dun_set_current_map(dest.map);
    Location at = {dest.x, dest.y};
    for (int i = 0; i < ALERT_AHEAD_SQUARES; i++) {
        at.x = (int16_t)(at.x + g_dir_dx[dest.dir]);
        at.y = (int16_t)(at.y + g_dir_dy[dest.dir]);
        uint16_t g = creature_get_group_at(at);
        if (g != THING_NONE)
            creature_alert(g, at, ALERT_STARTLED, ALERT_ALWAYS, 0);
        if (i)
            break;
        uint8_t square = dun_get_square(at);
        int type = square_type(square);
        int state = square & SQUARE_DOOR_STATE;
        if (type == SQUARE_WALL || (type == SQUARE_FAKE_WALL && !(square & SQUARE_FAKE_WALL_OPEN)) ||
            (type == SQUARE_DOOR && state != DOOR_STATE_BROKEN && state > DOOR_STATE_ONE_FOURTH))
            break;
    }
    /* the original walks the destination globals ahead */
    g_party.dest_x = at.x;
    g_party.dest_y = at.y;
    if (dest.map != saved)
        dun_set_current_map(saved);
}

/* The projectile of event e on (*x, *y) flies into the next square in its flight direction: it hits a wall or stairs
   there and stops, or a missile wall there turns it back (the square stays). As in the original, a turn back also
   writes the direction into the freed event slot of this move, which the projectile word still names. */
static SquareEntry enter_next_square(TimelineEvent *e, uint16_t projectile, int16_t *x, int16_t *y)
{
    int dir = flight_dir(e);
    int cell = thing_cell(projectile);
    int16_t from_x = *x;
    int16_t from_y = *y;
    *x = (int16_t)(*x + g_dir_dx[dir]);
    *y = (int16_t)(*y + g_dir_dy[dir]);
    uint8_t square = dun_get_square((Location){*x, *y});
    int type = square_type(square);
    if ((type == SQUARE_WALL || (type == SQUARE_FAKE_WALL && !(square & (SQUARE_FAKE_WALL_OPEN | SQUARE_FLAG_0))) ||
         (type == SQUARE_STAIRS && square_type(dungeon_square(g_party.cur_map, from_x, from_y)) == SQUARE_STAIRS)) &&
        hit_square(type, from_x, from_y, cell, projectile))
        return ENTRY_STOPPED;
    for (uint16_t t = dungeon_square_first_object(g_party.cur_map, *x, *y); t != THING_ENDOFLIST;
         t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_EXPLOSION || explosion_type(t) != BLAST_MISSILE_WALL)
            continue;
        TimelineEvent *queued = &g_timeline.events[thing_word(projectile, PROJECTILE_EVENT)];
        int back = dir_opposite(flight_dir(queued));
        set_event_dir(e, back);
        set_event_dir(queued, back);
        *x = from_x;
        *y = from_y;
        return ENTRY_TURNED_BACK;
    }
    return ENTRY_ENTERED;
}

void projectile_process_event(TimelineEvent *event)
{
    TimelineEvent e = *event;
    uint16_t projectile = le16(e.data + PROJECTILE_EVENT_THING);
    uint16_t position = le16(e.data + PROJECTILE_EVENT_POSITION);
    int16_t x = location_x(position);
    int16_t y = location_y(position);
    if (e.type == EVENT_PROJECTILE_MOVE) {
        e.type = EVENT_PROJECTILE_MOVE_2; /* the first move: no hit on the launch square */
    } else {
        int cell = thing_cell(projectile);
        if (!reflect_spell(&e, projectile, x, y) &&
            ((dun_on_party_square((Location){x, y}) &&
              hit_square(PROJECTILE_HIT_PARTY_SQUARE, x, y, cell, projectile)) ||
             hit_square(PROJECTILE_HIT_CREATURE, x, y, cell, projectile)))
            return;
        int step = position_step(le16(e.data + PROJECTILE_EVENT_POSITION));
        if (projectile_energy(projectile) <= step) {
            dungeon_unlink_thing_from_square(projectile, g_party.cur_map, x, y);
            projectile_destroy(projectile, NULL, x, y);
            return;
        }
        projectile_set_energy(projectile, (uint8_t)(projectile_energy(projectile) - step));
        uint8_t chance = projectile_hit_chance(projectile);
        projectile_set_hit_chance(projectile, chance < step ? 0 : (uint8_t)(chance - step));
    }
    projectile = le16(e.data + PROJECTILE_EVENT_THING);
    int cell = thing_cell(projectile);
    int dir = flight_dir(&e);
    bool leaves = dir == cell ||
                  dir_turn(dir, TURN_RIGHT) == cell; /* the projectile is in the front cells: it leaves the square */
    int16_t from_x = x;
    int16_t from_y = y;
    if (leaves) {
        SquareEntry entry = enter_next_square(&e, projectile, &x, &y);
        if (entry == ENTRY_STOPPED)
            return;
        leaves = entry == ENTRY_ENTERED;
    }
    int new_cell = cell_across(cell, dir);
    if (leaves) {
        MoveDest dest = move_thing_dest(thing_with_cell(projectile, new_cell), from_x, from_y, x, y).dest;
        if (thing_word(projectile, PROJECTILE_PAYLOAD) == SPELL_FIREBALL && g_party.cur_map == g_game.party_map)
            g_party.redraw |= PARTY_REDRAW_VIEW;
        set_event_dest(&e, dest);
        projectile = thing_with_cell(projectile, dest.cell);
        alert_creatures_ahead(dest);
    } else {
        if (square_type(dun_get_square((Location){x, y})) == SQUARE_DOOR &&
            hit_square(SQUARE_DOOR, x, y, cell, projectile))
            return;
        dungeon_unlink_thing_from_square(projectile, g_party.cur_map, x, y);
        projectile = thing_with_cell(projectile, new_cell);
        dungeon_link_thing_to_square(projectile, g_party.cur_map, x, y);
    }
    e.time_map++;
    put_le16(e.data + PROJECTILE_EVENT_THING, projectile);
    thing_set_word(projectile, PROJECTILE_EVENT, (uint16_t)timeline_add_event(&e));
}

/* ---- Explosion events ---- */

int explosion_get_attack(uint16_t explosion, uint16_t target)
{
    int type = explosion_type(explosion);
    if (type >= BLAST_TYPE_COUNT)
        return 0;
    uint8_t flags = explosion_attack_flags[type];
    if (!flags)
        return 0;
    int kind;
    if (target == THING_NONE) {
        if (!(flags & BLAST_PARTY))
            return 0;
        kind = -1;
    } else {
        kind = thing_type(target);
        if (!(kind == THING_DOOR && flags & BLAST_DOORS) && !(kind == THING_GROUP && flags & BLAST_CREATURES))
            return 0;
    }
    uint16_t attack = explosion_power(explosion);
    if (kind == THING_GROUP && creature_group_info(target)->door_flags & CREATURE_RESISTS_NON_FIRE &&
        type != BLAST_FIREBALL)
        attack = (uint16_t)((int16_t)attack >> 2);
    if (flags & BLAST_RANDOM) {
        attack = (uint16_t)((explosion_power(explosion) >> 1) + 1);
        attack = (uint16_t)(attack + rand_below(attack) + 1);
    }
    switch (type) {
    case BLAST_LIGHTNING:
        attack = (uint16_t)((int16_t)attack >> 1);
        __attribute__((fallthrough));
    case BLAST_FIREBALL:
        if (kind == THING_DOOR && !door_flag(target, DOOR_BREAKABLE_BY_MAGIC))
            return 0;
        return attack;
    case BLAST_HARM_NON_MATERIAL:
        return creature_group_info(target)->flags & CREATURE_NON_MATERIAL ? attack : 0;
    case BLAST_POISON_CLOUD:
        attack =
            (uint16_t)max_int(1, (int16_t)(min_int((int16_t)(explosion_power(explosion) >> POISON_CLOUD_POWER_SHIFT),
                                                   POISON_CLOUD_ATTACK_MAX) +
                                           rand_bit()));
        if (kind == THING_GROUP)
            attack = (uint16_t)creature_roll_armor_factor(target, attack);
        return attack;
    default:
        return attack;
    }
}

void explosion_process_event(TimelineEvent *e)
{
    int x = e->data[EXPLOSION_EVENT_X];
    int y = e->data[EXPLOSION_EVENT_Y];
    uint16_t t = le16(e->data + EXPLOSION_EVENT_THING);
    if (square_type(dun_get_square((Location){x, y})) == SQUARE_DOOR) {
        int16_t attack = (int16_t)explosion_get_attack(t, dungeon_square_first_thing(g_party.cur_map, x, y));
        if (attack)
            sensor_door_try_destroy(x, y, attack, true, 0);
    }
    int type = explosion_type(t);
    if (type != BLAST_MISSILE_WALL && type != BLAST_FIREBALL && type != BLAST_LIGHTNING) {
        uint16_t attack;
        if (dun_on_party_square((Location){x, y}) && (attack = (uint16_t)explosion_get_attack(t, THING_NONE)))
            champ_damage_party(attack, 0, 0);
        uint16_t g = creature_get_group_at((Location){x, y});
        if (g != THING_NONE && !creature_current_step_acts(g) && (attack = (uint16_t)explosion_get_attack(t, g)))
            creature_alert(g, (Location){x, y}, ALERT_HIT, ALERT_ALWAYS, attack);
        bool lasts = true;
        int power = explosion_power(t);
        switch (explosion_type(t)) {
        case BLAST_POISON_CLOUD:
            if (power < POISON_CLOUD_LAST)
                lasts = false;
            else
                explosion_set_power(t, (unsigned)(power - POISON_CLOUD_DECAY));
            break;
        case BLAST_DEATH_CLOUD:
            if (power <= DEATH_CLOUD_LAST)
                lasts = false;
            else
                explosion_set_power(t, (unsigned)(power - DEATH_CLOUD_DECAY));
            break;
        case BLAST_SMOKE:
            sndreq_request_with_fallback(
                (SoundCue){CAT_EXPLOSION, explosion_type(t), SOUND_EXPLODE, SOUND_PRIORITY_EXPLODE, SOUND_VOLUME_LOUD},
                SOUND_FALLBACK, x, y, SOUND_STEP_END);
            explosion_set_type(t, explosion_type(t) + 1);
            break;
        default:
            lasts = false;
        }
        if (lasts) {
            TimelineEvent next = *e;
            next.time_map++;
            timeline_add_event(&next);
            return;
        }
    }
    type = explosion_type(t);
    if (g_party.cur_map == g_game.party_map &&
        (type == BLAST_LIGHTNING || type == BLAST_FIREBALL || type == BLAST_STORM_LIGHTNING))
        g_party.redraw |= PARTY_REDRAW_VIEW;
    dungeon_unlink_thing_from_square(t, g_party.cur_map, x, y);
    thing_set_word(t, THING_NEXT, THING_NONE);
}

/* ---- Creature attacks ---- */

void projectile_order_attack_cells(uint8_t order[CELL_COUNT], Location at, Location party_at, int16_t cell)
{
    int dir = dun_get_direction_to(at.x, at.y, party_at.x, party_at.y);
    if ((uint16_t)cell == CELL_ANY) {
        for (int i = 0; i < DIR_COUNT; i++)
            order[i] = attack_cell_orders_random[dir + i];
        uint8_t swap;
        if (rand_bit()) {
            swap = order[0];
            order[0] = order[1];
            order[1] = swap;
        }
        if (rand_bit()) {
            swap = order[ORDER_SECOND_PAIR];
            order[ORDER_SECOND_PAIR] = order[ORDER_SECOND_PAIR + 1];
            order[ORDER_SECOND_PAIR + 1] = swap;
        }
        return;
    }
    uint16_t side = (uint16_t)cell;
    if (!(dir & 1))
        side++;
    for (int i = 0; i < DIR_COUNT; i++)
        order[i] = attack_cell_orders[dir * 2 + (side >> 1 & 1)][i];
}

/* The chance nibble of a wound location: 0 feet, 1 legs (wound_feet_legs), 2 torso, 3 head (wound_torso_head). */
static int wound_nibble(const CreatureInfo *info, int location)
{
    uint8_t chances = location < 2 ? info->wound_feet_legs : info->wound_torso_head;
    return location & 1 ? chances >> NIBBLE_BIT_COUNT : chances & NIBBLE_MASK;
}

/* True when the champion dodges the melee attack of a creature with dexterity. */
static bool attack_missed(const CreatureInfo *info, int16_t champion_index, uint16_t dexterity, int difficulty)
{
    int visibility;
    if (g_game.magic_light[MAGIC_INVISIBILITY] && !(info->flags & CREATURE_SEES_INVISIBLE))
        visibility = VISIBILITY_INVISIBLE;
    else if (info->flags & CREATURE_SEES_IN_DARK)
        visibility = 0;
    else
        visibility = g_view_light_level * 2;
    if (g_party_sleeping || dexterity == DEXTERITY_ALWAYS_HITS)
        return false;
    int threshold = (uint16_t)visibility + (int16_t)((rand_next() & ROLL_MELEE) + dexterity + difficulty);
    return ((uint16_t)champ_get_dexterity(champion_index) >= threshold - DEXTERITY_DODGE_MARGIN && rand_2bits()) ||
           champ_test_luck(champion(champion_index), LUCK_DODGE);
}

/* The wounds of a melee hit: the hands, or a location by the wound chances of the creature. */
static uint16_t attack_wounds(const CreatureInfo *info)
{
    uint32_t r = rand_next();
    if (!(r & ROLL_WOUND_LOCATION) || (!info->wound_feet_legs && !info->wound_torso_head))
        return r & 1 ? WOUND_ACTION_HAND : WOUND_READY_HAND;
    uint16_t roll = r & NIBBLE_MASK;
    if (!roll)
        roll = 1;
    int location = 0;
    while (location < WOUND_LOCATION_LAST && roll > wound_nibble(info, location))
        location++;
    return melee_wounds[location];
}

int projectile_attack_champion(uint16_t group, int16_t champion_index)
{
    if ((uint16_t)champion_index >= g_game.champion_count)
        return 0;
    Champion *c = champion(champion_index);
    if (!c->health)
        return 0;
    int16_t damage = 0;
    int difficulty = dungeon_map_difficulty(g_party.cur_map) * 2;
    const CreatureInfo *info = creature_group_info(group);
    int kind = info->attack_kind;
    uint16_t dexterity = info->dexterity;
    if (kind == ATTACK_KIND_ANY_DEXTERITY)
        dexterity = (uint16_t)min_int(DEXTERITY_ALWAYS_HITS, (int16_t)(dexterity * 2));
    else if (kind == ATTACK_KIND_ALWAYS_HITS)
        dexterity = DEXTERITY_ALWAYS_HITS;
    if (!attack_missed(info, champion_index, dexterity, difficulty)) {
        uint16_t wounds = attack_wounds(info);
        int16_t power = (int16_t)(info->attack + min_int((int16_t)info->attack,
                                                         (int16_t)((rand_next() & ROLL_MELEE_POWER) + difficulty)));
        bool skipped = false;
        if (kind != ATTACK_KIND_ALWAYS_HITS) {
            power = (int16_t)(power - champ_get_skill_level(champion_index, SKILL_PARRY, 1) * 2);
            if (power <= 1) {
                if (rand_bit())
                    skipped = true;
                else
                    power = (int16_t)(rand_2bits() + MELEE_WEAK_POWER_BASE);
            }
        }
        if (!skipped) {
            power >>= 1;
            uint16_t r1 = rand_below((uint16_t)power);
            power = (int16_t)(power + rand_2bits() + r1);
            power = (int16_t)(power + rand_below((uint16_t)power));
            power >>= 2;
            power = (int16_t)(power + rand_2bits() + 1);
            if (rand_bit())
                power = (int16_t)(power - (rand_below((uint16_t)((power >> 1) + 1)) - 1));
            damage = (int16_t)champ_take_damage(champion_index, power, wounds, kind);
            if (damage) {
                sndreq_request_after(sndreq_cue_with_fallback((SoundCue){CAT_CHAMPION, c->portrait, SOUND_HURT,
                                                                         SOUND_PRIORITY_HURT, SOUND_VOLUME_LOUD},
                                                              SOUND_FALLBACK),
                                     g_game.party_x, g_game.party_y, SOUND_DELAY_HURT);
                int16_t poison;
                if (info->poison && rand_bit() &&
                    (poison = (int16_t)champ_get_stat_adjusted_attack(champion(champion_index), STAT_VITALITY,
                                                                      info->poison)) != 0)
                    champ_poison(champion_index, (uint16_t)poison);
            }
        }
    }
    if (g_party_sleeping)
        champ_wake_party();
    return damage;
}

/* ---- Missiles that the party walks into ---- */

/* The number of square steps between two squares (no diagonals). */
static int16_t steps_between(int x1, int y1, int x2, int y2)
{
    int16_t dx = (int16_t)(x1 - x2);
    int16_t dy = (int16_t)(y1 - y2);
    return (int16_t)((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy));
}

void projectile_check_missiles_hit_party(Location from, Location to)
{
    /* by cell: the cell of the champion that a projectile there hits, -1 none */
    int8_t champs[CELL_COUNT];
    int8_t next[CELL_COUNT];
    bool again = false;
    for (int cell = 0; cell < CELL_COUNT; cell++) {
        champs[cell] = champ_get_index_in_cell(cell) >= 0 ? (int8_t)cell : -1;
        next[cell] = -1;
    }
    if (to.x >= 0 && steps_between(from.x, from.y, to.x, to.y) == 1) {
        int dir = dun_get_direction_to(from.x, from.y, to.x, to.y);
        int dir2 = dir_turn(dir, TURN_RIGHT);
        next[dir_turn(dir, TURN_LEFT)] = champs[dir];
        next[dir_turn(dir2, TURN_RIGHT)] = champs[dir2];
        again = champs[dir] >= 0 || champs[dir2] >= 0;
        if (champs[dir] < 0)
            champs[dir] = champs[dir_turn(dir, TURN_LEFT)];
        if (champs[dir2] < 0)
            champs[dir2] = champs[dir_turn(dir2, TURN_RIGHT)];
    }
    Location stop = from; /* where a hit projectile falls; the projectiles are on square */
    Location square = from;
    for (;;) {
        uint16_t t = dungeon_square_first_thing(g_party.cur_map, square.x, square.y);
        while (t != THING_ENDOFLIST) {
            if (thing_type(t) == THING_PROJECTILE &&
                g_timeline.events[thing_word(t, PROJECTILE_EVENT)].type != EVENT_PROJECTILE_MOVE &&
                champs[thing_cell(t)] >= 0 &&
                hit_impact(PROJECTILE_HIT_PARTY_SQUARE, from, stop, champs[thing_cell(t)], t)) {
                projectile_cancel_event(t);
                t = dungeon_square_first_thing(g_party.cur_map, square.x, square.y);
                continue;
            }
            t = dungeon_next_thing(t);
        }
        if (!again)
            return;
        square = to;
        stop = to;
        for (int i = 0; i < CELL_COUNT; i++)
            champs[i] = next[i];
        again = false;
    }
}

/* ---- Launches of sensors and champions ---- */

/* The first object on the square (x, y) in the two cells on side dir, THING_ENDOFLIST for none. */
static uint16_t launch_object(int16_t x, int16_t y, int16_t dir)
{
    uint16_t t = dungeon_square_first_thing(g_party.cur_map, x, y);
    while (t != THING_ENDOFLIST &&
           (thing_type(t) <= THING_SENSOR || (thing_cell(t) != dir && thing_cell(t) != dir_turn(dir, TURN_RIGHT))))
        t = dungeon_next_thing(t);
    return t;
}

void projectile_launch_missile(uint16_t sensor, const TimelineEvent *e)
{
    int16_t x = e->data[EVENT_X];
    int16_t y = e->data[EVENT_Y];
    int dir = e->data[EVENT_CELL];
    int cell = dir_opposite(dir);
    int kind = sensor_type(sensor);
    uint16_t data = sensor_data(sensor);
    bool single = kind == LAUNCH_ITEM || kind == LAUNCH_SPELL || kind == LAUNCH_OBJECT;
    uint16_t first;
    uint16_t second = (uint16_t)(data + SPELL_THING_FIRST);
    if (kind == LAUNCH_OBJECT || kind == LAUNCH_OBJECTS) {
        first = launch_object(x, y, dir);
        if (first == THING_ENDOFLIST)
            return;
        dungeon_unlink_thing_from_square(first, g_party.cur_map, x, y);
        if (!single) {
            second = launch_object(x, y, dir);
            if (second == THING_ENDOFLIST)
                single = true;
            else
                dungeon_unlink_thing_from_square(second, g_party.cur_map, x, y);
        }
    } else if (kind == LAUNCH_SPELL || kind == LAUNCH_SPELLS) {
        first = second;
    } else {
        first = item_create_by_index(data);
        if (first == THING_NONE)
            return;
        if (!single && (second = item_create_by_index(data)) == THING_NONE)
            single = true;
    }
    if (single)
        cell = dir_turn(cell, rand_bit());
    ProjectileLaunch launch = {.payload = first,
                               .x = (int16_t)(x + g_dir_dx[dir]),
                               .y = (int16_t)(y + g_dir_dy[dir]),
                               .cell = (int16_t)cell,
                               .dir = (int16_t)dir,
                               .energy = (uint8_t)sensor_launch_energy(sensor),
                               .hit_chance = LAUNCH_HIT_CHANCE,
                               .step = (uint8_t)sensor_launch_step(sensor),
                               .from_sensor = true};
    projectile_create(&launch);
    if (!single) {
        launch.payload = second;
        launch.cell = (int16_t)dir_turn(cell, TURN_RIGHT);
        projectile_create(&launch);
    }
}

void projectile_launch_from_champion(int16_t champion_index, uint16_t payload, int energy, int attack, int step)
{
    const Champion *c = champion(champion_index);
    int dir = c->dir;
    int cell = dir_side_cell(c->cell, (int16_t)dir);
    projectile_create(&(ProjectileLaunch){.payload = payload,
                                          .x = g_game.party_x,
                                          .y = g_game.party_y,
                                          .cell = (int16_t)cell,
                                          .dir = (int16_t)dir,
                                          .energy = (uint8_t)min_int((int16_t)energy, UINT8_MAX),
                                          .hit_chance = (uint8_t)min_int((int16_t)attack, UINT8_MAX),
                                          .step = (uint8_t)min_int((int16_t)step, UINT8_MAX)});
    g_game.throw_cooldown = THROW_COOLDOWN;
    g_game.throw_dir = (uint16_t)dir;
}
