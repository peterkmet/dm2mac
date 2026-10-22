/* SPDX-License-Identifier: GPL-3.0-or-later */
/* What the party sees of a square: its aspect, ornaments, and first thing; the 5 x 5 cell grid of a square. */
#include "aspect.h"
#include "gdat.h"
#include "ids.h"
#include "creature.h"
#include "dungeon.h"
#include "move.h"
#include "save.h"
#include "sensor.h"
#include <string.h>

/* The square byte that the floor things see on an open fake wall. */
#define FAKE_WALL_FLOOR_SQUARE  1
/* (CAT_WALLSET, graphics set, 0x0b, key): ornament on every floor square, bit 15: animated */
#define MAP_KEY_FLOOR_ORNAMENT  0x6b
#define ORN_KEY_FRAMES          0x0d /* (category, ornament, 0x0b, key): animation frames, bit 15: frames start at 1 */
/* Bit 15 of the values of the keys above. */
#define FRAMES_FROM_ONE         0x8000
#define FLOOR_ORNAMENT_ANIMATED 0x8000

/* The random wall ornament hash of SKULL.EXE. */
#define HASH_X_STEP           32
#define HASH_X_BASE           2000
#define HASH_MAP_STEP         64
#define HASH_MAP_BASE         3000
#define HASH_MULTIPLIER       31417
#define HASH_MAP_FACTOR       11
#define RANDOM_ORNAMENT_SLOTS 30 /* random wall ornament hash: one of 30 values picks an ornament of the list */

/* Random wall ornaments: the list of the map that view_enter_map entered last. */
static const uint8_t *wall_ornaments;

/* ---- Setup ---- */

void aspect_enter_map(int16_t map)
{
    wall_ornaments = dungeon_map_wall_ornaments(map);
}

/* ---- Lookups ---- */

uint16_t aspect_first_thing(int16_t map, int16_t x, int16_t y)
{
    if (x < 0 || x >= dungeon_map_width(map) || y < 0 || y >= dungeon_map_height(map))
        return THING_ENDOFLIST;
    return dungeon_square_first_thing(map, x, y);
}

uint16_t aspect_value_key(int category, int index, int entry)
{
    return gdat_entry_index(gdat_current(), category, index, GDAT_TYPE_VALUE, entry);
}

/* ---- Square aspects ---- */

/* A random wall ornament for a wall face, ORNAMENT_NONE when the face shows none. */
static uint8_t random_ornament(int map, int allowed, int count, int x, int factor)
{
    if (!allowed)
        return ORNAMENT_NONE;
    int map_size = dungeon_map_width(map) + dungeon_map_height(map);
    uint16_t a = (uint16_t)(x * HASH_X_STEP + HASH_X_BASE + (int16_t)factor);
    uint16_t b = (uint16_t)(map * HASH_MAP_STEP + HASH_MAP_BASE + map_size);
    int16_t hash = (int16_t)(((((a * HASH_MULTIPLIER) >> 1) + b * HASH_MAP_FACTOR + dungeon_ornament_seed()) >> 2) %
                             RANDOM_ORNAMENT_SLOTS);
    return hash < count ? wall_ornaments[hash] : ORNAMENT_NONE;
}

/* Gives the wall faces of s the random wall ornaments of map; an edge wall keeps none that has a kind. */
static void set_random_wall_ornaments(ViewSquare *s, int map, int dir, int x, int y)
{
    int count = dungeon_map_random_wall_count(map);
    for (int k = 0; k < VIEW_SIDE_COUNT; k++) {
        int allowed = s->square & WALL_FACE(dir_turn(dir, k));
        s->side[k] = random_ornament(map, allowed, count, x, (((dir + k) & DIR_MASK) + 1) * (y + 1));
    }
    /* an edge wall away from the map keeps no ornament that has a kind */
    if (x >= 0 && x < dungeon_map_width(map) && y >= 0 && y < dungeon_map_height(map))
        return;
    for (int k = 0; k < VIEW_SIDE_COUNT; k++)
        if (aspect_word_ornament(s->side[k]) != ORNAMENT_NONE &&
            aspect_value_key(CAT_WALL_ORNAMENT, aspect_word_ornament(s->side[k]), ORN_KEY_KIND))
            s->side[k] = ORNAMENT_NONE;
}

/* The animation frame of an ornament at time + offset: from its frame count, or from its frame text. */
static int ornament_frame(int category, int ornament, uint32_t time, int offset)
{
    uint16_t frames = aspect_value_key(category, ornament, ORN_KEY_FRAMES);
    char text[WALL_TEXT_SIZE];
    int first = 0;
    const char *sequence = NULL;
    if (!frames) {
        gdat_text(gdat_current(), (GdatKey){category, ornament, ORN_KEY_FRAMES}, text, sizeof text);
        if (!text[0])
            return 1;
        sequence = text;
        frames = (uint16_t)strlen(text);
    } else if (frames & FRAMES_FROM_ONE) {
        first = 1;
        frames &= ~FRAMES_FROM_ONE;
    }
    int frame = (int)((time + (uint32_t)offset) % frames) + first;
    if (!sequence)
        return frame;
    int c = (uint8_t)sequence[(uint16_t)frame];
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'Z')
        return c - 'K'; /* as in SKULL.EXE: 'K' is frame 0 */
    return c;
}

/* The wall ornament of a sensor: an entry of the wall ornament list of the current map. */
static uint8_t listed_ornament(uint16_t sensor, const uint8_t *list)
{
    int index = sensor_ornament_index(sensor);
    return index >= 0 ? list[index] : ORNAMENT_NONE;
}

/* How a wall or floor thing shows its ornament: a fixed frame, or an animation at time + offset. */
typedef struct {
    uint8_t ornament;
    bool animated;
    int frame, offset;
    uint32_t time;
    bool hides_rest; /* the things behind it in its cell do not show */
} OrnamentState;

/* The ornament of a wall text on face of s; a text plate goes on the face at once. Sets o->hides_rest when the
   things behind it in its cell do not show. Returns false when the text shows no ornament. */
static bool wall_text_ornament(ViewSquare *s, uint16_t thing, int face, OrnamentState *o)
{
    int kind = text_kind(thing), subtype = text_subtype(thing);
    bool visible = text_is_visible(thing);
    if (kind == TEXT_KIND_TEXT || (kind == TEXT_KIND_ORNAMENT && subtype == SUBTYPE_PLATE)) {
        s->side[face] = ORNAMENT_PLATE;
        if (face == SIDE_FRONT)
            s->text = visible ? thing : THING_TEXT_NONE;
        o->hides_rest = true;
        return false;
    }
    if (kind != TEXT_KIND_ORNAMENT)
        return false;
    o->ornament = text_value(thing);
    switch (subtype) {
    case SUBTYPE_STILL:
    case SUBTYPE_LADDER:
        break;
    case SUBTYPE_ANIMATED:
        o->animated = true;
        break;
    case SUBTYPE_TOGGLE:
    case SUBTYPE_HAND_SWITCH:
        o->animated = visible;
        break;
    case SUBTYPE_ITEM_HOLDER:
    case SUBTYPE_LADDER_HIDDEN:
    case SUBTYPE_ITEM_HOLDER_MARKER:
        o->frame = visible;
        if (o->frame) {
            o->animated = true;
            o->hides_rest = true;
        }
        break;
    default:
        return false;
    }
    return true;
}

/* The ornament of a wall sensor on face of s by its sensor type; wall text and altar sensors set the text of s.
   Sets o->hides_rest when the things behind it in its cell do not show. Returns false when the sensor has no
   ornament. */
static bool wall_sensor_ornament(ViewSquare *s, int map, uint16_t thing, int face, OrnamentState *o)
{
    o->ornament = listed_ornament(thing, dungeon_map_wall_ornaments(map));
    if (o->ornament == ORNAMENT_NONE)
        return false;
    bool active = sensor_flag(thing, SENSOR_BUSY), state = sensor_flag(thing, SENSOR_STATE);
    switch (sensor_type(thing)) {
    case SENSOR_TOGGLE_ORNAMENT:
        o->animated = state;
        o->frame = state;
        break;
    case SENSOR_TIMED_ORNAMENT:
    case SENSOR_TIMED_ORNAMENT_2:
        o->frame = active ^ state;
        break;
    case SENSOR_HOLDER:
        o->frame = state;
        if (o->frame) {
            o->animated = true;
            o->hides_rest = true;
        }
        break;
    case SENSOR_STATE_MATCH:
        if (active == state)
            o->animated = true;
        else
            o->frame = active;
        break;
    case SENSOR_ANIMATED:
        if (active) {
            o->offset = sensor_frame(thing);
            o->animated = true;
        }
        break;
    case SENSOR_SOUND_ANIMATION:
        if (active) {
            o->time = sensor_data(thing);
            o->animated = true;
        }
        break;
    case SENSOR_WALL_TEXT:
        s->text = face == SIDE_FRONT ? thing : THING_TEXT_NONE;
        break;
    case SENSOR_FRAME_DATA:
        o->frame = sensor_data(thing);
        break;
    case SENSOR_FRAME_STATE:
        o->frame = active;
        break;
    case SENSOR_ALTAR:
        if (sensor_flag(thing, SENSOR_ALTAR_SHOWN)) {
            if (face == SIDE_FRONT)
                s->text = sensor_data(thing);
            o->hides_rest = true;
            o->animated = true;
        }
        break;
    }
    return true;
}

/* Applies the wall things (texts, sensors) to the faces of s; only_front limits them to the front face.
   Returns the first thing that is not a wall thing. */
static uint16_t apply_wall_things(ViewSquare *s, int map, uint16_t thing, int dir, bool only_front)
{
    int done = 0;
    for (; thing != THING_ENDOFLIST && thing_type(thing) <= THING_SENSOR; thing = dungeon_next_thing(thing)) {
        int cell = thing_cell(thing);
        int face = dir_relative(cell, dir);
        if (done & 1 << cell || (only_front && face != SIDE_FRONT))
            continue;
        OrnamentState o = {.time = g_game.game_time};
        bool shows = false;
        if (thing_type(thing) == THING_TEXT)
            shows = wall_text_ornament(s, thing, face, &o);
        else if (thing_type(thing) == THING_SENSOR)
            shows = wall_sensor_ornament(s, map, thing, face, &o);
        if (o.hides_rest)
            done |= 1 << cell;
        if (!shows)
            continue;
        if (o.animated)
            o.frame = ornament_frame(CAT_WALL_ORNAMENT, o.ornament, o.time, o.offset);
        s->side[face] = aspect_face_word(o.ornament, o.frame);
    }
    return thing;
}

/* The floor ornament of a floor text; a shop stand clears the shop display side of s. Returns false when the text
   shows no ornament. */
static bool floor_text_ornament(ViewSquare *s, uint16_t thing, uint8_t square, OrnamentState *o)
{
    if (text_kind(thing) != TEXT_KIND_ORNAMENT)
        return false;
    o->ornament = text_value(thing);
    switch (text_subtype(thing)) {
    case SUBTYPE_STILL:
    case SUBTYPE_TRAP_TELEPORT:
    case SUBTYPE_WARP:
        break;
    case SUBTYPE_ANIMATED:
        o->animated = true;
        break;
    case SUBTYPE_TOGGLE:
        o->animated = text_is_visible(thing);
        break;
    case SUBTYPE_WARP_ONCE:
        o->frame = 1;
        break;
    case SUBTYPE_SQUARE_SWITCH:
        o->frame = square_flag(square, SQUARE_FLAG_3);
        break;
    case SUBTYPE_SHOP_STAND:
        s->side[SIDE_SHOP_DISPLAY] = 0;
        return false;
    default:
        return false;
    }
    return true;
}

/* The floor ornament of a floor sensor; a shop sensor clears the shop display side of s. Returns false when the
   sensor shows no ornament. */
static bool floor_sensor_ornament(ViewSquare *s, int map, uint16_t thing, OrnamentState *o)
{
    int type = sensor_type(thing);
    bool active = sensor_flag(thing, SENSOR_BUSY);
    if (type == SENSOR_SHOP)
        s->side[SIDE_SHOP_DISPLAY] = 0;
    o->ornament = listed_ornament(thing, dungeon_map_floor_ornaments(map));
    if (o->ornament == ORNAMENT_NONE)
        return false;
    if (type == SENSOR_LINK) {
        if (sensor_data(thing) - 1 != map)
            return false;
        o->animated = active;
    } else if (type == SENSOR_ANIMATED && active) {
        o->offset = sensor_frame(thing);
        o->animated = true;
    } else if (type == SENSOR_SOUND_ANIMATION && active) {
        o->time = sensor_data(thing);
        o->animated = true;
    }
    return true;
}

/* The floor ornament of the floor things of a square: texts and sensors with ornaments. */
static uint16_t apply_floor_things(ViewSquare *s, int map, uint16_t thing, uint8_t square)
{
    for (; thing != THING_ENDOFLIST && thing_type(thing) <= THING_SENSOR; thing = dungeon_next_thing(thing)) {
        OrnamentState o = {.time = g_game.game_time};
        bool shows = false;
        if (thing_type(thing) == THING_TEXT)
            shows = floor_text_ornament(s, thing, square, &o);
        else if (thing_type(thing) == THING_SENSOR)
            shows = floor_sensor_ornament(s, map, thing, &o);
        if (!shows)
            continue;
        if (o.animated)
            o.frame = ornament_frame(CAT_FLOOR_ORNAMENT, o.ornament, o.time, o.offset);
        s->side[SIDE_FLOOR_ORNAMENT] = aspect_floor_word(o.ornament, o.frame);
    }
    return thing;
}

/* The first thing after the wall things (texts, sensors) of a thing list. */
static uint16_t skip_wall_things(uint16_t thing)
{
    while (thing != THING_ENDOFLIST && thing_type(thing) <= THING_SENSOR)
        thing = dungeon_next_thing(thing);
    return thing;
}

/* The aspect of a wall or a closed fake wall: its ornaments and wall things. Things show on a fake wall only in
   the row or column of the party. */
static void wall_square_aspect(ViewSquare *s, int map, int dir, int x, int y)
{
    bool fake = square_type(s->square) == SQUARE_FAKE_WALL;
    if (fake)
        s->aspect = ASPECT_WALL;
    else
        set_random_wall_ornaments(s, map, dir, x, y);
    uint16_t thing = apply_wall_things(s, map, aspect_first_thing(map, x, y), dir, false);
    if (fake) {
        /* a closed fake wall away from the party row and column hides its things */
        if (g_game.party_x != x && g_game.party_y != y) {
            s->first_thing = THING_ENDOFLIST;
            return;
        }
    } else {
        for (int k = 0; k < VIEW_SIDE_COUNT; k++) {
            int d = dir_turn(dir, k);
            if (square_type(dungeon_square_edge(map, x + g_dir_dx[d], y + g_dir_dy[d])) == SQUARE_DOOR)
                s->side[k] = ORNAMENT_NONE;
        }
    }
    s->first_thing = thing;
}

/* The floor ornament of every floor square of the current map, if the map graphics have one. */
static void set_map_floor_ornament(ViewSquare *s, int map)
{
    uint16_t v = aspect_value_key(CAT_WALLSET, dungeon_map_wallset(map), MAP_KEY_FLOOR_ORNAMENT);
    if (!v)
        return;
    int frame = v & FLOOR_ORNAMENT_ANIMATED
                    ? ornament_frame(CAT_FLOOR_ORNAMENT, v & ORNAMENT_INDEX_MASK, g_game.game_time, 0)
                    : 0;
    s->side[SIDE_FLOOR_ORNAMENT] = aspect_floor_word(v & ORNAMENT_INDEX_MASK, frame);
}

/* The aspect of a door square: its state and thing; a door front shows the wall things of the face to its right. */
static void set_door_aspect(ViewSquare *s, int map, int dir, int x, int y)
{
    uint8_t square = s->square;
    s->side[SIDE_DOOR_STATE] = square & SQUARE_DOOR_STATE;
    s->side[SIDE_DOOR_THING] = aspect_first_thing(map, x, y);
    if (square_flag(square, SQUARE_DOOR_ALONG_X) == (dir & 1)) {
        s->aspect = ASPECT_DOOR_SIDE;
    } else {
        s->aspect = ASPECT_DOOR_FRONT;
        int d = dir_turn(dir, TURN_RIGHT);
        apply_wall_things(s, map, aspect_first_thing(map, x + g_dir_dx[d], y + g_dir_dy[d]), d, true);
    }
}

void aspect_get(ViewSquare *s, int16_t map, int16_t dir, int16_t x, int16_t y)
{
    for (int k = 0; k < VIEW_SIDE_COUNT; k++)
        s->side[k] = ORNAMENT_NONE;
    s->text = 0;
    uint8_t square = dungeon_square_edge(map, x, y);
    s->square = square;
    uint16_t thing = aspect_first_thing(map, x, y);
    int type = square_type(square);
    s->aspect = (uint16_t)type;
    bool floor_ornament = false;
    bool floor_things = true;
    switch (type) {
    case SQUARE_WALL:
    case SQUARE_FAKE_WALL:
        if (type == SQUARE_FAKE_WALL && square_flag(square, SQUARE_FAKE_WALL_OPEN)) {
            square = FAKE_WALL_FLOOR_SQUARE; /* an open fake wall is a floor */
            s->aspect = ASPECT_FLOOR;
            floor_ornament = true;
            break;
        }
        wall_square_aspect(s, map, dir, x, y);
        return;
    case SQUARE_FLOOR:
        floor_ornament = true;
        break;
    case SQUARE_PIT:
        if (square_flag(square, SQUARE_PIT_OPEN))
            s->side[SIDE_PIT_UP] = square & SQUARE_PIT_VARIANT;
        else {
            s->aspect = ASPECT_FLOOR;
            floor_ornament = true;
        }
        break;
    case SQUARE_STAIRS:
        s->aspect = (uint16_t)(ASPECT_STAIRS_FRONT + (square_flag(square, SQUARE_STAIRS_ALONG_X) != (dir & 1)));
        s->side[SIDE_STAIRS_UP] = square & SQUARE_STAIRS_UP;
        floor_things = false;
        break;
    case SQUARE_DOOR:
        set_door_aspect(s, map, dir, x, y);
        floor_things = false;
        break;
    case SQUARE_TELEPORTER:
        if (!square_flag(square, SQUARE_TELEPORTER_ON) || !square_flag(square, SQUARE_TELEPORTER_VISIBLE)) {
            s->aspect = ASPECT_FLOOR;
            s->square &= ~SQUARE_TELEPORTER_ON;
        }
        break;
    default:
        s->first_thing = thing;
        return;
    }
    if (floor_ornament)
        set_map_floor_ornament(s, map);
    s->first_thing = floor_things ? apply_floor_things(s, map, thing, square) : skip_wall_things(thing);
}

int aspect_wall_face_ornament(int16_t map, uint16_t first, int16_t dir)
{
    ViewSquare s;
    memset(&s, ORNAMENT_NONE, sizeof s); /* every byte of the square, not only its ornament fields */
    apply_wall_things(&s, map, first, dir, true);
    return aspect_word_ornament(s.side[SIDE_FRONT]);
}

/* ---- The cell grid ---- */

int aspect_rotate_cell(int grid_cell, int16_t dir)
{
    int x = grid_cell % CELL_GRID_SIDE - CELL_GRID_HALF;
    int y = grid_cell / CELL_GRID_SIDE - CELL_GRID_HALF;
    int t;
    switch (dir) {
    case DIR_EAST:
        t = x;
        x = y;
        y = -t;
        break;
    case DIR_SOUTH:
        x = -x;
        y = -y;
        break;
    case DIR_WEST:
        t = x;
        x = -y;
        y = t;
        break;
    }
    return (y + CELL_GRID_HALF) * CELL_GRID_SIDE + x + CELL_GRID_HALF;
}

int aspect_creature_cell(uint16_t group, int16_t dir)
{
    if (!gdat_exists(gdat_current(), CAT_CREATURE, group_creature(group), GDAT_TYPE_CREATURE_DATA, GFX_CREATURE_FRAMES))
        return CELL_CENTER;
    uint8_t frame[CREATURE_FRAME_SIZE];
    creature_anim_frame(gdat_current(), group, frame);
    return aspect_rotate_cell(frame[FRAME_CELL], dir);
}

void aspect_cell_to_xy(int base_x, int base_y, int grid_cell, int16_t *out_x, int16_t *out_y)
{
    *out_x = (int16_t)(grid_cell % CELL_GRID_SIDE + base_x * GRID_SQUARE_CELLS);
    *out_y = (int16_t)(grid_cell / CELL_GRID_SIDE + base_y * GRID_SQUARE_CELLS);
}
