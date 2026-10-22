/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_UI_INTERNAL_H
#define DM2_UI_UI_INTERNAL_H
/* Prefixes: ui_ */

/* Shared functions of the ui module: the interface pictures of ui.c that only the hook table of ui_hooks.c calls. */

/* Draws the icon of the leader hand object into the cursor: its 4 bpp color numbers and its palette colors. */
void ui_render_hand_icon(void);
/* Draws a GDAT image of the interface in zone of the screen. */
void ui_draw_interface_image(int category, int index, int entry, int zone);

#endif /* DM2_UI_UI_INTERNAL_H */
