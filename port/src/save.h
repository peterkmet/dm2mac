#ifndef SAVE_H
#define SAVE_H
#include <stdint.h>
#include "gdat.h"

#define CHAMPION_MAX 4
#define CHAMPION_SIZE 263
#define GLOBAL_FLAG_BYTES 8
#define GLOBAL_BYTE_COUNT 64
#define GLOBAL_WORD_COUNT 64
#define MAGIC_LIGHT_SIZE 6
#define SAVE_NAME_SIZE 16

enum {
    SAVE_OK = 0,
    SAVE_ERR_IO = -1,     /* the file cannot be opened, read, or written */
    SAVE_ERR_FORMAT = -2, /* the header or the dungeon image is damaged */
    SAVE_ERR_DATA = -3,   /* the bit-packed game state is damaged or ends too early */
};

/* Game state that the save file stores beside the dungeon, the timeline, and the random seed.
   The modules of later steps take these fields over. */
typedef struct {
    uint32_t game_time;
    uint16_t champion_count;
    uint16_t party_x, party_y, party_dir, party_map;
    uint16_t leader;
    uint16_t party_move_cooldown;
    uint32_t unknown_time_a, unknown_time_b;  /* game times, meaning not known */
    uint16_t unknown_countdown, unknown_dir;  /* set to 4 and to a party direction, meaning not known */
    uint8_t sound_volume, music_volume;
    uint8_t weather_visible, heavy_rain, wind_dir, rain_intensity, sky_darkness, cloud_cover;
    uint8_t storm_multiplier, storm_step, storm_curve;
    uint16_t storm_intensity;
    uint32_t next_hour_time;
    uint8_t global_flags[GLOBAL_FLAG_BYTES];
    uint8_t global_bytes[GLOBAL_BYTE_COUNT];
    uint16_t global_words[GLOBAL_WORD_COUNT];
    uint8_t champions[CHAMPION_MAX][CHAMPION_SIZE];
    uint16_t leader_hand;
    uint8_t magic_light[MAGIC_LIGHT_SIZE];
} Game;

extern Game game;

/* Loads a save file (SKSAVEn.DAT or .BAK) into the dungeon, the timeline, the random seed, and game.
   gdat answers the creature and container questions of the format. On an error the dungeon is empty. */
int save_load(const char *path, const gdat_t *gdat);
/* Writes the game to path. Like the original, it first renames an old file at path to bak_path and
   keeps the header bytes after the name from it; name goes into the header. */
int save_write(const char *path, const char *bak_path, const char *name, const gdat_t *gdat);

#endif
