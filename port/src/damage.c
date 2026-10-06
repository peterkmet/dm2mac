/* Champion statistics in fights, damage, wounds, poison, death, and the effects of time. */
#include <string.h>
#include "action.h"
#include "champion.h"
#include "dungeon.h"
#include "game.h"
#include "hooks.h"
#include "input.h"
#include "inventory.h"
#include "item.h"
#include "move.h"
#include "random.h"
#include "sensor.h"
#include "session.h"
#include "timeline.h"
#include "ui.h"
#include "weather.h"

/* Attack types of champ_take_damage (bit 15: a parry can block the attack). */
enum {
    ATTACK_FIRE = 1, ATTACK_SELF, ATTACK_BLUNT, ATTACK_SHARP, ATTACK_MAGIC, ATTACK_PSYCHIC, ATTACK_LIGHTNING, ATTACK_SELF_2
};
#define ATTACK_BLOCKABLE 0x8000
#define ATTACK_TYPE_MASK 0x7fff
#define WOUND_LOCATIONS 6           /* the hands, the head, the torso, the legs, the feet */
#define WOUND_SHARP 0x8000          /* wound location bit: a sharp attack */
#define COMMAND_PARRY 1             /* the last command of a hand that parries */
#define PERCENT_MAX 100
#define DEFENSE_BASE 0x82
#define PSYCHIC_BASE 0x73
#define ADJUST_BASE 0xaa
#define ADJUST_MIN 0x10
#define WOUND_ROLL_MASK 0x7f
#define WOUND_ROLL_BASE 10
enum { AURA_FIRE_SHIELD, AURA_SPELL_SHIELD, AURA_DEFENSE };
#define ATTR_DEFENDS 0x8000         /* ATTR_ARMOUR_CLASS bit of a hand item: it defends */
#define ARMOUR_SHARP_SHIFT 8
#define ARMOUR_SHARP_MASK 7
#define ARMOUR_DEFENSE_MASK 0xff
#define SHARP_FACTOR_BASE 4u        /* against a sharp attack: defense * (sharp factor + 4) / 8 */
#define SHARP_FACTOR_SHIFT 3
#define ATTR_AMMO 5
#define ATTR_LAUNCHER 0x8000        /* ATTR_AMMO bit: the weapon shoots */
#define POISON_EVENT_DELAY 0x24
#define POISON_MAX 0xc00
#define POISON_DAMAGE_ROUND 0x1e
#define POISON_DAMAGE_SHIFT 6
#define DAMAGE_SHOW_TIME 5
#define CHAMP_DAMAGE_SHOWN 0xffff
#define REDRAW_POISON 0x2800
#define REDRAW_STATS_SKILLS 0x3000
enum { SKILL_FIGHTER, SKILL_NINJA, SKILL_PRIEST, SKILL_WIZARD, SKILL_SWING = 4, SKILL_PARRY = 7, SKILL_STEAL = 8,
       SKILL_FIGHT = 9, SKILL_THROW = 10, SKILL_SHOOT = 11 };
#define STAT_MIN 10
#define STAT_MAX 220
#define LUCK_STEP 2
#define BONES_KEEP 0x3f80           /* bones word 2: keeps the stack count and the do-not-discard bit */
#define ITEM_DATA_SHIFT 14          /* bones word 2: the champion in the charge bits */
#define CURE_ALL 10000
#define WOUND_HAND_BIT(hand) ((hand) ? 2 : 1)
/* Time effects. */
#define REGEN_STEP 0x38
#define REGEN_RANGE 0x80
#define MANA_PER_STEP 40
#define MANA_STAMINA_BASE 16
#define MANA_STAMINA_MIN 7
#define HUNGER_BASE 4
#define HUNGER_STEP_MIN 1
#define HUNGER_STEP_MAX 6
#define NEW_PARTY_TIME 0x50         /* game steps after the first recruit: the party gets hungry faster */
#define NEW_PARTY_TIME_2 0xfa
#define HUNGRY (-0x200)
#define STARVING (-0x400)
#define HEALTH_REGEN_BASE 12
#define HEALTH_REGEN_SHIFT 7
#define STATS_TIME_MASK 0xff
#define STATS_TIME_MASK_ASLEEP 0x3f
#define REDRAW_BARS_FLAG 0x0800
/* Lights. */
#define ATTR_LIGHT_FLAG 0x10        /* ATTR_FLAGS bit: the item gives light */

int16_t champ_pending_damage[CHAMPION_MAX];
uint16_t champ_pending_wounds[CHAMPION_MAX];
uint16_t champ_regen_counter;

/* The defense factor of each wound location. */
static const uint8_t wound_defense_factor[WOUND_LOCATIONS] = {5, 5, 4, 6, 3, 1};
/* The order in which a dead champion drops its slots. */
static const uint8_t drop_order[SLOT_COUNT] = {
    5, 4, 9, 8, 7, 12, 6, 11, 3, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 10, 2, 0, 1
};

static int clamp16(int low, int v, int high)
{
    v = (int16_t)v;
    return v < (int16_t)low ? (int16_t)low : v > (int16_t)high ? (int16_t)high : v;
}

static int min16(int a, int b) { return (int16_t)a < (int16_t)b ? (int16_t)a : (int16_t)b; }

/* An event of the party map at delay game steps from now. */
static TimelineEvent party_event(uint32_t delay, int type, int priority)
{
    TimelineEvent e = {.time_map = (game.game_time + delay) | (uint32_t)(uint8_t)game.party_map << 24,
                       .type = (uint8_t)type, .priority = (uint8_t)priority};
    return e;
}

void champ_reset_step_state(void)
{
    memset(champ_pending_damage, 0, sizeof champ_pending_damage);
    memset(champ_pending_wounds, 0, sizeof champ_pending_wounds);
    champ_regen_counter = 0;
    leader_hand = (LeaderHand){.icon = HAND_ICON_NONE};
    ui.candidate = 0;
}

int champ_get_stat_adjusted_attack(Champion *c, int stat, int attack)
{
    int16_t left = (int16_t)(ADJUST_BASE - champ_get_stat(c, stat, 0));
    if (left < ADJUST_MIN) return (uint16_t)attack >> 3;
    return (int)((uint32_t)(uint16_t)attack * (uint16_t)left >> 7);
}

/* The defense of an armour attribute; against a sharp attack its sharp factor counts. */
static int item_armor_defense(uint16_t attr, int sharp)
{
    if (!sharp) return attr & ARMOUR_DEFENSE_MASK;
    return (int)((attr & ARMOUR_DEFENSE_MASK) * (((attr >> ARMOUR_SHARP_SHIFT) & ARMOUR_SHARP_MASK) + SHARP_FACTOR_BASE)
                 >> SHARP_FACTOR_SHIFT);
}

int champ_get_strength(int champ, int hand, int skill)
{
    Champion *c = champion(champ);
    int16_t v = (int16_t)((rand_next() & 0xf) + champ_get_stat(c, STAT_STRENGTH, 0));
    uint16_t item = c->slots[hand];
    uint16_t weight = (uint16_t)item_weight(item);
    uint16_t light = (uint16_t)((uint16_t)champ_get_max_load(c) >> 4);
    v = (int16_t)(v + weight - 12);
    if (weight > light) {
        v = (int16_t)(v - ((weight - light) >> 1));
        uint16_t heavy = (uint16_t)(((light - 12) >> 1) + light);
        if (weight > heavy) v = (int16_t)(v - (weight - heavy) * 2);
    }
    if ((int16_t)skill >= 0) {
        v = (int16_t)(v + champ_get_skill_level(champ, skill, 1) * 2);
        int bonus = 0;
        if (skill == SKILL_FIGHTER || (skill >= SKILL_SWING && skill <= SKILL_PARRY) || skill == SKILL_FIGHT) {
            bonus = item_attr(item, ATTR_STRENGTH);
        } else if (skill == SKILL_NINJA || skill == SKILL_THROW || skill == SKILL_SHOOT) {
            bonus = item_attr(item, ATTR_THROW_POWER);
            /* a launcher adds its power only to shooting, other items only to throwing */
            if (bonus && !(item_attr(item, ATTR_AMMO) & ATTR_LAUNCHER) != (skill != SKILL_SHOOT)) bonus = 0;
        }
        v = (int16_t)(v + bonus);
    }
    v = (int16_t)champ_scale_by_stamina(c, v);
    if (c->wounds & WOUND_HAND_BIT(hand)) v = (int16_t)(v >> 1);
    return clamp16(0, v >> 1, PERCENT_MAX);
}

int champ_get_dexterity(int champ)
{
    Champion *c = champion(champ);
    int16_t v = (int16_t)((rand_next() & 7) + champ_get_stat(c, STAT_DEXTERITY, 0));
    int32_t load = (int32_t)((int16_t)v >> 1) * (uint16_t)champ_get_load(champ);
    v = (int16_t)(v - load / (uint16_t)champ_get_max_load(c));
    if (v < 2) v = 2;
    if (party_sleeping) v = (int16_t)(v >> 1);
    int high = (int16_t)(PERCENT_MAX - (rand_next() & 7));
    int low = (int16_t)((rand_next() & 7) + 1);
    return clamp16(low, v >> 1, high);
}

int champ_test_luck(Champion *c, int chance)
{
    if (rand_bit() && rand_below(PERCENT_MAX) > (uint16_t)chance) return 1;
    uint16_t roll = rand_below((uint16_t)(champ_get_stat(c, STAT_LUCK, 0) * 2));
    int max = min16(STAT_MAX, champ_get_stat(c, STAT_LUCK, 1));
    int lucky = roll > (uint16_t)chance;
    c->stats[STAT_LUCK][0] = (uint8_t)clamp16(STAT_MIN, (lucky ? -LUCK_STEP : LUCK_STEP) + c->stats[STAT_LUCK][0], max);
    return lucky;
}

/* The defense of a wound location (bit 15: against a sharp attack) of champ, 0 to 100. */
static int champ_get_wound_defense(int champ, uint16_t location)
{
    Champion *c = champion(champ);
    int sharp = location & WOUND_SHARP;
    location &= (uint16_t)~WOUND_SHARP;
    int16_t hands = 0;
    for (int hand = 0; hand < SLOT_HAND_COUNT; hand++) {
        uint16_t attr = item_attr(c->slots[hand], ATTR_ARMOUR_CLASS);
        if (!(attr & ATTR_DEFENDS)) continue;
        int strength = (uint16_t)champ_get_strength(champ, hand, SKILL_PARRY);
        int v = (item_armor_defense(attr, sharp) + strength) * wound_defense_factor[location];
        hands = (int16_t)(hands + (v >> ((hand != location) + 4)));
    }
    int16_t v = (int16_t)rand_below((uint16_t)(((uint16_t)champ_get_stat(c, STAT_VITALITY, 0) >> 3) + 1));
    if (sharp) v = (int16_t)(v >> 1);
    if (c->aura == AURA_DEFENSE) v = (int16_t)(v + c->aura_value);
    v = (int16_t)(v + c->defense[0] + c->defense[1] + hands);
    if (location >= SLOT_HAND_COUNT)
        v = (int16_t)(v + item_armor_defense(item_attr(c->slots[location], ATTR_ARMOUR_CLASS), sharp));
    if (c->wounds & 1 << location) v = (int16_t)(v - (rand_2bits() + 8));
    if (party_sleeping) v = (int16_t)(v >> 1);
    return clamp16(0, v >> 1, PERCENT_MAX);
}

int champ_take_damage(int champ, int damage, int wound_mask, int attack_type)
{
    champ = (int16_t)champ;
    int16_t d = (int16_t)damage;
    if (champ == -1 || champ + 1 == ui.candidate || session.completed || d < 1 || !champion(champ)->health) return 0;
    Champion *c = champion(champ);
    int type = attack_type & ATTACK_TYPE_MASK;
    if (type) {
        int count = 0;
        uint16_t defense = 0;
        for (int w = 0; w < WOUND_LOCATIONS; w++) {
            if (!((uint16_t)wound_mask & 1 << w)) continue;
            count++;
            defense = (uint16_t)(defense + champ_get_wound_defense(champ, (uint16_t)((type == ATTACK_SHARP ? WOUND_SHARP : 0) | w)));
        }
        if (count) defense = (uint16_t)(defense / count);
        int16_t parry = 0;
        for (int hand = 0; hand < SLOT_HAND_COUNT; hand++)
            if (c->last_command[hand] == COMMAND_PARRY) parry = (int16_t)(parry + c->defense[hand]);
        if (parry) {
            int chance = (uint16_t)champ_get_skill_level(champ, SKILL_PARRY, 1) + ((uint16_t)parry >> 3);
            if (chance > (int)(rand_next() & 0xf)) {
                if (attack_type & ATTACK_BLOCKABLE) {
                    d = (int16_t)(d - parry);
                    if (d <= 0) return 0;
                }
                defense = (uint16_t)(defense + ((uint16_t)parry >> 2));
            }
        }
        int scaled = 1;
        switch (type) {
        case ATTACK_FIRE:
            d = (int16_t)champ_get_stat_adjusted_attack(c, STAT_ANTI_FIRE, (uint16_t)d);
            if (c->aura == AURA_FIRE_SHIELD) d = (int16_t)(d - c->aura_value);
            break;
        case ATTACK_SELF: case ATTACK_SELF_2:
            defense = (uint16_t)((defense >> 1) + champ_get_skill_level(champ, SKILL_NINJA, 1));
            break;
        case ATTACK_MAGIC:
            d = (int16_t)champ_get_stat_adjusted_attack(c, STAT_ANTI_MAGIC, (uint16_t)d);
            if (c->aura == AURA_SPELL_SHIELD) d = (int16_t)(d - c->aura_value);
            scaled = 0;
            break;
        case ATTACK_PSYCHIC: {
            int16_t left = (int16_t)(PSYCHIC_BASE - champ_get_stat(c, STAT_WISDOM, 0));
            if (left <= 0) return 0;
            d = (int16_t)((uint32_t)(uint16_t)d * (uint16_t)left >> 6);
            scaled = 0;
            break;
        }
        }
        if (scaled) {
            if (d <= 0) return 0;
            d = (int16_t)((uint32_t)(uint16_t)d * (uint16_t)(DEFENSE_BASE - defense) >> 6);
        }
        if (d <= 0) return 0;
        int16_t resist = (int16_t)champ_get_stat_adjusted_attack(c, STAT_VITALITY,
                                                                 (int)((rand_next() & WOUND_ROLL_MASK) + WOUND_ROLL_BASE));
        while (d > resist) {
            champ_pending_wounds[champ] |= (uint16_t)((1 << (rand_next() & 7)) & wound_mask);
            resist = (int16_t)(resist * 2);
            if (d <= resist || !resist) break;
        }
        if (party_sleeping) champ_wake_party();
    }
    champ_pending_damage[champ] = (int16_t)(champ_pending_damage[champ] + d);
    return d;
}

int champ_damage_party(int damage, int wound_mask, int attack_type)
{
    int16_t d = (int16_t)damage;
    if (!d) return 0;
    int hit = 0, spread = ((uint16_t)d >> 3) + 1;
    d = (int16_t)(d - spread);
    for (int i = 0; i < game.champion_count; i++) {
        int16_t v = (int16_t)(rand_below((uint16_t)(spread * 2)) + d);
        if (champ_take_damage(i, v > 1 ? v : 1, wound_mask, attack_type)) hit |= 1 << i;
    }
    return hit;
}

void champ_poison(int champ, int amount)
{
    champ = (int16_t)champ;
    if (champ == -1 || champ + 1 == ui.candidate) return;
    Champion *c = champion(champ);
    int16_t d = (int16_t)(((uint16_t)amount + POISON_DAMAGE_ROUND) >> POISON_DAMAGE_SHIFT);
    champ_take_damage(champ, d > 1 ? d : 1, 0, 0);
    c->redraw |= REDRAW_POISON;
    uint16_t left = (uint16_t)(amount - 1);
    if (!left) return;
    if (c->poison + left > POISON_MAX) {
        left = (uint16_t)(POISON_MAX - c->poison);
        if (!left) return;
    }
    c->poison = (int16_t)(c->poison + left);
    c->poisoned++;
    /* fix: the original leaves the last 4 data bytes uninitialized */
    TimelineEvent e = party_event(POISON_EVENT_DELAY, EVENT_POISON, champ);
    e.data[0] = (uint8_t)left;
    e.data[1] = (uint8_t)(left >> 8);
    timeline_add_event(&e);
}

void champ_cure_poison(int champ, int amount)
{
    champ = (int16_t)champ;
    if (champ == -1) return;
    Champion *c = champion(champ);
    int16_t left = (int16_t)amount;
    /* the original reads the first event slots as many as the timeline holds */
    for (int i = 0; i < timeline.count; i++) {
        TimelineEvent *e = &timeline.events[i];
        if (e->type != EVENT_POISON || e->priority != (uint8_t)champ) continue;
        int16_t v = (int16_t)(e->data[0] | e->data[1] << 8);
        if (left < v) {
            v = (int16_t)(v - left);
            e->data[0] = (uint8_t)v;
            e->data[1] = (uint8_t)(v >> 8);
            c->poison = (int16_t)(c->poison - left);
            return;
        }
        left = (int16_t)(left - v);
        c->poison = (int16_t)(c->poison - v);
        timeline_delete_event((uint16_t)i);
        if (!--c->poisoned || left <= 0) return;
    }
}

/* Drops all items of the champion on the party square, in its cell. */
static void champ_drop_all_objects(int champ)
{
    int cell = champion(champ)->cell;
    for (int i = 0; i < SLOT_COUNT; i++) {
        uint16_t t = champ_remove_object_from_slot(champ, drop_order[i]);
        if (t != THING_NONE)
            move_thing((uint16_t)((t & (THING_INDEX_MASK | THING_TYPE_MASK << THING_TYPE_SHIFT)) | cell << THING_CELL_SHIFT),
                       -1, 0, game.party_x, game.party_y);
    }
}

void champ_drag_click(int cell)
{
    int finish = !(cell & DRAG_CANCEL);
    if (!finish) cell &= 3;
    int16_t target = (int16_t)champ_get_index_in_cell((cell + game.party_dir) & 3);
    if (!input_drag_state) {
        if (target >= 0) input_drag_state = cell + 1;
        return;
    }
    int from = input_drag_state - 1;
    input_drag_state = 0;
    int16_t dragged = (int16_t)champ_get_index_in_cell((game.party_dir + from) & 3);
    if (finish) champion(dragged)->dir = (uint8_t)game.party_dir;
    if (from == cell) return;
    if (target >= 0) champion(target)->cell = (uint8_t)((from + game.party_dir) & 3);
    champion(dragged)->cell = (uint8_t)((cell + game.party_dir) & 3);
}

void champ_drag_cancel(void)
{
    if (input_drag_state) champ_drag_click((input_drag_state - 1) | DRAG_CANCEL);
}

void champ_kill(int champ)
{
    Champion *c = champion(champ);
    if (champ == action.champion - 1) ui_action_area_close();
    c->health = 0;
    c->action_delay[0] = c->action_delay[1] = c->action_delay[ACTION_SPELL] = 0;
    c->redraw |= REDRAW_STATUS_BOX;
    if (champ + 1 == ui.inventory) {
        if (ui.eye) {
            ui.eye = 0;
            if (game.leader_hand != THING_NONE) ui_show_object_name(game.leader_hand);
        }
        ui.mouth = 0;
        inv_toggle(CHAMPION_MAX);
    }
    champ_drop_all_objects(champ);
    int cell = c->cell;
    uint16_t bones = dungeon_unused_thing(THING_MISC | THING_ANY_RECORD);
    if (bones != THING_NONE) {
        uint16_t w = (uint16_t)((thing_word(bones, 2) | DO_NOT_DISCARD) & BONES_KEEP);
        set_thing_word(bones, 2, (uint16_t)(w | (champ & 3) << ITEM_DATA_SHIFT));
        move_thing((uint16_t)(bones | cell << THING_CELL_SHIFT), -1, 0, game.party_x, game.party_y);
    }
    c->rune_count = 0;
    c->runes[0] = 0;
    c->dir = (uint8_t)game.party_dir;
    c->unknown_26[3] = 0;
    if (((cell + DIR_COUNT - game.party_dir) & 3) + 1 == input_drag_state) champ_drag_cancel();
    if (c->poisoned) champ_cure_poison(champ, CURE_ALL);
    int alive = 0;
    while (alive < game.champion_count && !champion(alive)->health) alive++;
    if (alive == game.champion_count) {
        main_loop.end = 1;
        input_flush();
        party_sleeping = 0;
        main_loop.view_hidden = 0;
    } else if (champ == (int16_t)game.leader) {
        champ_set_leader(alive);
    }
    input_refresh_mode();
}

void champ_apply_pending_damage(void)
{
    for (int i = 0; i < game.champion_count; i++) {
        Champion *c = champion(i);
        c->wounds |= champ_pending_wounds[i];
        champ_pending_wounds[i] = 0;
        int16_t d = champ_pending_damage[i];
        if (!d) continue;
        champ_pending_damage[i] = 0;
        if (!c->health) continue;
        int16_t health = (int16_t)(c->health - d);
        if (health <= 0) {
            champ_kill(i);
            continue;
        }
        c->health = health;
        c->damage = (uint16_t)d;
        c->redraw |= REDRAW_BARS_FLAG;
        if (c->damage_event == CHAMP_DAMAGE_SHOWN) {
            /* fix: the original leaves the data bytes uninitialized */
            TimelineEvent e = party_event(DAMAGE_SHOW_TIME, EVENT_HIDE_DAMAGE, i);
            c->damage_event = (uint16_t)timeline_add_event(&e);
        } else {
            timeline.events[c->damage_event].time_map = (game.game_time + DAMAGE_SHOW_TIME)
                                                        | (uint32_t)(uint8_t)game.party_map << 24;
            timeline_reposition_event(c->damage_event);
        }
    }
}

void champ_apply_time_effects(void)
{
    if (!game.champion_count) return;
    champ_regen_counter = (uint16_t)(champ_regen_counter + REGEN_STEP);
    if (champ_regen_counter > REGEN_RANGE) champ_regen_counter = (uint16_t)(champ_regen_counter - REGEN_RANGE);
    for (int i = 0; i < game.champion_count; i++) {
        Champion *c = champion(i);
        if (!c->health || i + 1 == ui.candidate) continue;
        if (c->mana < c->max_mana) {
            uint16_t magic = (uint16_t)(champ_get_skill_level(i, SKILL_WIZARD, 1) + champ_get_skill_level(i, SKILL_PRIEST, 1));
            if (champ_regen_counter < (uint16_t)champ_get_stat(c, STAT_WISDOM, 0) + magic) {
                int gain = c->max_mana / MANA_PER_STEP + 1;
                if (party_sleeping) gain *= 2;
                int cost = (int16_t)(MANA_STAMINA_BASE - magic);
                if (cost < MANA_STAMINA_MIN) cost = MANA_STAMINA_MIN;
                champ_decrease_stamina(i, (int16_t)(cost * gain));
                c->mana = (int16_t)(c->mana + min16(gain, c->max_mana - c->mana));
            }
        } else if (c->mana > c->max_mana) {
            c->mana--;
        }
        uint16_t hunger = HUNGER_BASE;
        for (int16_t half = (int16_t)(c->max_stamina >> 1); half > c->stamina; half = (int16_t)(half >> 1)) hunger += 2;
        int step = clamp16(HUNGER_STEP_MIN, (int8_t)(c->max_stamina >> 8) - 1, HUNGER_STEP_MAX);
        uint16_t since = (uint16_t)(game.game_time - game.first_recruit_time);
        if (since > NEW_PARTY_TIME) step++;
        if (since > NEW_PARTY_TIME_2) step++;
        if (party_sleeping) step *= 2;
        int16_t stamina = 0;
        do {
            int low = hunger <= HUNGER_BASE;
            if (c->food < HUNGRY) {
                if (low) {
                    stamina = (int16_t)(stamina + step);
                    c->food = (int16_t)(c->food - 2);
                }
            } else {
                if (c->food >= 0) stamina = (int16_t)(stamina - step);
                c->food = (int16_t)(c->food - (low ? 2 : hunger >> 1));
            }
            if (c->water < HUNGRY) {
                if (low) {
                    stamina = (int16_t)(stamina + step);
                    c->water--;
                }
            } else {
                if (c->water >= 0) stamina = (int16_t)(stamina - step);
                c->water = (int16_t)(c->water - (low ? 1 : hunger >> 2));
            }
        } while (--hunger && c->stamina - stamina < c->max_stamina);
        champ_decrease_stamina(i, stamina);
        if (c->food < STARVING) c->food = STARVING;
        if (c->water < STARVING) c->water = STARVING;
        if (c->health < c->max_health && c->stamina >= c->max_stamina >> 2
            && champ_regen_counter < (uint16_t)champ_get_stat(c, STAT_VITALITY, 0) + HEALTH_REGEN_BASE) {
            int gain = (c->max_health >> HEALTH_REGEN_SHIFT) + 1;
            if (party_sleeping) gain *= 2;
            c->health = (int16_t)(c->health + min16(gain, c->max_health - c->health));
        }
        if (!((uint16_t)game.game_time & (party_sleeping ? STATS_TIME_MASK_ASLEEP : STATS_TIME_MASK))) {
            for (int s = 0; s < STAT_COUNT; s++) {
                uint8_t *v = c->stats[s];
                if (v[0] < v[1]) v[0]++;
                else if (v[0] > v[1]) v[0] = (uint8_t)(v[0] - v[0] / v[1]);
            }
        }
        c->redraw |= REDRAW_BARS_FLAG;
        if (i + 1 == ui.inventory) c->redraw |= REDRAW_STATS_SKILLS;
    }
}

void items_burn_held_lights(void)
{
    int burned = 0, count = game.champion_count - (ui.candidate != 0);
    for (int i = 0; i < count; i++) {
        Champion *c = champion(i);
        for (int hand = SLOT_HAND_COUNT - 1; hand >= 0; hand--) {
            uint16_t t = c->slots[hand];
            if (!(item_attr(t, ATTR_FLAGS) & ATTR_LIGHT_FLAG) || !item_charges(t)) continue;
            if (!item_add_charges(t, -1)) item_set_do_not_discard(t, 0);
            burned = 1;
        }
    }
    if (burned) light_update_party_light();
}

/* Puts champ into its cell, or into the first free cell from the party direction. */
static void champ_place_in_free_cell(int champ)
{
    Champion *c = champion(champ);
    int16_t health = c->health;
    c->health = 0;
    if (c->cell == NO_CHAMPION_CELL || champ_get_index_in_cell(c->cell) != -1) {
        int k = 0;
        while (champ_get_index_in_cell((game.party_dir + k) & 3) != -1) k++;
        c->cell = (uint8_t)((game.party_dir + k) & 3);
    }
    c->dir = (uint8_t)game.party_dir;
    c->health = health;
}

void champ_revive(int champ)
{
    Champion *c = champion(champ);
    champ_place_in_free_cell(champ);
    c->load = 0;
    for (int s = 0; s < SLOT_COUNT; s++) c->slots[s] = THING_NONE;
    int16_t max = (int16_t)((uint16_t)c->max_health - ((uint16_t)c->max_health >> 6) - 1);
    c->max_health = max > 25 ? max : 25;
    c->health = (int16_t)(c->max_health >> 1);
    c->redraw |= REDRAW_STATUS_BOX;
    c->aura = 0;
    c->aura_value = 0;
    input_refresh_mode();
}

/* The steps of an altar rebirth: smoke, then the bones of the champion vanish, then the champion lives again. */
enum { REBIRTH_REVIVE, REBIRTH_BONES, REBIRTH_SMOKE };
#define EXPLOSION_SMOKE 0xffe4
#define SMOKE_TIME 5
#define CATEGORY_MISC 0x15
#define ITEM_BONES 0

void timeline_event_altar_rebirth(TimelineEvent *e)
{
    int x = e->data[0], y = e->data[1], cell = e->data[2], champ = e->priority;
    switch (e->data[3]) {
    case REBIRTH_REVIVE:
        champ_revive(champ);
        return;
    case REBIRTH_SMOKE:
        explosion_create(EXPLOSION_SMOKE, 0, x, y, cell);
        e->time_map += SMOKE_TIME;
        break;
    case REBIRTH_BONES: {
        uint16_t t = dungeon_square_first_thing(party.cur_map, x, y);
        for (; t != THING_ENDOFLIST; t = dungeon_next_thing(t))
            if (t >> THING_CELL_SHIFT == cell && item_gdat_category(t) == CATEGORY_MISC && item_gdat_index(t) == ITEM_BONES
                && (int16_t)item_add_charges(t, 0) == champ)
                break;
        if (t == THING_ENDOFLIST) return;
        dungeon_unlink_thing_from_square(t, party.cur_map, x, y);
        set_thing_word(t, THING_NEXT, THING_NONE);
        e->time_map++;
        break;
    }
    default: return;
    }
    e->data[3]--;
    timeline_add_event(e);
}
