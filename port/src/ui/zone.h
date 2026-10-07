#ifndef ZONE_H
#define ZONE_H
#include <stdint.h>
#include "gdat.h"
#include "gfx.h"

/* Prefixes: zone_ */

/* Layout zones: GDAT (1, 0, 4, 0) holds a record per zone: anchor type, parent zone, x, y. Anchor types 0 to 8
   place the zone at (x, y) of its parent (center, top left, top right, bottom right, bottom left, top, right,
   bottom, left); 9 gives a size (x, y = width, height); 10 to 18 place it relative to the parent's anchor. */

#define ZONE_NONE 0xffff
#define ZONE_RELATIVE 0x8000    /* zone flag: add the offsets in *w and *h to the zone position */
#define ZONE_NO_ANCHOR (-1)     /* anchor override that keeps the anchor of the zone record */
#define ZONE_SCALE_FULL 10000   /* scale of zone_get_rect_clipped, in 1/10000 */
#define VIEWPORT_WIDTH 224
#define VIEWPORT_HEIGHT 136

/* The view part that stays visible while the party walks. */
extern const int16_t walk_view_rect[RECT_SIZE];

/* Clip rectangle of the next zone computations; active while its width is not 0. */
extern int16_t zone_clip_rect[RECT_SIZE];

/* Reads the layout zones from GRAPHICS.DAT; returns 0 when they are missing or damaged. */
int zone_load(const gdat_t *gdat);
/* Places an image of *w x *h pixels (0: the size of image) in zone and clips it. Returns rect, or NULL when
   nothing is visible; *w and *h then hold the clipped left and top pixel counts. */
int16_t *zone_compute_rect(const Bitmap *image, int16_t *rect, int zone, int16_t *w, int16_t *h, int anchor);
/* The rect of zone at w_scale and h_scale (1/10000) of the size of its parent size zone. */
int16_t *zone_get_rect_clipped(int zone, int16_t *rect, unsigned w_scale, unsigned h_scale);
int16_t *zone_get_rect(int zone, int16_t *rect);
/* The offset (*dx, *dy) at percent (0 to 100) of the way from zone from to zone to; returns -1 for an unknown zone. */
int zone_interpolate_offset(int from, int to, int percent, int16_t *dx, int16_t *dy);
/* Clips the next zones to the viewport minus these margins. */
void zone_set_clip_margins(int left, int top, int right, int bottom);

/* Zones of the interface (ui.c, view.c). */
/* The rect of an icon (16 x 16) placed in zone, with a 1 pixel frame. */
int16_t *zone_get_icon_rect(int zone, int16_t *rect);
/* The screen position of zone for a point (1 x 1 pixels). */
void zone_get_position(int zone, int16_t *x, int16_t *y);
/* The view zone of grid cell cell (0 to 24, 5 x 5) of view element base, turned by dir. */
int zone_id_for_view_cell(int base, int cell, int dir);

#endif
