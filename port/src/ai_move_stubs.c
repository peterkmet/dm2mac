/* Functions of other parts of Step 16 and of Step 17 that ai_move.c calls: weak versions until those parts exist. */
#include "ai_move.h"
#include "draw.h"
#include "dungeon.h"
#include "gdat.h"
#include "move.h"

#define WEAK __attribute__((weak))
#define SENSOR_LEVEL_LINK 0x1c
#define TEXT_KIND_ORNAMENT 0x0002
#define TEXT_LADDER 6
#define TEXT_LADDER_HIDDEN 7
#define CAT_ORNAMENT 9
#define ORNAMENT_KEY_LEADS_UP 0x11

/* The side (0 to 3) of a level link next to (x, y) of the current map toward level_delta: a link sensor (*sensor)
   or a ladder text (*sensor THING_NONE); -1 for none. */
WEAK int dun_find_adjacent_level_link(int x, int y, int level_delta, uint16_t *sensor)
{
    for (int16_t d = 0; d < 4; d++) {
        int sx = (int16_t)(x + dir_dx[d]), sy = (int16_t)(y + dir_dy[d]);
        if (sx < 0 || sx >= dungeon_map_width(party.cur_map) || sy < 0 || sy >= dungeon_map_height(party.cur_map)) continue;
        for (uint16_t t = dungeon_square_first_thing(party.cur_map, sx, sy); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
            int type = thing_type(t);
            if (type > THING_SENSOR) break;
            uint16_t data = thing_word(t, SENSOR_TYPE_DATA);
            if (type == THING_SENSOR) {
                int map = data >> SENSOR_DATA_SHIFT;
                if ((data & SENSOR_TYPE_MASK) == SENSOR_LEVEL_LINK && map < dungeon_map_count()
                    && (int16_t)level_delta == dungeon_map_level(map) - dungeon_map_level(party.cur_map)) {
                    *sensor = t;
                    return d;
                }
            } else if (type == THING_TEXT && (data & 6) == TEXT_KIND_ORNAMENT) {
                int sub = data >> 11;
                if (sub != TEXT_LADDER && sub != TEXT_LADDER_HIDDEN) continue;
                int up = gdat_entry_index(draw_gdat(), CAT_ORNAMENT, data >> 3 & 0xff, GDAT_TYPE_VALUE, ORNAMENT_KEY_LEADS_UP);
                if (((int16_t)level_delta == -1 && up) || ((int16_t)level_delta == 1 && !up)) {
                    *sensor = THING_NONE;
                    return d;
                }
            }
        }
    }
    *sensor = THING_NONE;
    return -1;
}

WEAK uint16_t projectile_get_attack(uint16_t projectile, uint16_t payload) { (void)projectile; (void)payload; return 0; }
WEAK uint16_t explosion_get_attack(uint16_t explosion, uint16_t target) { (void)explosion; (void)target; return 0; }
WEAK void light_add_square_light(int distance, int dir, int x, int y, int mode) { (void)distance; (void)dir; (void)x; (void)y; (void)mode; }
