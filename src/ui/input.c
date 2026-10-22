/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Mouse and keyboard input: the input tree of SKULL.EXE maps clicks and keys to commands; the command queue. */
#include "input.h"
#include "gdat.h"
#include "ids.h"
#include "le.h"
#include "action.h"
#include "champ_action.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "game.h"
#include "inventory.h"
#include "item.h"
#include "magic.h"
#include "minion.h"
#include "money.h"
#include "move.h"
#include "recruit.h"
#include "save.h"
#include "sensor.h"
#include "sound_request.h"
#include "action_area.h"
#include "advanced.h"
#include "dialog.h"
#include "draw.h"
#include "minion_map.h"
#include "music.h"
#include "play.h"
#include "session.h"
#include "text.h"
#include "ui.h"
#include "video.h"
#include "view.h"
#include "zone.h"
#include <stdlib.h>
#include <string.h>

#define INPUT_NODE_COUNT    76
#define INPUT_CHILD_COUNT   83
#define INPUT_MODE_COUNT    10
#define INPUT_ZONE_COUNT    62
#define INPUT_COMMAND_COUNT 264
#define INPUT_KEY_COUNT     121
#define CURSOR_ZONE_COUNT   18
#define CLICK_FEEDBACK_SIZE 236
#define LIST_START          0x8000
#define NODE_HAS_CHILDREN   0x80
#define NODE_INDEX_MASK     0x7f /* a child list entry: the node index */
#define COMMAND_MASK        0x7ff
#define COMMAND_OFF         0x08
#define ZONE_IN_PANEL       0x8000
#define ZONE_IN_ACTION_AREA 0x4000
#define ZONE_ID_MASK        0x3fff
#define EVENT_RING          11
#define EVENT_LIMIT         7
#define EVENT_LIMIT_SPECIAL 9
#define COMMAND_QUEUE       3
#define KEY_BUFFER          16
#define EVENT_ENTER_AREA    0x40
#define EVENT_LEAVE_AREA    0x60
#define EVENT_CLICK_OTHER   0x10 /* a click type of the original that the port never queues */
/* the mouse event types that find a click command: bits 0, 1, and 4 */
#define EVENT_CLICK_TYPES   (INPUT_BUTTON_RIGHT | INPUT_BUTTON_LEFT | EVENT_CLICK_OTHER)
#define ARROW_W             12
#define HAND_W              16
#define CURSOR_H            16
#define FACE_CURSOR_SKIP    4 /* color 4 of the face keeps the fill color of the dragged champion cursor */
#define FACE_HOT_X          8
#define FACE_HOT_Y          6
#define OBJECT_SHADOW       2 /* the offset of the black shadow of the held object cursor */
#define OBJECT_HOT          8 /* the hot spot of the held object cursor: the center of the icon */
#define OBJECT_CURSOR_SIZE  (HAND_ICON_SIZE + OBJECT_SHADOW)

typedef struct {
    uint8_t cond, arg;
    uint16_t link;
} InputNode;
typedef struct {
    uint16_t zone;
    int16_t commands, keys;
    uint8_t cursor;
} InputZone;
typedef struct {
    uint16_t zone;
    uint8_t kind, priority;
} CursorZone;
enum { CURSOR_KIND_HAND = 1, CURSOR_KIND_CHAMPION = 2 };
typedef struct {
    uint16_t command, zone;
    uint8_t buttons, flags;
} InputCommand;
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
static const InputNode input_mode_roots[INPUT_MODE_COUNT] = {
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
static const CursorZone cursor_zones[CURSOR_ZONE_COUNT] = {
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
/* The places of the click feedback scripts; a script of a command group is SIZE long per command. */
enum {
    SCRIPT_QUEUE = 0,
    SCRIPT_EXECUTE = 1,
    SCRIPT_MOVE_FIRST = 2,
    SCRIPT_MOVE_SIZE = 15,
    SCRIPT_HAND_FIRST = 0x5c,
    SCRIPT_HAND_SIZE = 5,
    SCRIPT_PARTY_ICON_FIRST = 0x66,
    SCRIPT_PARTY_ICON_SIZE = 5,
    SCRIPT_ACTION_CANCEL = 0x7a,
    SCRIPT_ROTATE_FIRST = 0x82,
    SCRIPT_ROTATE_SIZE = 8,
    SCRIPT_RUNE = 0x92,
    SCRIPT_REMOVE_RUNE = 0x96,
    SCRIPT_CAST_SPELL = 0x9e,
    SCRIPT_ACTION_FIRST = 0xa2,
    SCRIPT_ACTION_SIZE = 5,
    SCRIPT_INVENTORY_CLOSE = 0xb1,
    SCRIPT_SLEEP = 0xb6,
    SCRIPT_SAVE = 0xbb,
    SCRIPT_MAP_OPTION_FIRST = 0xc0,
    SCRIPT_MAP_OPTION_SIZE = 7,
    SCRIPT_VIEWPORT = 0xea
};
/* The click feedback scripts (see run_click_feedback). */
static const uint8_t click_feedback_scripts[CLICK_FEEDBACK_SIZE] = {
    0x86, 0x85, 0x47, 0x01, 0x03, 0x03, 0xff, 0x46, 0x00, 0x04, 0x03, 0x07, 0x01, 0x03, 0x02, 0xff, 0x82, 0x47, 0x01,
    0x03, 0x05, 0xff, 0x46, 0x00, 0x04, 0x03, 0x07, 0x01, 0x03, 0x04, 0xff, 0x82, 0x47, 0x01, 0x03, 0x07, 0xff, 0x46,
    0x00, 0x04, 0x03, 0x07, 0x01, 0x03, 0x06, 0xff, 0x82, 0x47, 0x01, 0x03, 0x09, 0xff, 0x46, 0x00, 0x04, 0x03, 0x07,
    0x01, 0x03, 0x08, 0xff, 0x82, 0x47, 0x01, 0x03, 0x0b, 0xff, 0x46, 0x00, 0x04, 0x03, 0x07, 0x01, 0x03, 0x0a, 0xff,
    0x82, 0x47, 0x01, 0x03, 0x0d, 0xff, 0x46, 0x00, 0x04, 0x03, 0x07, 0x01, 0x03, 0x0c, 0xff, 0x82, 0x49, 0x00, 0x00,
    0x06, 0x85, 0x49, 0x01, 0x00, 0x06, 0x85, 0x48, 0x00, 0x00, 0x06, 0x85, 0x48, 0x01, 0x00, 0x06, 0x85, 0x48, 0x02,
    0x00, 0x06, 0x85, 0x48, 0x03, 0x00, 0x06, 0x85, 0x47, 0x01, 0x04, 0x0f, 0xff, 0x00, 0x06, 0x85, 0x47, 0x01, 0x04,
    0x11, 0xff, 0x00, 0x04, 0x85, 0x47, 0x01, 0x04, 0x13, 0xff, 0x00, 0x04, 0x85, 0x4a, 0x00, 0x06, 0x85, 0x47, 0x01,
    0x05, 0x0a, 0xff, 0x00, 0x06, 0x85, 0x4b, 0x00, 0x06, 0x86, 0x4c, 0x00, 0x00, 0x06, 0x86, 0x4c, 0x01, 0x00, 0x06,
    0x86, 0x4c, 0x02, 0x00, 0x06, 0x86, 0x4d, 0x02, 0x00, 0x06, 0x86, 0x4d, 0x04, 0x00, 0x06, 0x85, 0x4d, 0x08, 0x00,
    0x06, 0x86, 0x4e, 0x01, 0x05, 0x00, 0x04, 0x8e, 0x00, 0x4e, 0x02, 0x05, 0x00, 0x04, 0x8e, 0x00, 0x4e, 0x04, 0x05,
    0x00, 0x04, 0x8e, 0x00, 0x4e, 0x08, 0x05, 0x00, 0x04, 0x8e, 0x00, 0x4f, 0x1a, 0xfe, 0x00, 0x00, 0x04, 0x85, 0x50,
    0x1a, 0xfe, 0x01, 0x00, 0x04, 0x85, 0x06, 0x83,
};

static uint8_t child_list_start[INPUT_CHILD_COUNT]; /* position of each child list */
static int16_t command_list_start[INPUT_COMMAND_COUNT];
static int16_t key_list_start[INPUT_KEY_COUNT];

static struct {
    int mode, saved_mode;
    struct {
        int16_t type, x, y;
    } events[EVENT_RING];
    int event_head, event_tail, event_count;
    int events_locked;
    int deferred, deferred_x, deferred_y, deferred_type;
    int dropped_left; /* a left click did not fit: a right release still may */
    uint16_t keys[KEY_BUFFER];
    int key_count;
    Command current; /* the command of the last event */
    Command queue[COMMAND_QUEUE];
    int queue_head, queue_tail;
    int disabled;            /* commands queued by feedback scripts that wait */
    int wait;                /* ticks without new commands */
    int16_t rect[RECT_SIZE]; /* the rectangle of the matched command */
    const uint8_t *script;   /* the running click feedback script */
    int script_delay;
    uint32_t last_ticks;
    int feedback_waiting; /* the original 0x7f474: the script waits for the next queue run */
    int arrow_pressed, arrow_zone, arrow_entry;
    uint8_t zone_active[INPUT_ZONE_COUNT];  /* the input zones of the input tree of the mode */
    uint8_t cursor_list[CURSOR_ZONE_COUNT]; /* the shown cursor zones (from 1), by priority */
    int cursor_count;
} in;

int input_feedback_busy;

bool input_is_waiting(void)
{
    return in.wait != 0;
}

bool input_is_idle(void)
{
    return !in.key_count && !in.event_count && !in.deferred && in.queue_head == in.queue_tail && !in.script &&
           !in.feedback_waiting && !input_feedback_busy;
}

void input_init(void)
{
    int n = 0;
    for (int i = 0; i < INPUT_COMMAND_COUNT; i++)
        if (input_commands[i].command & LIST_START)
            command_list_start[n++] = (int16_t)i;
    n = 0;
    for (int i = 0; i < INPUT_KEY_COUNT; i++)
        if (input_keys[i].command & LIST_START)
            key_list_start[n++] = (int16_t)i;
    n = 0;
    for (int i = 0; i < INPUT_CHILD_COUNT; i++)
        if (input_children[i] & NODE_HAS_CHILDREN)
            child_list_start[n++] = (uint8_t)i;
    memset(&in, 0, sizeof in);
    in.event_tail = EVENT_RING - 1;
}

/* ---- The input tree ---- */

/* The conditions of the input tree nodes with children. */
enum {
    NODE_IF_ALWAYS,
    NODE_IF_SCREEN_MODE,
    NODE_IF_BUTTON_COUNT,
    NODE_IF_INVENTORY,
    NODE_IF_CHAMPION_ALIVE,
    NODE_IF_CHAMPION_IN_CELL,
    NODE_IF_DRAGGING,
    NODE_IF_ICON_IN_CELL,
    NODE_IF_ACTION_COUNT,
    NODE_IF_RUNE_SET,
    NODE_IF_MINION_MAP_LAYOUT,
    NODE_IF_ACTION_MODE
};
#define MAP_WITHOUT_OPTIONS 5 /* the condition argument of a minion map without option buttons */

/* The champion in the cell relative to the party direction, -1 none. */
static int champion_in_party_cell(int cell)
{
    return champ_get_index_in_cell(dir_turn(cell, game.party_dir));
}

/* The condition of a node with children, tested with the argument of each child. */
static int input_condition(int cond, int arg)
{
    switch (cond) {
    case NODE_IF_ALWAYS:
        return 1;
    case NODE_IF_SCREEN_MODE:
        return arg == dialog.screen_mode;
    case NODE_IF_BUTTON_COUNT:
        return arg == dialog.button_count;
    case NODE_IF_INVENTORY:
        if (arg == ui.inventory)
            return 1;
        if (arg <= CHAMPION_MAX)
            return 0;
        return !ui.inventory || arg - CHAMPION_MAX != ui.inventory;
    case NODE_IF_CHAMPION_ALIVE:
        return champion(arg)->health != 0;
    case NODE_IF_CHAMPION_IN_CELL:
        return champion_in_party_cell(arg) >= 0;
    case NODE_IF_DRAGGING:
        return arg ? input_drag_state != 0 : input_drag_state == 0;
    case NODE_IF_ICON_IN_CELL:
        if (action.champion)
            return 0;
        return arg >= CELL_COUNT || champion_in_party_cell(arg) >= 0;
    case NODE_IF_ACTION_COUNT:
        return action.champion && arg == champ_action_count;
    case NODE_IF_RUNE_SET:
        return action.champion && arg & 1 << champion(action.champion - 1)->rune_count;
    case NODE_IF_MINION_MAP_LAYOUT:
        if (!action.champion)
            return 0;
        return arg == (action.flags & MAP_HAS_OPTIONS ? champ_action_count : MAP_WITHOUT_OPTIONS);
    case NODE_IF_ACTION_MODE:
        return arg == action.mode;
    }
    return 0;
}

static const InputNode *node_child(const InputNode *node, int i)
{
    return &input_nodes[input_children[child_list_start[node->link] + i] & NODE_INDEX_MASK];
}

static int node_child_end(const InputNode *node, int i)
{
    return i > 0 && input_children[child_list_start[node->link] + i] & NODE_HAS_CHILDREN;
}

/* The screen rectangle of an input zone. */
static int input_get_zone_rect(uint16_t zone, int16_t *rect)
{
    if (!zone_get_rect(zone & ZONE_ID_MASK, rect))
        return 0;
    if (zone & (ZONE_IN_PANEL | ZONE_IN_ACTION_AREA)) {
        int16_t x, y;
        zone_get_position(zone & ZONE_IN_PANEL ? ZONE_VIEWPORT : ZONE_ACTION_ORIGIN, &x, &y);
        rect[RECT_X] = (int16_t)(rect[RECT_X] + x);
        rect[RECT_Y] = (int16_t)(rect[RECT_Y] + y);
    }
    return 1;
}

static int rect_contains(const int16_t *r, int x, int y)
{
    return x >= r[RECT_X] && x <= r[RECT_X] + r[RECT_W] - 1 && y >= r[RECT_Y] && y <= r[RECT_Y] + r[RECT_H] - 1;
}

static const InputCommand *node_commands(const InputNode *node)
{
    int list = input_zones[node->link].commands;
    return list == -1 ? NULL : &input_commands[command_list_start[list]];
}

InputMatch input_match = {.zone = ZONE_NONE, .origin = -1};

/* The zone that the position of a command zone is relative to: the inventory panel, the action area, or none. */
static int16_t zone_origin(uint16_t zone)
{
    if (zone & ZONE_IN_PANEL)
        return ZONE_VIEWPORT;
    if (zone & ZONE_IN_ACTION_AREA)
        return ZONE_ACTION_ORIGIN;
    return -1;
}

/* Remembers the matched command entry: its zone, the click position, and its place in its list. */
static void select_entry(const InputCommand *list, const InputCommand *e, int x, int y)
{
    uint16_t command = e->command & COMMAND_MASK;
    input_match.zone = e->zone & ZONE_ID_MASK;
    input_match.origin = zone_origin(e->zone);
    input_match.value = command;
    input_match.choice = (int16_t)(command - ((list->command & COMMAND_MASK) - 1));
    input_match.x = (int16_t)x;
    input_match.y = (int16_t)y;
    memcpy(input_match.rect, in.rect, sizeof input_match.rect);
}

static uint16_t match_click_in_list(const InputCommand *list, int x, int y, int buttons)
{
    if (!list)
        return 0;
    const InputCommand *e = list;
    do {
        if (!(e->flags & COMMAND_OFF) && buttons & e->buttons && input_get_zone_rect(e->zone, in.rect) &&
            rect_contains(in.rect, x, y)) {
            select_entry(list, e, x, y);
            input_match.key = 0;
            return e->command & COMMAND_MASK;
        }
        e++;
    } while (!(e->command & LIST_START));
    in.rect[RECT_W] = in.rect[RECT_H] = 0;
    return 0;
}

static void select_command_in_list(uint16_t command, const InputCommand *list)
{
    input_match.origin = -1;
    if (list)
        for (const InputCommand *e = list; e->command & COMMAND_MASK; e++)
            if (!(e->flags & COMMAND_OFF) && (e->command & COMMAND_MASK) == command) {
                input_get_zone_rect(e->zone, in.rect);
                select_entry(list, e, in.rect[RECT_X], in.rect[RECT_Y]);
                return;
            }
    if (list)
        in.rect[RECT_W] = in.rect[RECT_H] = 0;
    input_match.zone = ZONE_NONE;
    input_match.x = input_match.y = 0;
    input_match.value = command;
    input_match.choice = 0;
}

static uint16_t find_click_command(const InputNode *node, int x, int y, int buttons)
{
    uint16_t command = 0;
    for (int i = 0; !node_child_end(node, i); i++) {
        const InputNode *child = node_child(node, i);
        if (!input_condition(node->cond & ~NODE_HAS_CHILDREN, child->arg))
            continue;
        if (child->cond & NODE_HAS_CHILDREN) {
            command = find_click_command(child, x, y, buttons);
        } else {
            int16_t r[RECT_SIZE];
            if (!input_get_zone_rect(input_zones[child->link].zone, r) || !rect_contains(r, x, y))
                continue;
            command = match_click_in_list(node_commands(child), x, y, buttons);
        }
        if (command)
            return command;
    }
    return command;
}

static uint16_t find_key_command(const InputNode *node, uint16_t key)
{
    uint16_t command = 0;
    for (int i = 0; !node_child_end(node, i); i++) {
        const InputNode *child = node_child(node, i);
        if (!input_condition(node->cond & ~NODE_HAS_CHILDREN, child->arg))
            continue;
        if (child->cond & NODE_HAS_CHILDREN) {
            command = find_key_command(child, key);
            if (command)
                return command;
            continue;
        }
        int list = input_zones[child->link].keys;
        if (list == -1)
            continue;
        const InputKey *k = &input_keys[key_list_start[list]];
        do {
            if (k->key == key) {
                command = k->command & COMMAND_MASK;
                select_command_in_list(command, node_commands(child));
                input_match.key = key;
                break;
            }
            k++;
        } while (!(k->command & LIST_START));
        if (command)
            return command;
    }
    return command;
}

/* The command of key in mode. In the advanced mode, F5 and F9 in the movement mode give the quick save and the quick
   load, Tab and M there open the automap, and a letter key W S A D Q E without a command gives the command of its
   keypad key. */
static uint16_t key_command(int mode, uint16_t key)
{
    uint16_t command = find_key_command(&input_mode_roots[mode], key);
    if (command || game_mode != GAME_MODE_ADVANCED)
        return command;
    if (mode == INPUT_MODE_MOVE && (key == KEY_QUICK_SAVE || key == KEY_QUICK_LOAD))
        return key == KEY_QUICK_SAVE ? CMD_QUICK_SAVE : CMD_QUICK_LOAD;
    if (mode == INPUT_MODE_MOVE && (key == KEY_AUTOMAP || key == KEY_AUTOMAP_M))
        return CMD_AUTOMAP;
    uint16_t keypad = advanced_movement_key(key);
    return keypad ? find_key_command(&input_mode_roots[mode], keypad) : 0;
}

uint16_t input_find_command(int mode, int x, int y, int buttons, int key, int16_t *rect)
{
    uint16_t command =
        key ? key_command(mode, (uint16_t)key) : find_click_command(&input_mode_roots[mode], x, y, buttons);
    memcpy(rect, in.rect, sizeof in.rect);
    return command;
}

/* Marks the zones of the input tree below node whose conditions hold. */
static void mark_active_zones(const InputNode *node, uint8_t *active)
{
    for (int i = 0; !node_child_end(node, i); i++) {
        const InputNode *child = node_child(node, i);
        if (!input_condition(node->cond & ~NODE_HAS_CHILDREN, child->arg))
            continue;
        if (child->cond & NODE_HAS_CHILDREN)
            mark_active_zones(child, active);
        else
            active[child->link] = 1;
    }
}

/* Shows the cursor zone c in front of the zones of the same or a lower priority (mouse_area_add). */
static void cursor_zone_add(int c)
{
    if (memchr(in.cursor_list, c, (size_t)in.cursor_count))
        return;
    int p = 0;
    while (p < in.cursor_count && cursor_zones[in.cursor_list[p] - 1].priority > cursor_zones[c - 1].priority)
        p++;
    memmove(&in.cursor_list[p + 1], &in.cursor_list[p], (size_t)(in.cursor_count - p));
    in.cursor_list[p] = (uint8_t)c;
    in.cursor_count++;
}

/* Hides the cursor zone c (mouse_area_remove). */
static void cursor_zone_remove(int c)
{
    uint8_t *e = memchr(in.cursor_list, c, (size_t)in.cursor_count);
    if (!e)
        return;
    memmove(e, e + 1, (size_t)(&in.cursor_list[--in.cursor_count] - e));
}

/* Marks the active input zones of the mode. A cursor zone of a zone that becomes active shows, of a zone that
   becomes inactive hides; with both changes it stays as it is, as in the original. */
static void update_active_zones(void)
{
    enum { CURSOR_ADD = 1, CURSOR_REMOVE = 2 };
    uint8_t active[INPUT_ZONE_COUNT] = {0}, change[CURSOR_ZONE_COUNT + 1] = {0};
    mark_active_zones(&input_mode_roots[in.mode], active);
    for (int i = 0; i < INPUT_ZONE_COUNT; i++)
        if (active[i] != in.zone_active[i]) {
            change[input_zones[i].cursor] |= active[i] ? CURSOR_ADD : CURSOR_REMOVE;
            in.zone_active[i] = active[i];
        }
    for (int c = 1; c <= CURSOR_ZONE_COUNT; c++) {
        if (change[c] == CURSOR_ADD)
            cursor_zone_add(c);
        if (change[c] == CURSOR_REMOVE)
            cursor_zone_remove(c);
    }
}

int input_cursor_at(int x, int y)
{
    int kind = 0;
    for (int i = 0; i < in.cursor_count; i++) {
        const CursorZone *z = &cursor_zones[in.cursor_list[i] - 1];
        int16_t r[RECT_SIZE];
        if (input_get_zone_rect(z->zone, r) && rect_contains(r, x, y)) {
            kind = z->kind;
            break;
        }
    }
    if (kind == CURSOR_KIND_CHAMPION)
        return CURSOR_CHAMPION;
    if (kind != CURSOR_KIND_HAND || (int16_t)game.leader < 0)
        return CURSOR_ARROW;
    return inv_cursor_object() == THING_NONE ? CURSOR_HAND : CURSOR_OBJECT;
}

/* The arrow (g_cursor_arrow_bitmap) and the hand (g_cursor_hand_bitmap) of input_init: 4 bpp, color 12 clear. */
static const uint8_t cursor_arrow[ARROW_W * CURSOR_H / 2] = {
    0x00, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0x0f, 0x0c, 0xcc, 0xcc, 0xcc, 0xcc, 0x0f, 0xf0, 0xcc, 0xcc,
    0xcc, 0xcc, 0x0f, 0xff, 0x0c, 0xcc, 0xcc, 0xcc, 0x0f, 0xff, 0xf0, 0xcc, 0xcc, 0xcc, 0x0f, 0xff,
    0xff, 0x0c, 0xcc, 0xcc, 0x0f, 0xff, 0xff, 0xf0, 0xcc, 0xcc, 0x0f, 0xff, 0xff, 0xff, 0x0c, 0xcc,
    0x0f, 0xff, 0xff, 0xff, 0xf0, 0xcc, 0x0f, 0xff, 0xff, 0x00, 0x00, 0x0c, 0x0f, 0xf0, 0xff, 0x0c,
    0xcc, 0xcc, 0x0f, 0x0c, 0x0f, 0xf0, 0xcc, 0xcc, 0x00, 0xcc, 0x0f, 0xf0, 0xcc, 0xcc, 0x0c, 0xcc,
    0xc0, 0xff, 0x0c, 0xcc, 0xcc, 0xcc, 0xc0, 0xff, 0x0c, 0xcc, 0xcc, 0xcc, 0xcc, 0x00, 0x0c, 0xcc,
};
static const uint8_t cursor_hand[HAND_W * CURSOR_H / 2] = {
    0xff, 0x0c, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0x0f, 0xf0, 0xf0, 0xf0, 0xcc, 0xcc, 0xcc, 0xcc, 0xc0, 0xff, 0x0f,
    0x0f, 0x0f, 0x0c, 0xcc, 0xcc, 0xcc, 0x0f, 0xf0, 0xf0, 0xf0, 0xf0, 0xcc, 0xcc, 0xcc, 0xc0, 0xff, 0x0f, 0x0f, 0x0f,
    0x0c, 0xcc, 0xcc, 0xc0, 0xff, 0xf0, 0xf0, 0xf0, 0xf0, 0xcc, 0x00, 0xcc, 0x0f, 0xff, 0xff, 0xff, 0xf0, 0xcc, 0xff,
    0x0c, 0x0f, 0xff, 0xff, 0xff, 0xff, 0x0c, 0x0f, 0xf0, 0x0f, 0xff, 0xff, 0xff, 0xff, 0x0c, 0x0f, 0xff, 0x0f, 0xff,
    0xff, 0xff, 0xff, 0x0c, 0xc0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0c, 0xc0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x0c, 0xcc, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xcc, 0xc0, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xcc, 0xcc,
    0xc0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xcc, 0xcc, 0xcc, 0xc0, 0xff, 0xff, 0xff, 0xff,
};
static uint8_t champion_cursor[FACE_SIZE * FACE_SIZE];
static uint8_t champion_cursor_key;

void input_set_champion_cursor(int champ)
{
    Bitmap *face = draw_temp_bitmap(FACE_SIZE, FACE_SIZE, IMAGE_4BPP);
    if (!face)
        return;
    memset(face->pixels, 0, (FACE_SIZE + 1) / 2 * FACE_SIZE);
    uint8_t colors[FACE_ICON_COLORS];
    ui_draw_champion_face_icon(face, champ, colors);
    Bitmap cursor = {champion_cursor, FACE_SIZE, FACE_SIZE, IMAGE_8BPP};
    memset(champion_cursor, UI_KEY, sizeof champion_cursor);
    const int16_t r[RECT_SIZE] = {0, 0, FACE_SIZE, FACE_SIZE};
    ui_blit_bitmap(face, &cursor, r, 0, 0, FACE_CURSOR_SKIP, colors);
    champion_cursor_key = colors[UI_KEY];
    draw_free_temp();
}

CursorImage input_cursor_image(int cursor)
{
    switch (cursor) {
    case CURSOR_HAND:
        return (CursorImage){cursor_hand, ui_colors, IMAGE_4BPP, HAND_W, CURSOR_H, 0, 0, UI_KEY};
    case CURSOR_OBJECT: {
        static uint8_t object_cursor[OBJECT_CURSOR_SIZE * OBJECT_CURSOR_SIZE];
        uint8_t key = leader_hand.palette[HAND_ICON_KEY], shadow[HAND_ICON_COLORS];
        memset(shadow, ui_colors[UI_BLACK], sizeof shadow);
        memset(object_cursor, key, sizeof object_cursor);
        const Bitmap icon = {leader_hand.cursor, HAND_ICON_SIZE, HAND_ICON_SIZE, IMAGE_8BPP};
        Bitmap image = {object_cursor, OBJECT_CURSOR_SIZE, OBJECT_CURSOR_SIZE, IMAGE_8BPP};
        const int16_t s[RECT_SIZE] = {OBJECT_SHADOW, OBJECT_SHADOW, HAND_ICON_SIZE, HAND_ICON_SIZE};
        const int16_t r[RECT_SIZE] = {0, 0, HAND_ICON_SIZE, HAND_ICON_SIZE};
        ui_blit_bitmap(&icon, &image, s, 0, 0, HAND_ICON_KEY, shadow);
        ui_blit_bitmap(&icon, &image, r, 0, 0, HAND_ICON_KEY, leader_hand.palette);
        return (CursorImage){object_cursor,      NULL,       IMAGE_8BPP, OBJECT_CURSOR_SIZE,
                             OBJECT_CURSOR_SIZE, OBJECT_HOT, OBJECT_HOT, key};
    }
    case CURSOR_CHAMPION:
        return (CursorImage){champion_cursor, NULL,       IMAGE_8BPP, FACE_SIZE,
                             FACE_SIZE,       FACE_HOT_X, FACE_HOT_Y, champion_cursor_key};
    default:
        return (CursorImage){cursor_arrow, ui_colors, IMAGE_4BPP, ARROW_W, CURSOR_H, 0, 0, UI_KEY};
    }
}

void input_draw_cursor(uint8_t *dst, int x, int y)
{
    if (ui.eye || ui.mouth)
        return; /* the original hides the pointer while the eye or the mouth is held */
    CursorImage c = input_cursor_image(input_cursor_at(x, y));
    for (int cy = 0; cy < c.height; cy++)
        for (int cx = 0; cx < c.width; cx++) {
            int sx = x - c.hot_x + cx, sy = y - c.hot_y + cy, i = cy * c.width + cx;
            if (sx < 0 || sy < 0 || sx >= SCREEN_W || sy >= SCREEN_H)
                continue;
            int n = c.bpp == IMAGE_4BPP ? (c.bits[i / 2] >> (i & 1 ? 0 : NIBBLE_BIT_COUNT)) & NIBBLE_MASK : c.bits[i];
            if (n != c.key)
                dst[sy * SCREEN_W + sx] = c.palette ? c.palette[n] : (uint8_t)n;
        }
}

void input_set_mode(int mode)
{
    if (mode != in.mode)
        input_flush();
    in.mode = mode;
    update_active_zones();
}

int input_get_mode(void)
{
    return in.mode;
}

void input_refresh_mode(void)
{
    update_active_zones();
}

void input_push_mode(int mode)
{
    in.saved_mode = in.mode;
    input_reset_states();
    input_set_mode(mode);
}

void input_pop_mode(void)
{
    input_set_mode(in.saved_mode);
}

/* ---- Events ---- */

void input_mouse_click(int x, int y, int type)
{
    if (in.events_locked) {
        in.deferred = 1;
        in.deferred_x = x;
        in.deferred_y = y;
        in.deferred_type = type;
        return;
    }
    int limit =
        (type == INPUT_LEFT_RELEASED && !in.dropped_left) || type == EVENT_ENTER_AREA || type == EVENT_LEAVE_AREA
            ? EVENT_LIMIT_SPECIAL
            : EVENT_LIMIT;
    in.dropped_left = 0;
    if (in.event_count < limit) {
        in.event_tail = (in.event_tail + 1) % EVENT_RING;
        in.events[in.event_tail].type = (int16_t)type;
        in.events[in.event_tail].x = (int16_t)x;
        in.events[in.event_tail].y = (int16_t)y;
        in.event_count++;
    } else if (type == INPUT_BUTTON_LEFT) {
        in.dropped_left = 1;
    }
}

void input_mouse_down(int x, int y, int button)
{
    input_mouse_buttons |= button;
    input_mouse_click(x, y, button);
}

void input_mouse_up(int x, int y, int button)
{
    input_mouse_buttons &= ~button;
    if (button == INPUT_BUTTON_LEFT && (ui.eye || ui.mouth || advanced_automap_open))
        input_mouse_click(x, y, INPUT_LEFT_RELEASED);
}

static void flush_deferred_click(void)
{
    if (!in.deferred)
        return;
    in.deferred = 0;
    input_mouse_click(in.deferred_x, in.deferred_y, in.deferred_type);
}

static void enqueue_key(uint16_t key)
{
    if (in.event_count < EVENT_LIMIT) {
        in.event_tail = (in.event_tail + 1) % EVENT_RING;
        in.events[in.event_tail].type = INPUT_KEY;
        in.events[in.event_tail].x = (int16_t)key;
        in.event_count++;
    }
    in.events_locked = 0;
    flush_deferred_click();
}

void input_key_down(uint16_t key)
{
    if (in.key_count < KEY_BUFFER)
        in.keys[in.key_count++] = key;
}

void input_flush(void)
{
    in.key_count = 0;
    int pos = (in.event_tail + 1) % EVENT_RING;
    if (pos != in.event_head || in.event_count) {
        /* only area changes and released left buttons stay */
        int write = in.event_head, read = in.event_head, kept = 0;
        for (int n = in.event_count; n > 0; n--) {
            int t = in.events[read].type;
            if (t == EVENT_ENTER_AREA || t == EVENT_LEAVE_AREA || t == INPUT_LEFT_RELEASED) {
                in.events[write] = in.events[read];
                write = (write + 1) % EVENT_RING;
                kept++;
            }
            read = (read + 1) % EVENT_RING;
        }
        in.event_count = kept;
        in.event_tail = (write + EVENT_RING - 1) % EVENT_RING;
    }
    in.disabled = 0;
    in.queue_head = in.queue_tail = 0;
    in.rect[RECT_W] = in.rect[RECT_H] = 0;
    input_match.zone = ZONE_NONE;
    input_match.origin = -1;
    input_match.x = input_match.y = 0;
    input_match.value = 0;
    input_match.choice = 0;
    in.events_locked = 0;
    flush_deferred_click();
}

/* ---- Click feedback ---- */

void input_refine_champion_click(Command *c)
{
    int command = c->command;
    if (command >= CMD_HAND_FIRST && command <= CMD_HAND_LAST) {
        int n = command - CMD_HAND_FIRST, hand = n & 1;
        int champ = champion_in_party_cell(n / 2);
        if (champ < 0 || champion(champ)->action_delay[hand] ||
            !champ_list_actions(champ, champion(champ)->slots[hand], hand))
            c->command = 0;
        else
            c->command = (uint16_t)(CMD_HAND_FIRST + champ * 2 + hand);
        return;
    }
    if (command < CMD_PARTY_ICON_FIRST || command > CMD_PARTY_ICON_LAST)
        return;
    int icon = command - CMD_PARTY_ICON_FIRST; /* the cell relative to the party direction */
    int champ = champion_in_party_cell(icon);
    if (champ < 0) {
        c->command = 0;
        return;
    }
    const int16_t *r = c->rect;
    /* the distances to the edges of the icon that face the party center */
    int16_t dy = (int16_t)(icon < 2 ? r[RECT_Y] + r[RECT_H] - 1 - c->y : c->y - r[RECT_Y]);
    int16_t dx = (int16_t)(icon == 1 || icon == 2 ? c->x - r[RECT_X] : r[RECT_X] + r[RECT_W] - 1 - c->x);
    if (dx <= dy)
        c->command = (uint16_t)(CMD_LEADER_FIRST + icon);
    else if (champion(champ)->action_delay[ACTION_SPELL])
        c->command = 0;
}

/* The script that shows a command being pressed (and queues it); NULL for no command. */
static const uint8_t *click_feedback_script(Command *c)
{
    input_refine_champion_click(c);
    uint16_t command = c->command;
    int offset;
    if (!command || command > CMD_AUTOMAP)
        return NULL;
    if ((command >= CMD_SLOT_FIRST && command <= CMD_SLOT_LAST) || command == CMD_VOLUME ||
        (command >= CMD_DRAG_FIRST && command <= CMD_DRAG_CANCEL) ||
        (command >= CMD_LEADER_FIRST && command <= CMD_LEADER_LAST) ||
        (command >= CMD_SOUND_VOLUME_FIRST && command <= CMD_MUSIC_VOLUME_LAST) ||
        (command >= CMD_EDIT_CHAMPION_NAME && command <= CMD_EDIT_CANCEL))
        offset = SCRIPT_EXECUTE;
    else if (command <= CMD_MOVE_LEFT)
        offset = (command - CMD_TURN_LEFT) * SCRIPT_MOVE_SIZE + SCRIPT_MOVE_FIRST;
    else if (command >= CMD_HAND_FIRST && command <= CMD_HAND_LAST)
        offset = (command - CMD_HAND_FIRST) % SLOT_HAND_COUNT * SCRIPT_HAND_SIZE + SCRIPT_HAND_FIRST;
    else if (command >= CMD_PARTY_ICON_FIRST && command <= CMD_PARTY_ICON_LAST)
        offset = (command - CMD_PARTY_ICON_FIRST) * SCRIPT_PARTY_ICON_SIZE + SCRIPT_PARTY_ICON_FIRST;
    else if (command == CMD_ACTION_CANCEL)
        offset = SCRIPT_ACTION_CANCEL;
    else if (command == CMD_ROTATE_LEFT || command == CMD_ROTATE_RIGHT)
        offset = (command - CMD_ROTATE_LEFT) * SCRIPT_ROTATE_SIZE + SCRIPT_ROTATE_FIRST;
    else if (command >= CMD_RUNE_FIRST && command <= CMD_RUNE_LAST)
        offset = SCRIPT_RUNE;
    else if (command == CMD_REMOVE_RUNE)
        offset = SCRIPT_REMOVE_RUNE;
    else if (command == CMD_CAST_SPELL)
        offset = SCRIPT_CAST_SPELL;
    else if (command > CMD_ACTION_CANCEL && command <= CMD_ACTION_LAST)
        offset = (command - CMD_ACTION_CANCEL - 1) * SCRIPT_ACTION_SIZE + SCRIPT_ACTION_FIRST;
    else if (command == CMD_INVENTORY_LEADER || command == CMD_RECRUIT_CANCEL)
        offset = SCRIPT_INVENTORY_CLOSE;
    else if (command == CMD_SLEEP)
        offset = SCRIPT_SLEEP;
    else if (command == CMD_SAVE)
        offset = SCRIPT_SAVE;
    else if (command >= CMD_MAP_OPTION_FIRST && command <= CMD_MAP_OPTION_LAST)
        offset = (command - CMD_MAP_OPTION_FIRST) * SCRIPT_MAP_OPTION_SIZE + SCRIPT_MAP_OPTION_FIRST;
    else if (command >= CMD_DIALOG_BUTTON_FIRST && command <= CMD_DIALOG_BUTTON_LAST)
        offset = SCRIPT_EXECUTE; /* dialog buttons (Step 20) */
    else
        offset = command == CMD_VIEWPORT ? SCRIPT_VIEWPORT : SCRIPT_QUEUE;
    return click_feedback_scripts + offset;
}

void rect_center_in(int16_t *dst, const int16_t *src, int w, int h)
{
    dst[RECT_W] = (int16_t)w;
    dst[RECT_X] = (int16_t)(src[RECT_X] + ((src[RECT_W] - w + 1) >> 1));
    dst[RECT_H] = (int16_t)h;
    dst[RECT_Y] = (int16_t)(src[RECT_Y] + ((src[RECT_H] - h + 1) >> 1));
}

/* Feedback script opcodes (low 6 bits; bit 6: run on, bit 7: end). */
enum {
    FB_DELAY,
    FB_RELEASE_QUEUE,
    FB_WAIT_QUEUE,
    FB_WAIT_STEP,
    FB_QUEUE,
    FB_EXECUTE,
    FB_QUEUE_AND_RELEASE,
    FB_PICTURE,
    FB_SPELL_ICON,
    FB_HAND_ICON,
    FB_RUNE_BUTTON,
    FB_ENTERED_RUNES,
    FB_ACTION_NAME,
    FB_INVENTORY_BUTTON,
    FB_MAP_OPTION_BUTTONS,
    FB_DIALOG_BUTTON,
    FB_DIALOG_BUTTON_2
};
#define FB_OP_MASK 0x3f
#define FB_RUN_ON  0x40
#define FB_END     0x80

static void queue_current(void)
{
    in.queue[in.queue_tail] = in.current;
    in.queue_tail = (in.queue_tail + 1) % COMMAND_QUEUE;
}

/* The commands that wait in the queue keep the input disabled until they run. */
static void release_queue(void)
{
    in.disabled = (in.queue_tail - in.queue_head + COMMAND_QUEUE) % COMMAND_QUEUE;
}

/* The feedback bitmap of the clicked button, open or closed. */
typedef struct {
    Offscreen o;
    int opened;
} Feedback;

/* The arguments of a picture opcode: a GDAT image and its transparent color; a dialog button has the first three. */
enum { PICTURE_CATEGORY, PICTURE_INDEX, PICTURE_ENTRY, PICTURE_KEY, PICTURE_ARGS };
#define DIALOG_BUTTON_ARGS PICTURE_KEY

/* Opens the feedback bitmap of the clicked button when it is closed; r becomes a w x h rect centered in it. */
static void feedback_init_rect(Feedback *f, int16_t *r, int w, int h)
{
    if (!f->opened) {
        memcpy(f->o.rect, in.current.rect, sizeof f->o.rect);
        offscreen_create(&f->o, -1, 0);
        f->opened = 1;
    }
    rect_center_in(r, f->o.rect, w, h);
}

/* Draws the picture of the arguments at p centered in the clicked button. */
static void feedback_picture(Feedback *f, const uint8_t *p)
{
    int category = p[PICTURE_CATEGORY], index = p[PICTURE_INDEX], entry = p[PICTURE_ENTRY];
    const Bitmap *image = draw_gdat_image(category, index, entry);
    if (!image)
        return;
    int16_t r[RECT_SIZE];
    feedback_init_rect(f, r, image->width, image->height);
    offscreen_blit(&f->o, image, r, 0, 0, (int8_t)p[PICTURE_KEY], 0, draw_gdat_image_palette(category, index, entry));
}

/* Draws the pressed rune button with the rune of the pressed button in the next rune set. */
static void feedback_rune_button(Feedback *f)
{
    const Bitmap *image = draw_gdat_image(CAT_INTERFACE, UI_INDEX_SPELL, GFX_SPELL_RUNE_PRESSED);
    if (!image)
        return;
    int16_t r[RECT_SIZE], w, h;
    feedback_init_rect(f, r, image->width, image->height);
    offscreen_blit(&f->o, image, r, 0, 0, GFX_OPAQUE, 0,
                   draw_gdat_image_palette(CAT_INTERFACE, UI_INDEX_SPELL, GFX_SPELL_RUNE_PRESSED));
    /* the rune of the pressed button in the next rune set */
    int set = champion(action.champion - 1)->rune_count, rune = in.current.command - CMD_RUNE_FIRST;
    char text[2] = {(char)(set * SPELL_RUNES_PER_SET + SPELL_RUNE_GLYPH_FIRST + rune), '\0'};
    text_measure(text, &w, &h);
    rect_center_in(r, f->o.rect, w, h);
    offscreen_draw_text_at(&f->o, r[RECT_X], r[RECT_Y] + r[RECT_H] - 1, ui_colors[UI_BLACK], ui_colors[UI_CYAN], text);
}

/* Copies the drawn action area to the screen. */
static void action_area_present(void)
{
    offscreen_flush(area_begin_draw(0), 1);
}

/* Draws the pressed hand icon of the action champion, or of the champion of a hand command, in the action area. */
static void feedback_hand_icon(int icon)
{
    int champ = action.champion - 1;
    if (!action.champion) {
        if (in.current.command < CMD_HAND_FIRST || in.current.command > CMD_HAND_LAST)
            return;
        champ = (in.current.command - CMD_HAND_FIRST) / SLOT_HAND_COUNT;
    }
    area_draw_hand_icon(champ, icon, 1);
    action_area_present();
}

/* Draws the pressed spell icon of the champion in the party cell in the action area. */
static void feedback_spell_icon(int cell)
{
    int champ = champion_in_party_cell(cell);
    if (champ < 0)
        return;
    area_draw_spell_icon(champ, 1);
    action_area_present();
}

/* Runs the opcode op of a feedback script with its arguments at p; returns the next opcode. */
static const uint8_t *feedback_run_op(Feedback *f, uint8_t op, const uint8_t *p, int *queue_full)
{
    switch (op & FB_OP_MASK) {
    case FB_DELAY:
        in.script_delay = *p++;
        break;
    case FB_RELEASE_QUEUE:
        release_queue();
        break;
    case FB_WAIT_QUEUE:
        in.feedback_waiting = 1;
        break;
    case FB_WAIT_STEP:
        input_feedback_busy = 1;
        break;
    case FB_QUEUE:
    case FB_QUEUE_AND_RELEASE:
        if ((in.queue_tail + 1) % COMMAND_QUEUE == in.queue_head) {
            if (!in.disabled)
                in.disabled = 1;
            *queue_full = 1; /* the script waits */
            break;
        }
        queue_current();
        if ((op & FB_OP_MASK) == FB_QUEUE_AND_RELEASE)
            release_queue();
        break;
    case FB_EXECUTE:
        in.script = p;
        if (!command_execute(&in.current))
            p = in.script - 1;
        else if (input_command_stop)
            release_queue();
        break;
    case FB_PICTURE:
        feedback_picture(f, p);
        p += PICTURE_ARGS;
        break;
    case FB_RUNE_BUTTON:
        feedback_rune_button(f);
        break;
    case FB_SPELL_ICON:
        feedback_spell_icon(*p++);
        break;
    case FB_HAND_ICON:
        feedback_hand_icon(*p++);
        break;
    case FB_ACTION_NAME:
        area_draw_name(*p++, 1);
        action_area_present();
        break;
    case FB_ENTERED_RUNES:
        area_draw_entered_runes(1);
        action_area_present();
        break;
    case FB_MAP_OPTION_BUTTONS:
        mmap_draw_option_buttons(*p++);
        action_area_present();
        break;
    case FB_INVENTORY_BUTTON:
        if (ui.inventory) {
            ui.buttons |= *p;
            ui_draw_champion_state(ui.inventory - 1);
        }
        p++;
        break;
    case FB_DIALOG_BUTTON:
    case FB_DIALOG_BUTTON_2:
        dialog.button_category = p[PICTURE_CATEGORY];
        dialog.button_index = p[PICTURE_INDEX];
        dialog.button_entry = p[PICTURE_ENTRY];
        p += DIALOG_BUTTON_ARGS;
        dialog_draw_button(input_match.zone, 1);
        f->opened = 0; /* the original forgets the open feedback bitmap */
        dialog_present();
        break;
    default:
        break;
    }
    return p;
}

/* Runs the click feedback script for elapsed timer ticks: pictures of pressed buttons, then the command. */
static void run_click_feedback(int elapsed)
{
    if (in.script_delay) {
        if (elapsed < in.script_delay) {
            in.script_delay -= elapsed;
            return;
        }
        in.script_delay = 0;
    }
    if (in.disabled || in.feedback_waiting || input_feedback_busy || !in.script)
        return;
    Feedback f = {0};
    int queue_full = 0;
    uint8_t op;
    do {
        op = in.script[0];
        const uint8_t *p = feedback_run_op(&f, op, in.script + 1, &queue_full);
        if (queue_full)
            break;
        in.script = p;
        if (op & FB_END) {
            in.script = NULL;
            break;
        }
    } while (op & FB_RUN_ON);
    if (f.opened)
        offscreen_flush(&f.o, 1);
}

/* The open automap takes the keys and the mouse events until it closes; the mouse drags the map. */
static void automap_input(void)
{
    uint16_t key;
    while (advanced_automap_open && (key = input_read_key()))
        advanced_automap_event(INPUT_KEY, key, 0);
    while (advanced_automap_open && in.event_count) {
        int type = in.events[in.event_head].type, x = in.events[in.event_head].x, y = in.events[in.event_head].y;
        in.event_count--;
        in.event_head = (in.event_head + 1) % EVENT_RING;
        advanced_automap_event(type, x, y);
    }
    if (advanced_automap_open)
        advanced_automap_drag(input_mouse_x, input_mouse_y);
    else
        input_flush(); /* the input after the close key does not reach the game */
}

void input_poll(void)
{
    int elapsed = (int)(main_loop.timer_ticks - in.last_ticks);
    if (!elapsed)
        return;
    in.last_ticks = main_loop.timer_ticks;
    if (advanced_automap_open) {
        automap_input();
        return;
    }
    if (!in.disabled && !in.feedback_waiting && !input_feedback_busy) {
        while (!in.script) {
            for (int i = 0; i < in.key_count; i++)
                enqueue_key(in.keys[i]);
            in.key_count = 0;
            in.events_locked = 1;
            uint16_t command = 0;
            while (!command) {
                if (!in.event_count)
                    break;
                int type = in.events[in.event_head].type, x = in.events[in.event_head].x,
                    y = in.events[in.event_head].y;
                in.event_count--;
                in.event_head = (in.event_head + 1) % EVENT_RING;
                if (type < INPUT_KEY) {
                    if (type & INPUT_LEFT_RELEASED)
                        command = CMD_CANCEL_CAPTURE;
                    else if (type & EVENT_CLICK_TYPES)
                        command = find_click_command(&input_mode_roots[in.mode], x, y, type);
                } else if (type == INPUT_KEY) {
                    command = key_command(in.mode, (uint16_t)x);
                } else if (type == EVENT_ENTER_AREA) {
                    command = CMD_DRAG_CANCEL;
                } else if (type == EVENT_LEAVE_AREA) {
                    command = CMD_CANCEL_PUSH;
                }
                if (command) {
                    in.current.x = input_match.x;
                    in.current.y = input_match.y;
                    memcpy(in.current.rect, in.rect, sizeof in.rect);
                    in.current.command = command;
                }
            }
            in.events_locked = 0;
            flush_deferred_click();
            if (!command)
                break;
            in.script = click_feedback_script(&in.current);
            elapsed = 0;
        }
    }
    run_click_feedback(elapsed);
}

void input_reset_states(void)
{
    ui.eye = ui.mouth = 0;
    party.push_mode = 0;
    ui.push_mode = 0;
    input_feedback_busy = 0;
    in.feedback_waiting = 0;
    in.script = NULL;
    input_drag_state = 0;
}

void input_cancel_push_mode(void)
{
    if (!party.push_mode)
        return;
    party.push_mode = 0;
    ui.push_mode = 0;
    ui_draw_movement_arrows();
}

/* Draws a movement arrow released or pressed. */
static void ui_draw_move_arrow(int entry, int zone, int pressed)
{
    in.arrow_pressed = pressed;
    in.arrow_zone = zone;
    in.arrow_entry = entry;
    Offscreen o;
    offscreen_create(&o, zone, 1);
    gfx_fill_bitmap(o.bitmap, ui_colors[UI_BLACK]);
    offscreen_draw_gdat_image(&o, CAT_INTERFACE, UI_INDEX_ARROWS, entry + (pressed != 0), zone, GFX_OPAQUE);
    offscreen_flush(&o, 1);
    draw_free_temp();
}

void input_release_move_arrow(void)
{
    if (in.arrow_pressed)
        ui_draw_move_arrow(in.arrow_entry, in.arrow_zone, 0);
}

/* ---- Clicks in the dungeon view ---- */

/* The squares of a wall ornament click: the party square, left, right, front. */
enum { WALL_CELL_PARTY, WALL_CELL_LEFT, WALL_CELL_RIGHT, WALL_CELL_FRONT };
#define PUSH_RESISTANCE_FIXED 0xff /* a creature that never moves: a click on it only sounds */

static int zone_contains_point(int zone, int x, int y)
{
    int16_t r[RECT_SIZE];
    return zone_get_rect(zone, r) && rect_contains(r, x, y);
}

/* A click on a wall ornament of the view (cell: a WALL_CELL_* square). */
static void view_click_wall_sensor(int cell)
{
    static const int8_t side_turn[DIR_COUNT] = {1, 1, 3,
                                                2}; /* the face that the party sees of each wall, from its direction */
    int dir = game.party_dir, x = game.party_x, y = game.party_y;
    if (cell == WALL_CELL_LEFT) {
        x += dir_dx[dir_turn(dir, TURN_LEFT)];
        y += dir_dy[dir_turn(dir, TURN_LEFT)];
    } else if (cell == WALL_CELL_RIGHT) {
        x += dir_dx[dir_turn(dir, TURN_RIGHT)];
        y += dir_dy[dir_turn(dir, TURN_RIGHT)];
    } else if (cell == WALL_CELL_FRONT) {
        x += dir_dx[dir];
        y += dir_dy[dir];
    }
    if (x < 0 || x >= dungeon_map_width(party.cur_map) || y < 0 || y >= dungeon_map_height(party.cur_map))
        return;
    sensor_process_wall_click(x, y, dir_turn(dir, side_turn[cell & DIR_MASK]), THING_NONE, game.leader_hand);
}

/* A click on a static creature in front: a push of its middle starts the push arrows, a side turns it. */
static void view_click_creature(uint16_t group, int x, int y, int square_x, int square_y)
{
    int side;
    if (!view_hit_test_creature(x, y, square_x, square_y, &side))
        return;
    if (creature_group_info(group)->push_resistance == PUSH_RESISTANCE_FIXED) {
        sound_request_with_fallback(CAT_CREATURE, item_gdat_index(group), SOUND_ENTRY_CLICK, SOUND_FALLBACK, square_x,
                                    square_y, 0, SOUND_PRIORITY_CLICK, SOUND_VOLUME_NORMAL);
    } else if (!side) {
        party.push_mode = 1;
        ui.push_mode = 1;
        ui_draw_movement_arrows();
        party.push_arrow = PUSH_ARROW_NONE;
    } else {
        creature_turn_group(group, 0, side);
    }
}

/* A click on the wall in front: a plain wall sounds; the mouse drag through a fake wall is not ported. */
static void view_click_front_wall(int passable, int x, int y)
{
    if (!game.champion_count)
        return;
    if (!passable)
        sound_request_with_fallback(CAT_WALLSET, dungeon_map_wallset(game.party_map), SOUND_ENTRY_CLICK, SOUND_FALLBACK,
                                    x, y, 0, SOUND_PRIORITY_CLICK, SOUND_VOLUME_LOUD);
    input_command_stop = 1;
}

/* A door button of the door at (x, y): the door toggles, the button shows pressed until the next game step. */
static void view_click_door_button(int x, int y, uint16_t door)
{
    sound_request(CAT_DUNGEON, 0, SOUND_ENTRY_ACTIVATE, SOUND_PRIORITY_CLICK, SOUND_VOLUME_NORMAL, game.party_x,
                  game.party_y, 1);
    sensor_schedule_square_event(x, y, 0, EFFECT_TOGGLE, game.game_time + 1);
    TimelineEvent e = {.time_map = timeline_time_map(game.game_time + 1, party.cur_map),
                       .type = EVENT_DOOR_BUTTON_RELEASE};
    uint16_t first = dungeon_square_first_thing(party.cur_map, x, y);
    put_le16(e.data + SENSOR_EVENT_THING, first);
    timeline_add_event(&e);
    door_set_flag(door, DOOR_BUTTON_PRESSED, true);
    input_command_stop = 1;
}

#define CELL_POSSESSION_FIRST 4 /* drop cells 4 to 7: the possession cells of a static creature */
#define CELL_POSSESSION_LAST  7

static void moneybox_clear_flag(uint16_t thing)
{
    if (item_is_moneybox(thing))
        moneybox_set_placed(thing, false);
}

/* A click with an empty hand on the object of click zone index: the leader takes the object. */
static void view_click_take_object(int x, int y, int index)
{
    if ((int16_t)game.leader == -1)
        return;
    const ViewClickZone *z = &view_click_zones[index];
    int sx = game.party_x, sy = game.party_y;
    if (z->cell) {
        if (z->cell != POS_FRONT)
            return;
        sx += dir_dx[game.party_dir];
        sy += dir_dy[game.party_dir];
    }
    uint16_t thing = view_hit_test_object(x, y, z->cell, sx, sy, z->thing, z->kind, 0);
    if (thing != THING_NONE) {
        move_thing(thing, sx, sy, -1, z->kind == CLICK_POSSESSIONS ? -1 : 0);
        moneybox_clear_flag(thing);
        champ_put_object_in_leader_hand(thing, 1);
    }
    input_command_stop = 1;
}

/* Drops the leader hand object into cell of the square (x, y): 0 to 3 a floor cell, 4 to 7 a possession cell of
   a static creature, relative to the party direction. Returns 1 when done. */
static int view_click_drop_object(int x, int y, int cell)
{
    if ((int16_t)game.leader == -1)
        return 0;
    int dir_cell = dir_turn(game.party_dir, cell);
    uint16_t group = creature_group_at(party.cur_map, x, y);
    int possession = cell >= CELL_POSSESSION_FIRST && cell <= CELL_POSSESSION_LAST;
    if (group != THING_NONE) {
        const CreatureInfo *info = creature_group_info(group);
        if (info->flags & CREATURE_STATIC) {
            if (possession && !(info->possessions & CREATURE_POSSESSIONS_ON_FLOOR))
                return 0;
            if (cell < CELL_POSSESSION_FIRST && !(info->possessions & POSSESSIONS_FLOOR_CLICKABLE))
                return 0;
        } else {
            group = THING_NONE;
        }
    }
    uint16_t thing = champ_remove_object_from_leader_hand();
    if (possession) {
        dir_cell = dir_turn(cell - CELL_POSSESSION_FIRST, game.party_dir);
        if (group != THING_NONE)
            for (uint16_t t = thing_word(group, GROUP_POSSESSIONS); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
                if (thing_cell(t) == dir_cell)
                    moneybox_clear_flag(t);
        move_thing(thing_with_cell(thing, dir_cell), -1, -1, x, y);
    } else {
        move_thing(thing_with_cell(thing, dir_cell), -1, 0, x, y);
    }
    input_command_stop = 1;
    return 1;
}

/* A click with the leader hand object on the objects of a square cell: it goes into an open chest there (1), or
   it drops into the cell when the click hits no object; pos: the view position of the square (0 or 3). */
static int view_click_place_object(int x, int y, int sx, int sy, int pos, int cell, int kind)
{
    uint16_t hand = thing_with_cell(game.leader_hand, dir_turn(game.party_dir, cell));
    uint16_t hit = view_hit_test_object(x, y, pos, sx, sy, hand, kind, 1);
    if (hit == THING_NONE) {
        champ_remove_object_from_leader_hand();
        return 1;
    }
    if (hit == THING_ENDOFLIST)
        return view_click_drop_object(sx, sy, kind == CLICK_POSSESSIONS ? cell + CELL_POSSESSION_FIRST : cell);
    return 0;
}

/* A click in the upper view with the leader hand object: the leader throws it to the left or the right side. */
static void view_click_throw_object(int x, int y)
{
    /* the original tests for square type 0x11 in front, which never occurs, and then uses zones 0x300 and 0x301 */
    int side;
    if (zone_contains_point(ZONE_FRONT_WALL_LOW, x, y))
        side = 0;
    else if (zone_contains_point(ZONE_FRONT_WALL_HIGH, x, y))
        side = 1;
    else
        return;
    if (champ_throw_leader_hand_object(side))
        input_command_stop = 1;
}

#define HALF_CELLS                 2 /* relative cells 0 and 1: the front half of a square, 2 and 3: its back half */
#define POSSESSION_VIEW_CELL_FIRST HALF_CELLS /* the possessions of a static creature show in the back half */
#define ALCOVE_CELL                2          /* the alcove in front keeps its objects in relative cell 2 */

/* The party direction, the party square, and the square in front of it at the click. */
typedef struct {
    int dir, px, py, fx, fy;
} ViewSquares;

/* A click on a door button zone: a door without a button passes the click to the wall beside it. */
static void view_click_door_button_zone(const ViewSquares *v, const ViewClickZone *z, uint16_t hand)
{
    int front = z->cell == POS_FRONT, dx = front ? v->fx : v->px, dy = front ? v->fy : v->py;
    uint16_t door = dungeon_square_first_thing(party.cur_map, dx, dy);
    if (!door_flag(door, DOOR_BUTTON)) {
        int side = dir_turn(v->dir, TURN_RIGHT);
        sensor_process_wall_click(dx + dir_dx[side], dy + dir_dy[side], dir_turn(v->dir, TURN_LEFT), THING_NONE, hand);
    } else if (hand == THING_NONE) {
        view_click_door_button(dx, dy, door);
    }
}

/* A click on the click zones of the view: objects, door buttons, and wall ornaments; returns 1 when it is done. */
static int view_click_zones_hit(const ViewSquares *v, int x, int y, uint16_t hand)
{
    for (int i = 0; i < view_click_zone_count; i++) {
        const ViewClickZone *z = &view_click_zones[i];
        if (!rect_contains(z->rect, x, y))
            continue;
        switch (z->kind) {
        case CLICK_OBJECT:
        case CLICK_POSSESSIONS:
        case CLICK_ALCOVE:
            if (z->thing == THING_NONE)
                break;
            if (hand == THING_NONE) {
                view_click_take_object(x, y, i);
                return 1;
            }
            if (z->kind == CLICK_ALCOVE) {
                if (view_click_place_object(x, y, v->fx, v->fy, 0, ALCOVE_CELL, CLICK_ALCOVE))
                    return 1;
            } else if (z->kind == CLICK_OBJECT) {
                for (int k = 0; k < HALF_CELLS; k++)
                    if (view_click_place_object(x, y, v->px, v->py, POS_PARTY, k, CLICK_OBJECT))
                        return 1;
            }
            break;
        case CLICK_DOOR_BUTTON:
            if ((int16_t)game.leader != -1)
                view_click_door_button_zone(v, z, hand);
            break;
        case CLICK_WALL_ORNAMENT:
            if (view_hit_test_wall_ornament(z->cell, x, y)) {
                view_click_wall_sensor(z->cell);
                return 1;
            }
            break;
        }
    }
    return 0;
}

/* A click with the leader hand object outside the click zones: it goes onto a cell of the party square, of the
   square in front (unless front_blocked), or of the possessions of a static creature there; else it is thrown. */
static void view_click_with_object(const ViewSquares *v, int x, int y, int front_blocked)
{
    for (int k = 0; k < HALF_CELLS; k++) {
        if (view_click_place_object(x, y, v->px, v->py, POS_PARTY, k, CLICK_OBJECT))
            return;
        if (zone_contains_point(ZONE_DROP_CELL_FIRST + k, x, y) && view_click_drop_object(v->px, v->py, k))
            return;
    }
    if (front_blocked)
        return;
    for (int k = HALF_CELLS; k < CELL_COUNT; k++) {
        if (view_click_place_object(x, y, v->fx, v->fy, POS_FRONT, k, CLICK_OBJECT))
            return;
        if (zone_contains_point(ZONE_DROP_CELL_FIRST + k, x, y) && view_click_drop_object(v->fx, v->fy, k))
            return;
    }
    uint16_t g = creature_group_at(party.cur_map, v->fx, v->fy);
    if (g != THING_NONE && creature_group_info(g)->flags & CREATURE_STATIC) {
        const CreatureInfo *info = creature_group_info(g);
        for (int k = 0; k < (info->possessions & POSSESSIONS_KIND_MASK); k++) {
            if (view_click_place_object(x, y, v->fx, v->fy, POS_FRONT, k + POSSESSION_VIEW_CELL_FIRST,
                                        CLICK_POSSESSIONS))
                return;
            if (zone_contains_point(info->possession_zone + k, x, y) &&
                view_click_drop_object(v->fx, v->fy, k + POSSESSION_VIEW_CELL_FIRST + CELL_POSSESSION_FIRST))
                return;
        }
    }
    view_click_throw_object(x, y);
}

void view_click_in_viewport(int x, int y)
{
    dun_set_current_map(game.party_map);
    if (party.push_mode) {
        party.push_mode = 0;
        ui.push_mode = 0;
        move_push_creature(party.push_arrow);
        ui_draw_movement_arrows();
        return;
    }
    int16_t origin[RECT_SIZE];
    if (zone_get_rect(ZONE_VIEWPORT, origin)) {
        x -= origin[RECT_X];
        y -= origin[RECT_Y];
    }
    int dir = game.party_dir;
    ViewSquares v = {dir, game.party_x, game.party_y, game.party_x + dir_dx[dir], game.party_y + dir_dy[dir]};
    uint8_t front = dun_get_square(v.fx, v.fy);
    uint16_t hand = game.leader_hand;
    if (view_click_zones_hit(&v, x, y, hand))
        return;
    int passable = square_type(front) == SQUARE_FAKE_WALL && front & SQUARE_FLAG_0 && !(front & SQUARE_FAKE_WALL_OPEN);
    if (hand != THING_NONE) {
        view_click_with_object(&v, x, y, square_type(front) == SQUARE_WALL && !passable);
        return;
    }
    if (square_type(front) == SQUARE_WALL &&
        (zone_contains_point(ZONE_FRONT_WALL_LOW, x, y) || zone_contains_point(ZONE_FRONT_WALL_HIGH, x, y))) {
        view_click_front_wall(passable, v.fx, v.fy);
        return;
    }
    uint16_t g = creature_group_at(party.cur_map, v.fx, v.fy);
    if (g != THING_NONE && creature_group_info(g)->flags & CREATURE_STATIC)
        view_click_creature(g, x, y, v.fx, v.fy);
}

/* Shows the pause message and waits for the resume command. */
static void game_pause(void)
{
    char text[TEXT_SIZE];
    main_loop.running = 0;
    play_hide_view();
    gfx_fill_bitmap(&view_viewport, ui_colors[UI_BLACK]);
    gdat_text(gdat_current(), (GdatKey){CAT_INTERFACE, 0, TEXT_PAUSED}, text, sizeof text);
    text_draw_in_zone(ZONE_VIEW_TEXT, ui_colors[UI_CYAN], text);
    play_present_viewport();
    input_push_mode(INPUT_MODE_PAUSE);
    input_flush();
    music_pause();
}

/* The commands of the save, volume, and start menus. */
static void command_execute_session(int command)
{
    switch (command) {
    case CMD_SAVE:
        if (game.champion_count && !ui.candidate) {
            in.disabled--;
            session_save();
            in.disabled++;
        }
        break;
    case CMD_VOLUME:
        dialog_volume();
        if (ui.inventory)
            ui_draw_champion_state(ui.inventory - 1);
        break;
    case CMD_NEW_GAME:
        session.new_game = 1;
        break;
    case CMD_NEW_GAME_B:
        session.new_game = 1;
        session.use_dungenb = 1;
        break;
    case CMD_LOAD_GAME:
        session.new_game = 0;
        break;
    case CMD_MENU_PICTURE:
        menu_show_picture_and_wait();
        break;
    case CMD_QUIT:
        session_quit();
        break;
    }
}

/* The commands of the movement, the leader, the inventory, the spells, and the pause and the sleep; returns 1 when
   command is one of them. */
static int command_execute_party(const Command *c, int idle)
{
    int command = c->command;
    if (command == CMD_CANCEL_CAPTURE) {
        ui.eye = ui.mouth = 0;
    } else if (command == CMD_CANCEL_PUSH) {
        input_cancel_push_mode();
    } else if (command == CMD_TURN_LEFT || command == CMD_TURN_RIGHT) {
        move_party_turn(command);
    } else if (command >= CMD_MOVE_FORWARD && command <= CMD_MOVE_LEFT) {
        move_party(command);
    } else if (command == CMD_ROTATE_LEFT || command == CMD_ROTATE_RIGHT) {
        if (action.champion) {
            Champion *ch = champion(action.champion - 1);
            ch->dir = (uint8_t)dir_turn(ch->dir, command == CMD_ROTATE_LEFT ? -1 : 1);
        }
        area_update(0);
    } else if (command >= CMD_LEADER_FIRST && command <= CMD_LEADER_LAST) {
        champ_set_leader(champion_in_party_cell(command - CMD_LEADER_FIRST));
        ui_draw_all_champion_states(0);
    } else if (command >= CMD_INVENTORY_FIRST && command <= CMD_INVENTORY_LEADER) {
        int champ = command - CMD_INVENTORY_FIRST;
        if (ui.candidate)
            recruit_finish(game.party_x, game.party_y, game.party_dir, game.party_map, CMD_RECRUIT_CANCEL);
        else if (champ == CHAMPION_MAX || champ < game.champion_count)
            ui_toggle_inventory(champ);
    } else if (command == CMD_LEADER_INVENTORY) {
        if ((int16_t)game.leader != -1)
            ui_toggle_inventory(game.leader);
    } else if (command == CMD_VIEWPORT) {
        view_click_in_viewport(c->x, c->y);
    } else if (command == CMD_CAST_SPELL) {
        input_command_stop = magic_cast_entered_runes();
    } else if (command == CMD_REMOVE_RUNE) {
        magic_remove_last_rune();
    } else if (command >= CMD_RUNE_FIRST && command <= CMD_RUNE_LAST) {
        magic_add_rune(command - CMD_RUNE_FIRST);
    } else if (command == CMD_MINION_MAP) {
        minion_map_click_set_destination(command, c->x, c->y);
    } else if (command >= CMD_MAP_OPTION_FIRST && command <= CMD_MAP_OPTION_LAST) {
        minion_toggle_map_option(command - CMD_MAP_OPTION_FIRST);
    } else if (command == CMD_PAUSE) {
        game_pause();
    } else if (command == CMD_RESUME) {
        if (!main_loop.running) {
            main_loop.running = 1;
            play_show_view();
            input_pop_mode();
            input_flush();
        }
    } else if (command == CMD_SLEEP && idle) {
        play_sleep();
    } else if (command == CMD_WAKE && idle) {
        champ_wake_party();
    } else {
        return 0;
    }
    return 1;
}

/* The commands of the champion actions, the hands, the slots, the money box, the drag, the recruiting, and the
   advanced mode; returns 1 when command is one of them. */
static int command_execute_champion(int command, int idle)
{
    if (command == CMD_ACTION_CANCEL) {
        champ_execute_selected_action(-1);
    } else if (command > CMD_ACTION_CANCEL && command <= CMD_ACTION_LAST) {
        input_command_stop = champ_execute_selected_action(command - CMD_ACTION_CANCEL - 1);
    } else if (command >= CMD_HAND_FIRST && command <= CMD_HAND_LAST) {
        action_area_open_hand((command - CMD_HAND_FIRST) / SLOT_HAND_COUNT,
                              (command - CMD_HAND_FIRST) % SLOT_HAND_COUNT);
    } else if (command >= CMD_PARTY_ICON_FIRST && command <= CMD_PARTY_ICON_LAST) {
        int champ = champion_in_party_cell(command - CMD_PARTY_ICON_FIRST);
        if (champ >= 0)
            action_area_open_spells(champ);
    } else if (command >= CMD_COIN_FIRST && command <= CMD_COIN_LAST) {
        money_box_click_coin(command - CMD_COIN_FIRST);
    } else if ((command == CMD_RECRUIT || command == CMD_RECRUIT_CANCEL) && idle) {
        recruit_finish(game.party_x, game.party_y, game.party_dir, game.party_map, command);
    } else if (command == CMD_MOUTH) {
        champ_consume_object(-1, THING_NONE, -1);
    } else if (command == CMD_EYE) {
        ui.eye = (input_mouse_buttons & INPUT_BUTTON_LEFT) != 0;
        if (ui.eye)
            input_flush();
    } else if (command >= CMD_SLOT_FIRST && command <= CMD_SLOT_LAST) {
        inv_click_slot(command - CMD_SLOT_FIRST);
    } else if (command >= CMD_STORE_FIRST && command <= CMD_STORE_LAST) {
        inv_store_leader_hand_object(command - CMD_STORE_FIRST);
    } else if (command >= CMD_DRAG_FIRST && command <= CMD_DRAG_CANCEL) {
        if (command == CMD_DRAG_CANCEL)
            champ_drag_cancel();
        else
            champ_drag_click(command - CMD_DRAG_FIRST);
        input_refresh_mode();
        area_update(0);
    } else if (command == CMD_QUICK_SAVE || command == CMD_QUICK_LOAD || command == CMD_AUTOMAP) {
        if (game.champion_count && !ui.candidate && idle) {
            if (command == CMD_QUICK_SAVE)
                advanced_quick_save();
            else if (command == CMD_QUICK_LOAD)
                advanced_quick_load();
            else
                advanced_open_automap();
        }
    } else if (command == CMD_EDIT_CHAMPION_NAME) {
        if (ui.inventory)
            session_edit_champion_name(champion(ui.inventory - 1));
    } else {
        return 0;
    }
    return 1;
}

/* A command that the original runs later; the port runs none. */
static void command_execute_later(const Command *c)
{
    (void)c;
}

int command_execute(Command *c)
{
    int command = c->command;
    int moving = command >= CMD_MOVE_FORWARD && command <= CMD_MOVE_LEFT;
    if ((moving && (game.party_move_cooldown ||
                    (game.throw_cooldown && game.throw_dir == dir_turn(game.party_dir, command - CMD_MOVE_FORWARD)))) ||
        (party.move_steps && (command == CMD_TURN_LEFT || command == CMD_TURN_RIGHT || command == CMD_VIEWPORT)) ||
        (command == CMD_VIEWPORT && !view_shows_party()) || in.wait) {
        input_command_stop = 1;
        return 0;
    }
    input_command_stop = 0;
    int idle = !ui.eye && !ui.mouth && !party.push_mode && !input_drag_state; /* no eye, mouth, drag or push mode */
    if (!command_execute_party(c, idle) && !command_execute_champion(command, idle)) {
        if ((command == CMD_SAVE || command == CMD_VOLUME || (command >= CMD_NEW_GAME && command <= CMD_MENU_PICTURE) ||
             command == CMD_QUIT) &&
            idle)
            command_execute_session(command);
        else
            command_execute_later(c);
    }
    if (input_match.value >= CMD_TEXT_FIRST && input_match.value <= CMD_TEXT_LAST)
        input_command_stop = 1;
    return 1;
}

int input_mouse_x = -1, input_mouse_y = -1;

#define ARROW_CENTER_X 20 /* half size of the middle part of the viewport, without an arrow */
#define ARROW_CENTER_Y 15

void input_update_viewport_arrow(void)
{
    /* by push arrow: the movement arrow entry and zone, the direction of the push and of the party step */
    static const uint8_t entries[PUSH_ARROW_COUNT] = {0x12, 0x10, 0x14, 0x16, 0x18, 0x0e};
    static const uint16_t zones[PUSH_ARROW_COUNT] = {0x2a, 0x29, 0x2b, 0x2c, 0x2d, 0x28};
    static const uint8_t push_dirs[PUSH_ARROW_COUNT] = {0, 1, 2, 2, 2, 3},
                         step_dirs[PUSH_ARROW_COUNT] = {0, 0, 1, 2, 3, 0};
    if (!view_arrows_enabled || party.push_pending_group != THING_NONE)
        return;
    int16_t r[RECT_SIZE];
    if (!zone_get_rect(ZONE_VIEWPORT, r))
        return;
    int x = input_mouse_x - r[RECT_X], y = input_mouse_y - r[RECT_Y];
    party.push_arrow = PUSH_ARROW_NONE;
    if (x < 0 || y < 0 || x >= VIEWPORT_WIDTH || y >= VIEWPORT_HEIGHT)
        return;
    int dx = x - VIEWPORT_WIDTH / 2, dy = y - VIEWPORT_HEIGHT / 2, arrow;
    if (abs(dx) < ARROW_CENTER_X) {
        if (abs(dy) < ARROW_CENTER_Y)
            return;
        arrow = dy >= 0 ? PUSH_ARROW_DOWN : PUSH_ARROW_UP;
    } else if (dx < 0) {
        arrow = abs(dy) < ARROW_CENTER_Y ? PUSH_ARROW_LEFT : dy >= 0 ? PUSH_ARROW_DOWN_LEFT : PUSH_ARROW_UP;
    } else {
        arrow = abs(dy) < ARROW_CENTER_Y ? PUSH_ARROW_RIGHT : dy >= 0 ? PUSH_ARROW_DOWN_RIGHT : PUSH_ARROW_UP;
    }
    int step = dir_turn(step_dirs[arrow], game.party_dir);
    if (dun_square_blocks_movement(
            dun_get_square((int16_t)(game.party_x + dir_dx[step]), (int16_t)(game.party_y + dir_dy[step]))))
        return;
    if (!creature_can_be_pushed((int16_t)(game.party_x + dir_dx[game.party_dir]),
                                (int16_t)(game.party_y + dir_dy[game.party_dir]),
                                dir_turn(push_dirs[arrow], game.party_dir), NULL))
        return;
    ui_draw_move_arrow(entries[arrow], zones[arrow], 1);
    party.push_arrow = (int16_t)arrow;
}

void input_set_wait(int steps)
{
    in.wait = steps;
}

void command_process_queue(void)
{
    if (in.wait)
        in.wait--;
    in.feedback_waiting = 0;
    do {
        input_poll();
        if (in.queue_head == in.queue_tail || advanced_automap_open)
            return;
        int head = in.queue_head;
        in.queue_head = (head + 1) % COMMAND_QUEUE;
        if (!command_execute(&in.queue[head]))
            in.queue_head = head;
        else if (in.disabled)
            in.disabled--;
    } while (!input_command_stop && !main_loop.quit);
}

/* The command entry of command in the input tree under node, NULL for none. */
static const InputCommand *find_command_entry(const InputNode *node, uint16_t command)
{
    for (int i = 0; !node_child_end(node, i); i++) {
        const InputNode *child = node_child(node, i);
        if (!input_condition(node->cond & ~NODE_HAS_CHILDREN, child->arg))
            continue;
        if (child->cond & NODE_HAS_CHILDREN) {
            const InputCommand *e = find_command_entry(child, command);
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

void input_simulate_command_click(uint16_t command)
{
    const InputCommand *e = find_command_entry(&input_mode_roots[in.mode], command);
    int16_t r[RECT_SIZE];
    if (e && input_get_zone_rect(e->zone, r))
        input_mouse_click(r[RECT_X], r[RECT_Y], e->buttons);
}

bool input_command_rect(uint16_t command, int16_t *rect)
{
    const InputCommand *e = find_command_entry(&input_mode_roots[in.mode], command);
    return e && input_get_zone_rect(e->zone, rect);
}

uint16_t input_read_key(void)
{
    if (!in.key_count)
        return 0;
    uint16_t key = in.keys[0];
    memmove(in.keys, in.keys + 1, --in.key_count * sizeof in.keys[0]);
    return key;
}

char kbd_key_to_ascii(uint16_t key)
{
    /* the characters of the PC scan codes 0x02 to 0x39 */
    static const char chars[] = "1234567890-=\0\0qwertyuiop[]\0\0asdfghjkl;'`\0\\zxcvbnm,./\0\0\0 ";
    static const char shifted[] = "!@#$%^&*()_+\0\0QWERTYUIOP{}\0\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0\0\0 ";
    enum { FIRST = 0x02, LAST = 0x39, SCAN_MASK = 0xff };
    int scan = key & SCAN_MASK;
    if (scan < FIRST || scan > LAST)
        return 0;
    return (key & KEY_SHIFT ? shifted : chars)[scan - FIRST];
}
