/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The leader hand, the champion slots, and the effects of the items that the champions hold. */
#include <string.h>
#include "action.h"
#include "champion.h"
#include "champion_internal.h"
#include "dungeon.h"
#include "game.h"
#include "hooks.h"
#include "item.h"
#include "magic.h"
#include "random.h"
#include "sensor.h"
#include "sound_request.h"
#include "weather.h"

/* ATTR_FLAGS bits of the effects. */
#define ATTR_HAS_EFFECTS       0x2000 /* ATTR_FLAGS bit: the item changes the champion */
/* Item attributes of the effects. */
#define ATTR_FOOD              0x03 /* item attribute: the food value */
#define ATTR_EFFECT_TIME       0x13 /* game steps until the effect of a consumed item ends */
#define ATTR_MANA_MAXIMUM      0x14
#define ATTR_STAT_FIRST        0x15
#define ATTR_SKILL_FIRST       0x1e
#define ATTR_LIGHT             0x32
#define ATTR_MOVE_SPEED        0x33
/* Bits of an item modifier attribute. */
#define MODIFIER_CONSUMED      0x4000 /* the modifier counts only when the item is consumed */
#define MODIFIER_ANY_SLOT      0x8000 /* the modifier counts in any slot */
/* Redraw flags of the item effects. */
#define REDRAW_STATS           (REDRAW_TITLE_ALL | REDRAW_LOAD)
#define REDRAW_SKILLS          REDRAW_TITLE_ALL
#define REDRAW_EATEN           (REDRAW_TITLE_ALL | REDRAW_LOAD | REDRAW_BARS)
/* Limits of the mana and the statistic changes. */
#define MANA_MAX               999
#define STAT_STEP              20 /* a statistic change shrinks by a quarter for each step away from the maximum */
/* The message color of an object name. */
#define MESSAGE_OBJECT_NAME    13
/* inv_click_slot slots 0 to 7: the hands of the status boxes. */
#define CLICK_STATUS_HANDS     8
/* Eating and drinking. */
#define WATER_PER_DRINK        800
#define WATER_PER_POTION       1600
#define MANA_POTION_MAX        900
#define DIVISOR_POWER_TOP      511 /* drink_potion: the divisor of the stat gains falls as the power rises */
#define DIVISOR_POWER_BASE     32
#define WOUND_TRIES            10
/* Potion gains by power: a statistic gains power / 25 + 8, strength power / 35 + 5. */
#define POTION_GAIN_DIVISOR    25
#define POTION_GAIN_BASE       8
#define STRENGTH_GAIN_DIVISOR  35
#define STRENGTH_GAIN_BASE     5
#define MANA_GAIN_LOSS         8 /* a mana potion gives twice the gain less this */
#define ANTIDOTE_POWER_DIVISOR 7
#define HEALTH_TRIES_DIVISOR   42 /* a health potion tries power / 42 times to heal each wound */
#define DIVISOR_POWER_SHIFT    3

LeaderHand g_leader_hand = {.icon = HAND_ICON_NONE};

/* The magic light of the party (the first word of g_game.magic_light). */
static void add_magic_light(int delta)
{
    save_set_magic_light((int16_t)(save_magic_light() + delta));
}

/* The modifier of attribute attr of thing for mode; equipped: the item is in a slot that fits it. */
static int stat_modifier(uint16_t thing, int attr, bool equipped, enum EffectMode mode)
{
    uint16_t v = item_attr(thing, attr);
    if (!v)
        return 0;
    if (!(v & MODIFIER_CONSUMED)) {
        if (!equipped && !(v & MODIFIER_ANY_SLOT))
            return 0;
    } else if (mode != EFFECT_MODE_CONSUME_END && mode != EFFECT_MODE_CONSUME && mode != EFFECT_MODE_RESTORE) {
        return 0;
    }
    int r = (int8_t)v;
    return mode < 0 ? -r : r;
}

/* Changes a statistic of the champion by delta; big changes away from the maximum shrink. */
static void adjust_stat(uint8_t *stat_values, int delta)
{
    int16_t d = (int16_t)delta;
    int16_t diff = (int16_t)(stat_values[0] + d - stat_values[1]);
    if ((diff < 0) == (d < 0)) {
        for (int16_t left = (int16_t)(diff < 0 ? -diff : diff); left > STAT_STEP; left = (int16_t)(left - STAT_STEP))
            d = (int16_t)(d - (d >> 2));
    }
    stat_values[0] = (uint8_t)champ_clamp16(STAT_MIN, (int16_t)(stat_values[0] + d), STAT_MAX);
}

void champ_compute_load(int champ)
{
    Champion *c = champion(champ);
    uint16_t load = 0;
    for (int s = 0; s < SLOT_COUNT; s++)
        load = (uint16_t)(load + item_weight(c->slots[s]));
    if (champ == g_action.champion - 1 && g_action.hand < SLOT_HAND_COUNT &&
        item_is_chest(g_action.hand_items[champ][g_action.hand]))
        for (int i = 0; i < CONTAINER_SLOTS; i++)
            load = (uint16_t)(load + item_weight(g_action.open_items[i]));
    c->load = load;
    c->redraw |= REDRAW_LOAD;
}

/* The mana and statistic modifiers of thing change champion c. */
static void apply_stat_modifiers(Champion *c, uint16_t thing, bool equipped, enum EffectMode mode)
{
    int m = stat_modifier(thing, ATTR_MANA_MAXIMUM, equipped, mode);
    if (m) {
        if (mode == EFFECT_MODE_ADD || mode == EFFECT_MODE_REMOVE)
            c->max_mana = (int16_t)(c->max_mana + m);
        else if (mode == EFFECT_MODE_CONSUME)
            c->mana = champ_clamp16(0, (int16_t)(m + c->mana), MANA_MAX);
        c->redraw |= REDRAW_BARS;
    }
    for (int s = 0; s < STAT_COUNT; s++) {
        m = stat_modifier(thing, ATTR_STAT_FIRST + s, equipped, mode);
        if (!m)
            continue;
        if (mode == EFFECT_MODE_ADD || mode == EFFECT_MODE_REMOVE)
            c->stat_bonus[s] = (int8_t)(c->stat_bonus[s] + m);
        else
            adjust_stat(c->stats[s], m);
        c->redraw |= REDRAW_STATS;
    }
}

/* Adds the event that ends the timed effect of the consumed thing on champ. */
static void add_effect_end_event(int champ, uint16_t thing)
{
    /* fix: the original leaves the last 3 data bytes uninitialized */
    TimelineEvent e = {.time_map =
                           timeline_time_map(g_game.game_time + item_attr(thing, ATTR_EFFECT_TIME), g_game.party_map),
                       .type = EVENT_ITEM_EFFECT,
                       .priority = (uint8_t)champ};
    e.data[EFFECT_DATA_TYPE] = (uint8_t)thing_type(thing);
    e.data[EFFECT_DATA_INDEX] = item_gdat_index(thing);
    timeline_add_event(&e);
}

void champ_apply_item_effects(int champ, uint16_t thing, int16_t slot, enum EffectMode mode)
{
    if ((int16_t)champ < 0 || thing == THING_NONE)
        return;
    if ((int16_t)slot < SLOT_COUNT) {
        uint16_t flags = item_attr(thing, ATTR_FLAGS);
        if (mode && flags & ITEM_FLAG_LIGHT)
            light_update_party_light();
        if (flags & ATTR_HAS_EFFECTS) {
            Champion *c = champion(champ);
            bool equipped = item_fits_slot(thing, slot, 1) != 0;
            bool timed = false;
            int m;
            if (mode != EFFECT_MODE_LOAD && mode != EFFECT_MODE_RESTORE && mode != EFFECT_MODE_CONSUME_END)
                apply_stat_modifiers(c, thing, equipped, mode);
            for (int s = 0; s < SKILL_COUNT; s++) {
                if (!(m = stat_modifier(thing, ATTR_SKILL_FIRST + s, equipped, mode)))
                    continue;
                c->skill_bonus[s] = (int8_t)(c->skill_bonus[s] + m);
                c->redraw |= REDRAW_SKILLS;
                timed = true;
            }
            if ((m = stat_modifier(thing, ATTR_MOVE_SPEED, equipped, mode))) {
                champ_add_move_speed(c, m);
                timed = true;
            }
            if (mode != EFFECT_MODE_CONSUME && mode != EFFECT_MODE_CONSUME_END && mode != EFFECT_MODE_RESTORE &&
                (m = stat_modifier(thing, ATTR_LIGHT, equipped, mode))) {
                add_magic_light(m);
                if (mode)
                    light_update_party_light();
            }
            if (timed && mode == EFFECT_MODE_CONSUME)
                add_effect_end_event(champ, thing);
        }
    }
    if (mode)
        champ_compute_load(champ);
}

uint16_t inv_cursor_object(void)
{
    return g_game.leader_hand;
}

void inv_show_object_name(uint16_t thing)
{
    g_hooks.print_newline();
    g_hooks.print_message(MESSAGE_OBJECT_NAME, item_name(thing));
}

void champ_put_in_leader_hand(uint16_t thing, bool defer_cursor)
{
    if (thing == THING_NONE)
        return;
    thing &= THING_ID_MASK;
    g_leader_hand.attr = item_attr(thing, ATTR_FLAGS);
    g_leader_hand.weight = (uint16_t)item_weight(thing);
    g_game.leader_hand = thing;
    g_hooks.render_hand_icon();
    inv_show_object_name(thing);
    (void)defer_cursor; /* the port draws the cursor each frame (inv_cursor_object) */
    champ_apply_item_effects((int16_t)g_game.leader, thing, -1, EFFECT_MODE_ADD);
    sensor_enter_square(g_game.party_x, g_game.party_y, THING_NONE, true, false);
}

uint16_t champ_take_from_leader_hand(void)
{
    uint16_t thing = g_game.leader_hand;
    if (thing == THING_NONE)
        return thing;
    g_leader_hand.attr = 0;
    g_leader_hand.weight = 0;
    g_game.leader_hand = THING_NONE;
    g_leader_hand.icon = HAND_ICON_NONE;
    champ_apply_item_effects((int16_t)g_game.leader, thing, -1, EFFECT_MODE_REMOVE);
    sensor_leave_square(g_game.party_x, g_game.party_y, THING_NONE, true, false);
    return thing;
}

void inv_update_hand_cursor(void)
{
    if (g_game.leader_hand == THING_NONE || !(item_attr(g_game.leader_hand, ATTR_FLAGS) & ITEM_FLAG_ICON_IN_HAND))
        return;
    uint8_t icon = (uint8_t)item_icon_index(g_game.leader_hand, -1, 1);
    if (icon == g_leader_hand.icon)
        return;
    g_hooks.render_hand_icon();
    g_leader_hand.icon = icon;
}

/* The thing in the slot of champ, or of the open chest for slots from SLOT_CONTAINER_FIRST on. */
static uint16_t slot_thing(int champ, int slot)
{
    return slot < SLOT_COUNT ? champion(champ)->slots[slot] : g_action.open_items[slot - SLOT_CONTAINER_FIRST];
}

/* Puts thing into the slot of champ, or of the open chest for slots from SLOT_CONTAINER_FIRST on. */
static void set_slot_thing(int champ, int slot, uint16_t thing)
{
    if (slot < SLOT_COUNT)
        champion(champ)->slots[slot] = thing;
    else
        g_action.open_items[slot - SLOT_CONTAINER_FIRST] = thing;
}

uint16_t champ_remove_object_from_slot(int champ, int16_t slot)
{
    uint16_t thing = slot_thing(champ, slot);
    set_slot_thing(champ, slot, THING_NONE);
    if (thing != THING_NONE) {
        if (champ == g_action.champion - 1 && slot < SLOT_HAND_COUNT && slot == g_action.hand)
            action_area_close();
        champ_apply_item_effects(champ, thing, slot, EFFECT_MODE_REMOVE);
    }
    return thing;
}

void champ_add_object_to_slot(int champ, uint16_t thing, int16_t slot)
{
    if (thing == THING_NONE)
        return;
    thing &= THING_ID_MASK;
    set_slot_thing(champ, slot, thing);
    champ_apply_item_effects(champ, thing, slot, EFFECT_MODE_ADD);
}

void inv_click_slot(int16_t slot)
{
    uint16_t hand = g_game.leader_hand;
    int champ;
    if ((int16_t)g_game.leader == -1)
        return;
    if (slot < CLICK_STATUS_HANDS) {
        champ = slot / SLOT_HAND_COUNT;
        if (g_ui.candidate || champ >= g_game.champion_count || champ + 1 == g_ui.inventory || !champion(champ)->health)
            return;
        slot %= SLOT_HAND_COUNT;
    } else {
        slot -= CLICK_STATUS_HANDS;
        champ = (slot < SLOT_COUNT ? g_ui.inventory : g_action.champion) - 1;
    }
    uint16_t thing = slot_thing(champ, slot);
    if (thing == THING_NONE && hand == THING_NONE)
        return;
    if (hand != THING_NONE && !item_fits_slot(hand, slot, 0))
        return;
    if (hand != THING_NONE)
        champ_take_from_leader_hand();
    if (thing != THING_NONE) {
        champ_remove_object_from_slot(champ, slot);
        champ_put_in_leader_hand(thing, 1);
    }
    if (hand != THING_NONE)
        champ_add_object_to_slot(champ, hand, slot);
    g_hooks.draw_all_champion_states(0);
    if (slot == SLOT_QUIVER_READY || slot == SLOT_QUIVER_FIRST || slot == SLOT_QUIVER_ACTION ||
        (slot > SLOT_QUIVER_FIRST && slot <= SLOT_QUIVER_LAST) || slot >= SLOT_COUNT) {
        g_action.redraw_delay = 1;
        g_hooks.update_action_area(0);
    }
}

void inv_store_leader_hand_object(int champ)
{
    if (g_game.leader_hand == THING_NONE || !champion(champ)->health)
        return;
    int slot = SLOT_POUCH_FIRST;
    while (slot < SLOT_COUNT && champion(champ)->slots[slot] != THING_NONE)
        slot++;
    if (slot == SLOT_COUNT)
        return;
    uint16_t thing = champ_take_from_leader_hand();
    if (thing != THING_NONE)
        champ_add_object_to_slot(champ, thing, slot);
}

void inv_put_in_free_slot(int champ, uint16_t thing)
{
    /* slot ranges in the order of the search, with the thing type that they take (-1 any) */
    static const struct {
        uint8_t first, last;
        int8_t type;
    } ranges[] = {
        {SLOT_BODY_FIRST,  SLOT_BODY_LAST,   -1          },
        {SLOT_BODY_EXTRA,  SLOT_BODY_EXTRA,  -1          },
        {SLOT_READY_HAND,  SLOT_ACTION_HAND, THING_WEAPON},
        {SLOT_READY_HAND,  SLOT_ACTION_HAND, THING_ARMOUR},
        {SLOT_POUCH_FIRST, SLOT_POUCH_LAST,  -1          }
    };
    for (size_t r = 0; r < sizeof ranges / sizeof *ranges; r++)
        for (int slot = ranges[r].first; slot <= ranges[r].last; slot++)
            if (champion(champ)->slots[slot] == THING_NONE && item_fits_slot(thing, slot, 0) &&
                (ranges[r].type < 0 || ranges[r].type == (int)thing_type(thing))) {
                champ_add_object_to_slot(champ, thing, slot);
                return;
            }
}

/* ---- Eating and drinking ---- */

/* A mana potion of gain value adds mana; above the maximum it adds only half. */
static void drink_mana_potion(Champion *c, int16_t value)
{
    int16_t mana = champ_min16(MANA_POTION_MAX, c->mana + value * 2 - MANA_GAIN_LOSS);
    if ((uint16_t)mana > c->max_mana) {
        int16_t limit = c->max_mana > c->mana ? c->max_mana : c->mana;
        mana = (int16_t)((uint16_t)mana - (((uint16_t)mana - limit) >> 1));
    }
    c->mana = mana;
}

/* A health potion of power adds health, and tries to heal the wounds. */
static void drink_health_potion(Champion *c, unsigned power, int16_t divisor)
{
    int16_t tries = (int16_t)(power / HEALTH_TRIES_DIVISOR) > 1 ? (int16_t)(power / HEALTH_TRIES_DIVISOR) : 1;
    c->health = (int16_t)(c->health + c->max_health / (uint16_t)divisor);
    uint16_t wounds = c->wounds;
    for (int left = WOUND_TRIES; wounds && left; left--) {
        for (int k = 0; k < tries; k++)
            c->wounds &= (uint16_t)rand_next();
        tries = 1;
        if (wounds != c->wounds)
            break;
    }
}

/* A potion changes the champion; false for a potion kind without an effect. */
static bool drink_potion(int champ, uint16_t thing)
{
    Champion *c = champion(champ);
    unsigned power = potion_power(thing);
    int16_t divisor = (int16_t)(((DIVISOR_POWER_TOP - (int)power) /
                                 (int)(((power + 1) >> DIVISOR_POWER_SHIFT) + DIVISOR_POWER_BASE)) >>
                                1);
    int16_t value = (int16_t)(power / POTION_GAIN_DIVISOR + POTION_GAIN_BASE);
    switch (potion_type(thing)) {
    case POTION_DEXTERITY:
        adjust_stat(c->stats[STAT_DEXTERITY], value);
        break;
    case POTION_STRENGTH:
        adjust_stat(c->stats[STAT_STRENGTH], (int16_t)(power / STRENGTH_GAIN_DIVISOR + STRENGTH_GAIN_BASE));
        break;
    case POTION_WISDOM:
        adjust_stat(c->stats[STAT_WISDOM], value);
        break;
    case POTION_VITALITY:
        adjust_stat(c->stats[STAT_VITALITY], value);
        break;
    case POTION_ANTIDOTE: {
        unsigned square = (uint16_t)((power / ANTIDOTE_POWER_DIVISOR) * (power / ANTIDOTE_POWER_DIVISOR));
        champ_cure_poison(champ, (int16_t)(value + power + square));
        break;
    }
    case POTION_STAMINA:
        c->stamina =
            (int16_t)(c->stamina + champ_min16(c->max_stamina / (uint16_t)divisor, c->max_stamina - c->stamina));
        break;
    case POTION_DEFENSE: {
        int16_t amount = (int16_t)((uint16_t)value + ((uint16_t)value >> 1));
        magic_add_party_aura((int16_t)(1 << champ), AURA_DEFENSE, amount, (uint16_t)(amount * amount));
        break;
    }
    case POTION_MANA:
        drink_mana_potion(c, value);
        break;
    case POTION_HEALTH:
        drink_health_potion(c, power, divisor);
        break;
    case POTION_WATER:
        c->water = champ_min16(c->water + WATER_PER_POTION, FOOD_MAX);
        break;
    default:
        return false;
    }
    return true;
}

void champ_consume_object(int champ, uint16_t thing, int16_t slot)
{
    if (thing == THING_NONE) {
        if (g_game.leader_hand == THING_NONE) {
            g_ui.mouth = (g_input_mouse_buttons & INPUT_BUTTON_LEFT) != 0; /* held until the left button is released */
            return;
        }
        champ = g_ui.inventory - 1;
        thing = g_game.leader_hand;
        slot = -1;
    }
    if (g_ui.candidate)
        return;
    Champion *c = champion(champ);
    int16_t food = (int16_t)item_attr(thing, ATTR_FOOD);
    if (food) {
        if (slot == -1)
            champ_take_from_leader_hand(); /* the original also animates the mouth */
        c->food = champ_min16(c->food + food, FOOD_MAX);
        champ_apply_item_effects(champ, thing, slot, EFFECT_MODE_CONSUME);
        thing_set_word(thing, THING_NEXT, THING_NONE);
        if (slot != -1)
            c->slots[slot] = THING_NONE;
    } else if (item_use_charge(thing)) {
        c->water = champ_min16(c->water + WATER_PER_DRINK, FOOD_MAX);
        if (slot != -1)
            c->slots[slot] = thing;
    } else {
        if (thing_type(thing) != THING_POTION || !drink_potion(champ, thing))
            return;
        if (slot == -1)
            champ_take_from_leader_hand();
        thing_set_word(thing, THING_NEXT, THING_NONE);
        uint16_t flask = dungeon_unused_thing(THING_POTION);
        if (flask == THING_NONE)
            return;
        item_set_gdat_index(flask, EMPTY_FLASK);
        if (slot == -1)
            champ_put_in_leader_hand(flask, 0);
        else
            c->slots[slot] = flask;
    }
    if (c->stamina > c->max_stamina)
        c->stamina = c->max_stamina;
    if (c->health > c->max_health)
        c->health = c->max_health;
    sndreq_request_with_fallback(
        (SoundCue){CAT_CHAMPION, c->portrait, SOUND_ENTRY_SWALLOW, SOUND_PRIORITY_SWALLOW, SOUND_VOLUME_NORMAL},
        SOUND_FALLBACK, g_game.party_x, g_game.party_y, 0);
    c->redraw |= REDRAW_EATEN;
    if (slot != -1) {
        c->redraw |= REDRAW_STATUS_BOX;
        champ_compute_load(champ);
    }
}
