#ifndef ASPECT_H
#define ASPECT_H
#include <stdint.h>

/* Prefixes: aspect_ cell_grid_ */

#define VIEW_SIDE_COUNT 4

/* What the view shows of one square (the original record, 18 bytes). */
typedef struct {
    int8_t x, y;                  /* map position */
    uint16_t aspect;              /* enum view_aspect */
    uint16_t square;              /* the square byte */
    uint16_t first_thing;         /* first thing that is not a wall thing */
    uint16_t side[VIEW_SIDE_COUNT]; /* by aspect: wall ornaments of the 4 faces, door state and thing, pit or stairs bits */
    uint16_t text;                /* wall text thing or portrait index */
} ViewSquare;

#define VIEW_SQUARE_RECORD_SIZE 18
_Static_assert(sizeof(ViewSquare) == VIEW_SQUARE_RECORD_SIZE, "the original view square record has 18 bytes");

/* The ornament word of a wall face: the ornament in the low byte, the frame in bits 10-15. */
#define ORN_FRAME_SHIFT 10
#define aspect_face_word(ornament, frame) ((uint16_t)((frame) << ORN_FRAME_SHIFT | (ornament)))
#define aspect_face_frame(word) ((int16_t)(word) >> ORN_FRAME_SHIFT)
/* The ornament word of a floor: the ornament in the low byte, frame * 10 in the high byte. */
#define FLOOR_FRAME_SHIFT 8
#define FLOOR_FRAME_STEP 10
#define aspect_floor_word(ornament, frame) ((uint16_t)((ornament) | (frame) * FLOOR_FRAME_STEP << FLOOR_FRAME_SHIFT))
#define aspect_floor_frame(word) ((int8_t)((word) >> FLOOR_FRAME_SHIFT) / FLOOR_FRAME_STEP)
#define ORNAMENT_NONE 0xff
#define ORNAMENT_INDEX_MASK 0xff
/* The ornament of a wall face or floor word: its low byte. */
static inline uint8_t aspect_word_ornament(uint16_t w) { return (uint8_t)w; }
#define ORNAMENT_PLATE 0            /* a text plate instead of a wall ornament */
/* View square sides by aspect. */
enum { SIDE_DOOR_STATE = 0, SIDE_DOOR_THING = 1, SIDE_FLOOR_ORNAMENT = 2, SIDE_STAIRS_UP = 0, SIDE_PIT_UP = 0 };
#define SIDE_SHOP_FLAG 3            /* floor: a shop display there sets it to 0 (read by the things drawing, 12b) */
/* Value of a view square that the original reads as "no thing" or "none". */
#define THING_TEXT_NONE 0xffff

enum view_aspect {
    ASPECT_WALL, ASPECT_FLOOR, ASPECT_PIT, ASPECT_TELEPORTER = 5, ASPECT_HIDDEN = 7,
    ASPECT_DOOR_SIDE = 0x10, ASPECT_DOOR_FRONT, ASPECT_STAIRS_FRONT, ASPECT_STAIRS_SIDE
};

/* Text and sensor fields of wall and floor things. */
enum sensor_type {
    SENSOR_TOGGLE_ORNAMENT = 0x17, SENSOR_TIMED_ORNAMENT = 0x18, SENSOR_HOLDER = 0x1a, SENSOR_STATE_MATCH = 0x1b,
    SENSOR_LINK = 0x27, SENSOR_ANIMATED = 0x2c, SENSOR_SHOP = 0x30, SENSOR_SOUND_ANIMATION = 0x32,
    SENSOR_WALL_TEXT = 0x3f, SENSOR_FRAME_DATA = 0x41, SENSOR_FRAME_STATE = 0x46, SENSOR_TIMED_ORNAMENT_2 = 0x4a,
    SENSOR_ALTAR = 0x7e,
};
#define SENSOR_ALTAR_SHOWN SENSOR_STATE
#define ORN_KEY_KIND 0x0a           /* (category, ornament, 0x0b, key): wall ornament kind; door: zone row of the damage picture */
#define WALL_TEXT_SIZE 128          /* a GDAT text of the map or ornament pictures */

/* The 5 x 5 grid of the cells of a square and its neighbours (aspect_cell_to_xy), row by row. */
#define CELL_GRID_SIDE 5
#define CELL_GRID_HALF (CELL_GRID_SIDE / 2)  /* the center row and column */
#define CELL_GRID_COUNT (CELL_GRID_SIDE * CELL_GRID_SIDE)
#define cell_grid_index(row, col) ((row) * CELL_GRID_SIDE + (col))
#define CELL_CENTER cell_grid_index(CELL_GRID_HALF, CELL_GRID_HALF)
#define GRID_SQUARE_CELLS 4         /* grid cells per square side; the 5 x 5 cells of neighbour squares overlap */

/* Sets the random wall ornament list of map: the aspect part of entering a map. */
void aspect_enter_map(int map);
/* The first thing of the square (x, y) of map, THING_ENDOFLIST away from the map. */
uint16_t aspect_first_thing(int map, int x, int y);
/* The value (category, index, 0x0b, entry) of the graphics. */
uint16_t aspect_value_key(int category, int index, int entry);
/* What the party looking to dir sees of the square (x, y) of map. */
void aspect_get(ViewSquare *s, int map, int dir, int x, int y);
/* The ornament (low byte of the front face word) that the wall things from first show on the face that looks to
   dir, as dun_apply_wall_things_to_aspect; 0xff none. */
int aspect_wall_face_ornament(int map, uint16_t first, int dir);
/* The grid cell (0 to 24) of a creature in its square, turned by dir quarter turns. */
int aspect_creature_cell(uint16_t group, int dir);
/* Turns a cell of a 5 x 5 grid around its center by dir quarter turns. */
int aspect_rotate_cell(int cell, int dir);
/* The thing grid position of cell (0 to 24) of the square (base_x, base_y) of the map. */
void aspect_cell_to_xy(int base_x, int base_y, uint16_t cell, int16_t *x, int16_t *y);

#endif
