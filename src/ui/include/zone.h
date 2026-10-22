/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_ZONE_H
#define DM2_UI_ZONE_H
/* Prefixes: zone_ */

/* Layout zones: GDAT (1, 0, 4, 0) holds a record per zone: anchor type, parent zone, x, y. Anchor types 0 to 8
   place the zone at (x, y) of its parent (center, top left, top right, bottom right, bottom left, top, right,
   bottom, left); 9 gives a size (x, y = width, height); 10 to 18 place it relative to the parent's anchor. */

#include "gdat.h"
#include "gfx.h"
#include <stdbool.h>
#include <stdint.h>

/* Zone flag: add the offsets in *w and *h to the zone position. */
#define ZONE_RELATIVE 0x8000

/* Anchor override that keeps the anchor of the zone record. */
#define ZONE_NO_ANCHOR (-1)

/* The scale of zone_get_rect_clipped (in 1/10000) and the size of the viewport. */
#define ZONE_SCALE_FULL 10000
#define VIEWPORT_WIDTH  224
#define VIEWPORT_HEIGHT 136

/* A zone and the anchor type that replaces the one of its record (ZONE_NO_ANCHOR keeps it). */
typedef struct {
    int zone;
    int anchor;
} AnchoredZone;

/* The view part that stays visible while the party walks. */
extern const int16_t g_zone_walk_view_rect[RECT_SIZE];

/* Clip rectangle of the next zone computations; active while its width is not 0. */
extern int16_t g_zone_clip_rect[RECT_SIZE];

/* Reads the layout zones from GRAPHICS.DAT; returns false when they are missing or damaged. */
bool zone_load(const gdat_t *gdat);
/* Places an image of *out_w x *out_h pixels (0: the size of image) in zone and clips it. Returns rect, or NULL when
   nothing is visible; *out_w and *out_h then hold the clipped left and top pixel counts. */
int16_t *zone_compute_rect(const Bitmap *image, int16_t *rect, int zone, int16_t *out_w, int16_t *out_h);
/* As zone_compute_rect, with the anchor type of the zone replaced. */
int16_t *zone_compute_anchored_rect(const Bitmap *image, int16_t *rect, AnchoredZone anchored, int16_t *out_w,
                                    int16_t *out_h);
/* The rect of zone at w_scale and h_scale (1/10000) of the size of its parent size zone. */
int16_t *zone_get_rect_clipped(int zone, int16_t *rect, unsigned w_scale, unsigned h_scale);
/* The rect of zone at the full size of its parent size zone. */
int16_t *zone_get_rect(int zone, int16_t *rect);
/* The offset (*out_dx, *out_dy) at percent (0 to 100) of the way from zone from to zone to; returns false for an unknown
   zone. */
bool zone_interpolate_offset(int from, int to, int percent, int16_t *out_dx, int16_t *out_dy);
/* Clips the next zones to the viewport minus these margins. */
void zone_set_clip_margins(int left, int top, int right, int bottom);

/* Zones of the interface (ui.c, view.c). */
/* The rect of an icon (16 x 16) placed in zone, with a 1 pixel frame. */
int16_t *zone_get_icon_rect(int zone, int16_t *rect);
/* The screen position of zone for a point (1 x 1 pixels). */
void zone_get_position(int zone, int16_t *out_x, int16_t *out_y);
/* The view zone of grid cell grid_cell (0 to 24, 5 x 5) of view element base, turned by dir. */
int zone_id_for_view_cell(int base, int grid_cell, int dir);

#endif /* DM2_UI_ZONE_H */
