/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_DRAW_H
#define DM2_UI_DRAW_H
/* Prefixes: draw_ */

/* Drawing of GDAT images into the frame buffer: blits, transparency and the interface colors. */

#include <stdint.h>
#include "gdat.h"
#include "gfx.h"

/* What a context does: draw its image, only compute its rectangle, or not even load the image. */
enum DrawMode { DRAW_BLIT, DRAW_RECT_ONLY, DRAW_NO_IMAGE };

/* Colors of a 4 bpp picture. */
#define IMAGE_COLORS 16

/* A picture to draw: an image (or a part of it), where it goes, and how. */
typedef struct {
    const Bitmap *image;
    int16_t x, y, w, h; /* the part of the image */
    uint8_t category, index, entry;
    int16_t item;         /* GDAT item of the image, -1 for an image that the caller gives */
    int16_t zone, anchor; /* layout zone (ZONE_NONE: rect is given), anchor override */
    int16_t dx, dy;       /* offset of the image in its zone */
    int16_t src_x, src_y; /* after drawing: the source position of rect */
    int16_t rect[RECT_SIZE];
    Bitmap *dst;
    int16_t transparent;
    enum DrawMode mode;
    int16_t flip;             /* mirror mode */
    int16_t scale_x, scale_y; /* SCALE_FULL is 100 % */
    int16_t palette_count;    /* entries of palette, <= 0: no color remap */
    uint8_t palette[REMAP_COLORS];
} DrawCtx;

/* The game screen as a bitmap. */
extern Bitmap g_draw_screen;

/* Setup */
/* Selects the GRAPHICS.DAT of the images; frees the decoded images of the last file. */
void draw_init(const Gdat *gdat);
/* Frees the temporary bitmaps and the decoded images; safe to call twice. */
void draw_free(void);

/* Images */
/* The decoded image (category, index, 1, entry); a missing image gives the GDAT placeholder image. */
const Bitmap *draw_gdat_image(int category, int index, int entry);
/* The 16 colors of a 4 bpp image, NULL for an 8 bpp image. */
const uint8_t *draw_gdat_image_palette(int category, int index, int entry);

/* Temporary bitmaps */
/* A temporary bitmap (not cleared) that lives until draw_free_temp; NULL when memory is out. */
Bitmap *draw_temp_bitmap(int width, int height, int bpp);
/* Frees all temporary bitmaps. */
void draw_free_temp(void);

/* Contexts */
/* Starts a context for the GDAT image (category, index, entry) at its GDAT offset, to the screen. */
DrawCtx *draw_ctx_init(DrawCtx *ctx, int category, int index, int entry);
/* Uses image as the picture of ctx. */
void draw_ctx_set_image(DrawCtx *ctx, const Bitmap *image);
/* Loads and scales the image of ctx. */
DrawCtx *draw_ctx_prepare(DrawCtx *ctx);
/* Draws the image of ctx into its destination. */
void draw_ctx_draw(DrawCtx *ctx);

#endif /* DM2_UI_DRAW_H */
