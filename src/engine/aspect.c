/* SPDX-License-Identifier: GPL-3.0-or-later */
/* What the party sees of a square: its aspect, ornaments, and first thing; the 5 x 5 cell grid of a square. */
#include "aspect.h"
#include "gdat.h"
#include "creature.h"
#include "dungeon.h"
#include "move.h"
#include "save.h"
#include "sensor.h"
#include <string.h>

#define FAKE_WALL_FLOOR_SQUARE  1 /* the square byte that the floor things see on an open fake wall */
/* (CAT_WALLSET, graphics set, 0x0b, key): ornament on every floor square, bit 15: animated */
#define MAP_KEY_FLOOR_ORNAMENT  0x6b
#define ORN_KEY_FRAMES          0x0d /* (category, ornament, 0x0b, key): animation frames, bit 15: frames start at 1 */
#define FRAMES_FROM_ONE         0x8000
#define FLOOR_ORNAMENT_ANIMATED 0x8000
#define CREATURE_FRAMES         0xfd /* creature data: the animation frames */
#define RANDOM_ORNAMENT_SLOTS   30   /* random wall ornament hash: one of 30 values picks an ornament of the list */

/* The random wall ornament hash of SKULL.EXE. */
#define HASH_X_STEP     32
#define HASH_X_BASE     2000
#define HASH_MAP_STEP   64
#define HASH_MAP_BASE   3000
#define HASH_MULTIPLIER 0x7ab9
#define HASH_MAP_FACTOR 11

/* Random wall ornaments: the list of the map that view_enter_map entered last. */
static const uint8_t *wall_ornaments;

void aspect_enter_map(int map)
{
    wall_ornaments = dungeon_map_wall_ornaments(map);
}

uint16_t aspect_first_thing(int map, int x, int y)
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

static void set_random_wall_ornaments(ViewSquare *s, int map, const int allowed[4], int dir, int x, int y)
{
    int count = dungeon_map_random_wall_count(map);
    for (int k = 0; k < VIEW_SIDE_COUNT; k++)
        s->side[k] = random_ornament(map, allowed[k], count, x, (((dir + k) & DIR_MASK) + 1) * (y + 1));
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
        gdat_text(gdat_current(), category, ornament, ORN_KEY_FRAMES, text, sizeof text);
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
static uint8_t sensor_ornament(uint16_t sensor, const uint8_t *list)
{
    int ordinal = sensor_ornament_ordinal(sensor);
    return ordinal ? list[ordinal - 1] : ORNAMENT_NONE;
}

/* How a wall or floor thing shows its ornament: a fixed frame, or an animation at time + offset. */
typedef struct {
    uint8_t ornament;
    int animated, frame, offset;
    uint32_t time;
} OrnamentState;

/* The ornament of a wall text on face of s; a text plate goes on the face at once. Sets *last when the things
   behind it in its cell do not show. Returns 0 when the text shows no ornament. */
static int wall_text_ornament(ViewSquare *s, uint16_t thing, int face, OrnamentState *o, int *last)
{
    int kind = text_kind(thing), subtype = text_subtype(thing);
    bool visible = text_is_visible(thing);
    if (kind == TEXT_KIND_TEXT || (kind == TEXT_KIND_ORNAMENT && subtype == SUBTYPE_PLATE)) {
        s->side[face] = ORNAMENT_PLATE;
        if (face == SIDE_FRONT)
            s->text = visible ? thing : THING_TEXT_NONE;
        *last = 1;
        return 0;
    }
    if (kind != TEXT_KIND_ORNAMENT)
        return 0;
    o->ornament = text_value(thing);
    switch (subtype) {
    case SUBTYPE_STILL:
    case SUBTYPE_LADDER:
        break;
    case SUBTYPE_ANIMATED:
        o->animated = 1;
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
            o->animated = 1;
            *last = 1;
        }
        break;
    default:
        return 0;
    }
    return 1;
}

/* The ornament of a wall sensor on face of s by its sensor type; wall text and altar sensors set the text of s.
   Sets *last when the things behind it in its cell do not show. Returns 0 when the sensor has no ornament. */
static int wall_sensor_ornament(ViewSquare *s, int map, uint16_t thing, int face, OrnamentState *o, int *last)
{
    o->ornament = sensor_ornament(thing, dungeon_map_wall_ornaments(map));
    if (o->ornament == ORNAMENT_NONE)
        return 0;
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
            o->animated = 1;
            *last = 1;
        }
        break;
    case SENSOR_STATE_MATCH:
        if (active == state)
            o->animated = 1;
        else
            o->frame = active;
        break;
    case SENSOR_ANIMATED:
        if (active) {
            o->offset = sensor_frame(thing);
            o->animated = 1;
        }
        break;
    case SENSOR_SOUND_ANIMATION:
        if (active) {
            o->time = sensor_data(thing);
            o->animated = 1;
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
            *last = 1;
            o->animated = 1;
        }
        break;
    }
    return 1;
}

/* Applies the wall things (texts, sensors) to the faces of s; only_front limits them to the front face.
   Returns the first thing that is not a wall thing. */
static uint16_t apply_wall_things(ViewSquare *s, int map, uint16_t thing, int dir, int only_front)
{
    int done = 0;
    for (; thing != THING_ENDOFLIST && thing_type(thing) <= THING_SENSOR; thing = dungeon_next_thing(thing)) {
        int cell = thing_cell(thing), face = dir_relative(cell, dir);
        if (done & 1 << cell || (only_front && face != SIDE_FRONT))
            continue;
        OrnamentState o = {.time = game.game_time};
        int last = 0, shows = 0;
        if (thing_type(thing) == THING_TEXT)
            shows = wall_text_ornament(s, thing, face, &o, &last);
        else if (thing_type(thing) == THING_SENSOR)
            shows = wall_sensor_ornament(s, map, thing, face, &o, &last);
        if (last)
            done |= 1 << cell;
        if (!shows)
            continue;
        if (o.animated)
            o.frame = ornament_frame(CAT_WALL_ORNAMENT, o.ornament, o.time, o.offset);
        s->side[face] = aspect_face_word(o.ornament, o.frame);
    }
    return thing;
}

/* The floor ornament of the floor things of a square: texts and sensors with ornaments. */
static uint16_t apply_floor_things(ViewSquare *s, int map, uint16_t thing, uint8_t square)
{
    for (; thing != THING_ENDOFLIST && thing_type(thing) <= THING_SENSOR; thing = dungeon_next_thing(thing)) {
        int animated = 0, frame = 0, offset = 0;
        uint32_t time = game.game_time;
        uint8_t ornament;
        if (thing_type(thing) == THING_TEXT) {
            if (text_kind(thing) != TEXT_KIND_ORNAMENT)
                continue;
            ornament = text_value(thing);
            switch (text_subtype(thing)) {
            case SUBTYPE_STILL:
            case SUBTYPE_TRAP_TELEPORT:
            case SUBTYPE_WARP:
                break;
            case SUBTYPE_ANIMATED:
                animated = 1;
                break;
            case SUBTYPE_TOGGLE:
                animated = text_is_visible(thing);
                break;
            case SUBTYPE_WARP_ONCE:
                frame = 1;
                break;
            case SUBTYPE_SQUARE_SWITCH:
                frame = square_flag(square, SQUARE_FLAG_3);
                break;
            case SUBTYPE_SHOP_STAND:
                s->side[SIDE_SHOP_FLAG] = 0;
                continue;
            default:
                continue;
            }
        } else if (thing_type(thing) == THING_SENSOR) {
            int type = sensor_type(thing);
            bool active = sensor_flag(thing, SENSOR_BUSY);
            if (type == SENSOR_SHOP)
                s->side[SIDE_SHOP_FLAG] = 0;
            ornament = sensor_ornament(thing, dungeon_map_floor_ornaments(map));
            if (ornament == ORNAMENT_NONE)
                continue;
            if (type == SENSOR_LINK) {
                if (sensor_data(thing) - 1 != map)
                    continue;
                animated = active;
            } else if (type == SENSOR_ANIMATED && active) {
                offset = sensor_frame(thing);
                animated = 1;
            } else if (type == SENSOR_SOUND_ANIMATION && active) {
                time = sensor_data(thing);
                animated = 1;
            }
        } else {
            continue;
        }
        if (animated)
            frame = ornament_frame(CAT_FLOOR_ORNAMENT, ornament, time, offset);
        s->side[SIDE_FLOOR_ORNAMENT] = aspect_floor_word(ornament, frame);
    }
    return thing;
}

static uint16_t skip_wall_things(uint16_t thing)
{
    while (thing != THING_ENDOFLIST && thing_type(thing) <= THING_SENSOR)
        thing = dungeon_next_thing(thing);
    return thing;
}

/* The aspect of a wall or a closed fake wall: its ornaments and wall things. Things show on a fake wall only in
   the row or column of the party. */
static void wall_square_aspect(ViewSquare *s, int map, uint8_t square, int fake, uint16_t thing, int dir, int x, int y)
{
    if (fake) {
        s->aspect = ASPECT_WALL;
    } else {
        int allowed[VIEW_SIDE_COUNT];
        for (int k = 0; k < VIEW_SIDE_COUNT; k++)
            allowed[k] = square & WALL_FACE(dir_turn(dir, k));
        set_random_wall_ornaments(s, map, allowed, dir, x, y);
    }
    thing = apply_wall_things(s, map, thing, dir, 0);
    if (fake) {
        /* a closed fake wall away from the party row and column hides its things */
        if (game.party_x != x && game.party_y != y) {
            s->first_thing = THING_ENDOFLIST;
            return;
        }
    } else {
        for (int k = 0; k < VIEW_SIDE_COUNT; k++) {
            int d = dir_turn(dir, k);
            if (square_type(dungeon_square_edge(map, x + dir_dx[d], y + dir_dy[d])) == SQUARE_DOOR)
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
                    ? ornament_frame(CAT_FLOOR_ORNAMENT, v & ORNAMENT_INDEX_MASK, game.game_time, 0)
                    : 0;
    s->side[SIDE_FLOOR_ORNAMENT] = aspect_floor_word(v & ORNAMENT_INDEX_MASK, frame);
}

void aspect_get(ViewSquare *s, int map, int dir, int x, int y)
{
    s->side[0] = s->side[1] = s->side[2] = s->side[3] = ORNAMENT_NONE;
    s->text = 0;
    uint8_t square = dungeon_square_edge(map, x, y);
    s->square = square;
    uint16_t thing = aspect_first_thing(map, x, y);
    int type = square_type(square);
    s->aspect = (uint16_t)type;
    int floor_ornament = 0, floor_things = 1;
    switch (type) {
    case SQUARE_WALL:
    case SQUARE_FAKE_WALL:
        if (type == SQUARE_FAKE_WALL && square_flag(square, SQUARE_FAKE_WALL_OPEN)) {
            square = FAKE_WALL_FLOOR_SQUARE; /* an open fake wall is a floor */
            s->aspect = ASPECT_FLOOR;
            floor_ornament = 1;
            break;
        }
        wall_square_aspect(s, map, square, type == SQUARE_FAKE_WALL, thing, dir, x, y);
        return;
    case SQUARE_FLOOR:
        floor_ornament = 1;
        break;
    case SQUARE_PIT:
        if (square_flag(square, SQUARE_PIT_OPEN))
            s->side[SIDE_PIT_UP] = square & SQUARE_PIT_VARIANT;
        else {
            s->aspect = ASPECT_FLOOR;
            floor_ornament = 1;
        }
        break;
    case SQUARE_STAIRS:
        s->aspect = (uint16_t)(ASPECT_STAIRS_FRONT + (square_flag(square, SQUARE_STAIRS_ALONG_X) != (dir & 1)));
        s->side[SIDE_STAIRS_UP] = square & SQUARE_STAIRS_UP;
        floor_things = 0;
        break;
    case SQUARE_DOOR:
        s->side[SIDE_DOOR_STATE] = square & SQUARE_DOOR_STATE;
        s->side[SIDE_DOOR_THING] = aspect_first_thing(map, x, y);
        if (square_flag(square, SQUARE_DOOR_ALONG_X) == (dir & 1)) {
            s->aspect = ASPECT_DOOR_SIDE;
        } else {
            s->aspect = ASPECT_DOOR_FRONT;
            int d = dir_turn(dir, TURN_RIGHT);
            apply_wall_things(s, map, aspect_first_thing(map, x + dir_dx[d], y + dir_dy[d]), d, 1);
        }
        floor_things = 0;
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

int aspect_wall_face_ornament(int map, uint16_t first, int dir)
{
    ViewSquare s;
    memset(&s, ORNAMENT_NONE, sizeof s); /* every byte of the square, not only its ornament fields */
    apply_wall_things(&s, map, first, dir, 1);
    return aspect_word_ornament(s.side[SIDE_FRONT]);
}

/* ---- The cell grid ---- */

int aspect_rotate_cell(int cell, int dir)
{
    int x = cell % CELL_GRID_SIDE - CELL_GRID_HALF, y = cell / CELL_GRID_SIDE - CELL_GRID_HALF, t;
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

/* The grid cell of a creature in its square, turned by dir: the cell of its animation frame. */
int aspect_creature_cell(uint16_t group, int dir)
{
    if (!gdat_exists(gdat_current(), CAT_CREATURE, group_creature(group), GDAT_TYPE_CREATURE_DATA, CREATURE_FRAMES))
        return CELL_CENTER;
    uint8_t frame[CREATURE_FRAME_SIZE];
    creature_anim_frame(gdat_current(), group, frame);
    return aspect_rotate_cell(frame[FRAME_CELL], dir);
}

void aspect_cell_to_xy(int base_x, int base_y, uint16_t cell, int16_t *x, int16_t *y)
{
    *x = (int16_t)(cell % CELL_GRID_SIDE + base_x * GRID_SQUARE_CELLS);
    *y = (int16_t)(cell / CELL_GRID_SIDE + base_y * GRID_SQUARE_CELLS);
}
