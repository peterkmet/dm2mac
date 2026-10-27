/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_DATA_LE_H
#define DM2_DATA_LE_H
/* Prefixes: le16 le32 put_le16 put_le32 be16 put_be32 */

/* Byte order reads and writes: little-endian for the DOS file formats, big-endian for the GDAT keys and PNG. */

#include <stdint.h>

/* Bit sizes of the integers and pixels that the file formats pack. */
#define LE16_BIT_COUNT   16
#define BYTE_BIT_COUNT   8
#define NIBBLE_BIT_COUNT 4 /* a 4 bpp pixel */

/* Masks of a byte and a nibble. */
#define BYTE_MASK   0xff
#define NIBBLE_MASK 0x0f

/* The base of decimal numbers in texts. */
#define DECIMAL_BASE 10

static inline uint16_t le16(const uint8_t *p)
{
    return p[0] | p[1] << BYTE_BIT_COUNT;
}
static inline uint32_t le32(const uint8_t *p)
{
    return le16(p) | (uint32_t)le16(p + 2) << LE16_BIT_COUNT;
}

static inline void put_le16(uint8_t *p, uint16_t word)
{
    p[0] = (uint8_t)word;
    p[1] = word >> BYTE_BIT_COUNT;
}

static inline void put_le32(uint8_t *p, uint32_t dword)
{
    put_le16(p, (uint16_t)dword);
    put_le16(p + 2, dword >> LE16_BIT_COUNT);
}

/* A big-endian read of the GDAT key table. */
static inline uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(p[0] << BYTE_BIT_COUNT | p[1]);
}

/* Big-endian writes of the PNG files. */
static inline void put_be32(uint8_t *p, uint32_t dword)
{
    for (int i = (int)sizeof dword - 1; i >= 0; i--, dword >>= BYTE_BIT_COUNT)
        p[i] = (uint8_t)dword;
}

#endif /* DM2_DATA_LE_H */
