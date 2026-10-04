#include <stdlib.h>
#include <string.h>
#include "draw.h"
#include "video.h"
#include "zone.h"

#define OFFSET_ENTRY 0xfe        /* value key (category, index, 0x0c, 0xfe): offset of all images of an index */
#define PLACEHOLDER_CATEGORY 0x15  /* the image that stands in for a missing image */
#define PLACEHOLDER_INDEX 0xfe
#define PLACEHOLDER_ENTRY 0xfe
#define IMAGE_COLORS 16
#define NO_PALETTE (-1)

Bitmap draw_screen = {&fb[0][0], SCREEN_W, SCREEN_H, 8};

static const gdat_t *gdat;
static Bitmap *images;   /* decoded images by GDAT item */
static int image_count;

/* Temporary bitmaps of one frame, freed together. */
typedef struct Temp {
    struct Temp *next;
    Bitmap bitmap;
} Temp;
static Temp *temps;

void draw_init(const gdat_t *g)
{
    for (int i = 0; i < image_count; i++) free(images[i].pixels);
    free(images);
    gdat = g;
    image_count = g ? gdat_item_count(g) : 0;
    images = calloc(image_count ? image_count : 1, sizeof *images);
    if (!images) image_count = 0;
}

const gdat_t *draw_gdat(void) { return gdat; }

static const Bitmap *decoded(int item)
{
    if (item < 0 || item >= image_count) return NULL;
    Bitmap *b = images + item;
    if (b->pixels) return b;
    int w, h, bpp;
    int size = gdat_image_size(gdat, item, &w, &h, &bpp);
    if (size < 0 || !(b->pixels = malloc(size ? size : 1))) return NULL;
    if (gdat_decode_image(gdat, item, b->pixels, size) != GDAT_OK) {
        free(b->pixels);
        b->pixels = NULL;
        return NULL;
    }
    b->width = (int16_t)w;
    b->height = (int16_t)h;
    b->bpp = (uint8_t)bpp;
    return b;
}

static int image_item(int category, int index, int entry)
{
    int item = gdat_find(gdat, category, index, GDAT_TYPE_IMAGE, entry);
    return item >= 0 ? item : gdat_find(gdat, PLACEHOLDER_CATEGORY, PLACEHOLDER_INDEX, GDAT_TYPE_IMAGE, PLACEHOLDER_ENTRY);
}

const Bitmap *draw_gdat_image(int category, int index, int entry)
{
    return gdat ? decoded(image_item(category, index, entry)) : NULL;
}

const uint8_t *draw_gdat_image_palette(int category, int index, int entry)
{
    return gdat ? gdat_image_colormap(gdat, image_item(category, index, entry)) : NULL;
}

Bitmap *draw_temp_bitmap(int width, int height, int bpp)
{
    size_t size = bpp == 4 ? (size_t)(width + 1) / 2 * height : (size_t)width * height;
    Temp *t = malloc(sizeof *t + (size ? size : 1));
    if (!t) return NULL;
    t->bitmap = (Bitmap){(uint8_t *)(t + 1), (int16_t)width, (int16_t)height, (uint8_t)bpp};
    t->next = temps;
    temps = t;
    return &t->bitmap;
}

void draw_free_temp(void)
{
    while (temps) {
        Temp *next = temps->next;
        free(temps);
        temps = next;
    }
}

/* Adds a packed offset (x in the high byte, y in the low byte) to the offset of ctx. */
static void add_offset(DrawCtx *ctx, int packed)
{
    ctx->dx += (int8_t)(packed >> 8);
    ctx->dy += (int8_t)packed;
}

DrawCtx *gfx_ctx_init(DrawCtx *ctx, int category, int index, int entry)
{
    memset(ctx, 0, sizeof *ctx);
    ctx->category = (uint8_t)category;
    ctx->index = (uint8_t)index;
    ctx->entry = (uint8_t)entry;
    ctx->scale_x = ctx->scale_y = SCALE_FULL;
    ctx->zone = (int16_t)ZONE_NONE;
    ctx->anchor = ZONE_NO_ANCHOR;
    ctx->dst = &draw_screen;
    ctx->transparent = GFX_OPAQUE;
    if ((uint8_t)category == CATEGORY_NONE || !gdat) return ctx;
    ctx->item = (int16_t)gdat_entry_index(gdat, category, index, GDAT_TYPE_IMAGE, entry);
    add_offset(ctx, (int16_t)gdat_entry_index(gdat, category, index, GDAT_TYPE_VALUE2, OFFSET_ENTRY));
    add_offset(ctx, (int16_t)gdat_image_offset(gdat, category, index, entry));
    const uint8_t *palette = draw_gdat_image_palette(category, index, entry);
    ctx->palette_count = palette ? IMAGE_COLORS : NO_PALETTE;
    if (palette) memcpy(ctx->palette, palette, IMAGE_COLORS);
    return ctx;
}

void gfx_ctx_set_image(DrawCtx *ctx, const Bitmap *image)
{
    ctx->image = image;
    ctx->x = ctx->y = 0;
    ctx->w = image ? image->width : 0;
    ctx->h = image ? image->height : 0;
}

DrawCtx *gfx_ctx_prepare(DrawCtx *ctx)
{
    int scaled = ctx->scale_x != SCALE_FULL || ctx->scale_y != SCALE_FULL;
    int given = ctx->item == -1 && ctx->image;
    if (scaled) {
        ctx->dx = gfx_scale_64(ctx->dx, ctx->scale_x);
        ctx->dy = gfx_scale_64(ctx->dy, ctx->scale_y);
    }
    if (ctx->flip & MIRROR_H) ctx->dx = (int16_t)-ctx->dx;
    if (ctx->transparent == DRAW_NO_IMAGE) return ctx;
    const Bitmap *src = given ? ctx->image : draw_gdat_image(ctx->category, ctx->index, ctx->entry);
    if (!scaled || !src) {
        gfx_ctx_set_image(ctx, src);
        return ctx;
    }
    int16_t w = gfx_scale_64(src->width, ctx->scale_x), h = gfx_scale_64(src->height, ctx->scale_y);
    if (w < 1 || h < 1) {
        ctx->w = w;
        ctx->h = h;
        return ctx;
    }
    Bitmap *out = draw_temp_bitmap(w, h, src->bpp);
    if (!out) return ctx;
    if (src->bpp == 8) gfx_blit_scaled_8bpp(src->pixels, out->pixels, src->width, src->height, w, h);
    else gfx_blit_scaled_4bpp(src->pixels, out->pixels, src->width, src->height, w, h, NULL);
    gfx_ctx_set_image(ctx, out);
    return ctx;
}

/* The source start of a mirrored blit: the part of the image beyond the drawn part. */
static int16_t mirrored_start(int16_t start, int16_t size, int16_t image_size, int mirrored)
{
    int16_t end = (int16_t)(size + start);
    return mirrored && image_size > end ? (int16_t)(image_size - end) : 0;
}

void gfx_ctx_draw(DrawCtx *ctx)
{
    if (ctx->w <= 0 || ctx->h <= 0 || !ctx->image) return;
    const Bitmap *image = ctx->image;
    int zone = (uint16_t)ctx->zone;
    int16_t sx = ctx->src_x, sy = ctx->src_y;
    if (zone != ZONE_NONE) {
        if (!(zone & ZONE_RELATIVE) && !ctx->dx && !ctx->dy) {
            sx = ctx->w;
            sy = ctx->h;
        } else {
            zone |= ZONE_RELATIVE;
            sx = (int16_t)(sx + ctx->dx);
            sy = (int16_t)(sy + ctx->dy);
        }
        if (!zone_compute_rect(image, ctx->rect, zone, &sx, &sy, ctx->anchor)) return;
    }
    sx = (int16_t)(sx + ctx->x);
    sy = (int16_t)(sy + ctx->y);
    int16_t fx = mirrored_start(sx, ctx->rect[RECT_W], image->width, ctx->flip & MIRROR_H);
    if (ctx->flip & MIRROR_H) sx = 0;
    sx = (int16_t)(sx + fx);
    int16_t fy = mirrored_start(sy, ctx->rect[RECT_H], image->height, ctx->flip & MIRROR_V);
    if (ctx->flip & MIRROR_V) sy = 0;
    sy = (int16_t)(sy + fy);
    if (ctx->transparent != DRAW_NO_BLIT)
        gfx_blit(image->pixels, ctx->dst->pixels, ctx->rect, sx, sy, image->width, ctx->dst->width, ctx->transparent,
                 ctx->flip, image->bpp, ctx->dst->bpp, ctx->palette_count > 0 ? ctx->palette : NULL);
    ctx->src_x = sx;
    ctx->src_y = sy;
}
