/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mve.h"
#include <string.h>
#include "le.h"
#include "mve_internal.h"

/* The video data header: its size and the offset of its flags. */
#define VIDEO_HEADER_SIZE 14 /* frame numbers, position, size, and flags before the blocks */
#define VIDEO_FLAGS_AT    12

/* The flag of the video data header. */
#define VIDEO_FLAG_SWAP 0x0001 /* the shown frame becomes the previous frame before the decode */

/* Sizes and counts of the blocks and their parts. */
#define ENCODINGS_PER_BYTE   2 /* the decoding map holds 4 bits per block */
#define HALF_BLOCK           (MVE_BLOCK_SIZE / 2)
#define BLOCK_PIXELS         (MVE_BLOCK_SIZE * MVE_BLOCK_SIZE)
#define CELL_SIDE            2 /* pixels per side of a 2 × 2 cell */
#define CELLS_PER_ROW        (MVE_BLOCK_SIZE / CELL_SIDE)
#define CELLS_PER_BLOCK      (CELLS_PER_ROW * CELLS_PER_ROW)
#define QUADRANT_COUNT       4
#define QUADRANT_PIXELS      (BLOCK_PIXELS / QUADRANT_COUNT)
#define HALF_PIXELS          (BLOCK_PIXELS / 2)
#define TWO_COLORS           2
#define FOUR_COLORS          4
#define FOUR_COLOR_BIT_COUNT 2 /* bits per pixel or cell for one of four colors */

/* The motion vectors of encodings 2 to 4. */
#define NEAR_MOTION_LIMIT      56 /* encodings 2 and 3: a smaller byte reaches the next 7 columns */
#define NEAR_MOTION_COLUMNS    7
#define FAR_MOTION_COLUMNS     29
#define FAR_MOTION_LEFT        14
#define PREVIOUS_MOTION_CENTER 8 /* encoding 4: a nibble 0-15 gives -8 to 7 */

/* The encodings of a block. */
#define ENC_PREVIOUS         0x0
#define ENC_UNCHANGED        0x1
#define ENC_NEAR_AHEAD       0x2
#define ENC_NEAR_BEHIND      0x3
#define ENC_PREVIOUS_NEAR    0x4
#define ENC_PREVIOUS_FAR     0x5
#define ENC_UNUSED           0x6
#define ENC_TWO_COLORS       0x7
#define ENC_TWO_COLOR_PARTS  0x8
#define ENC_FOUR_COLORS      0x9
#define ENC_FOUR_COLOR_PARTS 0xa
#define ENC_RAW              0xb
#define ENC_CELLS            0xc
#define ENC_QUADRANTS        0xd
#define ENC_SOLID            0xe
#define ENC_DITHER           0xf

/* The block data of one video opcode; a read past its end gives 0 and marks the data short. */
typedef struct {
    const uint8_t *p, *end;
    bool short_data;
} Reader;

/* The block that the decode draws. */
typedef struct {
    MveStream *m;
    Reader r;
    uint8_t *dst; /* its top left pixel in the current frame */
    int x, y;     /* its position in pixels */
} Block;

/* A rectangle of pixels in a block. */
typedef struct {
    int x, y, w, h;
} Rect;

/* A color list and the bits that select from it. */
typedef struct {
    const uint8_t *colors;
    uint64_t bits;
} Palette;

/* A motion vector: the source block is dx, dy pixels from the block. */
typedef struct {
    int dx, dy;
} Motion;

/* Cells of cell_w × cell_h pixels in cols × rows from (x, y) of a block. */
typedef struct {
    int x, y, cols, rows, cell_w, cell_h;
} Grid;

/* The quadrants of encodings 8 and 10 in column order, and of encoding 13 in row order. */
static const int column_quadrant_x[QUADRANT_COUNT] = {0, 0, HALF_BLOCK, HALF_BLOCK};
static const int column_quadrant_y[QUADRANT_COUNT] = {0, HALF_BLOCK, 0, HALF_BLOCK};
static const int row_quadrant_x[QUADRANT_COUNT] = {0, HALF_BLOCK, 0, HALF_BLOCK};
static const int row_quadrant_y[QUADRANT_COUNT] = {0, 0, HALF_BLOCK, HALF_BLOCK};

/* Reads the next byte of the block data; 0 and the short flag past its end. */
static uint8_t get8(Reader *r)
{
    if (r->p >= r->end) {
        r->short_data = true;
        return 0;
    }
    return *r->p++;
}

/* Reads count bytes as one little-endian number. */
static uint64_t get_bits(Reader *r, int count)
{
    uint64_t bits = 0;
    for (int i = 0; i < count; i++)
        bits |= (uint64_t)get8(r) << (i * BYTE_BIT_COUNT);
    return bits;
}

static void get_colors(Reader *r, uint8_t *colors, int count)
{
    for (int i = 0; i < count; i++)
        colors[i] = get8(r);
}

/* Paints a rectangle of the block with one color. */
static void fill(Block *b, Rect r, uint8_t color)
{
    for (int row = r.y; row < r.y + r.h; row++)
        memset(b->dst + row * b->m->width + r.x, color, r.w);
}

/* Gives each cell of the grid the color that the next bits_per_cell bits select. */
static void paint_pattern(Block *b, Grid g, const uint8_t *colors, uint64_t bits, int bits_per_cell)
{
    uint64_t mask = (1u << bits_per_cell) - 1;
    for (int row = 0; row < g.rows; row++)
        for (int col = 0; col < g.cols; col++, bits >>= bits_per_cell)
            fill(b, (Rect){g.x + col * g.cell_w, g.y + row * g.cell_h, g.cell_w, g.cell_h}, colors[bits & mask]);
}

/* Draws the left and right halves (vertical) or the top and bottom halves of the block, each with its colors. */
static void paint_halves(Block *b, bool vertical, Palette first, Palette second, int bits_per_cell)
{
    Grid g = vertical ? (Grid){0, 0, HALF_BLOCK, MVE_BLOCK_SIZE, 1, 1} : (Grid){0, 0, MVE_BLOCK_SIZE, HALF_BLOCK, 1, 1};
    paint_pattern(b, g, first.colors, first.bits, bits_per_cell);
    if (vertical)
        g.x = HALF_BLOCK;
    else
        g.y = HALF_BLOCK;
    paint_pattern(b, g, second.colors, second.bits, bits_per_cell);
}

/* Copies the block at (dx, dy) from its place in frame; a source outside the frame leaves the block unchanged. */
static void copy_from(Block *b, const uint8_t *frame, int dx, int dy)
{
    MveStream *m = b->m;
    int sx = b->x + dx;
    int sy = b->y + dy;
    if (sx < 0 || sy < 0 || sx + MVE_BLOCK_SIZE > m->width || sy + MVE_BLOCK_SIZE > m->height) {
        m->bad_blocks++;
        return;
    }
    for (int row = 0; row < MVE_BLOCK_SIZE; row++)
        memmove(b->dst + row * m->width, frame + (sy + row) * m->width + sx, MVE_BLOCK_SIZE);
}

/* The motion of encodings 2 and 3: the next 7 columns of the same rows, or 29 columns of the next rows. */
static Motion near_motion(uint8_t v)
{
    if (v < NEAR_MOTION_LIMIT)
        return (Motion){MVE_BLOCK_SIZE + v % NEAR_MOTION_COLUMNS, v / NEAR_MOTION_COLUMNS};
    return (Motion){-FAR_MOTION_LEFT + (v - NEAR_MOTION_LIMIT) % FAR_MOTION_COLUMNS,
                    MVE_BLOCK_SIZE + (v - NEAR_MOTION_LIMIT) / FAR_MOTION_COLUMNS};
}

/* Encoding 7: two colors, one bit per pixel, or per 2 × 2 cell when the first color is the higher one. */
static void decode_two_colors(Block *b)
{
    uint8_t p[TWO_COLORS];
    get_colors(&b->r, p, TWO_COLORS);
    if (p[0] <= p[1])
        paint_pattern(b, (Grid){0, 0, MVE_BLOCK_SIZE, MVE_BLOCK_SIZE, 1, 1}, p, get_bits(&b->r, sizeof(uint64_t)), 1);
    else
        paint_pattern(b, (Grid){0, 0, CELLS_PER_ROW, CELLS_PER_ROW, CELL_SIDE, CELL_SIDE}, p,
                      get_bits(&b->r, sizeof(uint16_t)), 1);
}

/* Encoding 9: four colors per pixel, or per cell of 2 × 2, 2 × 1, or 1 × 2 pixels. */
static void decode_four_colors(Block *b)
{
    uint8_t p[FOUR_COLORS];
    get_colors(&b->r, p, FOUR_COLORS);
    bool second_low = p[TWO_COLORS] <= p[TWO_COLORS + 1]; /* the order of P2 and P3 */
    if (p[0] <= p[1] && second_low) {
        uint64_t top = get_bits(&b->r, sizeof(uint64_t));
        paint_halves(b, false, (Palette){p, top}, (Palette){p, get_bits(&b->r, sizeof(uint64_t))},
                     FOUR_COLOR_BIT_COUNT);
    } else if (p[0] <= p[1]) {
        paint_pattern(b, (Grid){0, 0, CELLS_PER_ROW, CELLS_PER_ROW, CELL_SIDE, CELL_SIDE}, p,
                      get_bits(&b->r, sizeof(uint32_t)), FOUR_COLOR_BIT_COUNT);
    } else if (second_low) {
        paint_pattern(b, (Grid){0, 0, CELLS_PER_ROW, MVE_BLOCK_SIZE, CELL_SIDE, 1}, p,
                      get_bits(&b->r, sizeof(uint64_t)), FOUR_COLOR_BIT_COUNT);
    } else {
        paint_pattern(b, (Grid){0, 0, MVE_BLOCK_SIZE, CELLS_PER_ROW, 1, CELL_SIDE}, p,
                      get_bits(&b->r, sizeof(uint64_t)), FOUR_COLOR_BIT_COUNT);
    }
}

/* Encodings 8 and 10: count colors (2 or 4) per quadrant, or per half of the block. */
static void decode_color_parts(Block *b, int count, int bits_per_cell)
{
    int quadrant_bytes = QUADRANT_PIXELS * bits_per_cell / BYTE_BIT_COUNT;
    int half_bytes = HALF_PIXELS * bits_per_cell / BYTE_BIT_COUNT;
    uint8_t p[FOUR_COLORS * 2];
    get_colors(&b->r, p, count);
    if (p[0] <= p[1]) {
        for (int q = 0; q < QUADRANT_COUNT; q++) {
            if (q)
                get_colors(&b->r, p, count);
            Grid g = {column_quadrant_x[q], column_quadrant_y[q], HALF_BLOCK, HALF_BLOCK, 1, 1};
            paint_pattern(b, g, p, get_bits(&b->r, quadrant_bytes), bits_per_cell);
        }
        return;
    }
    uint64_t first = get_bits(&b->r, half_bytes);
    get_colors(&b->r, p + count, count);
    paint_halves(b, p[count] <= p[count + 1], (Palette){p, first}, (Palette){p + count, get_bits(&b->r, half_bytes)},
                 bits_per_cell);
}

/* Encodings 11 to 15: raw pixels, 2 × 2 cells, quadrants, one color, and a checkerboard. */
static void decode_plain_colors(Block *b, int encoding)
{
    uint8_t p[QUADRANT_COUNT];
    switch (encoding) {
    case ENC_RAW:
        for (int i = 0; i < BLOCK_PIXELS; i++)
            fill(b, (Rect){i % MVE_BLOCK_SIZE, i / MVE_BLOCK_SIZE, 1, 1}, get8(&b->r));
        break;
    case ENC_CELLS:
        for (int i = 0; i < CELLS_PER_BLOCK; i++)
            fill(b, (Rect){i % CELLS_PER_ROW * CELL_SIDE, i / CELLS_PER_ROW * CELL_SIDE, CELL_SIDE, CELL_SIDE},
                 get8(&b->r));
        break;
    case ENC_QUADRANTS:
        for (int q = 0; q < QUADRANT_COUNT; q++)
            fill(b, (Rect){row_quadrant_x[q], row_quadrant_y[q], HALF_BLOCK, HALF_BLOCK}, get8(&b->r));
        break;
    case ENC_SOLID:
        fill(b, (Rect){0, 0, MVE_BLOCK_SIZE, MVE_BLOCK_SIZE}, get8(&b->r));
        break;
    default: /* ENC_DITHER */
        get_colors(&b->r, p, TWO_COLORS);
        for (int i = 0; i < BLOCK_PIXELS; i++) {
            int x = i % MVE_BLOCK_SIZE;
            int y = i / MVE_BLOCK_SIZE;
            fill(b, (Rect){x, y, 1, 1}, p[(x + y) & 1]);
        }
    }
}

/* Draws one block by its encoding. */
static void decode_block(Block *b, int encoding)
{
    MveStream *m = b->m;
    const uint8_t *previous = m->buffers[m->current ^ 1];
    const uint8_t *current = m->buffers[m->current];
    Motion motion;
    uint8_t v;
    switch (encoding) {
    case ENC_PREVIOUS:
        copy_from(b, previous, 0, 0);
        break;
    case ENC_UNCHANGED:
        break;
    case ENC_NEAR_AHEAD:
    case ENC_NEAR_BEHIND:
        motion = near_motion(get8(&b->r));
        if (encoding == ENC_NEAR_BEHIND)
            motion = (Motion){-motion.dx, -motion.dy};
        copy_from(b, current, motion.dx, motion.dy);
        break;
    case ENC_PREVIOUS_NEAR:
        v = get8(&b->r);
        copy_from(b, previous, (v & NIBBLE_MASK) - PREVIOUS_MOTION_CENTER,
                  (v >> NIBBLE_BIT_COUNT) - PREVIOUS_MOTION_CENTER);
        break;
    case ENC_PREVIOUS_FAR:
        motion.dx = (int8_t)get8(&b->r);
        motion.dy = (int8_t)get8(&b->r);
        copy_from(b, previous, motion.dx, motion.dy);
        break;
    case ENC_UNUSED:
        m->bad_blocks++;
        break;
    case ENC_TWO_COLORS:
        decode_two_colors(b);
        break;
    case ENC_TWO_COLOR_PARTS:
        decode_color_parts(b, TWO_COLORS, 1);
        break;
    case ENC_FOUR_COLORS:
        decode_four_colors(b);
        break;
    case ENC_FOUR_COLOR_PARTS:
        decode_color_parts(b, FOUR_COLORS, FOUR_COLOR_BIT_COUNT);
        break;
    default:
        decode_plain_colors(b, encoding);
    }
}

bool mve_decode_video(MveStream *m, const uint8_t *video, int video_size)
{
    int columns = m->width / MVE_BLOCK_SIZE;
    int rows = m->height / MVE_BLOCK_SIZE;
    if (video_size < VIDEO_HEADER_SIZE || !columns || m->map_size * ENCODINGS_PER_BYTE < columns * rows)
        return false;
    if (le16(video + VIDEO_FLAGS_AT) & VIDEO_FLAG_SWAP)
        m->current ^= 1;
    Block b = {
        .m = m, .r = {video + VIDEO_HEADER_SIZE, video + video_size, false}
    };
    for (int i = 0; i < columns * rows && !b.r.short_data; i++) {
        b.x = i % columns * MVE_BLOCK_SIZE;
        b.y = i / columns * MVE_BLOCK_SIZE;
        b.dst = m->buffers[m->current] + b.y * m->width + b.x;
        decode_block(&b, m->map[i / ENCODINGS_PER_BYTE] >> (i % ENCODINGS_PER_BYTE * NIBBLE_BIT_COUNT) & NIBBLE_MASK);
    }
    return !b.r.short_data;
}
