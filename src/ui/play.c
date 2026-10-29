/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The main loop: timer ticks, game steps, the view of the party, sleep. */
#include "play.h"
#include <stdbool.h>
#include <string.h>
#include "action.h"
#include "action_area.h"
#include "advanced.h"
#include "ai.h"
#include "champion.h"
#include "creature.h"
#include "dialog.h"
#include "draw.h"
#include "game.h"
#include "game_text.h"
#include "gdat.h"
#include "ids.h"
#include "input.h"
#include "move.h"
#include "music.h"
#include "random.h"
#include "seen.h"
#include "sound_request.h"
#include "text.h"
#include "ui.h"
#include "view.h"
#include "weather.h"
#include "zone.h"

static bool tick_elapsed; /* a game step is due */
void (*g_play_frame_hook)(void);

/* Modal loops */

void play_wait_frames(int count)
{
    while (count-- > 0) {
        game_count_timer_tick();
        if (g_play_frame_hook)
            g_play_frame_hook();
    }
}

/* The view of the party */

void play_present_viewport(void)
{
    int steps = g_party.move_steps;
    int16_t r[RECT_SIZE];
    if (!zone_get_viewport_rect(r))
        return;
    for (int y = 0; y < VIEWPORT_HEIGHT; y++) {
        uint8_t *dst = g_draw_screen.pixels + (r[RECT_Y] + y) * g_draw_screen.width + r[RECT_X];
        if (!steps) {
            memcpy(dst, g_view_viewport.pixels + y * VIEWPORT_WIDTH, VIEWPORT_WIDTH);
            continue;
        }
        /* While the party walks, the visible part of the view fills the whole viewport (nearest neighbour). */
        const uint8_t *src =
            g_view_viewport.pixels +
            (g_zone_walk_view_rect[RECT_Y] + y * g_zone_walk_view_rect[RECT_H] / VIEWPORT_HEIGHT) * VIEWPORT_WIDTH;
        for (int x = 0; x < VIEWPORT_WIDTH; x++)
            dst[x] = src[g_zone_walk_view_rect[RECT_X] + x * g_zone_walk_view_rect[RECT_W] / VIEWPORT_WIDTH];
    }
}

/* The sleep message on a black viewport. */
static void render_sleep_message(void)
{
    char text[TEXT_SIZE];
    gfx_fill_bitmap(&g_view_viewport, g_ui_colors[UI_BLACK]);
    gdat_text(gdat_current(), (GdatKey){CAT_INTERFACE, 0, TEXT_SLEEPING}, text, sizeof text);
    text_draw_in_zone(ZONE_VIEW_TEXT, g_ui_colors[UI_CYAN], text);
}

void play_show_view(void)
{
    if (!g_main_loop.view_hidden || g_dialog.screen_mode)
        return;
    if (g_party_sleeping) {
        render_sleep_message();
        play_present_viewport();
        return;
    }
    game_set_view_hidden(false);
    action_request_redraw();
    if (g_ui.inventory) {
        int champ = g_ui.inventory - 1;
        game_set_open_inventory(0);
        ui_toggle_inventory(champ);
        return;
    }
    view_clear_viewport();
    ui_draw_movement_arrows();
}

void play_hide_view(void)
{
    if (g_main_loop.view_hidden || g_dialog.screen_mode)
        return;
    game_set_view_hidden(true);
    if (g_input_drag_state) {
        champ_drag_cancel();
        area_update(false);
    }
    input_cancel_push_mode();
    input_release_move_arrow();
}

void play_sleep(void)
{
    if (g_ui.candidate)
        return;
    if (g_ui.inventory)
        ui_close_inventory(false);
    action_area_close();
    play_hide_view();
    champ_set_party_sleeping(true);
    if (g_party.move_steps) {
        move_party(g_party.move_command);
        move_set_walk_steps(0);
    }
    render_sleep_message();
    play_present_viewport();
    game_set_step_ticks(SLEEP_STEP_TICKS);
    input_set_mode(INPUT_MODE_SLEEP);
    input_flush();
}

/* The main loop */

/* The events, map changes, and view of a game step, with each step of a walk. Returns false while the party sleeps. */
static bool step_view(void)
{
    for (;;) {
        if (g_party.new_map != -1)
            game_enter_new_map();
        game_process_timeline();
        if (g_party.new_map != -1)
            continue;
        world_update_day_night(false);
        if (g_party_sleeping)
            return false;
        if (!g_ui.candidate)
            play_show_view();
        if (!g_ui.inventory) {
            if (g_party.redraw)
                ai_refresh_party_maps();
            if (g_party.move_steps)
                view_draw_dungeon(g_party.view_dir, g_party.view_x, g_party.view_y);
            else
                view_draw_dungeon(g_game.party_dir, g_game.party_x, g_game.party_y);
            play_present_viewport();
            if (g_game_mode != GAME_MODE_VANILLA)
                seen_mark(g_game.party_map, g_game.party_x, g_game.party_y, g_game.party_dir);
        }
        if (!g_party.move_steps)
            return true;
        if (g_party.move_steps == 1)
            move_party(g_party.move_command);
        int map = g_party.new_map;
        move_set_walk_steps(g_party.move_steps - 1);
        if (map == -1)
            return true;
    }
}

/* The part of a game step before the input wait: events, the view, the party clocks. */
static void step_before_input(void)
{
    tick_elapsed = false;
    game_reset_step_ticks();
    if (step_view()) {
        g_input_feedback_busy = false;
        input_poll();
    }
    input_release_move_arrow();
    sndreq_flush_requests(false);
    champ_apply_pending_damage();
    game_run_party_clocks();
    inv_update_hand_cursor();
    ui_draw_all_champion_states(true);
    if (g_main_loop.end) {
        input_reset_states();
        return;
    }
    game_advance_time();
    text_clear_expired_rows();
    input_release_move_arrow();
}

/* The part of a game step after the input wait: a map change, repeated actions, music. */
static void step_after_input(void)
{
    int saved = g_party.cur_map;
    if (g_party.new_map != -1)
        dun_set_current_map(g_party.new_map);
    champ_process_repeated_actions();
    if (g_party.new_map != -1)
        dun_set_current_map(saved);
    music_update_for_map(g_game.party_map);
}

void play_start_session(void)
{
    game_start_play();
    input_set_mode(INPUT_MODE_MOVE);
    game_enter_map((Position){g_game.party_map, g_game.party_x, g_game.party_y}, false);
    g_dialog.progress_enabled = false;
    advanced_reset();
    if (g_session.loaded) {
        ui_fill_zone_screen(ZONE_SCREEN, 0);
        dialog_run(dialog_draw(DIALOG_LOADED, MESSAGE_NONE));
        ui_fill_zone_screen(ZONE_SCREEN, 0);
    } else if (g_session.new_game) {
        g_dialog.screen_mode = 0;
        ui_fill_zone_screen(ZONE_SCREEN, 0);
    }
    ui_draw_movement_arrows();
    champ_init_party_state();
    game_set_offer_restore(true);
    ai_refresh_party_maps();
    creature_reset_active_groups();
    if (g_session.new_game)
        move_thing(THING_NONE, MOVE_NO_SQUARE, 0, g_game.party_x, g_game.party_y);
    g_dialog.screen_mode = 0;
    step_before_input();
}

/* Whether the advanced mode draws the flight of the projectiles: a running game shows the dungeon view of the
   standing party with no redraw due, no screen over it, and a moving projectile in it. */
static bool flight_view_shows(void)
{
    return g_game_mode == GAME_MODE_ADVANCED && g_main_loop.running && !g_main_loop.end && !g_advanced_screen &&
           !g_main_loop.view_hidden && !g_ui.inventory && !g_ui.candidate && !g_dialog.screen_mode &&
           !g_party_sleeping && !g_party.move_steps && !g_party.redraw && view_shows_party() &&
           view_has_moving_projectiles();
}

/* Advanced mode: the view of a timer tick draws each projectile part of the way from its place of the game step before.
   No rule changes: the random seed stays. */
static void show_projectile_flight(void)
{
    if (!flight_view_shows())
        return;
    uint32_t seed = g_random_seed;
    view_set_flight_tick(g_main_loop.tick_count, g_main_loop.tick_threshold);
    view_draw_dungeon(g_game.party_dir, g_game.party_x, g_game.party_y);
    play_present_viewport();
    view_set_flight_tick(0, 0);
    rand_set_seed(seed);
}

void play_tick(void)
{
    if (g_main_loop.end || g_main_loop.quit)
        return;
    game_count_timer_tick();
    if (game_count_step_tick())
        tick_elapsed = true;
    command_process_queue();
    /* a viewport screen stops the game time; a quit (a lost game) ends all game work */
    if (!tick_elapsed || !g_main_loop.running || g_advanced_screen || g_main_loop.quit) {
        show_projectile_flight();
        return;
    }
    step_after_input();
    step_before_input();
    show_projectile_flight();
}
