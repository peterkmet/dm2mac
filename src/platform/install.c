/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "install.h"
#include <dirent.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include "os.h"
#include "source.h"

/* The mode of a new folder. */
#define FOLDER_MODE 0755

/* The deepest level of a folder source; a child of the working folder is level 1. */
#define SEARCH_DEPTH 3

/* The folder of the data files below the game folder. */
#define DATA_FOLDER "DATA"

/* The suffix of the temporary name of a game file. */
#define TEMPORARY_SUFFIX ".TMP"

/* The end of a message that does not fit. */
#define CUT_MARK "..."

/* A text in a buffer that ends with CUT_MARK when the next part does not fit. */
typedef struct {
    char *text;
    size_t size;
    size_t length;
    bool cut;
} Text;

/* The names of the entries of a folder, sorted with strcmp. */
typedef struct {
    char **names; /* owned, and each name owned */
    int count;
} NameList;

/* A file extension of a source form. */
typedef struct {
    SourceForm form;
    const char *extension;
} SourceExtension;

/* The search for a source and the copy from it. */
typedef struct {
    const char *folder; /* the working folder */
    const GameFile *files;
    uint8_t *buffer; /* owned: the size of the largest game file */
    Source *source;  /* owned: the source that passed, or NULL */
    char source_path[OS_PATH_SIZE];
    char failure_text[INSTALL_MESSAGE_SIZE]; /* the message lines of the failed sources */
    Text failures;
} Search;

/* The extensions of the source files in the working folder, without case. */
static const SourceExtension source_extensions[] = {
    {SOURCE_ZIP, ".zip"},
    {SOURCE_CD,  ".iso"},
    {SOURCE_CD,  ".img"},
    {SOURCE_CD,  ".bin"},
};

/* Appends a formatted part; a part that does not fit cuts the text and ends it with CUT_MARK. */
static void append_text(Text *text, const char *format, ...)
{
    if (text->cut || text->size == 0)
        return;
    va_list args;
    va_start(args, format);
    int written = vsnprintf(text->text + text->length, text->size - text->length, format, args);
    va_end(args);
    if (written >= 0 && (size_t)written < text->size - text->length) {
        text->length += (size_t)written;
        return;
    }
    text->cut = true;
    size_t mark = strlen(CUT_MARK);
    if (text->size > mark)
        memcpy(text->text + text->size - 1 - mark, CUT_MARK, mark + 1);
    text->length = strlen(text->text);
}

/* Writes the full path of path into full, or path itself when it has none. */
static void get_full_path(const char *path, char *full, size_t size)
{
    if (!os_get_full_path(path, full, size))
        snprintf(full, size, "%s", path);
}

/* Setup */

bool install_make_folder(const char *path)
{
    struct stat info;
    if (stat(path, &info) == 0)
        return S_ISDIR(info.st_mode);
#ifdef _WIN32
    return _mkdir(path) == 0;
#else
    return mkdir(path, FOLDER_MODE) == 0;
#endif
}

/* Commands */

static int compare_names(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Reads the names of the entries of the folder at path, without "." and "..", in strcmp order. */
static void list_folder(const char *path, NameList *list)
{
    list->names = NULL;
    list->count = 0;
    DIR *folder = opendir(path);
    if (!folder)
        return;
    struct dirent *entry;
    while ((entry = readdir(folder))) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char **names = realloc(list->names, (size_t)(list->count + 1) * sizeof *names);
        size_t length = strlen(entry->d_name) + 1;
        char *name = malloc(length);
        if (!names || !name)
            os_fatal("out of memory");
        memcpy(name, entry->d_name, length);
        names[list->count] = name;
        list->names = names;
        list->count++;
    }
    closedir(folder);
    if (list->count > 0)
        qsort(list->names, (size_t)list->count, sizeof *list->names, compare_names);
}

static void free_names(NameList *list)
{
    for (int i = 0; i < list->count; i++)
        free(list->names[i]);
    free(list->names);
}

static bool is_folder(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

/* Whether the file name has an extension of the source form, without case. */
static bool is_source_file(const char *name, SourceForm form)
{
    size_t length = strlen(name);
    for (size_t i = 0; i < sizeof source_extensions / sizeof source_extensions[0]; i++) {
        size_t extension = strlen(source_extensions[i].extension);
        if (source_extensions[i].form == form && length > extension &&
            strcasecmp(name + length - extension, source_extensions[i].extension) == 0)
            return true;
    }
    return false;
}

/* Logs a failed source and adds its line to the message. */
static void report_failure(Search *search, const char *path, const char *reason)
{
    os_log("%s: %s", path, reason);
    append_text(&search->failures, "\n%s: %s.", path, reason);
}

/* Opens the source and checks all game files in it; the source that passes stays open in the search. */
static bool check_source(Search *search, SourceForm form, const char *path)
{
    const char *problem = "";
    Source *source = source_open(form, path, &problem);
    if (!source) {
        report_failure(search, path, problem);
        return false;
    }
    for (int i = 0; i < GAMEFILE_COUNT; i++) {
        const GameFile *file = &search->files[i];
        if (!source_read(source, file->name, search->buffer, file->size) ||
            !gamefile_is_valid(file, search->buffer, file->size)) {
            char reason[OS_PATH_SIZE];
            snprintf(reason, sizeof reason, "%s is missing or wrong", file->name);
            report_failure(search, path, reason);
            source_close(source);
            return false;
        }
    }
    search->source = source;
    snprintf(search->source_path, sizeof search->source_path, "%s", path);
    return true;
}

/* Checks each subfolder of path in name order as a source, then its subfolders, down to SEARCH_DEPTH. */
static bool search_folders(Search *search, const char *path, int level)
{
    NameList list;
    list_folder(path, &list);
    bool found = false;
    for (int i = 0; i < list.count && !found; i++) {
        char child[OS_PATH_SIZE];
        if (level == 1 && strcasecmp(list.names[i], INSTALL_GAME_FOLDER) == 0)
            continue;
        if (snprintf(child, sizeof child, "%s/%s", path, list.names[i]) >= (int)sizeof child || !is_folder(child))
            continue;
        found = check_source(search, SOURCE_FOLDER, child) ||
                (level < SEARCH_DEPTH && search_folders(search, child, level + 1));
    }
    free_names(&list);
    return found;
}

/* Checks each file of the working folder with an extension of the form as a source, in name order. */
static bool search_files(Search *search, SourceForm form)
{
    NameList list;
    list_folder(search->folder, &list);
    bool found = false;
    for (int i = 0; i < list.count && !found; i++) {
        char path[OS_PATH_SIZE];
        if (!is_source_file(list.names[i], form) ||
            snprintf(path, sizeof path, "%s/%s", search->folder, list.names[i]) >= (int)sizeof path || is_folder(path))
            continue;
        found = check_source(search, form, path);
    }
    free_names(&list);
    return found;
}

/* Finds the first source that passes: the folders, then the ZIP archives, then the CD images. */
static bool find_source(Search *search)
{
    return search_folders(search, search->folder, 1) || search_files(search, SOURCE_ZIP) ||
           search_files(search, SOURCE_CD);
}

/* Deletes the temporary names of the game files that an earlier install left in the game folder. */
static void remove_temporary_files(const GameFile *files, const char *game)
{
    for (int i = 0; i < GAMEFILE_COUNT; i++) {
        char path[OS_PATH_SIZE];
        if (snprintf(path, sizeof path, "%s/%s%s", game, files[i].name, TEMPORARY_SUFFIX) < (int)sizeof path)
            remove(path);
    }
}

/* Copies one game file from the source to its temporary name, checks it, and renames it to the game file. */
static bool copy_file(const Search *search, const GameFile *file, const char *game)
{
    char temporary_name[OS_PATH_SIZE];
    char temporary[OS_PATH_SIZE];
    char target[OS_PATH_SIZE];
    if (snprintf(temporary_name, sizeof temporary_name, "%s%s", file->name, TEMPORARY_SUFFIX) >=
            (int)sizeof temporary_name ||
        snprintf(temporary, sizeof temporary, "%s/%s", game, temporary_name) >= (int)sizeof temporary ||
        snprintf(target, sizeof target, "%s/%s", game, file->name) >= (int)sizeof target ||
        !source_read(search->source, file->name, search->buffer, file->size))
        return false;
    FILE *out = fopen(temporary, "wb");
    if (!out)
        return false;
    bool ok = fwrite(search->buffer, 1, file->size, out) == file->size;
    ok = fclose(out) == 0 && ok;
    const GameFile written = {temporary_name, file->size, file->crc};
    ok = ok && gamefile_is_valid_in(&written, game);
    if (ok) {
        remove(target); /* Windows does not replace a file by a rename */
        ok = rename(temporary, target) == 0;
    }
    if (!ok)
        remove(temporary);
    return ok;
}

/* Copies each game file that is missing or fails in the game folder from the source. */
static bool copy_files(const Search *search, const char *game)
{
    os_log("installing the game files from %s", search->source_path);
    int count = 0;
    for (int i = 0; i < GAMEFILE_COUNT; i++) {
        if (gamefile_is_valid_in(&search->files[i], game))
            continue;
        if (!copy_file(search, &search->files[i], game))
            return false;
        count++;
    }
    os_log("installed %d game files", count);
    return true;
}

/* The first game file that the game folder has and that fails; NULL when there is none. */
static const GameFile *find_wrong_file(const GameFile *files, const char *game)
{
    for (int i = 0; i < GAMEFILE_COUNT; i++) {
        char path[OS_PATH_SIZE];
        if (gamefile_is_valid_in(&files[i], game) ||
            snprintf(path, sizeof path, "%s/%s", game, files[i].name) >= (int)sizeof path)
            continue;
        FILE *file = fopen(path, "rb");
        if (file) {
            fclose(file);
            return &files[i];
        }
    }
    return NULL;
}

/* Writes the message of a search without a source: the forms of a source, the working folder, and the failures. */
static void write_missing_message(const Search *search, const char *game, char *message, size_t size)
{
    char full[OS_PATH_SIZE];
    get_full_path(search->folder, full, sizeof full);
    Text text = {message, size, 0, false};
    if (size > 0)
        message[0] = '\0';
    append_text(&text, "The game files of Dungeon Master II are missing.\n");
    append_text(&text, "You need your own copy of the PC DOS version of Dungeon Master II (English, 1995).\n");
    append_text(&text, "Copy its folder, its ZIP archive (.zip), or its CD image (.iso, .img, .bin) into %s.", full);
    const GameFile *wrong = find_wrong_file(search->files, game);
    if (wrong)
        append_text(&text, "\n%s/%s is missing or wrong.", INSTALL_GAME_FOLDER, wrong->name);
    append_text(&text, "%s", search->failure_text);
}

/* Writes the message of a failed write into the game folder. */
static void write_failed_message(const char *game, char *message, size_t size)
{
    char full[OS_PATH_SIZE];
    get_full_path(game, full, sizeof full);
    Text text = {message, size, 0, false};
    if (size > 0)
        message[0] = '\0';
    append_text(&text, "cannot write the game files into %s", full);
}

/* Searches a source below search->folder and copies the failing game files from it. */
static InstallResult search_and_copy(Search *search, const char *game)
{
    uint32_t largest = 0;
    for (int i = 0; i < GAMEFILE_COUNT; i++)
        if (search->files[i].size > largest)
            largest = search->files[i].size;
    search->buffer = malloc(largest + 1u);
    if (!search->buffer)
        os_fatal("out of memory");
    InstallResult result = INSTALL_NO_SOURCE;
    if (find_source(search))
        result = copy_files(search, game) ? INSTALL_DONE : INSTALL_WRITE_FAILED;
    source_close(search->source);
    free(search->buffer);
    return result;
}

InstallResult install_game_files(const char *folder, const GameFile *files, char *message, size_t size)
{
    char game[OS_PATH_SIZE];
    char data[OS_PATH_SIZE];
    if (snprintf(game, sizeof game, "%s/%s", folder, INSTALL_GAME_FOLDER) >= (int)sizeof game ||
        snprintf(data, sizeof data, "%s/%s", game, DATA_FOLDER) >= (int)sizeof data) {
        write_failed_message(folder, message, size);
        return INSTALL_WRITE_FAILED;
    }
    if (!gamefile_find_bad(files, game))
        return INSTALL_READY;
    if (!install_make_folder(game) || !install_make_folder(data)) {
        write_failed_message(game, message, size);
        return INSTALL_WRITE_FAILED;
    }
    remove_temporary_files(files, game);
    Search search = {.folder = folder, .files = files};
    search.failures = (Text){search.failure_text, sizeof search.failure_text, 0, false};
    InstallResult result = search_and_copy(&search, game);
    if (result == INSTALL_NO_SOURCE)
        write_missing_message(&search, game, message, size);
    else if (result == INSTALL_WRITE_FAILED)
        write_failed_message(game, message, size);
    return result;
}
