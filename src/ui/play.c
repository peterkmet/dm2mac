/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The main loop: timer ticks, game steps, the view of the party, sleep. */
#include <string.h>
#include "action.h"
#include "action_area.h"
#include "advanced.h"
#include "champion.h"
#include "creature.h"
#include "dialog.h"
#include "draw.h"
#include "game.h"
#include "gdat.h"
#include "input.h"
#include "inventory.h"
#include "move.h"
#include "music.h"
#include "play.h"
#include "seen.h"
#include "sound_request.h"
#include "text.h"
#include "timeline.h"
#include "ui.h"
#include "view.h"
#include "weather.h"
#include "zone.h"

#define TIME_EFFECTS_PERIOD_MASK 0x3f       /* the champion clocks run each 64 game steps */
#define SLEEP_TIME_EFFECTS_PERIOD_MASK 0x0f /* and each 16 game steps while the party sleeps */
#define LIGHT_BURN_PERIOD_MASK 0x1ff        /* held lights burn down each 512 game steps */

static int tick_elapsed;
void (*play_frame_hook)(void);

void play_wait_frames(int count)
{
    while (count-- > 0) {
        main_loop.timer_ticks++;
        if (play_frame_hook) play_frame_hook();
    }
}

void play_present_viewport(void)
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

static void draw_sleep_message(void)
{
    char text[TEXT_SIZE];
    gfx_fill_bitmap(&view_viewport, ui_colors[UI_BLACK]);
    gdat_text(gdat_current(), CAT_INTERFACE, 0, TEXT_SLEEPING, text, sizeof text);
    text_draw_in_zone(ZONE_VIEW_TEXT, ui_colors[UI_CYAN], text);
}

void play_show_view(void)
{
    if (!main_loop.view_hidden || dialog.screen_mode) return;
    if (party_sleeping) {
        draw_sleep_message();
        play_present_viewport();
        return;
    }
    main_loop.view_hidden = 0;
    if (!action.champion) action.dirty = 1;
    else action.redraw_delay = 1;
    if (ui.inventory) {
        int champ = ui.inventory - 1;
        ui.inventory = 0;
        ui_toggle_inventory(champ);
        return;
    }
    view_clear_viewport();
    ui_draw_movement_arrows();
}

void play_hide_view(void)
{
    if (main_loop.view_hidden || dialog.screen_mode) return;
    main_loop.view_hidden = 1;
    if (input_drag_state) {
        champ_drag_cancel();
        area_update(0);
    }
    input_cancel_push_mode();
    input_release_move_arrow();
}

void play_sleep(void)
{
    if (ui.candidate) return;
    inv_arrows_on_close = 0;
    if (ui.inventory) ui_toggle_inventory(CHAMPION_MAX);
    inv_arrows_on_close = 1;
    action_area_close();
    play_hide_view();
    party_sleeping = 1;
    if (party.move_steps) {
        move_party(party.move_command);
        party.move_steps = 0;
    }
    draw_sleep_message();
    play_present_viewport();
    main_loop.tick_threshold = SLEEP_STEP_TICKS;
    input_set_mode(INPUT_MODE_SLEEP);
    input_flush();
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
        if (!ui.candidate) play_show_view();
        if (!ui.inventory) {
            if (party.redraw) creature_refresh_party_maps();
            if (party.move_steps) view_draw_dungeon(party.view_dir, party.view_x, party.view_y);
            else view_draw_dungeon(game.party_dir, game.party_x, game.party_y);
            play_present_viewport();
            if (game_mode != GAME_MODE_VANILLA) seen_mark(game.party_map, game.party_x, game.party_y, game.party_dir);
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
    input_release_move_arrow();
    sound_flush_requests(0);
    champ_apply_pending_damage();
    if (game.magic_light[MAGIC_HASTE]) game.magic_light[MAGIC_HASTE]--;
    if (!(game.game_time & (party_sleeping ? SLEEP_TIME_EFFECTS_PERIOD_MASK : TIME_EFFECTS_PERIOD_MASK))) champ_apply_time_effects();
    inv_update_hand_cursor();
    ui_draw_all_champion_states(1);
    if (main_loop.end) {
        input_reset_states();
        return;
    }
    game.game_time++;
    if (!(game.game_time & LIGHT_BURN_PERIOD_MASK)) champ_burn_held_lights();
    if (game.magic_light[MAGIC_ACTION_EFFECT]) game.magic_light[MAGIC_ACTION_EFFECT]--;
    if (game.party_move_cooldown) game.party_move_cooldown--;
    if (game.throw_cooldown) game.throw_cooldown--;
    text_clear_expired_rows();
    input_release_move_arrow();
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

void play_start_session(void)
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
        ui_fill_zone_screen(ZONE_SCREEN, 0);
        dialog_run(dialog_draw(DIALOG_LOADED, MESSAGE_NONE));
        ui_fill_zone_screen(ZONE_SCREEN, 0);
    } else if (session.new_game) {
        dialog.screen_mode = 0;
        ui_fill_zone_screen(ZONE_SCREEN, 0);
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

void play_tick(void)
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
