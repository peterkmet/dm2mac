/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "zip.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tinf/tinf.h>
#include "le.h"
#include "os.h"

/* The signatures of the records. */
#define ZIP_END_SIGNATURE     0x06054b50
#define ZIP_CENTRAL_SIGNATURE 0x02014b50
#define ZIP_LOCAL_SIGNATURE   0x04034b50

/* The ZIP64 marker in a size or an offset. */
#define ZIP64_VALUE 0xffffffff

/* The ZIP64 marker in the entry count. */
#define ZIP64_ENTRIES 0xffff

/* The bits of the general purpose flags. */
#define ZIP_FLAG_ENCRYPTED 0x0001

/* The sizes of the records without their names, extra fields and comments. */
#define ZIP_END_SIZE        22
#define ZIP_END_COMMENT_MAX 65535
#define ZIP_CENTRAL_SIZE    46
#define ZIP_LOCAL_SIZE      30

/* The offsets in the end of central directory record. */
#define ZIP_END_AT_COUNT            10
#define ZIP_END_AT_DIRECTORY_SIZE   12
#define ZIP_END_AT_DIRECTORY_OFFSET 16
#define ZIP_END_AT_COMMENT_SIZE     20

/* The offsets in a central directory entry. */
#define ZIP_CENTRAL_AT_GENERAL_PURPOSE 8
#define ZIP_CENTRAL_AT_METHOD          10
#define ZIP_CENTRAL_AT_COMPRESSED_SIZE 20
#define ZIP_CENTRAL_AT_SIZE            24
#define ZIP_CENTRAL_AT_NAME_SIZE       28
#define ZIP_CENTRAL_AT_EXTRA_SIZE      30
#define ZIP_CENTRAL_AT_COMMENT_SIZE    32
#define ZIP_CENTRAL_AT_LOCAL_OFFSET    42

/* The offsets in a local header. */
#define ZIP_LOCAL_AT_GENERAL_PURPOSE 6
#define ZIP_LOCAL_AT_NAME_SIZE       26
#define ZIP_LOCAL_AT_EXTRA_SIZE      28

struct Zip {
    FILE *file;
    int64_t file_size;
    uint8_t *directory; /* the central directory, owned */
    size_t directory_size;
    int entry_count;
};

/* A central directory entry with its name and the offset of the next entry. */
typedef struct {
    const uint8_t *name; /* in the directory, not terminated */
    size_t name_size;
    uint16_t flags;
    ZipEntry entry;
    size_t next;
} DirectoryEntry;

/* Reads count bytes at offset of the file. */
static bool read_at(FILE *fp, int64_t offset, void *buffer, size_t count)
{
    return !fseek(fp, offset, SEEK_SET) && fread(buffer, 1, count, fp) == count;
}

/* Reads the central directory entry at the offset; false when it does not fit into the directory. */
static bool entry_at(const Zip *zip, size_t at, DirectoryEntry *out)
{
    if (at > zip->directory_size || zip->directory_size - at < ZIP_CENTRAL_SIZE)
        return false;
    const uint8_t *p = zip->directory + at;
    if (le32(p) != ZIP_CENTRAL_SIGNATURE)
        return false;
    size_t name_size = le16(p + ZIP_CENTRAL_AT_NAME_SIZE);
    size_t variable_size = name_size + le16(p + ZIP_CENTRAL_AT_EXTRA_SIZE) + le16(p + ZIP_CENTRAL_AT_COMMENT_SIZE);
    if (zip->directory_size - at - ZIP_CENTRAL_SIZE < variable_size)
        return false;
    out->name = p + ZIP_CENTRAL_SIZE;
    out->name_size = name_size;
    out->flags = le16(p + ZIP_CENTRAL_AT_GENERAL_PURPOSE);
    out->entry.local_offset = le32(p + ZIP_CENTRAL_AT_LOCAL_OFFSET);
    out->entry.compressed_size = le32(p + ZIP_CENTRAL_AT_COMPRESSED_SIZE);
    out->entry.size = le32(p + ZIP_CENTRAL_AT_SIZE);
    out->entry.method = le16(p + ZIP_CENTRAL_AT_METHOD);
    out->next = at + ZIP_CENTRAL_SIZE + variable_size;
    return true;
}

/* Opening */

/* Finds the end record in the last bytes of the file: the last signature whose comment ends inside them. */
static bool find_end(const uint8_t *tail, size_t size, size_t *at)
{
    for (size_t i = size - ZIP_END_SIZE + 1; i-- > 0;) {
        if (le32(tail + i) == ZIP_END_SIGNATURE &&
            i + ZIP_END_SIZE + le16(tail + i + ZIP_END_AT_COMMENT_SIZE) <= size) {
            *at = i;
            return true;
        }
    }
    return false;
}

/* Reads the end record and the central directory into memory; false for a ZIP64 archive or a bad offset. */
static bool read_directory(Zip *zip)
{
    if (zip->file_size < ZIP_END_SIZE)
        return false;
    size_t tail_size =
        (size_t)(zip->file_size < ZIP_END_SIZE + ZIP_END_COMMENT_MAX ? zip->file_size
                                                                     : ZIP_END_SIZE + ZIP_END_COMMENT_MAX);
    int64_t tail_offset = zip->file_size - (int64_t)tail_size;
    uint8_t *tail = malloc(tail_size);
    if (!tail)
        os_fatal("out of memory");
    size_t end_at = 0;
    bool ok = read_at(zip->file, tail_offset, tail, tail_size) && find_end(tail, tail_size, &end_at);
    uint32_t directory_size = ok ? le32(tail + end_at + ZIP_END_AT_DIRECTORY_SIZE) : 0;
    uint32_t directory_offset = ok ? le32(tail + end_at + ZIP_END_AT_DIRECTORY_OFFSET) : 0;
    uint16_t count = ok ? le16(tail + end_at + ZIP_END_AT_COUNT) : 0;
    free(tail);
    if (!ok || count == ZIP64_ENTRIES || directory_size == ZIP64_VALUE || directory_offset == ZIP64_VALUE ||
        (int64_t)directory_offset + directory_size > tail_offset + (int64_t)end_at)
        return false;
    zip->entry_count = count;
    zip->directory_size = directory_size;
    zip->directory = malloc(directory_size ? directory_size : 1);
    if (!zip->directory)
        os_fatal("out of memory");
    return read_at(zip->file, directory_offset, zip->directory, directory_size);
}

/* Checks that every entry fits into the directory and is neither encrypted nor ZIP64. */
static bool check_directory(const Zip *zip)
{
    size_t at = 0;
    for (int i = 0; i < zip->entry_count; i++) {
        DirectoryEntry e;
        if (!entry_at(zip, at, &e) || (e.flags & ZIP_FLAG_ENCRYPTED) || e.entry.local_offset == ZIP64_VALUE ||
            e.entry.compressed_size == ZIP64_VALUE || e.entry.size == ZIP64_VALUE)
            return false;
        at = e.next;
    }
    return true;
}

Zip *zip_open(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return NULL;
    Zip *zip = calloc(1, sizeof *zip);
    if (!zip)
        os_fatal("out of memory");
    zip->file = fp;
    zip->file_size = -1;
    if (!fseek(fp, 0, SEEK_END))
        zip->file_size = ftell(fp);
    if (!read_directory(zip) || !check_directory(zip)) {
        zip_close(zip);
        return NULL;
    }
    return zip;
}

void zip_close(Zip *zip)
{
    if (!zip)
        return;
    fclose(zip->file);
    free(zip->directory);
    free(zip);
}

/* Entries */

/* Compares an entry name with a C string without case. */
static bool is_same_name(const uint8_t *name, size_t name_size, const char *text, size_t text_size)
{
    if (name_size != text_size)
        return false;
    for (size_t i = 0; i < name_size; i++) {
        if (tolower(name[i]) != tolower((unsigned char)text[i]))
            return false;
    }
    return true;
}

/* Whether the entry name is the marker or ends with "/" and the marker, without case. */
static bool is_root_entry(const DirectoryEntry *e, const char *marker, size_t marker_size)
{
    if (e->name_size < marker_size)
        return false;
    size_t prefix_size = e->name_size - marker_size;
    return is_same_name(e->name + prefix_size, marker_size, marker, marker_size) &&
           (prefix_size == 0 || e->name[prefix_size - 1] == '/');
}

/* Whether the name of a comes before the name of b in byte order. */
static bool is_name_before(const DirectoryEntry *a, const DirectoryEntry *b)
{
    size_t common = a->name_size < b->name_size ? a->name_size : b->name_size;
    int order = memcmp(a->name, b->name, common);
    return order < 0 || (order == 0 && a->name_size < b->name_size);
}

bool zip_find_root(const Zip *zip, const char *marker, char *root, size_t size)
{
    size_t marker_size = strlen(marker);
    DirectoryEntry first = {0};
    bool found = false;
    DirectoryEntry e;
    size_t at = 0;
    for (int i = 0; i < zip->entry_count && entry_at(zip, at, &e); i++) {
        if (is_root_entry(&e, marker, marker_size) && (!found || is_name_before(&e, &first))) {
            first = e;
            found = true;
        }
        at = e.next;
    }
    if (!found || first.name_size - marker_size >= size)
        return false;
    size_t prefix_size = first.name_size - marker_size;
    memcpy(root, first.name, prefix_size);
    root[prefix_size] = '\0';
    return true;
}

bool zip_find(const Zip *zip, const char *name, ZipEntry *entry)
{
    size_t name_size = strlen(name);
    DirectoryEntry e;
    size_t at = 0;
    for (int i = 0; i < zip->entry_count && entry_at(zip, at, &e); i++) {
        if (is_same_name(e.name, e.name_size, name, name_size)) {
            *entry = e.entry;
            return true;
        }
        at = e.next;
    }
    return false;
}

/* Reads the local header of the entry and gives the offset of its data; false when it is outside the file. */
static bool find_local_data(const Zip *zip, const ZipEntry *entry, int64_t *data_at)
{
    uint8_t header[ZIP_LOCAL_SIZE];
    if (zip->file_size - entry->local_offset < ZIP_LOCAL_SIZE ||
        !read_at(zip->file, entry->local_offset, header, sizeof header) || le32(header) != ZIP_LOCAL_SIGNATURE ||
        (le16(header + ZIP_LOCAL_AT_GENERAL_PURPOSE) & ZIP_FLAG_ENCRYPTED))
        return false;
    *data_at = (int64_t)entry->local_offset + ZIP_LOCAL_SIZE + le16(header + ZIP_LOCAL_AT_NAME_SIZE) +
               le16(header + ZIP_LOCAL_AT_EXTRA_SIZE);
    return zip->file_size - *data_at >= entry->compressed_size;
}

/* Inflates the deflate data at the offset into exactly size bytes. */
static bool inflate_entry(const Zip *zip, int64_t data_at, uint32_t compressed_size, uint8_t *data, uint32_t size)
{
    uint8_t *compressed = malloc(compressed_size ? compressed_size : 1);
    if (!compressed)
        os_fatal("out of memory");
    unsigned int out_size = size;
    bool ok = read_at(zip->file, data_at, compressed, compressed_size) &&
              tinf_uncompress(data, &out_size, compressed, compressed_size) == TINF_OK && out_size == size;
    free(compressed);
    return ok;
}

bool zip_read(const Zip *zip, const ZipEntry *entry, uint8_t *data, uint32_t size)
{
    int64_t data_at = 0;
    if (entry->size != size || (entry->method != ZIP_STORED && entry->method != ZIP_DEFLATED) ||
        (entry->method == ZIP_STORED && entry->compressed_size != size) || !find_local_data(zip, entry, &data_at))
        return false;
    if (entry->method == ZIP_STORED)
        return read_at(zip->file, data_at, data, size);
    return inflate_entry(zip, data_at, entry->compressed_size, data, size);
}
