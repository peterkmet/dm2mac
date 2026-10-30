/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The input tree of SKULL.EXE: the input tables, and the lookup of the command for a click or a key. */
#include <string.h>
#include "action.h"
#include "advanced.h"
#include "dialog.h"
#include "game.h"
#include "ids.h"
#include "input_internal.h"
#include "minion.h"
#include "zone.h"

/* Bits of an input tree node and of a child list entry. */
#define NODE_HAS_CHILDREN 0x80
#define NODE_INDEX_MASK   0x7f /* a child list entry: the node index */

/* Bits of the command and key lists: the list start, the command, and the off flag of a command entry. */
#define LIST_START   0x8000
#define COMMAND_MASK 0x07ff
#define COMMAND_OFF  0x0008

/* Bits of the layout zone of an input zone or a command entry. */
#define ZONE_IN_PANEL       0x8000
#define ZONE_IN_ACTION_AREA 0x4000
#define ZONE_ID_MASK        0x3fff

/* Changes of a cursor zone when the active input zones change. */
#define CURSOR_ADD    0x01
#define CURSOR_REMOVE 0x02

/* The conditions of the input tree nodes with children. */
#define NODE_IF_ALWAYS            0
#define NODE_IF_SCREEN_MODE       1
#define NODE_IF_BUTTON_COUNT      2
#define NODE_IF_INVENTORY         3
#define NODE_IF_CHAMPION_ALIVE    4
#define NODE_IF_CHAMPION_IN_CELL  5
#define NODE_IF_DRAGGING          6
#define NODE_IF_ICON_IN_CELL      7
#define NODE_IF_ACTION_COUNT      8
#define NODE_IF_RUNE_SET          9
#define NODE_IF_MINION_MAP_LAYOUT 10
#define NODE_IF_ACTION_MODE       11

/* The condition argument of a minion map without option buttons. */
#define MAP_WITHOUT_OPTIONS 5

typedef struct {
    uint16_t zone;
    int16_t commands, keys;
    uint8_t cursor;
} InputZone;
typedef struct {
    uint16_t command, key;
} InputKey;

/* The input tree nodes: condition (bit 7: the node has children), condition argument, child list or zone. */
static const InputNode input_nodes[INPUT_NODE_COUNT] = {
    {0x00, 0,   0 },
    {0x00, 0,   1 },
    {0x00, 0,   2 },
    {0x00, 0,   3 },
    {0x00, 0,   4 },
    {0x00, 1,   5 },
    {0x00, 5,   6 },
    {0x00, 5,   7 },
    {0x83, 0,   4 },
    {0x00, 2,   8 },
    {0x00, 6,   9 },
    {0x00, 6,   10},
    {0x83, 1,   5 },
    {0x00, 3,   11},
    {0x00, 7,   12},
    {0x00, 7,   13},
    {0x83, 2,   6 },
    {0x00, 4,   14},
    {0x00, 8,   15},
    {0x00, 8,   16},
    {0x83, 3,   7 },
    {0x84, 0,   8 },
    {0x00, 0,   17},
    {0x00, 0,   18},
    {0x00, 0,   19},
    {0x00, 0,   20},
    {0x00, 0,   21},
    {0x00, 0,   22},
    {0x00, 0,   23},
    {0x00, 1,   25},
    {0x00, 0,   26},
    {0x00, 1,   27},
    {0x00, 2,   28},
    {0x00, 3,   29},
    {0x85, 0,   10},
    {0x86, 99,  11},
    {0x00, 0,   30},
    {0x00, 1,   31},
    {0x00, 2,   32},
    {0x00, 3,   33},
    {0x87, 0,   12},
    {0x00, 1,   34},
    {0x00, 2,   35},
    {0x00, 3,   36},
    {0x88, 1,   13},
    {0x00, 0,   37},
    {0x00, 1,   38},
    {0x00, 2,   39},
    {0x00, 3,   40},
    {0x00, 5,   41},
    {0x8a, 3,   14},
    {0x00, 255, 42},
    {0x00, 15,  43},
    {0x00, 30,  44},
    {0x89, 2,   15},
    {0x00, 5,   45},
    {0x00, 6,   47},
    {0x82, 1,   16},
    {0x82, 0,   17},
    {0x8b, 0,   19},
    {0x00, 4,   46},
    {0x00, 7,   48},
    {0x00, 7,   49},
    {0x00, 1,   50},
    {0x00, 2,   51},
    {0x00, 3,   52},
    {0x00, 1,   53},
    {0x00, 2,   54},
    {0x00, 3,   55},
    {0x00, 5,   58},
    {0x00, 6,   59},
    {0x00, 5,   60},
    {0x00, 6,   61},
    {0x00, 4,   56},
    {0x00, 4,   57},
    {0x00, 0,   24},
};
/* The child lists of the nodes: node indices, bit 7 starts a list. */
static const uint8_t input_children[INPUT_CHILD_COUNT] = {
    0x80, 0x81, 0x82, 0x84, 0x85, 0x06, 0x07, 0x89, 0x0a, 0x0b, 0x8d, 0x0e, 0x0f, 0x91, 0x12, 0x13, 0x88,
    0x0c, 0x10, 0x14, 0xcb, 0x9e, 0x1f, 0x20, 0x21, 0xa2, 0x1d, 0xa4, 0x25, 0x26, 0x27, 0x23, 0xa9, 0x2a,
    0x2b, 0xad, 0x2e, 0x2f, 0x30, 0x31, 0xb3, 0x34, 0x35, 0xc2, 0x43, 0x44, 0x4a, 0x47, 0x48, 0x3e, 0xbf,
    0x40, 0x41, 0x49, 0x45, 0x46, 0x3d, 0xb9, 0x3a, 0xa8, 0x2c, 0x36, 0x32, 0x3c, 0x37, 0x38, 0x97, 0x16,
    0x3b, 0x15, 0x03, 0x98, 0x03, 0x9a, 0x1c, 0x1b, 0x03, 0x95, 0x1a, 0x19, 0x3b, 0x03, 0x80,
};

/* The root node of each input mode. */
const InputNode g_input_mode_roots[INPUT_MODE_COUNT] = {
    {0x80, 0, 0 },
    {0x80, 0, 1 },
    {0x80, 0, 2 },
    {0x80, 0, 3 },
    {0x81, 0, 18},
    {0x80, 0, 20},
    {0x80, 0, 21},
    {0x80, 0, 22},
    {0x80, 0, 23},
    {0x80, 0, 9 },
};

/* The input zones: layout zone (bit 15: in the inventory panel, bit 14: in the action area), command list,
   key list (-1 none), cursor zone. */
static const InputZone input_zones[INPUT_ZONE_COUNT] = {
    {0x0002, 0,  0,  0 },
    {0x0002, 1,  1,  0 },
    {0x0002, 2,  2,  0 },
    {0x0002, 3,  3,  0 },
    {0x0002, 4,  4,  0 },
    {0x00a1, 5,  5,  3 },
    {0x00a1, 6,  6,  3 },
    {0x009c, 7,  -1, 14},
    {0x00a2, 8,  7,  4 },
    {0x00a2, 9,  8,  4 },
    {0x009d, 10, -1, 15},
    {0x00a3, 11, 9,  5 },
    {0x00a3, 12, 10, 5 },
    {0x009e, 13, -1, 16},
    {0x00a4, 14, 11, 6 },
    {0x00a4, 15, 12, 6 },
    {0x009f, 16, -1, 17},
    {0x0009, 17, 13, 7 },
    {0x0007, 18, -1, 8 },
    {0x0007, 19, -1, 9 },
    {0x0002, 20, -1, 10},
    {0x0007, 21, -1, 8 },
    {0x0002, 22, -1, 10},
    {0x81ee, 23, -1, 11},
    {0x0002, 55, 14, 18},
    {0x002f, 24, -1, 12},
    {0x0031, 25, -1, 0 },
    {0x0032, 26, -1, 0 },
    {0x0033, 27, -1, 0 },
    {0x0034, 28, -1, 0 },
    {0x000b, 29, -1, 13},
    {0x000b, 30, -1, 13},
    {0x000b, 31, -1, 13},
    {0x000b, 32, -1, 13},
    {0x000b, 33, -1, 1 },
    {0x000b, 34, -1, 1 },
    {0x000b, 35, -1, 1 },
    {0x000b, 36, -1, 1 },
    {0x000b, 37, -1, 1 },
    {0x000b, 38, -1, 1 },
    {0x000b, 39, -1, 1 },
    {0x000b, 40, -1, 1 },
    {0x000b, 41, -1, 1 },
    {0x000b, 42, -1, 1 },
    {0x000b, 43, -1, 1 },
    {0x000b, 44, -1, 2 },
    {0x000b, 45, -1, 2 },
    {0x000b, 46, -1, 1 },
    {0x0002, 47, -1, 0 },
    {0x0002, 48, -1, 0 },
    {0x0002, 49, -1, 0 },
    {0x0002, 50, -1, 0 },
    {0x0002, 51, -1, 0 },
    {0x0002, 52, -1, 0 },
    {0x0002, 53, -1, 0 },
    {0x0002, 54, -1, 0 },
    {0x0002, 56, -1, 0 },
    {0x0002, 57, -1, 0 },
    {0x0002, 58, -1, 0 },
    {0x0002, 59, 14, 0 },
    {0x0002, 60, -1, 0 },
    {0x0002, 61, 14, 0 },
};

/* The cursor zones of the input zones (from 1): layout zone (bit 15: in the viewport; input_get_zone_rect adds the
   origin of the panel zone 7, which equals the viewport origin), cursor kind (1: hand or held object, 2: dragged
   champion), priority (a higher priority comes first). */
const CursorZone g_input_cursor_zones[CURSOR_ZONE_COUNT] = {
    {0x000b, 0, 1 },
    {0x005c, 1, 1 },
    {0x00a1, 1, 1 },
    {0x00a2, 1, 1 },
    {0x00a3, 1, 1 },
    {0x00a4, 1, 1 },
    {0x0009, 0, 1 },
    {0x0007, 1, 15},
    {0x0007, 1, 15},
    {0x823b, 0, 15},
    {0x81ee, 0, 15},
    {0x002f, 2, 15},
    {0x000b, 0, 1 },
    {0x009c, 0, 2 },
    {0x009d, 0, 2 },
    {0x009e, 0, 2 },
    {0x009f, 0, 2 },
    {0x8267, 0, 15},
};

/* The command lists: command (bit 15 starts a list), zone, mouse buttons, flags (bit 3: off). */
static const InputCommand input_commands[INPUT_COMMAND_COUNT] = {
    {0x80d7, 0x0197, 0x02, 0x80},
    {0x00d8, 0x0197, 0x10, 0x40},
    {0x00d9, 0x0199, 0x02, 0x00},
    {0x00e0, 0x01b2, 0x02, 0x00},
    {0x00da, 0x019b, 0x02, 0x00},
    {0x80ef, 0x0002, 0x02, 0x80},
    {0x00ef, 0x0002, 0x01, 0x40},
    {0x808f, 0x0007, 0x02, 0x80},
    {0x008f, 0x0002, 0x01, 0x40},
    {0x0090, 0x000d, 0x02, 0x40},
    {0x8090, 0x000d, 0x02, 0x40},
    {0x0052, 0x0002, 0x01, 0x40},
    {0x8091, 0x0002, 0x02, 0x80},
    {0x0091, 0x0002, 0x01, 0x40},
    {0x800b, 0x00a1, 0x02, 0x80},
    {0x8007, 0x00a1, 0x01, 0x40},
    {0x0007, 0x009c, 0x02, 0x80},
    {0x00ea, 0x00b9, 0x02, 0x80},
    {0x0014, 0x00d1, 0x02, 0x80},
    {0x0015, 0x00d2, 0x02, 0x00},
    {0x8007, 0x009c, 0x02, 0x80},
    {0x800b, 0x00a2, 0x02, 0x80},
    {0x8008, 0x00a2, 0x01, 0x40},
    {0x00eb, 0x00ba, 0x02, 0x80},
    {0x0016, 0x00d3, 0x02, 0x80},
    {0x0017, 0x00d4, 0x02, 0x00},
    {0x8008, 0x009d, 0x02, 0x80},
    {0x800b, 0x00a3, 0x02, 0x80},
    {0x8009, 0x00a3, 0x01, 0x40},
    {0x00ec, 0x00bb, 0x02, 0x80},
    {0x0018, 0x00d5, 0x02, 0x80},
    {0x0019, 0x00d6, 0x02, 0x00},
    {0x8009, 0x009e, 0x02, 0x80},
    {0x800b, 0x00a4, 0x02, 0x80},
    {0x800a, 0x00a4, 0x01, 0x40},
    {0x00ed, 0x00bc, 0x02, 0x80},
    {0x001a, 0x00d7, 0x02, 0x80},
    {0x001b, 0x00d8, 0x02, 0x00},
    {0x800a, 0x009f, 0x02, 0x80},
    {0x8001, 0x0028, 0x02, 0x80},
    {0x0003, 0x002a, 0x02, 0x00},
    {0x0002, 0x0029, 0x02, 0x00},
    {0x0006, 0x002d, 0x02, 0x00},
    {0x0005, 0x002c, 0x02, 0x00},
    {0x0004, 0x002b, 0x02, 0x00},
    {0x8050, 0x0007, 0x02, 0x10},
    {0x8050, 0x0007, 0x02, 0x10},
    {0x808c, 0x8232, 0x02, 0x80},
    {0x008e, 0x8234, 0x02, 0x00},
    {0x000b, 0x8236, 0x02, 0x00},
    {0x000b, 0x0002, 0x01, 0x40},
    {0x008d, 0x8238, 0x02, 0x80},
    {0x0048, 0x8267, 0x02, 0x00},
    {0x801c, 0x81fb, 0x02, 0x80},
    {0x001d, 0x81fc, 0x02, 0x00},
    {0x001e, 0x81fd, 0x02, 0x00},
    {0x001f, 0x81fe, 0x02, 0x00},
    {0x0020, 0x81ff, 0x02, 0x00},
    {0x0021, 0x8200, 0x02, 0x00},
    {0x0022, 0x8201, 0x02, 0x00},
    {0x0046, 0x8221, 0x02, 0x00},
    {0x0047, 0x8222, 0x02, 0x00},
    {0x0023, 0x8202, 0x02, 0x00},
    {0x0024, 0x8203, 0x02, 0x00},
    {0x0025, 0x8204, 0x02, 0x00},
    {0x0026, 0x8205, 0x02, 0x00},
    {0x0027, 0x8206, 0x02, 0x00},
    {0x0028, 0x8207, 0x02, 0x00},
    {0x0029, 0x8208, 0x02, 0x00},
    {0x002a, 0x8209, 0x02, 0x00},
    {0x002b, 0x820a, 0x02, 0x00},
    {0x002c, 0x820b, 0x02, 0x00},
    {0x002d, 0x820c, 0x02, 0x00},
    {0x002e, 0x820d, 0x02, 0x00},
    {0x002f, 0x820e, 0x02, 0x00},
    {0x0030, 0x820f, 0x02, 0x00},
    {0x0031, 0x8210, 0x02, 0x00},
    {0x0032, 0x8211, 0x02, 0x00},
    {0x0033, 0x8212, 0x02, 0x00},
    {0x0034, 0x8213, 0x02, 0x00},
    {0x0035, 0x8214, 0x02, 0x00},
    {0x0036, 0x8215, 0x02, 0x00},
    {0x0037, 0x8216, 0x02, 0x00},
    {0x0038, 0x8217, 0x02, 0x00},
    {0x0039, 0x8218, 0x02, 0x00},
    {0x8093, 0x8236, 0x02, 0x80},
    {0x008d, 0x8238, 0x02, 0x80},
    {0x0093, 0x0002, 0x01, 0x40},
    {0x8092, 0x81ee, 0x02, 0x80},
    {0x807d, 0x0031, 0x02, 0x80},
    {0x007e, 0x0032, 0x02, 0x80},
    {0x007f, 0x0033, 0x02, 0x80},
    {0x0080, 0x0034, 0x02, 0x80},
    {0x807d, 0x0031, 0x02, 0x80},
    {0x807e, 0x0032, 0x02, 0x80},
    {0x807f, 0x0033, 0x02, 0x80},
    {0x8080, 0x0034, 0x02, 0x80},
    {0x8075, 0x0046, 0x02, 0x80},
    {0x0074, 0x004a, 0x02, 0x00},
    {0x005f, 0x004f, 0x02, 0x40},
    {0x005f, 0x0075, 0x02, 0x08},
    {0x005f, 0x0076, 0x02, 0x08},
    {0x8077, 0x0047, 0x02, 0x80},
    {0x0076, 0x004b, 0x02, 0x00},
    {0x0060, 0x0050, 0x02, 0x40},
    {0x0060, 0x0077, 0x02, 0x08},
    {0x0060, 0x0078, 0x02, 0x08},
    {0x8079, 0x0048, 0x02, 0x80},
    {0x0078, 0x004c, 0x02, 0x00},
    {0x0061, 0x0051, 0x02, 0x40},
    {0x0061, 0x0079, 0x02, 0x08},
    {0x0061, 0x007a, 0x02, 0x08},
    {0x807b, 0x0049, 0x02, 0x80},
    {0x007a, 0x004d, 0x02, 0x00},
    {0x0062, 0x0052, 0x02, 0x40},
    {0x0062, 0x007b, 0x02, 0x08},
    {0x0062, 0x007c, 0x02, 0x08},
    {0x805d, 0x0060, 0x02, 0x80},
    {0x005e, 0x0061, 0x02, 0x00},
    {0x0070, 0x003b, 0x02, 0x80},
    {0x0071, 0x003f, 0x02, 0x20},
    {0x805d, 0x0060, 0x02, 0x80},
    {0x005e, 0x0061, 0x02, 0x00},
    {0x0070, 0x003b, 0x02, 0x80},
    {0x0071, 0x003f, 0x02, 0x20},
    {0x0072, 0x0040, 0x02, 0x20},
    {0x805d, 0x0060, 0x02, 0x80},
    {0x005e, 0x0061, 0x02, 0x00},
    {0x0070, 0x003b, 0x02, 0x80},
    {0x0071, 0x003f, 0x02, 0x20},
    {0x0072, 0x0040, 0x02, 0x20},
    {0x0073, 0x0041, 0x02, 0x20},
    {0x8070, 0x003b, 0x02, 0x80},
    {0x0056, 0x0065, 0x02, 0x00},
    {0x0057, 0x0066, 0x02, 0x00},
    {0x0058, 0x0067, 0x02, 0x00},
    {0x0059, 0x0068, 0x02, 0x00},
    {0x0055, 0x0063, 0x02, 0x10},
    {0x8070, 0x003b, 0x02, 0x80},
    {0x0056, 0x0065, 0x02, 0x00},
    {0x0057, 0x0066, 0x02, 0x00},
    {0x0058, 0x0067, 0x02, 0x00},
    {0x0059, 0x0068, 0x02, 0x00},
    {0x0071, 0x006e, 0x02, 0x80},
    {0x0055, 0x0063, 0x02, 0x10},
    {0x8070, 0x003b, 0x02, 0x80},
    {0x0056, 0x0065, 0x02, 0x00},
    {0x0057, 0x0066, 0x02, 0x00},
    {0x0058, 0x0067, 0x02, 0x00},
    {0x0059, 0x0068, 0x02, 0x00},
    {0x0071, 0x006e, 0x02, 0x80},
    {0x0072, 0x006f, 0x02, 0x80},
    {0x0055, 0x0063, 0x02, 0x10},
    {0x8070, 0x003b, 0x02, 0x80},
    {0x0056, 0x0065, 0x02, 0x00},
    {0x0057, 0x0066, 0x02, 0x00},
    {0x0058, 0x0067, 0x02, 0x00},
    {0x0059, 0x0068, 0x02, 0x00},
    {0x0071, 0x006e, 0x02, 0x80},
    {0x0072, 0x006f, 0x02, 0x80},
    {0x0073, 0x0070, 0x02, 0x80},
    {0x0055, 0x0063, 0x02, 0x10},
    {0x8070, 0x003b, 0x02, 0x80},
    {0x8070, 0x003b, 0x02, 0x80},
    {0x005d, 0x0060, 0x02, 0x00},
    {0x005e, 0x0061, 0x02, 0x00},
    {0x8065, 0x00f5, 0x02, 0x80},
    {0x0066, 0x00f6, 0x02, 0x00},
    {0x0067, 0x00f7, 0x02, 0x00},
    {0x0068, 0x00f8, 0x02, 0x00},
    {0x0069, 0x00f9, 0x02, 0x00},
    {0x006a, 0x00fa, 0x02, 0x00},
    {0x806c, 0x00fc, 0x02, 0x80},
    {0x006b, 0x00fe, 0x02, 0x00},
    {0x8070, 0x003b, 0x02, 0x80},
    {0x003a, 0x00e5, 0x02, 0x00},
    {0x003b, 0x00e6, 0x02, 0x00},
    {0x003c, 0x00e7, 0x02, 0x00},
    {0x003d, 0x00e8, 0x02, 0x00},
    {0x003e, 0x00e9, 0x02, 0x00},
    {0x003f, 0x00ea, 0x02, 0x00},
    {0x0040, 0x00eb, 0x02, 0x00},
    {0x0041, 0x00ec, 0x02, 0x00},
    {0x8070, 0x003b, 0x02, 0x80},
    {0x00e4, 0x00dd, 0x02, 0x00},
    {0x00e5, 0x00de, 0x02, 0x00},
    {0x00e6, 0x00df, 0x02, 0x00},
    {0x00e7, 0x00e0, 0x02, 0x00},
    {0x00e8, 0x00e1, 0x02, 0x00},
    {0x00e9, 0x00e2, 0x02, 0x00},
    {0x80ee, 0x000b, 0x02, 0x10},
    {0x80db, 0x81c8, 0x02, 0xa0},
    {0x0095, 0x8160, 0x02, 0x00},
    {0x0096, 0x8161, 0x02, 0x00},
    {0x0097, 0x8162, 0x02, 0x00},
    {0x0098, 0x8163, 0x02, 0x00},
    {0x0099, 0x8164, 0x02, 0x00},
    {0x009a, 0x8165, 0x02, 0x00},
    {0x009b, 0x8166, 0x02, 0x00},
    {0x009c, 0x8167, 0x02, 0x00},
    {0x009d, 0x8171, 0x02, 0x00},
    {0x009e, 0x8172, 0x02, 0x00},
    {0x009f, 0x8173, 0x02, 0x00},
    {0x00a0, 0x8174, 0x02, 0x00},
    {0x00a1, 0x8175, 0x02, 0x00},
    {0x00a2, 0x8176, 0x02, 0x00},
    {0x00a3, 0x8177, 0x02, 0x00},
    {0x00a4, 0x8178, 0x02, 0x00},
    {0x80db, 0x41c8, 0x02, 0xa0},
    {0x0095, 0x4160, 0x02, 0x00},
    {0x0096, 0x4161, 0x02, 0x00},
    {0x0097, 0x4162, 0x02, 0x00},
    {0x0098, 0x4163, 0x02, 0x00},
    {0x0099, 0x4164, 0x02, 0x00},
    {0x009a, 0x4165, 0x02, 0x00},
    {0x009b, 0x4166, 0x02, 0x00},
    {0x009c, 0x4167, 0x02, 0x00},
    {0x009d, 0x4171, 0x02, 0x00},
    {0x009e, 0x4172, 0x02, 0x00},
    {0x009f, 0x4173, 0x02, 0x00},
    {0x00a0, 0x4174, 0x02, 0x00},
    {0x00a1, 0x4175, 0x02, 0x00},
    {0x00a2, 0x4176, 0x02, 0x00},
    {0x00a3, 0x4177, 0x02, 0x00},
    {0x00a4, 0x4178, 0x02, 0x00},
    {0x80db, 0x81c8, 0x02, 0xa0},
    {0x80db, 0x81c9, 0x02, 0xa0},
    {0x00dc, 0x81c8, 0x02, 0x20},
    {0x80db, 0x81c9, 0x02, 0xa0},
    {0x00dc, 0x81cc, 0x02, 0x20},
    {0x00dd, 0x81cd, 0x02, 0x20},
    {0x80db, 0x41c8, 0x02, 0xa0},
    {0x80db, 0x41c9, 0x02, 0xa0},
    {0x00dc, 0x41c8, 0x02, 0x20},
    {0x80db, 0x41c9, 0x02, 0xa0},
    {0x00dc, 0x41cc, 0x02, 0x20},
    {0x00dd, 0x41cd, 0x02, 0x20},
    {0x8049, 0x8275, 0x02, 0x80},
    {0x004a, 0x8224, 0x02, 0x00},
    {0x004b, 0x0002, 0x02, 0x00},
    {0x004b, 0x0002, 0x01, 0x00},
    {0x80db, 0x81ca, 0x02, 0xa0},
    {0x00dc, 0x81cb, 0x02, 0x20},
    {0x00dd, 0x81cc, 0x02, 0x20},
    {0x00de, 0x81cd, 0x02, 0x20},
    {0x80db, 0x41ca, 0x02, 0xa0},
    {0x00dc, 0x41cb, 0x02, 0x20},
    {0x00dd, 0x41cc, 0x02, 0x20},
    {0x00de, 0x41cd, 0x02, 0x20},
    {0x80db, 0x81cd, 0x02, 0xa0},
    {0x00dc, 0x81cc, 0x02, 0x20},
    {0x00dd, 0x81c3, 0x02, 0x10},
    {0x80db, 0x81cd, 0x02, 0xa0},
    {0x00dc, 0x81cc, 0x02, 0x20},
    {0x00de, 0x81c5, 0x02, 0x20},
    {0x00dd, 0x81c3, 0x02, 0x10},
    {0x80db, 0x41cd, 0x02, 0xa0},
    {0x00dc, 0x41cc, 0x02, 0x20},
    {0x00dd, 0x41c3, 0x02, 0x10},
    {0x80db, 0x41cd, 0x02, 0xa0},
    {0x00dc, 0x41cc, 0x02, 0x20},
    {0x00de, 0x41c5, 0x02, 0x20},
    {0x00dd, 0x41c3, 0x02, 0x10},
    {0x8000, 0x0000, 0x00, 0x00},
};
/* The key lists: command (bit 15 starts a list), key code (scan code, 0x200 shift, 0x400 control, 0x800 alt). */
static const InputKey input_keys[INPUT_KEY_COUNT] = {
    {0x80d7, 0x001c},
    {0x00e0, 0x0410},
    {0x80ef, 0x001c},
    {0x808f, 0x001c},
    {0x0090, 0x0001},
    {0x808c, 0x081f},
    {0x0090, 0x0001},
    {0x0052, 0x0039},
    {0x8091, 0x0001},
    {0x800b, 0x0002},
    {0x8007, 0x0002},
    {0x008c, 0x081f},
    {0x800b, 0x0003},
    {0x8008, 0x0003},
    {0x008c, 0x081f},
    {0x800b, 0x0004},
    {0x8009, 0x0004},
    {0x008c, 0x081f},
    {0x800b, 0x0005},
    {0x800a, 0x0005},
    {0x008c, 0x081f},
    {0x8001, 0x004b},
    {0x0003, 0x004c},
    {0x0002, 0x004d},
    {0x0006, 0x004f},
    {0x0005, 0x0050},
    {0x0004, 0x0051},
    {0x00f0, 0x0047},
    {0x00f1, 0x0048},
    {0x00f2, 0x0049},
    {0x80a5, 0x000e},
    {0x00a7, 0x001c},
    {0x00a8, 0x021e},
    {0x00a8, 0x001e},
    {0x00a9, 0x0230},
    {0x00a9, 0x0030},
    {0x00aa, 0x022e},
    {0x00aa, 0x002e},
    {0x00ab, 0x0220},
    {0x00ab, 0x0020},
    {0x00ac, 0x0212},
    {0x00ac, 0x0012},
    {0x00ad, 0x0221},
    {0x00ad, 0x0021},
    {0x00ae, 0x0222},
    {0x00ae, 0x0022},
    {0x00af, 0x0223},
    {0x00af, 0x0023},
    {0x00b0, 0x0217},
    {0x00b0, 0x0017},
    {0x00b1, 0x0224},
    {0x00b1, 0x0024},
    {0x00b2, 0x0225},
    {0x00b2, 0x0025},
    {0x00b3, 0x0226},
    {0x00b3, 0x0026},
    {0x00b4, 0x0232},
    {0x00b4, 0x0032},
    {0x00b5, 0x0231},
    {0x00b5, 0x0031},
    {0x00b6, 0x0218},
    {0x00b6, 0x0018},
    {0x00b7, 0x0219},
    {0x00b7, 0x0019},
    {0x00b8, 0x0210},
    {0x00b8, 0x0010},
    {0x00b9, 0x0213},
    {0x00b9, 0x0013},
    {0x00ba, 0x021f},
    {0x00ba, 0x001f},
    {0x00bb, 0x0214},
    {0x00bb, 0x0014},
    {0x00bc, 0x0216},
    {0x00bc, 0x0016},
    {0x00bd, 0x022f},
    {0x00bd, 0x002f},
    {0x00be, 0x0211},
    {0x00be, 0x0011},
    {0x00bf, 0x022d},
    {0x00bf, 0x002d},
    {0x00c0, 0x0215},
    {0x00c0, 0x0015},
    {0x00c1, 0x022c},
    {0x00c1, 0x002c},
    {0x00c2, 0x000b},
    {0x00c3, 0x0002},
    {0x00c4, 0x0003},
    {0x00c5, 0x0004},
    {0x00c6, 0x0005},
    {0x00c7, 0x0006},
    {0x00c8, 0x0007},
    {0x00c9, 0x0008},
    {0x00ca, 0x0009},
    {0x00cb, 0x000a},
    {0x00c2, 0x0052},
    {0x00c3, 0x004f},
    {0x00c4, 0x0050},
    {0x00c5, 0x0051},
    {0x00c6, 0x004b},
    {0x00c7, 0x004c},
    {0x00c8, 0x004d},
    {0x00c9, 0x0047},
    {0x00ca, 0x0048},
    {0x00cb, 0x0049},
    {0x00d3, 0x0053},
    {0x00cc, 0x000c},
    {0x00cd, 0x020a},
    {0x00ce, 0x020b},
    {0x00cf, 0x0028},
    {0x00d0, 0x0235},
    {0x00d1, 0x0202},
    {0x00d2, 0x0033},
    {0x00d3, 0x0034},
    {0x00d4, 0x0027},
    {0x00d5, 0x0227},
    {0x00d6, 0x0039},
    {0x00df, 0x000f},
    {0x004b, 0x0001},
    {0x800b, 0x0039},
    {0x8093, 0x0039},
    {0x8000, 0x0000},
};

/* ---- Setup ---- */

void input_init(void)
{
    memset(&g_input, 0, sizeof g_input);
    g_input.event_tail = EVENT_RING - 1;
    int n = 0;
    for (int i = 0; i < INPUT_COMMAND_COUNT; i++)
        if (input_commands[i].command & LIST_START)
            g_input.command_list_start[n++] = (int16_t)i;
    n = 0;
    for (int i = 0; i < INPUT_KEY_COUNT; i++)
        if (input_keys[i].command & LIST_START)
            g_input.key_list_start[n++] = (int16_t)i;
    n = 0;
    for (int i = 0; i < INPUT_CHILD_COUNT; i++)
        if (input_children[i] & NODE_HAS_CHILDREN)
            g_input.child_list_start[n++] = (uint8_t)i;
}

bool input_is_waiting(void)
{
    return g_input.wait != 0;
}

bool input_is_idle(void)
{
    return !g_input.key_count && !g_input.event_count && !g_input.deferred &&
           g_input.queue_head == g_input.queue_tail && !g_input.script && !g_input.feedback_waiting &&
           !g_input_feedback_busy;
}

/* ---- The input tree ---- */

int input_party_cell_champion(int16_t cell)
{
    return champ_get_index_in_cell(dir_turn(cell, g_game.party_dir));
}

/* The condition of a node with children, tested with the argument of each child. */
static bool condition_holds(int cond, int arg)
{
    switch (cond) {
    case NODE_IF_ALWAYS:
        return true;
    case NODE_IF_SCREEN_MODE:
        return arg == g_dialog.screen_mode;
    case NODE_IF_BUTTON_COUNT:
        return arg == g_dialog.button_count;
    case NODE_IF_INVENTORY:
        if (arg == g_ui.inventory)
            return true;
        if (arg <= CHAMPION_MAX)
            return false;
        return !g_ui.inventory || arg - CHAMPION_MAX != g_ui.inventory;
    case NODE_IF_CHAMPION_ALIVE:
        return champion(arg)->health != 0;
    case NODE_IF_CHAMPION_IN_CELL:
        return input_party_cell_champion(arg) >= 0;
    case NODE_IF_DRAGGING:
        return arg ? g_input_drag_state != 0 : g_input_drag_state == 0;
    case NODE_IF_ICON_IN_CELL:
        if (g_action.champion)
            return false;
        return arg >= CELL_COUNT || input_party_cell_champion(arg) >= 0;
    case NODE_IF_ACTION_COUNT:
        return g_action.champion && arg == g_champ_action_count;
    case NODE_IF_RUNE_SET:
        return g_action.champion && arg & 1 << champion(g_action.champion - 1)->rune_count;
    case NODE_IF_MINION_MAP_LAYOUT:
        if (!g_action.champion)
            return false;
        return arg == (g_action.flags & MAP_HAS_OPTIONS ? g_champ_action_count : MAP_WITHOUT_OPTIONS);
    case NODE_IF_ACTION_MODE:
        return arg == g_action.mode;
    }
    return false;
}

static const InputNode *node_child(const InputNode *node, int i)
{
    return &input_nodes[input_children[g_input.child_list_start[node->link] + i] & NODE_INDEX_MASK];
}

static bool node_child_end(const InputNode *node, int i)
{
    return i > 0 && input_children[g_input.child_list_start[node->link] + i] & NODE_HAS_CHILDREN;
}

bool input_get_zone_rect(uint16_t zone, int16_t *rect)
{
    if (!zone_get_rect(zone & ZONE_ID_MASK, rect))
        return false;
    if (zone & (ZONE_IN_PANEL | ZONE_IN_ACTION_AREA)) {
        ScreenPoint origin = zone_get_position(zone & ZONE_IN_PANEL ? ZONE_VIEWPORT : ZONE_ACTION_ORIGIN);
        rect[RECT_X] = (int16_t)(rect[RECT_X] + origin.x);
        rect[RECT_Y] = (int16_t)(rect[RECT_Y] + origin.y);
    }
    return true;
}

bool rect_contains_point(const int16_t *r, int16_t x, int16_t y)
{
    return x >= r[RECT_X] && x <= r[RECT_X] + r[RECT_W] - 1 && y >= r[RECT_Y] && y <= r[RECT_Y] + r[RECT_H] - 1;
}

static const InputCommand *node_commands(const InputNode *node)
{
    int list = input_zones[node->link].commands;
    return list == -1 ? NULL : &input_commands[g_input.command_list_start[list]];
}

/* The zone that the position of a command zone is relative to: the inventory panel, the action area, or none. */
static int16_t origin_of_zone(uint16_t zone)
{
    if (zone & ZONE_IN_PANEL)
        return ZONE_VIEWPORT;
    if (zone & ZONE_IN_ACTION_AREA)
        return ZONE_ACTION_ORIGIN;
    return -1;
}

/* Remembers the matched command entry: its zone, the click position, its place in its list, and its rect. */
static void select_entry(const InputCommand *list, const InputCommand *e, int16_t x, int16_t y, const int16_t *rect)
{
    uint16_t command = e->command & COMMAND_MASK;
    g_input_match.zone = e->zone & ZONE_ID_MASK;
    g_input_match.origin = origin_of_zone(e->zone);
    g_input_match.value = command;
    g_input_match.choice = (int16_t)(command - ((list->command & COMMAND_MASK) - 1));
    g_input_match.x = x;
    g_input_match.y = y;
    memcpy(g_input_match.rect, rect, sizeof g_input_match.rect);
}

/* The command of the first entry of a command list whose buttons and zone take the click, 0 for none; rect becomes
   the rect of its zone. */
static uint16_t match_click_in_list(const InputCommand *list, int16_t x, int16_t y, int buttons, int16_t *rect)
{
    if (!list)
        return 0;
    const InputCommand *e = list;
    do {
        if (!(e->flags & COMMAND_OFF) && buttons & e->buttons && input_get_zone_rect(e->zone, rect) &&
            rect_contains_point(rect, x, y)) {
            select_entry(list, e, x, y, rect);
            g_input_match.key = 0;
            return e->command & COMMAND_MASK;
        }
        e++;
    } while (!(e->command & LIST_START));
    rect[RECT_W] = 0;
    rect[RECT_H] = 0;
    return 0;
}

/* Remembers the entry of command in a command list as the matched entry, or only the command without an entry; rect
   becomes the rect of the zone of the entry. As in the original, the walk ends at the first entry without a command;
   a list needs that end entry. */
static void select_command_in_list(uint16_t command, const InputCommand *list, int16_t *rect)
{
    g_input_match.origin = -1;
    if (list)
        for (const InputCommand *e = list; e->command & COMMAND_MASK; e++)
            if (!(e->flags & COMMAND_OFF) && (e->command & COMMAND_MASK) == command) {
                input_get_zone_rect(e->zone, rect);
                select_entry(list, e, rect[RECT_X], rect[RECT_Y], rect);
                return;
            }
    if (list) {
        rect[RECT_W] = 0;
        rect[RECT_H] = 0;
    }
    g_input_match.zone = ZONE_NONE;
    g_input_match.x = 0;
    g_input_match.y = 0;
    g_input_match.value = command;
    g_input_match.choice = 0;
}

/* The command of a click in the input tree below node, 0 for none; rect becomes the rect of its zone. */
static uint16_t find_click_command(const InputNode *node, int16_t x, int16_t y, int buttons, int16_t *rect)
{
    uint16_t command = 0;
    for (int i = 0; !node_child_end(node, i); i++) {
        const InputNode *child = node_child(node, i);
        if (!condition_holds(node->cond & ~NODE_HAS_CHILDREN, child->arg))
            continue;
        if (child->cond & NODE_HAS_CHILDREN) {
            command = find_click_command(child, x, y, buttons, rect);
        } else {
            int16_t r[RECT_SIZE];
            if (!input_get_zone_rect(input_zones[child->link].zone, r) || !rect_contains_point(r, x, y))
                continue;
            command = match_click_in_list(node_commands(child), x, y, buttons, rect);
        }
        if (command)
            return command;
    }
    return command;
}

/* The command of key in the input tree below node, 0 for none; rect becomes the rect of the zone of its command. */
static uint16_t find_key_command(const InputNode *node, uint16_t key, int16_t *rect)
{
    uint16_t command = 0;
    for (int i = 0; !node_child_end(node, i); i++) {
        const InputNode *child = node_child(node, i);
        if (!condition_holds(node->cond & ~NODE_HAS_CHILDREN, child->arg))
            continue;
        if (child->cond & NODE_HAS_CHILDREN) {
            command = find_key_command(child, key, rect);
            if (command)
                return command;
            continue;
        }
        int list = input_zones[child->link].keys;
        if (list == -1)
            continue;
        const InputKey *k = &input_keys[g_input.key_list_start[list]];
        do {
            if (k->key == key) {
                command = k->command & COMMAND_MASK;
                select_command_in_list(command, node_commands(child), rect);
                g_input_match.key = key;
                break;
            }
            k++;
        } while (!(k->command & LIST_START));
        if (command)
            return command;
    }
    return command;
}

const InputCommand *input_find_command_entry(const InputNode *node, uint16_t command)
{
    for (int i = 0; !node_child_end(node, i); i++) {
        const InputNode *child = node_child(node, i);
        if (!condition_holds(node->cond & ~NODE_HAS_CHILDREN, child->arg))
            continue;
        if (child->cond & NODE_HAS_CHILDREN) {
            const InputCommand *e = input_find_command_entry(child, command);
            if (e)
                return e;
            continue;
        }
        const InputCommand *e = node_commands(child);
        if (e)
            for (; e->command & COMMAND_MASK; e++)
                if ((e->command & COMMAND_MASK) == command)
                    return e;
    }
    return NULL;
}

bool input_right_click_cancels_dialog(int type)
{
    if (type != INPUT_BUTTON_RIGHT || g_game_mode != GAME_MODE_ADVANCED || g_input.mode != INPUT_MODE_DIALOG ||
        !g_dialog.right_choice)
        return false;
    g_input_match.choice = (int16_t)g_dialog.right_choice;
    return true;
}

uint16_t input_click_command(enum InputMode mode, int16_t x, int16_t y, int buttons, int16_t *rect)
{
    uint16_t command = find_click_command(&g_input_mode_roots[mode], x, y, buttons, rect);
    if (g_game_mode != GAME_MODE_ADVANCED || buttons != INPUT_BUTTON_LEFT)
        return command;
    for (int16_t champion_index = 0; champion_index < CHAMPION_MAX; champion_index++) {
        int16_t box[RECT_SIZE], target[RECT_SIZE];
        uint16_t open = (uint16_t)(CMD_INVENTORY_FIRST + champion_index);
        uint16_t store = (uint16_t)(CMD_STORE_FIRST + champion_index);
        if (!zone_get_rect(ZONE_STATUS_BOX + champion_index, box) || !rect_contains_point(box, x, y))
            continue;
        if (command && command != open && command != store)
            return command;
        const InputCommand *e =
            input_find_command_entry(&g_input_mode_roots[mode], g_game.leader_hand != THING_NONE ? store : open);
        if (e && input_get_zone_rect(e->zone, target))
            return find_click_command(&g_input_mode_roots[mode], target[RECT_X], target[RECT_Y], buttons, rect);
        return command;
    }
    return command;
}

uint16_t input_key_command(enum InputMode mode, uint16_t key, int16_t *rect)
{
    if (g_game_mode == GAME_MODE_ADVANCED && mode == INPUT_MODE_MOVE && key == KEY_OPTIONS)
        return CMD_OPTIONS;
    uint16_t command = find_key_command(&g_input_mode_roots[mode], key, rect);
    if (command || g_game_mode != GAME_MODE_ADVANCED)
        return command;
    if (mode == INPUT_MODE_MOVE && (key == KEY_QUICK_SAVE || key == KEY_QUICK_LOAD))
        return key == KEY_QUICK_SAVE ? CMD_QUICK_SAVE : CMD_QUICK_LOAD;
    if (mode == INPUT_MODE_MOVE && (key == KEY_AUTOMAP || key == KEY_AUTOMAP_M))
        return CMD_AUTOMAP;
    if (mode == INPUT_MODE_MOVE && key == KEY_CHEATS)
        return CMD_CHEATS;
    if (mode == INPUT_MODE_INVENTORY && key == KEY_AUTOMAP && !g_ui.candidate)
        return CMD_INVENTORY_LEADER;
    uint16_t keypad = advanced_movement_key(key);
    return keypad ? find_key_command(&g_input_mode_roots[mode], keypad, rect) : 0;
}

/* Marks the zones of the input tree below node whose conditions hold. */
static void mark_active_zones(const InputNode *node, bool *active)
{
    for (int i = 0; !node_child_end(node, i); i++) {
        const InputNode *child = node_child(node, i);
        if (!condition_holds(node->cond & ~NODE_HAS_CHILDREN, child->arg))
            continue;
        if (child->cond & NODE_HAS_CHILDREN)
            mark_active_zones(child, active);
        else
            active[child->link] = true;
    }
}

/* Shows the cursor zone c in front of the zones of the same or a lower priority. */
static void add_cursor_zone(int c)
{
    if (memchr(g_input.cursor_list, c, (size_t)g_input.cursor_count))
        return;
    int p = 0;
    while (p < g_input.cursor_count &&
           g_input_cursor_zones[g_input.cursor_list[p] - 1].priority > g_input_cursor_zones[c - 1].priority)
        p++;
    memmove(&g_input.cursor_list[p + 1], &g_input.cursor_list[p], (size_t)(g_input.cursor_count - p));
    g_input.cursor_list[p] = (uint8_t)c;
    g_input.cursor_count++;
}

/* Hides the cursor zone c. */
static void remove_cursor_zone(int c)
{
    uint8_t *e = memchr(g_input.cursor_list, c, (size_t)g_input.cursor_count);
    if (!e)
        return;
    memmove(e, e + 1, (size_t)(&g_input.cursor_list[--g_input.cursor_count] - e));
}

/* Marks the active input zones of the mode. A cursor zone of a zone that becomes active shows, of a zone that
   becomes inactive hides; with both changes it stays as it is, as in the original. */
static void update_active_zones(void)
{
    bool active[INPUT_ZONE_COUNT] = {false};
    uint8_t change[CURSOR_ZONE_COUNT + 1] = {0};
    mark_active_zones(&g_input_mode_roots[g_input.mode], active);
    for (int i = 0; i < INPUT_ZONE_COUNT; i++)
        if (active[i] != g_input.zone_active[i]) {
            change[input_zones[i].cursor] |= active[i] ? CURSOR_ADD : CURSOR_REMOVE;
            g_input.zone_active[i] = active[i];
        }
    for (int c = 1; c <= CURSOR_ZONE_COUNT; c++) {
        if (change[c] == CURSOR_ADD)
            add_cursor_zone(c);
        if (change[c] == CURSOR_REMOVE)
            remove_cursor_zone(c);
    }
}

void input_set_mode(enum InputMode mode)
{
    if (mode != g_input.mode)
        input_flush();
    g_input.mode = mode;
    update_active_zones();
}

enum InputMode input_get_mode(void)
{
    return g_input.mode;
}

void input_refresh_mode(void)
{
    update_active_zones();
}

void input_push_mode(enum InputMode mode)
{
    g_input.saved_mode = g_input.mode;
    input_reset_states();
    input_set_mode(mode);
}

void input_pop_mode(void)
{
    input_set_mode(g_input.saved_mode);
}
