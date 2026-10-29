/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "crc32.h"
#include "le.h"

/* The reflected CRC-32 polynomial of PNG and ZIP. */
#define CRC32_POLYNOMIAL 0xedb88320u

/* The number of byte values. */
#define CRC_TABLE_SIZE 256

/* The CRC-32 of each byte value; filled on the first use. */
static struct {
    uint32_t table[CRC_TABLE_SIZE];
} crc32;

uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
    if (!crc32.table[1])
        for (uint32_t i = 0; i < CRC_TABLE_SIZE; i++) {
            uint32_t c = i;
            for (int k = 0; k < BYTE_BIT_COUNT; k++)
                c = c & 1 ? CRC32_POLYNOMIAL ^ c >> 1 : c >> 1;
            crc32.table[i] = c;
        }
    crc = ~crc;
    while (n--)
        crc = crc32.table[(crc ^ *p++) & BYTE_MASK] ^ crc >> BYTE_BIT_COUNT;
    return ~crc;
}
