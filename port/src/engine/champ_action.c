/* The actions of the champions: the action menu, the actions, their delays and repeats, experience, throwing. */
#include "action.h"
#include "ai.h"
#include "champ_action.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "hooks.h"
#include "inventory.h"
#include "item.h"
#include "magic.h"
#include "minion.h"
#include "move.h"
#include "projectile.h"
#include "random.h"
#include "sensor.h"
#include "text.h"
#include "timeline.h"
#include "ui.h"
#include "view.h"

/* Fields of an action text (category, index, 5, entry). */
enum {
    FIELD_SKILL = 0, FIELD_COMMAND = 2, FIELD_DELAY = 3, FIELD_STAMINA = 4, FIELD_POWER = 5, FIELD_MODE = 6,
    FIELD_DEFENSE = 7, FIELD_CHARGES = 8, FIELD_EXPERIENCE = 9, FIELD_ATTACK = 10, FIELD_WOUNDS = 11,
    FIELD_SOUND = 13, FIELD_REPEATS = 14, FIELD_PIERCES = 15,
    FIELD_MELEE_KIND = 0x10     /* 1: full damage to creatures that take half damage from other hits */
};
/* Action commands. */
enum {
    COMMAND_INVISIBILITY = 2, COMMAND_SPELL_PROJECTILE, COMMAND_MELEE, COMMAND_FRIGHTEN, COMMAND_LIGHT,
    COMMAND_EXPLOSION, COMMAND_MELEE_2, COMMAND_HASTE, COMMAND_LEVITATE_DOWN, COMMAND_ACTION_EFFECT,
    COMMAND_AURA_FIRST = 0x0c, COMMAND_AURA_LAST = 0x0f, COMMAND_CONSUME = 0x10, COMMAND_LOAD = 0x11,
    COMMAND_SHOOT = 0x20, COMMAND_SHIELD_FIRST = 0x21, COMMAND_SHIELD_2, COMMAND_SHIELD_LAST, COMMAND_HEAL,
    COMMAND_LIGHT_2 = 0x26, COMMAND_LIGHT_3, COMMAND_THROW = 0x2a, COMMAND_MINION_MAP_SET = 0x2c,
    COMMAND_MINION_GUARD, COMMAND_MINION_FETCH, COMMAND_MINION_ATTACK, COMMAND_MINION_DISMISS,
    COMMAND_SUMMON_FIRST, COMMAND_SUMMON_2, COMMAND_SUMMON_LAST, COMMAND_WARP = 0x36
};
/* Charges of an action text (FIELD_CHARGES). */
enum { CHARGES_ALL = 0x10, CHARGES_NONE = 0x11, CHARGES_NONE_2 = 0x12 };
#define ALL_CHARGES 0x0f
#define ATTR_USED_UP 0x0800         /* ATTR_FLAGS bit: the item vanishes with its last charge */
#define ATTR_AMMO 5
#define ATTR_THROW_POWER_SHOOT 9
#define ATTR_SHOOT_ATTACK 10
#define ATTR_STEP 12                /* item attribute: the flight step of a missile */
#define AMMO_LAUNCHER 0x8000        /* ATTR_AMMO bit of a launcher; the low bits are the ammunition kinds */
#define AMMO_KINDS 0x7fff
#define FLAGS_KEEP_OPEN 0x0400      /* action.flags bit that the action area clears after an action */
#define POWER_MIN 0x20
#define HASTE_MAX 0xff
#define ACTION_EFFECT_MAX 200
#define HEAL_PER_STEP_MAX 10
#define HEAL_MANA 2
#define MANA_MAX 900
#define HEALTH_MAX 999
#define STAMINA_MAX 9999
#define DELAY_MAX 0xff
#define ACTION_DOOR_DELAY 6
#define MELEE_PIERCES 0x8000        /* attack bit: the hit reaches creatures that CREATURE_NOT_DOOR_HIT marks */
#define MELEE_LUCK_BASE 0x4b        /* a missed hit gets a luck test of 75 - attack */
#define MELEE_ALERT_FLAGS 0x6002
#define MELEE_ALERT_CHANCE 0x5a
#define DEXTERITY_UNHITTABLE 0xff
#define MELEE_RANDOM 0x1f           /* the random part of the hit chance, the defense, the damage, and the armor test */
#define MELEE_CHANCE_BASE 16        /* the hit chance: (random + dexterity + difficulty + 2 * light - base) / 2 */
#define MELEE_FACTOR_SHIFT 5        /* the damage factor of an action is in 32nds */
#define MELEE_WEAK_RANDOM 0xf       /* the random part of the rest of a weak hit */
#define MELEE_CRITICAL_ROLL 0x3f    /* a skill level above a roll of 0 to 63 doubles the damage plus a bonus */
#define MELEE_CRITICAL_BONUS 10
#define CREATURE_EXPERIENCE_SHIFT 8 /* creature info speed bits 8-11: the experience factor of a hit */
#define CREATURE_EXPERIENCE_MASK 0xf
#define MELEE_EXPERIENCE_SHIFT 4    /* the experience of a hit: factor * damage / 16 + base */
#define MELEE_EXPERIENCE_BASE 3
#define MELEE_STAMINA_HIT 4         /* the stamina cost of a hit: 4 to 7, of a miss: 2 to 3 */
#define MELEE_STAMINA_MISS 2
#define MAP_DIFFICULTY_SHIFT 12     /* map object counts: difficulty bits 12-15 */
#define ATTR_ANTI_ARMOR 0x0d        /* item attribute: the weapon breaks creature armor */
#define FRIGHTEN_RESIST_SHIFT 4     /* creature info speed bits 4-7: the resistance to fear, 15 immune */
#define FRIGHTEN_RESIST_MASK 0xf
#define FRIGHTEN_IMMUNE 0xf
#define FRIGHTEN_ALERT_FLAGS 0x2005
#define FRIGHTEN_ALERT_CHANCE 100
#define CAT_DOOR 0x0e
#define CAT_CREATURE 0x0f
#define SOUND_HIT 0x8d              /* sound entry of a door or creature that a melee action hits */
#define SOUND_FALLBACK 0xfe
#define SOUND_HIT_PRIORITY 0x3c
#define SOUND_ACTION_PRIORITY 0x73
#define SOUND_VOLUME 200
#define SPELL_BASE 0x80             /* FIELD_MODE of a projectile spell: the spell is the mode - SPELL_BASE */
#define THROW_ATTACK_RANDOM 0x1f    /* the random part of the attack of a throw */
#define THROW_DELAY 4
#define THROW_COOLDOWN 4
#define THROW_EXPERIENCE 8
#define KINETIC_MAX 0xff
#define ATTACK_MIN 0x28
#define ATTACK_MAX 200
#define STEP_MIN 5
#define STEP_BASE 11
#define RESULT_BLOCKED (-1)         /* action.result: a champion stands in front */
#define RESULT_NO_AMMO (-2)
#define RESULT_CANT (-6)
#define VAR_BLOCKED_COUNT 0x42      /* script variables that count the failed actions */
#define VAR_NO_AMMO_COUNT 0x43
#define VAR_CANT_COUNT 0x47
#define VAR_ADD 3
#define RESULT_DELAY_BASE 8
#define EXPLOSION_SUMMON_FAIL 0xffa8
#define EXPLOSION_COMMAND 0xff8e
#define EXPLOSION_SUMMON_POWER 0x6e
#define CENTER_CELL 0xff
#define CREATURE_MELEE_REACHES 0x0020   /* creature info flag: mode 1 attacks reach the creature */
#define MINION_ATTACKER 0x32        /* creature types of minions */
#define MINION_GUARD 0x33
#define MINION_FETCHER 0x30
#define MINION_SCOUT 0x31
#define MINION_CARRIER 0x34
#define MINION_ARMY 0x35
#define LOCATION_X_MASK 0x1f
#define LOCATION_MAP_SHIFT 10
#define DOOR_CLOSED 4
#define DOOR_BREAK_DELAY 2
#define SKILL_INFLUENCE 14
#define SKILL_HEAL 13
#define SKILL_THROW 10
#define SKILL_SHOOT 11
#define SKILL_SHIFT 2
#define SKILL_FIRST_SUB 4
#define SKILL_LAST_FIGHT 11         /* the fighter and ninja sub-skills learn less away from fights */
#define FIGHT_TIME_LONG 150
#define FIGHT_TIME_SHORT 40
#define MAP_EXPERIENCE_SHIFT 12     /* map object counts bits 12-15: the experience factor of the map */
#define LEVEL_EXPERIENCE 0x200
#define REDRAW_LEVEL 0x3800
#define REDRAW_BARS_FLAG 0x0800
#define REDRAW_INVENTORY_BOX 0x4000
#define TEXT_LEVEL_FIRST 6          /* GDAT texts (1, 0, 6 to 9): a champion gains a level of a base skill */
enum { BASE_FIGHTER, BASE_NINJA, BASE_PRIEST, BASE_WIZARD };

ActionRepeat champ_action_repeat[CHAMPION_MAX][2];
/* The creature group in front of the champion of the current action. */
static uint16_t champ_action_target = THING_NONE;
/* The creature info records of things that a blow does not hurt (static, without the bytes 2 and 8 of a fighter). */
static const uint8_t furniture_infos[] = {0, 1, 2, 4, 5, 6, 7, 8, 10, 11, 12, 20, 37, 38, 39, 45, 51, 56, 60};

static int creature_is_furniture(uint16_t group)
{
    int index = (int)(creature_group_info(group) - creature_info_at(0));
    for (size_t i = 0; i < sizeof furniture_infos; i++)
        if (furniture_infos[i] == index) return 1;
    return 0;
}

/* The action that champ_perform_action runs: its text. */
static ChampionAction current;

static int min16(int a, int b) { return (int16_t)a < (int16_t)b ? (int16_t)a : (int16_t)b; }
static int max16(int a, int b) { return (int16_t)a > (int16_t)b ? (int16_t)a : (int16_t)b; }
static int clamp16(int low, int v, int high) { return max16(low, min16(v, high)); }

static int16_t action_field(int field) { return text_gdat_field(current.category, current.index, current.entry, field); }

void ui_action_area_open_hand(int champ, int hand)
{
    if (!champion(champ)->health || !champ_list_actions(champ, champion(champ)->slots[hand], hand)) return;
    action.flash = (int16_t)(champ + 1);
    action.redraw_delay = 1;
    action.flags = 0;
    action.flash_hand = (int16_t)hand;
    action.champion = action.flash;
    action.hand = (int16_t)hand;
    champ_drag_cancel();
    ui_update_action_area(0);
    champ_draw_state(champ);
}

void ui_action_area_open_spells(int champ)
{
    if (!champion(champ)->health) return;
    action.hand = ACTION_HAND_SPELL;
    action.flash_hand = ACTION_HAND_SPELL;
    action.flash = (int16_t)(champ + 1);
    action.redraw_delay = 1;
    action.flags = 0;
    action.champion = action.flash;
    champ_drag_cancel();
    ui_update_action_area(0);
}

void champ_add_action_delay(int champ, int ticks, int hand)
{
    Champion *c = champion(champ);
    uint16_t delay = (uint16_t)((uint16_t)ticks + ((uint16_t)ticks >> 2));
    int count = 1;
    if ((int16_t)hand == -1) {
        count = 3;
        hand = 0;
    }
    if (game.magic_light[MAGIC_HASTE]) delay = (uint16_t)(delay >> 2);
    delay = (uint16_t)(delay + 2);
    for (; count; count--, hand++) {
        uint16_t old = c->action_delay[hand];
        uint16_t v = delay > old ? (uint16_t)(delay + (old >> 1)) : (uint16_t)(old + (delay >> 1));
        c->action_delay[hand] = (uint8_t)(v > DELAY_MAX ? DELAY_MAX : v);
    }
}

int champ_use_action_charge(int champ)
{
    int16_t charges = action_field(FIELD_CHARGES);
    if (charges == CHARGES_ALL) charges = ALL_CHARGES;
    if (charges != CHARGES_NONE && charges != CHARGES_NONE_2) item_add_charges(champ_action_thing, (int16_t)-charges);
    int empty = item_add_charges(champ_action_thing, 0) == 0;
    if (empty && item_attr(champ_action_thing, ATTR_FLAGS) & ATTR_USED_UP) {
        champ_remove_object_from_slot(champ, action.hand);
        set_thing_word(champ_action_thing, THING_NEXT, THING_NONE);
    }
    return empty;
}

int item_is_ammo_for_hand(int champ, int hand, uint16_t thing)
{
    uint16_t launcher = champion(champ)->slots[hand];
    if (thing == THING_NONE || launcher == THING_NONE) return 0;
    uint16_t kinds = item_attr(launcher, ATTR_AMMO);
    if (!(kinds & AMMO_LAUNCHER)) return 0;
    uint16_t ammo = item_attr(thing, ATTR_AMMO);
    return !(ammo & AMMO_LAUNCHER) && ammo & AMMO_KINDS & kinds;
}

int item_has_action_command(int champ, uint16_t thing, int command)
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
    if (action.champion && (action.hand == 0 || action.hand == 1))
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
            uint16_t *contents = (uint16_t *)(void *)(dungeon_thing_record(quiver) + CONTAINER_CONTENTS);
            for (uint16_t t = *contents; t != THING_ENDOFLIST; t = dungeon_next_thing(t))
                if (throwing ? item_has_action_command(champ, t, COMMAND_THROW) : item_is_ammo_for_hand(champ, hand, t)) {
                    dungeon_unlink_thing_from_list(t, (uint8_t *)contents);
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
    c->last_command[hand] = 0xff;
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

void champ_add_experience(int champ, int skill, uint32_t amount)
{
    /* the colors of the champion messages */
    static const uint8_t message_colors[CHAMPION_MAX] = {UI_GREEN, UI_YELLOW, UI_RED, UI_BLUE};
    if (skill >= SKILL_FIRST_SUB && skill <= SKILL_LAST_FIGHT && game.game_time - FIGHT_TIME_LONG > game.last_combat_time)
        amount >>= 1;
    if (!amount) return;
    int factor = dungeon_map_object_counts(party.cur_map) >> MAP_EXPERIENCE_SHIFT;
    if (factor) amount *= (uint32_t)factor;
    Champion *c = champion(champ);
    int base = skill >= SKILL_FIRST_SUB ? (skill - SKILL_FIRST_SUB) >> SKILL_SHIFT : skill;
    uint16_t old = (uint16_t)champ_get_skill_level(champ, base, 0);
    if (skill >= SKILL_FIRST_SUB && (int32_t)(game.game_time - FIGHT_TIME_SHORT) < (int32_t)game.last_combat_time)
        amount *= 2;
    c->experience[skill] += amount;
    if (skill >= SKILL_FIRST_SUB) c->experience[base] += amount;
    uint16_t level = (uint16_t)champ_get_skill_level(champ, base, 0);
    while (old < level) {
        uint32_t gain = level;
        old++;
        int bit_a = rand_bit();
        int bit_b = rand_bit() + 1;
        int bit_c = rand_bit();
        if (base != BASE_PRIEST) bit_c &= (int)gain;
        c->stats[STAT_VITALITY][1] = (uint8_t)(c->stats[STAT_VITALITY][1] + bit_c);
        uint16_t stamina = (uint16_t)c->max_stamina;
        c->stats[STAT_ANTI_FIRE][1] = (uint8_t)(c->stats[STAT_ANTI_FIRE][1] + (rand_bit() & ~gain & 0xff));
        int text;
        switch (base) {
        case BASE_FIGHTER:
            text = TEXT_LEVEL_FIRST;
            stamina = (uint16_t)(stamina >> 4);
            gain *= 3;
            c->stats[STAT_STRENGTH][1] = (uint8_t)(c->stats[STAT_STRENGTH][1] + bit_b);
            c->stats[STAT_DEXTERITY][1] = (uint8_t)(c->stats[STAT_DEXTERITY][1] + bit_a);
            break;
        case BASE_NINJA:
            text = TEXT_LEVEL_FIRST + 1;
            stamina = (uint16_t)(stamina / 21);
            gain *= 2;
            c->stats[STAT_STRENGTH][1] = (uint8_t)(c->stats[STAT_STRENGTH][1] + bit_a);
            c->stats[STAT_DEXTERITY][1] = (uint8_t)(c->stats[STAT_DEXTERITY][1] + bit_b);
            break;
        default:
            if (base == BASE_PRIEST) {
                text = TEXT_LEVEL_FIRST + 2;
                stamina = (uint16_t)(stamina / 25);
                c->max_mana = (int16_t)(c->max_mana + gain);
                gain += (gain + 1) >> 1;
                c->stats[STAT_WISDOM][1] = (uint8_t)(c->stats[STAT_WISDOM][1] + bit_a);
            } else {
                text = TEXT_LEVEL_FIRST + 3;
                stamina = (uint16_t)(stamina >> 5);
                c->max_mana = (int16_t)(c->max_mana + gain + (gain >> 1));
                c->stats[STAT_WISDOM][1] = (uint8_t)(c->stats[STAT_WISDOM][1] + bit_b);
            }
            c->max_mana = (int16_t)(c->max_mana + min16(rand_2bits(), (int16_t)(level - 1)));
            if (c->max_mana > MANA_MAX) c->max_mana = MANA_MAX;
            c->stats[STAT_ANTI_MAGIC][1] = (uint8_t)(c->stats[STAT_ANTI_MAGIC][1] + rand_2bits());
            break;
        }
        c->max_health = (int16_t)(c->max_health + rand_below((uint16_t)((gain >> 1) + 1)) + gain);
        if (c->max_health > HEALTH_MAX) c->max_health = HEALTH_MAX;
        c->max_stamina = (int16_t)(c->max_stamina + rand_below((uint16_t)((stamina >> 1) + 1)) + stamina);
        if (c->max_stamina > STAMINA_MAX) c->max_stamina = STAMINA_MAX;
        c->redraw |= REDRAW_LEVEL;
        champ_skill_raised[champ][base]++;
        text_print_newline();
        text_args.champion = (int16_t)champ;
        char line[TEXT_SIZE];
        text_print_message(message_colors[champ], text_gdat(CAT_INTERFACE, 0, text, line));
    }
}

/* The champion action that frightens the creature group in front at (x, y); returns 1 when it worked. */
static int champ_action_frighten(int power, int x, int y)
{
    if (champ_action_target == THING_NONE) return 0;
    unsigned resist = creature_group_info(champ_action_target)->speed >> FRIGHTEN_RESIST_SHIFT & FRIGHTEN_RESIST_MASK;
    if (resist > rand_below((uint16_t)power) || resist == FRIGHTEN_IMMUNE) return 0;
    /* fix: the original passes a leftover register (EDX) as the damage, no defined value; the port passes 0 */
    creature_alert(champ_action_target, x, y, FRIGHTEN_ALERT_FLAGS, FRIGHTEN_ALERT_CHANCE, 0);
    return 1;
}

/* A melee hit of champion champ (record c) on the creature group at (x, y): attack (bit 15: it hits creatures that
   only piercing hits reach) sets the hit chance, factor the damage. Returns the damage. */
static int champ_melee_attack_creature(Champion *c, int champ, uint16_t group, int x, int y, uint16_t attack,
                                       uint16_t factor, int skill, int kind)
{
    if ((uint16_t)champ >= game.champion_count || !c->health) return 0;
    int difficulty = (dungeon_map_object_counts(party.cur_map) >> MAP_DIFFICULTY_SHIFT) * 2;
    const CreatureInfo *info = creature_group_info(group);
    int pierces = attack & MELEE_PIERCES;
    attack &= (uint16_t)~MELEE_PIERCES;
    int16_t damage = 0, stamina, rest = 0;  /* fix: the original reads rest uninitialized for a strength of 0 */
    int hit = info->dexterity != DEXTERITY_UNHITTABLE;
    uint16_t at = creature_group_at(party.cur_map, x, y);
    if (hit && at != THING_NONE && creature_current_step_acts(at)) hit = 0;
    if (hit && info->flags & CREATURE_NOT_DOOR_HIT && !pierces) hit = 0;
    if (hit) {
        int16_t chance = (int16_t)(((int)(rand_next() & MELEE_RANDOM) + info->dexterity + difficulty + view_light_level * 2 - MELEE_CHANCE_BASE) >> 1);
        if ((int)(uint16_t)champ_get_dexterity(champ) <= chance && rand_2bits())
            hit = champ_test_luck(c, (uint16_t)(MELEE_LUCK_BASE - attack)) != 0;
    }
    if (hit) {
        int strength = (int16_t)champ_get_strength(champ, action.hand, skill);
        int big = 0;
        if (strength) {
            damage = (int16_t)(strength + rand_below((uint16_t)((strength >> 1) + 1)));
            damage = (int16_t)((int)factor * damage >> MELEE_FACTOR_SHIFT);
            int defense = (int)(rand_next() & MELEE_RANDOM) + info->defense + difficulty;
            damage = (int16_t)(damage + (int)(rand_next() & MELEE_RANDOM) - defense);
            rest = damage;
            big = damage > 1;
        }
        if (!big) {
            int r = rand_2bits();
            if (!r) {
                hit = 0;
            } else {
                damage = (int16_t)(r + 1);
                rest = (int16_t)(rest + (rand_next() & MELEE_WEAK_RANDOM));
                if (rest > 0 || rand_bit()) {
                    damage = (int16_t)(damage + rand_2bits());
                    if (!rand_2bits()) damage = (int16_t)(damage + max16(0, (int16_t)(rest + (rand_next() & MELEE_WEAK_RANDOM))));
                }
            }
        }
    }
    if (hit) {
        damage = (int16_t)(damage >> 1);
        int r = rand_below((uint16_t)damage);
        damage = (int16_t)(damage + r + rand_2bits());
        damage = (int16_t)(damage + rand_below((uint16_t)damage));
        damage = (int16_t)(damage >> 2);
        damage = (int16_t)(damage + rand_2bits() + 1);
        unsigned roll = rand_next() & MELEE_CRITICAL_ROLL;
        if ((unsigned)(uint16_t)champ_get_skill_level(champ, skill, 1) > roll) damage = (int16_t)(damage + damage + MELEE_CRITICAL_BONUS);
        uint16_t armor = item_attr(c->slots[action.hand], ATTR_ANTI_ARMOR);
        if (armor && damage > (int16_t)(rand_next() & MELEE_RANDOM)) damage = (int16_t)(damage + creature_roll_armor_factor(group, armor));
        int worth = info->speed >> CREATURE_EXPERIENCE_SHIFT & CREATURE_EXPERIENCE_MASK;
        champ_add_experience(champ, skill, (uint32_t)((worth * damage >> MELEE_EXPERIENCE_SHIFT) + MELEE_EXPERIENCE_BASE));
        stamina = (int16_t)(rand_2bits() + MELEE_STAMINA_HIT);
    } else {
        damage = 0;
        stamina = (int16_t)(rand_bit() + MELEE_STAMINA_MISS);
    }
    champ_decrease_stamina(champ, stamina);
    if (info->door_flags & CREATURE_DOOR_HALF_DAMAGE && kind != 1) damage = (int16_t)(damage >> (rand_bit() + 1));
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
    int rel = (c->cell + DIR_COUNT - c->dir) & 3;
    if (rel > 1) {
        int other = (c->cell + (rel == 2 ? 3 : 1)) & 3;
        if (champ_get_index_in_cell(other) != -1) {
            /* a champion in front of the attacker: it cannot reach */
            action.result = RESULT_BLOCKED;
            action.redraw_delay = (int16_t)max16(1, (int16_t)(RESULT_DELAY_BASE - dun_modify_global_var(VAR_BLOCKED_COUNT, 1, VAR_ADD)));
            return 0;
        }
    }
    if ((int16_t)mode == 1 && !(creature_group_info(champ_action_target)->flags & CREATURE_MELEE_REACHES)) return 0;
    uint16_t attack = (uint16_t)action_field(FIELD_ATTACK);
    uint16_t wounds = (uint16_t)action_field(FIELD_WOUNDS);
    if (action_field(FIELD_PIERCES)) attack |= 0x8000;
    action.result = (int16_t)champ_melee_attack_creature(c, champ, champ_action_target, x, y, attack, wounds, skill, kind);
    return 1;
}

/* The minion and summoning actions; returns 1 when the action worked. */
static int champ_action_minion(int champ, int command, uint16_t map_item, int skill, int power, int16_t *experience,
                               uint16_t *delay)
{
    uint8_t *map_record = map_item != THING_NONE ? dungeon_thing_record(map_item) : NULL;
    uint16_t group = map_record ? thing_word(map_item, MINION_MAP_GROUP) : THING_NONE;
    int type, x = game.party_x, y = game.party_y, map = game.party_map, dir = (game.party_dir + 2) & 3;
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
        if (creature_summon_group(summoned[command - COMMAND_SUMMON_FIRST], (uint16_t)((int16_t)power >> 3), dir,
                                  game.party_x, game.party_y, game.party_map, map_item, (int8_t)game.party_dir)
            == THING_NONE)
            explosion_create(EXPLOSION_SUMMON_FAIL, EXPLOSION_SUMMON_POWER, game.party_x, game.party_y, CENTER_CELL);
        return 1;
    }
    case COMMAND_MINION_GUARD: case COMMAND_MINION_FETCH:
        if (minion_find_link_projectile(group, map_item)) {
            group_set_creature(group, command == COMMAND_MINION_GUARD ? MINION_ATTACKER : MINION_GUARD);
            creature_free_cached_ai_data(group);
            return 1;
        }
        if (command == COMMAND_MINION_FETCH) {
            uint16_t target = thing_word(map_item, MINION_MAP_LOCATION);
            type = MINION_GUARD;
            dir = game.party_dir;
            x = target & LOCATION_X_MASK;
            y = target >> 5 & LOCATION_X_MASK;
            map = target >> LOCATION_MAP_SHIFT;
            search = -1;
        } else {
            type = MINION_ATTACKER;
        }
        break;
    default:  /* COMMAND_MINION_ATTACK */
        type = MINION_FETCHER;
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
    int16_t count = (int16_t)dun_modify_global_var(counter, 1, VAR_ADD);
    action.redraw_delay = (int16_t)max16(1, (int16_t)(RESULT_DELAY_BASE - count));
    action.result = (int16_t)result;
}

int champ_perform_action(int champ, int index)
{
    int repeated = index & ACTION_REPEATED;
    index &= ~ACTION_REPEATED;
    current = champ_actions[index];
    int hand = action.hand, other = hand ^ 1;
    Champion *c = champion(champ);
    if (!c->health) return 0;
    int command = action_field(FIELD_COMMAND);
    uint8_t sound = (uint8_t)action_field(FIELD_SOUND);
    int silent = 0;
    c->last_command[hand] = (uint8_t)command;
    uint16_t item = c->slots[hand];
    int fx = (int16_t)(game.party_x + dir_dx[c->dir]), fy = (int16_t)(game.party_y + dir_dy[c->dir]);
    champ_action_target = creature_group_at(party.cur_map, fx, fy);
    uint16_t delay = (uint16_t)action_field(FIELD_DELAY);
    int skill = (uint16_t)action_field(FIELD_SKILL);
    int16_t stamina = action_field(FIELD_STAMINA);
    stamina = (int16_t)(stamina + rand_bit());
    int16_t experience = action_field(FIELD_EXPERIENCE);
    int16_t power = action_field(FIELD_POWER);
    c->defense[hand] = (int8_t)action_field(FIELD_DEFENSE);
    uint8_t front = dun_get_square(fx, fy);
    int done = 1;
    switch (command) {
    case COMMAND_FRIGHTEN:
        power = (int16_t)(power + champ_get_skill_level(champ, SKILL_INFLUENCE, 1));
        done = champ_action_frighten(power, fx, fy);
        if (!done) experience = (int16_t)(experience >> 2);
        break;
    case COMMAND_INVISIBILITY: {
        power = (int16_t)max16(POWER_MIN, power);
        if (!game.magic_light[MAGIC_INVISIBILITY]++ && ui.inventory) champion(ui.inventory - 1)->redraw |= REDRAW_INVENTORY_BOX;
        /* fix: the original leaves the priority and the data bytes uninitialized */
        TimelineEvent e = {.time_map = (game.game_time + power) | (uint32_t)(uint8_t)game.party_map << 24,
                           .type = EVENT_INVISIBILITY};
        timeline_add_event(&e);
        break;
    }
    case COMMAND_SPELL_PROJECTILE: {
        int spell = (uint16_t)(action_field(FIELD_MODE) - SPELL_BASE);
        int16_t cost = (int16_t)(7 - min16(6, champ_get_skill_level(champ, skill, 1)));
        if (cost > c->mana) {
            power = (int16_t)max16(2, (int16_t)(power * c->mana / cost));
            cost = c->mana;
        }
        done = magic_cast_projectile_spell(champ, spell, power, (uint16_t)cost);
        if (!done) experience = (int16_t)(experience >> 1);
        break;
    }
    case COMMAND_MELEE: case COMMAND_MELEE_2:
        if (square_type(front) == SQUARE_DOOR && (front & SQUARE_DOOR_STATE) == DOOR_CLOSED) {
            sensor_door_try_destroy(fx, fy, champ_get_strength(champ, hand, skill), 0, DOOR_BREAK_DELAY);
            delay = ACTION_DOOR_DELAY;
            sound_play_with_fallback(CAT_DOOR, dun_get_door_type(dungeon_square_first_thing(party.cur_map, fx, fy)),
                                     SOUND_HIT, SOUND_FALLBACK, fx, fy, 1, SOUND_HIT_PRIORITY, SOUND_VOLUME);
            break;
        }
        if (champ_action_target != THING_NONE && creature_is_furniture(champ_action_target)) {
            experience = (int16_t)(experience >> 1);
        } else {
            done = champ_action_melee_attack(champ, action_field(FIELD_MODE), fx, fy, skill, action_field(FIELD_MELEE_KIND));
            if (!done) {
                experience = (int16_t)(experience >> 1);
                delay = (uint16_t)(delay >> 1);
            }
        }
        if (done && champ_action_target != THING_NONE)
            sound_request(CAT_CREATURE, item_gdat_index(champ_action_target), SOUND_HIT, SOUND_HIT_PRIORITY, SOUND_VOLUME,
                          fx, fy, 1);
        break;
    case COMMAND_LIGHT: case COMMAND_LIGHT_2: case COMMAND_LIGHT_3:
        magic_add_light(command, power);
        break;
    case COMMAND_EXPLOSION:
        explosion_create(EXPLOSION_COMMAND, (uint16_t)max16(2, power), game.party_x, game.party_y, CENTER_CELL);
        break;
    case COMMAND_HASTE:
        power = (int16_t)max16(POWER_MIN, power);
        game.magic_light[MAGIC_HASTE] = (uint8_t)min16(HASTE_MAX, (int16_t)(game.magic_light[MAGIC_HASTE] + power));
        break;
    case COMMAND_LEVITATE_DOWN: {
        int px = game.party_x + dir_dx[game.party_dir], py = game.party_y + dir_dy[game.party_dir];
        int open = 1;
        uint16_t g = creature_group_at(party.cur_map, px, py);
        if (g != THING_NONE && !(creature_group_info(g)->flags & CREATURE_NOT_SOLID)) open = 0;
        if (square_type(dun_get_square(px, py)) == SQUARE_PIT && open) {
            party.fell = 1;
            move_thing(THING_NONE, game.party_x, game.party_y, px, py);
            party.fell = 0;
        } else {
            delay = 0;
        }
        break;
    }
    case COMMAND_ACTION_EFFECT:
        game.magic_light[MAGIC_ACTION_EFFECT] = (uint8_t)min16(ACTION_EFFECT_MAX, game.magic_light[MAGIC_ACTION_EFFECT] + power);
        break;
    case COMMAND_AURA_FIRST: case COMMAND_AURA_FIRST + 1: case COMMAND_AURA_FIRST + 2: case COMMAND_AURA_LAST:
    case COMMAND_SHIELD_FIRST: case COMMAND_SHIELD_2: case COMMAND_SHIELD_LAST: {
        static const uint8_t auras[] = {5, 4, 6, 3};      /* the statistic auras of commands 0x0c to 0x0f */
        static const uint8_t shields[] = {1, 0, 2};      /* the shields of commands 0x21 to 0x23 */
        int shield = command >= COMMAND_SHIELD_FIRST;
        int aura = shield ? shields[command - COMMAND_SHIELD_FIRST] : auras[command - COMMAND_AURA_FIRST];
        power = (int16_t)(shield ? max16(POWER_MIN, power) * 3 : max16(POWER_MIN, power) << 2);
        if (!magic_cast_party_aura(c, aura, (uint16_t)power, shield)) {
            experience = (int16_t)(experience >> 2);
            delay = (uint16_t)(delay >> 1);
        }
        break;
    }
    case COMMAND_CONSUME:
        champ_consume_object(champ, item, hand);
        break;
    case COMMAND_LOAD: {
        int slot = champ_find_hand_source_slot(champ, hand);
        if (slot >= 0) {
            item = champ_remove_object_from_slot(champ, slot);
            champ_add_object_to_slot(champ, item, hand);
        }
        silent = 1;
        break;
    }
    case COMMAND_SHOOT: {
        uint16_t ammo = c->slots[other];
        if (!item_is_ammo_for_hand(champ, hand, ammo)) {
            if (!repeated) {
                show_failure(RESULT_NO_AMMO, VAR_NO_AMMO_COUNT);
                action.result_ammo = item_attr(item, ATTR_AMMO);
                action.result_flag = (int16_t)other;
                experience = 0;
            }
            done = 0;
            break;
        }
        ammo = champ_remove_object_from_slot(champ, other);
        int level = champ_get_skill_level(champ, SKILL_SHOOT, 1);
        uint16_t step = item_attr(ammo, ATTR_STEP);
        uint16_t attack = (uint16_t)(item_attr(item, ATTR_SHOOT_ATTACK) + level * 2);
        uint16_t kinetic = (uint16_t)(level + item_attr(item, ATTR_THROW_POWER_SHOOT) + item_attr(ammo, ATTR_THROW_POWER_SHOOT));
        champ_launch_projectile(champ, ammo, kinetic, attack, step);
        silent = 1;
        break;
    }
    case COMMAND_HEAL: {
        int16_t missing = (int16_t)(c->max_health - c->health);
        if (missing <= 0 || !c->mana) break;
        int16_t most = (int16_t)min16(HEAL_PER_STEP_MAX, champ_get_skill_level(champ, SKILL_HEAL, 1));
        experience = 2;
        do {
            int16_t v = (int16_t)min16(missing, most);
            c->health = (int16_t)(c->health + v);
            experience = (int16_t)(experience + 2);
            c->mana = (int16_t)(c->mana - HEAL_MANA);
            if (c->mana <= 0) break;
            missing = (int16_t)(missing - v);
        } while (missing);
        if (c->mana < 0) c->mana = 0;
        c->redraw |= REDRAW_BARS_FLAG;
        done = 1;
        break;
    }
    case COMMAND_THROW: {
        int side = c->cell == ((game.party_dir + 1) & 3) || c->cell == ((game.party_dir + 2) & 3);
        done = champ_throw_object(champ, hand, side);
        break;
    }
    case COMMAND_MINION_MAP_SET: case COMMAND_MINION_GUARD: case COMMAND_MINION_FETCH: case COMMAND_MINION_ATTACK:
    case COMMAND_MINION_DISMISS: case COMMAND_SUMMON_FIRST: case COMMAND_SUMMON_2: case COMMAND_SUMMON_LAST:
        done = champ_action_minion(champ, command, item, skill, power, &experience, &delay);
        break;
    case COMMAND_WARP:
        if (!move_party_warp_to_marker()) show_failure(RESULT_CANT, VAR_CANT_COUNT);
        break;
    default:
        break;
    }
    if (!silent)
        sound_play_with_fallback(current.category, current.index, sound, SOUND_FALLBACK, game.party_x, game.party_y, 0,
                                 SOUND_ACTION_PRIORITY, SOUND_VOLUME);
    if (!c->health || repeated) return done;
    if (delay) champ_add_action_delay(champ, delay, hand);
    if (stamina) champ_decrease_stamina(champ, stamina);
    if (experience) champ_add_experience(champ, skill, (uint32_t)(int32_t)experience);
    if (done) {
        ActionRepeat *r = &champ_action_repeat[champ][hand];
        r->left = (uint16_t)action_field(FIELD_REPEATS);
        if (r->left) {
            r->count = r->left;
            r->fresh = 1;
            r->thing = item;
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
        champion(champ)->redraw |= REDRAW_BARS_FLAG;
        done = champ_perform_action(champ, index);
        ActionRepeat *r = action.hand == 1 || action.hand == 0 ? &champ_action_repeat[champ][action.hand] : NULL;
        int use = !r || !r->left || (r->fresh && !rand_below((uint16_t)(r->count + 1)));
        if (use && champ_use_action_charge(champ) && r) {
            r->count = r->left = 0;
            r->fresh = 0;
        }
    }
    if (!action.flags) ui_action_area_close();
    else action.flags &= (uint16_t)~FLAGS_KEEP_OPEN;
    ui_update_action_area(0);
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
    if (action.champion && saved >= 0 && action.hand <= 1)
        champ_list_actions(action.champion - 1, champion(action.champion - 1)->slots[action.hand], action.hand);
}

/* The stamina that a throw of thing costs. */
static int item_get_throw_fatigue(uint16_t thing)
{
    int16_t left = (int16_t)((uint16_t)item_weight(thing) >> 1);
    uint16_t fatigue = (uint16_t)clamp16(1, left, 10);
    while ((left = (int16_t)(left - 10)) > 0) fatigue = (uint16_t)(fatigue + (left >> 1));
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
        experience = (throw_power >> 2) + 12;
        power = throw_power;
    }
    champ_add_experience(champ, SKILL_THROW, (uint16_t)experience);
    kinetic = (int16_t)(kinetic + power);
    int level = champ_get_skill_level(champ, SKILL_THROW, 1);
    kinetic = (int16_t)(kinetic + rand_below((uint16_t)((kinetic >> 2) + 8)) + level);
    kinetic = (int16_t)min16(kinetic, KINETIC_MAX);
    int attack = clamp16(ATTACK_MIN, (int16_t)((rand_next() & THROW_ATTACK_RANDOM) + level * 8), ATTACK_MAX);
    int step = item_attr(thing, ATTR_STEP);
    if (!step) step = max16(STEP_MIN, (int16_t)(STEP_BASE - level));
    projectile_create(thing, game.party_x, game.party_y, (game.party_dir + side) & 3, game.party_dir, (uint8_t)kinetic,
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
