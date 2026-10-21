/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Recruiting a champion from its mirror: the candidate joins the party or goes back. */
#include "gdat.h"
#include "ids.h"
#include "le.h"
#include "action.h"
#include "champion.h"
#include "dungeon.h"
#include "game.h"
#include "game_text.h"
#include "hooks.h"
#include "inventory.h"
#include "item.h"
#include "move.h"
#include "random.h"
#include "sensor.h"
#include "sound_request.h"
#include <string.h>

#define SENSOR_MIRROR         0x7e         /* the wall sensor of a champion mirror; its data is the portrait */
#define SENSOR_MIRROR_ACTIVE  SENSOR_STATE /* the mirror still holds its champion */
#define PORTRAIT_DATA_TYPE    8 /* GDAT item (0x16, portrait, 8, 0): health, stamina, mana, statistics, skills */
#define STAT_START_MIN        30
#define SKILL_EXPERIENCE_BASE 64
#define FOOD_START            1500
#define FOOD_RANDOM_MASK      0xff /* a new champion gets up to 255 more food and water */
#define MOUTH_SOUND_PRIORITY  0xff
#define MESSAGE_COLOR_FIRST   UI_GREEN
enum {
    DATA_HEALTH,
    DATA_STAMINA,
    DATA_MANA,
    DATA_STATS,
    DATA_SKILLS = 6
}; /* word indices; skill s at DATA_SKILLS + s */

/* The colors of the champion messages. */
static const uint8_t message_colors[CHAMPION_MAX] = {UI_GREEN, UI_YELLOW, UI_RED, UI_BLUE};

/* The champion of a new game joins without the inventory, the mouth, and the message (the original flag 0x75a29). */
static int recruit_silent;

/* Makes champion index the champion of portrait, facing dir, in the first free cell. */
static void champ_add_to_party(int portrait, int index, int dir)
{
    Champion *c = champion(index);
    memset(c, 0, sizeof *c);
    c->portrait = (uint8_t)portrait;
    c->last_command[0] = c->last_command[1] = COMMAND_NONE;
    c->damage_event = THING_NONE;
    c->dir = (uint8_t)dir;
    int k = 0;
    while (champ_get_index_in_cell(dir_turn(game.party_dir, k)) != -1)
        k++;
    c->cell = (uint8_t)dir_turn(game.party_dir, k);
    c->hit_dir = (uint8_t)game.party_dir;
    for (int s = 0; s < SLOT_COUNT; s++)
        c->slots[s] = THING_NONE;
    char text[TEXT_SIZE];
    const char *p = text_gdat(CAT_CHAMPION, portrait, GFX_PORTRAIT_TEXT, text);
    char ch;
    /* the name ends at a space or after 7 characters; the title follows (the 8th character of a long name is lost) */
    for (int i = 0;; i++) {
        ch = *p++;
        if (ch == ' ' || !ch || i >= CHAMPION_NAME_SIZE - 1)
            break;
        c->name[i] = ch;
    }
    for (int i = 0; ch && (ch = *p++) && i < CHAMPION_TITLE_SIZE - 1; i++)
        c->title[i] = ch;
    uint32_t size;
    const uint8_t *d =
        gdat_item(gdat_current(), gdat_find(gdat_current(), CAT_CHAMPION, portrait, PORTRAIT_DATA_TYPE, 0), &size);
    if (!d)
        return; /* fix: the original reads no data */
    c->health = c->max_health = (int16_t)le16(d + 2 * DATA_HEALTH);
    c->stamina = c->max_stamina = (int16_t)le16(d + 2 * DATA_STAMINA);
    c->mana = c->max_mana = (int16_t)le16(d + 2 * DATA_MANA);
    for (int s = 0; s < STAT_COUNT; s++) {
        int16_t v = (int16_t)le16(d + 2 * (DATA_STATS + s));
        c->stats[s][0] = c->stats[s][1] = (uint8_t)(v > STAT_START_MIN ? v : STAT_START_MIN);
    }
    for (int s = SKILL_BASE_COUNT; s < SKILL_COUNT; s++) {
        uint16_t level = le16(d + 2 * (DATA_SKILLS + s));
        c->experience[s] = level ? (uint32_t)SKILL_EXPERIENCE_BASE << level : 0;
    }
    for (int b = 0; b < SKILL_BASE_COUNT; b++) {
        uint32_t sum = 0;
        for (int k2 = 0; k2 < SUB_SKILL_COUNT; k2++)
            sum += c->experience[(b + 1) * SUB_SKILL_COUNT + k2];
        c->experience[b] = sum;
    }
    c->food = (int16_t)((rand_next() & FOOD_RANDOM_MASK) + FOOD_START);
    c->water = (int16_t)((rand_next() & FOOD_RANDOM_MASK) + FOOD_START);
}

/* The mirror sensor on the square (x, y) of the current map. */
static uint16_t mirror_sensor(int x, int y)
{
    for (uint16_t t = dungeon_square_first_thing(party.cur_map, x, y); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        if (thing_type(t) == THING_SENSOR && sensor_type(t) == SENSOR_MIRROR)
            return t;
    return THING_NONE;
}

int champ_start_recruit(int x, int y, int dir, int map)
{
    int index = game.champion_count;
    if (game.leader_hand != THING_NONE || index >= CHAMPION_MAX)
        return 0;
    int saved = party.cur_map;
    dun_set_current_map(map);
    int fx = x + dir_dx[dir], fy = y + dir_dy[dir];
    uint16_t mirror = mirror_sensor(fx, fy);
    if (mirror == THING_NONE) { /* fix: the original then takes an uninitialized portrait */
        dun_set_current_map(saved);
        return 0;
    }
    int portrait = (uint8_t)sensor_data(mirror);
    champ_add_to_party(portrait, index, dir);
    if (index) {
        action_area_close();
        g_hooks.update_action_area(0);
    }
    ui.candidate = (uint16_t)(index + 1);
    game.champion_count++;
    if (!index)
        champ_set_leader(0);
    int back = dir_opposite(dir);
    for (uint16_t t = dungeon_square_first_thing(party.cur_map, fx, fy); t != THING_ENDOFLIST;
         t = dungeon_next_thing(t))
        if (thing_type(t) > THING_SENSOR && thing_cell(t) == back)
            inv_put_object_in_free_slot(index, t);
    if (!recruit_silent) {
        g_hooks.toggle_inventory(index);
        g_hooks.set_input_mode(INPUT_MODE_RECRUIT);
        g_hooks.hide_view();
    }
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
        g_hooks.toggle_inventory(CHAMPION_MAX);
        if (game.champion_count == 1)
            champ_set_leader(-1);
        if (ui.candidate >= game.champion_count)
            game.champion_count--;
        ui.candidate = 0;
        c->health = 0;
        action_reset_champion(champ);
        g_hooks.fill_screen_zone(ZONE_STATUS_BOX + champ, ui_colors[UI_BLACK]);
        g_hooks.show_view();
        dun_set_current_map(saved);
        return;
    }
    if (!recruit_silent) {
        g_hooks.draw_viewport_image(MOUTH_CATEGORY, MOUTH_INDEX, GFX_MOUTH_OPEN, ZONE_INVENTORY_PANEL, UI_OPAQUE);
        g_hooks.present_viewport();
        sound_request(MOUTH_CATEGORY, MOUTH_INDEX, GFX_MOUTH_OPEN, MOUTH_SOUND_PRIORITY, SOUND_VOLUME_LOUD,
                      game.party_x, game.party_y, 0);
    }
    if (hand != THING_NONE)
        inv_put_object_in_free_slot(champ, hand);
    ui.candidate = 0;
    int fx = x + dir_dx[dir], fy = y + dir_dy[dir];
    for (int s = 0; s < SLOT_COUNT; s++)
        if (c->slots[s] != THING_NONE)
            dungeon_unlink_thing_from_square(c->slots[s], party.cur_map, fx, fy);
    uint16_t mirror = mirror_sensor(fx, fy);
    if (mirror != THING_NONE)
        sensor_set_flag(mirror, SENSOR_MIRROR_ACTIVE, false);
    if (game.champion_count == 1)
        game.first_recruit_time = game.game_time;
    if (!recruit_silent) {
        text_args.champion = (int16_t)champ;
        g_hooks.print_newline();
        char text[TEXT_SIZE];
        g_hooks.print_message(message_colors[champ], text_gdat(CAT_INTERFACE, 0, TEXT_JOINED, text));
        if (ui.inventory) {
            g_hooks.toggle_inventory(CHAMPION_MAX);
            g_hooks.show_view();
        }
    }
    dun_set_current_map(saved);
}

void champ_recruit_start_champion(int x, int y, int dir, int map)
{
    recruit_silent = 1;
    champ_start_recruit(x, y, dir, map);
    champ_finish_recruit(x, y, dir, map, CMD_RECRUIT);
    recruit_silent = 0;
}
