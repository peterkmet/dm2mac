/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_VIEW_INTERNAL_H
#define DM2_UI_VIEW_INTERNAL_H
/* Prefixes: view_ g_view_ VIEW_ */

/* The shared private declarations of the dungeon view (view.c, view_things.c, view_walls.c): the value keys, view
   constants and picture types, the view state, the tables of more than one file, and the functions that the files call. */

#include <stdbool.h>
#include <stdint.h>
#include "view.h"
#include "zone.h"

/* Value keys of two bytes: the first value in the high byte, the second in the low byte. */
#define VIEW_LOW_BYTE(v)  ((v) & BYTE_MASK)
#define VIEW_HIGH_BYTE(v) ((v) >> BYTE_BIT_COUNT)

/* Value keys of the ornament categories (category, ornament, 0x0b, key). */
#define ORN_KEY_NOT_FAR_SIDE 0x61 /* floor ornament: not drawn at view positions 14 and 15 */
/* Value key of a door type (CAT_DOOR, door type, 0x0b, key): the door has no frame. */
#define DOOR_KEY_NO_FRAME    0x40

/* Animation, light, scales, and text spacing of the view pictures. */
#define SHADE_FULL 64
#define NO_KEY     (-1)

/* Cell masks of the things drawing: bit n is cell n of the 5 x 5 grid of a square, row 0 the far row. */
#define THINGS_ALL  0x01ffffff
#define THINGS_NEAR 0x000003ff
#define THINGS_FAR  0x01fffc00

/* View positions. */
#define POS_DEPTH_MAX        4
#define DEPTH_MIDDLE         2
#define DEPTH_FAR            3 /* the last depth that shows things, doors, and ornament details */
#define POS_FAR_ROW          16
#define POS_NEAR_SIDE_1      1
#define POS_NEAR_SIDE_2      2
#define POS_FRONT_LEFT       4
#define POS_FRONT_RIGHT      5
#define POS_DEPTH2_CENTER    6
#define POS_DEPTH2_LEFT      7
#define POS_DEPTH2_RIGHT     8
#define POS_DEPTH2_FAR_LEFT  9
#define POS_DEPTH2_FAR_RIGHT 10
#define POS_DEPTH3_CENTER    11
#define POS_DEPTH3_LEFT      12
#define POS_DEPTH3_RIGHT     13
#define POS_FAR_SIDE_1       14
#define POS_FAR_SIDE_2       15
#define VIEW_POS_BIT(pos)    (1u << (pos))
/* No neighbour square in linked_squares. */
#define LINK_NONE            0xff
/* The door types of a map: door slots 0 and 1. */
#define MAP_DOOR_TYPE_COUNT  2

/* The open states of a chest picture: closed or open as its record says, and the two states of a hit test on a closed
   lid and on an open chest. */
enum ChestState {
    CHEST_CLOSED = 0, /* an object look and a hit test: the chest as its record says */
    CHEST_OPEN = 1,
    CHEST_HIT_OPEN = 2,
    CHEST_HIT_INSIDE = 3
};

/* The wall faces that a wall square shows (wall_faces). */
#define WALL_SHOWS_FRONT VIEW_DRAWS_FRONT_FACE
#define WALL_SHOWS_SIDE  VIEW_DRAWS_SIDE_FACE
#define WALL_SHOWS_BOTH  (WALL_SHOWS_FRONT | WALL_SHOWS_SIDE)
/* The neighbours of a view position in linked_squares: left, right, ahead, behind. */
#define NEIGHBOUR_AHEAD  2
#define NEIGHBOUR_COUNT  4

/* Value key of a wall ornament (CAT_WALL_ORNAMENT, ornament, 0x0c, key): the offset of the items in an alcove. */
#define ORN_KEY_ALCOVE_ITEMS 0xfd
/* The chests that open, and the transparent color of thing pictures. */
#define THING_KEY            10 /* transparent color of thing pictures */

/* Thing grids: a cell of 4 x 4 per square, at (lateral + 10, forward) from the eye of the party. */
#define GRID_STRIDE       17
#define ALCOVE_FIRST_SPOT 2 /* the jitter spots of the items in an alcove */
/* No view position in a thing grid cell. */
#define NO_POSITION       0xff

/* The view positions that show missiles and possessions, and explosions. */
#define MISSILE_POSITIONS   0x0000f9f9u /* positions 0, 3 to 8, 11 to 15: missiles and possessions */
#define EXPLOSION_POSITIONS 0x0007f9ffu /* positions 0 to 8, 11 to 18 */
/* The largest creature size of the steps toward the party. */
#define CREATURE_SIZE_MAX   3
/* The bits of the octant of a diagonal step toward the party. */
#define OCTANT_COUNT        8

/* The tile rows of the rain and of a wet picture: the picture size minus a trim and a random spread. */
#define TILE_TRIM          40 /* rain and explosion tint: the tile row is the picture minus 40 pixels and a spread */
/* The random bits of a tile start and of a tile spread. */
#define RANDOM_BYTE        0xff
#define RANDOM_TILE_SPREAD 0x1f

/* Sky objects: clouds, rain, and the landmarks of an outdoor map. */
#define SKY_LANDMARK_MAX 100

/* Flip parity kinds and the map flip flags (value key MAP_KEY_FLIP_FLAGS). */
#define PARITY_FLOOR   0x01
#define PARITY_CEILING 0x20

/* The projectiles of one view whose flight the views between game steps show. */
#define FLIGHT_TRACK_MAX 32

/* Where and how a view picture shows: its mirror, scale, shift, depth shade, zone and anchor, the transparent
   colors of its color remap, and its draw mode. */
typedef struct {
    int flip, scale_x, scale_y, dx, dy, depth, zone, anchor, key, key2;
    enum DrawMode mode;
} PicturePlace;

/* How an object shows: lifted by dy, at a jitter spot that keeps the objects of a cell apart (-1 none), as a
   possession of the static creature group, with its click zone, as a chest in an open state, or in the alcove. */
typedef struct {
    int dy, spot;
    uint16_t group;
    bool record_click;
    enum ChestState open_state;
    bool in_alcove;
    uint16_t projectile; /* the projectile that carries the object in flight, THING_NONE for none */
} ObjectLook;

/* The eye of the party in the thing grid: the direction it looks to and its grid point. */
typedef struct {
    int16_t dir;
    GridPoint point;
} ViewEye;

/* A door picture to put together: the door type, its ornament (an ordinal of the map door ornaments, 0 none), its
   damage, the picture entry and scale, the depth of its shade and of its ornaments, the zone row of the depth, and
   its transparent color. */
typedef struct {
    uint8_t type;
    int ornament;
    bool destroyed;
    int entry, scale, shade_depth, depth, row, key;
} DoorPicture;

/* The picture of a creature from one side: its entry, its mirror, and whether it is the picture of the other side. */
typedef struct {
    int entry, flip;
    bool mirrored;
} SidePicture;

/* An offset on the map as the party sees it: to the right (negative to the left) and forward. */
typedef struct {
    int16_t lateral, forward;
} RelativeOffset;

/* A grid cell (0 to 24) of the square at a view position. */
typedef struct {
    int pos, cell;
} SquareCell;

/* The picture of a thing: its entry and its mirror (MIRROR_*). */
typedef struct {
    int entry, flip;
} ThingPicture;

/* A projectile of a view (its thing id without the cell), its view square, and the picture corner in the views of
   the game step before and of the last game step. */
typedef struct {
    uint16_t thing;
    int pos;
    ViewPoint from, to;
} FlightTrack;

/* Right and forward square offset of each view position. */

typedef struct {
    int16_t map; /* the map of the squares that the view reads now */
    int16_t party_map;
    int16_t party_dir, party_x, party_y; /* the position the view is drawn for */
    bool walking;                        /* the party walks: g_party.move_steps is not 0 */
    int shade;
    bool flip; /* mirror the far pictures on odd squares */
    uint8_t gfx_set;
    uint8_t door_types[MAP_DOOR_TYPE_COUNT];
    int16_t wall_key;
    uint16_t flip_flags;
    bool clear; /* fill clear_rect before drawing: floor and ceiling leave a gap */
    int16_t clear_rect[RECT_SIZE];
    uint8_t clear_color;
    int landmark_set; /* graphics set of the landmark bits, -1: none yet */
    bool no_landmarks;
    uint8_t landmarks[(SKY_LANDMARK_MAX + BYTE_BIT_COUNT - 1) / BYTE_BIT_COUNT];
    /* the picture of view_setup_picture; the callers read back its offset (dx, dy) after a DRAW_NO_IMAGE setup, and its
       size (w, h) after view_render_offscreen */
    DrawCtx ctx;
    DrawCtx alcove;  /* the picture of the last alcove: its items are placed relative to it */
    bool rain_shown; /* the map graphics show rain */
    /* the colors of view_get_shaded_palette; fix: the original has 16 bytes, which the full table overruns */
    uint8_t shaded_palette[REMAP_COLORS];
    struct {
        uint16_t first; /* possessions of a creature whose other cells are drawn later, THING_ENDOFLIST none */
        uint16_t group;
        uint32_t cells;
    } pending;
    /* Smooth projectile flight of the advanced mode (no rule change): the projectiles of the last view of a game step
       and of the view of the game step before, and the timer tick of a view between game steps. */
    struct {
        FlightTrack tracks[FLIGHT_TRACK_MAX], last[FLIGHT_TRACK_MAX];
        int count, last_count;
        int16_t map, x, y, dir; /* the party view of the tracks */
        bool walking;
        uint32_t time;        /* the game time of the tracks */
        int tick, step_ticks; /* the timer tick of a view between game steps; 0 step_ticks: a view of a game step */
    } flight;
    uint8_t viewport_pixels[VIEWPORT_WIDTH * VIEWPORT_HEIGHT]; /* the pixels of g_view_viewport */
} ViewState;
extern ViewState g_view_state;

/* Shared tables. */
extern const uint8_t g_view_square_grid_corner[POS_FAR_ROW][2];
extern const uint8_t g_view_eye_cell[DIR_COUNT];
extern const uint8_t g_view_cell_view_position[DIR_COUNT];
extern const int8_t g_view_toward_party_step[CREATURE_SIZE_MAX];
extern const int8_t g_view_toward_party_diagonal[CREATURE_SIZE_MAX][OCTANT_COUNT][2];
extern const uint8_t g_view_depth_scale[POS_DEPTH_MAX + 1];
extern const uint8_t g_view_wall_faces[VIEW_SQUARE_COUNT];
extern const uint8_t g_view_door_front_shown[POS_FAR_ROW];
extern const uint8_t g_view_linked_squares[VIEW_SQUARE_COUNT][NEIGHBOUR_COUNT];
extern const uint8_t g_view_square_things_shown[POS_FAR_ROW];

/* view.c */
/* The square offset of a view position to the right of the view axis (negative to the left). */
int view_lateral_of(int pos);
/* The depth of a view position: its square offset forward. */
int view_depth_of(int pos);
/* The picture entry offset of the frame of a wall face word: 4 pictures per frame. */
int view_face_frame_entry(uint16_t w);
/* True when the picture entry exists in the graphics data of the category and index. */
bool view_picture_exists(int category, int index, int entry);
/* The door type of a door thing on the current map, 0xff for none. */
uint8_t view_map_door_type(uint16_t door);
/* True for a view between game steps (advanced mode): such a view changes no game state. */
bool view_between_steps(void);
/* The square forward squares ahead of at and right squares to the right, for the party looking to dir. */
Location view_step_relative(Location at, int16_t dir, int forward, int right);
/* The offset (dx, dy) on the map as the party looking to dir sees it. */
RelativeOffset view_relative_offset(int16_t dir, int dx, int dy);
/* The grid cell of a thing in its square for the party looking to dir. */
int view_get_thing_position(uint16_t thing, int16_t dir);
/* The cell (0 to 3) of a grid cell that holds objects, -1 for the others. */
int view_position_to_cell(int position);
/* True when the picture of kind should be mirrored for the party on the square at, looking to dir. */
bool view_get_flip_parity(int kind, int16_t dir, Location at);
/* True when the creature type has a picture at the entry. */
bool view_has_creature_picture(int type, int entry);
/* The zone of a view element plus offset: from the side ornament table, or from the place of the element in the grid. */
int view_element_zone(int element, int offset, bool use_table);
/* The scale (1/64) of a to b, rounded. */
int view_rounded_scale(int a, int b);
/* The color remap of a picture at depth: the depth shading table of the map, dimmed by the view shade. */
void view_get_depth_shading(uint8_t *palette, int depth, int key1, int key2, int16_t *count);
/* Starts the picture of the view context: the image, its scale, place, and shading. */
void view_setup_picture(const PicturePlace *p, int category, int index, int entry);
/* Draws the picture that view_setup_picture prepared. */
void view_flush_picture(void);
/* Draws the picture of view_setup_picture, or with draw false only finds its rectangle (for a hit test). */
void view_flush_picture_or_measure(bool draw);
/* Adds a click zone of kind at rect for the square at view position pos. */
void view_add_click_zone(const int16_t *rect, enum ClickKind kind, int pos);
/* Sets click zone index to the rectangle of the last picture (at least 14 x 11 pixels), or widens it. */
void view_set_click_zone(int index, uint16_t thing, enum ClickKind kind, int pos);
/* The colors of a 4 bpp picture dimmed by the view shade, with key kept; NULL for an 8 bpp picture in full dark. The
   colors stay valid until the next call. */
const uint8_t *view_get_shaded_palette(int category, int index, int entry, int key);
/* Draws a picture of the map graphics into a zone of the view, dimmed by the view shade. */
void view_show_map_picture(int entry, int zone, int key, int flip);
/* Prepares the picture of view_setup_picture with the mirror, scale, depth and key of p into an offscreen bitmap of its
   size, for pictures that are put together before they go into the view; p->key2 is the transparent color of the
   offscreen picture. Returns the bitmap, and the context in ctx. */
Bitmap *view_render_offscreen(DrawCtx *ctx, const PicturePlace *p, int category, int index, int entry);
/* The transparent color of a picture after its color remap. */
int view_mapped_key(const DrawCtx *c, int key);
/* Draws a finished offscreen picture into a zone of the view with the transparent color key. */
void view_show_offscreen(DrawCtx *ctx, Bitmap *offscreen, int zone, int anchor, int key);
/* view_things.c */
/* Draws the sparkles of a teleporter or of a sparkle explosion over the square at pos: the sparkle picture of
   (category, index), tiled from a random place, through the mask picture of the square. */
void view_show_sparkle_field(int pos, int category, int index);
/* The jitter spot of the next object in a cell, or in an alcove. */
int view_next_spot(int spot, bool in_alcove);
/* Draws an object at its cell of the square at pos, as look tells. */
void view_show_object(uint16_t thing, int pos, const ObjectLook *look);
/* Draws a creature group of the square at pos at depth: its animation frame, from the side it shows. */
void view_show_creature(int pos, int depth, uint16_t group);
/* Draws the creature groups of a thing list at the square at pos; true when it drew one. */
bool view_show_creatures(uint16_t thing, int pos, uint32_t cell_mask);
/* Draws the possessions that show_possessions left for later, at the square at pos. */
void view_show_pending_possessions(int pos);
/* Draws the explosions of a thing list at the square at pos, cell by cell from the far cells. */
void view_show_explosions(uint16_t first, int pos, uint32_t cell_mask);
/* Draws the things of the square at pos in the cells of cell_mask: objects, creatures, missiles, and with
   with_explosions the explosions. */
void view_show_square_things(int pos, uint32_t cell_mask, bool with_explosions);
/* Draws the items in an alcove of the front wall at pos, and the explosions in it. */
void view_show_alcove_contents(int pos);
/* view_walls.c */
/* Draws the ornament of the wall face at side (0 front, < 0 left, > 0 right) of the wall at pos; returns its color key,
   NO_KEY for no ornament. */
int view_show_wall_ornament(int pos, int side, bool record_click);
/* Draws the wall square at pos: the wall block and the ornaments of the faces that the party sees. */
void view_show_wall_square(int pos);
/* Draws the floor ornament of the square at pos. A seamless passage there shows the squares behind it.
   True for a passage that was drawn. */
bool view_show_floor_ornament(int pos);
/* Draws the hole of an open pit on the level above. */
void view_show_ceiling_pit(int pos);
/* Draws the open pit on the floor of the square at pos. */
void view_show_floor_pit(int pos);
/* Draws the stairs at pos as the party sees them from the side. */
void view_show_stairs_side(int pos);
/* Draws the stairs at pos as the party sees them from the front. */
void view_show_stairs_front(int pos);
/* Draws the door square at pos seen from the front: the things behind the door, the door, and the things before it. */
void view_show_door_front(int pos);
/* Draws the sky of an outdoor map: the weather objects, then the landmarks that the party sees. */
void view_outdoor_draw_sky(int16_t dir, int16_t x, int16_t y);

#endif
