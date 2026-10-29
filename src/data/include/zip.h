/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_DATA_ZIP_H
#define DM2_DATA_ZIP_H
/* Prefixes: zip_ */

/* ZIP archives (PKWARE APPNOTE): the central directory and the stored and deflated entries of an archive with the
   game files. It reads the archive only; it does not write, and it does not read ZIP64, encrypted or split archives. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The compression methods that the reader decodes. */
#define ZIP_STORED   0
#define ZIP_DEFLATED 8

typedef struct Zip Zip; /* an open archive: its file and its central directory */

/* An entry of the central directory. */
typedef struct {
    uint32_t local_offset; /* the offset of the local header */
    uint32_t compressed_size;
    uint32_t size;
    uint16_t method; /* ZIP_STORED or ZIP_DEFLATED */
} ZipEntry;

/* Opening */
/* Opens the archive and reads its central directory; NULL when the file is no ZIP archive or is damaged;
   exported for the tests. */
Zip *zip_open(const char *path);
/* Closes the archive and frees it; exported for the tests. */
void zip_close(Zip *zip);

/* Entries */
/* Writes the root folder of the game files into root ("" or "<folder>/"): the prefix of the first entry in name
   order whose name is marker or ends with "/" marker, without case; false when no entry has it;
   exported for the tests. */
bool zip_find_root(const Zip *zip, const char *marker, char *root, size_t size);
/* Finds the entry with the name, without case; false when it is missing; exported for the tests. */
bool zip_find(const Zip *zip, const char *name, ZipEntry *entry);
/* Reads the entry into data; false unless it is stored or deflated, its size is size, and it decodes to exactly
   size bytes; exported for the tests. */
bool zip_read(const Zip *zip, const ZipEntry *entry, uint8_t *data, uint32_t size);

#endif /* DM2_DATA_ZIP_H */
