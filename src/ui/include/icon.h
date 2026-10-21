/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_ICON_H
#define DM2_UI_ICON_H
/* Prefixes: icon_ */

/* The window icon, built from a creature graphic. */

#include "gdat.h"
#include <stdint.h>

/* Builds the window icon from the mummy graphic as ARGB, 0 for the see-through color; returns 0 when the graphic is missing or does not fit in max pixels. */
int icon_build(const gdat_t *g, uint32_t *argb, int max, int *w, int *h);

#endif /* DM2_UI_ICON_H */
