/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_VIEW_H
#define DM2_UI_VIEW_H
/* Prefixes: view_ */

/* The dungeon view: the squares in sight and their drawing. */

#include "gfx.h"
#include "aspect.h"
#include "draw.h"
#include <stdbool.h>
#include <stdint.h>

/* The dungeon view shows 23 squares: 3 at depth 0 and 1 (the party square, left, right), 5 at depth 2 and 3,
   7 at depth 4, ordered center, left, right, then further out. */
#define VIEW_SQUARE_COUNT 23

/* View positions of the party square and of the square in front of it. */
#define POS_PARTY 0
#define POS_FRONT 3

/* Picture kinds of a missile, by the pictures of its payload. */
#define MISSILE_NO_PICTURE (-1)
#define MISSILE_MIRRORED   0 /* front pictures 8 and 9, mirrored by the cell */
#define MISSILE_FRONT_BACK 1 /* front picture 8, back picture 10 */
#define MISSILE_FRONT      2 /* front picture 8 only */
#define MISSILE_NO_SIDE    3 /* no side picture 12 */

/* Thing grids of 21 x 17 cells (4 x 4 per square) around the party, at (lateral + 10) * 17 + forward. */
#define VIEW_GRID_SIZE 358

/* Kinds of a click zone of the view. */
#define CLICK_NONE          0
#define CLICK_OBJECT        1
#define CLICK_POSSESSIONS   2
#define CLICK_ALCOVE        3
#define CLICK_DOOR_BUTTON   4
#define CLICK_WALL_ORNAMENT 6

/* Click zones of the view: zones 0 to 7 are the cells of the party square and of the square in front, 8 an alcove. */
#define VIEW_CLICK_ZONE_MAX    32
#define VIEW_CLICK_ZONE_FIXED  9
#define VIEW_CLICK_ZONE_ALCOVE (VIEW_CLICK_ZONE_FIXED - 1) /* the click zone of the alcove ahead */

/* What the view draws of the square at a view position with an aspect: the bits of view_draws. */
#define VIEW_DRAWS_FRONT_FACE     0x01
#define VIEW_DRAWS_SIDE_FACE      0x02 /* the wall faces of a wall square */
#define VIEW_DRAWS_CREATURES      0x04
#define VIEW_DRAWS_OBJECTS        0x08
#define VIEW_DRAWS_MISSILES       0x10
#define VIEW_DRAWS_EXPLOSIONS     0x20
#define VIEW_DRAWS_FLOOR_ORNAMENT 0x40
#define VIEW_DRAWS_DOOR           0x80 /* the door picture with its button */

/* SENSOR_FLAGS of a wall shop sensor and its display sensor. */
#define SHOP_SHOWN        SENSOR_BUSY  /* the display item is chosen */
#define SHOP_STEP_FORWARD SENSOR_STATE /* the shop: the next click goes to the next item */

/* A click zone of the view: where an object, an ornament, a door button, or creature possessions show. */
typedef struct {
    int16_t rect[RECT_SIZE];
    uint16_t thing; /* THING_NONE: the zone is not set */
    uint8_t cell;   /* view position or cell, by kind */
    uint8_t kind;   /* CLICK_* */
} ViewClickZone;

/* ---- Map graphics ---- */

/* Sets up the map graphics of map for the view: the view part of entering a map. */
void view_enter_map(int map);

/* ---- Things of the view squares ---- */

extern ViewSquare g_view_squares[VIEW_SQUARE_COUNT]; /* exported for the tests */
/* The view position of the square that holds each grid cell. */
extern uint8_t g_view_creature_grid[VIEW_GRID_SIZE]; /* exported for the tests */
/* The view position of the square of the creature that stands at each grid cell, 0xff for none. */
extern uint8_t g_view_object_grid[VIEW_GRID_SIZE]; /* exported for the tests */
/* By view position: bit n set when grid cell n of the square holds a missile, or an object. */
extern uint32_t g_view_missile_masks[VIEW_SQUARE_COUNT]; /* exported for the tests */
extern uint32_t g_view_object_masks[VIEW_SQUARE_COUNT];  /* exported for the tests */
/* Moves a creature of size 1 to 3 at (*x, *y) toward the party eye at (party_x, party_y), in thing grid cells;
   exported for the tests. */
void view_offset_toward_party(int16_t party_x, int16_t party_y, int16_t size, int16_t *x, int16_t *y);
/* Loads the view square at view position pos for the party at (x, y) looking to dir. */
void view_load_square(int x, int y, int pos, int dir); /* exported for the tests */
/* Shows the squares behind a seamless passage that view_mark_hidden_squares hid. */
void view_unhide_linked_squares(void); /* exported for the tests */
/* Hides the wall squares that walls in front of them cover. */
void view_mark_hidden_squares(void); /* exported for the tests */

/* ---- Rain ---- */

/* Falling rain shows over the view: its slant shows the wind. */
bool view_rain_shows(void);
/* Fills the offscreen bitmap of the wet picture ctx: with rain, a random part of the rain picture, else key. */
void view_draw_wet_image(const DrawCtx *ctx, Bitmap *offscreen, int key); /* exported for the tests */

/* ---- The view ---- */

/* The view picture, 224 x 136 pixels. */
extern Bitmap g_view_viewport;
/* The viewport shows the push arrows (the party pushes or can push a creature). */
extern int g_view_arrows_enabled;
/* Fills the cleared band of the viewport (a gap that the floor and ceiling leave). */
void view_clear_viewport(void);
/* The view position of the square at depth (0 to 4) and side (negative to the left), -1 when the view has none. */
int view_position(int depth, int side);
/* What the view draws of the square at a view position with an aspect (ASPECT_*): VIEW_DRAWS_* bits. */
unsigned view_draws(int pos, int aspect);
/* True when the view draws the floor ornament (an ornament index) on a floor square at a view position. */
bool view_shows_floor_ornament(int pos, int ornament);
/* True when the last view picture shows the party position and direction. */
int view_shows_party(void);
/* Draws the view of the party at (x, y) of the party map looking to dir into g_view_viewport. */
void view_draw_dungeon(int dir, int x, int y);

/* ---- Clicks ---- */

extern ViewClickZone g_view_click_zones[VIEW_CLICK_ZONE_MAX];
extern int g_view_click_zone_count;
/* The vertical place of the last hit test of a view picture, in percent of half the picture height from its center:
   a shop click above the center shows the next item, else the last item. */
extern int16_t g_view_hit_offset_y; /* exported for the tests */
/* True when a click at (x, y) of the viewport hits the creature on (square_x, square_y) in front of the party; *side:
   0 its middle, 1 its right, 3 its left. */
int view_hit_test_creature(int x, int y, int square_x, int square_y, int *side);
/* True when the ornament of the wall at view position pos (0 to 3) shows a pixel at (x, y) of the viewport. */
int view_hit_test_wall_ornament(int pos, int x, int y);
/* The object of a click at (x, y) of the viewport among the objects in the cell of thing (the cell bits) on the
   square (square_x, square_y) at view position pos, by click zone kind; a chest opens or closes. placing: thing is the
   leader hand object that goes there. Returns the object, THING_NONE when the click is done (a chest changed, or
   thing went into an open chest), THING_ENDOFLIST when thing must drop; thing when nothing is hit. */
uint16_t view_hit_test_object(int x, int y, int pos, int square_x, int square_y, uint16_t thing, int kind, int placing);
/* A click in the dungeon view: walls, doors, objects, creatures (input.c). */
void view_click_in_viewport(int x, int y);

#endif /* DM2_UI_VIEW_H */
