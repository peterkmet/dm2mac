#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>
#include "gfx.h"

/* The dungeon view shows 23 squares: 3 at depth 0 and 1 (the party square, left, right), 5 at depth 2 and 3,
   7 at depth 4, ordered center, left, right, then further out. */
#define VIEW_SQUARE_COUNT 23
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

_Static_assert(sizeof(ViewSquare) == 18, "the original view square record has 18 bytes");

enum view_aspect {
    ASPECT_WALL, ASPECT_FLOOR, ASPECT_PIT, ASPECT_TELEPORTER = 5, ASPECT_HIDDEN = 7,
    ASPECT_DOOR_SIDE = 0x10, ASPECT_DOOR_FRONT, ASPECT_STAIRS_FRONT, ASPECT_STAIRS_SIDE
};

extern ViewSquare view_squares[VIEW_SQUARE_COUNT];
/* The view picture, 224 x 136 pixels. */
extern Bitmap view_viewport;
/* The party light level, 0 (bright) to 5. Step 17 computes it; until then it stays as set. */
extern int view_light_level;

/* Sets up the map graphics of map for the view: the view part of entering a map. */
void view_enter_map(int map);
/* Loads the view square at view position pos for the party at (x, y) looking to dir. */
void view_load_square(int x, int y, int pos, int dir);
/* Shows the squares behind a seamless passage that view_mark_hidden_squares hid. */
void view_unhide_linked_squares(void);
/* Hides the wall squares that walls in front of them cover. */
void view_mark_hidden_squares(void);
/* Draws the view of the party at (x, y) of the party map looking to dir into view_viewport. */
void view_draw_dungeon(int dir, int x, int y);
/* The view zone of grid cell cell (0 to 24, 5 x 5) of view element base, turned by dir. */
int zone_id_for_view_cell(int base, int cell, int dir);
/* Turns a cell of a 5 x 5 grid around its center by dir quarter turns. */
int view_rotate_grid5_cell(int cell, int dir);

#endif
