/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_VIEW_H
#define DM2_UI_VIEW_H
/* Prefixes: view_ */

/* The dungeon view: the squares in sight and their drawing. */

#include <stdbool.h>
#include <stdint.h>
#include "aspect.h"
#include "draw.h"
#include "gfx.h"
#include "weather.h"

/* The dungeon view shows 23 squares: 3 at depth 0 and 1 (the party square, left, right), 5 at depth 2 and 3,
   7 at depth 4, ordered center, left, right, then further out. */
#define VIEW_SQUARE_COUNT 23

/* Picture kinds of a missile, by the pictures of its payload. */
enum {
    MISSILE_NO_PICTURE = -1,
    MISSILE_MIRRORED,               /* front pictures 8 and 9, mirrored by the cell */
    MISSILE_FRONT_BACK,             /* front picture 8, back picture 10 */
    MISSILE_FRONT,                  /* front picture 8 only */
    MISSILE_NO_SIDE,                /* no side picture 12 */
};
enum { MISSILE_PICTURE_FRONT = 8, MISSILE_PICTURE_FRONT_2 = 9, MISSILE_PICTURE_BACK = 0x0a, MISSILE_PICTURE_SIDE = 0x0c };

extern ViewSquare view_squares[VIEW_SQUARE_COUNT];
/* Thing grids of 21 x 17 cells (4 x 4 per square) around the party, at (lateral + 10) * 17 + forward. */
#define VIEW_GRID_SIZE 358
/* The view position of the square that holds each grid cell. */
extern uint8_t view_creature_grid[VIEW_GRID_SIZE];
/* The view position of the square of the creature that stands at each grid cell, 0xff for none. */
extern uint8_t view_object_grid[VIEW_GRID_SIZE];
/* By view position: bit n set when grid cell n of the square holds a missile, or an object. */
extern uint32_t view_missile_masks[VIEW_SQUARE_COUNT];
extern uint32_t view_object_masks[VIEW_SQUARE_COUNT];
/* The view picture, 224 x 136 pixels. */
extern Bitmap view_viewport;
/* View positions of the party square and of the square in front of it. */
#define POS_PARTY 0
#define POS_FRONT 3
/* Sets up the map graphics of map for the view: the view part of entering a map. */
void view_enter_map(int map);
/* Loads the view square at view position pos for the party at (x, y) looking to dir. */
void view_load_square(int x, int y, int pos, int dir);
/* Shows the squares behind a seamless passage that view_mark_hidden_squares hid. */
void view_unhide_linked_squares(void);
/* Hides the wall squares that walls in front of them cover. */
void view_mark_hidden_squares(void);
/* A click zone of the view: where an object, an ornament, a door button, or creature possessions show. */
typedef struct {
    int16_t rect[RECT_SIZE];
    uint16_t thing;             /* THING_NONE: the zone is not set */
    uint8_t cell;               /* view position or cell, by kind */
    uint8_t kind;               /* enum view_click_kind */
} ViewClickZone;
enum view_click_kind {
    CLICK_NONE, CLICK_OBJECT, CLICK_POSSESSIONS, CLICK_ALCOVE, CLICK_DOOR_BUTTON, CLICK_WALL_ORNAMENT = 6
};
#define VIEW_CLICK_ZONE_MAX 32
#define VIEW_CLICK_ZONE_FIXED 9     /* zones 0 to 7: the cells of the party square and of the square in front; 8: an alcove */
#define VIEW_CLICK_ZONE_ALCOVE (VIEW_CLICK_ZONE_FIXED - 1)  /* the click zone of the alcove ahead */
extern ViewClickZone view_click_zones[VIEW_CLICK_ZONE_MAX];
extern int view_click_zone_count;
/* The object of a click at (x, y) of the viewport among the objects in the cell of thing (the cell bits) on the
   square (square_x, square_y) at view position pos, by click zone kind; a chest opens or closes. placing: thing is the
   leader hand object that goes there. Returns the object, THING_NONE when the click is done (a chest changed, or
   thing went into an open chest), THING_ENDOFLIST when thing must drop; thing when nothing is hit. */
uint16_t view_hit_test_object(int x, int y, int pos, int square_x, int square_y, uint16_t thing, int kind, int placing);
/* The vertical place of the last hit test of a view picture, in percent of half the picture height from its center:
   a shop click above the center shows the next item, else the last item. */
extern int16_t view_hit_offset_y;
/* True when the ornament of the wall at view position pos (0 to 3) shows a pixel at (x, y) of the viewport. */
int view_hit_test_wall_ornament(int pos, int x, int y);
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
/* Fills the offscreen bitmap of the wet picture ctx: with rain, a random part of the rain picture, else key. */
void view_draw_wet_image(const DrawCtx *ctx, Bitmap *offscreen, int key);
/* Moves a creature of size 1 to 3 at (*x, *y) toward the party eye at (party_x, party_y), in thing grid cells. */
void view_offset_creature_toward_party(int16_t party_x, int16_t party_y, int16_t size, int16_t *x, int16_t *y);

/* The viewport shows the push arrows (the party pushes or can push a creature). */
extern int view_arrows_enabled;
/* True when a click at (x, y) of the viewport hits the creature on (square_x, square_y) in front of the party; *side:
   0 its middle, 1 its right, 3 its left. */
int view_hit_test_creature(int x, int y, int square_x, int square_y, int *side);

/* Clicks in the viewport (input.c). */
/* A click in the dungeon view: walls, doors, objects, creatures. */
void view_click_in_viewport(int x, int y);

/* The view position of the square at depth (0 to 4) and side (negative to the left), -1 when the view has none. */
int view_position(int depth, int side);
/* What the view draws of the square at a view position with an aspect (enum view_aspect): VIEW_DRAWS_* bits. */
enum {
    VIEW_DRAWS_FRONT_FACE = 1, VIEW_DRAWS_SIDE_FACE = 2,   /* the wall faces of a wall square */
    VIEW_DRAWS_CREATURES = 4, VIEW_DRAWS_OBJECTS = 8, VIEW_DRAWS_MISSILES = 0x10, VIEW_DRAWS_EXPLOSIONS = 0x20,
    VIEW_DRAWS_FLOOR_ORNAMENT = 0x40, VIEW_DRAWS_DOOR = 0x80,   /* the door picture with its button */
};
unsigned view_draws(int pos, int aspect);
/* True when the view draws the floor ornament (an ornament index) on a floor square at a view position. */
bool view_shows_floor_ornament(int pos, int ornament);
/* SENSOR_FLAGS of a wall shop sensor and its display sensor. */
#define SHOP_SHOWN SENSOR_BUSY          /* the display item is chosen */
#define SHOP_STEP_FORWARD SENSOR_STATE  /* the shop: the next click goes to the next item */

/* Falling rain shows over the view: its slant shows the wind. */
bool view_rain_shows(void);

#endif /* DM2_UI_VIEW_H */
