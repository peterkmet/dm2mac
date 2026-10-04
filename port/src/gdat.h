#ifndef GDAT_H
#define GDAT_H
#include <stddef.h>
#include <stdint.h>

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

/* Key types whose key table value is a number, not an item number. */
enum { GDAT_TYPE_IMAGE = 1, GDAT_TYPE_VALUE = 0x0b, GDAT_TYPE_VALUE2 = 0x0c };

#define GDAT_PALETTE_COLORS 256
#define GDAT_NO_ITEM 0xffff

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
int text_find_int_field(const char *text, const char *key);
/* The drawing offset of an image: x in the high byte, y in the low byte, each signed. */
int gdat_image_offset(const gdat_t *g, int category, int index, int entry);

/* A 4 bpp image stream; with base, nibble command 5 copies pixels from the same place of base. */
int image_decode_4bpp(const uint8_t *src, size_t src_size, const uint8_t *base, uint8_t *dst, size_t dst_size);
/* An 8 bpp image stream: header, codec byte, LZSS data. */
int image_decode_8bpp(const uint8_t *src, size_t src_size, uint8_t *dst, size_t dst_size);

#endif
