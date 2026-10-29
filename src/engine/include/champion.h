/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_CHAMPION_H
#define DM2_ENGINE_CHAMPION_H
/* Prefixes: champ_ champion inv_ */

/* The champions: the leader hand, their slots, statistics, wounds, and the effects of their items and actions. */

#include <stdbool.h>
#include <stdint.h>
#include "save.h"

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
#define SKILL_HEAL       13
#define SKILL_INFLUENCE  14
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
#define REDRAW_SLOW_STEP     0x0010 /* a slow step time; no code sets it, as in SKULL.EXE */

/* Aura types: 0 to 2 shields, 3 to 6 raise a statistic. */
#define AURA_FIRE_SHIELD  0
#define AURA_SPELL_SHIELD 1
#define AURA_DEFENSE      2
#define AURA_STRENGTH     3
#define AURA_DEXTERITY    4
#define AURA_WISDOM       5
#define AURA_VITALITY     6
#define AURA_STAT_FIRST   AURA_STRENGTH
#define AURA_STAT_LAST    AURA_VITALITY
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

/* Champion.wounds bits: the hands, the head, the torso, the legs, the feet. */
#define WOUND_READY_HAND      0x01
#define WOUND_ACTION_HAND     0x02
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
/* The attack type bit of an attack that a parry can block; packed: the type comes from the creature table. */
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

/* Bytes of g_game.magic_light: the party spells, and the maximum of the haste byte. */
#define MAGIC_INVISIBILITY  2
#define MAGIC_ACTION_EFFECT 3
#define MAGIC_HASTE         4
#define HASTE_MAX           255

/* Modes of champ_apply_item_effects: how an item changes the champion that holds it. */
enum EffectMode {
    EFFECT_MODE_LOAD = 0,         /* a loaded game: the bonuses of the items that the champion holds */
    EFFECT_MODE_ADD = 1,          /* the item goes into a slot or the hand */
    EFFECT_MODE_REMOVE = -1,      /* the item leaves a slot or the hand */
    EFFECT_MODE_CONSUME = 2,      /* the champion eats or drinks the item */
    EFFECT_MODE_CONSUME_END = -2, /* the timed effect of a consumed item ends */
    EFFECT_MODE_RESTORE = 3       /* a loaded game: a timed effect of a consumed item */
};
/* Data bytes of an item effect event: the thing type and the GDAT index of the consumed item. */
#define EFFECT_DATA_TYPE  0
#define EFFECT_DATA_INDEX 2
/* The leader hand icon: its size in pixels, its color numbers, and the color number that stays clear. */
#define HAND_ICON_SIZE    16
#define HAND_ICON_COLORS  16
#define HAND_ICON_KEY     12
/* The icon entry of no leader hand icon. */
#define HAND_ICON_NONE    0xff
/* The mouth picture of the inventory: GDAT (CAT_WALL_ORNAMENT, MOUTH_ORNAMENT, GDAT_TYPE_IMAGE, entry). */
#define MOUTH_ORNAMENT    0x5b

/* The leader hand object as the original keeps it beside g_game.leader_hand. */
typedef struct {
    uint16_t attr;                                   /* item attribute 0 (flags) of the object */
    uint16_t weight;                                 /* its weight when it went into the hand */
    uint8_t icon;                                    /* the icon entry that the cursor shows, HAND_ICON_NONE none */
    uint8_t cursor[HAND_ICON_SIZE * HAND_ICON_SIZE]; /* the icon: color numbers 0 to 15, HAND_ICON_KEY clear */
    uint8_t palette[HAND_ICON_COLORS];               /* the palette colors of the color numbers */
} LeaderHand;

extern LeaderHand g_leader_hand;

/* One action of an item in a champion's hand: its GDAT text (category, index, GDAT_TYPE_TEXT, entry). */
typedef struct {
    uint8_t category, index, entry;
} ChampionAction;
/* The actions that the action menu of a hand shows at most. */
#define CHAMPION_ACTION_MAX 3
/* The open action menu: its actions, their count, and the hand thing. */
typedef struct {
    ChampionAction actions[CHAMPION_ACTION_MAX];
    int count;
    uint16_t thing;
} ChampActionList;

extern ChampionAction g_champ_actions[CHAMPION_ACTION_MAX];
extern int g_champ_action_count;
/* The hand thing of the open action menu; exported for the tests. */
extern uint16_t g_champ_action_thing;
/* The party sleeps. */
extern bool g_party_sleeping;
/* The base skills whose level went up, per champion; the inventory shows them green,
   and closing it takes back the counts that it showed. */
extern uint8_t g_champ_skill_raised[CHAMPION_MAX][SKILL_BASE_COUNT];

/* The champion record of the party slot index. */
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
int champ_get_stat(const Champion *c, int stat, int which);
/* The level of skill; with_bonus adds the skill bonus and the base skill's bonus. */
int champ_get_skill_level(int champ, int skill, bool with_bonus);
/* value reduced when the stamina is below half of its maximum. */
int champ_scale_by_stamina(const Champion *c, int value);
/* The load in 1/10 kg that the champion can carry. */
int champ_get_max_load(const Champion *c);
/* The load in 1/10 kg that the champion carries, with the leader hand object for the leader. */
int champ_get_load(int champ);
/* The number of game steps that the champion needs for a move (load, wounds, haste). */
int champ_get_move_delay(int champ);

/* Hands and actions */
/* Lists in g_champ_actions the actions of the thing in hand (THING_NONE: the champion's own actions);
   true when the action area has something to show. */
bool champ_list_actions(int champ, uint16_t thing, int hand);
/* Copies the open action menu into out. */
void champ_action_list_save(ChampActionList *out);
/* Makes the saved list in the open action menu again. */
void champ_action_list_restore(const ChampActionList *in);

/* Party */
/* The living champion in cell (0 to 3), -1 none. */
int champ_get_index_in_cell(int16_t cell);
/* Makes champ (-1: none) the leader. */
void champ_set_leader(int champ);
/* The index of the leader, -1 for none. */
int16_t champ_leader(void);
/* Gives champion_index a new name and title: the whole fields, the bytes after the end marks too. */
void champ_set_name(int16_t champion_index, const char name[CHAMPION_NAME_SIZE], const char title[CHAMPION_TITLE_SIZE]);
/* Turns the party to dir; the champions turn with it. */
void champ_set_party_direction(int16_t dir);
/* Turns one champion to dir; the party keeps its direction. */
void champ_set_direction(int16_t champion_index, int16_t dir);
/* The party wakes up. */
void champ_wake_party(void);

/* Damage, poison, and death (champion_damage.c). */
/* The damage of this game step, applied at its end; exported for the tests. */
extern int16_t g_champ_pending_damage[CHAMPION_MAX];
extern uint16_t g_champ_pending_wounds[CHAMPION_MAX]; /* the wounds of this game step; exported for the tests */
extern uint16_t g_champ_regen_counter; /* steps the regeneration of mana and health; exported for the tests */
/* Clears the damage of the step, the action repeats, the raised skills, the regeneration counter, the leader hand
   cache, and the recruit candidate, as at the program start. */
void champ_reset_step_state(void);
/* attack reduced by the statistic stat (anti-fire, anti-magic, vitality). */
int champ_get_stat_adjusted_attack(const Champion *c, int stat, int attack);
/* The strength of champ with the item in hand for skill (-1: no skill), 0 to 100; exported for the tests. */
int champ_get_strength(int champ, int hand, int skill);
/* The dexterity of champ with its load, 1 to 100. */
int champ_get_dexterity(int champ);
/* A luck test against chance; the luck changes by 2. True when the champion is lucky. */
bool champ_test_luck(Champion *c, int chance);
/* Wounds and damages champ (the damage shows at the end of the step); returns the damage, 0 when none. */
int champ_take_damage(int champ, int damage, int wound_mask, int attack_type);
/* Takes stamina from a champion. */
void champ_decrease_stamina(int champ, int amount);
/* Damages each champion by about damage; returns the mask of the champions that took damage. */
int champ_damage_party(int damage, int wound_mask, int attack_type);
/* Poisons champ: damage now, and a poison event that repeats with less. */
void champ_poison(int champ, int amount);
/* Takes amount away from the poison events of champ; exported for the tests. */
void champ_cure_poison(int champ, int amount);
/* A click on the face of a champion in cell: the first click takes the face, the second swaps it with the champion
   of the second cell. */
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

/* Champion actions (champion_action.c). */
/* g_action.result after the last champ_perform_action (damage >= 0, failure < 0); set by champ_execute_selected_action. */
extern int16_t g_champ_action_result;
/* Clears g_champ_action_result before a new action. */
void champ_action_clear_result(void);
/* Makes the hand (-1: both hands and the spell) of champ busy for about ticks game steps. */
void champ_add_action_delay(int champ, uint16_t ticks, int hand);
/* Takes the charges of action from its item in the open hand of champ; true when the item has no charges left;
   exported for the tests. */
bool champ_use_action_charge(int champ, ChampionAction action);
/* The hand of champ is free again: a shooter or a thrower takes the next ammunition or throwing item. */
void champ_reload_hand(int champ, int hand);
/* The level of the whole party: from the sum of the base skill experience of all champions. */
int champ_get_party_level(void);
/* Adds experience in skill; each new level raises the statistics and prints a message. */
void champ_add_experience(int champ, int skill, uint32_t experience);
/* Champ performs the action of g_champ_actions at index with the item in the open hand; a repeat (repeated) takes no
   delay, stamina, or experience. True when the action was done; exported for the tests. */
bool champ_perform_action(int champ, int index, bool repeated);
/* Runs the action at index (0 to 2, -1: none) of the open action menu; true when the action was done. */
bool champ_execute_selected_action(int index);
/* The repeats of the actions of all champions. */
void champ_process_repeated_actions(void);
/* The leader throws the object in the leader hand to the left or the right side; true when it flies. */
bool champ_throw_leader_hand_object(bool right_side);

/* The leader hand, the champion slots, and the item effects (champion_inventory.c). */
/* Computes the load of the champion: its slots and the items of its open chest. */
void champ_compute_load(int champ);
/* Applies (or takes back) the effects of thing in slot of champ (slot -1: the leader hand). */
void champ_apply_item_effects(int champ, uint16_t thing, int16_t slot, enum EffectMode mode);
/* The object of the leader hand that the cursor shows, THING_NONE none. */
uint16_t inv_cursor_object(void);
/* Puts thing into the hand of the leader; the cursor shows it from the next frame on (inv_cursor_object). */
void champ_put_in_leader_hand(uint16_t thing);
/* Takes the object out of the hand of the leader; THING_NONE when the hand is empty. */
uint16_t champ_take_from_leader_hand(void);
/* Updates the cursor when the icon of the animated leader hand object changes. */
void inv_update_hand_cursor(void);
/* Takes the item out of slot of champ (slots from SLOT_CONTAINER_FIRST on: the open chest); THING_NONE when empty. */
uint16_t champ_remove_object_from_slot(int champ, int16_t slot);
/* Puts thing into slot of champ. */
void champ_add_object_to_slot(int champ, uint16_t thing, int16_t slot);
/* A click on a slot: 0 to 7 the hands in the status boxes, then the inventory slots, then the open chest. The leader
   hand object and the item in the slot change places. */
void inv_click_slot(int16_t slot);
/* Puts the leader hand object into the first free pouch or pack slot of champ. */
void inv_store_leader_hand_object(int champ);
/* Puts thing into the first free slot of champ that takes it. */
void inv_put_in_free_slot(int champ, uint16_t thing);
/* Champ eats or drinks thing from slot (-1: the leader hand); thing THING_NONE: the leader hand object, for the
   champion of the open inventory (a click on the mouth). */
void champ_consume_object(int champ, uint16_t thing, int16_t slot);

/* Party state that the interface sets (champion.c). */
/* The party falls asleep (on) or wakes; only the flag. */
void champ_set_party_sleeping(bool on);
/* The raised skill counts that the closed inventory of champ showed (shown[SKILL_BASE_COUNT]) no longer show green. */
void champ_forget_shown_raises(int champ, const uint8_t *shown);

#endif /* DM2_ENGINE_CHAMPION_H */
