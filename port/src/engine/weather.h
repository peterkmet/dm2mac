#ifndef WEATHER_H
#define WEATHER_H
#include <stdint.h>
#include "gdat.h"

/* Prefixes: weather_ light_ world_ */

/* Weather and day state that the save file does not keep (the saved part is in game). */
typedef struct {
    int16_t strike_threshold;   /* lightning strikes the party square only when global byte 0x40 is above it */
    uint16_t storm_previous;    /* the storm intensity before the last storm step */
    int16_t hour_light;         /* the darkness of the hour: 1 (day) to 5 (night) */
    uint32_t day_time_offset;   /* the game time that the day starts with */
    int first_init;             /* the first weather_init of the run: a short calm start */
    int thunder_toggle;         /* only every second lightning bolt thunders */
    int lightning_flash;        /* a lightning shows in this step: full light */
    int dungeon_loaded;         /* a dungeon load happened in this run: a load keeps the weather */
} Weather;

extern Weather weather;
/* The light of the squares around the party and of the explosions near it; creature_refresh_party_maps (Step 16)
   clears them and adds each square with light_add_square_light. */
extern int16_t light_source_sum, explosion_light_sum;

/* light_add_square_light flags: what of the square gives light. */
enum { LIGHT_FLOOR = 1, LIGHT_CREATURES = 2, LIGHT_WALLS = 4 };
#define MAP_KEY_SKY_STATE 0x66      /* map graphics value key (CAT_WALLSET, set, 0x0b, key): the sky */
#define RAIN_STRONG 0x80            /* rain intensities: strong rain grows the clouds faster */
#define RAIN_MEDIUM 0x40
#define RAIN_LIGHT 0x10

/* Queues the next storm step delay ticks from now. */
void weather_schedule_event(int32_t delay);
/* Starts a new weather cycle (keep_state 0) or keeps the loaded weather (1); sets the light of the hour. */
void weather_init(int keep_state);
/* The weather part of a dungeon load: the time of day of the dungeon, then weather_init. */
void weather_load(const gdat_t *g);
/* The weather of a game step (storm_step 0): hour, rain, clouds, sky objects, lightning; or a storm step (1). */
void world_update_day_night(int storm_step);
/* Computes the party light level (view_light_level) from the hand lights, the square lights, magic, the map, the
   hour, and the weather. */
void light_update_party_light(void);
/* Adds the light of the square (x, y) of the current map, distance squares from the party, seen from dir. */
void light_add_square_light(int distance, int dir, int x, int y, unsigned flags);

#endif
