/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_DISCARD_H
#define DM2_ENGINE_DISCARD_H
/* Prefixes: discard_ */

/* The discard search: a full dungeon frees a record of a thing far from the party. */

#include <stdint.h>
#include "dungeon.h"

/* The subtype of discard_find_thing for a thing of any kind. */
#define DISCARD_ANY_KIND 0xff
/* The map where the last search for each thing type ended; the next search starts there; exported for the tests. */
extern uint8_t g_last_discarded_map[THING_TYPE_COUNT];
/* Frees a record of type for reuse when the table is full: removes a thing of kind subtype (DISCARD_ANY_KIND:
   any) that may go, away from the party; THING_NONE when there is none. */
uint16_t discard_find_thing(int type, int subtype);

#endif /* DM2_ENGINE_DISCARD_H */
