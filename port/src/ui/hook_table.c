/* The user interface functions that the game rules call (engine/hooks.h). */
#include "action_area.h"
#include "hooks.h"
#include "input.h"
#include "play.h"
#include "sound.h"
#include "text.h"
#include "ui.h"
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
};
