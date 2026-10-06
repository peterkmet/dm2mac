#ifndef HOOKS_H
#define HOOKS_H
#include <stdint.h>
#include "sound_request.h"
#include "timeline.h"
#include "projectile.h"

/* Calls of the movement, sensor, and main loop code into systems of later steps. Each hook is empty or
   returns the "nothing happened" value until its step ports it; the step then moves it to its module. */

/* Step 20: end of game. */
void game_complete(void);
/* The level digit table (dungeon_read_level_table): the map icon set of the squares outside the maps of a level;
   returns 0 until Step 20 reads the table. */
int dungeon_level_digit(int level);

#endif
