/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_DATA_CRC32_H
#define DM2_DATA_CRC32_H
/* Prefixes: crc32_ */

/* The CRC-32 of PNG and ZIP. */

#include <stddef.h>
#include <stdint.h>

/* The CRC-32 after n more bytes at p; start with 0, then feed the bytes in parts. */
uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n);

#endif
