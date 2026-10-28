/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_ASPECT_H
#define DM2_ENGINE_ASPECT_H
/* Prefixes: aspect_ cell_grid_ */

/* What the party sees of a square: its aspect, and the cell grid of a square. */

#include <stdint.h>

/* The sides of a view square: the 4 faces of a wall. */
#define VIEW_SIDE_COUNT 4

/* What the view shows of one square (the original record, 18 bytes). */
typedef struct {
    int8_t x, y;          /* map position */
    uint16_t aspect;      /* ASPECT_* */
    uint16_t square;      /* the square byte */
    uint16_t first_thing; /* first thing that is not a wall thing */
    uint16_t
        side[VIEW_SIDE_COUNT]; /* by aspect: wall ornaments of the 4 faces, door state and thing, pit or stairs bits */
    uint16_t text;             /* wall text thing or portrait index */
} ViewSquare;

/* The size of the view square record of the original. */
#define VIEW_SQUARE_RECORD_SIZE 18
_Static_assert(sizeof(ViewSquare) == VIEW_SQUARE_RECORD_SIZE, "the original view square record has 18 bytes");

/* The ornament word of a wall face: the ornament in the low byte, the frame in bits 10-15. */
#define ORN_FRAME_SHIFT                    10
#define aspect_face_word(ornament, frame)  ((uint16_t)((frame) << ORN_FRAME_SHIFT | (ornament)))
#define aspect_face_frame(word)            ((int16_t)(word) >> ORN_FRAME_SHIFT)
/* The ornament word of a floor: the ornament in the low byte, frame * 10 in the high byte. */
#define FLOOR_FRAME_SHIFT                  8
#define FLOOR_FRAME_STEP                   10
#define aspect_floor_word(ornament, frame) ((uint16_t)((ornament) | (frame) * FLOOR_FRAME_STEP << FLOOR_FRAME_SHIFT))
#define aspect_floor_frame(word)           ((int8_t)((word) >> FLOOR_FRAME_SHIFT) / FLOOR_FRAME_STEP)
/* No ornament, and the mask of the ornament in an ornament word. */
#define ORNAMENT_NONE                      0xff
#define ORNAMENT_INDEX_MASK                0xff
/* The ornament of a wall face or floor word: its low byte. */
static inline uint8_t aspect_word_ornament(uint16_t w)
{
    return (uint8_t)w;
}
/* The wall ornament of a text plate. */
#define ORNAMENT_PLATE      0 /* a text plate instead of a wall ornament */
/* View square sides by aspect. */
#define SIDE_DOOR_STATE     0
#define SIDE_DOOR_THING     1
#define SIDE_FLOOR_ORNAMENT 2
#define SIDE_STAIRS_UP      0
#define SIDE_PIT_UP         0
#define SIDE_FRONT          2 /* the wall face that looks at the party */
#define SIDE_SHOP_DISPLAY   3 /* floor: a shop display there sets it to 0 (read by the things drawing) */
/* Value of a view square that the original reads as "no thing" or "none". */
#define THING_TEXT_NONE     0xffff

/* What the view shows of a square (ViewSquare.aspect). */
#define ASPECT_WALL         0x00
#define ASPECT_FLOOR        0x01
#define ASPECT_PIT          0x02
#define ASPECT_TELEPORTER   0x05
#define ASPECT_HIDDEN       0x07
#define ASPECT_DOOR_SIDE    0x10
#define ASPECT_DOOR_FRONT   0x11
#define ASPECT_STAIRS_FRONT 0x12
#define ASPECT_STAIRS_SIDE  0x13

/* The sensor types that the view reads (the others in sensor.h). */
#define SENSOR_ANIMATED        0x2c
#define SENSOR_SHOP            0x30
#define SENSOR_SOUND_ANIMATION 0x32
#define SENSOR_FRAME_DATA      0x41
/* (category, ornament, GDAT_TYPE_VALUE, key): wall ornament kind; door: zone row of the damage picture */
#define ORN_KEY_KIND           0x0a
/* The values of ORN_KEY_KIND of a wall ornament: what the ornament holds. */
#define KIND_ALCOVE            0x01
#define KIND_SHOP              0x02
#define KIND_PORTRAIT          0x03
/* The size of a wall text buffer. */
#define WALL_TEXT_SIZE         128 /* a GDAT text of the map or ornament pictures */

/* The 5 x 5 grid of the cells of a square and its neighbours (aspect_cell_to_xy), row by row. */
#define CELL_GRID_SIDE            5
#define CELL_GRID_HALF            (CELL_GRID_SIDE / 2) /* the center row and column */
#define CELL_GRID_COUNT           (CELL_GRID_SIDE * CELL_GRID_SIDE)
#define cell_grid_index(row, col) ((row) * CELL_GRID_SIDE + (col))
/* The center cell of the grid, and the grid cells of a square side. */
#define CELL_CENTER               cell_grid_index(CELL_GRID_HALF, CELL_GRID_HALF)
#define GRID_SQUARE_CELLS         4 /* grid cells per square side; the 5 x 5 cells of neighbour squares overlap */

/* A point of the thing grid, in thing grid cells. */
typedef struct {
    int16_t x, y;
} GridPoint;

/* Setup */
/* Sets the random wall ornament list of map: the aspect part of entering a map. */
void aspect_enter_map(int16_t map);

/* Lookups */
/* The first thing of the square (x, y) of map, THING_ENDOFLIST away from the map. */
uint16_t aspect_first_thing(int16_t map, int16_t x, int16_t y);
/* The value (category, index, GDAT_TYPE_VALUE, entry) of the graphics. */
uint16_t aspect_value_key(int category, int index, int entry);

/* Square aspects */
/* What the party looking to dir sees of the square (x, y) of map. */
void aspect_get(ViewSquare *s, int16_t map, int16_t dir, int16_t x, int16_t y);
/* The ornament (low byte of the front face word) that the wall things from first show on the face that looks to
   dir, as aspect_get shows them; ORNAMENT_NONE none. */
int aspect_wall_face_ornament(int16_t map, uint16_t first, int16_t dir);

/* The cell grid */
/* Turns grid_cell of a 5 x 5 grid around its center by dir quarter turns. */
int aspect_rotate_cell(int grid_cell, int16_t dir);
/* The grid cell (0 to 24) of a creature in its square, turned by dir quarter turns: the cell of its animation frame. */
int aspect_creature_cell(uint16_t group, int16_t dir);
/* The thing grid position of grid_cell (0 to 24) of the square (base_x, base_y) of the map. */
GridPoint aspect_cell_to_xy(int base_x, int base_y, int grid_cell);

#endif /* DM2_ENGINE_ASPECT_H */
