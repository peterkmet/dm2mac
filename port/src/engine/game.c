/* The main loop: game steps, the timeline, map changes. */
#include <string.h>
#include "action.h"
#include "advanced.h"
#include "automap.h"
#include "champ_action.h"
#include "champion.h"
#include "creature.h"
#include "dialog.h"
#include "draw.h"
#include "dungeon.h"
#include "game.h"
#include "gdat.h"
#include "hooks.h"
#include "input.h"
#include "inventory.h"
#include "le.h"
#include "magic.h"
#include "move.h"
#include "music.h"
#include "random.h"
#include "save.h"
#include "sensor.h"
#include "session.h"
#include "text.h"
#include "timeline.h"
#include "ui.h"
#include "view.h"
#include "weather.h"
#include "zone.h"

#define REDRAW_INVENTORY_BOX 0x4000
#define CHAMP_DAMAGE_SHOWN 0xffff
#define TIME_EFFECTS_PERIOD_MASK 0x3f       /* the champion clocks run each 64 game steps */
#define SLEEP_TIME_EFFECTS_PERIOD_MASK 0x0f /* and each 16 game steps while the party sleeps */
#define LIGHT_BURN_PERIOD_MASK 0x1ff        /* held lights burn down each 512 game steps */
#define EVENT_TELEPORT_MAP 2                /* teleport party event: data byte of the map */
#define TELEPORT_DIR_MASK 3                 /* teleport party event: the direction in the map bits of its location word */
#define EVENT_SPAWN_TYPE 3                  /* spawn creature event: data byte of the creature type */

MainLoop main_loop = {.tick_threshold = STEP_TICKS, .running = 1};
static int tick_elapsed;
void (*game_frame_hook)(void);

void game_wait_frames(int count)
{
    while (count-- > 0) {
        main_loop.timer_ticks++;
        if (game_frame_hook) game_frame_hook();
    }
}

void screen_present_viewport(void)
{
    int steps = party.move_steps;
    int16_t r[RECT_SIZE];
    party.move_steps = 0;
    int ok = zone_get_rect(ZONE_VIEWPORT, r) != NULL;
    party.move_steps = (int16_t)steps;
    if (!ok) return;
    for (int y = 0; y < VIEWPORT_HEIGHT; y++) {
        uint8_t *dst = draw_screen.pixels + (r[RECT_Y] + y) * draw_screen.width + r[RECT_X];
        if (!steps) {
            memcpy(dst, view_viewport.pixels + y * VIEWPORT_WIDTH, VIEWPORT_WIDTH);
            continue;
        }
        /* While the party walks, the visible part of the view fills the whole viewport (nearest neighbour). */
        const uint8_t *src = view_viewport.pixels + (walk_view_rect[RECT_Y] + y * walk_view_rect[RECT_H] / VIEWPORT_HEIGHT) * VIEWPORT_WIDTH;
        for (int x = 0; x < VIEWPORT_WIDTH; x++) dst[x] = src[walk_view_rect[RECT_X] + x * walk_view_rect[RECT_W] / VIEWPORT_WIDTH];
    }
}

static void ui_draw_sleep_message(void)
{
    char text[TEXT_SIZE];
    gfx_fill_bitmap(&view_viewport, ui_colors[UI_BLACK]);
    gdat_text(draw_gdat(), CAT_INTERFACE, 0, TEXT_SLEEPING, text, sizeof text);
    text_draw_in_zone(ZONE_VIEW_TEXT, ui_colors[UI_CYAN], text);
}

void ui_show_game_view(void)
{
    if (!main_loop.view_hidden || dialog.screen_mode) return;
    if (party_sleeping) {
        ui_draw_sleep_message();
        screen_present_viewport();
        return;
    }
    main_loop.view_hidden = 0;
    if (!action.champion) action.dirty = 1;
    else action.redraw_delay = 1;
    if (ui.inventory) {
        int champ = ui.inventory - 1;
        ui.inventory = 0;
        inv_toggle(champ);
        return;
    }
    view_clear_viewport();
    ui_draw_movement_arrows();
}

void ui_hide_game_view(void)
{
    if (main_loop.view_hidden || dialog.screen_mode) return;
    main_loop.view_hidden = 1;
    if (input_drag_state) {
        champ_drag_cancel();
        ui_update_action_area(0);
    }
    input_cancel_push_mode();
    ui_release_move_arrow();
}

void champ_wake_party(void)
{
    input_command_stop = 1;
    party_sleeping = 0;
    main_loop.tick_threshold = STEP_TICKS;
    view_clear_viewport();
    input_set_mode(INPUT_MODE_MOVE);
}

void game_sleep(void)
{
    if (ui.candidate) return;
    inv_arrows_on_close = 0;
    if (ui.inventory) inv_toggle(CHAMPION_MAX);
    inv_arrows_on_close = 1;
    ui_action_area_close();
    ui_hide_game_view();
    party_sleeping = 1;
    if (party.move_steps) {
        move_party(party.move_command);
        party.move_steps = 0;
    }
    ui_draw_sleep_message();
    screen_present_viewport();
    main_loop.tick_threshold = SLEEP_STEP_TICKS;
    input_set_mode(INPUT_MODE_SLEEP);
    input_flush();
}

void game_enter_map(int x, int y, int map, int leaving)
{
    if (leaving) sensor_process_map_entry(0);
    dun_set_party_location(x, y, map);
    view_enter_map(map);
    world_update_day_night(0);
    sensor_process_map_entry(1);
    creature_activate_groups_on_map();
    party.redraw |= REDRAW_SQUARES;
    creature_refresh_party_maps();
    input_flush();
}

static void timeline_event_hide_damage(int champ)
{
    Champion *c = champion(champ);
    c->damage_event = CHAMP_DAMAGE_SHOWN;
    if (c->health) c->redraw |= REDRAW_BARS;
}

/* The aura of the champions in mask weakens by amount. */
static void timeline_event_aura(int mask, int amount)
{
    for (int i = 0; i < game.champion_count; i++) {
        Champion *c = champion(i);
        if (mask >> i & 1 && c->health) c->aura_value = (int16_t)(c->aura_value - amount);
        if (c->aura_value < 0) c->aura_value = 0;
    }
}

void timeline_process(void)
{
    while (timeline_is_event_due(game.game_time)) {
        input_poll();
        TimelineEvent e;
        timeline_pop_first_event(&e);
        dun_set_current_map((int8_t)timeline_event_map(&e));
        uint16_t word = le16(e.data);
        switch (e.type) {
        case EVENT_DOOR_ANIMATION: timeline_event_door_animation(&e); break;
        case EVENT_DOOR_DESTRUCTION: timeline_event_door_destruction(&e); break;
        case EVENT_SQUARE: timeline_event_square(&e); break;
        case EVENT_HIDE_DAMAGE: timeline_event_hide_damage(e.priority); break;
        case EVENT_ALTAR_REBIRTH: timeline_event_altar_rebirth(&e); break;
        case EVENT_ITEM_EFFECT: timeline_event_apply_item_effect(&e, EFFECT_MODE_CONSUME_END); break;
        case EVENT_DELAYED_SOUND: sound_play_delayed(word); break;
        case EVENT_EXPLOSION: explosion_process_event(&e); break;
        case EVENT_PROJECTILE_MOVE: case EVENT_PROJECTILE_MOVE_2: projectile_process_event(&e); break;
        case EVENT_CREATURE_ANIMATION: case EVENT_CREATURE_THINK: creature_process_event(e.data[EVENT_X], e.data[EVENT_Y], e.type); break;
        case EVENT_MOVE_GROUP: case EVENT_MOVE_GROUP_2: timeline_event_move_group(&e); break;
        case EVENT_MAGIC_LIGHT:
            dun_set_current_map(game.party_map);
            timeline_event_magic_light(&e);
            light_update_party_light();
            break;
        case EVENT_INVISIBILITY:  /* invisibility ends */
            if (!--game.magic_light[MAGIC_INVISIBILITY] && ui.inventory)
                champion(ui.inventory - 1)->redraw |= REDRAW_INVENTORY_BOX;
            break;
        case EVENT_AURA: timeline_event_aura(e.priority, (int16_t)word); break;
        case EVENT_POISON: {
            Champion *c = champion(e.priority);
            c->poisoned--;
            c->poison = (int16_t)(c->poison - word);
            champ_poison(e.priority, word);
            break;
        }
        case EVENT_WEATHER: world_update_day_night(1); break;
        case EVENT_SENSOR_ANIMATION_STEP: case EVENT_SENSOR_TIMER: case EVENT_SENSOR_IDLE: case EVENT_DOOR_BUTTON_RELEASE:
        case EVENT_SENSOR_ANIMATION_END: case EVENT_SENSOR_AMBIENT_SOUND: case EVENT_SENSOR_READY: case EVENT_SET_MARKER:
            timeline_event_sensor(&e);
            break;
        case EVENT_TELEPORT_PARTY:
            if (e.data[EVENT_TELEPORT_MAP] == game.party_map) {
                move_thing(THING_NONE, game.party_x, game.party_y, location_x(word), location_y(word));
                champ_set_party_direction(location_map(word) & TELEPORT_DIR_MASK);
            }
            break;
        case EVENT_SPAWN_CREATURE: {
            int dir = rand_2bits() ? dun_get_direction_to(e.data[EVENT_X], e.data[EVENT_Y], game.party_x, game.party_y)
                                     : rand_2bits();
            creature_create_group(e.data[EVENT_SPAWN_TYPE], GROUP_COUNT_FULL_HEALTH, dir, e.data[EVENT_X], e.data[EVENT_Y]);
            break;
        }
        }
    }
    dun_set_current_map(game.party_map);
}

/* The events, map changes, and view of a game step, with each step of a walk. Returns 0 while the party sleeps. */
static int game_step_view(void)
{
    for (;;) {
        if (party.new_map != -1) {
            game_enter_map(game.party_x, game.party_y, party.new_map, 1);
            party.move_steps = 0;
            move_thing(THING_NONE, -1, 0, game.party_x, game.party_y);
            party.new_map = -1;
        }
        timeline_process();
        if (party.new_map != -1) continue;
        world_update_day_night(0);
        if (party_sleeping) return 0;
        if (!ui.candidate) ui_show_game_view();
        if (!ui.inventory) {
            if (party.redraw) creature_refresh_party_maps();
            if (party.move_steps) view_draw_dungeon(party.view_dir, party.view_x, party.view_y);
            else view_draw_dungeon(game.party_dir, game.party_x, game.party_y);
            screen_present_viewport();
            if (game_mode == GAME_MODE_ADVANCED) automap_mark_seen(game.party_map, game.party_x, game.party_y, game.party_dir);
        }
        if (!party.move_steps) return 1;
        if (party.move_steps == 1) move_party(party.move_command);
        int map = party.new_map;
        party.move_steps--;
        if (map == -1) return 1;
    }
}

/* The part of a game step before the input wait: events, the view, the party clocks. */
static void game_step(void)
{
    tick_elapsed = 0;
    main_loop.tick_count = 0;
    if (game_step_view()) {
        input_feedback_busy = 0;
        input_poll();
    }
    ui_release_move_arrow();
    sound_flush_requests(0);
    champ_apply_pending_damage();
    if (game.magic_light[MAGIC_HASTE]) game.magic_light[MAGIC_HASTE]--;
    if (!(game.game_time & (party_sleeping ? SLEEP_TIME_EFFECTS_PERIOD_MASK : TIME_EFFECTS_PERIOD_MASK))) champ_apply_time_effects();
    inv_update_hand_cursor();
    champ_draw_all_states(1);
    if (main_loop.end) {
        input_reset_states();
        return;
    }
    game.game_time++;
    if (!(game.game_time & LIGHT_BURN_PERIOD_MASK)) items_burn_held_lights();
    if (game.magic_light[MAGIC_ACTION_EFFECT]) game.magic_light[MAGIC_ACTION_EFFECT]--;
    if (game.party_move_cooldown) game.party_move_cooldown--;
    if (game.throw_cooldown) game.throw_cooldown--;
    text_clear_expired_rows();
    ui_release_move_arrow();
}

/* The part of a game step after the input wait: a map change, repeated actions, music. */
static void game_step_end(void)
{
    int saved = party.cur_map;
    if (party.new_map != -1) dun_set_current_map(party.new_map);
    champ_process_repeated_actions();
    if (party.new_map != -1) dun_set_current_map(saved);
    music_update_for_map(game.party_map);
}

void game_start_session(void)
{
    party_sleeping = 0;
    party.push_mode = 0;
    main_loop.view_hidden = 0;
    main_loop.tick_threshold = STEP_TICKS;
    main_loop.running = 1;
    input_set_mode(INPUT_MODE_MOVE);
    game_enter_map(game.party_x, game.party_y, game.party_map, 0);
    dialog.progress_enabled = 0;
    advanced_reset();
    if (session.loaded) {
        gfx_fill_zone_screen(ZONE_SCREEN, 0);
        ui_dialog_run(ui_dialog_draw(DIALOG_LOADED, MESSAGE_NONE));
        gfx_fill_zone_screen(ZONE_SCREEN, 0);
    } else if (session.new_game) {
        dialog.screen_mode = 0;
        gfx_fill_zone_screen(ZONE_SCREEN, 0);
    }
    ui_draw_movement_arrows();
    champ_init_party_state();
    session.offer_restore = 1;
    creature_refresh_party_maps();
    creature_reset_active_groups();
    if (session.new_game) move_thing(THING_NONE, -1, 0, game.party_x, game.party_y);
    dialog.screen_mode = 0;
    game_step();
}

void game_tick(void)
{
    if (main_loop.end || main_loop.quit) return;
    main_loop.timer_ticks++;
    if (++main_loop.tick_count >= main_loop.tick_threshold) tick_elapsed = 1;
    command_process_queue();
    /* the automap stops the game time; a quit (a lost game) ends all game work */
    if (!tick_elapsed || !main_loop.running || advanced_automap_open || main_loop.quit) return;
    game_step_end();
    game_step();
}
