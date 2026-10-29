/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_DATA_GAMEFILE_H
#define DM2_DATA_GAMEFILE_H
/* Prefixes: gamefile_ */

/* The table of the game files and the check of a file against it. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The game files. */
#define GAMEFILE_COUNT  10
/* The game file that marks the root of the game files in a ZIP archive. */
#define GAMEFILE_MARKER "DATA/GRAPHICS.DAT"

/* A game file: its path below the game folder, its size, and its CRC-32. */
typedef struct {
    const char *name;
    uint32_t size;
    uint32_t crc;
} GameFile;

/* The 10 game files of the English PC DOS version of 1995. */
const GameFile *gamefile_table(void); /* exported for the tests */
/* Whether data holds the file: its size and its CRC-32. */
bool gamefile_is_valid(const GameFile *file, const uint8_t *data, size_t size); /* exported for the tests */
/* Reads folder/<file name> and checks it; false when the file is missing, has another size, or fails the CRC. */
bool gamefile_is_valid_in(const GameFile *file, const char *folder); /* exported for the tests */
/* The first file of files[GAMEFILE_COUNT] that is missing or fails in folder; NULL when all pass. */
const GameFile *gamefile_find_bad(const GameFile *files, const char *folder); /* exported for the tests */

#endif
