#ifndef HOOKS_H
#define HOOKS_H
#include <stdint.h>
#include "sound_request.h"
#include "timeline.h"
#include "projectile.h"

/* Calls of the movement, sensor, and main loop code into systems of later steps. Each hook is empty or
   returns the "nothing happened" value until its step ports it; the step then moves it to its module. */

#endif
