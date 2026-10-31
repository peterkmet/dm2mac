/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "recruit.h"
#include <string.h>
#include "action.h"
#include "champion.h"
#include "dungeon.h"
#include "game_text.h"
#include "gdat.h"
#include "gfx.h"
#include "hooks.h"
#include "ids.h"
#include "interface_state.h"
#include "le.h"
#include "random.h"
#include "sensor.h"
#include "sound_request.h"

/* The sensor flag of a mirror that still holds its champion. */
#define MIRROR_ACTIVE         SENSOR_STATE
/* The GDAT item type of the champion data (0x16, portrait, 8, 0): health, stamina, mana, statistics, skills. */
#define PORTRAIT_DATA_TYPE    8
/* Word indices of the champion data; skill s at DATA_SKILLS + s. */
#define DATA_HEALTH           0
#define DATA_STAMINA          1
#define DATA_MANA             2
#define DATA_STATS            3
#define DATA_SKILLS           6
#define DATA_WORD_SIZE        2 /* bytes per word of the champion data */
/* The 52 bytes of the champion data that add_to_party reads. */
#define DATA_SIZE             (DATA_WORD_SIZE * (DATA_SKILLS + SKILL_COUNT))
/* The skill level is taken modulo 32 for its shift, as in the original. */
#define SKILL_LEVEL_MASK      0x1f
/* Start values of a new champion. */
#define STAT_START_MIN        30
#define SKILL_EXPERIENCE_BASE 64
#define FOOD_START            1500
/* A new champion gets up to 255 more food and water. */
#define FOOD_RANDOM_MASK      0xff
/* The sound priority of the mouth of a recruit. */
#define MOUTH_SOUND_PRIORITY  0xff

/* The colors of the champion messages. */
static const uint8_t message_colors[CHAMPION_MAX] = {UI_GREEN, UI_YELLOW, UI_RED, UI_BLUE};

/* The champion of a new game joins without the inventory, the mouth, and the message. */
static bool recruit_silent;

/* Makes champion index the champion of portrait, facing dir, in the first free cell. */
static void add_to_party(int portrait, int index, int16_t dir)
{
    Champion *c = champion(index);
    memset(c, 0, sizeof *c);
    c->portrait = (uint8_t)portrait;
    c->last_command[SLOT_READY_HAND] = COMMAND_NONE;
    c->last_command[SLOT_ACTION_HAND] = COMMAND_NONE;
    c->damage_event = THING_NONE;
    c->dir = (uint8_t)dir;
    int k = 0;
    while (champ_get_index_in_cell(dir_turn(g_game.party_dir, k)) != -1)
        k++;
    c->cell = (uint8_t)dir_turn(g_game.party_dir, k);
    c->hit_dir = (uint8_t)g_game.party_dir;
    for (int s = 0; s < SLOT_COUNT; s++)
        c->slots[s] = THING_NONE;
    char text[TEXT_SIZE];
    const char *p = gtext_gdat(CAT_CHAMPION, portrait, GFX_PORTRAIT_TEXT, text);
    char ch;
    /* the name ends at a space or after 7 characters; the 8th character is lost, as in the original */
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
    if (!d || size < DATA_SIZE)
        return; /* fix: the original reads no data, or past a short item */
    c->health = (int16_t)le16(d + DATA_WORD_SIZE * DATA_HEALTH);
    c->max_health = c->health;
    c->stamina = (int16_t)le16(d + DATA_WORD_SIZE * DATA_STAMINA);
    c->max_stamina = c->stamina;
    c->mana = (int16_t)le16(d + DATA_WORD_SIZE * DATA_MANA);
    c->max_mana = c->mana;
    for (int s = 0; s < STAT_COUNT; s++) {
        int16_t v = (int16_t)le16(d + DATA_WORD_SIZE * (DATA_STATS + s));
        c->stats[s][STAT_CURRENT] = (uint8_t)(v > STAT_START_MIN ? v : STAT_START_MIN);
        c->stats[s][STAT_MAXIMUM] = c->stats[s][STAT_CURRENT];
    }
    for (int s = SKILL_BASE_COUNT; s < SKILL_COUNT; s++) {
        uint16_t level = le16(d + DATA_WORD_SIZE * (DATA_SKILLS + s));
        c->experience[s] = level ? (uint32_t)SKILL_EXPERIENCE_BASE << (level & SKILL_LEVEL_MASK) : 0;
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

/* The mirror sensor on the square at of the current map; its data is the portrait. */
static uint16_t mirror_sensor(Location at)
{
    for (uint16_t t = dungeon_first_thing_at(at); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        if (thing_type(t) == THING_SENSOR && sensor_type(t) == WALL_CLICK_PORTRAIT)
            return t;
    return THING_NONE;
}

bool recruit_start(Position at, int16_t dir)
{
    int index = g_game.champion_count;
    if (g_game.leader_hand != THING_NONE || index >= CHAMPION_MAX)
        return false;
    int16_t saved = g_current_map.map;
    dungeon_set_current_map(at.map);
    Location front = dungeon_step((Location){at.x, at.y}, dir);
    uint16_t mirror = mirror_sensor(front);
    if (mirror == THING_NONE) { /* fix: the original then takes an uninitialized portrait */
        dungeon_set_current_map(saved);
        return false;
    }
    int portrait = (uint8_t)sensor_data(mirror);
    add_to_party(portrait, index, dir);
    if (index) {
        action_area_close();
        g_hooks.update_action_area(false);
    }
    g_ui.candidate = (uint16_t)(index + 1);
    g_game.champion_count++;
    if (!index)
        champ_set_leader(0);
    int back = dir_opposite(dir);
    for (uint16_t t = dungeon_first_thing_at(front); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
        if (thing_type(t) > THING_SENSOR && thing_cell(t) == back)
            inv_put_in_free_slot(index, t);
    if (!recruit_silent) {
        g_hooks.toggle_inventory(index);
        g_hooks.set_input_mode(INPUT_MODE_RECRUIT);
        g_hooks.hide_view();
    }
    dungeon_set_current_map(saved);
    champ_compute_load(index);
    return true;
}

void recruit_finish(Position at, int16_t dir, int command)
{
    if (!g_ui.candidate)
        return; /* fix: without a candidate the original uses champion record 0xffff */
    int16_t champion_index = g_ui.candidate - 1;
    Champion *c = champion(champion_index);
    int16_t saved = g_current_map.map;
    dungeon_set_current_map(at.map);
    uint16_t hand = champ_take_from_leader_hand();
    if (command == CMD_RECRUIT_CANCEL) {
        /* the object in the hand is lost, as in the original */
        g_hooks.close_inventory(true);
        if (g_game.champion_count == 1)
            champ_set_leader(-1);
        if (g_ui.candidate >= g_game.champion_count)
            g_game.champion_count--;
        g_ui.candidate = 0;
        c->health = 0;
        action_reset_champion(champion_index);
        g_hooks.fill_screen_zone(ZONE_STATUS_BOX + champion_index, g_ui_colors[UI_BLACK]);
        g_hooks.show_view();
        dungeon_set_current_map(saved);
        return;
    }
    if (!recruit_silent) {
        g_hooks.draw_viewport_image(CAT_WALL_ORNAMENT, MOUTH_ORNAMENT, GFX_MOUTH_OPEN, ZONE_INVENTORY_PANEL,
                                    GFX_OPAQUE);
        g_hooks.present_viewport();
        sndreq_request(
            (SoundCue){CAT_WALL_ORNAMENT, MOUTH_ORNAMENT, GFX_MOUTH_OPEN, MOUTH_SOUND_PRIORITY, SOUND_VOLUME_LOUD},
            g_game.party_x, g_game.party_y, SOUND_NOW);
    }
    if (hand != THING_NONE)
        inv_put_in_free_slot(champion_index, hand);
    g_ui.candidate = 0;
    Location front = dungeon_step((Location){at.x, at.y}, dir);
    for (int s = 0; s < SLOT_COUNT; s++)
        if (c->slots[s] != THING_NONE)
            dungeon_unlink_thing_from_square(c->slots[s], g_current_map.map, front.x, front.y);
    uint16_t mirror = mirror_sensor(front);
    if (mirror != THING_NONE)
        sensor_set_flag(mirror, MIRROR_ACTIVE, false);
    if (g_game.champion_count == 1)
        g_game.first_recruit_time = g_game.game_time;
    if (!recruit_silent) {
        g_text_args.champion = (int16_t)champion_index;
        g_hooks.print_newline();
        char text[TEXT_SIZE];
        g_hooks.print_message(message_colors[champion_index], gtext_gdat(CAT_INTERFACE, 0, TEXT_JOINED, text));
        if (g_ui.inventory) {
            g_hooks.close_inventory(true);
            g_hooks.show_view();
        }
    }
    dungeon_set_current_map(saved);
}

bool recruit_start_champion(int16_t x, int16_t y, int16_t dir, int16_t map)
{
    recruit_silent = true;
    bool started = recruit_start((Position){map, x, y}, dir);
    if (started)
        recruit_finish((Position){map, x, y}, dir, CMD_RECRUIT);
    recruit_silent = false;
    return started;
}
