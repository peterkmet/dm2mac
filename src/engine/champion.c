/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "champion.h"
#include "gdat.h"
#include "ids.h"
#include "action.h"
#include "champ_action.h"
#include "creature.h"
#include "dungeon.h"
#include "game.h"
#include "game_text.h"
#include "inventory.h"
#include "item.h"
#include "minion.h"
#include "move.h"
#include "random.h"
#include "recruit.h"
#include "sensor.h"

/* The aura that raises a statistic: its strength limit, and the random raise of the statistic. */
#define AURA_MAX               100
#define AURA_STAT_SHIFT        7
#define AURA_BASE              4
/* The load that a champion can carry, in 1/10 kg: per strength point, the base, and the rounding. */
#define LOAD_PER_STRENGTH      8
#define LOAD_BASE              100
#define LOAD_ROUND             10
/* A wounded champion carries 1/8 less, with wounded legs 1/4 less. */
#define WOUND_LOAD_SHIFT       3
#define WOUND_LEGS_LOAD_SHIFT  2
/* Each doubling of the experience above this is one more level. */
#define SKILL_LEVEL_EXPERIENCE 512
/* No slot that a hand loads from. */
#define NO_SLOT                (-1)
/* The container types of the minion maps. */
#define MINION_MAP_FIRST_TYPE  1
#define MINION_MAP_LAST_TYPE   2

ChampionAction g_champ_actions[CHAMPION_ACTION_MAX];
int g_champ_action_count;
uint16_t g_champ_action_thing = THING_NONE;
int g_party_sleeping;
uint8_t g_champ_skill_raised[CHAMPION_MAX][SKILL_BASE_COUNT];

/* Setup */

/* Sets redraw flags on all champions. */
static void set_redraw_flags(uint16_t flags)
{
    for (int i = 0; i < g_game.champion_count; i++)
        champion(i)->redraw |= flags;
}

/* A new game: the champion of the start square (map square (0, 0) with a mirror sensor) joins the party. */
static void add_start_champion(void)
{
    g_game.leader_hand = THING_NONE;
    g_leader_hand.icon = HAND_ICON_NONE;
    for (uint16_t t = dungeon_square_first_thing(g_party.cur_map, 0, 0); t != THING_ENDOFLIST;
         t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_SENSOR || sensor_type(t) != WALL_CLICK_PORTRAIT)
            continue;
        recruit_start_champion(0, 1, DIR_NORTH, g_game.party_map);
        champion(0)->dir = (uint8_t)g_game.party_dir;
        champion(0)->cell = (uint8_t)g_game.party_dir;
        set_redraw_flags(REDRAW_STATUS_BOX);
        champ_set_leader(0);
        return;
    }
}

void champ_init_party_state(void)
{
    if (g_session.new_game) {
        add_start_champion();
        return;
    }
    if (g_game.leader_hand == THING_NONE) {
        g_leader_hand.icon = HAND_ICON_NONE;
    } else {
        uint16_t leader = g_game.leader;
        g_game.leader = (uint16_t)-1;
        champ_put_in_leader_hand(g_game.leader_hand, 1);
        g_game.leader = leader;
    }
    for (int i = 0; i < g_game.champion_count; i++) {
        champion(i)->redraw &= REDRAW_KEEP;
        action_reset_champion(i);
    }
    set_redraw_flags(REDRAW_STATUS_BOX);
    int leader = (int16_t)g_game.leader;
    if (leader != -1) {
        g_game.leader = (uint16_t)-1;
        champ_set_leader(leader);
    }
}

/* Statistics and load */

/* v limited to low and high. */
static int clamp(int low, int v, int high)
{
    return v < low ? low : v > high ? high : v;
}

int champ_get_stat(Champion *c, int stat, int which)
{
    int v = c->stats[stat][which];
    if (which == STAT_CURRENT && c->aura_value && c->aura >= AURA_STAT_FIRST && c->aura <= AURA_STAT_LAST &&
        stat == c->aura - AURA_STAT_OFFSET) {
        int aura = c->aura_value > AURA_MAX ? AURA_MAX : c->aura_value;
        v = (uint16_t)(v + rand_below((uint16_t)((aura * v >> AURA_STAT_SHIFT) + 1)) + AURA_BASE);
    }
    return clamp(STAT_MIN, (int16_t)(c->stat_bonus[stat] + v), STAT_MAX);
}

int champ_get_skill_level(int champ, int skill, int with_bonus)
{
    if (g_party_sleeping)
        return 1;
    const Champion *c = champion(champ);
    uint32_t experience = c->experience[skill];
    if (skill >= SKILL_BASE_COUNT) {
        int base = champ_base_skill(skill);
        int factor = with_bonus ? c->skill_bonus[base] + 1 : 1;
        experience = (experience + (uint32_t)(factor * (int32_t)c->experience[base])) >> 1;
    }
    int level = 1;
    for (; experience >= SKILL_LEVEL_EXPERIENCE; experience >>= 1)
        level++;
    if (with_bonus) {
        int16_t v = (int16_t)(level + c->skill_bonus[skill]);
        level = v > 1 ? v : 1;
    }
    return level;
}

int champ_scale_by_stamina(const Champion *c, int value)
{
    int16_t half = (int16_t)(c->max_stamina >> 1);
    if (c->stamina < half) {
        value = (int16_t)value >> 1;
        value = (int16_t)(c->stamina * value / half) + value;
    }
    return value;
}

int champ_get_max_load(Champion *c)
{
    int strength = champ_get_stat(c, STAT_STRENGTH, 0);
    uint16_t load = (uint16_t)champ_scale_by_stamina(c, (int16_t)(strength * LOAD_PER_STRENGTH + LOAD_BASE));
    if (c->wounds)
        load = (uint16_t)(load - (load >> ((c->wounds & WOUND_LEGS) == 0 ? WOUND_LOAD_SHIFT : WOUND_LEGS_LOAD_SHIFT)));
    uint16_t rounded = (uint16_t)(load + LOAD_ROUND - 1);
    return (uint16_t)(rounded - rounded % LOAD_ROUND);
}

int champ_get_load(int champ)
{
    const Champion *c = champion(champ);
    if (!c->health)
        return 0;
    uint16_t load = c->load;
    if (champ == g_game.leader)
        load = (uint16_t)(load + g_leader_hand.weight);
    return load;
}

/* Hands and actions */

int champ_find_hand_source_slot(int champ, int hand)
{
    const Champion *c = champion(champ);
    if (hand == SLOT_ACTION_HAND) {
        if (c->slots[SLOT_QUIVER_ACTION] != THING_NONE)
            return SLOT_QUIVER_ACTION;
        for (int s = SLOT_QUIVER_FIRST + 1; s <= SLOT_QUIVER_LAST; s++)
            if (c->slots[s] != THING_NONE)
                return s;
    } else if (hand == SLOT_READY_HAND) {
        if (c->slots[SLOT_QUIVER_READY] != THING_NONE)
            return SLOT_QUIVER_READY;
        if (c->slots[SLOT_QUIVER_FIRST] != THING_NONE)
            return SLOT_QUIVER_FIRST;
    }
    return NO_SLOT;
}

/* True when a minion map item allows command: summoning without a minion, its commands with one. */
static bool is_command_allowed(uint16_t thing, int command)
{
    if (thing == THING_NONE)
        return false;
    if (thing_type(thing) != THING_CONTAINER)
        return true;
    if (container_kind(thing) != CONTAINER_MINION_MAP)
        return true;
    int type = container_type(thing);
    if (type < MINION_MAP_FIRST_TYPE || type > MINION_MAP_LAST_TYPE)
        return false;
    uint16_t group = thing_word(thing, MINION_MAP_GROUP);
    if (!minion_find_link_projectile(group, thing)) {
        if (command == COMMAND_MINION_ATTACK || command == COMMAND_MINION_MAP_SET)
            return true;
        if (thing_word(thing, MINION_MAP_LOCATION) == THING_NONE)
            return false;
        return command == COMMAND_MINION_GUARD || command == COMMAND_MINION_FETCH;
    }
    if (command == COMMAND_MINION_DISMISS)
        return true;
    if (type != MINION_MAP_LAST_TYPE)
        return false;
    uint8_t creature = group_creature(group);
    if (creature == MINION_FETCH)
        return command == COMMAND_MINION_GUARD;
    if (creature == MINION_GUARD)
        return command == COMMAND_MINION_FETCH;
    return false;
}

bool champ_list_actions(int champ, uint16_t thing, int hand)
{
    int category, index;
    if (thing == THING_NONE) {
        category = CAT_CHAMPION;
        index = champion(champ)->portrait;
    } else {
        if (item_is_moneybox(thing) || item_is_chest(thing))
            return true;
        category = item_gdat_category(thing);
        index = item_gdat_index(thing);
    }
    int count = 0;
    for (int entry = GFX_ACTION_FIRST; entry <= GFX_ACTION_LAST && count < CHAMPION_ACTION_MAX; entry++) {
        if (!gdat_exists(gdat_current(), category, index, GDAT_TYPE_TEXT, entry))
            continue;
        int command = text_gdat_field(category, index, entry, FIELD_COMMAND);
        if (!command)
            continue;
        int16_t needs_hand = text_gdat_field(category, index, entry, FIELD_HAND);
        if (needs_hand && needs_hand - 1 != hand)
            continue;
        if (thing == THING_NONE) {
            if (command == COMMAND_LOAD && champ_find_hand_source_slot(champ, hand) < 0)
                continue;
        } else {
            if (!is_command_allowed(thing, command))
                continue;
            int16_t needs = text_gdat_field(category, index, entry, FIELD_CHARGES);
            if (needs == CHARGES_EMPTY) {
                if (item_charges(thing))
                    continue;
            } else {
                if (needs == CHARGES_USE_ALL || needs == CHARGES_USE_NONE)
                    needs = 1;
                if (needs && (int16_t)item_charges(thing) < needs)
                    continue;
            }
        }
        uint16_t skill = (uint16_t)text_gdat_field(category, index, entry, FIELD_SKILL);
        uint16_t level = (uint16_t)text_gdat_field(category, index, entry, FIELD_LEVEL);
        if (level <= (uint16_t)champ_get_skill_level(champ, skill, 1))
            g_champ_actions[count++] = (ChampionAction){(uint8_t)category, (uint8_t)index, (uint8_t)entry};
    }
    g_champ_action_count = count;
    g_champ_action_thing = thing;
    return item_is_minion_map(thing) || count > 0;
}

/* Party */

int champ_get_index_in_cell(int16_t cell)
{
    for (int i = 0; i < g_game.champion_count; i++)
        if (champion(i)->cell == cell && champion(i)->health)
            return i;
    return -1;
}

void champ_set_leader(int champ)
{
    int16_t leader = (int16_t)g_game.leader;
    if (champ == leader || (champ != -1 && !champion(champ)->health))
        return;
    if (leader != -1)
        champion(leader)->redraw |= REDRAW_LOAD | REDRAW_NAME;
    g_game.leader = (uint16_t)champ;
    if (champ != -1 && champ + 1 != g_ui.candidate)
        champion(champ)->redraw |= REDRAW_LOAD | REDRAW_NAME;
}

void champ_set_party_sleeping(bool on)
{
    g_party_sleeping = on;
}

void champ_forget_shown_raises(int champ, const uint8_t *shown)
{
    for (int i = 0; i < SKILL_BASE_COUNT; i++)
        g_champ_skill_raised[champ][i] = (uint8_t)(g_champ_skill_raised[champ][i] - shown[i]);
}
