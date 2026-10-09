/* The user interface functions that the game rules call (engine/hooks.h). */
#include "action_area.h"
#include "hooks.h"
#include "sound.h"
#include "text.h"
#include "ui.h"
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
};
