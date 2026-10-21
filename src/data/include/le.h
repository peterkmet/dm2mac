/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_DATA_LE_H
#define DM2_DATA_LE_H
/* Prefixes: le16 le32 put_le16 put_le32 put_be32 */

/* Little-endian reads and writes of the DOS file formats. */

#include <stdint.h>

#define LE16_BITS    16
/* Bit sizes and masks of bytes and nibbles, and the base of decimal numbers in texts. */
#define BYTE_BITS    8
#define BYTE_MASK    0xff
#define NIBBLE_BITS  4 /* a 4 bpp pixel */
#define NIBBLE_MASK  0xf
#define DECIMAL_BASE 10

static inline uint16_t le16(const uint8_t *p)
{
    return p[0] | p[1] << 8;
}
static inline uint32_t le32(const uint8_t *p)
{
    return le16(p) | (uint32_t)le16(p + 2) << LE16_BITS;
}

static inline void put_le16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = value >> 8;
}

static inline void put_le32(uint8_t *p, uint32_t value)
{
    put_le16(p, (uint16_t)value);
    put_le16(p + 2, value >> LE16_BITS);
}

/* Big-endian writes of the PNG files. */
static inline void put_be32(uint8_t *p, uint32_t value)
{
    for (int i = (int)sizeof value - 1; i >= 0; i--, value >>= BYTE_BITS)
        p[i] = (uint8_t)value;
}

#endif /* DM2_DATA_LE_H */
