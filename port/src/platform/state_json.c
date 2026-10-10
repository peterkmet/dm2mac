#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "state_json.h"
#include "action.h"
#include "champion.h"
#include "dialog.h"
#include "dungeon.h"
#include "game.h"
#include "game_text.h"
#include "gdat.h"
#include "input.h"
#include "item.h"
#include "magic.h"
#include "minion.h"
#include "money.h"
#include "random.h"
#include "move.h"
#include "save.h"
#include "text.h"
#include "ui.h"
#include "view.h"
#include "weather.h"

#define JSON_START_SIZE 1024          /* the first size of the text buffer */
#define LONG_TEXT_SIZE 24             /* the digits and sign of a long */
#define ESCAPE_SIZE 8                 /* a \u00XX escape and its end mark */
#define ESCAPE_LENGTH 6               /* the length of a \u00XX escape */
#define RUNE_NAME_COUNT (CHAMPION_RUNES_SIZE * SPELL_RUNES_PER_SET)   /* the power runes and three rune sets */
#define RUNE_NAME_MAX 4               /* the letters of the longest rune name */
#define SCROLL_TEXT_SIZE (TEXT_SIZE * 2)   /* a decoded scroll text, as the scroll panel holds it */

/* Appends n bytes; the buffer doubles when they do not fit. */
static void put(Json *j, const char *text, size_t n)
{
    if (j->len + n + 1 > j->cap) {
        size_t cap = j->cap ? j->cap : JSON_START_SIZE;
        while (cap < j->len + n + 1) cap *= 2;
        char *s = realloc(j->s, cap);
        if (!s) abort();
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
    if (j->len && !strchr("{[:", j->s[j->len - 1])) put(j, ",", 1);
}

void json_raw(Json *j, const char *text) { put(j, text, strlen(text)); }
void json_rawn(Json *j, const char *text, size_t n) { put(j, text, n); }
void json_open(Json *j, char bracket) { separate(j); put(j, &bracket, 1); }
void json_close(Json *j, char bracket) { put(j, &bracket, 1); }
void json_null(Json *j) { separate(j); json_raw(j, "null"); }
void json_bool(Json *j, int value) { separate(j); json_raw(j, value ? "true" : "false"); }

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

void json_str(Json *j, const char *text) { json_strn(j, text, strlen(text)); }

void json_key(Json *j, const char *key)
{
    json_str(j, key);
    put(j, ":", 1);
}

/* ---- The game state ---- */

/* The input modes by number (the roots of the input tree in input.c). */
static const char *const mode_names[] = {
    [INPUT_MODE_MENU] = "menu", [INPUT_MODE_PICTURE] = "picture", [INPUT_MODE_SLEEP] = "sleep", [INPUT_MODE_PAUSE] = "pause",
    [INPUT_MODE_DIALOG] = "dialog", [INPUT_MODE_MOVE] = "move", [INPUT_MODE_PUSH] = "push", [INPUT_MODE_RECRUIT] = "recruit",
    [INPUT_MODE_INVENTORY] = "inventory", [INPUT_MODE_NAME] = "name",
};
#define MODE_COUNT ((int)(sizeof mode_names / sizeof mode_names[0]))
static const char *const direction_names[] = {"north", "east", "south", "west"};
/* The places of the champions in the party, seen in the party direction. */
static const char *const party_cell_names[] = {"front_left", "front_right", "back_right", "back_left"};
/* The statistics that the skills panel shows (luck is not shown), from STAT_STRENGTH on. */
static const char *const statistic_names[STAT_COUNT] = {
    [STAT_STRENGTH] = "strength", [STAT_DEXTERITY] = "dexterity", [STAT_WISDOM] = "wisdom",
    [STAT_VITALITY] = "vitality", [STAT_ANTI_MAGIC] = "antimagic", [STAT_ANTI_FIRE] = "antifire",
};
static const char *const skill_names[SKILL_BASE_COUNT] = {"fighter", "ninja", "priest", "wizard"};
/* The wound bits of Champion.wounds: the hands, the head, the torso, the legs, the feet. */
static const char *const wound_names[] = {"ready_hand", "action_hand", "head", "torso", "legs", "feet"};
/* The champion slots by their place in the inventory picture: each hand loads from its quiver slots (the ready hand
   from 11 then 6, the action hand from 12 then 7 to 9); slot 13 and the two rows 14 to 29 are the backpack. */
static const char *const slot_names[SLOT_COUNT] = {
    "ready_hand", "action_hand", "head", "torso", "legs", "feet", "ready_quiver_2", "action_quiver_2",
    "action_quiver_3", "action_quiver_4", "neck", "ready_quiver_1", "action_quiver_1", "backpack_1", "backpack_2",
    "backpack_3", "backpack_4", "backpack_5", "backpack_6", "backpack_7", "backpack_8", "backpack_9", "backpack_10",
    "backpack_11", "backpack_12", "backpack_13", "backpack_14", "backpack_15", "backpack_16", "backpack_17",
};
/* The rune names by rune glyph - SPELL_RUNE_GLYPH_FIRST: the power runes, then the three rune sets. */
static const char *const rune_names[RUNE_NAME_COUNT] = {
    "lo", "um", "on", "ee", "pal", "mon", "ya", "vi", "oh", "ful", "des", "zo",
    "ven", "ew", "kath", "ir", "bro", "gor", "ku", "ros", "dain", "neta", "ra", "sar",
};
static const char *const hand_names[SLOT_HAND_COUNT] = {"ready", "action"};
static const char *const map_option_names[MAP_OPTION_COUNT] = {"creatures", "details", "items", "hold"};
/* The amounts of rain and clouds that the sky shows. */
static const char *const amount_names[] = {"none", "light", "medium", "heavy"};

const char *state_json_mode_name(int mode)
{
    return mode >= 0 && mode < MODE_COUNT && mode_names[mode] ? mode_names[mode] : "unknown";
}

bool state_json_game_over(void) { return main_loop.end || session.completed; }

static void key_int(Json *j, const char *key, long value) { json_key(j, key); json_int(j, value); }
static void key_str(Json *j, const char *key, const char *text) { json_key(j, key); json_str(j, text); }
static void key_bool(Json *j, const char *key, int value) { json_key(j, key); json_bool(j, value); }

/* A string of the first n bytes of text without its trailing spaces (GDAT names end with a space). */
static void trimmed(Json *j, const char *text, size_t n)
{
    n = strnlen(text, n);
    while (n && text[n - 1] == ' ') n--;
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
    case THING_WEAPON: return "weapon";
    case THING_ARMOUR: return "armour";
    case THING_SCROLL: return "scroll";
    case THING_POTION: return "potion";
    case THING_CONTAINER: return "container";
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
        if (rune >= 0 && rune < RUNE_NAME_COUNT) json_str(j, rune_names[rune]);
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
        for (const char *name = rune_names[rune]; *name; name++) out[n++] = (char)toupper((unsigned char)*name);
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
        if (level < 2) json_null(j);
        else rank_json(j, ui_skill_rank_text(champ, s, text));
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
        if (c->slots[s] == THING_NONE) continue;
        json_open(j, '{');
        key_int(j, "slot", s);
        key_str(j, "where", slot_names[s]);
        json_key(j, "object");
        object_json(j, c->slots[s]);
        json_close(j, '}');
    }
    json_close(j, ']');
}

/* The action names of the open action menu (champ_actions). */
static void action_names_json(Json *j)
{
    json_open(j, '[');
    for (int i = 0; i < champ_action_count; i++) {
        const ChampionAction *a = &champ_actions[i];
        const char *label = text_gdat_label(a->category, a->index, a->entry);
        trimmed(j, label ? label : "", TEXT_SIZE);
    }
    json_close(j, ']');
}

/* The action menu that each hand of a living champion opens; the open action menu stays as it is. */
static void hands_json(Json *j, int champ)
{
    ChampionAction saved[CHAMPION_ACTION_MAX];
    int saved_count = champ_action_count;
    uint16_t saved_thing = champ_action_thing;
    memcpy(saved, champ_actions, sizeof saved);
    json_open(j, '{');
    for (int hand = SLOT_READY_HAND; hand < SLOT_HAND_COUNT; hand++) {
        champ_action_count = 0;
        if (champion(champ)->health) champ_list_actions(champ, champion(champ)->slots[hand], hand);
        json_key(j, hand_names[hand]);
        action_names_json(j);
    }
    json_close(j, '}');
    memcpy(champ_actions, saved, sizeof saved);
    champ_action_count = saved_count;
    champ_action_thing = saved_thing;
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
    key_str(j, "position", party_cell_names[dir_relative(c->cell, game.party_dir)]);
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
        if (c->wounds & 1u << w) json_str(j, wound_names[w]);
    json_close(j, ']');
    json_key(j, "slots");
    slots_json(j, c);
    json_key(j, "hands");
    hands_json(j, champ);
    json_key(j, "runes");
    runes_json(j, c);
    json_key(j, "busy");
    json_open(j, '[');
    for (int k = 0; k < ACTION_DELAY_COUNT; k++) json_bool(j, c->action_delay[k] != 0);
    json_close(j, ']');
    json_close(j, '}');
}

/* The open dialog and its button texts. The slot dialogs do not set dialog.id: they give the id of the last
   dialog (docs/skull-formats.md). */
static void dialog_json(Json *j, int mode)
{
    if (mode != INPUT_MODE_DIALOG) {
        json_null(j);
        return;
    }
    json_open(j, '{');
    key_int(j, "id", dialog.id);
    json_key(j, "buttons");
    json_open(j, '[');
    for (int e = 0; e < DIALOG_TEXT_COUNT; e++) {
        char text[TEXT_SIZE];
        if (*text_gdat(CAT_DIALOG, dialog.id, e, text)) json_str(j, text);
    }
    json_close(j, ']');
    json_close(j, '}');
}

/* The party: square, direction, darkness, and whether it can move, sleeps, or died. */
static void party_json(Json *j, int mode)
{
    json_open(j, '{');
    key_int(j, "map", game.party_map);
    key_int(j, "level", dungeon_map_level(game.party_map));
    key_int(j, "x", game.party_x);
    key_int(j, "y", game.party_y);
    key_str(j, "facing", direction_names[game.party_dir & DIR_MASK]);
    key_int(j, "darkness", view_light_level);
    key_bool(j, "can_move", !game.party_move_cooldown && !party.move_steps && !input_is_waiting()
                            && (mode == INPUT_MODE_MOVE || mode == INPUT_MODE_PUSH));
    key_bool(j, "sleeping", party_sleeping);
    key_bool(j, "dead", main_loop.end != 0);
    json_close(j, '}');
}

/* The leader, the leader hand, the shown candidate (the last champion record), and the members of the party. */
static void champions_json(Json *j)
{
    int members = game.champion_count, candidate = -1;
    if (ui.candidate && members) candidate = --members;
    json_key(j, "leader");
    if ((int16_t)game.leader == -1 || (int16_t)game.leader == candidate) json_null(j);
    else json_int(j, (int16_t)game.leader);
    json_key(j, "leader_hand");
    object_json(j, game.champion_count ? game.leader_hand : THING_NONE);
    json_key(j, "candidate");
    if (candidate >= 0) champion_json(j, candidate);
    else json_null(j);
    json_key(j, "champions");
    json_open(j, '[');
    for (int i = 0; i < members && i < CHAMPION_MAX; i++) champion_json(j, i);
    json_close(j, ']');
}

/* The open action menu: whose hand and its actions. */
static void action_menu_json(Json *j)
{
    if (action.mode != ACTION_MODE_MENU || action.champion <= 0 || action.hand >= SLOT_HAND_COUNT) {
        json_null(j);
        return;
    }
    json_open(j, '{');
    key_int(j, "champion", action.champion - 1);
    key_str(j, "hand", hand_names[action.hand]);
    json_key(j, "actions");
    action_names_json(j);
    json_close(j, '}');
}

/* The open spell panel: the caster, the entered runes, and the runes of the next set with their mana cost. */
static void spell_json(Json *j)
{
    if (action.mode != ACTION_MODE_SPELL || action.champion <= 0) {
        json_null(j);
        return;
    }
    int caster = action.champion - 1;
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
    switch (ui.panel_mode) {
    case PANEL_MODE_FOOD: return "food";
    case PANEL_MODE_SKILLS: return "skills";
    case PANEL_MODE_OBJECT: return "object";
    case PANEL_MODE_SCROLL: return "scroll";
    case PANEL_MODE_MOUTH: return "mouth";
    }
    return "none";
}

/* The open inventory and the open chest of the action area. */
static void inventory_json(Json *j)
{
    json_key(j, "inventory");
    if (ui.inventory) {
        json_open(j, '{');
        key_int(j, "champion", ui.inventory - 1);
        key_str(j, "panel", panel_name());
        json_close(j, '}');
    } else {
        json_null(j);
    }
    json_key(j, "chest");
    if (action.mode != ACTION_MODE_CONTAINER) {
        json_null(j);
        return;
    }
    json_open(j, '[');
    for (int i = 0; i < CONTAINER_SLOTS; i++) {
        if (action.open_items[i] == THING_NONE) continue;
        json_open(j, '{');
        key_int(j, "slot", SLOT_CONTAINER_FIRST + i);
        json_key(j, "object");
        object_json(j, action.open_items[i]);
        json_close(j, '}');
    }
    json_close(j, ']');
}

/* The open money box: the coin type and count of each coin slot. */
static void money_box_json(Json *j)
{
    if (action.mode != ACTION_MODE_MONEY_BOX || action.champion <= 0 || action.hand >= SLOT_HAND_COUNT) {
        json_null(j);
        return;
    }
    uint16_t box = champion(action.champion - 1)->slots[action.hand];
    int16_t counts[COIN_TYPE_MAX];
    money_count_coins_in_box(box, counts);
    json_open(j, '[');
    for (int slot = 0; slot < COIN_TYPE_MAX; slot++) {
        int type = money_coin_type_for_slot(box, slot);
        if (type < 0) continue;
        uint16_t item = money_coin_item(type);
        char name[TEXT_SIZE];
        json_open(j, '{');
        key_int(j, "slot", slot);
        json_key(j, "coin");
        trimmed(j, text_gdat(item_index_gdat_category(item), item_index_gdat_index(item), ITEM_NAME_ENTRY, name),
                TEXT_SIZE);
        key_int(j, "count", counts[type]);
        json_close(j, '}');
    }
    json_close(j, ']');
}

/* The open minion map: whether its mana is paid, and its option buttons. */
static void minion_map_json(Json *j)
{
    if (action.mode != ACTION_MODE_MINION_MAP) {
        json_null(j);
        return;
    }
    json_open(j, '{');
    key_bool(j, "powered", (action.flags & MAP_POWERED) != 0);
    json_key(j, "options");
    json_open(j, '[');
    for (int i = 0; (action.flags & MAP_HAS_OPTIONS) && i < MAP_OPTION_COUNT; i++) {
        json_open(j, '{');
        key_int(j, "option", i);
        key_str(j, "name", map_option_names[i]);
        key_bool(j, "on", (action.flags & MAP_OPTION_CREATURES << i) != 0);
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
    key_bool(j, "outdoor", game.weather_visible != 0);
    if (!game.weather_visible) return;
    json_key(j, "weather");
    json_open(j, '{');
    const char *clouds = amount_name(game.cloud_cover, CLOUDS_SHOWN, CLOUDS_MORE, CLOUDS_MOST);
    const char *rain = amount_name(game.sky_darkness, DARKNESS_RAIN, DARKNESS_MEDIUM, DARKNESS_HEAVY);
    key_str(j, "clouds", weather_sky_has_clouds() ? clouds : amount_names[0]);
    key_str(j, "rain", weather_sky_has_rain() ? rain : amount_names[0]);
    if (view_rain_shows()) key_str(j, "wind", direction_names[game.wind_dir & DIR_MASK]);
    json_close(j, '}');
    key_str(j, "day_time", weather.hour_light <= HOUR_LIGHT_DAY ? "day"
                           : weather.hour_light >= HOUR_LIGHT_NIGHT ? "night" : "dawn_dusk");
}

void state_json_state(Json *j)
{
    uint32_t seed = random_seed;    /* a stat aura draws a random number in champ_get_stat: the read changes no game */
    int mode = input_get_mode();
    json_open(j, '{');
    key_int(j, "time", (long)game.game_time);
    key_str(j, "game_mode", game_mode_names[game_mode]);
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
        if (*text_message_row(r)) json_str(j, text_message_row(r));
    json_close(j, ']');
    outdoor_json(j);
    json_close(j, '}');
    random_seed = seed;
}

char *state_json_string(int part)
{
    Json j = {0};
    if (part == STATE_JSON_STATE) state_json_state(&j);
    else json_null(&j);
    return j.s;
}
