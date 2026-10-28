/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "draw.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "ids.h"
#include "os.h"
#include "video.h"
#include "zone.h"

/* Value key (category, index, 0x0c, 0xfe): offset of all images of an index; the index of the placeholder image. */
#define OFFSET_ENTRY      0xfe
#define PLACEHOLDER_INDEX 0xfe

/* Palette count of a context without a color remap. */
#define NO_PALETTE (-1)

/* Temporary bitmaps of one frame, freed together; the pixels follow the record. */
typedef struct Temp {
    struct Temp *next;
    Bitmap bitmap;
    uint8_t pixels[];
} Temp;

Bitmap g_draw_screen = {&g_video_fb[0][0], SCREEN_W, SCREEN_H, IMAGE_8BPP};

static struct {
    const Gdat *cache_owner; /* the GRAPHICS.DAT of the decoded images */
    Bitmap *images;          /* owned: decoded images by GDAT item, each with owned pixels */
    int image_count;
    Temp *temps; /* owned: the temporary bitmaps of this frame */
} draw_state;

/* Drops the decoded images and makes an empty image cache for the selected GRAPHICS.DAT. */
static void rebuild_cache(void)
{
    for (int i = 0; i < draw_state.image_count; i++)
        free(draw_state.images[i].pixels);
    free(draw_state.images);
    draw_state.cache_owner = gdat_current();
    draw_state.image_count = draw_state.cache_owner ? gdat_item_count(draw_state.cache_owner) : 0;
    draw_state.images = calloc(draw_state.image_count ? draw_state.image_count : 1, sizeof *draw_state.images);
    if (!draw_state.images)
        os_fatal("out of memory");
}

/* Rebuilds the image cache when another GRAPHICS.DAT is selected. */
static void follow_gdat(void)
{
    if (gdat_current() != draw_state.cache_owner)
        rebuild_cache();
}

/* Setup */

void draw_init(const Gdat *g)
{
    gdat_select(g);
    rebuild_cache();
}

void draw_free(void)
{
    draw_free_temp();
    for (int i = 0; i < draw_state.image_count; i++)
        free(draw_state.images[i].pixels);
    free(draw_state.images);
    draw_state.images = NULL;
    draw_state.image_count = 0;
    draw_state.cache_owner = NULL;
}

/* Images */

/* The decoded image of a GDAT item, decoded on first use; NULL when it cannot be decoded. */
static const Bitmap *decoded(int item)
{
    follow_gdat();
    if (item < 0 || item >= draw_state.image_count)
        return NULL;
    Bitmap *b = draw_state.images + item;
    if (b->pixels)
        return b;
    ImageInfo info;
    if (gdat_image_info(draw_state.cache_owner, item, &info) != GDAT_OK)
        return NULL;
    b->pixels = malloc(info.size ? info.size : 1);
    if (!b->pixels)
        return NULL;
    if (gdat_decode_image(draw_state.cache_owner, item, b->pixels, info.size) != GDAT_OK) {
        free(b->pixels);
        b->pixels = NULL;
        return NULL;
    }
    b->width = (int16_t)info.width;
    b->height = (int16_t)info.height;
    b->bpp = (uint8_t)info.bpp;
    return b;
}

/* The GDAT item of an image, or of the placeholder image when it is missing. */
static int find_item(int category, int index, int entry)
{
    const Gdat *gdat = gdat_current();
    int item = gdat_find(gdat, category, index, GDAT_TYPE_IMAGE, entry);
    return item >= 0 ? item : gdat_find(gdat, CAT_MISC, PLACEHOLDER_INDEX, GDAT_TYPE_IMAGE, GFX_PLACEHOLDER);
}

const Bitmap *draw_gdat_image(int category, int index, int entry)
{
    return gdat_current() ? decoded(find_item(category, index, entry)) : NULL;
}

const uint8_t *draw_gdat_image_palette(int category, int index, int entry)
{
    const Gdat *gdat = gdat_current();
    return gdat ? gdat_image_colormap(gdat, find_item(category, index, entry)) : NULL;
}

/* Temporary bitmaps */

Bitmap *draw_temp_bitmap(int width, int height, int bpp)
{
    size_t size = bpp == IMAGE_4BPP ? (size_t)(width + 1) / 2 * height : (size_t)width * height;
    Temp *t = malloc(sizeof *t + (size ? size : 1));
    if (!t)
        return NULL;
    t->bitmap = (Bitmap){t->pixels, (int16_t)width, (int16_t)height, (uint8_t)bpp};
    t->next = draw_state.temps;
    draw_state.temps = t;
    return &t->bitmap;
}

void draw_free_temp(void)
{
    while (draw_state.temps) {
        Temp *next = draw_state.temps->next;
        free(draw_state.temps);
        draw_state.temps = next;
    }
}

/* Contexts */

/* Adds a packed offset (x in the high byte, y in the low byte) to the offset of ctx. */
static void add_offset(DrawCtx *ctx, int packed)
{
    ctx->dx += gdat_offset_x(packed);
    ctx->dy += gdat_offset_y(packed);
}

DrawCtx *draw_ctx_init(DrawCtx *ctx, int category, int index, int entry)
{
    memset(ctx, 0, sizeof *ctx);
    ctx->category = (uint8_t)category;
    ctx->index = (uint8_t)index;
    ctx->entry = (uint8_t)entry;
    ctx->scale_x = SCALE_FULL;
    ctx->scale_y = SCALE_FULL;
    ctx->zone = (int16_t)ZONE_NONE;
    ctx->anchor = ZONE_NO_ANCHOR;
    ctx->dst = &g_draw_screen;
    ctx->transparent = GFX_OPAQUE;
    const Gdat *gdat = gdat_current();
    if ((uint8_t)category == CAT_NONE || !gdat)
        return ctx;
    ctx->item = (int16_t)gdat_entry_index(gdat, category, index, GDAT_TYPE_IMAGE, entry);
    add_offset(ctx, (int16_t)gdat_entry_index(gdat, category, index, GDAT_TYPE_VALUE2, OFFSET_ENTRY));
    add_offset(ctx, (int16_t)gdat_image_offset(gdat, category, index, entry));
    const uint8_t *colors = draw_gdat_image_palette(category, index, entry);
    ctx->palette_count = colors ? IMAGE_COLORS : NO_PALETTE;
    if (colors)
        memcpy(ctx->palette, colors, IMAGE_COLORS);
    return ctx;
}

void draw_ctx_set_image(DrawCtx *ctx, const Bitmap *image)
{
    ctx->image = image;
    ctx->x = 0;
    ctx->y = 0;
    ctx->w = image ? image->width : 0;
    ctx->h = image ? image->height : 0;
}

DrawCtx *draw_ctx_prepare(DrawCtx *ctx)
{
    bool scaled = ctx->scale_x != SCALE_FULL || ctx->scale_y != SCALE_FULL;
    bool given = ctx->item == -1 && ctx->image;
    if (scaled) {
        ctx->dx = gfx_scale_64(ctx->dx, ctx->scale_x);
        ctx->dy = gfx_scale_64(ctx->dy, ctx->scale_y);
    }
    if (ctx->flip & MIRROR_H)
        ctx->dx = (int16_t)-ctx->dx;
    if (ctx->transparent == DRAW_NO_IMAGE)
        return ctx;
    const Bitmap *src = given ? ctx->image : draw_gdat_image(ctx->category, ctx->index, ctx->entry);
    if (!scaled || !src) {
        draw_ctx_set_image(ctx, src);
        return ctx;
    }
    int16_t w = gfx_scale_64(src->width, ctx->scale_x);
    int16_t h = gfx_scale_64(src->height, ctx->scale_y);
    if (w < 1 || h < 1) {
        ctx->w = w;
        ctx->h = h;
        return ctx;
    }
    Bitmap *out = draw_temp_bitmap(w, h, src->bpp);
    if (!out)
        return ctx;
    if (src->bpp == IMAGE_8BPP)
        gfx_blit_scaled_8bpp(src, out);
    else
        gfx_blit_scaled_4bpp(src, out, NULL);
    draw_ctx_set_image(ctx, out);
    return ctx;
}

/* The source start of a mirrored blit: the part of the image beyond the drawn part. */
static int16_t mirrored_start(int16_t start, int16_t size, int16_t image_size, bool mirrored)
{
    int16_t end = (int16_t)(size + start);
    return mirrored && image_size > end ? (int16_t)(image_size - end) : 0;
}

void draw_ctx_draw(DrawCtx *ctx)
{
    if (ctx->w <= 0 || ctx->h <= 0 || !ctx->image)
        return;
    const Bitmap *image = ctx->image;
    int zone = (uint16_t)ctx->zone;
    int16_t sx = ctx->src_x;
    int16_t sy = ctx->src_y;
    if (zone != ZONE_NONE) {
        if (!(zone & ZONE_RELATIVE) && !ctx->dx && !ctx->dy) {
            sx = ctx->w;
            sy = ctx->h;
        } else {
            zone |= ZONE_RELATIVE;
            sx = (int16_t)(sx + ctx->dx);
            sy = (int16_t)(sy + ctx->dy);
        }
        if (!zone_compute_anchored_rect(image, ctx->rect, (AnchoredZone){zone, ctx->anchor}, &sx, &sy))
            return;
    }
    sx = (int16_t)(sx + ctx->x);
    sy = (int16_t)(sy + ctx->y);
    int16_t fx = mirrored_start(sx, ctx->rect[RECT_W], image->width, ctx->flip & MIRROR_H);
    if (ctx->flip & MIRROR_H)
        sx = 0;
    sx = (int16_t)(sx + fx);
    int16_t fy = mirrored_start(sy, ctx->rect[RECT_H], image->height, ctx->flip & MIRROR_V);
    if (ctx->flip & MIRROR_V)
        sy = 0;
    sy = (int16_t)(sy + fy);
    if (ctx->transparent != DRAW_NO_BLIT) {
        Blit blit = {.src = image->pixels,
                     .dst = ctx->dst->pixels,
                     .src_x = sx,
                     .src_y = sy,
                     .src_width = image->width,
                     .dst_width = ctx->dst->width,
                     .src_bpp = image->bpp,
                     .dst_bpp = ctx->dst->bpp,
                     .transparent = ctx->transparent,
                     .mirror = ctx->flip,
                     .colormap = ctx->palette_count > 0 ? ctx->palette : NULL};
        gfx_blit(&blit, ctx->rect);
    }
    ctx->src_x = sx;
    ctx->src_y = sy;
}
