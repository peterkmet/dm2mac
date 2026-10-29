/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Weather, day and night, and the party light. */
#include "weather.h"
#include "aspect.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "game.h"
#include "gdat.h"
#include "gfx.h"
#include "hooks.h"
#include "ids.h"
#include "item.h"
#include "move.h"
#include "num.h"
#include "projectile.h"
#include "random.h"
#include "save.h"
#include "sensor.h"
#include "sound_request.h"

/* Sound (CAT_EXPLOSION, 0x30, 0x81): a lightning that hits a fixed square. */
#define LIGHTNING_SOUND_INDEX 0x30

/* Sound (CAT_SKY, graphics set, 0): the thunder. */
#define THUNDER_PRIORITY 0x19
#define THUNDER_VOLUME   0x40

/* Game ticks of one hour, hours of a day, and the last hour. */
#define HOUR_TICKS    1365
#define HOURS_PER_DAY 24
#define LAST_HOUR     23

/* Map graphics value keys (CAT_WALLSET, graphics set, GDAT_TYPE_VALUE, key). x is in the high byte of the lightning
   target, y in the low byte. */
#define MAP_KEY_LIGHT             0x67
#define MAP_KEY_LEAST_LIGHT_LEVEL 0x68
#define MAP_KEY_LIGHTNING_TARGET  0x6c

/* Ornament value keys: the light of the ornament, and a window ornament whose light follows the hour. */
#define ORNAMENT_KEY_LIGHT    0xf8
#define ORNAMENT_KEY_DAYLIGHT 0x63

/* The light value of an ornament: bit 15 only while it animates, the rest the amount. */
#define LIGHT_ANIMATED_ONLY 0x8000
#define LIGHT_AMOUNT_MASK   0x7fff

/* Masks of the random draws. */
#define LIGHTNING_STRIKES_MASK 0x07
#define CLOUD_SLOW_MASK        0x03
#define STORM_SPAN_JITTER      0x0f

/* A creature info byte that lightning does not strike. */
#define CREATURE_LIGHTNING_PROOF 0xff

/* Script variables of the weather: the storms counted (cleared by each lightning of a weak storm), the lightning
   strikes seen near the party from the inventory or asleep. */
#define VAR_STORMS       0x40
#define VAR_STRIKES_SEEN 0x41

/* The shift of the hand lights: the first counts fully, each next one half as much. */
#define HAND_LIGHT_SHIFT 6

/* Sizes of the sky, the storm curves, and the light tables. */
#define SKY_LIGHTNING_KINDS 3 /* the number of lightning pictures from GFX_SKY_LIGHTNING */
#define SKY_STATE_COUNT     5 /* the sky states (MAP_KEY_SKY_STATE) of the map graphics */
#define SKY_OBJECT_MAX      5 /* sky objects of the weather, with the end mark */
#define STORM_STEPS         32
#define STORM_CURVE_COUNT   4 /* rand_2bits picks the curve of a storm cycle */
#define LIGHT_LEVEL_MAX     5
#define FALLOFF_WEATHER_MAX 8
#define FALLOFF_MAX         5
#define HAND_LIGHTS_MAX     (1 + SLOT_HAND_COUNT * CHAMPION_MAX)

/* Storm intensity and cloud cover: limits and the thresholds of lightning. */
#define CLOUDS_MAX           255
#define STORM_MAX            255
#define STORM_STRONG         205 /* from this storm intensity on lightning comes more often */
#define STORM_WEAK           182 /* below this storm intensity lightning does not hit the party */
#define STORM_LIGHTNING_HITS 60  /* rand_below(storm + 1) from this value: the lightning hits squares */

/* Ticks and random spans of the storm steps. */
#define STORM_STEP_DELAY   50 /* plus 0 to 255 ticks between storm steps */
#define STORM_RANDOM_SPAN  256
#define STORM_FIRST_DELAY  500  /* ticks to the first storm step of a run */
#define STORM_NEXT_DELAY   8000 /* plus STORM_FIRST_DELAY: ticks to the storm of a new cycle */
#define CLOUD_STEP_TICKS   3    /* clouds change at most each third tick */
#define THUNDER_CALM_DELAY 10   /* plus 5: thunder delay without a storm */
#define THUNDER_CALM_BASE  5
#define THUNDER_DELAY_BASE 76
#define THUNDER_DELAY_MAX  15

/* Chances and counts of a new weather cycle, of rain, and of lightning. */
#define CALM_CURVE              3
#define STORM_MULTIPLIER_MAX    3 /* a new storm cycle is 1 to 3 times as strong as its curve */
#define STRIKE_THRESHOLD_MIN    4 /* storms before lightning may hit the party: 4 to 7 */
#define STRIKE_THRESHOLD_SPAN   4
#define RAIN_CHANCE_MAX         7 /* rand_below(span) up to this starts rain */
#define LIGHTNING_CHANCE_WEAK   7
#define LIGHTNING_CHANCE_STRONG 40
#define LIGHTNING_CHANCE_CALM   64  /* 1 in 64 without a storm */
#define LIGHTNING_RANGE         32  /* random squares 0 to 31 */
#define LIGHTNING_PERCENT       100 /* the bolt stands at 0 to 99 % between its zone and the next zone */

/* Distances and time of lightning strikes. */
#define STRIKE_NEAR       2     /* square distance of a strike that always hits a creature square */
#define STRIKE_SEEN       3     /* square distance of a strike in front of the party */
#define STRIKE_PARTY_TIME 12000 /* game time before which lightning never hits the party square */

/* Light levels and square light values. */
#define LIGHT_LEVEL_LIT  1
#define EXPLOSION_GLARE  12 /* explosion light above which the view gets one level brighter (glare) */
#define WINDOW_LIGHT_MIN 3
#define SQUARE_LIGHT_MIN 2
#define MISSILE_FALLOFF  3

/* The light that one square gives: window light by the hour, other light, and the light of missiles and explosions. */
typedef struct {
    int16_t window, light, missiles;
} SquareLight;

/* The sky kinds of the map graphics: the weather shows (rain, clouds, lightning) in kinds 1 and 4. */
static const bool sky_state_weather[SKY_STATE_COUNT] = {false, true, false, false, true};
/* The darkness of each hour of the day. */
static const int8_t light_by_hour[HOURS_PER_DAY] = {5, 5, 4, 3, 2, 1, 1, 1, 1, 1, 1, 1,
                                                    1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5, 5};
/* The storm curves: the change of the storm intensity in each of the 32 storm steps. */
static const int8_t storm_curves[STORM_CURVE_COUNT][STORM_STEPS] = {
    {1,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5,  -5,  -5,  -1 },
    {1,  2,  2,  2,  4,  4,  6,  6,  6,  7,  8,  9,  7,  5,  4,  3,  -3, -4, -5, -7, -9, -8, -7, -6, -6, -6, -4, -4, -2,  -2,  -2,  -1 },
    {10, 18, 22, 26, -4, -4, -4, -4, -4, -4, -4, -4, -3, -3, -3, -3,
     -2,                                                                 -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,  -2,  -2,  -2 },
    {2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3,  4,  4,  4,  4,  4,  4,  4,  4,  -26, -22, -18, -10},
};
/* The light of a torch by its charges, of a magic light by its level or power. */
const int8_t g_light_power[LIGHT_POWER_COUNT] = {0, 5, 12, 24, 33, 40, 46, 51, 59, 68, 76, 82, 89, 94, 97, 100};
/* The light that each light level needs (level 0 above 99), and the daylight in % by the darkness of the hour. */
static const int8_t light_levels[LIGHT_LEVEL_MAX + 1] = {99, 75, 50, 25, 1, 0};
/* What a square light loses by its distance to the party: window light, other light. */
static const int16_t window_falloff[FALLOFF_WEATHER_MAX + 1] = {0, 6, 14, 30, 42, 54, 76, 88, 96};
static const int16_t light_falloff[FALLOFF_MAX + 1] = {0, 10, 22, 45, 70, 90};
/* Byte +2 of the creature info records of SKULL.EXE (meaning not known): 0xff, lightning does not strike there. */
static const uint8_t creature_lightning[CREATURE_INFO_COUNT] = {
    255, 255, 255, 100, 255, 255, 255, 255, 255, 80,  255, 255, 255, 130, 150, 130, 130, 200, 110, 85,  255,
    150, 100, 50,  65,  80,  50,  70,  100, 135, 175, 220, 120, 180, 75,  50,  160, 255, 255, 255, 220, 100,
    65,  75,  115, 255, 70,  255, 100, 75,  115, 255, 90,  255, 255, 140, 255, 100, 110, 140, 255, 100, 75,
};

Weather g_weather = {.first_init = true};
int16_t g_light_source_sum, g_explosion_light_sum;
int g_view_light_level;
/* The sky objects of the weather, ended by SKY_OBJECT_END. */
static SkyObject sky_objects[SKY_OBJECT_MAX] = {{.entry = SKY_OBJECT_END}};

static uint16_t map_value(int key)
{
    return gdat_entry_index(gdat_current(), CAT_WALLSET, dungeon_map_wallset(g_game.party_map), GDAT_TYPE_VALUE, key);
}

static bool sky_has(int entry)
{
    return gdat_exists(gdat_current(), CAT_SKY, dungeon_map_wallset(g_game.party_map), GDAT_TYPE_IMAGE, entry);
}

/* Sky */

SkyObject *weather_sky_objects(void)
{
    return sky_objects;
}

bool weather_sky_has_rain(void)
{
    return sky_has(GFX_SKY_RAIN_LIGHT);
}
bool weather_sky_has_clouds(void)
{
    return sky_has(GFX_SKY_CLOUDS_LIGHT);
}

bool weather_is_shown(uint16_t sky_state)
{
    return sky_state < SKY_STATE_COUNT && sky_state_weather[sky_state]; /* fix: no read past the table */
}

/* Weather and day */

/* Reads the sky object entry of the map graphics into object; false when the map has none. */
static bool load_sky_object(SkyObject *object)
{
    char text[WALL_TEXT_SIZE];
    gdat_text(gdat_current(), (GdatKey){CAT_SKY, dungeon_map_wallset(g_game.party_map), object->entry}, text,
              sizeof text);
    if (!text[0])
        return false;
    object->zone = (int16_t)gdat_text_int_field(text, "cd");
    object->dx = 0;
    object->dy = 0;
    object->flip = (uint8_t)gdat_text_int_field(text, "fw");
    object->scale_x = SCALE_FULL;
    object->scale_y = SCALE_FULL;
    return true;
}

static void set_hour_light(void)
{
    g_weather.hour_light = light_by_hour[(g_game.game_time + g_weather.day_time_offset) / HOUR_TICKS % HOURS_PER_DAY];
}

/* Queues the next storm step delay ticks from now. */
static void schedule_storm_step(int32_t delay)
{
    TimelineEvent e = {.time_map = g_game.game_time + (uint32_t)delay, .type = EVENT_WEATHER};
    timeline_add_event(&e); /* fix: the original leaves the data bytes uninitialized */
}

void weather_init(bool keep_state)
{
    if (keep_state) {
        g_weather.storm_previous = 0;
        if (!g_game.storm_multiplier)
            g_game.storm_multiplier = 1;
    } else {
        game_modify_global_var(VAR_STORMS, 0, GLOBAL_VAR_ASSIGN);
        g_game.next_hour_time = g_game.game_time + HOUR_TICKS;
        g_game.heavy_rain = 0;
        g_game.weather_visible = 0;
        int delay;
        if (g_weather.first_init) {
            g_game.cloud_cover = 0;
            delay = rand_below(STORM_FIRST_DELAY);
            g_game.storm_curve = CALM_CURVE;
            g_game.storm_multiplier = 1;
        } else {
            delay = rand_below(STORM_NEXT_DELAY) + STORM_FIRST_DELAY;
            g_game.storm_curve = (uint8_t)rand_2bits();
            g_game.storm_multiplier = (uint8_t)(rand_below(STORM_MULTIPLIER_MAX) + 1);
        }
        g_game.sky_darkness = 1;
        g_game.rain_intensity = 0;
        g_game.storm_intensity = 0;
        g_weather.storm_previous = 0;
        g_game.storm_step = 0;
        g_game.wind_dir = (uint8_t)rand_2bits();
        schedule_storm_step(delay);
    }
    g_weather.strike_threshold = (int16_t)(rand_below(STRIKE_THRESHOLD_SPAN) + STRIKE_THRESHOLD_MIN);
    set_hour_light();
    g_weather.first_init = false;
}

void weather_load(const Gdat *g)
{
    /* (CAT_DUNGEON, 0, GDAT_TYPE_VALUE, 0), a signed word: the hour of the day at the start of the dungeon */
    int hour = clamp_int(0, (int16_t)gdat_entry_index(g, CAT_DUNGEON, 0, GDAT_TYPE_VALUE, 0), LAST_HOUR);
    g_weather.day_time_offset = (uint32_t)hour * HOUR_TICKS;
    weather_init(g_weather.dungeon_loaded);
    g_weather.dungeon_loaded = true;
}

/* A storm step: the storm intensity follows its curve; after the last step a new weather cycle starts. */
static void step_storm(void)
{
    g_game.storm_step++;
    if (g_game.storm_step >= STORM_STEPS) {
        weather_init(false);
        return;
    }
    g_weather.storm_previous = g_game.storm_intensity;
    g_game.storm_intensity = (uint16_t)(g_game.storm_intensity +
                                        g_game.storm_multiplier * storm_curves[g_game.storm_curve][g_game.storm_step]);
    /* the sum wraps below 0 as a signed word, which clamps to 0 */
    g_game.storm_intensity = (uint16_t)clamp_int(0, (int16_t)g_game.storm_intensity, STORM_MAX);
    schedule_storm_step(rand_below(STORM_RANDOM_SPAN) + STORM_STEP_DELAY);
}

/* Rain and clouds follow the storm; returns true when a lightning comes in this step. */
static bool update_rain(void)
{
    if (!g_game.storm_intensity) {
        if (g_game.cloud_cover && g_game.game_time % CLOUD_STEP_TICKS == 0)
            g_game.cloud_cover--;
        bool lightning = rand_below(LIGHTNING_CHANCE_CALM) == 0;
        g_game.rain_intensity = 0;
        g_game.sky_darkness = 1;
        return lightning;
    }
    uint16_t span = (uint16_t)(STORM_RANDOM_SPAN - g_game.storm_intensity + (rand_next() & STORM_SPAN_JITTER));
    int chance = (int16_t)g_game.storm_intensity >= STORM_STRONG ? LIGHTNING_CHANCE_STRONG : LIGHTNING_CHANCE_WEAK;
    g_game.sky_darkness = (uint8_t)g_game.storm_intensity;
    /* rain starts by chance, then follows the storm */
    if (g_game.rain_intensity)
        g_game.rain_intensity = (uint8_t)g_game.storm_intensity;
    else
        g_game.rain_intensity = rand_below(span) <= RAIN_CHANCE_MAX;
    int rain = g_game.rain_intensity;
    if (rain && g_game.cloud_cover < CLOUDS_MAX) {
        bool even = !(g_game.game_time & 1);
        if (rain >= RAIN_STRONG || (rain >= RAIN_MEDIUM && even) ||
            (rain >= RAIN_LIGHT && g_game.game_time % CLOUD_STEP_TICKS == 0) || !(g_game.game_time & CLOUD_SLOW_MASK))
            g_game.cloud_cover++;
    }
    return sky_has(GFX_SKY_LIGHTNING) && rand_below(span) <= chance;
}

/* Lightning hits a random floor square of the current map; returns true when it struck. Creatures far from the party
   or not in front of it are spared; the party square only in a strong storm, late, and after enough storms. */
static bool lightning_strike(void)
{
    int x = rand_below(LIGHTNING_RANGE);
    int y = rand_below(LIGHTNING_RANGE);
    if (x >= dungeon_map_width(g_party.cur_map) || y >= dungeon_map_height(g_party.cur_map))
        return false;
    ViewSquare s;
    aspect_get(&s, g_party.cur_map, DIR_NORTH, x, y);
    PassageEnd end;
    if (s.aspect != ASPECT_FLOOR || dun_get_linked_teleporter_dest(&end, (Location){x, y}))
        return false;
    bool weak = (int16_t)g_game.storm_intensity < STORM_WEAK;
    if (g_game.party_map == g_party.cur_map && x == g_game.party_x && y == g_game.party_y &&
        (weak || g_game.game_time < STRIKE_PARTY_TIME || game_global_var(VAR_STORMS) <= g_weather.strike_threshold))
        return false;
    int distance = (int16_t)(abs_int((int16_t)(g_game.party_x - x)) + abs_int((int16_t)(g_game.party_y - y)));
    uint16_t group = creature_group_at((Position){g_party.cur_map, x, y});
    if (group != THING_NONE) {
        if (creature_lightning[creature_group_info(group) - creature_info_at(0)] == CREATURE_LIGHTNING_PROOF)
            return false;
        if (distance > STRIKE_NEAR &&
            (distance > STRIKE_SEEN || dun_get_direction_to(g_game.party_x, g_game.party_y, x, y) != g_game.party_dir))
            return false;
    }
    explosion_create(EXPLOSION_STORM_LIGHTNING, g_game.storm_intensity, x, y, EXPLOSION_AT_CENTER);
    if ((g_ui.inventory || g_party_sleeping) && !weak && distance <= STRIKE_SEEN &&
        dun_get_direction_to(g_game.party_x, g_game.party_y, x, y) == g_game.party_dir)
        game_modify_global_var(VAR_STRIKES_SEEN, 1, GLOBAL_VAR_ADD);
    return true;
}

/* The fixed square of the lightning of a map: x in the high byte of the map value, y in the low byte. */
static Location lightning_target(uint16_t packed)
{
    return (Location){packed >> BYTE_BIT_COUNT, packed & BYTE_MASK};
}

/* A lightning: it hits squares (or the fixed square of the map), or a bolt shows in the sky with thunder; then the
   view flashes. o is the next free sky object; returns the next free one after the bolt. */
static SkyObject *show_lightning(SkyObject *o)
{
    if ((int16_t)g_game.storm_intensity < STORM_WEAK)
        game_modify_global_var(VAR_STORMS, 0, GLOBAL_VAR_ASSIGN);
    uint16_t strikes = 0;
    if (rand_below(g_game.storm_intensity + 1) >= STORM_LIGHTNING_HITS) {
        strikes = (uint16_t)((rand_next() & LIGHTNING_STRIKES_MASK) + 1);
        uint16_t packed = map_value(MAP_KEY_LIGHTNING_TARGET);
        if (packed) {
            Location target = lightning_target(packed);
            sensor_schedule_square_event(target, 0, EFFECT_SET, g_game.game_time + 1);
            sndreq_request_with_fallback(
                (SoundCue){CAT_EXPLOSION, LIGHTNING_SOUND_INDEX, SOUND_EXPLODE, SOUND_PRIORITY_EXPLODE,
                           rand_below(LIGHTNING_RANGE) + SOUND_VOLUME_NORMAL},
                SOUND_FALLBACK, target.x, target.y, SOUND_STEP_END); /* the volume: normal plus 0 to 31 */
        } else {
            while (strikes && !lightning_strike())
                strikes--;
        }
    }
    bool bolt = false;
    if (!strikes && rand_bit()) {
        o->entry = (uint8_t)(GFX_SKY_LIGHTNING + rand_below(SKY_LIGHTNING_KINDS));
        if (load_sky_object(o)) {
            bolt = true;
            int percent = rand_below(LIGHTNING_PERCENT);
            ScreenPoint offset = {o->dx, o->dy};
            g_hooks.zone_offset(o->zone, o->zone + 1, percent, &offset);
            o->dx = offset.x;
            o->dy = offset.y;
            o->flip = (uint8_t)rand_2bits();
            o++;
        }
    }
    if (bolt) {
        if (!g_weather.thunder_toggle) {
            int delay;
            if (!g_game.storm_intensity)
                delay = rand_below(THUNDER_CALM_DELAY) + THUNDER_CALM_BASE;
            else
                delay = THUNDER_DELAY_BASE - (int16_t)g_game.storm_intensity /
                                                 (g_game.storm_multiplier ? g_game.storm_multiplier
                                                                          : 1); /* fix: the original divides by 0 */
            delay = clamp_int(1, (int16_t)delay, THUNDER_DELAY_MAX);
            sndreq_request_after(
                (SoundCue){CAT_SKY, dungeon_map_wallset(g_game.party_map), 0, THUNDER_PRIORITY, THUNDER_VOLUME},
                g_game.party_x, g_game.party_y, delay);
            g_weather.thunder_toggle = true;
        } else {
            g_weather.thunder_toggle = false;
        }
    }
    g_weather.lightning_flash = true;
    light_update_party_light();
    return o;
}

/* The sky picture of an amount: the light picture, the next from medium, the third from heavy. */
static int sky_entry(int amount, int medium, int heavy, int first)
{
    return first + (amount >= medium) + (amount >= heavy);
}

void world_update_day_night(bool storm_step)
{
    g_game.weather_visible = weather_is_shown(map_value(MAP_KEY_SKY_STATE));
    if (storm_step) {
        step_storm();
        return;
    }
    if (g_game.game_time >= g_game.next_hour_time) {
        set_hour_light();
        g_game.next_hour_time = g_game.game_time + HOUR_TICKS;
        if (g_game.weather_visible)
            light_update_party_light();
    }
    bool lightning = update_rain();
    if (!g_game.weather_visible)
        return;
    if (g_weather.lightning_flash) {
        g_weather.lightning_flash = false;
        light_update_party_light();
    }
    SkyObject *o = weather_sky_objects();
    if (sky_has(GFX_SKY_RAIN_LIGHT) && g_game.sky_darkness >= DARKNESS_RAIN) {
        if (g_game.sky_darkness >= DARKNESS_HEAVY)
            g_game.heavy_rain = 1;
        o->entry = (uint8_t)sky_entry(g_game.sky_darkness, DARKNESS_MEDIUM, DARKNESS_HEAVY, GFX_SKY_RAIN_LIGHT);
        if (load_sky_object(o))
            o++;
    }
    if (sky_has(GFX_SKY_CLOUDS_LIGHT) && g_game.cloud_cover >= CLOUDS_SHOWN) {
        o->entry = (uint8_t)sky_entry(g_game.cloud_cover, CLOUDS_MORE, CLOUDS_MOST, GFX_SKY_CLOUDS_LIGHT);
        if (load_sky_object(o))
            o++;
    }
    if (lightning)
        o = show_lightning(o);
    o->entry = SKY_OBJECT_END;
}

/* Light */

/* The daylight in % that a window lets in now. */
static int daylight(void)
{
    return light_levels[clamp_int(0, g_game.heavy_rain + g_weather.hour_light, LIGHT_LEVEL_MAX)];
}

void light_update_party_light(void)
{
    int level;
    if (!dungeon_map_difficulty(g_game.party_map)) { /* a map without difficulty bits is always lit */
        level = LIGHT_LEVEL_LIT;
    } else {
        int16_t lights[HAND_LIGHTS_MAX] = {0};
        int n = 0;
        if (item_attr(g_game.leader_hand, ATTR_FLAGS) & ITEM_FLAG_LIGHT)
            lights[n++] = (int16_t)item_add_charges(g_game.leader_hand, 0);
        for (int c = 0; c < g_game.champion_count; c++)
            for (int16_t hand = 0; hand < SLOT_HAND_COUNT; hand++) {
                uint16_t thing = champion(c)->slots[hand];
                if (item_attr(thing, ATTR_FLAGS) & ITEM_FLAG_LIGHT)
                    lights[n++] = (int16_t)item_add_charges(thing, 0);
            }
        /* one bubble pass, as in the original: the brightest torch moves up by one place only */
        for (int i = 0; i < n - 1; i++)
            if (lights[i] > lights[i + 1]) {
                int16_t t = lights[i];
                lights[i] = lights[i + 1];
                lights[i + 1] = t;
            }
        int16_t sum = 0;
        int shift = HAND_LIGHT_SHIFT;
        for (int i = 0; i < n; i++) {
            sum = (int16_t)(sum + ((g_light_power[lights[i]] << shift) >> HAND_LIGHT_SHIFT));
            if (shift)
                shift--;
        }
        int16_t magic = save_magic_light();
        sum = (int16_t)(sum + g_light_source_sum + magic + map_value(MAP_KEY_LIGHT));
        if (g_game.weather_visible)
            sum = (int16_t)(sum + daylight());
        level = 0;
        while (level < LIGHT_LEVEL_MAX && light_levels[level] >= sum)
            level++;
        int16_t lowest = (int16_t)map_value(MAP_KEY_LEAST_LIGHT_LEVEL);
        if (lowest > level)
            level = lowest;
        if (g_game.weather_visible && g_weather.lightning_flash)
            level = 0;
    }
    level -= g_explosion_light_sum > EXPLOSION_GLARE;
    g_view_light_level = clamp_int(0, (int16_t)level, LIGHT_LEVEL_MAX);
}

/* The light value of an ornament (0: none); *animated_only: it shines only while it animates. */
static uint16_t ornament_light(int category, int ornament, bool *animated_only)
{
    uint16_t v = gdat_entry_index(gdat_current(), category, ornament, GDAT_TYPE_VALUE, ORNAMENT_KEY_LIGHT);
    *animated_only = (v & LIGHT_ANIMATED_ONLY) != 0;
    return v & LIGHT_AMOUNT_MASK;
}

/* The light of a wall ornament: a window follows the hour; other light shines always or only while it animates. */
static void add_wall_light(SquareLight *sum, const ViewSquare *s)
{
    int ornament = aspect_word_ornament(s->side[SIDE_FRONT]);
    bool animated = aspect_word_animated(s->side[SIDE_FRONT]);
    bool animated_only;
    if (ornament == ORNAMENT_NONE)
        return;
    uint16_t v = ornament_light(CAT_WALL_ORNAMENT, ornament, &animated_only);
    if (!v && !animated_only)
        return;
    if (gdat_entry_index(gdat_current(), CAT_WALL_ORNAMENT, ornament, GDAT_TYPE_VALUE, ORNAMENT_KEY_DAYLIGHT))
        sum->window = (int16_t)(sum->window + v * daylight() / PERCENT);
    else if (!animated_only || animated)
        sum->light = (int16_t)(sum->light + v);
}

/* The light of the things on a floor square: the explosions of fire and lightning, and the fireball missiles. */
static void add_missile_light(SquareLight *sum, uint16_t first_thing)
{
    for (uint16_t t = first_thing; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (thing_type(t) == THING_EXPLOSION) {
            int kind = explosion_type(t);
            if (kind == BLAST_LIGHTNING || kind == BLAST_FIREBALL || kind == BLAST_STORM_LIGHTNING)
                sum->missiles = (int16_t)(sum->missiles + (explosion_power(t) >> 1));
        } else if (thing_type(t) == THING_PROJECTILE && thing_word(t, PROJECTILE_PAYLOAD) == SPELL_FIREBALL) {
            sum->missiles = (int16_t)(sum->missiles + (projectile_energy(t) >> 1) - light_falloff[MISSILE_FALLOFF]);
        }
    }
}

/* The light of a floor ornament and of the things on the square; false when an ornament without light hides the
   other light of the square. */
static bool add_floor_light(SquareLight *sum, const ViewSquare *s, Location at)
{
    int ornament = aspect_word_ornament(s->side[SIDE_FLOOR_ORNAMENT]);
    bool animated = aspect_word_animated(s->side[SIDE_FLOOR_ORNAMENT]);
    bool floor = s->aspect == ASPECT_FLOOR || s->aspect == ASPECT_PIT || s->aspect == ASPECT_TELEPORTER;
    if (floor && ornament != ORNAMENT_NONE) {
        bool animated_only;
        uint16_t v = ornament_light(CAT_FLOOR_ORNAMENT, ornament, &animated_only);
        /* as in the original, a floor ornament without light also hides the other light of its square */
        if (!v && !animated_only)
            return false;
        PassageEnd end;
        if (dun_get_linked_teleporter_dest(&end, at))
            sum->window = (int16_t)(sum->window + v * daylight() / PERCENT);
        else if (!animated_only || animated)
            sum->light = (int16_t)(sum->light + v);
    }
    add_missile_light(sum, s->first_thing);
    return true;
}

void light_add_square_light(int distance, int16_t dir, Location at, unsigned flags)
{
    SquareLight sum = {0, 0, 0};
    ViewSquare s;
    aspect_get(&s, g_party.cur_map, dir, at.x, at.y);
    switch (s.aspect) {
    case ASPECT_WALL:
        if (flags & LIGHT_WALLS)
            add_wall_light(&sum, &s);
        break;
    case ASPECT_FLOOR:
    case ASPECT_PIT:
    case ASPECT_TELEPORTER:
    case ASPECT_DOOR_SIDE:
    case ASPECT_DOOR_FRONT:
    case ASPECT_STAIRS_FRONT:
    case ASPECT_STAIRS_SIDE:
        if ((flags & LIGHT_FLOOR) && !add_floor_light(&sum, &s, at))
            break;
        if (flags & LIGHT_CREATURES) {
            uint16_t group = creature_group_at((Position){g_party.cur_map, at.x, at.y});
            if (group != THING_NONE)
                sum.light = (int16_t)(sum.light + (item_attr(group, ATTR_LIGHT_SOURCE) & LIGHT_AMOUNT_MASK));
        }
        break;
    }
    if (distance <= FALLOFF_WEATHER_MAX && sum.window)
        sum.window = (int16_t)max_int(WINDOW_LIGHT_MIN, (int16_t)(sum.window - window_falloff[distance]));
    else
        sum.window = 0;
    if (distance > FALLOFF_MAX)
        distance = FALLOFF_MAX;
    if (sum.light)
        sum.light = (int16_t)max_int(SQUARE_LIGHT_MIN, (int16_t)(sum.light - light_falloff[distance]));
    g_light_source_sum = (int16_t)(g_light_source_sum + sum.light + sum.window);
    if (sum.missiles)
        sum.missiles = (int16_t)max_int(SQUARE_LIGHT_MIN, (int16_t)(sum.missiles - light_falloff[distance]));
    g_explosion_light_sum = (int16_t)(g_explosion_light_sum + sum.missiles);
}
