/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_SAVE_H
#define DM2_ENGINE_SAVE_H
/* Prefixes: save_ */

/* The save game: its blocks, its layout, and reading and writing the file. */

#include "gdat.h"
#include "le.h"
#include <stdint.h>

/* Sizes of the game state blocks of the save. */
#define CHAMPION_MAX      4
#define CHAMPION_SIZE     263
#define GLOBAL_FLAG_BYTES 8
#define GLOBAL_BYTE_COUNT 64
#define GLOBAL_WORD_COUNT 64
#define MAGIC_LIGHT_SIZE  6

/* The save file header. */
#define SAVE_NAME_SIZE   32 /* a slot name of up to 31 characters and its end mark */
#define SAVE_HEADER_SIZE 42 /* the save file header: format word, slot name, empty slot mark */
#define SAVE_HEADER_NAME 2  /* the offset of the slot name in the header */

/* Start values of a loaded or new game. */
#define EVENT_SLACK          50 /* timeline events beyond one for each group, projectile, and explosion record */
#define VOLUME_SETTING_START 7  /* the sound and music volumes of a new game */

/* No leader of the party. */
#define LEADER_NONE 0xffff /* g_game.leader of a party without a leader */

/* The result of a load or a write. */
typedef enum {
    SAVE_OK = 0,
    SAVE_ERR_IO = -1,     /* the file cannot be opened, read, or written */
    SAVE_ERR_FORMAT = -2, /* the header or the dungeon image is damaged */
    SAVE_ERR_DATA = -3,   /* the bit-packed game state is damaged or ends too early */
} SaveResult;

/* Game state that the save file stores beside the dungeon, the timeline, and the random seed.
   The modules of later steps take these fields over. */
typedef struct {
    uint32_t game_time;
    uint16_t champion_count;
    uint16_t party_x, party_y, party_dir, party_map;
    uint16_t leader;
    uint16_t party_move_cooldown;
    uint32_t last_combat_time;          /* the last fight of the party: the fight skills learn less long after it */
    uint32_t first_recruit_time;        /* when the first champion joined: the party gets hungry faster after it */
    uint16_t throw_cooldown, throw_dir; /* steps after a throw in which the party cannot move toward throw_dir */
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

extern Game g_game;

/* The magic light of the party: the first word of g_game.magic_light. */
static inline int16_t save_magic_light(void)
{
    return (int16_t)le16(g_game.magic_light);
}
static inline void save_set_magic_light(int16_t light)
{
    put_le16(g_game.magic_light, (uint16_t)light);
}

/* Loads a save file (SKSAVEn.DAT or .BAK) into the dungeon, the timeline, the random seed, and g_game.
   gdat answers the creature and container questions of the format. On an error the dungeon is empty. */
SaveResult save_load(const char *path, const gdat_t *gdat);
/* Writes the game to path. Like the original, it first renames an old file at path to bak_path and
   keeps the header bytes after the name from it; name goes into the header. */
SaveResult save_write(const char *path, const char *bak_path, const char *name, const gdat_t *gdat);

/* The party has no champions and an empty leader hand: the state before a load or a new game. */
void save_clear_party(void);
/* The party stands at (x, y) of map and looks to dir. */
void save_set_party_position(int16_t x, int16_t y, int16_t dir, int16_t map);
/* The sound effect volume of the game. */
void save_set_sound_volume(uint8_t volume);
/* The music volume of the game. */
void save_set_music_volume(uint8_t volume);

#endif /* DM2_ENGINE_SAVE_H */
