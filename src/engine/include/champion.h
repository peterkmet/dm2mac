/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_CHAMPION_H
#define DM2_ENGINE_CHAMPION_H
/* Prefixes: champ_ champion */

/* The champions: their slots, statistics, wounds, and the effects of their actions. */

#include "save.h"
#include <stdbool.h>
#include <stdint.h>

/* Champion slots: the hands, the body and pack slots (to 29), then the slots of an open container. */
#define SLOT_READY_HAND      0
#define SLOT_ACTION_HAND     1
#define SLOT_HAND_COUNT      2
#define SLOT_CONTAINER_TEST  12
#define SLOT_COUNT           30
#define SLOT_CONTAINER_FIRST SLOT_COUNT
#define CONTAINER_SLOTS      8
/* SLOT_CONTAINER_TEST: when the open hand item fits this slot, container slots take only pouch items. */

/* Statistics of a champion, and the limits of a statistic value. */
#define STAT_LUCK        0
#define STAT_STRENGTH    1
#define STAT_DEXTERITY   2
#define STAT_WISDOM      3
#define STAT_VITALITY    4
#define STAT_ANTI_MAGIC  5
#define STAT_ANTI_FIRE   6
#define STAT_COUNT       7
#define STAT_MIN         10
#define STAT_MAX         220
/* The two values of a statistic in Champion.stats: current and maximum. */
#define STAT_CURRENT     0
#define STAT_MAXIMUM     1
#define STAT_VALUE_COUNT 2
/* Skills: the four base skills, then their sub-skills. */
#define SKILL_FIGHTER    0
#define SKILL_NINJA      1
#define SKILL_PRIEST     2
#define SKILL_WIZARD     3
#define SKILL_SWING      4
#define SKILL_PARRY      7
#define SKILL_STEAL      8
#define SKILL_FIGHT      9
#define SKILL_THROW      10
#define SKILL_SHOOT      11
#define SKILL_COUNT      20
#define SKILL_BASE_COUNT 4 /* skills 4 to 19 are sub-skills: 4 per base skill */
#define SUB_SKILL_COUNT  4 /* the sub-skills of each base skill */
/* The base skill of a skill: the skill itself, or the base skill of a sub-skill. */
static inline int champ_base_skill(int skill)
{
    return skill < SKILL_BASE_COUNT ? skill : (skill - SKILL_BASE_COUNT) / SUB_SKILL_COUNT;
}
/* Sizes of the fields of the champion record, and the spell entry of its action delays. */
#define CHAMPION_NAME_SIZE    8
#define CHAMPION_TITLE_SIZE   20
#define CHAMPION_RUNES_SIZE   4
#define CHAMPION_UNKNOWN_SIZE 2
#define ACTION_DELAY_COUNT    3 /* the two hands and the spell */
#define ACTION_SPELL          2

/* Champion redraw flags. */
#define REDRAW_LOAD          0x1000
#define REDRAW_TITLE_ALL     0x2000
#define REDRAW_STATUS_BOX    0x4000
#define REDRAW_BARS          0x0800
#define REDRAW_NAME          0x0400
#define REDRAW_ALL_INVENTORY 0x8000
#define REDRAW_ALL           0xfc00
#define REDRAW_KEEP          0x03ff /* the bits that stay after a redraw */
#define REDRAW_STEP_TIME     0x0010 /* the step time reads this bit; nothing in the port sets it (docs/todo.md) */

/* Aura types: 0 to 2 shields, 3 to 6 raise a statistic. */
#define AURA_FIRE_SHIELD  0
#define AURA_SPELL_SHIELD 1
#define AURA_DEFENSE      2
#define AURA_STAT_FIRST   3
#define AURA_STAT_LAST    6
#define AURA_STAT_OFFSET  2 /* aura type - 2 is the statistic */

/* The champion record of the original (263 bytes). */
typedef struct __attribute__((packed)) {
    char name[CHAMPION_NAME_SIZE];
    char title[CHAMPION_TITLE_SIZE];
    uint8_t dir, cell;
    uint8_t rune_count;
    uint8_t poisoned;
    uint8_t last_command[SLOT_HAND_COUNT];
    char runes[CHAMPION_RUNES_SIZE];
    uint8_t unknown_26[CHAMPION_UNKNOWN_SIZE];
    uint8_t hit_dir;    /* the direction of the hardest hit of the turn */
    uint8_t hit_damage; /* its damage */
    uint8_t action_delay[ACTION_DELAY_COUNT];
    uint8_t unknown_2d;
    uint16_t damage_event; /* timeline event of the damage display, THING_NONE none */
    uint16_t damage;
    uint16_t redraw;
    uint16_t wounds;
    int16_t health, max_health, stamina, max_stamina, mana, max_mana;
    int8_t defense[SLOT_HAND_COUNT];
    int16_t food, water;
    int16_t poison;
    uint8_t stats[STAT_COUNT][STAT_VALUE_COUNT]; /* current, maximum */
    int8_t stat_bonus[STAT_COUNT];
    uint32_t experience[SKILL_COUNT];
    int8_t skill_bonus[SKILL_COUNT];
    uint16_t slots[SLOT_COUNT];
    uint16_t load;
    uint8_t portrait;
    uint8_t aura;
    int16_t aura_value;
    uint16_t move_speed;
} Champion;

_Static_assert(sizeof(Champion) == CHAMPION_SIZE, "the original champion record has 263 bytes");

/* Champion.wounds bits: 0x01 and 0x02 the hands, then the head, the torso, the legs, the feet. */
#define WOUND_HEAD            0x04
#define WOUND_TORSO           0x08
#define WOUND_LEGS            0x10
#define WOUND_FEET            0x20
#define WOUND_ALL             0x3f
#define BUMP_WOUNDS           (WOUND_TORSO | WOUND_LEGS) /* wound locations of a bump: the torso and the legs */
#define FALL_WOUNDS           (WOUND_LEGS | WOUND_FEET)  /* wound locations of a fall: the legs and the feet */
/* The maximum food (Champion.food and the food gauge) and poison (Champion.poison) of a champion. */
#define FOOD_MAX              2048
#define POISON_MAX            3072
/* Attack types of champ_take_damage. */
#define ATTACK_TYPE_FIRE      1
#define ATTACK_TYPE_SELF      2
#define ATTACK_TYPE_BLUNT     3
#define ATTACK_TYPE_SHARP     4
#define ATTACK_TYPE_MAGIC     5
#define ATTACK_TYPE_PSYCHIC   6
#define ATTACK_TYPE_LIGHTNING 7
#define ATTACK_TYPE_SELF_2    8
/* The attack type bit of an attack that a parry can block. */
#define ATTACK_BLOCKABLE      0x8000
/* g_game.throw_cooldown after a throw or a launch. */
#define THROW_COOLDOWN        4
/* Champion sounds (GDAT category CAT_CHAMPION, index portrait): the scream of a fall or of the end of the game, and
   a push, a bump, or a closing door that hurts. A push that fails plays SOUND_HURT. */
#define SOUND_ENTRY_SCREAM    0x87
#define SOUND_PRIORITY_FALL   0xbe
#define SOUND_ENTRY_PUSHED    0x8a
#define SOUND_PRIORITY_PUSHED 0x64
/* The cell byte of no champion. */
#define NO_CHAMPION_CELL      0xff
/* last_command of a hand without a command. */
#define COMMAND_NONE          0xff
/* Champion slots of the quiver and the pouch. */
#define SLOT_QUIVER_FIRST     6 /* the slots that the hands load from: 6, 7 to 9, 11, 12 */
#define SLOT_QUIVER_LAST      9
#define SLOT_QUIVER_READY     11
#define SLOT_QUIVER_ACTION    12
#define SLOT_POUCH_FIRST      13
#define SLOT_POUCH_LAST       (SLOT_COUNT - 1)
/* The worn slots that a picked-up item tries before the hands: 2 to 5, then 10. */
#define SLOT_BODY_FIRST       2
#define SLOT_BODY_LAST        5
#define SLOT_BODY_EXTRA       10
/* The cell bit of champ_drag_click that cancels the drag. */
#define DRAG_CANCEL           0x8000

/* Bytes of g_game.magic_light: the party spells, and the maximum of the haste byte. */
#define MAGIC_INVISIBILITY  2
#define MAGIC_ACTION_EFFECT 3
#define MAGIC_HASTE         4
#define HASTE_MAX           255

/* One action of an item in a champion's hand: its GDAT text (category, index, 5, entry). */
typedef struct {
    uint8_t category, index, entry;
} ChampionAction;
/* The actions that the action menu of a hand shows at most. */
#define CHAMPION_ACTION_MAX 3

extern ChampionAction g_champ_actions[CHAMPION_ACTION_MAX];
extern int g_champ_action_count;
extern uint16_t g_champ_action_thing;
/* The party sleeps; Step 14 sets it. */
extern int g_party_sleeping;
/* The base skills whose level went up, per champion; the inventory shows them green,
   and closing it takes back the counts that it showed. Step 15 sets them. */
extern uint8_t g_champ_skill_raised[CHAMPION_MAX][SKILL_BASE_COUNT];

static inline Champion *champion(int index)
{
    return (Champion *)g_game.champions[index];
}
/* Adds delta to the move speed bonus, the low byte of move_speed; the high byte stays. */
static inline void champ_add_move_speed(Champion *c, int delta)
{
    c->move_speed = (uint16_t)((c->move_speed & ~BYTE_MASK) | (uint8_t)(c->move_speed + delta));
}

/* Setup */
/* Prepares the party of a loaded game for the screen: redraw flags, drawn hand state, the leader. */
void champ_init_party_state(void);

/* Statistics and load */
/* A statistic, current (which STAT_CURRENT, with the aura) or maximum (which STAT_MAXIMUM), with its bonus, 10 to
   220. */
int champ_get_stat(Champion *c, int stat, int which);
/* The level of skill; with_bonus adds the skill bonus and the base skill's bonus. */
int champ_get_skill_level(int champ, int skill, int with_bonus);
/* value reduced when the stamina is below half of its maximum. */
int champ_scale_by_stamina(const Champion *c, int value);
/* The load in 1/10 kg that the champion can carry. */
int champ_get_max_load(Champion *c);
/* The load in 1/10 kg that the champion carries, with the leader hand object for the leader. */
int champ_get_load(int champ);

/* Hands and actions */
/* The slot that a hand loads from (12 or 7-9 for the action hand, 11 or 6 for the ready hand), -1 none. */
int champ_find_hand_source_slot(int champ, int hand);
/* Lists in g_champ_actions the actions of the thing in hand (THING_NONE: the champion's own actions);
   true when the action area has something to show. */
bool champ_list_actions(int champ, uint16_t thing, int hand);

/* Party */
/* The living champion in cell (0 to 3), -1 none. */
int champ_get_index_in_cell(int16_t cell);
/* Makes champ (-1: none) the leader. */
void champ_set_leader(int champ);

/* Damage, poison, and death (champion_damage.c). */
/* The damage of this game step, applied at its end; exported for the tests. */
extern int16_t g_champ_pending_damage[CHAMPION_MAX];
extern uint16_t g_champ_pending_wounds[CHAMPION_MAX]; /* the wounds of this game step; exported for the tests */
extern uint16_t g_champ_regen_counter; /* steps the regeneration of mana and health; exported for the tests */
/* Clears the damage of the step, the regeneration counter, the leader hand cache, and the recruit candidate, as at
   the program start. */
void champ_reset_step_state(void);
/* attack reduced by the statistic stat (anti-fire, anti-magic, vitality). */
int champ_get_stat_adjusted_attack(Champion *c, int stat, int attack);
/* The strength of champ with the item in hand for skill (-1: no skill), 0 to 100. */
int champ_get_strength(int champ, int hand, int skill);
/* The dexterity of champ with its load, 1 to 100. */
int champ_get_dexterity(int champ);
/* A luck test against chance; the luck changes by 2. True when the champion is lucky. */
bool champ_test_luck(Champion *c, int chance);
/* Wounds and damages champ (the damage shows at the end of the step); returns the damage, 0 when none. */
int champ_take_damage(int champ, int damage, int wound_mask, int attack_type);
/* Damages each champion by about damage; returns the mask of the champions that took damage. */
int champ_damage_party(int damage, int wound_mask, int attack_type);
/* Poisons champ: damage now, and a poison event that repeats with less. */
void champ_poison(int champ, int amount);
/* Takes amount away from the poison events of champ. */
void champ_cure_poison(int champ, int amount);
/* A click on the face of a champion in cell (bit 15: cancel the drag): the first click takes the face, the second
   swaps it with the champion of the second cell. */
void champ_drag_click(int16_t cell);
/* Cancels the drag of a champion face. */
void champ_drag_cancel(void);
/* Applies the damage and wounds of this step; a champion without health dies. */
void champ_apply_pending_damage(void);
/* Mana, food, water, stamina, health, and statistics change with the time. */
void champ_apply_time_effects(void);
/* The torches in the hands burn down. */
void champ_burn_held_lights(void);
/* Brings champ back to life in a free cell, without items, with less maximum health. */
void champ_revive(int champ);

/* Champion actions (champ_action.c). */
/* g_action.result after the last champ_perform_action (damage >= 0, failure < 0); set by champ_execute_selected_action. */
extern int16_t g_champ_action_result;
/* Runs action (0 to 2, -1: none) of the open action menu; true when the action was done. */
bool champ_execute_selected_action(int action);
/* Champ performs action (an index in g_champ_actions, bit 15: a repeat) with the item in the open hand. */
int champ_perform_action(int champ, int action);
/* Makes the hand (-1: both hands and the spell) of champ busy for about ticks game steps. */
void champ_add_action_delay(int champ, uint16_t ticks, int hand);
/* Takes the charges of the current action from its item; true when the item has no charges left. */
bool champ_use_action_charge(int champ);
/* The hand of champ is free again: a shooter or a thrower takes the next ammunition or throwing item. */
void champ_reload_hand(int champ, int hand);
/* The repeats of the actions of all champions. */
void champ_process_repeated_actions(void);
/* Adds experience in skill; each new level raises the statistics and prints a message. */
void champ_add_experience(int champ, int skill, uint32_t amount);
/* The level of the whole party: from the sum of the base skill experience of all champions. */
int champ_get_party_level(void);
/* Champ throws the item in hand (-1: the leader hand object) to the left or right side; true when it flies. */
bool champ_throw_object(int champ, int hand, int side);
/* The leader throws the object in the leader hand; true when it flies. */
bool champ_throw_leader_hand_object(int side);

/* The party wakes up (game.c). */
void champ_wake_party(void);

/* Champion objects and statistics (inventory.c). */
/* Puts thing into the hand of the leader; defer_cursor: the original changes the mouse cursor later. */
void champ_put_in_leader_hand(uint16_t thing, int defer_cursor);
/* Takes the object out of the hand of the leader; THING_NONE when the hand is empty. */
uint16_t champ_take_from_leader_hand(void);
/* Applies (or takes back) the effects of thing in slot of champ (slot -1: the leader hand). */
void champ_apply_item_effects(int champ, uint16_t thing, int16_t slot, int mode);
/* Computes the load of the champion: its slots and the items of its open chest. */
void champ_compute_load(int champ);
/* Changes a statistic of the champion by delta; big changes away from the maximum shrink. */
void champ_adjust_stat(uint8_t *stat, int delta);
/* Takes the item out of slot of champ (slots from SLOT_CONTAINER_FIRST on: the open chest); THING_NONE when empty. */
uint16_t champ_remove_object_from_slot(int champ, int16_t slot);
/* Puts thing into slot of champ. */
void champ_add_object_to_slot(int champ, uint16_t thing, int16_t slot);
/* Champ eats or drinks thing from slot (-1: the leader hand); thing THING_NONE: the leader hand object, for the
   champion of the open inventory (a click on the mouth). */
void champ_consume_object(int champ, uint16_t thing, int16_t slot);

/* Champion movement (move.c). */
/* Turns the party to dir; the champions turn with it. */
void champ_set_party_direction(int16_t dir);
/* The number of game steps that the champion needs for a move (load, wounds, haste). */
int champ_get_move_delay(int champ);
/* The champion nearest to the square (x, y) next to the party, seen from cell (0xff: any); -1 none. */
int champ_get_nearest_at(int16_t x, int16_t y, int16_t cell);
/* Takes stamina from a champion. */
void champ_decrease_stamina(int champ, int amount);

/* Champion launches (projectile.c). */
/* A champion throws or casts payload from the party square. */
void champ_launch_projectile(int champ, uint16_t payload, int energy, int attack, int step);

/* Party state that the interface sets (champion.c). */
/* The party falls asleep (on) or wakes; only the flag. */
void champ_set_party_sleeping(bool on);
/* The raised skill counts that the closed inventory of champ showed (shown[SKILL_BASE_COUNT]) no longer show green. */
void champ_forget_shown_raises(int champ, const uint8_t *shown);

#endif /* DM2_ENGINE_CHAMPION_H */
