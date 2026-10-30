/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_VIEW_INTERNAL_H
#define DM2_UI_VIEW_INTERNAL_H
/* Prefixes: view_ g_view_ VIEW_ */

/* The shared private declarations of the dungeon view (view.c, view_things.c, view_walls.c): the value keys, view
   constants and picture types, the view state, the tables of more than one file, and the functions that the files call. */

#include <stdbool.h>
#include <stdint.h>
#include "ids.h"
#include "view.h"
#include "zone.h"

/* Value keys of two bytes: the first value in the high byte, the second in the low byte. */
#define VIEW_LOW_BYTE(v)  ((v) & BYTE_MASK)
#define VIEW_HIGH_BYTE(v) ((v) >> BYTE_BIT_COUNT)

/* Value keys (CAT_WALLSET, graphics set, 0x0b, key). */
#define MAP_KEY_WALL_COLOR   0x64 /* transparent color of the map pictures */
#define MAP_KEY_FLIP_FLAGS   0x65
#define MAP_KEY_WALL_MARGINS 0x6e /* + 1 beyond depth 1: left and right clip margins of a wall beside open squares */
#define MAP_KEY_NEAR_MARGINS 0x70 /* ceiling and floor clip margins when the squares of depth 1 are walls */
#define MAP_KEY_FAR_MARGINS  0x71 /* the same for the squares of depth 2 */
/* The entry step back from a missing flipped wall to the plain wall. */
#define MAP_WALL_FLIPPED_MISSING (GFX_MAP_WALL_FLIPPED - GFX_MAP_WALL)
/* Value keys of the ornament categories (category, ornament, 0x0b, key). */
#define ORN_KEY_COLOR            0x04 /* transparent color */
#define ORN_KEY_PLACE            0x05 /* anchor in the high byte, grid cell + 1 in the low byte */
#define ORN_KEY_NO_FLIP          0x07
#define ORN_KEY_ZONE_ROW         0x08 /* door ornaments and buttons: zone row */
#define ORN_KEY_RATIO_2          0x14 /* width : height of the picture at depth 2, as 2 bytes */
#define ORN_KEY_RATIO_3          0x15
#define ORN_KEY_OFFSCREEN_COLOR  0x11 /* transparent color of the picture in its offscreen bitmap */
#define ORN_KEY_DAMAGE_COLOR     0x12
#define ORN_KEY_BIG_NEAR         0x60 /* floor ornament: scale 0x60 on the squares beside the party */
#define ORN_KEY_NOT_FAR_SIDE     0x61 /* floor ornament: not drawn at view positions 14 and 15 */
#define ORN_KEY_WET              0x63 /* drawn wet by the rain */
/* Value key of a door type (CAT_DOOR, door type, 0x0b, key): the door has no frame. */
#define DOOR_KEY_NO_FRAME        0x40
/* Wall ornament pictures of a frame. */
#define ORN_PICTURE_SIDE         0
#define ORN_PICTURE_FRONT        1
#define ORN_PICTURE_RIGHT        2
/* No picture entry in a table, and the value entry of the portrait place of a portrait ornament. */
#define ENTRY_NONE               0xff
#define PORTRAIT_OFFSET_ENTRY    0xfd

/* Zones of the view elements, and cells of their 5 x 5 grid. */
#define ZONE_ROW_SIZE     4 /* zones of one ornament row */
#define CELL_FAR_MIDDLE   2 /* the middle cell of the far row */
#define CELL_LEFT_MIDDLE  10
#define CELL_RIGHT_MIDDLE 14
/* Zone anchors of the two sides of a door frame. */
#define ANCHOR_DOOR_RIGHT 4
#define ANCHOR_DOOR_LEFT  3

/* Doors: button frames, zone offsets, color keys, scales, and the ornament rows that take clicks. */
#define DOOR_BUTTON_FRAMES   5
#define DOOR_HALF_OFFSET     6 /* zone of the right door half, relative to the door zone */
#define DOOR_LEFT_OFFSET     3
#define DOOR_KEY_DEFAULT     10
#define ORNAMENT_KEY_DEFAULT 9
#define DOOR_NEAR_SCALE      113
#define DOOR_SIDE_SCALE      43 /* the door side picture in front of the party */
#define DOOR_SIDE_DEPTH      2
#define DOOR_ROW_NEAR        3 /* door ornament rows 3 and 4: the doors at depth 1 and 0 take clicks */
/* Door frame parts. */
#define DOOR_FRAME_TOP       0x01
#define DOOR_FRAME_LEFT      0x02
#define DOOR_FRAME_RIGHT     0x04
/* The square type bits of a square byte. */
#define SQUARE_TYPE_BITS     0xe0

/* Animation, light, scales, and text spacing of the view pictures. */
#define GAME_TIME_FRAMES   7 /* bits of the game time that animate pictures */
#define HALF_TIME_FRAMES   4
#define LIGHT_STEP         10 /* view shade per light level */
#define VIEW_SHADE_MAX     63
#define SHADE_FULL         64
#define SHADE_SHIFT        6   /* SHADE_FULL = 1 << SHADE_SHIFT */
#define SCALE_ROUND_SHIFT  7   /* a ratio in 1/128, rounded to 1/64 */
#define WALK_SCALE         52  /* the sky shrinks to 52/64 while the party walks */
#define SCALE_WIDE_SIDE    114 /* wall ornament scales at the far sides */
#define SCALE_FAR_SIDE     76
#define SCALE_BIG_NEAR     96 /* floor ornaments with ORN_KEY_BIG_NEAR beside the party */
#define WALL_TEXT_LINE_GAP 2  /* line gap of wall text */
#define NO_KEY             (-1)

/* Cell masks of the things drawing: bit n is cell n of the 5 x 5 grid of a square, row 0 the far row. */
#define THINGS_ALL         0x01ffffff
#define THINGS_NEAR        0x000003ff
#define THINGS_FAR         0x01fffc00
#define THINGS_STATIC_ONLY 0x40000000 /* only static creatures: the creatures behind a closed door */
#define THINGS_BEHIND_DOOR 0x40001000
#define THINGS_DOOR_CELL   0x00001000

/* View positions. */
#define POS_DEPTH_MAX         4
#define DEPTH_FRONT           1 /* the squares in front of the party */
#define DEPTH_MIDDLE          2
#define DEPTH_FAR             3  /* the last depth that shows things, doors, and ornament details */
#define POS_LINKED            21 /* positions that can hold a seamless passage */
#define POS_CEILING_PIT_LAST  8
#define POS_FLOOR_PIT_BLOCKED 11 /* from here on, an open pit hides its floor picture */
#define POS_FAR_ROW           16
#define POS_DRAWN             20
#define POS_NEAR_SIDE_1       1
#define POS_NEAR_SIDE_2       2
#define POS_FRONT_LEFT        4
#define POS_FRONT_RIGHT       5
#define POS_DEPTH2_CENTER     6
#define POS_DEPTH2_LEFT       7
#define POS_DEPTH2_RIGHT      8
#define POS_DEPTH2_FAR_LEFT   9
#define POS_DEPTH2_FAR_RIGHT  10
#define POS_DEPTH3_CENTER     11
#define POS_DEPTH3_LEFT       12
#define POS_DEPTH3_RIGHT      13
#define POS_FAR_SIDE_1        14
#define POS_FAR_SIDE_2        15
#define VIEW_POS_BIT(pos)     (1u << (pos))
/* The side columns two squares to the left or right of the view axis. */
#define LATERAL_FAR_SIDE      2
/* No neighbour square in linked_squares. */
#define LINK_NONE             0xff
/* The door types of a map: door slots 0 and 1. */
#define MAP_DOOR_TYPE_COUNT   2

/* The smallest click zone of a picture. */
#define CLICK_MIN_WIDTH             14
#define CLICK_MIN_HEIGHT            11
#define CLICK_LOW_HEIGHT            15 /* a lower picture moves its zone up to its middle */
#define CLICK_HALF_SIZE             7  /* a narrow or low zone starts this far before the middle of its picture */
/* The fixed click zones 4 to 7: the possessions of a static creature, by cell. */
#define CLICK_ZONE_POSSESSION_FIRST 4

/* Shops: a wall sensor (WALL_CLICK_RESET with a shop ornament) shows the item of the display sensor (SENSOR_SHOP)
   on its target square, with the item name and the merchant prices in coins. */
#define SHOP_CLASS_TEXT      0x10 /* + list * 3: the class set of the items of the list */
#define ORN_KEY_SHOP_LABEL   0xf2 /* (CAT_WALL_ORNAMENT, ornament, 0x0c, key): offsets of the name and prices */
#define ORN_KEY_SHOP_SELL    0xf1
#define ORN_KEY_SHOP_BUY     0xf0
/* The item flag and the price table bit of a shop. */
#define ITEM_FLAG_UNIQUE     0x1000 /* ATTR_FLAGS: the shop shows the item only when it exists */
#define SHOP_TABLE_CREATE    0x8000 /* table word: a new item is made when the display shows it */
/* The class text step of a price list, the coins of a price, and the coin picture size. */
#define SHOP_CLASS_TEXT_STEP 3
#define SHOP_PRICE_COINS     6
#define COIN_WIDTH           8
#define COIN_HEIGHT          7
#define DISPLAY_POS          POS_FRONT /* only the wall in front shows the name and prices */

/* The hit test of the creature in front of the party. */
#define HIT_SIDE_LIMIT 75 /* percent of the half picture width: a click beyond it hits a side */
#define HIT_DEPTH      1  /* the depth at which the creature in front is drawn for a hit test */

/* The hit test of a wall ornament. */
#define HIT_SMALL_SIZE 32 /* a smaller ornament takes every click on its wall */

/* The open states of a chest picture: closed or open as its record says, and the two states of a hit test on a closed
   lid and on an open chest. */
enum ChestState {
    CHEST_CLOSED = 0, /* an object look and a hit test: the chest as its record says */
    CHEST_OPEN = 1,
    CHEST_HIT_OPEN = 2,
    CHEST_HIT_INSIDE = 3
};

/* The items a chest holds. */
#define CHEST_CAPACITY          8
/* ATTR_SLOTS: no container takes the item. */
#define ATTR_SLOTS_NO_CONTAINER 0x8000

/* The wall faces that a wall square shows (wall_faces). */
#define WALL_SHOWS_FRONT   VIEW_DRAWS_FRONT_FACE
#define WALL_SHOWS_SIDE    VIEW_DRAWS_SIDE_FACE
#define WALL_SHOWS_BOTH    (WALL_SHOWS_FRONT | WALL_SHOWS_SIDE)
/* The neighbours of a view position in linked_squares: left, right, ahead, behind. */
#define NEIGHBOUR_LEFT     0
#define NEIGHBOUR_RIGHT    1
#define NEIGHBOUR_AHEAD    2
#define NEIGHBOUR_BEHIND   3
#define NEIGHBOUR_COUNT    4
/* The wall patterns of a view row: the walls of a row (center, left, right) give 8 patterns. */
#define ROW_PATTERNS       8
/* The wall bits of a row, and the far squares that the corner walls hide. */
#define ROW_WALLS          7 /* row wall bits: center, left, right */
#define ROW_SIDE_WALLS     6
#define HIDE_FAR_LEFT      (VIEW_POS_BIT(19) | VIEW_POS_BIT(21)) /* far squares hidden behind the corner walls */
#define HIDE_FAR_RIGHT     (VIEW_POS_BIT(20) | VIEW_POS_BIT(22))
/* The view positions of the door tables and of the door top frame tables. */
#define DOOR_POSITIONS     14
#define DOOR_TOP_POSITIONS 9
/* Door frame parts together. */
#define DOOR_FRAME_SIDES   (DOOR_FRAME_LEFT | DOOR_FRAME_RIGHT)
#define DOOR_FRAME_ALL     (DOOR_FRAME_TOP | DOOR_FRAME_SIDES)
/* Glyphs of the wall text font after the letters: space and period. */
#define GLYPH_SPACE        0x1a
#define GLYPH_PERIOD       0x1b
/* The wall text font has 8 x 8 glyphs. */
#define GLYPH_SIZE         8
/* The walking view: floor and ceiling shift, and the shift of the sky. */
#define WALK_FLOOR_DY      3
#define WALK_CEILING_DY    (-2)
#define WALK_SKY_DY        (-3)
#define WALK_SKY_HIGH_DY   2

/* The GDAT index of the object picture offsets of a category: (category, 0xfe, 0x0c, entry). */
#define CAT_ALCOVE_KEY               0xfe
/* Value keys of a creature type (CAT_CREATURE, type, 0x0b, key): transparent color, lift of its possessions. */
#define CREATURE_KEY_COLOR           0x04
#define CREATURE_KEY_POSSESSION_LIFT 0x41
/* Value key of a wall ornament (CAT_WALL_ORNAMENT, ornament, 0x0c, key): the offset of the items in an alcove. */
#define ORN_KEY_ALCOVE_ITEMS         0xfd
/* Value keys of an explosion type: random mirror mask, side flip of a missile, fixed power. */
#define EXPLOSION_KEY_FLIP           0x00
#define EXPLOSION_KEY_MISSILE_FLIP   0x01
#define EXPLOSION_KEY_FIXED_POWER    0x41
/* The chests that open, and the transparent color of thing pictures. */
#define CHEST_KINDS                  8  /* container GDAT indices below 8 are chests that open */
#define THING_KEY                    10 /* transparent color of thing pictures */
/* Animation bits of an active group: a random shift of the picture and a mirror. */
#define ANIM_MIRROR                  0x40
/* The shift of the vertical jitter in the animation bits. */
#define ANIM_JITTER_Y_SHIFT          3
/* Mirror bits of a creature frame: 2 bits per side, west side in bits 0-1. */
#define FRAME_MIRROR_MASK            0x03
#define FRAME_MIRROR_ALWAYS          0x01
#define FRAME_MIRROR_BY_ANIM         0x02 /* mirrored when the animation bits say so */
/* The shift of the mirror bits from one side to the next. */
#define FRAME_MIRROR_SHIFT           2
/* A moving creature shifts by its frame shift, sideways or forward. */
#define CREATURE_SHIFT_X             64
#define CREATURE_SHIFT_Y             7

/* Explosion types with their own drawing. */
#define EXPLOSION_GLOBAL_COUNTER   0x30 /* counts its views in global bytes 0x40 (away) and 0x41 (party square) */
#define EXPLOSION_ALCOVE_STILL     0x64 /* in an alcove: the still picture 0x0c */
#define EXPLOSION_ALCOVE_SCALED    0x65 /* in an alcove: the explosion picture */
/* The global byte variables that count the views of EXPLOSION_GLOBAL_COUNTER. */
#define GLOBAL_EXPLOSIONS_AWAY     0x40
#define GLOBAL_EXPLOSIONS_NEAR     0x41
/* Explosion pictures: size, growth, and the tint levels of an explosion on the party square. */
#define EXPLOSION_POWER_BIG        160 /* stronger explosions grow and rise */
#define EXPLOSION_POWER_SIZE       48  /* smallest power for the picture size */
#define EXPLOSION_SIZE_SHIFT       8   /* the picture size is the scale times (power + 1) / 256 */
#define EXPLOSION_GROW_BASE        192
#define EXPLOSION_GROW_NEAR        30
#define EXPLOSION_SPREAD_MIN       10
#define EXPLOSION_RISE_FACTOR      8 /* the rise and the spread: the viewport size * 8 / the growth */
#define EXPLOSION_SCALE_MIN        8
#define EXPLOSION_SCALE_MAX        64
#define EXPLOSION_TINT_LEVEL_SHIFT 5
#define EXPLOSION_TINT_LEVELS      3
#define TINT_ENTRY_WEAK            1 /* the tint picture after GFX_EXPLOSION_TINT of a weak and of a strong explosion */
#define TINT_ENTRY_STRONG          2

/* Missiles: the lift above the floor, the scales, and the base power of the half power spell. */
#define MISSILE_DY               (-92) /* missiles fly above the floor */
#define MISSILE_SCALE_MIN        8
#define MISSILE_HALF_POWER_BASE  128
/* The missile spell that shows half its power plus MISSILE_HALF_POWER_BASE. */
#define MISSILE_HALF_POWER_SPELL 0xff82

/* Thing grids: a cell of 4 x 4 per square, at (lateral + 10, forward) from the eye of the party. */
#define GRID_STRIDE            17
#define GRID_LATERAL_CENTER    10
#define GRID_LATERAL_LIMIT     10
#define GRID_FORWARD_LIMIT     17
#define PARTY_CELL_LAST        14 /* the party square draws the things of its 3 far rows */
#define PARTY_ROWS_BELOW_VIEW  2  /* the near rows of the party square are below the view */
#define CREATURE_COVERED_CELLS 7  /* a creature covers the missiles of the 7 cells drawn before its cell */
#define JITTER_COUNT           16
#define ALCOVE_FIRST_SPOT      2 /* the jitter spots of the items in an alcove */
#define ALCOVE_SPOT_END        14
#define CELL_COLUMN_LAST       (CELL_GRID_SIDE - 1)
#define CELL_ROW_STEP          (CELL_GRID_SIDE * GRID_SQUARE_CELLS) /* cell offset of the square ahead or behind */
/* No view position in a thing grid cell. */
#define NO_POSITION            0xff

/* Possessions of a static creature (CreatureInfo.possessions). */
#define POSSESSIONS_CELLS_MASK 0x00f0
/* The last possession kind of the creatures that carry their possessions low. */
#define POSSESSIONS_LOW_KIND   1

/* The view positions that show missiles and possessions, and explosions. */
#define MISSILE_POSITIONS   0x0000f9f9u /* positions 0, 3 to 8, 11 to 15: missiles and possessions */
#define EXPLOSION_POSITIONS 0x0007f9ffu /* positions 0 to 8, 11 to 18 */
#define ALCOVE_EXPLOSION_POSITIONS                                                                                     \
    (VIEW_POS_BIT(POS_FRONT) | VIEW_POS_BIT(POS_DEPTH2_CENTER) | VIEW_POS_BIT(POS_DEPTH2_LEFT) |                       \
     VIEW_POS_BIT(POS_DEPTH2_RIGHT) | VIEW_POS_BIT(POS_DEPTH3_CENTER) | VIEW_POS_BIT(POS_DEPTH3_LEFT) |                \
     VIEW_POS_BIT(POS_DEPTH3_RIGHT))
/* The rows of thing_cell_order: squares left of, on, and right of the view axis. */
#define ORDER_LEFT                 0
#define ORDER_AXIS                 1
#define ORDER_RIGHT                2
/* The sizes of thing_scale, missile_scale, and jitter_offset. */
#define THING_SCALE_COUNT          19
#define MISSILE_SCALE_COUNT        (2 * DEPTH_FAR + 1)
#define JITTER_OFFSET_COUNT        8
/* The first thing_scale index of objects and of explosions. */
#define OBJECT_SCALE_START         2
#define EXPLOSION_SCALE_START      3
/* The explosion cells in drawing order: the far left and right cells, the whole square, the near left and right
   cells. */
#define EXPLOSION_ORDER_COUNT      5
#define EXPLOSION_ORDER_LAST       (EXPLOSION_ORDER_COUNT - 1)
#define EXPLOSION_ORDER_FAR_RIGHT  1
#define EXPLOSION_ORDER_NEAR_LEFT  3
#define EXPLOSION_ORDER_NEAR_RIGHT 4
/* The cell of an explosion on the whole square. */
#define EXPLOSION_CELL_SQUARE      (-1)
/* The largest creature size of the steps toward the party. */
#define CREATURE_SIZE_MAX          3
/* The bits of the octant of a diagonal step toward the party. */
#define OCTANT_SAME_SHIFT          1
#define OCTANT_RIGHT_SHIFT         2
#define OCTANT_COUNT               8

/* The fields of a sparkle field row, and the step of the tile start in pixels. */
#define SPARKLE_TILE          0
#define SPARKLE_MASK_PICTURE  1
#define SPARKLE_WIDTH         2
#define SPARKLE_HEIGHT        3
#define SPARKLE_TILE_STEP     16
/* The mask picture byte of a sparkle field: none, the mirror bit, and the picture entry. */
#define SPARKLE_MASK_NONE     0xff
#define SPARKLE_MASK_MIRRORED 0x80
#define SPARKLE_MASK_ENTRY    0x7f

/* The sky pictures of the rain. */
#define RAIN_ALONG         0x71 /* sky pictures 0x71 to 0x74: rain seen along the wind, 0x6d to 0x70 across it */
#define RAIN_ACROSS        0x6d
/* The rain picture after RAIN_ALONG or RAIN_ACROSS by the rain intensity; 0 for a drizzle. */
#define RAIN_LEVEL_LIGHT   1
#define RAIN_LEVEL_MEDIUM  2
#define RAIN_LEVEL_STRONG  3
/* The tile rows of the rain and of a wet picture: the picture size minus a trim and a random spread. */
#define TILE_TRIM          40 /* rain and explosion tint: the tile row is the picture minus 40 pixels and a spread */
#define WET_TILE_TRIM      60
#define WET_TILE_EXTRA     16
/* The random bits of a tile start and of a tile spread. */
#define RANDOM_BYTE        0xff
#define RANDOM_TILE_SPREAD 0x1f

/* Sky objects: clouds, rain, and the landmarks of an outdoor map. */
#define SKY_LANDMARK_MAX      100
#define SKY_LANDMARK_DISTANCE 210 /* screen offset per square of lateral distance */
/* The mirror kinds of a sky object: by the floor or the ceiling parity. */
#define SKY_FLIP_FLOOR        0x08
#define SKY_FLIP_CEILING      0x02
#define SKY_FLIP_FLOOR_2      0x40
#define SKY_FLIP_CEILING_2    0x20
/* The move kinds of a sky object: fixed, or a landmark at its place on the map. */
#define SKY_FIXED             0
#define SKY_LANDMARK          1

/* Flip parity kinds and the map flip flags (value key MAP_KEY_FLIP_FLAGS). */
#define PARITY_SQUARE          0x00
#define PARITY_FLOOR           0x01
#define PARITY_CEILING         0x20
/* The map flip flags. */
#define FLIP_CEILING_PITS      0x01
#define FLIP_CEILING_BY_SQUARE 0x02
#define FLIP_CEILING_BY_TIME   0x04
#define FLIP_FLOOR_BY_SQUARE   0x08
#define FLIP_FLOOR_BY_TIME     0x10
#define FLIP_CEILING_BY_PARTY  0x20
#define FLIP_FLOOR_BY_PARTY    0x40

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
/* True when the thing is an item: a weapon up to a miscellaneous item. */
bool view_is_item(uint16_t thing);
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
