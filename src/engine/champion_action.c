/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The actions of the champions: the action menu, the actions, their delays and repeats, experience, throwing. */
#include "action.h"
#include "ai.h"
#include "champion.h"
#include "champion_internal.h"
#include "creature.h"
#include "dungeon.h"
#include "game.h"
#include "game_text.h"
#include "hooks.h"
#include "ids.h"
#include "item.h"
#include "magic.h"
#include "minion.h"
#include "move.h"
#include "num.h"
#include "projectile.h"
#include "random.h"
#include "sensor.h"
#include "sound_request.h"
#include "weather.h"

/* Item attributes that the actions read. */
#define ATTR_THROW_POWER_SHOOT 0x09
#define ATTR_SHOOT_ATTACK      0x0a
#define ATTR_STEP              0x0c /* item attribute: the flight step of a missile */
/* Item attribute bits and masks. */
#define ALL_CHARGES            0x000f /* the charges that CHARGES_USE_ALL takes */
#define ATTR_USED_UP           0x0800 /* ATTR_FLAGS bit: the item vanishes with its last charge */
/* Flag bits of the action area, the attacks, the creature alerts, the creature info and the redraw. */
#define CREATURE_MELEE_REACHES 0x0020 /* creature info flag: mode 1 attacks reach the creature */
#define FLAGS_KEEP_OPEN        0x0400 /* g_action.flags bit that the action area clears after an action */
#define FRIGHTEN_ALERT_FLAGS   0x2005
#define REDRAW_LEVEL           0x3800
#define MELEE_ALERT_FLAGS      0x6002
#define MELEE_ATTACK_PIERCES   0x8000 /* FIELD_ATTACK bit: the hit reaches creatures that CREATURE_NOT_DOOR_HIT marks */
/* The random masks of the hit rolls and the throw. */
#define MELEE_WEAK_RANDOM      0x0f /* the random part of the rest of a weak hit */
#define MELEE_RANDOM           0x1f /* the random part of the hit chance, the defense, the damage, and the armor test */
#define THROW_ATTACK_RANDOM    0x1f /* the random part of the attack of a throw */
#define MELEE_CRITICAL_ROLL    0x3f /* a skill level above a roll of 0 to 63 doubles the damage plus a bonus */
/* Creature info values: no fear reaches the creature, no hit reaches it. */
#define FRIGHTEN_IMMUNE        0x0f
#define DEXTERITY_UNHITTABLE   0xff
/* The sound entries and priorities of the actions, and the spell base of a projectile spell action. */
#define SOUND_HIT_PRIORITY     0x3c
#define SOUND_ACTION_PRIORITY  0x73
#define SPELL_BASE             0x80 /* FIELD_MODE of a projectile spell: the spell is the mode - SPELL_BASE */
#define SOUND_HIT              0x8d /* sound entry of a door or creature that a melee action hits */
/* Script variables that count the failed actions. */
#define VAR_BLOCKED_ACTIONS    0x42
#define VAR_NO_AMMO_ACTIONS    0x43
#define VAR_CANT_ACTIONS       0x47
/* The explosion types of the actions, and the cell of an explosion in the square center. */
#define CENTER_CELL            0x00ff
#define EXPLOSION_COMMAND      0xff8e
#define EXPLOSION_SUMMON_FAIL  0xffa8
/* g_action.result of a melee action when a champion stands in front. */
#define RESULT_BLOCKED         (-1)
/* The delays of the actions in game steps. */
#define DELAY_BASE             2
#define DELAY_EXTRA_SHIFT      2 /* an action delay grows by a quarter */
#define HASTE_DELAY_SHIFT      2 /* haste cuts the delay to a quarter */
#define DELAY_MAX              255
#define ACTION_DOOR_DELAY      6
#define DOOR_BREAK_DELAY       2
#define RESULT_DELAY_BASE      8
#define THROW_DELAY            4
/* The power, cost, and limits of the spells and the party magic. */
#define POWER_MIN              32
#define ACTION_EFFECT_MAX      200
#define HEAL_PER_STEP_MAX      10
#define HEAL_MANA              2
#define HEAL_EXPERIENCE        2 /* the experience of a heal: 2, and 2 for each healed step */
#define SPELL_COST_BASE        7 /* the mana of a projectile spell: 7 - the skill level, at least 1 */
#define SPELL_COST_SKILL_MAX   6
#define SPELL_POWER_MIN        2
#define EXPLOSION_POWER_MIN    2
#define EXPLOSION_SUMMON_POWER 110
#define SHIELD_POWER_FACTOR    3 /* the power of a shield: power * 3, of a statistic aura: power * 4 */
#define AURA_POWER_SHIFT       2
#define SUMMON_POWER_SHIFT     3 /* the level of a summoned group: the power / 8 */
/* The melee hit: its chance, damage, experience, and stamina, and the alert chances. */
#define MELEE_MODE_REACH       1  /* FIELD_MODE of an attack that reaches only CREATURE_MELEE_REACHES creatures */
#define MELEE_KIND_FULL        1  /* FIELD_MELEE_KIND: full damage to creatures that take half damage */
#define MELEE_LUCK_BASE        75 /* a missed hit gets a luck test of 75 - attack */
#define MELEE_ALERT_CHANCE     90
#define FRIGHTEN_ALERT_CHANCE  100
#define MELEE_CHANCE_BASE      16 /* the hit chance: (random + dexterity + difficulty + 2 * light - base) / 2 */
#define MELEE_FACTOR_SHIFT     5  /* the damage factor of an action is in 32nds */
#define MELEE_CRITICAL_BONUS   10
#define MELEE_EXPERIENCE_SHIFT 4 /* the experience of a hit: factor * damage / 16 + base */
#define MELEE_EXPERIENCE_BASE  3
#define MELEE_STAMINA_HIT      4 /* the stamina cost of a hit: 4 to 7, of a miss: 2 to 3 */
#define MELEE_STAMINA_MISS     2
/* The throw: its experience, kinetic energy, attack, step, and fatigue. */
#define THROW_EXPERIENCE       8
#define THROW_EXPERIENCE_SHIFT 2 /* the experience of a throw: throw power / 4 + 12 */
#define THROW_EXPERIENCE_BASE  12
#define KINETIC_MAX            255
#define THROW_KINETIC_SHIFT    2 /* the random part of the kinetic energy: 0 to kinetic / 4 + 7 */
#define THROW_KINETIC_RANDOM   8
#define ATTACK_MIN             40
#define ATTACK_MAX             200
#define THROW_ATTACK_PER_LEVEL 8
#define STEP_MIN               5
#define STEP_BASE              11
#define THROW_FATIGUE_STEP     10 /* a throw costs half the weight up to 10, then half of each further part */
/* The last sub-skill of the fighter and the ninja: these sub-skills learn less away from fights. */
#define SKILL_LAST_FIGHT       11
/* Experience, levels, and the limits of the statistics that a level raises. */
#define FIGHT_TIME_LONG        150
#define FIGHT_TIME_SHORT       40
#define LEVEL_EXPERIENCE       512
#define MAX_MANA_LIMIT         900 /* a level gain raises the maximum mana up to this */
#define HEALTH_MAX             999
#define STAMINA_MAX            9999
#define FIGHTER_HEALTH_FACTOR  3 /* the health gain of a level: level * 3 for a fighter, * 2 for a ninja */
/* The maximum stamina that a level of a base skill adds: max stamina / 16, / 21, / 25, / 32. */
#define FIGHTER_STAMINA_SHIFT  4
#define NINJA_STAMINA_DIVISOR  21
#define PRIEST_STAMINA_DIVISOR 25
#define WIZARD_STAMINA_SHIFT   5

/* The action that champ_perform_action runs: the champion, its hand and item, and what the action gives. */
typedef struct {
    int champ, hand, skill;
    bool repeated;
    Champion *c;
    uint16_t item;
    ChampionAction action; /* the text of the action */
    int fx, fy;            /* the square in front of the champion */
    uint8_t front;         /* the square byte in front */
    uint16_t target;       /* the creature group in front, THING_NONE none */
    int16_t power, experience;
    uint16_t delay;
    bool silent; /* the action plays no sound */
} ActionRun;

ActionRepeat g_action_repeats[CHAMPION_MAX][SLOT_HAND_COUNT];
/* g_action.result after the last champ_perform_action (damage >= 0, failure < 0). */
int16_t g_champ_action_result;
/* The creature info records of things that a blow does not hurt (static, without the bytes 2 and 8 of a fighter), from SKULL.EXE. */
static const uint8_t furniture_infos[] = {0, 1, 2, 4, 5, 6, 7, 8, 10, 11, 12, 20, 37, 38, 39, 45, 51, 56, 60};

/* A field of the GDAT text of action. */
static int16_t field_of(ChampionAction action, int field)
{
    return gtext_gdat_field(action.category, action.index, action.entry, field);
}

void champ_action_clear_result(void)
{
    g_champ_action_result = 0;
}

void champ_add_action_delay(int champ, uint16_t ticks, int16_t hand)
{
    Champion *c = champion(champ);
    uint16_t delay = (uint16_t)(ticks + (ticks >> DELAY_EXTRA_SHIFT));
    int count = 1;
    if (hand == -1) {
        count = ACTION_DELAY_COUNT;
        hand = 0;
    }
    if (g_game.magic_light[MAGIC_HASTE])
        delay = (uint16_t)(delay >> HASTE_DELAY_SHIFT);
    delay = (uint16_t)(delay + DELAY_BASE);
    for (; count; count--, hand++) {
        uint16_t old = c->action_delay[hand];
        uint16_t v = delay > old ? (uint16_t)(delay + (old >> 1)) : (uint16_t)(old + (delay >> 1));
        c->action_delay[hand] = (uint8_t)(v > DELAY_MAX ? DELAY_MAX : v);
    }
}

bool champ_use_action_charge(int champ, ChampionAction action)
{
    int16_t charges = field_of(action, FIELD_CHARGES);
    if (charges == CHARGES_USE_ALL)
        charges = ALL_CHARGES;
    if (charges != CHARGES_USE_NONE && charges != CHARGES_EMPTY)
        item_add_charges(g_champ_action_thing, (int16_t)-charges);
    bool empty = item_add_charges(g_champ_action_thing, 0) == 0;
    if (empty && item_attr(g_champ_action_thing, ATTR_FLAGS) & ATTR_USED_UP) {
        champ_remove_object_from_slot(champ, g_action.hand);
        thing_set_word(g_champ_action_thing, THING_NEXT, THING_NONE);
    }
    return empty;
}

/* The first ammunition or throwing item for the hand of champ in the quiver (slot 12, the chest in it, slots 7 to 9),
   taken out; THING_NONE when there is none. throwing: a throwing item, else ammunition for the launcher in hand. */
static uint16_t take_next_missile(int champ, int hand, bool throwing)
{
    Champion *c = champion(champ);
    int slot = SLOT_CONTAINER_TEST;
    bool ok = throwing ? item_has_action_command(champ, c->slots[slot], COMMAND_THROW)
                       : item_is_ammo_for_hand(champ, hand, c->slots[slot]);
    if (!ok) {
        uint16_t quiver = c->slots[SLOT_CONTAINER_TEST];
        if (item_is_chest(quiver)) {
            uint16_t *contents = container_contents(quiver);
            for (uint16_t t = *contents; t != THING_ENDOFLIST; t = dungeon_next_thing(t))
                if (throwing ? item_has_action_command(champ, t, COMMAND_THROW)
                             : item_is_ammo_for_hand(champ, hand, t)) {
                    dungeon_unlink_thing_from_list(t, contents);
                    return t;
                }
        }
        for (slot = SLOT_QUIVER_FIRST + 1; slot <= SLOT_QUIVER_LAST; slot++)
            if (throwing ? item_has_action_command(champ, c->slots[slot], COMMAND_THROW)
                         : item_is_ammo_for_hand(champ, hand, c->slots[slot]))
                break;
        if (slot > SLOT_QUIVER_LAST) {
            if (!throwing)
                return THING_NONE;
            slot = SLOT_CONTAINER_TEST; /* the original then takes what lies in the quiver slot */
        }
    }
    return champ_remove_object_from_slot(champ, slot);
}

void champ_reload_hand(int champ, int hand)
{
    Champion *c = champion(champ);
    c->action_delay[hand] = 0;
    if (!c->health || hand >= SLOT_HAND_COUNT)
        return;
    uint8_t command = c->last_command[hand];
    c->last_command[hand] = COMMAND_NONE;
    c->defense[hand] = 0;
    uint16_t t;
    int target;
    if (command == COMMAND_SHOOT) {
        if (c->slots[hand ^ 1] != THING_NONE)
            return;
        t = take_next_missile(champ, hand, false);
        if (t == THING_NONE)
            return;
        target = hand ^ 1;
    } else if (command == COMMAND_THROW) {
        if (c->slots[hand] != THING_NONE)
            return;
        t = take_next_missile(champ, hand, true);
        if (t == THING_NONE)
            return;
        target = hand;
    } else {
        return;
    }
    champ_add_object_to_slot(champ, t, target);
}

int champ_get_party_level(void)
{
    uint32_t sum = 0;
    for (int i = 0; i < g_game.champion_count; i++)
        for (int s = 0; s < SKILL_BASE_COUNT; s++)
            sum += champion(i)->experience[s];
    int level = 1;
    for (; sum >= LEVEL_EXPERIENCE; sum >>= 1)
        level++;
    return level;
}

/* A level raises the maximum health by about gain and the maximum stamina by about stamina, up to their limits. */
static void raise_health_and_stamina(Champion *c, uint32_t gain, uint16_t stamina)
{
    c->max_health = (int16_t)(c->max_health + rand_below((uint16_t)((gain >> 1) + 1)) + gain);
    if (c->max_health > HEALTH_MAX)
        c->max_health = HEALTH_MAX;
    c->max_stamina = (int16_t)(c->max_stamina + rand_below((uint16_t)((stamina >> 1) + 1)) + stamina);
    if (c->max_stamina > STAMINA_MAX)
        c->max_stamina = STAMINA_MAX;
}

/* Raises the maximum of statistic stat of c by amount. */
static void raise_maximum(Champion *c, int stat, int amount)
{
    c->stats[stat][STAT_MAXIMUM] = (uint8_t)(c->stats[stat][STAT_MAXIMUM] + amount);
}

/* A priest or wizard level raises the maximum mana a little more, up to its limit, and the anti-magic. */
static void raise_magic(Champion *c, uint16_t level)
{
    c->max_mana = (int16_t)(c->max_mana + min_int((int16_t)rand_2bits(), (int16_t)(level - 1)));
    if (c->max_mana > MAX_MANA_LIMIT)
        c->max_mana = MAX_MANA_LIMIT;
    raise_maximum(c, STAT_ANTI_MAGIC, rand_2bits());
}

/* Champ gains a level of base skill base (level: the new level): its statistics rise and a message shows it. */
static void raise_level(int champ, int base, uint16_t level)
{
    /* the colors of the champion messages */
    static const uint8_t message_colors[CHAMPION_MAX] = {UI_GREEN, UI_YELLOW, UI_RED, UI_BLUE};
    Champion *c = champion(champ);
    uint32_t gain = level;
    int bit_a = rand_bit();
    int bit_b = rand_bit() + 1;
    int bit_c = rand_bit();
    if (base != SKILL_PRIEST)
        bit_c &= (int)gain;
    raise_maximum(c, STAT_VITALITY, bit_c);
    uint16_t stamina = (uint16_t)c->max_stamina;
    raise_maximum(c, STAT_ANTI_FIRE, rand_bit() & ~gain & BYTE_MASK);
    int text;
    switch (base) {
    case SKILL_FIGHTER:
        text = TEXT_LEVEL_FIGHTER;
        stamina = (uint16_t)(stamina >> FIGHTER_STAMINA_SHIFT);
        gain *= FIGHTER_HEALTH_FACTOR;
        raise_maximum(c, STAT_STRENGTH, bit_b);
        raise_maximum(c, STAT_DEXTERITY, bit_a);
        break;
    case SKILL_NINJA:
        text = TEXT_LEVEL_NINJA;
        stamina = (uint16_t)(stamina / NINJA_STAMINA_DIVISOR);
        gain *= 2;
        raise_maximum(c, STAT_STRENGTH, bit_a);
        raise_maximum(c, STAT_DEXTERITY, bit_b);
        break;
    case SKILL_PRIEST:
        text = TEXT_LEVEL_PRIEST;
        stamina = (uint16_t)(stamina / PRIEST_STAMINA_DIVISOR);
        c->max_mana = (int16_t)(c->max_mana + gain);
        gain += (gain + 1) >> 1;
        raise_maximum(c, STAT_WISDOM, bit_a);
        raise_magic(c, level);
        break;
    case SKILL_WIZARD:
    default:
        text = TEXT_LEVEL_WIZARD;
        stamina = (uint16_t)(stamina >> WIZARD_STAMINA_SHIFT);
        c->max_mana = (int16_t)(c->max_mana + gain + (gain >> 1));
        raise_maximum(c, STAT_WISDOM, bit_b);
        raise_magic(c, level);
        break;
    }
    raise_health_and_stamina(c, gain, stamina);
    c->redraw |= REDRAW_LEVEL;
    g_champ_skill_raised[champ][base]++;
    g_hooks.print_newline();
    g_text_args.champion = (int16_t)champ;
    char line[TEXT_SIZE];
    g_hooks.print_message(message_colors[champ], gtext_gdat(CAT_INTERFACE, 0, text, line));
}

void champ_add_experience(int champ, int skill, uint32_t experience)
{
    if (skill >= SKILL_BASE_COUNT && skill <= SKILL_LAST_FIGHT &&
        g_game.game_time - FIGHT_TIME_LONG > g_game.last_combat_time)
        experience >>= 1;
    if (!experience)
        return;
    int factor = dungeon_map_difficulty(g_party.cur_map);
    if (factor)
        experience *= (uint32_t)factor;
    Champion *c = champion(champ);
    int base = champ_base_skill(skill);
    uint16_t old = (uint16_t)champ_get_skill_level(champ, base, false);
    if (skill >= SKILL_BASE_COUNT && (int32_t)(g_game.game_time - FIGHT_TIME_SHORT) < (int32_t)g_game.last_combat_time)
        experience *= 2;
    c->experience[skill] += experience;
    if (skill >= SKILL_BASE_COUNT)
        c->experience[base] += experience;
    uint16_t level = (uint16_t)champ_get_skill_level(champ, base, false);
    while (old < level) {
        old++;
        raise_level(champ, base, level);
    }
}

/* The champion action that frightens the creature group in front with the power of the action; true when it
   worked. */
static bool frighten_front_group(const ActionRun *a)
{
    if (a->target == THING_NONE)
        return false;
    unsigned resist = creature_fear_resistance(creature_group_info(a->target));
    if (resist > rand_below((uint16_t)a->power) || resist == FRIGHTEN_IMMUNE)
        return false;
    /* fix: the original passes a leftover register (EDX) as the damage, no defined value; the port passes 0 */
    creature_alert(a->target, (Location){a->fx, a->fy}, FRIGHTEN_ALERT_FLAGS, FRIGHTEN_ALERT_CHANCE, 0);
    return true;
}

/* The damage of a hit with strength and the damage factor of the action against the defense of the creature;
   0 when a weak hit misses. */
static int16_t melee_hit_damage(int strength, uint16_t factor, const CreatureInfo *info, int difficulty)
{
    int16_t damage = 0;
    int16_t rest = 0; /* fix: the original reads rest uninitialized for a strength of 0 */
    if (strength) {
        damage = (int16_t)(strength + rand_below((uint16_t)((strength >> 1) + 1)));
        damage = (int16_t)((int)factor * damage >> MELEE_FACTOR_SHIFT);
        int defense = (int)(rand_next() & MELEE_RANDOM) + info->defense + difficulty;
        damage = (int16_t)(damage + (int)(rand_next() & MELEE_RANDOM) - defense);
        rest = damage;
        if (damage > 1)
            return damage;
    }
    int r = rand_2bits();
    if (!r)
        return 0;
    damage = (int16_t)(r + 1);
    rest = (int16_t)(rest + (rand_next() & MELEE_WEAK_RANDOM));
    if (rest > 0 || rand_bit()) {
        damage = (int16_t)(damage + rand_2bits());
        if (!rand_2bits())
            damage = (int16_t)(damage + max_int(0, (int16_t)(rest + (rand_next() & MELEE_WEAK_RANDOM))));
    }
    return damage;
}

/* A melee hit of the champion of the action on the creature group in front: attack sets the hit chance, factor the
   damage; pierces: it hits creatures that only piercing hits reach (CREATURE_NOT_DOOR_HIT). Returns the damage. */
static int melee_attack_group(const ActionRun *a, uint16_t attack, uint16_t factor, int kind, bool pierces)
{
    uint16_t group = a->target;
    int champ = a->champ;
    int skill = a->skill;
    int x = a->fx;
    int y = a->fy;
    Champion *c = a->c;
    if ((uint16_t)champ >= g_game.champion_count || !c->health)
        return 0;
    int difficulty = dungeon_map_difficulty(g_party.cur_map) * 2;
    const CreatureInfo *info = creature_group_info(group);
    int16_t damage = 0;
    int16_t stamina;
    bool hit = info->dexterity != DEXTERITY_UNHITTABLE;
    uint16_t at = creature_group_at(g_party.cur_map, x, y);
    if (hit && at != THING_NONE && creature_current_step_acts(at))
        hit = false;
    if (hit && info->flags & CREATURE_NOT_DOOR_HIT && !pierces)
        hit = false;
    if (hit) {
        int16_t chance = (int16_t)(((int)(rand_next() & MELEE_RANDOM) + info->dexterity + difficulty +
                                    g_view_light_level * 2 - MELEE_CHANCE_BASE) >>
                                   1);
        if ((uint16_t)champ_get_dexterity(champ) <= chance && rand_2bits())
            hit = champ_test_luck(c, (uint16_t)(MELEE_LUCK_BASE - attack));
    }
    if (hit) {
        damage = melee_hit_damage((int16_t)champ_get_strength(champ, g_action.hand, skill), factor, info, difficulty);
        hit = damage != 0;
    }
    if (hit) {
        damage = (int16_t)(damage >> 1);
        int r = rand_below((uint16_t)damage);
        damage = (int16_t)(damage + r + rand_2bits());
        damage = (int16_t)(damage + rand_below((uint16_t)damage));
        damage = (int16_t)(damage >> 2);
        damage = (int16_t)(damage + rand_2bits() + 1);
        unsigned roll = rand_next() & MELEE_CRITICAL_ROLL;
        if ((uint16_t)champ_get_skill_level(champ, skill, true) > roll)
            damage = (int16_t)(damage + damage + MELEE_CRITICAL_BONUS);
        uint16_t extra = item_attr(c->slots[g_action.hand], ATTR_EXTRA_DAMAGE);
        if (extra && damage > (int16_t)(rand_next() & MELEE_RANDOM))
            damage = (int16_t)(damage + creature_roll_armor_factor(group, extra));
        int worth = creature_experience(info);
        champ_add_experience(champ, skill,
                             (uint32_t)((worth * damage >> MELEE_EXPERIENCE_SHIFT) + MELEE_EXPERIENCE_BASE));
        stamina = (int16_t)(rand_2bits() + MELEE_STAMINA_HIT);
    } else {
        damage = 0;
        stamina = (int16_t)(rand_bit() + MELEE_STAMINA_MISS);
    }
    champ_decrease_stamina(champ, stamina);
    if (info->door_flags & CREATURE_RESISTS_NON_FIRE && kind != MELEE_KIND_FULL)
        damage = (int16_t)(damage >> (rand_bit() + 1));
    creature_alert(group, (Location){x, y}, MELEE_ALERT_FLAGS, MELEE_ALERT_CHANCE, (uint16_t)damage);
    return damage;
}

/* A melee attack of the champion of the action on the creature group in front; true when the attack happened. */
static bool melee_attack_front(const ActionRun *a, int mode, int kind)
{
    Champion *c = a->c;
    if (a->target == THING_NONE)
        return false;
    uint16_t group = creature_group_at(g_party.cur_map, a->fx, a->fy);
    if (group == THING_NONE)
        return false;
    int rel = dir_relative(c->cell, c->dir);
    if (rel > 1) {
        int other = dir_turn(c->cell, rel == 2 ? TURN_LEFT : TURN_RIGHT);
        if (champ_get_index_in_cell(other) != -1) {
            /* a champion in front of the attacker: it cannot reach */
            g_action.result = RESULT_BLOCKED;
            g_action.redraw_delay = (int16_t)max_int(
                1, (int16_t)(RESULT_DELAY_BASE - dun_modify_global_var(VAR_BLOCKED_ACTIONS, 1, GLOBAL_VAR_ADD)));
            return false;
        }
    }
    if ((int16_t)mode == MELEE_MODE_REACH && !(creature_group_info(a->target)->flags & CREATURE_MELEE_REACHES))
        return false;
    uint16_t attack = (uint16_t)field_of(a->action, FIELD_ATTACK);
    uint16_t wounds = (uint16_t)field_of(a->action, FIELD_WOUNDS);
    /* as in the original, bit 15 of the attack value also makes the hit pierce, and the hit chance ignores it */
    bool pierces = field_of(a->action, FIELD_PIERCES) != 0 || attack & MELEE_ATTACK_PIERCES;
    attack &= (uint16_t)~MELEE_ATTACK_PIERCES;
    g_action.result = (int16_t)melee_attack_group(a, attack, wounds, kind, pierces);
    return true;
}

/* Summons a scout, carrier, or army minion group at the party; when none comes, an explosion shows it. */
static void summon_minions(const ActionRun *a, int command)
{
    static const uint8_t summoned[] = {MINION_SCOUT, MINION_CARRIER, MINION_ARMY};
    CreatureSummon summon = {.creature_type = summoned[command - COMMAND_SUMMON_FIRST],
                             .level = (uint16_t)((int16_t)a->power >> SUMMON_POWER_SHIFT),
                             .dir = dir_opposite(g_game.party_dir),
                             .x = g_game.party_x,
                             .y = g_game.party_y,
                             .map = g_game.party_map,
                             .link = a->item,
                             .search_dir = (int8_t)g_game.party_dir};
    if (creature_summon_group(summon) == THING_NONE)
        explosion_create(EXPLOSION_SUMMON_FAIL, EXPLOSION_SUMMON_POWER, g_game.party_x, g_game.party_y, CENTER_CELL);
}

/* A new guard, fetch, or attack minion of the minion map: at the party, a fetch minion at its location; true when it
   came. */
static bool send_minion(ActionRun *a, int command)
{
    uint16_t map_item = a->item;
    int type = MINION_ATTACK;
    int x = g_game.party_x;
    int y = g_game.party_y;
    int map = g_game.party_map;
    int dir = dir_opposite(g_game.party_dir);
    int search = (int8_t)a->c->dir;
    if (command == COMMAND_MINION_FETCH) {
        uint16_t target = thing_word(map_item, MINION_MAP_LOCATION);
        type = MINION_FETCH;
        dir = g_game.party_dir;
        x = location_x(target);
        y = location_y(target);
        map = location_map(target);
        search = -1;
    } else if (command == COMMAND_MINION_GUARD) {
        type = MINION_GUARD;
    }
    int level = (uint16_t)champ_get_skill_level(a->champ, a->skill, true);
    CreatureSummon summon = {.creature_type = type,
                             .level = level,
                             .dir = dir,
                             .x = x,
                             .y = y,
                             .map = map,
                             .link = map_item,
                             .search_dir = search};
    uint16_t summoned = creature_summon_group(summon);
    if (summoned == THING_NONE) {
        a->experience = (int16_t)(a->experience >> 2);
        a->delay = (uint16_t)(a->delay >> 1);
        explosion_create(EXPLOSION_SUMMON_FAIL, EXPLOSION_SUMMON_POWER, x, y, CENTER_CELL);
        return false;
    }
    if (command == COMMAND_MINION_ATTACK)
        minion_map_set_location(map_item, (Position){g_party.dest_map, g_party.dest_x, g_party.dest_y});
    thing_set_word(map_item, MINION_MAP_GROUP, summoned);
    return true;
}

/* The minion and summoning actions; true when the action worked. */
static bool run_minion_command(ActionRun *a, int command)
{
    uint16_t map_item = a->item;
    uint16_t group = map_item != THING_NONE ? thing_word(map_item, MINION_MAP_GROUP) : THING_NONE;
    switch (command) {
    case COMMAND_MINION_MAP_SET:
        if (minion_map_set_location(map_item, (Position){g_game.party_map, g_game.party_x, g_game.party_y}))
            return true;
        a->experience = 0;
        a->delay = 1;
        return false;
    case COMMAND_MINION_DISMISS:
        minion_dismiss(group);
        return true;
    case COMMAND_SUMMON_FIRST:
    case COMMAND_SUMMON_2:
    case COMMAND_SUMMON_LAST:
        summon_minions(a, command);
        return true;
    case COMMAND_MINION_GUARD:
    case COMMAND_MINION_FETCH:
        if (item_find_link_projectile(group, map_item)) {
            group_set_creature(group, command == COMMAND_MINION_GUARD ? MINION_GUARD : MINION_FETCH);
            ai_free_cached_data(group);
            return true;
        }
        break;
    default: /* COMMAND_MINION_ATTACK */
        break;
    }
    return send_minion(a, command);
}

/* A failure that shows a picture in the action area; counter counts the failures in a script variable. */
static void show_failure(int result, int counter)
{
    int16_t count = (int16_t)dun_modify_global_var(counter, 1, GLOBAL_VAR_ADD);
    g_action.redraw_delay = (int16_t)max_int(1, (int16_t)(RESULT_DELAY_BASE - count));
    g_action.result = (int16_t)result;
}

/* The party becomes invisible for power game steps, at least POWER_MIN. */
static void run_invisibility(int16_t power)
{
    power = (int16_t)max_int(POWER_MIN, (int16_t)power);
    if (!g_game.magic_light[MAGIC_INVISIBILITY]++ && g_ui.inventory)
        champion(g_ui.inventory - 1)->redraw |= REDRAW_STATUS_BOX;
    /* fix: the original leaves the priority and the data bytes uninitialized */
    TimelineEvent e = {.time_map = timeline_time_map(g_game.game_time + power, g_game.party_map),
                       .type = EVENT_INVISIBILITY};
    timeline_add_event(&e);
}

/* Casts the projectile spell of the action; less mana than the cost makes the spell weaker. */
static bool run_spell_projectile(ActionRun *a)
{
    int spell = (uint16_t)(field_of(a->action, FIELD_MODE) - SPELL_BASE);
    int16_t cost = (int16_t)(SPELL_COST_BASE -
                             min_int(SPELL_COST_SKILL_MAX, (int16_t)champ_get_skill_level(a->champ, a->skill, true)));
    if (cost > a->c->mana) {
        a->power = (int16_t)max_int(SPELL_POWER_MIN, (int16_t)(a->power * a->c->mana / cost));
        cost = a->c->mana;
    }
    bool done = magic_cast_projectile_spell(a->champ, spell, a->power, (uint16_t)cost);
    if (!done)
        a->experience = (int16_t)(a->experience >> 1);
    return done;
}

/* True when the creature group is furniture that a blow does not hurt. */
static bool is_furniture(uint16_t group)
{
    int index = (int)(creature_group_info(group) - creature_info_at(0));
    for (size_t i = 0; i < sizeof furniture_infos; i++)
        if (furniture_infos[i] == index)
            return true;
    return false;
}

/* A melee action: it bashes a closed door in front, or attacks the creature group in front. */
static bool run_melee(ActionRun *a)
{
    if (square_type(a->front) == SQUARE_DOOR && (a->front & SQUARE_DOOR_STATE) == DOOR_STATE_CLOSED) {
        sensor_door_try_destroy(a->fx, a->fy, champ_get_strength(a->champ, a->hand, a->skill), 0, DOOR_BREAK_DELAY);
        a->delay = ACTION_DOOR_DELAY;
        sndreq_request_with_fallback(
            (SoundCue){CAT_DOOR, dun_get_door_type(dungeon_square_first_thing(g_party.cur_map, a->fx, a->fy)),
                       SOUND_HIT, SOUND_HIT_PRIORITY, SOUND_VOLUME_LOUD},
            SOUND_FALLBACK, a->fx, a->fy, 1);
        return true;
    }
    bool done = true;
    if (a->target != THING_NONE && is_furniture(a->target)) {
        a->experience = (int16_t)(a->experience >> 1);
    } else {
        done = melee_attack_front(a, field_of(a->action, FIELD_MODE), field_of(a->action, FIELD_MELEE_KIND));
        if (!done) {
            a->experience = (int16_t)(a->experience >> 1);
            a->delay = (uint16_t)(a->delay >> 1);
        }
    }
    if (done && a->target != THING_NONE)
        sndreq_request(
            (SoundCue){CAT_CREATURE, item_gdat_index(a->target), SOUND_HIT, SOUND_HIT_PRIORITY, SOUND_VOLUME_LOUD},
            a->fx, a->fy, 1);
    return done;
}

/* The party falls into the pit in front, when no solid creature stands over it; else the action takes no time. */
static void run_levitate_down(ActionRun *a)
{
    int px = g_game.party_x + g_dir_dx[g_game.party_dir];
    int py = g_game.party_y + g_dir_dy[g_game.party_dir];
    bool open = true;
    uint16_t g = creature_group_at(g_party.cur_map, px, py);
    if (g != THING_NONE && !(creature_group_info(g)->flags & CREATURE_NOT_SOLID))
        open = false;
    if (square_type(dun_get_square((Location){px, py})) == SQUARE_PIT && open) {
        g_party.fell = 1;
        move_thing(THING_NONE, g_game.party_x, g_game.party_y, px, py);
        g_party.fell = 0;
    } else {
        a->delay = 0;
    }
}

/* A statistic aura or a shield on the party. */
static void run_aura(ActionRun *a, int command)
{
    /* the statistic auras of COMMAND_AURA_FIRST to COMMAND_AURA_LAST */
    static const uint8_t auras[] = {AURA_WISDOM, AURA_DEXTERITY, AURA_VITALITY, AURA_STRENGTH};
    /* the shields of COMMAND_SHIELD_FIRST to COMMAND_SHIELD_LAST */
    static const uint8_t shields[] = {AURA_SPELL_SHIELD, AURA_FIRE_SHIELD, AURA_DEFENSE};
    bool shield = command >= COMMAND_SHIELD_FIRST;
    int aura = shield ? shields[command - COMMAND_SHIELD_FIRST] : auras[command - COMMAND_AURA_FIRST];
    a->power = (int16_t)(shield ? max_int(POWER_MIN, (int16_t)a->power) * SHIELD_POWER_FACTOR
                                : max_int(POWER_MIN, (int16_t)a->power) << AURA_POWER_SHIFT);
    if (!magic_cast_party_aura(a->c, aura, (uint16_t)a->power, shield)) {
        a->experience = (int16_t)(a->experience >> 2);
        a->delay = (uint16_t)(a->delay >> 1);
    }
}

/* The hand takes its item from the slot that it loads from. */
static void run_load(ActionRun *a)
{
    int slot = champ_find_hand_source_slot(a->champ, a->hand);
    if (slot >= 0) {
        a->item = champ_remove_object_from_slot(a->champ, slot);
        champ_add_object_to_slot(a->champ, a->item, a->hand);
    }
    a->silent = true;
}

/* The launcher in hand shoots the ammunition in the other hand; without ammunition the action area shows it. */
static bool run_shoot(ActionRun *a)
{
    int other = a->hand ^ 1;
    uint16_t ammo = a->c->slots[other];
    if (!item_is_ammo_for_hand(a->champ, a->hand, ammo)) {
        if (!a->repeated) {
            show_failure(RESULT_NO_AMMO, VAR_NO_AMMO_ACTIONS);
            g_action.result_ammo = item_attr(a->item, ATTR_AMMO);
            g_action.result_flag = (int16_t)other;
            a->experience = 0;
        }
        return false;
    }
    ammo = champ_remove_object_from_slot(a->champ, other);
    int level = champ_get_skill_level(a->champ, SKILL_SHOOT, true);
    uint16_t step = item_attr(ammo, ATTR_STEP);
    uint16_t attack = (uint16_t)(item_attr(a->item, ATTR_SHOOT_ATTACK) + level * 2);
    uint16_t kinetic =
        (uint16_t)(level + item_attr(a->item, ATTR_THROW_POWER_SHOOT) + item_attr(ammo, ATTR_THROW_POWER_SHOOT));
    projectile_launch_from_champion(a->champ, ammo, kinetic, attack, step);
    a->silent = true;
    return true;
}

/* The champion heals itself with its mana, step by step. */
static void run_heal(ActionRun *a)
{
    Champion *c = a->c;
    int16_t missing = (int16_t)(c->max_health - c->health);
    if (missing <= 0 || !c->mana)
        return;
    int16_t most = (int16_t)min_int(HEAL_PER_STEP_MAX, (int16_t)champ_get_skill_level(a->champ, SKILL_HEAL, true));
    a->experience = HEAL_EXPERIENCE;
    while (missing) {
        int16_t v = (int16_t)min_int((int16_t)missing, (int16_t)most);
        c->health = (int16_t)(c->health + v);
        a->experience = (int16_t)(a->experience + HEAL_EXPERIENCE);
        c->mana = (int16_t)(c->mana - HEAL_MANA);
        if (c->mana <= 0)
            break;
        missing = (int16_t)(missing - v);
    }
    if (c->mana < 0)
        c->mana = 0;
    c->redraw |= REDRAW_BARS;
}

/* The party magic of an action: invisibility, light, an explosion, haste, the fall into a pit, the action effect, a
   statistic aura, or a shield. Another command does nothing. */
static void run_party_magic(ActionRun *a, int command)
{
    switch (command) {
    case COMMAND_INVISIBILITY:
        run_invisibility(a->power);
        break;
    case COMMAND_LIGHT:
    case COMMAND_LIGHT_2:
    case COMMAND_LIGHT_3:
        magic_add_light(command, a->power);
        break;
    case COMMAND_EXPLOSION:
        explosion_create(EXPLOSION_COMMAND, (uint16_t)max_int(EXPLOSION_POWER_MIN, (int16_t)a->power), g_game.party_x,
                         g_game.party_y, CENTER_CELL);
        break;
    case COMMAND_HASTE:
        a->power = (int16_t)max_int(POWER_MIN, (int16_t)a->power);
        g_game.magic_light[MAGIC_HASTE] =
            (uint8_t)min_int(HASTE_MAX, (int16_t)(g_game.magic_light[MAGIC_HASTE] + a->power));
        break;
    case COMMAND_LEVITATE_DOWN:
        run_levitate_down(a);
        break;
    case COMMAND_ACTION_EFFECT:
        g_game.magic_light[MAGIC_ACTION_EFFECT] =
            (uint8_t)min_int(ACTION_EFFECT_MAX, (int16_t)(g_game.magic_light[MAGIC_ACTION_EFFECT] + a->power));
        break;
    case COMMAND_AURA_FIRST:
    case COMMAND_AURA_2:
    case COMMAND_AURA_3:
    case COMMAND_AURA_LAST:
    case COMMAND_SHIELD_FIRST:
    case COMMAND_SHIELD_2:
    case COMMAND_SHIELD_LAST:
        run_aura(a, command);
        break;
    default:
        break;
    }
}

/* The stamina that a throw of thing costs. */
static int throw_fatigue(uint16_t thing)
{
    int16_t left = (int16_t)((uint16_t)item_weight(thing) >> 1);
    uint16_t fatigue = (uint16_t)clamp_int(1, (int16_t)left, THROW_FATIGUE_STEP);
    while ((left = (int16_t)(left - THROW_FATIGUE_STEP)) > 0)
        fatigue = (uint16_t)(fatigue + (left >> 1));
    return (int16_t)fatigue;
}

/* Champ throws the item in hand (-1: the leader hand object) to the left or the right side; true when it flies. */
static bool throw_object(int champ, int16_t hand, bool right_side)
{
    Champion *c = champion(champ);
    bool from_leader_hand = false;
    uint16_t saved = THING_NONE;
    uint16_t thing = THING_NONE;
    if (hand < 0) {
        if (g_game.leader_hand == THING_NONE)
            return false;
        /* the strength reads the object in the action hand */
        saved = c->slots[SLOT_ACTION_HAND];
        thing = champ_take_from_leader_hand();
        c->slots[SLOT_ACTION_HAND] = thing;
        hand = SLOT_ACTION_HAND;
        from_leader_hand = true;
    }
    int16_t kinetic = (int16_t)champ_get_strength(champ, hand, SKILL_THROW);
    if (from_leader_hand) {
        c->slots[SLOT_ACTION_HAND] = saved;
    } else {
        thing = champ_remove_object_from_slot(champ, hand);
        if (thing == THING_NONE)
            return false;
    }
    champ_decrease_stamina(champ, throw_fatigue(thing));
    champ_add_action_delay(champ, THROW_DELAY, hand);
    int experience = THROW_EXPERIENCE;
    int power = 1;
    uint16_t throw_power = item_attr(thing, ATTR_THROW_POWER);
    if (throw_power) {
        experience = (throw_power >> THROW_EXPERIENCE_SHIFT) + THROW_EXPERIENCE_BASE;
        power = throw_power;
    }
    champ_add_experience(champ, SKILL_THROW, (uint16_t)experience);
    kinetic = (int16_t)(kinetic + power);
    int level = champ_get_skill_level(champ, SKILL_THROW, true);
    kinetic =
        (int16_t)(kinetic + rand_below((uint16_t)((kinetic >> THROW_KINETIC_SHIFT) + THROW_KINETIC_RANDOM)) + level);
    kinetic = (int16_t)min_int((int16_t)kinetic, KINETIC_MAX);
    int attack = clamp_int(ATTACK_MIN, (int16_t)((rand_next() & THROW_ATTACK_RANDOM) + level * THROW_ATTACK_PER_LEVEL),
                           ATTACK_MAX);
    int step = item_attr(thing, ATTR_STEP);
    if (!step)
        step = max_int(STEP_MIN, (int16_t)(STEP_BASE - level));
    projectile_create(&(ProjectileLaunch){.payload = thing,
                                          .x = g_game.party_x,
                                          .y = g_game.party_y,
                                          .cell = (int16_t)dir_turn(g_game.party_dir, right_side),
                                          .dir = g_game.party_dir,
                                          .energy = (uint8_t)kinetic,
                                          .hit_chance = (uint8_t)attack,
                                          .step = (uint8_t)step});
    g_game.throw_cooldown = THROW_COOLDOWN;
    g_game.throw_dir = g_game.party_dir;
    return true;
}

/* Runs the command of the action; true when the action was done. */
static bool run_command(ActionRun *a, int command)
{
    switch (command) {
    case COMMAND_FRIGHTEN: {
        a->power = (int16_t)(a->power + champ_get_skill_level(a->champ, SKILL_INFLUENCE, true));
        bool done = frighten_front_group(a);
        if (!done)
            a->experience = (int16_t)(a->experience >> 2);
        return done;
    }
    case COMMAND_SPELL_PROJECTILE:
        return run_spell_projectile(a);
    case COMMAND_MELEE:
    case COMMAND_MELEE_2:
        return run_melee(a);
    case COMMAND_CONSUME:
        champ_consume_object(a->champ, a->item, a->hand);
        return true;
    case COMMAND_LOAD:
        run_load(a);
        return true;
    case COMMAND_SHOOT:
        return run_shoot(a);
    case COMMAND_HEAL:
        run_heal(a);
        return true;
    case COMMAND_THROW: {
        bool right_side =
            a->c->cell == dir_turn(g_game.party_dir, TURN_RIGHT) || a->c->cell == dir_opposite(g_game.party_dir);
        return throw_object(a->champ, a->hand, right_side);
    }
    case COMMAND_MINION_MAP_SET:
    case COMMAND_MINION_GUARD:
    case COMMAND_MINION_FETCH:
    case COMMAND_MINION_ATTACK:
    case COMMAND_MINION_DISMISS:
    case COMMAND_SUMMON_FIRST:
    case COMMAND_SUMMON_2:
    case COMMAND_SUMMON_LAST:
        return run_minion_command(a, command);
    case COMMAND_WARP:
        if (!move_party_warp_to_marker())
            show_failure(RESULT_CANT, VAR_CANT_ACTIONS);
        return true;
    default:
        run_party_magic(a, command);
        return true;
    }
}

bool champ_perform_action(int champ, int index, bool repeated)
{
    ChampionAction action = g_champ_actions[index];
    int hand = g_action.hand;
    Champion *c = champion(champ);
    if (!c->health)
        return false;
    int command = field_of(action, FIELD_COMMAND);
    uint8_t sound = (uint8_t)field_of(action, FIELD_SOUND);
    c->last_command[hand] = (uint8_t)command;
    ActionRun a = {
        .champ = champ, .hand = hand, .repeated = repeated, .c = c, .item = c->slots[hand], .action = action};
    a.fx = (int16_t)(g_game.party_x + g_dir_dx[c->dir]);
    a.fy = (int16_t)(g_game.party_y + g_dir_dy[c->dir]);
    a.target = creature_group_at(g_party.cur_map, a.fx, a.fy);
    a.delay = (uint16_t)field_of(action, FIELD_DELAY);
    a.skill = (uint16_t)field_of(action, FIELD_SKILL);
    int16_t stamina = field_of(action, FIELD_STAMINA);
    stamina = (int16_t)(stamina + rand_bit());
    a.experience = field_of(action, FIELD_EXPERIENCE);
    a.power = field_of(action, FIELD_POWER);
    c->defense[hand] = (int8_t)field_of(action, FIELD_DEFENSE);
    a.front = dun_get_square((Location){a.fx, a.fy});
    bool done = run_command(&a, command);
    if (!a.silent)
        sndreq_request_with_fallback(
            (SoundCue){action.category, action.index, sound, SOUND_ACTION_PRIORITY, SOUND_VOLUME_LOUD}, SOUND_FALLBACK,
            g_game.party_x, g_game.party_y, 0);
    if (!c->health || repeated)
        return done;
    if (a.delay)
        champ_add_action_delay(champ, a.delay, hand);
    if (stamina)
        champ_decrease_stamina(champ, stamina);
    if (a.experience)
        champ_add_experience(champ, a.skill, (uint32_t)a.experience);
    if (done) {
        ActionRepeat *r = &g_action_repeats[champ][hand];
        r->left = (uint16_t)field_of(action, FIELD_REPEATS);
        if (r->left) {
            r->count = r->left;
            r->fresh = true;
            r->thing = a.item;
            r->action = (uint8_t)index;
        }
    }
    return done;
}

bool champ_execute_selected_action(int index)
{
    bool done = false;
    if (!g_action.champion)
        return false;
    int champ = g_action.champion - 1;
    if ((int16_t)index == -1) {
        g_action.flags = 0;
    } else {
        champion(champ)->redraw |= REDRAW_BARS;
        ChampionAction action = g_champ_actions[index];
        done = champ_perform_action(champ, index, false);
        g_champ_action_result = g_action.result;
        ActionRepeat *r = g_action.hand == SLOT_ACTION_HAND || g_action.hand == SLOT_READY_HAND
                              ? &g_action_repeats[champ][g_action.hand]
                              : NULL;
        bool use = !r || !r->left || (r->fresh && !rand_below((uint16_t)(r->count + 1)));
        if (use && champ_use_action_charge(champ, action) && r) {
            r->count = 0;
            r->left = 0;
            r->fresh = false;
        }
    }
    if (!g_action.flags)
        action_area_close();
    else
        g_action.flags &= (uint16_t)~FLAGS_KEEP_OPEN;
    g_hooks.update_action_area(0);
    return done;
}

void champ_process_repeated_actions(void)
{
    int16_t saved = -1;
    for (int champ = 0; champ < g_game.champion_count; champ++)
        for (int hand = 0; hand < SLOT_HAND_COUNT; hand++) {
            ActionRepeat *r = &g_action_repeats[champ][hand];
            if (!r->left)
                continue;
            if (!champion(champ)->health || r->thing != champion(champ)->slots[hand]) {
                r->left = 0;
                continue;
            }
            if (r->fresh) {
                r->fresh = false;
                continue;
            }
            r->left--;
            saved = g_action.hand;
            g_action.hand = (int16_t)hand;
            champ_list_actions(champ, r->thing, hand);
            ChampionAction action = g_champ_actions[r->action];
            champ_perform_action(champ, r->action, true);
            if (!rand_below((uint16_t)(r->count + 1)) && champ_use_action_charge(champ, action)) {
                r->count = 0;
                r->left = 0;
                r->fresh = false;
            }
            g_action.hand = saved;
        }
    if (g_action.champion && saved >= 0 && g_action.hand <= SLOT_ACTION_HAND)
        champ_list_actions(g_action.champion - 1, champion(g_action.champion - 1)->slots[g_action.hand], g_action.hand);
}

bool champ_throw_leader_hand_object(bool right_side)
{
    if (g_game.leader == LEADER_NONE)
        return false;
    return throw_object(champ_leader(), -1, right_side);
}
