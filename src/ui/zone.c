/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "zone.h"
#include "ids.h"
#include "le.h"
#include "move.h"
#include <stdlib.h>

/* GDAT (1, 0, 4, 0), little-endian words: signature, range count, the first and last zone of each range,
   then 4 words per zone of each range. */
#define LAYOUT_INDEX       0
#define LAYOUT_TYPE        4
#define LAYOUT_ENTRY       0
#define LAYOUT_SIGNATURE   0xfc0d
#define RANGE_MAX          256
#define LAYOUT_RANGE_COUNT 2 /* the byte offset of the range count word of the header */
#define LAYOUT_RANGES      4 /* the byte offset of the first range */
#define RANGE_SIZE         4 /* the first and the last zone word of a range */
#define RANGE_LAST         2
#define WORD_SIZE          2

enum { ZONE_TYPE, ZONE_PARENT, ZONE_X, ZONE_Y, ZONE_FIELDS };
#define SIZE_W ZONE_X /* a size zone (type 9) holds the width and height in x and y */
#define SIZE_H ZONE_Y

enum anchor {
    ANCHOR_CENTER,
    ANCHOR_TOP_LEFT,
    ANCHOR_TOP_RIGHT,
    ANCHOR_BOTTOM_RIGHT,
    ANCHOR_BOTTOM_LEFT,
    ANCHOR_TOP,
    ANCHOR_RIGHT,
    ANCHOR_BOTTOM,
    ANCHOR_LEFT,
    ANCHOR_SIZE,
    ANCHOR_RELATIVE
};
#define ANCHOR_RELATIVE_LAST (ANCHOR_RELATIVE + ANCHOR_LEFT)
#define CLIP_FAR             (-10000)
#define CLIP_SIZE            20000
#define WALK_PARENT          3 /* the parent zone of the dungeon view pictures */
#define WALK_VIEW_X          21
#define WALK_VIEW_Y          8
#define WALK_VIEW_W          182
#define WALK_VIEW_H          110

int16_t zone_clip_rect[RECT_SIZE];

/* The view part that stays visible while the party walks. */
const int16_t walk_view_rect[RECT_SIZE] = {WALK_VIEW_X, WALK_VIEW_Y, WALK_VIEW_W, WALK_VIEW_H};

static struct {
    int count;
    int16_t first[RANGE_MAX], last[RANGE_MAX];
    int start[RANGE_MAX]; /* record index of the first zone of each range */
    int16_t (*records)[ZONE_FIELDS];
} layout;

int zone_load(const gdat_t *gdat)
{
    uint32_t size;
    const uint8_t *p = gdat_item(gdat, gdat_find(gdat, CAT_INTERFACE, LAYOUT_INDEX, LAYOUT_TYPE, LAYOUT_ENTRY), &size);
    free(layout.records);
    layout.records = NULL;
    layout.count = 0;
    if (!p || size < LAYOUT_RANGES || (uint16_t)le16(p) != LAYOUT_SIGNATURE)
        return 0;
    int count = le16(p + LAYOUT_RANGE_COUNT), zones = 0;
    if (count < 0 || count > RANGE_MAX || LAYOUT_RANGES + RANGE_SIZE * (uint32_t)count > size)
        return 0;
    for (int i = 0; i < count; i++) {
        layout.first[i] = le16(p + LAYOUT_RANGES + RANGE_SIZE * i);
        layout.last[i] = le16(p + LAYOUT_RANGES + RANGE_LAST + RANGE_SIZE * i);
        layout.start[i] = zones;
        if (layout.last[i] < layout.first[i])
            return 0;
        zones += layout.last[i] - layout.first[i] + 1;
    }
    const uint8_t *r = p + LAYOUT_RANGES + RANGE_SIZE * count;
    if (r + WORD_SIZE * ZONE_FIELDS * (size_t)zones > p + size)
        return 0;
    layout.records = malloc(sizeof *layout.records * (zones ? zones : 1));
    if (!layout.records)
        return 0;
    for (int i = 0; i < zones; i++)
        for (int f = 0; f < ZONE_FIELDS; f++)
            layout.records[i][f] = le16(r + WORD_SIZE * (ZONE_FIELDS * i + f));
    layout.count = count;
    return 1;
}

/* The record of zone, NULL for zone 0 or an unknown zone; the first range that holds it wins. */
static const int16_t *find_zone(int zone)
{
    zone = (int16_t)zone;
    if (zone == 0)
        return NULL;
    for (int i = 0; i < layout.count; i++)
        if (layout.first[i] <= zone && zone <= layout.last[i])
            return layout.records[layout.start[i] + zone - layout.first[i]];
    return NULL;
}

/* Clips rect to clip; returns NULL when nothing is left. */
static int16_t *rect_clip(int16_t *rect, const int16_t *clip)
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
    enum { NONE, HALF, ALL }; /* the part of the size that the offset takes */
    static const int8_t kind_x[ANCHOR_SIZE] = {HALF, NONE, ALL, ALL, NONE, HALF, ALL, HALF, NONE};
    static const int8_t kind_y[ANCHOR_SIZE] = {HALF, NONE, NONE, ALL, ALL, NONE, HALF, ALL, HALF};
    *dx = kind_x[anchor] == HALF ? half(w) : kind_x[anchor] == ALL ? w - 1 : 0;
    *dy = kind_y[anchor] == HALF ? half(h) : kind_y[anchor] == ALL ? h - 1 : 0;
}

/* Places a zone relative to the anchor of its parent, inside the parent's size zone: adds the offsets to (x, y),
   limits clip, and returns the size zone; NULL when a zone is missing, with *bad set when the parent cannot place it. */
static const int16_t *place_relative(const int16_t *rec, int child, int16_t *clip, int16_t *x, int16_t *y,
                                     int *last_parent, int *bad)
{
    const int16_t *p = find_zone(rec[ZONE_PARENT]);
    if (!p)
        return NULL;
    int16_t dx = p[ZONE_X], dy = p[ZONE_Y];
    int parent_anchor = (uint16_t)p[ZONE_TYPE];
    *last_parent = p[ZONE_PARENT];
    const int16_t *size = find_zone(p[ZONE_PARENT]);
    if (!size)
        return NULL;
    if (parent_anchor > ANCHOR_LEFT) {
        *bad = 1;
        return NULL;
    }
    int ox, oy;
    anchor_offset(parent_anchor, size[SIZE_W], size[SIZE_H], &ox, &oy);
    dx -= ox;
    dy -= oy;
    clip[RECT_X] += dx;
    clip[RECT_Y] += dy;
    clip_to_parent(clip, dx, dy, size[SIZE_W], size[SIZE_H]);
    anchor_offset(child - ANCHOR_RELATIVE, size[SIZE_W], size[SIZE_H], &ox, &oy);
    dx += ox + rec[ZONE_X];
    dy += oy;
    *x += dx;
    *y += dy + rec[ZONE_Y];
    return size;
}

/* Adds the offsets of the parent zones of rec to (x, y) and limits clip to them; *last_parent gets the last parent.
   Returns 0 when a parent cannot place the zone. */
static int place_in_parents(const int16_t *rec, int16_t *clip, int16_t *x, int16_t *y, int *last_parent)
{
    int pending = 0;
    *last_parent = 0;
    while (rec[ZONE_PARENT]) {
        int child = (uint16_t)rec[ZONE_TYPE];
        if (child >= ANCHOR_RELATIVE && child <= ANCHOR_RELATIVE_LAST) {
            int bad = 0;
            rec = place_relative(rec, child, clip, x, y, last_parent, &bad);
            if (bad)
                return 0;
            if (!rec)
                break;
            continue;
        }
        const int16_t *p = find_zone(rec[ZONE_PARENT]);
        if (!p)
            break;
        *last_parent = rec[ZONE_PARENT];
        int16_t dx = p[SIZE_W], dy = p[SIZE_H];
        int parent_type = (uint16_t)p[ZONE_TYPE];
        if (parent_type == ANCHOR_TOP_LEFT) {
            *x += dx;
            *y += dy;
            clip[RECT_X] += dx;
            clip[RECT_Y] += dy;
        } else if (parent_type == ANCHOR_SIZE) {
            if (child <= ANCHOR_LEFT) {
                int ox, oy;
                anchor_offset(child, p[SIZE_W], p[SIZE_H], &ox, &oy);
                dx = (int16_t)(rec[ZONE_X] - ox);
                dy = (int16_t)(rec[ZONE_Y] - oy);
            }
            if (pending) {
                pending = 0;
                *x += dx;
                *y += dy;
                clip[RECT_X] += dx;
                clip[RECT_Y] += dy;
            }
            clip_to_parent(clip, dx, dy, p[SIZE_W], p[SIZE_H]);
        } else if (parent_type <= ANCHOR_LEFT) {
            pending = 1;
        }
        rec = p;
    }
    return 1;
}

/* Clips the width x height box at rect to clip; *w and *h get the clipped left and top pixel counts. */
static int16_t *clip_placed(int16_t *rect, const int16_t *clip, int16_t width, int16_t height, int16_t *w, int16_t *h)
{
    int16_t d = (int16_t)(clip[RECT_X] - rect[RECT_X]), limit;
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

int16_t *zone_compute_rect(const Bitmap *image, int16_t *rect, int zone, int16_t *w, int16_t *h, int anchor)
{
    if ((uint16_t)zone == ZONE_NONE)
        return NULL;
    int relative = zone & ZONE_RELATIVE;
    const int16_t *rec = find_zone(zone & ~ZONE_RELATIVE);
    if (!rec)
        return NULL;
    int16_t clip[RECT_SIZE] = {CLIP_FAR, CLIP_FAR, CLIP_SIZE, CLIP_SIZE};
    int16_t local[ZONE_FIELDS];
    if ((int16_t)anchor != ZONE_NO_ANCHOR) {
        for (int f = 0; f < ZONE_FIELDS; f++)
            local[f] = rec[f];
        local[ZONE_TYPE] = (int16_t)anchor;
        rec = local;
    }
    int type = rec[ZONE_TYPE];
    int16_t x = 0, y = 0;
    if (type <= ANCHOR_LEFT) {
        x = rec[ZONE_X];
        y = rec[ZONE_Y];
    } else {
        if (type == ANCHOR_SIZE)
            return NULL;
        type -= ANCHOR_RELATIVE;
    }
    if (relative) {
        x += *w;
        y += *h;
        *w = 0;
        *h = 0;
    }
    if (!image && (*w <= 0 || *h <= 0))
        return NULL;
    int last_parent;
    if (!place_in_parents(rec, clip, &x, &y, &last_parent))
        return NULL;
    int16_t width = *w ? *w : image->width, height = *h ? *h : image->height;
    if ((unsigned)type > ANCHOR_LEFT)
        return NULL;
    int ox, oy;
    anchor_offset(type, width, height, &ox, &oy);
    rect[RECT_X] = (int16_t)(x - ox);
    rect[RECT_Y] = (int16_t)(y - oy);
    if (zone_clip_rect[RECT_W]) {
        for (int i = 0; i < RECT_SIZE; i++)
            clip[i] = zone_clip_rect[i];
    }
    if (party.move_steps && last_parent == WALK_PARENT && !rect_clip(clip, walk_view_rect))
        return NULL;
    return clip_placed(rect, clip, width, height, w, h);
}

int16_t *zone_get_rect_clipped(int zone, int16_t *rect, unsigned w_scale, unsigned h_scale)
{
    const int16_t *rec = find_zone(zone);
    if (!rec || !rec[ZONE_PARENT])
        return NULL;
    const int16_t *size = find_zone(rec[ZONE_PARENT]);
    if (!size || (uint16_t)size[ZONE_TYPE] != ANCHOR_SIZE)
        return NULL;
    w_scale = (uint16_t)w_scale;
    h_scale = (uint16_t)h_scale;
    int16_t w = w_scale == ZONE_SCALE_FULL ? size[SIZE_W] : (int16_t)((int)w_scale * size[SIZE_W] / ZONE_SCALE_FULL);
    int16_t h = h_scale == ZONE_SCALE_FULL ? size[SIZE_H] : (int16_t)((int)h_scale * size[SIZE_H] / ZONE_SCALE_FULL);
    if (w == 0 && w_scale)
        w = 1;
    if (h == 0 && h_scale)
        h = 1;
    if (w <= 0 || h <= 0)
        return NULL;
    return zone_compute_rect(NULL, rect, (int16_t)zone, &w, &h, ZONE_NO_ANCHOR);
}

int16_t *zone_get_rect(int zone, int16_t *rect)
{
    return zone_get_rect_clipped(zone, rect, ZONE_SCALE_FULL, ZONE_SCALE_FULL);
}

void zone_set_clip_margins(int left, int top, int right, int bottom)
{
    zone_clip_rect[RECT_X] = (int16_t)left;
    zone_clip_rect[RECT_Y] = (int16_t)top;
    zone_clip_rect[RECT_W] = (int16_t)(VIEWPORT_WIDTH - right - left);
    zone_clip_rect[RECT_H] = (int16_t)(VIEWPORT_HEIGHT - bottom - top);
}

int zone_interpolate_offset(int from, int to, int percent, int16_t *dx, int16_t *dy)
{
    const int16_t *a = find_zone(from), *b = find_zone(to);
    if (!a || !b)
        return -1;
    percent = percent < 0 ? 0 : percent > PERCENT ? PERCENT : percent;
    *dx = (int16_t)((b[ZONE_X] - a[ZONE_X]) * percent / PERCENT);
    *dy = (int16_t)((b[ZONE_Y] - a[ZONE_Y]) * percent / PERCENT);
    return 0;
}
