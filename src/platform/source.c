/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "source.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "gamefile.h"
#include "iso.h"
#include "os.h"
#include "zip.h"

struct Source {
    SourceForm form;
    char *folder;                /* SOURCE_FOLDER: owned copy of the path */
    Zip *zip;                    /* SOURCE_ZIP: owned */
    char zip_root[OS_PATH_SIZE]; /* SOURCE_ZIP: "" or "<folder>/" */
    Iso *iso;                    /* SOURCE_CD: owned */
};

/* Setup */

/* Opens a ZIP archive and finds the root of the game files in it. */
static bool open_zip(Source *source, const char *path, const char **problem)
{
    source->zip = zip_open(path);
    if (!source->zip) {
        *problem = "not a ZIP archive or damaged";
        return false;
    }
    if (!zip_find_root(source->zip, GAMEFILE_MARKER, source->zip_root, sizeof source->zip_root)) {
        *problem = "no " GAMEFILE_MARKER " in the archive";
        return false;
    }
    return true;
}

/* Opens a CD image. */
static bool open_cd(Source *source, const char *path, const char **problem)
{
    switch (iso_open(path, &source->iso)) {
    case ISO_OPEN_OK:
        return true;
    case ISO_OPEN_MODE2:
        *problem = "a MODE2 CD image: the port reads 2048-byte and MODE1 sectors only";
        return false;
    case ISO_OPEN_DAMAGED:
        break;
    }
    *problem = "not a CD image or damaged";
    return false;
}

/* Copies the folder path. */
static bool open_folder(Source *source, const char *path)
{
    source->folder = malloc(strlen(path) + 1);
    if (!source->folder)
        os_fatal("out of memory");
    strcpy(source->folder, path);
    return true;
}

Source *source_open(SourceForm form, const char *path, const char **problem)
{
    Source *source = calloc(1, sizeof *source);
    if (!source)
        os_fatal("out of memory");
    source->form = form;
    bool ok = false;
    switch (form) {
    case SOURCE_FOLDER:
        ok = open_folder(source, path);
        break;
    case SOURCE_ZIP:
        ok = open_zip(source, path, problem);
        break;
    case SOURCE_CD:
        ok = open_cd(source, path, problem);
        break;
    }
    if (ok)
        return source;
    source_close(source);
    return NULL;
}

void source_close(Source *source)
{
    if (!source)
        return;
    switch (source->form) {
    case SOURCE_FOLDER:
        free(source->folder);
        break;
    case SOURCE_ZIP:
        if (source->zip)
            zip_close(source->zip);
        break;
    case SOURCE_CD:
        if (source->iso)
            iso_close(source->iso);
        break;
    }
    free(source);
}

/* Queries */

/* Appends "/<entry>" to path for the entry of the folder at path whose name is part without case. */
static bool find_in_folder(char *path, size_t size, const char *part)
{
    DIR *folder = opendir(path);
    if (!folder)
        return false;
    size_t length = strlen(path);
    bool found = false;
    struct dirent *entry;
    while (!found && (entry = readdir(folder)))
        if (strcasecmp(entry->d_name, part) == 0)
            found = snprintf(path + length, size - length, "/%s", entry->d_name) < (int)(size - length);
    closedir(folder);
    return found;
}

/* Reads exactly size bytes from the file at path; a longer file fails. */
static bool read_file(const char *path, uint8_t *data, uint32_t size)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return false;
    bool ok = fread(data, 1, size, file) == size && fgetc(file) == EOF;
    fclose(file);
    return ok;
}

/* Writes the path of the file at name below folder into path; each part of name is found without case. */
static bool find_folder_path(const char *folder, const char *name, char *path, size_t size)
{
    char part[OS_PATH_SIZE];
    if (snprintf(path, size, "%s", folder) >= (int)size)
        return false;
    const char *at = name;
    for (;;) {
        const char *end = strchr(at, '/');
        size_t length = end ? (size_t)(end - at) : strlen(at);
        if (length >= sizeof part)
            return false;
        memcpy(part, at, length);
        part[length] = '\0';
        if (!find_in_folder(path, size, part))
            return false;
        if (!end)
            return true;
        at = end + 1;
    }
}

/* Writes the entry name <root><name> of the archive into entry_name. */
static bool make_entry_name(const Source *source, const char *name, char *entry_name, size_t size)
{
    return snprintf(entry_name, size, "%s%s", source->zip_root, name) < (int)size;
}

bool source_read(const Source *source, const char *name, uint8_t *data, uint32_t size)
{
    char path[OS_PATH_SIZE];
    ZipEntry entry;
    IsoFile file;
    switch (source->form) {
    case SOURCE_FOLDER:
        return find_folder_path(source->folder, name, path, sizeof path) && read_file(path, data, size);
    case SOURCE_ZIP:
        return make_entry_name(source, name, path, sizeof path) && zip_find(source->zip, path, &entry) &&
               zip_read(source->zip, &entry, data, size);
    case SOURCE_CD:
        return iso_find(source->iso, name, &file) && iso_read(source->iso, &file, data, size);
    }
    return false;
}

bool source_has_file(const Source *source, const char *name)
{
    char path[OS_PATH_SIZE];
    ZipEntry entry;
    IsoFile file;
    switch (source->form) {
    case SOURCE_FOLDER:
        return find_folder_path(source->folder, name, path, sizeof path);
    case SOURCE_ZIP:
        return make_entry_name(source, name, path, sizeof path) && zip_find(source->zip, path, &entry);
    case SOURCE_CD:
        return iso_find(source->iso, name, &file);
    }
    return false;
}
