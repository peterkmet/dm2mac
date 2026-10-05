#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>
#include "draw.h"
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
/* Thing grids of 21 x 17 cells (4 x 4 per square) around the party, at (lateral + 10) * 17 + forward. */
#define VIEW_GRID_SIZE 0x166
/* The view position of the square that holds each grid cell. */
extern uint8_t view_creature_grid[VIEW_GRID_SIZE];
/* The view position of the square of the creature that stands at each grid cell, 0xff for none. */
extern uint8_t view_object_grid[VIEW_GRID_SIZE];
/* By view position: bit n set when grid cell n of the square holds a missile, or an object. */
extern uint32_t view_missile_masks[VIEW_SQUARE_COUNT];
extern uint32_t view_object_masks[VIEW_SQUARE_COUNT];
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
/* Fills the cleared band of the viewport (a gap that the floor and ceiling leave). */
void view_clear_viewport(void);
/* True when the last view picture shows the party position and direction. */
int view_shows_party(void);
/* Draws the view of the party at (x, y) of the party map looking to dir into view_viewport. */
void view_draw_dungeon(int dir, int x, int y);
/* The colors of a 4 bpp picture dimmed by the view shade, with key kept; NULL for an 8 bpp picture in full dark. */
const uint8_t *view_get_shaded_palette(int category, int index, int entry, int key);
/* Copies a bitmap mirrored left to right. */
void view_copy_image_mode1(const Bitmap *src, Bitmap *dst);
/* The view zone of grid cell cell (0 to 24, 5 x 5) of view element base, turned by dir. */
int zone_id_for_view_cell(int base, int cell, int dir);
/* Fills the offscreen bitmap of the wet picture ctx: with rain, a random part of the rain picture, else key. */
void view_draw_wet_image(const DrawCtx *ctx, Bitmap *offscreen, int key);
/* The grid cell (0 to 24) of a creature in its square, turned by dir quarter turns. */
int creature_get_view_cell_zone(uint16_t group, int dir);
/* Turns a cell of a 5 x 5 grid around its center by dir quarter turns. */
int view_rotate_grid5_cell(int cell, int dir);
/* The thing grid position of cell (0 to 24) of the square (base_x, base_y) of the map. */
void view_grid5_cell_to_xy(int base_x, int base_y, uint16_t cell, int16_t *x, int16_t *y);
/* Moves a creature of size 1 to 3 at (*x, *y) toward the party eye at (party_x, party_y), in thing grid cells. */
void view_offset_creature_toward_party(int16_t party_x, int16_t party_y, int16_t size, int16_t *x, int16_t *y);

#endif
