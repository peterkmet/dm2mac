/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "zone.h"
#include <stdlib.h>
#include "aspect.h"
#include "ids.h"
#include "le.h"
#include "move.h"
#include "ui.h"

/* GDAT (1, 0, 4, 0), little-endian words: signature, range count, the first and last zone of each range,
   then 4 words per zone of each range. */
#define LAYOUT_INDEX 0x00
#define LAYOUT_TYPE  0x04

/* The slot frame around an icon, in pixels. */
#define ICON_FRAME 1

/* The first word of the layout item. */
#define LAYOUT_SIGNATURE 0xfc0d

/* Sizes and byte offsets of the layout item. */
#define RANGE_MAX          256
#define LAYOUT_RANGE_COUNT 2 /* the byte offset of the range count word of the header */
#define LAYOUT_RANGES      4 /* the byte offset of the first range */
#define RANGE_SIZE         4 /* the first and the last zone word of a range */
#define RANGE_LAST         2
#define WORD_SIZE          2

/* The words of a zone record. */
#define ZONE_TYPE   0
#define ZONE_PARENT 1
#define ZONE_X      2
#define ZONE_Y      3
#define ZONE_FIELDS 4

/* A size zone (type 9) holds the width and height in x and y. */
#define SIZE_W ZONE_X
#define SIZE_H ZONE_Y

/* Anchor types of a zone record. */
#define ANCHOR_CENTER        0
#define ANCHOR_TOP_LEFT      1
#define ANCHOR_TOP_RIGHT     2
#define ANCHOR_BOTTOM_RIGHT  3
#define ANCHOR_BOTTOM_LEFT   4
#define ANCHOR_TOP           5
#define ANCHOR_RIGHT         6
#define ANCHOR_BOTTOM        7
#define ANCHOR_LEFT          8
#define ANCHOR_SIZE          9
#define ANCHOR_RELATIVE      10
#define ANCHOR_RELATIVE_LAST (ANCHOR_RELATIVE + ANCHOR_LEFT)

/* The part of a size that an anchor offset takes. */
#define PART_NONE 0
#define PART_HALF 1
#define PART_ALL  2

/* The clip rectangle of a zone without a clip, and the view part that stays visible while the party walks. */
#define CLIP_FAR    (-10000)
#define CLIP_SIZE   20000
#define WALK_PARENT 3 /* the parent zone of the dungeon view pictures */
#define WALK_VIEW_X 21
#define WALK_VIEW_Y 8
#define WALK_VIEW_W 182
#define WALK_VIEW_H 110

/* A zone on its way through its parents: its position, the clip rectangle and the last parent zone. */
typedef struct {
    int16_t x, y;
    int16_t clip[RECT_SIZE];
    int last_parent;
} Placement;

int16_t g_zone_clip_rect[RECT_SIZE];

/* The view part that stays visible while the party walks. */
const int16_t g_zone_walk_view_rect[RECT_SIZE] = {WALK_VIEW_X, WALK_VIEW_Y, WALK_VIEW_W, WALK_VIEW_H};

static struct {
    int count;
    int16_t first[RANGE_MAX], last[RANGE_MAX];
    int start[RANGE_MAX]; /* record index of the first zone of each range */
    int16_t (*records)[ZONE_FIELDS];
} layout;

bool zone_load(const Gdat *gdat)
{
    uint32_t size;
    const uint8_t *p = gdat_item(gdat, gdat_find(gdat, CAT_INTERFACE, LAYOUT_INDEX, LAYOUT_TYPE, GFX_LAYOUT), &size);
    free(layout.records);
    layout.records = NULL;
    layout.count = 0;
    if (!p || size < LAYOUT_RANGES || (uint16_t)le16(p) != LAYOUT_SIGNATURE)
        return false;
    int count = le16(p + LAYOUT_RANGE_COUNT);
    int zones = 0;
    if (count < 0 || count > RANGE_MAX || LAYOUT_RANGES + RANGE_SIZE * (uint32_t)count > size)
        return false;
    for (int i = 0; i < count; i++) {
        layout.first[i] = le16(p + LAYOUT_RANGES + RANGE_SIZE * i);
        layout.last[i] = le16(p + LAYOUT_RANGES + RANGE_LAST + RANGE_SIZE * i);
        layout.start[i] = zones;
        if (layout.last[i] < layout.first[i])
            return false;
        zones += layout.last[i] - layout.first[i] + 1;
    }
    const uint8_t *r = p + LAYOUT_RANGES + RANGE_SIZE * count;
    if (r + WORD_SIZE * ZONE_FIELDS * (size_t)zones > p + size)
        return false;
    layout.records = malloc(sizeof *layout.records * (zones ? zones : 1));
    if (!layout.records)
        return false;
    for (int i = 0; i < zones; i++)
        for (int f = 0; f < ZONE_FIELDS; f++)
            layout.records[i][f] = le16(r + WORD_SIZE * (ZONE_FIELDS * i + f));
    layout.count = count;
    return true;
}

/* The record of zone, NULL for zone 0 or an unknown zone; the first range that holds it wins. */
static const int16_t *find_zone(int16_t zone)
{
    if (zone == 0)
        return NULL;
    for (int i = 0; i < layout.count; i++)
        if (layout.first[i] <= zone && zone <= layout.last[i])
            return layout.records[layout.start[i] + zone - layout.first[i]];
    return NULL;
}

/* Clips rect to clip; returns NULL when nothing is left. */
static int16_t *clip_rect(int16_t *rect, const int16_t *clip)
{
    int16_t d = (int16_t)(clip[RECT_X] - rect[RECT_X]);
    if (d > 0) {
        rect[RECT_X] += d;
        rect[RECT_W] -= d;
    }
    d = (int16_t)(clip[RECT_Y] - rect[RECT_Y]);
    if (d > 0) {
        rect[RECT_Y] += d;
        rect[RECT_H] -= d;
    }
    d = (int16_t)((rect[RECT_W] + rect[RECT_X] - 1) - (clip[RECT_X] + clip[RECT_W] - 1));
    if (d > 0)
        rect[RECT_W] -= d;
    d = (int16_t)((rect[RECT_H] + rect[RECT_Y] - 1) - (clip[RECT_Y] + clip[RECT_H] - 1));
    if (d > 0)
        rect[RECT_H] -= d;
    return rect[RECT_W] < 1 || rect[RECT_H] < 1 ? NULL : rect;
}

/* Limits clip to a parent of size w x h at (x, y). */
static void clip_to_parent(int16_t *clip, int x, int y, int w, int h)
{
    if (x > clip[RECT_X])
        clip[RECT_X] = (int16_t)x;
    if (x + w <= clip[RECT_X] + clip[RECT_W] - 1)
        clip[RECT_W] = (int16_t)(w - clip[RECT_X] + x);
    if (y > clip[RECT_Y])
        clip[RECT_Y] = (int16_t)y;
    if (y + h <= clip[RECT_Y] + clip[RECT_H] - 1)
        clip[RECT_H] = (int16_t)(h - clip[RECT_Y] + y);
}

static int half(int v)
{
    return (v + 1) >> 1;
}

/* The offset from the anchor point of a w x h box to its top left corner, by anchor type. */
static void anchor_offset(int anchor, int w, int h, int *dx, int *dy)
{
    static const int8_t kind_x[ANCHOR_SIZE] = {PART_HALF, PART_NONE, PART_ALL,  PART_ALL, PART_NONE,
                                               PART_HALF, PART_ALL,  PART_HALF, PART_NONE};
    static const int8_t kind_y[ANCHOR_SIZE] = {PART_HALF, PART_NONE, PART_NONE, PART_ALL, PART_ALL,
                                               PART_NONE, PART_HALF, PART_ALL,  PART_HALF};
    *dx = kind_x[anchor] == PART_HALF ? half(w) : kind_x[anchor] == PART_ALL ? w - 1 : 0;
    *dy = kind_y[anchor] == PART_HALF ? half(h) : kind_y[anchor] == PART_ALL ? h - 1 : 0;
}

/* Places a zone relative to the anchor of its parent, inside the parent's size zone: adds the offsets to the
   position, limits the clip, and returns the size zone; NULL when a zone is missing, with *bad set when the parent
   cannot place it. */
static const int16_t *place_relative(const int16_t *rec, int child, Placement *at, bool *bad)
{
    const int16_t *p = find_zone(rec[ZONE_PARENT]);
    if (!p)
        return NULL;
    int16_t dx = p[ZONE_X];
    int16_t dy = p[ZONE_Y];
    int parent_anchor = (uint16_t)p[ZONE_TYPE];
    at->last_parent = p[ZONE_PARENT];
    const int16_t *size = find_zone(p[ZONE_PARENT]);
    if (!size)
        return NULL;
    if (parent_anchor > ANCHOR_LEFT) {
        *bad = true;
        return NULL;
    }
    int ox, oy;
    anchor_offset(parent_anchor, size[SIZE_W], size[SIZE_H], &ox, &oy);
    dx -= ox;
    dy -= oy;
    at->clip[RECT_X] += dx;
    at->clip[RECT_Y] += dy;
    clip_to_parent(at->clip, dx, dy, size[SIZE_W], size[SIZE_H]);
    anchor_offset(child - ANCHOR_RELATIVE, size[SIZE_W], size[SIZE_H], &ox, &oy);
    dx += ox + rec[ZONE_X];
    dy += oy;
    at->x += dx;
    at->y += dy + rec[ZONE_Y];
    return size;
}

/* Adds the offsets of the parent zones of rec to the position and limits the clip to them; the last parent goes to
   at->last_parent. Returns false when a parent cannot place the zone. */
static bool place_in_parents(const int16_t *rec, Placement *at)
{
    bool pending = false;
    at->last_parent = 0;
    while (rec[ZONE_PARENT]) {
        int child = (uint16_t)rec[ZONE_TYPE];
        if (child >= ANCHOR_RELATIVE && child <= ANCHOR_RELATIVE_LAST) {
            bool bad = false;
            rec = place_relative(rec, child, at, &bad);
            if (bad)
                return false;
            if (!rec)
                break;
            continue;
        }
        const int16_t *p = find_zone(rec[ZONE_PARENT]);
        if (!p)
            break;
        at->last_parent = rec[ZONE_PARENT];
        int16_t dx = p[SIZE_W];
        int16_t dy = p[SIZE_H];
        int parent_type = (uint16_t)p[ZONE_TYPE];
        if (parent_type == ANCHOR_TOP_LEFT) {
            at->x += dx;
            at->y += dy;
            at->clip[RECT_X] += dx;
            at->clip[RECT_Y] += dy;
        } else if (parent_type == ANCHOR_SIZE) {
            if (child <= ANCHOR_LEFT) {
                int ox, oy;
                anchor_offset(child, p[SIZE_W], p[SIZE_H], &ox, &oy);
                dx = (int16_t)(rec[ZONE_X] - ox);
                dy = (int16_t)(rec[ZONE_Y] - oy);
            }
            if (pending) {
                pending = false;
                at->x += dx;
                at->y += dy;
                at->clip[RECT_X] += dx;
                at->clip[RECT_Y] += dy;
            }
            clip_to_parent(at->clip, dx, dy, p[SIZE_W], p[SIZE_H]);
        } else if (parent_type <= ANCHOR_LEFT) {
            pending = true;
        }
        rec = p;
    }
    return true;
}

/* Clips the *w x *h box at rect to clip; *w and *h then get the clipped left and top pixel counts. */
static int16_t *clip_placed(int16_t *rect, const int16_t *clip, int16_t *w, int16_t *h)
{
    int16_t width = *w;
    int16_t height = *h;
    int16_t d = (int16_t)(clip[RECT_X] - rect[RECT_X]);
    int16_t limit;
    if (d <= 0) {
        *w = 0;
        limit = (int16_t)(clip[RECT_W] + d);
    } else {
        *w = d;
        rect[RECT_X] = clip[RECT_X];
        width -= d;
        limit = clip[RECT_W];
    }
    rect[RECT_W] = width < limit ? width : limit;
    d = (int16_t)(clip[RECT_Y] - rect[RECT_Y]);
    if (d <= 0) {
        *h = 0;
        limit = (int16_t)(clip[RECT_H] + d);
    } else {
        *h = d;
        rect[RECT_Y] = clip[RECT_Y];
        height -= d;
        limit = clip[RECT_H];
    }
    rect[RECT_H] = height < limit ? height : limit;
    return rect[RECT_W] > 0 && rect[RECT_H] > 0 ? rect : NULL;
}

int16_t *zone_compute_rect(const Bitmap *image, int16_t *rect, uint16_t zone, int16_t *out_w, int16_t *out_h)
{
    return zone_compute_anchored_rect(image, rect, (AnchoredZone){zone, ZONE_NO_ANCHOR}, out_w, out_h);
}

int16_t *zone_compute_anchored_rect(const Bitmap *image, int16_t *rect, AnchoredZone anchored, int16_t *out_w,
                                    int16_t *out_h)
{
    if (anchored.zone == ZONE_NONE)
        return NULL;
    bool relative = anchored.zone & ZONE_RELATIVE;
    const int16_t *rec = find_zone(anchored.zone & ~ZONE_RELATIVE);
    if (!rec)
        return NULL;
    Placement at = {
        .clip = {CLIP_FAR, CLIP_FAR, CLIP_SIZE, CLIP_SIZE}
    };
    int16_t local[ZONE_FIELDS];
    if (anchored.anchor != ZONE_NO_ANCHOR) {
        for (int f = 0; f < ZONE_FIELDS; f++)
            local[f] = rec[f];
        local[ZONE_TYPE] = anchored.anchor;
        rec = local;
    }
    int type = rec[ZONE_TYPE];
    if (type <= ANCHOR_LEFT) {
        at.x = rec[ZONE_X];
        at.y = rec[ZONE_Y];
    } else {
        if (type == ANCHOR_SIZE)
            return NULL;
        type -= ANCHOR_RELATIVE;
    }
    if (relative) {
        at.x += *out_w;
        at.y += *out_h;
        *out_w = 0;
        *out_h = 0;
    }
    if (!image && (*out_w <= 0 || *out_h <= 0))
        return NULL;
    if (!place_in_parents(rec, &at))
        return NULL;
    if ((unsigned)type > ANCHOR_LEFT)
        return NULL;
    *out_w = *out_w ? *out_w : image->width;
    *out_h = *out_h ? *out_h : image->height;
    int ox, oy;
    anchor_offset(type, *out_w, *out_h, &ox, &oy);
    rect[RECT_X] = (int16_t)(at.x - ox);
    rect[RECT_Y] = (int16_t)(at.y - oy);
    if (g_zone_clip_rect[RECT_W]) {
        for (int i = 0; i < RECT_SIZE; i++)
            at.clip[i] = g_zone_clip_rect[i];
    }
    if (g_party.move_steps && at.last_parent == WALK_PARENT && !clip_rect(at.clip, g_zone_walk_view_rect))
        return NULL;
    return clip_placed(rect, at.clip, out_w, out_h);
}

int16_t *zone_get_rect_clipped(int16_t zone, int16_t *rect, uint16_t w_scale, uint16_t h_scale)
{
    const int16_t *rec = find_zone(zone);
    if (!rec || !rec[ZONE_PARENT])
        return NULL;
    const int16_t *size = find_zone(rec[ZONE_PARENT]);
    if (!size || (uint16_t)size[ZONE_TYPE] != ANCHOR_SIZE)
        return NULL;
    int16_t w = w_scale == ZONE_SCALE_FULL ? size[SIZE_W] : (int16_t)((int)w_scale * size[SIZE_W] / ZONE_SCALE_FULL);
    int16_t h = h_scale == ZONE_SCALE_FULL ? size[SIZE_H] : (int16_t)((int)h_scale * size[SIZE_H] / ZONE_SCALE_FULL);
    if (w == 0 && w_scale)
        w = 1;
    if (h == 0 && h_scale)
        h = 1;
    if (w <= 0 || h <= 0)
        return NULL;
    return zone_compute_rect(NULL, rect, zone, &w, &h);
}

int16_t *zone_get_rect(int16_t zone, int16_t *rect)
{
    return zone_get_rect_clipped(zone, rect, ZONE_SCALE_FULL, ZONE_SCALE_FULL);
}

int16_t *zone_get_viewport_rect(int16_t *rect)
{
    int steps = g_party.move_steps;
    move_set_walk_steps(0);
    int16_t *result = zone_get_rect(ZONE_VIEWPORT, rect);
    move_set_walk_steps(steps);
    return result;
}

bool zone_interpolate_offset(int16_t from, int16_t to, int16_t percent, ScreenPoint *out)
{
    const int16_t *a = find_zone(from);
    const int16_t *b = find_zone(to);
    if (!a || !b)
        return false;
    percent = percent < 0 ? 0 : percent > PERCENT ? PERCENT : percent;
    out->x = (int16_t)((b[ZONE_X] - a[ZONE_X]) * percent / PERCENT);
    out->y = (int16_t)((b[ZONE_Y] - a[ZONE_Y]) * percent / PERCENT);
    return true;
}

void zone_set_clip_margins(int16_t left, int16_t top, int16_t right, int16_t bottom)
{
    g_zone_clip_rect[RECT_X] = left;
    g_zone_clip_rect[RECT_Y] = top;
    g_zone_clip_rect[RECT_W] = (int16_t)(VIEWPORT_WIDTH - right - left);
    g_zone_clip_rect[RECT_H] = (int16_t)(VIEWPORT_HEIGHT - bottom - top);
}

/* Zones of the interface */

int16_t *zone_get_icon_rect(int16_t zone, int16_t *rect)
{
    int16_t w = ICON_SIZE;
    int16_t h = ICON_SIZE;
    zone_compute_rect(NULL, rect, zone, &w, &h);
    rect[RECT_X] -= ICON_FRAME;
    rect[RECT_Y] -= ICON_FRAME;
    rect[RECT_W] += 2 * ICON_FRAME;
    rect[RECT_H] += 2 * ICON_FRAME;
    return rect;
}

ScreenPoint zone_get_position(int16_t zone)
{
    int16_t r[RECT_SIZE] = {0};
    int16_t w = 1;
    int16_t h = 1;
    zone_compute_rect(NULL, r, zone, &w, &h);
    return (ScreenPoint){r[RECT_X], r[RECT_Y]};
}

int zone_id_for_view_cell(int16_t base, int16_t grid_cell, int16_t dir)
{
    return base * ZONE_CELL_GRID + aspect_rotate_cell(grid_cell, dir) + ZONE_VIEW_CELLS;
}
