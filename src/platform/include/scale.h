/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_PLATFORM_SCALE_H
#define DM2_PLATFORM_SCALE_H
/* Prefixes: scale */

/* The pixel upscale filters of the window: none, Scale2x and Scale3x. */

#include <stdint.h>

/* AdvMAME Scale2x on palette indices; dst is (w*2) x (h*2), edges replicate. */
void scale2x(const uint8_t *src, int w, int h, uint8_t *dst);
/* AdvMAME Scale3x on palette indices; dst is (w*3) x (h*3), edges replicate. */
void scale3x(const uint8_t *src, int w, int h, uint8_t *dst);

#endif /* DM2_PLATFORM_SCALE_H */
