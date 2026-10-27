/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_ICON_H
#define DM2_UI_ICON_H
/* Prefixes: icon_ */

/* The window icon, built from a creature graphic. */

#include <stdbool.h>
#include <stdint.h>
#include "gdat.h"

/* The size of the window icon in pixels. */
typedef struct {
    int width, height;
} IconSize;

/* Builds the window icon from the mummy graphic as ARGB, 0 for the see-through color; {0, 0} when the graphic is
   missing or does not fit in max pixels. */
IconSize icon_build(const Gdat *g, uint32_t *pixels, int max);

#endif /* DM2_UI_ICON_H */
