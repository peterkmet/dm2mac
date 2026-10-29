/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "iso.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "le.h"
#include "os.h"

/* Sector sizes of an image: the user data, and a raw sector with its sync, header and error codes. */
#define SECTOR_SIZE     2048
#define RAW_SECTOR_SIZE 2352
#define RAW_SYNC_SIZE   12
#define RAW_DATA_START  16 /* the user data after the sync and the header */
#define RAW_MODE_BYTE   15

/* The modes of a raw sector. */
#define SECTOR_MODE_DATA 0x01
#define SECTOR_MODE_XA   0x02

/* The primary volume descriptor. */
#define PRIMARY_SECTOR      16
#define PRIMARY_ID_START    1
#define PRIMARY_ROOT_RECORD 156
#define STANDARD_ID         "CD001"
#define STANDARD_ID_SIZE    5

/* The type byte of the primary volume descriptor. */
#define PRIMARY_TYPE 0x01

/* The fields of a directory record. */
#define RECORD_EXTENT      2
#define RECORD_DATA_LENGTH 10
#define RECORD_FLAGS_BYTE  25
#define RECORD_NAME_LENGTH 32
#define RECORD_NAME        33

/* The flags of a directory record. */
#define RECORD_FLAG_FOLDER 0x02

/* A path below the root: a file, or a folder and a file. */
#define PATH_MAX_PARTS 2

struct Iso {
    FILE *file; /* the open image; owned */
    bool raw;   /* sectors of RAW_SECTOR_SIZE bytes */
    uint32_t sector_count;
    IsoFile root;
};

/* A name of a path part, and whether it must be a folder. */
typedef struct {
    const char *name;
    size_t length;
    bool folder;
} PathPart;

/* Whether the bytes are the sync pattern of a raw sector: a zero byte, ten bytes 0xff, a zero byte. */
static bool is_raw_sync(const uint8_t sync[RAW_SYNC_SIZE])
{
    for (int i = 1; i < RAW_SYNC_SIZE - 1; i++)
        if (sync[i] != BYTE_MASK)
            return false;
    return !sync[0] && !sync[RAW_SYNC_SIZE - 1];
}

/* The sectors that size bytes fill. */
static uint32_t sectors_of(uint32_t size)
{
    return size / SECTOR_SIZE + (size % SECTOR_SIZE != 0);
}

/* Whether the sectors of an extent with size bytes are all inside the image. */
static bool is_inside(const Iso *iso, uint32_t sector, uint32_t size)
{
    return sector < iso->sector_count && sectors_of(size) <= iso->sector_count - sector;
}

/* Reads the user data of a sector of the image. */
static bool read_sector(const Iso *iso, uint32_t sector, uint8_t data[SECTOR_SIZE])
{
    if (sector >= iso->sector_count)
        return false;
    int64_t offset = (int64_t)sector * (iso->raw ? RAW_SECTOR_SIZE : SECTOR_SIZE) + (iso->raw ? RAW_DATA_START : 0);
    return !fseek(iso->file, offset, SEEK_SET) && fread(data, 1, SECTOR_SIZE, iso->file) == SECTOR_SIZE;
}

/* Reads the raw sync, the primary descriptor and its root folder into iso. */
static IsoOpenResult read_layout(Iso *iso, int64_t image_size)
{
    uint8_t sync[RAW_SYNC_SIZE];
    if (fread(sync, 1, sizeof sync, iso->file) != sizeof sync)
        return ISO_OPEN_DAMAGED;
    iso->raw = is_raw_sync(sync);
    int64_t sector_count = image_size / (iso->raw ? RAW_SECTOR_SIZE : SECTOR_SIZE);
    iso->sector_count = sector_count > UINT32_MAX ? UINT32_MAX : (uint32_t)sector_count;
    if (iso->raw && iso->sector_count > PRIMARY_SECTOR) {
        uint8_t mode = 0;
        int64_t mode_offset = (int64_t)PRIMARY_SECTOR * RAW_SECTOR_SIZE + RAW_MODE_BYTE;
        if (fseek(iso->file, mode_offset, SEEK_SET) || fread(&mode, 1, 1, iso->file) != 1)
            return ISO_OPEN_DAMAGED;
        if (mode != SECTOR_MODE_DATA)
            return mode == SECTOR_MODE_XA ? ISO_OPEN_MODE2 : ISO_OPEN_DAMAGED;
    }
    uint8_t descriptor[SECTOR_SIZE];
    if (!read_sector(iso, PRIMARY_SECTOR, descriptor) || descriptor[0] != PRIMARY_TYPE ||
        memcmp(descriptor + PRIMARY_ID_START, STANDARD_ID, STANDARD_ID_SIZE))
        return ISO_OPEN_DAMAGED;
    const uint8_t *root = descriptor + PRIMARY_ROOT_RECORD;
    iso->root.sector = le32(root + RECORD_EXTENT);
    iso->root.size = le32(root + RECORD_DATA_LENGTH);
    return is_inside(iso, iso->root.sector, iso->root.size) ? ISO_OPEN_OK : ISO_OPEN_DAMAGED;
}

/* Setup */

IsoOpenResult iso_open(const char *path, Iso **iso)
{
    *iso = NULL;
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return ISO_OPEN_DAMAGED;
    Iso *image = calloc(1, sizeof *image);
    if (!image)
        os_fatal("out of memory");
    image->file = fp;
    int64_t image_size = -1;
    if (!fseek(fp, 0, SEEK_END))
        image_size = ftell(fp);
    IsoOpenResult result = ISO_OPEN_DAMAGED;
    if (image_size >= 0 && !fseek(fp, 0, SEEK_SET))
        result = read_layout(image, image_size);
    if (result != ISO_OPEN_OK)
        iso_close(image);
    else
        *iso = image;
    return result;
}

void iso_close(Iso *iso)
{
    if (!iso)
        return;
    fclose(iso->file);
    free(iso);
}

/* Queries */

/* Whether a record name ("GRAPHICS.DAT;1") is the path part, without case, version and a final dot. */
static bool is_name(const uint8_t *name, int name_length, const char *part, size_t part_length)
{
    const uint8_t *version = memchr(name, ';', (size_t)name_length);
    if (version)
        name_length = (int)(version - name);
    if (name_length > 0 && name[name_length - 1] == '.')
        name_length--;
    if ((size_t)name_length != part_length)
        return false;
    for (size_t i = 0; i < part_length; i++)
        if (toupper(name[i]) != toupper((unsigned char)part[i]))
            return false;
    return true;
}

/* Finds the record of a path part in a folder. */
static bool find_in_folder(const Iso *iso, const IsoFile *parent, PathPart part, IsoFile *found)
{
    uint8_t data[SECTOR_SIZE];
    for (uint32_t i = 0; i < sectors_of(parent->size); i++) {
        if (!read_sector(iso, parent->sector + i, data))
            return false;
        uint32_t left = parent->size - i * SECTOR_SIZE;
        int end = left < SECTOR_SIZE ? (int)left : SECTOR_SIZE;
        for (int at = 0; at + RECORD_NAME <= end && data[at];) {
            const uint8_t *record = data + at;
            int length = record[0];
            int name_length = record[RECORD_NAME_LENGTH];
            if (length < RECORD_NAME + name_length || at + length > end)
                return false; /* a damaged record */
            bool is_folder = record[RECORD_FLAGS_BYTE] & RECORD_FLAG_FOLDER;
            if (is_folder == part.folder && is_name(record + RECORD_NAME, name_length, part.name, part.length)) {
                found->sector = le32(record + RECORD_EXTENT);
                found->size = le32(record + RECORD_DATA_LENGTH);
                return is_inside(iso, found->sector, found->size);
            }
            at += length;
        }
    }
    return false;
}

bool iso_find(const Iso *iso, const char *path, IsoFile *file)
{
    IsoFile folder = iso->root;
    const char *part = path;
    for (int parts = 1; parts <= PATH_MAX_PARTS; parts++) {
        const char *slash = strchr(part, '/');
        if (!slash)
            return *part && find_in_folder(iso, &folder, (PathPart){part, strlen(part), false}, file);
        if (slash == part || parts == PATH_MAX_PARTS ||
            !find_in_folder(iso, &folder, (PathPart){part, (size_t)(slash - part), true}, &folder))
            return false;
        part = slash + 1;
    }
    return false;
}

bool iso_read(const Iso *iso, const IsoFile *file, uint8_t *data, uint32_t size)
{
    if (file->size != size || !is_inside(iso, file->sector, file->size))
        return false;
    uint8_t sector[SECTOR_SIZE];
    for (uint32_t i = 0; i < sectors_of(size); i++) {
        if (!read_sector(iso, file->sector + i, sector))
            return false;
        uint32_t left = size - i * SECTOR_SIZE;
        memcpy(data + (size_t)i * SECTOR_SIZE, sector, left < SECTOR_SIZE ? left : SECTOR_SIZE);
    }
    return true;
}
