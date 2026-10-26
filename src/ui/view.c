/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "view.h"
#include <stdlib.h>
#include <string.h>
#include "aspect.h"
#include "champion.h"
#include "creature.h"
#include "draw.h"
#include "dungeon.h"
#include "game.h"
#include "game_text.h"
#include "ids.h"
#include "input.h"
#include "item.h"
#include "le.h"
#include "money.h"
#include "move.h"
#include "projectile.h"
#include "random.h"
#include "save.h"
#include "sensor.h"
#include "sound_request.h"
#include "text.h"
#include "ui.h"
#include "weather.h"
#include "zone.h"

/* Value keys of two bytes: the first value in the high byte, the second in the low byte. */
#define LOW_BYTE(v)  ((v) & BYTE_MASK)
#define HIGH_BYTE(v) ((v) >> BYTE_BIT_COUNT)

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
#define DOOR_KEY_NO_FRAME        0x40
#define ORN_KEY_BIG_NEAR         0x60 /* floor ornament: scale 0x60 on the squares beside the party */
#define ORN_KEY_NOT_FAR_SIDE     0x61 /* floor ornament: not drawn at view positions 14 and 15 */
#define ORN_KEY_WET              0x63 /* drawn wet by the rain */
/* Wall ornament pictures of a frame. */
#define ORN_PICTURE_SIDE         0
#define ORN_PICTURE_FRONT        1
#define ORN_PICTURE_RIGHT        2
/* No picture entry in a table, and the value entry of the portrait place of a portrait ornament. */
#define ENTRY_NONE               0xff
#define PORTRAIT_OFFSET_ENTRY    0xfd

/* Zones of the view elements, and cells of their 5 x 5 grid. */
#define ZONE_ROW_SIZE     4  /* zones of one ornament row */
#define ZONE_CELL_GRID    25 /* zones of the 5 x 5 grid of a view element */
#define CELL_FAR_MIDDLE   2  /* the middle cell of the far row */
#define CELL_LEFT_MIDDLE  10
#define CELL_RIGHT_MIDDLE 14
/* The cells of a square, as the party sees them. */
#define CELL_FRONT_LEFT   0
#define CELL_FRONT_RIGHT  1
#define CELL_BACK_RIGHT   2
#define CELL_BACK_LEFT    3
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
/* No neighbour square in linked_squares. */
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
#define WALL_SHOWS_FRONT VIEW_DRAWS_FRONT_FACE
#define WALL_SHOWS_SIDE  VIEW_DRAWS_SIDE_FACE
#define WALL_SHOWS_BOTH  (WALL_SHOWS_FRONT | WALL_SHOWS_SIDE)
static const uint8_t wall_faces[VIEW_SQUARE_COUNT] = {
    0, 2, 2, 1, 3, 3, 1, 3, 3, 2, 2, 1, 3, 3, 3, 3, 1, 1, 1, 1, 1, 0, 0,
};
/* The neighbour squares of each position: left, right, ahead, behind (0xff: none). A seamless passage shows them
   again, and the possessions of a creature can lie on them. */
#define NEIGHBOUR_LEFT   0
#define NEIGHBOUR_RIGHT  1
#define NEIGHBOUR_AHEAD  2
#define NEIGHBOUR_BEHIND 3
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
/* The wall bits of a row, and the far squares that the corner walls hide. */
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
/* Door top frames by position. */
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
/* Glyphs of the wall text font after the letters: space and period. */
#define GLYPH_SPACE      0x1a
#define GLYPH_PERIOD     0x1b
/* The wall text font has 8 x 8 glyphs. */
#define GLYPH_SIZE       8
/* The walking view: floor and ceiling shift, and the shift of the sky. */
#define WALK_FLOOR_DY    3
#define WALK_CEILING_DY  (-2)
#define WALK_SKY_DY      (-3)
#define WALK_SKY_HIGH_DY 2

/* ---- Things in the view (SKULL.EXE tables 0x7168a, 0x75b6d to 0x75ec0) ---- */

/* GDAT categories and entries of things. */
#define CAT_ALCOVE_KEY     0xfe /* (category, 0xfe, 0x0c, entry): offset of an object picture of the category */
#define CREATURE_KEY_COLOR 0x04 /* creature value keys */
#define CREATURE_KEY_POSSESSION_LIFT 0x41
#define ORN_KEY_ALCOVE_ITEMS 0xfd /* (CAT_WALL_ORNAMENT, ornament, 0x0c, key): offset of the items in an alcove */
#define EXPLOSION_KEY_FLIP   0x00 /* explosion value keys: random mirror mask, fixed power, side flip of a missile */
#define EXPLOSION_KEY_FIXED_POWER  0x41
#define EXPLOSION_KEY_MISSILE_FLIP 0x01
/* The chests that open, and the transparent color of thing pictures. */
#define CHEST_KINDS                8  /* container GDAT indices below 8 are chests that open */
#define THING_KEY                  10 /* transparent color of thing pictures */
/* Animation bits of an active group (Step 16): a random shift of the picture and a mirror. */
#define ANIM_MIRROR                0x40
/* The shift of the vertical jitter in the animation bits. */
#define ANIM_JITTER_Y_SHIFT        3
/* Mirror bits of a creature frame: 2 bits per side, west side in bits 0-1. */
#define FRAME_MIRROR_MASK          0x03
#define FRAME_MIRROR_ALWAYS        0x01
#define FRAME_MIRROR_BY_ANIM       0x02 /* mirrored when the animation bits say so */
/* The shift of the mirror bits from one side to the next. */
#define FRAME_MIRROR_SHIFT         2
/* A moving creature shifts by its frame shift, sideways or forward. */
#define CREATURE_SHIFT_X           64
#define CREATURE_SHIFT_Y           7

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
#define EXPLOSION_SCALE_MIN        8
#define EXPLOSION_SCALE_MAX        64
#define EXPLOSION_TINT_LEVEL_SHIFT 5
#define EXPLOSION_TINT_LEVELS      3

/* Missiles: the lift above the floor, the scales, and the base power of the half power spell. */
#define MISSILE_DY               (-92) /* missiles fly above the floor */
#define MISSILE_SCALE_FULL       64
#define MISSILE_SCALE_MIN        8
#define MISSILE_HALF_POWER_BASE  128
/* The missile spell that shows half its power plus MISSILE_HALF_POWER_BASE. */
#define MISSILE_HALF_POWER_SPELL 0xff82

/* Thing grids: a cell of 4 x 4 per square, at (lateral + 10, forward) from the eye of the party. */
#define GRID_STRIDE         17
#define GRID_LATERAL_CENTER 10
#define GRID_LATERAL_LIMIT  10
#define GRID_FORWARD_LIMIT  17
#define PARTY_CELL_LAST     14 /* the party square draws the things of its 3 far rows */
#define JITTER_COUNT        16
#define ALCOVE_FIRST_SPOT   2 /* the jitter spots of the items in an alcove */
#define ALCOVE_SPOT_END     14
#define CELL_COLUMN_LAST    (CELL_GRID_SIDE - 1)
#define CELL_ROW_STEP       (CELL_GRID_SIDE * GRID_SQUARE_CELLS) /* cell offset of the square ahead or behind */
/* No view position in a thing grid cell. */
#define NO_POSITION         0xff

/* Possessions of a static creature (CreatureInfo.possessions). */
#define POSSESSIONS_CELLS_MASK 0x00f0
/* The last possession kind of the creatures that carry their possessions low. */
#define POSSESSIONS_LOW_KIND   1

/* Positions whose things the drawing shows; 9 and 10 show none, the explosions show to depth 4. */
static const uint8_t square_things_shown[POS_FAR_ROW] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1};
/* The view positions that show missiles and possessions, and explosions. */
#define MISSILE_POSITIONS   0x0000f9f9u /* positions 0, 3 to 8, 11 to 15: missiles and possessions */
#define EXPLOSION_POSITIONS 0x0007f9ffu /* positions 0 to 8, 11 to 18 */
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
/* The first thing_scale index of objects and of explosions. */
#define OBJECT_SCALE_START    2
#define EXPLOSION_SCALE_START 3
/* The scale of missiles by depth * 2 - (1 for the near cells). */
static const uint8_t missile_scale[7] = {0x40, 0x34, 0x2b, 0x23, 0x1c, 0x17, 0x13};
/* Small offsets that keep the objects of a cell apart: an x and a y index into jitter_offset by spot. */
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
/* Object pictures by the open state of a chest (closed, open, and the click states lid and inside): side view, front
   view. The original keeps rows 2 and 3 in the 4 bytes after its 2-row table (0x75d6a). */
static const uint8_t object_picture[4][2] = {
    {GFX_OBJECT_PICTURE_CLOSED, GFX_OBJECT_PICTURE_CLOSED + 1},
    {GFX_OBJECT_PICTURE_OPEN,   GFX_OBJECT_PICTURE_OPEN + 1  },
    {GFX_CHEST_OPEN_PICTURE,    GFX_CHEST_OPEN_PICTURE + 1   },
    {GFX_CHEST_INSIDE_PICTURE,  GFX_CHEST_INSIDE_PICTURE + 1 }
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
/* The explosion cells in drawing order: the 4 cells and the whole square. */
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
#define CREATURE_SIZE_MAX  3
/* The bits of the octant of a diagonal step toward the party. */
#define OCTANT_SAME_SHIFT  1
#define OCTANT_RIGHT_SHIFT 2
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

/* Rain: the sky state of the map graphics (CAT_WALLSET, graphics set, 0x0b, 0x66) tells if rain shows. */
static const uint8_t sky_state_rain[5] = {0, 1, 0, 0, 1};
/* The sky pictures of the rain. */
#define RAIN_ALONG         0x71 /* sky pictures 0x71 to 0x74: rain seen along the wind, 0x6d to 0x70 across it */
#define RAIN_ACROSS        0x6d
/* The tile rows of the rain and of a wet picture: the picture size minus a trim and a random spread. */
#define RAIN_TILE_TRIM     40 /* the rain tile row is the picture minus 40 pixels minus 0 to 31 */
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

ViewSquare g_view_squares[VIEW_SQUARE_COUNT];
static uint8_t viewport_pixels[VIEWPORT_WIDTH * VIEWPORT_HEIGHT];
Bitmap g_view_viewport = {viewport_pixels, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, IMAGE_8BPP};

/* Where and how a view picture shows: its mirror, scale, shift, depth shade, zone and anchor, and the transparent
   colors of its color remap. */
typedef struct {
    int flip, scale_x, scale_y, dx, dy, depth, zone, anchor, key, key2;
} PicturePlace;

/* How an object shows: lifted by dy, at a jitter spot that keeps the objects of a cell apart (-1 none), as a
   possession of the static creature group, with its click zone, as a chest in an open state, or in the alcove. */
typedef struct {
    int dy, spot;
    uint16_t group;
    bool record_click;
    int open_state;
    bool in_alcove;
    uint16_t projectile; /* the projectile that carries the object in flight, 0 for none */
} ObjectLook;

/* The eye of the party in the thing grid: the direction it looks to and its grid point. */
typedef struct {
    int dir;
    int16_t x, y;
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
    int entry, flip, mirrored;
} SidePicture;

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

static struct {
    int16_t map; /* the map of the squares that the view reads now */
    int16_t party_map;
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
    uint8_t landmarks[(SKY_LANDMARK_MAX + BYTE_BIT_COUNT - 1) / BYTE_BIT_COUNT];
    DrawCtx ctx;    /* the picture of setup_picture */
    DrawCtx alcove; /* the picture of the last alcove: its items are placed relative to it */
    int rain_shown; /* the map graphics show rain */
    struct {
        uint16_t first; /* possessions of a creature whose other cells are drawn later, THING_ENDOFLIST none */
        uint16_t group;
        uint32_t cells;
    } pending;
} view = {.landmark_set = -1, .pending.first = THING_ENDOFLIST};

ViewClickZone g_view_click_zones[VIEW_CLICK_ZONE_MAX];
int g_view_click_zone_count;
uint8_t g_view_creature_grid[VIEW_GRID_SIZE];
int g_view_arrows_enabled;
uint8_t g_view_object_grid[VIEW_GRID_SIZE];
uint32_t g_view_missile_masks[VIEW_SQUARE_COUNT];
uint32_t g_view_object_masks[VIEW_SQUARE_COUNT];

/* Smooth projectile flight of the advanced mode (no rule change): the projectiles of the last view of a game step and
   of the view of the game step before, and the timer tick of a view between game steps. */
static struct {
    FlightTrack tracks[FLIGHT_TRACK_MAX], last[FLIGHT_TRACK_MAX];
    int count, last_count;
    int16_t map, x, y, dir; /* the party view of the tracks */
    bool walking;
    uint32_t time;        /* the game time of the tracks */
    int tick, step_ticks; /* the timer tick of a view between game steps; 0 step_ticks: a view of a game step */
} flight = {.map = -1};

/* The picture entry offset of the frame of a wall face word: 4 pictures per frame. */
static int face_frame_entry(uint16_t w)
{
    return (int8_t)HIGH_BYTE(w);
}

static int picture_exists(int category, int index, int entry)
{
    return gdat_exists(gdat_current(), category, index, GDAT_TYPE_IMAGE, entry);
}

/* The door type of a door thing on the current map, 0xff for none. */
static uint8_t map_door_type(uint16_t door)
{
    return view.door_types[door_map_type(door)];
}

static void step_relative(int dir, int forward, int right, int *x, int *y)
{
    *x += g_dir_dx[dir & DIR_MASK] * forward + g_dir_dx[dir_turn(dir, TURN_RIGHT)] * right;
    *y += g_dir_dy[dir & DIR_MASK] * forward + g_dir_dy[dir_turn(dir, TURN_RIGHT)] * right;
}

/* ---- Map graphics ---- */

void view_enter_map(int16_t map)
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
static int get_flip_parity(int kind, int dir, int x, int y)
{
    int parity =
        (y + x + dir + dungeon_map_offset_x(view.map) + dungeon_map_offset_y(view.map) + dungeon_map_level(view.map)) &
        1;
    int party_parity = g_game.party_dir & 1, time = g_game.game_time & GAME_TIME_FRAMES;
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

static bool has_creature_picture(int type, int entry)
{
    return picture_exists(CAT_CREATURE, type, (uint8_t)entry);
}

/* The grid cell of a thing in its square for the party looking to dir. */
static int get_thing_position(uint16_t thing, int dir)
{
    int cell = CELL_CENTER;
    switch (thing_type(thing)) {
    case THING_GROUP:
        return aspect_creature_cell(thing, dir_relative(dir, group_dir(thing)));
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
static int position_to_cell(int position)
{
    for (int cell = 0; cell < DIR_COUNT; cell++)
        if (cell_view_position[cell] == position)
            return cell;
    return position == CELL_CENTER ? DIR_COUNT : -1;
}

void view_offset_toward_party(int16_t party_x, int16_t party_y, int16_t creature_size, int16_t *out_x, int16_t *out_y)
{
    if (!creature_size)
        return;
    if (creature_size > CREATURE_SIZE_MAX)
        creature_size = CREATURE_SIZE_MAX;
    int16_t cx = *out_x, cy = *out_y, step = toward_party_step[creature_size - 1];
    int16_t dx = (int16_t)abs((int16_t)(party_x - cx)), dy;
    if (!dx) {
        *out_y = (int16_t)(party_y < cy ? *out_y - step : *out_y + step);
        return;
    }
    dy = (int16_t)abs((int16_t)(cy - party_y));
    if (!dy) {
        *out_x = (int16_t)(party_x < cx ? *out_x - step : *out_x + step);
        return;
    }
    bool below = party_y < cy;
    bool right = party_x < cx;
    bool same = below == right;
    bool steep = same == (dx < dy);
    int octant = steep | same << OCTANT_SAME_SHIFT | right << OCTANT_RIGHT_SHIFT;
    *out_x = (int16_t)(*out_x + toward_party_diagonal[creature_size - 1][octant][0]);
    *out_y = (int16_t)(*out_y + toward_party_diagonal[creature_size - 1][octant][1]);
}

/* Records the things of the square (x, y) at pos for the party eye: the grid cells of the square, the creatures in
   the object grid, and the cells with missiles and objects. */
static void load_square_things(int pos, const ViewEye *eye, int x, int y)
{
    int dir = eye->dir;
    ViewSquare *s = g_view_squares + pos;
    g_view_missile_masks[pos] = g_view_object_masks[pos] = 0;
    if (pos < POS_FAR_ROW && s->aspect != ASPECT_WALL) {
        const uint8_t *corner = square_grid_corner[pos];
        for (int col = 0; col < CELL_GRID_SIDE; col++)
            for (int row = 0; row < GRID_SQUARE_CELLS; row++)
                g_view_creature_grid[(corner[0] + col) * GRID_STRIDE + corner[1] + row] = (uint8_t)pos;
    }
    uint16_t thing = s->first_thing;
    if (thing == THING_ENDOFLIST)
        return;
    int16_t eye_x = eye->x, eye_y = eye->y;
    for (; thing != THING_ENDOFLIST; thing = dungeon_next_thing(thing)) {
        int type = thing_type(thing);
        if (type == THING_PROJECTILE) {
            g_view_missile_masks[pos] |= 1u << get_thing_position(thing, dir);
        } else if (is_item(thing)) {
            /* a shop stand on a floor shows its objects itself */
            if (s->aspect != ASPECT_FLOOR || s->side[SIDE_SHOP_DISPLAY] != 0)
                g_view_object_masks[pos] |= 1u << get_thing_position(thing, dir);
        } else if (type == THING_GROUP) {
            int16_t cx, cy;
            /* Step 14 adds the cell of the push arrow for a creature in front of the party */
            aspect_cell_to_xy(x, y, (uint16_t)get_thing_position(thing, DIR_NORTH), &cx, &cy);
            if (pos != POS_PARTY)
                view_offset_toward_party(eye_x, eye_y,
                                         (int8_t)creature_info(gdat_current(), group_creature(thing))->size, &cx, &cy);
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
            uint8_t *column = g_view_object_grid + (lateral + GRID_LATERAL_CENTER) * GRID_STRIDE;
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
        if (g_view_squares[POS_FRONT].aspect == ASPECT_TELEPORTER) {
            s->aspect = ASPECT_TELEPORTER;
        } else {
            s->side[0] = s->side[1] = s->side[2] = s->side[3] = ORNAMENT_NONE;
            s->text = 0;
            s->first_thing = THING_ENDOFLIST;
        }
        for (int i = POS_NEAR_SIDE_1; i <= POS_NEAR_SIDE_2; i++) {
            ViewSquare *side = g_view_squares + i, *ahead = g_view_squares + linked_squares[i][NEIGHBOUR_AHEAD];
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
            g_view_missile_masks[i] = g_view_object_masks[i] = 0;
        }
    } else if (s->aspect == ASPECT_DOOR_FRONT) {
        s->aspect = ASPECT_FLOOR;
        s->square = 0;
        s->side[SIDE_FLOOR_ORNAMENT] = ORNAMENT_NONE;
    }
}

void view_load_square(int16_t x, int16_t y, int pos, int16_t dir)
{
    int square_x = x, square_y = y;
    step_relative(dir, view_offset[pos][1], view_offset[pos][0], &square_x, &square_y);
    ViewSquare *s = g_view_squares + pos;
    s->x = (int8_t)square_x;
    s->y = (int8_t)square_y;
    aspect_get(s, view.map, dir, square_x, square_y);
    if (view.walking && pos == POS_PARTY)
        load_walking_party_square(s);
    ViewEye eye = {.dir = dir};
    aspect_cell_to_xy(x, y, eye_cell[dir], &eye.x, &eye.y);
    load_square_things(pos, &eye, square_x, square_y);
}

void view_unhide_linked_squares(void)
{
    TeleporterLink link;
    for (int pos = 0; pos < POS_LINKED; pos++) {
        ViewSquare *s = g_view_squares + pos;
        if (s->aspect != ASPECT_FLOOR || !dungeon_teleporter_link(view.map, s->x, s->y, &link))
            continue;
        for (int k = 0; k < 4; k++) {
            uint8_t other = linked_squares[pos][k];
            /* fix: the original reads far past the view squares for the missing neighbours */
            if (other != LINK_NONE && g_view_squares[other].aspect == ASPECT_HIDDEN)
                g_view_squares[other].aspect = ASPECT_WALL;
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
        if (g_view_squares[pos].aspect == ASPECT_WALL)
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
        if (hidden & POS_BIT(pos) && g_view_squares[pos].aspect == ASPECT_WALL)
            g_view_squares[pos].aspect = ASPECT_HIDDEN;
}

/* ---- Pictures ---- */

int zone_id_for_view_cell(int base, int grid_cell, int16_t dir)
{
    return base * ZONE_CELL_GRID + aspect_rotate_cell(grid_cell, dir) + ZONE_VIEW_CELLS;
}

static int element_zone(int element, int offset, int use_table)
{
    return use_table ? side_ornament_zone[element] + offset : element * ZONE_CELL_GRID + ZONE_VIEW_ELEMENTS + offset;
}

/* The scale (1/64) of a to b, rounded. */
static int rounded_scale(int a, int b)
{
    return ((a << SCALE_ROUND_SHIFT) / b + 1) >> 1;
}

/* The color remap of a picture at depth: the depth shading table of the map, dimmed by the view shade. */
static void get_depth_shading(uint8_t *palette, int depth, int key1, int key2, int16_t *count)
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
static void setup_picture(const PicturePlace *p, int category, int index, int entry)
{
    DrawCtx *c = draw_ctx_init(&view.ctx, category, index, entry);
    int scale_x = p->scale_x, depth = p->depth;
    if (scale_x != p->scale_y && (depth == DEPTH_MIDDLE || depth == DEPTH_FAR)) {
        uint16_t v = aspect_value_key(category, index, depth == DEPTH_MIDDLE ? ORN_KEY_RATIO_2 : ORN_KEY_RATIO_3);
        if (LOW_BYTE(v))
            scale_x = rounded_scale((int)HIGH_BYTE(v), LOW_BYTE(v)); /* fix: the original divides by 0 */
    }
    c->flip = (int16_t)p->flip;
    c->scale_x = (int16_t)scale_x;
    c->scale_y = (int16_t)p->scale_y;
    c->dx = (int16_t)(c->dx + p->dx);
    c->dy = (int16_t)(c->dy + p->dy);
    c->zone = (int16_t)p->zone;
    c->anchor = (int16_t)p->anchor;
    c->dst = &g_view_viewport;
    c->transparent = (int16_t)p->key;
    get_depth_shading(c->palette, depth, p->key, p->key2, &c->palette_count);
    draw_ctx_prepare(c);
}

static void flush_picture(void)
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
static void add_click_zone(const int16_t *rect, int kind, int cell)
{
    if (g_view_click_zone_count >= VIEW_CLICK_ZONE_MAX)
        return; /* fix: the original has no limit */
    ViewClickZone *z = &g_view_click_zones[g_view_click_zone_count++];
    memcpy(z->rect, rect, sizeof z->rect);
    z->kind = (uint8_t)kind;
    z->cell = (uint8_t)cell;
    z->thing = THING_NONE;
}

/* The smallest click zone of a picture. */
#define CLICK_MIN_WIDTH  14
#define CLICK_MIN_HEIGHT 11
#define CLICK_LOW_HEIGHT 15 /* a lower picture moves its zone up to its middle */
#define CLICK_HALF_SIZE  7  /* a narrow or low zone starts this far before the middle of its picture */

/* Sets click zone index to the rectangle of the last picture (at least 14 x 11 pixels), or widens it. */
static void set_click_zone(int index, uint16_t thing, int cell, int kind)
{
    ViewClickZone *z = &g_view_click_zones[index];
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

/* The colors of a 4 bpp picture dimmed by the view shade, with key kept; NULL for an 8 bpp picture in full dark. */
static const uint8_t *get_shaded_palette(int category, int index, int entry, int key)
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

/* Copies a bitmap mirrored left to right. */
static void copy_mirrored(const Bitmap *src, Bitmap *dst)
{
    gfx_blit_image(src, dst, MIRROR_H);
}

/* Draws a picture of the map graphics into a zone of the view, dimmed by the view shade. */
static void show_map_picture(int entry, int zone, int key, int flip)
{
    DrawCtx c;
    draw_ctx_init(&c, CAT_WALLSET, view.gfx_set, entry);
    pal_apply_dim_remap(c.palette, (uint8_t)view.shade, key, NO_KEY, &c.palette_count);
    c.transparent = (int16_t)key;
    c.dst = &g_view_viewport;
    c.flip = (int16_t)flip;
    if (view.walking && (zone == ZONE_CEILING || zone == ZONE_FLOOR))
        c.src_y = (int16_t)(c.src_y + (zone == ZONE_CEILING ? WALK_CEILING_DY : WALK_FLOOR_DY));
    c.zone = (int16_t)zone;
    draw_ctx_prepare(&c);
    draw_ctx_draw(&c);
}

/* Prepares the picture of setup_picture with the mirror, scale, depth and key of p into an offscreen bitmap of its
   size, for pictures that are put together before they go into the view; p->key2 is the transparent color of the
   offscreen picture. Returns the bitmap, and the context in ctx. */
static Bitmap *render_offscreen(DrawCtx *ctx, const PicturePlace *p, int category, int index, int entry)
{
    setup_picture(&(PicturePlace){.flip = p->flip,
                                  .scale_x = p->scale_x,
                                  .scale_y = p->scale_y,
                                  .depth = p->depth,
                                  .zone = ZONE_NONE,
                                  .anchor = ZONE_NO_ANCHOR,
                                  .key = p->key,
                                  .key2 = NO_KEY},
                  category, index, entry);
    *ctx = view.ctx;
    ctx->transparent = (int16_t)p->key2;
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
static void show_offscreen(DrawCtx *ctx, Bitmap *offscreen, int zone, int anchor, int key)
{
    ctx->transparent = (int16_t)key;
    ctx->dst = &g_view_viewport;
    ctx->zone = (int16_t)zone;
    ctx->anchor = (int16_t)anchor;
    ctx->palette_count = 0;
    draw_ctx_set_image(ctx, offscreen);
    draw_ctx_draw(ctx);
}

/* ---- Rain ---- */

/* The rain picture for the wind as the party sees it; *side is the wind direction relative to the party. */
static int rain_picture(int *side)
{
    *side = dir_relative(g_game.wind_dir, view.party_dir);
    int level = g_game.rain_intensity >= RAIN_STRONG   ? 3
                : g_game.rain_intensity >= RAIN_MEDIUM ? 2
                : g_game.rain_intensity >= RAIN_LIGHT  ? 1
                                                       : 0;
    return (*side == DIR_NORTH || *side == DIR_SOUTH ? RAIN_ALONG : RAIN_ACROSS) + level;
}

bool view_rain_shows(void)
{
    return g_game.rain_intensity && view.rain_shown;
}

/* Draws the rain over the view: a random part of the rain picture, tiled. */
static void show_rain(void)
{
    if (!view_rain_shows())
        return;
    int16_t rect[RECT_SIZE], w = VIEWPORT_WIDTH, h = VIEWPORT_HEIGHT;
    if (!zone_compute_rect(NULL, rect, ZONE_WALL, &w, &h))
        return;
    int side, entry = rain_picture(&side);
    const Bitmap *rain = draw_gdat_image(CAT_SKY, view.gfx_set, entry);
    if (!rain)
        return;
    if (side == DIR_EAST) {
        Bitmap *mirrored = draw_temp_bitmap(rain->width, rain->height, rain->bpp);
        if (!mirrored)
            return;
        copy_mirrored(rain, mirrored);
        rain = mirrored;
    }
    const uint8_t *palette = get_shaded_palette(CAT_SKY, view.gfx_set, entry, 0);
    int offset = rand_next() & RANDOM_BYTE;
    int width = (int16_t)(rain->width * rain->height - RAIN_TILE_TRIM - (rand_next() & RANDOM_TILE_SPREAD));
    TileFill fill = {.tile = rain->pixels, .tile_width = width, .tile_offset = offset, .colormap = palette};
    gfx_blit_tiled_4to8(&fill, g_view_viewport.pixels, VIEWPORT_WIDTH, rect);
}

void view_draw_wet_image(const DrawCtx *ctx, Bitmap *offscreen, int color_key)
{
    int transparent = mapped_key(ctx, color_key);
    if (!g_game.rain_intensity) {
        gfx_fill_bitmap(offscreen, transparent);
        return;
    }
    int side, entry = rain_picture(&side);
    const Bitmap *rain = draw_gdat_image(CAT_SKY, view.gfx_set, entry);
    Bitmap *drops = rain ? draw_temp_bitmap(ctx->rect[RECT_W], rain->height, IMAGE_4BPP) : NULL;
    const uint8_t *shaded = get_shaded_palette(CAT_SKY, view.gfx_set, entry, 0);
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

/* Draws the sparkles of a teleporter or of a sparkle explosion over the square at pos: the sparkle picture of
   (category, index), tiled from a random place, through the mask picture of the square. */
static void show_sparkle_field(int pos, int category, int index)
{
    if (pos >= POS_FAR_ROW)
        return;
    const uint8_t *field = sparkle_field[pos];
    int16_t rect[RECT_SIZE], w = field[SPARKLE_WIDTH], h = field[SPARKLE_HEIGHT];
    if (!zone_compute_rect(NULL, rect, ZONE_WALL + pos, &w, &h))
        return;
    const uint8_t *mask = NULL;
    if (field[SPARKLE_MASK_PICTURE] != SPARKLE_MASK_NONE) {
        const Bitmap *picture = draw_gdat_image(category, index, field[SPARKLE_MASK_PICTURE] & SPARKLE_MASK_ENTRY);
        if (!picture)
            return;
        mask = picture->pixels;
        if (field[SPARKLE_MASK_PICTURE] & SPARKLE_MASK_MIRRORED) {
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
    const uint8_t *palette = get_shaded_palette(category, index, GFX_SPARKLE_PICTURE, 0);
    int offset = rand_next() & RANDOM_BYTE;
    int width = (int16_t)((rand_bit() + field[SPARKLE_TILE]) * SPARKLE_TILE_STEP);
    TileFill fill = {
        .tile = sparkles->pixels, .tile_width = width, .tile_offset = offset, .mask = mask, .colormap = palette};
    gfx_blit_tiled_4to8(&fill, g_view_viewport.pixels, VIEWPORT_WIDTH, rect);
}

/* Moves a cell of the square at *pos by columns and rows (rows toward the far side); the cell can go onto a
   neighbour square. False when it leaves the view. */
static bool shift_object_cell(int *pos, int *cell, int columns, int rows)
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
            return false;
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
            return false;
    }
    *pos = p;
    *cell = c;
    return true;
}

/* The picture of an object in cell of the square at pos: the side or the front view, by the open state of a chest,
   mirrored by the side of the view axis. */
static ThingPicture pick_object_picture(uint16_t thing, int pos, int cell, int open_state, bool in_alcove)
{
    uint8_t category = item_gdat_category(thing), index = item_gdat_index(thing);
    bool chest = category == CAT_CONTAINER && index < CHEST_KINDS;
    if (chest && !open_state)
        open_state = container_is_open(thing);
    ThingPicture pic = {.entry = object_picture[open_state][0], .flip = 0};
    int lateral = view_lateral[pos];
    if (lateral != 0) {
        pic.flip = chest && lateral == 1;
    } else {
        int column = cell % CELL_GRID_SIDE;
        if ((column == CELL_GRID_SIDE / 2 || in_alcove) &&
            picture_exists(category, index, object_picture[open_state][1]))
            pic.entry = object_picture[open_state][1];
        else
            pic.flip = chest && !in_alcove && column > CELL_GRID_SIDE / 2;
    }
    return pic;
}

/* The track of the projectile thing on view square pos in a list of tracks, NULL for none; the cell bits of a
   projectile change in flight. A track on a view square that is not pos or next to it belongs to a freed projectile
   record that a new projectile took. */
static const FlightTrack *find_track(const FlightTrack *list, int count, uint16_t thing, int pos)
{
    for (int i = 0; i < count; i++)
        if (list[i].thing == (thing & THING_ID_MASK) && abs(view_depth[list[i].pos] - view_depth[pos]) <= 1 &&
            abs(view_lateral[list[i].pos] - view_lateral[pos]) <= 1)
            return &list[i];
    return NULL;
}

/* The top left corner of the view picture without the clip of its zone; false when it does not show. */
static bool picture_corner(ViewPoint *corner)
{
    const DrawCtx *c = &view.ctx;
    int16_t rect[RECT_SIZE];
    int16_t sx = (int16_t)(c->src_x + c->dx);
    int16_t sy = (int16_t)(c->src_y + c->dy);
    if (c->w <= 0 || c->h <= 0 || !c->image ||
        !zone_compute_anchored_rect(c->image, rect, (AnchoredZone){c->zone | ZONE_RELATIVE, c->anchor}, &sx, &sy))
        return false;
    corner->x = (int16_t)(rect[RECT_X] - sx);
    corner->y = (int16_t)(rect[RECT_Y] - sy);
    return true;
}

/* Draws the view picture of the projectile thing (0: a picture of no projectile) on view square pos. A view of a game
   step keeps its corner; a view between game steps draws it part of the way back to its corner of the game step
   before. */
static void flush_projectile(uint16_t thing, int pos)
{
    ViewPoint corner;
    if (thing && picture_corner(&corner)) {
        if (flight.step_ticks) {
            const FlightTrack *t = find_track(flight.tracks, flight.count, thing, pos);
            ViewPoint shift = t ? view_flight_shift(t->from, t->to, flight.tick, flight.step_ticks) : (ViewPoint){0, 0};
            view.ctx.src_x = (int16_t)(view.ctx.src_x + shift.x);
            view.ctx.src_y = (int16_t)(view.ctx.src_y + shift.y);
        } else if (flight.count < FLIGHT_TRACK_MAX) {
            const FlightTrack *last = find_track(flight.last, flight.last_count, thing, pos);
            flight.tracks[flight.count++] = (FlightTrack){thing & THING_ID_MASK, pos, last ? last->to : corner, corner};
        }
    }
    flush_picture();
}

/* Draws an object at its cell of the square at pos, as look tells. */
static void show_object(uint16_t thing, int pos, const ObjectLook *look)
{
    const gdat_t *g = gdat_current();
    int cell = CELL_CENTER, row, zone, dx = 0, dy = look->dy, spot = look->spot;
    uint16_t group = look->group;
    if (group != THING_NONE) {
        int type = group_creature(group);
        bool low = (creature_info(g, type)->possessions & POSSESSIONS_KIND_MASK) <= POSSESSIONS_LOW_KIND;
        int16_t lift = (int16_t)gdat_entry_index(g, CAT_CREATURE, type, GDAT_TYPE_VALUE, CREATURE_KEY_POSSESSION_LIFT);
        dy -= lift ? lift : possession_lift[low];
        int side = dir_relative(thing_cell(thing), view.party_dir);
        /* Step 14 adds the cell of the push arrow for a creature in front of the party */
        if (!shift_object_cell(&pos, &cell, possession_column_shift[low][side], possession_row_shift[low][side]))
            return;
    } else if (!look->in_alcove) {
        cell = get_thing_position(thing, view.party_dir);
    }
    int depth = view_depth[pos];
    if (look->in_alcove) {
        row = 0;
        uint16_t place =
            gdat_entry_index(g, view.alcove.category, view.alcove.index, GDAT_TYPE_VALUE2, ORN_KEY_ALCOVE_ITEMS);
        dx = view.alcove.dx + gdat_offset_x(place) + jitter_offset[jitter[spot][0]];
        dy = gdat_offset_y(place) + view.alcove.dy + jitter_offset[jitter[spot][1]];
        zone = view.alcove.zone;
    } else {
        row = CELL_COLUMN_LAST - cell / CELL_GRID_SIDE;
        if (pos == POS_PARTY && row < 2)
            return; /* the near rows of the party square are below the view */
        zone = zone_id_for_view_cell(pos, cell, 0) | ZONE_RELATIVE;
    }
    uint8_t category = item_gdat_category(thing), index = item_gdat_index(thing);
    ThingPicture pic = pick_object_picture(thing, pos, cell, look->open_state, look->in_alcove);
    if (spot != -1) {
        dx += jitter_offset[jitter[spot][0]];
        if (group == THING_NONE)
            dy += jitter_offset[jitter[spot][1]];
    }
    uint16_t offset = gdat_entry_index(g, category, CAT_ALCOVE_KEY, GDAT_TYPE_VALUE2, pic.entry);
    if (offset) {
        dx += gdat_offset_x(offset);
        dy += gdat_offset_y(offset);
    }
    int scale = thing_scale[depth * GRID_SQUARE_CELLS + row + OBJECT_SCALE_START];
    setup_picture(&(PicturePlace){.flip = pic.flip,
                                  .scale_x = scale,
                                  .scale_y = scale,
                                  .dx = (int16_t)dx,
                                  .dy = (int16_t)dy,
                                  .depth = depth,
                                  .zone = (int16_t)zone,
                                  .key = THING_KEY,
                                  .key2 = NO_KEY},
                  category, index, pic.entry);
    if (!look->record_click)
        view.ctx.transparent = DRAW_NO_BLIT;
    flush_projectile(look->projectile, pos);
}

/* Draws the objects of a thing list in cell (0 to 3, relative to the party) of the square at pos; with group,
   they are the possessions of that static creature. */
static void show_cell_objects(uint16_t thing, int pos, int cell, uint16_t group)
{
    if (thing == THING_ENDOFLIST || view_depth[pos] > DEPTH_FAR)
        return;
    const CreatureInfo *info = group != THING_NONE ? creature_info(gdat_current(), group_creature(group)) : NULL;
    int spot = 0;
    for (; thing != THING_ENDOFLIST; thing = dungeon_next_thing(thing)) {
        if (!is_item(thing) || dir_relative(thing_cell(thing), view.party_dir) != cell)
            continue;
        if (info && !(info->possessions & STATIC_POSSESSIONS_SHOWN) && !(info->possessions & POSSESSIONS_CELLS_MASK)) {
            /* a creature that hides what it holds: one click zone for all of it */
            if (g_view_click_zone_count < VIEW_CLICK_ZONE_MAX) {
                ViewClickZone *z = &g_view_click_zones[g_view_click_zone_count++];
                if (!zone_get_rect(info->possession_zone, z->rect))
                    memset(z->rect, 0, sizeof z->rect);
                z->kind = CLICK_POSSESSIONS;
                z->thing = thing;
                z->cell = (uint8_t)pos;
            }
            return;
        }
        show_object(thing, pos, &(ObjectLook){.spot = spot, .group = group, .record_click = true});
        spot = (spot + 1) % JITTER_COUNT;
        if (info) {
            if (creature_cell_mask(info->possessions) >> cell & 1)
                set_click_zone(cell + 4, thing, pos, CLICK_POSSESSIONS);
        } else if (pos == POS_PARTY || (pos == POS_FRONT && cell > CELL_FRONT_RIGHT)) {
            int16_t x = g_view_squares[pos].x, y = g_view_squares[pos].y;
            uint16_t g = creature_find_group_near_point(&x, &y, thing_cell(thing));
            if (g == THING_NONE || creature_is_flying(g) ||
                creature_group_info(g)->possessions & POSSESSIONS_FLOOR_CLICKABLE)
                set_click_zone(cell, thing, pos, CLICK_OBJECT);
        }
    }
}

/* The picture of the animation frame of a creature of type seen from side, its mirror, and whether the picture of
   the other side shows. */
static SidePicture pick_side_picture(int type, const uint8_t *frame, int side, int anim)
{
    SidePicture pic;
    int entry = frame[FRAME_ENTRY + side];
    pic.flip = pic.mirrored = 0;
    if (!has_creature_picture(type, entry)) {
        int back = dir_opposite(side);
        pic.flip = pic.mirrored = back & 1;
        entry = frame[FRAME_ENTRY + back];
        if (!has_creature_picture(type, entry)) {
            pic.flip = 0;
            entry = frame[FRAME_ENTRY + DIR_SOUTH];
        }
    } else {
        int bits = frame[FRAME_MIRROR] >> ((DIR_WEST - side) * FRAME_MIRROR_SHIFT) & FRAME_MIRROR_MASK;
        if (bits & FRAME_MIRROR_ALWAYS || (anim & ANIM_MIRROR && bits & FRAME_MIRROR_BY_ANIM))
            pic.flip = 1;
    }
    if (!has_creature_picture(type, entry)) {
        int saved = pic.flip;
        pic.flip = 0;
        entry = (uint8_t)(GFX_CREATURE_SIDE + side);
        if (!has_creature_picture(type, entry)) {
            int other = (uint8_t)(GFX_CREATURE_SIDE + dir_opposite(side));
            if (side & 1 && has_creature_picture(type, other)) {
                pic.flip = pic.mirrored = 1;
                entry = other;
            } else {
                entry = GFX_CREATURE_ANY_SIDE;
                pic.flip = saved;
            }
        }
        if (pic.flip != saved)
            pic.mirrored = 1;
    }
    pic.entry = entry;
    return pic;
}

/* Draws a creature group of the square at pos at depth: its animation frame, from the side it shows. */
static void show_creature(int pos, int depth, uint16_t group)
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
                                                                    : dir_relative(view.party_dir, group_dir(group));
    SidePicture pic = pick_side_picture(type, frame, side, anim);
    int entry = pic.entry, flip = pic.flip, mirrored = pic.mirrored;
    int cell = still[FRAME_CELL], scale;
    if (pos == POS_FRONT && g_view_arrows_enabled) {
        /* by push arrow: the cell and scale of the creature that the party pushes */
        static const uint8_t arrow_cells[7] = {2, 0x0e, 0x16, 0x16, 0x16, 0x0a, 0x0c};
        static const uint8_t arrow_scales[7] = {0x34, 0x40, 0x4e, 0x4e, 0x4e, 0x40, 0x40};
        cell = (int8_t)arrow_cells[g_party.push_arrow];
        scale = arrow_scales[g_party.push_arrow];
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
    setup_picture(
        &(PicturePlace){.flip = flip,
                        .scale_x = scale,
                        .scale_y = scale,
                        .dx = jitter_offset[anim & ANIM_JITTER] + dx,
                        .dy = jitter_offset[anim >> ANIM_JITTER_Y_SHIFT & ANIM_JITTER] + dy,
                        .depth = depth,
                        .zone = (int16_t)zone,
                        .anchor = ZONE_NO_ANCHOR,
                        .key = (int16_t)gdat_entry_index(g, CAT_CREATURE, type, GDAT_TYPE_VALUE, CREATURE_KEY_COLOR),
                        .key2 = NO_KEY},
        CAT_CREATURE, type, entry);
}

/* Draws the possessions of a static creature in the cells of cell_mask; the other cells wait for
   show_pending_possessions. */
static void show_possessions(uint16_t first, int pos, uint32_t cell_mask, uint16_t group)
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
            show_cell_objects(first, pos, position_to_cell(cells[k]), group);
        else
            missing |= bit;
    }
    if (missing) {
        view.pending.first = first;
        view.pending.group = group;
        view.pending.cells = missing;
    }
}

/* Draws the creature groups of a thing list at the square at pos; true when it drew one. */
static bool show_creatures(uint16_t thing, int pos, uint32_t cell_mask)
{
    bool drawn = false;
    int depth = view_depth[pos];
    if (view.walking && pos == POS_PARTY)
        return false;
    for (; thing != THING_ENDOFLIST; thing = dungeon_next_thing(thing)) {
        if (thing_type(thing) != THING_GROUP)
            continue;
        const CreatureInfo *info = creature_info(gdat_current(), group_creature(thing));
        if (cell_mask & THINGS_STATIC_ONLY && !(info->flags & CREATURE_STATIC))
            continue;
        drawn = true;
        show_creature(pos, depth, thing);
        flush_picture();
        if (!(info->flags & CREATURE_STATIC)) {
            group_set_mood_flag(thing, GROUP_SEEN, true);
        } else if (info->possessions & POSSESSIONS_KIND_MASK && depth <= DEPTH_FAR) {
            if (g_view_squares[pos].aspect == ASPECT_DOOR_FRONT)
                cell_mask = THINGS_NEAR; /* like the original, kept */
            show_possessions(thing_word(thing, GROUP_POSSESSIONS), pos, cell_mask, thing);
        }
    }
    return drawn;
}

/* Draws the possessions that show_possessions left for later, at the square at pos. */
static void show_pending_possessions(int pos)
{
    if (view.pending.first != THING_ENDOFLIST)
        show_possessions(view.pending.first, pos, view.pending.cells, view.pending.group);
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

/* The picture of a missile of picture kind in cell (0 to 3, relative to the party) of the square at pos, and its
   mirror: by its flight direction and the parity of its square. */
static ThingPicture missile_picture(uint16_t thing, int kind, int cell, int pos, bool spell)
{
    int dir = projectile_dir(thing), lateral = view_lateral[pos];
    int square_parity = (g_view_squares[pos].x + g_view_squares[pos].y) & 1;
    ThingPicture pic = {.flip = 0};
    if ((dir & 1) != (view.party_dir & 1)) { /* the missile flies across the view */
        pic.entry = GFX_MISSILE_PICTURE_SIDE;
        if (kind == MISSILE_MIRRORED) {
            if (cell == CELL_FRONT_LEFT || cell == CELL_BACK_LEFT)
                pic.flip |= MIRROR_H;
            if (square_parity)
                pic.flip |= MIRROR_V;
            else
                pic.flip ^= MIRROR_H;
        } else if (dir_turn(view.party_dir, TURN_RIGHT) == dir) {
            pic.flip = MIRROR_H;
        }
        return pic;
    }
    if (kind == MISSILE_MIRRORED) {
        if (square_parity)
            pic.flip = MIRROR_V;
        pic.entry =
            (cell < CELL_BACK_RIGHT) == (square_parity != 0) ? GFX_MISSILE_PICTURE_FRONT : GFX_MISSILE_PICTURE_FRONT_2;
    } else if (kind == MISSILE_FRONT || (kind == MISSILE_FRONT_BACK && dir != view.party_dir)) {
        pic.entry = GFX_MISSILE_PICTURE_FRONT;
    } else {
        pic.entry = GFX_MISSILE_PICTURE_BACK;
    }
    if (lateral < 0 || (lateral == 0 && cell != CELL_FRONT_RIGHT && cell != CELL_BACK_RIGHT))
        pic.flip |= MIRROR_H;
    if (cell & 1 && spell)
        pic.flip |= MIRROR_V;
    return pic;
}

/* Draws the missiles of a thing list at grid cell position of the square at pos. */
static void show_missiles(uint16_t thing, int pos, int position)
{
    int cell = position_to_cell(position);
    if (thing == THING_ENDOFLIST || cell < 0)
        return;
    int depth = view_depth[pos];
    if ((depth == 0 && cell >= CELL_BACK_RIGHT) || !(MISSILE_POSITIONS & POS_BIT(pos)))
        return;
    for (; thing != THING_ENDOFLIST; thing = dungeon_next_thing(thing)) {
        if (get_thing_position(thing, view.party_dir) != position || thing_type(thing) != THING_PROJECTILE)
            continue;
        int zone = zone_id_for_view_cell(pos, position, 0);
        int kind = missile_picture_kind(thing);
        uint16_t payload = thing_word(thing, PROJECTILE_PAYLOAD);
        if (kind == MISSILE_NO_PICTURE) {
            uint16_t object = thing_with_cell(payload, thing_cell(thing));
            show_object(
                object, pos,
                &(ObjectLook){
                    .dy = MISSILE_DY, .spot = -1, .group = THING_NONE, .record_click = true, .projectile = thing});
            continue;
        }
        int category = item_gdat_category(thing), index = item_gdat_index(thing);
        bool spell = category == CAT_EXPLOSION;
        int power = projectile_energy(thing);
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
        ThingPicture pic = {.entry = GFX_MISSILE_PICTURE_FRONT, .flip = 0};
        if (kind != MISSILE_NO_SIDE)
            pic = missile_picture(thing, kind, cell, pos, spell);
        int flips =
            spell ? gdat_entry_index(gdat_current(), CAT_EXPLOSION, index, GDAT_TYPE_VALUE, EXPLOSION_KEY_MISSILE_FLIP)
                  : MIRROR_HV;
        setup_picture(&(PicturePlace){.flip = flips & pic.flip,
                                      .scale_x = scale,
                                      .scale_y = scale,
                                      .depth = depth,
                                      .zone = (int16_t)(zone | ZONE_RELATIVE),
                                      .key = THING_KEY,
                                      .key2 = NO_KEY},
                      category, index, pic.entry);
        view.ctx.src_y = (int16_t)(view.ctx.src_y + gfx_scale_64(MISSILE_DY, (int16_t)offset_scale));
        flush_projectile(thing, pos);
    }
}

/* Starts the picture of an explosion of power size into zone, mirrored by flip. */
static void setup_explosion_picture(int type, int size, int flip, int zone)
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
    c->dst = &g_view_viewport;
    c->flip = (int16_t)flip;
    c->scale_x = c->scale_y = (int16_t)scale;
    pal_apply_dim_remap(c->palette, (uint8_t)view.shade, THING_KEY, NO_KEY, &c->palette_count);
    draw_ctx_prepare(c);
}

/* Adds 1 to a global byte variable of the game, up to 255. */
static void global_byte_add(int var)
{
    uint8_t *value = g_game.global_bytes + var - GLOBAL_BYTE_FIRST;
    if (flight.step_ticks)
        return; /* a view between game steps changes no game state */
    if (*value < GLOBAL_BYTE_MAX)
        ++*value;
}

/* Tints the whole view with an explosion on the party square: a random part of its tint picture, tiled. */
static void show_explosion_tint(int type, int power)
{
    int level = power >> EXPLOSION_TINT_LEVEL_SHIFT;
    int entry = GFX_EXPLOSION_TINT + (level ? (level > EXPLOSION_TINT_LEVELS ? 2 : 1) : 0);
    const Bitmap *tint = draw_gdat_image(CAT_EXPLOSION, type, entry);
    if (!tint)
        return;
    int area = tint->width * tint->height;
    const uint8_t *palette = get_shaded_palette(CAT_EXPLOSION, type, entry, 0);
    int16_t rect[RECT_SIZE];
    int offset = (int16_t)rand_below((uint16_t)(area - RAIN_TILE_TRIM));
    int width = (int16_t)(area - RAIN_TILE_TRIM + (rand_next() & RANDOM_TILE_SPREAD));
    TileFill fill = {.tile = tint->pixels, .tile_width = width, .tile_offset = offset, .colormap = palette};
    gfx_blit_tiled_4to8(&fill, g_view_viewport.pixels, VIEWPORT_WIDTH, zone_get_rect(ZONE_DIALOG_BACK, rect));
}

/* Moves the picture of a strong explosion up, and sideways when it is not centered: it grows and rises. order is
   the place of its cell in the explosion order. */
static void raise_explosion(int power, bool near, int depth, bool centered, int order)
{
    int16_t grow = (int16_t)(power - EXPLOSION_POWER_BIG);
    if (grow > 0) {
        grow = (int16_t)(EXPLOSION_GROW_BASE - grow);
        if (near && view_depth[POS_PARTY] == depth)
            grow = EXPLOSION_GROW_NEAR;
        view.ctx.src_y = (int16_t)(view.ctx.src_y + VIEWPORT_HEIGHT * 8 / grow);
        if (!centered) {
            int16_t spread = (int16_t)(VIEWPORT_WIDTH * 8 / (grow - EXPLOSION_SPREAD_MIN));
            view.ctx.src_x = (int16_t)(view.ctx.src_x + (order == 1 || order == 4 ? -spread : spread));
        }
    }
}

/* Draws the explosion thing at the square at pos, when its cell is the cell at order in the explosion order and in
   cell_mask; a sparkle explosion draws its sparkle field at the last order. */
static void show_explosion(uint16_t thing, int pos, int order, uint32_t cell_mask)
{
    int order_cell = explosion_cell_order[view.party_dir][order], depth = view_depth[pos];
    int type = explosion_type(thing);
    bool centered = explosion_is_centered(thing);
    if (picture_exists(CAT_EXPLOSION, type, GFX_SPARKLE_PICTURE)) {
        if (order == EXPLOSION_ORDER_LAST)
            show_sparkle_field(pos, CAT_EXPLOSION, type);
        return;
    }
    if (order_cell != (centered ? -1 : thing_cell(thing)))
        return;
    int flip =
        rand_2bits() & gdat_entry_index(gdat_current(), CAT_EXPLOSION, type, GDAT_TYPE_VALUE, EXPLOSION_KEY_FLIP);
    int power = gdat_entry_index(gdat_current(), CAT_EXPLOSION, type, GDAT_TYPE_VALUE, EXPLOSION_KEY_FIXED_POWER)
                    ? POWER_MAX
                    : explosion_power(thing);
    bool near = pos == POS_PARTY;
    if (near) {
        if (type != EXPLOSION_GLOBAL_COUNTER) {
            show_explosion_tint(type, power);
            return;
        }
        global_byte_add(GLOBAL_EXPLOSIONS_NEAR);
    } else {
        if (type == EXPLOSION_GLOBAL_COUNTER)
            global_byte_add(GLOBAL_EXPLOSIONS_AWAY);
        if (depth == 0 && order >= 3)
            return;
    }
    int position = centered ? CELL_CENTER : cell_view_position[thing_cell(thing)];
    int turned = aspect_rotate_cell(position, view.party_dir);
    if (!(cell_mask & 1u << turned))
        return;
    int scale = thing_scale[depth * GRID_SQUARE_CELLS + EXPLOSION_SCALE_START - turned / CELL_GRID_SIDE];
    int zone = zone_id_for_view_cell(pos, position, g_game.party_dir) | ZONE_RELATIVE;
    int strength = power + 1 > EXPLOSION_POWER_SIZE ? power + 1 : EXPLOSION_POWER_SIZE;
    setup_explosion_picture(type, (int16_t)(scale * strength >> EXPLOSION_SIZE_SHIFT), flip, zone);
    raise_explosion(power, near, depth, centered, order);
    view.ctx.src_x = gfx_scale_64(view.ctx.src_x, (int16_t)scale);
    view.ctx.src_y = gfx_scale_64(view.ctx.src_y, (int16_t)scale);
    flush_picture();
    if (near)
        show_explosion_tint(type, power);
}

/* Draws the explosions of a thing list at the square at pos, cell by cell from the far cells. */
static void show_explosions(uint16_t first, int pos, uint32_t cell_mask)
{
    if (first == THING_ENDOFLIST || !(EXPLOSION_POSITIONS & POS_BIT(pos)))
        return;
    for (int k = 0; k < EXPLOSION_ORDER_COUNT; k++)
        for (uint16_t thing = first; thing != THING_ENDOFLIST; thing = dungeon_next_thing(thing))
            if (thing_type(thing) == THING_EXPLOSION)
                show_explosion(thing, pos, k, cell_mask);
}

/* Draws the things of the square at pos in the cells of cell_mask: objects, creatures, missiles, and with
   with_explosions the explosions. */
static void show_square_things(int pos, uint32_t cell_mask, int with_explosions)
{
    if (pos >= POS_FAR_ROW || !square_things_shown[pos])
        return;
    const ViewSquare *s = g_view_squares + pos;
    uint16_t first = s->first_thing;
    const uint8_t *order = thing_cell_order[view_lateral[pos] < 0 ? 0 : view_lateral[pos] == 0 ? 1 : 2];
    const uint8_t *corner = square_grid_corner[pos];
    int last = pos == POS_PARTY ? PARTY_CELL_LAST : CELL_GRID_COUNT - 1;
    for (int i = 0; i <= last; i++) {
        int cell = order[i];
        uint32_t bit = cell_mask & 1u << cell;
        if (!bit)
            continue;
        if (bit & g_view_object_masks[pos])
            show_cell_objects(first, pos, position_to_cell(cell), THING_NONE);
        int grid =
            (corner[0] + cell % CELL_GRID_SIDE) * GRID_STRIDE + corner[1] + GRID_SQUARE_CELLS - cell / CELL_GRID_SIDE;
        uint8_t owner = g_view_object_grid[grid];
        if ((s->aspect == ASPECT_WALL || g_view_creature_grid[grid] == pos) && owner != NO_POSITION) {
            uint32_t mask = cell == CELL_CENTER ? cell_mask : cell_mask & ~THINGS_STATIC_ONLY;
            if (show_creatures(g_view_squares[owner].first_thing, owner, mask)) {
                g_view_object_grid[grid] = NO_POSITION;
                /* the missiles of the cells before, which the creature covered */
                if (g_view_missile_masks[pos] && i >= 7)
                    for (int j = i - 7; j < i; j++)
                        if (g_view_missile_masks[pos] & 1u << order[j])
                            show_missiles(first, pos, order[j]);
            }
        }
        if (bit & g_view_missile_masks[pos])
            show_missiles(first, pos, cell);
    }
    if (with_explosions)
        show_explosions(first, pos, cell_mask);
}

/* Draws the items in an alcove of the front wall at pos, and the explosions in it. */
static void show_alcove_contents(int pos)
{
    int spot = ALCOVE_FIRST_SPOT, depth = view_depth[pos];
    for (uint16_t thing = g_view_squares[pos].first_thing; thing != THING_ENDOFLIST;
         thing = dungeon_next_thing(thing)) {
        if (thing_cell(thing) != dir_opposite(g_game.party_dir))
            continue;
        if (is_item(thing)) {
            show_object(thing, pos,
                        &(ObjectLook){.spot = spot, .group = THING_NONE, .record_click = true, .in_alcove = true});
            if (++spot >= ALCOVE_SPOT_END)
                spot = ALCOVE_FIRST_SPOT;
            if (pos == POS_FRONT)
                set_click_zone(VIEW_CLICK_ZONE_ALCOVE, thing, POS_FRONT, CLICK_ALCOVE);
            continue;
        }
        if (thing_type(thing) != THING_EXPLOSION || !(ALCOVE_EXPLOSION_POSITIONS & POS_BIT(pos)))
            continue;
        int type = explosion_type(thing), flip = rand_2bits();
        int zone = element_zone(pos, CELL_CENTER, 0);
        if (type == EXPLOSION_ALCOVE_STILL) {
            int scale = alcove_explosion_scale[depth];
            setup_picture(&(PicturePlace){.flip = flip,
                                          .scale_x = scale,
                                          .scale_y = scale,
                                          .zone = zone,
                                          .anchor = ZONE_NO_ANCHOR,
                                          .key = THING_KEY,
                                          .key2 = NO_KEY},
                          CAT_EXPLOSION, type, GFX_ALCOVE_STILL_PICTURE);
        } else if (type == EXPLOSION_ALCOVE_SCALED) {
            setup_explosion_picture(type, alcove_explosion_size[depth], flip, zone);
        } else {
            continue;
        }
        flush_picture();
    }
}

/* ---- Walls ---- */

/* The glyph of a wall text character in the wall text font: a letter, a period, or a space. */
static uint8_t wall_text_glyph(uint8_t c)
{
    if (c >= 'A' && c <= 'Z')
        return (uint8_t)(c - 'A');
    return c == '.' ? GLYPH_PERIOD : GLYPH_SPACE;
}

/* Shops: a wall sensor (SENSOR_WALL_TEXT with a shop ornament) shows the item of the display sensor (SENSOR_SHOP)
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

int16_t g_view_hit_offset_y;

static void record_target(uint16_t record, int *x, int *y)
{
    *x = sensor_target_x(record);
    *y = sensor_target_y(record);
}

/* True when the target square of the sensor record holds an item of the item index type: in the possessions of a
   creature, or loose when loose is set. */
static bool target_has_item(uint16_t record, int type, int loose)
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
static void show_centered_label(int x, int y, const char *text)
{
    int16_t width, height, count = UI_COLOR_COUNT;
    if (!text_measure(text, &width, &height))
        return;
    uint8_t colors[UI_COLOR_COUNT];
    memcpy(colors, g_ui_colors, sizeof colors);
    pal_apply_dim_remap(colors, view.shade, NO_KEY, NO_KEY, &count);
    text_draw_with_shadow(&g_view_viewport, x - (width >> 1), y,
                          (TextColors){colors[UI_WHITE], colors[UI_BLACK] | TEXT_TRANSPARENT}, text);
}

/* The price table of a shop ornament and its word count in *count; NULL when the ornament has none. */
static const uint8_t *price_table(uint8_t ornament, int *count)
{
    int item = gdat_find(gdat_current(), CAT_WALL_ORNAMENT, ornament, GDAT_TYPE_TABLE, GFX_ORN_SHOP_TABLE);
    uint32_t size = 0;
    const uint8_t *data = item < 0 ? NULL : gdat_item(gdat_current(), item, &size);
    *count = (int)(size / 2);
    return data;
}

/* Chooses the item that the display shows: the next or the last item of the price list that the merchant sells. */
static void choose_shop_item(uint16_t shop, uint16_t display, uint8_t ornament, int creature)
{
    bool forward = sensor_flag(shop, SHOP_STEP_FORWARD);
    uint16_t item = sensor_data(display);
    uint8_t set[SHOP_CLASS_SET_SIZE];
    int list = sensor_delay(shop); /* the delay bits of a shop: its price list */
    if (money_parse_item_class_set(creature, list * SHOP_CLASS_TEXT_STEP + SHOP_CLASS_TEXT, set, 0)) {
        int count, at = item;
        const uint8_t *table = price_table(ornament, &count);
        if (table)
            for (at = 0; at < count && (le16(table + 2 * at) & ITEM_INDEX_NONE) != item; at++) {
            }
        /* fix: the original loops for ever when the merchant sells no item of the list */
        for (int tries = 0; tries <= ITEM_INDEX_NONE; tries++) {
            at += forward || g_view_hit_offset_y < 0 ? 1 : -1;
            if (table) {
                if (at < 0)
                    at = count - 1;
                else if (at >= count)
                    at = 0;
                item = le16(table + 2 * at) & ITEM_INDEX_NONE;
            } else {
                at &= ITEM_INDEX_NONE;
                item = (uint16_t)at;
            }
            if (!(set[item / BYTE_BIT_COUNT] & 1 << (item % BYTE_BIT_COUNT)))
                continue;
            if (gdat_entry_index(gdat_current(), item_index_gdat_category(item), item_index_gdat_index(item),
                                 GDAT_TYPE_VALUE, ATTR_FLAGS) &
                    ITEM_FLAG_UNIQUE &&
                !target_has_item(shop, item, 1) && !target_has_item(display, item, 0) &&
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
static void show_shop_price(int amount, uint8_t ornament, const PicturePlace *p, int offset_key)
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
    setup_picture(&(PicturePlace){.scale_x = SCALE_FULL,
                                  .scale_y = SCALE_FULL,
                                  .depth = p->depth,
                                  .zone = ZONE_NONE,
                                  .anchor = p->anchor,
                                  .key = NO_KEY,
                                  .key2 = NO_KEY},
                  CAT_WALL_ORNAMENT, ornament, GFX_ORN_SHOP_COINS);
    if (!view.ctx.image)
        return;
    int16_t rect[RECT_SIZE] = {0, 0, COIN_WIDTH, COIN_HEIGHT};
    for (int i = 0; i < count; i++, rect[RECT_X] += COIN_WIDTH)
        ui_blit_bitmap(&(BlitSource){.bitmap = view.ctx.image, .x = coins[i] * COIN_WIDTH, .transparent = NO_KEY},
                       strip, rect);
    if (zone_compute_anchored_rect(strip, rect, (AnchoredZone){p->zone | ZONE_RELATIVE, p->anchor}, &x, &y))
        ui_blit_bitmap(&(BlitSource){.bitmap = strip, .transparent = p->key, .colormap = view.ctx.palette},
                       &g_view_viewport, rect);
}

/* Draws the name and the sell and buy prices of the shop item in front of the party. */
static void show_shop_label(uint16_t shop, uint16_t item, int creature, uint8_t ornament, const PicturePlace *p)
{
    int category = item_index_gdat_category(item), index = item_index_gdat_index(item);
    int16_t lx, ly;
    zone_get_position(p->zone, &lx, &ly);
    uint16_t place =
        gdat_entry_index(gdat_current(), CAT_WALL_ORNAMENT, ornament, GDAT_TYPE_VALUE2, ORN_KEY_SHOP_LABEL);
    char raw[TEXT_SIZE], name[TEXT_SIZE];
    show_centered_label(lx + gdat_offset_x(place), ly + gdat_offset_y(place),
                        text_format(text_gdat(category, index, GFX_ITEM_NAME, raw), name));
    int list = sensor_delay(shop);
    PriceTerms buying = {creature, list, true, true};
    PriceTerms selling = {creature, list, false, true};
    int buy = money_get_item_value(item, &buying, -1);
    int sell = money_get_item_value(item, &selling, -1);
    show_shop_price(sell, ornament, p, ORN_KEY_SHOP_SELL);
    show_shop_price(buy, ornament, p, ORN_KEY_SHOP_BUY);
}

/* Draws the shop display of the wall sensor shop with the shop ornament: the item, its name and prices in front. */
static void show_shop_display(uint16_t shop, uint8_t ornament, int pos, const PicturePlace *p)
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
    /* a view between game steps changes no game state: it shows the item of the last view */
    if (!flight.step_ticks && (!sensor_flag(shop, SHOP_SHOWN) || sensor_flag(shop, SHOP_STEP_FORWARD)))
        choose_shop_item(shop, display, ornament, creature);
    uint16_t item = sensor_data(display);
    int category = item_index_gdat_category(item), index = item_index_gdat_index(item);
    uint16_t offset =
        gdat_entry_index(gdat_current(), CAT_WALL_ORNAMENT, ornament, GDAT_TYPE_VALUE2, ORN_KEY_ALCOVE_ITEMS);
    PicturePlace item_place = *p;
    item_place.flip = 0;
    item_place.dx = gdat_offset_x(offset);
    item_place.dy = gdat_offset_y(offset);
    item_place.key2 = NO_KEY;
    setup_picture(&item_place, category, index, 0);
    flush_picture();
    if (!target_has_item(shop, item, 1) && !target_has_item(display, item, 0)) {
        int count, at = 0;
        const uint8_t *table = price_table(ornament, &count);
        if (table)
            for (; at < count && (le16(table + 2 * at) & ITEM_INDEX_NONE) != item; at++) {
            }
        uint16_t made = !flight.step_ticks && table && at < count && le16(table + 2 * at) & SHOP_TABLE_CREATE
                            ? item_create_by_index(item)
                            : THING_NONE;
        if (made != THING_NONE) {
            dungeon_link_thing_to_square(made, view.map, x, y);
        } else {
            setup_picture(&item_place, CAT_WALL_ORNAMENT, ornament, GFX_ORN_SHOP_EMPTY);
            flush_picture();
        }
    }
    PicturePlace overlay = item_place;
    overlay.dx = overlay.dy = 0;
    setup_picture(&overlay, CAT_WALL_ORNAMENT, ornament, GFX_ORN_OVERLAY);
    flush_picture();
    if (pos != DISPLAY_POS)
        return;
    show_shop_label(shop, item, creature, ornament, p);
}

/* Blits the lines of a wall text with the map font glyph, each line centered in width, from the row of r. */
static void blit_wall_text(Blit *glyph, const char *text, int16_t *r, int width)
{
    for (const char *p = text;;) {
        int n = 0;
        while (p[n] && p[n] != '\n')
            n++;
        if (n) {
            r[RECT_X] = (int16_t)((width >> 1) - ((int16_t)(GLYPH_SIZE * n) >> 1));
            for (int i = 0; i < n && r[RECT_X] >= 0; i++, r[RECT_X] += GLYPH_SIZE) {
                glyph->src_x = wall_text_glyph((uint8_t)p[i]) * GLYPH_SIZE;
                gfx_blit(glyph, r);
            }
            p += n;
        }
        r[RECT_Y] = (int16_t)(r[RECT_Y] + GLYPH_SIZE + WALL_TEXT_LINE_GAP);
        if (!*p++)
            break;
    }
}

/* Draws the text of a front wall plate: the text goes into a bitmap of the plate size, centered, then into the view. */
static void show_wall_text(int pos, int entry, const PicturePlace *place)
{
    DrawCtx plate;
    draw_ctx_init(&plate, CAT_WALLSET, view.gfx_set, entry);
    const Bitmap *plate_image = draw_gdat_image(CAT_WALLSET, view.gfx_set, entry);
    const Bitmap *font = draw_gdat_image(CAT_WALLSET, view.gfx_set, GFX_MAP_FONT);
    if (!plate_image || !font)
        return;
    char text[WALL_TEXT_SIZE];
    text_decode_raw(g_view_squares[pos].text, 0, text, sizeof text);
    int lines = 1;
    for (const char *p = text; *p; p++)
        lines += *p == '\n';
    int line_height = GLYPH_SIZE + WALL_TEXT_LINE_GAP;
    Bitmap *bitmap = draw_temp_bitmap(plate_image->width, plate_image->height, IMAGE_4BPP);
    if (!bitmap)
        return;
    gfx_fill_bitmap(bitmap, place->key);
    const uint8_t *font_palette = draw_gdat_image_palette(CAT_WALLSET, view.gfx_set, GFX_MAP_FONT);
    int16_t r[RECT_SIZE] = {0, (int16_t)((plate_image->height >> 1) - (lines * line_height >> 1)), GLYPH_SIZE,
                            font->height};
    Blit glyph = {.src = font->pixels,
                  .dst = bitmap->pixels,
                  .src_width = font->width,
                  .dst_width = bitmap->width,
                  .src_bpp = font->bpp,
                  .dst_bpp = bitmap->bpp,
                  .transparent = place->key,
                  .mirror = MIRROR_NONE,
                  .colormap = font_palette};
    blit_wall_text(&glyph, text, r, plate_image->width);
    DrawCtx c;
    draw_ctx_init(&c, CAT_NONE, 0, 0);
    c.item = -1;
    draw_ctx_set_image(&c, bitmap);
    c.scale_x = (int16_t)place->scale_x;
    c.scale_y = (int16_t)place->scale_y;
    if (font_palette)
        memcpy(c.palette, font_palette, IMAGE_COLORS);
    c.palette_count = IMAGE_COLORS;
    get_depth_shading(c.palette, place->depth, place->key, NO_KEY, &c.palette_count);
    c.dst = &g_view_viewport;
    c.zone = (int16_t)place->zone;
    c.anchor = (int16_t)place->anchor;
    c.src_x = plate.dx;
    c.src_y = plate.dy;
    c.transparent = (int16_t)place->key;
    draw_ctx_prepare(&c);
    draw_ctx_draw(&c);
}

/* Draws the champion portrait of a portrait ornament on the front wall at pos; a click on the front wall finds it. */
static void show_wall_portrait(int pos, uint8_t ornament, const PicturePlace *p, int record_click)
{
    uint16_t offset =
        gdat_entry_index(gdat_current(), CAT_WALL_ORNAMENT, ornament, GDAT_TYPE_VALUE2, PORTRAIT_OFFSET_ENTRY);
    setup_picture(&(PicturePlace){.scale_x = p->scale_y,
                                  .scale_y = p->scale_y,
                                  .dx = gdat_offset_x(offset),
                                  .dy = gdat_offset_y(offset),
                                  .depth = p->depth,
                                  .zone = p->zone,
                                  .anchor = p->anchor,
                                  .key = NO_KEY,
                                  .key2 = NO_KEY},
                  CAT_CHAMPION, (uint8_t)g_view_squares[pos].text, GFX_PORTRAIT);
    if (!record_click)
        view.ctx.transparent = DRAW_NO_BLIT;
    flush_picture();
    if (record_click && p->depth == DEPTH_FRONT)
        add_click_zone(view.ctx.rect, CLICK_WALL_ORNAMENT, pos);
}

/* Draws a text plate on the wall face at side of the wall at pos, and the wall text on a front plate. */
static void show_wall_plate(int pos, int side, const PicturePlace *p, int record_click)
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
    setup_picture(&(PicturePlace){.flip = flip,
                                  .scale_x = p->scale_x,
                                  .scale_y = p->scale_y,
                                  .depth = p->depth,
                                  .zone = p->zone,
                                  .anchor = p->anchor,
                                  .key = p->key,
                                  .key2 = NO_KEY},
                  CAT_WALLSET, view.gfx_set, entry);
    if (!record_click)
        view.ctx.transparent = DRAW_NO_BLIT;
    flush_picture();
    if (side == 0 && g_view_squares[pos].text != THING_TEXT_NONE)
        show_wall_text(pos, entry, p);
}

/* Draws a wet wall ornament: its background picture, then the ornament with the rain over it. */
static void show_wet_wall_ornament(uint8_t ornament, int entry, const PicturePlace *p, int record_click,
                                   int16_t click_rect[RECT_SIZE])
{
    setup_picture(&(PicturePlace){.flip = p->flip,
                                  .scale_x = p->scale_x,
                                  .scale_y = p->scale_y,
                                  .depth = p->depth,
                                  .zone = p->zone,
                                  .anchor = p->anchor,
                                  .key = DRAW_NO_IMAGE,
                                  .key2 = DRAW_NO_IMAGE},
                  CAT_WALL_ORNAMENT, ornament, entry);
    setup_picture(&(PicturePlace){.flip = p->flip,
                                  .scale_x = p->scale_x,
                                  .scale_y = p->scale_y,
                                  .dx = view.ctx.dx,
                                  .dy = view.ctx.dy,
                                  .depth = p->depth,
                                  .zone = p->zone,
                                  .anchor = p->anchor,
                                  .key = p->key,
                                  .key2 = NO_KEY},
                  CAT_WALL_ORNAMENT, ornament, GFX_ORN_WET_BACKGROUND);
    if (!record_click)
        view.ctx.transparent = DRAW_NO_BLIT;
    flush_picture();
    DrawCtx c;
    Bitmap *offscreen = render_offscreen(
        &c,
        &(PicturePlace){.flip = p->flip,
                        .scale_x = p->scale_x,
                        .scale_y = p->scale_y,
                        .depth = p->depth,
                        .key = p->key,
                        .key2 = (int16_t)aspect_value_key(CAT_WALL_ORNAMENT, ornament, ORN_KEY_OFFSCREEN_COLOR)},
        CAT_WALL_ORNAMENT, ornament, entry);
    if (offscreen) {
        view_draw_wet_image(&c, offscreen, p->key);
        draw_ctx_draw(&c);
        show_offscreen(&c, offscreen, p->zone, p->anchor, mapped_key(&c, p->key));
    }
    memcpy(click_rect, c.rect, sizeof c.rect); /* the wet picture gives the click zone */
}

/* Draws what a front wall ornament of kind holds: the shop display, the overlay picture, or the items in an alcove. */
static void show_ornament_contents(int pos, uint8_t ornament, int kind, const PicturePlace *p, int record_click)
{
    view.alcove = view.ctx;
    if (picture_exists(CAT_WALL_ORNAMENT, ornament, GFX_ORN_OVERLAY)) {
        if (kind == KIND_SHOP) {
            show_shop_display(g_view_squares[pos].text, ornament, pos, p);
        } else {
            setup_picture(&(PicturePlace){.flip = p->flip,
                                          .scale_x = p->scale_x,
                                          .scale_y = p->scale_y,
                                          .depth = p->depth,
                                          .zone = p->zone,
                                          .anchor = p->anchor,
                                          .key = p->key,
                                          .key2 = NO_KEY},
                          CAT_WALL_ORNAMENT, ornament, GFX_ORN_OVERLAY);
            if (!record_click)
                view.ctx.transparent = DRAW_NO_BLIT;
            flush_picture();
        }
    } else if (kind == KIND_ALCOVE) {
        show_alcove_contents(pos);
    }
}

/* The picture entry of the wall ornament on the face at side (0 front, < 0 left, > 0 right) in frame; *flip is set
   when a right face shows the mirrored side picture. */
static int wall_ornament_entry(uint8_t ornament, int side, int frame, int *flip)
{
    int entry;
    if (side == 0) {
        entry =
            ORN_PICTURE_FRONT; /* front ornaments are not mirrored: the depth flip of SKULL.EXE is off (0x75c11 = 1) */
    } else {
        entry = ORN_PICTURE_SIDE;
        if (side > 0) {
            if (picture_exists(CAT_WALL_ORNAMENT, ornament, (uint8_t)(frame + ORN_PICTURE_RIGHT)))
                entry = ORN_PICTURE_RIGHT;
            else
                *flip = 1;
        }
    }
    return (uint8_t)(entry + frame);
}

/* Draws the ornament of the wall face at side (0 front, < 0 left, > 0 right) of the wall at pos; returns its color key. */
static int show_wall_ornament(int pos, int side, int record_click)
{
    int face = side < 0 ? 0 : side > 0 ? 2 : 1; /* the side words of the left, front, right faces */
    uint16_t w = g_view_squares[pos].side[face + 1];
    int frame = face_frame_entry(w), depth = view_depth[pos];
    uint8_t ornament = aspect_word_ornament(w);
    if (ornament == ORNAMENT_NONE)
        return -1;
    bool plate = ornament == ORNAMENT_PLATE;
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
    PicturePlace p = {.scale_x = depth_scale[depth],
                      .scale_y = depth_scale[depth],
                      .depth = depth,
                      .zone = element_zone(pos, cell, side != 0),
                      .anchor = anchor,
                      .key = key,
                      .key2 = NO_KEY};
    if (kind == KIND_PORTRAIT && side == 0) {
        show_wall_portrait(pos, ornament, &p, record_click);
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
        show_wall_plate(pos, side, &p, record_click);
        return key;
    }
    int16_t click_rect[RECT_SIZE] = {0};
    int entry = wall_ornament_entry(ornament, side, frame, &p.flip);
    if (side == 0 && aspect_value_key(CAT_WALL_ORNAMENT, ornament, ORN_KEY_WET)) {
        show_wet_wall_ornament(ornament, entry, &p, record_click, click_rect);
    } else {
        setup_picture(&p, CAT_WALL_ORNAMENT, ornament, entry);
        if (!record_click)
            view.ctx.transparent = DRAW_NO_BLIT;
        flush_picture();
        if (!record_click)
            return key;
        memcpy(click_rect, view.ctx.rect, sizeof click_rect);
    }
    if (record_click && kind != KIND_PORTRAIT && pos >= POS_NEAR_SIDE_1 && pos <= POS_FRONT)
        add_click_zone(click_rect, CLICK_WALL_ORNAMENT, pos);
    if (kind && side == 0)
        show_ornament_contents(pos, ornament, kind, &p, record_click);
    return key;
}

/* Draws the wall block at pos with the clip margins that open squares beside it need. */
static void show_wall_block(int pos)
{
    int lateral = view_lateral[pos], depth = view_depth[pos];
    uint16_t margins = aspect_value_key(CAT_WALLSET, view.gfx_set, MAP_KEY_WALL_MARGINS + (depth > DEPTH_FRONT));
    int left = 0, right = 0;
    if (depth > DEPTH_FRONT) {
        if (g_view_squares[POS_FRONT_LEFT].aspect == ASPECT_WALL)
            left = HIGH_BYTE(margins);
        if (g_view_squares[POS_FRONT_RIGHT].aspect == ASPECT_WALL)
            right = LOW_BYTE(margins);
    } else if (depth > 0) {
        if (g_view_squares[POS_NEAR_SIDE_1].aspect == ASPECT_WALL)
            left = HIGH_BYTE(margins);
        if (g_view_squares[POS_NEAR_SIDE_2].aspect == ASPECT_WALL)
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
    setup_picture(&(PicturePlace){.flip = flip,
                                  .scale_x = SCALE_FULL,
                                  .scale_y = SCALE_FULL,
                                  .depth = view.walking ? -depth : 0,
                                  .zone = ZONE_WALL + pos,
                                  .anchor = ZONE_NO_ANCHOR,
                                  .key = view.wall_key,
                                  .key2 = NO_KEY},
                  CAT_WALLSET, view.gfx_set, entry);
    flush_picture();
    g_zone_clip_rect[RECT_W] = 0;
}

/* Draws the wall square at pos: the wall block and the ornaments of the faces that the party sees. */
static void show_wall_square(int pos)
{
    show_wall_block(pos);
    switch (wall_faces[pos]) {
    case WALL_SHOWS_FRONT:
        show_wall_ornament(pos, 0, 1);
        break;
    case WALL_SHOWS_BOTH:
        show_wall_ornament(pos, 0, 1);
        __attribute__((fallthrough));
    case WALL_SHOWS_SIDE:
        show_wall_ornament(pos, view_lateral[pos], 1);
        break;
    }
}

/* ---- Floors, pits, stairs ---- */

/* Draws a floor ornament through an offscreen bitmap: wet with the rain, or over a passage (cleared). */
static void show_floor_ornament_offscreen(uint8_t ornament, int entry, const PicturePlace *p, int wet)
{
    DrawCtx c;
    Bitmap *offscreen = render_offscreen(
        &c,
        &(PicturePlace){.flip = p->flip,
                        .scale_x = p->scale_x,
                        .scale_y = p->scale_y,
                        .depth = p->depth,
                        .key = p->key,
                        .key2 = (int16_t)aspect_value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_OFFSCREEN_COLOR)},
        CAT_FLOOR_ORNAMENT, ornament, entry);
    if (!offscreen)
        return;
    int transparent = mapped_key(&c, p->key);
    if (wet)
        view_draw_wet_image(&c, offscreen, p->key);
    else
        gfx_fill_bitmap(offscreen, transparent);
    draw_ctx_draw(&c);
    show_offscreen(&c, offscreen, p->zone, p->anchor, transparent);
}

/* Draws the things of the squares behind the seamless passage link at pos and depth, on the linked map. */
static void show_linked_squares(const TeleporterLink *link, int pos, int depth)
{
    /* the squares behind the passage, on the linked map */
    int x = link->x, y = link->y, saved_dir = view.party_dir, saved_map = view.map;
    view.party_dir = link->target_side;
    view.map = link->map;
    step_relative(view.party_dir, -depth, 0, &x, &y);
    for (int d = POS_DEPTH_MAX; d >= depth; d--)
        view_load_square(x, y, depth_center[d], view.party_dir);
    for (int d = POS_DEPTH_MAX; d > depth; d--)
        show_square_things(depth_center[d], THINGS_ALL, 1);
    view.map = saved_map;
    view.party_dir = saved_dir;
    show_square_things(pos, THINGS_NEAR, 1);
}

/* Where and how the floor ornament in frame shows on the square at pos; *entry becomes the scaled picture when the
   ornament has no picture of its own there. */
static PicturePlace floor_ornament_place(int pos, uint8_t ornament, int frame, int *entry)
{
    int depth = view_depth[pos];
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
    if (!picture_exists(CAT_FLOOR_ORNAMENT, ornament, *entry)) {
        *entry = (uint8_t)(floor_ornament_scaled_entry[pos] + frame);
        scale = depth_scale[depth];
    }
    if ((pos == POS_NEAR_SIDE_1 || pos == POS_NEAR_SIDE_2) &&
        aspect_value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_BIG_NEAR))
        scale = SCALE_BIG_NEAR;
    PicturePlace p = {.flip = flip,
                      .scale_x = scale,
                      .scale_y = scale,
                      .depth = depth,
                      .zone = zone,
                      .anchor = anchor,
                      .key = key,
                      .key2 = NO_KEY};
    return p;
}

/* Draws the floor ornament of the square at pos. A seamless passage there shows the squares behind it.
   True for a passage that was drawn. */
static bool show_floor_ornament(int pos)
{
    int entry = floor_ornament_entry[pos];
    if (entry < 0)
        return false;
    ViewSquare *s = g_view_squares + pos;
    int depth = view_depth[pos];
    TeleporterLink link;
    int linked = dungeon_teleporter_link(view.map, s->x, s->y, &link);
    if (depth > DEPTH_FAR && !linked)
        return false;
    if (linked && pos != POS_NEAR_SIDE_1 && pos != POS_NEAR_SIDE_2 && link.side != g_game.party_dir &&
        link.side != dir_opposite(g_game.party_dir))
        return false;
    uint16_t w = s->side[SIDE_FLOOR_ORNAMENT];
    int frame = HIGH_BYTE(w);
    entry = (uint8_t)(entry + frame);
    uint8_t ornament = aspect_word_ornament(w);
    if (ornament == ORNAMENT_NONE || ornament == 0)
        return false;
    if ((pos == POS_FAR_SIDE_1 || pos == POS_FAR_SIDE_2) &&
        aspect_value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_NOT_FAR_SIDE))
        return false;
    PicturePlace p = floor_ornament_place(pos, ornament, frame, &entry);
    int wet = depth ? aspect_value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_WET) : 0;
    if (!depth || (!linked && !wet)) {
        setup_picture(&p, CAT_FLOOR_ORNAMENT, ornament, entry);
        flush_picture();
        return false;
    }
    PicturePlace no_image = p;
    no_image.key = no_image.key2 = DRAW_NO_IMAGE;
    setup_picture(&no_image, CAT_FLOOR_ORNAMENT, ornament, entry);
    if (view_lateral[pos] == 0) {
        PicturePlace background = p;
        background.dx = view.ctx.dx;
        background.dy = view.ctx.dy;
        setup_picture(&background, CAT_FLOOR_ORNAMENT, ornament, (uint8_t)(entry + GFX_ORN_WET_BACKGROUND));
        flush_picture();
        show_linked_squares(&link, pos, depth);
    }
    show_floor_ornament_offscreen(ornament, entry, &p, wet);
    return linked;
}

/* Draws the hole of an open pit on the level above. */
static void show_ceiling_pit(int pos)
{
    if (pos > POS_CEILING_PIT_LAST || !(view.flip_flags & FLIP_CEILING_PITS))
        return;
    int16_t x = g_view_squares[pos].x;
    int16_t y = g_view_squares[pos].y;
    int map = dungeon_location_on_map(view.map, -1, &x, &y);
    if (map < 0)
        return;
    /* fix: the original reads the square without a bounds check; outside the map it is a wall */
    uint8_t square = dungeon_square_edge(map, x, y);
    if (square_type(square) != SQUARE_PIT || !square_flag(square, SQUARE_PIT_OPEN))
        return;
    int flip = pos == POS_PARTY ? get_flip_parity(PARITY_CEILING, view.party_dir, view.party_x, view.party_y) != 0
                                : ceiling_pit_flip[pos];
    show_map_picture(ceiling_pit_entry[pos], ceiling_pit_zone[pos], view.wall_key, flip);
}

/* Draws the open pit on the floor of the square at pos. */
static void show_floor_pit(int pos)
{
    int16_t zone = floor_pit_zone[pos];
    if (zone < 0)
        return;
    bool up = g_view_squares[pos].side[SIDE_PIT_UP] != 0;
    if (pos >= POS_FLOOR_PIT_BLOCKED && up)
        return;
    int flip = pos == POS_PARTY ? get_flip_parity(PARITY_FLOOR, view.party_dir, view.party_x, view.party_y) != 0
                                : floor_pit_flip[pos];
    show_map_picture(up ? floor_pit_entry_up[pos] : floor_pit_entry[pos], zone, view.wall_key, flip);
}

/* Draws the stairs at pos as the party sees them from the side. */
static void show_stairs_side(int pos)
{
    bool up = g_view_squares[pos].side[SIDE_STAIRS_UP] != 0;
    int16_t zone = stairs_side_zone[pos][up];
    if (zone < 0)
        return;
    uint8_t entry = stairs_side_entry[pos][up];
    if (picture_exists(CAT_WALLSET, view.gfx_set, entry)) {
        show_map_picture(entry, zone, view.wall_key, 0);
        return;
    }
    setup_picture(&(PicturePlace){.flip = 1,
                                  .scale_x = SCALE_FULL,
                                  .scale_y = SCALE_FULL,
                                  .zone = zone,
                                  .anchor = ZONE_NO_ANCHOR,
                                  .key = view.wall_key,
                                  .key2 = NO_KEY},
                  CAT_WALLSET, view.gfx_set, stairs_side_flipped_entry[pos][up]);
    flush_picture();
}

/* Draws the stairs at pos as the party sees them from the front. */
static void show_stairs_front(int pos)
{
    if (pos > POS_CEILING_PIT_LAST)
        return;
    bool up = g_view_squares[pos].side[SIDE_STAIRS_UP] != 0;
    uint8_t entry = stairs_front_entry[pos][up];
    if (entry != ENTRY_NONE)
        show_map_picture(entry, stairs_front_zone[pos][up], view.wall_key, 0);
}

/* ---- Doors ---- */

static int inverse_scale(unsigned size, unsigned scaled)
{
    int scale = (int)((size * 2 * SCALE_FULL + SCALE_FULL) / (uint16_t)scaled) >> 1;
    return gfx_scale_64((int16_t)scaled, (int16_t)scale) < (int)size ? scale + 1 : scale;
}

/* Draws the ornament or the button of the door at pos; the doors near the party take clicks on it. */
static void show_door_ornament(int category, int ornament, int entry, int pos)
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
    setup_picture(&(PicturePlace){.scale_x = scale,
                                  .scale_y = scale,
                                  .depth = depth,
                                  .zone = base + ZONE_DOOR_ORNAMENT + row,
                                  .anchor = ZONE_NO_ANCHOR,
                                  .key = key,
                                  .key2 = NO_KEY},
                  category, ornament, entry);
    flush_picture();
    if (row >= DOOR_ROW_NEAR)
        add_click_zone(view.ctx.rect, CLICK_DOOR_BUTTON, pos);
}

/* Draws the parts (DOOR_FRAME_*) of the frame of the door at pos, with the button or the ornament on its right side. */
static void show_door_frame(int pos, int parts)
{
    uint16_t door = g_view_squares[pos].side[SIDE_DOOR_THING];
    if (aspect_value_key(CAT_DOOR, map_door_type(door), DOOR_KEY_NO_FRAME))
        return;
    if (parts & DOOR_FRAME_TOP && pos < DOOR_TOP_POSITIONS && door_top_frame[pos] >= 0)
        show_map_picture(door_top_frame[pos], door_top_zone[pos], view.wall_key, 0);
    int mpos = view.flip ? view_mirror[pos] : pos, left = view.flip ? 1 : 0;
    uint8_t entry = door_side_frame[mpos][left];
    if (parts & DOOR_FRAME_LEFT && entry != ENTRY_NONE) {
        setup_picture(&(PicturePlace){.scale_x = SCALE_FULL,
                                      .scale_y = SCALE_FULL,
                                      .zone = zone_id_for_view_cell(pos, CELL_LEFT_MIDDLE, 0),
                                      .anchor = ANCHOR_DOOR_RIGHT,
                                      .key = view.wall_key,
                                      .key2 = NO_KEY},
                      CAT_WALLSET, view.gfx_set, entry);
        flush_picture();
    }
    entry = door_side_frame[mpos][!left];
    if (!(parts & DOOR_FRAME_RIGHT) || entry == ENTRY_NONE)
        return;
    setup_picture(&(PicturePlace){.flip = 1,
                                  .scale_x = SCALE_FULL,
                                  .scale_y = SCALE_FULL,
                                  .zone = zone_id_for_view_cell(pos, CELL_RIGHT_MIDDLE, 0),
                                  .anchor = ANCHOR_DOOR_LEFT,
                                  .key = view.wall_key,
                                  .key2 = NO_KEY},
                  CAT_WALLSET, view.gfx_set, entry);
    flush_picture();
    if (door_flag(door, DOOR_BUTTON)) {
        show_door_ornament(CAT_DOOR_BUTTON, 0, door_flag(door, DOOR_BUTTON_PRESSED) * DOOR_BUTTON_FRAMES, pos);
    } else if (g_view_squares[pos].side[SIDE_FLOOR_ORNAMENT] != ORNAMENT_NONE) {
        uint16_t w = g_view_squares[pos].side[SIDE_FLOOR_ORNAMENT];
        show_door_ornament(CAT_WALL_ORNAMENT, aspect_word_ornament(w), (uint8_t)(face_frame_entry(w) + 1), pos);
    }
}

/* Puts the door picture d together offscreen with its ornament and its damage, and makes it the view picture. */
static void compose_door(const DoorPicture *d)
{
    /* the door with its ornament or damage is put together offscreen */
    DrawCtx c;
    Bitmap *offscreen = render_offscreen(
        &c,
        &(PicturePlace){
            .scale_x = d->scale, .scale_y = d->scale, .depth = d->shade_depth, .key = d->key, .key2 = NO_KEY},
        CAT_DOOR, d->type, d->entry);
    if (!offscreen)
        return;
    draw_ctx_draw(&c);
    int scale_x = d->scale, scale_y = d->scale;
    if (d->scale == SCALE_FULL) {
        const Bitmap *near = draw_gdat_image(CAT_DOOR, d->type, 0);
        if (near) {
            scale_x = inverse_scale((uint16_t)view.ctx.w, (uint16_t)near->width);
            scale_y = inverse_scale((uint16_t)view.ctx.h, (uint16_t)near->height);
        }
    }
    if (d->ornament) {
        uint8_t o = dungeon_map_door_ornaments(view.party_map)[d->ornament - 1];
        int okey = (int16_t)aspect_value_key(CAT_DOOR_ORNAMENT, o, ORN_KEY_COLOR);
        if (!okey)
            okey = ORNAMENT_KEY_DEFAULT;
        int ozone =
            aspect_value_key(CAT_DOOR_ORNAMENT, o, ORN_KEY_ZONE_ROW) * ZONE_ROW_SIZE + ZONE_ORNAMENT_ROWS + d->row;
        setup_picture(&(PicturePlace){.scale_x = scale_x,
                                      .scale_y = scale_y,
                                      .depth = d->depth,
                                      .zone = ozone,
                                      .anchor = ZONE_NO_ANCHOR,
                                      .key = okey,
                                      .key2 = NO_KEY},
                      CAT_DOOR_ORNAMENT, o, 0);
        view.ctx.dst = offscreen;
        flush_picture();
    }
    if (d->destroyed) {
        int dkey = (int16_t)aspect_value_key(CAT_DOOR, d->type, ORN_KEY_DAMAGE_COLOR);
        if (!dkey)
            dkey = ORNAMENT_KEY_DEFAULT;
        int dzone = aspect_value_key(CAT_DOOR, d->type, ORN_KEY_KIND) * ZONE_ROW_SIZE + ZONE_ORNAMENT_ROWS + d->row;
        setup_picture(&(PicturePlace){.scale_x = scale_x,
                                      .scale_y = scale_y,
                                      .depth = d->depth,
                                      .zone = dzone,
                                      .anchor = ZONE_NO_ANCHOR,
                                      .key = dkey,
                                      .key2 = d->key},
                      CAT_DOOR, d->type, GFX_DOOR_DAMAGE);
        view.ctx.dst = offscreen;
        flush_picture();
    }
    c.transparent = (int16_t)mapped_key(&c, d->key);
    c.palette_count = 0;
    draw_ctx_set_image(&c, offscreen);
    view.ctx = c;
    view.ctx.dst = &g_view_viewport;
}

/* Starts the picture of the closed or moving door at depth (zone row row) in state: put together with its ornament
   or its damage when it has one. */
static void setup_door_picture(uint16_t door, int state, int depth, int row)
{
    uint8_t type = map_door_type(door);
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
        setup_picture(&(PicturePlace){.scale_x = scale,
                                      .scale_y = scale,
                                      .depth = shade_depth,
                                      .zone = ZONE_NONE,
                                      .anchor = ZONE_NO_ANCHOR,
                                      .key = key,
                                      .key2 = NO_KEY},
                      CAT_DOOR, type, entry);
    } else {
        compose_door(&(DoorPicture){.type = type,
                                    .ornament = ornament,
                                    .destroyed = state == DOOR_STATE_BROKEN,
                                    .entry = entry,
                                    .scale = scale,
                                    .shade_depth = shade_depth,
                                    .depth = depth,
                                    .row = row,
                                    .key = key});
    }
}

/* Draws the door at pos between its frame parts, with the things of the cells of cell_mask behind it. */
static void show_door(int pos, int frame_before, int frame_after, uint32_t cell_mask)
{
    if (frame_before)
        show_door_frame(pos, frame_before);
    if (cell_mask)
        show_square_things(pos, cell_mask, 0);
    ViewSquare *s = g_view_squares + pos;
    int state = s->side[SIDE_DOOR_STATE], depth = view_depth[pos];
    int row = depth <= DEPTH_FAR ? door_depth_row[depth] : -1, zone = door_zone[pos];
    if (state != DOOR_STATE_OPEN && depth <= DEPTH_FAR && row >= 0 && zone >= 0) {
        uint16_t door = s->side[SIDE_DOOR_THING];
        setup_door_picture(door, state, depth, row);
        if (state < DOOR_STATE_CLOSED) {
            zone += state;
            if (!door_flag(door, DOOR_VERTICAL)) {
                /* the two halves slide apart: the right half first */
                int16_t half = (int16_t)(view.ctx.w >> 1);
                view.ctx.w = half;
                view.ctx.x = (int16_t)(view.ctx.x + half);
                view.ctx.zone = (int16_t)(zone + DOOR_HALF_OFFSET);
                flush_picture();
                view.ctx.x = (int16_t)(view.ctx.x - half);
                zone += DOOR_LEFT_OFFSET;
            }
        }
        view.ctx.zone = (int16_t)zone;
        flush_picture();
    }
    if (frame_after)
        show_door_frame(pos, frame_after);
}

/* Draws the door square at pos seen from the front: the things behind the door, the door, and the things before it. */
static void show_door_front(int pos)
{
    if (!door_front_shown[pos])
        return;
    uint32_t mask = g_view_squares[pos].side[SIDE_DOOR_STATE] == DOOR_STATE_BROKEN ? 0 : THINGS_BEHIND_DOOR;
    show_square_things(pos, THINGS_NEAR, pos != POS_PARTY);
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
            show_square_things(pos, THINGS_DOOR_CELL, 0);
        show_door(pos, frames[pos][0], frames[pos][1], 0);
        return;
    }
    if (pos != POS_NEAR_SIDE_1 && pos != POS_NEAR_SIDE_2 && pos != POS_DEPTH2_FAR_LEFT && pos != POS_DEPTH2_FAR_RIGHT)
        show_door(pos, frames[pos][0], frames[pos][1], mask);
    show_square_things(pos, THINGS_FAR, 1);
    show_pending_possessions(pos);
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

/* Places a landmark of the sky text definition for the party; false when it is not visible. */
static bool outdoor_place_sky_object(SkyObject *o, const char *definition, int dir, int x, int y)
{
    o->dx = o->dy = 0;
    int move = (int16_t)gdat_text_int_field(definition, "mv");
    if (move == SKY_FIXED) {
        o->scale_x = o->scale_y = SCALE_FULL;
        return true;
    }
    if (move != SKY_LANDMARK)
        return false;
    int16_t px = (int16_t)(x + dungeon_map_offset_x(view.map)), py = (int16_t)(y + dungeon_map_offset_y(view.map));
    int lx = (int16_t)gdat_text_int_field(definition, "xl"), ly = (int16_t)gdat_text_int_field(definition, "yl");
    int distance = outdoor_relative_distance(dir, &px, &py, lx, ly);
    if (py < 1)
        return false;
    int16_t near = (int16_t)(SHADE_FULL - (distance - gdat_text_int_field(definition, "fd")));
    int scale = near > 1 ? near : 1;
    o->dx = (int16_t)(px * SKY_LANDMARK_DISTANCE / distance);
    o->scale_x = o->scale_y = (uint8_t)rounded_scale(scale, SHADE_FULL);
    return true;
}

/* Draws a sky object for the party at (x, y) looking to dir, smaller while the party walks. */
static void outdoor_draw_sky_object(const SkyObject *o, int dir, int x, int y)
{
    if (!o->zone)
        return;
    int flip = (o->flip == SKY_FLIP_FLOOR && get_flip_parity(PARITY_FLOOR, dir, x, y)) ||
               (o->flip == SKY_FLIP_CEILING && get_flip_parity(PARITY_CEILING, dir, x, y)) ||
               (o->flip == SKY_FLIP_FLOOR_2 && get_flip_parity(PARITY_FLOOR, dir, x, y)) ||
               (o->flip == SKY_FLIP_CEILING_2 && get_flip_parity(PARITY_CEILING, dir, x, y));
    int scale_x = o->scale_x, scale_y = o->scale_y;
    int16_t dx = o->dx, dy = o->dy;
    if (view.walking) {
        dx = gfx_scale_64(dx, WALK_SCALE);
        dy = gfx_scale_64(dy, WALK_SCALE);
        scale_x = gfx_scale_64((int16_t)scale_x, WALK_SCALE);
        scale_y = gfx_scale_64((int16_t)scale_y, WALK_SCALE);
        if (o->scale_x == SCALE_FULL) {
            dx = (int16_t)(dx + g_zone_walk_view_rect[RECT_X]);
            dy = (int16_t)(dy + (o->zone == ZONE_SKY_HIGH ? WALK_SKY_HIGH_DY : g_zone_walk_view_rect[RECT_Y]));
        } else {
            dy = (int16_t)(dy + WALK_SKY_DY);
        }
    }
    setup_picture(&(PicturePlace){.flip = flip,
                                  .scale_x = scale_x,
                                  .scale_y = scale_y,
                                  .zone = (int16_t)(o->zone | ZONE_RELATIVE),
                                  .anchor = ZONE_NO_ANCHOR,
                                  .key = view.wall_key,
                                  .key2 = NO_KEY},
                  CAT_SKY, view.gfx_set, o->entry);
    view.ctx.src_x = (int16_t)(view.ctx.src_x + dx);
    view.ctx.src_y = (int16_t)(view.ctx.src_y + dy);
    flush_picture();
}

/* Draws the sky of an outdoor map: the weather objects, then the landmarks that the party sees. */
static void outdoor_draw_sky(int dir, int x, int y)
{
    if (g_game.weather_visible)
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
                view.landmarks[e / BYTE_BIT_COUNT] |= (uint8_t)(1 << (e % BYTE_BIT_COUNT));
                view.no_landmarks = 0;
            }
    }
    if (view.no_landmarks)
        return;
    /* like the original, the landmarks use the first sky object as their record */
    SkyObject *o = weather_sky_objects();
    for (int e = 0; e < SKY_LANDMARK_MAX; e++) {
        if (!(view.landmarks[e / BYTE_BIT_COUNT] & 1 << (e % BYTE_BIT_COUNT)))
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

/* Draws the side of the door frame in front of the party, unless its door type has no frame. */
static void show_front_door_side(void)
{
    if (aspect_value_key(CAT_DOOR, map_door_type(g_view_squares[POS_FRONT].side[SIDE_DOOR_THING]), DOOR_KEY_NO_FRAME))
        return;
    setup_picture(&(PicturePlace){.flip = view.flip,
                                  .scale_x = DOOR_SIDE_SCALE,
                                  .scale_y = DOOR_SIDE_SCALE,
                                  .depth = DOOR_SIDE_DEPTH,
                                  .zone = zone_id_for_view_cell(POS_FRONT, CELL_FAR_MIDDLE, 0),
                                  .anchor = ZONE_NO_ANCHOR,
                                  .key = view.wall_key,
                                  .key2 = NO_KEY},
                  CAT_WALLSET, view.gfx_set, GFX_MAP_DOOR_SIDE);
    flush_picture();
}

/* Draws the squares in sight except the party square, from the far ones to the near ones. */
static void show_far_squares(void)
{
    uint32_t cell_mask = THINGS_ALL; /* like the original, kept from square to square */
    for (int i = 0; i < POS_DRAWN; i++) {
        int pos = far_order[i], aspect = g_view_squares[pos].aspect;
        uint16_t first = g_view_squares[pos].first_thing;
        if (aspect == ASPECT_WALL) { /* a creature can stand in a wall that is not solid */
            if (first != THING_ENDOFLIST)
                show_creatures(first, pos, THINGS_ALL);
            show_wall_square(pos);
            continue;
        }
        if (pos >= POS_FAR_ROW) {
            if (aspect == ASPECT_FLOOR || aspect == ASPECT_PIT || aspect == ASPECT_TELEPORTER)
                show_floor_ornament(pos);
            else if (aspect != ASPECT_DOOR_FRONT)
                continue;
            if (first != THING_ENDOFLIST)
                show_creatures(first, pos, THINGS_ALL);
            continue;
        }
        switch (aspect) {
        case ASPECT_DOOR_FRONT:
            show_door_front(pos);
            continue;
        case ASPECT_STAIRS_FRONT:
            show_stairs_front(pos);
            break;
        case ASPECT_STAIRS_SIDE:
            show_stairs_side(pos);
            break;
        case ASPECT_PIT:
            show_floor_pit(pos);
            __attribute__((fallthrough));
        case ASPECT_FLOOR:
        case ASPECT_TELEPORTER:
        case ASPECT_DOOR_SIDE:
            if (aspect == ASPECT_DOOR_SIDE && pos == POS_FRONT)
                show_front_door_side();
            cell_mask = show_floor_ornament(pos) ? THINGS_FAR : THINGS_ALL;
            show_ceiling_pit(pos);
            break;
        default:
            continue;
        }
        show_square_things(pos, cell_mask, 1);
        if (aspect == ASPECT_TELEPORTER)
            show_sparkle_field(pos, CAT_TELEPORTER, 0);
    }
}

/* Draws the party square with its things, then the rain over the view and the explosions on the party square. */
static void show_party_square(void)
{
    int aspect = g_view_squares[POS_PARTY].aspect, linked = 0;
    TeleporterLink link;
    if (aspect == ASPECT_DOOR_FRONT) {
        show_door_front(POS_PARTY);
    } else {
        if (aspect == ASPECT_PIT) {
            show_floor_pit(POS_PARTY);
        } else if (aspect == ASPECT_DOOR_SIDE) {
            if (!aspect_value_key(CAT_DOOR, map_door_type(g_view_squares[POS_PARTY].side[SIDE_DOOR_THING]),
                                  DOOR_KEY_NO_FRAME))
                show_map_picture(GFX_MAP_DOOR_SIDE, zone_id_for_view_cell(POS_PARTY, CELL_FAR_MIDDLE, 0), view.wall_key,
                                 view.flip);
        } else if (aspect == ASPECT_STAIRS_SIDE) {
            bool up = g_view_squares[POS_PARTY].side[SIDE_STAIRS_UP] != 0;
            show_map_picture(up ? GFX_MAP_STAIRS_SIDE_UP : GFX_MAP_STAIRS_SIDE_DOWN,
                             up ? ZONE_PARTY_STAIRS_UP : ZONE_PARTY_STAIRS_DOWN, view.wall_key, 0);
            show_map_picture((up ? GFX_MAP_STAIRS_SIDE_UP : GFX_MAP_STAIRS_SIDE_DOWN) + 1,
                             (up ? ZONE_PARTY_STAIRS_UP : ZONE_PARTY_STAIRS_DOWN) + 1, view.wall_key, 0);
        }
        show_ceiling_pit(POS_PARTY);
        linked = dungeon_teleporter_link(view.map, g_view_squares[POS_PARTY].x, g_view_squares[POS_PARTY].y, &link);
        if (!linked)
            show_floor_ornament(POS_PARTY);
        show_square_things(POS_PARTY, THINGS_ALL, 0);
        if (aspect == ASPECT_TELEPORTER)
            show_sparkle_field(POS_PARTY, CAT_TELEPORTER, 0);
    }
    show_rain();
    if (linked)
        show_floor_ornament(POS_PARTY);
    show_explosions(g_view_squares[POS_PARTY].first_thing, POS_PARTY, THINGS_ALL);
}

void view_clear_viewport(void)
{
    if (view.clear)
        gfx_fill_rect(g_view_viewport.pixels, view.clear_rect, view.clear_color, VIEWPORT_WIDTH, IMAGE_8BPP);
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
    bool creatures = !(g_party.move_steps && pos == POS_PARTY); /* the walking view hides the party square creatures */
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

bool view_shows_party(void)
{
    return view.party_x == g_game.party_x && view.party_y == g_game.party_y && view.party_map == g_game.party_map &&
           view.party_dir == g_game.party_dir;
}

/* A view of a game step starts new projectile tracks; the tracks of the view of the game step just before stay when the
   party view is the same. Another view in the same game step keeps them. */
static void start_tracks(int dir, int16_t x, int16_t y)
{
    if (flight.step_ticks)
        return;
    bool same = flight.map == view.map && flight.x == x && flight.y == y && flight.dir == dir &&
                flight.walking == (view.walking != 0);
    if (same && flight.time + 1 == g_game.game_time) {
        memcpy(flight.last, flight.tracks, sizeof flight.tracks[0] * (size_t)flight.count);
        flight.last_count = flight.count;
    } else if (!same || flight.time != g_game.game_time) {
        flight.last_count = 0;
    }
    flight.count = 0;
    flight.map = view.map;
    flight.x = x;
    flight.y = y;
    flight.dir = (int16_t)dir;
    flight.walking = view.walking != 0;
    flight.time = g_game.game_time;
}

void view_draw_dungeon(int16_t dir, int16_t x, int16_t y)
{
    view.map = view.party_map = g_game.party_map;
    view.party_dir = dir;
    view.party_x = x;
    view.party_y = y;
    view.shade = game_has_cheat(CHEAT_LIGHT)
                     ? 0
                     : g_view_light_level * LIGHT_STEP; /* the creature rules keep g_view_light_level */
    view.walking = g_party.move_steps != 0;
    start_tracks(dir, x, y);
    g_view_click_zone_count = VIEW_CLICK_ZONE_FIXED;
    for (int i = 0; i < VIEW_CLICK_ZONE_FIXED; i++) {
        g_view_click_zones[i].thing = THING_NONE;
        g_view_click_zones[i].kind = CLICK_NONE;
    }
    memset(g_view_creature_grid, 0, sizeof g_view_creature_grid);
    memset(g_view_object_grid, NO_POSITION, sizeof g_view_object_grid);
    view_clear_viewport();
    g_view_arrows_enabled = g_party.push_mode || g_party.push_pending_group != THING_NONE;
    input_update_viewport_arrow();
    for (int pos = VIEW_SQUARE_COUNT - 1; pos >= 0; pos--)
        view_load_square(x, y, pos, dir);
    int margins = -1;
    if (g_view_squares[POS_DEPTH2_LEFT].aspect == ASPECT_WALL &&
        g_view_squares[POS_DEPTH2_CENTER].aspect == ASPECT_WALL &&
        g_view_squares[POS_DEPTH2_RIGHT].aspect == ASPECT_WALL)
        margins = MAP_KEY_FAR_MARGINS;
    if (g_view_squares[POS_FRONT_LEFT].aspect == ASPECT_WALL && g_view_squares[POS_FRONT].aspect == ASPECT_WALL &&
        g_view_squares[POS_FRONT_RIGHT].aspect == ASPECT_WALL)
        margins = MAP_KEY_NEAR_MARGINS;
    uint16_t m = margins < 0 ? 0 : aspect_value_key(CAT_WALLSET, view.gfx_set, margins);
    zone_set_clip_margins(0, 0, 0, LOW_BYTE(m));
    show_map_picture(GFX_MAP_CEILING, ZONE_CEILING, NO_KEY, get_flip_parity(PARITY_CEILING, dir, x, y) != 0);
    g_zone_clip_rect[RECT_W] = 0;
    zone_set_clip_margins(0, HIGH_BYTE(m), 0, 0);
    show_map_picture(GFX_MAP_FLOOR, ZONE_FLOOR, NO_KEY, get_flip_parity(PARITY_FLOOR, dir, x, y) != 0);
    g_zone_clip_rect[RECT_W] = 0;
    view.flip = get_flip_parity(PARITY_SQUARE, dir, x, y);
    outdoor_draw_sky(dir, x, y);
    view_unhide_linked_squares();
    view_mark_hidden_squares();
    show_far_squares();
    show_party_square();
    draw_free_temp();
}

/* ---- Projectile flight ---- */

ViewPoint view_flight_shift(ViewPoint old_corner, ViewPoint new_corner, int tick, int step_ticks)
{
    if (step_ticks <= 0 || tick >= step_ticks)
        return (ViewPoint){0, 0};
    int rest = step_ticks - tick;
    return (ViewPoint){(int16_t)((old_corner.x - new_corner.x) * rest / step_ticks),
                       (int16_t)((old_corner.y - new_corner.y) * rest / step_ticks)};
}

void view_set_flight_tick(int tick, int step_ticks)
{
    flight.tick = tick;
    flight.step_ticks = step_ticks;
}

bool view_has_moving_projectiles(void)
{
    for (int i = 0; i < flight.count; i++)
        if (flight.tracks[i].from.x != flight.tracks[i].to.x || flight.tracks[i].from.y != flight.tracks[i].to.y)
            return true;
    return false;
}

/* ---- Clicks ---- */

/* True when the last picture has a pixel other than key at (x, y) of the viewport. */
static bool hit_test_picture(int x, int y, int key)
{
    const DrawCtx *c = &view.ctx;
    const int16_t *r = c->rect;
    if (x < r[RECT_X] || x > r[RECT_X] + r[RECT_W] - 1 || y < r[RECT_Y] || y > r[RECT_Y] + r[RECT_H] - 1)
        return false;
    int sx = c->flip & MIRROR_H ? r[RECT_X] + r[RECT_W] - 1 - x + c->src_x : x - r[RECT_X] + c->src_x;
    int sy = y - r[RECT_Y] + c->src_y;
    const Bitmap *image = c->image;
    if (image && image->height >= 2)
        g_view_hit_offset_y = (int16_t)((sy - (image->height >> 1)) * PERCENT / (image->height >> 1));
    if (!image || sx < 0 || sy < 0 || sx >= image->width || sy >= image->height)
        return false;
    int pixel;
    if (image->bpp == IMAGE_8BPP) {
        pixel = image->pixels[sy * image->width + sx];
    } else {
        int i = sy * ((image->width + 1) & ~1) + sx;
        pixel = i & 1 ? image->pixels[i >> 1] & NIBBLE_MASK : image->pixels[i >> 1] >> NIBBLE_BIT_COUNT;
    }
    return pixel != key;
}

/* The hit test of the creature in front of the party. */
#define HIT_SIDE_LIMIT 75 /* percent of the half picture width: a click beyond it hits a side */
#define HIT_DEPTH      1  /* the depth at which the creature in front is drawn for a hit test */

bool view_hit_test_creature(int16_t x, int16_t y, int square_x, int square_y, int *out_side)
{
    uint16_t group = creature_get_group_at(square_x, square_y);
    if (group == THING_NONE)
        return false;
    show_creature(POS_FRONT, HIT_DEPTH, group);
    int key = view.ctx.transparent;
    view.ctx.transparent = DRAW_NO_BLIT;
    flush_picture();
    if (!hit_test_picture(x, y, key)) {
        draw_free_temp();
        return false;
    }
    const DrawCtx *c = &view.ctx;
    const int16_t *r = c->rect;
    int sx = c->flip & MIRROR_H ? r[RECT_X] + r[RECT_W] - 1 - x + c->src_x : x - r[RECT_X] + c->src_x;
    int half = c->image ? c->image->width >> 1 : 0;
    int16_t offset = half ? (int16_t)((sx - c->x - half) * PERCENT / half) : 0;
    *out_side = offset < -HIT_SIDE_LIMIT ? 3 : offset <= HIT_SIDE_LIMIT ? 0 : 1;
    draw_free_temp();
    return true;
}

/* The hit test of a wall ornament. */
#define HIT_SMALL_SIZE 32 /* a smaller ornament takes every click on its wall */

bool view_hit_test_wall_ornament(int pos, int16_t x, int16_t y)
{
    static const int8_t face_of_pos[4] = {0, -1, 1, 0}; /* the face that the party sees of the walls at 0 to 3 */
    if (pos > POS_FRONT)
        return false;
    ViewSquare saved = g_view_squares[pos];
    int sx = view.party_x, sy = view.party_y;
    step_relative(view.party_dir, view_offset[pos][1], view_offset[pos][0], &sx, &sy);
    g_view_squares[pos].x = (int8_t)sx;
    g_view_squares[pos].y = (int8_t)sy;
    aspect_get(&g_view_squares[pos], view.map, view.party_dir, sx, sy);
    bool hit = false;
    int key = show_wall_ornament(pos, face_of_pos[pos], 0);
    if (key >= 0)
        hit = hit_test_picture(x, y, key);
    if (pos == POS_FRONT || view.ctx.w < HIT_SMALL_SIZE || view.ctx.h < HIT_SMALL_SIZE)
        hit = true;
    g_view_squares[pos] = saved;
    draw_free_temp();
    return hit;
}

/* States of a hit test on a chest. */
#define CHEST_HIT_NONE          0
#define CHEST_HIT_OPEN          2
#define CHEST_HIT_INSIDE        3
/* The items a chest holds, and the transparent color of the hit test pictures. */
#define CHEST_CAPACITY          8
#define HIT_KEY                 10
/* ATTR_SLOTS: no container takes the item. */
#define ATTR_SLOTS_NO_CONTAINER 0x8000

/* A click on a chest in the view that hit_state tells: it opens or closes, takes thing (placing), or gives its last
   item to the leader hand. Returns hit, THING_NONE when done, THING_ENDOFLIST when thing must drop. */
static uint16_t chest_click(uint16_t chest, uint16_t hit, int hit_state, uint16_t thing, bool placing)
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
            champ_put_in_leader_hand(last, 1);
        }
        break;
    }
    }
    return THING_NONE;
}

uint16_t view_hit_test_object(const ViewObjectClick *click, uint16_t thing, bool placing)
{
    uint16_t group = THING_NONE, t;
    if (click->kind == CLICK_POSSESSIONS) {
        group = creature_group_at(g_party.cur_map, click->square_x, click->square_y);
        t = thing_word(group, GROUP_POSSESSIONS);
    } else {
        t = dungeon_square_first_thing(g_party.cur_map, click->square_x, click->square_y);
    }
    if (t == THING_ENDOFLIST)
        return placing ? thing : THING_NONE;
    int spot = click->kind == CLICK_ALCOVE ? ALCOVE_FIRST_SPOT : 0;
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
            show_object(
                t, click->pos,
                &(ObjectLook){
                    .spot = spot, .group = group, .open_state = state, .in_alcove = click->kind == CLICK_ALCOVE});
            if (hit_test_picture(click->x, click->y, HIT_KEY)) {
                chest = this_chest;
                hit = t;
                hit_state = state;
                break;
            }
            if (!state)
                break;
            state = CHEST_HIT_NONE;
        }
        spot++;
        if (click->kind == CLICK_ALCOVE) {
            if (spot >= ALCOVE_SPOT_END)
                spot = ALCOVE_FIRST_SPOT;
        } else {
            spot &= JITTER_COUNT - 1;
        }
    }
    draw_free_temp();
    if (chest == THING_NONE)
        return hit != THING_NONE ? hit : thing;
    return chest_click(chest, hit, hit_state, thing, placing);
}
