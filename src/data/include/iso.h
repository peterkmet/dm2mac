/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_DATA_ISO_H
#define DM2_DATA_ISO_H
/* Prefixes: iso_ */

/* ISO 9660 CD images (ECMA-119): finds and reads the files of the game on a CD image; it never writes to the image. */

#include <stdbool.h>
#include <stdint.h>

typedef struct Iso Iso; /* an open CD image: its file, the sector layout, and the root folder */

/* The result of opening a CD image. */
typedef enum { ISO_OPEN_OK, ISO_OPEN_DAMAGED, ISO_OPEN_MODE2 } IsoOpenResult;

/* A file of the image: its first sector and its size. */
typedef struct {
    uint32_t sector;
    uint32_t size;
} IsoFile;

/* Setup */
/* Opens the image; *iso is NULL unless the result is ISO_OPEN_OK; exported for the tests. */
IsoOpenResult iso_open(const char *path, Iso **iso);
/* Closes the image and frees it; exported for the tests. */
void iso_close(Iso *iso);

/* Queries */
/* Finds the file at a path below the root ("DATA/GRAPHICS.DAT"), without case and without a ";1" suffix;
   exported for the tests. */
bool iso_find(const Iso *iso, const char *path, IsoFile *file);
/* Reads the file into data; false unless its size is size and all its sectors are inside the image;
   exported for the tests. */
bool iso_read(const Iso *iso, const IsoFile *file, uint8_t *data, uint32_t size);

#endif /* DM2_DATA_ISO_H */
