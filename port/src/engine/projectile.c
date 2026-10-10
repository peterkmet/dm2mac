#include "projectile.h"
#include "ai.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "game.h"
#include "gdat.h"
#include "sound_request.h"
#include "item.h"
#include "le.h"
#include "money.h"
#include "move.h"
#include "random.h"
#include "save.h"
#include "sensor.h"
#include "weather.h"

uint16_t projectile_extra_damage, projectile_attack_type;
int missile_from_sensor;

/* Projectile event position word (event data bytes 2 and 3). */
#define POSITION_STEP_SHIFT 12
#define POSITION_STEP_MASK 0xf
#define POSITION_SQUARE_DIR 0x0fff  /* position word: x, y, and direction */
/* A coordinate above a byte: the low byte is the square, the high byte the stop square + 1. */
#define COORD_STOP_SHIFT 8
#define COORD_STOP_ONE 0x100

/* Explosion types (EXPLOSION_DATA bits 0-6). */
enum {
    BLAST_FIREBALL = 0, BLAST_LIGHTNING = 2, BLAST_HARM_NON_MATERIAL = 3, BLAST_POISON_CLOUD = 7,
    BLAST_MISSILE_WALL = 0x0e, BLAST_DEATH_CLOUD = 0x28, BLAST_FADING = 0x30, BLAST_SMOKE = 0x64
};
#define BLAST_TYPE_COUNT 8          /* the explosion types with an attack */
/* What an explosion type attacks: bit 0 random power, 1 doors, 2 the party, 3 creatures. */
enum { BLAST_RANDOM = 1, BLAST_DOORS = 2, BLAST_PARTY = 4, BLAST_CREATURES = 8 };
static const uint8_t explosion_attack_flags[BLAST_TYPE_COUNT] = {
    BLAST_RANDOM | BLAST_DOORS | BLAST_CREATURES, 0, BLAST_RANDOM | BLAST_DOORS | BLAST_CREATURES, BLAST_RANDOM | BLAST_CREATURES, 0, 0, 0,
    BLAST_PARTY | BLAST_CREATURES
};
#define POISON_CLOUD_DECAY 3        /* power that a poison cloud loses per step */
#define POISON_CLOUD_LAST 6         /* a poison cloud lasts while its power is at least this */
#define POISON_CLOUD_POWER_SHIFT 5  /* the attack of a poison cloud: its power / 32, at most 4, plus a random bit */
#define POISON_CLOUD_ATTACK_MAX 4
#define DEATH_CLOUD_DECAY 40
#define DEATH_CLOUD_LAST 0x37
#define SMOKE_DURATION 5

#define SOUND_LAUNCH 0x86           /* sound entries of a thing: launched, explodes */
#define SOUND_PRIORITY_LAUNCH 0x6e
#define SOUND_DELAY_HURT 2          /* a champion that a creature hurts screams after the next game step */

/* Sensor types that react to projectiles and explosions. */
#define SENSOR_PROJECTILE_HIT 0x22      /* the wall sensor of a cell triggers when an object hits it */
#define SENSOR_PROJECTILE_PORTAL 0x23   /* the object goes through the wall to the target square */
#define SENSOR_EXPLOSION 0x26           /* the floor sensor triggers on an explosion */
#define SENSOR_ANY_THING 0x1ff          /* sensor data: any object or explosion type */
#define SENSOR_STRONG_ONLY SENSOR_STATE  /* flags: explosions of power 0xff (0x7f for lightning) only; a hit stops the object */
#define SENSOR_EFFECT_ALWAYS EFFECT_HOLD  /* effect: trigger with the inverted match */
#define STRONG_POWER 255                /* the explosion power that a strong-only sensor needs */
#define STRONG_LIGHTNING_POWER 127
#define ORNAMENT_KEY_CATCH 0x13         /* ornament value: the alcove catches thrown objects */
#define DOOR_KEY_GRATE 0x10             /* door value: objects and weak spells fly through the door */
#define ITEM_GRATE_PASSES 0x0002        /* the object can fly through a grate */
#define ITEM_CAUGHT 0x0040              /* a creature that catches objects catches it */

/* Potions that explode on a hit. */
#define POTION_VEN_BOMB 3
#define POTION_FIRE_BOMB 0x13

/* Creature info bits that projectiles test. */
#define CREATURE_REFLECTS_SPELLS 0x0002     /* flags */
#define CREATURE_NON_MATERIAL CREATURE_NOT_DOOR_HIT
#define CREATURE_CATCHES_OBJECTS 0x0200     /* flags */
#define STATIC_SPELLS_ONLY 0x0100           /* possessions of a static creature: objects fly through it */
#define POSSESSIONS_DIRECTION 1             /* possessions bits 0-3: a dropped object lies in the flight direction cell */
#define DODGE_NONE 0x0f                     /* unknown_18 bits 4-7: explosion dodge; 0xf: explosions do not hurt */
#define ATTACK_KIND_ANY_DEXTERITY 9         /* unknown_1c: melee attack kinds */
#define ATTACK_KIND_ALWAYS_HITS 8
#define PUSH_BOUNCES 0xff                   /* push_resistance: objects bounce off the creature */
#define SPELL_PUSH_MIN 40                   /* the strength of a push or pull spell */
#define SPELL_PUSH_MAX 254
/* Random parts of the projectile attacks. */
#define ROLL_POISON 0x7f                    /* poison weakens when this roll is above the energy */
#define ROLL_GRATE 0x7f                     /* an object flies through a grate when its hit chance beats this roll */
#define ROLL_DOUBLE_POWER 0x1ff             /* a thrown object hits twice as hard when its hit chance beats this roll */
#define ROLL_SLIME 0x1f
#define ROLL_SLIME_POISON 0xf
#define SLIME_POISON_BASE 10
#define ROLL_FIRE 0xf                       /* a fire spell: two rolls plus a base */
#define FIRE_POWER_BASE 10
#define LIGHTNING_POWER_FACTOR 16
#define ROLL_POISON_CHAMPION 7              /* a hit with poison poisons the champion unless this roll is 0 */
#define THROW_CHANCE_SHIFT 4                /* a thrown object: factor hit chance / 16 + 3, power * factor^2 / 128 */
#define THROW_FACTOR_BASE 3
#define THROW_POWER_SHIFT 7
#define ATTACK_POWER_SHIFT 4                /* the attack of a hit: (power + energy) / 16 + 1 */
#define POISON_BOLT_ATTACK_SHIFT 3          /* a poison bolt: energy / 8 + 1 */
#define DEFENSE_SCALE_SHIFT 6               /* the damage to a creature: attack * 64 / defense */
#define HIT_CHANCE_BONUS_BASE 32            /* a high hit chance doubles the attack above this - hit chance / 8 */
#define HIT_CHANCE_BONUS_SHIFT 3
#define ROLL_MELEE 0x1f
#define ROLL_MELEE_POWER 0xf
#define ROLL_WOUND_LOCATION 0x70            /* all clear: the hit wounds the hands */
#define WOUND_LOCATION_LAST 3
#define MELEE_WEAK_POWER_BASE 2             /* a melee attack that armor stops still hits with 2 to 5 */
#define VISIBILITY_INVISIBLE 16             /* the hit bonus against an invisible party */
#define DEXTERITY_ALWAYS_HITS 0xff
#define DEXTERITY_DODGE_MARGIN 16
#define LUCK_DODGE 60

#define WOUND_PROJECTILE (WOUND_HEAD | WOUND_TORSO) /* the wounds of a projectile hit: head and torso */

/* Alert flags (creature_alert). */
#define ALERT_HIT 0x200d
#define ALERT_ATTACKED 2
#define ALERT_WEAK_DAMAGE 5                 /* a hit below this damage (not a poison cloud) alerts with the weak chance */
#define ALERT_CHANCE_WEAK 30
#define ALERT_CHANCE_STRONG 95


int projectile_event_dir(const TimelineEvent *e)
{
    return location_dir(le16(e->data + PROJECTILE_EVENT_POSITION));
}

int projectile_dir(uint16_t projectile)
{
    return projectile_event_dir(&timeline.events[thing_word(projectile, PROJECTILE_EVENT)]);
}

/* True for the party square on the current map. */
static int on_party_square(int x, int y)
{
    return party.cur_map == party.square_map && x == party.square_x && y == party.square_y;
}

static int16_t min16(int16_t a, int16_t b) { return a < b ? a : b; }
static int16_t max16(int16_t a, int16_t b) { return a > b ? a : b; }

void projectile_create(uint16_t payload, int x, int y, int cell, int dir, int energy, int attack, int step)
{
    uint16_t t = dungeon_unused_thing(THING_PROJECTILE);
    if (t == THING_NONE) {
        if (thing_type(payload) != THING_EXPLOSION) move_thing(thing_with_cell(payload, cell), -1, 0, x, y);
        return;
    }
    sound_request_with_fallback(item_gdat_category(payload), item_gdat_index(payload), SOUND_LAUNCH, SOUND_FALLBACK, (int16_t)x,
                             (int16_t)y, 1, SOUND_PRIORITY_LAUNCH, SOUND_VOLUME_NORMAL);
    t = thing_with_cell(t, cell);
    set_thing_word(t, PROJECTILE_PAYLOAD, payload);
    projectile_set_energy(t, (uint8_t)energy);
    projectile_set_hit_chance(t, (uint8_t)attack);
    dungeon_link_thing_to_square(t, party.cur_map, x, y);
    if (payload == SPELL_FIREBALL && party.cur_map == game.party_map) party.redraw |= REDRAW_VIEW;
    TimelineEvent e = {.time_map = timeline_time_map(game.game_time + 1, party.cur_map)};
    e.type = missile_from_sensor ? EVENT_PROJECTILE_MOVE_2 : EVENT_PROJECTILE_MOVE;
    put_le16(e.data + PROJECTILE_EVENT_THING, t);
    put_le16(e.data + PROJECTILE_EVENT_POSITION,
             (uint16_t)(event_square_dir(x, y, dir) | (step & POSITION_STEP_MASK) << POSITION_STEP_SHIFT));
    set_thing_word(t, PROJECTILE_EVENT, (uint16_t)timeline_add_event(&e));
}

void projectile_event_set_position(TimelineEvent *e, int x, int y, int dir)
{
    uint16_t w = le16(e->data + PROJECTILE_EVENT_POSITION);
    put_le16(e->data + PROJECTILE_EVENT_POSITION, (uint16_t)((w & ~POSITION_SQUARE_DIR) | event_square_dir(x, y, dir)));
}

/* An explosion of type with power on (x, y): the explosion sensors of the square react. */
static void explosion_trigger_sensors(int type, int x, int y, int power)
{
    for (uint16_t t = dungeon_square_first_thing(party.cur_map, x, y); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_SENSOR) continue;
        if (sensor_type(t) != SENSOR_EXPLOSION) continue;
        int match = sensor_data(t) == SENSOR_ANY_THING || sensor_data(t) == (uint16_t)(type - SPELL_THING_FIRST);
        match ^= sensor_flag(t, SENSOR_INVERTED);
        if (sensor_flag(t, SENSOR_STRONG_ONLY) && (int16_t)power < ((uint16_t)type == SPELL_LIGHTNING ? STRONG_LIGHTNING_POWER : STRONG_POWER))
            match = 0;
        if (sensor_effect(t) == SENSOR_EFFECT_ALWAYS) sensor_trigger_target(t, !match, 0);
        else if (match) sensor_trigger_target(t, sensor_effect(t), 0);
    }
}

/* The blast of a fireball or lightning hurts the party and the creature on (x, y), then a creature near it. */
static void explosion_blast(int type, int power, int x, int y, int cell)
{
    uint16_t full = (uint16_t)(((uint16_t)power >> 1) + 1);
    full = (uint16_t)(full + rand_below(full) + 1);
    uint16_t damage = full;
    if (type != SPELL_FIREBALL && type != EXPLOSION_STORM_LIGHTNING) {
        damage >>= 1;
        if (!damage) return;
    }
    if (on_party_square(x, y)) champ_damage_party(damage, WOUND_ALL, ATTACK_TYPE_FIRE);
    int16_t nx = (int16_t)x, ny = (int16_t)y, hx = (int16_t)x, hy = (int16_t)y;
    uint16_t g = creature_get_group_at(x, y);
    for (int first = 1;; first = 0) {
        if (g != THING_NONE && !creature_current_step_acts(g)) {
            const CreatureInfo *info = creature_group_info(g);
            if (info->door_flags & CREATURE_RESISTS_NON_FIRE && type != SPELL_FIREBALL)
                damage = (uint16_t)max16(1, (int16_t)(damage >> 2));
            int dodge = info->unknown_18 >> NIBBLE_BITS;
            if (dodge != DODGE_NONE) {
                if (info->flags & CREATURE_NON_MATERIAL) damage >>= 2;
                damage = (uint16_t)(damage - rand_below((uint16_t)(dodge * 2 + 1)));
                if ((int16_t)damage > 0) creature_alert(g, hx, hy, ALERT_HIT, ALERT_ALWAYS, damage);
            }
        }
        if (!first) return;
        g = creature_find_group_near_point(&nx, &ny, (uint16_t)cell);
        if (g == THING_NONE || (nx == x && ny == y)) return;
        hx = nx;
        hy = ny;
        damage = (uint16_t)((int16_t)full >> 1);
    }
}

void explosion_create(int type, int power, int x, int y, int cell)
{
    uint16_t t = dungeon_unused_thing(THING_EXPLOSION);
    if (t == THING_NONE) return;
    int16_t ex = (int16_t)x, ey = (int16_t)y;
    if ((uint16_t)x > UINT8_MAX) {
        x = (uint8_t)x;
        y = (uint8_t)y;
        ex = (int16_t)(((uint16_t)ex >> COORD_STOP_SHIFT) - 1);
        ey = (int16_t)((uint16_t)ey >> COORD_STOP_SHIFT);
    }
    int centered = (uint16_t)cell == EXPLOSION_AT_CENTER;
    if (!centered) t = thing_with_cell(t, cell);
    explosion_set_type(t, type - SPELL_THING_FIRST);
    explosion_set_centered(t, centered);
    explosion_set_power(t, (unsigned)power);
    int volume =
        on_party_square(ex, ey) ? SOUND_VOLUME_MAX : min16(SOUND_VOLUME_MAX, (int16_t)(((uint16_t)power >> 1) + SOUND_VOLUME_NORMAL));
    sound_request_with_fallback(CAT_EXPLOSION, explosion_type(t), SOUND_EXPLODE, SOUND_FALLBACK, ex, ey, 1, SOUND_PRIORITY_EXPLODE,
                             volume);
    dungeon_link_thing_to_square(t, party.cur_map, ex, ey);
    int16_t delay = (uint16_t)type == EXPLOSION_SMOKE ? SMOKE_DURATION
                    : (uint16_t)type == SPELL_MISSILE_WALL ? (int16_t)((uint16_t)power >> 1) : 1;
    TimelineEvent e = {.time_map = timeline_time_map(game.game_time + delay, party.cur_map), .type = EVENT_EXPLOSION};
    e.data[EXPLOSION_EVENT_X] = (uint8_t)ex;
    e.data[EXPLOSION_EVENT_Y] = (uint8_t)ey;
    put_le16(e.data + EXPLOSION_EVENT_THING, t);
    timeline_add_event(&e);
    uint16_t power_thing = t;
    switch ((uint16_t)type) {
    case SPELL_LIGHTNING: case SPELL_FIREBALL: case EXPLOSION_STORM_LIGHTNING: case EXPLOSION_STORM_BLAST:
        explosion_blast((uint16_t)type, power, x, y, cell);
        if (party.cur_map == game.party_map) party.redraw |= REDRAW_VIEW;
        break;
    case SPELL_DOOR_TOGGLE: case SPELL_DOOR_SET: {
        uint8_t square = dun_get_square(ex, ey);
        if (square_type(square) != SQUARE_DOOR || (square & SQUARE_DOOR_STATE) == DOOR_STATE_BROKEN) break;
        uint16_t door = dungeon_square_first_thing(party.cur_map, ex, ey);
        power_thing = door;
        if (door_has_button(door))
            sensor_schedule_square_event(ex, ey, 0, (uint16_t)type == SPELL_DOOR_TOGGLE ? EFFECT_TOGGLE : EFFECT_SET,
                                         game.game_time + 1);
        break;
    }
    }
    /* kept quirk: after a door spell the original reads the power from the high byte of the door record word */
    explosion_trigger_sensors((uint16_t)type, x, y, explosion_power(power_thing));
}

void projectile_cancel_event(uint16_t projectile)
{
    if (thing_type(projectile) == THING_PROJECTILE) timeline_delete_event(thing_word(projectile, PROJECTILE_EVENT));
}

void projectile_destroy(uint16_t projectile, uint16_t *holder, int x, int y)
{
    uint16_t payload = thing_word(projectile, PROJECTILE_PAYLOAD);
    if (thing_type(payload) != THING_EXPLOSION) {
        if (holder) {
            dungeon_append_thing(payload, holder);
        } else {
            uint16_t g = creature_get_group_at(x, y);
            if (g != THING_NONE && creature_group_info(g)->flags & CREATURE_STATIC) {
                int cell = thing_cell(projectile);
                if ((creature_group_info(g)->possessions & POSSESSIONS_KIND_MASK) == POSSESSIONS_DIRECTION)
                    cell = projectile_dir(projectile);
                move_thing(thing_with_cell(payload, cell), -2, -1, (int16_t)x, (int16_t)y);
            } else {
                move_thing(thing_with_cell(payload, thing_cell(projectile)), -2, 0, (int16_t)x, (int16_t)y);
            }
        }
        sound_request_with_fallback(item_gdat_category(projectile), item_gdat_index(projectile), SOUND_ENTRY_DROP, SOUND_FALLBACK,
                                 (int16_t)x, (int16_t)y, 1, SOUND_PRIORITY_DROP, SOUND_VOLUME_NORMAL);
    }
    set_thing_word(projectile, THING_NEXT, THING_NONE);
}

int projectile_get_attack(uint16_t projectile, uint16_t payload)
{
    uint16_t energy = projectile_energy(projectile);
    uint16_t power;
    projectile_extra_damage = 0;
    projectile_attack_type = ATTACK_TYPE_BLUNT;
    if (thing_type(payload) != THING_EXPLOSION) {
        uint16_t throw_power = item_attr(payload, ATTR_THROW_POWER);
        power = 0;
        if (throw_power) {
            int factor = (uint16_t)((projectile_hit_chance(projectile) >> THROW_CHANCE_SHIFT) + THROW_FACTOR_BASE);
            power = (uint16_t)((uint16_t)(throw_power + (energy >> 1)) * (factor * factor) >> THROW_POWER_SHIFT);
            projectile_attack_type = ATTACK_TYPE_SHARP;
            projectile_extra_damage = item_attr(payload, ATTR_EXTRA_DAMAGE);
            if (projectile_extra_damage && (rand_next() & ROLL_POISON) > energy)
                projectile_extra_damage = (uint16_t)(projectile_extra_damage - rand_below((uint16_t)((projectile_extra_damage >> 1) + 1)));
        }
        power = (uint16_t)(power + rand_2bits());
        power = (uint16_t)(power + item_weight(payload));
        if (projectile_hit_chance(projectile) > (rand_next() & ROLL_DOUBLE_POWER)) power = (uint16_t)(power * 2);
    } else if (payload == SPELL_SLIME) {
        uint16_t r = rand_next() & ROLL_SLIME_POISON;
        projectile_extra_damage = (uint16_t)(r + SLIME_POISON_BASE);
        power = (uint16_t)(r + (rand_next() & ROLL_SLIME));
    } else if (payload >= SPELL_HARM_NON_MATERIAL) {
        projectile_attack_type = ATTACK_TYPE_MAGIC;
        if (payload != SPELL_POISON_BOLT) return 0;
        projectile_extra_damage = energy;
        return (energy >> POISON_BOLT_ATTACK_SHIFT) + 1;
    } else {
        projectile_attack_type = ATTACK_TYPE_FIRE;
        power = rand_next() & ROLL_FIRE;
        power = (uint16_t)(power + (rand_next() & ROLL_FIRE) + FIRE_POWER_BASE);
        if (payload == SPELL_LIGHTNING) {
            projectile_attack_type = ATTACK_TYPE_LIGHTNING;
            power = (uint16_t)(power * LIGHTNING_POWER_FACTOR + energy);
        }
    }
    uint16_t attack = (uint16_t)(((power + energy) >> ATTACK_POWER_SHIFT) + 1);
    uint16_t r = rand_below((uint16_t)((attack >> 1) + 1));
    attack = (uint16_t)(attack + rand_2bits() + r);
    int bonus = HIT_CHANCE_BONUS_BASE - (projectile_hit_chance(projectile) >> HIT_CHANCE_BONUS_SHIFT);
    int16_t a = max16((int16_t)attack, (int16_t)((attack - bonus) * 2));
    return min16(a, (int16_t)(energy * 2));
}

/* The projectile from (x, y) hits the wall at (wall_x, wall_y) while it flies toward dir: the hit sensors of the
   wall cell react, an alcove can catch the object, a portal sensor sends it on. Returns 1 when the object stops. */
static int projectile_hit_wall_sensors(int wall_x, int wall_y, int x, int y, int dir, uint16_t projectile)
{
    int done = 0, side = dir_opposite(dir);
    uint16_t first = dungeon_square_first_thing(party.cur_map, wall_x, wall_y);
    uint16_t payload = thing_word(projectile, PROJECTILE_PAYLOAD);
    int object = thing_type(payload) != THING_EXPLOSION;
    for (uint16_t t = first; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        int cell = thing_cell(t);
        if (cell != side) continue;
        int to_x = wall_x, to_y = wall_y;
        if (object && !done && sensor_wall_ornament_value(first, t, ORNAMENT_KEY_CATCH) && !rand_2bits()) {
            dungeon_unlink_thing_from_square(projectile, party.cur_map, x, y);
            projectile = thing_with_cell(projectile, cell);
        } else if (thing_type(t) == THING_SENSOR && object) {
            uint16_t data = sensor_data(t);
            int kind = sensor_type(t);
            if (kind == SENSOR_PROJECTILE_HIT) {
                int match = data == SENSOR_ANY_THING || data == item_get_index(payload);
                match ^= sensor_flag(t, SENSOR_INVERTED);
                if (!match) continue;
                sensor_trigger_target(t, sensor_effect(t) == SENSOR_EFFECT_ALWAYS ? 0 : sensor_effect(t), 0);
                if (done || !sensor_flag(t, SENSOR_STRONG_ONLY)) continue;
                dungeon_unlink_thing_from_square(projectile, party.cur_map, x, y);
                projectile = thing_with_cell(projectile, side);
            } else if (kind == SENSOR_PROJECTILE_PORTAL && !done && (data == SENSOR_ANY_THING || data == item_get_index(payload))) {
                dungeon_unlink_thing_from_square(projectile, party.cur_map, x, y);
                to_x = sensor_target_x(t);
                to_y = sensor_target_y(t);
                int to_cell = square_type(dun_get_square(to_x, to_y)) == SQUARE_WALL ? sensor_target_cell(t) : sensor_effect(t);
                projectile = thing_with_cell(projectile, to_cell);
            } else {
                continue;
            }
        } else {
            continue;
        }
        projectile_destroy(projectile, NULL, to_x, to_y);
        done = 1;
    }
    return done;
}

/* The state of a projectile hit. */
typedef struct {
    uint16_t projectile, payload;
    int payload_type;
    int16_t stop_x, stop_y;     /* where the object falls */
    int16_t x, y;               /* the square of the hit */
    uint16_t *holder;           /* the possessions of a creature that catches the object */
    int explosive, potion_bomb, hit;
    uint16_t damage;
} Hit;

/* The projectile meets the creature near the cell of its hit square; returns 1 when it hits. */
static int projectile_hit_creature(Hit *h, int cell)
{
    int16_t x = h->x, y = h->y;
    uint16_t g = creature_find_group_near_point(&x, &y, (uint16_t)cell);
    if (g == THING_NONE || creature_current_step_acts(g)) return 0;
    const CreatureInfo *info = creature_group_info(g);
    uint16_t payload = h->payload;
    if (info->flags & CREATURE_NON_MATERIAL && payload != SPELL_HARM_NON_MATERIAL) return 0;
    int bounce;
    if (!(info->flags & CREATURE_STATIC)) {
        bounce = info->push_resistance == PUSH_BOUNCES;
    } else {
        if (info->possessions & STATIC_SPELLS_ONLY && payload < SPELL_THING_FIRST) return 0;
        if (creature_door_size(info) > 1) return 0;
        bounce = !(info->possessions & CREATURE_POSSESSIONS_ON_FLOOR);
    }
    if (bounce) {  /* the object falls back on the square that it came from */
        int back = dir_opposite(projectile_dir(h->projectile));
        h->stop_x = (int16_t)(h->stop_x + dir_dx[back]);
        h->stop_y = (int16_t)(h->stop_y + dir_dy[back]);
        int c = thing_cell(h->projectile);
        h->projectile = thing_with_cell(h->projectile, (back & 1) == (c & 1) ? c - 1 : c + 1);
    }
    int attack = projectile_get_attack(h->projectile, payload);
    h->damage = (uint16_t)((uint16_t)(attack << DEFENSE_SCALE_SHIFT) / info->defense);
    if (h->damage) {
        uint16_t damage = (uint16_t)(creature_roll_armor_factor(g, projectile_extra_damage) + h->damage);
        if (info->door_flags & CREATURE_RESISTS_NON_FIRE && projectile_attack_type != ATTACK_TYPE_FIRE)
            damage = (uint16_t)((int16_t)damage >> 2);
        creature_alert(g, x, y, ALERT_HIT, ALERT_ALWAYS, damage);
        if (!h->explosive && info->flags & CREATURE_CATCHES_OBJECTS && item_attr(payload, ATTR_FLAGS) & ITEM_CAUGHT)
            h->holder = group_possessions(g);
    }
    if (h->potion_bomb || h->payload_type == THING_WEAPON
        || (h->payload_type == THING_EXPLOSION && payload != SPELL_DOOR_TOGGLE && (payload < SPELL_PUSH || payload > SPELL_MISSILE_WALL)))
        creature_alert(g, x, y, ALERT_ATTACKED,
                       h->damage < ALERT_WEAK_DAMAGE && payload != SPELL_POISON_CLOUD ? ALERT_CHANCE_WEAK : ALERT_CHANCE_STRONG, 0);
    return 1;
}

/* The projectile hits the champion in cell; returns 0 when no champion stands there. */
static int projectile_hit_champion(Hit *h, int cell)
{
    int champ = champ_get_index_in_cell((uint16_t)cell);
    if (champ < 0) return 0;
    h->hit = 1;
    uint16_t attack = (uint16_t)projectile_get_attack(h->projectile, h->payload);
    if (!attack) return 1;
    if (h->payload == SPELL_POISON_BOLT) projectile_extra_damage = (uint16_t)(projectile_extra_damage * 2);
    /* the original compares without a wrap: only flights to the north and east can face a champion */
    if (champion(champ)->dir == projectile_dir(h->projectile) + 2) projectile_attack_type |= ATTACK_BLOCKABLE;  /* the champion faces it */
    if ((int16_t)champ_take_damage(champ, (int16_t)attack, WOUND_PROJECTILE, projectile_attack_type) && projectile_extra_damage
        && rand_next() & ROLL_POISON_CHAMPION)
        champ_poison(champ, projectile_extra_damage);
    return 1;
}

/* The projectile hits the door at (x, y). Returns 1 when it stops, 0 when it flies through. */
static int projectile_hit_door(Hit *h)
{
    uint8_t square = dungeon_square(party.cur_map, h->x, h->y);
    int state = square & SQUARE_DOOR_STATE;
    uint16_t door = dungeon_square_first_thing(party.cur_map, h->x, h->y);
    if (state != DOOR_STATE_BROKEN && (h->payload == SPELL_DOOR_TOGGLE || h->payload == SPELL_DOOR_SET)) return 1;
    if (state == DOOR_STATE_BROKEN || state <= DOOR_STATE_ONE_FOURTH) return 0;
    if (sensor_door_value(door, DOOR_KEY_GRATE)) {
        int passes;
        if (h->payload_type == THING_EXPLOSION) {
            passes = h->payload >= SPELL_HARM_NON_MATERIAL;
        } else {
            passes = projectile_hit_chance(h->projectile) > (rand_next() & ROLL_GRATE)
                     && item_attr(h->payload, ATTR_FLAGS) & ITEM_GRATE_PASSES;
        }
        if (passes) return 0;
    }
    uint16_t attack = (uint16_t)(projectile_get_attack(h->projectile, h->payload) + 1);
    sensor_door_try_destroy(h->x, h->y, (int16_t)(rand_below(attack) + attack), 0, 0);
    return 1;
}

/* Starts a hit of projectile from (x, y): its payload (a bomb potion becomes its blast) and the square where its
   object stops. Returns the potion of a bomb, 0 for none. */
static uint16_t hit_start(Hit *h, uint16_t projectile, int *x, int *y, uint16_t *blast_type, int *blast_power)
{
    uint16_t potion = 0;
    h->projectile = projectile;
    h->payload = thing_word(projectile, PROJECTILE_PAYLOAD);
    h->payload_type = thing_type(h->payload);
    if (h->payload_type == THING_POTION) {
        int kind = potion_type(h->payload);
        if (kind == POTION_VEN_BOMB || kind == POTION_FIRE_BOMB) {
            *blast_type = kind == POTION_VEN_BOMB ? SPELL_POISON_CLOUD : SPELL_FIREBALL;
            h->potion_bomb = 1;
            *blast_power = potion_power(h->payload);
            potion = h->payload;
        }
    }
    h->explosive = h->payload_type == THING_EXPLOSION && h->payload != SPELL_SLIME && h->payload != SPELL_POISON_BOLT;
    if ((int16_t)*x > UINT8_MAX) {
        h->stop_x = (int16_t)(((int16_t)*x >> COORD_STOP_SHIFT) - 1);
        h->stop_y = (int8_t)(*y >> COORD_STOP_SHIFT);
        *x = (uint8_t)*x;
        *y = (uint8_t)*y;
    } else {
        h->stop_x = (int16_t)*x;
        h->stop_y = (int16_t)*y;
    }
    h->x = (int16_t)*x;
    h->y = (int16_t)*y;
    return potion;
}

/* A projectile with an explosive payload explodes at (x, y) in cell; returns the strength of a push or pull spell. */
static uint16_t hit_explode(Hit *h, uint16_t potion, uint16_t blast_type, int blast_power, int16_t x, int16_t y, int cell,
                            int *push_dir)
{
    uint16_t power, push_strength = 0;
    if (h->potion_bomb) {
        set_thing_word(potion, THING_NEXT, THING_NONE);
        set_thing_word(h->projectile, PROJECTILE_PAYLOAD, blast_type);
        h->payload = blast_type;
        power = (uint16_t)blast_power;
    } else {
        power = projectile_energy(h->projectile);
        if (h->payload == SPELL_POISON_BOLT && !(power >>= 2)) return 0;
        if (h->payload == SPELL_LIGHTNING && !(power >>= 1)) return 0;
    }
    if (h->payload == SPELL_PUSH || h->payload == SPELL_PULL) {
        *push_dir = projectile_dir(h->projectile);
        push_strength = projectile_energy(h->projectile);
    }
    int at = h->payload == SPELL_POISON_CLOUD || h->payload == SPELL_DOOR_TOGGLE || h->payload == SPELL_DOOR_SET
             ? EXPLOSION_AT_CENTER : (uint16_t)cell;
    explosion_create(h->payload, power, (uint16_t)x, (uint16_t)y, at);
    return push_strength;
}

int projectile_hit(int impact, int x, int y, int cell, uint16_t projectile)
{
    Hit h = {0};
    uint16_t blast_type = 0;
    int blast_power = 0;
    uint16_t potion = hit_start(&h, projectile, &x, &y, &blast_type, &blast_power);
    int16_t fall_x = h.stop_x, fall_y = h.stop_y;  /* where the explosion and the object removal happen */
    int16_t kind = (int16_t)impact;
    if (kind == 0) {
        int dir = projectile_dir(projectile);
        if (projectile_hit_wall_sensors(fall_x + dir_dx[dir], fall_y + dir_dy[dir], fall_x, fall_y, dir, projectile)) return 1;
    } else if (kind == SQUARE_DOOR) {
        if (!projectile_hit_door(&h)) return 0;
    } else if (kind == PROJECTILE_HIT_CREATURE || kind == PROJECTILE_HIT_CHAMPION || kind == PROJECTILE_HIT_PARTY_SQUARE) {
        int champion_hit = kind != PROJECTILE_HIT_CREATURE && projectile_hit_champion(&h, cell);
        if (kind != PROJECTILE_HIT_CHAMPION) {
            if (projectile_hit_creature(&h, cell)) h.hit = 1;
            else if (kind != PROJECTILE_HIT_PARTY_SQUARE) return 0;
        } else if (!champion_hit) {
            return 0;
        }
    }
    if (kind == PROJECTILE_HIT_PARTY_SQUARE && !h.hit) return 0;
    uint16_t push_strength = 0;
    int push_dir = 0;
    if (h.explosive || h.potion_bomb || h.payload == SPELL_POISON_BOLT)
        push_strength = hit_explode(&h, potion, blast_type, blast_power, fall_x, fall_y, cell, &push_dir);
    dungeon_unlink_thing_from_square(h.projectile, party.cur_map, fall_x, fall_y);
    projectile_destroy(h.projectile, h.holder, h.stop_x, h.stop_y);
    if (push_strength) {
        int16_t strength = max16(min16((int16_t)push_strength, SPELL_PUSH_MAX), SPELL_PUSH_MIN);
        int dir = h.payload == SPELL_PUSH ? push_dir : dir_opposite(push_dir);
        creature_push_group((int16_t)x, (int16_t)y, (int16_t)dir, strength);
    }
    return 1;
}

/* Which way a spell turns on a creature that reflects spells: by the creature facing (even or odd), the cell of the
   spell, and its flight direction; 4: the spell flies on. */
static const int8_t spell_reflection[2][4][4] = {
    {{3, 2, 1, 0}, {4, 4, 4, 4}, {3, 2, 1, 0}, {4, 4, 4, 4}},
    {{4, 4, 4, 4}, {1, 0, 3, 2}, {4, 4, 4, 4}, {1, 0, 3, 2}},
};

/* Sets the flight direction in the position word of event. */
static void set_event_dir(TimelineEvent *e, int dir)
{
    uint16_t w = le16(e->data + PROJECTILE_EVENT_POSITION);
    put_le16(e->data + PROJECTILE_EVENT_POSITION,
             (uint16_t)((w & ~POSITION_SQUARE_DIR) | event_square_dir(location_x(w), location_y(w), dir)));
}

/* The creature on (x, y) may reflect a spell of the projectile; returns 1 when a creature stands there and
   reflects spells (then the projectile hits nothing on this square). */
static int projectile_reflect(TimelineEvent *e, uint16_t projectile, int x, int y)
{
    uint16_t g = creature_get_group_at(x, y);
    if (g == THING_NONE || !(creature_group_info(g)->flags & CREATURE_REFLECTS_SPELLS)
        || thing_word(projectile, PROJECTILE_PAYLOAD) < SPELL_THING_FIRST)
        return 0;
    int facing = group_dir(g) & 1;
    int dir = spell_reflection[facing][(int16_t)thing_cell(projectile) & DIR_MASK][projectile_event_dir(e)];
    if (dir < DIR_COUNT) {
        set_event_dir(e, dir);
        /* the original also writes the direction into the freed event of this move */
        set_event_dir(&timeline.events[thing_word(projectile, PROJECTILE_EVENT)], dir);
    }
    return 1;
}

/* A creature on the squares ahead of a projectile that flew on notices it. */
static void projectile_alert_ahead(void)
{
    int saved = party.cur_map;
    if (party.dest_map != party.cur_map) dun_set_current_map(party.dest_map);
    for (int i = 0; i < 2; i++) {
        party.dest_x = (int16_t)(party.dest_x + dir_dx[party.dest_dir]);
        party.dest_y = (int16_t)(party.dest_y + dir_dy[party.dest_dir]);
        uint16_t g = creature_get_group_at(party.dest_x, party.dest_y);
        if (g != THING_NONE) creature_alert(g, party.dest_x, party.dest_y, ALERT_STARTLED, ALERT_ALWAYS, 0);
        if (i) break;
        uint8_t square = dun_get_square(party.dest_x, party.dest_y);
        int type = square_type(square), state = square & SQUARE_DOOR_STATE;
        if (type == SQUARE_WALL || (type == SQUARE_FAKE_WALL && !(square & SQUARE_FAKE_WALL_OPEN))
            || (type == SQUARE_DOOR && state != DOOR_STATE_BROKEN && state > DOOR_STATE_ONE_FOURTH))
            break;
    }
    if (party.dest_map != saved) dun_set_current_map(saved);
}

/* The projectile on (*x, *y) flies into the next square toward dir: it hits a wall or stairs there (returns 1), or
   a missile wall there turns it back (*leaves 0, the square stays). */
static int projectile_enter_square(TimelineEvent *e, uint16_t projectile, int16_t *x, int16_t *y, int dir, int cell,
                                   int *leaves)
{
    int16_t from_x = *x, from_y = *y;
    *x = (int16_t)(*x + dir_dx[dir]);
    *y = (int16_t)(*y + dir_dy[dir]);
    uint8_t square = dun_get_square(*x, *y);
    int type = square_type(square);
    if ((type == SQUARE_WALL || (type == SQUARE_FAKE_WALL && !(square & (SQUARE_FAKE_WALL_OPEN | SQUARE_FLAG_0)))
         || (type == SQUARE_STAIRS && square_type(dungeon_square(party.cur_map, from_x, from_y)) == SQUARE_STAIRS))
        && projectile_hit(type, from_x, from_y, cell, projectile))
        return 1;
    for (uint16_t t = dungeon_square_first_object(party.cur_map, *x, *y); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_EXPLOSION || explosion_type(t) != BLAST_MISSILE_WALL)
            continue;
        TimelineEvent *queued = &timeline.events[thing_word(projectile, PROJECTILE_EVENT)];
        int back = dir_opposite(projectile_event_dir(queued));
        set_event_dir(e, back);
        set_event_dir(queued, back);
        *x = from_x;
        *y = from_y;
        *leaves = 0;
        break;
    }
    return 0;
}

/* The move event of a projectile: it hits what is on its square, then flies on to the next cell or square. */
void projectile_process_event(TimelineEvent *event)
{
    TimelineEvent e = *event;
    uint16_t projectile = le16(e.data + PROJECTILE_EVENT_THING);
    uint16_t position = le16(e.data + PROJECTILE_EVENT_POSITION);
    int16_t x = location_x(position), y = (int16_t)location_y(position);
    if (e.type == EVENT_PROJECTILE_MOVE) {
        e.type = EVENT_PROJECTILE_MOVE_2;  /* the first move: no hit on the launch square */
    } else {
        int cell = thing_cell(projectile);
        if (!projectile_reflect(&e, projectile, x, y)) {
            if (on_party_square(x, y) && projectile_hit(PROJECTILE_HIT_PARTY_SQUARE, x, y, cell, projectile)) return;
            if (projectile_hit(PROJECTILE_HIT_CREATURE, x, y, cell, projectile)) return;
        }
        int step = le16(e.data + PROJECTILE_EVENT_POSITION) >> POSITION_STEP_SHIFT;
        if (projectile_energy(projectile) <= step) {
            dungeon_unlink_thing_from_square(projectile, party.cur_map, x, y);
            projectile_destroy(projectile, NULL, x, y);
            return;
        }
        projectile_set_energy(projectile, (uint8_t)(projectile_energy(projectile) - step));
        uint8_t chance = projectile_hit_chance(projectile);
        projectile_set_hit_chance(projectile, chance < step ? 0 : (uint8_t)(chance - step));
    }
    projectile = le16(e.data + PROJECTILE_EVENT_THING);
    int cell = thing_cell(projectile);
    int dir = projectile_event_dir(&e);
    int leaves = dir == cell || dir_turn(dir, TURN_RIGHT) == cell;  /* the projectile is in the front cells: it leaves the square */
    int16_t from_x = x, from_y = y;
    if (leaves && projectile_enter_square(&e, projectile, &x, &y, dir, cell, &leaves)) return;
    int new_cell = dir_turn(cell, (dir & 1) == (cell & 1) ? -1 : 1);
    if (leaves) {
        move_thing(thing_with_cell(projectile, new_cell), from_x, from_y, x, y);
        if (thing_word(projectile, PROJECTILE_PAYLOAD) == SPELL_FIREBALL && party.cur_map == game.party_map)
            party.redraw |= REDRAW_VIEW;
        projectile_event_set_position(&e, party.dest_x, party.dest_y, party.dest_dir);
        projectile = thing_with_cell(projectile, party.dest_cell);
        timeline_set_event_map(&e, party.dest_map);
        projectile_alert_ahead();
    } else {
        if (square_type(dun_get_square(x, y)) == SQUARE_DOOR && projectile_hit(SQUARE_DOOR, x, y, cell, projectile)) return;
        dungeon_unlink_thing_from_square(projectile, party.cur_map, x, y);
        projectile = thing_with_cell(projectile, new_cell);
        dungeon_link_thing_to_square(projectile, party.cur_map, x, y);
    }
    e.time_map++;
    put_le16(e.data + PROJECTILE_EVENT_THING, projectile);
    set_thing_word(projectile, PROJECTILE_EVENT, (uint16_t)timeline_add_event(&e));
}

int explosion_get_attack(uint16_t explosion, uint16_t target)
{
    int type = explosion_type(explosion);
    if (type >= BLAST_TYPE_COUNT) return 0;
    uint8_t flags = explosion_attack_flags[type];
    if (!flags) return 0;
    int kind;
    if (target == THING_NONE) {
        if (!(flags & BLAST_PARTY)) return 0;
        kind = -1;
    } else {
        kind = thing_type(target);
        if (!(kind == THING_DOOR && flags & BLAST_DOORS) && !(kind == THING_GROUP && flags & BLAST_CREATURES)) return 0;
    }
    uint16_t attack = explosion_power(explosion);
    if (kind == THING_GROUP && creature_group_info(target)->door_flags & CREATURE_RESISTS_NON_FIRE && type != BLAST_FIREBALL)
        attack = (uint16_t)((int16_t)attack >> 2);
    if (flags & BLAST_RANDOM) {
        attack = (uint16_t)((explosion_power(explosion) >> 1) + 1);
        attack = (uint16_t)(attack + rand_below(attack) + 1);
    }
    switch (type) {
    case BLAST_LIGHTNING:
        attack = (uint16_t)((int16_t)attack >> 1);
        /* fall through */
    case BLAST_FIREBALL:
        if (kind == THING_DOOR && !door_flag(target, DOOR_BREAKABLE_BY_MAGIC)) return 0;
        return attack;
    case BLAST_HARM_NON_MATERIAL:
        return creature_group_info(target)->flags & CREATURE_NON_MATERIAL ? attack : 0;
    case BLAST_POISON_CLOUD:
        attack = (uint16_t)max16(1, (int16_t)(min16((int16_t)(explosion_power(explosion) >> POISON_CLOUD_POWER_SHIFT),
                                                    POISON_CLOUD_ATTACK_MAX) + rand_bit()));
        if (kind == THING_GROUP) attack = (uint16_t)creature_roll_armor_factor(target, attack);
        return attack;
    default:
        return attack;
    }
}

/* The event of an explosion: it hurts doors, the party, and creatures on its square, then a cloud fades or the explosion ends. */
void explosion_process_event(TimelineEvent *e)
{
    int x = e->data[EXPLOSION_EVENT_X], y = e->data[EXPLOSION_EVENT_Y];
    uint16_t t = le16(e->data + EXPLOSION_EVENT_THING);
    if (square_type(dun_get_square(x, y)) == SQUARE_DOOR) {
        int16_t attack = (int16_t)explosion_get_attack(t, dungeon_square_first_thing(party.cur_map, x, y));
        if (attack) sensor_door_try_destroy(x, y, attack, 1, 0);
    }
    int type = explosion_type(t);
    if (type != BLAST_MISSILE_WALL && type != BLAST_FIREBALL && type != BLAST_LIGHTNING) {
        uint16_t attack;
        if (on_party_square(x, y) && (attack = (uint16_t)explosion_get_attack(t, THING_NONE)))
            champ_damage_party(attack, 0, 0);
        uint16_t g = creature_get_group_at(x, y);
        if (g != THING_NONE && !creature_current_step_acts(g) && (attack = (uint16_t)explosion_get_attack(t, g)))
            creature_alert(g, x, y, ALERT_HIT, ALERT_ALWAYS, attack);
        int lasts = 1, power = explosion_power(t);
        switch (explosion_type(t)) {
        case BLAST_POISON_CLOUD:
            if (power < POISON_CLOUD_LAST) lasts = 0;
            else explosion_set_power(t, (unsigned)(power - POISON_CLOUD_DECAY));
            break;
        case BLAST_DEATH_CLOUD:
            if (power <= DEATH_CLOUD_LAST) lasts = 0;
            else explosion_set_power(t, (unsigned)(power - DEATH_CLOUD_DECAY));
            break;
        case BLAST_SMOKE:
            sound_request_with_fallback(CAT_EXPLOSION, explosion_type(t), SOUND_EXPLODE, SOUND_FALLBACK, x, y, 1, SOUND_PRIORITY_EXPLODE,
                                     SOUND_VOLUME_LOUD);
            explosion_set_type(t, explosion_type(t) + 1);
            break;
        default:
            lasts = 0;
        }
        if (lasts) {
            TimelineEvent next = *e;
            next.time_map++;
            timeline_add_event(&next);
            return;
        }
    }
    type = explosion_type(t);
    if (party.cur_map == game.party_map && (type == BLAST_LIGHTNING || type == BLAST_FIREBALL || type == BLAST_FADING))
        party.redraw |= REDRAW_VIEW;
    dungeon_unlink_thing_from_square(t, party.cur_map, x, y);
    set_thing_word(t, THING_NEXT, THING_NONE);
}

/* The order in which a creature attacks the 4 cells of the party, by its direction and side (8 rows), and the
   cells from the direction on, before a random swap of the pairs. */
static const uint8_t attack_cell_orders[8][4] = {
    {0, 1, 3, 2}, {1, 0, 2, 3}, {1, 2, 0, 3}, {2, 1, 3, 0}, {3, 2, 0, 1}, {2, 3, 1, 0}, {0, 3, 1, 2}, {3, 0, 2, 1}
};
static const uint8_t attack_cell_orders_random[7] = {0, 1, 2, 3, 0, 1, 2};

void creature_get_ordered_attack_cells(uint8_t order[4], int x, int y, int party_x, int party_y, int cell)
{
    int dir = dun_get_direction_to((int16_t)x, (int16_t)y, (int16_t)party_x, (int16_t)party_y);
    if ((uint16_t)cell == CELL_ANY) {
        for (int i = 0; i < DIR_COUNT; i++) order[i] = attack_cell_orders_random[dir + i];
        uint8_t swap;
        if (rand_bit()) {
            swap = order[0];
            order[0] = order[1];
            order[1] = swap;
        }
        if (rand_bit()) {
            swap = order[2];
            order[2] = order[3];
            order[3] = swap;
        }
        return;
    }
    uint16_t side = (uint16_t)cell;
    if (!(dir & 1)) side++;
    for (int i = 0; i < DIR_COUNT; i++) order[i] = attack_cell_orders[dir * 2 + (side >> 1 & 1)][i];
}

/* The wounds of a melee hit, by the hit location chosen from the creature's wound nibbles. */
static const uint8_t melee_wounds[4] = {WOUND_FEET, WOUND_LEGS, WOUND_TORSO, WOUND_HEAD};

/* The chance nibble of a wound location: 4 bits per location in unknown_1a (locations 0, 1) and unknown_1b (2, 3). */
static int wound_nibble(const CreatureInfo *info, int location)
{
    uint8_t chances = location < 2 ? info->unknown_1a : info->unknown_1b;
    return location & 1 ? chances >> NIBBLE_BITS : chances & NIBBLE_MASK;
}

/* True when the champion dodges the melee attack of a creature with dexterity. */
static int creature_attack_missed(const CreatureInfo *info, int champ, uint16_t dexterity, int difficulty)
{
    int visibility;
    if (game.magic_light[MAGIC_INVISIBILITY] && !(info->flags & CREATURE_SEES_INVISIBLE)) visibility = VISIBILITY_INVISIBLE;
    else if (info->flags & CREATURE_SEES_IN_DARK) visibility = 0;
    else visibility = view_light_level * 2;
    if (party_sleeping || dexterity == DEXTERITY_ALWAYS_HITS) return 0;
    int threshold = (uint16_t)visibility + (int16_t)((rand_next() & ROLL_MELEE) + dexterity + difficulty);
    return ((uint16_t)champ_get_dexterity(champ) >= threshold - DEXTERITY_DODGE_MARGIN && rand_2bits())
           || champ_test_luck(champion(champ), LUCK_DODGE);
}

/* The wounds of a melee hit: the hands, or a location by the wound chances of the creature. */
static uint16_t creature_attack_wounds(const CreatureInfo *info)
{
    uint32_t r = rand_next();
    if (!(r & ROLL_WOUND_LOCATION) || (!info->unknown_1a && !info->unknown_1b)) return (uint16_t)((r & 1) + 1);
    uint16_t roll = r & NIBBLE_MASK;
    if (!roll) roll = 1;
    int location = 0;
    while (location < WOUND_LOCATION_LAST && roll > wound_nibble(info, location)) location++;
    return melee_wounds[location];
}

int creature_attack_champion(uint16_t group, int champ)
{
    if ((uint16_t)champ >= game.champion_count) return 0;
    Champion *c = champion(champ);
    if (!c->health) return 0;
    int16_t damage = 0;
    int difficulty = dungeon_map_difficulty(party.cur_map) * 2;
    const CreatureInfo *info = creature_group_info(group);
    int kind = info->unknown_1c;
    uint16_t dexterity = info->dexterity;
    if (kind == ATTACK_KIND_ANY_DEXTERITY) dexterity = (uint16_t)min16(DEXTERITY_ALWAYS_HITS, (int16_t)(dexterity * 2));
    else if (kind == ATTACK_KIND_ALWAYS_HITS) dexterity = DEXTERITY_ALWAYS_HITS;
    if (!creature_attack_missed(info, champ, dexterity, difficulty)) {
        uint16_t wounds = creature_attack_wounds(info);
        int16_t power = (int16_t)(info->attack + min16(info->attack, (int16_t)((rand_next() & ROLL_MELEE_POWER) + difficulty)));
        int skipped = 0;
        if (kind != ATTACK_KIND_ALWAYS_HITS) {
            power = (int16_t)(power - champ_get_skill_level(champ, SKILL_PARRY, 1) * 2);
            if (power <= 1) {
                if (rand_bit()) skipped = 1;
                else power = (int16_t)(rand_2bits() + MELEE_WEAK_POWER_BASE);
            }
        }
        if (!skipped) {
            power >>= 1;
            uint16_t r1 = rand_below((uint16_t)power);
            power = (int16_t)(power + rand_2bits() + r1);
            power = (int16_t)(power + rand_below((uint16_t)power));
            power >>= 2;
            power = (int16_t)(power + rand_2bits() + 1);
            if (rand_bit()) power = (int16_t)(power - (rand_below((uint16_t)((power >> 1) + 1)) - 1));
            damage = (int16_t)champ_take_damage(champ, power, wounds, kind);
            if (damage) {
                sound_request_with_fallback(CAT_CHAMPION, c->portrait, SOUND_HURT, SOUND_FALLBACK, game.party_x, game.party_y,
                                         SOUND_DELAY_HURT, SOUND_PRIORITY_HURT, SOUND_VOLUME_LOUD);
                int16_t poison;
                if (info->poison && rand_bit() && (poison = (int16_t)champ_get_stat_adjusted_attack(champion(champ), STAT_VITALITY, info->poison)) != 0)
                    champ_poison(champ, (uint16_t)poison);
            }
        }
    }
    if (party_sleeping) champ_wake_party();
    return damage;
}

/* The number of square steps between two squares (no diagonals). */
static int16_t square_distance(int x1, int y1, int x2, int y2)
{
    int16_t dx = (int16_t)(x1 - x2), dy = (int16_t)(y1 - y2);
    return (int16_t)((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy));
}

void move_check_missiles_hit_party(int from_x, int from_y, int to_x, int to_y)
{
    uint8_t champs[DIR_COUNT] = {0}, next[DIR_COUNT] = {0};  /* by cell: the cell + 1 of the champion that a projectile there hits, 0 none */
    int again = 0;
    for (int cell = 0; cell < DIR_COUNT; cell++)
        if (champ_get_index_in_cell(cell) >= 0) champs[cell] = (uint8_t)(cell + 1);
    if ((int16_t)to_x >= 0 && square_distance(from_x, from_y, to_x, to_y) == 1) {
        int dir = dun_get_direction_to((int16_t)from_x, (int16_t)from_y, (int16_t)to_x, (int16_t)to_y);
        int dir2 = dir_turn(dir, TURN_RIGHT);
        next[0] = next[1] = next[2] = next[3] = 0;
        next[dir_turn(dir, TURN_LEFT)] = champs[dir];
        next[dir_turn(dir2, TURN_RIGHT)] = champs[dir2];
        again = champs[dir] || champs[dir2];
        if (!champs[dir]) champs[dir] = champs[dir_turn(dir, TURN_LEFT)];
        if (!champs[dir2]) champs[dir2] = champs[dir_turn(dir2, TURN_RIGHT)];
    }
    int x = from_x, y = from_y, sx = from_x, sy = from_y;  /* x, y: the hit argument; sx, sy: the square */
    for (;;) {
        uint16_t t = dungeon_square_first_thing(party.cur_map, sx, sy);
        while (t != THING_ENDOFLIST) {
            if (thing_type(t) == THING_PROJECTILE && timeline.events[thing_word(t, PROJECTILE_EVENT)].type != EVENT_PROJECTILE_MOVE
                && champs[thing_cell(t)]
                && projectile_hit(PROJECTILE_HIT_PARTY_SQUARE, (int16_t)x, (int16_t)y, champs[thing_cell(t)] - 1, t)) {
                projectile_cancel_event(t);
                t = dungeon_square_first_thing(party.cur_map, sx, sy);
                continue;
            }
            t = dungeon_next_thing(t);
        }
        if (!again) return;
        sx = to_x;
        sy = to_y;
        x = (uint16_t)(from_x | (((uint16_t)to_x << COORD_STOP_SHIFT) + COORD_STOP_ONE));
        y = (uint16_t)(from_y | (uint16_t)to_y << COORD_STOP_SHIFT);
        for (int i = 0; i < DIR_COUNT; i++) champs[i] = next[i];
        again = 0;
    }
}

#define LAUNCH_HIT_CHANCE 100

/* The first object on the square (x, y) in the two cells on side dir, THING_ENDOFLIST for none. */
static uint16_t launch_object(int x, int y, int dir)
{
    uint16_t t = dungeon_square_first_thing(party.cur_map, x, y);
    while (t != THING_ENDOFLIST
           && (thing_type(t) <= THING_SENSOR || (thing_cell(t) != dir && thing_cell(t) != dir_turn(dir, TURN_RIGHT))))
        t = dungeon_next_thing(t);
    return t;
}

void sensor_launch_missile(uint16_t sensor, const TimelineEvent *e)
{
    int16_t x = e->data[EVENT_X], y = e->data[EVENT_Y];
    int dir = e->data[EVENT_CELL], cell = dir_opposite(dir);
    int kind = sensor_type(sensor);
    uint16_t data = sensor_data(sensor);
    int energy = sensor_launch_energy(sensor), step = sensor_launch_step(sensor);
    int single = kind == LAUNCH_ITEM || kind == LAUNCH_SPELL || kind == LAUNCH_OBJECT;
    uint16_t first, second = (uint16_t)(data + SPELL_THING_FIRST);
    if (kind == LAUNCH_OBJECT || kind == LAUNCH_OBJECTS) {
        first = launch_object(x, y, dir);
        if (first == THING_ENDOFLIST) return;
        dungeon_unlink_thing_from_square(first, party.cur_map, x, y);
        if (!single) {
            second = launch_object(x, y, dir);
            if (second == THING_ENDOFLIST) single = 1;
            else dungeon_unlink_thing_from_square(second, party.cur_map, x, y);
        }
    } else if (kind == LAUNCH_SPELL || kind == LAUNCH_SPELLS) {
        first = second;
    } else {
        first = item_create_by_index(data);
        if (first == THING_NONE) return;
        if (!single && (second = item_create_by_index(data)) == THING_NONE) single = 1;
    }
    if (single) cell = dir_turn(cell, rand_bit());
    x = (int16_t)(x + dir_dx[dir]);
    y = (int16_t)(y + dir_dy[dir]);
    missile_from_sensor = 1;
    projectile_create(first, x, y, cell, dir, energy, LAUNCH_HIT_CHANCE, step);
    if (!single) projectile_create(second, x, y, dir_turn(cell, TURN_RIGHT), dir, energy, LAUNCH_HIT_CHANCE, step);
    missile_from_sensor = 0;
}

void champ_launch_projectile(int champ, uint16_t payload, int energy, int attack, int step)
{
    const Champion *c = champion(champ);
    int dir = c->dir;
    int cell = dir_turn(dir, (((c->cell - dir) + 1) & 2) >> 1);
    projectile_create(payload, game.party_x, game.party_y, cell, dir, (uint8_t)min16((int16_t)energy, UINT8_MAX),
                      (uint8_t)min16((int16_t)attack, UINT8_MAX), (uint8_t)min16((int16_t)step, UINT8_MAX));
    game.throw_cooldown = THROW_COOLDOWN;
    game.throw_dir = (uint16_t)dir;
}
