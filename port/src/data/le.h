#ifndef LE_H
#define LE_H
#include <stdint.h>

#define LE16_BITS 16

/* Prefixes: le16 le32 put_le16 put_le32 */

/* Little-endian reads and writes of the DOS file formats. */
static inline uint16_t le16(const uint8_t *p) { return p[0] | p[1] << 8; }
static inline uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t)le16(p + 2) << LE16_BITS; }

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

#endif
