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
    int start[RANGE_MAX];            /* record index of the first zone of each range */
    int16_t (*records)[ZONE_FIELDS]; /* owned: the zone records of all ranges */
} layout;

/* The record of zone, NULL for zone 0 or an unknown zone; the first range that holds it wins. */
static const int16_t *find_zone(uint16_t zone)
{
    if (zone == 0)
        return NULL;
    for (int i = 0; i < layout.count; i++)
        if (layout.first[i] <= zone && zone <= layout.last[i])
            return layout.records[layout.start[i] + zone - layout.first[i]];
    return NULL;
}

/* The anchor type of a zone record, read unsigned: a negative type of a damaged record fails every anchor test. */
static int anchor_type(const int16_t *rec)
{
    return (uint16_t)rec[ZONE_TYPE];
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
static void clip_to_parent(int16_t *clip, int16_t x, int16_t y, int16_t w, int16_t h)
{
    if (x > clip[RECT_X])
        clip[RECT_X] = x;
    if (x + w <= clip[RECT_X] + clip[RECT_W] - 1)
        clip[RECT_W] = (int16_t)(w - clip[RECT_X] + x);
    if (y > clip[RECT_Y])
        clip[RECT_Y] = y;
    if (y + h <= clip[RECT_Y] + clip[RECT_H] - 1)
        clip[RECT_H] = (int16_t)(h - clip[RECT_Y] + y);
}

static int half(int v)
{
    return (v + 1) >> 1;
}

/* The offset from the anchor point of a w x h box to its top left corner, by anchor type. */
static ScreenPoint anchor_offset(int anchor, int16_t w, int16_t h)
{
    static const int8_t kind_x[ANCHOR_SIZE] = {PART_HALF, PART_NONE, PART_ALL,  PART_ALL, PART_NONE,
                                               PART_HALF, PART_ALL,  PART_HALF, PART_NONE};
    static const int8_t kind_y[ANCHOR_SIZE] = {PART_HALF, PART_NONE, PART_NONE, PART_ALL, PART_ALL,
                                               PART_NONE, PART_HALF, PART_ALL,  PART_HALF};
    int dx = kind_x[anchor] == PART_HALF ? half(w) : kind_x[anchor] == PART_ALL ? w - 1 : 0;
    int dy = kind_y[anchor] == PART_HALF ? half(h) : kind_y[anchor] == PART_ALL ? h - 1 : 0;
    return (ScreenPoint){(int16_t)dx, (int16_t)dy};
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
    int parent_anchor = anchor_type(p);
    at->last_parent = p[ZONE_PARENT];
    const int16_t *size = find_zone(p[ZONE_PARENT]);
    if (!size)
        return NULL;
    if (parent_anchor > ANCHOR_LEFT) {
        *bad = true;
        return NULL;
    }
    ScreenPoint offset = anchor_offset(parent_anchor, size[SIZE_W], size[SIZE_H]);
    dx -= offset.x;
    dy -= offset.y;
    at->clip[RECT_X] += dx;
    at->clip[RECT_Y] += dy;
    clip_to_parent(at->clip, dx, dy, size[SIZE_W], size[SIZE_H]);
    offset = anchor_offset(child - ANCHOR_RELATIVE, size[SIZE_W], size[SIZE_H]);
    dx += offset.x + rec[ZONE_X];
    dy += offset.y;
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
        int child = anchor_type(rec);
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
        int parent_type = anchor_type(p);
        if (parent_type == ANCHOR_TOP_LEFT) {
            at->x += dx;
            at->y += dy;
            at->clip[RECT_X] += dx;
            at->clip[RECT_Y] += dy;
        } else if (parent_type == ANCHOR_SIZE) {
            if (child <= ANCHOR_LEFT) {
                ScreenPoint offset = anchor_offset(child, p[SIZE_W], p[SIZE_H]);
                dx = (int16_t)(rec[ZONE_X] - offset.x);
                dy = (int16_t)(rec[ZONE_Y] - offset.y);
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

/* Clips the box of *size at rect to clip; *size then gets the clipped left and top pixel counts. */
static int16_t *clip_placed(int16_t *rect, const int16_t *clip, ScreenSize *size)
{
    int16_t width = size->w;
    int16_t height = size->h;
    int16_t d = (int16_t)(clip[RECT_X] - rect[RECT_X]);
    int16_t limit;
    if (d <= 0) {
        size->w = 0;
        limit = (int16_t)(clip[RECT_W] + d);
    } else {
        size->w = d;
        rect[RECT_X] = clip[RECT_X];
        width -= d;
        limit = clip[RECT_W];
    }
    rect[RECT_W] = width < limit ? width : limit;
    d = (int16_t)(clip[RECT_Y] - rect[RECT_Y]);
    if (d <= 0) {
        size->h = 0;
        limit = (int16_t)(clip[RECT_H] + d);
    } else {
        size->h = d;
        rect[RECT_Y] = clip[RECT_Y];
        height -= d;
        limit = clip[RECT_H];
    }
    rect[RECT_H] = height < limit ? height : limit;
    return rect[RECT_W] > 0 && rect[RECT_H] > 0 ? rect : NULL;
}

/* Places the box of *size (0: the size of image; with ZONE_RELATIVE the offset) of anchored: its top left corner and
   its clip go to *at_out; false when the zone places nothing. */
static bool place_zone(const Bitmap *image, AnchoredZone anchored, ScreenSize *size, Placement *at_out)
{
    if (anchored.zone == ZONE_NONE)
        return false;
    bool relative = anchored.zone & ZONE_RELATIVE;
    const int16_t *rec = find_zone(anchored.zone & ~ZONE_RELATIVE);
    if (!rec)
        return false;
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
    int type = anchor_type(rec);
    if (type <= ANCHOR_LEFT) {
        at.x = rec[ZONE_X];
        at.y = rec[ZONE_Y];
    } else {
        if (type == ANCHOR_SIZE)
            return false;
        type -= ANCHOR_RELATIVE;
    }
    if (relative) {
        at.x += size->w;
        at.y += size->h;
        size->w = 0;
        size->h = 0;
    }
    if (!image && (size->w <= 0 || size->h <= 0))
        return false;
    if (!place_in_parents(rec, &at))
        return false;
    if (type > ANCHOR_LEFT)
        return false;
    size->w = size->w ? size->w : image->width;
    size->h = size->h ? size->h : image->height;
    ScreenPoint offset = anchor_offset(type, size->w, size->h);
    at.x = (int16_t)(at.x - offset.x);
    at.y = (int16_t)(at.y - offset.y);
    if (g_zone_clip_rect[RECT_W]) {
        for (int i = 0; i < RECT_SIZE; i++)
            at.clip[i] = g_zone_clip_rect[i];
    }
    *at_out = at;
    return true;
}

/* Cuts the clip of at to the walk view while the party walks; false when nothing is left. */
static bool clip_to_walk_view(Placement *at)
{
    return !g_party.move_steps || at->last_parent != WALK_PARENT || clip_rect(at->clip, g_zone_walk_view_rect);
}

/* ---- Setup ---- */

bool zone_load(const Gdat *gdat)
{
    uint32_t size;
    const uint8_t *p = gdat_item(gdat, gdat_find(gdat, CAT_INTERFACE, LAYOUT_INDEX, LAYOUT_TYPE, GFX_LAYOUT), &size);
    zone_free();
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
    uint32_t records_at = LAYOUT_RANGES + RANGE_SIZE * (uint32_t)count;
    if (WORD_SIZE * ZONE_FIELDS * (size_t)zones > size - records_at) /* fix: byte counts, no pointer past the item */
        return false;
    const uint8_t *r = p + records_at;
    layout.records = malloc(sizeof *layout.records * (zones ? zones : 1));
    if (!layout.records)
        return false;
    for (int i = 0; i < zones; i++)
        for (int f = 0; f < ZONE_FIELDS; f++)
            layout.records[i][f] = le16(r + WORD_SIZE * (ZONE_FIELDS * i + f));
    layout.count = count;
    return true;
}

void zone_free(void)
{
    free(layout.records);
    layout.records = NULL;
    layout.count = 0;
}

/* ---- Rects of the zones ---- */

int16_t *zone_compute_rect(const Bitmap *image, int16_t *rect, uint16_t zone, ScreenSize *size)
{
    return zone_compute_anchored_rect(image, rect, (AnchoredZone){zone, ZONE_NO_ANCHOR}, size);
}

int16_t *zone_compute_anchored_rect(const Bitmap *image, int16_t *rect, AnchoredZone anchored, ScreenSize *size)
{
    Placement at;
    if (!place_zone(image, anchored, size, &at))
        return NULL;
    rect[RECT_X] = at.x;
    rect[RECT_Y] = at.y;
    if (!clip_to_walk_view(&at))
        return NULL;
    return clip_placed(rect, at.clip, size);
}

int16_t *zone_get_rect_clipped(uint16_t zone, int16_t *rect, uint16_t w_scale, uint16_t h_scale)
{
    const int16_t *rec = find_zone(zone);
    if (!rec || !rec[ZONE_PARENT])
        return NULL;
    const int16_t *size = find_zone(rec[ZONE_PARENT]);
    if (!size || anchor_type(size) != ANCHOR_SIZE)
        return NULL;
    ScreenSize box = {
        w_scale == ZONE_SCALE_FULL ? size[SIZE_W] : (int16_t)((int)w_scale * size[SIZE_W] / ZONE_SCALE_FULL),
        h_scale == ZONE_SCALE_FULL ? size[SIZE_H] : (int16_t)((int)h_scale * size[SIZE_H] / ZONE_SCALE_FULL)};
    if (box.w == 0 && w_scale)
        box.w = 1;
    if (box.h == 0 && h_scale)
        box.h = 1;
    if (box.w <= 0 || box.h <= 0)
        return NULL;
    return zone_compute_rect(NULL, rect, zone, &box);
}

int16_t *zone_get_rect(uint16_t zone, int16_t *rect)
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

bool zone_interpolate_offset(uint16_t from, uint16_t to, int16_t percent, ScreenPoint *out)
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

/* ---- Clip ---- */

void zone_set_clip_margins(int16_t left, int16_t top, int16_t right, int16_t bottom)
{
    g_zone_clip_rect[RECT_X] = left;
    g_zone_clip_rect[RECT_Y] = top;
    g_zone_clip_rect[RECT_W] = (int16_t)(VIEWPORT_WIDTH - right - left);
    g_zone_clip_rect[RECT_H] = (int16_t)(VIEWPORT_HEIGHT - bottom - top);
}

/* ---- Zones of the interface ---- */

int16_t *zone_get_icon_rect(uint16_t zone, int16_t *rect)
{
    ScreenSize size = {ICON_SIZE, ICON_SIZE};
    Placement at;
    if (!place_zone(NULL, (AnchoredZone){zone, ZONE_NO_ANCHOR}, &size, &at) || !clip_to_walk_view(&at)) {
        for (int i = 0; i < RECT_SIZE; i++) /* fix: the original widens the rect that it did not set */
            rect[i] = 0;
        return rect;
    }
    rect[RECT_X] = at.x;
    rect[RECT_Y] = at.y;
    clip_placed(rect, at.clip, &size); /* as in the original, an icon that the clip hides keeps its cut rect */
    rect[RECT_X] -= ICON_FRAME;
    rect[RECT_Y] -= ICON_FRAME;
    rect[RECT_W] += 2 * ICON_FRAME;
    rect[RECT_H] += 2 * ICON_FRAME;
    return rect;
}

ScreenPoint zone_get_position(uint16_t zone)
{
    int16_t r[RECT_SIZE] = {0};
    ScreenSize size = {1, 1};
    zone_compute_rect(NULL, r, zone, &size);
    return (ScreenPoint){r[RECT_X], r[RECT_Y]};
}

uint16_t zone_id_for_view_cell(int16_t base, int16_t grid_cell, int16_t dir)
{
    return base * ZONE_CELL_GRID + aspect_rotate_cell(grid_cell, dir) + ZONE_VIEW_CELLS;
}
