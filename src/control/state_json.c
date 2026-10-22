/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "state_json.h"
#include "gdat.h"
#include "ids.h"
#include "os.h"
#include "action.h"
#include "aspect.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "game.h"
#include "game_text.h"
#include "item.h"
#include "magic.h"
#include "minion.h"
#include "money.h"
#include "move.h"
#include "projectile.h"
#include "random.h"
#include "save.h"
#include "seen.h"
#include "sensor.h"
#include "weather.h"
#include "dialog.h"
#include "input.h"
#include "minion_map.h"
#include "text.h"
#include "ui.h"
#include "view.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSON_START_SIZE  1024                                        /* the first size of the text buffer */
#define LONG_TEXT_SIZE   24                                          /* the digits and sign of a long */
#define ESCAPE_SIZE      8                                           /* a \u00XX escape and its end mark */
#define ESCAPE_LENGTH    6                                           /* the length of a \u00XX escape */
#define RUNE_NAME_COUNT  (CHAMPION_RUNES_SIZE * SPELL_RUNES_PER_SET) /* the power runes and three rune sets */
#define RUNE_NAME_MAX    4                                           /* the letters of the longest rune name */
#define SCROLL_TEXT_SIZE (TEXT_SIZE * 2) /* a decoded scroll text, as the scroll panel holds it */
#define MAP_GRID_PREFIX  3               /* the "yy " in front of each grid row */
#define PILE_ALCOVE      CELL_COUNT      /* the cell number of the alcove pile, after the 4 floor cells */

/* Appends n bytes; the buffer doubles when they do not fit. */
static void put(Json *j, const char *text, size_t n)
{
    if (j->len + n + 1 > j->cap) {
        size_t cap = j->cap ? j->cap : JSON_START_SIZE;
        while (cap < j->len + n + 1)
            cap *= 2;
        char *s = realloc(j->s, cap);
        if (!s)
            os_fatal("out of memory");
        j->s = s;
        j->cap = cap;
    }
    memcpy(j->s + j->len, text, n);
    j->len += n;
    j->s[j->len] = 0;
}

/* A comma when a value comes before this one. */
static void separate(Json *j)
{
    if (j->len && !strchr("{[:", j->s[j->len - 1]))
        put(j, ",", 1);
}

void json_raw(Json *j, const char *text)
{
    put(j, text, strlen(text));
}
void json_rawn(Json *j, const char *text, size_t n)
{
    put(j, text, n);
}
void json_open(Json *j, char bracket)
{
    separate(j);
    put(j, &bracket, 1);
}
void json_close(Json *j, char bracket)
{
    put(j, &bracket, 1);
}
void json_null(Json *j)
{
    separate(j);
    json_raw(j, "null");
}
void json_bool(Json *j, int value)
{
    separate(j);
    json_raw(j, value ? "true" : "false");
}

void json_int(Json *j, long value)
{
    char buf[LONG_TEXT_SIZE];
    separate(j);
    snprintf(buf, sizeof buf, "%ld", value);
    json_raw(j, buf);
}

/* A JSON string of the first n bytes of text (up to its end mark); bytes from 0x80 go as they are (UTF-8). */
void json_strn(Json *j, const char *text, size_t n)
{
    char buf[ESCAPE_SIZE];
    separate(j);
    put(j, "\"", 1);
    for (size_t i = 0; i < n && text[i]; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\') {
            buf[0] = '\\';
            buf[1] = (char)c;
            put(j, buf, 2);
        } else if (c == '\n') {
            put(j, "\\n", 2);
        } else if (c < ' ') {
            snprintf(buf, sizeof buf, "\\u%04x", c);
            put(j, buf, ESCAPE_LENGTH);
        } else {
            put(j, text + i, 1);
        }
    }
    put(j, "\"", 1);
}

void json_str(Json *j, const char *text)
{
    json_strn(j, text, strlen(text));
}

void json_key(Json *j, const char *key)
{
    json_str(j, key);
    put(j, ":", 1);
}

/* ---- The game state ---- */

/* The input modes by number (the roots of the input tree in input.c). */
static const char *const mode_names[] = {
    [INPUT_MODE_MENU] = "menu",   [INPUT_MODE_PICTURE] = "picture", [INPUT_MODE_SLEEP] = "sleep",
    [INPUT_MODE_PAUSE] = "pause", [INPUT_MODE_DIALOG] = "dialog",   [INPUT_MODE_MOVE] = "move",
    [INPUT_MODE_PUSH] = "push",   [INPUT_MODE_RECRUIT] = "recruit", [INPUT_MODE_INVENTORY] = "inventory",
    [INPUT_MODE_NAME] = "name",
};
#define MODE_COUNT ((int)(sizeof mode_names / sizeof mode_names[0]))
static const char *const direction_names[] = {"north", "east", "south", "west"};
/* The places of the champions in the party, seen in the party direction. */
static const char *const party_cell_names[] = {"front_left", "front_right", "back_right", "back_left"};
/* The statistics that the skills panel shows (luck is not shown), from STAT_STRENGTH on. */
static const char *const statistic_names[STAT_COUNT] = {
    [STAT_STRENGTH] = "strength", [STAT_DEXTERITY] = "dexterity",  [STAT_WISDOM] = "wisdom",
    [STAT_VITALITY] = "vitality", [STAT_ANTI_MAGIC] = "antimagic", [STAT_ANTI_FIRE] = "antifire",
};
static const char *const skill_names[SKILL_BASE_COUNT] = {"fighter", "ninja", "priest", "wizard"};
/* The wound bits of Champion.wounds: the hands, the head, the torso, the legs, the feet. */
static const char *const wound_names[] = {"ready_hand", "action_hand", "head", "torso", "legs", "feet"};
/* The champion slots by their place in the inventory picture: each hand loads from its quiver slots (the ready hand
   from 11 then 6, the action hand from 12 then 7 to 9); slot 13 and the two rows 14 to 29 are the backpack. */
static const char *const slot_names[SLOT_COUNT] = {
    "ready_hand",
    "action_hand",
    "head",
    "torso",
    "legs",
    "feet",
    "ready_quiver_2",
    "action_quiver_2",
    "action_quiver_3",
    "action_quiver_4",
    "neck",
    "ready_quiver_1",
    "action_quiver_1",
    "backpack_1",
    "backpack_2",
    "backpack_3",
    "backpack_4",
    "backpack_5",
    "backpack_6",
    "backpack_7",
    "backpack_8",
    "backpack_9",
    "backpack_10",
    "backpack_11",
    "backpack_12",
    "backpack_13",
    "backpack_14",
    "backpack_15",
    "backpack_16",
    "backpack_17",
};
/* The rune names by rune glyph - SPELL_RUNE_GLYPH_FIRST: the power runes, then the three rune sets. */
static const char *const rune_names[RUNE_NAME_COUNT] = {
    "lo",  "um", "on",   "ee", "pal", "mon", "ya", "vi",  "oh",   "ful",  "des", "zo",
    "ven", "ew", "kath", "ir", "bro", "gor", "ku", "ros", "dain", "neta", "ra",  "sar",
};
static const char *const hand_names[SLOT_HAND_COUNT] = {"ready", "action"};
static const char *const map_option_names[MAP_OPTION_COUNT] = {"creatures", "details", "items", "hold"};
/* The amounts of rain and clouds that the sky shows. */
static const char *const amount_names[] = {"none", "light", "medium", "heavy"};

const char *state_json_mode_name(int mode)
{
    return mode >= 0 && mode < MODE_COUNT && mode_names[mode] ? mode_names[mode] : "unknown";
}

bool state_json_game_over(void)
{
    return g_main_loop.end || g_session.completed;
}

static void key_int(Json *j, const char *key, long value)
{
    json_key(j, key);
    json_int(j, value);
}
static void key_str(Json *j, const char *key, const char *text)
{
    json_key(j, key);
    json_str(j, text);
}
static void key_bool(Json *j, const char *key, int value)
{
    json_key(j, key);
    json_bool(j, value);
}

/* A string of the first n bytes of text without its trailing spaces (GDAT names end with a space). */
static void trimmed(Json *j, const char *text, size_t n)
{
    n = strnlen(text, n);
    while (n && text[n - 1] == ' ')
        n--;
    json_strn(j, text, n);
}

static void pair(Json *j, const char *key, long a, long b)
{
    json_key(j, key);
    json_open(j, '[');
    json_int(j, a);
    json_int(j, b);
    json_close(j, ']');
}

/* A weight in tenths of a kilogram as a decimal number, as the load line shows it. */
static void tenths(Json *j, long value)
{
    char buf[LONG_TEXT_SIZE];
    separate(j);
    snprintf(buf, sizeof buf, "%ld.%ld", value / WEIGHT_TENTHS, value % WEIGHT_TENTHS);
    json_raw(j, buf);
}

static const char *object_kind(uint16_t thing)
{
    switch (thing_type(thing)) {
    case THING_WEAPON:
        return "weapon";
    case THING_ARMOUR:
        return "armour";
    case THING_SCROLL:
        return "scroll";
    case THING_POTION:
        return "potion";
    case THING_CONTAINER:
        return "container";
    }
    return "misc";
}

/* An object as {name, kind}; a scroll also gives its text. THING_NONE gives null. */
static void object_json(Json *j, uint16_t thing)
{
    if (thing == THING_NONE) {
        json_null(j);
        return;
    }
    json_open(j, '{');
    json_key(j, "name");
    trimmed(j, item_name(thing), TEXT_SIZE);
    key_str(j, "kind", object_kind(thing));
    if (thing_type(thing) == THING_SCROLL) {
        char text[SCROLL_TEXT_SIZE];
        key_str(j, "text", text_decode_dungeon(text, sizeof text, thing, SCROLL_TEXT_MODE));
    }
    json_close(j, '}');
}

/* The names of the runes that champion c entered. */
static void runes_json(Json *j, const Champion *c)
{
    json_open(j, '[');
    for (int k = 0; k < c->rune_count && k < CHAMPION_RUNES_SIZE; k++) {
        int rune = (uint8_t)c->runes[k] - SPELL_RUNE_GLYPH_FIRST;
        if (rune >= 0 && rune < RUNE_NAME_COUNT)
            json_str(j, rune_names[rune]);
    }
    json_close(j, ']');
}

/* A skill rank text with the power rune glyphs of the master ranks as their names ("LO MASTER FIGHTER"). */
static void rank_json(Json *j, const char *text)
{
    char out[TEXT_SIZE * 2];
    size_t n = 0;
    for (; *text && n + RUNE_NAME_MAX < sizeof out; text++) {
        int rune = (uint8_t)*text - SPELL_RUNE_GLYPH_FIRST;
        if (rune < 0 || rune >= SPELL_RUNES_PER_SET) {
            out[n++] = *text;
            continue;
        }
        for (const char *name = rune_names[rune]; *name; name++)
            out[n++] = (char)toupper((unsigned char)*name);
    }
    out[n] = 0;
    json_str(j, out);
}

/* The level and the rank text of each base skill; the rank is null below level 2, as the skills panel shows it. */
static void skills_json(Json *j, int champ)
{
    json_open(j, '{');
    for (int s = 0; s < SKILL_BASE_COUNT; s++) {
        int level = champ_get_skill_level(champ, s, 1);
        char text[TEXT_SIZE];
        json_key(j, skill_names[s]);
        json_open(j, '{');
        key_int(j, "level", level);
        json_key(j, "rank");
        if (level < 2)
            json_null(j);
        else
            rank_json(j, ui_skill_rank_text(champ, s, text));
        json_close(j, '}');
    }
    json_close(j, '}');
}

/* The statistics as the skills panel shows them: [current, maximum]. */
static void statistics_json(Json *j, Champion *c)
{
    json_open(j, '{');
    for (int s = STAT_STRENGTH; s < STAT_COUNT; s++)
        pair(j, statistic_names[s], (int16_t)champ_get_stat(c, s, 0), (uint16_t)champ_get_stat(c, s, 1));
    json_close(j, '}');
}

/* The filled champion slots: slot number, place, and object. */
static void slots_json(Json *j, const Champion *c)
{
    json_open(j, '[');
    for (int s = 0; s < SLOT_COUNT; s++) {
        if (c->slots[s] == THING_NONE)
            continue;
        json_open(j, '{');
        key_int(j, "slot", s);
        key_str(j, "where", slot_names[s]);
        json_key(j, "object");
        object_json(j, c->slots[s]);
        json_close(j, '}');
    }
    json_close(j, ']');
}

/* The action names of the open action menu (g_champ_actions). */
static void action_names_json(Json *j)
{
    json_open(j, '[');
    for (int i = 0; i < g_champ_action_count; i++) {
        const ChampionAction *a = &g_champ_actions[i];
        const char *label = text_gdat_label(a->category, a->index, a->entry);
        trimmed(j, label ? label : "", TEXT_SIZE);
    }
    json_close(j, ']');
}

/* The action menu that each hand of a living champion opens; the open action menu stays as it is. */
static void hands_json(Json *j, int champ)
{
    ChampionAction saved[CHAMPION_ACTION_MAX];
    int saved_count = g_champ_action_count;
    uint16_t saved_thing = g_champ_action_thing;
    memcpy(saved, g_champ_actions, sizeof saved);
    json_open(j, '{');
    for (int hand = SLOT_READY_HAND; hand < SLOT_HAND_COUNT; hand++) {
        g_champ_action_count = 0;
        if (champion(champ)->health)
            champ_list_actions(champ, champion(champ)->slots[hand], hand);
        json_key(j, hand_names[hand]);
        action_names_json(j);
    }
    json_close(j, '}');
    memcpy(g_champ_actions, saved, sizeof saved);
    g_champ_action_count = saved_count;
    g_champ_action_thing = saved_thing;
}

int state_json_hand_action_count(int champ, int hand)
{
    ChampionAction saved[CHAMPION_ACTION_MAX];
    int saved_count = g_champ_action_count, count = 0;
    uint16_t saved_thing = g_champ_action_thing;
    memcpy(saved, g_champ_actions, sizeof saved);
    g_champ_action_count = 0;
    if (champion(champ)->health)
        champ_list_actions(champ, champion(champ)->slots[hand], hand);
    count = g_champ_action_count;
    memcpy(g_champ_actions, saved, sizeof saved);
    g_champ_action_count = saved_count;
    g_champ_action_thing = saved_thing;
    return count;
}

/* The bars, food, water, load, and place of a champion. */
static void champion_values_json(Json *j, int champ)
{
    Champion *c = champion(champ);
    key_bool(j, "alive", c->health > 0);
    pair(j, "health", c->health, c->max_health);
    pair(j, "stamina", c->stamina / STAMINA_DISPLAY_UNIT, c->max_stamina / STAMINA_DISPLAY_UNIT);
    pair(j, "mana", c->mana, c->max_mana);
    key_int(j, "food", c->food);
    key_int(j, "water", c->water);
    json_key(j, "load_kg");
    json_open(j, '[');
    tenths(j, champ_get_load(champ));
    tenths(j, champ_get_max_load(c));
    json_close(j, ']');
    key_str(j, "position", party_cell_names[dir_relative(c->cell, g_game.party_dir)]);
    key_bool(j, "poisoned", c->poisoned != 0);
}

/* A champion: name, title, values, statistics, skills, wounds, slots, hand menus, runes, busy hands. */
static void champion_json(Json *j, int champ)
{
    Champion *c = champion(champ);
    json_open(j, '{');
    key_int(j, "index", champ);
    json_key(j, "name");
    trimmed(j, c->name, CHAMPION_NAME_SIZE);
    json_key(j, "title");
    trimmed(j, c->title, CHAMPION_TITLE_SIZE);
    champion_values_json(j, champ);
    json_key(j, "statistics");
    statistics_json(j, c);
    json_key(j, "skills");
    skills_json(j, champ);
    json_key(j, "wounds");
    json_open(j, '[');
    for (size_t w = 0; w < sizeof wound_names / sizeof wound_names[0]; w++)
        if (c->wounds & 1u << w)
            json_str(j, wound_names[w]);
    json_close(j, ']');
    json_key(j, "slots");
    slots_json(j, c);
    json_key(j, "hands");
    hands_json(j, champ);
    json_key(j, "runes");
    runes_json(j, c);
    json_key(j, "busy");
    json_open(j, '[');
    for (int k = 0; k < ACTION_DELAY_COUNT; k++)
        json_bool(j, c->action_delay[k] != 0);
    json_close(j, ']');
    json_close(j, '}');
}

/* The open dialog and its button texts. The slot dialogs do not set g_dialog.id: they give the id of the last
   dialog (docs/skull-formats.md). */
static void dialog_json(Json *j, int mode)
{
    if (mode != INPUT_MODE_DIALOG) {
        json_null(j);
        return;
    }
    json_open(j, '{');
    key_int(j, "id", g_dialog.id);
    json_key(j, "buttons");
    json_open(j, '[');
    for (int e = 0; e < DIALOG_TEXT_COUNT; e++) {
        char text[TEXT_SIZE];
        if (*text_gdat(CAT_DIALOG, g_dialog.id, e, text))
            json_str(j, text);
    }
    json_close(j, ']');
    json_close(j, '}');
}

/* The party: square, direction, darkness, and whether it can move, sleeps, or died. */
static void party_json(Json *j, int mode)
{
    json_open(j, '{');
    key_int(j, "map", g_game.party_map);
    key_int(j, "level", dungeon_map_level(g_game.party_map));
    key_int(j, "x", g_game.party_x);
    key_int(j, "y", g_game.party_y);
    key_str(j, "facing", direction_names[g_game.party_dir & DIR_MASK]);
    key_int(j, "darkness", g_view_light_level);
    key_bool(j, "can_move",
             !g_game.party_move_cooldown && !g_party.move_steps && !input_is_waiting() &&
                 (mode == INPUT_MODE_MOVE || mode == INPUT_MODE_PUSH));
    key_bool(j, "sleeping", g_party_sleeping);
    key_bool(j, "dead", g_main_loop.end != 0);
    json_close(j, '}');
}

/* The leader, the leader hand, the shown candidate (the last champion record), and the members of the party. */
static void champions_json(Json *j)
{
    int members = g_game.champion_count, candidate = -1;
    if (g_ui.candidate && members)
        candidate = --members;
    json_key(j, "leader");
    if ((int16_t)g_game.leader == -1 || (int16_t)g_game.leader == candidate)
        json_null(j);
    else
        json_int(j, (int16_t)g_game.leader);
    json_key(j, "leader_hand");
    object_json(j, g_game.champion_count ? g_game.leader_hand : THING_NONE);
    json_key(j, "candidate");
    if (candidate >= 0)
        champion_json(j, candidate);
    else
        json_null(j);
    json_key(j, "champions");
    json_open(j, '[');
    for (int i = 0; i < members && i < CHAMPION_MAX; i++)
        champion_json(j, i);
    json_close(j, ']');
}

/* The open action menu: whose hand and its actions. */
static void action_menu_json(Json *j)
{
    if (g_action.mode != ACTION_MODE_MENU || g_action.champion <= 0 || g_action.hand >= SLOT_HAND_COUNT) {
        json_null(j);
        return;
    }
    json_open(j, '{');
    key_int(j, "champion", g_action.champion - 1);
    key_str(j, "hand", hand_names[g_action.hand]);
    json_key(j, "actions");
    action_names_json(j);
    json_close(j, '}');
}

/* The open spell panel: the caster, the entered runes, and the runes of the next set with their mana cost. */
static void spell_json(Json *j)
{
    if (g_action.mode != ACTION_MODE_SPELL || g_action.champion <= 0) {
        json_null(j);
        return;
    }
    int caster = g_action.champion - 1;
    const Champion *c = champion(caster);
    json_open(j, '{');
    key_int(j, "caster", caster);
    json_key(j, "chosen");
    runes_json(j, c);
    json_key(j, "available");
    json_open(j, '[');
    for (int r = 0; c->rune_count < CHAMPION_RUNES_SIZE && r < SPELL_RUNES_PER_SET; r++) {
        json_open(j, '{');
        key_int(j, "rune", r);
        key_str(j, "name", rune_names[c->rune_count * SPELL_RUNES_PER_SET + r]);
        key_int(j, "mana", magic_rune_mana_cost(caster, r));
        json_close(j, '}');
    }
    json_close(j, ']');
    json_close(j, '}');
}

static const char *panel_name(void)
{
    switch (g_ui.panel_mode) {
    case PANEL_MODE_FOOD:
        return "food";
    case PANEL_MODE_SKILLS:
        return "skills";
    case PANEL_MODE_OBJECT:
        return "object";
    case PANEL_MODE_SCROLL:
        return "scroll";
    case PANEL_MODE_MOUTH:
        return "mouth";
    }
    return "none";
}

/* The open inventory and the open chest of the action area. */
static void inventory_json(Json *j)
{
    json_key(j, "inventory");
    if (g_ui.inventory) {
        json_open(j, '{');
        key_int(j, "champion", g_ui.inventory - 1);
        key_str(j, "panel", panel_name());
        json_close(j, '}');
    } else {
        json_null(j);
    }
    json_key(j, "chest");
    if (g_action.mode != ACTION_MODE_CONTAINER) {
        json_null(j);
        return;
    }
    json_open(j, '[');
    for (int i = 0; i < CONTAINER_SLOTS; i++) {
        if (g_action.open_items[i] == THING_NONE)
            continue;
        json_open(j, '{');
        key_int(j, "slot", SLOT_CONTAINER_FIRST + i);
        json_key(j, "object");
        object_json(j, g_action.open_items[i]);
        json_close(j, '}');
    }
    json_close(j, ']');
}

/* The open money box: the coin type and count of each coin slot. */
static void money_box_json(Json *j)
{
    if (g_action.mode != ACTION_MODE_MONEY_BOX || g_action.champion <= 0 || g_action.hand >= SLOT_HAND_COUNT) {
        json_null(j);
        return;
    }
    uint16_t box = champion(g_action.champion - 1)->slots[g_action.hand];
    int16_t counts[COIN_TYPE_MAX];
    money_count_coins_in_box(box, counts);
    json_open(j, '[');
    for (int slot = 0; slot < COIN_TYPE_MAX; slot++) {
        int type = money_coin_type_for_slot(box, slot);
        if (type < 0)
            continue;
        uint16_t item = money_coin_item(type);
        char name[TEXT_SIZE];
        json_open(j, '{');
        key_int(j, "slot", slot);
        json_key(j, "coin");
        trimmed(j, text_gdat(item_index_gdat_category(item), item_index_gdat_index(item), GFX_ITEM_NAME, name),
                TEXT_SIZE);
        key_int(j, "count", counts[type]);
        json_close(j, '}');
    }
    json_close(j, ']');
}

/* The open minion map: whether its mana is paid, and its option buttons. */
static void minion_map_json(Json *j)
{
    if (g_action.mode != ACTION_MODE_MINION_MAP) {
        json_null(j);
        return;
    }
    json_open(j, '{');
    key_bool(j, "powered", (g_action.flags & MAP_POWERED) != 0);
    json_key(j, "options");
    json_open(j, '[');
    for (int i = 0; (g_action.flags & MAP_HAS_OPTIONS) && i < MAP_OPTION_COUNT; i++) {
        json_open(j, '{');
        key_int(j, "option", i);
        key_str(j, "name", map_option_names[i]);
        key_bool(j, "on", (g_action.flags & MAP_OPTION_CREATURES << i) != 0);
        json_close(j, '}');
    }
    json_close(j, ']');
    json_close(j, '}');
}

/* The amount word of a weather value with its light, medium, and heavy thresholds. */
static const char *amount_name(int value, int light, int medium, int heavy)
{
    return amount_names[(value >= light) + (value >= medium) + (value >= heavy)];
}

/* Whether the party sees the sky; outside also the weather that the sky shows and the time of day. The wind shows
   only as the slant of the falling rain. */
static void outdoor_json(Json *j)
{
    key_bool(j, "outdoor", g_game.weather_visible != 0);
    if (!g_game.weather_visible)
        return;
    json_key(j, "weather");
    json_open(j, '{');
    const char *clouds = amount_name(g_game.cloud_cover, CLOUDS_SHOWN, CLOUDS_MORE, CLOUDS_MOST);
    const char *rain = amount_name(g_game.sky_darkness, DARKNESS_RAIN, DARKNESS_MEDIUM, DARKNESS_HEAVY);
    key_str(j, "clouds", weather_sky_has_clouds() ? clouds : amount_names[0]);
    key_str(j, "rain", weather_sky_has_rain() ? rain : amount_names[0]);
    if (view_rain_shows())
        key_str(j, "wind", direction_names[g_game.wind_dir & DIR_MASK]);
    json_close(j, '}');
    key_str(j, "day_time",
            g_weather.hour_light <= HOUR_LIGHT_DAY     ? "day"
            : g_weather.hour_light >= HOUR_LIGHT_NIGHT ? "night"
                                                       : "dawn_dusk");
}

void state_json_state(Json *j)
{
    uint32_t seed = g_random_seed; /* a stat aura draws a random number in champ_get_stat: the read changes no game */
    int mode = input_get_mode();
    json_open(j, '{');
    key_int(j, "time", (long)g_game.game_time);
    key_str(j, "game_mode", g_game_mode_names[g_game_mode]);
    key_str(j, "mode", state_json_mode_name(mode));
    json_key(j, "dialog");
    dialog_json(j, mode);
    json_key(j, "party");
    party_json(j, mode);
    champions_json(j);
    json_key(j, "action_menu");
    action_menu_json(j);
    json_key(j, "spell");
    spell_json(j);
    inventory_json(j);
    json_key(j, "money_box");
    money_box_json(j);
    json_key(j, "minion_map");
    minion_map_json(j);
    json_key(j, "messages");
    json_open(j, '[');
    for (int r = 0; r < MESSAGE_ROWS; r++)
        if (*text_message_row(r))
            json_str(j, text_message_row(r));
    json_close(j, ']');
    outdoor_json(j);
    json_close(j, '}');
    g_random_seed = seed;
}

/* ---- The view and the map ---- */

/* The view sides from the far left to the far right. */
static const char *const side_names[VIEW_WIDTH] = {"left_3", "left_2", "left", "center", "right", "right_2", "right_3"};
/* Explosion names by explosion type (the spell things from SPELL_THING_FIRST). */
static const char *const explosion_names[] = {
    [SPELL_FIREBALL - SPELL_THING_FIRST] = "fireball",
    [SPELL_SLIME - SPELL_THING_FIRST] = "slime",
    [SPELL_LIGHTNING - SPELL_THING_FIRST] = "lightning",
    [SPELL_HARM_NON_MATERIAL - SPELL_THING_FIRST] = "harm_non_material",
    [SPELL_DOOR_TOGGLE - SPELL_THING_FIRST] = "door_toggle",
    [SPELL_POISON_BOLT - SPELL_THING_FIRST] = "poison_bolt",
    [SPELL_POISON_CLOUD - SPELL_THING_FIRST] = "poison_cloud",
    [SPELL_PUSH - SPELL_THING_FIRST] = "push",
    [SPELL_PULL - SPELL_THING_FIRST] = "pull",
    [SPELL_DOOR_SET - SPELL_THING_FIRST] = "door_set",
    [SPELL_MISSILE_WALL - SPELL_THING_FIRST] = "missile_wall",
    [EXPLOSION_DEATH_CLOUD - SPELL_THING_FIRST] = "death_cloud",
    [EXPLOSION_STORM_LIGHTNING - SPELL_THING_FIRST] = "storm_lightning",
    [EXPLOSION_STORM_BLAST - SPELL_THING_FIRST] = "storm_blast",
    [EXPLOSION_SMOKE - SPELL_THING_FIRST] = "smoke",
};
#define EXPLOSION_NAME_COUNT (sizeof explosion_names / sizeof explosion_names[0])
/* Creature names by creature type: no names in the game data; add a name here when a player names it. */
static const char *const creature_names[CREATURE_INFO_COUNT] = {0};

const char *state_json_side_name(int side)
{
    return side >= -VIEW_HALF_WIDTH && side <= VIEW_HALF_WIDTH ? side_names[side + VIEW_HALF_WIDTH] : "unknown";
}

void state_json_explosion_name(Json *j, uint16_t type)
{
    if (type < EXPLOSION_NAME_COUNT && explosion_names[type])
        json_str(j, explosion_names[type]);
    else
        json_int(j, type);
}

void state_json_creature_name(Json *j, uint16_t type)
{
    if (type < CREATURE_INFO_COUNT && creature_names[type])
        json_str(j, creature_names[type]);
    else
        json_int(j, type);
}

/* Where the view stands: the party square, or the walk position while the party walks. */
static void view_place(int *x, int *y, int *dir)
{
    *x = g_party.move_steps ? g_party.view_x : g_game.party_x;
    *y = g_party.move_steps ? g_party.view_y : g_game.party_y;
    *dir = (g_party.move_steps ? g_party.view_dir : g_game.party_dir) & DIR_MASK;
}

/* The squares that the view shows now (seen_visible); returns the count. */
static int visible_squares(SeenSquare out[SEEN_VISIBLE_MAX])
{
    int x, y, dir;
    view_place(&x, &y, &dir);
    return seen_visible(g_game.party_map, x, y, dir, out);
}

static bool on_party_map(int x, int y)
{
    return x >= 0 && y >= 0 && x < dungeon_map_width(g_game.party_map) && y < dungeon_map_height(g_game.party_map);
}

/* The look of a square: a fake wall is a wall until it opens; a closed pit and a teleporter that is off or not
   visible are floor; a square off the map is a wall. */
static const char *element_name(int x, int y)
{
    if (!on_party_map(x, y))
        return "wall";
    uint8_t square = dungeon_square(g_game.party_map, x, y);
    switch (square_type(square)) {
    case SQUARE_FLOOR:
        return "floor";
    case SQUARE_PIT:
        return square & SQUARE_PIT_OPEN ? "pit" : "floor";
    case SQUARE_STAIRS:
        return square & SQUARE_STAIRS_UP ? "stairs_up" : "stairs_down";
    case SQUARE_DOOR:
        return "door";
    case SQUARE_TELEPORTER:
        return square & SQUARE_TELEPORTER_ON && square & SQUARE_TELEPORTER_VISIBLE ? "teleporter" : "floor";
    case SQUARE_FAKE_WALL:
        return square & SQUARE_FAKE_WALL_OPEN ? "floor" : "wall";
    }
    return "wall";
}

static bool is_wall(int x, int y)
{
    return !strcmp(element_name(x, y), "wall");
}

static const char *door_state_name(int state)
{
    switch (state) {
    case DOOR_STATE_OPEN:
        return "open";
    case DOOR_STATE_CLOSED:
        return "closed";
    case DOOR_STATE_BROKEN:
        return "broken";
    }
    return "partly_open";
}

static bool is_object(uint16_t thing)
{
    return thing_type(thing) >= THING_WEAPON && thing_type(thing) <= THING_MISC;
}

/* What the view draws on the square (x, y) at view position pos, seen to dir: VIEW_DRAWS_* bits. */
static unsigned square_draws(int pos, int dir, int x, int y)
{
    ViewSquare s;
    aspect_get(&s, g_game.party_map, dir, x, y);
    return view_draws(pos, s.aspect);
}

/* True when the view draws thing on a square that draws (VIEW_DRAWS_* bits). */
static bool thing_shown(uint16_t thing, unsigned draws)
{
    switch (thing_type(thing)) {
    case THING_GROUP:
        return (draws & VIEW_DRAWS_CREATURES) != 0;
    case THING_PROJECTILE:
        return (draws & VIEW_DRAWS_MISSILES) != 0;
    case THING_EXPLOSION:
        return (draws & VIEW_DRAWS_EXPLOSIONS) != 0;
    }
    return is_object(thing) && draws & VIEW_DRAWS_OBJECTS;
}

int state_json_visible_things(uint16_t type, StateJsonSeenThing *out, int max)
{
    SeenSquare squares[SEEN_VISIBLE_MAX];
    int x, y, dir, count = visible_squares(squares), n = 0;
    view_place(&x, &y, &dir);
    for (int i = 0; i < count; i++) {
        const SeenSquare *q = &squares[i];
        if (is_wall(q->x, q->y))
            continue;
        unsigned draws = square_draws(view_position(q->depth, q->side), dir, q->x, q->y);
        for (uint16_t t = dungeon_square_first_thing(g_game.party_map, q->x, q->y); t != THING_ENDOFLIST;
             t = dungeon_next_thing(t))
            if (thing_type(t) == type && thing_shown(t, draws) && n < max)
                out[n++] = (StateJsonSeenThing){q->depth, q->side, q->x, q->y, t};
    }
    return n;
}

int state_json_visible_groups(uint16_t groups[STATE_JSON_VISIBLE_GROUPS_MAX])
{
    StateJsonSeenThing things[STATE_JSON_VISIBLE_GROUPS_MAX];
    int count = state_json_visible_things(THING_GROUP, things, STATE_JSON_VISIBLE_GROUPS_MAX);
    for (int i = 0; i < count; i++)
        groups[i] = things[i].thing;
    return count;
}

/* A flying thing: a projectile with the name of its object ("spell" for a spell), or an explosion with its type. */
static void flying_json(Json *j, uint16_t thing)
{
    json_open(j, '{');
    if (thing_type(thing) == THING_PROJECTILE) {
        uint16_t payload = thing_word(thing, PROJECTILE_PAYLOAD);
        key_str(j, "kind", "projectile");
        json_key(j, "name");
        if (payload < SPELL_THING_FIRST && is_object(payload))
            trimmed(j, item_name(payload), TEXT_SIZE);
        else
            json_str(j, "spell");
    } else {
        key_str(j, "kind", "explosion");
        key_int(j, "type", explosion_type(thing));
        json_key(j, "name");
        state_json_explosion_name(j, explosion_type(thing));
    }
    json_close(j, '}');
}

/* Appends list (a JSON array text) under key and frees it. */
static void key_list(Json *j, const char *key, Json *list)
{
    json_close(list, ']');
    json_key(j, key);
    json_raw(j, list->s);
    free(list->s);
}

/* The creature groups (one creature each), the objects by cell (seen from the party), and the flying things that
   the view draws on the square (draws: VIEW_DRAWS_* bits). */
static void square_things_json(Json *j, unsigned draws, int x, int y, int dir)
{
    Json creatures = {0}, objects = {0}, flying = {0};
    json_open(&creatures, '[');
    json_open(&objects, '[');
    json_open(&flying, '[');
    for (uint16_t t = dungeon_square_first_thing(g_game.party_map, x, y); t != THING_ENDOFLIST;
         t = dungeon_next_thing(t)) {
        if (!thing_shown(t, draws))
            continue;
        if (thing_type(t) == THING_GROUP) {
            json_open(&creatures, '{');
            json_key(&creatures, "type");
            state_json_creature_name(&creatures, group_creature(t));
            key_int(&creatures, "count", 1);
            json_close(&creatures, '}');
        } else if (is_object(t)) {
            json_open(&objects, '{');
            key_str(&objects, "cell", party_cell_names[dir_relative(thing_cell(t), dir)]);
            json_key(&objects, "name");
            trimmed(&objects, item_name(t), TEXT_SIZE);
            json_close(&objects, '}');
        } else {
            flying_json(&flying, t);
        }
    }
    key_list(j, "creatures", &creatures);
    key_list(j, "objects", &objects);
    key_list(j, "flying", &flying);
}

/* The door state, and the door button when the view draws the door. */
static void door_json(Json *j, unsigned draws, int x, int y)
{
    key_str(j, "door", door_state_name(dungeon_square(g_game.party_map, x, y) & SQUARE_DOOR_STATE));
    uint16_t door = dungeon_square_first_thing(g_game.party_map, x, y);
    if (draws & VIEW_DRAWS_DOOR && door != THING_ENDOFLIST && thing_type(door) == THING_DOOR)
        key_bool(j, "button", door_has_button(door));
}

/* The item that the display of a shown wall shop holds, as {"shop": name}; true when no item is chosen. */
static void shop_json(Json *j, uint16_t shop)
{
    json_key(j, "shop");
    if (thing_type(shop) != THING_SENSOR || !sensor_flag(shop, SHOP_SHOWN) || sensor_flag(shop, SHOP_STEP_FORWARD)) {
        json_bool(j, 1);
        return;
    }
    int x = sensor_target_x(shop), y = sensor_target_y(shop);
    for (uint16_t t = aspect_first_thing(g_game.party_map, x, y); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR;
         t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_SENSOR || sensor_type(t) != SENSOR_SHOP)
            continue;
        uint16_t item = sensor_data(t);
        char name[TEXT_SIZE];
        trimmed(j, text_gdat(item_index_gdat_category(item), item_index_gdat_index(item), GFX_ITEM_NAME, name),
                TEXT_SIZE);
        return;
    }
    json_bool(j, 1);
}

/* A wall sensor or ornament text on the face: "switch" for a click switch, "lock" (the ornament) for one that takes
   an object. Returns false when the face has neither. */
static bool wall_sensors_json(Json *j, int ornament, int face, int x, int y)
{
    bool click = false, takes = false;
    for (uint16_t t = aspect_first_thing(g_game.party_map, x, y); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR;
         t = dungeon_next_thing(t)) {
        if (thing_cell(t) != face)
            continue;
        if (thing_type(t) == THING_TEXT && text_kind(t) == TEXT_KIND_ORNAMENT) {
            int sub = text_subtype(t);
            click |= sub == SUBTYPE_HAND_SWITCH;
            takes |= sub == SUBTYPE_ITEM_HOLDER || sub == SUBTYPE_ITEM_HOLDER_MARKER;
        }
        if (thing_type(t) != THING_SENSOR || sensor_ornament_index(t) < 0)
            continue;
        switch (sensor_type(t)) {
        case WALL_CLICK_ANY:
        case WALL_CLICK_EMPTY_HAND:
        case WALL_CLICK_TOGGLE:
        case WALL_CLICK_BUTTON:
        case WALL_CLICK_BUTTON_2:
        case WALL_CLICK_DOOR_BUTTON:
        case WALL_CLICK_TELEPORT:
            click = true;
            break;
        case WALL_CLICK_ITEM:
        case WALL_CLICK_CHARGED_ITEM:
        case WALL_CLICK_COUNTED_ITEM:
        case WALL_ITEM_HOLDER:
        case WALL_ANY_ITEM:
        case WALL_ITEM:
        case WALL_NOT_ITEM:
            takes = true;
            break;
        }
    }
    if (click)
        key_bool(j, "switch", 1);
    if (takes)
        key_int(j, "lock", ornament);
    return click || takes;
}

/* What a wall ornament on the face holds: a mirror, an alcove, a shop, a fountain, a switch or a lock; else the
   ornament index. */
static void wall_ornament_json(Json *j, const ViewSquare *s, int k, int face, int x, int y)
{
    int ornament = aspect_word_ornament(s->side[k]);
    int kind = aspect_value_key(CAT_WALL_ORNAMENT, ornament, ORN_KEY_KIND);
    if (kind == KIND_PORTRAIT)
        key_bool(j, "mirror", 1);
    else if (kind == KIND_ALCOVE)
        key_bool(j, "alcove", 1);
    else if (kind == KIND_SHOP)
        shop_json(j, k == SIDE_FRONT ? s->text : THING_TEXT_NONE);
    else if (aspect_value_key(CAT_WALL_ORNAMENT, ornament, ORNAMENT_KEY_TOUCH))
        key_bool(j, "fountain", 1);
    else if (!wall_sensors_json(j, ornament, face, x, y))
        key_int(j, "ornament", ornament);
}

/* The wall face k of s (a view side word: TURN_RIGHT, SIDE_FRONT, TURN_LEFT) under key, when it shows something. A
   text plate gives its text on the front face (null where the view draws no text). */
static void wall_face_json(Json *j, const char *key, const ViewSquare *s, int k, int dir, int x, int y)
{
    int ornament = aspect_word_ornament(s->side[k]), face = dir_turn(dir, k);
    if (ornament == ORNAMENT_NONE)
        return;
    json_key(j, key);
    json_open(j, '{');
    key_str(j, "facing", direction_names[face]);
    if (ornament != ORNAMENT_PLATE) {
        wall_ornament_json(j, s, k, face, x, y);
    } else if (k == SIDE_FRONT && thing_type(s->text) == THING_TEXT) {
        char text[SCROLL_TEXT_SIZE];
        key_str(j, "text", text_decode_dungeon(text, sizeof text, s->text, 0));
    } else {
        json_key(j, "text");
        json_null(j);
    }
    json_close(j, '}');
}

/* The wall faces that the view draws on the wall square at view position pos and side. */
static void wall_faces_json(Json *j, int pos, int side, int dir, int x, int y)
{
    ViewSquare s;
    aspect_get(&s, g_game.party_map, dir, x, y);
    unsigned draws = view_draws(pos, ASPECT_WALL);
    if (draws & VIEW_DRAWS_SIDE_FACE)
        wall_face_json(j, "side_face", &s, side < 0 ? TURN_RIGHT : TURN_LEFT, dir, x, y);
    if (draws & VIEW_DRAWS_FRONT_FACE)
        wall_face_json(j, "front_face", &s, SIDE_FRONT, dir, x, y);
}

/* The floor ornament index of a floor, pit, or teleporter square; ORNAMENT_NONE for none. */
static int floor_ornament(int dir, int x, int y)
{
    ViewSquare s;
    aspect_get(&s, g_game.party_map, dir, x, y);
    if (s.aspect != ASPECT_FLOOR && s.aspect != ASPECT_PIT && s.aspect != ASPECT_TELEPORTER)
        return ORNAMENT_NONE;
    return aspect_word_ornament(s.side[SIDE_FLOOR_ORNAMENT]);
}

/* A square of the view: row, side, position, element, and what the view draws on it. */
static void view_square_json(Json *j, const SeenSquare *q, int dir)
{
    int pos = view_position(q->depth, q->side);
    json_open(j, '{');
    key_int(j, "depth", q->depth);
    key_int(j, "side", q->side);
    key_int(j, "x", q->x);
    key_int(j, "y", q->y);
    key_str(j, "element", element_name(q->x, q->y));
    if (is_wall(q->x, q->y)) {
        wall_faces_json(j, pos, q->side, dir, q->x, q->y);
    } else {
        unsigned draws = square_draws(pos, dir, q->x, q->y);
        if (square_type(dungeon_square(g_game.party_map, q->x, q->y)) == SQUARE_DOOR)
            door_json(j, draws, q->x, q->y);
        int ornament = floor_ornament(dir, q->x, q->y);
        if (view_shows_floor_ornament(pos, ornament))
            key_int(j, "floor_ornament", ornament);
        square_things_json(j, draws, q->x, q->y, dir);
    }
    json_close(j, '}');
}

/* The square of a click zone on the square of the view or the square ahead of it, and the top object in the cell of the zone
   thing; THING_NONE when the zone holds no object. */
static uint16_t zone_top(const ViewClickZone *z, int *x, int *y)
{
    uint16_t top = THING_NONE;
    int dir;
    view_place(x, y, &dir);
    if (z->cell == POS_FRONT) {
        *x += g_dir_dx[dir];
        *y += g_dir_dy[dir];
    }
    if (z->thing == THING_NONE || (z->cell != POS_PARTY && z->cell != POS_FRONT) || !on_party_map(*x, *y))
        return top;
    for (uint16_t t = dungeon_square_first_thing(g_game.party_map, *x, *y); t != THING_ENDOFLIST;
         t = dungeon_next_thing(t))
        if (is_object(t) && thing_cell(t) == thing_cell(z->thing))
            top = t;
    return top;
}

/* The pile of a click zone as {cell, top, objects}, the objects top first. */
static void pile_json(Json *j, const ViewClickZone *z, int cell)
{
    int x, y, n = 0, at = thing_cell(z->thing);
    uint16_t top = zone_top(z, &x, &y), first = dungeon_square_first_thing(g_game.party_map, x, y);
    json_open(j, '{');
    key_int(j, "cell", cell);
    json_key(j, "top");
    trimmed(j, item_name(top), TEXT_SIZE);
    json_key(j, "objects");
    json_open(j, '[');
    for (uint16_t t = first; t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        n += is_object(t) && thing_cell(t) == at;
    for (int k = n - 1; k >= 0; k--) {
        int i = 0;
        for (uint16_t t = first; t != THING_ENDOFLIST; t = dungeon_next_thing(t))
            if (is_object(t) && thing_cell(t) == at && i++ == k)
                trimmed(j, item_name(t), TEXT_SIZE);
    }
    json_close(j, ']');
    json_close(j, '}');
}

/* The object piles of the click zones of the last drawn view (cells 0 to 3, seen from the party), the alcove ahead,
   and whether a click on the wall ahead reaches something; none while the inventory hides the view. */
static void piles_json(Json *j)
{
    bool shown = !g_ui.inventory;
    int x, y;
    json_key(j, "piles");
    json_open(j, '[');
    for (int k = 0; shown && k < CELL_COUNT; k++)
        if (g_view_click_zones[k].kind == CLICK_OBJECT && zone_top(&g_view_click_zones[k], &x, &y) != THING_NONE)
            pile_json(j, &g_view_click_zones[k], k);
    json_close(j, ']');
    const ViewClickZone *alcove = &g_view_click_zones[VIEW_CLICK_ZONE_ALCOVE];
    bool has_alcove = shown && alcove->kind == CLICK_ALCOVE;
    json_key(j, "alcove");
    if (has_alcove && zone_top(alcove, &x, &y) != THING_NONE)
        pile_json(j, alcove, PILE_ALCOVE);
    else
        json_null(j);
    bool clickable = has_alcove;
    for (int i = VIEW_CLICK_ZONE_FIXED; shown && i < g_view_click_zone_count; i++) {
        const ViewClickZone *z = &g_view_click_zones[i];
        clickable |= z->cell == POS_FRONT && (z->kind == CLICK_WALL_ORNAMENT || z->kind == CLICK_DOOR_BUTTON);
    }
    key_bool(j, "wall_ahead_clickable", clickable);
}

void state_json_view(Json *j)
{
    SeenSquare squares[SEEN_VISIBLE_MAX];
    int x, y, dir, count = visible_squares(squares);
    view_place(&x, &y, &dir);
    json_open(j, '{');
    key_str(j, "facing", direction_names[dir]);
    key_int(j, "darkness", g_view_light_level);
    json_key(j, "squares");
    json_open(j, '[');
    for (int i = 0; i < count; i++)
        view_square_json(j, &squares[i], dir);
    json_close(j, ']');
    piles_json(j);
    json_close(j, '}');
}

/* The map glyph of an explored square by its look; '_' marks a floor with a floor ornament that the map draws. */
static char map_char(int x, int y)
{
    const char *name = element_name(x, y);
    uint8_t square = dungeon_square(g_game.party_map, x, y);
    int state = square & SQUARE_DOOR_STATE;
    if (!strcmp(name, "wall"))
        return '#';
    if (!strcmp(name, "pit"))
        return 'O';
    if (!strcmp(name, "stairs_up"))
        return '<';
    if (!strcmp(name, "stairs_down"))
        return '>';
    if (!strcmp(name, "teleporter"))
        return 'T';
    if (!strcmp(name, "door"))
        return state == DOOR_STATE_OPEN || state == DOOR_STATE_BROKEN ? 'd' : 'D';
    return mmap_shows_floor_ornament(floor_ornament(g_game.party_dir & DIR_MASK, x, y)) ? '_' : '.';
}

/* Map row y in out: one glyph per square, '@' for the party, a space for a square not explored and not in view. */
static void map_row(int y, int w, const SeenSquare *squares, int count, char *out)
{
    for (int x = 0; x < w; x++) {
        bool seen = seen_is_seen(g_game.party_map, x, y);
        for (int i = 0; i < count && !seen; i++)
            seen = squares[i].x == x && squares[i].y == y;
        out[x] = x == g_game.party_x && y == g_game.party_y ? '@' : seen ? map_char(x, y) : ' ';
    }
    out[w] = 0;
}

/* The map rows under key; with a grid, the x digits above and the y numbers in front. */
static void map_rows_json(Json *j, const char *key, bool grid)
{
    SeenSquare squares[SEEN_VISIBLE_MAX];
    int w = dungeon_map_width(g_game.party_map), h = dungeon_map_height(g_game.party_map),
        count = visible_squares(squares);
    int prefix = grid ? MAP_GRID_PREFIX : 0;
    char line[MAP_GRID_PREFIX + MAP_SIDE_MAX + 1];
    if (w > MAP_SIDE_MAX)
        w = MAP_SIDE_MAX;
    json_key(j, key);
    json_open(j, '[');
    if (grid) {
        memset(line, ' ', MAP_GRID_PREFIX);
        for (int x = 0; x < w; x++)
            line[MAP_GRID_PREFIX + x] = (char)('0' + x % DECIMAL_BASE);
        line[MAP_GRID_PREFIX + w] = 0;
        json_str(j, line);
    }
    for (int y = 0; y < h; y++) {
        snprintf(line, sizeof line, "%2d ", y);
        map_row(y, w, squares, count, line + prefix);
        json_str(j, line);
    }
    json_close(j, ']');
}

void state_json_map(Json *j)
{
    json_open(j, '{');
    key_int(j, "map", g_game.party_map);
    key_int(j, "level", dungeon_map_level(g_game.party_map));
    key_int(j, "width", dungeon_map_width(g_game.party_map));
    key_int(j, "height", dungeon_map_height(g_game.party_map));
    pair(j, "party", g_game.party_x, g_game.party_y);
    key_str(j, "facing", direction_names[g_game.party_dir & DIR_MASK]);
    key_str(j, "legend",
            "row index = y (north at the top), column = x; @ party, # wall, . floor, D closed door, "
            "d open door, < stairs up, > stairs down, O pit, T teleporter, "
            "_ floor ornament (for example a pressure pad), space not explored; "
            "grid: the rows with x mod 10 above and y in front");
    map_rows_json(j, "rows", false);
    map_rows_json(j, "grid", true);
    json_close(j, '}');
}

char *state_json_string(int part)
{
    Json j = {0};
    if (part == STATE_JSON_STATE)
        state_json_state(&j);
    else if (part == STATE_JSON_VIEW)
        state_json_view(&j);
    else if (part == STATE_JSON_MAP)
        state_json_map(&j);
    else
        json_null(&j);
    return j.s;
}
