/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_WEATHER_H
#define DM2_ENGINE_WEATHER_H
/* Prefixes: weather_ light_ world_ */

/* Weather, day and night, and the party light. */

#include "gdat.h"
#include <stdbool.h>
#include <stdint.h>

/* Weather and day state that the save file does not keep (the saved part is in game). */
typedef struct {
    int16_t strike_threshold; /* lightning strikes the party square only when global byte 0x40 is above it */
    uint16_t storm_previous;  /* the storm intensity before the last storm step */
    int16_t hour_light;       /* the darkness of the hour: 1 (day) to 5 (night) */
    uint32_t day_time_offset; /* the game time that the day starts with */
    int first_init;           /* the first weather_init of the run: a short calm start */
    int thunder_toggle;       /* only every second lightning bolt thunders */
    int lightning_flash;      /* a lightning shows in this step: full light */
    int dungeon_loaded;       /* a dungeon load happened in this run: a load keeps the weather */
} Weather;

extern Weather g_weather;
/* The party light level, 0 (bright) to DARKEST_LIGHT (light_update_party_light). */
extern int g_view_light_level;
/* The light of the squares around the party and of the explosions near it; creature_refresh_party_maps (Step 16)
   clears them and adds each square with light_add_square_light. */
extern int16_t g_light_source_sum, g_explosion_light_sum;

/* The g_view_light_level of the darkest view. */
#define DARKEST_LIGHT 5

/* light_add_square_light flags: what of the square gives light. */
#define LIGHT_FLOOR     0x01
#define LIGHT_CREATURES 0x02
#define LIGHT_WALLS     0x04

/* Map graphics value key (CAT_WALLSET, set, 0x0b, key): the sky. */
#define MAP_KEY_SKY_STATE 0x66

/* Rain intensities: strong rain grows the clouds faster. */
#define RAIN_STRONG 128
#define RAIN_MEDIUM 64
#define RAIN_LIGHT  16

/* The sky darkness (game.sky_darkness) from which the rain shows light, medium, heavy; the cloud cover
   (game.cloud_cover) from which the clouds show light, medium, heavy. */
#define DARKNESS_RAIN   16
#define DARKNESS_MEDIUM 64
#define DARKNESS_HEAVY  128
#define CLOUDS_SHOWN    64
#define CLOUDS_MORE     128
#define CLOUDS_MOST     192

/* g_weather.hour_light of the day and of the night; between them dawn and dusk. */
#define HOUR_LIGHT_DAY   1
#define HOUR_LIGHT_NIGHT 5

/* A sky object of the outdoor view (the original record, 10 bytes): rain, clouds, lightning, landmarks. */
typedef struct {
    uint8_t entry;        /* GDAT entry (0x17, graphics set, entry) */
    uint8_t flip;         /* how the picture mirrors */
    int16_t zone, dx, dy; /* layout zone and offset */
    uint8_t scale_x, scale_y;
} SkyObject;

/* The end mark of the sky objects. */
#define SKY_OBJECT_END 0xff

/* The weather sky objects that the view draws, ended by SKY_OBJECT_END (world_update_day_night fills them). */
SkyObject *weather_sky_objects(void);
/* The sky set of the party map has rain pictures. */
bool weather_sky_has_rain(void);
/* The sky set of the party map has cloud pictures. */
bool weather_sky_has_clouds(void);
/* Starts a new weather cycle (keep_state false) or keeps the loaded weather (true); sets the light of the hour.
   exported for the tests */
void weather_init(bool keep_state);
/* The weather part of a dungeon load: the time of day of the dungeon, then weather_init. */
void weather_load(const gdat_t *g);
/* The weather of a game step (storm_step 0): hour, rain, clouds, sky objects, lightning; or a storm step (1). */
void world_update_day_night(int storm_step);
/* Computes the party light level (g_view_light_level) from the hand lights, the square lights, magic, the map, the
   hour, and the weather. */
void light_update_party_light(void);
/* Adds the light of the square (x, y) of the current map, distance squares from the party, seen from dir. */
void light_add_square_light(int distance, int dir, int x, int y, unsigned flags);

#endif /* DM2_ENGINE_WEATHER_H */
