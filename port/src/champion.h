#ifndef CHAMPION_H
#define CHAMPION_H
#include <stdint.h>
#include "save.h"
#include "timeline.h"

/* Champion slots: the hands, the body and pack slots (to 29), then the slots of an open container. */
enum {
    SLOT_READY_HAND, SLOT_ACTION_HAND, SLOT_HAND_COUNT, SLOT_CONTAINER_TEST = 12, SLOT_COUNT = 30,
    SLOT_CONTAINER_FIRST = SLOT_COUNT, CONTAINER_SLOTS = 8
};
/* SLOT_CONTAINER_TEST: when the open hand item fits this slot, container slots take only pouch items. */

enum { STAT_LUCK, STAT_STRENGTH, STAT_DEXTERITY, STAT_WISDOM, STAT_VITALITY, STAT_ANTI_MAGIC, STAT_ANTI_FIRE, STAT_COUNT };
#define SKILL_COUNT 20
#define SKILL_BASE_COUNT 4      /* skills 4 to 19 are sub-skills: 4 per base skill */
#define CHAMPION_NAME_SIZE 8
#define CHAMPION_TITLE_SIZE 20
#define CHAMPION_RUNES_SIZE 4
#define ACTION_DELAY_COUNT 3    /* the two hands and the spell */
#define ACTION_SPELL 2

/* Champion redraw flags. */
#define REDRAW_LOAD 0x1000
#define REDRAW_TITLE_ALL 0x2000
#define REDRAW_STATUS_BOX 0x4000
#define REDRAW_BARS 0x0800
#define REDRAW_NAME 0x0400
#define REDRAW_ALL_INVENTORY 0x8000
#define REDRAW_ALL 0xfc00
#define REDRAW_KEEP 0x03ff       /* the bits that stay after a redraw */

/* Aura types: 0 to 2 shields, 3 to 6 raise a statistic. */
#define AURA_STAT_FIRST 3
#define AURA_STAT_LAST 6
#define AURA_STAT_OFFSET 2      /* aura type - 2 is the statistic */

/* The champion record of the original (263 bytes). */
typedef struct __attribute__((packed)) {
    char name[CHAMPION_NAME_SIZE];
    char title[CHAMPION_TITLE_SIZE];
    uint8_t dir, cell;
    uint8_t rune_count;
    uint8_t poisoned;
    uint8_t last_command[2];
    char runes[CHAMPION_RUNES_SIZE];
    uint8_t unknown_26[4];
    uint8_t action_delay[ACTION_DELAY_COUNT];
    uint8_t unknown_2d;
    uint16_t damage_event;      /* timeline event of the damage display, THING_NONE none */
    uint16_t damage;
    uint16_t redraw;
    uint16_t wounds;
    int16_t health, max_health, stamina, max_stamina, mana, max_mana;
    int8_t defense[2];
    int16_t food, water;
    int16_t poison;
    uint8_t stats[STAT_COUNT][2];   /* current, maximum */
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

#define WOUND_LEGS 0x10
#define NO_CHAMPION_CELL 0xff
#define DRAG_CANCEL 0x8000

/* Bytes of game.magic_light: the party spells. */
enum { MAGIC_INVISIBILITY = 2, MAGIC_ACTION_EFFECT = 3, MAGIC_HASTE = 4 };

/* One action of an item in a champion's hand: its GDAT text (category, index, 5, entry). */
typedef struct {
    uint8_t category, index, entry;
} ChampionAction;
#define CHAMPION_ACTION_MAX 3

extern ChampionAction champ_actions[CHAMPION_ACTION_MAX];
extern int champ_action_count;
extern uint16_t champ_action_thing;
/* The party sleeps; Step 14 sets it. */
extern int party_sleeping;
/* The base skills whose level went up, per champion; the inventory shows them green,
   and closing it takes back the counts that it showed. Step 15 sets them. */
extern uint8_t champ_skill_raised[CHAMPION_MAX][SKILL_BASE_COUNT];

static inline Champion *champion(int index) { return (Champion *)game.champions[index]; }

/* A statistic, current (which 0, with the aura) or maximum (which 1), with its bonus, 10 to 220. */
int champ_get_stat(Champion *c, int stat, int which);
/* The level of skill; with_bonus adds the skill bonus and the base skill's bonus. */
int champ_get_skill_level(int champ, int skill, int with_bonus);
/* value reduced when the stamina is below half of its maximum. */
int champ_scale_by_stamina(const Champion *c, int value);
/* The load in 1/10 kg that the champion can carry. */
int champ_get_max_load(Champion *c);
/* The load in 1/10 kg that the champion carries, with the leader hand object for the leader. */
int champ_get_load(int champ);
/* The slot that a hand loads from (12 or 7-9 for the action hand, 11 or 6 for the ready hand), -1 none. */
int champ_find_hand_source_slot(int champ, int hand);
/* Lists in champ_actions the actions of the thing in hand (THING_NONE: the champion's own actions);
   returns 1 when the action area has something to show. */
int champ_list_actions(int champ, uint16_t thing, int hand);
/* The living champion in cell (0 to 3), -1 none. */
int champ_get_index_in_cell(int cell);
/* Makes champ (-1: none) the leader. */
void champ_set_leader(int champ);
/* Damage, poison, and death (damage.c). */
extern int16_t champ_pending_damage[CHAMPION_MAX];   /* the damage of this game step, applied at its end */
extern uint16_t champ_pending_wounds[CHAMPION_MAX];
extern uint16_t champ_regen_counter;                 /* steps the regeneration of mana and health */
/* attack reduced by the statistic stat (anti-fire, anti-magic, vitality). */
int champ_get_stat_adjusted_attack(Champion *c, int stat, int attack);
/* The strength of champ with the item in hand for skill (-1: no skill), 0 to 100. */
int champ_get_strength(int champ, int hand, int skill);
/* The dexterity of champ with its load, 1 to 100. */
int champ_get_dexterity(int champ);
/* A luck test against chance; the luck changes by 2. Returns 1 when the champion is lucky. */
int champ_test_luck(Champion *c, int chance);
/* Wounds and damages champ (the damage shows at the end of the step); returns the damage, 0 when none. */
int champ_take_damage(int champ, int damage, int wound_mask, int attack_type);
/* Damages each champion by about damage; returns the mask of the champions that took damage. */
int champ_damage_party(int damage, int wound_mask, int attack_type);
/* Poisons champ: damage now, and a poison event that repeats with less. */
void champ_poison(int champ, int amount);
/* Takes amount away from the poison events of champ. */
void champ_cure_poison(int champ, int amount);
/* Clears the damage of the step, the regeneration counter, and the leader hand cache, as at the program start. */
void champ_reset_step_state(void);
/* Applies the damage and wounds of this step; a champion without health dies. */
void champ_apply_pending_damage(void);
/* Kills champ: it drops its items and its bones. */
void champ_kill(int champ);
/* Mana, food, water, stamina, health, and statistics change with the time. */
void champ_apply_time_effects(void);
/* The torches in the hands burn down. */
void items_burn_held_lights(void);
/* Brings champ back to life in a free cell, without items, with less maximum health. */
void champ_revive(int champ);
/* A step of an altar rebirth (event 0x0d). */
void timeline_event_altar_rebirth(TimelineEvent *e);
/* A click on the face of a champion in cell (bit 15: cancel the drag): the first click takes the face, the second
   swaps it with the champion of the second cell. */
void champ_drag_click(int cell);
void champ_drag_cancel(void);

/* Prepares the party of a loaded game for the screen: redraw flags, drawn hand state, the leader. */
void champ_init_party_state(void);

#endif
