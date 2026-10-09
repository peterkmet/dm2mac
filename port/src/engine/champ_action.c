/* The actions of the champions: the action menu, the actions, their delays and repeats, experience, throwing. */
#include "action.h"
#include "ai.h"
#include "champ_action.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "hooks.h"
#include "sound_request.h"
#include "projectile.h"
#include "inventory.h"
#include "item.h"
#include "magic.h"
#include "minion.h"
#include "move.h"
#include "projectile.h"
#include "random.h"
#include "sensor.h"
#include "game_text.h"
#include "ui.h"
#include "view.h"

#define ALL_CHARGES 0x0f
#define ATTR_USED_UP 0x0800         /* ATTR_FLAGS bit: the item vanishes with its last charge */
#define ATTR_THROW_POWER_SHOOT 9
#define ATTR_SHOOT_ATTACK 0x0a
#define ATTR_STEP 0x0c              /* item attribute: the flight step of a missile */
#define AMMO_KINDS 0x7fff
#define FLAGS_KEEP_OPEN 0x0400      /* action.flags bit that the action area clears after an action */
#define POWER_MIN 32
#define ACTION_EFFECT_MAX 200
#define HEAL_PER_STEP_MAX 10
#define HEAL_MANA 2
#define MAX_MANA_LIMIT 900         /* a level gain raises the maximum mana up to this */
#define HEALTH_MAX 999
#define STAMINA_MAX 9999
#define DELAY_MAX 255
#define ACTION_DOOR_DELAY 6
#define MELEE_PIERCES 0x8000        /* attack bit: the hit reaches creatures that CREATURE_NOT_DOOR_HIT marks */
#define MELEE_LUCK_BASE 75          /* a missed hit gets a luck test of 75 - attack */
#define MELEE_ALERT_FLAGS 0x6002
#define MELEE_ALERT_CHANCE 90
#define DEXTERITY_UNHITTABLE 0xff
#define MELEE_RANDOM 0x1f           /* the random part of the hit chance, the defense, the damage, and the armor test */
#define MELEE_CHANCE_BASE 16        /* the hit chance: (random + dexterity + difficulty + 2 * light - base) / 2 */
#define MELEE_FACTOR_SHIFT 5        /* the damage factor of an action is in 32nds */
#define MELEE_WEAK_RANDOM 0xf       /* the random part of the rest of a weak hit */
#define MELEE_CRITICAL_ROLL 0x3f    /* a skill level above a roll of 0 to 63 doubles the damage plus a bonus */
#define MELEE_CRITICAL_BONUS 10
#define MELEE_EXPERIENCE_SHIFT 4    /* the experience of a hit: factor * damage / 16 + base */
#define MELEE_EXPERIENCE_BASE 3
#define MELEE_STAMINA_HIT 4         /* the stamina cost of a hit: 4 to 7, of a miss: 2 to 3 */
#define MELEE_STAMINA_MISS 2
#define FRIGHTEN_IMMUNE 0xf
#define FRIGHTEN_ALERT_FLAGS 0x2005
#define FRIGHTEN_ALERT_CHANCE 100
#define SOUND_HIT 0x8d              /* sound entry of a door or creature that a melee action hits */
#define SOUND_HIT_PRIORITY 0x3c
#define SOUND_ACTION_PRIORITY 0x73
#define SPELL_BASE 0x80             /* FIELD_MODE of a projectile spell: the spell is the mode - SPELL_BASE */
#define THROW_ATTACK_RANDOM 0x1f    /* the random part of the attack of a throw */
#define THROW_DELAY 4
#define THROW_EXPERIENCE 8
#define KINETIC_MAX 255
#define ATTACK_MIN 40
#define ATTACK_MAX 200
#define STEP_MIN 5
#define STEP_BASE 11
#define RESULT_BLOCKED (-1)         /* action.result: a champion stands in front */
#define VAR_BLOCKED_COUNT 0x42      /* script variables that count the failed actions */
#define VAR_NO_AMMO_COUNT 0x43
#define VAR_CANT_COUNT 0x47
#define RESULT_DELAY_BASE 8
#define EXPLOSION_SUMMON_FAIL 0xffa8
#define EXPLOSION_COMMAND 0xff8e
#define EXPLOSION_SUMMON_POWER 110
#define CENTER_CELL 0xff
#define CREATURE_MELEE_REACHES 0x0020   /* creature info flag: mode 1 attacks reach the creature */
#define DOOR_BREAK_DELAY 2
#define SKILL_INFLUENCE 14
#define SKILL_HEAL 13
#define SKILL_LAST_FIGHT 11         /* the fighter and ninja sub-skills learn less away from fights */
#define FIGHT_TIME_LONG 150
#define FIGHT_TIME_SHORT 40
#define LEVEL_EXPERIENCE 512
#define REDRAW_LEVEL 0x3800
#define TEXT_LEVEL_FIRST 6          /* GDAT texts (1, 0, 6 to 9): a champion gains a level of a base skill */
#define DELAY_EXTRA_SHIFT 2         /* an action delay grows by a quarter */
#define HASTE_DELAY_SHIFT 2         /* haste cuts the delay to a quarter */
#define DELAY_BASE 2
/* The maximum stamina that a level of a base skill adds: max stamina / 16, / 21, / 25, / 32. */
#define FIGHTER_STAMINA_SHIFT 4
#define NINJA_STAMINA_DIVISOR 21
#define PRIEST_STAMINA_DIVISOR 25
#define WIZARD_STAMINA_SHIFT 5
#define FIGHTER_HEALTH_FACTOR 3     /* the health gain of a level: level * 3 for a fighter, * 2 for a ninja */
#define SUMMON_POWER_SHIFT 3        /* the level of a summoned group: the power / 8 */
#define MELEE_MODE_REACH 1          /* FIELD_MODE of an attack that reaches only CREATURE_MELEE_REACHES creatures */
#define MELEE_KIND_FULL 1           /* FIELD_MELEE_KIND: full damage to creatures that take half damage */
#define SPELL_COST_BASE 7           /* the mana of a projectile spell: 7 - the skill level, at least 1 */
#define SPELL_COST_SKILL_MAX 6
#define SPELL_POWER_MIN 2
#define EXPLOSION_POWER_MIN 2
#define SHIELD_POWER_FACTOR 3       /* the power of a shield: power * 3, of a statistic aura: power * 4 */
#define AURA_POWER_SHIFT 2
#define HEAL_EXPERIENCE 2           /* the experience of a heal: 2, and 2 for each healed step */
#define THROW_FATIGUE_STEP 10       /* a throw costs half the weight up to 10, then half of each further part */
#define THROW_EXPERIENCE_SHIFT 2    /* the experience of a throw: throw power / 4 + 12 */
#define THROW_EXPERIENCE_BASE 12
#define THROW_KINETIC_SHIFT 2       /* the random part of the kinetic energy: 0 to kinetic / 4 + 7 */
#define THROW_KINETIC_RANDOM 8
#define THROW_ATTACK_PER_LEVEL 8
enum { BASE_FIGHTER, BASE_NINJA, BASE_PRIEST, BASE_WIZARD };

ActionRepeat champ_action_repeat[CHAMPION_MAX][2];
/* The creature group in front of the champion of the current action. */
static uint16_t champ_action_target = THING_NONE;
/* The creature info records of things that a blow does not hurt (static, without the bytes 2 and 8 of a fighter), from SKULL.EXE. */
static const uint8_t furniture_infos[] = {0, 1, 2, 4, 5, 6, 7, 8, 10, 11, 12, 20, 37, 38, 39, 45, 51, 56, 60};

static bool creature_is_furniture(uint16_t group)
{
    int index = (int)(creature_group_info(group) - creature_info_at(0));
    for (size_t i = 0; i < sizeof furniture_infos; i++)
        if (furniture_infos[i] == index) return true;
    return false;
}

/* The action that champ_perform_action runs: its text. */
static ChampionAction current;

static int min16(int a, int b) { return (int16_t)a < (int16_t)b ? (int16_t)a : (int16_t)b; }
static int max16(int a, int b) { return (int16_t)a > (int16_t)b ? (int16_t)a : (int16_t)b; }
static int clamp16(int low, int v, int high) { return max16(low, min16(v, high)); }

static int16_t action_field(int field) { return text_gdat_field(current.category, current.index, current.entry, field); }

void action_area_open_hand(int champ, int hand)
{
    if (!champion(champ)->health || !champ_list_actions(champ, champion(champ)->slots[hand], hand)) return;
    action.flash = (int16_t)(champ + 1);
    action.redraw_delay = 1;
    action.flags = 0;
    action.flash_hand = (int16_t)hand;
    action.champion = action.flash;
    action.hand = (int16_t)hand;
    champ_drag_cancel();
    action_update_area(0);
    ui_draw_champion_state(champ);
}

void action_area_open_spells(int champ)
{
    if (!champion(champ)->health) return;
    action.hand = ACTION_HAND_SPELL;
    action.flash_hand = ACTION_HAND_SPELL;
    action.flash = (int16_t)(champ + 1);
    action.redraw_delay = 1;
    action.flags = 0;
    action.champion = action.flash;
    champ_drag_cancel();
    action_update_area(0);
}

void champ_add_action_delay(int champ, int ticks, int hand)
{
    Champion *c = champion(champ);
    uint16_t delay = (uint16_t)((uint16_t)ticks + ((uint16_t)ticks >> DELAY_EXTRA_SHIFT));
    int count = 1;
    if ((int16_t)hand == -1) {
        count = ACTION_DELAY_COUNT;
        hand = 0;
    }
    if (game.magic_light[MAGIC_HASTE]) delay = (uint16_t)(delay >> HASTE_DELAY_SHIFT);
    delay = (uint16_t)(delay + DELAY_BASE);
    for (; count; count--, hand++) {
        uint16_t old = c->action_delay[hand];
        uint16_t v = delay > old ? (uint16_t)(delay + (old >> 1)) : (uint16_t)(old + (delay >> 1));
        c->action_delay[hand] = (uint8_t)(v > DELAY_MAX ? DELAY_MAX : v);
    }
}

int champ_use_action_charge(int champ)
{
    int16_t charges = action_field(FIELD_CHARGES);
    if (charges == CHARGES_USE_ALL) charges = ALL_CHARGES;
    if (charges != CHARGES_USE_NONE && charges != CHARGES_EMPTY) item_add_charges(champ_action_thing, (int16_t)-charges);
    int empty = item_add_charges(champ_action_thing, 0) == 0;
    if (empty && item_attr(champ_action_thing, ATTR_FLAGS) & ATTR_USED_UP) {
        champ_remove_object_from_slot(champ, action.hand);
        set_thing_word(champ_action_thing, THING_NEXT, THING_NONE);
    }
    return empty;
}

bool item_is_ammo_for_hand(int champ, int hand, uint16_t thing)
{
    uint16_t launcher = champion(champ)->slots[hand];
    if (thing == THING_NONE || launcher == THING_NONE) return 0;
    uint16_t kinds = item_attr(launcher, ATTR_AMMO);
    if (!(kinds & AMMO_LAUNCHER)) return 0;
    uint16_t ammo = item_attr(thing, ATTR_AMMO);
    return !(ammo & AMMO_LAUNCHER) && ammo & AMMO_KINDS & kinds;
}

bool item_has_action_command(int champ, uint16_t thing, int command)
{
    int found = 0;
    if (thing == THING_NONE) return 0;
    if (champ_list_actions(champ, thing, -1)) {
        for (int i = 0; i < champ_action_count; i++)
            if ((uint16_t)text_gdat_field(champ_actions[i].category, champ_actions[i].index, champ_actions[i].entry,
                                          FIELD_COMMAND) == (uint16_t)command) {
                found = 1;
                break;
            }
    }
    if (action.champion && (action.hand == SLOT_READY_HAND || action.hand == SLOT_ACTION_HAND))
        champ_list_actions(action.champion - 1, champion(action.champion - 1)->slots[action.hand], action.hand);
    return found;
}

/* The first ammunition or throwing item for the hand of champ in the quiver (slot 12, the chest in it, slots 7 to 9),
   taken out; THING_NONE when there is none. throwing: a throwing item, else ammunition for the launcher in hand. */
static uint16_t take_next_missile(int champ, int hand, int throwing)
{
    Champion *c = champion(champ);
    int slot = SLOT_CONTAINER_TEST;
    int ok = throwing ? item_has_action_command(champ, c->slots[slot], COMMAND_THROW) : item_is_ammo_for_hand(champ, hand, c->slots[slot]);
    if (!ok) {
        uint16_t quiver = c->slots[SLOT_CONTAINER_TEST];
        if (item_is_chest(quiver)) {
            uint16_t *contents = container_contents(quiver);
            for (uint16_t t = *contents; t != THING_ENDOFLIST; t = dungeon_next_thing(t))
                if (throwing ? item_has_action_command(champ, t, COMMAND_THROW) : item_is_ammo_for_hand(champ, hand, t)) {
                    dungeon_unlink_thing_from_list(t, contents);
                    return t;
                }
        }
        for (slot = SLOT_QUIVER_FIRST + 1; slot <= SLOT_QUIVER_LAST; slot++)
            if (throwing ? item_has_action_command(champ, c->slots[slot], COMMAND_THROW)
                         : item_is_ammo_for_hand(champ, hand, c->slots[slot]))
                break;
        if (slot > SLOT_QUIVER_LAST) {
            if (!throwing) return THING_NONE;
            slot = SLOT_CONTAINER_TEST;  /* the original then takes what lies in the quiver slot */
        }
    }
    return champ_remove_object_from_slot(champ, slot);
}

void champ_reload_hand(int champ, int hand)
{
    Champion *c = champion(champ);
    c->action_delay[hand] = 0;
    if (!c->health || hand >= SLOT_HAND_COUNT) return;
    uint8_t command = c->last_command[hand];
    c->last_command[hand] = COMMAND_NONE;
    c->defense[hand] = 0;
    uint16_t t;
    int target;
    if (command == COMMAND_SHOOT) {
        if (c->slots[hand ^ 1] != THING_NONE) return;
        t = take_next_missile(champ, hand, 0);
        if (t == THING_NONE) return;
        target = hand ^ 1;
    } else if (command == COMMAND_THROW) {
        if (c->slots[hand] != THING_NONE) return;
        t = take_next_missile(champ, hand, 1);
        if (t == THING_NONE) return;
        target = hand;
    } else {
        return;
    }
    champ_add_object_to_slot(champ, t, target);
}

int champ_get_party_level(void)
{
    uint32_t sum = 0;
    for (int i = 0; i < game.champion_count; i++)
        for (int s = 0; s < SKILL_BASE_COUNT; s++) sum += champion(i)->experience[s];
    int level = 1;
    for (; sum >= LEVEL_EXPERIENCE; sum >>= 1) level++;
    return level;
}

/* Champ gains a level of base skill base (level: the new level): its statistics rise and a message shows it. */
static void champ_raise_level(int champ, int base, uint16_t level)
{
    /* the colors of the champion messages */
    static const uint8_t message_colors[CHAMPION_MAX] = {UI_GREEN, UI_YELLOW, UI_RED, UI_BLUE};
    Champion *c = champion(champ);
    uint32_t gain = level;
    int bit_a = rand_bit();
    int bit_b = rand_bit() + 1;
    int bit_c = rand_bit();
    if (base != BASE_PRIEST) bit_c &= (int)gain;
    c->stats[STAT_VITALITY][1] = (uint8_t)(c->stats[STAT_VITALITY][1] + bit_c);
    uint16_t stamina = (uint16_t)c->max_stamina;
    c->stats[STAT_ANTI_FIRE][1] = (uint8_t)(c->stats[STAT_ANTI_FIRE][1] + (rand_bit() & ~gain & BYTE_MASK));
    int text;
    switch (base) {
    case BASE_FIGHTER:
        text = TEXT_LEVEL_FIRST;
        stamina = (uint16_t)(stamina >> FIGHTER_STAMINA_SHIFT);
        gain *= FIGHTER_HEALTH_FACTOR;
        c->stats[STAT_STRENGTH][1] = (uint8_t)(c->stats[STAT_STRENGTH][1] + bit_b);
        c->stats[STAT_DEXTERITY][1] = (uint8_t)(c->stats[STAT_DEXTERITY][1] + bit_a);
        break;
    case BASE_NINJA:
        text = TEXT_LEVEL_FIRST + 1;
        stamina = (uint16_t)(stamina / NINJA_STAMINA_DIVISOR);
        gain *= 2;
        c->stats[STAT_STRENGTH][1] = (uint8_t)(c->stats[STAT_STRENGTH][1] + bit_a);
        c->stats[STAT_DEXTERITY][1] = (uint8_t)(c->stats[STAT_DEXTERITY][1] + bit_b);
        break;
    default:
        if (base == BASE_PRIEST) {
            text = TEXT_LEVEL_FIRST + 2;
            stamina = (uint16_t)(stamina / PRIEST_STAMINA_DIVISOR);
            c->max_mana = (int16_t)(c->max_mana + gain);
            gain += (gain + 1) >> 1;
            c->stats[STAT_WISDOM][1] = (uint8_t)(c->stats[STAT_WISDOM][1] + bit_a);
        } else {
            text = TEXT_LEVEL_FIRST + 3;
            stamina = (uint16_t)(stamina >> WIZARD_STAMINA_SHIFT);
            c->max_mana = (int16_t)(c->max_mana + gain + (gain >> 1));
            c->stats[STAT_WISDOM][1] = (uint8_t)(c->stats[STAT_WISDOM][1] + bit_b);
        }
        c->max_mana = (int16_t)(c->max_mana + min16(rand_2bits(), (int16_t)(level - 1)));
        if (c->max_mana > MAX_MANA_LIMIT) c->max_mana = MAX_MANA_LIMIT;
        c->stats[STAT_ANTI_MAGIC][1] = (uint8_t)(c->stats[STAT_ANTI_MAGIC][1] + rand_2bits());
        break;
    }
    c->max_health = (int16_t)(c->max_health + rand_below((uint16_t)((gain >> 1) + 1)) + gain);
    if (c->max_health > HEALTH_MAX) c->max_health = HEALTH_MAX;
    c->max_stamina = (int16_t)(c->max_stamina + rand_below((uint16_t)((stamina >> 1) + 1)) + stamina);
    if (c->max_stamina > STAMINA_MAX) c->max_stamina = STAMINA_MAX;
    c->redraw |= REDRAW_LEVEL;
    champ_skill_raised[champ][base]++;
    g_hooks.print_newline();
    text_args.champion = (int16_t)champ;
    char line[TEXT_SIZE];
    g_hooks.print_message(message_colors[champ], text_gdat(CAT_INTERFACE, 0, text, line));
}

void champ_add_experience(int champ, int skill, uint32_t amount)
{
    if (skill >= SKILL_BASE_COUNT && skill <= SKILL_LAST_FIGHT && game.game_time - FIGHT_TIME_LONG > game.last_combat_time)
        amount >>= 1;
    if (!amount) return;
    int factor = dungeon_map_difficulty(party.cur_map);
    if (factor) amount *= (uint32_t)factor;
    Champion *c = champion(champ);
    int base = champ_base_skill(skill);
    uint16_t old = (uint16_t)champ_get_skill_level(champ, base, 0);
    if (skill >= SKILL_BASE_COUNT && (int32_t)(game.game_time - FIGHT_TIME_SHORT) < (int32_t)game.last_combat_time)
        amount *= 2;
    c->experience[skill] += amount;
    if (skill >= SKILL_BASE_COUNT) c->experience[base] += amount;
    uint16_t level = (uint16_t)champ_get_skill_level(champ, base, 0);
    while (old < level) {
        old++;
        champ_raise_level(champ, base, level);
    }
}

/* The champion action that frightens the creature group in front at (x, y); returns 1 when it worked. */
static int champ_action_frighten(int power, int x, int y)
{
    if (champ_action_target == THING_NONE) return 0;
    unsigned resist = creature_fear_resistance(creature_group_info(champ_action_target));
    if (resist > rand_below((uint16_t)power) || resist == FRIGHTEN_IMMUNE) return 0;
    /* fix: the original passes a leftover register (EDX) as the damage, no defined value; the port passes 0 */
    creature_alert(champ_action_target, x, y, FRIGHTEN_ALERT_FLAGS, FRIGHTEN_ALERT_CHANCE, 0);
    return 1;
}

/* The damage of a hit with strength and the damage factor of the action against the defense of the creature;
   0 when a weak hit misses. */
static int16_t melee_hit_damage(int strength, uint16_t factor, const CreatureInfo *info, int difficulty)
{
    int16_t damage = 0, rest = 0;  /* fix: the original reads rest uninitialized for a strength of 0 */
    if (strength) {
        damage = (int16_t)(strength + rand_below((uint16_t)((strength >> 1) + 1)));
        damage = (int16_t)((int)factor * damage >> MELEE_FACTOR_SHIFT);
        int defense = (int)(rand_next() & MELEE_RANDOM) + info->defense + difficulty;
        damage = (int16_t)(damage + (int)(rand_next() & MELEE_RANDOM) - defense);
        rest = damage;
        if (damage > 1) return damage;
    }
    int r = rand_2bits();
    if (!r) return 0;
    damage = (int16_t)(r + 1);
    rest = (int16_t)(rest + (rand_next() & MELEE_WEAK_RANDOM));
    if (rest > 0 || rand_bit()) {
        damage = (int16_t)(damage + rand_2bits());
        if (!rand_2bits()) damage = (int16_t)(damage + max16(0, (int16_t)(rest + (rand_next() & MELEE_WEAK_RANDOM))));
    }
    return damage;
}

/* A melee hit of champion champ (record c) on the creature group at (x, y): attack (bit 15: it hits creatures that
   only piercing hits reach) sets the hit chance, factor the damage. Returns the damage. */
static int champ_melee_attack_creature(Champion *c, int champ, uint16_t group, int x, int y, uint16_t attack,
                                       uint16_t factor, int skill, int kind)
{
    if ((uint16_t)champ >= game.champion_count || !c->health) return 0;
    int difficulty = dungeon_map_difficulty(party.cur_map) * 2;
    const CreatureInfo *info = creature_group_info(group);
    int pierces = attack & MELEE_PIERCES;
    attack &= (uint16_t)~MELEE_PIERCES;
    int16_t damage = 0, stamina;
    int hit = info->dexterity != DEXTERITY_UNHITTABLE;
    uint16_t at = creature_group_at(party.cur_map, x, y);
    if (hit && at != THING_NONE && creature_current_step_acts(at)) hit = 0;
    if (hit && info->flags & CREATURE_NOT_DOOR_HIT && !pierces) hit = 0;
    if (hit) {
        int16_t chance = (int16_t)(((int)(rand_next() & MELEE_RANDOM) + info->dexterity + difficulty + view_light_level * 2 - MELEE_CHANCE_BASE) >> 1);
        if ((uint16_t)champ_get_dexterity(champ) <= chance && rand_2bits())
            hit = champ_test_luck(c, (uint16_t)(MELEE_LUCK_BASE - attack)) != 0;
    }
    if (hit) {
        damage = melee_hit_damage((int16_t)champ_get_strength(champ, action.hand, skill), factor, info, difficulty);
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
        if ((uint16_t)champ_get_skill_level(champ, skill, 1) > roll) damage = (int16_t)(damage + damage + MELEE_CRITICAL_BONUS);
        uint16_t extra = item_attr(c->slots[action.hand], ATTR_EXTRA_DAMAGE);
        if (extra && damage > (int16_t)(rand_next() & MELEE_RANDOM)) damage = (int16_t)(damage + creature_roll_armor_factor(group, extra));
        int worth = creature_experience(info);
        champ_add_experience(champ, skill, (uint32_t)((worth * damage >> MELEE_EXPERIENCE_SHIFT) + MELEE_EXPERIENCE_BASE));
        stamina = (int16_t)(rand_2bits() + MELEE_STAMINA_HIT);
    } else {
        damage = 0;
        stamina = (int16_t)(rand_bit() + MELEE_STAMINA_MISS);
    }
    champ_decrease_stamina(champ, stamina);
    if (info->door_flags & CREATURE_RESISTS_NON_FIRE && kind != MELEE_KIND_FULL) damage = (int16_t)(damage >> (rand_bit() + 1));
    creature_alert(group, x, y, MELEE_ALERT_FLAGS, MELEE_ALERT_CHANCE, (uint16_t)damage);
    return damage;
}

/* A melee attack of champ on the creature group at (x, y); returns 1 when the attack happened. */
static int champ_action_melee_attack(int champ, int mode, int x, int y, int skill, int kind)
{
    Champion *c = champion(champ);
    if (champ_action_target == THING_NONE) return 0;
    uint16_t group = creature_group_at(party.cur_map, x, y);
    if (group == THING_NONE) return 0;
    int rel = dir_relative(c->cell, c->dir);
    if (rel > 1) {
        int other = dir_turn(c->cell, rel == 2 ? TURN_LEFT : TURN_RIGHT);
        if (champ_get_index_in_cell(other) != -1) {
            /* a champion in front of the attacker: it cannot reach */
            action.result = RESULT_BLOCKED;
            action.redraw_delay = (int16_t)max16(1, (int16_t)(RESULT_DELAY_BASE - dun_modify_global_var(VAR_BLOCKED_COUNT, 1, GLOBAL_VAR_ADD)));
            return 0;
        }
    }
    if ((int16_t)mode == MELEE_MODE_REACH && !(creature_group_info(champ_action_target)->flags & CREATURE_MELEE_REACHES)) return 0;
    uint16_t attack = (uint16_t)action_field(FIELD_ATTACK);
    uint16_t wounds = (uint16_t)action_field(FIELD_WOUNDS);
    if (action_field(FIELD_PIERCES)) attack |= MELEE_PIERCES;
    action.result = (int16_t)champ_melee_attack_creature(c, champ, champ_action_target, x, y, attack, wounds, skill, kind);
    return 1;
}

/* The minion and summoning actions; returns 1 when the action worked. */
static int champ_action_minion(int champ, int command, uint16_t map_item, int skill, int power, int16_t *experience,
                               uint16_t *delay)
{
    uint16_t group = map_item != THING_NONE ? thing_word(map_item, MINION_MAP_GROUP) : THING_NONE;
    int type, x = game.party_x, y = game.party_y, map = game.party_map, dir = dir_opposite(game.party_dir);
    int search = (int8_t)champion(champ)->dir;
    switch (command) {
    case COMMAND_MINION_MAP_SET:
        if (minion_map_set_location(map_item, game.party_x, game.party_y, game.party_map)) return 1;
        *experience = 0;
        *delay = 1;
        return 0;
    case COMMAND_MINION_DISMISS:
        minion_dismiss(group);
        return 1;
    case COMMAND_SUMMON_FIRST: case COMMAND_SUMMON_2: case COMMAND_SUMMON_LAST: {
        static const uint8_t summoned[] = {MINION_SCOUT, MINION_CARRIER, MINION_ARMY};
        if (creature_summon_group(summoned[command - COMMAND_SUMMON_FIRST], (uint16_t)((int16_t)power >> SUMMON_POWER_SHIFT), dir,
                                  game.party_x, game.party_y, game.party_map, map_item, (int8_t)game.party_dir)
            == THING_NONE)
            explosion_create(EXPLOSION_SUMMON_FAIL, EXPLOSION_SUMMON_POWER, game.party_x, game.party_y, CENTER_CELL);
        return 1;
    }
    case COMMAND_MINION_GUARD: case COMMAND_MINION_FETCH:
        if (minion_find_link_projectile(group, map_item)) {
            group_set_creature(group, command == COMMAND_MINION_GUARD ? MINION_GUARD : MINION_FETCH);
            ai_free_cached_data(group);
            return 1;
        }
        if (command == COMMAND_MINION_FETCH) {
            uint16_t target = thing_word(map_item, MINION_MAP_LOCATION);
            type = MINION_FETCH;
            dir = game.party_dir;
            x = location_x(target);
            y = location_y(target);
            map = location_map(target);
            search = -1;
        } else {
            type = MINION_GUARD;
        }
        break;
    default:  /* COMMAND_MINION_ATTACK */
        type = MINION_ATTACK;
        break;
    }
    int level = (uint16_t)champ_get_skill_level(champ, skill, 1);
    uint16_t summoned = creature_summon_group(type, level, dir, x, y, map, map_item, search);
    if (summoned == THING_NONE) {
        *experience = (int16_t)(*experience >> 2);
        *delay = (uint16_t)(*delay >> 1);
        explosion_create(EXPLOSION_SUMMON_FAIL, EXPLOSION_SUMMON_POWER, x, y, CENTER_CELL);
        return 0;
    }
    if (command == COMMAND_MINION_ATTACK) minion_map_set_location(map_item, party.dest_x, party.dest_y, party.dest_map);
    set_thing_word(map_item, MINION_MAP_GROUP, summoned);
    return 1;
}

/* A failure that shows a picture in the action area; counter counts the failures in a script variable. */
static void show_failure(int result, int counter)
{
    int16_t count = (int16_t)dun_modify_global_var(counter, 1, GLOBAL_VAR_ADD);
    action.redraw_delay = (int16_t)max16(1, (int16_t)(RESULT_DELAY_BASE - count));
    action.result = (int16_t)result;
}

/* The action that champ_perform_action runs: the champion, its hand and item, and what the action gives. */
typedef struct {
    int champ, hand, skill, repeated;
    Champion *c;
    uint16_t item;
    int fx, fy;                 /* the square in front of the champion */
    uint8_t front;              /* the square byte in front */
    int16_t power, experience;
    uint16_t delay;
    int silent;                 /* the action plays no sound */
} ActionRun;

/* The party becomes invisible for power game steps, at least POWER_MIN. */
static void action_invisibility(int16_t power)
{
    power = (int16_t)max16(POWER_MIN, power);
    if (!game.magic_light[MAGIC_INVISIBILITY]++ && ui.inventory) champion(ui.inventory - 1)->redraw |= REDRAW_STATUS_BOX;
    /* fix: the original leaves the priority and the data bytes uninitialized */
    TimelineEvent e = {.time_map = timeline_time_map(game.game_time + power, game.party_map), .type = EVENT_INVISIBILITY};
    timeline_add_event(&e);
}

/* Casts the projectile spell of the action; less mana than the cost makes the spell weaker. */
static int action_spell_projectile(ActionRun *a)
{
    int spell = (uint16_t)(action_field(FIELD_MODE) - SPELL_BASE);
    int16_t cost = (int16_t)(SPELL_COST_BASE - min16(SPELL_COST_SKILL_MAX, champ_get_skill_level(a->champ, a->skill, 1)));
    if (cost > a->c->mana) {
        a->power = (int16_t)max16(SPELL_POWER_MIN, (int16_t)(a->power * a->c->mana / cost));
        cost = a->c->mana;
    }
    int done = magic_cast_projectile_spell(a->champ, spell, a->power, (uint16_t)cost);
    if (!done) a->experience = (int16_t)(a->experience >> 1);
    return done;
}

/* A melee action: it bashes a closed door in front, or attacks the creature group in front. */
static int action_melee(ActionRun *a)
{
    if (square_type(a->front) == SQUARE_DOOR && (a->front & SQUARE_DOOR_STATE) == DOOR_STATE_CLOSED) {
        sensor_door_try_destroy(a->fx, a->fy, champ_get_strength(a->champ, a->hand, a->skill), 0, DOOR_BREAK_DELAY);
        a->delay = ACTION_DOOR_DELAY;
        sound_request_with_fallback(CAT_DOOR, dun_get_door_type(dungeon_square_first_thing(party.cur_map, a->fx, a->fy)),
                                 SOUND_HIT, SOUND_FALLBACK, a->fx, a->fy, 1, SOUND_HIT_PRIORITY, SOUND_VOLUME_LOUD);
        return 1;
    }
    int done = 1;
    if (champ_action_target != THING_NONE && creature_is_furniture(champ_action_target)) {
        a->experience = (int16_t)(a->experience >> 1);
    } else {
        done = champ_action_melee_attack(a->champ, action_field(FIELD_MODE), a->fx, a->fy, a->skill,
                                         action_field(FIELD_MELEE_KIND));
        if (!done) {
            a->experience = (int16_t)(a->experience >> 1);
            a->delay = (uint16_t)(a->delay >> 1);
        }
    }
    if (done && champ_action_target != THING_NONE)
        sound_request(CAT_CREATURE, item_gdat_index(champ_action_target), SOUND_HIT, SOUND_HIT_PRIORITY, SOUND_VOLUME_LOUD,
                      a->fx, a->fy, 1);
    return done;
}

/* The party falls into the pit in front, when no solid creature stands over it; else the action takes no time. */
static void action_levitate_down(ActionRun *a)
{
    int px = game.party_x + dir_dx[game.party_dir], py = game.party_y + dir_dy[game.party_dir];
    int open = 1;
    uint16_t g = creature_group_at(party.cur_map, px, py);
    if (g != THING_NONE && !(creature_group_info(g)->flags & CREATURE_NOT_SOLID)) open = 0;
    if (square_type(dun_get_square(px, py)) == SQUARE_PIT && open) {
        party.fell = 1;
        move_thing(THING_NONE, game.party_x, game.party_y, px, py);
        party.fell = 0;
    } else {
        a->delay = 0;
    }
}

/* A statistic aura or a shield on the party. */
static void action_aura(ActionRun *a, int command)
{
    static const uint8_t auras[] = {5, 4, 6, 3};      /* the statistic auras of commands 0x0c to 0x0f */
    static const uint8_t shields[] = {1, 0, 2};      /* the shields of commands 0x21 to 0x23 */
    int shield = command >= COMMAND_SHIELD_FIRST;
    int aura = shield ? shields[command - COMMAND_SHIELD_FIRST] : auras[command - COMMAND_AURA_FIRST];
    a->power = (int16_t)(shield ? max16(POWER_MIN, a->power) * SHIELD_POWER_FACTOR
                                : max16(POWER_MIN, a->power) << AURA_POWER_SHIFT);
    if (!magic_cast_party_aura(a->c, aura, (uint16_t)a->power, shield)) {
        a->experience = (int16_t)(a->experience >> 2);
        a->delay = (uint16_t)(a->delay >> 1);
    }
}

/* The hand takes its item from the slot that it loads from. */
static void action_load(ActionRun *a)
{
    int slot = champ_find_hand_source_slot(a->champ, a->hand);
    if (slot >= 0) {
        a->item = champ_remove_object_from_slot(a->champ, slot);
        champ_add_object_to_slot(a->champ, a->item, a->hand);
    }
    a->silent = 1;
}

/* The launcher in hand shoots the ammunition in the other hand; without ammunition the action area shows it. */
static int action_shoot(ActionRun *a)
{
    int other = a->hand ^ 1;
    uint16_t ammo = a->c->slots[other];
    if (!item_is_ammo_for_hand(a->champ, a->hand, ammo)) {
        if (!a->repeated) {
            show_failure(RESULT_NO_AMMO, VAR_NO_AMMO_COUNT);
            action.result_ammo = item_attr(a->item, ATTR_AMMO);
            action.result_flag = (int16_t)other;
            a->experience = 0;
        }
        return 0;
    }
    ammo = champ_remove_object_from_slot(a->champ, other);
    int level = champ_get_skill_level(a->champ, SKILL_SHOOT, 1);
    uint16_t step = item_attr(ammo, ATTR_STEP);
    uint16_t attack = (uint16_t)(item_attr(a->item, ATTR_SHOOT_ATTACK) + level * 2);
    uint16_t kinetic = (uint16_t)(level + item_attr(a->item, ATTR_THROW_POWER_SHOOT) + item_attr(ammo, ATTR_THROW_POWER_SHOOT));
    champ_launch_projectile(a->champ, ammo, kinetic, attack, step);
    a->silent = 1;
    return 1;
}

/* The champion heals itself with its mana, step by step. */
static void action_heal(ActionRun *a)
{
    Champion *c = a->c;
    int16_t missing = (int16_t)(c->max_health - c->health);
    if (missing <= 0 || !c->mana) return;
    int16_t most = (int16_t)min16(HEAL_PER_STEP_MAX, champ_get_skill_level(a->champ, SKILL_HEAL, 1));
    a->experience = HEAL_EXPERIENCE;
    do {
        int16_t v = (int16_t)min16(missing, most);
        c->health = (int16_t)(c->health + v);
        a->experience = (int16_t)(a->experience + HEAL_EXPERIENCE);
        c->mana = (int16_t)(c->mana - HEAL_MANA);
        if (c->mana <= 0) break;
        missing = (int16_t)(missing - v);
    } while (missing);
    if (c->mana < 0) c->mana = 0;
    c->redraw |= REDRAW_BARS;
}

/* Runs the command of the action; returns 1 when the action was done. */
static int champ_run_command(ActionRun *a, int command)
{
    switch (command) {
    case COMMAND_FRIGHTEN: {
        a->power = (int16_t)(a->power + champ_get_skill_level(a->champ, SKILL_INFLUENCE, 1));
        int done = champ_action_frighten(a->power, a->fx, a->fy);
        if (!done) a->experience = (int16_t)(a->experience >> 2);
        return done;
    }
    case COMMAND_INVISIBILITY: action_invisibility(a->power); return 1;
    case COMMAND_SPELL_PROJECTILE: return action_spell_projectile(a);
    case COMMAND_MELEE: case COMMAND_MELEE_2: return action_melee(a);
    case COMMAND_LIGHT: case COMMAND_LIGHT_2: case COMMAND_LIGHT_3: magic_add_light(command, a->power); return 1;
    case COMMAND_EXPLOSION:
        explosion_create(EXPLOSION_COMMAND, (uint16_t)max16(EXPLOSION_POWER_MIN, a->power), game.party_x, game.party_y,
                         CENTER_CELL);
        return 1;
    case COMMAND_HASTE:
        a->power = (int16_t)max16(POWER_MIN, a->power);
        game.magic_light[MAGIC_HASTE] = (uint8_t)min16(HASTE_MAX, (int16_t)(game.magic_light[MAGIC_HASTE] + a->power));
        return 1;
    case COMMAND_LEVITATE_DOWN: action_levitate_down(a); return 1;
    case COMMAND_ACTION_EFFECT:
        game.magic_light[MAGIC_ACTION_EFFECT] = (uint8_t)min16(ACTION_EFFECT_MAX, game.magic_light[MAGIC_ACTION_EFFECT] + a->power);
        return 1;
    case COMMAND_AURA_FIRST: case COMMAND_AURA_FIRST + 1: case COMMAND_AURA_FIRST + 2: case COMMAND_AURA_LAST:
    case COMMAND_SHIELD_FIRST: case COMMAND_SHIELD_2: case COMMAND_SHIELD_LAST:
        action_aura(a, command);
        return 1;
    case COMMAND_CONSUME: champ_consume_object(a->champ, a->item, a->hand); return 1;
    case COMMAND_LOAD: action_load(a); return 1;
    case COMMAND_SHOOT: return action_shoot(a);
    case COMMAND_HEAL: action_heal(a); return 1;
    case COMMAND_THROW: {
        int side = a->c->cell == dir_turn(game.party_dir, TURN_RIGHT) || a->c->cell == dir_opposite(game.party_dir);
        return champ_throw_object(a->champ, a->hand, side);
    }
    case COMMAND_MINION_MAP_SET: case COMMAND_MINION_GUARD: case COMMAND_MINION_FETCH: case COMMAND_MINION_ATTACK:
    case COMMAND_MINION_DISMISS: case COMMAND_SUMMON_FIRST: case COMMAND_SUMMON_2: case COMMAND_SUMMON_LAST:
        return champ_action_minion(a->champ, command, a->item, a->skill, a->power, &a->experience, &a->delay);
    case COMMAND_WARP:
        if (!move_party_warp_to_marker()) show_failure(RESULT_CANT, VAR_CANT_COUNT);
        return 1;
    default:
        return 1;
    }
}

int champ_perform_action(int champ, int index)
{
    int repeated = index & ACTION_REPEATED;
    index &= ~ACTION_REPEATED;
    current = champ_actions[index];
    int hand = action.hand;
    Champion *c = champion(champ);
    if (!c->health) return 0;
    int command = action_field(FIELD_COMMAND);
    uint8_t sound = (uint8_t)action_field(FIELD_SOUND);
    c->last_command[hand] = (uint8_t)command;
    ActionRun a = {.champ = champ, .hand = hand, .repeated = repeated, .c = c, .item = c->slots[hand]};
    a.fx = (int16_t)(game.party_x + dir_dx[c->dir]);
    a.fy = (int16_t)(game.party_y + dir_dy[c->dir]);
    champ_action_target = creature_group_at(party.cur_map, a.fx, a.fy);
    a.delay = (uint16_t)action_field(FIELD_DELAY);
    a.skill = (uint16_t)action_field(FIELD_SKILL);
    int16_t stamina = action_field(FIELD_STAMINA);
    stamina = (int16_t)(stamina + rand_bit());
    a.experience = action_field(FIELD_EXPERIENCE);
    a.power = action_field(FIELD_POWER);
    c->defense[hand] = (int8_t)action_field(FIELD_DEFENSE);
    a.front = dun_get_square(a.fx, a.fy);
    int done = champ_run_command(&a, command);
    if (!a.silent)
        sound_request_with_fallback(current.category, current.index, sound, SOUND_FALLBACK, game.party_x, game.party_y, 0,
                                 SOUND_ACTION_PRIORITY, SOUND_VOLUME_LOUD);
    if (!c->health || repeated) return done;
    if (a.delay) champ_add_action_delay(champ, a.delay, hand);
    if (stamina) champ_decrease_stamina(champ, stamina);
    if (a.experience) champ_add_experience(champ, a.skill, (uint32_t)a.experience);
    if (done) {
        ActionRepeat *r = &champ_action_repeat[champ][hand];
        r->left = (uint16_t)action_field(FIELD_REPEATS);
        if (r->left) {
            r->count = r->left;
            r->fresh = 1;
            r->thing = a.item;
            r->action = (uint8_t)index;
        }
    }
    return done;
}

int champ_execute_selected_action(int index)
{
    int done = 0;
    if (!action.champion) return 0;
    int champ = action.champion - 1;
    if ((int16_t)index == -1) {
        action.flags = 0;
    } else {
        champion(champ)->redraw |= REDRAW_BARS;
        done = champ_perform_action(champ, index);
        ActionRepeat *r = action.hand == SLOT_ACTION_HAND || action.hand == SLOT_READY_HAND ? &champ_action_repeat[champ][action.hand]
                                                                                            : NULL;
        int use = !r || !r->left || (r->fresh && !rand_below((uint16_t)(r->count + 1)));
        if (use && champ_use_action_charge(champ) && r) {
            r->count = r->left = 0;
            r->fresh = 0;
        }
    }
    if (!action.flags) action_area_close();
    else action.flags &= (uint16_t)~FLAGS_KEEP_OPEN;
    action_update_area(0);
    return done;
}

void champ_process_repeated_actions(void)
{
    int16_t saved = -1;
    for (int champ = 0; champ < game.champion_count; champ++)
        for (int hand = 0; hand < SLOT_HAND_COUNT; hand++) {
            ActionRepeat *r = &champ_action_repeat[champ][hand];
            if (!r->left) continue;
            if (!champion(champ)->health || r->thing != champion(champ)->slots[hand]) {
                r->left = 0;
                continue;
            }
            if (r->fresh) {
                r->fresh = 0;
                continue;
            }
            r->left--;
            saved = action.hand;
            action.hand = (int16_t)hand;
            champ_list_actions(champ, r->thing, hand);
            champ_perform_action(champ, r->action | ACTION_REPEATED);
            if (!rand_below((uint16_t)(r->count + 1)) && champ_use_action_charge(champ)) {
                r->count = r->left = 0;
                r->fresh = 0;
            }
            action.hand = saved;
        }
    if (action.champion && saved >= 0 && action.hand <= SLOT_ACTION_HAND)
        champ_list_actions(action.champion - 1, champion(action.champion - 1)->slots[action.hand], action.hand);
}

/* The stamina that a throw of thing costs. */
static int item_get_throw_fatigue(uint16_t thing)
{
    int16_t left = (int16_t)((uint16_t)item_weight(thing) >> 1);
    uint16_t fatigue = (uint16_t)clamp16(1, left, THROW_FATIGUE_STEP);
    while ((left = (int16_t)(left - THROW_FATIGUE_STEP)) > 0) fatigue = (uint16_t)(fatigue + (left >> 1));
    return (int16_t)fatigue;
}

int champ_throw_object(int champ, int hand, int side)
{
    Champion *c = champion(champ);
    int from_leader_hand = 0;
    uint16_t saved = THING_NONE, thing = THING_NONE;
    if ((int16_t)hand < 0) {
        if (game.leader_hand == THING_NONE) return 0;
        /* the strength reads the object in the action hand */
        saved = c->slots[SLOT_ACTION_HAND];
        thing = champ_remove_object_from_leader_hand();
        c->slots[SLOT_ACTION_HAND] = thing;
        hand = SLOT_ACTION_HAND;
        from_leader_hand = 1;
    }
    int16_t kinetic = (int16_t)champ_get_strength(champ, hand, SKILL_THROW);
    if (from_leader_hand) {
        c->slots[SLOT_ACTION_HAND] = saved;
    } else {
        thing = champ_remove_object_from_slot(champ, hand);
        if (thing == THING_NONE) return 0;
    }
    champ_decrease_stamina(champ, item_get_throw_fatigue(thing));
    champ_add_action_delay(champ, THROW_DELAY, hand);
    int experience = THROW_EXPERIENCE, power = 1;
    uint16_t throw_power = item_attr(thing, ATTR_THROW_POWER);
    if (throw_power) {
        experience = (throw_power >> THROW_EXPERIENCE_SHIFT) + THROW_EXPERIENCE_BASE;
        power = throw_power;
    }
    champ_add_experience(champ, SKILL_THROW, (uint16_t)experience);
    kinetic = (int16_t)(kinetic + power);
    int level = champ_get_skill_level(champ, SKILL_THROW, 1);
    kinetic = (int16_t)(kinetic + rand_below((uint16_t)((kinetic >> THROW_KINETIC_SHIFT) + THROW_KINETIC_RANDOM)) + level);
    kinetic = (int16_t)min16(kinetic, KINETIC_MAX);
    int attack = clamp16(ATTACK_MIN, (int16_t)((rand_next() & THROW_ATTACK_RANDOM) + level * THROW_ATTACK_PER_LEVEL), ATTACK_MAX);
    int step = item_attr(thing, ATTR_STEP);
    if (!step) step = max16(STEP_MIN, (int16_t)(STEP_BASE - level));
    projectile_create(thing, game.party_x, game.party_y, dir_turn(game.party_dir, side), game.party_dir, (uint8_t)kinetic,
                      (uint8_t)attack, (uint8_t)step);
    game.throw_cooldown = THROW_COOLDOWN;
    game.throw_dir = game.party_dir;
    return 1;
}

int champ_throw_leader_hand_object(int side)
{
    if ((int16_t)game.leader == -1) return 0;
    return champ_throw_object((int16_t)game.leader, -1, side);
}
