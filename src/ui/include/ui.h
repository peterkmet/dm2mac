/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_UI_H
#define DM2_UI_UI_H
/* Prefixes: ui_ offscreen_ */

/* The offscreen bitmaps shared by the panels and the drawing of the interface pictures. */

#include "gfx.h"
#include "game.h"
#include "draw.h"
#include <stdint.h>

/* The number of movement arrow zones. */
#define ZONE_ARROW_COUNT 6

/* What the inventory panel shows (ui.panel_mode). */
enum {
    PANEL_MODE_NONE,
    PANEL_MODE_FOOD,
    PANEL_MODE_SKILLS,
    PANEL_MODE_OBJECT,
    PANEL_MODE_SCROLL = 5,
    PANEL_MODE_MOUTH = 7
};
#define STAMINA_DISPLAY_UNIT 10     /* the screen shows the stamina divided by this */
#define WEIGHT_TENTHS        10     /* weights and loads are in tenths of a kilogram */
#define SCROLL_TEXT_MODE     0x8002 /* decode a scroll text: no leading new line, also when the text is hidden */

/* GDAT images of the interface: (category 1, index, 1, entry), the inventory set (7, 0, 1, entry), the wall sets. */
enum { UI_INDEX_STATUS = 2, UI_INDEX_ARROWS = 3, UI_INDEX_ACTION = 4, UI_INDEX_FACES = 6 };
#define UI_KEY    12 /* transparent color of the interface pictures */
#define ICON_SIZE 16 /* item icons are 16 x 16 */
#define FACE_SIZE 17 /* champion face icons are 17 x 17, one per direction */

/* A bitmap of a screen rectangle that collects drawing and copies the changed parts to the screen. */
#define OFFSCREEN_DIRTY_MAX 5
typedef struct {
    Bitmap *bitmap;          /* NULL while closed */
    int16_t rect[RECT_SIZE]; /* where it goes on the screen */
    int16_t dirty_count;
    int16_t dirty[OFFSCREEN_DIRTY_MAX][RECT_SIZE];
} Offscreen;

/* Opens an offscreen bitmap of zone (-1: of o->rect); mark_dirty marks it all as changed. */
void offscreen_create(Offscreen *o, int zone, int mark_dirty);
/* Copies the changed parts to the screen; close frees the bitmap. */
void offscreen_flush(Offscreen *o, int close);
void offscreen_add_dirty_rect(Offscreen *o, const int16_t *rect);
/* Drawing in screen coordinates. */
void offscreen_fill_rect(Offscreen *o, const int16_t *rect, int color);
/* Fills every second pixel (a checkerboard) with UI color ui_color. */
void offscreen_fill_rect_dithered(Offscreen *o, const int16_t *rect, int ui_color);
void offscreen_blit(Offscreen *o, const Bitmap *src, const int16_t *rect, int src_x, int src_y, int transparent,
                    int mirror, const uint8_t *colormap);
/* Draws a GDAT image placed in zone. */
void offscreen_draw_gdat_image(Offscreen *o, int category, int index, int entry, int zone, int transparent);
/* Draws a prepared picture placed in zone (-1: at its rect). */
void offscreen_draw_ctx(Offscreen *o, DrawCtx *ctx, int zone, int transparent);
/* Draws text with its base line at (x, y) in screen coordinates. */
void offscreen_draw_text_at(Offscreen *o, int x, int y, int color, int bg, const char *text);
/* Draws text placed in zone, plain or with a shadow. */
void offscreen_draw_text_in_zone(Offscreen *o, int zone, int color, int bg, const char *text);
void offscreen_draw_text_in_zone_with_shadow(Offscreen *o, int zone, int color, int bg, const char *text);

/* Fills every second pixel of rect in dst with UI color ui_color. */
void ui_fill_rect_dithered(uint8_t *dst, const int16_t *rect, int ui_color, int dst_width);
/* Copies src into rect of dst from (src_x, src_y). */
void ui_blit_bitmap(const Bitmap *src, Bitmap *dst, const int16_t *rect, int src_x, int src_y, int transparent,
                    const uint8_t *colormap);
/* Draws the GDAT image (category, index, entry) in zone of the view picture, without its GDAT offset. */
void ui_draw_gdat_in_viewport_zone(int category, int index, int entry, int zone, int transparent);
/* Fills the screen rectangle of zone with palette color color. */
void ui_fill_zone_screen(int zone, int color);
/* A face icon (17 x 17, 4 bpp) of a champion, turned to the party direction, into dst; its 16 colors into palette. */
#define FACE_ICON_COLORS 16
void ui_draw_champion_face_icon(Bitmap *dst, int champ, uint8_t *palette);
/* Draws the icon of the leader hand object into the cursor: its 4 bpp color numbers and its palette colors. */
void ui_render_hand_icon(void);
/* Draws a GDAT image of the interface in zone of the screen. */
void ui_draw_interface_image(int category, int index, int entry, int zone);

/* Clears the interface caches for a new screen. */
void ui_reset(void);
/* Draws the whole screen of a loaded game: the background, the movement arrows, the champions, the action area. */
void ui_draw_game_screen(void);
void ui_draw_movement_arrows(void);
/* Draws what changed of the status box (and of the inventory) of champ. */
void ui_draw_champion_state(int champ);
/* Draws all status boxes, the open inventory, and the action area (tick: one game tick passed). */
void ui_draw_all_champion_states(int tick);
/* Opens the inventory of champ, or closes it when it is open (champ 4: close). */
void ui_toggle_inventory(int champ);
/* The text lines of the eye panel for an object: its name, its weight, and its gauges ("<label> <value>/<max>"), a
   scroll's text, a chest's item names, or a money box's coin counts. Returns the count. */
#define OBJECT_INFO_LINES 8
#define OBJECT_INFO_SIZE  64
int ui_object_info_lines(uint16_t thing, char lines[OBJECT_INFO_LINES][OBJECT_INFO_SIZE]);
/* The rank text of a base skill as the skills panel shows it (for example "NOVICE FIGHTER"), in dst (TEXT_SIZE bytes).
   The master ranks start with a power rune glyph of the font. */
char *ui_skill_rank_text(int champ, int skill, char *dst);
/* Closing the inventory redraws the movement arrows (0 while the party falls asleep). */
extern int inv_arrows_on_close;

#endif /* DM2_UI_UI_H */
