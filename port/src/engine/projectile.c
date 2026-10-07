#include "projectile.h"
#include "ai.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "game.h"
#include "hooks.h"
#include "item.h"
#include "money.h"
#include "move.h"
#include "random.h"
#include "save.h"
#include "sensor.h"
#include "view.h"

uint16_t projectile_extra_damage, projectile_attack_type;
int missile_from_sensor;

/* Projectile event position word (event data bytes 2 and 3). */
#define PROJECTILE_POSITION 2
#define POSITION_DIR_SHIFT 10
#define POSITION_STEP_SHIFT 12
#define PROJECTILE_ENERGY 4         /* record byte: the range and power left */
#define PROJECTILE_HIT_CHANCE 5     /* record byte */
#define CELL_MASK 3
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
#define POISON_CLOUD_LAST 0x600     /* a poison cloud lasts while its power word part is at least this */
#define DEATH_CLOUD_DECAY 0x28
#define DEATH_CLOUD_LAST 0x3700
#define SMOKE_DURATION 5
#define EXPLOSION_POWER_BITS 0xff00

#define CAT_EXPLOSION 0x0d
#define CAT_CHAMPION 0x16
#define SOUND_LAUNCH 0x86           /* sound entries of a thing: launched, falls, explodes */
#define SOUND_FALL 0x85
#define SOUND_EXPLODE 0x81
#define SOUND_HURT 0x82
#define SOUND_ANY 0xfe
#define SOUND_PRIORITY_LAUNCH 0x6e
#define SOUND_PRIORITY_EXPLODE 0x6c
#define SOUND_PRIORITY_HURT 0x69
#define SOUND_VOLUME_NORMAL 0x80
#define SOUND_VOLUME_LOUD 200
#define SOUND_VOLUME_MAX 0xff

/* Sensor types that react to projectiles and explosions. */
#define SENSOR_PROJECTILE_HIT 0x22      /* the wall sensor of a cell triggers when an object hits it */
#define SENSOR_PROJECTILE_PORTAL 0x23   /* the object goes through the wall to the target square */
#define SENSOR_EXPLOSION 0x26           /* the floor sensor triggers on an explosion */
#define SENSOR_ANY_THING 0x1ff          /* sensor data: any object or explosion type */
#define SENSOR_STRONG_ONLY 0x0004       /* flags: explosions of power 0xff (0x7f for lightning) only; a hit stops the object */
#define SENSOR_INVERTED_BIT 5
#define SENSOR_EFFECT_SHIFT 3
#define SENSOR_EFFECT_ALWAYS 0x18       /* effect bits: trigger with the inverted match */
#define SENSOR_TARGET 6                 /* target word: cell bits 4-5, x bits 6-10, y bits 11-15 */
#define TARGET_CELL_SHIFT 4
#define TARGET_X_SHIFT 6
#define TARGET_Y_SHIFT 11
#define LAUNCH_ENERGY_SHIFT 4           /* missile launcher target word: energy bits 4-11, step bits 12-15 */
#define LAUNCH_STEP_SHIFT 12
#define STRONG_POWER 0xff               /* the explosion power that a strong-only sensor needs */
#define STRONG_LIGHTNING_POWER 0x7f
#define ORNAMENT_KEY_CATCH 0x13         /* ornament value: the alcove catches thrown objects */
#define DOOR_KEY_GRATE 0x10             /* door value: objects and weak spells fly through the door */
#define DOOR_BUTTON 0x0040
#define DOOR_SPELL_OPENS 0x2000
#define DOOR_BREAKABLE_BY_MAGIC 0x0080
#define DOOR_STATE_BROKEN 5
#define DOOR_STATE_ALMOST_OPEN 1
#define ITEM_ATTR_FLAGS 0               /* item value 0: ITEM_* flags */
#define ITEM_ATTR_THROW_POWER 9
#define ITEM_ATTR_POISON 0x0d
#define ITEM_GRATE_PASSES 0x0002        /* the object can fly through a grate */
#define ITEM_CAUGHT 0x0040              /* a creature that catches objects catches it */

/* Potions that explode on a hit. */
#define POTION_VEN_BOMB 3
#define POTION_FIRE_BOMB 0x13
#define POTION_TYPE_MASK 0x7fff
#define POTION_TYPE_SHIFT 8
#define POTION_POWER_MASK 0xff

/* Creature info bits that projectiles test. */
#define CREATURE_REFLECTS_SPELLS 0x0002     /* flags */
#define CREATURE_NON_MATERIAL CREATURE_NOT_DOOR_HIT
#define CREATURE_CATCHES_OBJECTS 0x0200     /* flags */
#define CREATURE_SEES_INVISIBLE 0x0400      /* flags */
#define CREATURE_IGNORES_LIGHT 0x0800       /* flags */
#define CREATURE_RESISTS_NON_FIRE 0x10      /* door_flags: only fire hurts it fully */
#define STATIC_SPELLS_ONLY 0x0100           /* possessions of a static creature: objects fly through it */
#define POSSESSIONS_DIRECTION 1             /* possessions bits 0-3: a dropped object lies in the flight direction cell */
#define DODGE_NONE 0x0f                     /* unknown_18 bits 4-7: explosion dodge; 0xf: explosions do not hurt */
#define ATTACK_KIND_ANY_DEXTERITY 9         /* unknown_1c: melee attack kinds */
#define ATTACK_KIND_ALWAYS_HITS 8
#define DOOR_SIZE_MASK 3
#define PUSH_BOUNCES 0xff                   /* push_resistance: objects bounce off the creature */
#define SPELL_PUSH_MIN 0x28                 /* the strength of a push or pull spell */
#define SPELL_PUSH_MAX 0xfe
/* Random parts of the projectile attacks. */
#define ROLL_POISON 0x7f                    /* poison weakens when this roll is above the energy */
#define ROLL_GRATE 0x7f                     /* an object flies through a grate when its hit chance beats this roll */
#define ROLL_DOUBLE_POWER 0x1ff             /* a thrown object hits twice as hard when its hit chance beats this roll */
#define ROLL_SLIME 0x1f
#define HIT_CHANCE_BONUS_BASE 0x20          /* a high hit chance doubles the attack above this - hit chance / 8 */
#define ROLL_MELEE 0x1f
#define ROLL_WOUND_LOCATION 0x70            /* all clear: the hit wounds the hands */
#define VISIBILITY_INVISIBLE 0x10           /* the hit bonus against an invisible party */
#define DEXTERITY_ALWAYS_HITS 0xff
#define DEXTERITY_DODGE_MARGIN 0x10
#define LUCK_DODGE 0x3c

/* Projectile attack types (champ_take_damage). */
enum { ATTACK_FIRE = 1, ATTACK_NORMAL = 3, ATTACK_THROWN = 4, ATTACK_MAGIC = 5, ATTACK_LIGHTNING = 7 };
#define ATTACK_FACING 0x8000                /* the champion faces the projectile */
#define WOUND_PROJECTILE 0x0c               /* the wounds of a projectile hit: head and torso */
#define WOUND_EXPLOSION 0x3f
#define WOUND_HEAD 0x04
#define WOUND_TORSO 0x08
#define WOUND_FEET 0x20

/* Alert flags (creature_alert). */
#define ALERT_HIT 0x200d
#define ALERT_MISSILE_NEAR 0x2006
#define ALERT_ATTACKED 2

#define MAP_DIFFICULTY_SHIFT 12             /* map object counts: difficulty bits 12-15 */
#define SKILL_DEFENSE 7
#define STAT_VITALITY 4

/* The flight direction bits of the queued move of projectile (they are in the timeline event). */
static int projectile_dir(uint16_t projectile)
{
    const TimelineEvent *e = &timeline.events[thing_word(projectile, PROJECTILE_EVENT)];
    return e->data[PROJECTILE_POSITION + 1] >> (POSITION_DIR_SHIFT - 8) & 3;
}

static uint16_t event_word(const TimelineEvent *e, int at) { return (uint16_t)(e->data[at] | e->data[at + 1] << 8); }

static void set_event_word(TimelineEvent *e, int at, uint16_t w)
{
    e->data[at] = (uint8_t)w;
    e->data[at + 1] = (uint8_t)(w >> 8);
}

/* True for the party square on the current map. */
static int on_party_square(int x, int y)
{
    return party.cur_map == party.square_map && x == party.square_x && y == party.square_y;
}

/* A thing at cell. */
static uint16_t with_cell(uint16_t thing, int cell)
{
    return (uint16_t)((thing & THING_ID_MASK) | (cell & CELL_MASK) << THING_CELL_SHIFT);
}

static int16_t min16(int16_t a, int16_t b) { return a < b ? a : b; }
static int16_t max16(int16_t a, int16_t b) { return a > b ? a : b; }

void projectile_create(uint16_t payload, int x, int y, int cell, int dir, int energy, int attack, int step)
{
    uint16_t t = dungeon_unused_thing(THING_PROJECTILE);
    if (t == THING_NONE) {
        if (thing_type(payload) != THING_EXPLOSION) move_thing(with_cell(payload, cell), -1, 0, x, y);
        return;
    }
    sound_play_with_fallback(item_gdat_category(payload), item_gdat_index(payload), SOUND_LAUNCH, SOUND_ANY, (int16_t)x,
                             (int16_t)y, 1, SOUND_PRIORITY_LAUNCH, SOUND_VOLUME_NORMAL);
    t = with_cell(t, cell);
    uint8_t *record = dungeon_thing_record(t);
    set_thing_word(t, PROJECTILE_PAYLOAD, payload);
    record[PROJECTILE_ENERGY] = (uint8_t)energy;
    record[PROJECTILE_HIT_CHANCE] = (uint8_t)attack;
    dungeon_link_thing_to_square(t, party.cur_map, x, y);
    if (payload == SPELL_FIREBALL && party.cur_map == game.party_map) party.redraw |= REDRAW_VIEW;
    TimelineEvent e = {.time_map = (game.game_time + 1) | (uint32_t)(uint8_t)party.cur_map << 24};
    e.type = missile_from_sensor ? EVENT_PROJECTILE_MOVE_2 : EVENT_PROJECTILE_MOVE;
    set_event_word(&e, 0, t);
    set_event_word(&e, PROJECTILE_POSITION, (uint16_t)((x & LOCATION_COORD_MASK) | (y & LOCATION_COORD_MASK) << LOCATION_Y_SHIFT
                                                       | (dir & 3) << POSITION_DIR_SHIFT
                                                       | (step & 0xf) << POSITION_STEP_SHIFT));
    set_thing_word(t, PROJECTILE_EVENT, (uint16_t)timeline_add_event(&e));
}

/* An explosion of type with power on (x, y): the explosion sensors of the square react. */
static void explosion_trigger_sensors(int type, int x, int y, int power)
{
    for (uint16_t t = dungeon_square_first_thing(party.cur_map, x, y); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_SENSOR) continue;
        uint16_t w = thing_word(t, SENSOR_TYPE_DATA), flags = thing_word(t, SENSOR_FLAGS);
        if ((w & SENSOR_TYPE_MASK) != SENSOR_EXPLOSION) continue;
        int match = (w >> SENSOR_DATA_SHIFT) == SENSOR_ANY_THING || (w >> SENSOR_DATA_SHIFT) == (uint16_t)(type - SPELL_THING_FIRST);
        match ^= flags >> SENSOR_INVERTED_BIT & 1;
        if (flags & SENSOR_STRONG_ONLY && (int16_t)power < ((uint16_t)type == SPELL_LIGHTNING ? STRONG_LIGHTNING_POWER : STRONG_POWER))
            match = 0;
        if ((flags & SENSOR_EFFECT_ALWAYS) == SENSOR_EFFECT_ALWAYS) sensor_trigger_target(t, !match, 0);
        else if (match) sensor_trigger_target(t, flags >> SENSOR_EFFECT_SHIFT & 3, 0);
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
    if (on_party_square(x, y)) champ_damage_party(damage, WOUND_EXPLOSION, ATTACK_FIRE);
    int16_t nx = (int16_t)x, ny = (int16_t)y, hx = (int16_t)x, hy = (int16_t)y;
    uint16_t g = creature_get_group_at(x, y);
    for (int first = 1;; first = 0) {
        if (g != THING_NONE && !creature_current_step_acts(g)) {
            const CreatureInfo *info = creature_group_info(g);
            if (info->door_flags & CREATURE_RESISTS_NON_FIRE && type != SPELL_FIREBALL)
                damage = (uint16_t)max16(1, (int16_t)(damage >> 2));
            int dodge = info->unknown_18 >> 4;
            if (dodge != DODGE_NONE) {
                if (info->flags & CREATURE_NON_MATERIAL) damage >>= 2;
                damage = (uint16_t)(damage - rand_below((uint16_t)(dodge * 2 + 1)));
                if ((int16_t)damage > 0) creature_alert(g, hx, hy, ALERT_HIT, 100, damage);
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
    uint16_t w = thing_word(t, EXPLOSION_DATA);
    if ((uint16_t)cell == EXPLOSION_AT_CENTER) {
        w |= EXPLOSION_CENTERED;
    } else {
        w &= ~EXPLOSION_CENTERED;
        t = with_cell(t, cell);
    }
    w = (uint16_t)((w & EXPLOSION_CENTERED) | ((type - SPELL_THING_FIRST) & EXPLOSION_TYPE_MASK)
                   | (uint8_t)power << EXPLOSION_POWER_SHIFT);
    set_thing_word(t, EXPLOSION_DATA, w);
    int volume =
        on_party_square(ex, ey) ? SOUND_VOLUME_MAX : min16(SOUND_VOLUME_MAX, (int16_t)(((uint16_t)power >> 1) + SOUND_VOLUME_NORMAL));
    sound_play_with_fallback(CAT_EXPLOSION, w & EXPLOSION_TYPE_MASK, SOUND_EXPLODE, SOUND_ANY, ex, ey, 1, SOUND_PRIORITY_EXPLODE,
                             volume);
    dungeon_link_thing_to_square(t, party.cur_map, ex, ey);
    int16_t delay = (uint16_t)type == EXPLOSION_SMOKE ? SMOKE_DURATION
                    : (uint16_t)type == SPELL_MISSILE_WALL ? (int16_t)((uint16_t)power >> 1) : 1;
    TimelineEvent e = {.time_map = (game.game_time + delay) | (uint32_t)(uint8_t)party.cur_map << 24, .type = EVENT_EXPLOSION};
    e.data[0] = (uint8_t)ex;
    e.data[1] = (uint8_t)ey;
    set_event_word(&e, 2, t);
    timeline_add_event(&e);
    /* the power that the sensors test: the original reads it from the door record after a door spell */
    const uint8_t *power_record = dungeon_thing_record(t);
    switch ((uint16_t)type) {
    case SPELL_LIGHTNING: case SPELL_FIREBALL: case EXPLOSION_STORM_LIGHTNING: case EXPLOSION_STORM_BLAST:
        explosion_blast((uint16_t)type, power, x, y, cell);
        if (party.cur_map == game.party_map) party.redraw |= REDRAW_VIEW;
        break;
    case SPELL_DOOR_TOGGLE: case SPELL_DOOR_SET: {
        uint8_t square = dun_get_square(ex, ey);
        if (square_type(square) != SQUARE_DOOR || (square & SQUARE_DOOR_STATE) == DOOR_STATE_BROKEN) break;
        uint16_t door = dungeon_square_first_thing(party.cur_map, ex, ey);
        power_record = dungeon_thing_record(door);
        if (thing_word(door, DOOR_DATA) & (DOOR_BUTTON | DOOR_SPELL_OPENS))
            sensor_schedule_square_event(ex, ey, 0, (uint16_t)type == SPELL_DOOR_TOGGLE ? EFFECT_TOGGLE : EFFECT_SET,
                                         game.game_time + 1);
        break;
    }
    }
    explosion_trigger_sensors((uint16_t)type, x, y, power_record[EXPLOSION_DATA + 1]);
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
                int cell = projectile >> THING_CELL_SHIFT;
                if ((creature_group_info(g)->possessions & 0xf) == POSSESSIONS_DIRECTION) cell = projectile_dir(projectile);
                move_thing(with_cell(payload, cell), -2, -1, (int16_t)x, (int16_t)y);
            } else {
                move_thing((uint16_t)((payload & THING_ID_MASK) | (projectile & ~THING_ID_MASK)), -2, 0, (int16_t)x, (int16_t)y);
            }
        }
        sound_play_with_fallback(item_gdat_category(projectile), item_gdat_index(projectile), SOUND_FALL, SOUND_ANY,
                                 (int16_t)x, (int16_t)y, 1, SOUND_PRIORITY_DROP, SOUND_VOLUME_NORMAL);
    }
    set_thing_word(projectile, THING_NEXT, THING_NONE);
}

int projectile_get_attack(uint16_t projectile, uint16_t payload)
{
    const uint8_t *record = dungeon_thing_record(projectile);
    uint16_t energy = record[PROJECTILE_ENERGY];
    uint16_t power;
    projectile_extra_damage = 0;
    projectile_attack_type = ATTACK_NORMAL;
    if (thing_type(payload) != THING_EXPLOSION) {
        uint16_t throw_power = item_attr(payload, ITEM_ATTR_THROW_POWER);
        power = 0;
        if (throw_power) {
            int factor = (uint16_t)((record[PROJECTILE_HIT_CHANCE] >> 4) + 3);
            power = (uint16_t)((int)(uint16_t)(throw_power + (energy >> 1)) * (factor * factor) >> 7);
            projectile_attack_type = ATTACK_THROWN;
            projectile_extra_damage = item_attr(payload, ITEM_ATTR_POISON);
            if (projectile_extra_damage && (rand_next() & ROLL_POISON) > energy)
                projectile_extra_damage = (uint16_t)(projectile_extra_damage - rand_below((uint16_t)((projectile_extra_damage >> 1) + 1)));
        }
        power = (uint16_t)(power + rand_2bits());
        power = (uint16_t)(power + item_weight(payload));
        if (record[PROJECTILE_HIT_CHANCE] > (rand_next() & ROLL_DOUBLE_POWER)) power = (uint16_t)(power * 2);
    } else if (payload == SPELL_SLIME) {
        uint16_t r = rand_next() & 0xf;
        projectile_extra_damage = (uint16_t)(r + 10);
        power = (uint16_t)(r + (rand_next() & ROLL_SLIME));
    } else if (payload >= SPELL_HARM_NON_MATERIAL) {
        projectile_attack_type = ATTACK_MAGIC;
        if (payload != SPELL_POISON_BOLT) return 0;
        projectile_extra_damage = energy;
        return (energy >> 3) + 1;
    } else {
        projectile_attack_type = ATTACK_FIRE;
        power = rand_next() & 0xf;
        power = (uint16_t)(power + (rand_next() & 0xf) + 10);
        if (payload == SPELL_LIGHTNING) {
            projectile_attack_type = ATTACK_LIGHTNING;
            power = (uint16_t)(power * 16 + energy);
        }
    }
    uint16_t attack = (uint16_t)(((power + energy) >> 4) + 1);
    uint16_t r = rand_below((uint16_t)((attack >> 1) + 1));
    attack = (uint16_t)(attack + rand_2bits() + r);
    int16_t a = max16((int16_t)attack, (int16_t)((attack - (HIT_CHANCE_BONUS_BASE - (record[PROJECTILE_HIT_CHANCE] >> 3))) * 2));
    return min16(a, (int16_t)(energy * 2));
}

/* The projectile from (x, y) hits the wall at (wall_x, wall_y) while it flies toward dir: the hit sensors of the
   wall cell react, an alcove can catch the object, a portal sensor sends it on. Returns 1 when the object stops. */
static int projectile_hit_wall_sensors(int wall_x, int wall_y, int x, int y, int dir, uint16_t projectile)
{
    int done = 0, side = (dir + 2) & 3;
    uint16_t first = dungeon_square_first_thing(party.cur_map, wall_x, wall_y);
    uint16_t payload = thing_word(projectile, PROJECTILE_PAYLOAD);
    int object = thing_type(payload) != THING_EXPLOSION;
    for (uint16_t t = first; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        int cell = t >> THING_CELL_SHIFT;
        if (cell != side) continue;
        int to_x = wall_x, to_y = wall_y;
        if (object && !done && sensor_wall_ornament_value(first, t, ORNAMENT_KEY_CATCH) && !rand_2bits()) {
            dungeon_unlink_thing_from_square(projectile, party.cur_map, x, y);
            projectile = with_cell(projectile, cell);
        } else if (thing_type(t) == THING_SENSOR && object) {
            uint16_t w = thing_word(t, SENSOR_TYPE_DATA), flags = thing_word(t, SENSOR_FLAGS);
            uint16_t data = w >> SENSOR_DATA_SHIFT;
            int kind = w & SENSOR_TYPE_MASK;
            if (kind == SENSOR_PROJECTILE_HIT) {
                int match = data == SENSOR_ANY_THING || data == item_get_index(payload);
                match ^= flags >> SENSOR_INVERTED_BIT & 1;
                if (!match) continue;
                sensor_trigger_target(t, (flags & SENSOR_EFFECT_ALWAYS) == SENSOR_EFFECT_ALWAYS ? 0 : flags >> SENSOR_EFFECT_SHIFT & 3, 0);
                if (done || !(flags & SENSOR_STRONG_ONLY)) continue;
                dungeon_unlink_thing_from_square(projectile, party.cur_map, x, y);
                projectile = with_cell(projectile, side);
            } else if (kind == SENSOR_PROJECTILE_PORTAL && !done && (data == SENSOR_ANY_THING || data == item_get_index(payload))) {
                dungeon_unlink_thing_from_square(projectile, party.cur_map, x, y);
                uint16_t target = thing_word(t, SENSOR_TARGET);
                to_x = target >> TARGET_X_SHIFT & LOCATION_COORD_MASK;
                to_y = target >> TARGET_Y_SHIFT;
                int to_cell = square_type(dun_get_square(to_x, to_y)) == SQUARE_WALL ? target >> TARGET_CELL_SHIFT & 3 : flags >> SENSOR_EFFECT_SHIFT & 3;
                projectile = with_cell(projectile, to_cell);
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
        if ((info->flags >> CREATURE_DOOR_SIZE_SHIFT & DOOR_SIZE_MASK) > 1) return 0;
        bounce = !(info->possessions & CREATURE_POSSESSIONS_ON_FLOOR);
    }
    if (bounce) {  /* the object falls back on the square that it came from */
        int back = (projectile_dir(h->projectile) + 2) & 3;
        h->stop_x = (int16_t)(h->stop_x + dir_dx[back]);
        h->stop_y = (int16_t)(h->stop_y + dir_dy[back]);
        int c = h->projectile >> THING_CELL_SHIFT;
        h->projectile = with_cell(h->projectile, (back & 1) == (c & 1) ? c - 1 : c + 1);
    }
    int attack = projectile_get_attack(h->projectile, payload);
    h->damage = (uint16_t)((uint16_t)(attack << 6) / info->defense);
    if (h->damage) {
        uint16_t damage = (uint16_t)(creature_roll_armor_factor(g, projectile_extra_damage) + h->damage);
        if (info->door_flags & CREATURE_RESISTS_NON_FIRE && projectile_attack_type != ATTACK_FIRE)
            damage = (uint16_t)((int16_t)damage >> 2);
        creature_alert(g, x, y, ALERT_HIT, 100, damage);
        if (!h->explosive && info->flags & CREATURE_CATCHES_OBJECTS && item_attr(payload, ITEM_ATTR_FLAGS) & ITEM_CAUGHT)
            h->holder = (uint16_t *)(dungeon_thing_record(g) + GROUP_POSSESSIONS);
    }
    if (h->potion_bomb || h->payload_type == THING_WEAPON
        || (h->payload_type == THING_EXPLOSION && payload != SPELL_DOOR_TOGGLE && (payload < SPELL_PUSH || payload > SPELL_MISSILE_WALL)))
        creature_alert(g, x, y, ALERT_ATTACKED, h->damage < 5 && payload != SPELL_POISON_CLOUD ? 30 : 95, 0);
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
    if (champion(champ)->dir == projectile_dir(h->projectile) + 2) projectile_attack_type |= ATTACK_FACING;
    if ((int16_t)champ_take_damage(champ, (int16_t)attack, WOUND_PROJECTILE, projectile_attack_type) && projectile_extra_damage
        && rand_next() & 7)
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
    if (state == DOOR_STATE_BROKEN || state <= DOOR_STATE_ALMOST_OPEN) return 0;
    if (door_value(door, DOOR_KEY_GRATE)) {
        int passes;
        if (h->payload_type == THING_EXPLOSION) {
            passes = h->payload >= SPELL_HARM_NON_MATERIAL;
        } else {
            passes = dungeon_thing_record(h->projectile)[PROJECTILE_HIT_CHANCE] > (rand_next() & ROLL_GRATE)
                     && item_attr(h->payload, ITEM_ATTR_FLAGS) & ITEM_GRATE_PASSES;
        }
        if (passes) return 0;
    }
    uint16_t attack = (uint16_t)(projectile_get_attack(h->projectile, h->payload) + 1);
    sensor_door_try_destroy(h->x, h->y, (int16_t)(rand_below(attack) + attack), 0, 0);
    return 1;
}

int projectile_hit(int impact, int x, int y, int cell, uint16_t projectile)
{
    Hit h = {.projectile = projectile, .payload = thing_word(projectile, PROJECTILE_PAYLOAD)};
    h.payload_type = thing_type(h.payload);
    uint16_t blast_type = 0, potion = 0;
    int blast_power = 0;
    if (h.payload_type == THING_POTION) {
        uint16_t w = thing_word(h.payload, 2);
        int kind = (w & POTION_TYPE_MASK) >> POTION_TYPE_SHIFT;
        if (kind == POTION_VEN_BOMB || kind == POTION_FIRE_BOMB) {
            blast_type = kind == POTION_VEN_BOMB ? SPELL_POISON_CLOUD : SPELL_FIREBALL;
            h.potion_bomb = 1;
            blast_power = w & POTION_POWER_MASK;
            potion = h.payload;
        }
    }
    h.explosive = h.payload_type == THING_EXPLOSION && h.payload != SPELL_SLIME && h.payload != SPELL_POISON_BOLT;
    if ((int16_t)x > UINT8_MAX) {
        h.stop_x = (int16_t)(((int16_t)x >> COORD_STOP_SHIFT) - 1);
        h.stop_y = (int8_t)(y >> COORD_STOP_SHIFT);
        x = (uint8_t)x;
        y = (uint8_t)y;
    } else {
        h.stop_x = (int16_t)x;
        h.stop_y = (int16_t)y;
    }
    h.x = (int16_t)x;
    h.y = (int16_t)y;
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
    if (h.explosive || h.potion_bomb || h.payload == SPELL_POISON_BOLT) {
        uint16_t power;
        uint8_t *record = dungeon_thing_record(projectile);
        int explode = 1;
        if (h.potion_bomb) {
            set_thing_word(potion, THING_NEXT, THING_NONE);
            set_thing_word(projectile, PROJECTILE_PAYLOAD, blast_type);
            h.payload = blast_type;
            power = (uint16_t)blast_power;
        } else {
            power = record[PROJECTILE_ENERGY];
            if (h.payload == SPELL_POISON_BOLT && !(power >>= 2)) explode = 0;
            if (h.payload == SPELL_LIGHTNING && !(power >>= 1)) explode = 0;
        }
        if (explode) {
            if (h.payload == SPELL_PUSH || h.payload == SPELL_PULL) {
                push_dir = projectile_dir(projectile);
                push_strength = record[PROJECTILE_ENERGY];
            }
            int at = h.payload == SPELL_POISON_CLOUD || h.payload == SPELL_DOOR_TOGGLE || h.payload == SPELL_DOOR_SET
                     ? EXPLOSION_AT_CENTER : (uint16_t)cell;
            explosion_create(h.payload, power, (uint16_t)fall_x, (uint16_t)fall_y, at);
        }
    }
    dungeon_unlink_thing_from_square(h.projectile, party.cur_map, fall_x, fall_y);
    projectile_destroy(h.projectile, h.holder, h.stop_x, h.stop_y);
    if (push_strength) {
        int16_t strength = max16(min16((int16_t)push_strength, SPELL_PUSH_MAX), SPELL_PUSH_MIN);
        int dir = h.payload == SPELL_PUSH ? push_dir : (push_dir + 2) & 3;
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
    uint16_t w = event_word(e, PROJECTILE_POSITION);
    set_event_word(e, PROJECTILE_POSITION, (uint16_t)((w & ~(3 << POSITION_DIR_SHIFT)) | (dir & 3) << POSITION_DIR_SHIFT));
}

/* The creature on (x, y) may reflect a spell of the projectile; returns 1 when a creature stands there and
   reflects spells (then the projectile hits nothing on this square). */
static int projectile_reflect(TimelineEvent *e, uint16_t projectile, int x, int y)
{
    uint16_t g = creature_get_group_at(x, y);
    if (g == THING_NONE || !(creature_group_info(g)->flags & CREATURE_REFLECTS_SPELLS)
        || thing_word(projectile, PROJECTILE_PAYLOAD) < SPELL_THING_FIRST)
        return 0;
    int facing = thing_word(g, GROUP_DIR) >> GROUP_DIR_SHIFT & 1;
    int dir = spell_reflection[facing][(int16_t)(projectile >> THING_CELL_SHIFT) & 3][event_word(e, PROJECTILE_POSITION) >> POSITION_DIR_SHIFT & 3];
    if (dir < 4) {
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
        if (g != THING_NONE) creature_alert(g, party.dest_x, party.dest_y, ALERT_MISSILE_NEAR, 100, 0);
        if (i) break;
        uint8_t square = dun_get_square(party.dest_x, party.dest_y);
        int type = square_type(square), state = square & SQUARE_DOOR_STATE;
        if (type == SQUARE_WALL || (type == SQUARE_FAKE_WALL && !(square & SQUARE_FAKE_WALL_OPEN))
            || (type == SQUARE_DOOR && state != DOOR_STATE_BROKEN && state > DOOR_STATE_ALMOST_OPEN))
            break;
    }
    if (party.dest_map != saved) dun_set_current_map(saved);
}

/* The move event of a projectile: it hits what is on its square, then flies on to the next cell or square. */
void projectile_process_event(TimelineEvent *event)
{
    TimelineEvent e = *event;
    uint16_t projectile = event_word(&e, 0);
    uint8_t *record = dungeon_thing_record(projectile);
    int16_t x = location_x(e.data[PROJECTILE_POSITION]), y = (int16_t)location_y(event_word(&e, PROJECTILE_POSITION));
    if (e.type == EVENT_PROJECTILE_MOVE) {
        e.type = EVENT_PROJECTILE_MOVE_2;  /* the first move: no hit on the launch square */
    } else {
        int cell = projectile >> THING_CELL_SHIFT;
        if (!projectile_reflect(&e, projectile, x, y)) {
            if (on_party_square(x, y) && projectile_hit(PROJECTILE_HIT_PARTY_SQUARE, x, y, cell, projectile)) return;
            if (projectile_hit(PROJECTILE_HIT_CREATURE, x, y, cell, projectile)) return;
        }
        int step = event_word(&e, PROJECTILE_POSITION) >> POSITION_STEP_SHIFT;
        if (record[PROJECTILE_ENERGY] <= step) {
            dungeon_unlink_thing_from_square(projectile, party.cur_map, x, y);
            projectile_destroy(projectile, NULL, x, y);
            return;
        }
        record[PROJECTILE_ENERGY] = (uint8_t)(record[PROJECTILE_ENERGY] - step);
        record[PROJECTILE_HIT_CHANCE] = record[PROJECTILE_HIT_CHANCE] < step ? 0 : (uint8_t)(record[PROJECTILE_HIT_CHANCE] - step);
    }
    projectile = event_word(&e, 0);
    int cell = projectile >> THING_CELL_SHIFT;
    int dir = event_word(&e, PROJECTILE_POSITION) >> POSITION_DIR_SHIFT & 3;
    int leaves = dir == cell || ((dir + 1) & 3) == cell;  /* the projectile is in the front cells: it leaves the square */
    int16_t from_x = x, from_y = y;
    if (leaves) {
        x = (int16_t)(x + dir_dx[dir]);
        y = (int16_t)(y + dir_dy[dir]);
        uint8_t square = dun_get_square(x, y);
        int type = square_type(square);
        if ((type == SQUARE_WALL || (type == SQUARE_FAKE_WALL && !(square & (SQUARE_FAKE_WALL_OPEN | 1)))
             || (type == SQUARE_STAIRS && square_type(dungeon_square(party.cur_map, from_x, from_y)) == SQUARE_STAIRS))
            && projectile_hit(type, from_x, from_y, cell, projectile))
            return;
        for (uint16_t t = dungeon_square_first_object(party.cur_map, x, y); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
            if (thing_type(t) != THING_EXPLOSION || (thing_word(t, EXPLOSION_DATA) & EXPLOSION_TYPE_MASK) != BLAST_MISSILE_WALL)
                continue;
            TimelineEvent *queued = &timeline.events[thing_word(projectile, PROJECTILE_EVENT)];
            int back = ((event_word(queued, PROJECTILE_POSITION) >> POSITION_DIR_SHIFT & 3) + 2) & 3;
            set_event_dir(&e, back);
            set_event_dir(queued, back);
            x = from_x;
            y = from_y;
            leaves = 0;
            break;
        }
    }
    int new_cell = ((dir & 1) == (cell & 1) ? cell - 1 : cell + 1) & 3;
    if (leaves) {
        move_thing(with_cell(projectile, new_cell), from_x, from_y, x, y);
        if (thing_word(projectile, PROJECTILE_PAYLOAD) == SPELL_FIREBALL && party.cur_map == game.party_map)
            party.redraw |= REDRAW_VIEW;
        uint16_t w = event_word(&e, PROJECTILE_POSITION);
        w = (uint16_t)((w & ~POSITION_SQUARE_DIR) | (party.dest_x & LOCATION_COORD_MASK) | (party.dest_y & LOCATION_COORD_MASK) << LOCATION_Y_SHIFT
                        | (party.dest_dir & 3) << POSITION_DIR_SHIFT);
        set_event_word(&e, PROJECTILE_POSITION, w);
        projectile = with_cell(projectile, party.dest_cell);
        e.time_map = (e.time_map & EVENT_TIME_MASK) | (uint32_t)(uint16_t)party.dest_map << 24;
        projectile_alert_ahead();
    } else {
        if (square_type(dun_get_square(x, y)) == SQUARE_DOOR && projectile_hit(SQUARE_DOOR, x, y, cell, projectile)) return;
        dungeon_unlink_thing_from_square(projectile, party.cur_map, x, y);
        projectile = with_cell(projectile, new_cell);
        dungeon_link_thing_to_square(projectile, party.cur_map, x, y);
    }
    e.time_map++;
    set_event_word(&e, 0, projectile);
    set_thing_word(projectile, PROJECTILE_EVENT, (uint16_t)timeline_add_event(&e));
}

int explosion_get_attack(uint16_t explosion, uint16_t target)
{
    uint16_t w = thing_word(explosion, EXPLOSION_DATA);
    int type = w & EXPLOSION_TYPE_MASK;
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
    uint16_t attack = w >> EXPLOSION_POWER_SHIFT;
    if (kind == THING_GROUP && creature_group_info(target)->door_flags & CREATURE_RESISTS_NON_FIRE && type != BLAST_FIREBALL)
        attack = (uint16_t)((int16_t)attack >> 2);
    if (flags & BLAST_RANDOM) {
        attack = (uint16_t)(((w >> EXPLOSION_POWER_SHIFT) >> 1) + 1);
        attack = (uint16_t)(attack + rand_below(attack) + 1);
    }
    switch (type) {
    case BLAST_LIGHTNING:
        attack = (uint16_t)((int16_t)attack >> 1);
        /* fall through */
    case BLAST_FIREBALL:
        if (kind == THING_DOOR && !(thing_word(target, DOOR_DATA) & DOOR_BREAKABLE_BY_MAGIC)) return 0;
        return attack;
    case BLAST_HARM_NON_MATERIAL:
        return creature_group_info(target)->flags & CREATURE_NON_MATERIAL ? attack : 0;
    case BLAST_POISON_CLOUD:
        attack = (uint16_t)max16(1, (int16_t)(min16((int16_t)((w >> EXPLOSION_POWER_SHIFT) >> 5), 4) + rand_bit()));
        if (kind == THING_GROUP) attack = (uint16_t)creature_roll_armor_factor(target, attack);
        return attack;
    default:
        return attack;
    }
}

/* The event of an explosion: it hurts doors, the party, and creatures on its square, then a cloud fades or the explosion ends. */
void explosion_process_event(TimelineEvent *e)
{
    int x = e->data[0], y = e->data[1];
    uint16_t t = event_word(e, 2);
    if (square_type(dun_get_square(x, y)) == SQUARE_DOOR) {
        int16_t attack = (int16_t)explosion_get_attack(t, dungeon_square_first_thing(party.cur_map, x, y));
        if (attack) sensor_door_try_destroy(x, y, attack, 1, 0);
    }
    int type = thing_word(t, EXPLOSION_DATA) & EXPLOSION_TYPE_MASK;
    if (type != BLAST_MISSILE_WALL && type != BLAST_FIREBALL && type != BLAST_LIGHTNING) {
        uint16_t attack;
        if (on_party_square(x, y) && (attack = (uint16_t)explosion_get_attack(t, THING_NONE)))
            champ_damage_party(attack, 0, 0);
        uint16_t g = creature_get_group_at(x, y);
        if (g != THING_NONE && !creature_current_step_acts(g) && (attack = (uint16_t)explosion_get_attack(t, g)))
            creature_alert(g, x, y, ALERT_HIT, 100, attack);
        uint16_t w = thing_word(t, EXPLOSION_DATA);
        int lasts = 1;
        switch (w & EXPLOSION_TYPE_MASK) {
        case BLAST_POISON_CLOUD:
            if ((w & EXPLOSION_POWER_BITS) < POISON_CLOUD_LAST) lasts = 0;
            else w = (uint16_t)((uint8_t)w | (uint8_t)((w >> EXPLOSION_POWER_SHIFT) - POISON_CLOUD_DECAY) << EXPLOSION_POWER_SHIFT);
            break;
        case BLAST_DEATH_CLOUD:
            if ((w & EXPLOSION_POWER_BITS) <= DEATH_CLOUD_LAST) lasts = 0;
            else w = (uint16_t)((uint8_t)w | (uint8_t)((w >> EXPLOSION_POWER_SHIFT) - DEATH_CLOUD_DECAY) << EXPLOSION_POWER_SHIFT);
            break;
        case BLAST_SMOKE:
            sound_play_with_fallback(CAT_EXPLOSION, w & EXPLOSION_TYPE_MASK, SOUND_EXPLODE, SOUND_ANY, x, y, 1, SOUND_PRIORITY_EXPLODE,
                                     SOUND_VOLUME_LOUD);
            w = (uint16_t)((w & ~EXPLOSION_TYPE_MASK) | ((w + 1) & EXPLOSION_TYPE_MASK));
            break;
        default:
            lasts = 0;
        }
        if (lasts) {
            set_thing_word(t, EXPLOSION_DATA, w);
            TimelineEvent next = *e;
            next.time_map++;
            timeline_add_event(&next);
            return;
        }
    }
    type = thing_word(t, EXPLOSION_DATA) & EXPLOSION_TYPE_MASK;
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
        for (int i = 0; i < 4; i++) order[i] = attack_cell_orders_random[dir + i];
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
    for (int i = 0; i < 4; i++) order[i] = attack_cell_orders[dir * 2 + (side >> 1 & 1)][i];
}

/* The wounds of a melee hit, by the hit location chosen from the creature's wound nibbles. */
static const uint8_t melee_wounds[4] = {WOUND_FEET, WOUND_LEGS, WOUND_TORSO, WOUND_HEAD};

int creature_attack_champion(uint16_t group, int champ)
{
    if ((uint16_t)champ >= game.champion_count) return 0;
    Champion *c = champion(champ);
    if (!c->health) return 0;
    int16_t damage = 0;
    int difficulty = (dungeon_map_object_counts(party.cur_map) >> MAP_DIFFICULTY_SHIFT) * 2;
    const CreatureInfo *info = creature_group_info(group);
    int kind = info->unknown_1c;
    int visibility;
    if (game.magic_light[MAGIC_INVISIBILITY] && !(info->flags & CREATURE_SEES_INVISIBLE)) visibility = VISIBILITY_INVISIBLE;
    else if (info->flags & CREATURE_IGNORES_LIGHT) visibility = 0;
    else visibility = view_light_level * 2;
    uint16_t dexterity = info->dexterity;
    if (kind == ATTACK_KIND_ANY_DEXTERITY) dexterity = (uint16_t)min16(DEXTERITY_ALWAYS_HITS, (int16_t)(dexterity * 2));
    else if (kind == ATTACK_KIND_ALWAYS_HITS) dexterity = DEXTERITY_ALWAYS_HITS;
    int missed = 0;
    if (!party_sleeping && dexterity != DEXTERITY_ALWAYS_HITS) {
        int threshold = (uint16_t)visibility + (int16_t)((rand_next() & ROLL_MELEE) + dexterity + difficulty);
        missed = ((int)(uint16_t)champ_get_dexterity(champ) >= threshold - DEXTERITY_DODGE_MARGIN && rand_2bits())
                 || champ_test_luck(champion(champ), LUCK_DODGE);
    }
    if (!missed) {
        uint32_t r = rand_next();
        uint16_t wounds;
        uint16_t nibbles = (uint16_t)(info->unknown_1a | info->unknown_1b << 8);
        if (!(r & ROLL_WOUND_LOCATION) || !nibbles) {
            wounds = (uint16_t)((r & 1) + 1);
        } else {
            uint16_t roll = r & 0xf;
            if (!roll) roll = 1;
            int location = 0;
            while (location < 3 && roll > (nibbles & 0xf)) {
                nibbles >>= 4;
                location++;
            }
            wounds = melee_wounds[location];
        }
        int16_t power = (int16_t)(info->attack + min16(info->attack, (int16_t)((rand_next() & 0xf) + difficulty)));
        int skipped = 0;
        if (kind != ATTACK_KIND_ALWAYS_HITS) {
            power = (int16_t)(power - champ_get_skill_level(champ, SKILL_DEFENSE, 1) * 2);
            if (power <= 1) {
                if (rand_bit()) skipped = 1;
                else power = (int16_t)(rand_2bits() + 2);
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
                sound_play_with_fallback(CAT_CHAMPION, c->portrait, SOUND_HURT, SOUND_ANY, game.party_x, game.party_y, 2, SOUND_PRIORITY_HURT,
                                         SOUND_VOLUME_LOUD);
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
    uint8_t champs[4] = {0}, next[4] = {0};  /* by cell: the cell + 1 of the champion that a projectile there hits, 0 none */
    int again = 0;
    for (int cell = 0; cell < 4; cell++)
        if (champ_get_index_in_cell(cell) >= 0) champs[cell] = (uint8_t)(cell + 1);
    if ((int16_t)to_x >= 0 && square_distance(from_x, from_y, to_x, to_y) == 1) {
        int dir = dun_get_direction_to((int16_t)from_x, (int16_t)from_y, (int16_t)to_x, (int16_t)to_y);
        int dir2 = (dir + 1) & 3;
        next[0] = next[1] = next[2] = next[3] = 0;
        next[(dir + 3) & 3] = champs[dir];
        next[(dir2 + 1) & 3] = champs[dir2];
        again = champs[dir] || champs[dir2];
        if (!champs[dir]) champs[dir] = champs[(dir + 3) & 3];
        if (!champs[dir2]) champs[dir2] = champs[(dir2 + 1) & 3];
    }
    int x = from_x, y = from_y, sx = from_x, sy = from_y;  /* x, y: the hit argument; sx, sy: the square */
    for (;;) {
        uint16_t t = dungeon_square_first_thing(party.cur_map, sx, sy);
        while (t != THING_ENDOFLIST) {
            if (thing_type(t) == THING_PROJECTILE && timeline.events[thing_word(t, PROJECTILE_EVENT)].type != EVENT_PROJECTILE_MOVE
                && champs[t >> THING_CELL_SHIFT]
                && projectile_hit(PROJECTILE_HIT_PARTY_SQUARE, (int16_t)x, (int16_t)y, champs[t >> THING_CELL_SHIFT] - 1, t)) {
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
        for (int i = 0; i < 4; i++) champs[i] = next[i];
        again = 0;
    }
}

/* Sensor types that launch missiles. */
enum { LAUNCH_ITEM = 7, LAUNCH_SPELL = 8, LAUNCH_ITEMS = 9, LAUNCH_SPELLS = 0x0a, LAUNCH_OBJECT = 0x0e, LAUNCH_OBJECTS = 0x0f };
#define LAUNCH_HIT_CHANCE 100

/* The first object on the square (x, y) in the two cells on side dir, THING_ENDOFLIST for none. */
static uint16_t launch_object(int x, int y, int dir)
{
    uint16_t t = dungeon_square_first_thing(party.cur_map, x, y);
    while (t != THING_ENDOFLIST
           && (thing_type(t) <= THING_SENSOR || ((t >> THING_CELL_SHIFT) != dir && (t >> THING_CELL_SHIFT) != ((dir + 1) & 3))))
        t = dungeon_next_thing(t);
    return t;
}

void sensor_launch_missile(uint16_t sensor, const TimelineEvent *e)
{
    int16_t x = e->data[0], y = e->data[1];
    int dir = e->data[2], cell = (dir + 2) & 3;
    uint16_t w = thing_word(sensor, SENSOR_TYPE_DATA), target = thing_word(sensor, SENSOR_TARGET);
    int kind = w & SENSOR_TYPE_MASK;
    uint16_t data = w >> SENSOR_DATA_SHIFT;
    int energy = (uint8_t)(target >> LAUNCH_ENERGY_SHIFT), step = target >> LAUNCH_STEP_SHIFT;
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
    if (single) cell = (rand_bit() + cell) & 3;
    x = (int16_t)(x + dir_dx[dir]);
    y = (int16_t)(y + dir_dy[dir]);
    missile_from_sensor = 1;
    projectile_create(first, x, y, cell, dir, energy, LAUNCH_HIT_CHANCE, step);
    if (!single) projectile_create(second, x, y, (cell + 1) & 3, dir, energy, LAUNCH_HIT_CHANCE, step);
    missile_from_sensor = 0;
}

void champ_launch_projectile(int champ, uint16_t payload, int energy, int attack, int step)
{
    const Champion *c = champion(champ);
    int dir = c->dir;
    int cell = (dir + ((((c->cell - dir) + 1) & 2) >> 1)) & 3;
    projectile_create(payload, game.party_x, game.party_y, cell, dir, (uint8_t)min16((int16_t)energy, UINT8_MAX),
                      (uint8_t)min16((int16_t)attack, UINT8_MAX), (uint8_t)min16((int16_t)step, UINT8_MAX));
    game.throw_cooldown = 4;
    game.throw_dir = (uint16_t)dir;
}
