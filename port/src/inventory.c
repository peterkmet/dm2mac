/* The leader hand, the champion slots, and the effects of the items that the champions hold. */
#include <string.h>
#include "action.h"
#include "champion.h"
#include "draw.h"
#include "dungeon.h"
#include "hooks.h"
#include "inventory.h"
#include "item.h"
#include "random.h"
#include "move.h"
#include "sensor.h"
#include "text.h"
#include "timeline.h"
#include "ui.h"

/* Item attributes of the effects. */
enum {
    ATTR_LIGHT_FLAGS = 0x10,        /* ATTR_FLAGS bit: the item gives light */
    ATTR_HAS_EFFECTS = 0x2000,      /* ATTR_FLAGS bit: the item changes the champion */
    ATTR_MAX_MANA = 0x14, ATTR_STAT_FIRST = 0x15, ATTR_SKILL_FIRST = 0x1e, ATTR_LIGHT = 0x32, ATTR_MOVE_SPEED = 0x33,
    ATTR_EFFECT_TIME = 0x13         /* game steps until the effect of a consumed item ends */
};
#define MODIFIER_CONSUMED 0x4000    /* the modifier counts only when the item is consumed */
#define MODIFIER_ANY_SLOT 0x8000    /* the modifier counts in any slot */
#define MANA_MAX 999
#define STAT_MIN 10
#define STAT_MAX 220
#define STAT_STEP 20                /* a statistic change shrinks by a quarter for each step away from the maximum */
#define MESSAGE_OBJECT_NAME 13      /* the message color of an object name */
#define REDRAW_MANA 0x0800          /* REDRAW_BARS */
#define REDRAW_STATS 0x3000
#define REDRAW_SKILLS 0x2000
#define SOUND_SWALLOW 0x83

LeaderHand leader_hand = {.icon = HAND_ICON_NONE};

static int16_t clamp16(int low, int v, int high) { return (int16_t)(v < low ? low : v > high ? high : v); }

/* The magic light of the party (the first word of game.magic_light). */
static void add_magic_light(int delta)
{
    uint16_t v = (uint16_t)(game.magic_light[0] | game.magic_light[1] << 8);
    v = (uint16_t)(v + delta);
    game.magic_light[0] = (uint8_t)v;
    game.magic_light[1] = (uint8_t)(v >> 8);
}

/* The modifier of attribute attr of thing for mode; equipped: the item is in a slot that fits it. */
static int item_stat_modifier(uint16_t thing, int attr, int equipped, int mode)
{
    uint16_t v = item_attr(thing, attr);
    if (!v) return 0;
    if (!(v & MODIFIER_CONSUMED)) {
        if (!equipped && !(v & MODIFIER_ANY_SLOT)) return 0;
    } else if (mode != EFFECT_MODE_CONSUME_END && mode != EFFECT_MODE_CONSUME && mode != EFFECT_MODE_RESTORE) {
        return 0;
    }
    int r = (int8_t)v;
    return mode < 0 ? -r : r;
}

void champ_adjust_stat(uint8_t *stat, int delta)
{
    int16_t d = (int16_t)delta;
    int16_t diff = (int16_t)(stat[0] + d - stat[1]);
    if ((diff < 0) == (d < 0)) {
        for (int16_t left = (int16_t)(diff < 0 ? -diff : diff); left > STAT_STEP; left = (int16_t)(left - STAT_STEP))
            d = (int16_t)(d - (d >> 2));
    }
    stat[0] = (uint8_t)clamp16(STAT_MIN, (int16_t)(stat[0] + d), STAT_MAX);
}

void champ_compute_load(int champ)
{
    Champion *c = champion(champ);
    uint16_t load = 0;
    for (int s = 0; s < SLOT_COUNT; s++) load = (uint16_t)(load + item_weight(c->slots[s]));
    if (champ == action.champion - 1 && action.hand < SLOT_HAND_COUNT && item_is_chest(action.hand_items[champ][action.hand]))
        for (int i = 0; i < CONTAINER_SLOTS; i++) load = (uint16_t)(load + item_weight(action.open_items[i]));
    c->load = load;
    c->redraw |= REDRAW_LOAD;
}

void champ_apply_item_effects(int champ, uint16_t thing, int slot, int mode)
{
    if ((int16_t)champ < 0 || thing == THING_NONE) return;
    if ((int16_t)slot < SLOT_COUNT) {
        uint16_t flags = item_attr(thing, ATTR_FLAGS);
        if (mode && flags & ATTR_LIGHT_FLAGS) light_update_party_light();
        if (flags & ATTR_HAS_EFFECTS) {
            Champion *c = champion(champ);
            int equipped = item_fits_slot(thing, slot, 1) != 0, timed = 0, m;
            if (mode != EFFECT_MODE_LOAD && mode != EFFECT_MODE_RESTORE && mode != EFFECT_MODE_CONSUME_END) {
                if ((m = item_stat_modifier(thing, ATTR_MAX_MANA, equipped, mode))) {
                    if (mode == EFFECT_MODE_ADD || mode == EFFECT_MODE_REMOVE) c->max_mana = (int16_t)(c->max_mana + m);
                    else if (mode == EFFECT_MODE_CONSUME) c->mana = clamp16(0, (int16_t)(m + c->mana), MANA_MAX);
                    c->redraw |= REDRAW_MANA;
                }
                for (int s = 0; s < STAT_COUNT; s++) {
                    if (!(m = item_stat_modifier(thing, ATTR_STAT_FIRST + s, equipped, mode))) continue;
                    if (mode == EFFECT_MODE_ADD || mode == EFFECT_MODE_REMOVE) c->stat_bonus[s] = (int8_t)(c->stat_bonus[s] + m);
                    else champ_adjust_stat(c->stats[s], m);
                    c->redraw |= REDRAW_STATS;
                }
            }
            for (int s = 0; s < SKILL_COUNT; s++) {
                if (!(m = item_stat_modifier(thing, ATTR_SKILL_FIRST + s, equipped, mode))) continue;
                c->skill_bonus[s] = (int8_t)(c->skill_bonus[s] + m);
                c->redraw |= REDRAW_SKILLS;
                timed = 1;
            }
            if ((m = item_stat_modifier(thing, ATTR_MOVE_SPEED, equipped, mode))) {
                c->move_speed = (uint16_t)((c->move_speed & 0xff00) | (uint8_t)(c->move_speed + m));
                timed = 1;
            }
            if (mode != EFFECT_MODE_CONSUME && mode != EFFECT_MODE_CONSUME_END && mode != EFFECT_MODE_RESTORE
                && (m = item_stat_modifier(thing, ATTR_LIGHT, equipped, mode))) {
                add_magic_light(m);
                if (mode) light_update_party_light();
            }
            if (timed && mode == EFFECT_MODE_CONSUME) {
                /* fix: the original leaves the last 3 data bytes uninitialized */
                TimelineEvent e = {.time_map = (game.game_time + item_attr(thing, ATTR_EFFECT_TIME))
                                               | (uint32_t)(uint8_t)game.party_map << 24,
                                   .type = EVENT_ITEM_EFFECT, .priority = (uint8_t)champ};
                e.data[0] = (uint8_t)thing_type(thing);
                e.data[2] = item_gdat_index(thing);
                timeline_add_event(&e);
            }
        }
    }
    if (mode) champ_compute_load(champ);
}

/* Draws the icon of the leader hand object into the cursor: its 4 bpp color numbers and its palette colors. */
static void render_hand_icon(void)
{
    static const uint8_t identity[HAND_ICON_COLORS] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    uint16_t t = game.leader_hand;
    int category = item_gdat_category(t), index = item_gdat_index(t), entry = item_icon_index(t, -1, 1);
    const Bitmap *image = draw_gdat_image(category, index, entry);
    const uint8_t *colors = draw_gdat_image_palette(category, index, entry);
    Bitmap cursor = {leader_hand.cursor, HAND_ICON_SIZE, HAND_ICON_SIZE, 8};
    memset(leader_hand.cursor, HAND_ICON_KEY, sizeof leader_hand.cursor);
    const int16_t r[RECT_SIZE] = {0, 0, HAND_ICON_SIZE, HAND_ICON_SIZE};
    if (image && colors) {
        gfx_blit_bitmap(image, &cursor, r, 0, 0, UI_OPAQUE, identity);
        memcpy(leader_hand.palette, colors, sizeof leader_hand.palette);
    }
    draw_free_temp();
}

void ui_show_object_name(uint16_t thing)
{
    text_print_newline();
    text_print_message(MESSAGE_OBJECT_NAME, item_name(thing));
}

void champ_put_object_in_leader_hand(uint16_t thing, int defer_cursor)
{
    if (thing == THING_NONE) return;
    thing &= THING_INDEX_MASK | THING_TYPE_MASK << THING_TYPE_SHIFT;
    leader_hand.attr = item_attr(thing, ATTR_FLAGS);
    leader_hand.weight = (uint16_t)item_weight(thing);
    game.leader_hand = thing;
    render_hand_icon();
    ui_show_object_name(thing);
    (void)defer_cursor;  /* the port draws the cursor each frame (inv_cursor_object) */
    champ_apply_item_effects((int16_t)game.leader, thing, -1, EFFECT_MODE_ADD);
    sensor_process_floor(game.party_x, game.party_y, THING_NONE, 1, 1, 0);
}

uint16_t champ_remove_object_from_leader_hand(void)
{
    uint16_t thing = game.leader_hand;
    if (thing == THING_NONE) return thing;
    leader_hand.attr = 0;
    leader_hand.weight = 0;
    game.leader_hand = THING_NONE;
    leader_hand.icon = HAND_ICON_NONE;
    champ_apply_item_effects((int16_t)game.leader, thing, -1, EFFECT_MODE_REMOVE);
    sensor_process_floor(game.party_x, game.party_y, THING_NONE, 1, 0, 0);
    return thing;
}

void inv_update_hand_cursor(void)
{
    if (game.leader_hand == THING_NONE || !(item_attr(game.leader_hand, ATTR_FLAGS) & ITEM_FLAG_ICON_IN_HAND)) return;
    uint8_t icon = (uint8_t)item_icon_index(game.leader_hand, -1, 1);
    if (icon == leader_hand.icon) return;
    render_hand_icon();
    leader_hand.icon = icon;
}

uint16_t inv_cursor_object(void) { return game.leader_hand; }

/* The slot of champ, or of the open chest for slots from SLOT_CONTAINER_FIRST on. */
static uint16_t *slot_of(int champ, int slot)
{
    return slot < SLOT_COUNT ? &champion(champ)->slots[slot] : &action.open_items[slot - SLOT_CONTAINER_FIRST];
}

uint16_t champ_remove_object_from_slot(int champ, int slot)
{
    uint16_t *at = slot_of(champ, slot), thing = *at;
    *at = THING_NONE;
    if (thing != THING_NONE) {
        if (champ == action.champion - 1 && slot < SLOT_HAND_COUNT && slot == action.hand) ui_action_area_close();
        champ_apply_item_effects(champ, thing, slot, EFFECT_MODE_REMOVE);
    }
    return thing;
}

void champ_add_object_to_slot(int champ, uint16_t thing, int slot)
{
    if (thing == THING_NONE) return;
    thing &= THING_INDEX_MASK | THING_TYPE_MASK << THING_TYPE_SHIFT;
    *slot_of(champ, slot) = thing;
    champ_apply_item_effects(champ, thing, slot, EFFECT_MODE_ADD);
}

#define CLICK_STATUS_HANDS 8        /* inv_click_slot slots 0 to 7: the hands of the status boxes */

void inv_click_slot(int slot)
{
    uint16_t hand = game.leader_hand;
    int champ;
    if ((int16_t)game.leader == -1) return;
    if (slot < CLICK_STATUS_HANDS) {
        champ = slot >> 1;
        if (ui.candidate || champ >= game.champion_count || champ + 1 == ui.inventory || !champion(champ)->health) return;
        slot &= 1;
    } else {
        slot -= CLICK_STATUS_HANDS;
        champ = (slot < SLOT_COUNT ? ui.inventory : action.champion) - 1;
    }
    uint16_t thing = *slot_of(champ, slot);
    if (thing == THING_NONE && hand == THING_NONE) return;
    if (hand != THING_NONE && !item_fits_slot(hand, slot, 0)) return;
    if (hand != THING_NONE) champ_remove_object_from_leader_hand();
    if (thing != THING_NONE) {
        champ_remove_object_from_slot(champ, slot);
        champ_put_object_in_leader_hand(thing, 1);
    }
    if (hand != THING_NONE) champ_add_object_to_slot(champ, hand, slot);
    champ_draw_all_states(0);
    if (slot == SLOT_QUIVER_READY || slot == SLOT_QUIVER_FIRST || slot == SLOT_QUIVER_ACTION
        || (slot > SLOT_QUIVER_FIRST && slot <= SLOT_QUIVER_LAST) || slot >= SLOT_COUNT) {
        action.redraw_delay = 1;
        ui_update_action_area(0);
    }
}

void inv_store_leader_hand_object(int champ)
{
    if (game.leader_hand == THING_NONE || !champion(champ)->health) return;
    int slot = SLOT_POUCH_FIRST;
    while (slot < SLOT_COUNT && champion(champ)->slots[slot] != THING_NONE) slot++;
    if (slot == SLOT_COUNT) return;
    uint16_t thing = champ_remove_object_from_leader_hand();
    if (thing != THING_NONE) champ_add_object_to_slot(champ, thing, slot);
}

void inv_put_object_in_free_slot(int champ, uint16_t thing)
{
    /* slot ranges in the order of the search, with the thing type that they take (-1 any) */
    static const struct { uint8_t first, last; int8_t type; } ranges[] = {
        {2, 5, -1}, {10, 10, -1}, {0, 1, THING_WEAPON}, {0, 1, THING_ARMOUR}, {13, 29, -1}
    };
    for (size_t r = 0; r < sizeof ranges / sizeof *ranges; r++)
        for (int slot = ranges[r].first; slot <= ranges[r].last; slot++)
            if (champion(champ)->slots[slot] == THING_NONE && item_fits_slot(thing, slot, 0)
                && (ranges[r].type < 0 || ranges[r].type == (int)thing_type(thing))) {
                champ_add_object_to_slot(champ, thing, slot);
                return;
            }
}

void timeline_event_apply_item_effect(const TimelineEvent *e, int mode)
{
    /* the effect ends: a record of the item type stands in for the eaten item, with its kind */
    uint16_t thing = (uint16_t)((e->data[0] | e->data[1] << 8) << THING_TYPE_SHIFT);
    uint8_t *record = dungeon_thing_record(thing), saved[DUNGEON_RECORD_MAX];
    int size = dungeon_thing_size(thing_type(thing));
    memcpy(saved, record, size);
    item_set_gdat_index(thing, e->data[2]);
    champ_apply_item_effects(e->priority, thing, -1, mode);
    memcpy(record, saved, size);
}

/* ---- Eating and drinking ---- */

#define ATTR_FOOD 3                 /* item attribute: the food value */
#define ATTR_REFILLS 0x0001         /* ATTR_FLAGS bit: the item holds water */
#define FOOD_MAX 0x800
#define WATER_PER_DRINK 800
#define WATER_PER_POTION 1600
#define MANA_POTION_MAX 900
#define POTION_POWER_MASK 0xff
#define POTION_TYPE_SHIFT 8
#define POTION_TYPE_MASK 0x7f
#define EMPTY_FLASK 0x14            /* the potion kind of an empty flask */
#define WOUND_TRIES 10
#define REDRAW_EATEN 0x3800
enum {
    POTION_DEXTERITY = 6, POTION_STRENGTH, POTION_WISDOM, POTION_VITALITY, POTION_ANTIDOTE, POTION_STAMINA,
    POTION_DEFENSE, POTION_MANA, POTION_HEALTH, POTION_WATER
};
#define AURA_DEFENSE 2

static int16_t min16(int a, int b) { return (int16_t)a < (int16_t)b ? (int16_t)a : (int16_t)b; }

int item_use_charge(uint16_t thing)
{
    if (!(item_attr(thing, ATTR_FLAGS) & ATTR_REFILLS) || !item_charges(thing)) return 0;
    item_add_charges(thing, -1);
    if (thing == game.leader_hand) champ_put_object_in_leader_hand(thing, 0);
    return 1;
}

/* A potion changes the champion; returns 0 for a potion kind without an effect. */
static int drink_potion(int champ, uint16_t thing)
{
    Champion *c = champion(champ);
    uint16_t w = thing_word(thing, 2);
    unsigned power = w & POTION_POWER_MASK;
    int16_t divisor = (int16_t)(((0x1ff - (int)power) / (int)(((power + 1) >> 3) + 0x20)) >> 1);
    int16_t value = (int16_t)(power / 25 + 8);
    switch (w >> POTION_TYPE_SHIFT & POTION_TYPE_MASK) {
    case POTION_DEXTERITY: champ_adjust_stat(c->stats[STAT_DEXTERITY], value); break;
    case POTION_STRENGTH: champ_adjust_stat(c->stats[STAT_STRENGTH], (int16_t)(power / 35 + 5)); break;
    case POTION_WISDOM: champ_adjust_stat(c->stats[STAT_WISDOM], value); break;
    case POTION_VITALITY: champ_adjust_stat(c->stats[STAT_VITALITY], value); break;
    case POTION_ANTIDOTE: {
        unsigned square = (power / 7) * (power / 7) & 0xffff;
        champ_cure_poison(champ, (int16_t)(value + power + square));
        break;
    }
    case POTION_STAMINA:
        c->stamina = (int16_t)(c->stamina + min16(c->max_stamina / (uint16_t)divisor, c->max_stamina - c->stamina));
        break;
    case POTION_DEFENSE: {
        int16_t amount = (int16_t)((uint16_t)value + ((uint16_t)value >> 1));
        magic_add_party_aura((int16_t)(1 << champ), AURA_DEFENSE, amount, (uint16_t)(amount * amount));
        break;
    }
    case POTION_MANA: {
        int16_t mana = min16(MANA_POTION_MAX, c->mana + value * 2 - 8);
        if ((uint16_t)mana > c->max_mana) {
            int16_t limit = c->max_mana > c->mana ? c->max_mana : c->mana;
            mana = (int16_t)((uint16_t)mana - (((uint16_t)mana - limit) >> 1));
        }
        c->mana = mana;
        break;
    }
    case POTION_HEALTH: {
        int16_t tries = (int16_t)(power / 42) > 1 ? (int16_t)(power / 42) : 1;
        c->health = (int16_t)(c->health + c->max_health / (uint16_t)divisor);
        uint16_t wounds = c->wounds;
        for (int left = WOUND_TRIES; wounds && left; left--) {
            for (int k = 0; k < tries; k++) c->wounds &= (uint16_t)rand_next();
            tries = 1;
            if (wounds != c->wounds) break;
        }
        break;
    }
    case POTION_WATER: c->water = min16(c->water + WATER_PER_POTION, FOOD_MAX); break;
    default: return 0;
    }
    return 1;
}

void champ_consume_object(int champ, uint16_t thing, int slot)
{
    if (thing == THING_NONE) {
        /* ponytail: the mouth held with an empty hand needs the mouse capture of the original; nothing here */
        if (game.leader_hand == THING_NONE) return;
        champ = ui.inventory - 1;
        thing = game.leader_hand;
        slot = -1;
    }
    if (ui.candidate) return;
    Champion *c = champion(champ);
    int16_t food = (int16_t)item_attr(thing, ATTR_FOOD);
    if (food) {
        if (slot == -1) champ_remove_object_from_leader_hand();  /* the original also animates the mouth */
        c->food = min16(c->food + food, FOOD_MAX);
        champ_apply_item_effects(champ, thing, slot, EFFECT_MODE_CONSUME);
        set_thing_word(thing, THING_NEXT, THING_NONE);
        if (slot != -1) c->slots[slot] = THING_NONE;
    } else if (item_use_charge(thing)) {
        c->water = min16(c->water + WATER_PER_DRINK, FOOD_MAX);
        if (slot != -1) c->slots[slot] = thing;
    } else {
        if (thing_type(thing) != THING_POTION || !drink_potion(champ, thing)) return;
        if (slot == -1) champ_remove_object_from_leader_hand();
        set_thing_word(thing, THING_NEXT, THING_NONE);
        uint16_t flask = dungeon_unused_thing(THING_POTION);
        if (flask == THING_NONE) return;
        item_set_gdat_index(flask, EMPTY_FLASK);
        if (slot == -1) champ_put_object_in_leader_hand(flask, 0);
        else c->slots[slot] = flask;
    }
    if (c->stamina > c->max_stamina) c->stamina = c->max_stamina;
    if (c->health > c->max_health) c->health = c->max_health;
    sound_play_with_fallback(GDAT_CHAMPION_CATEGORY, c->portrait, SOUND_SWALLOW, 0xfe, game.party_x, game.party_y, 0, 0x96, 0x80);
    c->redraw |= REDRAW_EATEN;
    if (slot != -1) {
        c->redraw |= REDRAW_STATUS_BOX;
        champ_compute_load(champ);
    }
}
