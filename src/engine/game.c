/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The rules of the main loop: the timeline and the map changes. */
#include "game.h"
#include <stdbool.h>
#include <string.h>
#include "ai.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "hooks.h"
#include "le.h"
#include "magic.h"
#include "move.h"
#include "projectile.h"
#include "random.h"
#include "save.h"
#include "sensor.h"
#include "sound_request.h"
#include "weather.h"

/* The periods of the party clocks: a game step runs a clock when the game time has no bit of its mask. */
#define TIME_EFFECTS_PERIOD_MASK       0x03f /* the champion clocks run each 64 game steps */
#define SLEEP_TIME_EFFECTS_PERIOD_MASK 0x00f /* and each 16 game steps while the party sleeps */
#define LIGHT_BURN_PERIOD_MASK         0x1ff /* held lights burn down each 512 game steps */

MainLoop g_main_loop = {.tick_threshold = STEP_TICKS, .running = true};
UiState g_ui;
bool g_input_command_stop;
int g_input_drag_state;
int g_input_mouse_buttons;
Session g_session = {.slot = SAVE_SLOT_NONE};
/* The cheat bits that are on (CHEAT_*). */
static uint8_t cheats;
enum GameMode g_game_mode = GAME_MODE_VANILLA;
const char *const g_game_mode_names[GAME_MODE_COUNT] = {"vanilla", "advanced", "turnbased"};

void game_enter_map(int16_t x, int16_t y, int16_t map, bool leaving)
{
    if (leaving)
        sensor_process_map_entry(false);
    dun_set_party_location((Position){map, x, y});
    g_hooks.enter_map_view(map);
    world_update_day_night(0);
    sensor_process_map_entry(true);
    creature_activate_groups_on_map();
    g_party.redraw |= REDRAW_SQUARES;
    ai_refresh_party_maps();
    g_hooks.flush_input();
}

/* The damage number of champion champ stops showing; the bars redraw. */
static void hide_damage(int champ)
{
    Champion *c = champion(champ);
    c->damage_event = THING_NONE;
    if (c->health)
        c->redraw |= REDRAW_BARS;
}

/* The aura of the champions in mask weakens by amount. */
static void weaken_aura(int mask, int amount)
{
    for (int i = 0; i < g_game.champion_count; i++) {
        Champion *c = champion(i);
        if (mask >> i & 1 && c->health)
            c->aura_value = (int16_t)(c->aura_value - amount);
        if (c->aura_value < 0)
            c->aura_value = 0;
    }
}

/* Runs a timeline event of the dungeon: doors, squares, sensors, creatures, projectiles, and the weather; false for
   another event type. */
static bool process_world_event(TimelineEvent *e)
{
    switch (e->type) {
    case EVENT_DOOR_ANIMATION:
        sensor_event_door_animation(e);
        break;
    case EVENT_DOOR_DESTRUCTION:
        sensor_event_door_destruction(e);
        break;
    case EVENT_SQUARE:
        sensor_event_square(e);
        break;
    case EVENT_ALTAR_REBIRTH:
        timeline_event_altar_rebirth(e);
        break;
    case EVENT_EXPLOSION:
        explosion_process_event(e);
        break;
    case EVENT_PROJECTILE_MOVE:
    case EVENT_PROJECTILE_MOVE_2:
        projectile_process_event(e);
        break;
    case EVENT_CREATURE_ANIMATION:
    case EVENT_CREATURE_THINK:
        creature_process_event(e->data[EVENT_X], e->data[EVENT_Y], e->type);
        break;
    case EVENT_MOVE_GROUP:
    case EVENT_MOVE_GROUP_2:
        timeline_event_move_group(e);
        break;
    case EVENT_WEATHER:
        world_update_day_night(1);
        break;
    case EVENT_SENSOR_ANIMATION_STEP:
    case EVENT_SENSOR_TIMER:
    case EVENT_SENSOR_IDLE:
    case EVENT_DOOR_BUTTON_RELEASE:
    case EVENT_SENSOR_ANIMATION_END:
    case EVENT_SENSOR_AMBIENT_SOUND:
    case EVENT_SENSOR_READY:
    case EVENT_SET_MARKER:
        sensor_process_event(e);
        break;
    case EVENT_SPAWN_CREATURE: {
        int dir = rand_2bits()
                      ? dun_get_direction_to(e->data[EVENT_X], e->data[EVENT_Y], g_game.party_x, g_game.party_y)
                      : rand_2bits();
        creature_create_group(e->data[SPAWN_EVENT_CREATURE], GROUP_COUNT_FULL_HEALTH, dir, e->data[EVENT_X],
                              e->data[EVENT_Y]);
        break;
    }
    default:
        return false;
    }
    return true;
}

/* Runs a timeline event of the party: teleports, damage numbers, item effects, sounds, light, invisibility, aura, and poison. */
static void process_party_event(TimelineEvent *e, uint16_t word)
{
    switch (e->type) {
    case EVENT_TELEPORT_PARTY:
        if (e->data[TELEPORT_EVENT_MAP] == g_game.party_map) {
            move_thing(THING_NONE, g_game.party_x, g_game.party_y, location_x(word), location_y(word));
            champ_set_party_direction(location_dir(word));
        }
        break;
    case EVENT_HIDE_DAMAGE:
        hide_damage(e->priority);
        break;
    case EVENT_ITEM_EFFECT:
        timeline_event_apply_item_effect(e, EFFECT_MODE_CONSUME_END);
        break;
    case EVENT_DELAYED_SOUND:
        sndreq_request_delayed(word);
        break;
    case EVENT_MAGIC_LIGHT:
        dun_set_current_map(g_game.party_map);
        magic_event_light(e);
        light_update_party_light();
        break;
    case EVENT_INVISIBILITY: /* invisibility ends */
        if (!--g_game.magic_light[MAGIC_INVISIBILITY] && g_ui.inventory)
            champion(g_ui.inventory - 1)->redraw |= REDRAW_STATUS_BOX;
        break;
    case EVENT_AURA:
        weaken_aura(e->priority, (int16_t)word);
        break;
    case EVENT_POISON: {
        Champion *c = champion(e->priority);
        c->poisoned--;
        c->poison = (int16_t)(c->poison - word);
        champ_poison(e->priority, word);
        break;
    }
    default:
        break;
    }
}

void game_process_timeline(void)
{
    while (timeline_is_event_due(g_game.game_time)) {
        g_hooks.poll_input();
        TimelineEvent e;
        timeline_pop_first_event(&e);
        dun_set_current_map((int8_t)timeline_event_map(&e));
        uint16_t word = le16(e.data);
        if (!process_world_event(&e))
            process_party_event(&e, word);
    }
    dun_set_current_map(g_game.party_map);
}

void game_enter_new_map(void)
{
    game_enter_map(g_game.party_x, g_game.party_y, g_party.new_map, true);
    g_party.move_steps = 0;
    move_thing(THING_NONE, -1, 0, g_game.party_x, g_game.party_y);
    g_party.new_map = -1;
}

void game_run_party_clocks(void)
{
    if (g_game.magic_light[MAGIC_HASTE])
        g_game.magic_light[MAGIC_HASTE]--;
    if (!(g_game.game_time & (g_party_sleeping ? SLEEP_TIME_EFFECTS_PERIOD_MASK : TIME_EFFECTS_PERIOD_MASK)))
        champ_apply_time_effects();
}

void game_advance_time(void)
{
    g_game.game_time++;
    if (!(g_game.game_time & LIGHT_BURN_PERIOD_MASK))
        champ_burn_held_lights();
    if (g_game.magic_light[MAGIC_ACTION_EFFECT])
        g_game.magic_light[MAGIC_ACTION_EFFECT]--;
    if (g_game.party_move_cooldown)
        g_game.party_move_cooldown--;
    if (g_game.throw_cooldown)
        g_game.throw_cooldown--;
}

void game_global_byte_add(int index)
{
    uint8_t *value = g_game.global_bytes + index - GLOBAL_BYTE_FIRST;
    if (*value < GLOBAL_BYTE_MAX)
        ++*value;
}

void game_start_play(void)
{
    g_party_sleeping = false;
    g_party.push_mode = 0;
    g_main_loop.view_hidden = false;
    g_main_loop.tick_threshold = STEP_TICKS;
    g_main_loop.running = true;
}

void game_set_running(bool on)
{
    g_main_loop.running = on;
}

void game_set_view_hidden(bool on)
{
    g_main_loop.view_hidden = on;
}

void game_set_step_ticks(uint16_t ticks)
{
    g_main_loop.tick_threshold = ticks;
}

void game_count_timer_tick(void)
{
    g_main_loop.timer_ticks++;
}

bool game_count_step_tick(void)
{
    return ++g_main_loop.tick_count >= g_main_loop.tick_threshold;
}

void game_reset_step_ticks(void)
{
    g_main_loop.tick_count = 0;
}

void game_clear_end(void)
{
    g_main_loop.end = false;
}

void game_set_end(bool on)
{
    g_main_loop.end = on;
}

void game_set_closed(bool on)
{
    g_main_loop.closed = on;
}

void game_request_quit(void)
{
    g_main_loop.quit = true;
}

void game_clear_ui_state(void)
{
    memset(&g_ui, 0, sizeof g_ui);
}

void game_set_screen_refresh(bool on)
{
    g_ui.screen_needs_refresh = on;
}

void game_set_inventory_buttons(uint16_t buttons)
{
    g_ui.buttons = buttons;
}

void game_set_panel_mode(enum PanelMode mode)
{
    g_ui.panel_mode = (int16_t)mode;
}

void game_set_open_inventory(int inventory)
{
    g_ui.inventory = (int16_t)inventory;
}

void game_set_eye_held(bool on)
{
    g_ui.eye = on;
}

void game_release_eye_and_mouth(void)
{
    g_ui.eye = false;
    g_ui.mouth = false;
}

void game_set_command_stop(bool on)
{
    g_input_command_stop = on;
}

void game_clear_drag(void)
{
    g_input_drag_state = 0;
}

void game_set_mouse_buttons(int pressed)
{
    g_input_mouse_buttons = pressed;
}

void game_set_new_game(int choice)
{
    g_session.new_game = choice;
}

void game_set_dungenb(bool on)
{
    g_session.use_dungenb = on;
}

void game_set_session_loaded(bool on)
{
    g_session.loaded = on;
}

void game_set_save_slot(int16_t slot)
{
    g_session.slot = slot;
}

void game_note_slot_loaded(void)
{
    g_session.new_game = 0;
    g_session.load_time = g_game.game_time;
    g_session.loaded = true;
}

void game_note_slot_saved(void)
{
    g_session.new_game = 0;
    g_session.save_time = g_game.game_time;
}

void game_clear_save_times(void)
{
    g_session.load_time = 0;
    g_session.save_time = 0;
}

void game_set_restart(bool on)
{
    g_session.restart = on;
}

void game_set_offer_restore(bool on)
{
    g_session.offer_restore = on;
}

void game_set_completed(bool on)
{
    g_session.completed = on;
}

void game_set_quit_code(int code)
{
    g_session.quit_code = code;
}

void game_set_exit_code(int code)
{
    g_session.exit_code = code;
}

void game_restore_session(const Session *session)
{
    g_session = *session;
}

uint8_t game_get_cheats(void)
{
    return cheats;
}

void game_set_cheats(uint8_t bits)
{
    cheats = bits & CHEAT_ALL;
}

void game_toggle_cheat(uint8_t cheat)
{
    game_set_cheats(cheats ^ cheat);
}

bool game_has_cheat(uint8_t cheat)
{
    return cheats & cheat;
}
