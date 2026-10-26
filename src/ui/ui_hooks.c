/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The user interface functions that the game rules call (engine/hooks.h). */
#include "action_area.h"
#include "hooks.h"
#include "input.h"
#include "play.h"
#include "session.h"
#include "sound.h"
#include "text.h"
#include "ui.h"
#include "ui_internal.h"
#include "view.h"
#include "zone.h"

Hooks g_hooks = {
    .print_message = text_print_message,
    .print_newline = text_print_newline,
    .play_sound = sound_play,
    .zone_offset = zone_interpolate_offset,
    .zone_rect = zone_get_rect,
    .update_action_area = area_update,
    .reset_action_area = area_reset,
    .reset_action_champion = area_reset_champion,
    .render_hand_icon = ui_render_hand_icon,
    .draw_interface_image = ui_draw_interface_image,
    .enter_map_view = view_enter_map,
    .flush_input = input_flush,
    .poll_input = input_poll,
    .clear_viewport = view_clear_viewport,
    .set_input_mode = input_set_mode,
    .show_view = play_show_view,
    .hide_view = play_hide_view,
    .present_viewport = play_present_viewport,
    .refresh_input_mode = input_refresh_mode,
    .set_wait = input_set_wait,
    .set_champion_cursor = input_set_champion_cursor,
    .toggle_inventory = ui_toggle_inventory,
    .draw_champion_state = ui_draw_champion_state,
    .draw_all_champion_states = ui_draw_all_champion_states,
    .draw_movement_arrows = ui_draw_movement_arrows,
    .fill_screen_zone = ui_fill_zone_screen,
    .draw_viewport_image = ui_draw_viewport_image,
    .complete_game = session_complete,
    .draw_dungeon = view_draw_dungeon,
};
