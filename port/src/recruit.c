/* Recruiting a champion from its mirror: the candidate joins the party or goes back. */
#include <string.h>
#include "action.h"
#include "champion.h"
#include "draw.h"
#include "dungeon.h"
#include "game.h"
#include "hooks.h"
#include "input.h"
#include "inventory.h"
#include "item.h"
#include "move.h"
#include "random.h"
#include "text.h"
#include "ui.h"

#define SENSOR_MIRROR 0x7e          /* the wall sensor of a champion mirror; its data is the portrait */
#define SENSOR_MIRROR_ACTIVE 0x0004 /* sensor flags bit: the mirror still holds its champion */
#define PORTRAIT_TEXT 0x18          /* GDAT text (0x16, portrait, 5, 0x18): "NAME TITLE" */
#define PORTRAIT_DATA_TYPE 8        /* GDAT item (0x16, portrait, 8, 0): health, stamina, mana, statistics, skills */
#define STAT_START_MIN 30
#define SKILL_EXPERIENCE_BASE 0x40
#define FOOD_START 1500
#define CMD_RECRUIT_CANCEL 0x93
#define TEXT_JOINED 0x0e            /* GDAT text (1, 0, 0x0e): a champion joins the party */
#define MOUTH_CATEGORY 9
#define MOUTH_INDEX 0x5b
#define MOUTH_OPEN 0xfb
#define INPUT_MODE_RECRUIT 7
#define MESSAGE_COLOR_FIRST UI_GREEN
enum { DATA_HEALTH, DATA_STAMINA, DATA_MANA, DATA_STATS, DATA_SKILLS = 6 };  /* word indices; skill s at DATA_SKILLS + s */

static const int8_t dir_dx[DIR_COUNT] = {0, 1, 0, -1}, dir_dy[DIR_COUNT] = {-1, 0, 1, 0};
/* The colors of the champion messages. */
static const uint8_t message_colors[CHAMPION_MAX] = {UI_GREEN, UI_YELLOW, UI_RED, UI_BLUE};

static int16_t le16s(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

/* Makes champion index the champion of portrait, facing dir, in the first free cell. */
static void champ_add_to_party(int portrait, int index, int dir)
{
    Champion *c = champion(index);
    memset(c, 0, sizeof *c);
    c->portrait = (uint8_t)portrait;
    c->last_command[0] = c->last_command[1] = 0xff;
    c->damage_event = THING_NONE;
    c->dir = (uint8_t)dir;
    int k = 0;
    while (champ_get_index_in_cell((k + game.party_dir) & 3) != -1) k++;
    c->cell = (uint8_t)((k + game.party_dir) & 3);
    c->unknown_26[2] = (uint8_t)game.party_dir;
    for (int s = 0; s < SLOT_COUNT; s++) c->slots[s] = THING_NONE;
    char text[TEXT_SIZE];
    const char *p = text_gdat(GDAT_CHAMPION_CATEGORY, portrait, PORTRAIT_TEXT, text);
    char ch;
    /* the name ends at a space or after 7 characters; the title follows (the 8th character of a long name is lost) */
    for (int i = 0;; i++) {
        ch = *p++;
        if (ch == ' ' || !ch || i >= CHAMPION_NAME_SIZE - 1) break;
        c->name[i] = ch;
    }
    for (int i = 0; ch && (ch = *p++) && i < CHAMPION_TITLE_SIZE - 1; i++) c->title[i] = ch;
    uint32_t size;
    const uint8_t *d = gdat_item(draw_gdat(), gdat_find(draw_gdat(), GDAT_CHAMPION_CATEGORY, portrait, PORTRAIT_DATA_TYPE, 0),
                                 &size);
    if (!d) return;  /* fix: the original reads no data */
    c->health = c->max_health = le16s(d + 2 * DATA_HEALTH);
    c->stamina = c->max_stamina = le16s(d + 2 * DATA_STAMINA);
    c->mana = c->max_mana = le16s(d + 2 * DATA_MANA);
    for (int s = 0; s < STAT_COUNT; s++) {
        int16_t v = le16s(d + 2 * (DATA_STATS + s));
        c->stats[s][0] = c->stats[s][1] = (uint8_t)(v > STAT_START_MIN ? v : STAT_START_MIN);
    }
    for (int s = SKILL_BASE_COUNT; s < SKILL_COUNT; s++) {
        uint16_t level = (uint16_t)le16s(d + 2 * (DATA_SKILLS + s));
        c->experience[s] = level ? (uint32_t)SKILL_EXPERIENCE_BASE << level : 0;
    }
    for (int b = 0; b < SKILL_BASE_COUNT; b++) {
        uint32_t sum = 0;
        for (int k2 = 0; k2 < 4; k2++) sum += c->experience[(b + 1) * 4 + k2];
        c->experience[b] = sum;
    }
    c->food = (int16_t)((rand_next() & 0xff) + FOOD_START);
    c->water = (int16_t)((rand_next() & 0xff) + FOOD_START);
}

/* The mirror sensor on the square (x, y) of the current map. */
static uint16_t mirror_sensor(int x, int y)
{
    for (uint16_t t = dungeon_square_first_thing(party.cur_map, x, y); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        if (thing_type(t) == THING_SENSOR && (thing_word(t, SENSOR_TYPE_DATA) & SENSOR_TYPE_MASK) == SENSOR_MIRROR) return t;
    return THING_NONE;
}

int champ_start_recruit(int x, int y, int dir, int map)
{
    int index = game.champion_count;
    if (game.leader_hand != THING_NONE || index >= CHAMPION_MAX) return 0;
    int saved = party.cur_map;
    dun_set_current_map(map);
    int fx = x + dir_dx[dir], fy = y + dir_dy[dir];
    uint16_t mirror = mirror_sensor(fx, fy);
    if (mirror == THING_NONE) {  /* fix: the original then takes an uninitialized portrait */
        dun_set_current_map(saved);
        return 0;
    }
    int portrait = (uint8_t)(thing_word(mirror, SENSOR_TYPE_DATA) >> SENSOR_DATA_SHIFT);
    champ_add_to_party(portrait, index, dir);
    if (index) {
        ui_action_area_close();
        ui_update_action_area(0);
    }
    ui.candidate = (uint16_t)(index + 1);
    game.champion_count++;
    if (!index) champ_set_leader(0);
    int back = (dir + 2) & 3;
    for (uint16_t t = dungeon_square_first_thing(party.cur_map, fx, fy); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        if (thing_type(t) > THING_SENSOR && t >> THING_CELL_SHIFT == back) inv_put_object_in_free_slot(index, t);
    inv_toggle(index);
    input_set_mode(INPUT_MODE_RECRUIT);
    ui_hide_game_view();
    dun_set_current_map(saved);
    champ_compute_load(index);
    return index + 1;
}

void champ_finish_recruit(int x, int y, int dir, int map, int command)
{
    int champ = ui.candidate - 1;
    Champion *c = champion(champ);
    int saved = party.cur_map;
    dun_set_current_map(map);
    uint16_t hand = champ_remove_object_from_leader_hand();
    if (command == CMD_RECRUIT_CANCEL) {
        /* the object in the hand is lost, as in the original */
        inv_toggle(CHAMPION_MAX);
        if (game.champion_count == 1) champ_set_leader(-1);
        if (ui.candidate >= game.champion_count) game.champion_count--;
        ui.candidate = 0;
        c->health = 0;
        action_reset_champion(champ);
        gfx_fill_zone_screen(ZONE_STATUS_BOX + champ, ui_colors[UI_BLACK]);
        ui_show_game_view();
        dun_set_current_map(saved);
        return;
    }
    gfx_draw_gdat_in_viewport_zone(MOUTH_CATEGORY, MOUTH_INDEX, MOUTH_OPEN, ZONE_INVENTORY_PANEL, UI_OPAQUE);
    screen_present_viewport();
    if (hand != THING_NONE) inv_put_object_in_free_slot(champ, hand);
    ui.candidate = 0;
    int fx = x + dir_dx[dir], fy = y + dir_dy[dir];
    for (int s = 0; s < SLOT_COUNT; s++)
        if (c->slots[s] != THING_NONE) dungeon_unlink_thing_from_square(c->slots[s], party.cur_map, fx, fy);
    uint16_t mirror = mirror_sensor(fx, fy);
    if (mirror != THING_NONE)
        set_thing_word(mirror, SENSOR_FLAGS, thing_word(mirror, SENSOR_FLAGS) & (uint16_t)~SENSOR_MIRROR_ACTIVE);
    if (game.champion_count == 1) game.first_recruit_time = game.game_time;
    text_args.champion = (int16_t)champ;
    text_print_newline();
    char text[TEXT_SIZE];
    text_print_message(message_colors[champ], text_gdat(CAT_INTERFACE, 0, TEXT_JOINED, text));
    if (ui.inventory) {
        inv_toggle(CHAMPION_MAX);
        ui_show_game_view();
    }
    dun_set_current_map(saved);
}
