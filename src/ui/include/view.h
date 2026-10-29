/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_VIEW_H
#define DM2_UI_VIEW_H
/* Prefixes: view_ */

/* The dungeon view: the squares in sight and their drawing. */

#include <stdbool.h>
#include <stdint.h>
#include "aspect.h"
#include "draw.h"
#include "dungeon.h"
#include "gfx.h"

/* The dungeon view shows 23 squares: 3 at depth 0 and 1 (the party square, left, right), 5 at depth 2 and 3,
   7 at depth 4, ordered center, left, right, then further out. */
#define VIEW_SQUARE_COUNT 23

/* View positions of the party square and of the square in front of it. */
#define POS_PARTY 0
#define POS_FRONT 3

/* Picture kinds of a missile, by the pictures of its payload. */
enum MissileKind {
    MISSILE_NO_PICTURE = -1,
    MISSILE_MIRRORED = 0,   /* front pictures 8 and 9, mirrored by the cell */
    MISSILE_FRONT_BACK = 1, /* front picture 8, back picture 10 */
    MISSILE_FRONT = 2,      /* front picture 8 only */
    MISSILE_NO_SIDE = 3     /* no side picture 12 */
};

/* Thing grids of 21 x 17 cells (4 x 4 per square) around the party, at (lateral + 10) * 17 + forward; 358 bytes, one
   more than 21 x 17, as the original allocates (0x166). */
#define VIEW_GRID_SIZE 358

/* Kinds of a click zone of the view. */
enum ClickKind {
    CLICK_NONE = 0,
    CLICK_OBJECT = 1,
    CLICK_POSSESSIONS = 2,
    CLICK_ALCOVE = 3,
    CLICK_DOOR_BUTTON = 4,
    CLICK_WALL_ORNAMENT = 6
};

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

/* A click zone of the view: where an object, an ornament, a door button, or creature possessions show. */
typedef struct {
    int16_t rect[RECT_SIZE];
    uint16_t thing; /* THING_NONE: the zone is not set */
    uint8_t pos;    /* the view position of the square */
    uint8_t kind;   /* enum ClickKind */
} ViewClickZone;

/* A click on the objects of a square cell: the click at (x, y) of the viewport, the view position of the square, the
   square at, and the click zone kind. */
typedef struct {
    int16_t x, y;
    int pos;
    Location at;
    enum ClickKind kind;
} ViewObjectClick;

/* A pixel point of the viewport. */
typedef struct {
    int16_t x, y;
} ViewPoint;

/* ---- Map graphics ---- */

/* Sets up the map graphics of map for the view: the view part of entering a map. */
void view_enter_map(int16_t map);

/* ---- Things of the view squares ---- */

extern ViewSquare g_view_squares[VIEW_SQUARE_COUNT]; /* exported for the tests */
/* The view position of the square that holds each grid cell. */
extern uint8_t g_view_creature_grid[VIEW_GRID_SIZE]; /* exported for the tests */
/* The view position of the square of the creature that stands at each grid cell, 0xff for none. */
extern uint8_t g_view_object_grid[VIEW_GRID_SIZE]; /* exported for the tests */
/* By view position: bit n set when grid cell n of the square holds a missile, or an object. */
extern uint32_t g_view_missile_masks[VIEW_SQUARE_COUNT]; /* exported for the tests */
extern uint32_t g_view_object_masks[VIEW_SQUARE_COUNT];  /* exported for the tests */
/* The place of a creature of size 1 to 3 at creature moved toward the party eye at eye; exported for the tests. */
GridPoint view_offset_toward_party(GridPoint eye, GridPoint creature, int16_t creature_size);
/* Loads the view square at view position pos for the party on the square at, looking to dir. */
void view_load_square(Location at, int pos, int16_t dir); /* exported for the tests */
/* Shows the squares behind a seamless passage that view_mark_hidden_squares hid. */
void view_unhide_linked_squares(void); /* exported for the tests */
/* Hides the wall squares that walls in front of them cover. */
void view_mark_hidden_squares(void); /* exported for the tests */

/* ---- Rain ---- */

/* Falling rain shows over the view: its slant shows the wind. */
bool view_shows_rain(void);
/* Fills the offscreen bitmap of the wet picture ctx: with rain, a random part of the rain picture, else color_key. */
void view_draw_wet_image(const DrawCtx *ctx, Bitmap *offscreen, int color_key); /* exported for the tests */

/* ---- Things ---- */

/* The picture kind of a flying object (a missile) by the pictures of its payload in its GDAT entry. */
enum MissileKind view_missile_kind(uint16_t thing);

/* ---- The view ---- */

/* The view picture, 224 x 136 pixels. */
extern Bitmap g_view_viewport;
/* The viewport shows the push arrows (the party pushes or can push a creature). */
extern bool g_view_arrows_enabled;
/* Fills the cleared band of the viewport (a gap that the floor and ceiling leave). */
void view_clear_viewport(void);
/* The view position of the square at depth (0 to 4) and side (negative to the left), -1 when the view has none. */
int view_position(int depth, int side);
/* What the view draws of the square at a view position with an aspect (ASPECT_*): VIEW_DRAWS_* bits. */
unsigned view_draws(int pos, int aspect);
/* True when the view draws the floor ornament (an ornament index) on a floor square at a view position. */
bool view_shows_floor_ornament(int pos, int ornament);
/* True when the last view picture shows the party position and direction. */
bool view_shows_party(void);
/* Draws into g_view_viewport the view of the party on the square at of the party map, looking to dir. */
void view_draw_dungeon(Location at, int16_t dir);

/* ---- Projectile flight ---- */

/* The shift of a projectile picture whose corner moved from old_corner to new_corner in the last game step, at timer
   tick tick of a game step of step_ticks ticks: the part of the way back to old_corner that is still left (all of it
   at tick 0); exported for the tests. */
ViewPoint view_flight_shift(ViewPoint old_corner, ViewPoint new_corner, int tick, int step_ticks);
/* The next views draw each projectile at timer tick tick of a game step of step_ticks ticks, part of the way from its
   place of the game step before; 0 step_ticks: the views of a game step, at the places of the projectiles. */
void view_set_flight_tick(int tick, int step_ticks);
/* True when a projectile of the last view moved since the view of the game step before. */
bool view_has_moving_projectiles(void);

/* ---- Clicks ---- */

extern ViewClickZone g_view_click_zones[VIEW_CLICK_ZONE_MAX];
extern int g_view_click_zone_count;
/* The vertical place of the last hit test of a view picture, in percent of half the picture height from its center:
   a shop click above the center shows the next item, else the last item. */
extern int16_t g_view_hit_offset_y; /* exported for the tests */
/* True when a click at (x, y) of the viewport hits the creature on the square at in front of the party; *out_side:
   0 its middle, TURN_RIGHT its right, TURN_LEFT its left. */
bool view_hit_test_creature(int16_t x, int16_t y, Location at, int *out_side);
/* True when the ornament of the wall at view position pos (0 to 3) shows a pixel at (x, y) of the viewport. */
bool view_hit_test_wall_ornament(int pos, int16_t x, int16_t y);
/* The outcome of a click on the objects of a square cell. */
enum ObjectClick {
    OBJECT_CLICK_HIT,  /* thing: the object that the click hits, or the click thing when it hits no object */
    OBJECT_CLICK_DONE, /* a chest opened or closed, took the click thing, or gave its last item to the leader; or the
                          cell has no object and the click places nothing */
    OBJECT_CLICK_DROP  /* the click thing must drop into the cell */
};
typedef struct {
    enum ObjectClick kind;
    uint16_t thing; /* OBJECT_CLICK_HIT only, else THING_NONE */
} ObjectClickResult;
/* The outcome of a click among the objects in the cell of thing (the cell bits); a chest opens or closes. placing:
   thing is the leader hand object that goes there. */
ObjectClickResult view_hit_test_object(const ViewObjectClick *click, uint16_t thing, bool placing);

#endif /* DM2_UI_VIEW_H */
