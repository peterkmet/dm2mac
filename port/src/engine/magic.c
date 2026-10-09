/* Magic: runes, spell casting, magic light, party auras, and missile spells. */
#include <stddef.h>
#include "action.h"
#include "ai.h"
#include "creature.h"
#include "game.h"
#include "gdat.h"
#include "hooks.h"
#include "game_text.h"
#include "dungeon.h"
#include "sound_request.h"
#include "projectile.h"
#include "input.h"
#include "champ_action.h"
#include "inventory.h"
#include "item.h"
#include "le.h"
#include "money.h"
#include "magic.h"
#include "move.h"
#include "random.h"
#include "sensor.h"
#include "ui.h"
#include "weather.h"

#define SPELL_COUNT 34
#define SPELL_KIND_MASK 0x0f
#define SPELL_TYPE_SHIFT 4
#define SPELL_TYPE_MASK 0x3f
#define SPELL_DELAY_SHIFT 10
#define SPELL_DELAY_MASK 0x3f
#define SPELL_POWER_RUNE_SHIFT 24
#define SPELL_RUNES_MASK 0x00ffffff     /* the runes after the power rune */
enum spell_kind { SPELL_POTION = 1, SPELL_MISSILE, SPELL_OTHER, SPELL_SUMMON };
/* Types of the SPELL_OTHER spells. */
enum {
    OTHER_LIGHT, OTHER_DARKNESS, OTHER_AURA_1, OTHER_INVISIBILITY, OTHER_AURA_2, OTHER_TORCH, OTHER_AURA_5,
    OTHER_AURA_4, OTHER_AURA_0, OTHER_AURA_6, OTHER_AURA_3, OTHER_HASTE, OTHER_EXPLOSION = 14, OTHER_CREATE_ITEM
};
#define MISSILE_DOUBLE_SKILL 4          /* this missile spell counts the skill twice */
#define SUMMON_RECAST_VANISHES 0x35     /* a cast of this summon makes a summoned one vanish instead */
#define RUNE_FIRST 0x60                 /* the glyph of the first rune; 6 runes per set */
#define RUNES_PER_SET 6
#define POWER_RUNE_BASE 0x5f            /* power = power rune - this */
#define WISDOM_BONUS 15
#define WISDOM_CHANCE_MAX 115
#define CAST_ROLL_MASK 0x7f
#define EXPERIENCE_ROLL_MASK 7
#define POTION_ROLL_MASK 0x0f
#define POTION_POWER_PER_POWER 40
#define FLASK_EMPTY 0x14                /* GDAT index of the empty flask */
#define MISSILE_THING_BASE 0xff80       /* the thing of a missile spell type */
#define MISSILE_POWER_MIN 21
#define MISSILE_POWER_MAX 255
#define MISSILE_ATTACK 90
#define MISSILE_STEP_MAX 10
#define EXPLOSION_POOF (MISSILE_THING_BASE + 0x28)  /* the summon found no room */
#define EXPLOSION_POOF_POWER 110
#define EXPLOSION_ON_SQUARE 0xff
#define SUMMON_NO_LINK 0xffff
#define INVISIBILITY_TICKS_SHIFT 3
#define LIGHT_TICKS_FACTOR 9
#define AURA_PARTY 0x0f
#define AURA_COST 4
#define AURA_BIG 50                     /* a champion with a bigger aura makes a new aura a quarter */
#define AURA_SHIFT 5
#define LIGHT_EVENT_STEP 8              /* game ticks between two steps of a fading light */
#define LIGHT_POWER_MIN 32
#define LIGHT_POWER_MAX 256
#define LIGHT_STRENGTH_MIN 8            /* power / 8, at least this */
#define DARKNESS_SIGN (-2)              /* darkness takes twice the light of its level */
#define TORCH_TICKS 2000                /* the base duration of the light spells */
#define LIGHT_TICKS 10000
#define SPELL_DELAY_POWER_BASE 18       /* the action delay: factor * (power + 18) / 24 */
#define SPELL_DELAY_DIVISOR 24
#define CAST_KIND_MASK 0xf0
/* The item that the item spell creates: GDAT value (0x0d, 0x0f, 0x0b, 0x42). */
#define SPELL_ITEM_INDEX 0x0f
#define SPELL_ITEM_KEY 0x42
/* Global variables that count the failed casts; the cast failure picture GDAT (1, 5, 0x0b). */
enum { GLOBAL_FLASK_MISSING = 0x44, GLOBAL_SPELL_FAILED = 0x45, GLOBAL_SPELL_UNKNOWN = 0x46 };
#define FLASK_PICTURE 0x0b
#define FAILURE_DELAY_BASE 8
#define FLASK_DELAY_MIN 3
#define MAGIC_LIGHT_LEVELS 16            /* the light spells reach level 15 */
#define RUNE_BITS 8                     /* each rune takes one byte of the spell key */
#define RUNE_SETS 4                     /* the power runes, then the three runes after them */
#define POWER_RUNE_FACTOR_SHIFT 3       /* the mana factor of a power rune is in eighths */
#define LIGHT_POWER_SHIFT 3             /* the light strength is power / 8 */
#define DARKNESS_TICKS 16               /* the duration of darkness: this per strength above the minimum, plus this */
#define TORCH_TICKS_STRENGTH_BASE 3
#define TORCH_TICKS_PER_STRENGTH 128
#define TORCH_LEVEL_SHIFT 2             /* the torch level is a quarter of the strength, plus 1 */
#define LIGHT_TICKS_PER_STRENGTH 512
#define AURA_BIG_SHIFT 2
#define MISSILE_STEP_MANA_SHIFT 5       /* each 32 points of maximum mana make the missile fly one step farther */
#define MISSILE_STEP_BONUS_MAX 6
#define MISSILE_POWER_PER_STEP 4        /* a weak missile gets this power more and flies power / 4 steps */
#define MISSILE_POWER_STEP_SHIFT 2
#define MISSILE_SKILL_BASE 4            /* the missile strength: (skill * 2 + 4) * (power + 2) */
#define MISSILE_POWER_BASE 2
#define SUMMON_LEVEL_DIVISOR 6          /* the summon level: (random 0 to 3 + skill * 2) * power / 6 */
#define OTHER_STEPS_PER_POWER 4
#define AURA_STRONG_BASE 100            /* the two strong auras start at this value */
#define AURA_STEPS_BASE 3
#define HASTE_PER_STEP 8
#define EXPERIENCE_PER_DIFFICULTY 8
#define EXPERIENCE_PER_LEVEL 16
#define CAST_SKILL_MASK 0x0f            /* the base skill in a cast result */

/* The spells of SKULL.EXE. */
static const Spell spells[SPELL_COUNT] = {
    {0x00686f76, 4, 0x11, 0x2c03}, {0x006a6f77, 1, 0x0f, 0x1813}, {0x00666f74, 4, 0x0f, 0x3823}, {0x00686d77, 3, 0x11, 0x5833},
    {0x00666f00, 2, 0x0f, 0x3c43}, {0x00690000, 1, 0x10, 0x1c53}, {0x00686d74, 2, 0x02, 0x3863}, {0x00686d73, 2, 0x02, 0x3873},
    {0x00697075, 4, 0x0f, 0x3883}, {0x00686d75, 2, 0x02, 0x3893}, {0x00686d72, 2, 0x02, 0x38a3}, {0x00686f73, 4, 0x03, 0x78b3},
    {0x006b7073, 3, 0x02, 0x78e3}, {0x00666d00, 0, 0x03, 0x04f3}, {0x00686c00, 3, 0x13, 0x4072}, {0x00686e76, 4, 0x11, 0x3c22},
    {0x00696f00, 3, 0x10, 0x5402}, {0x00697072, 4, 0x0d, 0x1c71}, {0x006a6d00, 1, 0x12, 0x2832}, {0x006a6c00, 1, 0x13, 0x2062},
    {0x006b0000, 1, 0x11, 0x1c42}, {0x00667000, 2, 0x0f, 0x30c1}, {0x00660000, 2, 0x0d, 0x1cb1}, {0x00667074, 4, 0x0d, 0x1c81},
    {0x00667075, 4, 0x0d, 0x1c91}, {0x00670000, 1, 0x0d, 0x40e1}, {0x00677000, 1, 0x0d, 0x34a1}, {0x00687073, 4, 0x0d, 0x1c61},
    {0x006b7076, 3, 0x02, 0x80d1}, {0x006b6d72, 6, 0x03, 0x7b14}, {0x006b6d75, 4, 0x0f, 0x3f44}, {0x006b6d73, 5, 0x02, 0x3354},
    {0x00686e72, 2, 0x03, 0x5892}, {0x00686e73, 2, 0x03, 0x58a2},
};
/* The mana of a rune by rune set (6 runes each); the power runes of set 0 cost the plain value. */
static const uint8_t rune_mana[RUNE_SETS * RUNES_PER_SET] = {
    1, 2, 3, 4, 5, 6, 2, 3, 4, 5, 6, 7, 4, 5, 6, 7, 7, 9, 2, 2, 3, 4, 6, 7
};
/* The mana factor (in eighths) of each power rune for the runes after it. */
static const uint8_t power_rune_mana_factor[RUNES_PER_SET] = {8, 12, 16, 20, 24, 28};
/* The light of each magic light level. */
static const int8_t light_power[MAGIC_LIGHT_LEVELS] = {
    0, 5, 12, 24, 33, 40, 46, 51, 59, 68, 76, 82, 89, 94, 97, 100
};

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int iclamp(int lo, int v, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Adds delta to the light of the party spells. */
static void magic_light_add(int delta) { save_set_magic_light((int16_t)(save_magic_light() + delta)); }

/* An event of the party map at game time + delay. */
static TimelineEvent party_event(int type, uint32_t delay)
{
    TimelineEvent e = {0};
    e.time_map = timeline_time_map(game.game_time + delay, game.party_map);
    e.type = (uint8_t)type;
    return e;
}

static uint16_t event_word(const TimelineEvent *e) { return le16(e->data); }
static void set_event_word(TimelineEvent *e, uint16_t v) { put_le16(e->data, v); }

/* The runes of a champion with the byte after them (the end mark of 4 runes). */
static uint8_t *champion_runes(Champion *c) { return (uint8_t *)c + offsetof(Champion, runes); }

const Spell *magic_find_spell(const uint8_t *runes)
{
    if (!runes[1]) return NULL;
    uint32_t key = 0;
    for (int shift = SPELL_POWER_RUNE_SHIFT; shift >= 0; shift -= RUNE_BITS) {
        key |= (uint32_t)*runes++ << shift;
        if (!*runes) break;
    }
    for (int i = 0; i < SPELL_COUNT; i++) {
        const Spell *s = &spells[i];
        if (s->runes >> SPELL_POWER_RUNE_SHIFT ? key == s->runes : (key & SPELL_RUNES_MASK) == s->runes) return s;
    }
    return NULL;
}

int magic_find_flask_in_hands(const Champion *c)
{
    for (int hand = SLOT_HAND_COUNT - 1; hand >= 0; hand--) {
        uint16_t t = c->slots[hand];
        if (thing_type(t) == THING_POTION && item_gdat_index(t) == FLASK_EMPTY) return hand;
    }
    return -1;
}

void magic_add_light(int kind, int power)
{
    int level = imax(LIGHT_STRENGTH_MIN, (int16_t)iclamp(LIGHT_POWER_MIN, (int16_t)(power + 1), LIGHT_POWER_MAX) >> LIGHT_POWER_SHIFT);
    int sign = 1;
    uint16_t ticks = 0;  /* the callers use only the three kinds; the original leaves the delay of another kind unset */
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
    magic_light_add(light_power[level] * sign);
    light_update_party_light();
}

void timeline_event_magic_light(TimelineEvent *e)
{
    int16_t level = (int16_t)event_word(e);
    if (!level) return;
    int fading_light = level < 0;  /* light events count up from minus the level, darkness events down */
    if (fading_light) level = (int16_t)-level;
    int16_t next = (int16_t)(level - 1);
    int delta = light_power[level] - light_power[next];
    if (fading_light) {
        delta = -delta;
        next = (int16_t)-next;
    } else {
        delta *= 2;
    }
    magic_light_add(delta);
    if (!next) return;
    TimelineEvent fade = party_event(EVENT_MAGIC_LIGHT, LIGHT_EVENT_STEP);
    set_event_word(&fade, (uint16_t)next);
    timeline_add_event(&fade);
}

void magic_add_party_aura(int mask, int type, int16_t value, uint16_t duration)
{
    int big = 0;
    for (int i = 0; i < CHAMPION_MAX; i++) {
        Champion *c = champion(i);
        int bit = 1 << i;
        if (mask & bit) {
            if (!c->health) mask &= ~bit;
            if (type != c->aura || !c->health) {
                /* a new aura type: the old aura and its events end */
                c->aura_value = 0;
                for (int k = 0; k < timeline.used_end; k++) {
                    TimelineEvent *e = &timeline.events[k];
                    if (e->type != EVENT_AURA || !(e->priority & mask)) continue;
                    if (!(e->priority & ~mask)) timeline_delete_event((uint16_t)k);
                    else e->priority &= (uint8_t)~mask;
                }
            }
        }
        if (c->aura_value > AURA_BIG) big = 1;
    }
    if (big) value = (int16_t)(value >> AURA_BIG_SHIFT);
    for (int i = 0; i < game.champion_count; i++) {
        if (!(mask & 1 << i)) continue;
        Champion *c = champion(i);
        c->aura = (uint8_t)type;
        c->aura_value = (int16_t)(c->aura_value + value);
    }
    TimelineEvent e = party_event(EVENT_AURA, duration);
    e.priority = (uint8_t)mask;
    set_event_word(&e, (uint16_t)value);
    timeline_add_event(&e);
}

int magic_cast_party_aura(Champion *c, int type, uint16_t value, int pay)
{
    int paid = 1;
    if (pay) {
        if (!c->mana) return 0;
        if (c->mana < AURA_COST) {
            value >>= 1;
            c->mana = 0;
            paid = 0;
        } else {
            c->mana -= AURA_COST;
        }
    }
    magic_add_party_aura(AURA_PARTY, type, (int16_t)(value >> AURA_SHIFT), value);
    return paid;
}

int magic_cast_projectile_spell(int champ, uint16_t thing, int power, int cost)
{
    Champion *c = champion(champ);
    if (c->mana < cost) return 0;
    c->mana = (int16_t)(c->mana - cost);
    c->redraw |= REDRAW_BARS;
    int step = MISSILE_STEP_MAX - imin(MISSILE_STEP_BONUS_MAX, c->max_mana >> MISSILE_STEP_MANA_SHIFT);
    if ((int16_t)power < (int16_t)step * MISSILE_POWER_PER_STEP) {
        power += MISSILE_POWER_PER_STEP;
        step = (int16_t)power >> MISSILE_POWER_STEP_SHIFT;
    }
    champ_launch_projectile((int)(c - champion(0)), thing, (uint16_t)power, MISSILE_ATTACK, (uint16_t)step);
    return 1;
}

/* A summon spell: a creature group of type near the party, or a summoned creature that vanishes. */
static void cast_summon(int type, int skill, int power)
{
    if (type == SUMMON_RECAST_VANISHES) {
        for (int i = 0; i < dungeon_thing_count(THING_GROUP); i++) {
            uint16_t group = thing_make(THING_GROUP, i);
            if (thing_word(group, THING_NEXT) == THING_NONE || group_creature(group) != type
                || group_active(group) == GROUP_NOT_ACTIVE)
                continue;
            ActiveGroup *a = &active_groups[group_active(group)];
            if (a->action != CREATURE_ACTION_VANISH) {
                a->next_action = CREATURE_ACTION_VANISH;
                return;
            }
        }
    }
    int dir = game.party_dir;
    int level = (uint16_t)((rand_2bits() + skill * 2) * power / SUMMON_LEVEL_DIVISOR);
    if (creature_summon_group(type, level, dir_opposite(dir), game.party_x, game.party_y, game.party_map, SUMMON_NO_LINK,
                              (int8_t)dir) == THING_NONE)
        explosion_create(EXPLOSION_POOF, EXPLOSION_POOF_POWER, game.party_x, game.party_y, EXPLOSION_ON_SQUARE);
}

/* The spell that the item spell creates goes into the leader hand, or onto the party square. */
static void cast_create_item(void)
{
    uint16_t thing = item_create_by_index(gdat_entry_index(gdat_current(), CAT_EXPLOSION, SPELL_ITEM_INDEX, GDAT_TYPE_VALUE,
                                                           SPELL_ITEM_KEY));
    if (game.leader_hand == THING_NONE) {
        champ_put_object_in_leader_hand(thing, 1);
        return;
    }
    int cell = dir_turn(game.party_dir, rand_bit());
    if (thing == THING_NONE) return;  /* fix: the original moves an invalid thing when no record is free */
    move_thing(thing_with_cell(thing, cell), -1, 0, game.party_x, game.party_y);
}

/* The spells that are neither potions, missiles, nor summons. */
static void cast_other(Champion *c, int type, int skill, int power)
{
    static const int8_t aura_types[OTHER_CREATE_ITEM + 1] = {-1, -1, 1, -1, 2, -1, 5, 4, 0, 6, 3};
    int steps = (power + 1) * OTHER_STEPS_PER_POWER;
    switch (type) {
    case OTHER_LIGHT: magic_add_light(LIGHT_LIGHT, (int16_t)(steps * LIGHT_TICKS_FACTOR)); return;
    case OTHER_DARKNESS: magic_add_light(LIGHT_DARKNESS, (int16_t)(steps * LIGHT_TICKS_FACTOR)); return;
    case OTHER_TORCH: magic_add_light(LIGHT_TORCH, (int16_t)(steps * LIGHT_TICKS_FACTOR)); return;
    case OTHER_INVISIBILITY: {
        if (!game.magic_light[MAGIC_INVISIBILITY]++ && ui.inventory) champion(ui.inventory - 1)->redraw |= REDRAW_STATUS_BOX;
        TimelineEvent e = party_event(EVENT_INVISIBILITY, (uint16_t)(steps << INVISIBILITY_TICKS_SHIFT));
        timeline_add_event(&e);
        return;
    }
    case OTHER_AURA_0: case OTHER_AURA_1:
        magic_cast_party_aura(c, aura_types[type], (uint16_t)(steps * steps + AURA_STRONG_BASE), 0);
        return;
    case OTHER_AURA_2:
        magic_cast_party_aura(c, aura_types[type], (uint16_t)(steps * steps), 0);
        return;
    case OTHER_AURA_3: case OTHER_AURA_4: case OTHER_AURA_5: case OTHER_AURA_6:
        magic_cast_party_aura(c, aura_types[type], (uint16_t)((steps + AURA_STEPS_BASE) * (steps + AURA_STEPS_BASE)), 0);
        return;
    case OTHER_HASTE:
        game.magic_light[MAGIC_HASTE] = (uint8_t)imin((int16_t)(steps * HASTE_PER_STEP + game.magic_light[MAGIC_HASTE]), HASTE_MAX);
        return;
    case OTHER_EXPLOSION: {
        int strength = iclamp(MISSILE_POWER_MIN, (int16_t)((skill * 2 + MISSILE_SKILL_BASE) * (power + MISSILE_POWER_BASE)), MISSILE_POWER_MAX);
        explosion_create(MISSILE_THING_BASE + OTHER_EXPLOSION, strength, game.party_x, game.party_y, EXPLOSION_ON_SQUARE);
        return;
    }
    case OTHER_CREATE_ITEM: cast_create_item(); return;
    }
}

/* The cast result of a failure kind: the kind with the base skill of the spell. */
static int cast_failure(int kind, const Spell *spell)
{
    int skill = spell->skill;
    return kind | champ_base_skill(skill);
}

int magic_cast_spell(int champ, const Spell *spell, int power)
{
    Champion *c = champion(champ);
    int kind = spell->effect & SPELL_KIND_MASK, type = spell->effect >> SPELL_TYPE_SHIFT & SPELL_TYPE_MASK;
    int delay = (spell->effect >> SPELL_DELAY_SHIFT & SPELL_DELAY_MASK) * (power + SPELL_DELAY_POWER_BASE) / SPELL_DELAY_DIVISOR;
    int level = power + spell->difficulty;
    uint16_t experience = (uint16_t)(spell->difficulty * (power - 1) * EXPERIENCE_PER_DIFFICULTY + level * EXPERIENCE_PER_LEVEL
                                     + (int)(rand_next() & EXPERIENCE_ROLL_MASK) + level * level);
    int skill = champ_get_skill_level(champ, spell->skill, 1);
    for (int16_t tries = (int16_t)(level - skill); tries > 0; tries--) {
        int roll = (int)(rand_next() & CAST_ROLL_MASK);
        if (imin(champ_get_stat(c, STAT_WISDOM, 0) + WISDOM_BONUS, WISDOM_CHANCE_MAX) < roll) {
            champ_add_experience(champ, spell->skill, experience >> ((level - skill) & SHIFT_COUNT_MASK));
            return cast_failure(CAST_FAILED, spell);
        }
    }
    switch (kind) {
    case SPELL_POTION: {
        int hand = magic_find_flask_in_hands(c);
        if (hand < 0) return cast_failure(CAST_NEEDS_FLASK, spell);
        uint16_t flask = champ_remove_object_from_slot(champ, hand);
        item_set_gdat_index(flask, type);
        potion_set_power(flask, (unsigned)(power * POTION_POWER_PER_POWER + (int)(rand_next() & POTION_ROLL_MASK)));
        champ_add_object_to_slot(champ, flask, hand);
        break;
    }
    case SPELL_MISSILE: {
        int s = type == MISSILE_DOUBLE_SKILL ? skill * 2 : skill;
        int strength = iclamp(MISSILE_POWER_MIN, (int16_t)((s * 2 + MISSILE_SKILL_BASE) * (power + MISSILE_POWER_BASE)), MISSILE_POWER_MAX);
        magic_cast_projectile_spell(champ, (uint16_t)(MISSILE_THING_BASE + type), strength, 0);
        break;
    }
    case SPELL_OTHER: cast_other(c, type, skill, power); break;
    case SPELL_SUMMON: cast_summon(type, skill, power); break;
    }
    if ((int16_t)delay) {
        champ_add_experience(champ, spell->skill, experience);
        champ_add_action_delay(champ, (uint16_t)delay, ACTION_SPELL);
    }
    return 0;
}

void magic_report_cast_failure(int result)
{
    int min_delay = 1, var;
    switch (result & CAST_KIND_MASK) {
    case CAST_FAILED:
        action.result = (int16_t)((result & CAST_SKILL_MASK) == SKILL_WIZARD ? RESULT_WIZARD_FAILED : RESULT_PRIEST_FAILED);
        var = GLOBAL_SPELL_FAILED;
        break;
    case CAST_UNKNOWN_SPELL:
        action.result = RESULT_UNKNOWN_SPELL;
        var = GLOBAL_SPELL_UNKNOWN;
        break;
    case CAST_NEEDS_FLASK:
        g_hooks.draw_interface_image(CAT_INTERFACE, UI_INDEX_SPELL, FLASK_PICTURE, ZONE_PANEL);
        var = GLOBAL_FLASK_MISSING;
        min_delay = FLASK_DELAY_MIN;
        break;
    default:
        return;
    }
    int count = dun_modify_global_var(var, 1, GLOBAL_VAR_ADD);
    action.redraw_delay = (int16_t)imax(min_delay, (int16_t)(FAILURE_DELAY_BASE - count));
}

int magic_cast_entered_runes(void)
{
    int champ = (uint16_t)(action.champion - 1);
    Champion *c = champion(champ);
    uint8_t *runes = champion_runes(c);
    const Spell *spell = magic_find_spell(runes);
    int result = spell ? magic_cast_spell(champ, spell, (int16_t)(runes[0] - POWER_RUNE_BASE)) : CAST_UNKNOWN_SPELL;
    if (result) magic_report_cast_failure(result);
    if ((result & CAST_KIND_MASK) != CAST_NEEDS_FLASK) {
        runes[0] = 0;
        c->rune_count = 0;
        action_area_close();
    }
    return result != 0;
}

void magic_add_rune(int rune)
{
    Champion *c = champion(action.champion - 1);
    uint8_t *runes = champion_runes(c);
    int n = c->rune_count;
    uint16_t mana = rune_mana[n * RUNES_PER_SET + rune];
    if (n) mana = (uint16_t)(mana * power_rune_mana_factor[runes[0] - RUNE_FIRST] >> POWER_RUNE_FACTOR_SHIFT);
    int added = mana <= c->mana;
    if (added) {
        c->mana = (int16_t)(c->mana - mana);
        c->redraw |= REDRAW_BARS;
        runes[n] = (uint8_t)(rune + n * RUNES_PER_SET + RUNE_FIRST);
        c->rune_count = (uint8_t)(n + 1);
        runes[n + 1] = 0;
    }
    action.redraw_delay = 1;
    g_hooks.update_action_area(0);
    input_refresh_mode();
    if (added) ui_draw_champion_state(action.champion - 1);
}

/* The champion who casts takes back the last rune. */
void magic_remove_last_rune(void)
{
    Champion *c = champion(action.champion - 1);
    c->rune_count--;
    champion_runes(c)[c->rune_count] = 0;
    action.redraw_delay = 1;
    g_hooks.update_action_area(0);
    input_refresh_mode();
}

int move_party_warp_to_marker(void)
{
    for (uint16_t t = dungeon_square_first_thing(party.cur_map, game.party_x, game.party_y);
         t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_TEXT) continue;
        if (text_kind(t) != TEXT_KIND_ORNAMENT) continue;
        int subtype = text_subtype(t), mode;
        if (subtype == SUBTYPE_WARP_ONCE) mode = MARKER_CLEAR_ALL;
        else if (subtype == SUBTYPE_WARP) mode = MARKER_FIND_SET;
        else continue;
        int16_t x, y;
        uint8_t marker = text_value(t);
        int map = dun_find_marker(mode, 0, marker, &x, &y);
        if (map < 0) continue;
        move_party_to(x, y, map, game.party_dir);
        sound_request_with_fallback(CAT_FLOOR_ORNAMENT, marker, SOUND_ENTRY_ACTIVATE, SOUND_FALLBACK,
                                 game.party_x, game.party_y, 1, SOUND_PRIORITY_CLICK, SOUND_VOLUME_NORMAL);
        if (mode == MARKER_CLEAR_ALL) text_set_visible(t, true);
        return 1;
    }
    return 0;
}
