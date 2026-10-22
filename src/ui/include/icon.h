/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_ICON_H
#define DM2_UI_ICON_H
/* Prefixes: icon_ */

/* The window icon, built from a creature graphic. */

#include "gdat.h"
#include <stdbool.h>
#include <stdint.h>

/* Builds the window icon from the mummy graphic as ARGB, 0 for the see-through color; false when the graphic is missing or does not fit in max pixels. */
bool icon_build(const gdat_t *g, uint32_t *pixels, int max, int *out_width, int *out_height);

#endif /* DM2_UI_ICON_H */
