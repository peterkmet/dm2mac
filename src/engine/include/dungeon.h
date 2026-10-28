/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_DUNGEON_H
#define DM2_ENGINE_DUNGEON_H
/* Prefixes: dungeon_ dir_ square_ thing_ location_ event_ door_ group_ teleporter_ container_ misc_ potion_ scroll_ weapon_ armour_ moneybox_ textrec_ */

/* The dungeon: the things, their record layout, the squares, and the maps. It reads and links records and squares;
   it does not move things or apply the rules of play (move.h and the game modules do). */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* Thing types: bits 10-13 of a thing id. */
#define THING_DOOR       0
#define THING_TELEPORTER 1
#define THING_TEXT       2
#define THING_SENSOR     3
#define THING_GROUP      4
#define THING_WEAPON     5
#define THING_ARMOUR     6
#define THING_SCROLL     7
#define THING_POTION     8
#define THING_CONTAINER  9
#define THING_MISC       10
#define THING_PROJECTILE 14
#define THING_EXPLOSION  15
#define THING_TYPE_COUNT 16

/* Square byte: square type in bits 5-7; the other bits depend on the type. */
#define SQUARE_TYPE_SHIFT         5
#define square_type(s)            ((s) >> SQUARE_TYPE_SHIFT)
/* Square types. */
#define SQUARE_WALL               0
#define SQUARE_FLOOR              1
#define SQUARE_PIT                2
#define SQUARE_STAIRS             3
#define SQUARE_DOOR               4
#define SQUARE_TELEPORTER         5
#define SQUARE_FAKE_WALL          6
#define SQUARE_UNKNOWN            7
/* The square byte of dungeon_square_edge for a square away from the map. */
#define SQUARE_OUTSIDE            0xe0
/* Square bits 0-3 by square type. */
#define SQUARE_PIT_OPEN           0x08
#define SQUARE_PIT_VARIANT        0x04 /* selects the second pit picture */
#define SQUARE_STAIRS_ALONG_X     0x08 /* the stairs run east to west */
#define SQUARE_STAIRS_UP          0x04
#define SQUARE_DOOR_ALONG_X       0x08 /* the door faces east and west */
#define SQUARE_DOOR_STATE         0x07 /* 0 open, 1-3 half open, 4 closed, 5 broken */
#define SQUARE_TELEPORTER_ON      0x08
#define SQUARE_TELEPORTER_VISIBLE 0x04
#define SQUARE_FAKE_WALL_OPEN     0x04
#define SQUARE_FLAG_3             0x08 /* bit 3 of any square: one floor text ornament shows it */
#define SQUARE_LEVEL_LINK         0x02 /* a square with a link to another level */
#define SQUARE_FLAG_0         0x01 /* a fake wall that blinks with MAP_OPTION_DETAILS; a pit that is not a way down */
#define SQUARE_HAS_THINGS     0x10 /* bit 4 of any square: things lie on it */
/* The values of SQUARE_DOOR_STATE. */
#define DOOR_STATE_OPEN       0
#define DOOR_STATE_ONE_FOURTH 1 /* a door one step down: the party passes under it */
#define DOOR_STATE_HALF       2
#define DOOR_STATE_THREE_FOURTHS 3
#define DOOR_STATE_CLOSED        4
#define DOOR_STATE_BROKEN        5
#define square_flag(s, bit)      (((s) & (bit)) != 0)
/* Bits 0-3 of a wall: the faces that show a random ornament, by the direction the face looks to. */
#define WALL_FACE(dir)           (1 << (DIR_WEST - (dir)))

/* Directions, clockwise from north, and the cells of a square, numbered like the directions. */
#define DIR_NORTH               0
#define DIR_EAST                1
#define DIR_SOUTH               2
#define DIR_WEST                3
#define DIR_COUNT               4
#define CELL_COUNT              4
/* Directions and cells wrap at 4. */
#define DIR_MASK                (DIR_COUNT - 1)
#define CELL_MASK               (CELL_COUNT - 1)
/* Quarter turns of dir_turn. */
#define TURN_RIGHT              1 /* dir_turn: the direction to the right */
#define TURN_BACK               2 /* dir_turn: the opposite direction */
#define TURN_LEFT               3 /* dir_turn: the direction to the left */
/* The direction or cell after a number of quarter turns clockwise. */
#define dir_turn(d, turns)      (((d) + (turns)) & DIR_MASK)
#define dir_opposite(d)         dir_turn(d, TURN_BACK)
/* The direction or cell d seen from a party or creature that looks to facing. */
#define dir_relative(d, facing) dir_turn(d, -(facing))
/* The cell on side dir of a square that is in the same column (seen from dir) as cell. */
static inline int16_t dir_side_cell(int16_t cell, int16_t dir)
{
    return (int16_t)dir_turn(dir, (((cell - dir) + 1) & 2) >> 1);
}

/* The whole of a chance or a ratio in percent. */
#define PERCENT         100
/* No thing, and the end of a thing list. */
#define THING_NONE      0xffff
#define THING_ENDOFLIST 0xfffe
/* The levels of the dungeon and the size of a map. */
#define LEVEL_COUNT     63
#define MAP_SIDE_MAX    32 /* the squares of the longest map side */
/* The end of a level list of dungeon_level_maps. */
#define LEVEL_LIST_END  0xff
/* The dungeon level in the map size word. */
#define MAP_LEVEL_MASK  0x3f

/* A thing id: record index in bits 0-9, thing type in bits 10-13, cell in bits 14-15. */
#define THING_INDEX_MASK 0x03ff
#define THING_TYPE_MASK  0x000f
#define THING_ID_MASK    0x3fff /* a thing id without its cell */
/* The positions of the thing type and the cell in a thing id. */
#define THING_TYPE_SHIFT 10
#define THING_CELL_SHIFT 14
#define thing_type(t)    (((t) >> THING_TYPE_SHIFT) & THING_TYPE_MASK)
#define thing_index(t)   ((t) & THING_INDEX_MASK)
#define thing_cell(t)    ((t) >> THING_CELL_SHIFT)
/* The thing id of thing in cell; the cell wraps at 4. */
static inline uint16_t thing_with_cell(uint16_t thing, int16_t cell)
{
    return (uint16_t)((thing & THING_ID_MASK) | (cell & DIR_MASK) << THING_CELL_SHIFT);
}
/* The thing id of the record index of a thing type. */
static inline uint16_t thing_make(int type, int index)
{
    return (uint16_t)(type << THING_TYPE_SHIFT | index);
}
/* A cell argument: any cell. */
#define CELL_ANY 0xff

/* A location word: x bits 0-4, y bits 5-9, map bits 10-15. */
#define LOCATION_COORD_MASK 0x001f
#define LOCATION_MAP_MASK   0x003f
#define LOCATION_MAP_BITS   0xfc00
/* The positions of y and the map in a location word. */
#define LOCATION_Y_SHIFT    5
#define LOCATION_MAP_SHIFT  10
#define location_x(w)       ((w) & LOCATION_COORD_MASK)
#define location_y(w)       ((w) >> LOCATION_Y_SHIFT & LOCATION_COORD_MASK)
#define location_map(w)     ((w) >> LOCATION_MAP_SHIFT)
#define location_pack(x, y, map)                                                                                       \
    (((x) & LOCATION_COORD_MASK) | ((y) & LOCATION_COORD_MASK) << LOCATION_Y_SHIFT |                                   \
     ((map) & LOCATION_MAP_MASK) << LOCATION_MAP_SHIFT)
/* A location word with new x and y; the map bits stay. */
#define location_set_xy(w, x, y)                                                                                       \
    (((w) & LOCATION_MAP_BITS) | ((x) & LOCATION_COORD_MASK) | ((y) & LOCATION_COORD_MASK) << LOCATION_Y_SHIFT)
/* A square and direction word of an event: x and y as in a location word, the direction in bits 10-11. */
static inline uint16_t event_square_dir(int16_t x, int16_t y, int16_t dir)
{
    return (uint16_t)location_pack(x, y, dir & DIR_MASK);
}
/* The direction in the map bits of a square and direction word. */
static inline uint16_t location_dir(uint16_t word)
{
    return location_map(word) & DIR_MASK;
}

/* A square of the current map. */
typedef struct {
    int16_t x, y;
} Location;

/* A square with its map. */
typedef struct {
    int16_t map, x, y;
} Position;

/* Byte offsets of thing record fields. Every record starts with the link to the next thing. */
#define THING_NEXT        0x00
#define TELEPORTER_TARGET 0x02 /* x bits 0-4, y bits 5-9, rotation bits 10-11, bit 12 absolute, scope bits 13-14 */
#define TELEPORTER_MAP    0x04 /* target map in bits 8-15, bit 0 closed, bits 1-2 fixed */
#define TEXT_DATA         0x02 /* bit 0 visible, bits 1-2 kind, bits 3-10 ornament, bits 11-15 subtype */
#define DOOR_DATA         0x02 /* bit 0 door type, bits 1-4 ornament ordinal, bits 5-13: see the DOOR_ bits */
#define SENSOR_TYPE_DATA  0x02 /* sensor type bits 0-6, data bits 7-15 */
#define SENSOR_FLAGS  0x04 /* bit 0 busy, bit 2 state, bits 3-4 effect, bits 7-10 delay, bits 12-15 ornament ordinal */
#define SENSOR_TARGET 0x06 /* target square: cell bits 4-5, x bits 6-10, y bits 11-15 */
#define GROUP_POSSESSIONS     0x02
#define GROUP_CREATURE        0x04 /* byte: creature type */
#define GROUP_ACTIVE          0x05 /* byte: active group index, 0xff none */
#define GROUP_HEALTH          0x06
#define GROUP_ITEM_CLASS      0x08 /* the item class of a creature that acts, -1 while it is created */
#define GROUP_ANIM_SEQUENCE   0x08 /* static creatures: first frame of the animation sequence */
#define GROUP_FLAGS           0x0a /* the mood bits of the group */
#define GROUP_ANIM_STATE      0x0a /* static creatures: animation state word */
#define GROUP_HOME            0x0c /* home square: x bits 0-4, y bits 5-9, map bits 10-15 */
#define GROUP_DIR             0x0e /* bit 7 keep cells, bits 8-9 direction the creature faces, bit 10 do not discard */
#define ITEM_DATA             0x02 /* weapons, armour, scrolls, potions, and miscellaneous items: see the ITEM_ bits */
#define CONTAINER_CONTENTS    0x02
#define MINION_MAP_GROUP      0x02 /* a minion map: its minion group */
#define CONTAINER_KIND        0x04 /* bit 0 open, bits 1-2 container kind, bits 13-15 subtype */
#define MINION_MAP_LOCATION   0x06 /* a minion map: its target location, THING_NONE for none */
#define MONEYBOX_FLAGS        0x06 /* a money box: bit 10 placed by a creature */
#define PROJECTILE_PAYLOAD    0x02
#define PROJECTILE_ENERGY     0x04 /* byte: the range and power left */
#define PROJECTILE_HIT_CHANCE 0x05 /* byte */
#define PROJECTILE_LOCATION 0x04 /* a minion link projectile or a missile that a creature carries: its location word */
#define PROJECTILE_EVENT    0x06 /* timeline event that moves the projectile */
#define PROJECTILE_CARRIED  0x06 /* a missile that a creature carries: bit 0 the creature waits to enter its location */
#define EXPLOSION_DATA      0x02 /* explosion type bits 0-6, bit 7 centered on the square, power bits 8-15 */

/* Thing records */
/* The record of thing in the record table of its thing type. */
uint8_t *dungeon_thing_record(uint16_t thing);
/* The 16-bit field of a thing record at byte offset field. */
uint16_t thing_word(uint16_t thing, int field);
/* Sets the 16-bit field of a thing record at byte offset field. */
void thing_set_word(uint16_t thing, int field, uint16_t word);

/* Building blocks of the accessors below; other modules call the named accessors. */
static inline uint8_t thing_byte(uint16_t thing, int field)
{
    return dungeon_thing_record(thing)[field];
}
static inline void thing_set_byte(uint16_t thing, int field, uint8_t byte)
{
    dungeon_thing_record(thing)[field] = byte;
}
static inline uint16_t thing_field(uint16_t thing, int field, int shift, uint16_t mask)
{
    return (uint16_t)(thing_word(thing, field) >> shift & mask);
}
static inline void thing_set_field(uint16_t thing, int field, int shift, uint16_t mask, unsigned bits)
{
    uint16_t w = thing_word(thing, field);
    thing_set_word(thing, field, (uint16_t)((w & ~(mask << shift)) | (bits & mask) << shift));
}
static inline bool thing_bit(uint16_t thing, int field, uint16_t bit)
{
    return (thing_word(thing, field) & bit) != 0;
}
static inline void thing_set_bit(uint16_t thing, int field, uint16_t bit, bool on)
{
    uint16_t w = thing_word(thing, field);
    thing_set_word(thing, field, on ? w | bit : w & ~bit);
}
/* The head word of a thing list in a record: possessions, contents, or the link to the next thing. The callers
   read it natively, so the port needs a little-endian host. */
_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "thing_list reads the record words natively");
static inline uint16_t *thing_list(uint16_t holder, int field)
{
    return (uint16_t *)(void *)(dungeon_thing_record(holder) + field);
}
/* The possessions of a group and the contents of a container. */
static inline uint16_t *group_possessions(uint16_t group)
{
    return thing_list(group, GROUP_POSSESSIONS);
}
static inline uint16_t *container_contents(uint16_t container)
{
    return thing_list(container, CONTAINER_CONTENTS);
}

/* The position of the door ornament (bits 1-4) in DOOR_DATA. */
#define DOOR_ORNAMENT_SHIFT     1
/* DOOR_DATA bits, and the mask of the door ornament after the shift. */
#define DOOR_MAP_TYPE           0x0001 /* the door has door type 1 of its map, else door type 0 */
#define DOOR_ORNAMENT_MASK      0x000f
#define DOOR_VERTICAL           0x0020 /* the door moves up and down as a whole: small creatures pass under it */
#define DOOR_BUTTON             0x0040
#define DOOR_BREAKABLE_BY_MAGIC 0x0080
#define DOOR_BREAKABLE          0x0100 /* by force */
#define DOOR_OPENING            0x0200 /* the moving door opens, else it closes */
#define DOOR_MOVING             0x0400
#define DOOR_BUTTON_PRESSED     0x0800
#define DOOR_MARKED             0x1000 /* a creature opened the door; the door clears it when it closes */
#define DOOR_SWITCH_ON          0x2000 /* the state of sensor 0x46: its wall button and door spells operate the door */
static inline bool door_flag(uint16_t door, uint16_t bit)
{
    return thing_bit(door, DOOR_DATA, bit);
}
static inline void door_set_flag(uint16_t door, uint16_t bit, bool on)
{
    thing_set_bit(door, DOOR_DATA, bit, on);
}
/* The door type of the map that the door uses: 0 or 1. */
static inline uint16_t door_map_type(uint16_t door)
{
    return thing_field(door, DOOR_DATA, 0, DOOR_MAP_TYPE);
}
/* The ordinal of the door ornament in the door ornament list of the map, 0 for none. */
static inline uint16_t door_ornament(uint16_t door)
{
    return thing_field(door, DOOR_DATA, DOOR_ORNAMENT_SHIFT, DOOR_ORNAMENT_MASK);
}
/* A button on the door or the wall switch of the door operates it. */
static inline bool door_has_button(uint16_t door)
{
    return door_flag(door, DOOR_BUTTON | DOOR_SWITCH_ON);
}

/* TEXT_DATA fields. */
#define TEXT_VISIBLE               0x0001
#define TEXT_KIND_MASK             0x0003
#define TEXT_VALUE_MASK            0x00ff
#define TEXT_OFFSET_MASK           0x1fff
#define TEXT_SUBTYPE_MASK          0x001f
/* The positions of the TEXT_DATA fields: kind bits 1-2, value or offset from bit 3, subtype bits 11-15. */
#define TEXT_KIND_SHIFT            1
#define TEXT_VALUE_SHIFT           3 /* a text: its offset in the text data; an ornament: its value in bits 3-10 */
#define TEXT_SUBTYPE_SHIFT         11
/* Text kinds. */
#define TEXT_KIND_TEXT             0
#define TEXT_KIND_ORNAMENT         1
#define TEXT_KIND_AI_MARKER        2
/* An AI marker: id bits 3-7, kind bits 8-11, level delta bits 12-15 of TEXT_DATA. */
#define AI_MARKER_ID_SHIFT         3
#define AI_MARKER_KIND_SHIFT       8
#define AI_MARKER_LEVEL_SHIFT      12
/* The masks of the AI marker fields after the shift. */
#define AI_MARKER_ID_MASK          0x1f
#define AI_MARKER_KIND_MASK        0x0f
#define AI_MARKER_LEVEL_MASK       0x0f
/* Subtypes of ornament texts, by what the view does with them (game meanings from docs/reference/symbols/range-6.md). */
#define SUBTYPE_STILL              0x00 /* plain ornament */
#define SUBTYPE_ANIMATED           0x02
#define SUBTYPE_ITEM_HOLDER        0x04 /* holds an item while visible; the view shows frame 1 */
#define SUBTYPE_TOGGLE             0x05 /* animated while visible (wall text toggle) */
#define SUBTYPE_LADDER             0x06 /* a ladder to another level */
#define SUBTYPE_LADDER_HIDDEN      0x07 /* a ladder that works while the text is not visible */
#define SUBTYPE_ITEM_HOLDER_MARKER 0x08 /* an item holder that sets a marker; the dungeon load makes it visible */
#define SUBTYPE_RANDOM_PIT         0x09 /* a floor trigger that opens the pit at random */
#define SUBTYPE_TRAP_TELEPORT      0x0a /* a floor trap that teleports the party */
#define SUBTYPE_HAND_SWITCH        0x0d /* a switch that an empty hand operates */
#define SUBTYPE_PLATE              0x0e /* a text plate on the wall */
#define SUBTYPE_WARP_ONCE          0x0f /* a warp marker that clears all markers; the view shows frame 1 */
#define SUBTYPE_WARP               0x10 /* a warp marker that finds the set marker */
#define SUBTYPE_SQUARE_SWITCH      0x11 /* frame from SQUARE_FLAG_3 */
#define SUBTYPE_SHOP_STAND         0x14 /* clears SIDE_SHOP_DISPLAY */
static inline bool textrec_is_visible(uint16_t text)
{
    return thing_bit(text, TEXT_DATA, TEXT_VISIBLE);
}
static inline void textrec_set_visible(uint16_t text, bool visible)
{
    thing_set_bit(text, TEXT_DATA, TEXT_VISIBLE, visible);
}
static inline uint16_t textrec_kind(uint16_t text)
{
    return thing_field(text, TEXT_DATA, TEXT_KIND_SHIFT, TEXT_KIND_MASK);
}
/* The ornament subtype: what the ornament does. */
static inline uint16_t textrec_subtype(uint16_t text)
{
    return thing_field(text, TEXT_DATA, TEXT_SUBTYPE_SHIFT, TEXT_SUBTYPE_MASK);
}
/* An ornament: its ornament (GDAT index) or the value that its subtype uses. */
static inline uint16_t textrec_value(uint16_t text)
{
    return thing_field(text, TEXT_DATA, TEXT_VALUE_SHIFT, TEXT_VALUE_MASK);
}
/* A text: the offset of its words in the text data. */
static inline uint16_t textrec_offset(uint16_t text)
{
    return thing_field(text, TEXT_DATA, TEXT_VALUE_SHIFT, TEXT_OFFSET_MASK);
}

/* SENSOR_TYPE_DATA fields. */
#define SENSOR_TYPE_MASK         0x007f
#define SENSOR_DATA_MASK         0x01ff
#define SENSOR_DATA_SIGN         0x0100 /* a counter: the count is below zero */
#define SENSOR_DATA_SOUND        0x0100 /* an animated ornament: its ambient sound runs */
#define SENSOR_DATA_FRAME_MASK   0x00ff /* an animated ornament: the frame offset of its animation */
/* The position of the sensor data (bits 7-15) in SENSOR_TYPE_DATA. */
#define SENSOR_DATA_SHIFT        7
/* Sensor types of the dungeon links. */
#define SENSOR_LEVEL_LINK        0x1c /* sensor type: a link to the square of another level */
#define SENSOR_TELEPORTER_LINK   0x27
/* SENSOR_FLAGS bits. */
#define SENSOR_BUSY              0x0001 /* the sensor ignores triggers until its animation or its wait ends */
#define SENSOR_STATE             0x0004 /* on; some sensor types use it as an option */
#define SENSOR_EFFECT_MASK       0x0003
#define SENSOR_INVERTED          0x0020
#define SENSOR_AUDIBLE           0x0040
#define SENSOR_DELAY_MASK        0x000f
#define SENSOR_ORNAMENT_MASK     0x000f
/* The positions of the SENSOR_FLAGS fields: effect bits 3-4, delay bits 7-10, ornament ordinal bits 12-15. */
#define SENSOR_EFFECT_SHIFT      3
#define SENSOR_DELAY_SHIFT       7
#define SENSOR_ORNAMENT_SHIFT    12
/* SENSOR_TARGET fields. */
#define SENSOR_TARGET_CELL_SHIFT 4
#define SENSOR_TARGET_X_SHIFT    6
#define SENSOR_TARGET_Y_SHIFT    11
/* The mask of the target cell after the shift. */
#define SENSOR_TARGET_CELL_MASK  0x03
/* A missile launcher: energy in bits 4-11 and step in bits 12-15 of SENSOR_TARGET. */
#define LAUNCH_ENERGY_SHIFT      4
#define LAUNCH_STEP_SHIFT        12
/* The masks of the launcher energy and step after the shift. */
#define LAUNCH_ENERGY_MASK       0xff
#define LAUNCH_STEP_MASK         0x0f

/* TELEPORTER_TARGET bits. */
#define TELEPORTER_ROTATION_MASK   0x0003
#define TELEPORTER_ABSOLUTE        0x1000 /* the rotation sets the direction, it does not add to it */
#define TELEPORTER_SCOPE_MASK      0x0003
#define TELEPORTER_SCOPE_CREATURES 0x0001 /* teleporter scope bit: the teleporter moves creatures */
#define TELEPORTER_AUDIBLE         0x8000
/* The positions of the rotation (bits 10-11) and the scope (bits 13-14) in TELEPORTER_TARGET. */
#define TELEPORTER_ROTATION_SHIFT  10
#define TELEPORTER_SCOPE_SHIFT     13 /* who uses the teleporter: bit 13 creatures, bit 14 the party and objects */
/* TELEPORTER_MAP bits. */
#define TELEPORTER_CLOSED          0x0001
#define TELEPORTER_FIXED           0x0006 /* both bits: square events do not turn the teleporter on or off */
#define TELEPORTER_MAP_MASK        0x00ff
/* The position of the target map (bits 8-15) in TELEPORTER_MAP. */
#define TELEPORTER_MAP_SHIFT       8
static inline uint16_t teleporter_x(uint16_t teleporter)
{
    return location_x(thing_word(teleporter, TELEPORTER_TARGET));
}
static inline uint16_t teleporter_y(uint16_t teleporter)
{
    return location_y(thing_word(teleporter, TELEPORTER_TARGET));
}
static inline uint16_t teleporter_map(uint16_t teleporter)
{
    return thing_field(teleporter, TELEPORTER_MAP, TELEPORTER_MAP_SHIFT, TELEPORTER_MAP_MASK);
}
/* The quarter turns of the direction of the thing that goes through. */
static inline uint16_t teleporter_rotation(uint16_t teleporter)
{
    return thing_field(teleporter, TELEPORTER_TARGET, TELEPORTER_ROTATION_SHIFT, TELEPORTER_ROTATION_MASK);
}
static inline uint16_t teleporter_scope(uint16_t teleporter)
{
    return thing_field(teleporter, TELEPORTER_TARGET, TELEPORTER_SCOPE_SHIFT, TELEPORTER_SCOPE_MASK);
}
/* A bit of TELEPORTER_TARGET: TELEPORTER_ABSOLUTE or TELEPORTER_AUDIBLE. */
static inline bool teleporter_flag(uint16_t teleporter, uint16_t bit)
{
    return thing_bit(teleporter, TELEPORTER_TARGET, bit);
}
static inline bool teleporter_is_closed(uint16_t teleporter)
{
    return thing_bit(teleporter, TELEPORTER_MAP, TELEPORTER_CLOSED);
}
static inline bool teleporter_is_fixed(uint16_t teleporter)
{
    return thing_field(teleporter, TELEPORTER_MAP, 0, TELEPORTER_FIXED) == TELEPORTER_FIXED;
}

static inline uint8_t group_creature(uint16_t group)
{
    return thing_byte(group, GROUP_CREATURE);
}
static inline void group_set_creature(uint16_t group, uint8_t type)
{
    thing_set_byte(group, GROUP_CREATURE, type);
}
/* The index of the active group of the group, GROUP_NOT_ACTIVE for none. */
static inline uint8_t group_active(uint16_t group)
{
    return thing_byte(group, GROUP_ACTIVE);
}
static inline void group_set_active(uint16_t group, uint8_t index)
{
    thing_set_byte(group, GROUP_ACTIVE, index);
}
/* The animation sequence and state words of a static creature. */
static inline uint16_t *group_anim_words(uint16_t group)
{
    return thing_list(group, GROUP_ANIM_SEQUENCE);
}
/* GROUP_FLAGS: the mood bits. */
#define GROUP_SEEN    0x0002 /* the party saw the creature since the last mood update */
#define MOOD_ANGRY    0x0004
#define MOOD_FEAR     0x0008
#define MOOD_RESTLESS 0x0010
#define MOOD_BORED    0x0020
#define MOOD_QUICK    0x0040 /* the creature hurries */
#define MOOD_WANDERS  0x0080
#define MOOD_STAYS    0x1000
#define MOOD_LAZY     0x2000
#define MOOD_HURRY    0x4000
#define MOOD_FAR      0x8000 /* the creature is not on the party map: it moves slowly */
static inline uint16_t group_mood(uint16_t group)
{
    return thing_word(group, GROUP_FLAGS);
}
static inline void group_set_mood(uint16_t group, uint16_t mood)
{
    thing_set_word(group, GROUP_FLAGS, mood);
}
static inline bool group_mood_flag(uint16_t group, uint16_t bit)
{
    return thing_bit(group, GROUP_FLAGS, bit);
}
static inline void group_set_mood_flag(uint16_t group, uint16_t bit, bool on)
{
    thing_set_bit(group, GROUP_FLAGS, bit, on);
}
/* GROUP_DIR bits. */
#define GROUP_KEEP_CELLS     0x0080 /* the possessions keep their cells */
#define GROUP_DIR_MASK       0x0003
#define GROUP_DO_NOT_DISCARD 0x0400 /* the record stays when the dungeon needs free records */
/* The position of the direction (bits 8-9) in GROUP_DIR. */
#define GROUP_DIR_SHIFT      8
static inline uint16_t group_dir(uint16_t group)
{
    return thing_field(group, GROUP_DIR, GROUP_DIR_SHIFT, GROUP_DIR_MASK);
}
static inline void group_set_dir(uint16_t group, int16_t dir)
{
    thing_set_field(group, GROUP_DIR, GROUP_DIR_SHIFT, GROUP_DIR_MASK, dir);
}
/* A bit of GROUP_DIR: GROUP_KEEP_CELLS or GROUP_DO_NOT_DISCARD. */
static inline bool group_flag(uint16_t group, uint16_t bit)
{
    return thing_bit(group, GROUP_DIR, bit);
}
static inline void group_set_flag(uint16_t group, uint16_t bit, bool on)
{
    thing_set_bit(group, GROUP_DIR, bit, on);
}

/* ITEM_DATA of weapons, armour, and miscellaneous items: type bits 0-6, bit 7 do not discard, charges. */
#define ITEM_TYPE_MASK        0x007f /* the GDAT index */
#define ITEM_DO_NOT_DISCARD   0x0080
#define CHARGES_MASK          0x000f
#define MISC_CHARGES_MASK     0x0003
#define MISC_STACK_MASK       0x003f
/* The positions of the charges and of the stack size in ITEM_DATA. */
#define WEAPON_CHARGES_SHIFT  10
#define ARMOUR_CHARGES_SHIFT  9
#define MISC_CHARGES_SHIFT    14 /* also the champion of a champion token or of bones */
#define MISC_STACK_SHIFT      8  /* a miscellaneous item: the stack size - 1 in bits 8-13 */
/* ITEM_DATA of potions: power bits 0-7, type bits 8-14, bit 15 do not discard. */
#define POTION_POWER_MASK     0x00ff
#define POTION_TYPE_MASK      0x007f
#define POTION_DO_NOT_DISCARD 0x8000
/* The position of the potion type in ITEM_DATA. */
#define POTION_TYPE_SHIFT     8
/* Potion kinds. */
#define POTION_DEXTERITY      0x06
#define POTION_STRENGTH       0x07
#define POTION_WISDOM         0x08
#define POTION_VITALITY       0x09
#define POTION_ANTIDOTE       0x0a
#define POTION_STAMINA        0x0b
#define POTION_DEFENSE        0x0c
#define POTION_MANA           0x0d
#define POTION_HEALTH         0x0e
#define POTION_WATER          0x0f
#define EMPTY_FLASK           0x14 /* the potion kind of an empty flask */
/* ITEM_DATA of scrolls: an ornament text id in bits 10-15, else a text thing index in bits 0-9. */
#define SCROLL_TEXT_SHIFT     10
/* The mask of the ornament text id of a scroll after the shift. */
#define SCROLL_TEXT_MASK      0x3f
/* The ornament text id of a scroll, 0 for a scroll with a text thing. */
static inline uint16_t scroll_ornament_text(uint16_t scroll)
{
    return thing_field(scroll, ITEM_DATA, SCROLL_TEXT_SHIFT, SCROLL_TEXT_MASK);
}
/* The text thing of a scroll without an ornament text. */
static inline uint16_t scroll_text(uint16_t scroll)
{
    return (uint16_t)(THING_TEXT << THING_TYPE_SHIFT | thing_field(scroll, ITEM_DATA, 0, THING_INDEX_MASK));
}
static inline uint16_t weapon_charges(uint16_t weapon)
{
    return thing_field(weapon, ITEM_DATA, WEAPON_CHARGES_SHIFT, CHARGES_MASK);
}
static inline void weapon_set_charges(uint16_t weapon, uint16_t charges)
{
    thing_set_field(weapon, ITEM_DATA, WEAPON_CHARGES_SHIFT, CHARGES_MASK, charges);
}
static inline uint16_t armour_charges(uint16_t armour)
{
    return thing_field(armour, ITEM_DATA, ARMOUR_CHARGES_SHIFT, CHARGES_MASK);
}
static inline void armour_set_charges(uint16_t armour, uint16_t charges)
{
    thing_set_field(armour, ITEM_DATA, ARMOUR_CHARGES_SHIFT, CHARGES_MASK, charges);
}
/* The charges of a miscellaneous item; the champion of a champion token or of bones. */
static inline uint16_t misc_charges(uint16_t misc)
{
    return thing_field(misc, ITEM_DATA, MISC_CHARGES_SHIFT, MISC_CHARGES_MASK);
}
static inline void misc_set_charges(uint16_t misc, uint16_t charges)
{
    thing_set_field(misc, ITEM_DATA, MISC_CHARGES_SHIFT, MISC_CHARGES_MASK, charges);
}
/* The stack size - 1 of a miscellaneous item (coins). */
static inline uint16_t misc_stack(uint16_t misc)
{
    return thing_field(misc, ITEM_DATA, MISC_STACK_SHIFT, MISC_STACK_MASK);
}
static inline void misc_set_stack(uint16_t misc, uint16_t count)
{
    thing_set_field(misc, ITEM_DATA, MISC_STACK_SHIFT, MISC_STACK_MASK, count);
}
static inline uint16_t potion_power(uint16_t potion)
{
    return thing_field(potion, ITEM_DATA, 0, POTION_POWER_MASK);
}
static inline void potion_set_power(uint16_t potion, uint16_t power)
{
    thing_set_field(potion, ITEM_DATA, 0, POTION_POWER_MASK, power);
}
static inline uint16_t potion_type(uint16_t potion)
{
    return thing_field(potion, ITEM_DATA, POTION_TYPE_SHIFT, POTION_TYPE_MASK);
}
static inline void potion_set_type(uint16_t potion, uint16_t type)
{
    thing_set_field(potion, ITEM_DATA, POTION_TYPE_SHIFT, POTION_TYPE_MASK, type);
}
/* CONTAINER_KIND fields. */
#define CONTAINER_OPEN       0x0001
#define CONTAINER_KIND_MASK  0x0003
#define CONTAINER_TYPE_MASK  0x0007
/* The positions of the container kind (bits 1-2) and type (bits 13-15) in CONTAINER_KIND. */
#define CONTAINER_KIND_SHIFT 1
#define CONTAINER_TYPE_SHIFT 13
/* Container kinds: a chest or a money box, a minion map. */
#define CONTAINER_CHEST      0
#define CONTAINER_MINION_MAP 1
/* a creature placed the money box: creatures take it as empty until the party moves it */
#define MONEYBOX_PLACED      0x0400
static inline bool container_is_open(uint16_t container)
{
    return thing_bit(container, CONTAINER_KIND, CONTAINER_OPEN);
}
static inline void container_set_open(uint16_t container, bool open)
{
    thing_set_bit(container, CONTAINER_KIND, CONTAINER_OPEN, open);
}
static inline uint16_t container_kind(uint16_t container)
{
    return thing_field(container, CONTAINER_KIND, CONTAINER_KIND_SHIFT, CONTAINER_KIND_MASK);
}
static inline void container_set_kind(uint16_t container, uint16_t kind)
{
    thing_set_field(container, CONTAINER_KIND, CONTAINER_KIND_SHIFT, CONTAINER_KIND_MASK, kind);
}
/* The type of the container within its kind. */
static inline uint16_t container_type(uint16_t container)
{
    return thing_field(container, CONTAINER_KIND, CONTAINER_TYPE_SHIFT, CONTAINER_TYPE_MASK);
}
static inline void container_set_type(uint16_t container, uint16_t type)
{
    thing_set_field(container, CONTAINER_KIND, CONTAINER_TYPE_SHIFT, CONTAINER_TYPE_MASK, type);
}
/* The GDAT index of a container: its kind in bits 3-4, its type in bits 0-2. */
#define CONTAINER_INDEX_KIND_SHIFT 3
static inline uint16_t container_gdat_index(uint16_t container)
{
    return (uint16_t)(container_type(container) | container_kind(container) << CONTAINER_INDEX_KIND_SHIFT);
}
static inline void container_set_gdat_index(uint16_t container, uint16_t index)
{
    container_set_kind(container, index >> CONTAINER_INDEX_KIND_SHIFT);
    container_set_type(container, index);
}
static inline bool moneybox_is_placed(uint16_t box)
{
    return thing_bit(box, MONEYBOX_FLAGS, MONEYBOX_PLACED);
}
static inline void moneybox_set_placed(uint16_t box, bool placed)
{
    thing_set_bit(box, MONEYBOX_FLAGS, MONEYBOX_PLACED, placed);
}

/* The PROJECTILE_CARRIED bit. */
#define PROJECTILE_HOLDER_WAITS 0x0001
/* EXPLOSION_DATA fields. */
#define EXPLOSION_TYPE_MASK     0x007f
#define EXPLOSION_CENTERED      0x0080 /* the explosion is in the middle of its square, not on a cell */
#define EXPLOSION_POWER_MASK    0x00ff
/* The position of the explosion power (bits 8-15) in EXPLOSION_DATA. */
#define EXPLOSION_POWER_SHIFT   8

/* Setup */
/* Loads a new game from DUNGEON.DAT; false on a read error or a compressed file. */
bool dungeon_load(const char *path);
/* Reads a dungeon at the file position: DUNGEON.DAT (new_game, adds free records) or the image in a save file. */
bool dungeon_read(FILE *f, bool new_game);
/* Writes the dungeon image of a save file: the header with the counts of the loaded game, then the sections. */
bool dungeon_write(FILE *f);
/* Frees the loaded dungeon. */
void dungeon_free(void);
/* The pass of a new game after the load of DUNGEON.DAT: numbered teleporter links, shared lists of linked
   teleporters, level link marks, the start states of sensors and texts, creature health and homes. */
void dungeon_link_things_after_load(void);
/* Reads the level digits from the text on the party start square (the first load of a run). */
void dungeon_read_level_table(void);

/* The dungeon header, the maps and the levels */
/* The location word of the party start square. */
uint16_t dungeon_party_start(void);
/* The first word of the dungeon header; it seeds the random wall ornaments. */
uint16_t dungeon_ornament_seed(void);
/* The number of maps of the dungeon. */
int dungeon_map_count(void);
/* The squares of map along x. */
int dungeon_map_width(int16_t map);
/* The squares of map along y. */
int dungeon_map_height(int16_t map);
/* The dungeon level of map. */
int dungeon_map_level(int16_t map);
/* The x position of the map on its level. */
int dungeon_map_offset_x(int16_t map);
/* The y position of the map on its level. */
int dungeon_map_offset_y(int16_t map);
/* Map flags bits: the map has door type 0, door type 1. */
#define MAP_HAS_DOOR_TYPE_0      0x0080
#define MAP_HAS_DOOR_TYPE_1      0x0100
/* The positions of the fields of the map descriptor words. */
#define MAP_WALLSET_SHIFT        4
#define MAP_DOOR_TYPE_SHIFT      8 /* door type 0; door type 1 follows 4 bits higher */
#define MAP_DOOR_TYPE_1_SHIFT    12
#define MAP_RANDOM_WALL_SHIFT    4 /* map ornament counts: random wall ornaments in bits 4-7 */
#define MAP_CREATURE_TYPES_SHIFT 4 /* map object counts bits 4-7: the number of creature types of the map */
#define MAP_DIFFICULTY_SHIFT 12    /* map object counts: difficulty bits 12-15, also the experience factor of the map */
/* The mask of a 4-bit field of a map descriptor word after the shift. */
#define MAP_NIBBLE_MASK      0xf
/* The door type of a door slot without a door type. */
#define DOOR_TYPE_NONE       0xff
/* The map flags word: bit 7 door type 0 present, bit 8 door type 1 present; exported for the tests. */
uint16_t dungeon_map_flags(int16_t map);
/* The map object counts word: door ornaments bits 0-3, creature types bits 4-7; exported for the tests. */
uint16_t dungeon_map_object_counts(int16_t map);
/* The map graphics word: graphics set bits 4-7, door types bits 8-11 and 12-15; exported for the tests. */
uint16_t dungeon_map_graphics(int16_t map);
/* The wall set (graphics set) of map. */
int dungeon_map_wallset(int16_t map);
/* The door type of door slot 0 or 1 of map, DOOR_TYPE_NONE when the map has none. */
uint8_t dungeon_map_door_type(int16_t map, int16_t slot);
/* The door type of door on map: the door type of its slot, DOOR_TYPE_NONE as in the original (the callers pass it to
   GDAT as the entry index). */
uint8_t dungeon_door_type(int16_t map, uint16_t door);
/* The number of random wall ornaments of map. */
int dungeon_map_random_wall_count(int16_t map);
/* The number of creature types of map. */
int dungeon_map_creature_type_count(int16_t map);
/* The difficulty of map, also the experience factor of the map. */
int dungeon_map_difficulty(int16_t map);
/* The creature types of map (count in dungeon_map_object_counts bits 4-7). */
const uint8_t *dungeon_map_creature_types(int16_t map);
/* The ornament lists of map: GDAT indices of its wall, floor, and door ornaments. */
const uint8_t *dungeon_map_wall_ornaments(int16_t map);
/* The GDAT indices of the floor ornaments of map. */
const uint8_t *dungeon_map_floor_ornaments(int16_t map);
/* The GDAT indices of the door ornaments of map. */
const uint8_t *dungeon_map_door_ornaments(int16_t map);
/* The maps of a level, ended by LEVEL_LIST_END; exported for the tests. */
const uint8_t *dungeon_level_maps(int level);
/* The level digit of level: the map icon set of the squares outside the maps of the level. */
int dungeon_level_digit(int level);
/* The number of words of the text data. */
int dungeon_text_word_count(void);
/* The text data: the words of the dungeon texts. */
const uint16_t *dungeon_text_data(void);

/* Squares */
/* The square byte at (x, y); x and y must be inside the map. */
uint8_t dungeon_square(int16_t map, int16_t x, int16_t y);
/* The square byte at (x, y) to change; x and y must be inside the map. */
uint8_t *dungeon_square_byte(int16_t map, int16_t x, int16_t y);
/* The square byte of map at (x, y). Away from the map it gives the edge walls: a wall (with the bit of the
   side that faces the map) next to an open square at the map edge, else SQUARE_OUTSIDE. */
uint8_t dungeon_square_edge(int16_t map, int16_t x, int16_t y);
/* The first thing on the square (x, y), THING_ENDOFLIST when the square has no things. */
uint16_t dungeon_square_first_thing(int16_t map, int16_t x, int16_t y);
/* The first thing on the square after its doors, teleporters, texts, and sensors. */
uint16_t dungeon_square_first_object(int16_t map, int16_t x, int16_t y);
/* Entries of the square first-thing table, including the free entries of a new game; exported for the tests. */
int dungeon_square_thing_count(void);
/* The square first-thing table; exported for the tests. */
const uint16_t *dungeon_square_things(void);
/* The map of the level at level_delta from the level of from.map that holds the dungeon position of from, and its
   square there; map -1 (with the square of from) when no such map has an open square there. */
Position dungeon_location_on_map(Position from, int level_delta);
/* The same search from list position *next (0: the first map of the level); sets *next to the position after the
   map that it found, 0 when it found none. */
Position dungeon_location_on_map_from(Position from, int level_delta, int *next);
/* Level deltas: one level up, one level down. */
#define LEVEL_UP           (-1)
#define LEVEL_DOWN         1
/* Level transfer flags. */
#define TRANSFER_PIT       0x008
#define TRANSFER_FALL_INTO 0x010
#define TRANSFER_STAIRS    0x100
/* The square one level up (level_delta -1) or down (1) that the square from leads to by its stairs, its open pit
   (TRANSFER_PIT), or a level link, with its map; map -1 none. Leaves from.map current. */
Position dungeon_find_level_transfer(Position from, int level_delta, unsigned flags);
/* A pair of teleporters with link sensors: a seamless passage to another map. */
typedef struct {
    int16_t side, target_side; /* side of the passage on the source and on the target square, 0 to 3 */
    int16_t x, y, map;         /* the target square */
} TeleporterLink;
/* Fills link for a teleporter square whose target is a teleporter, both with link sensors; false when not. */
bool dungeon_teleporter_link(int16_t map, int16_t x, int16_t y, TeleporterLink *link);
/* The target map of a teleporter square whose target square is a teleporter too, both with a link sensor; else -1. */
int dungeon_teleporter_link_map(int16_t map, int16_t x, int16_t y);

/* Things */
/* The number of records of thing type type, the free records included. */
int dungeon_thing_count(int type);
/* The record size in bytes of thing type type. */
int dungeon_thing_size(int type);
/* True when thing has a record in the loaded dungeon. */
bool dungeon_thing_valid(uint16_t thing);
/* The next thing in the thing list of thing. */
uint16_t dungeon_next_thing(uint16_t thing);
/* The largest thing record (a creature group). */
#define DUNGEON_RECORD_MAX 16
/* Copies the record of thing to or from buffer (DUNGEON_RECORD_MAX bytes). */
void dungeon_save_record(uint16_t thing, uint8_t *buffer);
/* Copies buffer (DUNGEON_RECORD_MAX bytes) back into the record of thing. */
void dungeon_restore_record(uint16_t thing, const uint8_t *buffer);

/* Thing lists */
/* Removes thing from the thing list whose first-thing word is at list. */
void dungeon_unlink_thing_from_list(uint16_t thing, uint16_t *list);
/* Appends thing at the end of a thing list. */
void dungeon_append_thing(uint16_t thing, uint16_t *list);
/* Appends thing at the end of the square's thing list; false when the first-thing table is full. */
bool dungeon_link_thing_to_square(uint16_t thing, int16_t map, int16_t x, int16_t y);
/* Removes thing from the square's thing list. */
void dungeon_unlink_thing_from_square(uint16_t thing, int16_t map, int16_t x, int16_t y);
/* Takes a free record of type, cleared, without cell bits. When the table is full, it reuses a thing from
   dun_get_discarded_thing; THING_NONE when there is none. */
uint16_t dungeon_unused_thing(int type);
/* As dungeon_unused_thing; a miscellaneous item may also take the records that the dungeon keeps for the game. */
uint16_t dungeon_unused_thing_any_record(int type);

#endif /* DM2_ENGINE_DUNGEON_H */
