/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Magic: runes, spell casting, magic light, party auras, and missile spells. */
#include "magic.h"
#include <stdbool.h>
#include <stddef.h>
#include "action.h"
#include "ai.h"
#include "creature.h"
#include "dungeon.h"
#include "game.h"
#include "gdat.h"
#include "hooks.h"
#include "ids.h"
#include "item.h"
#include "le.h"
#include "move.h"
#include "num.h"
#include "projectile.h"
#include "random.h"
#include "sensor.h"
#include "weather.h"

/* The size of the spell table. */
#define SPELL_COUNT 34

/* Masks of the effect word of a spell. */
#define SPELL_KIND_MASK  0x0f
#define SPELL_TYPE_MASK  0x3f
#define SPELL_DELAY_MASK 0x3f

/* The rune key of a spell: the runes after the power rune. */
#define SPELL_RUNES_MASK 0x00ffffff

/* Shifts of the effect word and the rune key of a spell. */
#define SPELL_TYPE_SHIFT       4
#define SPELL_DELAY_SHIFT      10
#define SPELL_POWER_RUNE_SHIFT 24
#define RUNE_KEY_SHIFT         8 /* each rune takes one byte of the spell key */

/* Kinds of spells. */
#define SPELL_POTION  0x01
#define SPELL_MISSILE 0x02
#define SPELL_OTHER   0x03
#define SPELL_SUMMON  0x04

/* Types of the SPELL_OTHER spells. */
#define OTHER_LIGHT          0x00
#define OTHER_DARKNESS       0x01
#define OTHER_SPELL_SHIELD   0x02
#define OTHER_INVISIBILITY   0x03
#define OTHER_PARTY_SHIELD   0x04
#define OTHER_TORCH          0x05
#define OTHER_AURA_WISDOM    0x06
#define OTHER_AURA_DEXTERITY 0x07
#define OTHER_FIRE_SHIELD    0x08
#define OTHER_AURA_VITALITY  0x09
#define OTHER_AURA_STRENGTH  0x0a
#define OTHER_HASTE          0x0b
#define OTHER_EXPLOSION      0x0e
#define OTHER_CREATE_ITEM    0x0f

/* Spell types and ids that the rules single out. */
#define MISSILE_DOUBLE_SKILL   0x04 /* this missile spell counts the skill twice */
#define SUMMON_RECAST_VANISHES 0x35 /* a cast of this summon makes a summoned one vanish instead */

/* Rune glyphs. */
#define RUNE_FIRST      0x60 /* the glyph of the first rune; 6 runes per set */
#define POWER_RUNE_BASE 0x5f /* power = power rune - this */

/* Rune counts. */
#define RUNES_PER_SET           6
#define RUNE_SETS               4 /* the power runes, then the three runes after them */
#define POWER_RUNE_FACTOR_SHIFT 3 /* the mana factor of a power rune is in eighths */

/* The cast roll. */
#define WISDOM_BONUS      15
#define WISDOM_CHANCE_MAX 115

/* Masks of the random rolls of a cast. */
#define CAST_ROLL_MASK       0x7f
#define EXPERIENCE_ROLL_MASK 0x07
#define POTION_ROLL_MASK     0x0f

/* Potion and experience amounts. */
#define POTION_POWER_PER_POWER    40
#define SPELL_DELAY_POWER_BASE    18 /* the action delay: factor * (power + 18) / 24 */
#define SPELL_DELAY_DIVISOR       24
#define EXPERIENCE_PER_DIFFICULTY 8
#define EXPERIENCE_PER_LEVEL      16

/* The thing of a missile spell type, and the explosion that a failed summon makes. */
#define MISSILE_THING_BASE  0xff80
#define EXPLOSION_POOF      (MISSILE_THING_BASE + 0x28) /* the summon found no room */
#define EXPLOSION_ON_SQUARE 0x00ff
#define SUMMON_NO_LINK      0xffff

/* Missile and summon amounts. */
#define MISSILE_POWER_MIN        21
#define MISSILE_POWER_MAX        255
#define MISSILE_ATTACK           90
#define MISSILE_STEP_MAX         10
#define EXPLOSION_POOF_POWER     110
#define MISSILE_STEP_MANA_SHIFT  5 /* each 32 points of maximum mana make the missile fly one step farther */
#define MISSILE_STEP_BONUS_MAX   6
#define MISSILE_POWER_PER_STEP   4 /* a weak missile gets this power more and flies power / 4 steps */
#define MISSILE_POWER_STEP_SHIFT 2
#define MISSILE_SKILL_BASE       4 /* the missile strength: (skill * 2 + 4) * (power + 2) */
#define MISSILE_POWER_BASE       2
#define SUMMON_LEVEL_DIVISOR     6 /* the summon level: (random 0 to 3 + skill * 2) * power / 6 */

/* Light, aura and haste amounts of the other spells. */
#define INVISIBILITY_TICKS_SHIFT  3
#define LIGHT_TICKS_FACTOR        9
#define AURA_COST                 4
#define AURA_BIG                  50 /* a champion with a bigger aura makes a new aura a quarter */
#define AURA_SHIFT                5
#define AURA_BIG_SHIFT            2
#define LIGHT_EVENT_STEP          8 /* game ticks between two steps of a fading light */
#define LIGHT_POWER_MIN           32
#define LIGHT_POWER_MAX           256
#define LIGHT_STRENGTH_MIN        8    /* power / 8, at least this */
#define DARKNESS_SIGN             (-2) /* darkness takes twice the light of its level */
#define TORCH_TICKS               2000 /* the base duration of the light spells */
#define LIGHT_TICKS               10000
#define LIGHT_POWER_SHIFT         3  /* the light strength is power / 8 */
#define DARKNESS_TICKS            16 /* the duration of darkness: this per strength above the minimum, plus this */
#define TORCH_TICKS_STRENGTH_BASE 3
#define TORCH_TICKS_PER_STRENGTH  128
#define TORCH_LEVEL_SHIFT         2 /* the torch level is a quarter of the strength, plus 1 */
#define LIGHT_TICKS_PER_STRENGTH  512
#define OTHER_STEPS_PER_POWER     4
#define AURA_STRONG_BASE          100 /* the two strong auras start at this value */
#define AURA_STEPS_BASE           3
#define HASTE_PER_STEP            8
#define FAILURE_DELAY_BASE        8
#define FLASK_DELAY_MIN           3

/* The party of an aura cast, and the cast result mask of the base skill. */
#define AURA_PARTY      0x0f
#define CAST_SKILL_MASK 0x0f /* the base skill in a cast result */

/* The item that the item spell creates: GDAT value (0x0d, 0x0f, 0x0b, 0x42). */
#define SPELL_ITEM_INDEX 0x0f
#define SPELL_ITEM_KEY   0x42

/* Global variables that count the failed casts; the cast failure picture GDAT (1, 5, 0x0b). */
#define GLOBAL_FLASK_MISSING 0x44
#define GLOBAL_SPELL_FAILED  0x45
#define GLOBAL_SPELL_UNKNOWN 0x46

/* The port name of the spells of a kind and type. */
typedef struct {
    uint8_t kind, type;
    const char *name;
} SpellName;

/* The spells of SKULL.EXE. */
static const Spell spells[SPELL_COUNT] = {
    {0x00686f76, 4, 0x11, 0x2c03},
    {0x006a6f77, 1, 0x0f, 0x1813},
    {0x00666f74, 4, 0x0f, 0x3823},
    {0x00686d77, 3, 0x11, 0x5833},
    {0x00666f00, 2, 0x0f, 0x3c43},
    {0x00690000, 1, 0x10, 0x1c53},
    {0x00686d74, 2, 0x02, 0x3863},
    {0x00686d73, 2, 0x02, 0x3873},
    {0x00697075, 4, 0x0f, 0x3883},
    {0x00686d75, 2, 0x02, 0x3893},
    {0x00686d72, 2, 0x02, 0x38a3},
    {0x00686f73, 4, 0x03, 0x78b3},
    {0x006b7073, 3, 0x02, 0x78e3},
    {0x00666d00, 0, 0x03, 0x04f3},
    {0x00686c00, 3, 0x13, 0x4072},
    {0x00686e76, 4, 0x11, 0x3c22},
    {0x00696f00, 3, 0x10, 0x5402},
    {0x00697072, 4, 0x0d, 0x1c71},
    {0x006a6d00, 1, 0x12, 0x2832},
    {0x006a6c00, 1, 0x13, 0x2062},
    {0x006b0000, 1, 0x11, 0x1c42},
    {0x00667000, 2, 0x0f, 0x30c1},
    {0x00660000, 2, 0x0d, 0x1cb1},
    {0x00667074, 4, 0x0d, 0x1c81},
    {0x00667075, 4, 0x0d, 0x1c91},
    {0x00670000, 1, 0x0d, 0x40e1},
    {0x00677000, 1, 0x0d, 0x34a1},
    {0x00687073, 4, 0x0d, 0x1c61},
    {0x006b7076, 3, 0x02, 0x80d1},
    {0x006b6d72, 6, 0x03, 0x7b14},
    {0x006b6d75, 4, 0x0f, 0x3f44},
    {0x006b6d73, 5, 0x02, 0x3354},
    {0x00686e72, 2, 0x03, 0x5892},
    {0x00686e73, 2, 0x03, 0x58a2},
};
/* The mana of a rune by rune set (6 runes each); the power runes of set 0 cost the plain value. */
static const uint8_t rune_mana[RUNE_SETS * RUNES_PER_SET] = {1, 2, 3, 4, 5, 6, 2, 3, 4, 5, 6, 7,
                                                             4, 5, 6, 7, 7, 9, 2, 2, 3, 4, 6, 7};
/* The mana factor (in eighths) of each power rune for the runes after it. */
static const uint8_t power_rune_mana_factor[RUNES_PER_SET] = {8, 12, 16, 20, 24, 28};
/* The port names of the spells by kind and type: a potion is named by its drink effect, a missile by its explosion,
   a summon by its minion. */
static const SpellName spell_names[] = {
    {SPELL_POTION,  POTION_DEXTERITY,                            "DEXTERITY POTION" },
    {SPELL_POTION,  POTION_STRENGTH,                             "STRENGTH POTION"  },
    {SPELL_POTION,  POTION_WISDOM,                               "WISDOM POTION"    },
    {SPELL_POTION,  POTION_VITALITY,                             "VITALITY POTION"  },
    {SPELL_POTION,  POTION_ANTIDOTE,                             "ANTIDOTE"         },
    {SPELL_POTION,  POTION_STAMINA,                              "STAMINA POTION"   },
    {SPELL_POTION,  POTION_DEFENSE,                              "SHIELD POTION"    },
    {SPELL_POTION,  POTION_MANA,                                 "MANA POTION"      },
    {SPELL_POTION,  POTION_HEALTH,                               "HEALTH POTION"    },
    {SPELL_MISSILE, SPELL_FIREBALL - SPELL_THING_FIRST,          "FIREBALL"         },
    {SPELL_MISSILE, SPELL_LIGHTNING - SPELL_THING_FIRST,         "LIGHTNING BOLT"   },
    {SPELL_MISSILE, SPELL_HARM_NON_MATERIAL - SPELL_THING_FIRST, "HARM NON-MATERIAL"},
    {SPELL_MISSILE, SPELL_DOOR_TOGGLE - SPELL_THING_FIRST,       "OPEN DOOR"        },
    {SPELL_MISSILE, SPELL_POISON_BOLT - SPELL_THING_FIRST,       "POISON BOLT"      },
    {SPELL_MISSILE, SPELL_POISON_CLOUD - SPELL_THING_FIRST,      "POISON CLOUD"     },
    {SPELL_MISSILE, SPELL_PUSH - SPELL_THING_FIRST,              "PUSH"             },
    {SPELL_MISSILE, SPELL_PULL - SPELL_THING_FIRST,              "PULL"             },
    {SPELL_OTHER,   OTHER_LIGHT,                                 "LIGHT"            },
    {SPELL_OTHER,   OTHER_DARKNESS,                              "DARKNESS"         },
    {SPELL_OTHER,   OTHER_SPELL_SHIELD,                          "SPELL SHIELD"     },
    {SPELL_OTHER,   OTHER_INVISIBILITY,                          "INVISIBILITY"     },
    {SPELL_OTHER,   OTHER_PARTY_SHIELD,                          "PARTY SHIELD"     },
    {SPELL_OTHER,   OTHER_TORCH,                                 "MAGIC TORCH"      },
    {SPELL_OTHER,   OTHER_AURA_WISDOM,                           "AURA OF WISDOM"   },
    {SPELL_OTHER,   OTHER_AURA_DEXTERITY,                        "AURA OF DEXTERITY"},
    {SPELL_OTHER,   OTHER_FIRE_SHIELD,                           "FIRE SHIELD"      },
    {SPELL_OTHER,   OTHER_AURA_VITALITY,                         "AURA OF VITALITY" },
    {SPELL_OTHER,   OTHER_AURA_STRENGTH,                         "AURA OF STRENGTH" },
    {SPELL_OTHER,   OTHER_HASTE,                                 "HASTE"            },
    {SPELL_OTHER,   OTHER_EXPLOSION,                             "MISSILE WALL"     },
    {SPELL_OTHER,   OTHER_CREATE_ITEM,                           "YA EW ITEM"       },
    {SPELL_SUMMON,  MINION_SCOUT,                                "SCOUT MINION"     },
    {SPELL_SUMMON,  MINION_CARRIER,                              "CARRIER MINION"   },
    {SPELL_SUMMON,  MINION_ARMY,                                 "ARMY MINION"      },
};
/* The rune names by rune: the power runes, then the three rune sets. */
static const char *const rune_names[RUNE_SETS * RUNES_PER_SET] = {
    "lo",  "um", "on",   "ee", "pal", "mon", "ya", "vi",  "oh",   "ful",  "des", "zo",
    "ven", "ew", "kath", "ir", "bro", "gor", "ku", "ros", "dain", "neta", "ra",  "sar",
};

/* The kind of a spell (SPELL_POTION, SPELL_MISSILE, SPELL_OTHER, SPELL_SUMMON). */
static int spell_kind(const Spell *spell)
{
    return spell->effect & SPELL_KIND_MASK;
}

/* The type of a spell inside its kind. */
static int spell_type(const Spell *spell)
{
    return spell->effect >> SPELL_TYPE_SHIFT & SPELL_TYPE_MASK;
}

/* Adds delta to the light of the party spells. */
static void add_spell_light(int delta)
{
    save_set_magic_light((int16_t)(save_magic_light() + delta));
}

/* An event of the party map at game time + delay. */
static TimelineEvent party_event(int type, uint32_t delay)
{
    TimelineEvent e = {0};
    e.time_map = timeline_time_map(g_game.game_time + delay, g_game.party_map);
    e.type = (uint8_t)type;
    return e;
}

static uint16_t read_event_word(const TimelineEvent *e)
{
    return le16(e->data);
}
static void set_event_word(TimelineEvent *e, uint16_t v)
{
    put_le16(e->data, v);
}

/* The runes of a champion with the byte after them: the end mark of 4 runes is unknown_26[0], as in the original. */
static uint8_t *runes_of(Champion *c)
{
    return (uint8_t *)c + offsetof(Champion, runes);
}

const Spell *magic_find_spell(const uint8_t *runes)
{
    if (!runes[1])
        return NULL;
    uint32_t key = 0;
    for (int shift = SPELL_POWER_RUNE_SHIFT; shift >= 0; shift -= RUNE_KEY_SHIFT) {
        key |= (uint32_t)*runes++ << shift;
        if (!*runes)
            break;
    }
    for (int i = 0; i < SPELL_COUNT; i++) {
        const Spell *s = &spells[i];
        if (s->runes >> SPELL_POWER_RUNE_SHIFT ? key == s->runes : (key & SPELL_RUNES_MASK) == s->runes)
            return s;
    }
    return NULL;
}

const Spell *magic_get_spell(int index)
{
    return index >= 0 && index < SPELL_COUNT ? &spells[index] : NULL;
}

uint8_t magic_get_spell_rune(const Spell *spell, int step)
{
    return (uint8_t)(spell->runes >> (SPELL_POWER_RUNE_SHIFT - step * RUNE_KEY_SHIFT)); /* the byte of the step */
}

const char *magic_get_spell_name(const Spell *spell)
{
    for (size_t i = 0; i < sizeof spell_names / sizeof *spell_names; i++)
        if (spell_names[i].kind == spell_kind(spell) && spell_names[i].type == spell_type(spell))
            return spell_names[i].name;
    return "?";
}

const char *magic_get_rune_name(int rune)
{
    return rune >= 0 && rune < RUNE_SETS * RUNES_PER_SET ? rune_names[rune] : NULL;
}

void magic_add_light(int kind, int power)
{
    int level =
        max_int(LIGHT_STRENGTH_MIN,
                (int16_t)clamp_int(LIGHT_POWER_MIN, (int16_t)(power + 1), LIGHT_POWER_MAX) >> LIGHT_POWER_SHIFT);
    int sign = 1;
    uint16_t ticks = 0; /* the callers use only the three kinds; the original leaves the delay of another kind unset */
    if (kind == LIGHT_DARKNESS) {
        ticks = (uint16_t)((level - LIGHT_STRENGTH_MIN) * DARKNESS_TICKS + DARKNESS_TICKS);
        sign = DARKNESS_SIGN;
        level = (level >> 1) - 1;
    } else if (kind == LIGHT_TORCH) {
        ticks = (uint16_t)((level - TORCH_TICKS_STRENGTH_BASE) * TORCH_TICKS_PER_STRENGTH + TORCH_TICKS);
        level = (level >> TORCH_LEVEL_SHIFT) + 1;
    } else if (kind == LIGHT_LIGHT) {
        ticks = (uint16_t)((level - LIGHT_STRENGTH_MIN) * LIGHT_TICKS_PER_STRENGTH + LIGHT_TICKS);
        level = (level >> 1) - 1;
    }
    TimelineEvent e = party_event(EVENT_MAGIC_LIGHT, ticks);
    set_event_word(&e, (uint16_t)(kind == LIGHT_DARKNESS ? level : -level));
    timeline_add_event(&e);
    add_spell_light(g_light_power[level] * sign);
    light_update_party_light();
}

void magic_event_light(TimelineEvent *e)
{
    int16_t level = (int16_t)read_event_word(e);
    if (!level)
        return;
    bool fading_light = level < 0; /* light events count up from minus the level, darkness events down */
    if (fading_light)
        level = (int16_t)-level;
    int16_t next = (int16_t)(level - 1);
    int delta = g_light_power[level] - g_light_power[next];
    if (fading_light) {
        delta = -delta;
        next = (int16_t)-next;
    } else {
        delta *= 2;
    }
    add_spell_light(delta);
    if (!next)
        return;
    TimelineEvent fade = party_event(EVENT_MAGIC_LIGHT, LIGHT_EVENT_STEP);
    set_event_word(&fade, (uint16_t)next);
    timeline_add_event(&fade);
}

void magic_add_party_aura(int champion_mask, int type, int value, uint16_t duration)
{
    bool big = false;
    for (int i = 0; i < CHAMPION_MAX; i++) {
        Champion *c = champion(i);
        int bit = 1 << i;
        if (champion_mask & bit) {
            if (!c->health)
                champion_mask &= ~bit;
            if (type != c->aura || !c->health) {
                /* a new aura type: the old aura and its events end */
                c->aura_value = 0;
                /* the cleanup takes the whole champion_mask off the events, as in the original */
                for (int k = 0; k < g_timeline.used_end; k++) {
                    TimelineEvent *e = &g_timeline.events[k];
                    if (e->type != EVENT_AURA || !(e->priority & champion_mask))
                        continue;
                    if (!(e->priority & ~champion_mask))
                        timeline_delete_event((uint16_t)k);
                    else
                        e->priority &= (uint8_t)~champion_mask;
                }
            }
        }
        if (c->aura_value > AURA_BIG)
            big = true;
    }
    if (big)
        value = (int16_t)(value >> AURA_BIG_SHIFT);
    for (int i = 0; i < g_game.champion_count; i++) {
        if (!(champion_mask & 1 << i))
            continue;
        Champion *c = champion(i);
        c->aura = (uint8_t)type;
        c->aura_value = (int16_t)(c->aura_value + value);
    }
    TimelineEvent e = party_event(EVENT_AURA, duration);
    e.priority = (uint8_t)champion_mask;
    set_event_word(&e, (uint16_t)value);
    timeline_add_event(&e);
}

bool magic_cast_party_aura(Champion *c, int type, int power, bool pay)
{
    bool paid = true;
    if (pay) {
        if (!c->mana)
            return false;
        if (c->mana < AURA_COST) {
            power >>= 1;
            c->mana = 0;
            paid = false;
        } else {
            c->mana -= AURA_COST;
        }
    }
    magic_add_party_aura(AURA_PARTY, type, (int16_t)(power >> AURA_SHIFT), power);
    return paid;
}

bool magic_cast_projectile_spell(int champ, uint16_t thing, int power, int cost)
{
    Champion *c = champion(champ);
    if (c->mana < cost)
        return false;
    c->mana = (int16_t)(c->mana - cost);
    c->redraw |= REDRAW_BARS;
    int step = MISSILE_STEP_MAX - min_int(MISSILE_STEP_BONUS_MAX, c->max_mana >> MISSILE_STEP_MANA_SHIFT);
    if ((int16_t)power < (int16_t)step * MISSILE_POWER_PER_STEP) {
        power += MISSILE_POWER_PER_STEP;
        step = (int16_t)power >> MISSILE_POWER_STEP_SHIFT;
    }
    projectile_launch_from_champion((int)(c - champion(0)), thing, (uint16_t)power, MISSILE_ATTACK, (uint16_t)step);
    return true;
}

/* A summon spell: a creature group of type near the party, or a summoned creature that vanishes. */
static void cast_summon(int type, int skill, int power)
{
    if (type == SUMMON_RECAST_VANISHES) {
        for (int i = 0; i < dungeon_thing_count(THING_GROUP); i++) {
            uint16_t group = thing_make(THING_GROUP, i);
            if (thing_word(group, THING_NEXT) == THING_NONE || group_creature(group) != type ||
                group_active(group) == GROUP_NOT_ACTIVE)
                continue;
            ActiveGroup *a = &g_active_groups[group_active(group)];
            if (a->action != CREATURE_ACTION_VANISH) {
                a->next_action = CREATURE_ACTION_VANISH;
                return;
            }
        }
    }
    int dir = g_game.party_dir;
    int level = (uint16_t)((rand_2bits() + skill * 2) * power / SUMMON_LEVEL_DIVISOR);
    CreatureSummon summon = {.creature_type = type,
                             .level = level,
                             .dir = dir_opposite(dir),
                             .x = g_game.party_x,
                             .y = g_game.party_y,
                             .map = g_game.party_map,
                             .link = SUMMON_NO_LINK,
                             .search_dir = (int8_t)dir};
    if (creature_summon_group(summon) == THING_NONE)
        explosion_create(EXPLOSION_POOF, EXPLOSION_POOF_POWER, g_game.party_x, g_game.party_y, EXPLOSION_ON_SQUARE);
}

/* The spell that the item spell creates goes into the leader hand, or onto the party square. */
static void cast_create_item(void)
{
    uint16_t thing = item_create_by_index(
        gdat_entry_index(gdat_current(), CAT_EXPLOSION, SPELL_ITEM_INDEX, GDAT_TYPE_VALUE, SPELL_ITEM_KEY));
    if (g_game.leader_hand == THING_NONE) {
        champ_put_in_leader_hand(thing, 1);
        return;
    }
    int cell = dir_turn(g_game.party_dir, rand_bit());
    if (thing == THING_NONE)
        return; /* fix: the original moves an invalid thing when no record is free */
    move_thing(thing_with_cell(thing, cell), -1, 0, g_game.party_x, g_game.party_y);
}

/* The spells that are neither potions, missiles, nor summons. */
static void cast_other(Champion *c, int type, int skill, int power)
{
    /* the aura type of each aura spell */
    static const int8_t aura_types[OTHER_CREATE_ITEM + 1] = {
        [OTHER_FIRE_SHIELD] = AURA_FIRE_SHIELD,  [OTHER_SPELL_SHIELD] = AURA_SPELL_SHIELD,
        [OTHER_PARTY_SHIELD] = AURA_DEFENSE,     [OTHER_AURA_STRENGTH] = AURA_STRENGTH,
        [OTHER_AURA_DEXTERITY] = AURA_DEXTERITY, [OTHER_AURA_WISDOM] = AURA_WISDOM,
        [OTHER_AURA_VITALITY] = AURA_VITALITY};
    int steps = (power + 1) * OTHER_STEPS_PER_POWER;
    switch (type) {
    case OTHER_LIGHT:
        magic_add_light(LIGHT_LIGHT, (int16_t)(steps * LIGHT_TICKS_FACTOR));
        return;
    case OTHER_DARKNESS:
        magic_add_light(LIGHT_DARKNESS, (int16_t)(steps * LIGHT_TICKS_FACTOR));
        return;
    case OTHER_TORCH:
        magic_add_light(LIGHT_TORCH, (int16_t)(steps * LIGHT_TICKS_FACTOR));
        return;
    case OTHER_INVISIBILITY: {
        if (!g_game.magic_light[MAGIC_INVISIBILITY]++ && g_ui.inventory)
            champion(g_ui.inventory - 1)->redraw |= REDRAW_STATUS_BOX;
        TimelineEvent e = party_event(EVENT_INVISIBILITY, (uint16_t)(steps << INVISIBILITY_TICKS_SHIFT));
        timeline_add_event(&e);
        return;
    }
    case OTHER_FIRE_SHIELD:
    case OTHER_SPELL_SHIELD:
        magic_cast_party_aura(c, aura_types[type], (uint16_t)(steps * steps + AURA_STRONG_BASE), 0);
        return;
    case OTHER_PARTY_SHIELD:
        magic_cast_party_aura(c, aura_types[type], (uint16_t)(steps * steps), 0);
        return;
    case OTHER_AURA_STRENGTH:
    case OTHER_AURA_DEXTERITY:
    case OTHER_AURA_WISDOM:
    case OTHER_AURA_VITALITY:
        magic_cast_party_aura(c, aura_types[type], (uint16_t)((steps + AURA_STEPS_BASE) * (steps + AURA_STEPS_BASE)),
                              0);
        return;
    case OTHER_HASTE:
        g_game.magic_light[MAGIC_HASTE] =
            (uint8_t)min_int((int16_t)(steps * HASTE_PER_STEP + g_game.magic_light[MAGIC_HASTE]), HASTE_MAX);
        return;
    case OTHER_EXPLOSION: {
        int strength =
            clamp_int(MISSILE_POWER_MIN, (int16_t)((skill * 2 + MISSILE_SKILL_BASE) * (power + MISSILE_POWER_BASE)),
                      MISSILE_POWER_MAX);
        explosion_create(MISSILE_THING_BASE + OTHER_EXPLOSION, strength, g_game.party_x, g_game.party_y,
                         EXPLOSION_ON_SQUARE);
        return;
    }
    case OTHER_CREATE_ITEM:
        cast_create_item();
        return;
    }
}

/* The cast result of a failure kind: the kind with the base skill of the spell. */
static int cast_failure(int kind, const Spell *spell)
{
    int skill = spell->skill;
    return kind | champ_base_skill(skill);
}

/* The hand (1, then 0) that holds an empty flask, -1 none. */
static int find_flask_in_hands(const Champion *c)
{
    for (int hand = SLOT_HAND_COUNT - 1; hand >= 0; hand--) {
        uint16_t t = c->slots[hand];
        if (thing_type(t) == THING_POTION && item_gdat_index(t) == EMPTY_FLASK)
            return hand;
    }
    return -1;
}

/* The action delay of a spell cast with power. */
static int spell_delay(const Spell *spell, int power)
{
    return (spell->effect >> SPELL_DELAY_SHIFT & SPELL_DELAY_MASK) * (power + SPELL_DELAY_POWER_BASE) /
           SPELL_DELAY_DIVISOR;
}

/* The effect of a spell that champion champ casts with power and skill: a potion in a flask, a missile, another
   spell, or a summon. Returns the cast result: 0, or the failure without an empty flask for a potion. */
static int cast_effect(int champ, const Spell *spell, int power, int skill)
{
    Champion *c = champion(champ);
    int type = spell_type(spell);
    switch (spell_kind(spell)) {
    case SPELL_POTION: {
        int hand = find_flask_in_hands(c);
        if (hand < 0)
            return cast_failure(CAST_NEEDS_FLASK, spell);
        uint16_t flask = champ_remove_object_from_slot(champ, hand);
        item_set_gdat_index(flask, type);
        potion_set_power(flask, (unsigned)(power * POTION_POWER_PER_POWER + (int)(rand_next() & POTION_ROLL_MASK)));
        champ_add_object_to_slot(champ, flask, hand);
        break;
    }
    case SPELL_MISSILE: {
        int s = type == MISSILE_DOUBLE_SKILL ? skill * 2 : skill;
        int strength =
            clamp_int(MISSILE_POWER_MIN, (int16_t)((s * 2 + MISSILE_SKILL_BASE) * (power + MISSILE_POWER_BASE)),
                      MISSILE_POWER_MAX);
        magic_cast_projectile_spell(champ, (uint16_t)(MISSILE_THING_BASE + type), strength, 0);
        break;
    }
    case SPELL_OTHER:
        cast_other(c, type, skill, power);
        break;
    case SPELL_SUMMON:
        cast_summon(type, skill, power);
        break;
    }
    return 0;
}

int magic_cast_spell(int champ, const Spell *spell, int power)
{
    Champion *c = champion(champ);
    int delay = spell_delay(spell, power);
    int level = power + spell->difficulty;
    uint16_t experience =
        (uint16_t)(spell->difficulty * (power - 1) * EXPERIENCE_PER_DIFFICULTY + level * EXPERIENCE_PER_LEVEL +
                   (int)(rand_next() & EXPERIENCE_ROLL_MASK) + level * level);
    int skill = champ_get_skill_level(champ, spell->skill, 1);
    for (int16_t tries = (int16_t)(level - skill); tries > 0; tries--) {
        int roll = (int)(rand_next() & CAST_ROLL_MASK);
        if (min_int(champ_get_stat(c, STAT_WISDOM, 0) + WISDOM_BONUS, WISDOM_CHANCE_MAX) < roll) {
            champ_add_experience(champ, spell->skill, experience >> ((level - skill) & BIT_INDEX_MASK));
            return cast_failure(CAST_FAILED, spell);
        }
    }
    int result = cast_effect(champ, spell, power, skill);
    if (result)
        return result;
    if ((int16_t)delay) {
        champ_add_experience(champ, spell->skill, experience);
        champ_add_action_delay(champ, (uint16_t)delay, ACTION_SPELL);
    }
    return 0;
}

int magic_cast_cheat_spell(int champ, const Spell *spell)
{
    if (!champion(champ)->health)
        return cast_failure(CAST_FAILED, spell);
    uint8_t power_rune = magic_get_spell_rune(spell, 0);
    int power = power_rune ? power_rune - POWER_RUNE_BASE : POWER_MON;
    int result = cast_effect(champ, spell, power, champ_get_skill_level(champ, spell->skill, 1));
    int delay = spell_delay(spell, power);
    if (!result && (int16_t)delay)
        champ_add_action_delay(champ, (uint16_t)delay, ACTION_SPELL);
    return result;
}

/* Shows why a cast failed (CAST_*) and counts it in the global variables. */
static void report_cast_failure(int result)
{
    int min_delay = 1, var;
    switch (result & CAST_KIND_MASK) {
    case CAST_FAILED:
        g_action.result =
            (int16_t)((result & CAST_SKILL_MASK) == SKILL_WIZARD ? RESULT_WIZARD_FAILED : RESULT_PRIEST_FAILED);
        var = GLOBAL_SPELL_FAILED;
        break;
    case CAST_UNKNOWN_SPELL:
        g_action.result = RESULT_UNKNOWN_SPELL;
        var = GLOBAL_SPELL_UNKNOWN;
        break;
    case CAST_NEEDS_FLASK:
        g_hooks.draw_interface_image(CAT_INTERFACE, UI_INDEX_SPELL, GFX_FLASK_PICTURE, ZONE_PANEL);
        var = GLOBAL_FLASK_MISSING;
        min_delay = FLASK_DELAY_MIN;
        break;
    default:
        return;
    }
    int count = dun_modify_global_var(var, 1, GLOBAL_VAR_ADD);
    g_action.redraw_delay = (int16_t)max_int(min_delay, (int16_t)(FAILURE_DELAY_BASE - count));
}

bool magic_cast_entered_runes(void)
{
    int champ = g_action.champion - 1;
    Champion *c = champion(champ);
    uint8_t *runes = runes_of(c);
    const Spell *spell = magic_find_spell(runes);
    int result = spell ? magic_cast_spell(champ, spell, (int16_t)(runes[0] - POWER_RUNE_BASE)) : CAST_UNKNOWN_SPELL;
    if (result)
        report_cast_failure(result);
    if ((result & CAST_KIND_MASK) != CAST_NEEDS_FLASK) {
        runes[0] = 0;
        c->rune_count = 0;
        action_area_close();
    }
    return result != 0;
}

uint16_t magic_rune_mana_cost(int champ, int rune)
{
    Champion *c = champion(champ);
    uint8_t *runes = runes_of(c);
    int n = c->rune_count;
    uint16_t mana = rune_mana[n * RUNES_PER_SET + rune];
    if (n)
        mana = (uint16_t)(mana * power_rune_mana_factor[runes[0] - RUNE_FIRST] >> POWER_RUNE_FACTOR_SHIFT);
    return mana;
}

void magic_add_rune(int rune)
{
    Champion *c = champion(g_action.champion - 1);
    uint8_t *runes = runes_of(c);
    int n = c->rune_count;
    uint16_t mana = magic_rune_mana_cost(g_action.champion - 1, rune);
    bool added = mana <= c->mana;
    if (added) {
        c->mana = (int16_t)(c->mana - mana);
        c->redraw |= REDRAW_BARS;
        runes[n] = (uint8_t)(rune + n * RUNES_PER_SET + RUNE_FIRST);
        c->rune_count = (uint8_t)(n + 1);
        runes[n + 1] = 0;
    }
    g_action.redraw_delay = 1;
    g_hooks.update_action_area(0);
    g_hooks.refresh_input_mode();
    if (added)
        g_hooks.draw_champion_state(g_action.champion - 1);
}

void magic_remove_last_rune(void)
{
    Champion *c = champion(g_action.champion - 1);
    c->rune_count--;
    runes_of(c)[c->rune_count] = 0;
    g_action.redraw_delay = 1;
    g_hooks.update_action_area(0);
    g_hooks.refresh_input_mode();
}
