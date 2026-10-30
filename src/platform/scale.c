/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "scale.h"
#include <stdbool.h>
#include <string.h>

/* Columns of a 3 by 3 output block of Scale3x. */
#define COLUMN_LEFT   0
#define COLUMN_MIDDLE 1
#define COLUMN_RIGHT  2

/* The source pixel in column and row; positions outside the image repeat the edge pixel. */
static uint8_t pixel(const uint8_t *src, int w, int h, int column, int row)
{
    column = column < 0 ? 0 : column >= w ? w - 1 : column;
    row = row < 0 ? 0 : row >= h ? h - 1 : row;
    return src[row * w + column];
}

void scale2x(const uint8_t *src, int w, int h, uint8_t *dst)
{
    /* Neighbors use the AdvMAME names, upper case against rule 4.6: B above, D E F on the row (E the pixel), H below. */
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t B = pixel(src, w, h, x, y - 1);
            uint8_t D = pixel(src, w, h, x - 1, y);
            uint8_t E = pixel(src, w, h, x, y);
            uint8_t F = pixel(src, w, h, x + 1, y);
            uint8_t H = pixel(src, w, h, x, y + 1);
            uint8_t *o = dst + SCALE2X_FACTOR * y * SCALE2X_FACTOR * w + SCALE2X_FACTOR * x;
            bool on = B != H && D != F;
            o[0] = on && D == B ? D : E;
            o[1] = on && B == F ? F : E;
            o[SCALE2X_FACTOR * w] = on && D == H ? D : E;
            o[SCALE2X_FACTOR * w + 1] = on && H == F ? F : E;
        }
}

void scale3x(const uint8_t *src, int w, int h, uint8_t *dst)
{
    /* Neighbors use the AdvMAME names, upper case against rule 4.6: A B C above, D E F on the row (E the pixel), G H I
       below. */
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t A = pixel(src, w, h, x - 1, y - 1);
            uint8_t B = pixel(src, w, h, x, y - 1);
            uint8_t C = pixel(src, w, h, x + 1, y - 1);
            uint8_t D = pixel(src, w, h, x - 1, y);
            uint8_t E = pixel(src, w, h, x, y);
            uint8_t F = pixel(src, w, h, x + 1, y);
            uint8_t G = pixel(src, w, h, x - 1, y + 1);
            uint8_t H = pixel(src, w, h, x, y + 1);
            uint8_t I = pixel(src, w, h, x + 1, y + 1);
            uint8_t *o = dst + SCALE3X_FACTOR * y * SCALE3X_FACTOR * w + SCALE3X_FACTOR * x;
            uint8_t *o1 = o + SCALE3X_FACTOR * w;
            uint8_t *o2 = o1 + SCALE3X_FACTOR * w;
            memset(o, E, SCALE3X_FACTOR);
            memset(o1, E, SCALE3X_FACTOR);
            memset(o2, E, SCALE3X_FACTOR);
            if (B == H || D == F)
                continue;
            o[COLUMN_LEFT] = D == B ? D : E;
            o[COLUMN_MIDDLE] = (D == B && E != C) || (B == F && E != A) ? B : E;
            o[COLUMN_RIGHT] = B == F ? F : E;
            o1[COLUMN_LEFT] = (D == B && E != G) || (D == H && E != A) ? D : E;
            o1[COLUMN_RIGHT] = (B == F && E != I) || (H == F && E != C) ? F : E;
            o2[COLUMN_LEFT] = D == H ? D : E;
            o2[COLUMN_MIDDLE] = (D == H && E != I) || (H == F && E != G) ? H : E;
            o2[COLUMN_RIGHT] = H == F ? F : E;
        }
}
