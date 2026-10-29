/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_PLATFORM_SOURCE_H
#define DM2_PLATFORM_SOURCE_H
/* Prefixes: source_ */

/* The sources of the game files: a folder, a ZIP archive or a CD image. It reads a source only; it never writes to
   it. */

#include <stdbool.h>
#include <stdint.h>

/* The forms of a source, in the order of the search. */
typedef enum { SOURCE_FOLDER, SOURCE_ZIP, SOURCE_CD } SourceForm;

typedef struct Source Source; /* an open source */

/* Setup */
/* Opens a source; NULL with *problem set to a short text for the message ("not a ZIP archive or damaged",
   "not a CD image or damaged", "a MODE2 CD image: the port reads 2048-byte and MODE1 sectors only", "no
   DATA/GRAPHICS.DAT in the archive"). */
Source *source_open(SourceForm form, const char *path, const char **problem);
/* Closes the source and frees it. */
void source_close(Source *source);

/* Queries */
/* Reads the game file at name ("DATA/GRAPHICS.DAT") into data; false unless it is there with size bytes. */
bool source_read(const Source *source, const char *name, uint8_t *data, uint32_t size);
/* Whether the source has a file at name, of any size. */
bool source_has_file(const Source *source, const char *name);

#endif /* DM2_PLATFORM_SOURCE_H */
