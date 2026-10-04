#include <stdlib.h>
#include <string.h>
#include "draw.h"
#include "dungeon.h"
#include "save.h"
#include "view.h"
#include "zone.h"

/* GDAT categories of the view pictures. */
enum {
    CAT_MAP = 0x08,             /* index: graphics set of the map */
    CAT_WALL_ORNAMENT = 0x09,
    CAT_FLOOR_ORNAMENT = 0x0a,
    CAT_DOOR_ORNAMENT = 0x0b,
    CAT_DOOR_BUTTON = 0x0c,
    CAT_DOOR = 0x0e,
    CAT_PORTRAIT = 0x16,
    CAT_SKY = 0x17,
};
#define TYPE_REMAP 7                /* GDAT key type of a depth shading color table */
#define TYPE_DATA 0x0d              /* GDAT key type of raw data */
#define CAT_INTERFACE 1
#define UI_COLORS_ENTRY 0xfe        /* (CAT_INTERFACE, 0, TYPE_DATA, entry): the interface colors */
#define LOW_BYTE(v) ((v) & 0xff)
#define HIGH_BYTE(v) ((v) >> 8)

/* Value keys (CAT_MAP, graphics set, 0x0b, key). */
enum {
    MAP_KEY_WALL_COLOR = 0x64,      /* transparent color of the map pictures */
    MAP_KEY_FLIP_FLAGS = 0x65,
    MAP_KEY_FLOOR_ORNAMENT = 0x6b,  /* ornament on every floor square, bit 15: animated */
    MAP_KEY_WALL_MARGINS = 0x6e,    /* + 1 beyond depth 1: left and right clip margins of a wall beside open squares */
    MAP_KEY_NEAR_MARGINS = 0x70,    /* ceiling and floor clip margins when the squares of depth 1 are walls */
    MAP_KEY_FAR_MARGINS = 0x71,     /* the same for the squares of depth 2 */
};
/* Pictures (CAT_MAP, graphics set, 1, entry). */
/* Map graphics of the map descriptor. */
#define MAP_GFX_SET_SHIFT 4
#define MAP_DOOR_TYPE_SHIFT 8       /* door type 0; door type 1 follows 4 bits higher */
#define MAP_HAS_DOOR_TYPE_0 0x0080
#define MAP_HAS_DOOR_TYPE_1 0x0100
#define DOOR_TYPE_NONE 0xff
#define NIBBLE 0xf
enum {
    MAP_FLOOR = 0, MAP_CEILING = 1, MAP_FONT = 3, MAP_DOOR_SIDE = 6, MAP_WALL = 0x22, MAP_FAR_WALL = 0x32,
    MAP_STAIRS_SIDE_UP = 0x39, MAP_STAIRS_SIDE_DOWN = 0x4d, MAP_WALL_FLIPPED = 0xb0,
    MAP_PLATE_FRONT = 0xfc, MAP_PLATE_SIDE = 0xfd, MAP_PLATE_RIGHT = 0xfe,
};
#define MAP_WALL_FLIPPED_MISSING (MAP_WALL_FLIPPED - MAP_WALL)  /* back from a missing flipped wall to the plain wall */
/* Value keys of the ornament categories (category, ornament, 0x0b, key). */
enum {
    ORN_KEY_COLOR = 4,              /* transparent color */
    ORN_KEY_PLACE = 5,              /* anchor in the high byte, grid cell + 1 in the low byte */
    ORN_KEY_NO_FLIP = 7,
    ORN_KEY_ZONE_ROW = 8,           /* door ornaments and buttons: zone row */
    ORN_KEY_KIND = 0x0a,            /* wall ornament kind; door: zone row of the damage picture */
    ORN_KEY_FRAMES = 0x0d,          /* animation frames, bit 15: frames start at 1 */
    ORN_KEY_WIDTH_SCALE_2 = 0x14,   /* width : height of the picture at depth 2, as 2 bytes */
    ORN_KEY_WIDTH_SCALE_3 = 0x15,
    ORN_KEY_OFFSCREEN_COLOR = 0x11, /* transparent color of the picture in its offscreen bitmap */
    ORN_KEY_DAMAGE_COLOR = 0x12,
    DOOR_KEY_NO_FRAME = 0x40,
    ORN_KEY_BIG_NEAR = 0x60,        /* floor ornament: scale 0x60 on the squares beside the party */
    ORN_KEY_NOT_FAR_SIDE = 0x61,    /* floor ornament: not drawn at view positions 14 and 15 */
    ORN_KEY_WET = 0x63,             /* drawn wet by the rain */
};
enum wall_ornament_kind { KIND_ALCOVE = 1, KIND_SHOP = 2, KIND_PORTRAIT = 3 };
#define ORNAMENT_NONE 0xff
#define FRAMES_FROM_ONE 0x8000
#define FLOOR_ORNAMENT_ANIMATED 0x8000
#define ENTRY_NONE 0xff
#define ORNAMENT_PLATE 0            /* a text plate instead of a wall ornament */
#define ORN_OVERLAY 0x0f            /* wall ornament picture drawn over the ornament */
#define ORN_WET_BACKGROUND 200      /* + entry: the picture behind a wet ornament */
#define DOOR_DAMAGE 0x41            /* door picture of the damage mask */
#define PORTRAIT_ENTRY 1
#define PORTRAIT_OFFSET_ENTRY 0xfd

/* Layout zones of the view. */
enum {
    ZONE_CEILING = 700, ZONE_FLOOR = 701, ZONE_WALL = 702, ZONE_DOOR_ORNAMENT = 1950, ZONE_ORNAMENT_ROWS = 2000,
    ZONE_VIEW_ELEMENTS = 3100, ZONE_VIEW_CELLS = 5000, ZONE_SKY_HIGH = 6001,
    ZONE_PARTY_STAIRS_UP = 811, ZONE_PARTY_STAIRS_DOWN = 824,
};
#define ZONE_ROW_SIZE 4             /* zones of one ornament row */
#define ZONE_CELL_GRID 25           /* zones of the 5 x 5 grid of a view element */
#define CELL_CENTER 12
#define CELL_GRID_SIDE 5
#define ANCHOR_DOOR_RIGHT 4
#define ANCHOR_DOOR_LEFT 3

/* View square sides by aspect. */
enum { SIDE_DOOR_STATE = 0, SIDE_DOOR_THING = 1, SIDE_FLOOR_ORNAMENT = 2, SIDE_STAIRS_UP = 0, SIDE_PIT_UP = 0 };
#define SIDE_SHOP_FLAG 3            /* floor: a shop display there sets it to 0 (read by the things drawing, 12b) */
#define FAKE_WALL_FLOOR_SQUARE 1    /* the square byte that the floor things see on an open fake wall */
#define SIDE_FRONT 2                /* the wall face that looks at the party */
#define ORN_INDEX_MASK 0xff
#define ORN_FRAME_SHIFT 10          /* wall ornament words: frame in bits 10-15 */
#define FLOOR_FRAME_STEP 0xa00      /* floor ornament words: frame * 10 in the high byte */

enum door_state { DOOR_OPEN, DOOR_DESTROYED = 5 };
#define DOOR_TYPE_BIT 0x01          /* door record bits */
#define DOOR_ORNAMENT_SHIFT 1
#define DOOR_ORNAMENT_MASK 0xf
#define DOOR_NO_SLIDE 0x20          /* the door moves as a whole, not in two halves */
#define DOOR_BUTTON 0x40
#define DOOR_BUTTON_PRESSED_SHIFT 11
#define DOOR_BUTTON_FRAMES 5
#define DOOR_HALF_OFFSET 6          /* zone of the right door half, relative to the door zone */
#define DOOR_LEFT_OFFSET 3
#define DOOR_FRAME_TOP 1            /* door frame parts */
#define DOOR_FRAME_LEFT 2
#define DOOR_FRAME_RIGHT 4
#define DOOR_KEY_DEFAULT 10
#define ORNAMENT_KEY_DEFAULT 9
#define DOOR_NEAR_SCALE 0x71
#define DOOR_SIDE_SCALE 0x2b        /* the door side picture in front of the party */
#define SQUARE_TYPE_BITS 0xe0

/* Text and sensor fields of wall and floor things. */
#define TEXT_VISIBLE 0x01
#define TEXT_KIND_SHIFT 1
#define TEXT_KIND_MASK 3
#define TEXT_ORNAMENT_SHIFT 3
#define TEXT_SUBTYPE_SHIFT 11
#define TEXT_SUBTYPE_MASK 0x1f
enum text_kind { TEXT_KIND_TEXT, TEXT_KIND_ORNAMENT };
/* Subtypes of ornament texts, by what the view does with them (game meanings from docs/skull-notes/range-6.md). */
enum text_subtype {
    SUBTYPE_STILL = 0,              /* plain ornament */
    SUBTYPE_ANIMATED = 2,
    SUBTYPE_SWITCH = 4,             /* frame 1 while visible */
    SUBTYPE_TOGGLE = 5,             /* animated while visible (wall text toggle) */
    SUBTYPE_STILL_2 = 6,
    SUBTYPE_SWITCH_2 = 7,
    SUBTYPE_SWITCH_3 = 8,
    SUBTYPE_TRAP_TELEPORT = 10,
    SUBTYPE_TOGGLE_2 = 13,
    SUBTYPE_PLATE = 14,             /* a text plate on the wall */
    SUBTYPE_WARP_MARKER = 15,       /* frame 1 */
    SUBTYPE_WARP_MARKER_2 = 16,
    SUBTYPE_SQUARE_SWITCH = 17,     /* frame from SQUARE_FLAG_3 */
    SUBTYPE_SHOP_STAND = 20,        /* clears SIDE_SHOP_FLAG */
};
#define SENSOR_ACTIVE_BIT 0         /* bits of SENSOR_FLAGS */
#define SENSOR_STATE_BIT 2
#define SENSOR_ORNAMENT_SHIFT 12
enum sensor_type {
    SENSOR_TOGGLE_ORNAMENT = 0x17, SENSOR_TIMED_ORNAMENT = 0x18, SENSOR_HOLDER = 0x1a, SENSOR_STATE_MATCH = 0x1b,
    SENSOR_LINK = 0x27, SENSOR_ANIMATED = 0x2c, SENSOR_SHOP = 0x30, SENSOR_SOUND_ANIMATION = 0x32,
    SENSOR_WALL_TEXT = 0x3f, SENSOR_FRAME_DATA = 0x41, SENSOR_FRAME_STATE = 0x46, SENSOR_TIMED_ORNAMENT_2 = 0x4a,
    SENSOR_ALTAR = 0x7e,
};
#define SENSOR_ALTAR_SHOWN 0x0004
#define SENSOR_DATA_BYTE 0xff

#define GAME_TIME_FRAMES 7          /* bits of the game time that animate pictures */
#define HALF_TIME_FRAMES 4
#define LIGHT_STEP 10               /* view shade per light level */
#define VIEW_SHADE_MAX 63
#define SHADE_FULL 64
#define WALK_SCALE 0x34             /* the sky shrinks to 52/64 while the party walks */
#define SCALE_WIDE_SIDE 0x72        /* wall ornament scales at the far sides */
#define SCALE_FAR_SIDE 0x4c
#define SCALE_BIG_NEAR 0x60         /* floor ornaments with ORN_KEY_BIG_NEAR beside the party */
#define RANDOM_ORNAMENT_SLOTS 30    /* random wall ornament hash: one of 30 values picks an ornament of the list */
#define FONT_SPACING 2              /* line gap of wall text */
#define WALL_TEXT_SIZE 128
#define NO_KEY (-1)
#define IMAGE_COLORS 16             /* colors of a 4 bpp picture */

/* Cell masks of the things drawing (12b). */
#define THINGS_ALL 0x1ffffff
#define THINGS_NEAR 0x3ff
#define THINGS_FAR 0x1fffc00
#define THINGS_BEHIND_DOOR 0x40001000
#define THINGS_DOOR_CELL 0x1000

/* The random wall ornament hash of SKULL.EXE. */
#define HASH_X_STEP 0x20
#define HASH_X_BASE 2000
#define HASH_MAP_STEP 0x40
#define HASH_MAP_BASE 3000
#define HASH_MULTIPLIER 0x7ab9
#define HASH_MAP_FACTOR 11

/* Value of a view square that the original reads as "no thing" or "none". */
#define THING_TEXT_NONE 0xffff

/* View positions (SKULL.EXE tables 0x75acc to 0x76025). */
#define POS_PARTY 0
#define POS_FRONT 3
#define POS_DEPTH_MAX 4
#define POS_LINKED 21               /* positions that can hold a seamless passage */
#define POS_CEILING_PIT_LAST 8
#define POS_FLOOR_PIT_BLOCKED 11    /* from here on, an open pit hides its floor picture */
#define POS_FAR_ROW 16
#define POS_DRAWN 20
#define POS_NEAR_SIDE_1 1
#define POS_NEAR_SIDE_2 2
#define POS_FAR_SIDE_1 14
#define POS_FAR_SIDE_2 15
#define POS_BIT(pos) (1u << (pos))
#define LINK_NONE 0xff

/* Right and forward square offset of each view position. */
static const int8_t view_offset[VIEW_SQUARE_COUNT][2] = {
    {0, 0}, {-1, 0}, {1, 0}, {0, 1}, {-1, 1}, {1, 1}, {0, 2}, {-1, 2}, {1, 2}, {-2, 2}, {2, 2}, {0, 3},
    {-1, 3}, {1, 3}, {-2, 3}, {2, 3}, {0, 4}, {-1, 4}, {1, 4}, {-2, 4}, {2, 4}, {-3, 4}, {3, 4},
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
enum { WALL_SHOWS_FRONT = 1, WALL_SHOWS_SIDE = 2, WALL_SHOWS_BOTH = 3 };
static const uint8_t wall_faces[VIEW_SQUARE_COUNT] = {
    0, 2, 2, 1, 3, 3, 1, 3, 3, 2, 2, 1, 3, 3, 3, 3, 1, 1, 1, 1, 1, 0, 0,
};
/* The squares that a seamless passage at each position shows again (0xff: none). */
static const uint8_t linked_squares[POS_LINKED][4] = {
    {1, 2, 3, 255}, {255, 0, 4, 255}, {0, 255, 5, 255}, {4, 5, 6, 0}, {255, 3, 7, 1}, {3, 255, 8, 2},
    {7, 8, 11, 3}, {9, 6, 12, 4}, {6, 10, 13, 5}, {255, 7, 14, 255}, {8, 255, 15, 255}, {12, 13, 16, 6},
    {14, 11, 17, 7}, {11, 15, 18, 8}, {255, 12, 19, 9}, {13, 255, 20, 10}, {17, 18, 255, 11}, {19, 16, 255, 12},
    {16, 20, 255, 13}, {21, 17, 255, 14}, {18, 22, 255, 15},
};
/* Hidden square masks by the wall pattern of the rows (bit n: view position n). */
static const uint32_t hide_row1[4] = {0, 0x200, 0x400, 0x600};
static const uint32_t hide_row2[8] = {0, 0x10840, 0x204200, 0x2b5ac0, 0x408400, 0x55ad40, 0x60c600, 0x7fffc0};
static const uint32_t hide_row3[8] = {0, 0x10800, 0x204000, 0x2b5800, 0x408000, 0x55a800, 0x60c000, 0x7ff800};
static const uint32_t hide_row4[8] = {0, 0x10000, 0x20000, 0xb0000, 0x40000, 0x50000, 0x60000, 0x70000};
#define HIDE_FAR_LEFT (POS_BIT(19) | POS_BIT(21))  /* far squares hidden behind the corner walls */
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
    {211, 212}, {255, 255}, {255, 255}, {7, 8}, {255, 213}, {214, 255}, {9, 10}, {255, 215}, {216, 255},
    {255, 255}, {255, 255}, {11, 12}, {13, 14}, {15, 16},
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
    {255, 255}, {255, 255}, {255, 255}, {79, 59}, {80, 60}, {81, 61}, {82, 62}, {83, 63}, {84, 64},
    {255, 255}, {255, 255}, {85, 65}, {86, 66}, {87, 67}, {88, 68}, {89, 69},
};
static const uint8_t stairs_side_flipped_entry[POS_FAR_ROW][2] = {
    {255, 255}, {255, 255}, {255, 255}, {79, 59}, {80, 60}, {80, 60}, {82, 62}, {83, 63}, {83, 63},
    {255, 255}, {255, 255}, {85, 65}, {86, 66}, {86, 66}, {88, 68}, {88, 68},
};
static const int16_t stairs_side_zone[POS_FAR_ROW][2] = {
    {-1, -1}, {-1, -1}, {-1, -1}, {822, 809}, {821, 808}, {823, 810}, {819, 806}, {818, 805}, {820, 807},
    {-1, -1}, {-1, -1}, {816, 803}, {815, 802}, {817, 804}, {800, 800}, {801, 801},
};
static const uint8_t stairs_front_entry[POS_CEILING_PIT_LAST + 1][2] = {
    {255, 255}, {205, 199}, {206, 200}, {255, 255}, {207, 201}, {208, 202}, {255, 255}, {209, 203}, {210, 204},
};
static const int16_t stairs_front_zone[POS_CEILING_PIT_LAST + 1][2] = {
    {-1, -1}, {832, 832}, {833, 833}, {-1, -1}, {830, 828}, {831, 829}, {-1, -1}, {826, 826}, {827, 827},
};
/* Wall text: the 5-bit codes 29 and 30 of the dungeon text select an escape character or word. */
static const char *const text_escape_words[] = {"x", "y", "THE ", "YOU ", "z", "{", "|", "}", "~", "\x7f"};
static const char text_escape_chars[] = "abcdefghijklmnopqrstuvwx01234567";
enum { TEXT_SPACE = 26, TEXT_PERIOD = 27, TEXT_NEWLINE = 28, TEXT_ESCAPE_CHAR = 29, TEXT_ESCAPE_WORD = 30, TEXT_END = 31 };
#define TEXT_CODE_BITS 5
#define TEXT_CODE_MASK 0x1f
#define GLYPH_SPACE 0x1a
#define GLYPH_PERIOD 0x1b
#define GLYPH_SIZE 8                /* the wall text font has 8 x 8 glyphs */
/* The walking view: floor and ceiling shift, and the shift of the sky. */
static const int16_t walk_floor_dy = 3, walk_ceiling_dy = -2;
static const int16_t walk_sky_dy = -3, walk_sky_high_dy = 2, walk_view_x = 21, walk_view_y = 8;

/* Sky objects: clouds, rain, and the landmarks of an outdoor map. */
#define SKY_OBJECT_MAX 5
#define SKY_OBJECT_END 0xff
#define SKY_LANDMARK_MAX 100
#define SKY_RAIN_LIGHT 0x67
#define SKY_RAIN_MEDIUM 0x68
#define SKY_RAIN_HEAVY 0x69
#define SKY_CLOUDS_LIGHT 0x6a
#define SKY_CLOUDS_MEDIUM 0x6b
#define SKY_CLOUDS_HEAVY 0x6c
#define SKY_LANDMARK_DISTANCE 210   /* screen offset per square of lateral distance */
#define SKY_DARKNESS_RAIN 0x10      /* rain pictures from this sky darkness on, heavier from the next limits */
#define SKY_DARKNESS_MEDIUM 0x40
#define SKY_DARKNESS_HEAVY 0x80
#define SKY_CLOUDS_SHOWN 0x40
#define SKY_CLOUDS_MORE 0x80
#define SKY_CLOUDS_MOST 0xc0
enum sky_flip { SKY_FLIP_FLOOR = 8, SKY_FLIP_CEILING = 2, SKY_FLIP_FLOOR_2 = 0x40, SKY_FLIP_CEILING_2 = 0x20 };
enum sky_move { SKY_FIXED, SKY_LANDMARK };
typedef struct {
    uint8_t entry;
    uint8_t flip;               /* enum sky_flip */
    int16_t zone, dx, dy;
    uint8_t scale_x, scale_y;
} SkyObject;

/* Flip parity kinds and the map flip flags (value key MAP_KEY_FLIP_FLAGS). */
enum { PARITY_SQUARE = 0, PARITY_FLOOR = 1, PARITY_CEILING = 0x20 };
enum {
    FLIP_CEILING_PITS = 0x01, FLIP_CEILING_BY_SQUARE = 0x02, FLIP_CEILING_BY_TIME = 0x04,
    FLIP_FLOOR_BY_SQUARE = 0x08, FLIP_FLOOR_BY_TIME = 0x10, FLIP_CEILING_BY_PARTY = 0x20, FLIP_FLOOR_BY_PARTY = 0x40,
};

ViewSquare view_squares[VIEW_SQUARE_COUNT];
static uint8_t viewport_pixels[VIEWPORT_WIDTH * VIEWPORT_HEIGHT];
Bitmap view_viewport = {viewport_pixels, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, 8};
int view_light_level;

static struct {
    int map;                    /* the map of the squares that the view reads now */
    int party_map;
    int party_dir, party_x, party_y;   /* the position the view is drawn for */
    int walking;                /* the party walks: the original g_party_move_steps (Step 14) */
    int shade;
    int flip;                   /* mirror the far pictures on odd squares */
    uint8_t gfx_set;
    uint8_t door_types[2];
    int16_t wall_key;
    uint16_t flip_flags;
    const uint8_t *wall_ornaments; /* random wall ornaments: the list of the party map */
    int gfx_has_rain, gfx_has_clouds;
    int clear;                  /* fill clear_rect before drawing: floor and ceiling leave a gap */
    int16_t clear_rect[RECT_SIZE];
    uint8_t clear_color;
    SkyObject sky[SKY_OBJECT_MAX];
    int landmark_set;           /* graphics set of the landmark bits, -1: none yet */
    int no_landmarks;
    uint8_t landmarks[(SKY_LANDMARK_MAX + 7) / 8];
    DrawCtx ctx;                /* the picture of view_setup_picture */
} view = {.landmark_set = -1};

/* The first thing of the square (x, y) of the current map, THING_ENDOFLIST away from the map. */
static uint16_t first_thing(int x, int y)
{
    if (x < 0 || x >= dungeon_map_width(view.map) || y < 0 || y >= dungeon_map_height(view.map)) return THING_ENDOFLIST;
    return dungeon_square_first_thing(view.map, x, y);
}

static uint16_t value_key(int category, int index, int entry)
{
    return gdat_entry_index(draw_gdat(), category, index, GDAT_TYPE_VALUE, entry);
}

static int picture_exists(int category, int index, int entry)
{
    return gdat_exists(draw_gdat(), category, index, GDAT_TYPE_IMAGE, entry);
}

/* The door type of a door thing on the current map, 0xff for none. */
static uint8_t door_type(uint16_t door)
{
    return view.door_types[thing_word(door, DOOR_DATA) & DOOR_TYPE_BIT];
}

static void move_relative(int dir, int forward, int right, int *x, int *y)
{
    static const int8_t dx[DIR_COUNT] = {0, 1, 0, -1}, dy[DIR_COUNT] = {-1, 0, 1, 0};
    *x += dx[dir & 3] * forward + dx[(dir + 1) & 3] * right;
    *y += dy[dir & 3] * forward + dy[(dir + 1) & 3] * right;
}

/* ---- Map graphics and sky objects ---- */

/* Reads a weather sky object (rain, clouds) of the map graphics; returns 0 when the map has none. */
static int weather_load_sky_object(SkyObject *object)
{
    char text[WALL_TEXT_SIZE];
    gdat_text(draw_gdat(), CAT_SKY, view.gfx_set, object->entry, text, sizeof text);
    if (!text[0]) return 0;
    object->zone = (int16_t)text_find_int_field(text, "cd");
    object->dx = object->dy = 0;
    object->flip = (uint8_t)text_find_int_field(text, "fw");
    object->scale_x = object->scale_y = SCALE_FULL;
    return 1;
}

/* The rain and clouds of the saved weather; the weather update (Step 17) rebuilds them every step. */
static void build_weather_sky(void)
{
    SkyObject *o = view.sky;
    if (game.weather_visible) {
        if (view.gfx_has_rain && game.sky_darkness >= SKY_DARKNESS_RAIN) {
            o->entry = game.sky_darkness < SKY_DARKNESS_MEDIUM ? SKY_RAIN_LIGHT
                       : game.sky_darkness < SKY_DARKNESS_HEAVY ? SKY_RAIN_MEDIUM : SKY_RAIN_HEAVY;
            if (weather_load_sky_object(o)) o++;
        }
        if (view.gfx_has_clouds && game.cloud_cover >= SKY_CLOUDS_SHOWN) {
            o->entry = game.cloud_cover < SKY_CLOUDS_MORE ? SKY_CLOUDS_LIGHT
                       : game.cloud_cover < SKY_CLOUDS_MOST ? SKY_CLOUDS_MEDIUM : SKY_CLOUDS_HEAVY;
            if (weather_load_sky_object(o)) o++;
        }
    }
    o->entry = SKY_OBJECT_END;
}

void view_enter_map(int map)
{
    const gdat_t *g = draw_gdat();
    uint16_t graphics = dungeon_map_graphics(map), flags = dungeon_map_flags(map);
    view.map = view.party_map = map;
    view.gfx_set = graphics >> MAP_GFX_SET_SHIFT & NIBBLE;
    view.door_types[0] = flags & MAP_HAS_DOOR_TYPE_0 ? graphics >> MAP_DOOR_TYPE_SHIFT & NIBBLE : DOOR_TYPE_NONE;
    view.door_types[1] = flags & MAP_HAS_DOOR_TYPE_1 ? graphics >> (MAP_DOOR_TYPE_SHIFT + 4) & NIBBLE : DOOR_TYPE_NONE;
    view.wall_key = (int16_t)value_key(CAT_MAP, view.gfx_set, MAP_KEY_WALL_COLOR);
    view.flip_flags = value_key(CAT_MAP, view.gfx_set, MAP_KEY_FLIP_FLAGS);
    view.wall_ornaments = dungeon_map_wall_ornaments(map);
    view.gfx_has_rain = gdat_exists(g, CAT_SKY, view.gfx_set, GDAT_TYPE_IMAGE, SKY_RAIN_LIGHT);
    view.gfx_has_clouds = gdat_exists(g, CAT_SKY, view.gfx_set, GDAT_TYPE_IMAGE, SKY_CLOUDS_LIGHT);
    /* floor and ceiling pictures that do not fill the viewport leave a band that is cleared */
    const Bitmap *floor = draw_gdat_image(CAT_MAP, view.gfx_set, MAP_FLOOR);
    const Bitmap *ceiling = draw_gdat_image(CAT_MAP, view.gfx_set, MAP_CEILING);
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
    const uint8_t *colors = gdat_item(g, gdat_find(g, CAT_INTERFACE, 0, TYPE_DATA, UI_COLORS_ENTRY), &size);
    view.clear_color = colors && size ? colors[0] : 0;
    build_weather_sky();
}

/* 1 when the picture of kind should be mirrored for the party at (x, y) looking to dir. */
static int view_get_flip_parity(int kind, int dir, int x, int y)
{
    int parity = (y + x + dir + dungeon_map_offset_x(view.map) + dungeon_map_offset_y(view.map)
                  + dungeon_map_level(view.map)) & 1;
    int party = game.party_dir & 1, time = game.game_time & GAME_TIME_FRAMES;
    uint16_t flags = view.flip_flags;
    if (kind == PARITY_FLOOR) {
        if (!(flags & FLIP_FLOOR_BY_SQUARE)) return flags & FLIP_FLOOR_BY_PARTY ? party : 0;
        if (flags & FLIP_FLOOR_BY_TIME) return time >= HALF_TIME_FRAMES;
        return parity;
    }
    if (kind == PARITY_CEILING) {
        if (flags & FLIP_CEILING_BY_SQUARE) return flags & FLIP_CEILING_BY_TIME ? time < HALF_TIME_FRAMES : !parity;
        return flags & FLIP_CEILING_BY_PARTY ? party : 0;
    }
    return parity;
}

/* ---- Square aspects ---- */

/* A random wall ornament for a wall face, ORNAMENT_NONE when the face shows none. */
static uint8_t random_ornament(int allowed, int count, int x, int factor)
{
    if (!allowed) return ORNAMENT_NONE;
    int map_size = dungeon_map_width(view.map) + dungeon_map_height(view.map);
    uint16_t a = (uint16_t)(x * HASH_X_STEP + HASH_X_BASE + (int16_t)factor);
    uint16_t b = (uint16_t)(view.map * HASH_MAP_STEP + HASH_MAP_BASE + map_size);
    int16_t hash = (int16_t)(((((a * HASH_MULTIPLIER) >> 1) + b * HASH_MAP_FACTOR + dungeon_ornament_seed()) >> 2)
                             % RANDOM_ORNAMENT_SLOTS);
    return hash < count ? view.wall_ornaments[hash] : ORNAMENT_NONE;
}

static void set_random_wall_ornaments(ViewSquare *s, const int allowed[4], int dir, int x, int y)
{
    int count = dungeon_map_ornament_counts(view.map) >> 4 & NIBBLE;
    for (int k = 0; k < VIEW_SIDE_COUNT; k++)
        s->side[k] = random_ornament(allowed[k], count, x, ((dir + k & 3) + 1) * (y + 1));
    /* an edge wall away from the map keeps no ornament that has a kind */
    if (x >= 0 && x < dungeon_map_width(view.map) && y >= 0 && y < dungeon_map_height(view.map)) return;
    for (int k = 0; k < VIEW_SIDE_COUNT; k++)
        if ((uint8_t)s->side[k] != ORNAMENT_NONE && value_key(CAT_WALL_ORNAMENT, (uint8_t)s->side[k], ORN_KEY_KIND))
            s->side[k] = ORNAMENT_NONE;
}

/* The animation frame of an ornament at time + offset: from its frame count, or from its frame text. */
static int ornament_frame(int category, int ornament, uint32_t time, int offset)
{
    uint16_t frames = value_key(category, ornament, ORN_KEY_FRAMES);
    char text[WALL_TEXT_SIZE];
    int first = 0;
    const char *sequence = NULL;
    if (!frames) {
        gdat_text(draw_gdat(), category, ornament, ORN_KEY_FRAMES, text, sizeof text);
        if (!text[0]) return 1;
        sequence = text;
        frames = (uint16_t)strlen(text);
    } else if (frames & FRAMES_FROM_ONE) {
        first = 1;
        frames &= ~FRAMES_FROM_ONE;
    }
    int frame = (int)((time + (uint32_t)offset) % frames) + first;
    if (!sequence) return frame;
    int c = (uint8_t)sequence[(uint16_t)frame];
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return c - 'K';  /* as in SKULL.EXE: 'K' is frame 0 */
    return c;
}

/* The wall ornament of a sensor: an entry of the wall ornament list of the current map. */
static uint8_t sensor_ornament(uint16_t sensor, const uint8_t *list)
{
    int ordinal = thing_word(sensor, SENSOR_FLAGS) >> SENSOR_ORNAMENT_SHIFT;
    return ordinal ? list[ordinal - 1] : ORNAMENT_NONE;
}

static int sensor_bit(uint16_t sensor, int bit) { return thing_word(sensor, SENSOR_FLAGS) >> bit & 1; }

/* Applies the wall things (texts, sensors) to the faces of s; only_front limits them to the front face.
   Returns the first thing that is not a wall thing. */
static uint16_t apply_wall_things(ViewSquare *s, uint16_t thing, int dir, int only_front)
{
    int done = 0;
    for (; thing != THING_ENDOFLIST && thing_type(thing) <= THING_SENSOR; thing = dungeon_next_thing(thing)) {
        int cell = thing >> THING_CELL_SHIFT, face = (cell - dir) & 3;
        if (done & 1 << cell || (only_front && face != SIDE_FRONT)) continue;
        int animated = 0, frame = 0, offset = 0;
        uint32_t time = game.game_time;
        uint8_t ornament;
        if (thing_type(thing) == THING_TEXT) {
            uint16_t w = thing_word(thing, TEXT_DATA);
            int kind = w >> TEXT_KIND_SHIFT & TEXT_KIND_MASK, subtype = w >> TEXT_SUBTYPE_SHIFT & TEXT_SUBTYPE_MASK;
            if (kind == TEXT_KIND_TEXT || (kind == TEXT_KIND_ORNAMENT && subtype == SUBTYPE_PLATE)) {
                s->side[face] = ORNAMENT_PLATE;
                if (face == SIDE_FRONT) s->text = w & TEXT_VISIBLE ? thing : THING_TEXT_NONE;
                done |= 1 << cell;
                continue;
            }
            if (kind != TEXT_KIND_ORNAMENT) continue;
            ornament = (uint8_t)(w >> TEXT_ORNAMENT_SHIFT);
            switch (subtype) {
            case SUBTYPE_STILL: case SUBTYPE_STILL_2: break;
            case SUBTYPE_ANIMATED: animated = 1; break;
            case SUBTYPE_TOGGLE: case SUBTYPE_TOGGLE_2: animated = w & TEXT_VISIBLE; break;
            case SUBTYPE_SWITCH: case SUBTYPE_SWITCH_2: case SUBTYPE_SWITCH_3:
                frame = w & TEXT_VISIBLE;
                if (frame) {
                    animated = 1;
                    done |= 1 << cell;
                }
                break;
            default: continue;
            }
        } else if (thing_type(thing) == THING_SENSOR) {
            ornament = sensor_ornament(thing, dungeon_map_wall_ornaments(view.map));
            if (ornament == ORNAMENT_NONE) continue;
            uint16_t data = thing_word(thing, SENSOR_TYPE_DATA);
            int active = sensor_bit(thing, SENSOR_ACTIVE_BIT), state = sensor_bit(thing, SENSOR_STATE_BIT);
            switch (data & SENSOR_TYPE_MASK) {
            case SENSOR_TOGGLE_ORNAMENT: animated = state; frame = state; break;
            case SENSOR_TIMED_ORNAMENT: case SENSOR_TIMED_ORNAMENT_2: frame = active ^ state; break;
            case SENSOR_HOLDER:
                frame = state;
                if (frame) {
                    animated = 1;
                    done |= 1 << cell;
                }
                break;
            case SENSOR_STATE_MATCH:
                if (active == state) animated = 1;
                else frame = active;
                break;
            case SENSOR_ANIMATED:
                if (active) {
                    offset = data >> SENSOR_DATA_SHIFT & SENSOR_DATA_BYTE;
                    animated = 1;
                }
                break;
            case SENSOR_SOUND_ANIMATION:
                if (active) {
                    time = data >> SENSOR_DATA_SHIFT;
                    animated = 1;
                }
                break;
            case SENSOR_WALL_TEXT: s->text = face == SIDE_FRONT ? thing : THING_TEXT_NONE; break;
            case SENSOR_FRAME_DATA: frame = data >> SENSOR_DATA_SHIFT; break;
            case SENSOR_FRAME_STATE: frame = active; break;
            case SENSOR_ALTAR:
                if (thing_word(thing, SENSOR_FLAGS) & SENSOR_ALTAR_SHOWN) {
                    if (face == SIDE_FRONT) s->text = data >> SENSOR_DATA_SHIFT;
                    done |= 1 << cell;
                    animated = 1;
                }
                break;
            }
        } else {
            continue;
        }
        if (animated) frame = ornament_frame(CAT_WALL_ORNAMENT, ornament, time, offset);
        s->side[face] = (uint16_t)(frame << ORN_FRAME_SHIFT | ornament);
    }
    return thing;
}

/* The floor ornament of the floor things of a square: texts and sensors with ornaments. */
static uint16_t apply_floor_things(ViewSquare *s, uint16_t thing, uint8_t square)
{
    for (; thing != THING_ENDOFLIST && thing_type(thing) <= THING_SENSOR; thing = dungeon_next_thing(thing)) {
        int animated = 0, frame = 0, offset = 0;
        uint32_t time = game.game_time;
        uint8_t ornament;
        if (thing_type(thing) == THING_TEXT) {
            uint16_t w = thing_word(thing, TEXT_DATA);
            if ((w & (TEXT_KIND_MASK << TEXT_KIND_SHIFT)) != TEXT_KIND_ORNAMENT << TEXT_KIND_SHIFT) continue;
            ornament = (uint8_t)(w >> TEXT_ORNAMENT_SHIFT);
            switch (w >> TEXT_SUBTYPE_SHIFT & TEXT_SUBTYPE_MASK) {
            case SUBTYPE_STILL: case SUBTYPE_TRAP_TELEPORT: case SUBTYPE_WARP_MARKER_2: break;
            case SUBTYPE_ANIMATED: animated = 1; break;
            case SUBTYPE_TOGGLE: animated = w & TEXT_VISIBLE; break;
            case SUBTYPE_WARP_MARKER: frame = 1; break;
            case SUBTYPE_SQUARE_SWITCH: frame = square_flag(square, SQUARE_FLAG_3); break;
            case SUBTYPE_SHOP_STAND: s->side[SIDE_SHOP_FLAG] = 0; continue;
            default: continue;
            }
        } else if (thing_type(thing) == THING_SENSOR) {
            uint16_t data = thing_word(thing, SENSOR_TYPE_DATA);
            int type = data & SENSOR_TYPE_MASK, active = sensor_bit(thing, SENSOR_ACTIVE_BIT);
            if (type == SENSOR_SHOP) s->side[SIDE_SHOP_FLAG] = 0;
            ornament = sensor_ornament(thing, dungeon_map_floor_ornaments(view.map));
            if (ornament == ORNAMENT_NONE) continue;
            if (type == SENSOR_LINK) {
                if ((data >> SENSOR_DATA_SHIFT) - 1 != view.map) continue;
                animated = active;
            } else if (type == SENSOR_ANIMATED && active) {
                offset = data >> SENSOR_DATA_SHIFT & SENSOR_DATA_BYTE;
                animated = 1;
            } else if (type == SENSOR_SOUND_ANIMATION && active) {
                time = data >> SENSOR_DATA_SHIFT;
                animated = 1;
            }
        } else {
            continue;
        }
        if (animated) frame = ornament_frame(CAT_FLOOR_ORNAMENT, ornament, time, offset);
        s->side[SIDE_FLOOR_ORNAMENT] = (uint16_t)(ornament | frame * FLOOR_FRAME_STEP);
    }
    return thing;
}

static uint16_t skip_wall_things(uint16_t thing)
{
    while (thing != THING_ENDOFLIST && thing_type(thing) <= THING_SENSOR) thing = dungeon_next_thing(thing);
    return thing;
}

/* What the party looking to dir sees of the square (x, y) of the current map. */
static void get_square_aspect(ViewSquare *s, int dir, int x, int y)
{
    static const int8_t dx[DIR_COUNT] = {0, 1, 0, -1}, dy[DIR_COUNT] = {-1, 0, 1, 0};
    s->side[0] = s->side[1] = s->side[2] = s->side[3] = ORNAMENT_NONE;
    s->text = 0;
    uint8_t square = dungeon_square_edge(view.map, x, y);
    s->square = square;
    uint16_t thing = first_thing(x, y);
    int type = square_type(square);
    s->aspect = (uint16_t)type;
    int floor_ornament = 0, floor_things = 1;
    switch (type) {
    case SQUARE_WALL:
    case SQUARE_FAKE_WALL: {
        int fake = type == SQUARE_FAKE_WALL;
        if (fake && square_flag(square, SQUARE_FAKE_WALL_OPEN)) {
            square = FAKE_WALL_FLOOR_SQUARE;  /* an open fake wall is a floor */
            s->aspect = ASPECT_FLOOR;
            floor_ornament = 1;
            break;
        }
        if (fake) {
            s->aspect = ASPECT_WALL;
        } else {
            int allowed[VIEW_SIDE_COUNT];
            for (int k = 0; k < VIEW_SIDE_COUNT; k++) allowed[k] = square & WALL_FACE((dir + k) & 3);
            set_random_wall_ornaments(s, allowed, dir, x, y);
        }
        thing = apply_wall_things(s, thing, dir, 0);
        if (fake) {
            /* a closed fake wall away from the party row and column hides its things */
            if (game.party_x != x && game.party_y != y) {
                s->first_thing = THING_ENDOFLIST;
                return;
            }
        } else {
            for (int k = 0; k < VIEW_SIDE_COUNT; k++) {
                int d = (dir + k) & 3;
                if (square_type(dungeon_square_edge(view.map, x + dx[d], y + dy[d])) == SQUARE_DOOR)
                    s->side[k] = ORNAMENT_NONE;
            }
        }
        s->first_thing = thing;
        return;
    }
    case SQUARE_FLOOR: floor_ornament = 1; break;
    case SQUARE_PIT:
        if (square_flag(square, SQUARE_PIT_OPEN)) s->side[SIDE_PIT_UP] = square & SQUARE_PIT_VARIANT;
        else {
            s->aspect = ASPECT_FLOOR;
            floor_ornament = 1;
        }
        break;
    case SQUARE_STAIRS:
        s->aspect = (uint16_t)(ASPECT_STAIRS_FRONT + (square_flag(square, SQUARE_STAIRS_ALONG_X) != (dir & 1)));
        s->side[SIDE_STAIRS_UP] = square & SQUARE_STAIRS_UP;
        floor_things = 0;
        break;
    case SQUARE_DOOR:
        s->side[SIDE_DOOR_STATE] = square & SQUARE_DOOR_STATE;
        s->side[SIDE_DOOR_THING] = first_thing(x, y);
        if (square_flag(square, SQUARE_DOOR_ALONG_X) == (dir & 1)) {
            s->aspect = ASPECT_DOOR_SIDE;
        } else {
            s->aspect = ASPECT_DOOR_FRONT;
            int d = (dir + 1) & 3;
            apply_wall_things(s, first_thing(x + dx[d], y + dy[d]), d, 1);
        }
        floor_things = 0;
        break;
    case SQUARE_TELEPORTER:
        if (!square_flag(square, SQUARE_TELEPORTER_ON) || !square_flag(square, SQUARE_TELEPORTER_VISIBLE)) {
            s->aspect = ASPECT_FLOOR;
            s->square &= ~SQUARE_TELEPORTER_ON;
        }
        break;
    default:
        s->first_thing = thing;
        return;
    }
    if (floor_ornament) {
        uint16_t v = value_key(CAT_MAP, dungeon_map_graphics(view.map) >> MAP_GFX_SET_SHIFT & NIBBLE, MAP_KEY_FLOOR_ORNAMENT);
        if (v) {
            int frame = v & FLOOR_ORNAMENT_ANIMATED ? ornament_frame(CAT_FLOOR_ORNAMENT, v & ORN_INDEX_MASK, game.game_time, 0) : 0;
            s->side[SIDE_FLOOR_ORNAMENT] = (uint16_t)((v & ORN_INDEX_MASK) | frame * FLOOR_FRAME_STEP);
        }
    }
    s->first_thing = floor_things ? apply_floor_things(s, thing, square) : skip_wall_things(thing);
}

void view_load_square(int x, int y, int pos, int dir)
{
    move_relative(dir, view_offset[pos][1], view_offset[pos][0], &x, &y);
    ViewSquare *s = view_squares + pos;
    s->x = (int8_t)x;
    s->y = (int8_t)y;
    get_square_aspect(s, dir, x, y);
    if (!view.walking || pos != POS_PARTY) return;
    /* while walking, the party square shows what the squares beside it will show */
    if (s->aspect == ASPECT_WALL) {
        if (view_squares[POS_FRONT].aspect == ASPECT_TELEPORTER) {
            s->aspect = ASPECT_TELEPORTER;
        } else {
            s->side[0] = s->side[1] = s->side[2] = s->side[3] = ORNAMENT_NONE;
            s->text = 0;
            s->first_thing = THING_ENDOFLIST;
        }
        for (int i = 1; i <= 2; i++) {
            ViewSquare *side = view_squares + i, *ahead = view_squares + i + 3;
            side->first_thing = THING_ENDOFLIST;
            side->square = ahead->square & SQUARE_TYPE_BITS;
            side->aspect = ahead->aspect;
            if (side->aspect == ASPECT_WALL) {
                side->side[0] = side->side[1] = side->side[2] = side->side[3] = ORNAMENT_NONE;
                side->text = THING_TEXT_NONE;
            } else if (side->aspect == ASPECT_PIT) {
                side->aspect = ASPECT_FLOOR;
                side->square = 0;
            } else if (side->aspect == ASPECT_FLOOR || side->aspect == ASPECT_TELEPORTER || side->aspect == ASPECT_DOOR_SIDE) {
                side->side[SIDE_FLOOR_ORNAMENT] = ORNAMENT_NONE;
            }
        }
    } else if (s->aspect == ASPECT_DOOR_FRONT) {
        s->aspect = ASPECT_FLOOR;
        s->square = 0;
        s->side[SIDE_FLOOR_ORNAMENT] = ORNAMENT_NONE;
    }
}

void view_unhide_linked_squares(void)
{
    TeleporterLink link;
    for (int pos = 0; pos < POS_LINKED; pos++) {
        ViewSquare *s = view_squares + pos;
        if (s->aspect != ASPECT_FLOOR || !dungeon_teleporter_link(view.map, s->x, s->y, &link)) continue;
        for (int k = 0; k < 4; k++) {
            uint8_t other = linked_squares[pos][k];
            /* fix: the original reads far past the view squares for the missing neighbours */
            if (other != LINK_NONE && view_squares[other].aspect == ASPECT_HIDDEN) view_squares[other].aspect = ASPECT_WALL;
        }
    }
}

void view_mark_hidden_squares(void)
{
    uint32_t walls = 0;
    for (int pos = 0; pos < POS_FAR_ROW; pos++)
        if (view_squares[pos].aspect == ASPECT_WALL) walls |= POS_BIT(pos);
    /* the rows: positions 1-2, 3-5, 6-8, 11-13; a center wall hides the squares behind it */
    uint32_t hidden = hide_row1[walls >> 1 & 3]
                      | hide_row2[(walls & POS_BIT(3) ? walls & 6 : 0) | (walls >> 3 & 7)]
                      | hide_row3[(walls & POS_BIT(6) ? walls >> 3 & 6 : 0) | (walls >> 6 & 7)]
                      | hide_row4[(walls & POS_BIT(11) ? walls >> 6 & 6 : 0) | (walls >> 11 & 7)];
    uint32_t covered = walls | hidden;
    if (covered & POS_BIT(12) && covered & (POS_BIT(14) | POS_BIT(9))) hidden |= HIDE_FAR_LEFT;
    if (covered & POS_BIT(13) && covered & (POS_BIT(15) | POS_BIT(10))) hidden |= HIDE_FAR_RIGHT;
    for (int pos = 0; pos < VIEW_SQUARE_COUNT; pos++)
        if (hidden & POS_BIT(pos) && view_squares[pos].aspect == ASPECT_WALL) view_squares[pos].aspect = ASPECT_HIDDEN;
}

/* ---- Pictures ---- */

int view_rotate_grid5_cell(int cell, int dir)
{
    int x = cell % CELL_GRID_SIDE - 2, y = cell / CELL_GRID_SIDE - 2, t;
    switch (dir) {
    case 1: t = x; x = y; y = -t; break;
    case 2: x = -x; y = -y; break;
    case 3: t = x; x = -y; y = t; break;
    }
    return (y + 2) * CELL_GRID_SIDE + x + 2;
}

int zone_id_for_view_cell(int base, int cell, int dir)
{
    return base * ZONE_CELL_GRID + view_rotate_grid5_cell(cell, dir) + ZONE_VIEW_CELLS;
}

static int zone_id_for_view_element(int element, int offset, int use_table)
{
    return use_table ? side_ornament_zone[element] + offset : element * ZONE_CELL_GRID + ZONE_VIEW_ELEMENTS + offset;
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
        entry = depth + 9;
    }
    if (gdat_exists(draw_gdat(), CAT_MAP, view.gfx_set, TYPE_REMAP, entry))
        pal_get_dimmed_remap(draw_gdat(), palette, CAT_MAP, view.gfx_set, entry, view.shade, key1, key2, count);
    else
        pal_apply_dim_remap(palette, SHADE_FULL - ((SHADE_FULL - light) * (SHADE_FULL - view.shade) >> 6), key1, key2,
                            count);
}

/* Starts the picture of the view context: the image, its scale, place, and shading. */
static void view_setup_picture(int flip, int scale_x, int scale_y, int dx, int dy, int depth, int zone, int anchor,
                               int key, int key2, int category, int index, int entry)
{
    DrawCtx *c = gfx_ctx_init(&view.ctx, category, index, entry);
    if (scale_x != scale_y && (depth == 2 || depth == 3)) {
        uint16_t v = value_key(category, index, depth == 2 ? ORN_KEY_WIDTH_SCALE_2 : ORN_KEY_WIDTH_SCALE_3);
        if (LOW_BYTE(v)) scale_x = ((int)(HIGH_BYTE(v) << 7) / LOW_BYTE(v) + 1) >> 1;  /* fix: the original divides by 0 */
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
    gfx_ctx_prepare(c);
}

static void view_flush_picture(void) { gfx_ctx_draw(&view.ctx); }

const uint8_t *view_get_shaded_palette(int category, int index, int entry, int key)
{
    static uint8_t palette[REMAP_COLORS];  /* fix: the original has 16 bytes, which the full table overruns */
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
    gfx_ctx_init(&c, category, index, entry);
    pal_apply_dim_remap(c.palette, (uint8_t)view.shade, key, NO_KEY, &c.palette_count);
    c.transparent = (int16_t)key;
    c.dst = &view_viewport;
    c.flip = (int16_t)flip;
    if (view.walking && (zone == ZONE_CEILING || zone == ZONE_FLOOR))
        c.src_y = (int16_t)(c.src_y + (zone == ZONE_CEILING ? walk_ceiling_dy : walk_floor_dy));
    c.zone = (int16_t)zone;
    gfx_ctx_prepare(&c);
    gfx_ctx_draw(&c);
}

/* Prepares the picture of view_setup_picture into an offscreen bitmap of its size, for pictures that are put
   together before they go into the view; returns the bitmap in *out and the context in ctx. */
static Bitmap *view_render_picture_offscreen(DrawCtx *ctx, int category, int index, int entry, int scale_x,
                                             int scale_y, int depth, int flip, int key, int key2)
{
    view_setup_picture(flip, scale_x, scale_y, 0, 0, depth, ZONE_NONE, ZONE_NO_ANCHOR, key, NO_KEY, category,
                       index, entry);
    *ctx = view.ctx;
    ctx->transparent = (int16_t)key2;
    int16_t r[RECT_SIZE] = {0, 0, ctx->w, ctx->h};
    memcpy(ctx->rect, r, sizeof r);
    ctx->dst = draw_temp_bitmap(ctx->w > 0 ? ctx->w : 1, ctx->h > 0 ? ctx->h : 1, 8);
    return ctx->dst;
}

/* The transparent color of a picture after its color remap. */
static int mapped_key(const DrawCtx *c, int key) { return c->palette_count > 0 ? c->palette[(uint8_t)key] : key; }

/* Draws a finished offscreen picture into a zone of the view with the transparent color key. */
static void draw_offscreen(DrawCtx *ctx, Bitmap *offscreen, int zone, int anchor, int key)
{
    ctx->transparent = (int16_t)key;
    ctx->dst = &view_viewport;
    ctx->zone = (int16_t)zone;
    ctx->anchor = (int16_t)anchor;
    ctx->palette_count = 0;
    gfx_ctx_set_image(ctx, offscreen);
    gfx_ctx_draw(ctx);
}

/* 12b ports the rain on wet ornaments (view_draw_wet_image); until then the offscreen bitmap starts clear. */
static void view_draw_wet_image(const DrawCtx *ctx, Bitmap *offscreen, int key)
{
    gfx_fill_bitmap(offscreen, mapped_key(ctx, key));
}

/* 12b ports the things of a square (view_draw_square_things). */
static void view_draw_square_things(int pos, uint32_t cell_mask, int with_explosions)
{
    (void)pos, (void)cell_mask, (void)with_explosions;
}

static void view_clear_viewport(void)
{
    if (view.clear) gfx_fill_rect(view_viewport.pixels, view.clear_rect, view.clear_color, VIEWPORT_WIDTH, 8);
}

/* ---- Walls ---- */

static uint8_t wall_text_glyph(uint8_t c)
{
    if (c >= 'A' && c <= 'Z') return (uint8_t)(c - 'A');
    return c == '.' ? GLYPH_PERIOD : GLYPH_SPACE;
}

/* The text of a wall text thing, with lines ended by '\n'. */
static void decode_wall_text(uint16_t thing, char *out, size_t size)
{
    char *o = out, *end = out + size - 1;
    *o = '\0';
    if (thing_type(thing) != THING_TEXT) return;
    uint16_t w = thing_word(thing, TEXT_DATA);
    int kind = w >> TEXT_KIND_SHIFT & TEXT_KIND_MASK;
    if (kind > TEXT_KIND_ORNAMENT || (kind == TEXT_KIND_ORNAMENT && w >> TEXT_SUBTYPE_SHIFT != 14)) return;
    if (!(w & TEXT_VISIBLE)) return;
    if (kind == TEXT_KIND_ORNAMENT) {
        gdat_text(draw_gdat(), 3, 0, (uint8_t)(w >> TEXT_ORNAMENT_SHIFT), out, size);
        return;
    }
    int offset = w >> TEXT_ORNAMENT_SHIFT;
    const uint16_t *p = dungeon_text_data() + offset, *text_end = dungeon_text_data() + dungeon_text_word_count();
    uint16_t word = 0;
    int escape = 0;
    for (int phase = 0; p <= text_end; phase = (phase + 1) % 3) {
        if (phase == 0) {
            if (p == text_end) break;  /* fix: the original reads past the text of a damaged dungeon */
            word = *p++;
        }
        int code = word >> (TEXT_CODE_BITS * (2 - phase)) & TEXT_CODE_MASK;
        if (escape) {
            const char *s = escape == TEXT_ESCAPE_WORD ? (code < 10 ? text_escape_words[code] : "")
                                                       : (char[]){text_escape_chars[code], '\0'};
            while (*s && o < end) *o++ = *s++;
            escape = 0;
        } else if (code < TEXT_NEWLINE) {
            if (o < end) *o++ = code == TEXT_SPACE ? ' ' : code == TEXT_PERIOD ? '.' : (char)('A' + code);
        } else if (code == TEXT_NEWLINE) {
            if (o < end) *o++ = '\n';
        } else if (code == TEXT_END) {
            break;
        } else {
            escape = code;
        }
    }
    *o = '\0';
}

/* Draws the text of a front wall plate: the text goes into a bitmap of the plate size, centered, then into the view. */
static void draw_wall_text(int pos, int entry, int scale_x, int scale_y, int depth, int zone, int anchor, int key)
{
    DrawCtx plate;
    gfx_ctx_init(&plate, CAT_MAP, view.gfx_set, entry);
    const Bitmap *plate_image = draw_gdat_image(CAT_MAP, view.gfx_set, entry);
    const Bitmap *font = draw_gdat_image(CAT_MAP, view.gfx_set, MAP_FONT);
    if (!plate_image || !font) return;
    char text[WALL_TEXT_SIZE];
    decode_wall_text(view_squares[pos].text, text, sizeof text);
    int lines = 1;
    for (const char *p = text; *p; p++) lines += *p == '\n';
    int line_height = GLYPH_SIZE + FONT_SPACING;
    Bitmap *bitmap = draw_temp_bitmap(plate_image->width, plate_image->height, 4);
    if (!bitmap) return;
    gfx_fill_bitmap(bitmap, key);
    const uint8_t *font_palette = draw_gdat_image_palette(CAT_MAP, view.gfx_set, MAP_FONT);
    int16_t r[RECT_SIZE] = {0, (int16_t)((plate_image->height >> 1) - (lines * line_height >> 1)), GLYPH_SIZE, font->height};
    for (const char *p = text;;) {
        int n = 0;
        while (p[n] && p[n] != '\n') n++;
        if (n) {
            r[RECT_X] = (int16_t)((plate_image->width >> 1) - ((int16_t)(GLYPH_SIZE * n) >> 1));
            for (int i = 0; i < n && r[RECT_X] >= 0; i++, r[RECT_X] += GLYPH_SIZE)
                gfx_blit(font->pixels, bitmap->pixels, r, wall_text_glyph((uint8_t)p[i]) * GLYPH_SIZE, 0, font->width,
                         bitmap->width, key, MIRROR_NONE, font->bpp, bitmap->bpp, font_palette);
            p += n;
        }
        r[RECT_Y] = (int16_t)(r[RECT_Y] + line_height);
        if (!*p++) break;
    }
    DrawCtx c;
    gfx_ctx_init(&c, CATEGORY_NONE, 0, 0);
    c.item = -1;
    gfx_ctx_set_image(&c, bitmap);
    c.scale_x = (int16_t)scale_x;
    c.scale_y = (int16_t)scale_y;
    if (font_palette) memcpy(c.palette, font_palette, IMAGE_COLORS);
    c.palette_count = IMAGE_COLORS;
    view_get_depth_shading(c.palette, depth, key, NO_KEY, &c.palette_count);
    c.dst = &view_viewport;
    c.zone = (int16_t)zone;
    c.anchor = (int16_t)anchor;
    c.src_x = plate.dx;
    c.src_y = plate.dy;
    c.transparent = (int16_t)key;
    gfx_ctx_prepare(&c);
    gfx_ctx_draw(&c);
}

/* Draws the ornament of the wall face at side (0 front, < 0 left, > 0 right) of the wall at pos; returns its color key. */
static int view_draw_wall_ornament(int pos, int side, int record_click)
{
    int face = side < 0 ? 0 : side > 0 ? 2 : 1;  /* the side words of the left, front, right faces */
    uint16_t w = view_squares[pos].side[face + 1];
    int frame = (int8_t)(w >> 8), depth = view_depth[pos];
    uint8_t ornament = (uint8_t)w;
    if (ornament == ORNAMENT_NONE) return -1;
    int plate = ornament == ORNAMENT_PLATE;
    int kind = value_key(CAT_WALL_ORNAMENT, ornament, ORN_KEY_KIND);
    int key = plate ? 0 : value_key(CAT_WALL_ORNAMENT, ornament, ORN_KEY_COLOR);
    if (!key) key = view.wall_key;
    int cell = CELL_CENTER, anchor = 0;
    uint16_t place = plate ? 0 : value_key(CAT_WALL_ORNAMENT, ornament, ORN_KEY_PLACE);
    if (place) {
        anchor = HIGH_BYTE(place);
        cell = LOW_BYTE(place) - 1;
    }
    int zone = zone_id_for_view_element(pos, cell, side != 0);
    int scale_y = depth_scale[depth], scale_x = scale_y;
    int flip = 0;
    if (kind == KIND_PORTRAIT && side == 0) {
        uint16_t offset = gdat_entry_index(draw_gdat(), CAT_WALL_ORNAMENT, ornament, GDAT_TYPE_VALUE2, PORTRAIT_OFFSET_ENTRY);
        view_setup_picture(0, scale_y, scale_y, (int8_t)(offset >> 8), (int8_t)offset, depth, zone, anchor, NO_KEY,
                           NO_KEY, CAT_PORTRAIT, (uint8_t)view_squares[pos].text, PORTRAIT_ENTRY);
        if (!record_click) view.ctx.transparent = DRAW_NO_BLIT;
        view_flush_picture();
        if (!record_click) return key;
    }
    if (side <= -2 || side >= 2) {
        if (depth == 2) scale_x = SCALE_WIDE_SIDE;
        else if (depth == 3) scale_x = SCALE_FAR_SIDE;
    }
    if (plate) {
        int entry = MAP_PLATE_FRONT;
        if (side < 0) entry = MAP_PLATE_SIDE;
        else if (side > 0 && picture_exists(CAT_MAP, view.gfx_set, MAP_PLATE_RIGHT)) entry = MAP_PLATE_RIGHT;
        else if (side > 0) {
            entry = MAP_PLATE_SIDE;
            flip = 1;
        }
        view_setup_picture(flip, scale_x, scale_y, 0, 0, depth, zone, anchor, key, NO_KEY, CAT_MAP, view.gfx_set, entry);
        if (!record_click) view.ctx.transparent = DRAW_NO_BLIT;
        view_flush_picture();
        if (side == 0 && view_squares[pos].text != THING_TEXT_NONE)
            draw_wall_text(pos, entry, scale_x, scale_y, depth, zone, anchor, key);
        return key;
    }
    int entry;
    if (side == 0) {
        entry = 1;  /* front ornaments are not mirrored: the depth flip of SKULL.EXE is off (0x75c11 = 1) */
    } else {
        entry = 0;
        if (side > 0) {
            if (picture_exists(CAT_WALL_ORNAMENT, ornament, (uint8_t)(frame + 2))) entry = 2;
            else flip = 1;
        }
    }
    entry = (uint8_t)(entry + frame);
    if (side == 0 && value_key(CAT_WALL_ORNAMENT, ornament, ORN_KEY_WET)) {
        view_setup_picture(flip, scale_x, scale_y, 0, 0, depth, zone, anchor, DRAW_NO_IMAGE, DRAW_NO_IMAGE,
                           CAT_WALL_ORNAMENT, ornament, entry);
        view_setup_picture(flip, scale_x, scale_y, view.ctx.dx, view.ctx.dy, depth, zone, anchor, key, NO_KEY,
                           CAT_WALL_ORNAMENT, ornament, ORN_WET_BACKGROUND);
        if (!record_click) view.ctx.transparent = DRAW_NO_BLIT;
        view_flush_picture();
        DrawCtx c;
        Bitmap *offscreen = view_render_picture_offscreen(&c, CAT_WALL_ORNAMENT, ornament, entry, scale_x, scale_y,
                                                          depth, flip, key,
                                                          (int16_t)value_key(CAT_WALL_ORNAMENT, ornament, ORN_KEY_OFFSCREEN_COLOR));
        if (offscreen) {
            view_draw_wet_image(&c, offscreen, key);
            gfx_ctx_draw(&c);
            draw_offscreen(&c, offscreen, zone, anchor, mapped_key(&c, key));
        }
    } else {
        view_setup_picture(flip, scale_x, scale_y, 0, 0, depth, zone, anchor, key, NO_KEY, CAT_WALL_ORNAMENT, ornament,
                           entry);
        if (!record_click) view.ctx.transparent = DRAW_NO_BLIT;
        view_flush_picture();
        if (!record_click) return key;
    }
    /* Step 14 adds the click zone of the ornament (view_add_click_zone) */
    if (kind && side == 0) {
        if (picture_exists(CAT_WALL_ORNAMENT, ornament, ORN_OVERLAY)) {
            if (kind != KIND_SHOP) {  /* Step 15 ports the shop display (shop_draw_display) */
                view_setup_picture(flip, scale_x, scale_y, 0, 0, depth, zone, anchor, key, NO_KEY, CAT_WALL_ORNAMENT,
                                   ornament, ORN_OVERLAY);
                if (!record_click) view.ctx.transparent = DRAW_NO_BLIT;
                view_flush_picture();
            }
        }
        /* 12b ports the alcove contents (view_draw_alcove_contents) */
    }
    return key;
}

/* Draws the wall block at pos with the clip margins that open squares beside it need. */
static void view_draw_wall_block(int pos)
{
    int lateral = view_lateral[pos], depth = view_depth[pos];
    uint16_t margins = value_key(CAT_MAP, view.gfx_set, MAP_KEY_WALL_MARGINS + (depth > 1));
    int left = 0, right = 0;
    if (depth > 1) {
        if (view_squares[4].aspect == ASPECT_WALL) left = HIGH_BYTE(margins);
        if (view_squares[5].aspect == ASPECT_WALL) right = LOW_BYTE(margins);
    } else if (depth > 0) {
        if (view_squares[1].aspect == ASPECT_WALL) left = HIGH_BYTE(margins);
        if (view_squares[2].aspect == ASPECT_WALL) right = LOW_BYTE(margins);
    }
    if (left || right) zone_set_clip_margins(left, 0, right, 0);
    int flip = lateral > 0, entry;
    if (pos >= POS_FAR_ROW) {
        if (lateral == -2 || lateral == 2) flip = 0;
        flip ^= view.flip;
        entry = MAP_FAR_WALL;
    } else if (view.flip) {
        entry = (uint8_t)(view_mirror[pos] + MAP_WALL_FLIPPED);
        if (!picture_exists(CAT_MAP, view.gfx_set, entry)) {
            if (lateral == 0) flip = 1;
            entry -= MAP_WALL_FLIPPED_MISSING;
        }
    } else {
        entry = pos + MAP_WALL;
    }
    view_setup_picture(flip, SCALE_FULL, SCALE_FULL, 0, 0, view.walking ? -depth : 0, ZONE_WALL + pos, ZONE_NO_ANCHOR,
                       view.wall_key, NO_KEY, CAT_MAP, view.gfx_set, entry);
    view_flush_picture();
    zone_clip_rect[RECT_W] = 0;
}

static void view_draw_wall_square(int pos)
{
    view_draw_wall_block(pos);
    switch (wall_faces[pos]) {
    case WALL_SHOWS_FRONT: view_draw_wall_ornament(pos, 0, 1); break;
    case WALL_SHOWS_BOTH: view_draw_wall_ornament(pos, 0, 1); /* fall through */
    case WALL_SHOWS_SIDE: view_draw_wall_ornament(pos, view_lateral[pos], 1); break;
    }
}

/* ---- Floors, pits, stairs ---- */

/* Draws the floor ornament of the square at pos. A seamless passage there shows the squares behind it.
   Returns 1 for a passage that was drawn. */
static int view_draw_floor_ornament(int pos)
{
    int entry = floor_ornament_entry[pos];
    if (entry < 0) return 0;
    ViewSquare *s = view_squares + pos;
    int depth = view_depth[pos];
    TeleporterLink link;
    int linked = dungeon_teleporter_link(view.map, s->x, s->y, &link);
    if (depth > 3 && !linked) return 0;
    if (linked && pos != POS_NEAR_SIDE_1 && pos != POS_NEAR_SIDE_2 && link.side != game.party_dir
        && link.side != ((game.party_dir + 2) & 3))
        return 0;
    uint16_t w = s->side[SIDE_FLOOR_ORNAMENT];
    int frame = w >> 8;
    entry = (uint8_t)(entry + frame);
    uint8_t ornament = (uint8_t)w;
    if (ornament == ORNAMENT_NONE || ornament == 0) return 0;
    if ((pos == POS_FAR_SIDE_1 || pos == POS_FAR_SIDE_2) && value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_NOT_FAR_SIDE))
        return 0;
    int flip = 0;
    if (view_lateral[pos] == 0) {
        if (!value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_NO_FLIP)) flip = view.flip ^ !(depth & 1);
    } else if (view_lateral[pos] >= 1) {
        flip = 1;
    }
    int key = value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_COLOR);
    if (!key) key = view.wall_key;
    int cell = CELL_CENTER, anchor = 0;
    uint16_t place = value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_PLACE);
    if (place) {
        anchor = HIGH_BYTE(place);
        cell = LOW_BYTE(place) - 1;
    }
    int zone = zone_id_for_view_cell(pos, cell, 0), scale = SCALE_FULL;
    if (!picture_exists(CAT_FLOOR_ORNAMENT, ornament, entry)) {
        entry = (uint8_t)(floor_ornament_scaled_entry[pos] + frame);
        scale = depth_scale[depth];
    }
    if ((pos == POS_NEAR_SIDE_1 || pos == POS_NEAR_SIDE_2) && value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_BIG_NEAR))
        scale = SCALE_BIG_NEAR;
    int wet = depth ? value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_WET) : 0;
    if (!depth || (!linked && !wet)) {
        view_setup_picture(flip, scale, scale, 0, 0, depth, zone, anchor, key, NO_KEY, CAT_FLOOR_ORNAMENT, ornament, entry);
        view_flush_picture();
        return 0;
    }
    view_setup_picture(flip, scale, scale, 0, 0, depth, zone, anchor, DRAW_NO_IMAGE, DRAW_NO_IMAGE, CAT_FLOOR_ORNAMENT,
                       ornament, entry);
    if (view_lateral[pos] == 0) {
        view_setup_picture(flip, scale, scale, view.ctx.dx, view.ctx.dy, depth, zone, anchor, key, NO_KEY,
                           CAT_FLOOR_ORNAMENT, ornament, (uint8_t)(entry + ORN_WET_BACKGROUND));
        view_flush_picture();
        /* the squares behind the passage, on the linked map */
        int x = link.x, y = link.y, saved_dir = view.party_dir, saved_map = view.map;
        view.party_dir = link.target_side;
        view.map = link.map;
        move_relative(view.party_dir, -depth, 0, &x, &y);
        for (int d = POS_DEPTH_MAX; d >= depth; d--) view_load_square(x, y, depth_center[d], view.party_dir);
        for (int d = POS_DEPTH_MAX; d > depth; d--) view_draw_square_things(depth_center[d], THINGS_ALL, 1);
        view.map = saved_map;
        view.party_dir = saved_dir;
        view_draw_square_things(pos, THINGS_NEAR, 1);
    }
    DrawCtx c;
    Bitmap *offscreen = view_render_picture_offscreen(&c, CAT_FLOOR_ORNAMENT, ornament, entry, scale, scale, depth, flip,
                                                      key, (int16_t)value_key(CAT_FLOOR_ORNAMENT, ornament,
                                                                              ORN_KEY_OFFSCREEN_COLOR));
    if (!offscreen) return linked;
    int transparent = mapped_key(&c, key);
    if (wet) view_draw_wet_image(&c, offscreen, key);
    else gfx_fill_bitmap(offscreen, transparent);
    gfx_ctx_draw(&c);
    draw_offscreen(&c, offscreen, zone, anchor, transparent);
    return linked;
}

/* Draws the hole of an open pit on the level above. */
static void view_draw_ceiling_pit(int pos)
{
    if (pos > POS_CEILING_PIT_LAST || !(view.flip_flags & FLIP_CEILING_PITS)) return;
    int x = view_squares[pos].x, y = view_squares[pos].y;
    int map = dungeon_location_on_map(view.map, -1, &x, &y);
    if (map < 0) return;
    /* fix: the original reads the square without a bounds check; outside the map it is a wall */
    uint8_t square = dungeon_square_edge(map, x, y);
    if (square_type(square) != SQUARE_PIT || !square_flag(square, SQUARE_PIT_OPEN)) return;
    int flip = pos == POS_PARTY ? view_get_flip_parity(PARITY_CEILING, view.party_dir, view.party_x, view.party_y) != 0
                                : ceiling_pit_flip[pos];
    view_draw_picture_in_zone(CAT_MAP, view.gfx_set, ceiling_pit_entry[pos], ceiling_pit_zone[pos], view.wall_key, flip);
}

static void view_draw_floor_pit(int pos)
{
    int16_t zone = floor_pit_zone[pos];
    if (zone < 0) return;
    int up = view_squares[pos].side[SIDE_PIT_UP] != 0;
    if (pos >= POS_FLOOR_PIT_BLOCKED && up) return;
    int flip = pos == POS_PARTY ? view_get_flip_parity(PARITY_FLOOR, view.party_dir, view.party_x, view.party_y) != 0
                                : floor_pit_flip[pos];
    view_draw_picture_in_zone(CAT_MAP, view.gfx_set, up ? floor_pit_entry_up[pos] : floor_pit_entry[pos], zone,
                              view.wall_key, flip);
}

static void view_draw_stairs_side(int pos)
{
    int up = view_squares[pos].side[SIDE_STAIRS_UP] != 0;
    int16_t zone = stairs_side_zone[pos][up];
    if (zone < 0) return;
    uint8_t entry = stairs_side_entry[pos][up];
    if (picture_exists(CAT_MAP, view.gfx_set, entry)) {
        view_draw_picture_in_zone(CAT_MAP, view.gfx_set, entry, zone, view.wall_key, 0);
        return;
    }
    view_setup_picture(1, SCALE_FULL, SCALE_FULL, 0, 0, 0, zone, ZONE_NO_ANCHOR, view.wall_key, NO_KEY, CAT_MAP,
                       view.gfx_set, stairs_side_flipped_entry[pos][up]);
    view_flush_picture();
}

static void view_draw_stairs_front(int pos)
{
    if (pos > POS_CEILING_PIT_LAST) return;
    int up = view_squares[pos].side[SIDE_STAIRS_UP] != 0;
    uint8_t entry = stairs_front_entry[pos][up];
    if (entry != ENTRY_NONE)
        view_draw_picture_in_zone(CAT_MAP, view.gfx_set, entry, stairs_front_zone[pos][up], view.wall_key, 0);
}

/* ---- Doors ---- */

static int view_inverse_scale(unsigned size, unsigned scaled)
{
    int scale = (int)((size * 128 + 64) / (uint16_t)scaled) >> 1;
    return gfx_scale_64((int16_t)scaled, (int16_t)scale) < (int)size ? scale + 1 : scale;
}

static void view_draw_door_ornament(int category, int ornament, int entry, int pos)
{
    int row = door_ornament_row[pos];
    if (row < 0 || ornament == ORNAMENT_NONE) return;
    int depth = view_depth[pos], scale = depth_scale[depth], base = 0, key;
    if (category == CAT_DOOR_BUTTON) {
        base = value_key(CAT_DOOR_BUTTON, ornament, ORN_KEY_ZONE_ROW) * DOOR_BUTTON_FRAMES;
        key = view.wall_key;
    } else {
        key = (int16_t)value_key(category, ornament, ORN_KEY_COLOR);
        if (!key) key = view.wall_key;
    }
    view_setup_picture(0, scale, scale, 0, 0, depth, base + ZONE_DOOR_ORNAMENT + row, ZONE_NO_ANCHOR, key, NO_KEY,
                       category, ornament, entry);
    view_flush_picture();
    /* Step 14 adds the click zone of the door button */
}

static void view_draw_door_frame(int pos, int parts)
{
    uint16_t door = view_squares[pos].side[SIDE_DOOR_THING];
    if (value_key(CAT_DOOR, door_type(door), DOOR_KEY_NO_FRAME)) return;
    if (parts & DOOR_FRAME_TOP && pos < DOOR_TOP_POSITIONS && door_top_frame[pos] >= 0)
        view_draw_picture_in_zone(CAT_MAP, view.gfx_set, door_top_frame[pos], door_top_zone[pos], view.wall_key, 0);
    int mpos = view.flip ? view_mirror[pos] : pos, left = view.flip ? 1 : 0;
    uint8_t entry = door_side_frame[mpos][left];
    if (parts & DOOR_FRAME_LEFT && entry != ENTRY_NONE) {
        view_setup_picture(0, SCALE_FULL, SCALE_FULL, 0, 0, 0, zone_id_for_view_cell(pos, 10, 0), ANCHOR_DOOR_RIGHT,
                           view.wall_key, NO_KEY, CAT_MAP, view.gfx_set, entry);
        view_flush_picture();
    }
    entry = door_side_frame[mpos][!left];
    if (!(parts & DOOR_FRAME_RIGHT) || entry == ENTRY_NONE) return;
    view_setup_picture(1, SCALE_FULL, SCALE_FULL, 0, 0, 0, zone_id_for_view_cell(pos, 14, 0), ANCHOR_DOOR_LEFT,
                       view.wall_key, NO_KEY, CAT_MAP, view.gfx_set, entry);
    view_flush_picture();
    uint16_t data = thing_word(door, DOOR_DATA);
    if (data & DOOR_BUTTON) {
        view_draw_door_ornament(CAT_DOOR_BUTTON, 0, (data >> DOOR_BUTTON_PRESSED_SHIFT & 1) * DOOR_BUTTON_FRAMES, pos);
    } else if (view_squares[pos].side[SIDE_FLOOR_ORNAMENT] != ORNAMENT_NONE) {
        uint16_t w = view_squares[pos].side[SIDE_FLOOR_ORNAMENT];
        view_draw_door_ornament(CAT_WALL_ORNAMENT, (uint8_t)w, (uint8_t)((int8_t)(w >> 8) + 1), pos);
    }
}

static void view_draw_door(int pos, int frame_before, int frame_after, uint32_t cell_mask)
{
    if (frame_before) view_draw_door_frame(pos, frame_before);
    if (cell_mask) view_draw_square_things(pos, cell_mask, 0);
    ViewSquare *s = view_squares + pos;
    int state = s->side[SIDE_DOOR_STATE], depth = view_depth[pos];
    int row = depth <= 3 ? door_depth_row[depth] : -1, zone = door_zone[pos];
    if (state != DOOR_OPEN && depth <= 3 && row >= 0 && zone >= 0) {
        uint16_t door = s->side[SIDE_DOOR_THING], data = thing_word(door, DOOR_DATA);
        uint8_t type = door_type(door);
        int key = (int16_t)value_key(CAT_DOOR, type, ORN_KEY_COLOR);
        if (!key) key = DOOR_KEY_DEFAULT;
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
        int ornament = data >> DOOR_ORNAMENT_SHIFT & DOOR_ORNAMENT_MASK;
        if (!ornament && state != DOOR_DESTROYED) {
            view_setup_picture(0, scale, scale, 0, 0, shade_depth, ZONE_NONE, ZONE_NO_ANCHOR, key, NO_KEY, CAT_DOOR, type,
                               entry);
        } else {
            /* the door with its ornament or damage is put together offscreen */
            DrawCtx c;
            Bitmap *offscreen = view_render_picture_offscreen(&c, CAT_DOOR, type, entry, scale, scale, shade_depth, 0,
                                                              key, NO_KEY);
            if (offscreen) {
                gfx_ctx_draw(&c);
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
                    int okey = (int16_t)value_key(CAT_DOOR_ORNAMENT, o, ORN_KEY_COLOR);
                    if (!okey) okey = ORNAMENT_KEY_DEFAULT;
                    int ozone = value_key(CAT_DOOR_ORNAMENT, o, ORN_KEY_ZONE_ROW) * ZONE_ROW_SIZE + ZONE_ORNAMENT_ROWS + row;
                    view_setup_picture(0, scale_x, scale_y, 0, 0, depth, ozone, ZONE_NO_ANCHOR, okey, NO_KEY,
                                       CAT_DOOR_ORNAMENT, o, 0);
                    view.ctx.dst = offscreen;
                    view_flush_picture();
                }
                if (state == DOOR_DESTROYED) {
                    int dkey = (int16_t)value_key(CAT_DOOR, type, ORN_KEY_DAMAGE_COLOR);
                    if (!dkey) dkey = ORNAMENT_KEY_DEFAULT;
                    int dzone = value_key(CAT_DOOR, type, ORN_KEY_KIND) * ZONE_ROW_SIZE + ZONE_ORNAMENT_ROWS + row;
                    view_setup_picture(0, scale_x, scale_y, 0, 0, depth, dzone, ZONE_NO_ANCHOR, dkey, key, CAT_DOOR, type,
                                       DOOR_DAMAGE);
                    view.ctx.dst = offscreen;
                    view_flush_picture();
                }
                c.transparent = (int16_t)mapped_key(&c, key);
                c.palette_count = 0;
                gfx_ctx_set_image(&c, offscreen);
                view.ctx = c;
                view.ctx.dst = &view_viewport;
            }
        }
        if (state < 4) {
            zone += state;
            if (!(data & DOOR_NO_SLIDE)) {
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
    if (frame_after) view_draw_door_frame(pos, frame_after);
}

static void view_draw_door_front(int pos)
{
    if (!door_front_shown[pos]) return;
    uint32_t mask = view_squares[pos].side[SIDE_DOOR_STATE] == DOOR_DESTROYED ? 0 : THINGS_BEHIND_DOOR;
    view_draw_square_things(pos, THINGS_NEAR, pos != POS_PARTY);
    static const uint8_t frames[POS_FAR_ROW][2] = {
        {6, 0}, {0, 0}, {0, 0}, {7, 0}, {1, 4}, {1, 2}, {7, 0}, {1, 4}, {1, 2}, {0, 0}, {0, 0}, {6, 0}, {2, 4},
        {4, 2}, {0, 0}, {0, 0},
    };
    if (pos == POS_PARTY) {
        if (mask) view_draw_square_things(pos, THINGS_DOOR_CELL, 0);
        view_draw_door(pos, frames[pos][0], frames[pos][1], 0);
        return;
    }
    if (pos != POS_NEAR_SIDE_1 && pos != POS_NEAR_SIDE_2 && pos != 9 && pos != 10)
        view_draw_door(pos, frames[pos][0], frames[pos][1], mask);
    view_draw_square_things(pos, THINGS_FAR, 1);
    /* 12b ports the possessions of dying creatures (view_draw_pending_possessions) */
}

/* ---- The sky of outdoor maps ---- */

/* The lateral and forward distance from the party at (*x, *y) to (x2, y2) looking to dir, and the distance;
   0 for a point not in front. */
static int outdoor_relative_distance(int dir, int16_t *x, int16_t *y, int x2, int y2)
{
    int16_t forward = 0, lateral = 0;
    switch (dir) {
    case 0: forward = (int16_t)(*y - y2); lateral = (int16_t)(x2 - *x); break;
    case 1: forward = (int16_t)(x2 - *x); lateral = (int16_t)(y2 - *y); break;
    case 2: forward = (int16_t)(y2 - *y); lateral = (int16_t)(*x - x2); break;
    case 3: forward = (int16_t)(*x - x2); lateral = (int16_t)(*y - y2); break;
    }
    *x = lateral;
    *y = forward;
    if (forward < 1) return 0;
    uint16_t n = (uint16_t)(lateral * lateral + forward * forward);
    if (n <= 2) return 1;
    uint16_t root = n >> 1, next;
    for (;;) {  /* integer square root by Newton steps */
        next = (uint16_t)((n / root + root) >> 1);
        if (next >= root) return root;
        root = next;
    }
}

/* Places a landmark of the sky text definition for the party; returns 0 when it is not visible. */
static int outdoor_place_sky_object(SkyObject *o, const char *definition, int dir, int x, int y)
{
    o->dx = o->dy = 0;
    int move = (int16_t)text_find_int_field(definition, "mv");
    if (move == SKY_FIXED) {
        o->scale_x = o->scale_y = SCALE_FULL;
        return 1;
    }
    if (move != SKY_LANDMARK) return 0;
    int16_t px = (int16_t)(x + dungeon_map_offset_x(view.map)), py = (int16_t)(y + dungeon_map_offset_y(view.map));
    int lx = (int16_t)text_find_int_field(definition, "xl"), ly = (int16_t)text_find_int_field(definition, "yl");
    int distance = outdoor_relative_distance(dir, &px, &py, lx, ly);
    if (py < 1) return 0;
    int16_t near = (int16_t)(SHADE_FULL - (distance - text_find_int_field(definition, "fd")));
    int scale = near > 1 ? near : 1;
    o->dx = (int16_t)(px * SKY_LANDMARK_DISTANCE / distance);
    o->scale_x = o->scale_y = (uint8_t)(((scale << 7) / SHADE_FULL + 1) >> 1);
    return 1;
}

static void outdoor_draw_sky_object(const SkyObject *o, int dir, int x, int y)
{
    if (!o->zone) return;
    int flip = (o->flip == SKY_FLIP_FLOOR && view_get_flip_parity(PARITY_FLOOR, dir, x, y))
               || (o->flip == SKY_FLIP_CEILING && view_get_flip_parity(PARITY_CEILING, dir, x, y))
               || (o->flip == SKY_FLIP_FLOOR_2 && view_get_flip_parity(PARITY_FLOOR, dir, x, y))
               || (o->flip == SKY_FLIP_CEILING_2 && view_get_flip_parity(PARITY_CEILING, dir, x, y));
    int scale_x = o->scale_x, scale_y = o->scale_y;
    int16_t dx = o->dx, dy = o->dy;
    if (view.walking) {
        dx = gfx_scale_64(dx, WALK_SCALE);
        dy = gfx_scale_64(dy, WALK_SCALE);
        scale_x = gfx_scale_64((int16_t)scale_x, WALK_SCALE);
        scale_y = gfx_scale_64((int16_t)scale_y, WALK_SCALE);
        if (o->scale_x == SCALE_FULL) {
            dx = (int16_t)(dx + walk_view_x);
            dy = (int16_t)(dy + (o->zone == ZONE_SKY_HIGH ? walk_sky_high_dy : walk_view_y));
        } else {
            dy = (int16_t)(dy + walk_sky_dy);
        }
    }
    view_setup_picture(flip, scale_x, scale_y, 0, 0, 0, (int16_t)(o->zone | ZONE_RELATIVE), ZONE_NO_ANCHOR, view.wall_key,
                       NO_KEY, CAT_SKY, view.gfx_set, o->entry);
    view.ctx.src_x = (int16_t)(view.ctx.src_x + dx);
    view.ctx.src_y = (int16_t)(view.ctx.src_y + dy);
    view_flush_picture();
}

static void outdoor_draw_sky(int dir, int x, int y)
{
    if (game.weather_visible)
        for (const SkyObject *o = view.sky; o->entry != SKY_OBJECT_END; o++) outdoor_draw_sky_object(o, dir, x, y);
    char text[WALL_TEXT_SIZE];
    if (view.landmark_set != view.gfx_set) {
        view.landmark_set = view.gfx_set;
        view.no_landmarks = 1;
        memset(view.landmarks, 0, sizeof view.landmarks);
        for (int e = 0; e < SKY_LANDMARK_MAX; e++)
            if (picture_exists(CAT_SKY, view.gfx_set, e) && *gdat_text(draw_gdat(), CAT_SKY, view.gfx_set, e, text, sizeof text)) {
                view.landmarks[e >> 3] |= (uint8_t)(1 << (e & 7));
                view.no_landmarks = 0;
            }
    }
    if (view.no_landmarks) return;
    /* like the original, the landmarks use the first sky object as their record */
    SkyObject *o = view.sky;
    for (int e = 0; e < SKY_LANDMARK_MAX; e++) {
        if (!(view.landmarks[e >> 3] & 1 << (e & 7))) continue;
        gdat_text(draw_gdat(), CAT_SKY, view.gfx_set, e, text, sizeof text);
        if (!outdoor_place_sky_object(o, text, dir, x, y)) continue;
        o->entry = (uint8_t)e;
        o->zone = (int16_t)text_find_int_field(text, "cd");
        o->flip = (uint8_t)text_find_int_field(text, "fw");
        outdoor_draw_sky_object(o, dir, x, y);
    }
}

/* ---- The view ---- */

static void view_draw_far_squares(void)
{
    uint32_t cell_mask = THINGS_ALL;  /* like the original, kept from square to square */
    for (int i = 0; i < POS_DRAWN; i++) {
        int pos = far_order[i], aspect = view_squares[pos].aspect;
        if (pos >= POS_FAR_ROW) {
            if (aspect == ASPECT_WALL) view_draw_wall_square(pos);
            else if (aspect == ASPECT_FLOOR || aspect == ASPECT_PIT || aspect == ASPECT_TELEPORTER) view_draw_floor_ornament(pos);
            /* 12b draws the creatures here (view_draw_creatures) */
            continue;
        }
        switch (aspect) {
        case ASPECT_WALL: view_draw_wall_square(pos); continue;
        case ASPECT_DOOR_FRONT: view_draw_door_front(pos); continue;
        case ASPECT_STAIRS_FRONT: view_draw_stairs_front(pos); break;
        case ASPECT_STAIRS_SIDE: view_draw_stairs_side(pos); break;
        case ASPECT_PIT: view_draw_floor_pit(pos); /* fall through */
        case ASPECT_FLOOR:
        case ASPECT_TELEPORTER:
        case ASPECT_DOOR_SIDE:
            if (aspect == ASPECT_DOOR_SIDE && pos == POS_FRONT
                && !value_key(CAT_DOOR, door_type(view_squares[POS_FRONT].side[SIDE_DOOR_THING]), DOOR_KEY_NO_FRAME)) {
                view_setup_picture(view.flip, DOOR_SIDE_SCALE, DOOR_SIDE_SCALE, 0, 0, 2, zone_id_for_view_cell(POS_FRONT, 2, 0),
                                   ZONE_NO_ANCHOR, view.wall_key, NO_KEY, CAT_MAP, view.gfx_set, MAP_DOOR_SIDE);
                view_flush_picture();
            }
            cell_mask = view_draw_floor_ornament(pos) ? THINGS_FAR : THINGS_ALL;
            view_draw_ceiling_pit(pos);
            break;
        default: continue;
        }
        view_draw_square_things(pos, cell_mask, 1);
        /* 12b draws the sparkles of a teleporter here (view_draw_sparkle_field) */
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
            if (!value_key(CAT_DOOR, door_type(view_squares[POS_PARTY].side[SIDE_DOOR_THING]), DOOR_KEY_NO_FRAME))
                view_draw_picture_in_zone(CAT_MAP, view.gfx_set, MAP_DOOR_SIDE, zone_id_for_view_cell(POS_PARTY, 2, 0),
                                          view.wall_key, view.flip);
        } else if (aspect == ASPECT_STAIRS_SIDE) {
            int up = view_squares[POS_PARTY].side[SIDE_STAIRS_UP] != 0;
            view_draw_picture_in_zone(CAT_MAP, view.gfx_set, up ? MAP_STAIRS_SIDE_UP : MAP_STAIRS_SIDE_DOWN,
                                      up ? ZONE_PARTY_STAIRS_UP : ZONE_PARTY_STAIRS_DOWN, view.wall_key, 0);
            view_draw_picture_in_zone(CAT_MAP, view.gfx_set, (up ? MAP_STAIRS_SIDE_UP : MAP_STAIRS_SIDE_DOWN) + 1,
                                      (up ? ZONE_PARTY_STAIRS_UP : ZONE_PARTY_STAIRS_DOWN) + 1, view.wall_key, 0);
        }
        view_draw_ceiling_pit(POS_PARTY);
        linked = dungeon_teleporter_link(view.map, view_squares[POS_PARTY].x, view_squares[POS_PARTY].y, &link);
        if (!linked) view_draw_floor_ornament(POS_PARTY);
        view_draw_square_things(POS_PARTY, THINGS_ALL, 0);
    }
    /* 12b draws the rain (weather_draw_rain) */
    if (linked) view_draw_floor_ornament(POS_PARTY);
    /* 12b draws the explosions on the party square (view_draw_explosions) */
}

void view_draw_dungeon(int dir, int x, int y)
{
    view.map = view.party_map = game.party_map;
    view.party_dir = dir;
    view.party_x = x;
    view.party_y = y;
    view.shade = view_light_level * LIGHT_STEP;
    view.walking = 0;
    zone_walk_clip = view.walking;
    view_clear_viewport();
    for (int pos = VIEW_SQUARE_COUNT - 1; pos >= 0; pos--) view_load_square(x, y, pos, dir);
    int margins = -1;
    if (view_squares[7].aspect == ASPECT_WALL && view_squares[6].aspect == ASPECT_WALL && view_squares[8].aspect == ASPECT_WALL)
        margins = MAP_KEY_FAR_MARGINS;
    if (view_squares[4].aspect == ASPECT_WALL && view_squares[3].aspect == ASPECT_WALL && view_squares[5].aspect == ASPECT_WALL)
        margins = MAP_KEY_NEAR_MARGINS;
    uint16_t m = margins < 0 ? 0 : value_key(CAT_MAP, view.gfx_set, margins);
    zone_set_clip_margins(0, 0, 0, LOW_BYTE(m));
    view_draw_picture_in_zone(CAT_MAP, view.gfx_set, MAP_CEILING, ZONE_CEILING, NO_KEY,
                              view_get_flip_parity(PARITY_CEILING, dir, x, y) != 0);
    zone_clip_rect[RECT_W] = 0;
    zone_set_clip_margins(0, HIGH_BYTE(m), 0, 0);
    view_draw_picture_in_zone(CAT_MAP, view.gfx_set, MAP_FLOOR, ZONE_FLOOR, NO_KEY,
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
