/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Champion statistics in fights, damage, wounds, poison, death, and the effects of time. */
#include <stdlib.h>
#include <string.h>
#include "action.h"
#include "champion.h"
#include "champion_internal.h"
#include "dungeon.h"
#include "game.h"
#include "hooks.h"
#include "item.h"
#include "move.h"
#include "random.h"
#include "sensor.h"
#include "timeline.h"
#include "weather.h"

/* Attack and wound bits. */
#define ATTACK_TYPE_MASK           0x7fff
#define WOUND_SHARP                0x8000 /* wound location bit: a sharp attack */
#define ATTR_DEFENDS               0x8000 /* ATTR_ARMOUR_CLASS bit of a hand item: it defends */
#define WOUND_HAND_BIT(hand)       ((hand) ? 2 : 1)
/* Masks of the random rolls. */
#define WOUND_ROLL_MASK            0x7f
#define WOUND_LOCATION_ROLL        0x07 /* the random wound location of a hit: 0 to 7 */
#define PARRY_ROLL                 0x0f
#define STRENGTH_ROLL              0x0f
#define DEXTERITY_ROLL             0x07
/* Fields of an armour attribute: the defense and the sharp factor. */
#define ARMOUR_DEFENSE_MASK        0xff
#define ARMOUR_SHARP_MASK          0x07
/* The action command of a hand that parries (last_command). */
#define COMMAND_PARRY              0x01
/* Fight formulas: bases, limits, and shifts. */
#define WOUND_LOCATIONS            6 /* the hands, the head, the torso, the legs, the feet */
#define DEFENSE_BASE               130
#define PSYCHIC_BASE               115
#define ADJUST_BASE                170
#define ADJUST_MIN                 16
#define WOUND_ROLL_BASE            10
#define ADJUST_SHIFT               7 /* champ_get_stat_adjusted_attack: attack * (170 - statistic) / 128 */
#define ADJUST_WEAK_SHIFT          3 /* a statistic above 154 lets an eighth of the attack through */
#define DEFENSE_SHIFT              6 /* damage * (130 - defense) / 64 */
#define PSYCHIC_SHIFT              6 /* damage * (115 - wisdom) / 64 */
#define PARRY_CHANCE_SHIFT         3 /* the parry chance gains an eighth of the parry defense */
#define PARRY_DEFENSE_SHIFT        2 /* a parry adds a quarter of its defense */
#define HAND_DEFENSE_SHIFT         4 /* a hand item defends its own hand with 1/16 of its value, the rest with 1/32 */
#define VITALITY_DEFENSE_SHIFT     3
#define WOUNDED_DEFENSE_LOSS       8 /* a wounded location loses 8 to 11 defense */
#define PARTY_SPREAD_SHIFT         3 /* party damage varies by an eighth around its value */
#define ARMOUR_SHARP_SHIFT         8
#define SHARP_FACTOR_BASE          4u /* against a sharp attack: defense * (sharp factor + 4) / 8 */
#define SHARP_FACTOR_SHIFT         3
/* Poison and the damage display. */
#define POISON_EVENT_DELAY         36
#define POISON_DAMAGE_ROUND        30
#define POISON_DAMAGE_SHIFT        6
#define DAMAGE_SHOW_TIME           5
#define CURE_ALL                   10000
/* A stamina change of at least this redraws the bars. */
#define STAMINA_REDRAW_LIMIT       10
/* Redraw flags of a poisoned champion and of the statistics and skills. */
#define REDRAW_POISON              (REDRAW_TITLE_ALL | REDRAW_BARS)
#define REDRAW_STATS_SKILLS        (REDRAW_TITLE_ALL | REDRAW_LOAD)
/* Luck, strength, and dexterity. */
#define LUCK_STEP                  2
#define DEXTERITY_MIN              2
#define LIGHT_LOAD_SHIFT           4  /* an item up to 1/16 of the maximum load is light */
#define WEIGHT_FREE                12 /* the item weight that does not change the strength */
/* Time effects. */
#define REGEN_STEP                 56
#define REGEN_RANGE                128
#define MANA_PER_STEP              40
#define MANA_STAMINA_BASE          16
#define MANA_STAMINA_MIN           7
#define HUNGER_BASE                4
#define HUNGER_STEP_MIN            1
#define HUNGER_STEP_MAX            6
#define NEW_PARTY_TIME             80 /* game steps after the first recruit: the party gets hungry faster */
#define NEW_PARTY_TIME_2           250
#define HUNGRY                     (-512)
#define STARVING                   (-1024)
#define HEALTH_REGEN_BASE          12
#define HEALTH_REGEN_SHIFT         7
#define HEALTH_REGEN_STAMINA_SHIFT 2 /* the health grows only with a quarter of the maximum stamina */
#define HUNGER_STAMINA_SHIFT       8 /* one more hunger step per 256 points of maximum stamina */
#define HUNGER_PER_HALF            2 /* the hunger grows for each halving of the stamina below the maximum */
#define FOOD_LOW_COST              2
#define WATER_HUNGER_SHIFT         2 /* water goes down by a quarter of the hunger, food by half */
#define REVIVE_HEALTH_SHIFT        6 /* a revived champion loses 1/64 of its maximum health, and 1 */
#define REVIVE_HEALTH_MIN          25
/* Masks of the game time at which the statistics move toward their maximum. */
#define STATS_TIME_MASK            0xff
#define STATS_TIME_MASK_ASLEEP     0x3f

int16_t g_champ_pending_damage[CHAMPION_MAX];
uint16_t g_champ_pending_wounds[CHAMPION_MAX];
uint16_t g_champ_regen_counter;

/* The defense factor of each wound location. */
static const uint8_t wound_defense_factor[WOUND_LOCATIONS] = {5, 5, 4, 6, 3, 1};
/* The order in which a dead champion drops its slots, from SKULL.EXE. */
static const uint8_t drop_order[SLOT_COUNT] = {5,  4,  9,  8,  7,  12, 6,  11, 3,  13, 14, 15, 16, 17, 18,
                                               19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 10, 2,  0,  1};

static int clamp16(int low, int v, int high)
{
    v = (int16_t)v;
    return v<(int16_t)low ? (int16_t)low : v>(int16_t) high ? (int16_t)high : v;
}

static int min16(int a, int b)
{
    return (int16_t)a < (int16_t)b ? (int16_t)a : (int16_t)b;
}

/* The 32-bit product of two 16-bit values, as the original multiplies them. */
static uint32_t mul16(uint16_t a, uint16_t b)
{
    return (uint32_t)a * b;
}

/* An event of the party map at delay game steps from now. */
static TimelineEvent party_event(uint32_t delay, int type, int priority)
{
    TimelineEvent e = {.time_map = timeline_time_map(g_game.game_time + delay, g_game.party_map),
                       .type = (uint8_t)type,
                       .priority = (uint8_t)priority};
    return e;
}

/* Damage, poison, and death */

void champ_reset_step_state(void)
{
    memset(g_champ_pending_damage, 0, sizeof g_champ_pending_damage);
    memset(g_champ_pending_wounds, 0, sizeof g_champ_pending_wounds);
    g_champ_regen_counter = 0;
    g_leader_hand = (LeaderHand){.icon = HAND_ICON_NONE};
    g_ui.candidate = 0;
}

int champ_get_stat_adjusted_attack(Champion *c, int stat, int attack)
{
    int16_t left = (int16_t)(ADJUST_BASE - champ_get_stat(c, stat, 0));
    if (left < ADJUST_MIN)
        return (uint16_t)attack >> ADJUST_WEAK_SHIFT;
    return (int)(mul16(attack, left) >> ADJUST_SHIFT);
}

/* The defense of an armour attribute; against a sharp attack its sharp factor counts. */
static int armor_defense(uint16_t attr, bool sharp)
{
    if (!sharp)
        return attr & ARMOUR_DEFENSE_MASK;
    return (int)((attr & ARMOUR_DEFENSE_MASK) *
                     (((attr >> ARMOUR_SHARP_SHIFT) & ARMOUR_SHARP_MASK) + SHARP_FACTOR_BASE) >>
                 SHARP_FACTOR_SHIFT);
}

int champ_get_strength(int champ, int hand, int skill)
{
    Champion *c = champion(champ);
    int16_t v = (int16_t)((rand_next() & STRENGTH_ROLL) + champ_get_stat(c, STAT_STRENGTH, 0));
    uint16_t item = c->slots[hand];
    uint16_t weight = (uint16_t)item_weight(item);
    uint16_t light = (uint16_t)((uint16_t)champ_get_max_load(c) >> LIGHT_LOAD_SHIFT);
    v = (int16_t)(v + weight - WEIGHT_FREE);
    if (weight > light) {
        v = (int16_t)(v - ((weight - light) >> 1));
        uint16_t heavy = (uint16_t)(((light - WEIGHT_FREE) >> 1) + light);
        if (weight > heavy)
            v = (int16_t)(v - (weight - heavy) * 2);
    }
    if ((int16_t)skill >= 0) {
        v = (int16_t)(v + champ_get_skill_level(champ, skill, 1) * 2);
        int bonus = 0;
        if (skill == SKILL_FIGHTER || (skill >= SKILL_SWING && skill <= SKILL_PARRY) || skill == SKILL_FIGHT) {
            bonus = item_attr(item, ATTR_STRENGTH);
        } else if (skill == SKILL_NINJA || skill == SKILL_THROW || skill == SKILL_SHOOT) {
            bonus = item_attr(item, ATTR_THROW_POWER);
            /* a launcher adds its power only to shooting, other items only to throwing */
            if (bonus && !(item_attr(item, ATTR_AMMO) & AMMO_LAUNCHER) != (skill != SKILL_SHOOT))
                bonus = 0;
        }
        v = (int16_t)(v + bonus);
    }
    v = (int16_t)champ_scale_by_stamina(c, v);
    if (c->wounds & WOUND_HAND_BIT(hand))
        v = (int16_t)(v >> 1);
    return clamp16(0, v >> 1, PERCENT);
}

int champ_get_dexterity(int champ)
{
    Champion *c = champion(champ);
    int16_t v = (int16_t)((rand_next() & DEXTERITY_ROLL) + champ_get_stat(c, STAT_DEXTERITY, 0));
    int32_t load = (int32_t)((int16_t)v >> 1) * (uint16_t)champ_get_load(champ);
    v = (int16_t)(v - load / (uint16_t)champ_get_max_load(c));
    if (v < DEXTERITY_MIN)
        v = DEXTERITY_MIN;
    if (g_party_sleeping)
        v = (int16_t)(v >> 1);
    int high = (int16_t)(PERCENT - (rand_next() & DEXTERITY_ROLL));
    int low = (int16_t)((rand_next() & DEXTERITY_ROLL) + 1);
    return clamp16(low, v >> 1, high);
}

bool champ_test_luck(Champion *c, int chance)
{
    if (rand_bit() && rand_below(PERCENT) > (uint16_t)chance)
        return true;
    uint16_t roll = rand_below((uint16_t)(champ_get_stat(c, STAT_LUCK, 0) * 2));
    int max = min16(STAT_MAX, champ_get_stat(c, STAT_LUCK, 1));
    bool lucky = roll > (uint16_t)chance;
    c->stats[STAT_LUCK][0] = (uint8_t)clamp16(STAT_MIN, (lucky ? -LUCK_STEP : LUCK_STEP) + c->stats[STAT_LUCK][0], max);
    return lucky;
}

/* The defense of a wound location (bit 15: against a sharp attack) of champ, 0 to 100. */
static int wound_defense(int champ, uint16_t location)
{
    Champion *c = champion(champ);
    bool sharp = (location & WOUND_SHARP) != 0;
    location &= (uint16_t)~WOUND_SHARP;
    int16_t hands = 0;
    for (int hand = 0; hand < SLOT_HAND_COUNT; hand++) {
        uint16_t attr = item_attr(c->slots[hand], ATTR_ARMOUR_CLASS);
        if (!(attr & ATTR_DEFENDS))
            continue;
        int strength = (uint16_t)champ_get_strength(champ, hand, SKILL_PARRY);
        int v = (armor_defense(attr, sharp) + strength) * wound_defense_factor[location];
        hands = (int16_t)(hands + (v >> ((hand != location) + HAND_DEFENSE_SHIFT)));
    }
    int16_t v =
        (int16_t)rand_below((uint16_t)(((uint16_t)champ_get_stat(c, STAT_VITALITY, 0) >> VITALITY_DEFENSE_SHIFT) + 1));
    if (sharp)
        v = (int16_t)(v >> 1);
    if (c->aura == AURA_DEFENSE)
        v = (int16_t)(v + c->aura_value);
    v = (int16_t)(v + c->defense[0] + c->defense[1] + hands);
    if (location >= SLOT_HAND_COUNT)
        v = (int16_t)(v + armor_defense(item_attr(c->slots[location], ATTR_ARMOUR_CLASS), sharp));
    if (c->wounds & 1 << location)
        v = (int16_t)(v - (rand_2bits() + WOUNDED_DEFENSE_LOSS));
    if (g_party_sleeping)
        v = (int16_t)(v >> 1);
    return clamp16(0, v >> 1, PERCENT);
}

/* The mean defense of champ over the wound locations of wound_mask against an attack of type. */
static uint16_t mean_defense(int champ, int wound_mask, int type)
{
    int count = 0;
    uint16_t defense = 0;
    for (int w = 0; w < WOUND_LOCATIONS; w++) {
        if (!((uint16_t)wound_mask & 1 << w))
            continue;
        count++;
        defense =
            (uint16_t)(defense + wound_defense(champ, (uint16_t)((type == ATTACK_TYPE_SHARP ? WOUND_SHARP : 0) | w)));
    }
    return count ? (uint16_t)(defense / count) : defense;
}

/* The damage d that is left after the defense, a parry, the statistics, and the auras of champ; 0 or less: none. */
static int16_t reduce_damage(int champ, int16_t d, int wound_mask, int attack_type)
{
    Champion *c = champion(champ);
    int type = attack_type & ATTACK_TYPE_MASK;
    uint16_t defense = mean_defense(champ, wound_mask, type);
    int16_t parry = 0;
    for (int hand = 0; hand < SLOT_HAND_COUNT; hand++)
        if (c->last_command[hand] == COMMAND_PARRY)
            parry = (int16_t)(parry + c->defense[hand]);
    if (parry) {
        int chance = (uint16_t)champ_get_skill_level(champ, SKILL_PARRY, 1) + ((uint16_t)parry >> PARRY_CHANCE_SHIFT);
        if (chance > (int)(rand_next() & PARRY_ROLL)) {
            if (attack_type & ATTACK_BLOCKABLE) {
                d = (int16_t)(d - parry);
                if (d <= 0)
                    return 0;
            }
            defense = (uint16_t)(defense + ((uint16_t)parry >> PARRY_DEFENSE_SHIFT));
        }
    }
    bool scaled = true;
    switch (type) {
    case ATTACK_TYPE_FIRE:
        d = (int16_t)champ_get_stat_adjusted_attack(c, STAT_ANTI_FIRE, (uint16_t)d);
        if (c->aura == AURA_FIRE_SHIELD)
            d = (int16_t)(d - c->aura_value);
        break;
    case ATTACK_TYPE_SELF:
    case ATTACK_TYPE_SELF_2:
        defense = (uint16_t)((defense >> 1) + champ_get_skill_level(champ, SKILL_NINJA, 1));
        break;
    case ATTACK_TYPE_MAGIC:
        d = (int16_t)champ_get_stat_adjusted_attack(c, STAT_ANTI_MAGIC, (uint16_t)d);
        if (c->aura == AURA_SPELL_SHIELD)
            d = (int16_t)(d - c->aura_value);
        scaled = false;
        break;
    case ATTACK_TYPE_PSYCHIC: {
        int16_t left = (int16_t)(PSYCHIC_BASE - champ_get_stat(c, STAT_WISDOM, 0));
        if (left <= 0)
            return 0;
        d = (int16_t)(mul16(d, left) >> PSYCHIC_SHIFT);
        scaled = false;
        break;
    }
    }
    if (scaled) {
        if (d <= 0)
            return 0;
        d = (int16_t)(mul16(d, DEFENSE_BASE - defense) >> DEFENSE_SHIFT);
    }
    return d;
}

int champ_take_damage(int champ, int damage, int wound_mask, int attack_type)
{
    champ = (int16_t)champ;
    int16_t d = (int16_t)damage;
    if (champ == -1 || champ + 1 == g_ui.candidate || g_session.completed || d < 1 || !champion(champ)->health)
        return 0;
    if (attack_type & ATTACK_TYPE_MASK) {
        d = reduce_damage(champ, d, wound_mask, attack_type);
        if (d <= 0)
            return 0;
        int16_t resist = (int16_t)champ_get_stat_adjusted_attack(
            champion(champ), STAT_VITALITY, (int)((rand_next() & WOUND_ROLL_MASK) + WOUND_ROLL_BASE));
        while (d > resist) {
            g_champ_pending_wounds[champ] |= (uint16_t)((1 << (rand_next() & WOUND_LOCATION_ROLL)) & wound_mask);
            resist = (int16_t)(resist * 2);
            if (d <= resist || !resist)
                break;
        }
        if (g_party_sleeping)
            champ_wake_party();
    }
    g_champ_pending_damage[champ] = (int16_t)(g_champ_pending_damage[champ] + d);
    return d;
}

void champ_decrease_stamina(int champ, int amount)
{
    if (champ == -1)
        return;
    Champion *c = champion(champ);
    c->stamina = (int16_t)(c->stamina - amount);
    if (c->stamina <= 0) {
        int16_t missing = c->stamina;
        c->stamina = 0;
        champ_take_damage(champ, (int16_t)(-missing >> 1), 0, 0);
    } else if (c->stamina > c->max_stamina) {
        c->stamina = c->max_stamina;
    }
    if (abs((int16_t)amount) >= STAMINA_REDRAW_LIMIT)
        c->redraw |= REDRAW_BARS;
}

int champ_damage_party(int damage, int wound_mask, int attack_type)
{
    int16_t d = (int16_t)damage;
    if (!d)
        return 0;
    int hit = 0;
    int spread = ((uint16_t)d >> PARTY_SPREAD_SHIFT) + 1;
    d = (int16_t)(d - spread);
    for (int i = 0; i < g_game.champion_count; i++) {
        int16_t v = (int16_t)(rand_below((uint16_t)(spread * 2)) + d);
        if (champ_take_damage(i, v > 1 ? v : 1, wound_mask, attack_type))
            hit |= 1 << i;
    }
    return hit;
}

void champ_poison(int champ, int amount)
{
    champ = (int16_t)champ;
    if (champ == -1 || champ + 1 == g_ui.candidate)
        return;
    Champion *c = champion(champ);
    int16_t d = (int16_t)(((uint16_t)amount + POISON_DAMAGE_ROUND) >> POISON_DAMAGE_SHIFT);
    champ_take_damage(champ, d > 1 ? d : 1, 0, 0);
    c->redraw |= REDRAW_POISON;
    uint16_t left = (uint16_t)(amount - 1);
    if (!left)
        return;
    if (c->poison + left > POISON_MAX) {
        left = (uint16_t)(POISON_MAX - c->poison);
        if (!left)
            return;
    }
    c->poison = (int16_t)(c->poison + left);
    c->poisoned++;
    /* fix: the original leaves the last 4 data bytes uninitialized */
    TimelineEvent e = party_event(POISON_EVENT_DELAY, EVENT_POISON, champ);
    put_le16(e.data, left); /* the poison that is left */
    timeline_add_event(&e);
}

void champ_cure_poison(int champ, int amount)
{
    champ = (int16_t)champ;
    if (champ == -1)
        return;
    Champion *c = champion(champ);
    int16_t left = (int16_t)amount;
    /* the original reads the first event slots as many as the timeline holds, so it can miss a poison event in a
       later slot; the advanced mode reads all slots */
    for (int i = 0; i < (g_game_mode == GAME_MODE_ADVANCED ? g_timeline.capacity : g_timeline.count); i++) {
        TimelineEvent *e = &g_timeline.events[i];
        if (e->type != EVENT_POISON || e->priority != (uint8_t)champ)
            continue;
        int16_t v = (int16_t)le16(e->data);
        if (left < v) {
            v = (int16_t)(v - left);
            put_le16(e->data, (uint16_t)v);
            c->poison = (int16_t)(c->poison - left);
            return;
        }
        left = (int16_t)(left - v);
        c->poison = (int16_t)(c->poison - v);
        timeline_delete_event((uint16_t)i);
        if (!--c->poisoned || left <= 0)
            return;
    }
}

/* Drops all items of the champion on the party square, in its cell. */
static void drop_all_objects(int champ)
{
    int cell = champion(champ)->cell;
    for (int i = 0; i < SLOT_COUNT; i++) {
        uint16_t t = champ_remove_object_from_slot(champ, drop_order[i]);
        if (t != THING_NONE)
            move_thing(thing_with_cell(t, cell), -1, 0, g_game.party_x, g_game.party_y);
    }
}

/* A click on the face in relative cell: takes the face, or drops the dragged face there (finish false: a cancel). */
static void drag_click(int16_t cell, bool finish)
{
    int16_t target = (int16_t)champ_get_index_in_cell(dir_turn(cell, g_game.party_dir));
    if (!g_input_drag_state) {
        if (target < 0)
            return;
        g_hooks.set_champion_cursor(target);
        g_input_drag_state = cell + 1;
        return;
    }
    int from = g_input_drag_state - 1;
    g_input_drag_state = 0;
    int16_t dragged = (int16_t)champ_get_index_in_cell(dir_turn(g_game.party_dir, from));
    if (finish)
        champion(dragged)->dir = (uint8_t)g_game.party_dir;
    if (from == cell)
        return;
    if (target >= 0)
        champion(target)->cell = (uint8_t)dir_turn(from, g_game.party_dir);
    champion(dragged)->cell = (uint8_t)dir_turn(cell, g_game.party_dir);
}

void champ_drag_click(int16_t cell)
{
    drag_click(cell, true);
}

void champ_drag_cancel(void)
{
    if (g_input_drag_state)
        drag_click((int16_t)(g_input_drag_state - 1), false);
}

/* Kills champ: it drops its items and its bones. */
static void kill_champion(int champ)
{
    Champion *c = champion(champ);
    if (champ == g_action.champion - 1)
        action_area_close();
    c->health = 0;
    c->action_delay[0] = 0;
    c->action_delay[1] = 0;
    c->action_delay[ACTION_SPELL] = 0;
    c->redraw |= REDRAW_STATUS_BOX;
    if (champ + 1 == g_ui.inventory) {
        if (g_ui.eye) {
            g_ui.eye = 0;
            if (g_game.leader_hand != THING_NONE)
                inv_show_object_name(g_game.leader_hand);
        }
        g_ui.mouth = 0;
        g_hooks.toggle_inventory(CHAMPION_MAX);
    }
    drop_all_objects(champ);
    int cell = c->cell;
    uint16_t bones = dungeon_unused_thing(THING_MISC | THING_ANY_RECORD);
    if (bones != THING_NONE) {
        item_set_type(bones, ITEM_BONES);
        item_set_kept(bones, true);
        misc_set_charges(bones, (uint16_t)champ); /* the champion of the bones */
        move_thing(thing_with_cell(bones, cell), -1, 0, g_game.party_x, g_game.party_y);
    }
    c->rune_count = 0;
    c->runes[0] = 0;
    c->dir = (uint8_t)g_game.party_dir;
    c->hit_damage = 0;
    if (dir_relative(cell, g_game.party_dir) + 1 == g_input_drag_state)
        champ_drag_cancel();
    if (c->poisoned)
        champ_cure_poison(champ, CURE_ALL);
    int alive = 0;
    while (alive < g_game.champion_count && !champion(alive)->health)
        alive++;
    if (alive == g_game.champion_count) {
        g_main_loop.end = 1;
        g_hooks.flush_input();
        g_party_sleeping = 0;
        g_main_loop.view_hidden = 0;
    } else if (champ == (int16_t)g_game.leader) {
        champ_set_leader(alive);
    }
    g_hooks.refresh_input_mode();
}

void champ_apply_pending_damage(void)
{
    for (int i = 0; i < g_game.champion_count; i++) {
        Champion *c = champion(i);
        c->wounds |= g_champ_pending_wounds[i];
        g_champ_pending_wounds[i] = 0;
        int16_t d = g_champ_pending_damage[i];
        if (!d)
            continue;
        g_champ_pending_damage[i] = 0;
        if (!c->health)
            continue;
        int16_t health = (int16_t)(c->health - d);
        if (health <= 0 && game_has_cheat(CHEAT_IMMORTAL))
            health = 1;
        if (health <= 0) {
            kill_champion(i);
            continue;
        }
        c->health = health;
        c->damage = (uint16_t)d;
        c->redraw |= REDRAW_BARS;
        if (c->damage_event == THING_NONE) {
            /* fix: the original leaves the data bytes uninitialized */
            TimelineEvent e = party_event(DAMAGE_SHOW_TIME, EVENT_HIDE_DAMAGE, i);
            c->damage_event = (uint16_t)timeline_add_event(&e);
        } else {
            g_timeline.events[c->damage_event].time_map =
                timeline_time_map(g_game.game_time + DAMAGE_SHOW_TIME, g_game.party_map);
            timeline_reposition_event(c->damage_event);
        }
    }
}

/* Champion i regains mana by its wisdom and magic skills, and pays stamina for it; mana above the maximum fades. */
static void regen_mana(int i)
{
    Champion *c = champion(i);
    if (c->mana < c->max_mana) {
        uint16_t magic =
            (uint16_t)(champ_get_skill_level(i, SKILL_WIZARD, 1) + champ_get_skill_level(i, SKILL_PRIEST, 1));
        if (g_champ_regen_counter < (uint16_t)champ_get_stat(c, STAT_WISDOM, 0) + magic) {
            int gain = c->max_mana / MANA_PER_STEP + 1;
            if (g_party_sleeping)
                gain *= 2;
            int cost = (int16_t)(MANA_STAMINA_BASE - magic);
            if (cost < MANA_STAMINA_MIN)
                cost = MANA_STAMINA_MIN;
            champ_decrease_stamina(i, (int16_t)(cost * gain));
            c->mana = (int16_t)(c->mana + min16(gain, c->max_mana - c->mana));
        }
    } else if (c->mana > c->max_mana) {
        c->mana--;
    }
}

/* Champion i eats and drinks: food and water go down, and hunger or thirst costs stamina. */
static void consume_food_and_water(int i)
{
    Champion *c = champion(i);
    uint16_t hunger = HUNGER_BASE;
    for (int16_t half = (int16_t)(c->max_stamina >> 1); half > c->stamina; half = (int16_t)(half >> 1))
        hunger += HUNGER_PER_HALF;
    int step = clamp16(HUNGER_STEP_MIN, (int8_t)(c->max_stamina >> HUNGER_STAMINA_SHIFT) - 1, HUNGER_STEP_MAX);
    uint16_t since = (uint16_t)(g_game.game_time - g_game.first_recruit_time);
    if (since > NEW_PARTY_TIME)
        step++;
    if (since > NEW_PARTY_TIME_2)
        step++;
    if (g_party_sleeping)
        step *= 2;
    int16_t stamina = 0;
    int16_t food = c->food;
    int16_t water = c->water;
    do {
        bool low = hunger <= HUNGER_BASE;
        if (c->food < HUNGRY) {
            if (low) {
                stamina = (int16_t)(stamina + step);
                c->food = (int16_t)(c->food - FOOD_LOW_COST);
            }
        } else {
            if (c->food >= 0)
                stamina = (int16_t)(stamina - step);
            c->food = (int16_t)(c->food - (low ? FOOD_LOW_COST : hunger >> 1));
        }
        if (c->water < HUNGRY) {
            if (low) {
                stamina = (int16_t)(stamina + step);
                c->water--;
            }
        } else {
            if (c->water >= 0)
                stamina = (int16_t)(stamina - step);
            c->water = (int16_t)(c->water - (low ? 1 : hunger >> WATER_HUNGER_SHIFT));
        }
    } while (--hunger && c->stamina - stamina < c->max_stamina);
    champ_decrease_stamina(i, stamina);
    if (c->food < STARVING)
        c->food = STARVING;
    if (c->water < STARVING)
        c->water = STARVING;
    if (game_has_cheat(CHEAT_FOOD)) {
        c->food = food;
        c->water = water;
    }
}

void champ_apply_time_effects(void)
{
    if (!g_game.champion_count)
        return;
    g_champ_regen_counter = (uint16_t)(g_champ_regen_counter + REGEN_STEP);
    if (g_champ_regen_counter > REGEN_RANGE)
        g_champ_regen_counter = (uint16_t)(g_champ_regen_counter - REGEN_RANGE);
    for (int i = 0; i < g_game.champion_count; i++) {
        Champion *c = champion(i);
        if (!c->health || i + 1 == g_ui.candidate)
            continue;
        regen_mana(i);
        consume_food_and_water(i);
        if (c->health < c->max_health && c->stamina >= c->max_stamina >> HEALTH_REGEN_STAMINA_SHIFT &&
            g_champ_regen_counter < (uint16_t)champ_get_stat(c, STAT_VITALITY, 0) + HEALTH_REGEN_BASE) {
            int gain = (c->max_health >> HEALTH_REGEN_SHIFT) + 1;
            if (g_party_sleeping)
                gain *= 2;
            c->health = (int16_t)(c->health + min16(gain, c->max_health - c->health));
        }
        if (!((uint16_t)g_game.game_time & (g_party_sleeping ? STATS_TIME_MASK_ASLEEP : STATS_TIME_MASK))) {
            for (int s = 0; s < STAT_COUNT; s++) {
                uint8_t *v = c->stats[s];
                if (v[0] < v[1])
                    v[0]++;
                else if (v[0] > v[1])
                    v[0] = (uint8_t)(v[0] - v[0] / v[1]);
            }
        }
        c->redraw |= REDRAW_BARS;
        if (i + 1 == g_ui.inventory)
            c->redraw |= REDRAW_STATS_SKILLS;
    }
}

void champ_burn_held_lights(void)
{
    bool burned = false;
    int count = g_game.champion_count;
    if (g_ui.candidate)
        count--; /* the recruit candidate burns no lights */
    for (int i = 0; i < count; i++) {
        Champion *c = champion(i);
        for (int hand = SLOT_HAND_COUNT - 1; hand >= 0; hand--) {
            uint16_t t = c->slots[hand];
            if (!(item_attr(t, ATTR_FLAGS) & ITEM_FLAG_LIGHT) || !item_charges(t))
                continue;
            if (!item_add_charges(t, -1))
                item_set_do_not_discard(t, 0);
            burned = true;
        }
    }
    if (burned)
        light_update_party_light();
}

/* Puts champ into its cell, or into the first free cell from the party direction. */
static void place_in_free_cell(int champ)
{
    Champion *c = champion(champ);
    int16_t health = c->health;
    c->health = 0;
    if (c->cell == NO_CHAMPION_CELL || champ_get_index_in_cell(c->cell) != -1) {
        int k = 0;
        while (champ_get_index_in_cell(dir_turn(g_game.party_dir, k)) != -1)
            k++;
        c->cell = (uint8_t)dir_turn(g_game.party_dir, k);
    }
    c->dir = (uint8_t)g_game.party_dir;
    c->health = health;
}

void champ_revive(int champ)
{
    Champion *c = champion(champ);
    place_in_free_cell(champ);
    c->load = 0;
    for (int s = 0; s < SLOT_COUNT; s++)
        c->slots[s] = THING_NONE;
    int16_t max = (int16_t)((uint16_t)c->max_health - ((uint16_t)c->max_health >> REVIVE_HEALTH_SHIFT) - 1);
    c->max_health = max > REVIVE_HEALTH_MIN ? max : REVIVE_HEALTH_MIN;
    c->health = (int16_t)(c->max_health >> 1);
    c->redraw |= REDRAW_STATUS_BOX;
    c->aura = 0;
    c->aura_value = 0;
    g_hooks.refresh_input_mode();
}
