#ifndef ICON_H
#define ICON_H
#include <stdint.h>
#include "gdat.h"

/* Prefixes: icon_ */

/* Builds the window icon from the mummy graphic as ARGB, 0 for the see-through color; returns 0 when the graphic is missing or does not fit in max pixels. */
int icon_build(const gdat_t *g, uint32_t *argb, int max, int *w, int *h);

#endif
