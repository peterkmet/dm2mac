#ifndef GDAT_H
#define GDAT_H
#include <stddef.h>
#include <stdint.h>

/* Prefixes: gdat_ image_ */

/* GRAPHICS.DAT: a header with item sizes, a key table (item 0), then the items. */

enum {
    GDAT_OK = 0,
    GDAT_ERR_IO = -1,        /* the file cannot be read */
    GDAT_ERR_FORMAT = -2,    /* bad signature, bad key table, or file size not equal to the item sizes */
    GDAT_ERR_ITEM = -3,      /* item number out of range */
    GDAT_ERR_NOT_IMAGE = -4, /* the item has no image header */
    GDAT_ERR_DATA = -5,      /* the compressed stream is damaged or uses an unsupported codec */
    GDAT_ERR_SPACE = -6,     /* the destination buffer is too small */
};

/* GDAT key types: the third part of a key. Value and value2 keys hold a number, not an item number. */
enum {
    GDAT_TYPE_IMAGE = 1,
    GDAT_TYPE_SOUND = 2,
    GDAT_TYPE_TEXT = 5,
    GDAT_TYPE_REMAP = 7,            /* a color remap table, for example a depth shading table */
    GDAT_TYPE_CREATURE_DATA = 7,    /* the animation tables of a creature */
    GDAT_TYPE_TABLE = 8,            /* a word table */
    GDAT_TYPE_VALUE = 0x0b,
    GDAT_TYPE_VALUE2 = 0x0c,
    GDAT_TYPE_DATA = 0x0d           /* raw data, for example the interface colors */
};

/* GDAT categories: the first part of a key (category, index, type, entry). */
enum gdat_category {
    CAT_INTERFACE = 1,          /* the interface pictures, the font, the palette, the layout */
    CAT_DUNGEON = 3,            /* index 0: the dungeon (sounds, texts, the start hour); index map + 1: a map */
    CAT_MUSIC = 4,
    CAT_MENU = 5,
    CAT_END = 6,
    CAT_PANEL = 7,              /* the inventory panel pictures */
    CAT_WALLSET = 8,            /* index: graphics set of the map */
    CAT_WALL_ORNAMENT = 9,
    CAT_FLOOR_ORNAMENT = 0x0a,
    CAT_DOOR_ORNAMENT = 0x0b,
    CAT_DOOR_BUTTON = 0x0c,
    CAT_EXPLOSION = 0x0d,       /* missiles, explosions, and spells */
    CAT_DOOR = 0x0e,
    CAT_CREATURE = 0x0f,
    CAT_WEAPON = 0x10,
    CAT_SCROLL = 0x12,
    CAT_CONTAINER = 0x14,
    CAT_MISC = 0x15,            /* miscellaneous items; also the image that stands in for a missing image */
    CAT_CHAMPION = 0x16,
    CAT_SKY = 0x17,             /* index: graphics set; the sky objects and the thunder sound */
    CAT_TELEPORTER = 0x18,      /* index 0: the sparkles and the sound of a teleporter */
    CAT_DIALOG = 0x1a,
    CAT_NONE = 0xff             /* no GDAT image, for example a draw context without one */
};

#define GDAT_PALETTE_COLORS 256
#define GDAT_NO_ITEM 0xffff
#define IMAGE_4BPP 4            /* bits per pixel of a 16-color image */
#define IMAGE_8BPP 8            /* bits per pixel of a 256-color image */

typedef struct gdat gdat_t;

/* Reads the whole file; *out is NULL on error. */
int gdat_load(const char *path, gdat_t **out);
void gdat_free(gdat_t *g);
int gdat_item_count(const gdat_t *g);
/* The raw bytes of an item, NULL for a bad item number. */
const uint8_t *gdat_item(const gdat_t *g, int item, uint32_t *size);
/* The value of key (category, index, type, entry) for the English game, -1 if absent. */
int gdat_find(const gdat_t *g, int category, int index, int type, int entry);

/* Width, height, and bits per pixel (4 or 8) of an image item; returns the decoded byte size or an error. */
int gdat_image_size(const gdat_t *g, int item, int *width, int *height, int *bpp);
/* Decodes an image item into dst. 4 bpp rows hold an even pixel count, high nibble first; pad nibbles keep the dst content. */
int gdat_decode_image(const gdat_t *g, int item, uint8_t *dst, size_t dst_size);
/* The 16 palette indices of the colors of a 4 bpp image, NULL for an 8 bpp image. */
const uint8_t *gdat_image_colormap(const gdat_t *g, int item);
/* The 6-bit VGA red, green, blue of a color of the game palette. */
int gdat_palette_color(const gdat_t *g, int color, uint8_t rgb[3]);

/* The original lookup: the value of a value key or 0, else the item of the key or GDAT_NO_ITEM. */
uint16_t gdat_entry_index(const gdat_t *g, int category, int index, int type, int entry);
int gdat_exists(const gdat_t *g, int category, int index, int type, int entry);
/* The text entry (category, index, 5, entry), decrypted, in dst; an empty string when it is absent. */
char *gdat_text(const gdat_t *g, int category, int index, int entry, char *dst, size_t dst_size);
/* The number after the last key in text, as the original parses "key=number" fields. */
int gdat_text_int_field(const char *text, const char *key);
/* The drawing offset of an image: x in the high byte, y in the low byte, each signed. */
int gdat_image_offset(const gdat_t *g, int category, int index, int entry);
#define GDAT_OFFSET_X_SHIFT 8
static inline int8_t gdat_offset_x(int offset) { return (int8_t)(offset >> GDAT_OFFSET_X_SHIFT); }
static inline int8_t gdat_offset_y(int offset) { return (int8_t)offset; }

/* A 4 bpp image stream; with base, nibble command 5 copies pixels from the same place of base. */
int image_decode_4bpp(const uint8_t *src, size_t src_size, const uint8_t *base, uint8_t *dst, size_t dst_size);
/* An 8 bpp image stream: header, codec byte, LZSS data. */
int image_decode_8bpp(const uint8_t *src, size_t src_size, uint8_t *dst, size_t dst_size);

#endif
