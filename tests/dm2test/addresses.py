"""Addresses of the original SKULL.EXE that more than one dm2test module uses."""

VGA = 0xA0000
MONEY_INIT_COIN_TABLE = 0x154E0
G_UI_COLORS, G_FONT_BITMAP = 0x7F210, 0x80414
G_CHAMPIONS = 0x7FBD0
G_SKY_OBJECTS, SKY_OBJECTS_SIZE = 0x80438, 50
# Weather globals of the original: name -> (address, struct format); the C values come from World.
WEATHER = {
    "weather_visible": (0x8047B, "<B"), "heavy_rain": (0x8047C, "<B"), "wind_dir": (0x8047F, "<B"),
    "rain_intensity": (0x8047E, "<B"), "sky_darkness": (0x8047A, "<B"), "cloud_cover": (0x80479, "<B"),
    "storm_multiplier": (0x80480, "<B"), "storm_step": (0x80477, "<B"), "storm_curve": (0x80474, "<B"),
    "storm_intensity": (0x80470, "<H"), "next_hour_time": (0x80430, "<I"),
    "strike_threshold": (0x8046C, "<h"), "storm_previous": (0x8046A, "<H"), "hour_light": (0x80472, "<h"),
    "day_time_offset": (0x80434, "<I"), "first_init": (0x76184, "<i"), "thunder_toggle": (0x76188, "<i"),
    "lightning_flash": (0x7F248, "<i"), "light_level": (0x7F282, "<h"), "g_light_source_sum": (0x7F970, "<h"),
    "g_explosion_light_sum": (0x7F974, "<h"),
}
