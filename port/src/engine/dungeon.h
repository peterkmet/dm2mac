#ifndef DUNGEON_H
#define DUNGEON_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* Prefixes: dungeon_ thing_ set_thing_ location_ event_ door_ group_ teleporter_ container_ misc_ potion_ scroll_ weapon_ armour_ moneybox_ text_is_ text_kind text_offset text_set_visible text_subtype text_value */

enum thing_type {
    THING_DOOR, THING_TELEPORTER, THING_TEXT, THING_SENSOR, THING_GROUP, THING_WEAPON, THING_ARMOUR,
    THING_SCROLL, THING_POTION, THING_CONTAINER, THING_MISC, THING_PROJECTILE = 14, THING_EXPLOSION,
    THING_TYPE_COUNT
};

/* Square byte: square type in bits 5-7; the other bits depend on the type. */
#define SQUARE_TYPE_SHIFT 5
#define square_type(s) ((s) >> SQUARE_TYPE_SHIFT)
enum square_type {
    SQUARE_WALL, SQUARE_FLOOR, SQUARE_PIT, SQUARE_STAIRS, SQUARE_DOOR, SQUARE_TELEPORTER, SQUARE_FAKE_WALL,
    SQUARE_UNKNOWN
};
#define SQUARE_OUTSIDE 0xe0     /* dungeon_square_edge: a square away from the map */
/* Square bits 0-3 by square type. */
#define SQUARE_PIT_OPEN 0x08
#define SQUARE_PIT_VARIANT 0x04         /* selects the second pit picture */
#define SQUARE_STAIRS_ALONG_X 0x08      /* the stairs run east to west */
#define SQUARE_STAIRS_UP 0x04
#define SQUARE_DOOR_ALONG_X 0x08        /* the door faces east and west */
#define SQUARE_DOOR_STATE 0x07          /* 0 open, 1-3 half open, 4 closed, 5 broken */
#define SQUARE_TELEPORTER_ON 0x08
#define SQUARE_TELEPORTER_VISIBLE 0x04
#define SQUARE_FAKE_WALL_OPEN 0x04
#define SQUARE_FLAG_3 0x08              /* bit 3 of any square: one floor text ornament shows it */
#define SQUARE_LEVEL_LINK 0x02          /* a square with a link to another level */
#define SQUARE_FLAG_0 0x01              /* a fake wall that blinks with MAP_OPTION_DETAILS; a pit that is not a way down */
#define SQUARE_HAS_THINGS 0x10          /* bit 4 of any square: things lie on it */
/* The values of SQUARE_DOOR_STATE. */
enum door_state {
    DOOR_STATE_OPEN,
    DOOR_STATE_ONE_FOURTH,      /* a door one step down: the party passes under it */
    DOOR_STATE_HALF, DOOR_STATE_THREE_FOURTHS, DOOR_STATE_CLOSED, DOOR_STATE_BROKEN
};
#define square_flag(s, bit) (((s) & (bit)) != 0)
/* Bits 0-3 of a wall: the faces that show a random ornament, by the direction the face looks to. */
#define WALL_FACE(dir) (8 >> (dir))

enum direction { DIR_NORTH, DIR_EAST, DIR_SOUTH, DIR_WEST, DIR_COUNT };
#define DIR_MASK (DIR_COUNT - 1)        /* directions and cells wrap at 4 */
#define CELL_COUNT 4                    /* the cells of a square, numbered like the directions */
#define TURN_RIGHT 1                    /* dir_turn: the direction to the right */
#define TURN_LEFT 3                     /* dir_turn: the direction to the left */
/* The direction or cell after a number of quarter turns clockwise. */
#define dir_turn(d, turns) (((d) + (turns)) & DIR_MASK)
#define dir_opposite(d) dir_turn(d, 2)
/* The direction or cell d seen from a party or creature that looks to facing. */
#define dir_relative(d, facing) dir_turn(d, -(facing))

#define PERCENT 100                     /* the whole of a chance or a ratio in percent */
#define THING_NONE 0xffff
#define THING_ENDOFLIST 0xfffe
#define LEVEL_COUNT 63
#define MAP_SIDE_MAX 32                 /* the squares of the longest map side */
#define LEVEL_LIST_END 0xff
#define MAP_LEVEL_MASK 0x3f             /* the dungeon level in the map size word */

/* A thing id: record index in bits 0-9, thing type in bits 10-13, cell in bits 14-15. */
#define THING_TYPE_SHIFT 10
#define THING_TYPE_MASK 0xf
#define thing_type(t) (((t) >> THING_TYPE_SHIFT) & THING_TYPE_MASK)
#define THING_INDEX_MASK 0x3ff
#define thing_index(t) ((t) & THING_INDEX_MASK)
#define THING_CELL_SHIFT 14
#define THING_ID_MASK 0x3fff            /* a thing id without its cell */
#define thing_cell(t) ((t) >> THING_CELL_SHIFT)
/* The thing id of thing in cell; the cell wraps at 4. */
static inline uint16_t thing_with_cell(uint16_t thing, int cell)
{
    return (uint16_t)((thing & THING_ID_MASK) | (cell & DIR_MASK) << THING_CELL_SHIFT);
}
/* The thing id of the record index of a thing type. */
static inline uint16_t thing_make(int type, int index) { return (uint16_t)(type << THING_TYPE_SHIFT | index); }
#define CELL_ANY 0xff                   /* a cell argument: any cell */

/* A location word: x bits 0-4, y bits 5-9, map bits 10-15. */
#define LOCATION_COORD_MASK 0x1f
#define LOCATION_Y_SHIFT 5
#define LOCATION_MAP_SHIFT 10
#define LOCATION_MAP_MASK 0x3f
#define LOCATION_MAP_BITS 0xfc00
#define location_x(w) ((w) & LOCATION_COORD_MASK)
#define location_y(w) ((w) >> LOCATION_Y_SHIFT & LOCATION_COORD_MASK)
#define location_map(w) ((w) >> LOCATION_MAP_SHIFT)
#define location_pack(x, y, map) \
    (((x) & LOCATION_COORD_MASK) | ((y) & LOCATION_COORD_MASK) << LOCATION_Y_SHIFT | ((map) & LOCATION_MAP_MASK) << LOCATION_MAP_SHIFT)
/* A location word with new x and y; the map bits stay. */
#define location_set_xy(w, x, y) (((w) & LOCATION_MAP_BITS) | ((x) & LOCATION_COORD_MASK) | ((y) & LOCATION_COORD_MASK) << LOCATION_Y_SHIFT)
/* A square and direction word of an event: x and y as in a location word, the direction in bits 10-11. */
static inline uint16_t event_square_dir(int x, int y, int dir) { return (uint16_t)location_pack(x, y, dir & DIR_MASK); }
/* The direction in the map bits of a square and direction word. */
static inline uint16_t location_dir(uint16_t word) { return location_map(word) & DIR_MASK; }

/* Byte offsets of thing record fields. Every record starts with the link to the next thing. */
enum thing_field {
    THING_NEXT = 0,
    TELEPORTER_TARGET = 2,      /* x bits 0-4, y bits 5-9, rotation bits 10-11, bit 12 absolute, scope bits 13-14 */
    TELEPORTER_MAP = 4,         /* target map in bits 8-15, bit 0 closed, bits 1-2 fixed */
    TEXT_DATA = 2,              /* bit 0 visible, bits 1-2 kind, bits 3-10 ornament, bits 11-15 subtype */
    DOOR_DATA = 2,              /* bit 0 door type, bits 1-4 ornament ordinal, bits 5-13: see the DOOR_ bits */
    SENSOR_TYPE_DATA = 2,       /* sensor type bits 0-6, data bits 7-15 */
    SENSOR_FLAGS = 4,           /* bit 0 busy, bit 2 state, bits 3-4 effect, bits 7-10 delay, bits 12-15 ornament ordinal */
    SENSOR_TARGET = 6,          /* target square: cell bits 4-5, x bits 6-10, y bits 11-15 */
    GROUP_POSSESSIONS = 2,
    GROUP_CREATURE = 4,         /* byte: creature type */
    GROUP_ACTIVE = 5,           /* byte: active group index, 0xff none */
    GROUP_HEALTH = 6,
    GROUP_ITEM_CLASS = 8,       /* the item class of a creature that acts, -1 while it is created */
    GROUP_ANIM_SEQUENCE = 8,    /* static creatures: first frame of the animation sequence */
    GROUP_FLAGS = 0x0a,         /* the mood bits of the group */
    GROUP_ANIM_STATE = 0x0a,    /* static creatures: animation state word */
    GROUP_HOME = 0x0c,          /* home square: x bits 0-4, y bits 5-9, map bits 10-15 */
    GROUP_DIR = 0x0e,           /* bit 7 keep cells, bits 8-9 direction the creature faces, bit 10 do not discard */
    ITEM_DATA = 2,              /* weapons, armour, scrolls, potions, and miscellaneous items: see the ITEM_ bits */
    CONTAINER_CONTENTS = 2,
    MINION_MAP_GROUP = 2,       /* a minion map: its minion group */
    CONTAINER_KIND = 4,         /* bit 0 open, bits 1-2 container kind, bits 13-15 subtype */
    MINION_MAP_LOCATION = 6,    /* a minion map: its target location, THING_NONE for none */
    MONEYBOX_FLAGS = 6,         /* a money box: bit 10 placed by a creature */
    PROJECTILE_PAYLOAD = 2,
    PROJECTILE_ENERGY = 4,      /* byte: the range and power left */
    PROJECTILE_HIT_CHANCE = 5,  /* byte */
    PROJECTILE_LOCATION = 4,    /* a minion link projectile or a missile that a creature carries: its location word */
    PROJECTILE_EVENT = 6,       /* timeline event that moves the projectile */
    PROJECTILE_CARRIED = 6,     /* a missile that a creature carries: bit 0 the creature waits to enter its location */
    EXPLOSION_DATA = 2,         /* explosion type bits 0-6, bit 7 centered on the square, power bits 8-15 */
};

uint8_t *dungeon_thing_record(uint16_t thing);
/* The 16-bit field of a thing record at byte offset field. */
uint16_t thing_word(uint16_t thing, int field);
void set_thing_word(uint16_t thing, int field, uint16_t value);

/* Building blocks of the accessors below; other modules call the named accessors. */
static inline uint8_t thing_byte(uint16_t thing, int field) { return dungeon_thing_record(thing)[field]; }
static inline void set_thing_byte(uint16_t thing, int field, uint8_t value) { dungeon_thing_record(thing)[field] = value; }
static inline uint16_t thing_field(uint16_t thing, int field, int shift, uint16_t mask)
{
    return (uint16_t)(thing_word(thing, field) >> shift & mask);
}
static inline void set_thing_field(uint16_t thing, int field, int shift, uint16_t mask, unsigned value)
{
    uint16_t w = thing_word(thing, field);
    set_thing_word(thing, field, (uint16_t)((w & ~(mask << shift)) | (value & mask) << shift));
}
static inline bool thing_bit(uint16_t thing, int field, uint16_t bit) { return (thing_word(thing, field) & bit) != 0; }
static inline void set_thing_bit(uint16_t thing, int field, uint16_t bit, bool on)
{
    uint16_t w = thing_word(thing, field);
    set_thing_word(thing, field, on ? w | bit : w & ~bit);
}
/* The head word of a thing list in a record: possessions, contents, or the link to the next thing. */
static inline uint16_t *thing_list(uint16_t holder, int field)
{
    return (uint16_t *)(void *)(dungeon_thing_record(holder) + field);
}
/* The possessions of a group and the contents of a container. */
static inline uint16_t *group_possessions(uint16_t group) { return thing_list(group, GROUP_POSSESSIONS); }
static inline uint16_t *container_contents(uint16_t container) { return thing_list(container, CONTAINER_CONTENTS); }

/* DOOR_DATA bits. */
#define DOOR_MAP_TYPE 0x0001            /* the door has door type 1 of its map, else door type 0 */
#define DOOR_ORNAMENT_SHIFT 1
#define DOOR_ORNAMENT_MASK 0xf
#define DOOR_VERTICAL 0x0020            /* the door moves up and down as a whole: small creatures pass under it */
#define DOOR_BUTTON 0x0040
#define DOOR_BREAKABLE_BY_MAGIC 0x0080
#define DOOR_BREAKABLE 0x0100           /* by force */
#define DOOR_OPENING 0x0200             /* the moving door opens, else it closes */
#define DOOR_MOVING 0x0400
#define DOOR_BUTTON_PRESSED 0x0800
#define DOOR_MARKED 0x1000              /* a creature opened the door; the door clears it when it closes */
#define DOOR_SWITCH_ON 0x2000           /* the state of sensor 0x46: its wall button and door spells operate the door */
static inline bool door_flag(uint16_t door, uint16_t bit) { return thing_bit(door, DOOR_DATA, bit); }
static inline void door_set_flag(uint16_t door, uint16_t bit, bool on) { set_thing_bit(door, DOOR_DATA, bit, on); }
/* The door type of the map that the door uses: 0 or 1. */
static inline uint16_t door_map_type(uint16_t door) { return thing_field(door, DOOR_DATA, 0, DOOR_MAP_TYPE); }
/* The ordinal of the door ornament in the door ornament list of the map, 0 for none. */
static inline uint16_t door_ornament(uint16_t door)
{
    return thing_field(door, DOOR_DATA, DOOR_ORNAMENT_SHIFT, DOOR_ORNAMENT_MASK);
}
/* A button on the door or the wall switch of the door operates it. */
static inline bool door_has_button(uint16_t door) { return door_flag(door, DOOR_BUTTON | DOOR_SWITCH_ON); }

/* TEXT_DATA fields. */
#define TEXT_VISIBLE 0x0001
#define TEXT_KIND_SHIFT 1
#define TEXT_KIND_MASK 3
enum text_kind { TEXT_KIND_TEXT, TEXT_KIND_ORNAMENT, TEXT_KIND_AI_MARKER };
#define TEXT_VALUE_SHIFT 3              /* a text: its offset in the text data; an ornament: its value in bits 3-10 */
#define TEXT_VALUE_MASK 0xff
#define TEXT_OFFSET_MASK 0x1fff
#define TEXT_SUBTYPE_SHIFT 11
#define TEXT_SUBTYPE_MASK 0x1f
#define AI_MARKER_ID_SHIFT 3            /* an AI marker: id bits 3-7, kind bits 8-11, level delta bits 12-15 */
#define AI_MARKER_ID_MASK 0x1f
#define AI_MARKER_KIND_SHIFT 8
#define AI_MARKER_KIND_MASK 0xf
#define AI_MARKER_LEVEL_SHIFT 12
#define AI_MARKER_LEVEL_MASK 0xf
/* Subtypes of ornament texts, by what the view does with them (game meanings from docs/skull-notes/range-6.md). */
enum text_subtype {
    SUBTYPE_STILL = 0,              /* plain ornament */
    SUBTYPE_ANIMATED = 2,
    SUBTYPE_SWITCH = 4,             /* frame 1 while visible */
    SUBTYPE_TOGGLE = 5,             /* animated while visible (wall text toggle) */
    SUBTYPE_LADDER = 6,             /* a ladder to another level */
    SUBTYPE_LADDER_HIDDEN = 7,      /* a ladder that works while the text is not visible */
    SUBTYPE_SWITCH_3 = 8,
    SUBTYPE_TRAP_TELEPORT = 10,
    SUBTYPE_HAND_SWITCH = 13,       /* a switch that an empty hand operates */
    SUBTYPE_PLATE = 14,             /* a text plate on the wall */
    SUBTYPE_WARP_ONCE = 15,         /* a warp marker that clears all markers; the view shows frame 1 */
    SUBTYPE_WARP = 16,              /* a warp marker that finds the set marker */
    SUBTYPE_SQUARE_SWITCH = 17,     /* frame from SQUARE_FLAG_3 */
    SUBTYPE_SHOP_STAND = 20,        /* clears SIDE_SHOP_FLAG */
};
static inline bool text_is_visible(uint16_t text) { return thing_bit(text, TEXT_DATA, TEXT_VISIBLE); }
static inline void text_set_visible(uint16_t text, bool visible) { set_thing_bit(text, TEXT_DATA, TEXT_VISIBLE, visible); }
static inline uint16_t text_kind(uint16_t text) { return thing_field(text, TEXT_DATA, TEXT_KIND_SHIFT, TEXT_KIND_MASK); }
/* The ornament subtype: what the ornament does. */
static inline uint16_t text_subtype(uint16_t text) { return thing_field(text, TEXT_DATA, TEXT_SUBTYPE_SHIFT, TEXT_SUBTYPE_MASK); }
/* An ornament: its ornament (GDAT index) or the value that its subtype uses. */
static inline uint16_t text_value(uint16_t text) { return thing_field(text, TEXT_DATA, TEXT_VALUE_SHIFT, TEXT_VALUE_MASK); }
/* A text: the offset of its words in the text data. */
static inline uint16_t text_offset(uint16_t text) { return thing_field(text, TEXT_DATA, TEXT_VALUE_SHIFT, TEXT_OFFSET_MASK); }

/* SENSOR_TYPE_DATA fields. */
#define SENSOR_TYPE_MASK 0x7f
#define SENSOR_LEVEL_LINK 0x1c          /* sensor type: a link to the square of another level */
#define SENSOR_DATA_SHIFT 7
#define SENSOR_DATA_MASK 0x1ff
#define SENSOR_DATA_SIGN 0x100          /* a counter: the count is below zero */
#define SENSOR_DATA_SOUND 0x100         /* an animated ornament: its ambient sound runs */
#define SENSOR_DATA_FRAME_MASK 0xff     /* an animated ornament: the frame offset of its animation */
#define SENSOR_TELEPORTER_LINK 0x27
/* SENSOR_FLAGS bits. */
#define SENSOR_BUSY 0x0001              /* the sensor ignores triggers until its animation or its wait ends */
#define SENSOR_STATE 0x0004             /* on; some sensor types use it as an option */
#define SENSOR_EFFECT_SHIFT 3
#define SENSOR_EFFECT_MASK 3
#define SENSOR_INVERTED 0x0020
#define SENSOR_AUDIBLE 0x0040
#define SENSOR_DELAY_SHIFT 7
#define SENSOR_DELAY_MASK 0xf
#define SENSOR_ORNAMENT_SHIFT 12
#define SENSOR_ORNAMENT_MASK 0xf
/* SENSOR_TARGET fields. */
#define SENSOR_TARGET_CELL_SHIFT 4
#define SENSOR_TARGET_CELL_MASK 3
#define SENSOR_TARGET_X_SHIFT 6
#define SENSOR_TARGET_Y_SHIFT 11
/* A missile launcher: energy in bits 4-11 and step in bits 12-15 of SENSOR_TARGET. */
#define LAUNCH_ENERGY_SHIFT 4
#define LAUNCH_ENERGY_MASK 0xff
#define LAUNCH_STEP_SHIFT 12
#define LAUNCH_STEP_MASK 0xf

/* TELEPORTER_TARGET bits. */
#define TELEPORTER_ROTATION_SHIFT 10
#define TELEPORTER_ROTATION_MASK 3
#define TELEPORTER_ABSOLUTE 0x1000      /* the rotation sets the direction, it does not add to it */
#define TELEPORTER_SCOPE_SHIFT 13       /* who uses the teleporter: bit 13 creatures, bit 14 the party and objects */
#define TELEPORTER_SCOPE_MASK 3
#define TELEPORTER_SCOPE_CREATURES 1    /* teleporter scope bit: the teleporter moves creatures */
#define TELEPORTER_AUDIBLE 0x8000
/* TELEPORTER_MAP bits. */
#define TELEPORTER_CLOSED 0x0001
#define TELEPORTER_FIXED 0x0006         /* both bits: square events do not turn the teleporter on or off */
#define TELEPORTER_MAP_SHIFT 8
#define TELEPORTER_MAP_MASK 0xff
static inline uint16_t teleporter_x(uint16_t teleporter) { return location_x(thing_word(teleporter, TELEPORTER_TARGET)); }
static inline uint16_t teleporter_y(uint16_t teleporter) { return location_y(thing_word(teleporter, TELEPORTER_TARGET)); }
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
static inline bool teleporter_flag(uint16_t teleporter, uint16_t bit) { return thing_bit(teleporter, TELEPORTER_TARGET, bit); }
static inline bool teleporter_is_closed(uint16_t teleporter) { return thing_bit(teleporter, TELEPORTER_MAP, TELEPORTER_CLOSED); }
static inline bool teleporter_is_fixed(uint16_t teleporter)
{
    return thing_field(teleporter, TELEPORTER_MAP, 0, TELEPORTER_FIXED) == TELEPORTER_FIXED;
}

static inline uint8_t group_creature(uint16_t group) { return thing_byte(group, GROUP_CREATURE); }
static inline void group_set_creature(uint16_t group, uint8_t type) { set_thing_byte(group, GROUP_CREATURE, type); }
/* The index of the active group of the group, GROUP_NOT_ACTIVE for none. */
static inline uint8_t group_active(uint16_t group) { return thing_byte(group, GROUP_ACTIVE); }
static inline void group_set_active(uint16_t group, uint8_t index) { set_thing_byte(group, GROUP_ACTIVE, index); }
/* The animation sequence and state words of a static creature. */
static inline uint16_t *group_anim_words(uint16_t group) { return thing_list(group, GROUP_ANIM_SEQUENCE); }
/* GROUP_FLAGS: the mood bits. */
#define GROUP_SEEN 0x0002               /* the party saw the creature since the last mood update */
#define MOOD_ANGRY 0x0004
#define MOOD_FEAR 0x0008
#define MOOD_RESTLESS 0x0010
#define MOOD_BORED 0x0020
#define MOOD_QUICK 0x0040               /* the creature hurries */
#define MOOD_WANDERS 0x0080
#define MOOD_STAYS 0x1000
#define MOOD_LAZY 0x2000
#define MOOD_HURRY 0x4000
#define MOOD_FAR 0x8000                 /* the creature is not on the party map: it moves slowly */
static inline uint16_t group_mood(uint16_t group) { return thing_word(group, GROUP_FLAGS); }
static inline void group_set_mood(uint16_t group, uint16_t mood) { set_thing_word(group, GROUP_FLAGS, mood); }
static inline bool group_mood_flag(uint16_t group, uint16_t bit) { return thing_bit(group, GROUP_FLAGS, bit); }
static inline void group_set_mood_flag(uint16_t group, uint16_t bit, bool on) { set_thing_bit(group, GROUP_FLAGS, bit, on); }
/* GROUP_DIR bits. */
#define GROUP_KEEP_CELLS 0x0080         /* the possessions keep their cells */
#define GROUP_DIR_SHIFT 8
#define GROUP_DIR_MASK 3
#define GROUP_DO_NOT_DISCARD 0x0400     /* the record stays when the dungeon needs free records */
static inline uint16_t group_dir(uint16_t group) { return thing_field(group, GROUP_DIR, GROUP_DIR_SHIFT, GROUP_DIR_MASK); }
static inline void group_set_dir(uint16_t group, uint16_t dir)
{
    set_thing_field(group, GROUP_DIR, GROUP_DIR_SHIFT, GROUP_DIR_MASK, dir);
}
/* A bit of GROUP_DIR: GROUP_KEEP_CELLS or GROUP_DO_NOT_DISCARD. */
static inline bool group_flag(uint16_t group, uint16_t bit) { return thing_bit(group, GROUP_DIR, bit); }
static inline void group_set_flag(uint16_t group, uint16_t bit, bool on) { set_thing_bit(group, GROUP_DIR, bit, on); }

/* ITEM_DATA of weapons, armour, and miscellaneous items: type bits 0-6, bit 7 do not discard, charges. */
#define ITEM_TYPE_MASK 0x7f             /* the GDAT index */
#define ITEM_DO_NOT_DISCARD 0x0080
#define WEAPON_CHARGES_SHIFT 10
#define ARMOUR_CHARGES_SHIFT 9
#define CHARGES_MASK 0xf
#define MISC_CHARGES_SHIFT 14           /* also the champion of a champion token or of bones */
#define MISC_CHARGES_MASK 3
#define MISC_STACK_SHIFT 8              /* a miscellaneous item: the stack size - 1 in bits 8-13 */
#define MISC_STACK_MASK 0x3f
/* ITEM_DATA of potions: power bits 0-7, type bits 8-14, bit 15 do not discard. */
#define POTION_POWER_MASK 0xff
#define POTION_TYPE_SHIFT 8
#define POTION_TYPE_MASK 0x7f
#define POTION_DO_NOT_DISCARD 0x8000
/* ITEM_DATA of scrolls: an ornament text id in bits 10-15, else a text thing index in bits 0-9. */
#define SCROLL_TEXT_SHIFT 10
#define SCROLL_TEXT_MASK 0x3f
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
    set_thing_field(weapon, ITEM_DATA, WEAPON_CHARGES_SHIFT, CHARGES_MASK, charges);
}
static inline uint16_t armour_charges(uint16_t armour)
{
    return thing_field(armour, ITEM_DATA, ARMOUR_CHARGES_SHIFT, CHARGES_MASK);
}
static inline void armour_set_charges(uint16_t armour, uint16_t charges)
{
    set_thing_field(armour, ITEM_DATA, ARMOUR_CHARGES_SHIFT, CHARGES_MASK, charges);
}
/* The charges of a miscellaneous item; the champion of a champion token or of bones. */
static inline uint16_t misc_charges(uint16_t misc) { return thing_field(misc, ITEM_DATA, MISC_CHARGES_SHIFT, MISC_CHARGES_MASK); }
static inline void misc_set_charges(uint16_t misc, uint16_t charges)
{
    set_thing_field(misc, ITEM_DATA, MISC_CHARGES_SHIFT, MISC_CHARGES_MASK, charges);
}
/* The stack size - 1 of a miscellaneous item (coins). */
static inline uint16_t misc_stack(uint16_t misc) { return thing_field(misc, ITEM_DATA, MISC_STACK_SHIFT, MISC_STACK_MASK); }
static inline void misc_set_stack(uint16_t misc, uint16_t count)
{
    set_thing_field(misc, ITEM_DATA, MISC_STACK_SHIFT, MISC_STACK_MASK, count);
}
static inline uint16_t potion_power(uint16_t potion) { return thing_field(potion, ITEM_DATA, 0, POTION_POWER_MASK); }
static inline void potion_set_power(uint16_t potion, uint16_t power)
{
    set_thing_field(potion, ITEM_DATA, 0, POTION_POWER_MASK, power);
}
static inline uint16_t potion_type(uint16_t potion) { return thing_field(potion, ITEM_DATA, POTION_TYPE_SHIFT, POTION_TYPE_MASK); }
static inline void potion_set_type(uint16_t potion, uint16_t type)
{
    set_thing_field(potion, ITEM_DATA, POTION_TYPE_SHIFT, POTION_TYPE_MASK, type);
}
/* CONTAINER_KIND fields. */
#define CONTAINER_OPEN 0x0001
#define CONTAINER_KIND_SHIFT 1
#define CONTAINER_KIND_MASK 3
enum container_kind { CONTAINER_CHEST, CONTAINER_MINION_MAP };  /* a chest or a money box, a minion map */
#define CONTAINER_TYPE_SHIFT 13
#define CONTAINER_TYPE_MASK 7
#define MONEYBOX_PLACED 0x0400          /* a creature placed the money box: creatures take it as empty until the party moves it */
static inline bool container_is_open(uint16_t container) { return thing_bit(container, CONTAINER_KIND, CONTAINER_OPEN); }
static inline void container_set_open(uint16_t container, bool open)
{
    set_thing_bit(container, CONTAINER_KIND, CONTAINER_OPEN, open);
}
static inline uint16_t container_kind(uint16_t container)
{
    return thing_field(container, CONTAINER_KIND, CONTAINER_KIND_SHIFT, CONTAINER_KIND_MASK);
}
static inline void container_set_kind(uint16_t container, uint16_t kind)
{
    set_thing_field(container, CONTAINER_KIND, CONTAINER_KIND_SHIFT, CONTAINER_KIND_MASK, kind);
}
/* The type of the container within its kind. */
static inline uint16_t container_type(uint16_t container)
{
    return thing_field(container, CONTAINER_KIND, CONTAINER_TYPE_SHIFT, CONTAINER_TYPE_MASK);
}
static inline void container_set_type(uint16_t container, uint16_t type)
{
    set_thing_field(container, CONTAINER_KIND, CONTAINER_TYPE_SHIFT, CONTAINER_TYPE_MASK, type);
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
static inline bool moneybox_is_placed(uint16_t box) { return thing_bit(box, MONEYBOX_FLAGS, MONEYBOX_PLACED); }
static inline void moneybox_set_placed(uint16_t box, bool placed) { set_thing_bit(box, MONEYBOX_FLAGS, MONEYBOX_PLACED, placed); }

#define PROJECTILE_HOLDER_WAITS 0x0001  /* PROJECTILE_CARRIED bit */
/* EXPLOSION_DATA fields. */
#define EXPLOSION_TYPE_MASK 0x7f
#define EXPLOSION_CENTERED 0x0080       /* the explosion is in the middle of its square, not on a cell */
#define EXPLOSION_POWER_SHIFT 8
#define EXPLOSION_POWER_MASK 0xff

/* Loads a new game from DUNGEON.DAT; returns 1 on success, 0 on a read error or a compressed file. */
int dungeon_load(const char *path);
/* Reads a dungeon at the file position: DUNGEON.DAT (new_game 1, adds free records) or the image in a save file (new_game 0). */
int dungeon_read(FILE *f, int new_game);
/* Writes the dungeon image of a save file: the header with the counts of the loaded game, then the sections. */
int dungeon_write(FILE *f);
void dungeon_free(void);

uint16_t dungeon_party_start(void);
int dungeon_map_count(void);
int dungeon_map_width(int map);
int dungeon_map_height(int map);
int dungeon_map_level(int map);
/* The square byte at (x, y); x and y must be inside the map. */
uint8_t dungeon_square(int map, int x, int y);
/* The first thing on the square (x, y), THING_ENDOFLIST when the square has no things. */
uint16_t dungeon_square_first_thing(int map, int x, int y);
/* The first thing on the square after its doors, teleporters, texts, and sensors. */
uint16_t dungeon_square_first_object(int map, int x, int y);
/* Entries of the square first-thing table, including the free entries of a new game. */
int dungeon_square_thing_count(void);
const uint16_t *dungeon_square_things(void);
int dungeon_thing_count(int type);
int dungeon_thing_size(int type);
#define DUNGEON_RECORD_MAX 16     /* the largest thing record (a creature group) */
/* Copies the record of thing to or from buffer (DUNGEON_RECORD_MAX bytes). */
void dungeon_save_record(uint16_t thing, uint8_t *buffer);
void dungeon_restore_record(uint16_t thing, const uint8_t *buffer);
/* True when thing has a record in the loaded dungeon. */
int dungeon_thing_valid(uint16_t thing);
uint16_t dungeon_next_thing(uint16_t thing);
uint8_t *dungeon_square_byte(int map, int x, int y);
/* Removes thing from the thing list whose first-thing word is at list. */
void dungeon_unlink_thing_from_list(uint16_t thing, uint16_t *list);
/* Appends thing at the end of a thing list. */
void dungeon_append_thing(uint16_t thing, uint16_t *list);
/* Appends thing at the end of the square's thing list; returns 0 when the first-thing table is full. */
int dungeon_link_thing_to_square(uint16_t thing, int map, int x, int y);
/* Removes thing from the square's thing list. */
void dungeon_unlink_thing_from_square(uint16_t thing, int map, int x, int y);
/* Takes a free record of type, cleared, without cell bits. When the table is full, it reuses a thing from
   dun_get_discarded_thing; THING_NONE when there is none. type | THING_ANY_RECORD: a miscellaneous item may take
   the records that the dungeon keeps for the game. */
#define THING_ANY_RECORD 0x8000
uint16_t dungeon_unused_thing(int type);
#define DISCARD_ANY_KIND 0xff        /* dun_get_discarded_thing (move.h): a thing of any kind */
/* The map where the last search for each thing type ended; the next search starts there. */
extern uint8_t last_discarded_map[THING_TYPE_COUNT];
/* The target map of a teleporter square whose target square is a teleporter too, both with a link sensor; else -1. */
int dungeon_teleporter_link_map(int map, int x, int y);
/* The square byte of map at (x, y). Away from the map it gives the edge walls: a wall (with the bit of the
   side that faces the map) next to an open square at the map edge, else SQUARE_OUTSIDE. */
uint8_t dungeon_square_edge(int map, int x, int y);
/* The map of the level at level_delta from the level of map that holds the dungeon position of (x, y) of map, and
   its square there in *x, *y; -1 when no such map has an open square there. */
int dungeon_location_on_map(int map, int level_delta, int *x, int *y);
/* The same search from list position *next (0: the first map of the level); sets *next to the position after the
   map that it found, 0 when it found none. */
int dungeon_location_on_map_from(int map, int level_delta, int *x, int *y, int *next);
#define LEVEL_UP (-1)
#define LEVEL_DOWN 1
/* Level transfer flags. */
#define TRANSFER_PIT 0x008
#define TRANSFER_FALL_INTO 0x010
#define TRANSFER_STAIRS 0x100

/* A pair of teleporters with link sensors: a seamless passage to another map. */
typedef struct {
    int side, target_side;  /* side of the passage on the source and on the target square, 0 to 3 */
    int x, y, map;          /* the target square */
} TeleporterLink;
/* Fills link for a teleporter square whose target is a teleporter, both with link sensors; returns 0 when not. */
int dungeon_teleporter_link(int map, int x, int y, TeleporterLink *link);

/* Map descriptor values. */
int dungeon_map_offset_x(int map);              /* position of the map on its level */
int dungeon_map_offset_y(int map);
uint16_t dungeon_map_flags(int map);            /* bit 7 door type 0 present, bit 8 door type 1 present */
uint16_t dungeon_map_ornament_counts(int map);  /* wall bits 0-3, random wall 4-7, floor 8-11, random floor 12-15 */
uint16_t dungeon_map_object_counts(int map);    /* door ornaments bits 0-3, creature types bits 4-7 */
uint16_t dungeon_map_graphics(int map);         /* graphics set bits 4-7, door types bits 8-11 and 12-15 */
#define MAP_HAS_DOOR_TYPE_0 0x0080
#define MAP_HAS_DOOR_TYPE_1 0x0100
#define MAP_WALLSET_SHIFT 4
#define MAP_DOOR_TYPE_SHIFT 8       /* door type 0; door type 1 follows 4 bits higher */
#define MAP_DOOR_TYPE_1_SHIFT 12
#define MAP_NIBBLE_MASK 0xf
#define DOOR_TYPE_NONE 0xff
#define MAP_RANDOM_WALL_SHIFT 4     /* map ornament counts: random wall ornaments in bits 4-7 */
#define MAP_CREATURE_TYPES_SHIFT 4  /* map object counts bits 4-7: the number of creature types of the map */
#define MAP_DIFFICULTY_SHIFT 12     /* map object counts: difficulty bits 12-15, also the experience factor of the map */
/* The wall set (graphics set) of map. */
#define dungeon_map_wallset(map) (dungeon_map_graphics(map) >> MAP_WALLSET_SHIFT & MAP_NIBBLE_MASK)
/* The door type of door slot 0 or 1 of map, DOOR_TYPE_NONE when the map has none. */
static inline uint8_t dungeon_map_door_type(int map, int slot)
{
    if (!(dungeon_map_flags(map) & (slot ? MAP_HAS_DOOR_TYPE_1 : MAP_HAS_DOOR_TYPE_0))) return DOOR_TYPE_NONE;
    return dungeon_map_graphics(map) >> (slot ? MAP_DOOR_TYPE_1_SHIFT : MAP_DOOR_TYPE_SHIFT) & MAP_NIBBLE_MASK;
}
/* The number of random wall ornaments of map. */
#define dungeon_map_random_wall_count(map) (dungeon_map_ornament_counts(map) >> MAP_RANDOM_WALL_SHIFT & MAP_NIBBLE_MASK)
/* The number of creature types of map. */
#define dungeon_map_creature_type_count(map) (dungeon_map_object_counts(map) >> MAP_CREATURE_TYPES_SHIFT & MAP_NIBBLE_MASK)
#define dungeon_map_difficulty(map) (dungeon_map_object_counts(map) >> MAP_DIFFICULTY_SHIFT)
/* The creature types of map (count in dungeon_map_object_counts bits 4-7). */
const uint8_t *dungeon_map_creature_types(int map);
/* The ornament lists of map: GDAT indices of its wall, floor, and door ornaments. */
const uint8_t *dungeon_map_wall_ornaments(int map);
const uint8_t *dungeon_map_floor_ornaments(int map);
const uint8_t *dungeon_map_door_ornaments(int map);
/* The first word of the dungeon header; it seeds the random wall ornaments. */
uint16_t dungeon_ornament_seed(void);

/* The pass of a new game after the load of DUNGEON.DAT: numbered teleporter links, shared lists of linked
   teleporters, level link marks, the start states of sensors and texts, creature health and homes. */
void dungeon_link_things_after_load(void);
/* Reads the level digits from the text on the party start square (the first load of a run). */
void dungeon_read_level_table(void);
/* The level digit of level: the map icon set of the squares outside the maps of the level. */
int dungeon_level_digit(int level);

int dungeon_text_word_count(void);
const uint16_t *dungeon_text_data(void);
/* The maps of a level, ended by LEVEL_LIST_END. */
const uint8_t *dungeon_level_maps(int level);

#endif
