#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gdat.h"

/* File header: signature, item count, size of item 0 (u32), sizes of items 1 to count-1 (u16). */
#define GDAT_SIGNATURE 0x8005
#define HEADER_COUNT 2
#define HEADER_KEYS_SIZE 4
#define HEADER_ITEM_SIZES 8
#define ITEM_KEYS 0

/* Key table (item 0), big-endian: signature, row count, column count, column descriptors, rows. */
#define KEYS_SIGNATURE 0x8001
#define KEYS_ROW_COUNT 2
#define KEYS_COLUMN_COUNT 4
#define KEYS_COLUMNS 7
#define KEYS_ROWS (6 + KEYS_COLUMNS * 2)
#define ROW_SIZE 8
#define ROW_CATEGORY 0
#define ROW_INDEX 1
#define ROW_TYPE 2
#define ROW_ENTRY 3
#define ROW_LANGUAGE 4
#define ROW_VALUE 6
#define LANGUAGE_MASK 0xf0
#define LANGUAGE_ALL 0x00
#define LANGUAGE_ENGLISH 0x10
#define VALUE_ITEM_MASK 0x7fff

/* Image header: width and height in the low 10 bits of two u16; the top 6 bits of byte 3 give the format. */
#define IMAGE_WIDTH 0
#define IMAGE_HEIGHT 2
#define IMAGE_FORMAT_BYTE 3
#define IMAGE_FORMAT_SHIFT 2
#define IMAGE_DIM_MASK 0x3ff
#define FORMAT_8BPP 0x1f
#define FORMAT_RAW 0x20
#define RAW_BPP 4
#define RAW_PIXELS 10
#define COLORMAP_SIZE 16
#define IMAGE_OFFSET_SHIFT 10      /* the top 6 bits of the width and height words hold the image offset */
#define OFFSET_IN_KEYS (-32)       /* x offset bits: the offset is the value key (category, index, 0x0c, entry) */
#define IMAGE_8BPP_OFFSET_X 4
#define IMAGE_8BPP_OFFSET_Y 5

/* The GDAT flag word: value key (0, 0, 0x0b, 0). */
#define FLAGS_CATEGORY 0
#define FLAGS_INDEX 0
#define FLAGS_ENTRY 0
#define FLAG_ENCRYPTED_TEXT 0x08
#define TEXT_INVERT 0xff           /* encrypted text: byte = (stored ^ 0xff) - position */
#define GDAT_TYPE_TEXT 5

/* 8 bpp stream: codec byte, then the LZSS data. */
#define CODEC_OFFSET 6
#define LZSS_DATA 8
#define CODEC_LZSS_4_4 2
#define CODEC_LZSS_5_3 3
#define LZSS_MIN_MATCH 3
#define FLAG_BITS 8

/* 4 bpp stream: nibbles from nibble 8, high nibble first; a local color table, then commands. */
#define NIBBLE_START 8
#define LOCAL_COLORS 6
#define DELTA_LOCAL_COLORS 5
#define CMD_KIND_MASK 7
#define CMD_RUN 8
#define CMD_COPY_BASE 5
#define CMD_COPY_ABOVE 6
#define CMD_COLOR_NIBBLE 7
#define RUN_SHORT_MIN 2
#define RUN_LONG 0xf
#define RUN_LONG_MIN 0x11
#define RUN_HUGE 0xff
#define RUN_HUGE_NIBBLES 4

/* The game palette: key (1, 0, 9, 254), 256 entries of color index, red, green, blue. */
#define PALETTE_CATEGORY 1
#define PALETTE_INDEX 0
#define PALETTE_TYPE 9
#define PALETTE_ENTRY 0xfe
#define PALETTE_ENTRY_SIZE 4
#define PALETTE_RED 1

struct gdat {
    uint8_t *file;
    int count;
    uint32_t *offset, *size;
    const uint8_t *rows;
    int row_count;
};

static unsigned le16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t)le16(p + 2) << 16; }
static unsigned be16(const uint8_t *p) { return p[0] << 8 | p[1]; }

/* Checks the header, item sizes, and key table, and builds the item offset table. */
static int gdat_parse(gdat_t *g, size_t file_size)
{
    const uint8_t *f = g->file;
    if (file_size < HEADER_ITEM_SIZES || le16(f) != GDAT_SIGNATURE) return GDAT_ERR_FORMAT;
    g->count = le16(f + HEADER_COUNT);
    size_t end = HEADER_ITEM_SIZES + 2 * (size_t)(g->count - 1);
    if (g->count < 1 || end > file_size) return GDAT_ERR_FORMAT;
    g->offset = malloc(g->count * sizeof *g->offset);
    g->size = malloc(g->count * sizeof *g->size);
    if (!g->offset || !g->size) return GDAT_ERR_IO;
    for (int i = 0; i < g->count; i++) {
        g->size[i] = i == ITEM_KEYS ? le32(f + HEADER_KEYS_SIZE) : le16(f + HEADER_ITEM_SIZES + 2 * (i - 1));
        g->offset[i] = end;
        end += g->size[i];
    }
    if (end != file_size) return GDAT_ERR_FORMAT;
    const uint8_t *keys = f + g->offset[ITEM_KEYS];
    if (g->size[ITEM_KEYS] < KEYS_ROWS || be16(keys) != KEYS_SIGNATURE || be16(keys + KEYS_COLUMN_COUNT) != KEYS_COLUMNS)
        return GDAT_ERR_FORMAT;
    g->row_count = be16(keys + KEYS_ROW_COUNT);
    g->rows = keys + KEYS_ROWS;
    return KEYS_ROWS + (size_t)g->row_count * ROW_SIZE > g->size[ITEM_KEYS] ? GDAT_ERR_FORMAT : GDAT_OK;
}

int gdat_load(const char *path, gdat_t **out)
{
    *out = NULL;
    gdat_t *g = calloc(1, sizeof *g);
    FILE *fp = fopen(path, "rb");
    if (!g || !fp) {
        free(g);
        if (fp) fclose(fp);
        return GDAT_ERR_IO;
    }
    long file_size = fseek(fp, 0, SEEK_END) ? -1 : ftell(fp);
    int err = GDAT_ERR_IO;
    if (file_size >= 0 && !fseek(fp, 0, SEEK_SET) && (g->file = malloc(file_size ? file_size : 1))
        && fread(g->file, 1, file_size, fp) == (size_t)file_size)
        err = gdat_parse(g, file_size);
    fclose(fp);
    if (err) gdat_free(g);
    else *out = g;
    return err;
}

void gdat_free(gdat_t *g)
{
    if (!g) return;
    free(g->file);
    free(g->offset);
    free(g->size);
    free(g);
}

int gdat_item_count(const gdat_t *g) { return g->count; }

const uint8_t *gdat_item(const gdat_t *g, int item, uint32_t *size)
{
    if (item < 0 || item >= g->count) return NULL;
    *size = g->size[item];
    return g->file + g->offset[item];
}

int gdat_find(const gdat_t *g, int category, int index, int type, int entry)
{
    // ponytail: linear scan of 11854 rows, sort or index the rows if lookups get hot
    for (int i = 0; i < g->row_count; i++) {
        const uint8_t *r = g->rows + i * ROW_SIZE;
        int language = r[ROW_LANGUAGE] & LANGUAGE_MASK;
        if (r[ROW_CATEGORY] != category || r[ROW_INDEX] != index || r[ROW_TYPE] != type || r[ROW_ENTRY] != entry
            || (language != LANGUAGE_ALL && language != LANGUAGE_ENGLISH))
            continue;
        unsigned value = be16(r + ROW_VALUE);
        return type == GDAT_TYPE_VALUE || type == GDAT_TYPE_VALUE2 ? (int)value : (int)(value & VALUE_ITEM_MASK);
    }
    return -1;
}

static unsigned image_dim(const uint8_t *src, int field) { return le16(src + field) & IMAGE_DIM_MASK; }

/* Decoded byte size of a width x height image; 4 bpp rows hold an even pixel count. */
static size_t image_bytes(unsigned width, unsigned height, int bpp)
{
    return bpp == 8 ? (size_t)width * height : (size_t)(width + 1) / 2 * height;
}

/* Reads the image header of an item; returns its format or an error. */
static int image_header(const gdat_t *g, int item, const uint8_t **data, uint32_t *size, int *width, int *height, int *bpp)
{
    *data = gdat_item(g, item, size);
    if (!*data) return GDAT_ERR_ITEM;
    if (*size < LZSS_DATA) return GDAT_ERR_NOT_IMAGE;
    int format = (*data)[IMAGE_FORMAT_BYTE] >> IMAGE_FORMAT_SHIFT;
    *width = image_dim(*data, IMAGE_WIDTH);
    *height = image_dim(*data, IMAGE_HEIGHT);
    if (format == FORMAT_8BPP) *bpp = 8;
    else if (format == FORMAT_RAW && *size >= RAW_PIXELS) *bpp = le16(*data + RAW_BPP);
    else if (format == FORMAT_RAW) return GDAT_ERR_NOT_IMAGE;
    else *bpp = 4;  /* the format bits of a 4 bpp image can hold its y offset (gdat_image_offset) */
    if (*bpp != 4 && *bpp != 8) return GDAT_ERR_NOT_IMAGE;
    return format;
}

int gdat_image_size(const gdat_t *g, int item, int *width, int *height, int *bpp)
{
    const uint8_t *data;
    uint32_t size;
    int format = image_header(g, item, &data, &size, width, height, bpp);
    return format < 0 ? format : (int)image_bytes(*width, *height, *bpp);
}

int gdat_decode_image(const gdat_t *g, int item, uint8_t *dst, size_t dst_size)
{
    const uint8_t *data;
    uint32_t size;
    int width, height, bpp;
    int format = image_header(g, item, &data, &size, &width, &height, &bpp);
    if (format < 0) return format;
    if (format == FORMAT_8BPP) return image_decode_8bpp(data, size, dst, dst_size);
    if (format != FORMAT_RAW) return image_decode_4bpp(data, size, NULL, dst, dst_size);
    size_t bytes = image_bytes(width, height, bpp);
    if (RAW_PIXELS + bytes > size) return GDAT_ERR_DATA;
    if (bytes > dst_size) return GDAT_ERR_SPACE;
    memcpy(dst, data + RAW_PIXELS, bytes);
    return GDAT_OK;
}

const uint8_t *gdat_image_colormap(const gdat_t *g, int item)
{
    const uint8_t *data;
    uint32_t size;
    int width, height, bpp;
    if (image_header(g, item, &data, &size, &width, &height, &bpp) < 0 || bpp != 4 || size < COLORMAP_SIZE) return NULL;
    return data + size - COLORMAP_SIZE;
}

int gdat_palette_color(const gdat_t *g, int color, uint8_t rgb[3])
{
    uint32_t size;
    const uint8_t *palette = gdat_item(g, gdat_find(g, PALETTE_CATEGORY, PALETTE_INDEX, PALETTE_TYPE, PALETTE_ENTRY), &size);
    if (!palette || size < GDAT_PALETTE_COLORS * PALETTE_ENTRY_SIZE) return GDAT_ERR_FORMAT;
    if (color < 0 || color >= GDAT_PALETTE_COLORS) return GDAT_ERR_ITEM;
    memcpy(rgb, palette + color * PALETTE_ENTRY_SIZE + PALETTE_RED, 3);
    return GDAT_OK;
}

typedef struct {
    const uint8_t *src;
    size_t pos, end;
    int bad;
} nibbles_t;

static int next_nibble(nibbles_t *n)
{
    if (n->pos >= n->end) {
        n->bad = 1;
        return 0;
    }
    uint8_t b = n->src[n->pos / 2];
    return n->pos++ & 1 ? b & 0xf : b >> 4;
}

/* A run length: 2 to 16 in one nibble, 17 to 271 in two, any 16-bit count in four more. */
static unsigned run_length(nibbles_t *n)
{
    unsigned v = next_nibble(n);
    if (v != RUN_LONG) return v + RUN_SHORT_MIN;
    v = next_nibble(n) << 4;
    v |= next_nibble(n);
    if (v != RUN_HUGE) return v + RUN_LONG_MIN;
    v = 0;
    for (int i = 0; i < RUN_HUGE_NIBBLES; i++) v = v << 4 | next_nibble(n);
    return v;
}

static int get_pixel(const uint8_t *buf, size_t pos) { return pos & 1 ? buf[pos / 2] & 0xf : buf[pos / 2] >> 4; }

static void set_pixel(uint8_t *buf, size_t pos, int color)
{
    uint8_t *p = buf + pos / 2;
    *p = pos & 1 ? (*p & 0xf0) | color : (*p & 0x0f) | color << 4;
}

/* Run-length 4 bpp image: each command sets a run of pixels to a local color, a literal color,
   the pixels one row above, or (delta images) the pixels of the base image; runs wrap at the row end. */
int image_decode_4bpp(const uint8_t *src, size_t src_size, const uint8_t *base, uint8_t *dst, size_t dst_size)
{
    if (src_size < NIBBLE_START / 2) return GDAT_ERR_DATA;
    unsigned width = image_dim(src, IMAGE_WIDTH), row = (width + 1) & ~1u;
    size_t total = (size_t)row * image_dim(src, IMAGE_HEIGHT);
    if (total / 2 > dst_size) return GDAT_ERR_SPACE;
    nibbles_t n = { src, NIBBLE_START, src_size * 2, 0 };
    int colors = base ? DELTA_LOCAL_COLORS : LOCAL_COLORS;
    int local[LOCAL_COLORS];
    for (int i = 0; i < colors; i++) local[i] = next_nibble(&n);
    size_t pos = 0;
    unsigned x = 0;
    while (pos < total) {
        int code = next_nibble(&n), kind = code & CMD_KIND_MASK, color = 0;
        if (kind == CMD_COLOR_NIBBLE) color = next_nibble(&n);
        else if (kind < colors) color = local[kind];
        unsigned count = code & CMD_RUN ? run_length(&n) : 1;
        if (n.bad) return GDAT_ERR_DATA;
        for (; count > 0; count--) {
            if (pos >= total || (kind == CMD_COPY_ABOVE && pos < row)) return GDAT_ERR_DATA;
            if (kind == CMD_COPY_ABOVE) color = get_pixel(dst, pos - row);
            else if (base && kind == CMD_COPY_BASE) color = get_pixel(base, pos);
            set_pixel(dst, pos++, color);
            if (++x == width) {
                x = 0;
                pos += row - width;
            }
        }
    }
    return GDAT_OK;
}

/* LZSS after reDM2 docs/formats.md "Compression": flag bits from bit 0, 1 = literal byte;
   a reference holds length - 3 in its low length_bits and the distance back in the other bits. */
static int lzss_decode(const uint8_t *src, size_t src_size, uint8_t *dst, size_t dst_size, int length_bits)
{
    size_t in = 0, out = 0;
    int flags = 0, flags_left = 0;
    for (;;) {
        if (flags_left == 0) {
            if (in >= src_size) break;
            flags = src[in++];
            flags_left = FLAG_BITS;
        }
        int literal = flags & 1;
        flags >>= 1;
        flags_left--;
        if (literal) {
            if (in >= src_size) break;
            if (out >= dst_size) return GDAT_ERR_DATA;
            dst[out++] = src[in++];
            continue;
        }
        if (src_size - in < 2) break;
        size_t length = (src[in] & ((1u << length_bits) - 1)) + LZSS_MIN_MATCH;
        size_t distance = (size_t)src[in + 1] << (FLAG_BITS - length_bits) | src[in] >> length_bits;
        in += 2;
        if (distance == 0 || distance > out || length > dst_size - out) return GDAT_ERR_DATA;
        for (; length > 0; length--, out++) dst[out] = dst[out - distance];
    }
    return (int)out;
}

int image_decode_8bpp(const uint8_t *src, size_t src_size, uint8_t *dst, size_t dst_size)
{
    if (src_size < LZSS_DATA) return GDAT_ERR_DATA;
    size_t bytes = image_bytes(image_dim(src, IMAGE_WIDTH), image_dim(src, IMAGE_HEIGHT), 8);
    if (bytes > dst_size) return GDAT_ERR_SPACE;
    int length_bits;
    switch (src[CODEC_OFFSET]) {
    case CODEC_LZSS_4_4: length_bits = 4; break;
    case CODEC_LZSS_5_3: length_bits = 5; break;
    default: return GDAT_ERR_DATA;
    }
    int out = lzss_decode(src + LZSS_DATA, src_size - LZSS_DATA, dst, bytes, length_bits);
    return out == (int)bytes ? GDAT_OK : GDAT_ERR_DATA;
}

uint16_t gdat_entry_index(const gdat_t *g, int category, int index, int type, int entry)
{
    int value = gdat_find(g, category, index, type, entry);
    if (value >= 0) return (uint16_t)value;
    return type == GDAT_TYPE_VALUE || type == GDAT_TYPE_VALUE2 ? 0 : GDAT_NO_ITEM;
}

int gdat_exists(const gdat_t *g, int category, int index, int type, int entry)
{
    return gdat_find(g, category, index, type, entry) >= 0;
}

char *gdat_text(const gdat_t *g, int category, int index, int entry, char *dst, size_t dst_size)
{
    uint32_t size;
    const uint8_t *text = gdat_item(g, gdat_find(g, category, index, GDAT_TYPE_TEXT, entry), &size);
    int encrypted = gdat_entry_index(g, FLAGS_CATEGORY, FLAGS_INDEX, GDAT_TYPE_VALUE, FLAGS_ENTRY) & FLAG_ENCRYPTED_TEXT;
    size_t n = 0;
    for (; text && n < size && n + 1 < dst_size; n++) {
        uint8_t c = encrypted ? (uint8_t)((text[n] ^ TEXT_INVERT) - n) : text[n];
        if (!c) break;
        dst[n] = (char)c;
    }
    dst[n] = '\0';
    return dst;
}

int text_find_int_field(const char *text, const char *key)
{
    int value = 0;  /* like the original, a second match goes on with the digits of the first */
    for (const char *p = strstr(text, key); p; p = strstr(p, key)) {
        p += strlen(key);
        if (*p == '=') p++;
        int negative = *p == '-';
        if (negative) p++;
        for (; *p >= '0' && *p <= '9'; p++) value = value * 10 + (*p - '0');
        if (negative) value = -value;
    }
    return value;
}

int gdat_image_offset(const gdat_t *g, int category, int index, int entry)
{
    uint32_t size;
    const uint8_t *data = gdat_item(g, gdat_find(g, category, index, GDAT_TYPE_IMAGE, entry), &size);
    if (!data || size < LZSS_DATA) return 0;
    int x = (int16_t)le16(data + IMAGE_WIDTH) >> IMAGE_OFFSET_SHIFT;
    int y = (int16_t)le16(data + IMAGE_HEIGHT) >> IMAGE_OFFSET_SHIFT;
    if (x == OFFSET_IN_KEYS) return gdat_entry_index(g, category, index, GDAT_TYPE_VALUE2, entry);
    if (y == FORMAT_8BPP) {
        x = (int8_t)data[IMAGE_8BPP_OFFSET_X];
        y = (int8_t)data[IMAGE_8BPP_OFFSET_Y];
    } else if (y == OFFSET_IN_KEYS) {
        y = (int8_t)data[IMAGE_8BPP_OFFSET_Y];  /* an uncompressed image: the high byte of its bpp */
    }
    return x * 256 + (uint8_t)y;
}
