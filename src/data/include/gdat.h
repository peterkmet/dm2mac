/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_DATA_GDAT_H
#define DM2_DATA_GDAT_H
/* Prefixes: gdat_ image_ */

/* GRAPHICS.DAT: a header with item sizes, a key table (item 0), then the items. */

#include <stddef.h>
#include <stdint.h>

/* Results of the loaders and decoders. */
#define GDAT_OK            0
#define GDAT_ERR_IO        (-1) /* the file cannot be read */
#define GDAT_ERR_FORMAT    (-2) /* bad signature, bad key table, or file size not equal to the item sizes */
#define GDAT_ERR_ITEM      (-3) /* item number out of range */
#define GDAT_ERR_NOT_IMAGE (-4) /* the item has no image header */
#define GDAT_ERR_DATA      (-5) /* the compressed stream is damaged or uses an unsupported codec */
#define GDAT_ERR_SPACE     (-6) /* the destination buffer is too small */

/* GDAT key types: the third part of a key; value keys hold a number, not an item number; type 7 depends on the category. */
#define GDAT_TYPE_IMAGE         0x01
#define GDAT_TYPE_SOUND         0x02
#define GDAT_TYPE_TEXT          0x05
#define GDAT_TYPE_REMAP         0x07 /* a color remap table, for example a depth shading table */
#define GDAT_TYPE_CREATURE_DATA 0x07 /* the animation tables of a creature */
#define GDAT_TYPE_TABLE         0x08 /* a word table */
#define GDAT_TYPE_VALUE         0x0b
#define GDAT_TYPE_VALUE2        0x0c
#define GDAT_TYPE_DATA          0x0d /* raw data, for example the interface colors */

/* GDAT categories: the first part of a key (category, index, type, entry). */
#define CAT_INTERFACE      0x01 /* the interface pictures, the font, the palette, the layout */
#define CAT_DUNGEON        0x03 /* index 0: the dungeon (sounds, texts, the start hour); index map + 1: a map */
#define CAT_MUSIC          0x04
#define CAT_MENU           0x05
#define CAT_END            0x06
#define CAT_PANEL          0x07 /* the inventory panel pictures */
#define CAT_WALLSET        0x08 /* index: graphics set of the map */
#define CAT_WALL_ORNAMENT  0x09
#define CAT_FLOOR_ORNAMENT 0x0a
#define CAT_DOOR_ORNAMENT  0x0b
#define CAT_DOOR_BUTTON    0x0c
#define CAT_EXPLOSION      0x0d /* missiles, explosions, and spells */
#define CAT_DOOR           0x0e
#define CAT_CREATURE       0x0f
#define CAT_WEAPON         0x10
#define CAT_SCROLL         0x12
#define CAT_CONTAINER      0x14
#define CAT_MISC           0x15 /* miscellaneous items; also the image that stands in for a missing image */
#define CAT_CHAMPION       0x16
#define CAT_SKY            0x17 /* index: graphics set; the sky objects and the thunder sound */
#define CAT_TELEPORTER     0x18 /* index 0: the sparkles and the sound of a teleporter */
#define CAT_DIALOG         0x1a
#define CAT_NONE           0xff /* no GDAT image, for example a draw context without one */

/* Number of colors of the game palette. */
#define GDAT_PALETTE_COLORS 256

/* Red, green and blue of a palette color. */
#define GDAT_RGB_SIZE 3

/* The item number of a key that has no item. */
#define GDAT_NO_ITEM 0xffff

/* Bits per pixel of the image formats. */
#define IMAGE_4BPP 4 /* a 16-color image */
#define IMAGE_8BPP 8 /* a 256-color image */

/* A GDAT key without its type: the category, the index, and the entry. */
typedef struct {
    int category;
    int index;
    int entry;
} GdatKey;

typedef struct gdat gdat_t;

/* Loading */
/* Reads the whole file; *out is NULL on error. */
int gdat_load(const char *path, gdat_t **out);
/* Frees a loaded file; exported for the tests. */
void gdat_free(gdat_t *g);
/* The GRAPHICS.DAT that the game reads; NULL before the first gdat_select. */
const gdat_t *gdat_current(void);
/* Sets the GRAPHICS.DAT that the game reads. */
void gdat_select(const gdat_t *g);

/* Items and keys */
/* The number of items of the file. */
int gdat_item_count(const gdat_t *g);
/* The raw bytes of an item, NULL for a bad item number. */
const uint8_t *gdat_item(const gdat_t *g, int item, uint32_t *size);
/* The value of key (category, index, type, entry) for the English game, -1 if absent. */
int gdat_find(const gdat_t *g, int category, int index, int type, int entry);

/* Images */
/* Width, height, and bits per pixel (4 or 8) of an image item; returns the decoded byte size or an error. */
int gdat_image_size(const gdat_t *g, int item, int *width, int *height, int *bpp);
/* Decodes an image item into dst. 4 bpp rows hold an even pixel count, high nibble first; pad nibbles keep the dst content. */
int gdat_decode_image(const gdat_t *g, int item, uint8_t *dst, size_t dst_size);
/* The 16 palette indices of the colors of a 4 bpp image, NULL for an 8 bpp image. */
const uint8_t *gdat_image_colormap(const gdat_t *g, int item);
/* The 6-bit VGA red, green, blue of a color of the game palette. */
int gdat_palette_color(const gdat_t *g, int color, uint8_t rgb[GDAT_RGB_SIZE]);
/* A 4 bpp image stream; with base, nibble command 5 copies pixels from the same place of base. A command sets a run of
   pixels to a local color, a literal color, the pixels one row above, or (delta images) the pixels of base; runs wrap
   at the row end; exported for the tests. */
int image_decode_4bpp(const uint8_t *src, size_t src_size, const uint8_t *base, uint8_t *dst, size_t dst_size);
/* An 8 bpp image stream: header, codec byte, LZSS data; exported for the tests. */
int image_decode_8bpp(const uint8_t *src, size_t src_size, uint8_t *dst, size_t dst_size);

/* Values and texts */
/* The original lookup: the value of a value key or 0, else the item of the key or GDAT_NO_ITEM. */
uint16_t gdat_entry_index(const gdat_t *g, int category, int index, int type, int entry);
/* Whether the key (category, index, type, entry) exists. */
int gdat_exists(const gdat_t *g, int category, int index, int type, int entry);
/* The text entry (category, index, 5, entry), decrypted, in dst; an empty string when it is absent. */
char *gdat_text(const gdat_t *g, GdatKey entry_key, char *dst, size_t dst_size);
/* The number after the last key in text, as the original parses "key=number" fields. */
int gdat_text_int_field(const char *text, const char *key);
/* The drawing offset of an image: x in the high byte, y in the low byte, each signed. */
int gdat_image_offset(const gdat_t *g, int category, int index, int entry);
/* The shift of x in the packed drawing offset. */
#define GDAT_OFFSET_X_SHIFT 8
static inline int8_t gdat_offset_x(int offset)
{
    return (int8_t)(offset >> GDAT_OFFSET_X_SHIFT);
}
static inline int8_t gdat_offset_y(int offset)
{
    return (int8_t)offset;
}

#endif /* DM2_DATA_GDAT_H */
