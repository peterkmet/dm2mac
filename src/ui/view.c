/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "view.h"
#include "ids.h"
#include "aspect.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "inventory.h"
#include "item.h"
#include "merchant.h"
#include "money.h"
#include "move.h"
#include "projectile.h"
#include "random.h"
#include "save.h"
#include "sensor.h"
#include "sound_request.h"
#include "timeline.h"
#include "weather.h"
#include "advanced.h"
#include "draw.h"
#include "input.h"
#include "text.h"
#include "ui.h"
#include "zone.h"
#include <stdlib.h>
#include <string.h>

/* Value keys of two bytes: the first value in the high byte, the second in the low byte. */
#define LOW_BYTE(v)  ((v) & BYTE_MASK)
#define HIGH_BYTE(v) ((v) >> BYTE_BITS)

/* Value keys (CAT_WALLSET, graphics set, 0x0b, key). */
enum {
    MAP_KEY_WALL_COLOR = 0x64, /* transparent color of the map pictures */
    MAP_KEY_FLIP_FLAGS = 0x65,
    MAP_KEY_WALL_MARGINS = 0x6e, /* + 1 beyond depth 1: left and right clip margins of a wall beside open squares */
    MAP_KEY_NEAR_MARGINS = 0x70, /* ceiling and floor clip margins when the squares of depth 1 are walls */
    MAP_KEY_FAR_MARGINS = 0x71,  /* the same for the squares of depth 2 */
};
#define MAP_WALL_FLIPPED_MISSING                                                                                       \
    (GFX_MAP_WALL_FLIPPED - GFX_MAP_WALL) /* back from a missing flipped wall to the plain wall */
/* Value keys of the ornament categories (category, ornament, 0x0b, key). */
enum {
    ORN_KEY_COLOR = 4, /* transparent color */
    ORN_KEY_PLACE = 5, /* anchor in the high byte, grid cell + 1 in the low byte */
    ORN_KEY_NO_FLIP = 7,
    ORN_KEY_ZONE_ROW = 8,         /* door ornaments and buttons: zone row */
    ORN_KEY_WIDTH_SCALE_2 = 0x14, /* width : height of the picture at depth 2, as 2 bytes */
    ORN_KEY_WIDTH_SCALE_3 = 0x15,
    ORN_KEY_OFFSCREEN_COLOR = 0x11, /* transparent color of the picture in its offscreen bitmap */
    ORN_KEY_DAMAGE_COLOR = 0x12,
    DOOR_KEY_NO_FRAME = 0x40,
    ORN_KEY_BIG_NEAR = 0x60,     /* floor ornament: scale 0x60 on the squares beside the party */
    ORN_KEY_NOT_FAR_SIDE = 0x61, /* floor ornament: not drawn at view positions 14 and 15 */
    ORN_KEY_WET = 0x63,          /* drawn wet by the rain */
};
enum { ORN_PICTURE_SIDE, ORN_PICTURE_FRONT, ORN_PICTURE_RIGHT }; /* wall ornament pictures of a frame */
#define ENTRY_NONE            0xff
#define PORTRAIT_OFFSET_ENTRY 0xfd

#define ZONE_ROW_SIZE     4  /* zones of one ornament row */
#define ZONE_CELL_GRID    25 /* zones of the 5 x 5 grid of a view element */
#define CELL_FAR_MIDDLE   2  /* the middle cell of the far row */
#define CELL_LEFT_MIDDLE  10
#define CELL_RIGHT_MIDDLE 14
/* The cells of a square, as the party sees them. */
enum { CELL_FRONT_LEFT, CELL_FRONT_RIGHT, CELL_BACK_RIGHT, CELL_BACK_LEFT };
#define ANCHOR_DOOR_RIGHT 4
#define ANCHOR_DOOR_LEFT  3

/* The picture entry offset of the frame of a wall face word: 4 pictures per frame. */
static int face_frame_entry(uint16_t w)
{
    return (int8_t)HIGH_BYTE(w);
}

#define DOOR_BUTTON_FRAMES   5
#define DOOR_HALF_OFFSET     6 /* zone of the right door half, relative to the door zone */
#define DOOR_LEFT_OFFSET     3
#define DOOR_FRAME_TOP       1 /* door frame parts */
#define DOOR_FRAME_LEFT      2
#define DOOR_FRAME_RIGHT     4
#define DOOR_KEY_DEFAULT     10
#define ORNAMENT_KEY_DEFAULT 9
#define DOOR_NEAR_SCALE      113
#define DOOR_SIDE_SCALE      43 /* the door side picture in front of the party */
#define DOOR_SIDE_DEPTH      2
#define DOOR_ROW_NEAR        3 /* door ornament rows 3 and 4: the doors at depth 1 and 0 take clicks */
#define SQUARE_TYPE_BITS     0xe0

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
#define THINGS_ALL         0x1ffffff
#define THINGS_NEAR        0x3ff
#define THINGS_FAR         0x1fffc00
#define THINGS_STATIC_ONLY 0x40000000 /* only static creatures: the creatures behind a closed door */
#define THINGS_BEHIND_DOOR 0x40001000
#define THINGS_DOOR_CELL   0x1000

/* View positions (SKULL.EXE tables 0x75acc to 0x76025). */
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
#define POS_BIT(pos)          (1u << (pos))
#define LINK_NONE             0xff

/* Right and forward square offset of each view position. */
static const int8_t view_offset[VIEW_SQUARE_COUNT][2] = {
    {0,  0},
    {-1, 0},
    {1,  0},
    {0,  1},
    {-1, 1},
    {1,  1},
    {0,  2},
    {-1, 2},
    {1,  2},
    {-2, 2},
    {2,  2},
    {0,  3},
    {-1, 3},
    {1,  3},
    {-2, 3},
    {2,  3},
    {0,  4},
    {-1, 4},
    {1,  4},
    {-2, 4},
    {2,  4},
    {-3, 4},
    {3,  4},
};
static const int8_t view_lateral[VIEW_SQUARE_COUNT] = {
    0, -1, 1, 0, -1, 1, 0, -1, 1, -2, 2, 0, -1, 1, -2, 2, 0, -1, 1, -2, 2, -3, 3,
};
static const int8_t view_depth[VIEW_SQUARE_COUNT] = {
    0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4,
};
/* The position on the other side of the view axis. */
static const int8_t view_mirror[VIEW_SQUARE_COUNT] = {
    0, 2, 1, 3, 5, 4, 6, 8, 7, 10, 9, 11, 13, 12, 15, 14, 16, 18, 17, 20, 19, 22, 21,
};
/* The scale (1/64) of the pictures at each depth. */
static const uint8_t depth_scale[POS_DEPTH_MAX + 1] = {96, 64, 43, 28, 19};
/* The center position of each depth. */
static const uint8_t depth_center[POS_DEPTH_MAX + 1] = {0, 3, 6, 11, 16};
/* The light (1/64 darker) of each depth, and while walking toward or away. */
static const int8_t depth_light[POS_DEPTH_MAX + 1] = {0, 0, 12, 28, 46};
static const int8_t walk_light[POS_DEPTH_MAX + 1] = {0, 0, 5, 19, 36};
static const int8_t walk_back_light[POS_DEPTH_MAX + 1] = {-1, 0, -7, -9, -10};
/* The far squares in drawing order. */
static const uint8_t far_order[POS_DRAWN] = {19, 20, 17, 18, 16, 14, 15, 12, 13, 11, 9, 10, 7, 8, 6, 4, 5, 3, 1, 2};
/* The wall faces that a wall square shows: 1 front, 2 side, 3 both. */
enum { WALL_SHOWS_FRONT = VIEW_DRAWS_FRONT_FACE, WALL_SHOWS_SIDE = VIEW_DRAWS_SIDE_FACE, WALL_SHOWS_BOTH = 3 };
static const uint8_t wall_faces[VIEW_SQUARE_COUNT] = {
    0, 2, 2, 1, 3, 3, 1, 3, 3, 2, 2, 1, 3, 3, 3, 3, 1, 1, 1, 1, 1, 0, 0,
};
/* The neighbour squares of each position: left, right, ahead, behind (0xff: none). A seamless passage shows them
   again, and the possessions of a creature can lie on them. */
enum { NEIGHBOUR_LEFT, NEIGHBOUR_RIGHT, NEIGHBOUR_AHEAD, NEIGHBOUR_BEHIND };
static const uint8_t linked_squares[VIEW_SQUARE_COUNT][4] = {
    {1,   2,   3,   255},
    {255, 0,   4,   255},
    {0,   255, 5,   255},
    {4,   5,   6,   0  },
    {255, 3,   7,   1  },
    {3,   255, 8,   2  },
    {7,   8,   11,  3  },
    {9,   6,   12,  4  },
    {6,   10,  13,  5  },
    {255, 7,   14,  255},
    {8,   255, 15,  255},
    {12,  13,  16,  6  },
    {14,  11,  17,  7  },
    {11,  15,  18,  8  },
    {255, 12,  19,  9  },
    {13,  255, 20,  10 },
    {17,  18,  255, 11 },
    {19,  16,  255, 12 },
    {16,  20,  255, 13 },
    {21,  17,  255, 14 },
    {18,  22,  255, 15 },
    {255, 19,  255, 255},
    {20,  255, 255, 255},
};
/* Hidden square masks by the wall pattern of the rows (bit n: view position n). */
static const uint32_t hide_row1[4] = {0, 0x200, 0x400, 0x600};
static const uint32_t hide_row2[8] = {0, 0x10840, 0x204200, 0x2b5ac0, 0x408400, 0x55ad40, 0x60c600, 0x7fffc0};
static const uint32_t hide_row3[8] = {0, 0x10800, 0x204000, 0x2b5800, 0x408000, 0x55a800, 0x60c000, 0x7ff800};
static const uint32_t hide_row4[8] = {0, 0x10000, 0x20000, 0xb0000, 0x40000, 0x50000, 0x60000, 0x70000};
#define ROW_WALLS      7 /* row wall bits: center, left, right */
#define ROW_SIDE_WALLS 6
#define HIDE_FAR_LEFT  (POS_BIT(19) | POS_BIT(21)) /* far squares hidden behind the corner walls */
#define HIDE_FAR_RIGHT (POS_BIT(20) | POS_BIT(22))
/* Zones of the side wall ornaments by position. */
static const int16_t side_ornament_zone[POS_FAR_ROW] = {
    -1, 4425, 4450, -1, 4500, 4525, -1, 4575, 4600, 4625, 4650, -1, 4700, 4725, 4750, 4775,
};
/* Floor ornament picture entries by position, and the entries for ornaments without their own pictures. */
static const int8_t floor_ornament_entry[VIEW_SQUARE_COUNT] = {
    0, 1, 1, 2, 3, 3, 4, 5, 5, 6, 6, 7, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9,
};
static const uint8_t floor_ornament_scaled_entry[VIEW_SQUARE_COUNT] = {
    2, 3, 3, 2, 3, 3, 2, 3, 3, 3, 3, 2, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2,
};
/* Ceiling pits (holes of pits on the level above) and floor pits. */
static const int16_t ceiling_pit_zone[POS_CEILING_PIT_LAST + 1] = {871, 870, 872, 868, 867, 869, 865, 864, 866};
static const uint8_t ceiling_pit_entry[POS_CEILING_PIT_LAST + 1] = {153, 154, 154, 156, 157, 157, 159, 160, 160};
static const uint8_t ceiling_pit_flip[POS_CEILING_PIT_LAST + 1] = {0, 0, 1, 0, 0, 1, 0, 0, 1};
static const int16_t floor_pit_zone[POS_FAR_ROW] = {
    862, 861, 863, 859, 858, 860, 856, 855, 857, -1, -1, 853, 852, 854, 850, 851,
};
static const uint8_t floor_pit_flip[POS_FAR_ROW] = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1};
static const uint8_t floor_pit_entry_up[POS_FAR_ROW] = {
    130, 131, 131, 133, 134, 134, 136, 137, 137, 255, 255, 118, 119, 119, 121, 121,
};
static const uint8_t floor_pit_entry[POS_FAR_ROW] = {
    107, 108, 108, 110, 111, 111, 113, 114, 114, 255, 255, 118, 119, 119, 121, 121,
};
/* Doors. */
#define DOOR_POSITIONS 14
static const int8_t door_ornament_row[DOOR_POSITIONS] = {4, -1, -1, 3, -1, -1, 2, -1, -1, -1, -1, 1, -1, 0};
static const uint8_t door_side_frame[DOOR_POSITIONS][2] = {
    {211, 212},
    {255, 255},
    {255, 255},
    {7,   8  },
    {255, 213},
    {214, 255},
    {9,   10 },
    {255, 215},
    {216, 255},
    {255, 255},
    {255, 255},
    {11,  12 },
    {13,  14 },
    {15,  16 },
};
#define DOOR_TOP_POSITIONS 9
static const int8_t door_top_frame[DOOR_TOP_POSITIONS] = {-1, -1, -1, 18, 19, 20, 21, 22, 23};
static const int16_t door_top_zone[DOOR_TOP_POSITIONS] = {0, 0, 0, 754, 753, 755, 751, 750, 752};
static const int8_t door_depth_row[POS_DEPTH_MAX + 1] = {3, 2, 1, 0, -1};
static const int16_t door_zone[POS_FAR_ROW] = {
    3810, -1, -1, 3790, 3780, 3800, 3760, 3750, 3770, -1, -1, 3730, 3720, 3740, 3700, 3710,
};
static const uint8_t door_front_shown[POS_FAR_ROW] = {1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1};
/* Stairs seen from the side and from the front, by position and up (1) or down (0). */
static const uint8_t stairs_side_entry[POS_FAR_ROW][2] = {
    {255, 255},
    {255, 255},
    {255, 255},
    {79,  59 },
    {80,  60 },
    {81,  61 },
    {82,  62 },
    {83,  63 },
    {84,  64 },
    {255, 255},
    {255, 255},
    {85,  65 },
    {86,  66 },
    {87,  67 },
    {88,  68 },
    {89,  69 },
};
static const uint8_t stairs_side_flipped_entry[POS_FAR_ROW][2] = {
    {255, 255},
    {255, 255},
    {255, 255},
    {79,  59 },
    {80,  60 },
    {80,  60 },
    {82,  62 },
    {83,  63 },
    {83,  63 },
    {255, 255},
    {255, 255},
    {85,  65 },
    {86,  66 },
    {86,  66 },
    {88,  68 },
    {88,  68 },
};
static const int16_t stairs_side_zone[POS_FAR_ROW][2] = {
    {-1,  -1 },
    {-1,  -1 },
    {-1,  -1 },
    {822, 809},
    {821, 808},
    {823, 810},
    {819, 806},
    {818, 805},
    {820, 807},
    {-1,  -1 },
    {-1,  -1 },
    {816, 803},
    {815, 802},
    {817, 804},
    {800, 800},
    {801, 801},
};
static const uint8_t stairs_front_entry[POS_CEILING_PIT_LAST + 1][2] = {
    {255, 255},
    {205, 199},
    {206, 200},
    {255, 255},
    {207, 201},
    {208, 202},
    {255, 255},
    {209, 203},
    {210, 204},
};
static const int16_t stairs_front_zone[POS_CEILING_PIT_LAST + 1][2] = {
    {-1,  -1 },
    {832, 832},
    {833, 833},
    {-1,  -1 },
    {830, 828},
    {831, 829},
    {-1,  -1 },
    {826, 826},
    {827, 827},
};
#define GLYPH_SPACE      0x1a
#define GLYPH_PERIOD     0x1b
#define GLYPH_SIZE       8 /* the wall text font has 8 x 8 glyphs */
/* The walking view: floor and ceiling shift, and the shift of the sky. */
#define WALK_FLOOR_DY    3
#define WALK_CEILING_DY  (-2)
#define WALK_SKY_DY      (-3)
#define WALK_SKY_HIGH_DY 2

/* ---- Things in the view (SKULL.EXE tables 0x7168a, 0x75b6d to 0x75ec0) ---- */

/* GDAT categories and entries of things. */
#define CAT_ALCOVE_KEY     0xfe /* (category, 0xfe, 0x0c, entry): offset of an object picture of the category */
#define CHEST_KINDS        8    /* container GDAT indices below 8 are chests that open */
#define CREATURE_KEY_COLOR 4    /* creature value keys */
#define CREATURE_KEY_POSSESSION_LIFT 0x41
#define ORN_KEY_ALCOVE_ITEMS       0xfd /* (CAT_WALL_ORNAMENT, ornament, 0x0c, key): offset of the items in an alcove */
#define EXPLOSION_KEY_FLIP         0 /* explosion value keys: random mirror mask, fixed power, side flip of a missile */
#define EXPLOSION_KEY_FIXED_POWER  0x41
#define EXPLOSION_KEY_MISSILE_FLIP 1
#define THING_KEY                  10 /* transparent color of thing pictures */
/* Animation bits of an active group (Step 16): a random shift of the picture and a mirror. */
#define ANIM_JITTER_Y_SHIFT        3
#define ANIM_MIRROR                0x40
/* Mirror bits of a creature frame: 2 bits per side, west side in bits 0-1. */
#define FRAME_MIRROR_BITS          2
#define FRAME_MIRROR_MASK          3
#define FRAME_MIRROR_ALWAYS        1
#define FRAME_MIRROR_BY_ANIM       2  /* mirrored when the animation bits say so */
#define CREATURE_SHIFT_X           64 /* a moving creature shifts by its frame shift, sideways or forward */
#define CREATURE_SHIFT_Y           7

/* Explosion types with their own drawing. */
enum {
    EXPLOSION_GLOBAL_COUNTER = 0x30, /* counts its views in global bytes 0x40 (away) and 0x41 (party square) */
    EXPLOSION_ALCOVE_STILL = 0x64,   /* in an alcove: the still picture 0x0c */
    EXPLOSION_ALCOVE_SCALED = 0x65,  /* in an alcove: the explosion picture */
};
#define GLOBAL_EXPLOSIONS_AWAY     0x40
#define GLOBAL_EXPLOSIONS_NEAR     0x41
#define EXPLOSION_POWER_BIG        160 /* stronger explosions grow and rise */
#define EXPLOSION_POWER_SIZE       48  /* smallest power for the picture size */
#define EXPLOSION_SIZE_SHIFT       8   /* the picture size is the scale times (power + 1) / 256 */
#define EXPLOSION_GROW_BASE        192
#define EXPLOSION_GROW_NEAR        30
#define EXPLOSION_SPREAD_MIN       10
#define EXPLOSION_SCALE_MIN        8
#define EXPLOSION_SCALE_MAX        64
#define EXPLOSION_TINT_LEVEL_SHIFT 5
#define EXPLOSION_TINT_LEVELS      3
#define VIEWPORT_ZONE              4 /* the layout zone of the whole viewport */

#define MISSILE_DY               (-92) /* missiles fly above the floor */
#define MISSILE_SCALE_FULL       64
#define MISSILE_SCALE_MIN        8
#define MISSILE_HALF_POWER_SPELL 0xff82 /* this spell shows half its power plus 128 */
#define MISSILE_HALF_POWER_BASE  128

/* Thing grids: a cell of 4 x 4 per square, at (lateral + 10, forward) from the eye of the party. */
#define GRID_STRIDE          17
#define GRID_LATERAL_CENTER  10
#define GRID_LATERAL_LIMIT   10
#define GRID_FORWARD_LIMIT   17
#define NO_POSITION          0xff
#define PARTY_CELL_LAST      14 /* the party square draws the things of its 3 far rows */
#define JITTER_COUNT         16
#define ALCOVE_FIRST_ORDINAL 2
#define ALCOVE_ORDINAL_END   14
#define CELL_COLUMN_LAST     (CELL_GRID_SIDE - 1)
#define CELL_ROW_STEP        (CELL_GRID_SIDE * GRID_SQUARE_CELLS) /* cell offset of the square ahead or behind */

/* Possessions of a static creature (CreatureInfo.possessions). */
#define POSSESSIONS_CELLS_MASK 0x00f0
#define POSSESSIONS_LOW_KIND   1

/* Positions whose things the drawing shows; 9 and 10 show none, the explosions show to depth 4. */
static const uint8_t square_things_shown[POS_FAR_ROW] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1};
#define MISSILE_POSITIONS   0xf9f9u  /* positions 0, 3 to 8, 11 to 15: missiles and possessions */
#define EXPLOSION_POSITIONS 0x7f9ffu /* positions 0 to 8, 11 to 18 */
#define ALCOVE_EXPLOSION_POSITIONS                                                                                     \
    (POS_BIT(3) | POS_BIT(6) | POS_BIT(7) | POS_BIT(8) | POS_BIT(11) | POS_BIT(12) | POS_BIT(13))
/* The order of the cells of a square in the things drawing, for squares left of, on, and right of the view axis. */
static const uint8_t thing_cell_order[3][CELL_GRID_COUNT] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24},
    {0, 4, 1, 3, 2, 5, 9, 6, 8, 7, 10, 14, 11, 13, 12, 15, 19, 16, 18, 17, 20, 24, 21, 23, 22},
    {4, 3, 2, 1, 0, 9, 8, 7, 6, 5, 14, 13, 12, 11, 10, 19, 18, 17, 16, 15, 24, 23, 22, 21, 20},
};
/* The thing grid cell of the far left cell of each square up to depth 3. */
static const uint8_t square_grid_corner[POS_FAR_ROW][2] = {
    {8,  0 },
    {4,  0 },
    {12, 0 },
    {8,  4 },
    {4,  4 },
    {12, 4 },
    {8,  8 },
    {4,  8 },
    {12, 8 },
    {0,  8 },
    {16, 8 },
    {8,  12},
    {4,  12},
    {12, 12},
    {0,  12},
    {16, 12},
};
/* The grid cell of the eye of the party in its square, by the direction it looks to. */
static const uint8_t eye_cell[DIR_COUNT] = {22, 10, 2, 14};
/* The grid cells of the 4 cells of a square (north west, north east, south east, south west). */
static const uint8_t cell_view_position[DIR_COUNT] = {6, 8, 18, 16};
/* The scale of things by depth * 4 + row (row 0 is the near row), from index 1. Index 0 is the byte before the
   table in SKULL.EXE, the last depth scale: the explosions in the near cells of the party square use it. */
static const uint8_t thing_scale[19] = {
    0x13, 0x60, 0x57, 0x4e, 0x47, 0x40, 0x3a, 0x34, 0x2f, 0x2b, 0x27, 0x23, 0x1f, 0x1c, 0x1a, 0x17, 0x15, 0x13, 0x11,
};
#define OBJECT_SCALE_START    2
#define EXPLOSION_SCALE_START 3
/* The scale of missiles by depth * 2 - (1 for the near cells). */
static const uint8_t missile_scale[7] = {0x40, 0x34, 0x2b, 0x23, 0x1c, 0x17, 0x13};
/* Small offsets that keep the objects of a cell apart: an x and a y index into jitter_offset by ordinal. */
static const uint8_t jitter[JITTER_COUNT][2] = {
    {2, 5},
    {0, 6},
    {5, 7},
    {3, 0},
    {7, 1},
    {1, 2},
    {6, 3},
    {3, 3},
    {5, 5},
    {2, 6},
    {7, 7},
    {1, 0},
    {3, 1},
    {6, 2},
    {1, 3},
    {5, 3},
};
static const int8_t jitter_offset[8] = {0, 1, 2, 3, 0, -3, -2, -1};
/* Object pictures by the open state of a chest: side view, front view. */
static const uint8_t object_picture[2][2] = {
    {GFX_OBJECT_PICTURE_CLOSED, GFX_OBJECT_PICTURE_CLOSED + 1},
    {GFX_OBJECT_PICTURE_OPEN,   GFX_OBJECT_PICTURE_OPEN + 1  }
};
/* The possessions of a static creature: lift above the floor, and their cell shift by the side they lie on,
   for creatures that carry them high (0) and low (1). */
static const uint8_t possession_lift[2] = {58, 53};
static const int8_t possession_column_shift[2][DIR_COUNT] = {
    {-1, 1, 1, -1},
    {0,  1, 0, -1}
};
static const int8_t possession_row_shift[2][DIR_COUNT] = {
    {1, 1, -1, -1},
    {2, 0, -2, 0 }
};
/* The cells that show the possessions of a creature, for squares left of or on the view axis, and right of it. */
static const uint8_t possession_cells[2][4] = {
    {6, 8, 16, 18},
    {8, 6, 18, 16}
};
/* The explosions in an alcove: scale of the still picture and size of the explosion picture by depth. */
static const uint8_t alcove_explosion_scale[POS_DEPTH_MAX] = {0, 0x40, 0x28, 0x1e};
static const uint8_t alcove_explosion_size[POS_DEPTH_MAX] = {0, 0x30, 0x20, 0x18};
#define EXPLOSION_ORDER_COUNT 5
#define EXPLOSION_ORDER_LAST  (EXPLOSION_ORDER_COUNT - 1)
/* The order of the explosion cells by the party direction: the 4 cells, -1 for an explosion on the whole square. */
static const int8_t explosion_cell_order[DIR_COUNT][EXPLOSION_ORDER_COUNT] = {
    {0, 1, -1, 3, 2},
    {1, 2, -1, 0, 3},
    {2, 3, -1, 1, 0},
    {3, 0, -1, 2, 1},
};
/* A creature stands nearer to the party by its size: straight ahead or beside, and diagonally (lateral, forward
   by the octant). */
#define CREATURE_SIZE_MAX 3
static const int8_t toward_party_step[CREATURE_SIZE_MAX] = {1, 1, 2};
static const int8_t toward_party_diagonal[CREATURE_SIZE_MAX][8][2] = {
    {{0, -1}, {1, 0},  {1, 0}, {0, 1}, {0, 1},  {-1, 0}, {-1, 0},  {0, -1} },
    {{1, -1}, {1, -1}, {1, 1}, {1, 1}, {-1, 1}, {-1, 1}, {-1, -1}, {-1, -1}},
    {{1, -2}, {2, -1}, {2, 1}, {1, 2}, {-1, 2}, {-2, 1}, {-2, 1},  {-1, -2}}, /* (-2, 1) twice, as in SKULL.EXE */
};
/* Sparkle fields: the tile start (in 16 pixels), the mask picture (bit 7 mirrored, 0xff none), width, height. */
static const uint8_t sparkle_field[POS_FAR_ROW][4] = {
    {0x3b, 0xff, 224, 136},
    {0x3f, 0x05, 33,  136},
    {0x3f, 0x85, 33,  136},
    {0x3d, 0xff, 160, 111},
    {0x3f, 0x04, 60,  111},
    {0x3f, 0x84, 60,  111},
    {0x3c, 0xff, 106, 74 },
    {0x3f, 0x03, 78,  74 },
    {0x3f, 0x83, 78,  74 },
    {0x3f, 0x02, 8,   52 },
    {0x3f, 0x82, 8,   52 },
    {0x3f, 0xff, 70,  49 },
    {0x3f, 0x01, 83,  49 },
    {0x3f, 0x81, 83,  49 },
    {0x3f, 0x00, 36,  49 },
    {0x3f, 0x80, 36,  49 },
};
enum { SPARKLE_TILE, SPARKLE_MASK, SPARKLE_WIDTH, SPARKLE_HEIGHT };
#define SPARKLE_MASK_NONE     0xff
#define SPARKLE_MASK_MIRRORED 0x80
#define SPARKLE_MASK_ENTRY    0x7f
#define SPARKLE_TILE_STEP     16

/* Rain: the sky state of the map graphics (CAT_WALLSET, graphics set, 0x0b, 0x66) tells if rain shows. */
static const uint8_t sky_state_rain[5] = {0, 1, 0, 0, 1};
#define RAIN_ALONG         0x71 /* sky pictures 0x71 to 0x74: rain seen along the wind, 0x6d to 0x70 across it */
#define RAIN_ACROSS        0x6d
#define RAIN_TILE_TRIM     40 /* the rain tile row is the picture minus 40 pixels minus 0 to 31 */
#define WET_TILE_TRIM      60
#define WET_TILE_EXTRA     16
#define RANDOM_BYTE        0xff
#define RANDOM_TILE_SPREAD 0x1f

/* Sky objects: clouds, rain, and the landmarks of an outdoor map. */
#define SKY_LANDMARK_MAX      100
#define SKY_LANDMARK_DISTANCE 210 /* screen offset per square of lateral distance */
enum sky_flip { SKY_FLIP_FLOOR = 8, SKY_FLIP_CEILING = 2, SKY_FLIP_FLOOR_2 = 0x40, SKY_FLIP_CEILING_2 = 0x20 };
enum sky_move { SKY_FIXED, SKY_LANDMARK };

/* Flip parity kinds and the map flip flags (value key MAP_KEY_FLIP_FLAGS). */
enum { PARITY_SQUARE = 0, PARITY_FLOOR = 1, PARITY_CEILING = 0x20 };
enum {
    FLIP_CEILING_PITS = 0x01,
    FLIP_CEILING_BY_SQUARE = 0x02,
    FLIP_CEILING_BY_TIME = 0x04,
    FLIP_FLOOR_BY_SQUARE = 0x08,
    FLIP_FLOOR_BY_TIME = 0x10,
    FLIP_CEILING_BY_PARTY = 0x20,
    FLIP_FLOOR_BY_PARTY = 0x40,
};

ViewSquare view_squares[VIEW_SQUARE_COUNT];
static uint8_t viewport_pixels[VIEWPORT_WIDTH * VIEWPORT_HEIGHT];
Bitmap view_viewport = {viewport_pixels, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, IMAGE_8BPP};

static struct {
    int map; /* the map of the squares that the view reads now */
    int party_map;
    int party_dir, party_x, party_y; /* the position the view is drawn for */
    int walking;                     /* the party walks: the original g_party_move_steps (Step 14) */
    int shade;
    int flip; /* mirror the far pictures on odd squares */
    uint8_t gfx_set;
    uint8_t door_types[2];
    int16_t wall_key;
    uint16_t flip_flags;
    int clear; /* fill clear_rect before drawing: floor and ceiling leave a gap */
    int16_t clear_rect[RECT_SIZE];
    uint8_t clear_color;
    int landmark_set; /* graphics set of the landmark bits, -1: none yet */
    int no_landmarks;
    uint8_t landmarks[(SKY_LANDMARK_MAX + BYTE_BITS - 1) / BYTE_BITS];
    DrawCtx ctx;    /* the picture of view_setup_picture */
    DrawCtx alcove; /* the picture of the last alcove: its items are placed relative to it */
    int rain_shown; /* the map graphics show rain */
    struct {
        uint16_t first; /* possessions of a creature whose other cells are drawn later, THING_ENDOFLIST none */
        uint16_t group;
        uint32_t cells;
    } pending;
} view = {.landmark_set = -1, .pending.first = THING_ENDOFLIST};

ViewClickZone view_click_zones[VIEW_CLICK_ZONE_MAX];
int view_click_zone_count;
uint8_t view_creature_grid[VIEW_GRID_SIZE];
int view_arrows_enabled;
uint8_t view_object_grid[VIEW_GRID_SIZE];
uint32_t view_missile_masks[VIEW_SQUARE_COUNT];
uint32_t view_object_masks[VIEW_SQUARE_COUNT];

static int picture_exists(int category, int index, int entry)
{
    return gdat_exists(gdat_current(), category, index, GDAT_TYPE_IMAGE, entry);
}

/* The door type of a door thing on the current map, 0xff for none. */
static uint8_t door_type(uint16_t door)
{
    return view.door_types[door_map_type(door)];
}

static void move_relative(int dir, int forward, int right, int *x, int *y)
{
    *x += dir_dx[dir & DIR_MASK] * forward + dir_dx[dir_turn(dir, TURN_RIGHT)] * right;
    *y += dir_dy[dir & DIR_MASK] * forward + dir_dy[dir_turn(dir, TURN_RIGHT)] * right;
}

/* ---- Map graphics ---- */

void view_enter_map(int map)
{
    const gdat_t *g = gdat_current();
    sound_clear_requests();
    view.map = view.party_map = map;
    view.gfx_set = dungeon_map_wallset(map);
    view.door_types[0] = dungeon_map_door_type(map, 0);
    view.door_types[1] = dungeon_map_door_type(map, 1);
    view.wall_key = (int16_t)aspect_value_key(CAT_WALLSET, view.gfx_set, MAP_KEY_WALL_COLOR);
    view.flip_flags = aspect_value_key(CAT_WALLSET, view.gfx_set, MAP_KEY_FLIP_FLAGS);
    aspect_enter_map(map);
    uint16_t sky_state = aspect_value_key(CAT_WALLSET, view.gfx_set, MAP_KEY_SKY_STATE);
    view.rain_shown = sky_state < sizeof sky_state_rain && sky_state_rain[sky_state]; /* fix: no read past the table */
    /* floor and ceiling pictures that do not fill the viewport leave a band that is cleared */
    const Bitmap *floor = draw_gdat_image(CAT_WALLSET, view.gfx_set, GFX_MAP_FLOOR);
    const Bitmap *ceiling = draw_gdat_image(CAT_WALLSET, view.gfx_set, GFX_MAP_CEILING);
    view.clear = 0;
    if (floor && ceiling) {
        uint16_t area = (uint16_t)(floor->width * floor->height);
        uint16_t rest = (uint16_t)(VIEWPORT_WIDTH * VIEWPORT_HEIGHT - ceiling->width * ceiling->height);
        if (area < rest) {
            view.clear = 1;
            int16_t r[RECT_SIZE] = {0, ceiling->height, VIEWPORT_WIDTH,
                                    (int16_t)(VIEWPORT_HEIGHT - (ceiling->height + floor->height))};
            memcpy(view.clear_rect, r, sizeof r);
        }
    }
    uint32_t size;
    const uint8_t *colors = gdat_item(g, gdat_find(g, CAT_INTERFACE, 0, GDAT_TYPE_DATA, GFX_UI_COLORS), &size);
    view.clear_color = colors && size ? colors[0] : 0;
}

/* 1 when the picture of kind should be mirrored for the party at (x, y) looking to dir. */
static int view_get_flip_parity(int kind, int dir, int x, int y)
{
    int parity =
        (y + x + dir + dungeon_map_offset_x(view.map) + dungeon_map_offset_y(view.map) + dungeon_map_level(view.map)) &
        1;
    int party_parity = game.party_dir & 1, time = game.game_time & GAME_TIME_FRAMES;
    uint16_t flags = view.flip_flags;
    if (kind == PARITY_FLOOR) {
        if (!(flags & FLIP_FLOOR_BY_SQUARE))
            return flags & FLIP_FLOOR_BY_PARTY ? party_parity : 0;
        if (flags & FLIP_FLOOR_BY_TIME)
            return time >= HALF_TIME_FRAMES;
        return parity;
    }
    if (kind == PARITY_CEILING) {
        if (flags & FLIP_CEILING_BY_SQUARE)
            return flags & FLIP_CEILING_BY_TIME ? time < HALF_TIME_FRAMES : !parity;
        return flags & FLIP_CEILING_BY_PARTY ? party_parity : 0;
    }
    return parity;
}

/* ---- Things of the view squares ---- */

static bool is_item(uint16_t thing)
{
    int type = thing_type(thing);
    return type >= THING_WEAPON && type <= THING_MISC;
}

static int creature_dir(uint16_t group)
{
    return group_dir(group);
}

static int creature_picture_exists(int type, int entry)
{
    return picture_exists(CAT_CREATURE, type, (uint8_t)entry);
}

/* The grid cell of a thing in its square for the party looking to dir. */
static int view_get_thing_position(uint16_t thing, int dir)
{
    int cell = CELL_CENTER;
    switch (thing_type(thing)) {
    case THING_GROUP:
        return aspect_creature_cell(thing, dir_relative(dir, creature_dir(thing)));
    case THING_EXPLOSION:
        if (explosion_is_centered(thing))
            break;
        __attribute__((fallthrough));
    case THING_WEAPON:
    case THING_ARMOUR:
    case THING_SCROLL:
    case THING_POTION:
    case THING_CONTAINER:
    case THING_MISC:
    case THING_PROJECTILE:
        cell = cell_view_position[thing_cell(thing)];
        break;
    }
    return aspect_rotate_cell(cell, dir);
}

/* The cell (0 to 3) of a grid cell that holds objects, -1 for the others. */
static int view_position_to_cell(int position)
{
    for (int cell = 0; cell < DIR_COUNT; cell++)
        if (cell_view_position[cell] == position)
            return cell;
    return position == CELL_CENTER ? DIR_COUNT : -1;
}

void view_offset_creature_toward_party(int16_t party_x, int16_t party_y, int16_t size, int16_t *x, int16_t *y)
{
    if (!size)
        return;
    if (size > CREATURE_SIZE_MAX)
        size = CREATURE_SIZE_MAX;
    int16_t cx = *x, cy = *y, step = toward_party_step[size - 1];
    int16_t dx = (int16_t)abs((int16_t)(party_x - cx)), dy;
    if (!dx) {
        *y = (int16_t)(party_y < cy ? *y - step : *y + step);
        return;
    }
    dy = (int16_t)abs((int16_t)(cy - party_y));
    if (!dy) {
        *x = (int16_t)(party_x < cx ? *x - step : *x + step);
        return;
    }
    int below = party_y < cy, right = party_x < cx, same = below == right;
    int octant = (same == (dx < dy)) | same << 1 | right << 2;
    *x = (int16_t)(*x + toward_party_diagonal[size - 1][octant][0]);
    *y = (int16_t)(*y + toward_party_diagonal[size - 1][octant][1]);
}

/* Records the things of the square at pos for the party at (party_x, party_y) looking to dir: the grid cells of
   the square, the creatures in the object grid, and the cells with missiles and objects. */
static void load_square_things(int pos, int dir, int party_x, int party_y, int x, int y)
{
    ViewSquare *s = view_squares + pos;
    view_missile_masks[pos] = view_object_masks[pos] = 0;
    if (pos < POS_FAR_ROW && s->aspect != ASPECT_WALL) {
        const uint8_t *corner = square_grid_corner[pos];
        for (int col = 0; col < CELL_GRID_SIDE; col++)
            for (int row = 0; row < GRID_SQUARE_CELLS; row++)
                view_creature_grid[(corner[0] + col) * GRID_STRIDE + corner[1] + row] = (uint8_t)pos;
    }
    uint16_t thing = s->first_thing;
    if (thing == THING_ENDOFLIST)
        return;
    int16_t eye_x, eye_y;
    aspect_cell_to_xy(party_x, party_y, eye_cell[dir], &eye_x, &eye_y);
    for (; thing != THING_ENDOFLIST; thing = dungeon_next_thing(thing)) {
        int type = thing_type(thing);
        if (type == THING_PROJECTILE) {
            view_missile_masks[pos] |= 1u << view_get_thing_position(thing, dir);
        } else if (is_item(thing)) {
            /* a shop stand on a floor shows its objects itself */
            if (s->aspect != ASPECT_FLOOR || s->side[SIDE_SHOP_FLAG] != 0)
                view_object_masks[pos] |= 1u << view_get_thing_position(thing, dir);
        } else if (type == THING_GROUP) {
            int16_t cx, cy;
            /* Step 14 adds the cell of the push arrow for a creature in front of the party */
            aspect_cell_to_xy(x, y, (uint16_t)view_get_thing_position(thing, DIR_NORTH), &cx, &cy);
            if (pos != POS_PARTY)
                view_offset_creature_toward_party(
                    eye_x, eye_y, (int8_t)creature_info(gdat_current(), group_creature(thing))->size, &cx, &cy);
            int16_t lateral, forward;
            switch (dir) {
            case DIR_NORTH:
                lateral = (int16_t)(cx - eye_x);
                forward = (int16_t)(eye_y - cy);
                break;
            case DIR_EAST:
                lateral = (int16_t)(cy - eye_y);
                forward = (int16_t)(cx - eye_x);
                break;
            case DIR_SOUTH:
                lateral = (int16_t)(eye_x - cx);
                forward = (int16_t)(cy - eye_y);
                break;
            default:
                lateral = (int16_t)(eye_y - cy);
                forward = (int16_t)(eye_x - cx);
                break;
            }
            if (forward >= GRID_FORWARD_LIMIT || abs(lateral) >= GRID_LATERAL_LIMIT)
                continue;
            uint8_t *column = view_object_grid + (lateral + GRID_LATERAL_CENTER) * GRID_STRIDE;
            while (forward >= 0 && column[forward] != NO_POSITION)
                forward--;
            if (forward >= 0)
                column[forward] = (uint8_t)pos;
        }
    }
}

/* While walking, the party square shows what the squares beside it will show. */
static void load_walking_party_square(ViewSquare *s)
{
    if (s->aspect == ASPECT_WALL) {
        if (view_squares[POS_FRONT].aspect == ASPECT_TELEPORTER) {
            s->aspect = ASPECT_TELEPORTER;
        } else {
            s->side[0] = s->side[1] = s->side[2] = s->side[3] = ORNAMENT_NONE;
            s->text = 0;
            s->first_thing = THING_ENDOFLIST;
        }
        for (int i = POS_NEAR_SIDE_1; i <= POS_NEAR_SIDE_2; i++) {
            ViewSquare *side = view_squares + i, *ahead = view_squares + linked_squares[i][NEIGHBOUR_AHEAD];
            side->first_thing = THING_ENDOFLIST;
            side->square = ahead->square & SQUARE_TYPE_BITS;
            side->aspect = ahead->aspect;
            if (side->aspect == ASPECT_WALL) {
                side->side[0] = side->side[1] = side->side[2] = side->side[3] = ORNAMENT_NONE;
                side->text = THING_TEXT_NONE;
            } else if (side->aspect == ASPECT_PIT) {
                side->aspect = ASPECT_FLOOR;
                side->square = 0;
            } else if (side->aspect == ASPECT_FLOOR || side->aspect == ASPECT_TELEPORTER ||
                       side->aspect == ASPECT_DOOR_SIDE) {
                side->side[SIDE_FLOOR_ORNAMENT] = ORNAMENT_NONE;
            }
            view_missile_masks[i] = view_object_masks[i] = 0;
        }
    } else if (s->aspect == ASPECT_DOOR_FRONT) {
        s->aspect = ASPECT_FLOOR;
        s->square = 0;
        s->side[SIDE_FLOOR_ORNAMENT] = ORNAMENT_NONE;
    }
}

void view_load_square(int x, int y, int pos, int dir)
{
    int square_x = x, square_y = y;
    move_relative(dir, view_offset[pos][1], view_offset[pos][0], &square_x, &square_y);
    ViewSquare *s = view_squares + pos;
    s->x = (int8_t)square_x;
    s->y = (int8_t)square_y;
    aspect_get(s, view.map, dir, square_x, square_y);
    if (view.walking && pos == POS_PARTY)
        load_walking_party_square(s);
    load_square_things(pos, dir, x, y, square_x, square_y);
}

void view_unhide_linked_squares(void)
{
    TeleporterLink link;
    for (int pos = 0; pos < POS_LINKED; pos++) {
        ViewSquare *s = view_squares + pos;
        if (s->aspect != ASPECT_FLOOR || !dungeon_teleporter_link(view.map, s->x, s->y, &link))
            continue;
        for (int k = 0; k < 4; k++) {
            uint8_t other = linked_squares[pos][k];
            /* fix: the original reads far past the view squares for the missing neighbours */
            if (other != LINK_NONE && view_squares[other].aspect == ASPECT_HIDDEN)
                view_squares[other].aspect = ASPECT_WALL;
        }
    }
}

/* The walls of the center, left, and right squares of the row at center: bits 0 to 2. */
static uint32_t row_walls(uint32_t walls, int center)
{
    return walls >> center & ROW_WALLS;
}

/* The hide table index of the row at center: its walls, and with a center wall the side walls of the row before. */
static uint32_t row_pattern(uint32_t walls, int before, int center)
{
    return (walls & POS_BIT(center) ? row_walls(walls, before) & ROW_SIDE_WALLS : 0) | row_walls(walls, center);
}

void view_mark_hidden_squares(void)
{
    uint32_t walls = 0;
    for (int pos = 0; pos < POS_FAR_ROW; pos++)
        if (view_squares[pos].aspect == ASPECT_WALL)
            walls |= POS_BIT(pos);
    /* the rows: positions 1-2, 3-5, 6-8, 11-13; a center wall hides the squares behind it */
    uint32_t hidden = hide_row1[row_walls(walls, POS_PARTY) >> 1] |
                      hide_row2[row_pattern(walls, POS_PARTY, POS_FRONT)] |
                      hide_row3[row_pattern(walls, POS_FRONT, POS_DEPTH2_CENTER)] |
                      hide_row4[row_pattern(walls, POS_DEPTH2_CENTER, POS_DEPTH3_CENTER)];
    uint32_t covered = walls | hidden;
    if (covered & POS_BIT(POS_DEPTH3_LEFT) && covered & (POS_BIT(POS_FAR_SIDE_1) | POS_BIT(POS_DEPTH2_FAR_LEFT)))
        hidden |= HIDE_FAR_LEFT;
    if (covered & POS_BIT(POS_DEPTH3_RIGHT) && covered & (POS_BIT(POS_FAR_SIDE_2) | POS_BIT(POS_DEPTH2_FAR_RIGHT)))
        hidden |= HIDE_FAR_RIGHT;
    for (int pos = 0; pos < VIEW_SQUARE_COUNT; pos++)
        if (hidden & POS_BIT(pos) && view_squares[pos].aspect == ASPECT_WALL)
            view_squares[pos].aspect = ASPECT_HIDDEN;
}

/* ---- Pictures ---- */

int zone_id_for_view_cell(int base, int cell, int dir)
{
    return base * ZONE_CELL_GRID + aspect_rotate_cell(cell, dir) + ZONE_VIEW_CELLS;
}

static int zone_id_for_view_element(int element, int offset, int use_table)
{
    return use_table ? side_ornament_zone[element] + offset : element * ZONE_CELL_GRID + ZONE_VIEW_ELEMENTS + offset;
}

/* The scale (1/64) of a to b, rounded. */
static int rounded_scale(int a, int b)
{
    return ((a << SCALE_ROUND_SHIFT) / b + 1) >> 1;
}

/* The color remap of a picture at depth: the depth shading table of the map, dimmed by the view shade. */
static void view_get_depth_shading(uint8_t *palette, int depth, int key1, int key2, int16_t *count)
{
    int light, entry = (uint8_t)depth;
    if (!view.walking) {
        light = depth_light[depth];
    } else if (depth < 0) {
        light = walk_back_light[-depth] > -view.shade ? walk_back_light[-depth] : -view.shade;
        entry = 1;
    } else {
        light = walk_light[depth];
        entry = depth + GFX_REMAP_WALK_BASE;
    }
    if (gdat_exists(gdat_current(), CAT_WALLSET, view.gfx_set, GDAT_TYPE_REMAP, entry)) {
        DimmedRemap remap = {CAT_WALLSET, view.gfx_set, entry, view.shade, key1, key2};
        pal_get_dimmed_remap(gdat_current(), palette, &remap, count);
    } else
        pal_apply_dim_remap(palette, SHADE_FULL - ((SHADE_FULL - light) * (SHADE_FULL - view.shade) >> SHADE_SHIFT),
                            key1, key2, count);
}

/* Starts the picture of the view context: the image, its scale, place, and shading. */
static void view_setup_picture(int flip, int scale_x, int scale_y, int dx, int dy, int depth, int zone, int anchor,
                               int key, int key2, int category, int index, int entry)
{
    DrawCtx *c = draw_ctx_init(&view.ctx, category, index, entry);
    if (scale_x != scale_y && (depth == DEPTH_MIDDLE || depth == DEPTH_FAR)) {
        uint16_t v =
            aspect_value_key(category, index, depth == DEPTH_MIDDLE ? ORN_KEY_WIDTH_SCALE_2 : ORN_KEY_WIDTH_SCALE_3);
        if (LOW_BYTE(v))
            scale_x = rounded_scale((int)HIGH_BYTE(v), LOW_BYTE(v)); /* fix: the original divides by 0 */
    }
    c->flip = (int16_t)flip;
    c->scale_x = (int16_t)scale_x;
    c->scale_y = (int16_t)scale_y;
    c->dx = (int16_t)(c->dx + dx);
    c->dy = (int16_t)(c->dy + dy);
    c->zone = (int16_t)zone;
    c->anchor = (int16_t)anchor;
    c->dst = &view_viewport;
    c->transparent = (int16_t)key;
    view_get_depth_shading(c->palette, depth, key, key2, &c->palette_count);
    draw_ctx_prepare(c);
}

static void view_flush_picture(void)
{
    draw_ctx_draw(&view.ctx);
}

static int imax(int a, int b)
{
    return a > b ? a : b;
}
static int imin(int a, int b)
{
    return a < b ? a : b;
}

/* Adds a click zone at rect. */
static void view_add_click_zone(const int16_t *rect, int kind, int cell)
{
    if (view_click_zone_count >= VIEW_CLICK_ZONE_MAX)
        return; /* fix: the original has no limit */
    ViewClickZone *z = &view_click_zones[view_click_zone_count++];
    memcpy(z->rect, rect, sizeof z->rect);
    z->kind = (uint8_t)kind;
    z->cell = (uint8_t)cell;
    z->thing = THING_NONE;
}

#define CLICK_MIN_WIDTH  14
#define CLICK_MIN_HEIGHT 11
#define CLICK_LOW_HEIGHT 15 /* a lower picture moves its zone up to its middle */
#define CLICK_HALF_SIZE  7  /* a narrow or low zone starts this far before the middle of its picture */

/* Sets click zone index to the rectangle of the last picture (at least 14 x 11 pixels), or widens it. */
static void view_set_click_zone(int index, uint16_t thing, int cell, int kind)
{
    ViewClickZone *z = &view_click_zones[index];
    const int16_t *r = view.ctx.rect;
    if (z->thing == THING_NONE) {
        memcpy(z->rect, r, sizeof z->rect);
        int16_t h = z->rect[RECT_H];
        if (h < CLICK_LOW_HEIGHT) {
            if (h < CLICK_MIN_HEIGHT)
                z->rect[RECT_H] = CLICK_MIN_HEIGHT;
            z->rect[RECT_Y] = (int16_t)(z->rect[RECT_Y] + (h >> 1) - CLICK_HALF_SIZE);
        }
        int16_t w = z->rect[RECT_W];
        if (w < CLICK_MIN_WIDTH) {
            z->rect[RECT_W] = CLICK_MIN_WIDTH;
            z->rect[RECT_X] = (int16_t)(z->rect[RECT_X] + (w >> 1) - CLICK_HALF_SIZE);
        }
    } else {
        int x2 = imax((int16_t)(z->rect[RECT_X] + z->rect[RECT_W]), (int16_t)(r[RECT_X] + r[RECT_W]));
        int x1 = imin(z->rect[RECT_X], r[RECT_X]);
        z->rect[RECT_X] = (int16_t)x1;
        z->rect[RECT_W] = (int16_t)(x2 - x1);
        int y2 = imax((int16_t)(z->rect[RECT_Y] + z->rect[RECT_H]), (int16_t)(r[RECT_Y] + r[RECT_H]));
        int y1 = imin(z->rect[RECT_Y], r[RECT_Y]);
        z->rect[RECT_Y] = (int16_t)y1;
        z->rect[RECT_H] = (int16_t)(y2 - y1);
    }
    z->kind = (uint8_t)kind;
    z->thing = thing;
    z->cell = (uint8_t)cell;
}

const uint8_t *view_get_shaded_palette(int category, int index, int entry, int key)
{
    static uint8_t palette[REMAP_COLORS]; /* fix: the original has 16 bytes, which the full table overruns */
    const uint8_t *colors = draw_gdat_image_palette(category, index, entry);
    int16_t count = 0;
    if (colors) {
        memcpy(palette, colors, IMAGE_COLORS);
        count = IMAGE_COLORS;
    } else if (view.shade == VIEW_SHADE_MAX) {
        return NULL;
    }
    return pal_apply_dim_remap(palette, (uint8_t)view.shade, key, NO_KEY, &count);
}

void view_copy_image_mode1(const Bitmap *src, Bitmap *dst)
{
    gfx_blit_image(src, dst, MIRROR_H);
}

/* Draws a map picture into a zone of the view, dimmed by the view shade. */
static void view_draw_picture_in_zone(int category, int index, int entry, int zone, int key, int flip)
{
    DrawCtx c;
    draw_ctx_init(&c, category, index, entry);
    pal_apply_dim_remap(c.palette, (uint8_t)view.shade, key, NO_KEY, &c.palette_count);
    c.transparent = (int16_t)key;
    c.dst = &view_viewport;
    c.flip = (int16_t)flip;
    if (view.walking && (zone == ZONE_CEILING || zone == ZONE_FLOOR))
        c.src_y = (int16_t)(c.src_y + (zone == ZONE_CEILING ? WALK_CEILING_DY : WALK_FLOOR_DY));
    c.zone = (int16_t)zone;
    draw_ctx_prepare(&c);
    draw_ctx_draw(&c);
}

/* Prepares the picture of view_setup_picture into an offscreen bitmap of its size, for pictures that are put
   together before they go into the view; returns the bitmap in *out and the context in ctx. */
static Bitmap *view_render_picture_offscreen(DrawCtx *ctx, int category, int index, int entry, int scale_x, int scale_y,
                                             int depth, int flip, int key, int key2)
{
    view_setup_picture(flip, scale_x, scale_y, 0, 0, depth, ZONE_NONE, ZONE_NO_ANCHOR, key, NO_KEY, category, index,
                       entry);
    *ctx = view.ctx;
    ctx->transparent = (int16_t)key2;
    int16_t r[RECT_SIZE] = {0, 0, ctx->w, ctx->h};
    memcpy(ctx->rect, r, sizeof r);
    ctx->dst = draw_temp_bitmap(ctx->w > 0 ? ctx->w : 1, ctx->h > 0 ? ctx->h : 1, IMAGE_8BPP);
    return ctx->dst;
}

/* The transparent color of a picture after its color remap. */
static int mapped_key(const DrawCtx *c, int key)
{
    return c->palette_count > 0 ? c->palette[(uint8_t)key] : key;
}

/* Draws a finished offscreen picture into a zone of the view with the transparent color key. */
static void draw_offscreen(DrawCtx *ctx, Bitmap *offscreen, int zone, int anchor, int key)
{
    ctx->transparent = (int16_t)key;
    ctx->dst = &view_viewport;
    ctx->zone = (int16_t)zone;
    ctx->anchor = (int16_t)anchor;
    ctx->palette_count = 0;
    draw_ctx_set_image(ctx, offscreen);
    draw_ctx_draw(ctx);
}

/* ---- Rain ---- */

/* The rain picture for the wind as the party sees it; *side is the wind direction relative to the party. */
static int weather_get_rain_image(int *side)
{
    *side = dir_relative(game.wind_dir, view.party_dir);
    int level = game.rain_intensity >= RAIN_STRONG   ? 3
                : game.rain_intensity >= RAIN_MEDIUM ? 2
                : game.rain_intensity >= RAIN_LIGHT  ? 1
                                                     : 0;
    return (*side == DIR_NORTH || *side == DIR_SOUTH ? RAIN_ALONG : RAIN_ACROSS) + level;
}

bool view_rain_shows(void)
{
    return game.rain_intensity && view.rain_shown;
}

/* Draws the rain over the view: a random part of the rain picture, tiled. */
static void weather_draw_rain(void)
{
    if (!view_rain_shows())
        return;
    int16_t rect[RECT_SIZE], w = VIEWPORT_WIDTH, h = VIEWPORT_HEIGHT;
    if (!zone_compute_rect(NULL, rect, ZONE_WALL, &w, &h, ZONE_NO_ANCHOR))
        return;
    int side, entry = weather_get_rain_image(&side);
    const Bitmap *rain = draw_gdat_image(CAT_SKY, view.gfx_set, entry);
    if (!rain)
        return;
    if (side == DIR_EAST) {
        Bitmap *mirrored = draw_temp_bitmap(rain->width, rain->height, rain->bpp);
        if (!mirrored)
            return;
        view_copy_image_mode1(rain, mirrored);
        rain = mirrored;
    }
    const uint8_t *palette = view_get_shaded_palette(CAT_SKY, view.gfx_set, entry, 0);
    int offset = rand_next() & RANDOM_BYTE;
    int width = (int16_t)(rain->width * rain->height - RAIN_TILE_TRIM - (rand_next() & RANDOM_TILE_SPREAD));
    TileFill fill = {.tile = rain->pixels, .tile_width = width, .tile_offset = offset, .colormap = palette};
    gfx_blit_tiled_4to8(&fill, view_viewport.pixels, VIEWPORT_WIDTH, rect);
}

void view_draw_wet_image(const DrawCtx *ctx, Bitmap *offscreen, int key)
{
    int transparent = mapped_key(ctx, key);
    if (!game.rain_intensity) {
        gfx_fill_bitmap(offscreen, transparent);
        return;
    }
    int side, entry = weather_get_rain_image(&side);
    const Bitmap *rain = draw_gdat_image(CAT_SKY, view.gfx_set, entry);
    Bitmap *drops = rain ? draw_temp_bitmap(ctx->rect[RECT_W], rain->height, IMAGE_4BPP) : NULL;
    const uint8_t *shaded = view_get_shaded_palette(CAT_SKY, view.gfx_set, entry, 0);
    if (!drops || !shaded)
        return;
    int16_t r[RECT_SIZE] = {0, 0, (int16_t)((ctx->rect[RECT_W] + 1) & ~1), rain->height};
    Blit blit = {.src = rain->pixels,
                 .dst = drops->pixels,
                 .src_width = rain->width,
                 .dst_width = r[RECT_W],
                 .src_bpp = rain->bpp,
                 .dst_bpp = drops->bpp,
                 .transparent = GFX_OPAQUE,
                 .mirror = side == DIR_EAST ? MIRROR_H : MIRROR_NONE};
    gfx_blit(&blit, r);
    uint8_t palette[IMAGE_COLORS];
    memcpy(palette, shaded, IMAGE_COLORS);
    palette[0] = (uint8_t)transparent;
    int16_t area = (int16_t)(drops->width * drops->height);
    int offset = rand_below((uint16_t)(area - WET_TILE_TRIM));
    int width = (int16_t)(area - (rand_next() & RANDOM_TILE_SPREAD) - WET_TILE_EXTRA);
    if (width <= offset) { /* fix: the original tiles from past the end of the rain bitmap */
        gfx_fill_bitmap(offscreen, transparent);
        return;
    }
    TileFill fill = {.tile = drops->pixels,
                     .tile_width = width,
                     .tile_offset = offset,
                     .transparent = GFX_OPAQUE,
                     .colormap = palette};
    gfx_blit_tiled_4to8(&fill, offscreen->pixels, ctx->w, ctx->rect);
}

/* ---- Things ---- */

static void view_draw_creature_possessions(uint16_t first, int pos, uint32_t cell_mask, uint16_t group);

/* Draws the sparkles of a teleporter or of a sparkle explosion over the square at pos: the sparkle picture of
   (category, index), tiled from a random place, through the mask picture of the square. */
static void view_draw_sparkle_field(int pos, int category, int index)
{
    if (pos >= POS_FAR_ROW)
        return;
    const uint8_t *field = sparkle_field[pos];
    int16_t rect[RECT_SIZE], w = field[SPARKLE_WIDTH], h = field[SPARKLE_HEIGHT];
    if (!zone_compute_rect(NULL, rect, ZONE_WALL + pos, &w, &h, ZONE_NO_ANCHOR))
        return;
    const uint8_t *mask = NULL;
    if (field[SPARKLE_MASK] != SPARKLE_MASK_NONE) {
        const Bitmap *picture = draw_gdat_image(category, index, field[SPARKLE_MASK] & SPARKLE_MASK_ENTRY);
        if (!picture)
            return;
        mask = picture->pixels;
        if (field[SPARKLE_MASK] & SPARKLE_MASK_MIRRORED) {
            Bitmap *mirrored = draw_temp_bitmap(picture->width, picture->height, IMAGE_4BPP);
            if (!mirrored)
                return;
            gfx_fill_bitmap(mirrored, 0); /* fix: the original leaves the last column of an odd width undefined */
            int16_t r[RECT_SIZE] = {0, 0, (int16_t)(picture->width - (picture->width & 1)), picture->height};
            Blit blit = {.src = picture->pixels,
                         .dst = mirrored->pixels,
                         .src_width = picture->width,
                         .dst_width = picture->width,
                         .src_bpp = IMAGE_4BPP,
                         .dst_bpp = IMAGE_4BPP,
                         .transparent = GFX_OPAQUE,
                         .mirror = MIRROR_H};
            gfx_blit(&blit, r);
            mask = mirrored->pixels;
        }
    }
    const Bitmap *sparkles = draw_gdat_image(category, index, GFX_SPARKLE_PICTURE);
    if (!sparkles)
        return;
    const uint8_t *palette = view_get_shaded_palette(category, index, GFX_SPARKLE_PICTURE, 0);
    int offset = rand_next() & RANDOM_BYTE;
    int width = (int16_t)((rand_bit() + field[SPARKLE_TILE]) * SPARKLE_TILE_STEP);
    TileFill fill = {
        .tile = sparkles->pixels, .tile_width = width, .tile_offset = offset, .mask = mask, .colormap = palette};
    gfx_blit_tiled_4to8(&fill, view_viewport.pixels, VIEWPORT_WIDTH, rect);
}

/* Moves a cell of the square at *pos by columns and rows (rows toward the far side); the cell can go onto a
   neighbour square. Returns 0 when it leaves the view. */
static int view_shift_object_cell(int *pos, int *cell, int columns, int rows)
{
    int p = *pos, c = *cell;
    if (rows) {
        c -= rows * CELL_GRID_SIDE;
        if (c < 0) {
            p = (int8_t)linked_squares[p][NEIGHBOUR_AHEAD];
            c += CELL_ROW_STEP;
        } else if (c >= CELL_GRID_COUNT) {
            p = (int8_t)linked_squares[p][NEIGHBOUR_BEHIND];
            c -= CELL_ROW_STEP;
        }
        if (p < 0)
            return 0;
    }
    if (columns) {
        int column = c % CELL_GRID_SIDE + columns;
        if (column > CELL_COLUMN_LAST) {
            c += columns - GRID_SQUARE_CELLS;
            p = (int8_t)linked_squares[p][NEIGHBOUR_RIGHT];
        } else if (column < 0) {
            c += columns + GRID_SQUARE_CELLS;
            p = (int8_t)linked_squares[p][NEIGHBOUR_LEFT];
        } else {
            c += columns;
        }
        if (p < 0)
            return 0;
    }
    *pos = p;
    *cell = c;
    return 1;
}

/* Draws an object at its cell of the square at pos, moved by (dx, dy). ordinal (or -1) keeps the objects of a
   cell apart; group places the possessions of a static creature; in_alcove places it in the alcove of the wall. */
static void view_draw_object(uint16_t thing, int pos, int dx, int dy, int ordinal, uint16_t group, int record_click,
                             int open_state, int in_alcove)
{
    const gdat_t *g = gdat_current();
    int cell = CELL_CENTER, row, zone;
    if (group != THING_NONE) {
        int type = group_creature(group);
        int low = (creature_info(g, type)->possessions & POSSESSIONS_KIND_MASK) <= POSSESSIONS_LOW_KIND;
        int16_t lift = (int16_t)gdat_entry_index(g, CAT_CREATURE, type, GDAT_TYPE_VALUE, CREATURE_KEY_POSSESSION_LIFT);
        dy -= lift ? lift : possession_lift[low];
        int side = dir_relative(thing_cell(thing), view.party_dir);
        /* Step 14 adds the cell of the push arrow for a creature in front of the party */
        if (!view_shift_object_cell(&pos, &cell, possession_column_shift[low][side], possession_row_shift[low][side]))
            return;
    } else if (!in_alcove) {
        cell = view_get_thing_position(thing, view.party_dir);
    }
    int depth = view_depth[pos];
    if (in_alcove) {
        row = 0;
        uint16_t place =
            gdat_entry_index(g, view.alcove.category, view.alcove.index, GDAT_TYPE_VALUE2, ORN_KEY_ALCOVE_ITEMS);
        dx = view.alcove.dx + gdat_offset_x(place) + jitter_offset[jitter[ordinal][0]];
        dy = gdat_offset_y(place) + view.alcove.dy + jitter_offset[jitter[ordinal][1]];
        zone = view.alcove.zone;
    } else {
        row = CELL_COLUMN_LAST - cell / CELL_GRID_SIDE;
        if (pos == POS_PARTY && row < 2)
            return; /* the near rows of the party square are below the view */
        zone = zone_id_for_view_cell(pos, cell, 0) | ZONE_RELATIVE;
    }
    uint8_t category = item_gdat_category(thing), index = item_gdat_index(thing);
    int chest = category == CAT_CONTAINER && index < CHEST_KINDS;
    if (chest && !open_state)
        open_state = container_is_open(thing);
    int entry = object_picture[open_state][0], flip = 0, lateral = view_lateral[pos];
    if (lateral != 0) {
        flip = chest && lateral == 1;
    } else {
        int column = cell % CELL_GRID_SIDE;
        if ((column == CELL_GRID_SIDE / 2 || in_alcove) &&
            picture_exists(category, index, object_picture[open_state][1]))
            entry = object_picture[open_state][1];
        else
            flip = chest && !in_alcove && column > CELL_GRID_SIDE / 2;
    }
    if (ordinal != -1) {
        dx += jitter_offset[jitter[ordinal][0]];
        if (group == THING_NONE)
            dy += jitter_offset[jitter[ordinal][1]];
    }
    uint16_t offset = gdat_entry_index(g, category, CAT_ALCOVE_KEY, GDAT_TYPE_VALUE2, entry);
    if (offset) {
        dx += gdat_offset_x(offset);
        dy += gdat_offset_y(offset);
    }
    int scale = thing_scale[depth * GRID_SQUARE_CELLS + row + OBJECT_SCALE_START];
    view_setup_picture(flip, scale, scale, (int16_t)dx, (int16_t)dy, depth, (int16_t)zone, 0, THING_KEY, NO_KEY,
                       category, index, entry);
    if (!record_click)
        view.ctx.transparent = DRAW_NO_BLIT;
    view_flush_picture();
}

/* Draws the objects of a thing list in cell (0 to 3, relative to the party) of the square at pos; with group,
   they are the possessions of that static creature. */
static void view_draw_objects_in_cell(uint16_t thing, int pos, int cell, uint16_t group)
{
    if (thing == THING_ENDOFLIST || view_depth[pos] > DEPTH_FAR)
        return;
    const CreatureInfo *info = group != THING_NONE ? creature_info(gdat_current(), group_creature(group)) : NULL;
    int ordinal = 0;
    for (; thing != THING_ENDOFLIST; thing = dungeon_next_thing(thing)) {
        if (!is_item(thing) || dir_relative(thing_cell(thing), view.party_dir) != cell)
            continue;
        if (info && !(info->possessions & STATIC_POSSESSIONS_SHOWN) && !(info->possessions & POSSESSIONS_CELLS_MASK)) {
            /* a creature that hides what it holds: one click zone for all of it */
            if (view_click_zone_count < VIEW_CLICK_ZONE_MAX) {
                ViewClickZone *z = &view_click_zones[view_click_zone_count++];
                if (!zone_get_rect(info->possession_zone, z->rect))
                    memset(z->rect, 0, sizeof z->rect);
                z->kind = CLICK_POSSESSIONS;
                z->thing = thing;
                z->cell = (uint8_t)pos;
            }
            return;
        }
        view_draw_object(thing, pos, 0, 0, ordinal, group, 1, 0, 0);
        ordinal = (ordinal + 1) % JITTER_COUNT;
        if (info) {
            if (creature_cell_mask(info->possessions) >> cell & 1)
                view_set_click_zone(cell + 4, thing, pos, CLICK_POSSESSIONS);
        } else if (pos == POS_PARTY || (pos == POS_FRONT && cell > CELL_FRONT_RIGHT)) {
            int16_t x = view_squares[pos].x, y = view_squares[pos].y;
            uint16_t g = creature_find_group_near_point(&x, &y, thing_cell(thing));
            if (g == THING_NONE || creature_is_flying(g) ||
                creature_group_info(g)->possessions & POSSESSIONS_FLOOR_CLICKABLE)
                view_set_click_zone(cell, thing, pos, CLICK_OBJECT);
        }
    }
}

/* The picture of the animation frame of a creature of type seen from side: *flip mirrors it, *mirrored tells
   that the picture of the other side shows. */
static int creature_side_picture(int type, const uint8_t *frame, int side, int anim, int *flip, int *mirrored)
{
    int entry = frame[FRAME_ENTRY + side];
    *flip = *mirrored = 0;
    if (!creature_picture_exists(type, entry)) {
        int back = dir_opposite(side);
        *flip = *mirrored = back & 1;
        entry = frame[FRAME_ENTRY + back];
        if (!creature_picture_exists(type, entry)) {
            *flip = 0;
            entry = frame[FRAME_ENTRY + DIR_SOUTH];
        }
    } else {
        int bits = frame[FRAME_MIRROR] >> ((DIR_WEST - side) * FRAME_MIRROR_BITS) & FRAME_MIRROR_MASK;
        if (bits & FRAME_MIRROR_ALWAYS || (anim & ANIM_MIRROR && bits & FRAME_MIRROR_BY_ANIM))
            *flip = 1;
    }
    if (!creature_picture_exists(type, entry)) {
        int saved = *flip;
        *flip = 0;
        entry = (uint8_t)(GFX_CREATURE_SIDE + side);
        if (!creature_picture_exists(type, entry)) {
            int other = (uint8_t)(GFX_CREATURE_SIDE + dir_opposite(side));
            if (side & 1 && creature_picture_exists(type, other)) {
                *flip = *mirrored = 1;
                entry = other;
            } else {
                entry = GFX_CREATURE_ANY_SIDE;
                *flip = saved;
            }
        }
        if (*flip != saved)
            *mirrored = 1;
    }
    return entry;
}

/* Draws a creature group of the square at pos at depth: its animation frame, from the side it shows. */
static void view_draw_creature(int pos, int depth, uint16_t group)
{
    const gdat_t *g = gdat_current();
    int type = group_creature(group);
    uint8_t frame[CREATURE_FRAME_SIZE];
    creature_anim_frame(g, group, frame);
    /* a vanishing creature keeps the cell, size, and shift of the frame where it stopped */
    uint8_t still[CREATURE_FRAME_SIZE];
    memcpy(still, frame, sizeof still);
    ActiveGroup *active = creature_active(group);
    if (active && active->action == CREATURE_ACTION_VANISH)
        creature_anim_frame_of(g, type, (uint16_t)active->counters[0], (uint16_t)active->counters[1], still);
    int anim = active ? active->anim_bits : 0;
    int side = creature_info(g, type)->flags & CREATURE_FACES_PARTY ? DIR_SOUTH
                                                                    : dir_relative(view.party_dir, creature_dir(group));
    int flip, mirrored;
    int entry = creature_side_picture(type, frame, side, anim, &flip, &mirrored);
    int cell = still[FRAME_CELL], scale;
    if (pos == POS_FRONT && view_arrows_enabled) {
        /* by push arrow: the cell and scale of the creature that the party pushes */
        static const uint8_t arrow_cells[7] = {2, 0x0e, 0x16, 0x16, 0x16, 0x0a, 0x0c};
        static const uint8_t arrow_scales[7] = {0x34, 0x40, 0x4e, 0x4e, 0x4e, 0x40, 0x40};
        cell = (int8_t)arrow_cells[party.push_arrow];
        scale = arrow_scales[party.push_arrow];
        side = 0;
    } else {
        uint32_t size;
        const uint8_t *sizes =
            gdat_item(g, gdat_find(g, CAT_CREATURE, type, GDAT_TYPE_CREATURE_DATA, GFX_CREATURE_SIZES), &size);
        unsigned at = still[FRAME_SIZE] * DIR_COUNT + side;
        scale = gfx_scale_64(sizes && at < size ? sizes[at] : 0, depth_scale[depth]); /* fix: no read past the table */
    }
    int zone = zone_id_for_view_cell(pos, cell, side) | ZONE_RELATIVE;
    int shift = (int8_t)still[FRAME_SHIFT], dx = 0, dy = 0;
    if (shift) {
        int sx = 0, sy = 0;
        if (side == DIR_NORTH)
            sy = -CREATURE_SHIFT_Y;
        else if (side == DIR_SOUTH)
            sy = CREATURE_SHIFT_Y;
        else
            sx = mirrored ? CREATURE_SHIFT_X : -CREATURE_SHIFT_X;
        dx = gfx_scale_64((int16_t)sx, (int16_t)shift);
        dy = gfx_scale_64((int16_t)sy, (int16_t)shift);
    }
    view_setup_picture(flip, scale, scale, jitter_offset[anim & ANIM_JITTER] + dx,
                       jitter_offset[anim >> ANIM_JITTER_Y_SHIFT & ANIM_JITTER] + dy, depth, (int16_t)zone,
                       ZONE_NO_ANCHOR,
                       (int16_t)gdat_entry_index(g, CAT_CREATURE, type, GDAT_TYPE_VALUE, CREATURE_KEY_COLOR), NO_KEY,
                       CAT_CREATURE, type, entry);
}

/* Draws the creature groups of a thing list at the square at pos; returns 1 when it drew one. */
static int view_draw_creatures(uint16_t thing, int pos, uint32_t cell_mask)
{
    int drawn = 0, depth = view_depth[pos];
    if (view.walking && pos == POS_PARTY)
        return 0;
    for (; thing != THING_ENDOFLIST; thing = dungeon_next_thing(thing)) {
        if (thing_type(thing) != THING_GROUP)
            continue;
        const CreatureInfo *info = creature_info(gdat_current(), group_creature(thing));
        if (cell_mask & THINGS_STATIC_ONLY && !(info->flags & CREATURE_STATIC))
            continue;
        drawn = 1;
        view_draw_creature(pos, depth, thing);
        view_flush_picture();
        if (!(info->flags & CREATURE_STATIC)) {
            group_set_mood_flag(thing, GROUP_SEEN, true);
        } else if (info->possessions & POSSESSIONS_KIND_MASK && depth <= DEPTH_FAR) {
            if (view_squares[pos].aspect == ASPECT_DOOR_FRONT)
                cell_mask = THINGS_NEAR; /* like the original, kept */
            view_draw_creature_possessions(thing_word(thing, GROUP_POSSESSIONS), pos, cell_mask, thing);
        }
    }
    return drawn;
}

/* Draws the possessions of a static creature in the cells of cell_mask; the other cells wait for
   view_draw_pending_possessions. */
static void view_draw_creature_possessions(uint16_t first, int pos, uint32_t cell_mask, uint16_t group)
{
    if (first == THING_ENDOFLIST || !(MISSILE_POSITIONS & POS_BIT(pos)))
        return;
    view.pending.first = THING_ENDOFLIST;
    if (view_depth[pos] == 0)
        return;
    const uint8_t *cells = possession_cells[view_lateral[pos] > 0];
    uint32_t missing = 0;
    for (int k = 0; k < DIR_COUNT; k++) {
        uint32_t bit = 1u << cells[k];
        if (cell_mask & bit)
            view_draw_objects_in_cell(first, pos, view_position_to_cell(cells[k]), group);
        else
            missing |= bit;
    }
    if (missing) {
        view.pending.first = first;
        view.pending.group = group;
        view.pending.cells = missing;
    }
}

/* Draws the possessions that view_draw_creature_possessions left for later, at the square at pos. */
static void view_draw_pending_possessions(int pos)
{
    if (view.pending.first != THING_ENDOFLIST)
        view_draw_creature_possessions(view.pending.first, pos, view.pending.cells, view.pending.group);
    view.pending.first = THING_ENDOFLIST;
}

/* The picture kind of a missile by the pictures of its payload. */
static int missile_picture_kind(uint16_t thing)
{
    int category = item_gdat_category(thing), index = item_gdat_index(thing);
    if (!picture_exists(category, index, GFX_MISSILE_PICTURE_FRONT))
        return MISSILE_NO_PICTURE;
    if (!picture_exists(category, index, GFX_MISSILE_PICTURE_SIDE))
        return MISSILE_NO_SIDE;
    if (picture_exists(category, index, GFX_MISSILE_PICTURE_BACK))
        return MISSILE_FRONT_BACK;
    if (picture_exists(category, index, GFX_MISSILE_PICTURE_FRONT_2))
        return MISSILE_MIRRORED;
    return MISSILE_FRONT;
}

/* The picture of a missile of picture kind in cell (0 to 3, relative to the party) of a square at lateral, and
   its mirror in *flip: by its flight direction and the parity of its square. */
static int missile_picture(uint16_t thing, int kind, int cell, int lateral, int spell, int square_parity, int *flip)
{
    int dir = projectile_dir(thing), entry;
    if ((dir & 1) != (view.party_dir & 1)) { /* the missile flies across the view */
        entry = GFX_MISSILE_PICTURE_SIDE;
        if (kind == MISSILE_MIRRORED) {
            if (cell == CELL_FRONT_LEFT || cell == CELL_BACK_LEFT)
                *flip |= MIRROR_H;
            if (square_parity)
                *flip |= MIRROR_V;
            else
                *flip ^= MIRROR_H;
        } else if (dir_turn(view.party_dir, TURN_RIGHT) == dir) {
            *flip = MIRROR_H;
        }
        return entry;
    }
    if (kind == MISSILE_MIRRORED) {
        if (square_parity)
            *flip = MIRROR_V;
        entry =
            (cell < CELL_BACK_RIGHT) == (square_parity != 0) ? GFX_MISSILE_PICTURE_FRONT : GFX_MISSILE_PICTURE_FRONT_2;
    } else if (kind == MISSILE_FRONT || (kind == MISSILE_FRONT_BACK && dir != view.party_dir)) {
        entry = GFX_MISSILE_PICTURE_FRONT;
    } else {
        entry = GFX_MISSILE_PICTURE_BACK;
    }
    if (lateral < 0 || (lateral == 0 && cell != CELL_FRONT_RIGHT && cell != CELL_BACK_RIGHT))
        *flip |= MIRROR_H;
    if (cell & 1 && spell)
        *flip |= MIRROR_V;
    return entry;
}

/* Draws the missiles of a thing list at grid cell position of the square at pos. */
static void view_draw_missiles(uint16_t thing, int pos, int position)
{
    int cell = view_position_to_cell(position);
    if (thing == THING_ENDOFLIST || cell < 0)
        return;
    int depth = view_depth[pos], lateral = view_lateral[pos];
    if ((depth == 0 && cell >= CELL_BACK_RIGHT) || !(MISSILE_POSITIONS & POS_BIT(pos)))
        return;
    int square_parity = (view_squares[pos].x + view_squares[pos].y) & 1;
    for (; thing != THING_ENDOFLIST; thing = dungeon_next_thing(thing)) {
        if (view_get_thing_position(thing, view.party_dir) != position || thing_type(thing) != THING_PROJECTILE)
            continue;
        int zone = zone_id_for_view_cell(pos, position, 0);
        int kind = missile_picture_kind(thing);
        uint16_t payload = thing_word(thing, PROJECTILE_PAYLOAD);
        if (kind == MISSILE_NO_PICTURE) {
            uint16_t object = thing_with_cell(payload, thing_cell(thing));
            view_draw_object(object, pos, 0, MISSILE_DY, -1, THING_NONE, 1, 0, 0);
            continue;
        }
        int category = item_gdat_category(thing), index = item_gdat_index(thing);
        int spell = category == CAT_EXPLOSION, power = projectile_energy(thing);
        int scale, offset_scale;
        if ((!spell || power == POWER_MAX) && depth == 0) {
            scale = offset_scale = MISSILE_SCALE_FULL;
        } else {
            int i = depth * 2 - (cell >> 1);
            if (i < 0)
                continue;
            scale = offset_scale = missile_scale[i];
            if (spell) {
                if (payload == MISSILE_HALF_POWER_SPELL)
                    power = (power >> 1) + MISSILE_HALF_POWER_BASE;
                scale = gfx_scale_64((int16_t)rounded_scale(power, POWER_MAX), (int16_t)offset_scale);
                if (scale < MISSILE_SCALE_MIN)
                    scale = MISSILE_SCALE_MIN;
            }
        }
        int flip = 0, entry = kind == MISSILE_NO_SIDE
                                  ? GFX_MISSILE_PICTURE_FRONT
                                  : missile_picture(thing, kind, cell, lateral, spell, square_parity, &flip);
        int flips =
            spell ? gdat_entry_index(gdat_current(), CAT_EXPLOSION, index, GDAT_TYPE_VALUE, EXPLOSION_KEY_MISSILE_FLIP)
                  : MIRROR_HV;
        view_setup_picture(flips & flip, scale, scale, 0, 0, depth, (int16_t)(zone | ZONE_RELATIVE), 0, THING_KEY,
                           NO_KEY, category, index, entry);
        view.ctx.src_x = (int16_t)(view.ctx.src_x + gfx_scale_64(0, (int16_t)offset_scale));
        view.ctx.src_y = (int16_t)(view.ctx.src_y + gfx_scale_64(MISSILE_DY, (int16_t)offset_scale));
        view_flush_picture();
    }
}

/* Starts the picture of an explosion of power size into zone, mirrored by flip. */
static void view_setup_explosion_picture(int type, int size, int flip, int zone)
{
    int scale = (int16_t)(size & ~1);
    scale = scale < EXPLOSION_SCALE_MIN   ? EXPLOSION_SCALE_MIN
            : scale > EXPLOSION_SCALE_MAX ? EXPLOSION_SCALE_MAX
                                          : scale;
    DrawCtx *c = draw_ctx_init(&view.ctx, CAT_EXPLOSION, type, GFX_EXPLOSION_PICTURE);
    c->src_x = (int16_t)(c->src_x + c->dx);
    c->src_y = (int16_t)(c->src_y + c->dy);
    c->dx = c->dy = 0;
    c->zone = (int16_t)zone;
    c->anchor = 0;
    c->transparent = THING_KEY;
    c->dst = &view_viewport;
    c->flip = (int16_t)flip;
    c->scale_x = c->scale_y = (int16_t)scale;
    pal_apply_dim_remap(c->palette, (uint8_t)view.shade, THING_KEY, NO_KEY, &c->palette_count);
    draw_ctx_prepare(c);
}

/* Adds 1 to a global byte variable of the game, up to 255. */
static void global_byte_add(int var)
{
    uint8_t *value = game.global_bytes + var - GLOBAL_BYTE_FIRST;
    if (*value < GLOBAL_BYTE_MAX)
        ++*value;
}

/* Tints the whole view with an explosion on the party square: a random part of its tint picture, tiled. */
static void draw_explosion_tint(int type, int power)
{
    int level = power >> EXPLOSION_TINT_LEVEL_SHIFT;
    int entry = GFX_EXPLOSION_TINT + (level ? (level > EXPLOSION_TINT_LEVELS ? 2 : 1) : 0);
    const Bitmap *tint = draw_gdat_image(CAT_EXPLOSION, type, entry);
    if (!tint)
        return;
    int area = tint->width * tint->height;
    const uint8_t *palette = view_get_shaded_palette(CAT_EXPLOSION, type, entry, 0);
    int16_t rect[RECT_SIZE];
    int offset = (int16_t)rand_below((uint16_t)(area - RAIN_TILE_TRIM));
    int width = (int16_t)(area - RAIN_TILE_TRIM + (rand_next() & RANDOM_TILE_SPREAD));
    TileFill fill = {.tile = tint->pixels, .tile_width = width, .tile_offset = offset, .colormap = palette};
    gfx_blit_tiled_4to8(&fill, view_viewport.pixels, VIEWPORT_WIDTH, zone_get_rect(VIEWPORT_ZONE, rect));
}

/* Draws the explosions of a thing list at the square at pos, cell by cell from the far cells. */
static void view_draw_explosions(uint16_t first, int pos, uint32_t cell_mask)
{
    if (first == THING_ENDOFLIST || !(EXPLOSION_POSITIONS & POS_BIT(pos)))
        return;
    int depth = view_depth[pos];
    for (int k = 0; k < EXPLOSION_ORDER_COUNT; k++) {
        int order_cell = explosion_cell_order[view.party_dir][k];
        for (uint16_t thing = first; thing != THING_ENDOFLIST; thing = dungeon_next_thing(thing)) {
            if (thing_type(thing) != THING_EXPLOSION)
                continue;
            int type = explosion_type(thing);
            bool centered = explosion_is_centered(thing);
            if (picture_exists(CAT_EXPLOSION, type, GFX_SPARKLE_PICTURE)) {
                if (k == EXPLOSION_ORDER_LAST)
                    view_draw_sparkle_field(pos, CAT_EXPLOSION, type);
                continue;
            }
            if (order_cell != (centered ? -1 : thing_cell(thing)))
                continue;
            int flip = rand_2bits() &
                       gdat_entry_index(gdat_current(), CAT_EXPLOSION, type, GDAT_TYPE_VALUE, EXPLOSION_KEY_FLIP);
            int power =
                gdat_entry_index(gdat_current(), CAT_EXPLOSION, type, GDAT_TYPE_VALUE, EXPLOSION_KEY_FIXED_POWER)
                    ? POWER_MAX
                    : explosion_power(thing);
            int near = pos == POS_PARTY;
            if (near) {
                if (type != EXPLOSION_GLOBAL_COUNTER) {
                    draw_explosion_tint(type, power);
                    continue;
                }
                global_byte_add(GLOBAL_EXPLOSIONS_NEAR);
            } else {
                if (type == EXPLOSION_GLOBAL_COUNTER)
                    global_byte_add(GLOBAL_EXPLOSIONS_AWAY);
                if (depth == 0 && k >= 3)
                    continue;
            }
            int position = centered ? CELL_CENTER : cell_view_position[thing_cell(thing)];
            int turned = aspect_rotate_cell(position, view.party_dir);
            if (!(cell_mask & 1u << turned))
                continue;
            int scale = thing_scale[depth * GRID_SQUARE_CELLS + EXPLOSION_SCALE_START - turned / CELL_GRID_SIDE];
            int zone = zone_id_for_view_cell(pos, position, game.party_dir) | ZONE_RELATIVE;
            int strength = power + 1 > EXPLOSION_POWER_SIZE ? power + 1 : EXPLOSION_POWER_SIZE;
            view_setup_explosion_picture(type, (int16_t)(scale * strength >> EXPLOSION_SIZE_SHIFT), flip, zone);
            int16_t grow = (int16_t)(power - EXPLOSION_POWER_BIG);
            if (grow > 0) {
                grow = (int16_t)(EXPLOSION_GROW_BASE - grow);
                if (near && view_depth[POS_PARTY] == depth)
                    grow = EXPLOSION_GROW_NEAR;
                view.ctx.src_y = (int16_t)(view.ctx.src_y + VIEWPORT_HEIGHT * 8 / grow);
                if (!centered) {
                    int16_t spread = (int16_t)(VIEWPORT_WIDTH * 8 / (grow - EXPLOSION_SPREAD_MIN));
                    view.ctx.src_x = (int16_t)(view.ctx.src_x + (k == 1 || k == 4 ? -spread : spread));
                }
            }
            view.ctx.src_x = gfx_scale_64(view.ctx.src_x, (int16_t)scale);
            view.ctx.src_y = gfx_scale_64(view.ctx.src_y, (int16_t)scale);
            view_flush_picture();
            if (near)
                draw_explosion_tint(type, power);
        }
    }
}

/* Draws the things of the square at pos in the cells of cell_mask: objects, creatures, missiles, and with
   with_explosions the explosions. */
static void view_draw_square_things(int pos, uint32_t cell_mask, int with_explosions)
{
    if (pos >= POS_FAR_ROW || !square_things_shown[pos])
        return;
    const ViewSquare *s = view_squares + pos;
    uint16_t first = s->first_thing;
    const uint8_t *order = thing_cell_order[view_lateral[pos] < 0 ? 0 : view_lateral[pos] == 0 ? 1 : 2];
    const uint8_t *corner = square_grid_corner[pos];
    int last = pos == POS_PARTY ? PARTY_CELL_LAST : CELL_GRID_COUNT - 1;
    for (int i = 0; i <= last; i++) {
        int cell = order[i];
        uint32_t bit = cell_mask & 1u << cell;
        if (!bit)
            continue;
        if (bit & view_object_masks[pos])
            view_draw_objects_in_cell(first, pos, view_position_to_cell(cell), THING_NONE);
        int grid =
            (corner[0] + cell % CELL_GRID_SIDE) * GRID_STRIDE + corner[1] + GRID_SQUARE_CELLS - cell / CELL_GRID_SIDE;
        uint8_t owner = view_object_grid[grid];
        if ((s->aspect == ASPECT_WALL || view_creature_grid[grid] == pos) && owner != NO_POSITION) {
            uint32_t mask = cell == CELL_CENTER ? cell_mask : cell_mask & ~THINGS_STATIC_ONLY;
            if (view_draw_creatures(view_squares[owner].first_thing, owner, mask)) {
                view_object_grid[grid] = NO_POSITION;
                /* the missiles of the cells before, which the creature covered */
                if (view_missile_masks[pos] && i >= 7)
                    for (int j = i - 7; j < i; j++)
                        if (view_missile_masks[pos] & 1u << order[j])
                            view_draw_missiles(first, pos, order[j]);
            }
        }
        if (bit & view_missile_masks[pos])
            view_draw_missiles(first, pos, cell);
    }
    if (with_explosions)
        view_draw_explosions(first, pos, cell_mask);
}

/* Draws the items in an alcove of the front wall at pos, and the explosions in it. */
static void view_draw_alcove_contents(int pos)
{
    int ordinal = ALCOVE_FIRST_ORDINAL, depth = view_depth[pos];
    for (uint16_t thing = view_squares[pos].first_thing; thing != THING_ENDOFLIST; thing = dungeon_next_thing(thing)) {
        if (thing_cell(thing) != dir_opposite(game.party_dir))
            continue;
        if (is_item(thing)) {
            view_draw_object(thing, pos, 0, 0, ordinal, THING_NONE, 1, 0, 1);
            if (++ordinal >= ALCOVE_ORDINAL_END)
                ordinal = ALCOVE_FIRST_ORDINAL;
            if (pos == POS_FRONT)
                view_set_click_zone(VIEW_CLICK_ZONE_ALCOVE, thing, POS_FRONT, CLICK_ALCOVE);
            continue;
        }
        if (thing_type(thing) != THING_EXPLOSION || !(ALCOVE_EXPLOSION_POSITIONS & POS_BIT(pos)))
            continue;
        int type = explosion_type(thing), flip = rand_2bits();
        int zone = zone_id_for_view_element(pos, CELL_CENTER, 0);
        if (type == EXPLOSION_ALCOVE_STILL) {
            int scale = alcove_explosion_scale[depth];
            view_setup_picture(flip, scale, scale, 0, 0, 0, zone, ZONE_NO_ANCHOR, THING_KEY, NO_KEY, CAT_EXPLOSION,
                               type, GFX_ALCOVE_STILL_PICTURE);
        } else if (type == EXPLOSION_ALCOVE_SCALED) {
            view_setup_explosion_picture(type, alcove_explosion_size[depth], flip, zone);
        } else {
            continue;
        }
        view_flush_picture();
    }
}

void view_clear_viewport(void)
{
    if (view.clear)
        gfx_fill_rect(view_viewport.pixels, view.clear_rect, view.clear_color, VIEWPORT_WIDTH, IMAGE_8BPP);
}

/* ---- Walls ---- */

static uint8_t wall_text_glyph(uint8_t c)
{
    if (c >= 'A' && c <= 'Z')
        return (uint8_t)(c - 'A');
    return c == '.' ? GLYPH_PERIOD : GLYPH_SPACE;
}

/* Shops: a wall sensor (SENSOR_WALL_TEXT with a shop ornament) shows the item of the display sensor (SENSOR_SHOP)
   on its target square, with the item name and the merchant prices in coins. */
#define SHOP_CLASS_TEXT      0x10 /* + list * 3: the class set of the items of the list */
#define SHOP_CLASS_TEXT_STEP 3
#define ITEM_FLAG_UNIQUE     0x1000 /* ATTR_FLAGS: the shop shows the item only when it exists */
#define SHOP_TABLE_CREATE    0x8000 /* table word: a new item is made when the display shows it */
#define ORN_KEY_SHOP_LABEL   0xf2   /* (CAT_WALL_ORNAMENT, ornament, 0x0c, key): offsets of the name and prices */
#define ORN_KEY_SHOP_SELL    0xf1
#define ORN_KEY_SHOP_BUY     0xf0
#define SHOP_PRICE_COINS     6
#define COIN_WIDTH           8
#define COIN_HEIGHT          7
#define DISPLAY_POS          POS_FRONT /* only the wall in front shows the name and prices */

int16_t view_hit_offset_y;

static void record_target(uint16_t record, int *x, int *y)
{
    *x = sensor_target_x(record);
    *y = sensor_target_y(record);
}

/* True when the target square of the sensor record holds an item of the item index type: in the possessions of a
   creature, or loose when loose is set. */
static bool shop_square_has_object_type(uint16_t record, int type, int loose)
{
    int x, y;
    record_target(record, &x, &y);
    for (uint16_t t = aspect_first_thing(view.map, x, y); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        int kind = thing_type(t);
        if (loose && kind >= THING_WEAPON && kind <= THING_MISC) {
            if (item_get_index(t) == type)
                return true;
        } else if (kind == THING_GROUP) {
            for (uint16_t p = thing_word(t, GROUP_POSSESSIONS); p != THING_ENDOFLIST; p = dungeon_next_thing(p))
                if (item_get_index(p) == type)
                    return true;
        }
    }
    return false;
}

/* Draws text centered at x with its base line at y in the view, in the view shade. */
static void view_draw_centered_label(int x, int y, const char *text)
{
    int16_t width, height, count = UI_COLOR_COUNT;
    if (!text_measure(text, &width, &height))
        return;
    uint8_t colors[UI_COLOR_COUNT];
    memcpy(colors, ui_colors, sizeof colors);
    pal_apply_dim_remap(colors, view.shade, NO_KEY, NO_KEY, &count);
    text_draw_with_shadow(view_viewport.pixels, view_viewport.width, x - (width >> 1), y, colors[UI_WHITE],
                          colors[UI_BLACK] | TEXT_TRANSPARENT, text);
}

static const uint16_t *shop_table(uint8_t ornament, int *count)
{
    int item = gdat_find(gdat_current(), CAT_WALL_ORNAMENT, ornament, GDAT_TYPE_TABLE, GFX_ORN_SHOP_TABLE);
    uint32_t size = 0;
    const uint8_t *data = item < 0 ? NULL : gdat_item(gdat_current(), item, &size);
    *count = (int)(size / 2);
    return (const uint16_t *)data;
}

/* Chooses the item that the display shows: the next or the last item of the price list that the merchant sells. */
static void shop_choose_item(uint16_t shop, uint16_t display, uint8_t ornament, int creature)
{
    bool forward = sensor_flag(shop, SHOP_STEP_FORWARD);
    uint16_t item = sensor_data(display);
    uint8_t set[SHOP_CLASS_SET_SIZE];
    int list = sensor_delay(shop); /* the delay bits of a shop: its price list */
    if (shop_parse_item_class_set(creature, list * SHOP_CLASS_TEXT_STEP + SHOP_CLASS_TEXT, set, 0)) {
        int count, at = item;
        const uint16_t *table = shop_table(ornament, &count);
        if (table)
            for (at = 0; at < count && (table[at] & ITEM_INDEX_NONE) != item; at++) {
            }
        /* fix: the original loops for ever when the merchant sells no item of the list */
        for (int tries = 0; tries <= ITEM_INDEX_NONE; tries++) {
            at += forward || view_hit_offset_y < 0 ? 1 : -1;
            if (table) {
                if (at < 0)
                    at = count - 1;
                else if (at >= count)
                    at = 0;
                item = table[at] & ITEM_INDEX_NONE;
            } else {
                at &= ITEM_INDEX_NONE;
                item = (uint16_t)at;
            }
            if (!(set[item / BYTE_BITS] & 1 << (item % BYTE_BITS)))
                continue;
            if (gdat_entry_index(gdat_current(), item_index_gdat_category(item), item_index_gdat_index(item),
                                 GDAT_TYPE_VALUE, ATTR_FLAGS) &
                    ITEM_FLAG_UNIQUE &&
                !shop_square_has_object_type(shop, item, 1) && !shop_square_has_object_type(display, item, 0) &&
                !item_party_possesses_type(item))
                continue;
            break;
        }
        sensor_set_flag(shop, SHOP_STEP_FORWARD, false);
    }
    sensor_set_flag(shop, SHOP_SHOWN, true);
    sensor_set_flag(display, SHOP_SHOWN, true);
    sensor_set_data(display, item & ITEM_INDEX_NONE);
}

/* Draws amount as coins at the offset of key from zone. */
static void shop_draw_price(int amount, uint8_t ornament, int depth, int zone, int anchor, int key, int offset_key)
{
    uint16_t offset = gdat_entry_index(gdat_current(), CAT_WALL_ORNAMENT, ornament, GDAT_TYPE_VALUE2, offset_key);
    int16_t x = gdat_offset_x(offset), y = gdat_offset_y(offset), count;
    uint8_t coins[SHOP_PRICE_COINS];
    if (!(uint16_t)amount)
        return;
    money_split_amount(amount, SHOP_PRICE_COINS, coins, &count);
    if (count <= 0)
        return;
    Bitmap *strip = draw_temp_bitmap(count * COIN_WIDTH, COIN_HEIGHT, IMAGE_4BPP);
    if (!strip)
        return;
    view_setup_picture(0, SCALE_FULL, SCALE_FULL, 0, 0, depth, ZONE_NONE, anchor, NO_KEY, NO_KEY, CAT_WALL_ORNAMENT,
                       ornament, GFX_ORN_SHOP_COINS);
    if (!view.ctx.image)
        return;
    int16_t rect[RECT_SIZE] = {0, 0, COIN_WIDTH, COIN_HEIGHT};
    for (int i = 0; i < count; i++, rect[RECT_X] += COIN_WIDTH)
        ui_blit_bitmap(view.ctx.image, strip, rect, coins[i] * COIN_WIDTH, 0, NO_KEY, NULL);
    if (zone_compute_rect(strip, rect, zone | ZONE_RELATIVE, &x, &y, anchor))
        ui_blit_bitmap(strip, &view_viewport, rect, 0, 0, key, view.ctx.palette);
}

/* Draws the shop display of the wall sensor shop with the shop ornament: the item, its name and prices in front. */
static void shop_draw_display(uint16_t shop, uint8_t ornament, int depth, int pos, int scale_x, int scale_y, int zone,
                              int anchor, int key)
{
    int x, y;
    record_target(shop, &x, &y);
    uint16_t display = THING_ENDOFLIST;
    for (uint16_t t = aspect_first_thing(view.map, x, y); t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR;
         t = dungeon_next_thing(t))
        if (thing_type(t) == THING_SENSOR && sensor_type(t) == SENSOR_SHOP) {
            display = t;
            break;
        }
    if (display == THING_ENDOFLIST)
        return; /* fix: the original reads a NULL record */
    int creature = (uint8_t)sensor_data(shop);
    if (!sensor_flag(shop, SHOP_SHOWN) || sensor_flag(shop, SHOP_STEP_FORWARD))
        shop_choose_item(shop, display, ornament, creature);
    uint16_t item = sensor_data(display);
    int category = item_index_gdat_category(item), index = item_index_gdat_index(item);
    uint16_t offset =
        gdat_entry_index(gdat_current(), CAT_WALL_ORNAMENT, ornament, GDAT_TYPE_VALUE2, ORN_KEY_ALCOVE_ITEMS);
    view_setup_picture(0, scale_x, scale_y, gdat_offset_x(offset), gdat_offset_y(offset), depth, zone, anchor, key,
                       NO_KEY, category, index, 0);
    view_flush_picture();
    if (!shop_square_has_object_type(shop, item, 1) && !shop_square_has_object_type(display, item, 0)) {
        int count, at = 0;
        const uint16_t *table = shop_table(ornament, &count);
        if (table)
            for (; at < count && (table[at] & ITEM_INDEX_NONE) != item; at++) {
            }
        uint16_t made = table && at < count && table[at] & SHOP_TABLE_CREATE ? item_create_by_index(item) : THING_NONE;
        if (made != THING_NONE) {
            dungeon_link_thing_to_square(made, view.map, x, y);
        } else {
            view_setup_picture(0, scale_x, scale_y, gdat_offset_x(offset), gdat_offset_y(offset), depth, zone, anchor,
                               key, NO_KEY, CAT_WALL_ORNAMENT, ornament, GFX_ORN_SHOP_EMPTY);
            view_flush_picture();
        }
    }
    view_setup_picture(0, scale_x, scale_y, 0, 0, depth, zone, anchor, key, NO_KEY, CAT_WALL_ORNAMENT, ornament,
                       GFX_ORN_OVERLAY);
    view_flush_picture();
    if (pos != DISPLAY_POS)
        return;
    int16_t lx, ly;
    zone_get_position(zone, &lx, &ly);
    uint16_t place =
        gdat_entry_index(gdat_current(), CAT_WALL_ORNAMENT, ornament, GDAT_TYPE_VALUE2, ORN_KEY_SHOP_LABEL);
    char raw[TEXT_SIZE], name[TEXT_SIZE];
    view_draw_centered_label(lx + gdat_offset_x(place), ly + gdat_offset_y(place),
                             text_format(text_gdat(category, index, GFX_ITEM_NAME, raw), name));
    int list = sensor_delay(shop);
    int buy = shop_get_item_value(item, creature, list, 1, 1, -1);
    int sell = shop_get_item_value(item, creature, list, 0, 1, -1);
    shop_draw_price(sell, ornament, depth, zone, anchor, key, ORN_KEY_SHOP_SELL);
    shop_draw_price(buy, ornament, depth, zone, anchor, key, ORN_KEY_SHOP_BUY);
}

/* Draws the text of a front wall plate: the text goes into a bitmap of the plate size, centered, then into the view. */
static void draw_wall_text(int pos, int entry, int scale_x, int scale_y, int depth, int zone, int anchor, int key)
{
    DrawCtx plate;
    draw_ctx_init(&plate, CAT_WALLSET, view.gfx_set, entry);
    const Bitmap *plate_image = draw_gdat_image(CAT_WALLSET, view.gfx_set, entry);
    const Bitmap *font = draw_gdat_image(CAT_WALLSET, view.gfx_set, GFX_MAP_FONT);
    if (!plate_image || !font)
        return;
    char text[WALL_TEXT_SIZE];
    text_decode_raw(view_squares[pos].text, 0, text, sizeof text);
    int lines = 1;
    for (const char *p = text; *p; p++)
        lines += *p == '\n';
    int line_height = GLYPH_SIZE + WALL_TEXT_LINE_GAP;
    Bitmap *bitmap = draw_temp_bitmap(plate_image->width, plate_image->height, IMAGE_4BPP);
    if (!bitmap)
        return;
    gfx_fill_bitmap(bitmap, key);
    const uint8_t *font_palette = draw_gdat_image_palette(CAT_WALLSET, view.gfx_set, GFX_MAP_FONT);
    int16_t r[RECT_SIZE] = {0, (int16_t)((plate_image->height >> 1) - (lines * line_height >> 1)), GLYPH_SIZE,
                            font->height};
    Blit glyph = {.src = font->pixels,
                  .dst = bitmap->pixels,
                  .src_width = font->width,
                  .dst_width = bitmap->width,
                  .src_bpp = font->bpp,
                  .dst_bpp = bitmap->bpp,
                  .transparent = key,
                  .mirror = MIRROR_NONE,
                  .colormap = font_palette};
    for (const char *p = text;;) {
        int n = 0;
        while (p[n] && p[n] != '\n')
            n++;
        if (n) {
            r[RECT_X] = (int16_t)((plate_image->width >> 1) - ((int16_t)(GLYPH_SIZE * n) >> 1));
            for (int i = 0; i < n && r[RECT_X] >= 0; i++, r[RECT_X] += GLYPH_SIZE) {
                glyph.src_x = wall_text_glyph((uint8_t)p[i]) * GLYPH_SIZE;
                gfx_blit(&glyph, r);
            }
            p += n;
        }
        r[RECT_Y] = (int16_t)(r[RECT_Y] + line_height);
        if (!*p++)
            break;
    }
    DrawCtx c;
    draw_ctx_init(&c, CAT_NONE, 0, 0);
    c.item = -1;
    draw_ctx_set_image(&c, bitmap);
    c.scale_x = (int16_t)scale_x;
    c.scale_y = (int16_t)scale_y;
    if (font_palette)
        memcpy(c.palette, font_palette, IMAGE_COLORS);
    c.palette_count = IMAGE_COLORS;
    view_get_depth_shading(c.palette, depth, key, NO_KEY, &c.palette_count);
    c.dst = &view_viewport;
    c.zone = (int16_t)zone;
    c.anchor = (int16_t)anchor;
    c.src_x = plate.dx;
    c.src_y = plate.dy;
    c.transparent = (int16_t)key;
    draw_ctx_prepare(&c);
    draw_ctx_draw(&c);
}

/* Where and how a wall ornament picture shows. */
typedef struct {
    int flip, scale_x, scale_y, depth, zone, anchor, key;
} OrnamentPlace;

/* Draws the champion portrait of a portrait ornament on the front wall at pos; a click on the front wall finds it. */
static void draw_wall_portrait(int pos, uint8_t ornament, const OrnamentPlace *p, int record_click)
{
    uint16_t offset =
        gdat_entry_index(gdat_current(), CAT_WALL_ORNAMENT, ornament, GDAT_TYPE_VALUE2, PORTRAIT_OFFSET_ENTRY);
    view_setup_picture(0, p->scale_y, p->scale_y, gdat_offset_x(offset), gdat_offset_y(offset), p->depth, p->zone,
                       p->anchor, NO_KEY, NO_KEY, CAT_CHAMPION, (uint8_t)view_squares[pos].text, GFX_PORTRAIT);
    if (!record_click)
        view.ctx.transparent = DRAW_NO_BLIT;
    view_flush_picture();
    if (record_click && p->depth == DEPTH_FRONT)
        view_add_click_zone(view.ctx.rect, CLICK_WALL_ORNAMENT, pos);
}

/* Draws a text plate on the wall face at side of the wall at pos, and the wall text on a front plate. */
static void draw_wall_plate(int pos, int side, const OrnamentPlace *p, int record_click)
{
    int entry = GFX_MAP_PLATE_FRONT, flip = 0;
    if (side < 0)
        entry = GFX_MAP_PLATE_SIDE;
    else if (side > 0 && picture_exists(CAT_WALLSET, view.gfx_set, GFX_MAP_PLATE_RIGHT))
        entry = GFX_MAP_PLATE_RIGHT;
    else if (side > 0) {
        entry = GFX_MAP_PLATE_SIDE;
        flip = 1;
    }
    view_setup_picture(flip, p->scale_x, p->scale_y, 0, 0, p->depth, p->zone, p->anchor, p->key, NO_KEY, CAT_WALLSET,
                       view.gfx_set, entry);
    if (!record_click)
        view.ctx.transparent = DRAW_NO_BLIT;
    view_flush_picture();
    if (side == 0 && view_squares[pos].text != THING_TEXT_NONE)
        draw_wall_text(pos, entry, p->scale_x, p->scale_y, p->depth, p->zone, p->anchor, p->key);
}

/* Draws a wet wall ornament: its background picture, then the ornament with the rain over it. */
static void draw_wet_wall_ornament(uint8_t ornament, int entry, const OrnamentPlace *p, int record_click,
                                   int16_t click_rect[RECT_SIZE])
{
    view_setup_picture(p->flip, p->scale_x, p->scale_y, 0, 0, p->depth, p->zone, p->anchor, DRAW_NO_IMAGE,
                       DRAW_NO_IMAGE, CAT_WALL_ORNAMENT, ornament, entry);
    view_setup_picture(p->flip, p->scale_x, p->scale_y, view.ctx.dx, view.ctx.dy, p->depth, p->zone, p->anchor, p->key,
                       NO_KEY, CAT_WALL_ORNAMENT, ornament, GFX_ORN_WET_BACKGROUND);
    if (!record_click)
        view.ctx.transparent = DRAW_NO_BLIT;
    view_flush_picture();
    DrawCtx c;
    Bitmap *offscreen = view_render_picture_offscreen(
        &c, CAT_WALL_ORNAMENT, ornament, entry, p->scale_x, p->scale_y, p->depth, p->flip, p->key,
        (int16_t)aspect_value_key(CAT_WALL_ORNAMENT, ornament, ORN_KEY_OFFSCREEN_COLOR));
    if (offscreen) {
        view_draw_wet_image(&c, offscreen, p->key);
        draw_ctx_draw(&c);
        draw_offscreen(&c, offscreen, p->zone, p->anchor, mapped_key(&c, p->key));
    }
    memcpy(click_rect, c.rect, sizeof c.rect); /* the wet picture gives the click zone */
}

/* Draws what a front wall ornament of kind holds: the shop display, the overlay picture, or the items in an alcove. */
static void draw_ornament_contents(int pos, uint8_t ornament, int kind, const OrnamentPlace *p, int record_click)
{
    view.alcove = view.ctx;
    if (picture_exists(CAT_WALL_ORNAMENT, ornament, GFX_ORN_OVERLAY)) {
        if (kind == KIND_SHOP) {
            shop_draw_display(view_squares[pos].text, ornament, p->depth, pos, p->scale_x, p->scale_y, p->zone,
                              p->anchor, p->key);
        } else {
            view_setup_picture(p->flip, p->scale_x, p->scale_y, 0, 0, p->depth, p->zone, p->anchor, p->key, NO_KEY,
                               CAT_WALL_ORNAMENT, ornament, GFX_ORN_OVERLAY);
            if (!record_click)
                view.ctx.transparent = DRAW_NO_BLIT;
            view_flush_picture();
        }
    } else if (kind == KIND_ALCOVE) {
        view_draw_alcove_contents(pos);
    }
}

/* Draws the ornament of the wall face at side (0 front, < 0 left, > 0 right) of the wall at pos; returns its color key. */
static int view_draw_wall_ornament(int pos, int side, int record_click)
{
    int face = side < 0 ? 0 : side > 0 ? 2 : 1; /* the side words of the left, front, right faces */
    uint16_t w = view_squares[pos].side[face + 1];
    int frame = face_frame_entry(w), depth = view_depth[pos];
    uint8_t ornament = aspect_word_ornament(w);
    if (ornament == ORNAMENT_NONE)
        return -1;
    int plate = ornament == ORNAMENT_PLATE;
    int kind = aspect_value_key(CAT_WALL_ORNAMENT, ornament, ORN_KEY_KIND);
    int key = plate ? 0 : aspect_value_key(CAT_WALL_ORNAMENT, ornament, ORN_KEY_COLOR);
    if (!key)
        key = view.wall_key;
    int cell = CELL_CENTER, anchor = 0;
    uint16_t place = plate ? 0 : aspect_value_key(CAT_WALL_ORNAMENT, ornament, ORN_KEY_PLACE);
    if (place) {
        anchor = HIGH_BYTE(place);
        cell = LOW_BYTE(place) - 1;
    }
    OrnamentPlace p = {
        0, depth_scale[depth], depth_scale[depth], depth, zone_id_for_view_element(pos, cell, side != 0), anchor, key};
    if (kind == KIND_PORTRAIT && side == 0) {
        draw_wall_portrait(pos, ornament, &p, record_click);
        if (!record_click)
            return key;
    }
    if (side <= -2 || side >= 2) {
        if (depth == DEPTH_MIDDLE)
            p.scale_x = SCALE_WIDE_SIDE;
        else if (depth == DEPTH_FAR)
            p.scale_x = SCALE_FAR_SIDE;
    }
    if (plate) {
        draw_wall_plate(pos, side, &p, record_click);
        return key;
    }
    int entry;
    int16_t click_rect[RECT_SIZE] = {0};
    if (side == 0) {
        entry =
            ORN_PICTURE_FRONT; /* front ornaments are not mirrored: the depth flip of SKULL.EXE is off (0x75c11 = 1) */
    } else {
        entry = ORN_PICTURE_SIDE;
        if (side > 0) {
            if (picture_exists(CAT_WALL_ORNAMENT, ornament, (uint8_t)(frame + ORN_PICTURE_RIGHT)))
                entry = ORN_PICTURE_RIGHT;
            else
                p.flip = 1;
        }
    }
    entry = (uint8_t)(entry + frame);
    if (side == 0 && aspect_value_key(CAT_WALL_ORNAMENT, ornament, ORN_KEY_WET)) {
        draw_wet_wall_ornament(ornament, entry, &p, record_click, click_rect);
    } else {
        view_setup_picture(p.flip, p.scale_x, p.scale_y, 0, 0, depth, p.zone, anchor, key, NO_KEY, CAT_WALL_ORNAMENT,
                           ornament, entry);
        if (!record_click)
            view.ctx.transparent = DRAW_NO_BLIT;
        view_flush_picture();
        if (!record_click)
            return key;
        memcpy(click_rect, view.ctx.rect, sizeof click_rect);
    }
    if (record_click && kind != KIND_PORTRAIT && pos >= POS_NEAR_SIDE_1 && pos <= POS_FRONT)
        view_add_click_zone(click_rect, CLICK_WALL_ORNAMENT, pos);
    if (kind && side == 0)
        draw_ornament_contents(pos, ornament, kind, &p, record_click);
    return key;
}

/* Draws the wall block at pos with the clip margins that open squares beside it need. */
static void view_draw_wall_block(int pos)
{
    int lateral = view_lateral[pos], depth = view_depth[pos];
    uint16_t margins = aspect_value_key(CAT_WALLSET, view.gfx_set, MAP_KEY_WALL_MARGINS + (depth > DEPTH_FRONT));
    int left = 0, right = 0;
    if (depth > DEPTH_FRONT) {
        if (view_squares[POS_FRONT_LEFT].aspect == ASPECT_WALL)
            left = HIGH_BYTE(margins);
        if (view_squares[POS_FRONT_RIGHT].aspect == ASPECT_WALL)
            right = LOW_BYTE(margins);
    } else if (depth > 0) {
        if (view_squares[POS_NEAR_SIDE_1].aspect == ASPECT_WALL)
            left = HIGH_BYTE(margins);
        if (view_squares[POS_NEAR_SIDE_2].aspect == ASPECT_WALL)
            right = LOW_BYTE(margins);
    }
    if (left || right)
        zone_set_clip_margins(left, 0, right, 0);
    int flip = lateral > 0, entry;
    if (pos >= POS_FAR_ROW) {
        if (lateral == -2 || lateral == 2)
            flip = 0;
        flip ^= view.flip;
        entry = GFX_MAP_FAR_WALL;
    } else if (view.flip) {
        entry = (uint8_t)(view_mirror[pos] + GFX_MAP_WALL_FLIPPED);
        if (!picture_exists(CAT_WALLSET, view.gfx_set, entry)) {
            if (lateral == 0)
                flip = 1;
            entry -= MAP_WALL_FLIPPED_MISSING;
        }
    } else {
        entry = pos + GFX_MAP_WALL;
    }
    view_setup_picture(flip, SCALE_FULL, SCALE_FULL, 0, 0, view.walking ? -depth : 0, ZONE_WALL + pos, ZONE_NO_ANCHOR,
                       view.wall_key, NO_KEY, CAT_WALLSET, view.gfx_set, entry);
    view_flush_picture();
    zone_clip_rect[RECT_W] = 0;
}

static void view_draw_wall_square(int pos)
{
    view_draw_wall_block(pos);
    switch (wall_faces[pos]) {
    case WALL_SHOWS_FRONT:
        view_draw_wall_ornament(pos, 0, 1);
        break;
    case WALL_SHOWS_BOTH:
        view_draw_wall_ornament(pos, 0, 1);
        __attribute__((fallthrough));
    case WALL_SHOWS_SIDE:
        view_draw_wall_ornament(pos, view_lateral[pos], 1);
        break;
    }
}

/* ---- Floors, pits, stairs ---- */

/* Draws a floor ornament through an offscreen bitmap: wet with the rain, or over a passage (cleared). */
static void draw_floor_ornament_offscreen(uint8_t ornament, int entry, const OrnamentPlace *p, int wet)
{
    DrawCtx c;
    Bitmap *offscreen = view_render_picture_offscreen(
        &c, CAT_FLOOR_ORNAMENT, ornament, entry, p->scale_x, p->scale_y, p->depth, p->flip, p->key,
        (int16_t)aspect_value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_OFFSCREEN_COLOR));
    if (!offscreen)
        return;
    int transparent = mapped_key(&c, p->key);
    if (wet)
        view_draw_wet_image(&c, offscreen, p->key);
    else
        gfx_fill_bitmap(offscreen, transparent);
    draw_ctx_draw(&c);
    draw_offscreen(&c, offscreen, p->zone, p->anchor, transparent);
}

/* Draws the things of the squares behind the seamless passage link at pos and depth, on the linked map. */
static void draw_linked_squares(const TeleporterLink *link, int pos, int depth)
{
    /* the squares behind the passage, on the linked map */
    int x = link->x, y = link->y, saved_dir = view.party_dir, saved_map = view.map;
    view.party_dir = link->target_side;
    view.map = link->map;
    move_relative(view.party_dir, -depth, 0, &x, &y);
    for (int d = POS_DEPTH_MAX; d >= depth; d--)
        view_load_square(x, y, depth_center[d], view.party_dir);
    for (int d = POS_DEPTH_MAX; d > depth; d--)
        view_draw_square_things(depth_center[d], THINGS_ALL, 1);
    view.map = saved_map;
    view.party_dir = saved_dir;
    view_draw_square_things(pos, THINGS_NEAR, 1);
}

/* Draws the floor ornament of the square at pos. A seamless passage there shows the squares behind it.
   Returns 1 for a passage that was drawn. */
static int view_draw_floor_ornament(int pos)
{
    int entry = floor_ornament_entry[pos];
    if (entry < 0)
        return 0;
    ViewSquare *s = view_squares + pos;
    int depth = view_depth[pos];
    TeleporterLink link;
    int linked = dungeon_teleporter_link(view.map, s->x, s->y, &link);
    if (depth > DEPTH_FAR && !linked)
        return 0;
    if (linked && pos != POS_NEAR_SIDE_1 && pos != POS_NEAR_SIDE_2 && link.side != game.party_dir &&
        link.side != dir_opposite(game.party_dir))
        return 0;
    uint16_t w = s->side[SIDE_FLOOR_ORNAMENT];
    int frame = HIGH_BYTE(w);
    entry = (uint8_t)(entry + frame);
    uint8_t ornament = aspect_word_ornament(w);
    if (ornament == ORNAMENT_NONE || ornament == 0)
        return 0;
    if ((pos == POS_FAR_SIDE_1 || pos == POS_FAR_SIDE_2) &&
        aspect_value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_NOT_FAR_SIDE))
        return 0;
    int flip = 0;
    if (view_lateral[pos] == 0) {
        if (!aspect_value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_NO_FLIP))
            flip = view.flip ^ !(depth & 1);
    } else if (view_lateral[pos] >= 1) {
        flip = 1;
    }
    int key = aspect_value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_COLOR);
    if (!key)
        key = view.wall_key;
    int cell = CELL_CENTER, anchor = 0;
    uint16_t place = aspect_value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_PLACE);
    if (place) {
        anchor = HIGH_BYTE(place);
        cell = LOW_BYTE(place) - 1;
    }
    int zone = zone_id_for_view_cell(pos, cell, 0), scale = SCALE_FULL;
    if (!picture_exists(CAT_FLOOR_ORNAMENT, ornament, entry)) {
        entry = (uint8_t)(floor_ornament_scaled_entry[pos] + frame);
        scale = depth_scale[depth];
    }
    if ((pos == POS_NEAR_SIDE_1 || pos == POS_NEAR_SIDE_2) &&
        aspect_value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_BIG_NEAR))
        scale = SCALE_BIG_NEAR;
    int wet = depth ? aspect_value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_WET) : 0;
    if (!depth || (!linked && !wet)) {
        view_setup_picture(flip, scale, scale, 0, 0, depth, zone, anchor, key, NO_KEY, CAT_FLOOR_ORNAMENT, ornament,
                           entry);
        view_flush_picture();
        return 0;
    }
    view_setup_picture(flip, scale, scale, 0, 0, depth, zone, anchor, DRAW_NO_IMAGE, DRAW_NO_IMAGE, CAT_FLOOR_ORNAMENT,
                       ornament, entry);
    if (view_lateral[pos] == 0) {
        view_setup_picture(flip, scale, scale, view.ctx.dx, view.ctx.dy, depth, zone, anchor, key, NO_KEY,
                           CAT_FLOOR_ORNAMENT, ornament, (uint8_t)(entry + GFX_ORN_WET_BACKGROUND));
        view_flush_picture();
        draw_linked_squares(&link, pos, depth);
    }
    OrnamentPlace p = {flip, scale, scale, depth, zone, anchor, key};
    draw_floor_ornament_offscreen(ornament, entry, &p, wet);
    return linked;
}

/* Draws the hole of an open pit on the level above. */
static void view_draw_ceiling_pit(int pos)
{
    if (pos > POS_CEILING_PIT_LAST || !(view.flip_flags & FLIP_CEILING_PITS))
        return;
    int x = view_squares[pos].x, y = view_squares[pos].y;
    int map = dungeon_location_on_map(view.map, -1, &x, &y);
    if (map < 0)
        return;
    /* fix: the original reads the square without a bounds check; outside the map it is a wall */
    uint8_t square = dungeon_square_edge(map, x, y);
    if (square_type(square) != SQUARE_PIT || !square_flag(square, SQUARE_PIT_OPEN))
        return;
    int flip = pos == POS_PARTY ? view_get_flip_parity(PARITY_CEILING, view.party_dir, view.party_x, view.party_y) != 0
                                : ceiling_pit_flip[pos];
    view_draw_picture_in_zone(CAT_WALLSET, view.gfx_set, ceiling_pit_entry[pos], ceiling_pit_zone[pos], view.wall_key,
                              flip);
}

static void view_draw_floor_pit(int pos)
{
    int16_t zone = floor_pit_zone[pos];
    if (zone < 0)
        return;
    int up = view_squares[pos].side[SIDE_PIT_UP] != 0;
    if (pos >= POS_FLOOR_PIT_BLOCKED && up)
        return;
    int flip = pos == POS_PARTY ? view_get_flip_parity(PARITY_FLOOR, view.party_dir, view.party_x, view.party_y) != 0
                                : floor_pit_flip[pos];
    view_draw_picture_in_zone(CAT_WALLSET, view.gfx_set, up ? floor_pit_entry_up[pos] : floor_pit_entry[pos], zone,
                              view.wall_key, flip);
}

static void view_draw_stairs_side(int pos)
{
    int up = view_squares[pos].side[SIDE_STAIRS_UP] != 0;
    int16_t zone = stairs_side_zone[pos][up];
    if (zone < 0)
        return;
    uint8_t entry = stairs_side_entry[pos][up];
    if (picture_exists(CAT_WALLSET, view.gfx_set, entry)) {
        view_draw_picture_in_zone(CAT_WALLSET, view.gfx_set, entry, zone, view.wall_key, 0);
        return;
    }
    view_setup_picture(1, SCALE_FULL, SCALE_FULL, 0, 0, 0, zone, ZONE_NO_ANCHOR, view.wall_key, NO_KEY, CAT_WALLSET,
                       view.gfx_set, stairs_side_flipped_entry[pos][up]);
    view_flush_picture();
}

static void view_draw_stairs_front(int pos)
{
    if (pos > POS_CEILING_PIT_LAST)
        return;
    int up = view_squares[pos].side[SIDE_STAIRS_UP] != 0;
    uint8_t entry = stairs_front_entry[pos][up];
    if (entry != ENTRY_NONE)
        view_draw_picture_in_zone(CAT_WALLSET, view.gfx_set, entry, stairs_front_zone[pos][up], view.wall_key, 0);
}

/* ---- Doors ---- */

static int view_inverse_scale(unsigned size, unsigned scaled)
{
    int scale = (int)((size * 2 * SCALE_FULL + SCALE_FULL) / (uint16_t)scaled) >> 1;
    return gfx_scale_64((int16_t)scaled, (int16_t)scale) < (int)size ? scale + 1 : scale;
}

static void view_draw_door_ornament(int category, int ornament, int entry, int pos)
{
    int row = door_ornament_row[pos];
    if (row < 0 || ornament == ORNAMENT_NONE)
        return;
    int depth = view_depth[pos], scale = depth_scale[depth], base = 0, key;
    if (category == CAT_DOOR_BUTTON) {
        base = aspect_value_key(CAT_DOOR_BUTTON, ornament, ORN_KEY_ZONE_ROW) * DOOR_BUTTON_FRAMES;
        key = view.wall_key;
    } else {
        key = (int16_t)aspect_value_key(category, ornament, ORN_KEY_COLOR);
        if (!key)
            key = view.wall_key;
    }
    view_setup_picture(0, scale, scale, 0, 0, depth, base + ZONE_DOOR_ORNAMENT + row, ZONE_NO_ANCHOR, key, NO_KEY,
                       category, ornament, entry);
    view_flush_picture();
    if (row >= DOOR_ROW_NEAR)
        view_add_click_zone(view.ctx.rect, CLICK_DOOR_BUTTON, pos);
}

static void view_draw_door_frame(int pos, int parts)
{
    uint16_t door = view_squares[pos].side[SIDE_DOOR_THING];
    if (aspect_value_key(CAT_DOOR, door_type(door), DOOR_KEY_NO_FRAME))
        return;
    if (parts & DOOR_FRAME_TOP && pos < DOOR_TOP_POSITIONS && door_top_frame[pos] >= 0)
        view_draw_picture_in_zone(CAT_WALLSET, view.gfx_set, door_top_frame[pos], door_top_zone[pos], view.wall_key, 0);
    int mpos = view.flip ? view_mirror[pos] : pos, left = view.flip ? 1 : 0;
    uint8_t entry = door_side_frame[mpos][left];
    if (parts & DOOR_FRAME_LEFT && entry != ENTRY_NONE) {
        view_setup_picture(0, SCALE_FULL, SCALE_FULL, 0, 0, 0, zone_id_for_view_cell(pos, CELL_LEFT_MIDDLE, 0),
                           ANCHOR_DOOR_RIGHT, view.wall_key, NO_KEY, CAT_WALLSET, view.gfx_set, entry);
        view_flush_picture();
    }
    entry = door_side_frame[mpos][!left];
    if (!(parts & DOOR_FRAME_RIGHT) || entry == ENTRY_NONE)
        return;
    view_setup_picture(1, SCALE_FULL, SCALE_FULL, 0, 0, 0, zone_id_for_view_cell(pos, CELL_RIGHT_MIDDLE, 0),
                       ANCHOR_DOOR_LEFT, view.wall_key, NO_KEY, CAT_WALLSET, view.gfx_set, entry);
    view_flush_picture();
    if (door_flag(door, DOOR_BUTTON)) {
        view_draw_door_ornament(CAT_DOOR_BUTTON, 0, door_flag(door, DOOR_BUTTON_PRESSED) * DOOR_BUTTON_FRAMES, pos);
    } else if (view_squares[pos].side[SIDE_FLOOR_ORNAMENT] != ORNAMENT_NONE) {
        uint16_t w = view_squares[pos].side[SIDE_FLOOR_ORNAMENT];
        view_draw_door_ornament(CAT_WALL_ORNAMENT, aspect_word_ornament(w), (uint8_t)(face_frame_entry(w) + 1), pos);
    }
}

/* Puts the door picture of type together offscreen with its ornament and its damage, and makes it the view
   picture. row is the zone row of the door depth. */
static void compose_door(uint8_t type, int ornament, int destroyed, int entry, int scale, int shade_depth, int depth,
                         int row, int key)
{
    /* the door with its ornament or damage is put together offscreen */
    DrawCtx c;
    Bitmap *offscreen =
        view_render_picture_offscreen(&c, CAT_DOOR, type, entry, scale, scale, shade_depth, 0, key, NO_KEY);
    if (!offscreen)
        return;
    draw_ctx_draw(&c);
    int scale_x = scale, scale_y = scale;
    if (scale == SCALE_FULL) {
        const Bitmap *near = draw_gdat_image(CAT_DOOR, type, 0);
        if (near) {
            scale_x = view_inverse_scale((uint16_t)view.ctx.w, (uint16_t)near->width);
            scale_y = view_inverse_scale((uint16_t)view.ctx.h, (uint16_t)near->height);
        }
    }
    if (ornament) {
        uint8_t o = dungeon_map_door_ornaments(view.party_map)[ornament - 1];
        int okey = (int16_t)aspect_value_key(CAT_DOOR_ORNAMENT, o, ORN_KEY_COLOR);
        if (!okey)
            okey = ORNAMENT_KEY_DEFAULT;
        int ozone = aspect_value_key(CAT_DOOR_ORNAMENT, o, ORN_KEY_ZONE_ROW) * ZONE_ROW_SIZE + ZONE_ORNAMENT_ROWS + row;
        view_setup_picture(0, scale_x, scale_y, 0, 0, depth, ozone, ZONE_NO_ANCHOR, okey, NO_KEY, CAT_DOOR_ORNAMENT, o,
                           0);
        view.ctx.dst = offscreen;
        view_flush_picture();
    }
    if (destroyed) {
        int dkey = (int16_t)aspect_value_key(CAT_DOOR, type, ORN_KEY_DAMAGE_COLOR);
        if (!dkey)
            dkey = ORNAMENT_KEY_DEFAULT;
        int dzone = aspect_value_key(CAT_DOOR, type, ORN_KEY_KIND) * ZONE_ROW_SIZE + ZONE_ORNAMENT_ROWS + row;
        view_setup_picture(0, scale_x, scale_y, 0, 0, depth, dzone, ZONE_NO_ANCHOR, dkey, key, CAT_DOOR, type,
                           GFX_DOOR_DAMAGE);
        view.ctx.dst = offscreen;
        view_flush_picture();
    }
    c.transparent = (int16_t)mapped_key(&c, key);
    c.palette_count = 0;
    draw_ctx_set_image(&c, offscreen);
    view.ctx = c;
    view.ctx.dst = &view_viewport;
}

static void view_draw_door(int pos, int frame_before, int frame_after, uint32_t cell_mask)
{
    if (frame_before)
        view_draw_door_frame(pos, frame_before);
    if (cell_mask)
        view_draw_square_things(pos, cell_mask, 0);
    ViewSquare *s = view_squares + pos;
    int state = s->side[SIDE_DOOR_STATE], depth = view_depth[pos];
    int row = depth <= DEPTH_FAR ? door_depth_row[depth] : -1, zone = door_zone[pos];
    if (state != DOOR_STATE_OPEN && depth <= DEPTH_FAR && row >= 0 && zone >= 0) {
        uint16_t door = s->side[SIDE_DOOR_THING];
        uint8_t type = door_type(door);
        int key = (int16_t)aspect_value_key(CAT_DOOR, type, ORN_KEY_COLOR);
        if (!key)
            key = DOOR_KEY_DEFAULT;
        int entry = depth - 1, scale = SCALE_FULL, shade_depth = 0;
        if (depth == 0 || !picture_exists(CAT_DOOR, type, entry)) {
            if (depth) {
                shade_depth = depth;
                scale = depth_scale[depth];
            } else {
                scale = DOOR_NEAR_SCALE;
            }
            entry = 0;
        }
        int ornament = door_ornament(door);
        if (!ornament && state != DOOR_STATE_BROKEN) {
            view_setup_picture(0, scale, scale, 0, 0, shade_depth, ZONE_NONE, ZONE_NO_ANCHOR, key, NO_KEY, CAT_DOOR,
                               type, entry);
        } else {
            compose_door(type, ornament, state == DOOR_STATE_BROKEN, entry, scale, shade_depth, depth, row, key);
        }
        if (state < DOOR_STATE_CLOSED) {
            zone += state;
            if (!door_flag(door, DOOR_VERTICAL)) {
                /* the two halves slide apart: the right half first */
                int16_t half = (int16_t)(view.ctx.w >> 1);
                view.ctx.w = half;
                view.ctx.x = (int16_t)(view.ctx.x + half);
                view.ctx.zone = (int16_t)(zone + DOOR_HALF_OFFSET);
                view_flush_picture();
                view.ctx.x = (int16_t)(view.ctx.x - half);
                zone += DOOR_LEFT_OFFSET;
            }
        }
        view.ctx.zone = (int16_t)zone;
        view_flush_picture();
    }
    if (frame_after)
        view_draw_door_frame(pos, frame_after);
}

static void view_draw_door_front(int pos)
{
    if (!door_front_shown[pos])
        return;
    uint32_t mask = view_squares[pos].side[SIDE_DOOR_STATE] == DOOR_STATE_BROKEN ? 0 : THINGS_BEHIND_DOOR;
    view_draw_square_things(pos, THINGS_NEAR, pos != POS_PARTY);
    static const uint8_t frames[POS_FAR_ROW][2] = {
        {6, 0},
        {0, 0},
        {0, 0},
        {7, 0},
        {1, 4},
        {1, 2},
        {7, 0},
        {1, 4},
        {1, 2},
        {0, 0},
        {0, 0},
        {6, 0},
        {2, 4},
        {4, 2},
        {0, 0},
        {0, 0},
    };
    if (pos == POS_PARTY) {
        if (mask)
            view_draw_square_things(pos, THINGS_DOOR_CELL, 0);
        view_draw_door(pos, frames[pos][0], frames[pos][1], 0);
        return;
    }
    if (pos != POS_NEAR_SIDE_1 && pos != POS_NEAR_SIDE_2 && pos != POS_DEPTH2_FAR_LEFT && pos != POS_DEPTH2_FAR_RIGHT)
        view_draw_door(pos, frames[pos][0], frames[pos][1], mask);
    view_draw_square_things(pos, THINGS_FAR, 1);
    view_draw_pending_possessions(pos);
}

/* ---- The sky of outdoor maps ---- */

/* The lateral and forward distance from the party at (*x, *y) to (x2, y2) looking to dir, and the distance;
   0 for a point not in front. */
static int outdoor_relative_distance(int dir, int16_t *x, int16_t *y, int x2, int y2)
{
    int16_t forward = 0, lateral = 0;
    switch (dir) {
    case DIR_NORTH:
        forward = (int16_t)(*y - y2);
        lateral = (int16_t)(x2 - *x);
        break;
    case DIR_EAST:
        forward = (int16_t)(x2 - *x);
        lateral = (int16_t)(y2 - *y);
        break;
    case DIR_SOUTH:
        forward = (int16_t)(y2 - *y);
        lateral = (int16_t)(*x - x2);
        break;
    case DIR_WEST:
        forward = (int16_t)(*x - x2);
        lateral = (int16_t)(*y - y2);
        break;
    }
    *x = lateral;
    *y = forward;
    if (forward < 1)
        return 0;
    uint16_t n = (uint16_t)(lateral * lateral + forward * forward);
    if (n <= 2)
        return 1;
    uint16_t root = n >> 1, next;
    for (;;) { /* integer square root by Newton steps */
        next = (uint16_t)((n / root + root) >> 1);
        if (next >= root)
            return root;
        root = next;
    }
}

/* Places a landmark of the sky text definition for the party; returns 0 when it is not visible. */
static int outdoor_place_sky_object(SkyObject *o, const char *definition, int dir, int x, int y)
{
    o->dx = o->dy = 0;
    int move = (int16_t)gdat_text_int_field(definition, "mv");
    if (move == SKY_FIXED) {
        o->scale_x = o->scale_y = SCALE_FULL;
        return 1;
    }
    if (move != SKY_LANDMARK)
        return 0;
    int16_t px = (int16_t)(x + dungeon_map_offset_x(view.map)), py = (int16_t)(y + dungeon_map_offset_y(view.map));
    int lx = (int16_t)gdat_text_int_field(definition, "xl"), ly = (int16_t)gdat_text_int_field(definition, "yl");
    int distance = outdoor_relative_distance(dir, &px, &py, lx, ly);
    if (py < 1)
        return 0;
    int16_t near = (int16_t)(SHADE_FULL - (distance - gdat_text_int_field(definition, "fd")));
    int scale = near > 1 ? near : 1;
    o->dx = (int16_t)(px * SKY_LANDMARK_DISTANCE / distance);
    o->scale_x = o->scale_y = (uint8_t)rounded_scale(scale, SHADE_FULL);
    return 1;
}

static void outdoor_draw_sky_object(const SkyObject *o, int dir, int x, int y)
{
    if (!o->zone)
        return;
    int flip = (o->flip == SKY_FLIP_FLOOR && view_get_flip_parity(PARITY_FLOOR, dir, x, y)) ||
               (o->flip == SKY_FLIP_CEILING && view_get_flip_parity(PARITY_CEILING, dir, x, y)) ||
               (o->flip == SKY_FLIP_FLOOR_2 && view_get_flip_parity(PARITY_FLOOR, dir, x, y)) ||
               (o->flip == SKY_FLIP_CEILING_2 && view_get_flip_parity(PARITY_CEILING, dir, x, y));
    int scale_x = o->scale_x, scale_y = o->scale_y;
    int16_t dx = o->dx, dy = o->dy;
    if (view.walking) {
        dx = gfx_scale_64(dx, WALK_SCALE);
        dy = gfx_scale_64(dy, WALK_SCALE);
        scale_x = gfx_scale_64((int16_t)scale_x, WALK_SCALE);
        scale_y = gfx_scale_64((int16_t)scale_y, WALK_SCALE);
        if (o->scale_x == SCALE_FULL) {
            dx = (int16_t)(dx + walk_view_rect[RECT_X]);
            dy = (int16_t)(dy + (o->zone == ZONE_SKY_HIGH ? WALK_SKY_HIGH_DY : walk_view_rect[RECT_Y]));
        } else {
            dy = (int16_t)(dy + WALK_SKY_DY);
        }
    }
    view_setup_picture(flip, scale_x, scale_y, 0, 0, 0, (int16_t)(o->zone | ZONE_RELATIVE), ZONE_NO_ANCHOR,
                       view.wall_key, NO_KEY, CAT_SKY, view.gfx_set, o->entry);
    view.ctx.src_x = (int16_t)(view.ctx.src_x + dx);
    view.ctx.src_y = (int16_t)(view.ctx.src_y + dy);
    view_flush_picture();
}

static void outdoor_draw_sky(int dir, int x, int y)
{
    if (game.weather_visible)
        for (const SkyObject *o = weather_sky_objects(); o->entry != SKY_OBJECT_END; o++)
            outdoor_draw_sky_object(o, dir, x, y);
    char text[WALL_TEXT_SIZE];
    if (view.landmark_set != view.gfx_set) {
        view.landmark_set = view.gfx_set;
        view.no_landmarks = 1;
        memset(view.landmarks, 0, sizeof view.landmarks);
        for (int e = 0; e < SKY_LANDMARK_MAX; e++)
            if (picture_exists(CAT_SKY, view.gfx_set, e) &&
                *gdat_text(gdat_current(), (GdatKey){CAT_SKY, view.gfx_set, e}, text, sizeof text)) {
                view.landmarks[e / BYTE_BITS] |= (uint8_t)(1 << (e % BYTE_BITS));
                view.no_landmarks = 0;
            }
    }
    if (view.no_landmarks)
        return;
    /* like the original, the landmarks use the first sky object as their record */
    SkyObject *o = weather_sky_objects();
    for (int e = 0; e < SKY_LANDMARK_MAX; e++) {
        if (!(view.landmarks[e / BYTE_BITS] & 1 << (e % BYTE_BITS)))
            continue;
        gdat_text(gdat_current(), (GdatKey){CAT_SKY, view.gfx_set, e}, text, sizeof text);
        if (!outdoor_place_sky_object(o, text, dir, x, y))
            continue;
        o->entry = (uint8_t)e;
        o->zone = (int16_t)gdat_text_int_field(text, "cd");
        o->flip = (uint8_t)gdat_text_int_field(text, "fw");
        outdoor_draw_sky_object(o, dir, x, y);
    }
}

/* ---- The view ---- */

static void view_draw_far_squares(void)
{
    uint32_t cell_mask = THINGS_ALL; /* like the original, kept from square to square */
    for (int i = 0; i < POS_DRAWN; i++) {
        int pos = far_order[i], aspect = view_squares[pos].aspect;
        uint16_t first = view_squares[pos].first_thing;
        if (aspect == ASPECT_WALL) { /* a creature can stand in a wall that is not solid */
            if (first != THING_ENDOFLIST)
                view_draw_creatures(first, pos, THINGS_ALL);
            view_draw_wall_square(pos);
            continue;
        }
        if (pos >= POS_FAR_ROW) {
            if (aspect == ASPECT_FLOOR || aspect == ASPECT_PIT || aspect == ASPECT_TELEPORTER)
                view_draw_floor_ornament(pos);
            else if (aspect != ASPECT_DOOR_FRONT)
                continue;
            if (first != THING_ENDOFLIST)
                view_draw_creatures(first, pos, THINGS_ALL);
            continue;
        }
        switch (aspect) {
        case ASPECT_DOOR_FRONT:
            view_draw_door_front(pos);
            continue;
        case ASPECT_STAIRS_FRONT:
            view_draw_stairs_front(pos);
            break;
        case ASPECT_STAIRS_SIDE:
            view_draw_stairs_side(pos);
            break;
        case ASPECT_PIT:
            view_draw_floor_pit(pos);
            __attribute__((fallthrough));
        case ASPECT_FLOOR:
        case ASPECT_TELEPORTER:
        case ASPECT_DOOR_SIDE:
            if (aspect == ASPECT_DOOR_SIDE && pos == POS_FRONT &&
                !aspect_value_key(CAT_DOOR, door_type(view_squares[POS_FRONT].side[SIDE_DOOR_THING]),
                                  DOOR_KEY_NO_FRAME)) {
                view_setup_picture(view.flip, DOOR_SIDE_SCALE, DOOR_SIDE_SCALE, 0, 0, DOOR_SIDE_DEPTH,
                                   zone_id_for_view_cell(POS_FRONT, CELL_FAR_MIDDLE, 0), ZONE_NO_ANCHOR, view.wall_key,
                                   NO_KEY, CAT_WALLSET, view.gfx_set, GFX_MAP_DOOR_SIDE);
                view_flush_picture();
            }
            cell_mask = view_draw_floor_ornament(pos) ? THINGS_FAR : THINGS_ALL;
            view_draw_ceiling_pit(pos);
            break;
        default:
            continue;
        }
        view_draw_square_things(pos, cell_mask, 1);
        if (aspect == ASPECT_TELEPORTER)
            view_draw_sparkle_field(pos, CAT_TELEPORTER, 0);
    }
}

static void view_draw_party_square(void)
{
    int aspect = view_squares[POS_PARTY].aspect, linked = 0;
    TeleporterLink link;
    if (aspect == ASPECT_DOOR_FRONT) {
        view_draw_door_front(POS_PARTY);
    } else {
        if (aspect == ASPECT_PIT) {
            view_draw_floor_pit(POS_PARTY);
        } else if (aspect == ASPECT_DOOR_SIDE) {
            if (!aspect_value_key(CAT_DOOR, door_type(view_squares[POS_PARTY].side[SIDE_DOOR_THING]),
                                  DOOR_KEY_NO_FRAME))
                view_draw_picture_in_zone(CAT_WALLSET, view.gfx_set, GFX_MAP_DOOR_SIDE,
                                          zone_id_for_view_cell(POS_PARTY, CELL_FAR_MIDDLE, 0), view.wall_key,
                                          view.flip);
        } else if (aspect == ASPECT_STAIRS_SIDE) {
            int up = view_squares[POS_PARTY].side[SIDE_STAIRS_UP] != 0;
            view_draw_picture_in_zone(CAT_WALLSET, view.gfx_set, up ? GFX_MAP_STAIRS_SIDE_UP : GFX_MAP_STAIRS_SIDE_DOWN,
                                      up ? ZONE_PARTY_STAIRS_UP : ZONE_PARTY_STAIRS_DOWN, view.wall_key, 0);
            view_draw_picture_in_zone(CAT_WALLSET, view.gfx_set,
                                      (up ? GFX_MAP_STAIRS_SIDE_UP : GFX_MAP_STAIRS_SIDE_DOWN) + 1,
                                      (up ? ZONE_PARTY_STAIRS_UP : ZONE_PARTY_STAIRS_DOWN) + 1, view.wall_key, 0);
        }
        view_draw_ceiling_pit(POS_PARTY);
        linked = dungeon_teleporter_link(view.map, view_squares[POS_PARTY].x, view_squares[POS_PARTY].y, &link);
        if (!linked)
            view_draw_floor_ornament(POS_PARTY);
        view_draw_square_things(POS_PARTY, THINGS_ALL, 0);
        if (aspect == ASPECT_TELEPORTER)
            view_draw_sparkle_field(POS_PARTY, CAT_TELEPORTER, 0);
    }
    weather_draw_rain();
    if (linked)
        view_draw_floor_ornament(POS_PARTY);
    view_draw_explosions(view_squares[POS_PARTY].first_thing, POS_PARTY, THINGS_ALL);
}

int view_position(int depth, int side)
{
    for (int pos = 0; pos < VIEW_SQUARE_COUNT; pos++)
        if (view_depth[pos] == depth && view_lateral[pos] == side)
            return pos;
    return -1;
}

unsigned view_draws(int pos, int aspect)
{
    if (pos < 0 || pos >= VIEW_SQUARE_COUNT)
        return 0;
    unsigned draws = wall_faces[pos];
    if (aspect == ASPECT_WALL)
        return draws;
    bool creatures = !(party.move_steps && pos == POS_PARTY); /* the walking view hides the party square creatures */
    if (pos >= POS_FAR_ROW) {
        bool floor = aspect == ASPECT_FLOOR || aspect == ASPECT_PIT || aspect == ASPECT_TELEPORTER;
        return pos < POS_DRAWN && (floor || aspect == ASPECT_DOOR_FRONT) ? draws | VIEW_DRAWS_CREATURES : draws;
    }
    if (aspect == ASPECT_DOOR_FRONT && !door_front_shown[pos])
        return draws;
    draws |= VIEW_DRAWS_FLOOR_ORNAMENT | VIEW_DRAWS_DOOR;
    if (!square_things_shown[pos])
        return draws;
    draws |= VIEW_DRAWS_OBJECTS | (creatures ? VIEW_DRAWS_CREATURES : 0);
    if (MISSILE_POSITIONS & POS_BIT(pos))
        draws |= VIEW_DRAWS_MISSILES;
    if (EXPLOSION_POSITIONS & POS_BIT(pos))
        draws |= VIEW_DRAWS_EXPLOSIONS;
    return draws;
}

bool view_shows_floor_ornament(int pos, int ornament)
{
    if (!(view_draws(pos, ASPECT_FLOOR) & VIEW_DRAWS_FLOOR_ORNAMENT) || ornament == ORNAMENT_NONE || !ornament)
        return false;
    return !((pos == POS_FAR_SIDE_1 || pos == POS_FAR_SIDE_2) &&
             aspect_value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_NOT_FAR_SIDE));
}

int view_shows_party(void)
{
    return view.party_x == game.party_x && view.party_y == game.party_y && view.party_map == game.party_map &&
           view.party_dir == game.party_dir;
}

void view_draw_dungeon(int dir, int x, int y)
{
    view.map = view.party_map = game.party_map;
    view.party_dir = dir;
    view.party_x = x;
    view.party_y = y;
    view.shade = cheat_light ? 0 : view_light_level * LIGHT_STEP; /* the creature rules keep view_light_level */
    view.walking = party.move_steps != 0;
    view_click_zone_count = VIEW_CLICK_ZONE_FIXED;
    for (int i = 0; i < VIEW_CLICK_ZONE_FIXED; i++) {
        view_click_zones[i].thing = THING_NONE;
        view_click_zones[i].kind = CLICK_NONE;
    }
    memset(view_creature_grid, 0, sizeof view_creature_grid);
    memset(view_object_grid, NO_POSITION, sizeof view_object_grid);
    view_clear_viewport();
    view_arrows_enabled = party.push_mode || party.push_pending_group != THING_NONE;
    input_update_viewport_arrow();
    for (int pos = VIEW_SQUARE_COUNT - 1; pos >= 0; pos--)
        view_load_square(x, y, pos, dir);
    int margins = -1;
    if (view_squares[POS_DEPTH2_LEFT].aspect == ASPECT_WALL && view_squares[POS_DEPTH2_CENTER].aspect == ASPECT_WALL &&
        view_squares[POS_DEPTH2_RIGHT].aspect == ASPECT_WALL)
        margins = MAP_KEY_FAR_MARGINS;
    if (view_squares[POS_FRONT_LEFT].aspect == ASPECT_WALL && view_squares[POS_FRONT].aspect == ASPECT_WALL &&
        view_squares[POS_FRONT_RIGHT].aspect == ASPECT_WALL)
        margins = MAP_KEY_NEAR_MARGINS;
    uint16_t m = margins < 0 ? 0 : aspect_value_key(CAT_WALLSET, view.gfx_set, margins);
    zone_set_clip_margins(0, 0, 0, LOW_BYTE(m));
    view_draw_picture_in_zone(CAT_WALLSET, view.gfx_set, GFX_MAP_CEILING, ZONE_CEILING, NO_KEY,
                              view_get_flip_parity(PARITY_CEILING, dir, x, y) != 0);
    zone_clip_rect[RECT_W] = 0;
    zone_set_clip_margins(0, HIGH_BYTE(m), 0, 0);
    view_draw_picture_in_zone(CAT_WALLSET, view.gfx_set, GFX_MAP_FLOOR, ZONE_FLOOR, NO_KEY,
                              view_get_flip_parity(PARITY_FLOOR, dir, x, y) != 0);
    zone_clip_rect[RECT_W] = 0;
    view.flip = view_get_flip_parity(PARITY_SQUARE, dir, x, y);
    outdoor_draw_sky(dir, x, y);
    view_unhide_linked_squares();
    view_mark_hidden_squares();
    view_draw_far_squares();
    view_draw_party_square();
    draw_free_temp();
}

/* ---- Clicks ---- */

/* True when the last picture has a pixel other than key at (x, y) of the viewport. */
static int view_hit_test_picture(int x, int y, int key)
{
    const DrawCtx *c = &view.ctx;
    const int16_t *r = c->rect;
    if (x < r[RECT_X] || x > r[RECT_X] + r[RECT_W] - 1 || y < r[RECT_Y] || y > r[RECT_Y] + r[RECT_H] - 1)
        return 0;
    int sx = c->flip & MIRROR_H ? r[RECT_X] + r[RECT_W] - 1 - x + c->src_x : x - r[RECT_X] + c->src_x;
    int sy = y - r[RECT_Y] + c->src_y;
    const Bitmap *image = c->image;
    if (image && image->height >= 2)
        view_hit_offset_y = (int16_t)((sy - (image->height >> 1)) * PERCENT / (image->height >> 1));
    if (!image || sx < 0 || sy < 0 || sx >= image->width || sy >= image->height)
        return 0;
    int pixel;
    if (image->bpp == IMAGE_8BPP) {
        pixel = image->pixels[sy * image->width + sx];
    } else {
        int i = sy * ((image->width + 1) & ~1) + sx;
        pixel = i & 1 ? image->pixels[i >> 1] & NIBBLE_MASK : image->pixels[i >> 1] >> NIBBLE_BITS;
    }
    return pixel != key;
}

#define HIT_SIDE_LIMIT 75 /* percent of the half picture width: a click beyond it hits a side */
#define HIT_DEPTH      1  /* the depth at which the creature in front is drawn for a hit test */

int view_hit_test_creature(int x, int y, int square_x, int square_y, int *side)
{
    uint16_t group = creature_get_group_at(square_x, square_y);
    if (group == THING_NONE)
        return 0;
    view_draw_creature(POS_FRONT, HIT_DEPTH, group);
    int key = view.ctx.transparent;
    view.ctx.transparent = DRAW_NO_BLIT;
    view_flush_picture();
    if (!view_hit_test_picture(x, y, key)) {
        draw_free_temp();
        return 0;
    }
    const DrawCtx *c = &view.ctx;
    const int16_t *r = c->rect;
    int sx = c->flip & MIRROR_H ? r[RECT_X] + r[RECT_W] - 1 - x + c->src_x : x - r[RECT_X] + c->src_x;
    int half = c->image ? c->image->width >> 1 : 0;
    int16_t offset = half ? (int16_t)((sx - c->x - half) * PERCENT / half) : 0;
    *side = offset < -HIT_SIDE_LIMIT ? 3 : offset <= HIT_SIDE_LIMIT ? 0 : 1;
    draw_free_temp();
    return 1;
}

#define HIT_SMALL_SIZE 32 /* a smaller ornament takes every click on its wall */

int view_hit_test_wall_ornament(int pos, int x, int y)
{
    static const int8_t face_of_pos[4] = {0, -1, 1, 0}; /* the face that the party sees of the walls at 0 to 3 */
    if (pos > POS_FRONT)
        return 0;
    ViewSquare saved = view_squares[pos];
    int sx = view.party_x, sy = view.party_y;
    move_relative(view.party_dir, view_offset[pos][1], view_offset[pos][0], &sx, &sy);
    view_squares[pos].x = (int8_t)sx;
    view_squares[pos].y = (int8_t)sy;
    aspect_get(&view_squares[pos], view.map, view.party_dir, sx, sy);
    int hit = 0, key = view_draw_wall_ornament(pos, face_of_pos[pos], 0);
    if (key >= 0)
        hit = view_hit_test_picture(x, y, key);
    if (pos == POS_FRONT || view.ctx.w < HIT_SMALL_SIZE || view.ctx.h < HIT_SMALL_SIZE)
        hit = 1;
    view_squares[pos] = saved;
    draw_free_temp();
    return hit;
}

/* States of a hit test on a chest. */
enum { CHEST_HIT_NONE, CHEST_HIT_OPEN = 2, CHEST_HIT_INSIDE = 3 };
#define CHEST_CAPACITY          8
#define HIT_KEY                 10
#define ATTR_SLOTS_NO_CONTAINER 0x8000

/* A click on a chest in the view that hit_state tells: it opens or closes, takes thing (placing), or gives its last
   item to the leader hand. Returns hit, THING_NONE when done, THING_ENDOFLIST when thing must drop. */
static uint16_t chest_click(uint16_t chest, uint16_t hit, int hit_state, uint16_t thing, int placing)
{
    switch (hit_state) {
    case CHEST_HIT_NONE:
        if (placing)
            return THING_ENDOFLIST;
        if (!container_is_open(chest))
            return hit;
        container_set_open(chest, false);
        break;
    case CHEST_HIT_OPEN:
        container_set_open(chest, true);
        break;
    case CHEST_HIT_INSIDE: {
        if (placing && item_attr(thing, ATTR_SLOTS) & ATTR_SLOTS_NO_CONTAINER)
            return THING_ENDOFLIST;
        uint16_t *contents = container_contents(chest);
        uint16_t last = *contents;
        int count = 0;
        for (uint16_t c = *contents; c != THING_ENDOFLIST; c = dungeon_next_thing(c), count++)
            last = c;
        if (placing) {
            if (count >= CHEST_CAPACITY)
                return THING_ENDOFLIST;
            dungeon_append_thing(thing, contents);
        } else if (count > 0) {
            dungeon_unlink_thing_from_list(last, contents);
            champ_put_object_in_leader_hand(last, 1);
        }
        break;
    }
    }
    return THING_NONE;
}

uint16_t view_hit_test_object(int x, int y, int pos, int square_x, int square_y, uint16_t thing, int kind, int placing)
{
    uint16_t group = THING_NONE, t;
    if (kind == CLICK_POSSESSIONS) {
        group = creature_group_at(party.cur_map, square_x, square_y);
        t = thing_word(group, GROUP_POSSESSIONS);
    } else {
        t = dungeon_square_first_thing(party.cur_map, square_x, square_y);
    }
    if (t == THING_ENDOFLIST)
        return placing ? thing : THING_NONE;
    int ordinal = kind == CLICK_ALCOVE ? ALCOVE_FIRST_ORDINAL : 0;
    uint16_t hit = THING_NONE, chest = THING_NONE;
    int hit_state = CHEST_HIT_NONE;
    for (; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (!is_item(t) || thing_cell(t) != thing_cell(thing))
            continue;
        int state = CHEST_HIT_NONE;
        uint16_t this_chest = THING_NONE;
        if (item_is_chest(t) &&
            gdat_exists(gdat_current(), CAT_CONTAINER, item_gdat_index(t), GDAT_TYPE_IMAGE, GFX_CHEST_OPEN_PICTURE)) {
            this_chest = t;
            if (container_is_open(t))
                state = CHEST_HIT_INSIDE;
            else if (!placing)
                state = CHEST_HIT_OPEN;
        }
        for (;;) {
            view_draw_object(t, pos, 0, 0, ordinal, group, 0, state, kind == CLICK_ALCOVE);
            if (view_hit_test_picture(x, y, HIT_KEY) == 1) {
                chest = this_chest;
                hit = t;
                hit_state = state;
                break;
            }
            if (!state)
                break;
            state = CHEST_HIT_NONE;
        }
        ordinal++;
        if (kind == CLICK_ALCOVE) {
            if (ordinal >= ALCOVE_ORDINAL_END)
                ordinal = ALCOVE_FIRST_ORDINAL;
        } else {
            ordinal &= JITTER_COUNT - 1;
        }
    }
    draw_free_temp();
    if (chest == THING_NONE)
        return hit != THING_NONE ? hit : thing;
    return chest_click(chest, hit, hit_state, thing, placing);
}
