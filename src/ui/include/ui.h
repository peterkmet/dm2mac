/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_UI_H
#define DM2_UI_UI_H
/* Prefixes: ui_ offscreen_ */

/* The offscreen bitmaps shared by the panels and the drawing of the interface pictures. */

#include <stdbool.h>
#include <stdint.h>
#include "draw.h"
#include "gdat.h"
#include "gfx.h"
#include "text.h"

/* The number of movement arrow zones. */
#define ZONE_ARROW_COUNT 6

/* The units of the inventory values on the screen. */
#define STAMINA_DISPLAY_UNIT 10 /* the screen shows the stamina divided by this */
#define WEIGHT_TENTHS        10 /* weights and loads are in tenths of a kilogram */

/* GDAT images of the interface: (CAT_INTERFACE, index, GDAT_TYPE_IMAGE, entry), the inventory set (CAT_PANEL, 0,
   GDAT_TYPE_IMAGE, entry), the wall sets. */
#define UI_INDEX_STATUS 2
#define UI_INDEX_ARROWS 3
#define UI_INDEX_ACTION 4
#define UI_INDEX_FACES  6
#define UI_INDEX_AURA   7 /* the aura pictures of the action area */

/* The transparent color and the icon sizes of the interface pictures. */
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

/* A part of a picture to copy: the bitmap, the start (x, y) of the part, the transparent color (GFX_OPAQUE: none),
   the mirror mode (MIRROR_*), and the color map (NULL: none). */
typedef struct {
    const Bitmap *bitmap;
    int16_t x, y;
    int16_t transparent;
    uint16_t mirror;
    const uint8_t *colormap;
} BlitSource;

/* The text lines of the eye panel for an object, and their size. */
#define OBJECT_INFO_LINES 8
#define OBJECT_INFO_SIZE  64

/* The colors of a face icon. */
#define FACE_ICON_COLORS 16

/* ---- Setup ---- */

/* Clears the interface caches for a new screen. */
void ui_reset(void);

/* ---- Queries ---- */

/* The text lines of the eye panel for an object: its name, its weight, and its gauges ("<label> <value>/<max>"), a
   scroll's text, a chest's item names, or a money box's coin counts. Returns the count. */
int ui_object_info_lines(uint16_t thing, char lines[OBJECT_INFO_LINES][OBJECT_INFO_SIZE]);
/* The rank text of a base skill as the skills panel shows it (for example "NOVICE FIGHTER"), in text (TEXT_SIZE bytes).
   The master ranks start with a power rune glyph of the font. */
char *ui_skill_rank_text(int16_t champion_index, int skill, char *text);

/* ---- Offscreen bitmaps ---- */

/* Opens an offscreen bitmap of zone (ZONE_NONE: of o->rect); mark_dirty marks it all as changed. */
void offscreen_create(Offscreen *o, int zone, bool mark_dirty);
/* Copies the changed parts to the screen; close frees the bitmap. */
void offscreen_flush(Offscreen *o, bool close);
/* Fills rect (screen coordinates) with palette color color. */
void offscreen_fill_rect(Offscreen *o, const int16_t *rect, int color);
/* Fills every second pixel (a checkerboard) with UI color ui_color. */
void offscreen_fill_rect_dithered(Offscreen *o, const int16_t *rect, int ui_color);
/* Copies the part of source into rect (screen coordinates). */
void offscreen_blit(Offscreen *o, const BlitSource *source, const int16_t *rect);
/* Draws a GDAT image placed in zone. */
void offscreen_draw_gdat_image(Offscreen *o, GdatKey picture, int zone, int transparent);
/* Draws a prepared picture placed in zone (ZONE_NONE: at its rect). */
void offscreen_draw_ctx(Offscreen *o, DrawCtx *ctx, int zone, int transparent);
/* Draws text with its base line at (x, y) in screen coordinates. */
void offscreen_draw_text_at(Offscreen *o, int16_t x, int16_t y, TextColors colors, const char *text);
/* Draws text placed in zone. */
void offscreen_draw_text_in_zone(Offscreen *o, int zone, TextColors colors, const char *text);
/* Draws text with a shadow placed in zone. */
void offscreen_draw_shadow_text(Offscreen *o, int zone, TextColors colors, const char *text);

/* ---- Interface pictures ---- */

/* Copies the part of source into rect of dst. */
void ui_blit_bitmap(const BlitSource *source, Bitmap *dst, const int16_t *rect);
/* Draws the GDAT image (category, index, entry) in zone of the view picture, without its GDAT offset. */
void ui_draw_viewport_image(int category, int index, int entry, int zone, int transparent);
/* Fills the screen rectangle of zone with palette color color. */
void ui_fill_zone_screen(int zone, int color);
/* A face icon (17 x 17, 4 bpp) of a champion, turned to the party direction, into dst; its 16 colors into palette. */
void ui_draw_champion_face_icon(Bitmap *dst, int16_t champion_index, uint8_t *palette);

/* ---- Screen ---- */

/* Draws the whole screen of a loaded game: the background, the movement arrows, the champions, the action area. */
void ui_draw_game_screen(void); /* exported for the tests */
/* Draws the movement arrows (or the push arrows) and selects their input mode. */
void ui_draw_movement_arrows(void);

/* ---- Status boxes and inventory ---- */

/* Draws what changed of the status box (and of the inventory) of a champion. */
void ui_draw_champion_state(int16_t champion_index);
/* Draws all status boxes, the open inventory, and the action area (tick: one game tick passed). */
void ui_draw_all_champion_states(bool tick);
/* Closes the open inventory; draw_arrows redraws the movement arrows (false while the party falls asleep). */
void ui_close_inventory(bool draw_arrows);
/* Opens the inventory of a champion, or closes it when it is open. */
void ui_toggle_inventory(int16_t champion_index);

#endif /* DM2_UI_UI_H */
