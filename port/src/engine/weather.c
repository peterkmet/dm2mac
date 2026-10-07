/* Weather, day and night, and the party light. */
#include "champion.h"
#include "creature.h"
#include "draw.h"
#include "dungeon.h"
#include "hooks.h"
#include "item.h"
#include "le.h"
#include "move.h"
#include "random.h"
#include "save.h"
#include "sensor.h"
#include "ui.h"
#include "view.h"
#include "weather.h"
#include "zone.h"

#define CAT_DUNGEON 3               /* (3, 0, 0x0b, 0): the hour of the day at the start of the dungeon */
#define CAT_MAP 8
#define CAT_WALL_ORNAMENT 9
#define CAT_FLOOR_ORNAMENT 0x0a
#define CAT_SKY 0x17
#define CAT_THUNDER 0x17            /* sound (0x17, graphics set, 0): the thunder */
#define CAT_LIGHTNING_SOUND 0x0d    /* sound (0x0d, 0x30, 0x81): a lightning that hits a fixed square */
#define LIGHTNING_SOUND_INDEX 0x30
#define LIGHTNING_SOUND_ENTRY 0x81
#define SOUND_NO_FALLBACK 0xfe
#define LIGHTNING_SOUND_PRIORITY 0x6c
#define LIGHTNING_SOUND_VOLUME 0x80 /* plus 0 to 31 */
#define THUNDER_PRIORITY 0x19
#define THUNDER_VOLUME 0x40
#define HOUR_TICKS 0x555            /* game ticks of one hour */
#define HOURS_PER_DAY 24
#define LAST_HOUR 23

/* Map graphics value keys (CAT_MAP, graphics set, 0x0b, key). */
enum {
    MAP_KEY_LIGHT = 0x67,           /* light added to the party light */
    MAP_KEY_MIN_LIGHT_LEVEL = 0x68, /* the party light level is at least this */
    MAP_KEY_LIGHTNING_TARGET = 0x6c, /* x in the high byte, y in the low byte: the square that lightning hits */
    MAP_KEY_SKY_STATE = 0x66
};
/* Ornament value keys. */
#define ORNAMENT_KEY_LIGHT 0xf8     /* light of the ornament; bit 15: only while it animates */
#define ORNAMENT_KEY_DAYLIGHT 0x63  /* the ornament is a window: its light follows the hour */
#define LIGHT_ANIMATED_ONLY 0x8000
#define LIGHT_AMOUNT_MASK 0x7fff
#define ITEM_ATTR_LIGHT 0xf8        /* item and creature attribute: light */
#define ITEM_FLAG_LIGHT 0x10        /* ATTR_FLAGS bit: a torch, its charges give light */

/* Sky objects (GDAT (CAT_SKY, graphics set, entry)) and the weather amounts that show them. */
enum { SKY_LIGHTNING = 0x64, SKY_LIGHTNING_KINDS = 3, SKY_RAIN_LIGHT = 0x67, SKY_RAIN_MEDIUM, SKY_RAIN_HEAVY,
       SKY_CLOUDS_LIGHT, SKY_CLOUDS_MEDIUM, SKY_CLOUDS_HEAVY };
#define DARKNESS_RAIN 0x10
#define DARKNESS_MEDIUM 0x40
#define DARKNESS_HEAVY 0x80
#define CLOUDS_SHOWN 0x40
#define CLOUDS_MORE 0x80
#define CLOUDS_MOST 0xc0
#define CLOUDS_MAX 0xff
#define RAIN_STRONG 0x80            /* rain intensities that grow the clouds faster */
#define RAIN_MEDIUM 0x40
#define RAIN_LIGHT 0x10
#define STORM_MAX 0xff
#define STORM_STEPS 32
#define STORM_STRONG 0xcd           /* from this storm intensity on lightning comes more often */
#define STORM_WEAK 0xb6             /* below this storm intensity lightning does not hit the party */
#define STORM_LIGHTNING_HITS 0x3c   /* rand_below(storm + 1) from this value: the lightning hits squares */
#define STORM_STEP_DELAY 0x32       /* plus 0 to 255 ticks between storm steps */
#define STORM_RANDOM_SPAN 0x100
#define STORM_FIRST_DELAY 500       /* ticks to the first storm step of a run */
#define STORM_NEXT_DELAY 8000       /* plus STORM_FIRST_DELAY: ticks to the storm of a new cycle */
#define CALM_CURVE 3
#define STORM_MULTIPLIER_MAX 3      /* a new storm cycle is 1 to 3 times as strong as its curve */
#define STRIKE_THRESHOLD_MIN 4      /* storms before lightning may hit the party: 4 to 7 */
#define STRIKE_THRESHOLD_SPAN 4
#define RAIN_CHANCE_MAX 7           /* rand_below(span) up to this starts rain */
#define LIGHTNING_CHANCE_WEAK 7
#define LIGHTNING_CHANCE_STRONG 0x28
#define LIGHTNING_CHANCE_CALM 0x40  /* 1 in 64 without a storm */
#define LIGHTNING_RANGE 0x20        /* random squares 0 to 31 */
#define LIGHTNING_STRIKES_MASK 7
#define LIGHTNING_PERCENT 100       /* the bolt stands at 0 to 99 % between its zone and the next zone */
#define LIGHTNING_EXPLOSION 0xffb0  /* explosion type of a lightning strike */
#define EXPLOSION_ANY_CELL 0xff
#define STRIKE_NEAR 2               /* square distance of a strike that always hits a creature square */
#define STRIKE_SEEN 3               /* square distance of a strike in front of the party */
#define STRIKE_PARTY_TIME 12000     /* game time before which lightning never hits the party square */
#define THUNDER_CALM_DELAY 10       /* plus 5: thunder delay without a storm */
#define THUNDER_CALM_BASE 5
#define THUNDER_DELAY_BASE 0x4c
#define THUNDER_DELAY_MAX 15
#define STORM_SPAN_JITTER 0xf      /* plus 0 to 15 on the random span of rain and lightning */
#define TARGET_X_SHIFT 8            /* lightning target value: x in the high byte, y in the low byte */
#define TARGET_Y_MASK 0xff
#define PERCENT 100
#define CLOUD_STEP_TICKS 3          /* clouds change at most each third tick */
#define CLOUD_SLOW_TICKS_MASK 3     /* any rain grows the clouds each fourth tick */
#define CREATURE_LIGHTNING_PROOF 0xff
/* Script variables of the weather. */
#define VAR_STORM_COUNT 0x40        /* cleared by each lightning of a weak storm */
#define VAR_STRIKES_SEEN 0x41       /* lightning strikes seen near the party from the inventory or asleep */
enum { VAR_ADD = 3, VAR_ASSIGN = 6 };
#define MAGIC_LIGHT_BYTE 0          /* game.magic_light bytes 0-1: the light of light spells */
#define MAP_DARK_BITS 0xf000        /* map object counts: a map without these bits is always lit */
#define LIGHT_LEVEL_MAX 5
#define LIGHT_LEVEL_LIT 1
#define HAND_LIGHT_SHIFT 6          /* the first hand light counts fully, each next one half as much */
#define HAND_LIGHTS_MAX (1 + SLOT_HAND_COUNT * CHAMPION_MAX)
#define EXPLOSION_GLARE 12          /* explosion light above which the view gets one level darker (glare) */
#define FALLOFF_WEATHER_MAX 8
#define FALLOFF_MAX 5
#define WINDOW_LIGHT_MIN 3
#define SQUARE_LIGHT_MIN 2
#define MISSILE_FALLOFF 3
#define MISSILE_SPELL 0xff80        /* projectile payload of a spell */
enum { EXPLOSION_FIRE = 0, EXPLOSION_LIGHT = 2, EXPLOSION_GLOW = 0x30 };
#define SIDE_LIGHT 2                /* the view square side of the front wall ornament or of the floor ornament */
#define ORNAMENT_MASK 0xff          /* ornament index in a side word; 0xff: no ornament */
#define TORCH_CHARGE_COUNT 16

/* The sky kinds of the map graphics: the weather shows (rain, clouds, lightning) in kinds 1 and 4. */
static const uint8_t sky_state_weather[] = {0, 1, 0, 0, 1};
/* The darkness of each hour of the day. */
static const int8_t light_by_hour[HOURS_PER_DAY] = {5, 5, 4, 3, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5, 5};
/* The storm curves: the change of the storm intensity in each of the 32 storm steps. */
static const int8_t storm_curves[][STORM_STEPS] = {
    {1, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -1},
    {1, 2, 2, 2, 4, 4, 6, 6, 6, 7, 8, 9, 7, 5, 4, 3, -3, -4, -5, -7, -9, -8, -7, -6, -6, -6, -4, -4, -2, -2, -2, -1},
    {10, 18, 22, 26, -4, -4, -4, -4, -4, -4, -4, -4, -3, -3, -3, -3, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2, -2,
     -2, -2, -2, -2},
    {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, -26, -22, -18, -10},
};
/* The light of a torch by its charges. */
static const int8_t light_power[TORCH_CHARGE_COUNT] = {0, 5, 12, 24, 33, 40, 46, 51, 59, 68, 76, 82, 89, 94, 97, 100};
/* The light that each light level needs (level 0 above 99), and the daylight in % by the darkness of the hour. */
static const int8_t light_levels[LIGHT_LEVEL_MAX + 1] = {99, 75, 50, 25, 1, 0};
/* What a square light loses by its distance to the party: window light, other light. */
static const int16_t window_falloff[FALLOFF_WEATHER_MAX + 1] = {0, 6, 14, 30, 42, 54, 76, 88, 96};
static const int16_t light_falloff[FALLOFF_MAX + 1] = {0, 10, 22, 45, 70, 90};
/* Byte +2 of the creature info records of SKULL.EXE (meaning not known): 0xff, lightning does not strike there. */
static const uint8_t creature_lightning[CREATURE_INFO_COUNT] = {
    255, 255, 255, 100, 255, 255, 255, 255, 255, 80, 255, 255, 255, 130, 150, 130, 130, 200, 110, 85, 255, 150, 100,
    50, 65, 80, 50, 70, 100, 135, 175, 220, 120, 180, 75, 50, 160, 255, 255, 255, 220, 100, 65, 75, 115, 255, 70, 255,
    100, 75, 115, 255, 90, 255, 255, 140, 255, 100, 110, 140, 255, 100, 75,
};

Weather weather = {.first_init = 1};
int16_t light_source_sum, explosion_light_sum;

static int clamp(int lo, int v, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int gfx_set(void) { return dungeon_map_wallset(game.party_map); }

static uint16_t map_value(int key) { return gdat_entry_index(draw_gdat(), CAT_MAP, gfx_set(), GDAT_TYPE_VALUE, key); }

static int sky_has(int entry) { return gdat_exists(draw_gdat(), CAT_SKY, gfx_set(), GDAT_TYPE_IMAGE, entry); }

/* The daylight in % that a window lets in now. */
static int daylight(void)
{
    return light_levels[clamp(0, (int16_t)(game.heavy_rain + weather.hour_light), LIGHT_LEVEL_MAX)];
}

static void set_hour_light(void)
{
    weather.hour_light = light_by_hour[(game.game_time + weather.day_time_offset) / HOUR_TICKS % HOURS_PER_DAY];
}

void weather_schedule_event(int32_t delay)
{
    TimelineEvent e = {.time_map = game.game_time + (uint32_t)delay, .type = EVENT_WEATHER};
    timeline_add_event(&e);  /* fix: the original leaves the data bytes uninitialized */
}

void weather_init(int keep_state)
{
    if (keep_state) {
        weather.storm_previous = 0;
        if (!game.storm_multiplier) game.storm_multiplier = 1;
    } else {
        dun_modify_global_var(VAR_STORM_COUNT, 0, VAR_ASSIGN);
        game.next_hour_time = game.game_time + HOUR_TICKS;
        game.heavy_rain = 0;
        game.weather_visible = 0;
        int delay;
        if (weather.first_init) {
            game.cloud_cover = 0;
            delay = rand_below(STORM_FIRST_DELAY);
            game.storm_curve = CALM_CURVE;
            game.storm_multiplier = 1;
        } else {
            delay = rand_below(STORM_NEXT_DELAY) + STORM_FIRST_DELAY;
            game.storm_curve = (uint8_t)rand_2bits();
            game.storm_multiplier = (uint8_t)(rand_below(STORM_MULTIPLIER_MAX) + 1);
        }
        game.sky_darkness = 1;
        game.rain_intensity = 0;
        game.storm_intensity = 0;
        weather.storm_previous = 0;
        game.storm_step = 0;
        game.wind_dir = (uint8_t)rand_2bits();
        weather_schedule_event(delay);
    }
    weather.strike_threshold = (int16_t)(rand_below(STRIKE_THRESHOLD_SPAN) + STRIKE_THRESHOLD_MIN);
    set_hour_light();
    weather.first_init = 0;
}

void weather_load(const gdat_t *g)
{
    int hour = clamp(0, (int16_t)gdat_entry_index(g, CAT_DUNGEON, 0, GDAT_TYPE_VALUE, 0), LAST_HOUR);
    weather.day_time_offset = (uint32_t)hour * HOUR_TICKS;
    weather_init(weather.dungeon_loaded);
    weather.dungeon_loaded = 1;
}

/* A storm step: the storm intensity follows its curve; after the last step a new weather cycle starts. */
static void weather_storm_step(void)
{
    game.storm_step++;
    if (game.storm_step >= STORM_STEPS) {
        weather_init(0);
        return;
    }
    weather.storm_previous = game.storm_intensity;
    game.storm_intensity = (uint16_t)(game.storm_intensity
                                      + game.storm_multiplier * storm_curves[game.storm_curve][game.storm_step]);
    game.storm_intensity = (uint16_t)clamp(0, (int16_t)game.storm_intensity, STORM_MAX);
    weather_schedule_event(rand_below(STORM_RANDOM_SPAN) + STORM_STEP_DELAY);
}

/* Rain and clouds follow the storm; returns 1 when a lightning comes in this step. */
static int weather_update_rain(void)
{
    if (!game.storm_intensity) {
        if (game.cloud_cover && game.game_time % CLOUD_STEP_TICKS == 0) game.cloud_cover--;
        int lightning = rand_below(LIGHTNING_CHANCE_CALM) == 0;
        game.rain_intensity = 0;
        game.sky_darkness = 1;
        return lightning;
    }
    uint16_t span = (uint16_t)(STORM_RANDOM_SPAN - game.storm_intensity + (rand_next() & STORM_SPAN_JITTER));
    int chance = (int16_t)game.storm_intensity >= STORM_STRONG ? LIGHTNING_CHANCE_STRONG : LIGHTNING_CHANCE_WEAK;
    game.sky_darkness = (uint8_t)game.storm_intensity;
    /* rain starts by chance, then follows the storm */
    game.rain_intensity = game.rain_intensity ? (uint8_t)game.storm_intensity : rand_below(span) <= RAIN_CHANCE_MAX;
    int rain = game.rain_intensity;
    if (rain && game.cloud_cover < CLOUDS_MAX) {
        int even = !(game.game_time & 1);
        if (rain >= RAIN_STRONG || (rain >= RAIN_MEDIUM && even)
            || (rain >= RAIN_LIGHT && game.game_time % CLOUD_STEP_TICKS == 0) || !(game.game_time & CLOUD_SLOW_TICKS_MASK))
            game.cloud_cover++;
    }
    return sky_has(SKY_LIGHTNING) && rand_below(span) <= chance;
}

/* The ornament index and the animation frame of a view square side word. */
static int side_ornament(uint16_t side) { return side & ORNAMENT_MASK; }
static bool side_animates(uint16_t side) { return side >> BYTE_BITS != 0; }

static int abs16(int v) { return (int16_t)v < 0 ? -(int16_t)v : (int16_t)v; }
static int max16(int lo, int v) { return (int16_t)v > lo ? (int16_t)v : lo; }

/* Lightning hits a random floor square of the current map; returns 1 when it struck. Creatures far from the party
   or not in front of it are spared; the party square only in a strong storm, late, and after enough storms. */
static int lightning_strike(void)
{
    int x = rand_below(LIGHTNING_RANGE), y = rand_below(LIGHTNING_RANGE);
    if (x >= dungeon_map_width(party.cur_map) || y >= dungeon_map_height(party.cur_map)) return 0;
    ViewSquare s;
    dun_get_square_aspect(&s, DIR_NORTH, x, y);
    PassageEnd end;
    if (s.aspect != ASPECT_FLOOR || dun_get_linked_teleporter_dest(&end, x, y)) return 0;
    int weak = (int16_t)game.storm_intensity < STORM_WEAK;
    if (game.party_map == party.cur_map && x == game.party_x && y == game.party_y
        && (weak || game.game_time < STRIKE_PARTY_TIME || dun_get_global_var(VAR_STORM_COUNT) <= weather.strike_threshold))
        return 0;
    int distance = (int16_t)(abs16(game.party_x - x) + abs16(game.party_y - y));
    uint16_t group = creature_group_at(party.cur_map, x, y);
    if (group != THING_NONE) {
        if (creature_lightning[creature_group_info(group) - creature_info_at(0)] == CREATURE_LIGHTNING_PROOF) return 0;
        if (distance > STRIKE_NEAR
            && (distance > STRIKE_SEEN || dun_get_direction_to(game.party_x, game.party_y, x, y) != game.party_dir))
            return 0;
    }
    explosion_create(LIGHTNING_EXPLOSION, game.storm_intensity, x, y, EXPLOSION_ANY_CELL);
    if ((ui.inventory || party_sleeping) && !weak && distance <= STRIKE_SEEN
        && dun_get_direction_to(game.party_x, game.party_y, x, y) == game.party_dir)
        dun_modify_global_var(VAR_STRIKES_SEEN, 1, VAR_ADD);
    return 1;
}

/* A lightning: it hits squares (or the fixed square of the map), or a bolt shows in the sky with thunder; then the
   view flashes. o is the next free sky object; returns the next free one after the bolt. */
static SkyObject *weather_lightning(SkyObject *o)
{
    if ((int16_t)game.storm_intensity < STORM_WEAK) dun_modify_global_var(VAR_STORM_COUNT, 0, VAR_ASSIGN);
    uint16_t strikes = 0;
    if (rand_below((uint16_t)(game.storm_intensity + 1)) >= STORM_LIGHTNING_HITS) {
        strikes = (uint16_t)((rand_next() & LIGHTNING_STRIKES_MASK) + 1);
        uint16_t target = map_value(MAP_KEY_LIGHTNING_TARGET);
        if (target) {
            int x = target >> TARGET_X_SHIFT, y = target & TARGET_Y_MASK;
            sensor_schedule_square_event(x, y, 0, EFFECT_SET, game.game_time + 1);
            sound_play_with_fallback(CAT_LIGHTNING_SOUND, LIGHTNING_SOUND_INDEX, LIGHTNING_SOUND_ENTRY, SOUND_NO_FALLBACK,
                                     x, y, 1, LIGHTNING_SOUND_PRIORITY,
                                     (int16_t)(rand_below(LIGHTNING_RANGE) + LIGHTNING_SOUND_VOLUME));
        } else {
            while (strikes && !lightning_strike()) strikes--;
        }
    }
    int bolt = 0;
    if (!strikes && rand_bit()) {
        o->entry = (uint8_t)(SKY_LIGHTNING + rand_below(SKY_LIGHTNING_KINDS));
        if (weather_load_sky_object(o)) {
            bolt = 1;
            int percent = (int16_t)rand_below(LIGHTNING_PERCENT);
            zone_interpolate_offset(o->zone, (int16_t)(o->zone + 1), percent, &o->dx, &o->dy);
            o->flip = (uint8_t)rand_2bits();
            o++;
        }
    }
    if (bolt) {
        if (!weather.thunder_toggle) {
            int delay;
            if (!game.storm_intensity) delay = rand_below(THUNDER_CALM_DELAY) + THUNDER_CALM_BASE;
            else delay = THUNDER_DELAY_BASE - (int16_t)game.storm_intensity
                         / (game.storm_multiplier ? game.storm_multiplier : 1);  /* fix: the original divides by 0 */
            delay = clamp(1, (int16_t)delay, THUNDER_DELAY_MAX);
            sound_request(CAT_THUNDER, gfx_set(), 0, THUNDER_PRIORITY, THUNDER_VOLUME, game.party_x, game.party_y, delay);
            weather.thunder_toggle = 1;
        } else {
            weather.thunder_toggle = 0;
        }
    }
    weather.lightning_flash = 1;
    light_update_party_light();
    return o;
}

/* The sky picture of an amount: the light picture, the next from medium, the third from heavy. */
static int sky_entry(int amount, int medium, int heavy, int first)
{
    return first + (amount >= medium) + (amount >= heavy);
}

void world_update_day_night(int storm_step)
{
    uint16_t sky = map_value(MAP_KEY_SKY_STATE);
    game.weather_visible = sky < sizeof sky_state_weather && sky_state_weather[sky];  /* fix: no read past the table */
    if (storm_step) {
        weather_storm_step();
        return;
    }
    if (game.game_time >= game.next_hour_time) {
        set_hour_light();
        game.next_hour_time = game.game_time + HOUR_TICKS;
        if (game.weather_visible) light_update_party_light();
    }
    int lightning = weather_update_rain();
    if (!game.weather_visible) return;
    if (weather.lightning_flash) {
        weather.lightning_flash = 0;
        light_update_party_light();
    }
    SkyObject *o = view_sky_objects();
    if (sky_has(SKY_RAIN_LIGHT) && game.sky_darkness >= DARKNESS_RAIN) {
        if (game.sky_darkness >= DARKNESS_HEAVY) game.heavy_rain = 1;
        o->entry = (uint8_t)sky_entry(game.sky_darkness, DARKNESS_MEDIUM, DARKNESS_HEAVY, SKY_RAIN_LIGHT);
        if (weather_load_sky_object(o)) o++;
    }
    if (sky_has(SKY_CLOUDS_LIGHT) && game.cloud_cover >= CLOUDS_SHOWN) {
        o->entry = (uint8_t)sky_entry(game.cloud_cover, CLOUDS_MORE, CLOUDS_MOST, SKY_CLOUDS_LIGHT);
        if (weather_load_sky_object(o)) o++;
    }
    if (lightning) o = weather_lightning(o);
    o->entry = SKY_OBJECT_END;
}

void light_update_party_light(void)
{
    int level;
    if (!(dungeon_map_object_counts(game.party_map) & MAP_DARK_BITS)) {
        level = LIGHT_LEVEL_LIT;
    } else {
        int16_t lights[HAND_LIGHTS_MAX] = {0};
        int n = 0;
        if (item_attr(game.leader_hand, ATTR_FLAGS) & ITEM_FLAG_LIGHT)
            lights[n++] = (int16_t)item_add_charges(game.leader_hand, 0);
        for (int c = 0; c < game.champion_count; c++)
            for (int hand = 0; hand < SLOT_HAND_COUNT; hand++) {
                uint16_t thing = champion(c)->slots[hand];
                if (item_attr(thing, ATTR_FLAGS) & ITEM_FLAG_LIGHT) lights[n++] = (int16_t)item_add_charges(thing, 0);
            }
        /* one bubble pass, as the original: the brightest torch moves up by one place only */
        for (int i = 0; i < n - 1; i++)
            if (lights[i] > lights[i + 1]) {
                int16_t t = lights[i];
                lights[i] = lights[i + 1];
                lights[i + 1] = t;
            }
        int16_t sum = 0;
        int shift = HAND_LIGHT_SHIFT;
        for (int i = 0; i < n; i++) {
            sum = (int16_t)(sum + ((light_power[lights[i]] << shift) >> HAND_LIGHT_SHIFT));
            if (shift) shift--;
        }
        int16_t magic = (int16_t)le16(&game.magic_light[MAGIC_LIGHT_BYTE]);
        sum = (int16_t)(sum + light_source_sum + magic + map_value(MAP_KEY_LIGHT));
        if (game.weather_visible) sum = (int16_t)(sum + daylight());
        level = 0;
        while (level < LIGHT_LEVEL_MAX && light_levels[level] >= sum) level++;
        int16_t lowest = (int16_t)map_value(MAP_KEY_MIN_LIGHT_LEVEL);
        if (lowest > level) level = lowest;
        if (game.weather_visible && weather.lightning_flash) level = 0;
    }
    level -= explosion_light_sum > EXPLOSION_GLARE;
    view_light_level = clamp(0, (int16_t)level, LIGHT_LEVEL_MAX);
}

/* The light value of an ornament (0: none); *animated_only: it shines only while it animates. */
static uint16_t ornament_light(int category, int ornament, int *animated_only)
{
    uint16_t v = gdat_entry_index(draw_gdat(), category, ornament, GDAT_TYPE_VALUE, ORNAMENT_KEY_LIGHT);
    *animated_only = (v & LIGHT_ANIMATED_ONLY) != 0;
    return v & LIGHT_AMOUNT_MASK;
}

void light_add_square_light(int distance, int dir, int x, int y, unsigned flags)
{
    int16_t window = 0, light = 0, missiles = 0;
    ViewSquare s;
    dun_get_square_aspect(&s, dir, x, y);
    /* the front wall ornament, or the floor ornament: index in the low byte, animation frame in the high byte */
    int ornament = side_ornament(s.side[SIDE_LIGHT]), animated = side_animates(s.side[SIDE_LIGHT]);
    int animated_only;
    PassageEnd end;
    switch (s.aspect) {
    case ASPECT_WALL: {
        if (!(flags & LIGHT_WALLS) || ornament == ORNAMENT_MASK) break;
        uint16_t v = ornament_light(CAT_WALL_ORNAMENT, ornament, &animated_only);
        if (!v && !animated_only) break;
        if (gdat_entry_index(draw_gdat(), CAT_WALL_ORNAMENT, ornament, GDAT_TYPE_VALUE, ORNAMENT_KEY_DAYLIGHT))
            window = (int16_t)(window + v * daylight() / PERCENT);
        else if (!animated_only || animated) light = (int16_t)(light + v);
        break;
    }
    case ASPECT_FLOOR: case ASPECT_PIT: case ASPECT_TELEPORTER:
    case ASPECT_DOOR_SIDE: case ASPECT_DOOR_FRONT: case ASPECT_STAIRS_FRONT: case ASPECT_STAIRS_SIDE:
        if (flags & LIGHT_FLOOR) {
            int floor = s.aspect == ASPECT_FLOOR || s.aspect == ASPECT_PIT || s.aspect == ASPECT_TELEPORTER;
            if (floor && ornament != ORNAMENT_MASK) {
                uint16_t v = ornament_light(CAT_FLOOR_ORNAMENT, ornament, &animated_only);
                /* like the original, a floor ornament without light also hides the other light of its square */
                if (!v && !animated_only) break;
                if (dun_get_linked_teleporter_dest(&end, x, y)) window = (int16_t)(window + v * daylight() / PERCENT);
                else if (!animated_only || animated) light = (int16_t)(light + v);
            }
            for (uint16_t t = s.first_thing; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
                if (thing_type(t) == THING_EXPLOSION) {
                    int kind = explosion_type(t);
                    if (kind == EXPLOSION_LIGHT || kind == EXPLOSION_FIRE || kind == EXPLOSION_GLOW)
                        missiles = (int16_t)(missiles + (explosion_power(t) >> 1));
                } else if (thing_type(t) == THING_PROJECTILE && thing_word(t, PROJECTILE_PAYLOAD) == MISSILE_SPELL) {
                    missiles = (int16_t)(missiles + (projectile_energy(t) >> 1)
                                         - light_falloff[MISSILE_FALLOFF]);
                }
            }
        }
        if (flags & LIGHT_CREATURES) {
            uint16_t group = creature_group_at(party.cur_map, x, y);
            if (group != THING_NONE) light = (int16_t)(light + (item_attr(group, ITEM_ATTR_LIGHT) & LIGHT_AMOUNT_MASK));
        }
        break;
    }
    if (distance <= FALLOFF_WEATHER_MAX && window) window = (int16_t)max16(WINDOW_LIGHT_MIN, window - window_falloff[distance]);
    else window = 0;
    if (distance > FALLOFF_MAX) distance = FALLOFF_MAX;
    if (light) light = (int16_t)max16(SQUARE_LIGHT_MIN, light - light_falloff[distance]);
    light_source_sum = (int16_t)(light_source_sum + light + window);
    if (missiles) missiles = (int16_t)max16(SQUARE_LIGHT_MIN, missiles - light_falloff[distance]);
    explosion_light_sum = (int16_t)(explosion_light_sum + missiles);
}
